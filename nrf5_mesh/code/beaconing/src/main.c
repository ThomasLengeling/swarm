/* Copyright (c) 2010 - 2020, Nordic Semiconductor ASA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "ble.h"
#include "boards.h"
#include "bsp.h"
#include "simple_hal.h"
#include "nrf_mesh.h"
#include "log.h"
#include "advertiser.h"
#include "mesh_app_utils.h"
#include "mesh_stack.h"
#include "ble_softdevice_support.h"
#include "mesh_provisionee.h"
#include "nrf_mesh_config_examples.h"
#include "app_timer.h"
#include "example_common.h"
#include "nrf_mesh_configure.h"
#include "ad_type_filter.h"
#include "buttons.h"
#include "leds.h"
#include "neopixel.h"
#include "neopixel_SPI.h"
#include "swarm_state.h"

#if defined(NRF51) && defined(NRF_MESH_STACK_DEPTH)
#include "stack_depth.h"
#endif

/*****************************************************************************
 * Definitions
 *****************************************************************************/
#define ADVERTISER_BUFFER_SIZE (64)

/*****************************************************************************
 * Forward declaration of static functions
 *****************************************************************************/

/*****************************************************************************
 * Static variables
 *****************************************************************************/
/** Single advertiser instance. May periodically transmit one packet at a time. */
static advertiser_t m_advertiser;

static uint8_t m_adv_buffer[ADVERTISER_BUFFER_SIZE];
static bool m_device_provisioned;
static unsigned long lastPackSend;

advertiser_tx_complete_cb_t tx_complete_cb;

static void rx_cb(const nrf_mesh_adv_packet_rx_data_t *p_rx_data)
{
    LEDS_OFF(BSP_LED_0_MASK); /* @c LED_RGB_RED_MASK on pca10031 */
    char msg[128];

    // sprintf(msg, "%02x", p_rx_data->p_payload);
    uint8_t *word = "SWARM";
    //printf("Rx_cb: %s\n", p_rx_data->p_payload);
    if (strstr(p_rx_data->p_payload, word) != NULL)
    {
        // __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, " ---> Target Pack RCV\n");
        char payload_hex[p_rx_data->length];
        for (int i = 0 ; i != p_rx_data->length ; i++) {
            sprintf(&payload_hex[2*i], "%02X", p_rx_data->p_payload[i]);
        }
        // payload_hex[p_rx_data->length] = '\0';
        if (false)
        {
            (void)sprintf(msg, "RX [@%u]: RSSI: %3d ADV TYPE: %x ADDR: [%02x:%02x:%02x:%02x:%02x:%02x] %d bytes: %s\n",
                          p_rx_data->p_metadata->params.scanner.timestamp,
                          p_rx_data->p_metadata->params.scanner.rssi,
                          p_rx_data->adv_type,
                          p_rx_data->p_metadata->params.scanner.adv_addr.addr[0],
                          p_rx_data->p_metadata->params.scanner.adv_addr.addr[1],
                          p_rx_data->p_metadata->params.scanner.adv_addr.addr[2],
                          p_rx_data->p_metadata->params.scanner.adv_addr.addr[3],
                          p_rx_data->p_metadata->params.scanner.adv_addr.addr[4],
                          p_rx_data->p_metadata->params.scanner.adv_addr.addr[5],
                          p_rx_data->length,
                          payload_hex);
            __LOG_XB(LOG_SRC_APP, LOG_LEVEL_INFO, msg, p_rx_data->p_payload, p_rx_data->length);
        }

        unsigned long rxTimeAlive = 0;
        rxTimeAlive = ((p_rx_data->p_payload[7 + 0] << 8) + (p_rx_data->p_payload[7 + 1]));

        uint8_t compressed_phase = (p_rx_data->p_payload[7 + 2]);
        int remote_phase = ((float)compressed_phase / 0xFF) * PHASE_DURATION;

        uint8_t remote_pattern = p_rx_data->p_payload[7 + 3];

        uint8_t compressed_color = p_rx_data->p_payload[7 + 4];
        btn_color_t remote_color = {
            (compressed_color >> 5) * (255 / 7),
            ((compressed_color >> 2) & 0x07) * (255 / 7),
            (compressed_color & 0x03) * (255 / 3)};

        unsigned long timealive = timealive_duration();
        if (rxTimeAlive > timealive +3)
        {
            sprintf(msg, " ---> Older node found: %d > me: %d, phase: %d, pattern: %d, color: %x %x %x\n", rxTimeAlive, timealive, remote_phase, remote_pattern, remote_color.r, remote_color.g, remote_color.b);
            __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, msg);
            
            set_updated_timealive(rxTimeAlive);
            touch_lastsync();

            set_phase(remote_phase);
            set_button_pattern(remote_pattern);
            set_next_color(remote_color);
            bsp_board_led_invert(1);
        }
        else
        {
            touch_lastsync();
            int phase = current_phase();
            // __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, " ---> I'm older, ignoring: %d (me: %d, phase: %d)\n", rxTimeAlive, timealive, phase);
        }
    }

    //  LEDS_ON(BSP_LED_0_MASK);  /* @c LED_RGB_RED_MASK on pca10031 */
}

static void adv_init(void)
{
    advertiser_instance_init(&m_advertiser, NULL, m_adv_buffer, ADVERTISER_BUFFER_SIZE);
}

static void pack_send()
{
    unsigned long timealive = timealive_duration();
    if (compareMillis(lastPackSend, millis()) <= 200)
    {
        // __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, " ---> Packet sent too soon, cooling off... %ld (me: %d)\n", lastPackSend, timealive);
        return;
    }
    // __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, " ---> Sending packet... %ld (me: %d)\n", lastPackSend, timealive);
    lastPackSend = millis();

    /* Let scanner accept Complete Local Name AD Type. */
    bearer_adtype_add(BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME);
    advertiser_enable(&m_advertiser);
    advertiser_flush(&m_advertiser);

    int phase = current_phase();
    uint8_t compressed_phase = (((float)phase / PHASE_DURATION) * 0xFF);

    uint8_t pattern = btn_current_pattern();

    btn_color_t color = btn_next_color();
    uint8_t compressed_color = (((color.r * 7 / 255) << 5) +
                                ((color.g * 7 / 255) << 2) +
                                (color.b * 3 / 255));

    //  advertiser_enable(&m_advertiser);
    uint8_t adv_data[] =
        {
            0x06 + 0x05,                         /* AD data length (including type, but not itself) 6 bytes plus size of the data */
            BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME, /* AD data type (Complete local name) */
            'S',                                 /* AD data payload (Name of device) */
            'W',
            'A',
            'R',
            'M',
            // TODO: Add a parity bit
            (uint8_t)((timealive >> 8) & 0xFF),
            (uint8_t)((timealive & 0xFF)), // TODO: Bump timealive younger when not in sync and joining a swarm
            (uint8_t)compressed_phase,
            (uint8_t)pattern,
            (uint8_t)compressed_color};

    /* Allocate packet */
    adv_packet_t *p_packet = advertiser_packet_alloc(&m_advertiser, sizeof(adv_data));
    if (p_packet)
    {
        /* Construct packet contents */
        memcpy(p_packet->packet.payload, adv_data, sizeof(adv_data));
        /* Repeat forever */
        p_packet->config.repeats = 0x01;

        advertiser_packet_send(&m_advertiser, p_packet);
        //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, " ---> PHASE AT ZERO packet sent  \n");
    }
    //   __NOP();
    //    advertiser_disable(&m_advertiser);
}

static void node_reset(void)
{
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, " ---> ----- Node reset -----\n");
    hal_led_blink_ms(LEDS_MASK, LED_BLINK_INTERVAL_MS, LED_BLINK_CNT_RESET);
    /* This function may return if there are ongoing flash operations. */
    mesh_stack_device_reset();
}

static void config_server_evt_cb(const config_server_evt_t *p_evt)
{
    if (p_evt->type == CONFIG_SERVER_EVT_NODE_RESET)
    {
        node_reset();
    }
}

static void device_identification_start_cb(uint8_t attention_duration_s)
{
    hal_led_mask_set(LEDS_MASK, false);
    hal_led_blink_ms(BSP_LED_2_MASK | BSP_LED_3_MASK,
                     LED_BLINK_ATTENTION_INTERVAL_MS,
                     LED_BLINK_ATTENTION_COUNT(attention_duration_s));
}

static void provisioning_aborted_cb(void)
{
    hal_led_blink_stop();
}

static void unicast_address_print(void)
{
    dsm_local_unicast_address_t node_address;
    dsm_local_unicast_addresses_get(&node_address);
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, " ---> Node Address: 0x%04x \n", node_address.address_start);
}

static void provisioning_complete_cb(void)
{
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, " ---> Successfully provisioned\n");

    unicast_address_print();
    hal_led_blink_stop();
    hal_led_mask_set(LEDS_MASK, LED_MASK_STATE_OFF);
    hal_led_blink_ms(LEDS_MASK, LED_BLINK_INTERVAL_MS, LED_BLINK_CNT_PROV);
}

static void mesh_init(void)
{
    mesh_stack_init_params_t init_params =
        {
            .core.irq_priority = NRF_MESH_IRQ_PRIORITY_THREAD,
            .core.lfclksrc = DEV_BOARD_LF_CLK_CFG,
            .models.config_server_cb = config_server_evt_cb};

    uint32_t status = mesh_stack_init(&init_params, &m_device_provisioned);
    switch (status)
    {
    case NRF_ERROR_INVALID_DATA:
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, " ***> Data in the persistent memory was corrupted. Device starts as unprovisioned.\n");
        __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, " ***> Reset device before starting of the provisioning process.\n");
        break;
    case NRF_SUCCESS:
        break;
    default:
        ERROR_CHECK(status);
    }

    /* Start listening for incoming packets */
    nrf_mesh_rx_cb_set(rx_cb);

    /* Initialize the advertiser */
    adv_init();
}

static void initialize(void)
{
#if defined(NRF51) && defined(NRF_MESH_STACK_DEPTH)
    stack_depth_paint_stack();
#endif

    ERROR_CHECK(app_timer_init());
    hal_leds_init();

    __LOG_INIT(LOG_SRC_APP, LOG_LEVEL_INFO, log_callback_rtt);
    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, " ---> ----- Swarm Bluetooth Mesh Beacon Initializing... -----\n");

    ble_stack_init();

    mesh_init();

    bsp_configuration();

    lastPackSend = 0;

    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, " ---> Initialization complete.\n");
}

static void start(void)
{
    if (!m_device_provisioned)
    {
        static const uint8_t static_auth_data[NRF_MESH_KEY_SIZE] = STATIC_AUTH_DATA;
        mesh_provisionee_start_params_t prov_start_params =
            {
                .p_static_data = static_auth_data,
                .prov_complete_cb = provisioning_complete_cb,
                .prov_device_identification_start_cb = device_identification_start_cb,
                .prov_device_identification_stop_cb = NULL,
                .prov_abort_cb = provisioning_aborted_cb,
                .p_device_uri = EX_URI_BEACON};
        ERROR_CHECK(mesh_provisionee_prov_start(&prov_start_params));
    }
    else
    {
        unicast_address_print();
    }

    /* Start advertising own beacon */
    /* Note: If application wants to start beacons at later time, adv_start() API must be called
     * from the same IRQ priority context same as that of the Mesh Stack. */

    pack_send();

    mesh_app_uuid_print(nrf_mesh_configure_device_uuid_get());

    ERROR_CHECK(mesh_stack_start());

    __LOG(LOG_SRC_APP, LOG_LEVEL_INFO, " ---> Swarm Bluetooth Mesh Beacon started.\n");

    //   hal_led_mask_set(LEDS_MASK, LED_MASK_STATE_OFF);
    //  hal_led_blink_ms(LEDS_MASK, LED_BLINK_INTERVAL_MS, LED_BLINK_CNT_START);
}

int main(void)
{
    initialize();
    start();

    // ERROR_CHECK(app_timer_init());

    pwm_init();
    phasetimer_init();
    swarm_state_init();

    for (;;)
    {

        ledloop();

        //__LOG(LOG_SRC_APP, LOG_LEVEL_INFO, " ---> PHASE AT ZERO  \n");
        pack_send();

        //(void)sd_app_evt_wait();
        bool done = nrf_mesh_process();
        if (done)
        {
            sd_app_evt_wait();
        }
    }
}

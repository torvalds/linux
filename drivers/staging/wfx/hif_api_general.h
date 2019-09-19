/* SPDX-License-Identifier: Apache-2.0 */
/*
 * WFx hardware interface definitions
 *
 * Copyright (c) 2018-2019, Silicon Laboratories Inc.
 */

#ifndef WFX_HIF_API_GENERAL_H
#define WFX_HIF_API_GENERAL_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/if_ether.h>
#else
#include <net/ethernet.h>
#include <stdint.h>
#define __packed __attribute__((__packed__))
#endif

#define API_SSID_SIZE                       32

#define HIF_ID_IS_INDICATION               0x80
#define HIF_COUNTER_MAX                    7

struct hif_msg {
	uint16_t    len;
	uint8_t     id;
	uint8_t     reserved:1;
	uint8_t     interface:2;
	uint8_t     seqnum:3;
	uint8_t     encrypted:2;
	uint8_t     body[];
} __packed;

enum hif_general_requests_ids {
	HIF_REQ_ID_CONFIGURATION                         = 0x09,
	HIF_REQ_ID_CONTROL_GPIO                          = 0x26,
	HIF_REQ_ID_SET_SL_MAC_KEY                        = 0x27,
	HIF_REQ_ID_SL_EXCHANGE_PUB_KEYS                  = 0x28,
	HIF_REQ_ID_SL_CONFIGURE                          = 0x29,
	HIF_REQ_ID_PREVENT_ROLLBACK                      = 0x2a,
	HIF_REQ_ID_PTA_SETTINGS                          = 0x2b,
	HIF_REQ_ID_PTA_PRIORITY                          = 0x2c,
	HIF_REQ_ID_PTA_STATE                             = 0x2d,
	HIF_REQ_ID_SHUT_DOWN                             = 0x32,
};

enum hif_general_confirmations_ids {
	HIF_CNF_ID_CONFIGURATION                         = 0x09,
	HIF_CNF_ID_CONTROL_GPIO                          = 0x26,
	HIF_CNF_ID_SET_SL_MAC_KEY                        = 0x27,
	HIF_CNF_ID_SL_EXCHANGE_PUB_KEYS                  = 0x28,
	HIF_CNF_ID_SL_CONFIGURE                          = 0x29,
	HIF_CNF_ID_PREVENT_ROLLBACK                      = 0x2a,
	HIF_CNF_ID_PTA_SETTINGS                          = 0x2b,
	HIF_CNF_ID_PTA_PRIORITY                          = 0x2c,
	HIF_CNF_ID_PTA_STATE                             = 0x2d,
	HIF_CNF_ID_SHUT_DOWN                             = 0x32,
};

enum hif_general_indications_ids {
	HIF_IND_ID_EXCEPTION                             = 0xe0,
	HIF_IND_ID_STARTUP                               = 0xe1,
	HIF_IND_ID_WAKEUP                                = 0xe2,
	HIF_IND_ID_GENERIC                               = 0xe3,
	HIF_IND_ID_ERROR                                 = 0xe4,
	HIF_IND_ID_SL_EXCHANGE_PUB_KEYS                  = 0xe5
};

enum hif_hi_status {
	HI_STATUS_SUCCESS                             = 0x0000,
	HI_STATUS_FAILURE                             = 0x0001,
	HI_INVALID_PARAMETER                          = 0x0002,
	HI_STATUS_GPIO_WARNING                        = 0x0003,
	HI_ERROR_UNSUPPORTED_MSG_ID                   = 0x0004,
	SL_MAC_KEY_STATUS_SUCCESS                     = 0x005A,
	SL_MAC_KEY_STATUS_FAILED_KEY_ALREADY_BURNED   = 0x006B,
	SL_MAC_KEY_STATUS_FAILED_RAM_MODE_NOT_ALLOWED = 0x007C,
	SL_MAC_KEY_STATUS_FAILED_UNKNOWN_MODE         = 0x008D,
	SL_PUB_KEY_EXCHANGE_STATUS_SUCCESS            = 0x009E,
	SL_PUB_KEY_EXCHANGE_STATUS_FAILED             = 0x00AF,
	PREVENT_ROLLBACK_CNF_SUCCESS                  = 0x1234,
	PREVENT_ROLLBACK_CNF_WRONG_MAGIC_WORD         = 0x1256
};

enum hif_api_rate_index {
	API_RATE_INDEX_B_1MBPS                   = 0,
	API_RATE_INDEX_B_2MBPS                   = 1,
	API_RATE_INDEX_B_5P5MBPS                 = 2,
	API_RATE_INDEX_B_11MBPS                  = 3,
	API_RATE_INDEX_PBCC_22MBPS               = 4,
	API_RATE_INDEX_PBCC_33MBPS               = 5,
	API_RATE_INDEX_G_6MBPS                   = 6,
	API_RATE_INDEX_G_9MBPS                   = 7,
	API_RATE_INDEX_G_12MBPS                  = 8,
	API_RATE_INDEX_G_18MBPS                  = 9,
	API_RATE_INDEX_G_24MBPS                  = 10,
	API_RATE_INDEX_G_36MBPS                  = 11,
	API_RATE_INDEX_G_48MBPS                  = 12,
	API_RATE_INDEX_G_54MBPS                  = 13,
	API_RATE_INDEX_N_6P5MBPS                 = 14,
	API_RATE_INDEX_N_13MBPS                  = 15,
	API_RATE_INDEX_N_19P5MBPS                = 16,
	API_RATE_INDEX_N_26MBPS                  = 17,
	API_RATE_INDEX_N_39MBPS                  = 18,
	API_RATE_INDEX_N_52MBPS                  = 19,
	API_RATE_INDEX_N_58P5MBPS                = 20,
	API_RATE_INDEX_N_65MBPS                  = 21,
	API_RATE_NUM_ENTRIES                     = 22
};


enum hif_fw_type {
	HIF_FW_TYPE_ETF                             = 0x0,
	HIF_FW_TYPE_WFM                             = 0x1,
	HIF_FW_TYPE_WSM                             = 0x2
};

struct hif_capabilities {
	uint8_t    link_mode:2;
	uint8_t    reserved1:6;
	uint8_t    reserved2;
	uint8_t    reserved3;
	uint8_t    reserved4;
} __packed;

struct hif_otp_regul_sel_mode_info {
	uint8_t    region_sel_mode:4;
	uint8_t    reserved:4;
} __packed;

struct hif_otp_phy_info {
	uint8_t    phy1_region:3;
	uint8_t    phy0_region:3;
	uint8_t    otp_phy_ver:2;
} __packed;

#define API_OPN_SIZE                                    14
#define API_UID_SIZE                                    8
#define API_DISABLED_CHANNEL_LIST_SIZE                  2
#define API_FIRMWARE_LABEL_SIZE                         128

struct hif_ind_startup {
	uint32_t   status;
	uint16_t   hardware_id;
	uint8_t    opn[API_OPN_SIZE];
	uint8_t    uid[API_UID_SIZE];
	uint16_t   num_inp_ch_bufs;
	uint16_t   size_inp_ch_buf;
	uint8_t    num_links_ap;
	uint8_t    num_interfaces;
	uint8_t    mac_addr[2][ETH_ALEN];
	uint8_t    api_version_minor;
	uint8_t    api_version_major;
	struct hif_capabilities capabilities;
	uint8_t    firmware_build;
	uint8_t    firmware_minor;
	uint8_t    firmware_major;
	uint8_t    firmware_type;
	uint8_t    disabled_channel_list[API_DISABLED_CHANNEL_LIST_SIZE];
	struct hif_otp_regul_sel_mode_info regul_sel_mode_info;
	struct hif_otp_phy_info otp_phy_info;
	uint32_t   supported_rate_mask;
	uint8_t    firmware_label[API_FIRMWARE_LABEL_SIZE];
} __packed;

struct hif_ind_wakeup {
} __packed;

struct hif_req_configuration {
	uint16_t   length;
	uint8_t    pds_data[];
} __packed;

struct hif_cnf_configuration {
	uint32_t   status;
} __packed;

enum hif_gpio_mode {
	HIF_GPIO_MODE_D0                            = 0x0,
	HIF_GPIO_MODE_D1                            = 0x1,
	HIF_GPIO_MODE_OD0                           = 0x2,
	HIF_GPIO_MODE_OD1                           = 0x3,
	HIF_GPIO_MODE_TRISTATE                      = 0x4,
	HIF_GPIO_MODE_TOGGLE                        = 0x5,
	HIF_GPIO_MODE_READ                          = 0x6
};

struct hif_req_control_gpio {
	uint8_t gpio_label;
	uint8_t gpio_mode;
} __packed;

enum hif_gpio_error {
	HIF_GPIO_ERROR_0                            = 0x0,
	HIF_GPIO_ERROR_1                            = 0x1,
	HIF_GPIO_ERROR_2                            = 0x2
};

struct hif_cnf_control_gpio {
	uint32_t status;
	uint32_t value;
} __packed;

enum hif_generic_indication_type {
	HIF_GENERIC_INDICATION_TYPE_RAW               = 0x0,
	HIF_GENERIC_INDICATION_TYPE_STRING            = 0x1,
	HIF_GENERIC_INDICATION_TYPE_RX_STATS          = 0x2
};

struct hif_rx_stats {
	uint32_t   nb_rx_frame;
	uint32_t   nb_crc_frame;
	uint32_t   per_total;
	uint32_t   throughput;
	uint32_t   nb_rx_by_rate[API_RATE_NUM_ENTRIES];
	uint16_t   per[API_RATE_NUM_ENTRIES];
	int16_t    snr[API_RATE_NUM_ENTRIES];
	int16_t    rssi[API_RATE_NUM_ENTRIES];
	int16_t    cfo[API_RATE_NUM_ENTRIES];
	uint32_t   date;
	uint32_t   pwr_clk_freq;
	uint8_t    is_ext_pwr_clk;
	int8_t     current_temp;
} __packed;

union hif_indication_data {
	struct hif_rx_stats                                   rx_stats;
	uint8_t                                       raw_data[1];
};

struct hif_ind_generic {
	uint32_t indication_type;
	union hif_indication_data indication_data;
} __packed;


#define HIF_EXCEPTION_DATA_SIZE            124

struct hif_ind_exception {
	uint8_t    data[HIF_EXCEPTION_DATA_SIZE];
} __packed;


enum hif_error {
	HIF_ERROR_FIRMWARE_ROLLBACK             = 0x0,
	HIF_ERROR_FIRMWARE_DEBUG_ENABLED        = 0x1,
	HIF_ERROR_OUTDATED_SESSION_KEY          = 0x2,
	HIF_ERROR_INVALID_SESSION_KEY           = 0x3,
	HIF_ERROR_OOR_VOLTAGE                   = 0x4,
	HIF_ERROR_PDS_VERSION                   = 0x5,
	HIF_ERROR_OOR_TEMPERATURE               = 0x6,
	HIF_ERROR_REQ_DURING_KEY_EXCHANGE       = 0x7,
	HIF_ERROR_MULTI_TX_CNF_SECURELINK       = 0x8,
	HIF_ERROR_SECURELINK_OVERFLOW           = 0x9,
	HIF_ERROR_SECURELINK_DECRYPTION         = 0xa
};

struct hif_ind_error {
	uint32_t   type;
	uint8_t    data[];
} __packed;

enum hif_secure_link_state {
	SEC_LINK_UNAVAILABLE                    = 0x0,
	SEC_LINK_RESERVED                       = 0x1,
	SEC_LINK_EVAL                           = 0x2,
	SEC_LINK_ENFORCED                       = 0x3
};

enum hif_sl_encryption_type {
	NO_ENCRYPTION = 0,
	TX_ENCRYPTION = 1,
	RX_ENCRYPTION = 2,
	HP_ENCRYPTION = 3
};

struct hif_sl_msg_hdr {
	uint32_t    seqnum:30;
	uint32_t    encrypted:2;
} __packed;

struct hif_sl_msg {
	struct hif_sl_msg_hdr hdr;
	uint16_t        len;
	uint8_t         payload[];
} __packed;

#define AES_CCM_TAG_SIZE     16

struct hif_sl_tag {
	uint8_t tag[16];
} __packed;

enum hif_sl_mac_key_dest {
	SL_MAC_KEY_DEST_OTP                        = 0x78,
	SL_MAC_KEY_DEST_RAM                        = 0x87
};

#define API_KEY_VALUE_SIZE      32

struct hif_req_set_sl_mac_key {
	uint8_t    otp_or_ram;
	uint8_t    key_value[API_KEY_VALUE_SIZE];
} __packed;

struct hif_cnf_set_sl_mac_key {
	uint32_t   status;
} __packed;

#define API_HOST_PUB_KEY_SIZE                           32
#define API_HOST_PUB_KEY_MAC_SIZE                       64

enum hif_sl_session_key_alg {
	HIF_SL_CURVE25519                                = 0x01,
	HIF_SL_KDF                                       = 0x02
};

struct hif_req_sl_exchange_pub_keys {
	uint8_t    algorithm:2;
	uint8_t    reserved1:6;
	uint8_t    reserved2[3];
	uint8_t    host_pub_key[API_HOST_PUB_KEY_SIZE];
	uint8_t    host_pub_key_mac[API_HOST_PUB_KEY_MAC_SIZE];
} __packed;

struct hif_cnf_sl_exchange_pub_keys {
	uint32_t   status;
} __packed;

#define API_NCP_PUB_KEY_SIZE                            32
#define API_NCP_PUB_KEY_MAC_SIZE                        64

struct hif_ind_sl_exchange_pub_keys {
	uint32_t   status;
	uint8_t    ncp_pub_key[API_NCP_PUB_KEY_SIZE];
	uint8_t    ncp_pub_key_mac[API_NCP_PUB_KEY_MAC_SIZE];
} __packed;

#define API_ENCR_BMP_SIZE        32

struct hif_req_sl_configure {
	uint8_t    encr_bmp[API_ENCR_BMP_SIZE];
	uint8_t    disable_session_key_protection:1;
	uint8_t    reserved1:7;
	uint8_t    reserved2[3];
} __packed;

struct hif_cnf_sl_configure {
	uint32_t status;
} __packed;

struct hif_req_prevent_rollback {
	uint32_t   magic_word;
} __packed;

struct hif_cnf_prevent_rollback {
	uint32_t    status;
} __packed;

enum hif_pta_mode {
	PTA_1W_WLAN_MASTER = 0,
	PTA_1W_COEX_MASTER = 1,
	PTA_2W             = 2,
	PTA_3W             = 3,
	PTA_4W             = 4
};

enum hif_signal_level {
	SIGNAL_LOW  = 0,
	SIGNAL_HIGH = 1
};

enum hif_coex_type {
	COEX_TYPE_GENERIC = 0,
	COEX_TYPE_BLE     = 1
};

enum hif_grant_state {
	NO_GRANT = 0,
	GRANT    = 1
};

struct hif_req_pta_settings {
	uint8_t pta_mode;
	uint8_t request_signal_active_level;
	uint8_t priority_signal_active_level;
	uint8_t freq_signal_active_level;
	uint8_t grant_signal_active_level;
	uint8_t coex_type;
	uint8_t default_grant_state;
	uint8_t simultaneous_rx_accesses;
	uint8_t priority_sampling_time;
	uint8_t tx_rx_sampling_time;
	uint8_t freq_sampling_time;
	uint8_t grant_valid_time;
	uint8_t fem_control_time;
	uint8_t first_slot_time;
	uint16_t periodic_tx_rx_sampling_time;
	uint16_t coex_quota;
	uint16_t wlan_quota;
} __packed;

struct hif_cnf_pta_settings {
	uint32_t status;
} __packed;

enum hif_pta_priority {
	HIF_PTA_PRIORITY_COEX_MAXIMIZED = 0x00000562,
	HIF_PTA_PRIORITY_COEX_HIGH      = 0x00000462,
	HIF_PTA_PRIORITY_BALANCED       = 0x00001461,
	HIF_PTA_PRIORITY_WLAN_HIGH      = 0x00001851,
	HIF_PTA_PRIORITY_WLAN_MAXIMIZED = 0x00001A51
};

struct hif_req_pta_priority {
	uint32_t priority;
} __packed;

struct hif_cnf_pta_priority {
	uint32_t status;
} __packed;

enum hif_pta_state {
	PTA_OFF = 0,
	PTA_ON  = 1
};

struct hif_req_pta_state {
	uint32_t pta_state;
} __packed;

struct hif_cnf_pta_state {
	uint32_t status;
} __packed;

#endif

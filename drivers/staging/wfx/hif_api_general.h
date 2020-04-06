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
	u16    len;
	u8     id;
	u8     reserved:1;
	u8     interface:2;
	u8     seqnum:3;
	u8     encrypted:2;
	u8     body[];
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
	u8    link_mode:2;
	u8    reserved1:6;
	u8    reserved2;
	u8    reserved3;
	u8    reserved4;
} __packed;

struct hif_otp_regul_sel_mode_info {
	u8    region_sel_mode:4;
	u8    reserved:4;
} __packed;

struct hif_otp_phy_info {
	u8    phy1_region:3;
	u8    phy0_region:3;
	u8    otp_phy_ver:2;
} __packed;

struct hif_ind_startup {
	u32   status;
	u16   hardware_id;
	u8    opn[14];
	u8    uid[8];
	u16   num_inp_ch_bufs;
	u16   size_inp_ch_buf;
	u8    num_links_ap;
	u8    num_interfaces;
	u8    mac_addr[2][ETH_ALEN];
	u8    api_version_minor;
	u8    api_version_major;
	struct hif_capabilities capabilities;
	u8    firmware_build;
	u8    firmware_minor;
	u8    firmware_major;
	u8    firmware_type;
	u8    disabled_channel_list[2];
	struct hif_otp_regul_sel_mode_info regul_sel_mode_info;
	struct hif_otp_phy_info otp_phy_info;
	u32   supported_rate_mask;
	u8    firmware_label[128];
} __packed;

struct hif_ind_wakeup {
} __packed;

struct hif_req_configuration {
	u16   length;
	u8    pds_data[];
} __packed;

struct hif_cnf_configuration {
	u32   status;
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
	u8 gpio_label;
	u8 gpio_mode;
} __packed;

struct hif_cnf_control_gpio {
	u32 status;
	u32 value;
} __packed;

enum hif_generic_indication_type {
	HIF_GENERIC_INDICATION_TYPE_RAW               = 0x0,
	HIF_GENERIC_INDICATION_TYPE_STRING            = 0x1,
	HIF_GENERIC_INDICATION_TYPE_RX_STATS          = 0x2
};

struct hif_rx_stats {
	u32   nb_rx_frame;
	u32   nb_crc_frame;
	u32   per_total;
	u32   throughput;
	u32   nb_rx_by_rate[API_RATE_NUM_ENTRIES];
	u16   per[API_RATE_NUM_ENTRIES];
	s16    snr[API_RATE_NUM_ENTRIES];
	s16    rssi[API_RATE_NUM_ENTRIES];
	s16    cfo[API_RATE_NUM_ENTRIES];
	u32   date;
	u32   pwr_clk_freq;
	u8    is_ext_pwr_clk;
	s8     current_temp;
} __packed;

union hif_indication_data {
	struct hif_rx_stats                                   rx_stats;
	u8                                       raw_data[1];
};

struct hif_ind_generic {
	u32 indication_type;
	union hif_indication_data indication_data;
} __packed;


struct hif_ind_exception {
	u8    data[124];
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
	u32   type;
	u8    data[];
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
	u32    seqnum:30;
	u32    encrypted:2;
} __packed;

struct hif_sl_msg {
	struct hif_sl_msg_hdr hdr;
	u16        len;
	u8         payload[];
} __packed;

#define AES_CCM_TAG_SIZE     16

struct hif_sl_tag {
	u8 tag[16];
} __packed;

enum hif_sl_mac_key_dest {
	SL_MAC_KEY_DEST_OTP                        = 0x78,
	SL_MAC_KEY_DEST_RAM                        = 0x87
};

#define API_KEY_VALUE_SIZE      32

struct hif_req_set_sl_mac_key {
	u8    otp_or_ram;
	u8    key_value[API_KEY_VALUE_SIZE];
} __packed;

struct hif_cnf_set_sl_mac_key {
	u32   status;
} __packed;

enum hif_sl_session_key_alg {
	HIF_SL_CURVE25519                                = 0x01,
	HIF_SL_KDF                                       = 0x02
};

#define API_HOST_PUB_KEY_SIZE                           32
#define API_HOST_PUB_KEY_MAC_SIZE                       64

struct hif_req_sl_exchange_pub_keys {
	u8    algorithm:2;
	u8    reserved1:6;
	u8    reserved2[3];
	u8    host_pub_key[API_HOST_PUB_KEY_SIZE];
	u8    host_pub_key_mac[API_HOST_PUB_KEY_MAC_SIZE];
} __packed;

struct hif_cnf_sl_exchange_pub_keys {
	u32   status;
} __packed;

#define API_NCP_PUB_KEY_SIZE                            32
#define API_NCP_PUB_KEY_MAC_SIZE                        64

struct hif_ind_sl_exchange_pub_keys {
	u32   status;
	u8    ncp_pub_key[API_NCP_PUB_KEY_SIZE];
	u8    ncp_pub_key_mac[API_NCP_PUB_KEY_MAC_SIZE];
} __packed;

struct hif_req_sl_configure {
	u8    encr_bmp[32];
	u8    disable_session_key_protection:1;
	u8    reserved1:7;
	u8    reserved2[3];
} __packed;

struct hif_cnf_sl_configure {
	u32 status;
} __packed;

#endif

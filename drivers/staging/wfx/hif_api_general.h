/* SPDX-License-Identifier: Apache-2.0 */
/*
 * WFx hardware interface definitions
 *
 * Copyright (c) 2018-2020, Silicon Laboratories Inc.
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

#define HIF_ID_IS_INDICATION      0x80
#define HIF_COUNTER_MAX           7

struct hif_msg {
	__le16 len;
	u8     id;
	u8     reserved:1;
	u8     interface:2;
	u8     seqnum:3;
	u8     encrypted:2;
	u8     body[];
} __packed;

enum hif_general_requests_ids {
	HIF_REQ_ID_CONFIGURATION        = 0x09,
	HIF_REQ_ID_CONTROL_GPIO         = 0x26,
	HIF_REQ_ID_SET_SL_MAC_KEY       = 0x27,
	HIF_REQ_ID_SL_EXCHANGE_PUB_KEYS = 0x28,
	HIF_REQ_ID_SL_CONFIGURE         = 0x29,
	HIF_REQ_ID_PREVENT_ROLLBACK     = 0x2a,
	HIF_REQ_ID_PTA_SETTINGS         = 0x2b,
	HIF_REQ_ID_PTA_PRIORITY         = 0x2c,
	HIF_REQ_ID_PTA_STATE            = 0x2d,
	HIF_REQ_ID_SHUT_DOWN            = 0x32,
};

enum hif_general_confirmations_ids {
	HIF_CNF_ID_CONFIGURATION        = 0x09,
	HIF_CNF_ID_CONTROL_GPIO         = 0x26,
	HIF_CNF_ID_SET_SL_MAC_KEY       = 0x27,
	HIF_CNF_ID_SL_EXCHANGE_PUB_KEYS = 0x28,
	HIF_CNF_ID_SL_CONFIGURE         = 0x29,
	HIF_CNF_ID_PREVENT_ROLLBACK     = 0x2a,
	HIF_CNF_ID_PTA_SETTINGS         = 0x2b,
	HIF_CNF_ID_PTA_PRIORITY         = 0x2c,
	HIF_CNF_ID_PTA_STATE            = 0x2d,
	HIF_CNF_ID_SHUT_DOWN            = 0x32,
};

enum hif_general_indications_ids {
	HIF_IND_ID_EXCEPTION            = 0xe0,
	HIF_IND_ID_STARTUP              = 0xe1,
	HIF_IND_ID_WAKEUP               = 0xe2,
	HIF_IND_ID_GENERIC              = 0xe3,
	HIF_IND_ID_ERROR                = 0xe4,
	HIF_IND_ID_SL_EXCHANGE_PUB_KEYS = 0xe5
};

#define HIF_STATUS_SUCCESS                         (cpu_to_le32(0x0000))
#define HIF_STATUS_FAIL                            (cpu_to_le32(0x0001))
#define HIF_STATUS_INVALID_PARAMETER               (cpu_to_le32(0x0002))
#define HIF_STATUS_WARNING                         (cpu_to_le32(0x0003))
#define HIF_STATUS_UNKNOWN_REQUEST                 (cpu_to_le32(0x0004))
#define HIF_STATUS_RX_FAIL_DECRYPT                 (cpu_to_le32(0x0010))
#define HIF_STATUS_RX_FAIL_MIC                     (cpu_to_le32(0x0011))
#define HIF_STATUS_RX_FAIL_NO_KEY                  (cpu_to_le32(0x0012))
#define HIF_STATUS_TX_FAIL_RETRIES                 (cpu_to_le32(0x0013))
#define HIF_STATUS_TX_FAIL_TIMEOUT                 (cpu_to_le32(0x0014))
#define HIF_STATUS_TX_FAIL_REQUEUE                 (cpu_to_le32(0x0015))
#define HIF_STATUS_REFUSED                         (cpu_to_le32(0x0016))
#define HIF_STATUS_BUSY                            (cpu_to_le32(0x0017))
#define HIF_STATUS_SLK_SET_KEY_SUCCESS             (cpu_to_le32(0x005A))
#define HIF_STATUS_SLK_SET_KEY_ALREADY_BURNED      (cpu_to_le32(0x006B))
#define HIF_STATUS_SLK_SET_KEY_DISALLOWED_MODE     (cpu_to_le32(0x007C))
#define HIF_STATUS_SLK_SET_KEY_UNKNOWN_MODE        (cpu_to_le32(0x008D))
#define HIF_STATUS_SLK_NEGO_SUCCESS                (cpu_to_le32(0x009E))
#define HIF_STATUS_SLK_NEGO_FAILED                 (cpu_to_le32(0x00AF))
#define HIF_STATUS_ROLLBACK_SUCCESS                (cpu_to_le32(0x1234))
#define HIF_STATUS_ROLLBACK_FAIL                   (cpu_to_le32(0x1256))

enum hif_api_rate_index {
	API_RATE_INDEX_B_1MBPS     = 0,
	API_RATE_INDEX_B_2MBPS     = 1,
	API_RATE_INDEX_B_5P5MBPS   = 2,
	API_RATE_INDEX_B_11MBPS    = 3,
	API_RATE_INDEX_PBCC_22MBPS = 4,
	API_RATE_INDEX_PBCC_33MBPS = 5,
	API_RATE_INDEX_G_6MBPS     = 6,
	API_RATE_INDEX_G_9MBPS     = 7,
	API_RATE_INDEX_G_12MBPS    = 8,
	API_RATE_INDEX_G_18MBPS    = 9,
	API_RATE_INDEX_G_24MBPS    = 10,
	API_RATE_INDEX_G_36MBPS    = 11,
	API_RATE_INDEX_G_48MBPS    = 12,
	API_RATE_INDEX_G_54MBPS    = 13,
	API_RATE_INDEX_N_6P5MBPS   = 14,
	API_RATE_INDEX_N_13MBPS    = 15,
	API_RATE_INDEX_N_19P5MBPS  = 16,
	API_RATE_INDEX_N_26MBPS    = 17,
	API_RATE_INDEX_N_39MBPS    = 18,
	API_RATE_INDEX_N_52MBPS    = 19,
	API_RATE_INDEX_N_58P5MBPS  = 20,
	API_RATE_INDEX_N_65MBPS    = 21,
	API_RATE_NUM_ENTRIES       = 22
};

enum hif_fw_type {
	HIF_FW_TYPE_ETF  = 0x0,
	HIF_FW_TYPE_WFM  = 0x1,
	HIF_FW_TYPE_WSM  = 0x2
};

struct hif_ind_startup {
	// As the others, this struct is interpreted as little endian by the
	// device. However, this struct is also used by the driver. We prefer to
	// declare it in native order and doing byte swap on reception.
	__le32 status;
	u16    hardware_id;
	u8     opn[14];
	u8     uid[8];
	u16    num_inp_ch_bufs;
	u16    size_inp_ch_buf;
	u8     num_links_ap;
	u8     num_interfaces;
	u8     mac_addr[2][ETH_ALEN];
	u8     api_version_minor;
	u8     api_version_major;
	u8     link_mode:2;
	u8     reserved1:6;
	u8     reserved2;
	u8     reserved3;
	u8     reserved4;
	u8     firmware_build;
	u8     firmware_minor;
	u8     firmware_major;
	u8     firmware_type;
	u8     disabled_channel_list[2];
	u8     region_sel_mode:4;
	u8     reserved5:4;
	u8     phy1_region:3;
	u8     phy0_region:3;
	u8     otp_phy_ver:2;
	u32    supported_rate_mask;
	u8     firmware_label[128];
} __packed;

struct hif_ind_wakeup {
} __packed;

struct hif_req_configuration {
	__le16 length;
	u8     pds_data[];
} __packed;

struct hif_cnf_configuration {
	__le32 status;
} __packed;

enum hif_gpio_mode {
	HIF_GPIO_MODE_D0       = 0x0,
	HIF_GPIO_MODE_D1       = 0x1,
	HIF_GPIO_MODE_OD0      = 0x2,
	HIF_GPIO_MODE_OD1      = 0x3,
	HIF_GPIO_MODE_TRISTATE = 0x4,
	HIF_GPIO_MODE_TOGGLE   = 0x5,
	HIF_GPIO_MODE_READ     = 0x6
};

struct hif_req_control_gpio {
	u8     gpio_label;
	u8     gpio_mode;
} __packed;

struct hif_cnf_control_gpio {
	__le32 status;
	__le32 value;
} __packed;

enum hif_generic_indication_type {
	HIF_GENERIC_INDICATION_TYPE_RAW                = 0x0,
	HIF_GENERIC_INDICATION_TYPE_STRING             = 0x1,
	HIF_GENERIC_INDICATION_TYPE_RX_STATS           = 0x2,
	HIF_GENERIC_INDICATION_TYPE_TX_POWER_LOOP_INFO = 0x3,
};

struct hif_rx_stats {
	__le32 nb_rx_frame;
	__le32 nb_crc_frame;
	__le32 per_total;
	__le32 throughput;
	__le32 nb_rx_by_rate[API_RATE_NUM_ENTRIES];
	__le16 per[API_RATE_NUM_ENTRIES];
	__le16 snr[API_RATE_NUM_ENTRIES];  // signed value
	__le16 rssi[API_RATE_NUM_ENTRIES]; // signed value
	__le16 cfo[API_RATE_NUM_ENTRIES];  // signed value
	__le32 date;
	__le32 pwr_clk_freq;
	u8     is_ext_pwr_clk;
	s8     current_temp;
} __packed;

struct hif_tx_power_loop_info {
	__le16 tx_gain_dig;
	__le16 tx_gain_pa;
	__le16 target_pout; // signed value
	__le16 p_estimation; // signed value
	__le16 vpdet;
	u8     measurement_index;
	u8     reserved;
} __packed;

struct hif_ind_generic {
	__le32 type;
	union {
		struct hif_rx_stats rx_stats;
		struct hif_tx_power_loop_info tx_power_loop_info;
	} data;
} __packed;

enum hif_error {
	HIF_ERROR_FIRMWARE_ROLLBACK           = 0x00,
	HIF_ERROR_FIRMWARE_DEBUG_ENABLED      = 0x01,
	HIF_ERROR_SLK_OUTDATED_SESSION_KEY    = 0x02,
	HIF_ERROR_SLK_SESSION_KEY             = 0x03,
	HIF_ERROR_OOR_VOLTAGE                 = 0x04,
	HIF_ERROR_PDS_PAYLOAD                 = 0x05,
	HIF_ERROR_OOR_TEMPERATURE             = 0x06,
	HIF_ERROR_SLK_REQ_DURING_KEY_EXCHANGE = 0x07,
	HIF_ERROR_SLK_MULTI_TX_UNSUPPORTED    = 0x08,
	HIF_ERROR_SLK_OVERFLOW                = 0x09,
	HIF_ERROR_SLK_DECRYPTION              = 0x0a,
	HIF_ERROR_SLK_WRONG_ENCRYPTION_STATE  = 0x0b,
	HIF_ERROR_HIF_BUS_FREQUENCY_TOO_LOW   = 0x0c,
	HIF_ERROR_HIF_RX_DATA_TOO_LARGE       = 0x0e,
	HIF_ERROR_HIF_TX_QUEUE_FULL           = 0x0d,
	HIF_ERROR_HIF_BUS                     = 0x0f,
	HIF_ERROR_PDS_TESTFEATURE             = 0x10,
	HIF_ERROR_SLK_UNCONFIGURED            = 0x11,
};

struct hif_ind_error {
	__le32 type;
	u8     data[];
} __packed;

struct hif_ind_exception {
	__le32 type;
	u8     data[];
} __packed;

enum hif_secure_link_state {
	SEC_LINK_UNAVAILABLE = 0x0,
	SEC_LINK_RESERVED    = 0x1,
	SEC_LINK_EVAL        = 0x2,
	SEC_LINK_ENFORCED    = 0x3
};

#endif

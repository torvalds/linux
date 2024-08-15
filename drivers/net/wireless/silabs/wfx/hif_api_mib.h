/* SPDX-License-Identifier: GPL-2.0-only or Apache-2.0 */
/*
 * WF200 hardware interface definitions
 *
 * Copyright (c) 2018-2020, Silicon Laboratories Inc.
 */

#ifndef WFX_HIF_API_MIB_H
#define WFX_HIF_API_MIB_H

#include "hif_api_general.h"

#define HIF_API_IPV4_ADDRESS_SIZE 4
#define HIF_API_IPV6_ADDRESS_SIZE 16

enum wfx_hif_mib_ids {
	HIF_MIB_ID_GL_OPERATIONAL_POWER_MODE        = 0x2000,
	HIF_MIB_ID_GL_BLOCK_ACK_INFO                = 0x2001,
	HIF_MIB_ID_GL_SET_MULTI_MSG                 = 0x2002,
	HIF_MIB_ID_CCA_CONFIG                       = 0x2003,
	HIF_MIB_ID_ETHERTYPE_DATAFRAME_CONDITION    = 0x2010,
	HIF_MIB_ID_PORT_DATAFRAME_CONDITION         = 0x2011,
	HIF_MIB_ID_MAGIC_DATAFRAME_CONDITION        = 0x2012,
	HIF_MIB_ID_MAC_ADDR_DATAFRAME_CONDITION     = 0x2013,
	HIF_MIB_ID_IPV4_ADDR_DATAFRAME_CONDITION    = 0x2014,
	HIF_MIB_ID_IPV6_ADDR_DATAFRAME_CONDITION    = 0x2015,
	HIF_MIB_ID_UC_MC_BC_DATAFRAME_CONDITION     = 0x2016,
	HIF_MIB_ID_CONFIG_DATA_FILTER               = 0x2017,
	HIF_MIB_ID_SET_DATA_FILTERING               = 0x2018,
	HIF_MIB_ID_ARP_IP_ADDRESSES_TABLE           = 0x2019,
	HIF_MIB_ID_NS_IP_ADDRESSES_TABLE            = 0x201A,
	HIF_MIB_ID_RX_FILTER                        = 0x201B,
	HIF_MIB_ID_BEACON_FILTER_TABLE              = 0x201C,
	HIF_MIB_ID_BEACON_FILTER_ENABLE             = 0x201D,
	HIF_MIB_ID_GRP_SEQ_COUNTER                  = 0x2030,
	HIF_MIB_ID_TSF_COUNTER                      = 0x2031,
	HIF_MIB_ID_STATISTICS_TABLE                 = 0x2032,
	HIF_MIB_ID_COUNTERS_TABLE                   = 0x2033,
	HIF_MIB_ID_MAX_TX_POWER_LEVEL               = 0x2034,
	HIF_MIB_ID_EXTENDED_COUNTERS_TABLE          = 0x2035,
	HIF_MIB_ID_DOT11_MAC_ADDRESS                = 0x2040,
	HIF_MIB_ID_DOT11_MAX_TRANSMIT_MSDU_LIFETIME = 0x2041,
	HIF_MIB_ID_DOT11_MAX_RECEIVE_LIFETIME       = 0x2042,
	HIF_MIB_ID_DOT11_WEP_DEFAULT_KEY_ID         = 0x2043,
	HIF_MIB_ID_DOT11_RTS_THRESHOLD              = 0x2044,
	HIF_MIB_ID_SLOT_TIME                        = 0x2045,
	HIF_MIB_ID_CURRENT_TX_POWER_LEVEL           = 0x2046,
	HIF_MIB_ID_NON_ERP_PROTECTION               = 0x2047,
	HIF_MIB_ID_TEMPLATE_FRAME                   = 0x2048,
	HIF_MIB_ID_BEACON_WAKEUP_PERIOD             = 0x2049,
	HIF_MIB_ID_RCPI_RSSI_THRESHOLD              = 0x204A,
	HIF_MIB_ID_BLOCK_ACK_POLICY                 = 0x204B,
	HIF_MIB_ID_OVERRIDE_INTERNAL_TX_RATE        = 0x204C,
	HIF_MIB_ID_SET_ASSOCIATION_MODE             = 0x204D,
	HIF_MIB_ID_SET_UAPSD_INFORMATION            = 0x204E,
	HIF_MIB_ID_SET_TX_RATE_RETRY_POLICY         = 0x204F,
	HIF_MIB_ID_PROTECTED_MGMT_POLICY            = 0x2050,
	HIF_MIB_ID_SET_HT_PROTECTION                = 0x2051,
	HIF_MIB_ID_KEEP_ALIVE_PERIOD                = 0x2052,
	HIF_MIB_ID_ARP_KEEP_ALIVE_PERIOD            = 0x2053,
	HIF_MIB_ID_INACTIVITY_TIMER                 = 0x2054,
	HIF_MIB_ID_INTERFACE_PROTECTION             = 0x2055,
	HIF_MIB_ID_BEACON_STATS                     = 0x2056,
};

enum wfx_hif_op_power_mode {
	HIF_OP_POWER_MODE_ACTIVE    = 0x0,
	HIF_OP_POWER_MODE_DOZE      = 0x1,
	HIF_OP_POWER_MODE_QUIESCENT = 0x2
};

struct wfx_hif_mib_gl_operational_power_mode {
	u8     power_mode:4;
	u8     reserved1:3;
	u8     wup_ind_activation:1;
	u8     reserved2[3];
} __packed;

struct wfx_hif_mib_gl_set_multi_msg {
	u8     enable_multi_tx_conf:1;
	u8     reserved1:7;
	u8     reserved2[3];
} __packed;

enum wfx_hif_arp_ns_frame_treatment {
	HIF_ARP_NS_FILTERING_DISABLE = 0x0,
	HIF_ARP_NS_FILTERING_ENABLE  = 0x1,
	HIF_ARP_NS_REPLY_ENABLE      = 0x2
};

struct wfx_hif_mib_arp_ip_addr_table {
	u8     condition_idx;
	u8     arp_enable;
	u8     reserved[2];
	u8     ipv4_address[HIF_API_IPV4_ADDRESS_SIZE];
} __packed;

struct wfx_hif_mib_rx_filter {
	u8     reserved1:1;
	u8     bssid_filter:1;
	u8     reserved2:1;
	u8     fwd_probe_req:1;
	u8     keep_alive_filter:1;
	u8     reserved3:3;
	u8     reserved4[3];
} __packed;

struct wfx_hif_ie_table_entry {
	u8     ie_id;
	u8     has_changed:1;
	u8     no_longer:1;
	u8     has_appeared:1;
	u8     reserved:1;
	u8     num_match_data:4;
	u8     oui[3];
	u8     match_data[3];
} __packed;

struct wfx_hif_mib_bcn_filter_table {
	__le32 num_of_info_elmts;
	struct wfx_hif_ie_table_entry ie_table[];
} __packed;

enum wfx_hif_beacon_filter {
	HIF_BEACON_FILTER_DISABLE  = 0x0,
	HIF_BEACON_FILTER_ENABLE   = 0x1,
	HIF_BEACON_FILTER_AUTO_ERP = 0x2
};

struct wfx_hif_mib_bcn_filter_enable {
	__le32 enable;
	__le32 bcn_count;
} __packed;

struct wfx_hif_mib_extended_count_table {
	__le32 count_drop_plcp;
	__le32 count_drop_fcs;
	__le32 count_tx_frames;
	__le32 count_rx_frames;
	__le32 count_rx_frames_failed;
	__le32 count_drop_decryption;
	__le32 count_drop_tkip_mic;
	__le32 count_drop_no_key;
	__le32 count_tx_frames_multicast;
	__le32 count_tx_frames_success;
	__le32 count_tx_frames_failed;
	__le32 count_tx_frames_retried;
	__le32 count_tx_frames_multi_retried;
	__le32 count_drop_duplicate;
	__le32 count_rts_success;
	__le32 count_rts_failed;
	__le32 count_ack_failed;
	__le32 count_rx_frames_multicast;
	__le32 count_rx_frames_success;
	__le32 count_drop_cmac_icv;
	__le32 count_drop_cmac_replay;
	__le32 count_drop_ccmp_replay;
	__le32 count_drop_bip_mic;
	__le32 count_rx_bcn_success;
	__le32 count_rx_bcn_miss;
	__le32 count_rx_bcn_dtim;
	__le32 count_rx_bcn_dtim_aid0_clr;
	__le32 count_rx_bcn_dtim_aid0_set;
	__le32 reserved[12];
} __packed;

struct wfx_hif_mib_count_table {
	__le32 count_drop_plcp;
	__le32 count_drop_fcs;
	__le32 count_tx_frames;
	__le32 count_rx_frames;
	__le32 count_rx_frames_failed;
	__le32 count_drop_decryption;
	__le32 count_drop_tkip_mic;
	__le32 count_drop_no_key;
	__le32 count_tx_frames_multicast;
	__le32 count_tx_frames_success;
	__le32 count_tx_frames_failed;
	__le32 count_tx_frames_retried;
	__le32 count_tx_frames_multi_retried;
	__le32 count_drop_duplicate;
	__le32 count_rts_success;
	__le32 count_rts_failed;
	__le32 count_ack_failed;
	__le32 count_rx_frames_multicast;
	__le32 count_rx_frames_success;
	__le32 count_drop_cmac_icv;
	__le32 count_drop_cmac_replay;
	__le32 count_drop_ccmp_replay;
	__le32 count_drop_bip_mic;
} __packed;

struct wfx_hif_mib_mac_address {
	u8     mac_addr[ETH_ALEN];
	__le16 reserved;
} __packed;

struct wfx_hif_mib_wep_default_key_id {
	u8     wep_default_key_id;
	u8     reserved[3];
} __packed;

struct wfx_hif_mib_dot11_rts_threshold {
	__le32 threshold;
} __packed;

struct wfx_hif_mib_slot_time {
	__le32 slot_time;
} __packed;

struct wfx_hif_mib_current_tx_power_level {
	__le32 power_level; /* signed value */
} __packed;

struct wfx_hif_mib_non_erp_protection {
	u8     use_cts_to_self:1;
	u8     reserved1:7;
	u8     reserved2[3];
} __packed;

enum wfx_hif_tmplt {
	HIF_TMPLT_PRBREQ = 0x0,
	HIF_TMPLT_BCN    = 0x1,
	HIF_TMPLT_NULL   = 0x2,
	HIF_TMPLT_QOSNUL = 0x3,
	HIF_TMPLT_PSPOLL = 0x4,
	HIF_TMPLT_PRBRES = 0x5,
	HIF_TMPLT_ARP    = 0x6,
	HIF_TMPLT_NA     = 0x7
};

#define HIF_API_MAX_TEMPLATE_FRAME_SIZE 700

struct wfx_hif_mib_template_frame {
	u8     frame_type;
	u8     init_rate:7;
	u8     mode:1;
	__le16 frame_length;
	u8     frame[];
} __packed;

struct wfx_hif_mib_beacon_wake_up_period {
	u8     wakeup_period_min;
	u8     receive_dtim:1;
	u8     reserved1:7;
	u8     wakeup_period_max;
	u8     reserved2;
} __packed;

struct wfx_hif_mib_rcpi_rssi_threshold {
	u8     detection:1;
	u8     rcpi_rssi:1;
	u8     upperthresh:1;
	u8     lowerthresh:1;
	u8     reserved:4;
	u8     lower_threshold;
	u8     upper_threshold;
	u8     rolling_average_count;
} __packed;

#define DEFAULT_BA_MAX_RX_BUFFER_SIZE 16

struct wfx_hif_mib_block_ack_policy {
	u8     block_ack_tx_tid_policy;
	u8     reserved1;
	u8     block_ack_rx_tid_policy;
	u8     block_ack_rx_max_buffer_size;
} __packed;

enum wfx_hif_mpdu_start_spacing {
	HIF_MPDU_START_SPACING_NO_RESTRIC = 0x0,
	HIF_MPDU_START_SPACING_QUARTER    = 0x1,
	HIF_MPDU_START_SPACING_HALF       = 0x2,
	HIF_MPDU_START_SPACING_ONE        = 0x3,
	HIF_MPDU_START_SPACING_TWO        = 0x4,
	HIF_MPDU_START_SPACING_FOUR       = 0x5,
	HIF_MPDU_START_SPACING_EIGHT      = 0x6,
	HIF_MPDU_START_SPACING_SIXTEEN    = 0x7
};

struct wfx_hif_mib_set_association_mode {
	u8     preambtype_use:1;
	u8     mode:1;
	u8     rateset:1;
	u8     spacing:1;
	u8     reserved1:4;
	u8     short_preamble:1;
	u8     reserved2:7;
	u8     greenfield:1;
	u8     reserved3:7;
	u8     mpdu_start_spacing;
	__le32 basic_rate_set;
} __packed;

struct wfx_hif_mib_set_uapsd_information {
	u8     trig_bckgrnd:1;
	u8     trig_be:1;
	u8     trig_video:1;
	u8     trig_voice:1;
	u8     reserved1:4;
	u8     deliv_bckgrnd:1;
	u8     deliv_be:1;
	u8     deliv_video:1;
	u8     deliv_voice:1;
	u8     reserved2:4;
	__le16 min_auto_trigger_interval;
	__le16 max_auto_trigger_interval;
	__le16 auto_trigger_step;
} __packed;

struct wfx_hif_tx_rate_retry_policy {
	u8     policy_index;
	u8     short_retry_count;
	u8     long_retry_count;
	u8     first_rate_sel:2;
	u8     terminate:1;
	u8     count_init:1;
	u8     reserved1:4;
	u8     rate_recovery_count;
	u8     reserved2[3];
	u8     rates[12];
} __packed;

#define HIF_TX_RETRY_POLICY_MAX     15
#define HIF_TX_RETRY_POLICY_INVALID HIF_TX_RETRY_POLICY_MAX

struct wfx_hif_mib_set_tx_rate_retry_policy {
	u8     num_tx_rate_policies;
	u8     reserved[3];
	struct wfx_hif_tx_rate_retry_policy tx_rate_retry_policy[];
} __packed;

struct wfx_hif_mib_protected_mgmt_policy {
	u8     pmf_enable:1;
	u8     unpmf_allowed:1;
	u8     host_enc_auth_frames:1;
	u8     reserved1:5;
	u8     reserved2[3];
} __packed;

struct wfx_hif_mib_keep_alive_period {
	__le16 keep_alive_period;
	u8     reserved[2];
} __packed;

#endif

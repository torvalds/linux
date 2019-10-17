/* SPDX-License-Identifier: Apache-2.0 */
/*
 * WFx hardware interface definitions
 *
 * Copyright (c) 2018-2019, Silicon Laboratories Inc.
 */

#ifndef WFX_HIF_API_MIB_H
#define WFX_HIF_API_MIB_H

#include "hif_api_general.h"

#define HIF_API_IPV4_ADDRESS_SIZE                       4
#define HIF_API_IPV6_ADDRESS_SIZE                       16

enum hif_mib_ids {
	HIF_MIB_ID_GL_OPERATIONAL_POWER_MODE       = 0x2000,
	HIF_MIB_ID_GL_BLOCK_ACK_INFO               = 0x2001,
	HIF_MIB_ID_GL_SET_MULTI_MSG                = 0x2002,
	HIF_MIB_ID_CCA_CONFIG                      = 0x2003,
	HIF_MIB_ID_ETHERTYPE_DATAFRAME_CONDITION   = 0x2010,
	HIF_MIB_ID_PORT_DATAFRAME_CONDITION        = 0x2011,
	HIF_MIB_ID_MAGIC_DATAFRAME_CONDITION       = 0x2012,
	HIF_MIB_ID_MAC_ADDR_DATAFRAME_CONDITION    = 0x2013,
	HIF_MIB_ID_IPV4_ADDR_DATAFRAME_CONDITION   = 0x2014,
	HIF_MIB_ID_IPV6_ADDR_DATAFRAME_CONDITION   = 0x2015,
	HIF_MIB_ID_UC_MC_BC_DATAFRAME_CONDITION    = 0x2016,
	HIF_MIB_ID_CONFIG_DATA_FILTER              = 0x2017,
	HIF_MIB_ID_SET_DATA_FILTERING              = 0x2018,
	HIF_MIB_ID_ARP_IP_ADDRESSES_TABLE          = 0x2019,
	HIF_MIB_ID_NS_IP_ADDRESSES_TABLE           = 0x201A,
	HIF_MIB_ID_RX_FILTER                       = 0x201B,
	HIF_MIB_ID_BEACON_FILTER_TABLE             = 0x201C,
	HIF_MIB_ID_BEACON_FILTER_ENABLE            = 0x201D,
	HIF_MIB_ID_GRP_SEQ_COUNTER                 = 0x2030,
	HIF_MIB_ID_TSF_COUNTER                     = 0x2031,
	HIF_MIB_ID_STATISTICS_TABLE                = 0x2032,
	HIF_MIB_ID_COUNTERS_TABLE                  = 0x2033,
	HIF_MIB_ID_MAX_TX_POWER_LEVEL              = 0x2034,
	HIF_MIB_ID_EXTENDED_COUNTERS_TABLE         = 0x2035,
	HIF_MIB_ID_DOT11_MAC_ADDRESS               = 0x2040,
	HIF_MIB_ID_DOT11_MAX_TRANSMIT_MSDU_LIFETIME = 0x2041,
	HIF_MIB_ID_DOT11_MAX_RECEIVE_LIFETIME      = 0x2042,
	HIF_MIB_ID_DOT11_WEP_DEFAULT_KEY_ID        = 0x2043,
	HIF_MIB_ID_DOT11_RTS_THRESHOLD             = 0x2044,
	HIF_MIB_ID_SLOT_TIME                       = 0x2045,
	HIF_MIB_ID_CURRENT_TX_POWER_LEVEL          = 0x2046,
	HIF_MIB_ID_NON_ERP_PROTECTION              = 0x2047,
	HIF_MIB_ID_TEMPLATE_FRAME                  = 0x2048,
	HIF_MIB_ID_BEACON_WAKEUP_PERIOD            = 0x2049,
	HIF_MIB_ID_RCPI_RSSI_THRESHOLD             = 0x204A,
	HIF_MIB_ID_BLOCK_ACK_POLICY                = 0x204B,
	HIF_MIB_ID_OVERRIDE_INTERNAL_TX_RATE       = 0x204C,
	HIF_MIB_ID_SET_ASSOCIATION_MODE            = 0x204D,
	HIF_MIB_ID_SET_UAPSD_INFORMATION           = 0x204E,
	HIF_MIB_ID_SET_TX_RATE_RETRY_POLICY        = 0x204F,
	HIF_MIB_ID_PROTECTED_MGMT_POLICY           = 0x2050,
	HIF_MIB_ID_SET_HT_PROTECTION               = 0x2051,
	HIF_MIB_ID_KEEP_ALIVE_PERIOD               = 0x2052,
	HIF_MIB_ID_ARP_KEEP_ALIVE_PERIOD           = 0x2053,
	HIF_MIB_ID_INACTIVITY_TIMER                = 0x2054,
	HIF_MIB_ID_INTERFACE_PROTECTION            = 0x2055,
	HIF_MIB_ID_BEACON_STATS                    = 0x2056,
};

#define HIF_OP_POWER_MODE_MASK                     0xf

enum hif_op_power_mode {
	HIF_OP_POWER_MODE_ACTIVE                   = 0x0,
	HIF_OP_POWER_MODE_DOZE                     = 0x1,
	HIF_OP_POWER_MODE_QUIESCENT                = 0x2
};

struct hif_mib_gl_operational_power_mode {
	uint8_t    power_mode:4;
	uint8_t    reserved1:3;
	uint8_t    wup_ind_activation:1;
	uint8_t    reserved2[3];
} __packed;

struct hif_mib_gl_block_ack_info {
	uint8_t    rx_buffer_size;
	uint8_t    rx_max_num_agreements;
	uint8_t    tx_buffer_size;
	uint8_t    tx_max_num_agreements;
} __packed;

struct hif_mib_gl_set_multi_msg {
	uint8_t    enable_multi_tx_conf:1;
	uint8_t    reserved1:7;
	uint8_t    reserved2[3];
} __packed;

enum hif_cca_thr_mode {
	HIF_CCA_THR_MODE_RELATIVE = 0x0,
	HIF_CCA_THR_MODE_ABSOLUTE = 0x1
};

struct hif_mib_gl_cca_config {
	uint8_t  cca_thr_mode;
	uint8_t  reserved[3];
} __packed;

#define MAX_NUMBER_DATA_FILTERS             0xA

#define MAX_NUMBER_IPV4_ADDR_CONDITIONS     0x4
#define MAX_NUMBER_IPV6_ADDR_CONDITIONS     0x4
#define MAX_NUMBER_MAC_ADDR_CONDITIONS      0x4
#define MAX_NUMBER_UC_MC_BC_CONDITIONS      0x4
#define MAX_NUMBER_ETHER_TYPE_CONDITIONS    0x4
#define MAX_NUMBER_PORT_CONDITIONS          0x4
#define MAX_NUMBER_MAGIC_CONDITIONS         0x4
#define MAX_NUMBER_ARP_CONDITIONS           0x2
#define MAX_NUMBER_NS_CONDITIONS            0x2

struct hif_mib_ethertype_data_frame_condition {
	uint8_t    condition_idx;
	uint8_t    reserved;
	uint16_t   ether_type;
} __packed;

enum hif_udp_tcp_protocol {
	HIF_PROTOCOL_UDP                       = 0x0,
	HIF_PROTOCOL_TCP                       = 0x1,
	HIF_PROTOCOL_BOTH_UDP_TCP              = 0x2
};

enum hif_which_port {
	HIF_PORT_DST                           = 0x0,
	HIF_PORT_SRC                           = 0x1,
	HIF_PORT_SRC_OR_DST                    = 0x2
};

struct hif_mib_ports_data_frame_condition {
	uint8_t    condition_idx;
	uint8_t    protocol;
	uint8_t    which_port;
	uint8_t    reserved1;
	uint16_t   port_number;
	uint8_t    reserved2[2];
} __packed;

#define HIF_API_MAGIC_PATTERN_SIZE                 32

struct hif_mib_magic_data_frame_condition {
	uint8_t    condition_idx;
	uint8_t    offset;
	uint8_t    magic_pattern_length;
	uint8_t    reserved;
	uint8_t    magic_pattern[HIF_API_MAGIC_PATTERN_SIZE];
} __packed;

enum hif_mac_addr_type {
	HIF_MAC_ADDR_A1                            = 0x0,
	HIF_MAC_ADDR_A2                            = 0x1,
	HIF_MAC_ADDR_A3                            = 0x2
};

struct hif_mib_mac_addr_data_frame_condition {
	uint8_t    condition_idx;
	uint8_t    address_type;
	uint8_t    mac_address[ETH_ALEN];
} __packed;

enum hif_ip_addr_mode {
	HIF_IP_ADDR_SRC                            = 0x0,
	HIF_IP_ADDR_DST                            = 0x1
};

struct hif_mib_ipv4_addr_data_frame_condition {
	uint8_t    condition_idx;
	uint8_t    address_mode;
	uint8_t    reserved[2];
	uint8_t    i_pv4_address[HIF_API_IPV4_ADDRESS_SIZE];
} __packed;

struct hif_mib_ipv6_addr_data_frame_condition {
	uint8_t    condition_idx;
	uint8_t    address_mode;
	uint8_t    reserved[2];
	uint8_t    i_pv6_address[HIF_API_IPV6_ADDRESS_SIZE];
} __packed;

union hif_addr_type {
	uint8_t value;
	struct {
		uint8_t    type_unicast:1;
		uint8_t    type_multicast:1;
		uint8_t    type_broadcast:1;
		uint8_t    reserved:5;
	} bits;
};

struct hif_mib_uc_mc_bc_data_frame_condition {
	uint8_t    condition_idx;
	union hif_addr_type param;
	uint8_t    reserved[2];
} __packed;

struct hif_mib_config_data_filter {
	uint8_t    filter_idx;
	uint8_t    enable;
	uint8_t    reserved1[2];
	uint8_t    eth_type_cond;
	uint8_t    port_cond;
	uint8_t    magic_cond;
	uint8_t    mac_cond;
	uint8_t    ipv4_cond;
	uint8_t    ipv6_cond;
	uint8_t    uc_mc_bc_cond;
	uint8_t    reserved2;
} __packed;

struct hif_mib_set_data_filtering {
	uint8_t    default_filter;
	uint8_t    enable;
	uint8_t    reserved[2];
} __packed;

enum hif_arp_ns_frame_treatment {
	HIF_ARP_NS_FILTERING_DISABLE                  = 0x0,
	HIF_ARP_NS_FILTERING_ENABLE                   = 0x1,
	HIF_ARP_NS_REPLY_ENABLE                       = 0x2
};

struct hif_mib_arp_ip_addr_table {
	uint8_t    condition_idx;
	uint8_t    arp_enable;
	uint8_t    reserved[2];
	uint8_t    ipv4_address[HIF_API_IPV4_ADDRESS_SIZE];
} __packed;

struct hif_mib_ns_ip_addr_table {
	uint8_t    condition_idx;
	uint8_t    ns_enable;
	uint8_t    reserved[2];
	uint8_t    ipv6_address[HIF_API_IPV6_ADDRESS_SIZE];
} __packed;

struct hif_mib_rx_filter {
	uint8_t    reserved1:1;
	uint8_t    bssid_filter:1;
	uint8_t    reserved2:1;
	uint8_t    fwd_probe_req:1;
	uint8_t    keep_alive_filter:1;
	uint8_t    reserved3:3;
	uint8_t    reserved4[3];
} __packed;

#define HIF_API_OUI_SIZE                                3
#define HIF_API_MATCH_DATA_SIZE                         3

struct hif_ie_table_entry {
	uint8_t    ie_id;
	uint8_t    has_changed:1;
	uint8_t    no_longer:1;
	uint8_t    has_appeared:1;
	uint8_t    reserved:1;
	uint8_t    num_match_data:4;
	uint8_t    oui[HIF_API_OUI_SIZE];
	uint8_t    match_data[HIF_API_MATCH_DATA_SIZE];
} __packed;

struct hif_mib_bcn_filter_table {
	uint32_t   num_of_info_elmts;
	struct hif_ie_table_entry ie_table[];
} __packed;

enum hif_beacon_filter {
	HIF_BEACON_FILTER_DISABLE                  = 0x0,
	HIF_BEACON_FILTER_ENABLE                   = 0x1,
	HIF_BEACON_FILTER_AUTO_ERP                 = 0x2
};

struct hif_mib_bcn_filter_enable {
	uint32_t   enable;
	uint32_t   bcn_count;
} __packed;

struct hif_mib_group_seq_counter {
	uint32_t   bits4716;
	uint16_t   bits1500;
	uint16_t   reserved;
} __packed;

struct hif_mib_tsf_counter {
	uint32_t   tsf_counterlo;
	uint32_t   tsf_counterhi;
} __packed;

struct hif_mib_stats_table {
	int16_t    latest_snr;
	uint8_t    latest_rcpi;
	int8_t     latest_rssi;
} __packed;

struct hif_mib_extended_count_table {
	uint32_t   count_plcp_errors;
	uint32_t   count_fcs_errors;
	uint32_t   count_tx_packets;
	uint32_t   count_rx_packets;
	uint32_t   count_rx_packet_errors;
	uint32_t   count_rx_decryption_failures;
	uint32_t   count_rx_mic_failures;
	uint32_t   count_rx_no_key_failures;
	uint32_t   count_tx_multicast_frames;
	uint32_t   count_tx_frames_success;
	uint32_t   count_tx_frame_failures;
	uint32_t   count_tx_frames_retried;
	uint32_t   count_tx_frames_multi_retried;
	uint32_t   count_rx_frame_duplicates;
	uint32_t   count_rts_success;
	uint32_t   count_rts_failures;
	uint32_t   count_ack_failures;
	uint32_t   count_rx_multicast_frames;
	uint32_t   count_rx_frames_success;
	uint32_t   count_rx_cmacicv_errors;
	uint32_t   count_rx_cmac_replays;
	uint32_t   count_rx_mgmt_ccmp_replays;
	uint32_t   count_rx_bipmic_errors;
	uint32_t   count_rx_beacon;
	uint32_t   count_miss_beacon;
	uint32_t   reserved[15];
} __packed;

struct hif_mib_count_table {
	uint32_t   count_plcp_errors;
	uint32_t   count_fcs_errors;
	uint32_t   count_tx_packets;
	uint32_t   count_rx_packets;
	uint32_t   count_rx_packet_errors;
	uint32_t   count_rx_decryption_failures;
	uint32_t   count_rx_mic_failures;
	uint32_t   count_rx_no_key_failures;
	uint32_t   count_tx_multicast_frames;
	uint32_t   count_tx_frames_success;
	uint32_t   count_tx_frame_failures;
	uint32_t   count_tx_frames_retried;
	uint32_t   count_tx_frames_multi_retried;
	uint32_t   count_rx_frame_duplicates;
	uint32_t   count_rts_success;
	uint32_t   count_rts_failures;
	uint32_t   count_ack_failures;
	uint32_t   count_rx_multicast_frames;
	uint32_t   count_rx_frames_success;
	uint32_t   count_rx_cmacicv_errors;
	uint32_t   count_rx_cmac_replays;
	uint32_t   count_rx_mgmt_ccmp_replays;
	uint32_t   count_rx_bipmic_errors;
} __packed;

struct hif_mib_max_tx_power_level {
	int32_t       max_tx_power_level_rf_port1;
	int32_t       max_tx_power_level_rf_port2;
} __packed;

struct hif_mib_beacon_stats {
	int32_t     latest_tbtt_diff;
	uint32_t    reserved[4];
} __packed;

struct hif_mib_mac_address {
	uint8_t    mac_addr[ETH_ALEN];
	uint16_t   reserved;
} __packed;

struct hif_mib_dot11_max_transmit_msdu_lifetime {
	uint32_t   max_life_time;
} __packed;

struct hif_mib_dot11_max_receive_lifetime {
	uint32_t   max_life_time;
} __packed;

struct hif_mib_wep_default_key_id {
	uint8_t    wep_default_key_id;
	uint8_t    reserved[3];
} __packed;

struct hif_mib_dot11_rts_threshold {
	uint32_t   threshold;
} __packed;

struct hif_mib_slot_time {
	uint32_t   slot_time;
} __packed;

struct hif_mib_current_tx_power_level {
	int32_t   power_level;
} __packed;

struct hif_mib_non_erp_protection {
	uint8_t   use_cts_to_self:1;
	uint8_t   reserved1:7;
	uint8_t   reserved2[3];
} __packed;

enum hif_tx_mode {
	HIF_TX_MODE_MIXED                        = 0x0,
	HIF_TX_MODE_GREENFIELD                   = 0x1
};

enum hif_tmplt {
	HIF_TMPLT_PRBREQ                           = 0x0,
	HIF_TMPLT_BCN                              = 0x1,
	HIF_TMPLT_NULL                             = 0x2,
	HIF_TMPLT_QOSNUL                           = 0x3,
	HIF_TMPLT_PSPOLL                           = 0x4,
	HIF_TMPLT_PRBRES                           = 0x5,
	HIF_TMPLT_ARP                              = 0x6,
	HIF_TMPLT_NA                               = 0x7
};

#define HIF_API_MAX_TEMPLATE_FRAME_SIZE                              700

struct hif_mib_template_frame {
	uint8_t    frame_type;
	uint8_t    init_rate:7;
	uint8_t    mode:1;
	uint16_t   frame_length;
	uint8_t    frame[HIF_API_MAX_TEMPLATE_FRAME_SIZE];
} __packed;

struct hif_mib_beacon_wake_up_period {
	uint8_t    wakeup_period_min;
	uint8_t    receive_dtim:1;
	uint8_t    reserved1:7;
	uint8_t    wakeup_period_max;
	uint8_t    reserved2;
} __packed;

struct hif_mib_rcpi_rssi_threshold {
	uint8_t    detection:1;
	uint8_t    rcpi_rssi:1;
	uint8_t    upperthresh:1;
	uint8_t    lowerthresh:1;
	uint8_t    reserved:4;
	uint8_t    lower_threshold;
	uint8_t    upper_threshold;
	uint8_t    rolling_average_count;
} __packed;

#define DEFAULT_BA_MAX_RX_BUFFER_SIZE 16

struct hif_mib_block_ack_policy {
	uint8_t    block_ack_tx_tid_policy;
	uint8_t    reserved1;
	uint8_t    block_ack_rx_tid_policy;
	uint8_t    block_ack_rx_max_buffer_size;
} __packed;

struct hif_mib_override_int_rate {
	uint8_t    internal_tx_rate;
	uint8_t    non_erp_internal_tx_rate;
	uint8_t    reserved[2];
} __packed;

enum hif_mpdu_start_spacing {
	HIF_MPDU_START_SPACING_NO_RESTRIC          = 0x0,
	HIF_MPDU_START_SPACING_QUARTER             = 0x1,
	HIF_MPDU_START_SPACING_HALF                = 0x2,
	HIF_MPDU_START_SPACING_ONE                 = 0x3,
	HIF_MPDU_START_SPACING_TWO                 = 0x4,
	HIF_MPDU_START_SPACING_FOUR                = 0x5,
	HIF_MPDU_START_SPACING_EIGHT               = 0x6,
	HIF_MPDU_START_SPACING_SIXTEEN             = 0x7
};

struct hif_mib_set_association_mode {
	uint8_t    preambtype_use:1;
	uint8_t    mode:1;
	uint8_t    rateset:1;
	uint8_t    spacing:1;
	uint8_t    reserved:4;
	uint8_t    preamble_type;
	uint8_t    mixed_or_greenfield_type;
	uint8_t    mpdu_start_spacing;
	uint32_t   basic_rate_set;
} __packed;

struct hif_mib_set_uapsd_information {
	uint8_t    trig_bckgrnd:1;
	uint8_t    trig_be:1;
	uint8_t    trig_video:1;
	uint8_t    trig_voice:1;
	uint8_t    reserved1:4;
	uint8_t    deliv_bckgrnd:1;
	uint8_t    deliv_be:1;
	uint8_t    deliv_video:1;
	uint8_t    deliv_voice:1;
	uint8_t    reserved2:4;
	uint16_t   min_auto_trigger_interval;
	uint16_t   max_auto_trigger_interval;
	uint16_t   auto_trigger_step;
} __packed;

struct hif_mib_tx_rate_retry_policy {
	uint8_t    policy_index;
	uint8_t    short_retry_count;
	uint8_t    long_retry_count;
	uint8_t    first_rate_sel:2;
	uint8_t    terminate:1;
	uint8_t    count_init:1;
	uint8_t    reserved1:4;
	uint8_t    rate_recovery_count;
	uint8_t    reserved2[3];
	uint8_t    rates[12];
} __packed;

#define HIF_MIB_NUM_TX_RATE_RETRY_POLICIES    15

struct hif_mib_set_tx_rate_retry_policy {
	uint8_t    num_tx_rate_policies;
	uint8_t    reserved[3];
	struct hif_mib_tx_rate_retry_policy tx_rate_retry_policy[];
} __packed;

struct hif_mib_protected_mgmt_policy {
	uint8_t   pmf_enable:1;
	uint8_t   unpmf_allowed:1;
	uint8_t   host_enc_auth_frames:1;
	uint8_t   reserved1:5;
	uint8_t   reserved2[3];
} __packed;

struct hif_mib_set_ht_protection {
	uint8_t   dual_cts_prot:1;
	uint8_t   reserved1:7;
	uint8_t   reserved2[3];
} __packed;

struct hif_mib_keep_alive_period {
	uint16_t   keep_alive_period;
	uint8_t    reserved[2];
} __packed;

struct hif_mib_arp_keep_alive_period {
	uint16_t   arp_keep_alive_period;
	uint8_t    encr_type;
	uint8_t    reserved;
	uint8_t    sender_ipv4_address[HIF_API_IPV4_ADDRESS_SIZE];
	uint8_t    target_ipv4_address[HIF_API_IPV4_ADDRESS_SIZE];
} __packed;

struct hif_mib_inactivity_timer {
	uint8_t    min_active_time;
	uint8_t    max_active_time;
	uint16_t   reserved;
} __packed;

struct hif_mib_interface_protection {
	uint8_t   use_cts_prot:1;
	uint8_t   reserved1:7;
	uint8_t   reserved2[3];
} __packed;


#endif

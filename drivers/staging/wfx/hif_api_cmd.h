/* SPDX-License-Identifier: Apache-2.0 */
/*
 * WFx hardware interface definitions
 *
 * Copyright (c) 2018-2019, Silicon Laboratories Inc.
 */

#ifndef WFX_HIF_API_CMD_H
#define WFX_HIF_API_CMD_H

#include "hif_api_general.h"

#define HIF_NUM_AC                             4

#define HIF_API_SSID_SIZE                      API_SSID_SIZE

enum hif_requests_ids {
	HIF_REQ_ID_RESET                             = 0x0a,
	HIF_REQ_ID_READ_MIB                          = 0x05,
	HIF_REQ_ID_WRITE_MIB                         = 0x06,
	HIF_REQ_ID_START_SCAN                        = 0x07,
	HIF_REQ_ID_STOP_SCAN                         = 0x08,
	HIF_REQ_ID_TX                                = 0x04,
	HIF_REQ_ID_JOIN                              = 0x0b,
	HIF_REQ_ID_SET_PM_MODE                       = 0x10,
	HIF_REQ_ID_SET_BSS_PARAMS                    = 0x11,
	HIF_REQ_ID_ADD_KEY                           = 0x0c,
	HIF_REQ_ID_REMOVE_KEY                        = 0x0d,
	HIF_REQ_ID_EDCA_QUEUE_PARAMS                 = 0x13,
	HIF_REQ_ID_START                             = 0x17,
	HIF_REQ_ID_BEACON_TRANSMIT                   = 0x18,
	HIF_REQ_ID_UPDATE_IE                         = 0x1b,
	HIF_REQ_ID_MAP_LINK                          = 0x1c,
};

enum hif_confirmations_ids {
	HIF_CNF_ID_RESET                             = 0x0a,
	HIF_CNF_ID_READ_MIB                          = 0x05,
	HIF_CNF_ID_WRITE_MIB                         = 0x06,
	HIF_CNF_ID_START_SCAN                        = 0x07,
	HIF_CNF_ID_STOP_SCAN                         = 0x08,
	HIF_CNF_ID_TX                                = 0x04,
	HIF_CNF_ID_MULTI_TRANSMIT                    = 0x1e,
	HIF_CNF_ID_JOIN                              = 0x0b,
	HIF_CNF_ID_SET_PM_MODE                       = 0x10,
	HIF_CNF_ID_SET_BSS_PARAMS                    = 0x11,
	HIF_CNF_ID_ADD_KEY                           = 0x0c,
	HIF_CNF_ID_REMOVE_KEY                        = 0x0d,
	HIF_CNF_ID_EDCA_QUEUE_PARAMS                 = 0x13,
	HIF_CNF_ID_START                             = 0x17,
	HIF_CNF_ID_BEACON_TRANSMIT                   = 0x18,
	HIF_CNF_ID_UPDATE_IE                         = 0x1b,
	HIF_CNF_ID_MAP_LINK                          = 0x1c,
};

enum hif_indications_ids {
	HIF_IND_ID_RX                                = 0x84,
	HIF_IND_ID_SCAN_CMPL                         = 0x86,
	HIF_IND_ID_JOIN_COMPLETE                     = 0x8f,
	HIF_IND_ID_SET_PM_MODE_CMPL                  = 0x89,
	HIF_IND_ID_SUSPEND_RESUME_TX                 = 0x8c,
	HIF_IND_ID_EVENT                             = 0x85
};

union hif_commands_ids {
	enum hif_requests_ids request;
	enum hif_confirmations_ids confirmation;
	enum hif_indications_ids indication;
};

enum hif_status {
	HIF_STATUS_SUCCESS                         = 0x0,
	HIF_STATUS_FAILURE                         = 0x1,
	HIF_INVALID_PARAMETER                      = 0x2,
	HIF_STATUS_WARNING                         = 0x3,
	HIF_ERROR_UNSUPPORTED_MSG_ID               = 0x4,
	HIF_STATUS_DECRYPTFAILURE                  = 0x10,
	HIF_STATUS_MICFAILURE                      = 0x11,
	HIF_STATUS_NO_KEY_FOUND                    = 0x12,
	HIF_STATUS_RETRY_EXCEEDED                  = 0x13,
	HIF_STATUS_TX_LIFETIME_EXCEEDED            = 0x14,
	HIF_REQUEUE                                = 0x15,
	HIF_STATUS_REFUSED                         = 0x16,
	HIF_STATUS_BUSY                            = 0x17
};

struct hif_reset_flags {
	uint8_t    reset_stat:1;
	uint8_t    reset_all_int:1;
	uint8_t    reserved1:6;
	uint8_t    reserved2[3];
} __packed;

struct hif_req_reset {
	struct hif_reset_flags reset_flags;
} __packed;

struct hif_cnf_reset {
	uint32_t   status;
} __packed;

struct hif_req_read_mib {
	uint16_t   mib_id;
	uint16_t   reserved;
} __packed;

struct hif_cnf_read_mib {
	uint32_t   status;
	uint16_t   mib_id;
	uint16_t   length;
	uint8_t    mib_data[];
} __packed;

struct hif_req_write_mib {
	uint16_t   mib_id;
	uint16_t   length;
	uint8_t    mib_data[];
} __packed;

struct hif_cnf_write_mib {
	uint32_t   status;
} __packed;

struct hif_ie_flags {
	uint8_t    beacon:1;
	uint8_t    probe_resp:1;
	uint8_t    probe_req:1;
	uint8_t    reserved1:5;
	uint8_t    reserved2;
} __packed;

struct hif_ie_tlv {
	uint8_t    type;
	uint8_t    length;
	uint8_t    data[];
} __packed;

struct hif_req_update_ie {
	struct hif_ie_flags ie_flags;
	uint16_t   num_i_es;
	struct hif_ie_tlv ie[];
} __packed;

struct hif_cnf_update_ie {
	uint32_t   status;
} __packed;

struct hif_scan_type {
	uint8_t    type:1;
	uint8_t    mode:1;
	uint8_t    reserved:6;
} __packed;

struct hif_scan_flags {
	uint8_t    fbg:1;
	uint8_t    reserved1:1;
	uint8_t    pre:1;
	uint8_t    reserved2:5;
} __packed;

struct hif_auto_scan_param {
	uint16_t   interval;
	uint8_t    reserved;
	int8_t     rssi_thr;
} __packed;

struct hif_ssid_def {
	uint32_t   ssid_length;
	uint8_t    ssid[HIF_API_SSID_SIZE];
} __packed;

#define HIF_API_MAX_NB_SSIDS                           2
#define HIF_API_MAX_NB_CHANNELS                       14

struct hif_req_start_scan {
	uint8_t    band;
	struct hif_scan_type scan_type;
	struct hif_scan_flags scan_flags;
	uint8_t    max_transmit_rate;
	struct hif_auto_scan_param auto_scan_param;
	uint8_t    num_of_probe_requests;
	uint8_t    probe_delay;
	uint8_t    num_of_ssi_ds;
	uint8_t    num_of_channels;
	uint32_t   min_channel_time;
	uint32_t   max_channel_time;
	int32_t    tx_power_level;
	uint8_t    ssid_and_channel_lists[];
} __packed;

struct hif_start_scan_req_cstnbssid_body {
	uint8_t    band;
	struct hif_scan_type scan_type;
	struct hif_scan_flags scan_flags;
	uint8_t    max_transmit_rate;
	struct hif_auto_scan_param auto_scan_param;
	uint8_t    num_of_probe_requests;
	uint8_t    probe_delay;
	uint8_t    num_of_ssi_ds;
	uint8_t    num_of_channels;
	uint32_t   min_channel_time;
	uint32_t   max_channel_time;
	int32_t    tx_power_level;
	struct hif_ssid_def ssid_def[HIF_API_MAX_NB_SSIDS];
	uint8_t    channel_list[];
} __packed;

struct hif_cnf_start_scan {
	uint32_t   status;
} __packed;

struct hif_cnf_stop_scan {
	uint32_t   status;
} __packed;

enum hif_pm_mode_status {
	HIF_PM_MODE_ACTIVE                         = 0x0,
	HIF_PM_MODE_PS                             = 0x1,
	HIF_PM_MODE_UNDETERMINED                   = 0x2
};

struct hif_ind_scan_cmpl {
	uint32_t   status;
	uint8_t    pm_mode;
	uint8_t    num_channels_completed;
	uint16_t   reserved;
} __packed;

enum hif_queue_id {
	HIF_QUEUE_ID_BACKGROUND                    = 0x0,
	HIF_QUEUE_ID_BESTEFFORT                    = 0x1,
	HIF_QUEUE_ID_VIDEO                         = 0x2,
	HIF_QUEUE_ID_VOICE                         = 0x3
};

enum hif_frame_format {
	HIF_FRAME_FORMAT_NON_HT                    = 0x0,
	HIF_FRAME_FORMAT_MIXED_FORMAT_HT           = 0x1,
	HIF_FRAME_FORMAT_GF_HT_11N                 = 0x2
};

enum hif_stbc {
	HIF_STBC_NOT_ALLOWED                       = 0x0,
	HIF_STBC_ALLOWED                           = 0x1
};

struct hif_queue {
	uint8_t    queue_id:2;
	uint8_t    peer_sta_id:4;
	uint8_t    reserved:2;
} __packed;

struct hif_data_flags {
	uint8_t    more:1;
	uint8_t    fc_offset:3;
	uint8_t    reserved:4;
} __packed;

struct hif_tx_flags {
	uint8_t    start_exp:1;
	uint8_t    reserved:3;
	uint8_t    retry_policy_index:4;
} __packed;

struct hif_ht_tx_parameters {
	uint8_t    frame_format:4;
	uint8_t    fec_coding:1;
	uint8_t    short_gi:1;
	uint8_t    reserved1:1;
	uint8_t    stbc:1;
	uint8_t    reserved2;
	uint8_t    aggregation:1;
	uint8_t    reserved3:7;
	uint8_t    reserved4;
} __packed;

struct hif_req_tx {
	uint32_t   packet_id;
	uint8_t    max_tx_rate;
	struct hif_queue queue_id;
	struct hif_data_flags data_flags;
	struct hif_tx_flags tx_flags;
	uint32_t   reserved;
	uint32_t   expire_time;
	struct hif_ht_tx_parameters ht_tx_parameters;
	uint8_t    frame[];
} __packed;

enum hif_qos_ackplcy {
	HIF_QOS_ACKPLCY_NORMAL                         = 0x0,
	HIF_QOS_ACKPLCY_TXNOACK                        = 0x1,
	HIF_QOS_ACKPLCY_NOEXPACK                       = 0x2,
	HIF_QOS_ACKPLCY_BLCKACK                        = 0x3
};

struct hif_tx_result_flags {
	uint8_t    aggr:1;
	uint8_t    requeue:1;
	uint8_t    ack_policy:2;
	uint8_t    txop_limit:1;
	uint8_t    reserved1:3;
	uint8_t    reserved2;
} __packed;

struct hif_cnf_tx {
	uint32_t   status;
	uint32_t   packet_id;
	uint8_t    txed_rate;
	uint8_t    ack_failures;
	struct hif_tx_result_flags tx_result_flags;
	uint32_t   media_delay;
	uint32_t   tx_queue_delay;
} __packed;

struct hif_cnf_multi_transmit {
	uint32_t   num_tx_confs;
	struct hif_cnf_tx   tx_conf_payload[];
} __packed;

enum hif_ri_flags_encrypt {
	HIF_RI_FLAGS_UNENCRYPTED                   = 0x0,
	HIF_RI_FLAGS_WEP_ENCRYPTED                 = 0x1,
	HIF_RI_FLAGS_TKIP_ENCRYPTED                = 0x2,
	HIF_RI_FLAGS_AES_ENCRYPTED                 = 0x3,
	HIF_RI_FLAGS_WAPI_ENCRYPTED                = 0x4
};

struct hif_rx_flags {
	uint8_t    encryp:3;
	uint8_t    in_aggr:1;
	uint8_t    first_aggr:1;
	uint8_t    last_aggr:1;
	uint8_t    defrag:1;
	uint8_t    beacon:1;
	uint8_t    tim:1;
	uint8_t    bitmap:1;
	uint8_t    match_ssid:1;
	uint8_t    match_bssid:1;
	uint8_t    more:1;
	uint8_t    reserved1:1;
	uint8_t    ht:1;
	uint8_t    stbc:1;
	uint8_t    match_uc_addr:1;
	uint8_t    match_mc_addr:1;
	uint8_t    match_bc_addr:1;
	uint8_t    key_type:1;
	uint8_t    key_index:4;
	uint8_t    reserved2:1;
	uint8_t    peer_sta_id:4;
	uint8_t    reserved3:2;
	uint8_t    reserved4:1;
} __packed;

struct hif_ind_rx {
	uint32_t   status;
	uint16_t   channel_number;
	uint8_t    rxed_rate;
	uint8_t    rcpi_rssi;
	struct hif_rx_flags rx_flags;
	uint8_t    frame[];
} __packed;


struct hif_req_edca_queue_params {
	uint8_t    queue_id;
	uint8_t    reserved1;
	uint8_t    aifsn;
	uint8_t    reserved2;
	uint16_t   cw_min;
	uint16_t   cw_max;
	uint16_t   tx_op_limit;
	uint16_t   allowed_medium_time;
	uint32_t   reserved3;
} __packed;

struct hif_cnf_edca_queue_params {
	uint32_t   status;
} __packed;

enum hif_ap_mode {
	HIF_MODE_IBSS                              = 0x0,
	HIF_MODE_BSS                               = 0x1
};

enum hif_preamble {
	HIF_PREAMBLE_LONG                          = 0x0,
	HIF_PREAMBLE_SHORT                         = 0x1,
	HIF_PREAMBLE_SHORT_LONG12                  = 0x2
};

struct hif_join_flags {
	uint8_t    reserved1:2;
	uint8_t    force_no_beacon:1;
	uint8_t    force_with_ind:1;
	uint8_t    reserved2:4;
} __packed;

struct hif_req_join {
	uint8_t    mode;
	uint8_t    band;
	uint16_t   channel_number;
	uint8_t    bssid[ETH_ALEN];
	uint16_t   atim_window;
	uint8_t    preamble_type;
	uint8_t    probe_for_join;
	uint8_t    reserved;
	struct hif_join_flags join_flags;
	uint32_t   ssid_length;
	uint8_t    ssid[HIF_API_SSID_SIZE];
	uint32_t   beacon_interval;
	uint32_t   basic_rate_set;
} __packed;

struct hif_cnf_join {
	uint32_t   status;
} __packed;

struct hif_ind_join_complete {
	uint32_t   status;
} __packed;

struct hif_bss_flags {
	uint8_t    lost_count_only:1;
	uint8_t    reserved:7;
} __packed;

struct hif_req_set_bss_params {
	struct hif_bss_flags bss_flags;
	uint8_t    beacon_lost_count;
	uint16_t   aid;
	uint32_t   operational_rate_set;
} __packed;

struct hif_cnf_set_bss_params {
	uint32_t   status;
} __packed;

struct hif_pm_mode {
	uint8_t    enter_psm:1;
	uint8_t    reserved:6;
	uint8_t    fast_psm:1;
} __packed;

struct hif_req_set_pm_mode {
	struct hif_pm_mode pm_mode;
	uint8_t    fast_psm_idle_period;
	uint8_t    ap_psm_change_period;
	uint8_t    min_auto_ps_poll_period;
} __packed;

struct hif_cnf_set_pm_mode {
	uint32_t   status;
} __packed;

struct hif_ind_set_pm_mode_cmpl {
	uint32_t   status;
	uint8_t    pm_mode;
	uint8_t    reserved[3];
} __packed;


struct hif_req_start {
	uint8_t    mode;
	uint8_t    band;
	uint16_t   channel_number;
	uint32_t   reserved1;
	uint32_t   beacon_interval;
	uint8_t    dtim_period;
	uint8_t    preamble_type;
	uint8_t    reserved2;
	uint8_t    ssid_length;
	uint8_t    ssid[HIF_API_SSID_SIZE];
	uint32_t   basic_rate_set;
} __packed;

struct hif_cnf_start {
	uint32_t   status;
} __packed;

enum hif_beacon {
	HIF_BEACON_STOP                       = 0x0,
	HIF_BEACON_START                      = 0x1
};

struct hif_req_beacon_transmit {
	uint8_t    enable_beaconing;
	uint8_t    reserved[3];
} __packed;

struct hif_cnf_beacon_transmit {
	uint32_t   status;
} __packed;

enum hif_sta_map_direction {
	HIF_STA_MAP                       = 0x0,
	HIF_STA_UNMAP                     = 0x1
};

struct hif_map_link_flags {
	uint8_t    map_direction:1;
	uint8_t    mfpc:1;
	uint8_t    reserved:6;
} __packed;

struct hif_req_map_link {
	uint8_t    mac_addr[ETH_ALEN];
	struct hif_map_link_flags map_link_flags;
	uint8_t    peer_sta_id;
} __packed;

struct hif_cnf_map_link {
	uint32_t   status;
} __packed;

struct hif_suspend_resume_flags {
	uint8_t    resume:1;
	uint8_t    reserved1:2;
	uint8_t    bc_mc_only:1;
	uint8_t    reserved2:4;
	uint8_t    reserved3;
} __packed;

struct hif_ind_suspend_resume_tx {
	struct hif_suspend_resume_flags suspend_resume_flags;
	uint16_t   peer_sta_set;
} __packed;


#define MAX_KEY_ENTRIES         24
#define HIF_API_WEP_KEY_DATA_SIZE                       16
#define HIF_API_TKIP_KEY_DATA_SIZE                      16
#define HIF_API_RX_MIC_KEY_SIZE                         8
#define HIF_API_TX_MIC_KEY_SIZE                         8
#define HIF_API_AES_KEY_DATA_SIZE                       16
#define HIF_API_WAPI_KEY_DATA_SIZE                      16
#define HIF_API_MIC_KEY_DATA_SIZE                       16
#define HIF_API_IGTK_KEY_DATA_SIZE                      16
#define HIF_API_RX_SEQUENCE_COUNTER_SIZE                8
#define HIF_API_IPN_SIZE                                8

enum hif_key_type {
	HIF_KEY_TYPE_WEP_DEFAULT                   = 0x0,
	HIF_KEY_TYPE_WEP_PAIRWISE                  = 0x1,
	HIF_KEY_TYPE_TKIP_GROUP                    = 0x2,
	HIF_KEY_TYPE_TKIP_PAIRWISE                 = 0x3,
	HIF_KEY_TYPE_AES_GROUP                     = 0x4,
	HIF_KEY_TYPE_AES_PAIRWISE                  = 0x5,
	HIF_KEY_TYPE_WAPI_GROUP                    = 0x6,
	HIF_KEY_TYPE_WAPI_PAIRWISE                 = 0x7,
	HIF_KEY_TYPE_IGTK_GROUP                    = 0x8,
	HIF_KEY_TYPE_NONE                          = 0x9
};

struct hif_wep_pairwise_key {
	uint8_t    peer_address[ETH_ALEN];
	uint8_t    reserved;
	uint8_t    key_length;
	uint8_t    key_data[HIF_API_WEP_KEY_DATA_SIZE];
} __packed;

struct hif_wep_group_key {
	uint8_t    key_id;
	uint8_t    key_length;
	uint8_t    reserved[2];
	uint8_t    key_data[HIF_API_WEP_KEY_DATA_SIZE];
} __packed;

struct hif_tkip_pairwise_key {
	uint8_t    peer_address[ETH_ALEN];
	uint8_t    reserved[2];
	uint8_t    tkip_key_data[HIF_API_TKIP_KEY_DATA_SIZE];
	uint8_t    rx_mic_key[HIF_API_RX_MIC_KEY_SIZE];
	uint8_t    tx_mic_key[HIF_API_TX_MIC_KEY_SIZE];
} __packed;

struct hif_tkip_group_key {
	uint8_t    tkip_key_data[HIF_API_TKIP_KEY_DATA_SIZE];
	uint8_t    rx_mic_key[HIF_API_RX_MIC_KEY_SIZE];
	uint8_t    key_id;
	uint8_t    reserved[3];
	uint8_t    rx_sequence_counter[HIF_API_RX_SEQUENCE_COUNTER_SIZE];
} __packed;

struct hif_aes_pairwise_key {
	uint8_t    peer_address[ETH_ALEN];
	uint8_t    reserved[2];
	uint8_t    aes_key_data[HIF_API_AES_KEY_DATA_SIZE];
} __packed;

struct hif_aes_group_key {
	uint8_t    aes_key_data[HIF_API_AES_KEY_DATA_SIZE];
	uint8_t    key_id;
	uint8_t    reserved[3];
	uint8_t    rx_sequence_counter[HIF_API_RX_SEQUENCE_COUNTER_SIZE];
} __packed;

struct hif_wapi_pairwise_key {
	uint8_t    peer_address[ETH_ALEN];
	uint8_t    key_id;
	uint8_t    reserved;
	uint8_t    wapi_key_data[HIF_API_WAPI_KEY_DATA_SIZE];
	uint8_t    mic_key_data[HIF_API_MIC_KEY_DATA_SIZE];
} __packed;

struct hif_wapi_group_key {
	uint8_t    wapi_key_data[HIF_API_WAPI_KEY_DATA_SIZE];
	uint8_t    mic_key_data[HIF_API_MIC_KEY_DATA_SIZE];
	uint8_t    key_id;
	uint8_t    reserved[3];
} __packed;

struct hif_igtk_group_key {
	uint8_t    igtk_key_data[HIF_API_IGTK_KEY_DATA_SIZE];
	uint8_t    key_id;
	uint8_t    reserved[3];
	uint8_t    ipn[HIF_API_IPN_SIZE];
} __packed;

union hif_privacy_key_data {
	struct hif_wep_pairwise_key                       wep_pairwise_key;
	struct hif_wep_group_key                          wep_group_key;
	struct hif_tkip_pairwise_key                      tkip_pairwise_key;
	struct hif_tkip_group_key                         tkip_group_key;
	struct hif_aes_pairwise_key                       aes_pairwise_key;
	struct hif_aes_group_key                          aes_group_key;
	struct hif_wapi_pairwise_key                      wapi_pairwise_key;
	struct hif_wapi_group_key                         wapi_group_key;
	struct hif_igtk_group_key                         igtk_group_key;
};

struct hif_req_add_key {
	uint8_t    type;
	uint8_t    entry_index;
	uint8_t    int_id:2;
	uint8_t    reserved1:6;
	uint8_t    reserved2;
	union hif_privacy_key_data key;
} __packed;

struct hif_cnf_add_key {
	uint32_t   status;
} __packed;

struct hif_req_remove_key {
	uint8_t    entry_index;
	uint8_t    reserved[3];
} __packed;

struct hif_cnf_remove_key {
	uint32_t   status;
} __packed;

enum hif_event_ind {
	HIF_EVENT_IND_BSSLOST                      = 0x1,
	HIF_EVENT_IND_BSSREGAINED                  = 0x2,
	HIF_EVENT_IND_RCPI_RSSI                    = 0x3,
	HIF_EVENT_IND_PS_MODE_ERROR                = 0x4,
	HIF_EVENT_IND_INACTIVITY                   = 0x5
};

enum hif_ps_mode_error {
	HIF_PS_ERROR_NO_ERROR                      = 0,
	HIF_PS_ERROR_AP_NOT_RESP_TO_POLL           = 1,
	HIF_PS_ERROR_AP_NOT_RESP_TO_UAPSD_TRIGGER  = 2,
	HIF_PS_ERROR_AP_SENT_UNICAST_IN_DOZE       = 3,
	HIF_PS_ERROR_AP_NO_DATA_AFTER_TIM          = 4
};

union hif_event_data {
	uint8_t    rcpi_rssi;
	uint32_t   ps_mode_error;
	uint32_t   peer_sta_set;
};

struct hif_ind_event {
	uint32_t   event_id;
	union hif_event_data event_data;
} __packed;


#endif

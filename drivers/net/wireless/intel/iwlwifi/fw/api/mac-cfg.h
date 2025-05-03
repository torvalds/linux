/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2012-2014, 2018-2019, 2021-2025 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#ifndef __iwl_fw_api_mac_cfg_h__
#define __iwl_fw_api_mac_cfg_h__

#include "mac.h"

/**
 * enum iwl_mac_conf_subcmd_ids - mac configuration command IDs
 */
enum iwl_mac_conf_subcmd_ids {
	/**
	 * @LOW_LATENCY_CMD: &struct iwl_mac_low_latency_cmd
	 */
	LOW_LATENCY_CMD = 0x3,
	/**
	 * @CHANNEL_SWITCH_TIME_EVENT_CMD: &struct iwl_chan_switch_te_cmd
	 */
	CHANNEL_SWITCH_TIME_EVENT_CMD = 0x4,
	/**
	 * @MISSED_VAP_NOTIF: &struct iwl_missed_vap_notif
	 */
	MISSED_VAP_NOTIF = 0xFA,
	/**
	 * @SESSION_PROTECTION_CMD: &struct iwl_mvm_session_prot_cmd
	 */
	SESSION_PROTECTION_CMD = 0x5,
	/**
	 * @CANCEL_CHANNEL_SWITCH_CMD: &struct iwl_cancel_channel_switch_cmd
	 */
	CANCEL_CHANNEL_SWITCH_CMD = 0x6,
	/**
	 * @MAC_CONFIG_CMD: &struct iwl_mac_config_cmd
	 */
	MAC_CONFIG_CMD = 0x8,
	/**
	 * @LINK_CONFIG_CMD: &struct iwl_link_config_cmd
	 */
	LINK_CONFIG_CMD = 0x9,
	/**
	 * @STA_CONFIG_CMD: &struct iwl_sta_cfg_cmd
	 */
	STA_CONFIG_CMD = 0xA,
	/**
	 * @AUX_STA_CMD: &struct iwl_aux_sta_cmd
	 */
	AUX_STA_CMD = 0xB,
	/**
	 * @STA_REMOVE_CMD: &struct iwl_remove_sta_cmd
	 */
	STA_REMOVE_CMD = 0xC,
	/**
	 * @STA_DISABLE_TX_CMD: &struct iwl_mvm_sta_disable_tx_cmd
	 */
	STA_DISABLE_TX_CMD = 0xD,
	/**
	 * @ROC_CMD: &struct iwl_roc_req
	 */
	ROC_CMD = 0xE,
	/**
	 * @TWT_OPERATION_CMD: &struct iwl_twt_operation_cmd
	 */
	TWT_OPERATION_CMD = 0x10,
	/**
	 * @MISSED_BEACONS_NOTIF: &struct iwl_missed_beacons_notif
	 */
	MISSED_BEACONS_NOTIF = 0xF6,
	/**
	 * @EMLSR_TRANS_FAIL_NOTIF: &struct iwl_esr_trans_fail_notif
	 */
	EMLSR_TRANS_FAIL_NOTIF = 0xF7,
	/**
	 * @ROC_NOTIF: &struct iwl_roc_notif
	 */
	ROC_NOTIF = 0xF8,
	/**
	 * @SESSION_PROTECTION_NOTIF: &struct iwl_session_prot_notif
	 */
	SESSION_PROTECTION_NOTIF = 0xFB,

	/**
	 * @PROBE_RESPONSE_DATA_NOTIF: &struct iwl_probe_resp_data_notif
	 */
	PROBE_RESPONSE_DATA_NOTIF = 0xFC,

	/**
	 * @CHANNEL_SWITCH_START_NOTIF: &struct iwl_channel_switch_start_notif
	 */
	CHANNEL_SWITCH_START_NOTIF = 0xFF,

	/**
	 *@CHANNEL_SWITCH_ERROR_NOTIF: &struct iwl_channel_switch_error_notif
	 */
	CHANNEL_SWITCH_ERROR_NOTIF = 0xF9,
};

#define IWL_P2P_NOA_DESC_COUNT	(2)

/**
 * struct iwl_p2p_noa_attr - NOA attr contained in probe resp FW notification
 *
 * @id: attribute id
 * @len_low: length low half
 * @len_high: length high half
 * @idx: instance of NoA timing
 * @ctwin: GO's ct window and pwer save capability
 * @desc: NoA descriptor
 * @reserved: reserved for alignment purposes
 */
struct iwl_p2p_noa_attr {
	u8 id;
	u8 len_low;
	u8 len_high;
	u8 idx;
	u8 ctwin;
	struct ieee80211_p2p_noa_desc desc[IWL_P2P_NOA_DESC_COUNT];
	u8 reserved;
} __packed;

#define IWL_PROBE_RESP_DATA_NO_CSA (0xff)

/**
 * struct iwl_probe_resp_data_notif - notification with NOA and CSA counter
 *
 * @mac_id: the mac which should send the probe response
 * @noa_active: notifies if the noa attribute should be handled
 * @noa_attr: P2P NOA attribute
 * @csa_counter: current csa counter
 * @reserved: reserved for alignment purposes
 */
struct iwl_probe_resp_data_notif {
	__le32 mac_id;
	__le32 noa_active;
	struct iwl_p2p_noa_attr noa_attr;
	u8 csa_counter;
	u8 reserved[3];
} __packed; /* PROBE_RESPONSE_DATA_NTFY_API_S_VER_1 */

/**
 * struct iwl_missed_vap_notif - notification of missing vap detection
 *
 * @mac_id: the mac for which the ucode sends the notification for
 * @num_beacon_intervals_elapsed: beacons elpased with no vap profile inside
 * @profile_periodicity: beacons period to have our profile inside
 * @reserved: reserved for alignment purposes
 */
struct iwl_missed_vap_notif {
	__le32 mac_id;
	u8 num_beacon_intervals_elapsed;
	u8 profile_periodicity;
	u8 reserved[2];
} __packed; /* MISSED_VAP_NTFY_API_S_VER_1 */

/**
 * struct iwl_channel_switch_start_notif_v1 - Channel switch start notification
 *
 * @id_and_color: ID and color of the MAC
 */
struct iwl_channel_switch_start_notif_v1 {
	__le32 id_and_color;
} __packed; /* CHANNEL_SWITCH_START_NTFY_API_S_VER_1 */

/**
 * struct iwl_channel_switch_start_notif - Channel switch start notification
 *
 * @link_id: FW link id
 */
struct iwl_channel_switch_start_notif {
	__le32 link_id;
} __packed; /* CHANNEL_SWITCH_START_NTFY_API_S_VER_3 */

#define CS_ERR_COUNT_ERROR BIT(0)
#define CS_ERR_LONG_DELAY_AFTER_CS BIT(1)
#define CS_ERR_LONG_TX_BLOCK BIT(2)
#define CS_ERR_TX_BLOCK_TIMER_EXPIRED BIT(3)

/**
 * struct iwl_channel_switch_error_notif_v1 - Channel switch error notification
 *
 * @mac_id: the mac for which the ucode sends the notification for
 * @csa_err_mask: mask of channel switch error that can occur
 */
struct iwl_channel_switch_error_notif_v1 {
	__le32 mac_id;
	__le32 csa_err_mask;
} __packed; /* CHANNEL_SWITCH_ERROR_NTFY_API_S_VER_1 */

/**
 * struct iwl_channel_switch_error_notif - Channel switch error notification
 *
 * @link_id: FW link id
 * @csa_err_mask: mask of channel switch error that can occur
 */
struct iwl_channel_switch_error_notif {
	__le32 link_id;
	__le32 csa_err_mask;
} __packed; /* CHANNEL_SWITCH_ERROR_NTFY_API_S_VER_2 */

/**
 * struct iwl_cancel_channel_switch_cmd - Cancel Channel Switch command
 *
 * @id: the id of the link or mac that should cancel the channel switch
 */
struct iwl_cancel_channel_switch_cmd {
	__le32 id;
} __packed; /* MAC_CANCEL_CHANNEL_SWITCH_S_VER_1 */

/**
 * struct iwl_chan_switch_te_cmd - Channel Switch Time Event command
 *
 * @mac_id: MAC ID for channel switch
 * @action: action to perform, see &enum iwl_ctxt_action
 * @tsf: beacon tsf
 * @cs_count: channel switch count from CSA/eCSA IE
 * @cs_delayed_bcn_count: if set to N (!= 0) GO/AP can delay N beacon intervals
 *	at the new channel after the channel switch, otherwise (N == 0) expect
 *	beacon right after the channel switch.
 * @cs_mode: 1 - quiet, 0 - otherwise
 * @reserved: reserved for alignment purposes
 */
struct iwl_chan_switch_te_cmd {
	__le32 mac_id;
	__le32 action;
	__le32 tsf;
	u8 cs_count;
	u8 cs_delayed_bcn_count;
	u8 cs_mode;
	u8 reserved;
} __packed; /* MAC_CHANNEL_SWITCH_TIME_EVENT_S_VER_2 */

/**
 * struct iwl_mac_low_latency_cmd - set/clear mac to 'low-latency mode'
 *
 * @mac_id: MAC ID to whom to apply the low-latency configurations
 * @low_latency_rx: 1/0 to set/clear Rx low latency direction
 * @low_latency_tx: 1/0 to set/clear Tx low latency direction
 * @reserved: reserved for alignment purposes
 */
struct iwl_mac_low_latency_cmd {
	__le32 mac_id;
	u8 low_latency_rx;
	u8 low_latency_tx;
	__le16 reserved;
} __packed; /* MAC_LOW_LATENCY_API_S_VER_1 */

/**
 * struct iwl_mac_client_data - configuration data for client MAC context
 *
 * @is_assoc: 1 for associated state, 0 otherwise
 * @esr_transition_timeout: the timeout required by the AP for the
 *	eSR transition.
 *	Available only from version 2 of the command.
 *	This value comes from the EMLSR transition delay in the EML
 *	Capabilities subfield.
 * @medium_sync_delay: the value as it appears in P802.11be_D2.2 Figure 9-1002j.
 * @assoc_id: unique ID assigned by the AP during association
 * @reserved1: alignment
 * @data_policy: see &enum iwl_mac_data_policy
 * @reserved2: alignment
 * @ctwin: client traffic window in TU (period after TBTT when GO is present).
 *	0 indicates that there is no CT window.
 */
struct iwl_mac_client_data {
	u8 is_assoc;
	u8 esr_transition_timeout;
	__le16 medium_sync_delay;

	__le16 assoc_id;
	__le16 reserved1;
	__le16 data_policy;
	__le16 reserved2;
	__le32 ctwin;
} __packed; /* MAC_CONTEXT_CONFIG_CLIENT_DATA_API_S_VER_2 */

/**
 * struct iwl_mac_p2p_dev_data  - configuration data for P2P device MAC context
 *
 * @is_disc_extended: if set to true, P2P Device discoverability is enabled on
 *	other channels as well. This should be to true only in case that the
 *	device is discoverable and there is an active GO. Note that setting this
 *	field when not needed, will increase the number of interrupts and have
 *	effect on the platform power, as this setting opens the Rx filters on
 *	all macs.
 */
struct iwl_mac_p2p_dev_data {
	__le32 is_disc_extended;
} __packed; /* MAC_CONTEXT_CONFIG_P2P_DEV_DATA_API_S_VER_1 */

/**
 * enum iwl_mac_config_filter_flags - MAC context configuration filter flags
 *
 * @MAC_CFG_FILTER_PROMISC: accept all data frames
 * @MAC_CFG_FILTER_ACCEPT_CONTROL_AND_MGMT: pass all management and
 *	control frames to the host
 * @MAC_CFG_FILTER_ACCEPT_GRP: accept multicast frames
 * @MAC_CFG_FILTER_ACCEPT_BEACON: accept beacon frames
 * @MAC_CFG_FILTER_ACCEPT_BCAST_PROBE_RESP: accept broadcast probe response
 * @MAC_CFG_FILTER_ACCEPT_PROBE_REQ: accept probe requests
 */
enum iwl_mac_config_filter_flags {
	MAC_CFG_FILTER_PROMISC			= BIT(0),
	MAC_CFG_FILTER_ACCEPT_CONTROL_AND_MGMT	= BIT(1),
	MAC_CFG_FILTER_ACCEPT_GRP		= BIT(2),
	MAC_CFG_FILTER_ACCEPT_BEACON		= BIT(3),
	MAC_CFG_FILTER_ACCEPT_BCAST_PROBE_RESP	= BIT(4),
	MAC_CFG_FILTER_ACCEPT_PROBE_REQ		= BIT(5),
}; /* MAC_FILTER_FLAGS_MASK_E_VER_1 */

/**
 * struct iwl_mac_config_cmd - command structure to configure MAC contexts in
 *	MLD API
 * ( MAC_CONTEXT_CONFIG_CMD = 0x8 )
 *
 * @id_and_color: ID and color of the MAC
 * @action: action to perform, see &enum iwl_ctxt_action
 * @mac_type: one of &enum iwl_mac_types
 * @local_mld_addr: mld address
 * @reserved_for_local_mld_addr: reserved
 * @filter_flags: combination of &enum iwl_mac_config_filter_flags
 * @he_support: does this MAC support HE
 * @he_ap_support: HE AP enabled, "pseudo HE", no trigger frame handling
 * @eht_support: does this MAC support EHT. Requires he_support
 * @nic_not_ack_enabled: mark that the NIC doesn't support receiving
 *	ACK-enabled AGG, (i.e. both BACK and non-BACK frames in single AGG).
 *	If the NIC is not ACK_ENABLED it may use the EOF-bit in first non-0
 *	len delim to determine if AGG or single.
 * @client: client mac data
 * @p2p_dev: mac data for p2p device
 */
struct iwl_mac_config_cmd {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	__le32 id_and_color;
	__le32 action;
	/* MAC_CONTEXT_TYPE_API_E */
	__le32 mac_type;
	u8 local_mld_addr[6];
	__le16 reserved_for_local_mld_addr;
	__le32 filter_flags;
	__le16 he_support;
	__le16 he_ap_support;
	__le32 eht_support;
	__le32 nic_not_ack_enabled;
	/* MAC_CONTEXT_CONFIG_SPECIFIC_DATA_API_U_VER_2 */
	union {
		struct iwl_mac_client_data client;
		struct iwl_mac_p2p_dev_data p2p_dev;
	};
} __packed; /* MAC_CONTEXT_CONFIG_CMD_API_S_VER_2 */

/**
 * enum iwl_link_ctx_modify_flags - indicate to the fw what fields are being
 *	modified in &iwl_link_ctx_cfg_cmd
 *
 * @LINK_CONTEXT_MODIFY_ACTIVE: covers iwl_link_ctx_cfg_cmd::active
 * @LINK_CONTEXT_MODIFY_RATES_INFO: covers iwl_link_ctx_cfg_cmd::cck_rates,
 *	iwl_link_ctx_cfg_cmd::ofdm_rates,
 *	iwl_link_ctx_cfg_cmd::cck_short_preamble,
 *	iwl_link_ctx_cfg_cmd::short_slot
 * @LINK_CONTEXT_MODIFY_PROTECT_FLAGS: covers
 *	iwl_link_ctx_cfg_cmd::protection_flags
 * @LINK_CONTEXT_MODIFY_QOS_PARAMS: covers iwl_link_ctx_cfg_cmd::qos_flags,
 *	iwl_link_ctx_cfg_cmd::ac,
 * @LINK_CONTEXT_MODIFY_BEACON_TIMING: covers iwl_link_ctx_cfg_cmd::bi,
 *	iwl_link_ctx_cfg_cmd::dtim_interval,
 *	iwl_link_ctx_cfg_cmd::dtim_time,
 *	iwl_link_ctx_cfg_cmd::dtim_tsf,
 *	iwl_link_ctx_cfg_cmd::assoc_beacon_arrive_time.
 *	This flag can be set only once after assoc.
 * @LINK_CONTEXT_MODIFY_HE_PARAMS: covers
 *	iwl_link_ctx_cfg_cmd::htc_trig_based_pkt_ext
 *	iwl_link_ctx_cfg_cmd::rand_alloc_ecwmin,
 *	iwl_link_ctx_cfg_cmd::rand_alloc_ecwmax,
 *	iwl_link_ctx_cfg_cmd::trig_based_txf,
 *	iwl_link_ctx_cfg_cmd::bss_color,
 *	iwl_link_ctx_cfg_cmd::ndp_fdbk_buff_th_exp,
 *	iwl_link_ctx_cfg_cmd::ref_bssid_addr
 *	iwl_link_ctx_cfg_cmd::bssid_index,
 *	iwl_link_ctx_cfg_cmd::frame_time_rts_th.
 *	This flag can be set any time.
 * @LINK_CONTEXT_MODIFY_BSS_COLOR_DISABLE: covers
 *	iwl_link_ctx_cfg_cmd::bss_color_disable
 * @LINK_CONTEXT_MODIFY_EHT_PARAMS: covers iwl_link_ctx_cfg_cmd::puncture_mask.
 *	This flag can be set only if the MAC that this link relates to has
 *	eht_support set to true. No longer used since _VER_3 of this command.
 * @LINK_CONTEXT_MODIFY_BANDWIDTH: Covers iwl_link_ctx_cfg_cmd::modify_bandwidth.
 *	Request RX OMI to the AP to modify bandwidth of this link.
 * @LINK_CONTEXT_MODIFY_ALL: set all above flags
 */
enum iwl_link_ctx_modify_flags {
	LINK_CONTEXT_MODIFY_ACTIVE		= BIT(0),
	LINK_CONTEXT_MODIFY_RATES_INFO		= BIT(1),
	LINK_CONTEXT_MODIFY_PROTECT_FLAGS	= BIT(2),
	LINK_CONTEXT_MODIFY_QOS_PARAMS		= BIT(3),
	LINK_CONTEXT_MODIFY_BEACON_TIMING	= BIT(4),
	LINK_CONTEXT_MODIFY_HE_PARAMS		= BIT(5),
	LINK_CONTEXT_MODIFY_BSS_COLOR_DISABLE	= BIT(6),
	LINK_CONTEXT_MODIFY_EHT_PARAMS		= BIT(7),
	LINK_CONTEXT_MODIFY_BANDWIDTH		= BIT(8),
	LINK_CONTEXT_MODIFY_ALL			= 0xff,
}; /* LINK_CONTEXT_MODIFY_MASK_E_VER_1 */

/**
 * enum iwl_link_ctx_protection_flags - link protection flags
 * @LINK_PROT_FLG_TGG_PROTECT: 11g protection when transmitting OFDM frames,
 *	this will require CCK RTS/CTS2self.
 *	RTS/CTS will protect full burst time.
 * @LINK_PROT_FLG_HT_PROT: enable HT protection
 * @LINK_PROT_FLG_FAT_PROT: protect 40 MHz transmissions
 * @LINK_PROT_FLG_SELF_CTS_EN: allow CTS2self
 */
enum iwl_link_ctx_protection_flags {
	LINK_PROT_FLG_TGG_PROTECT	= BIT(0),
	LINK_PROT_FLG_HT_PROT		= BIT(1),
	LINK_PROT_FLG_FAT_PROT		= BIT(2),
	LINK_PROT_FLG_SELF_CTS_EN	= BIT(3),
}; /* LINK_PROTECT_FLAGS_E_VER_1 */

/**
 * enum iwl_link_ctx_flags - link context flags
 *
 * @LINK_FLG_BSS_COLOR_DIS: BSS color disable, don't use the BSS
 *	color for RX filter but use MAC header
 *	enabled AGG, i.e. both BACK and non-BACK frames in a single AGG
 * @LINK_FLG_MU_EDCA_CW: indicates that there is an element of MU EDCA
 *	parameter set, i.e. the backoff counters for trig-based ACs
 * @LINK_FLG_RU_2MHZ_BLOCK: indicates that 26-tone RU OFDMA transmission are
 *      not allowed (as there are OBSS that might classify such transmissions as
 *      radar pulses).
 * @LINK_FLG_NDP_FEEDBACK_ENABLED: mark support for NDP feedback and change
 *	of threshold
 */
enum iwl_link_ctx_flags {
	LINK_FLG_BSS_COLOR_DIS		= BIT(0),
	LINK_FLG_MU_EDCA_CW		= BIT(1),
	LINK_FLG_RU_2MHZ_BLOCK		= BIT(2),
	LINK_FLG_NDP_FEEDBACK_ENABLED	= BIT(3),
}; /* LINK_CONTEXT_FLAG_E_VER_1 */

/**
 * enum iwl_link_modify_bandwidth - link modify (RX OMI) bandwidth
 * @IWL_LINK_MODIFY_BW_20: request 20 MHz
 * @IWL_LINK_MODIFY_BW_40: request 40 MHz
 * @IWL_LINK_MODIFY_BW_80: request 80 MHz
 * @IWL_LINK_MODIFY_BW_160: request 160 MHz
 * @IWL_LINK_MODIFY_BW_320: request 320 MHz
 */
enum iwl_link_modify_bandwidth {
	IWL_LINK_MODIFY_BW_20,
	IWL_LINK_MODIFY_BW_40,
	IWL_LINK_MODIFY_BW_80,
	IWL_LINK_MODIFY_BW_160,
	IWL_LINK_MODIFY_BW_320,
};

/**
 * struct iwl_link_config_cmd - command structure to configure the LINK context
 *	in MLD API
 * ( LINK_CONFIG_CMD =0x9 )
 *
 * @action: action to perform, see &enum iwl_ctxt_action
 * @link_id: the id of the link that this cmd configures
 * @mac_id: interface ID. Relevant only if action is FW_CTXT_ACTION_ADD
 * @phy_id: PHY index. Can be changed only if the link was inactive
 *	(and stays inactive). If the link is active (or becomes active),
 *	this field is ignored.
 * @local_link_addr: the links MAC address. Can be changed only if the link was
 *	inactive (and stays inactive). If the link is active
 *	(or becomes active), this field is ignored.
 * @reserved_for_local_link_addr: reserved
 * @modify_mask: from &enum iwl_link_ctx_modify_flags, selects what to change.
 *	Relevant only if action is FW_CTXT_ACTION_MODIFY
 * @active: indicates whether the link is active or not
 * @listen_lmac: indicates whether the link should be allocated on the Listen
 *	Lmac or on the Main Lmac. Cannot be changed on an active Link.
 *	Relevant only for eSR.
 * @block_tx: tell the firmware that this link can't Tx. This should be used
 *	only when a link is de-activated because of CSA with mode = 1.
 *	Available since version 5.
 * @modify_bandwidth: bandwidth request value for RX OMI (see also
 *	%LINK_CONTEXT_MODIFY_BANDWIDTH), from &enum iwl_link_modify_bandwidth.
 * @reserved1: in version 2, listen_lmac became reserved
 * @cck_rates: basic rates available for CCK
 * @ofdm_rates: basic rates available for OFDM
 * @cck_short_preamble: 1 for enabling short preamble, 0 otherwise
 * @short_slot: 1 for enabling short slots, 0 otherwise
 * @protection_flags: combination of &enum iwl_link_ctx_protection_flags
 * @qos_flags: from &enum iwl_mac_qos_flags
 * @ac: one iwl_mac_qos configuration for each AC
 * @htc_trig_based_pkt_ext: default PE in 4us units
 * @rand_alloc_ecwmin: random CWmin = 2**ECWmin-1
 * @rand_alloc_ecwmax: random CWmax = 2**ECWmax-1
 * @ndp_fdbk_buff_th_exp: set exponent for the NDP feedback buffered threshold
 * @trig_based_txf: MU EDCA Parameter set for the trigger based traffic queues
 * @bi: beacon interval in TU, applicable only when associated
 * @dtim_interval: DTIM interval in TU.
 *	Relevant only for GO, otherwise this is offloaded.
 * @puncture_mask: puncture mask for EHT (removed in VER_3)
 * @frame_time_rts_th: HE duration RTS threshold, in units of 32us
 * @flags: a combination from &enum iwl_link_ctx_flags
 * @flags_mask: what of %flags have changed. Also &enum iwl_link_ctx_flags
 * Below fields are for multi-bssid:
 * @ref_bssid_addr: reference BSSID used by the AP
 * @reserved_for_ref_bssid_addr: reserved
 * @bssid_index: index of the associated VAP
 * @bss_color: 11ax AP ID that is used in the HE SIG-A to mark inter BSS frame
 * @spec_link_id: link_id as the AP knows it
 * @ul_mu_data_disable: OM Control UL MU Data Disable RX Support (bit 44) in
 *	HE MAC Capabilities information field as defined in figure 9-897 in
 *	IEEE802.11REVme-D5.0
 * @ibss_bssid_addr: bssid for ibss
 * @reserved_for_ibss_bssid_addr: reserved
 * @reserved3: reserved for future use
 */
struct iwl_link_config_cmd {
	__le32 action;
	__le32 link_id;
	__le32 mac_id;
	__le32 phy_id;
	u8 local_link_addr[6];
	__le16 reserved_for_local_link_addr;
	__le32 modify_mask;
	__le32 active;
	union {
		__le32 listen_lmac; /* only _VER_1 */
		struct {
			u8 block_tx; /* since _VER_5 */
			u8 modify_bandwidth; /* since _VER_6 */
			u8 reserved1[2];
		};
	};
	__le32 cck_rates;
	__le32 ofdm_rates;
	__le32 cck_short_preamble;
	__le32 short_slot;
	__le32 protection_flags;
	/* MAC_QOS_PARAM_API_S_VER_1 */
	__le32 qos_flags;
	struct iwl_ac_qos ac[AC_NUM + 1];
	u8 htc_trig_based_pkt_ext;
	u8 rand_alloc_ecwmin;
	u8 rand_alloc_ecwmax;
	u8 ndp_fdbk_buff_th_exp;
	struct iwl_he_backoff_conf trig_based_txf[AC_NUM];
	__le32 bi;
	__le32 dtim_interval;
	__le16 puncture_mask; /* removed in _VER_3 */
	__le16 frame_time_rts_th;
	__le32 flags;
	__le32 flags_mask; /* removed in _VER_6 */
	/* The below fields are for multi-bssid */
	u8 ref_bssid_addr[6];
	__le16 reserved_for_ref_bssid_addr;
	u8 bssid_index;
	u8 bss_color;
	u8 spec_link_id;
	u8 ul_mu_data_disable;
	u8 ibss_bssid_addr[6];
	__le16 reserved_for_ibss_bssid_addr;
	__le32 reserved3[8];
} __packed; /* LINK_CONTEXT_CONFIG_CMD_API_S_VER_1, _VER_2, _VER_3, _VER_4, _VER_5, _VER_6 */

/* Currently FW supports link ids in the range 0-3 and can have
 * at most two active links for each vif.
 */
#define IWL_FW_MAX_ACTIVE_LINKS_NUM 2
#define IWL_FW_MAX_LINK_ID 3

/**
 * enum iwl_fw_sta_type - FW station types
 * @STATION_TYPE_PEER: represents a peer - AP in BSS, a TDLS sta, a client in
 *	P2P.
 * @STATION_TYPE_BCAST_MGMT: The station used to send beacons and
 *	probe responses. Also used for traffic injection in sniffer mode
 * @STATION_TYPE_MCAST: the station used for BCAST / MCAST in GO. Will be
 *	suspended / resumed at the right timing depending on the clients'
 *	power save state and the DTIM timing
 * @STATION_TYPE_AUX: aux sta. In the FW there is no need for a special type
 *	for the aux sta, so this type is only for driver - internal use.
 */
enum iwl_fw_sta_type {
	STATION_TYPE_PEER,
	STATION_TYPE_BCAST_MGMT,
	STATION_TYPE_MCAST,
	STATION_TYPE_AUX,
}; /* STATION_TYPE_E_VER_1 */

/**
 * struct iwl_sta_cfg_cmd - cmd structure to add a peer sta to the uCode's
 *	station table
 * ( STA_CONFIG_CMD = 0xA )
 *
 * @sta_id: index of station in uCode's station table
 * @link_id: the id of the link that is used to communicate with this sta
 * @peer_mld_address: the peers mld address
 * @reserved_for_peer_mld_address: reserved
 * @peer_link_address: the address of the link that is used to communicate
 *	with this sta
 * @reserved_for_peer_link_address: reserved
 * @station_type: type of this station. See &enum iwl_fw_sta_type
 * @assoc_id: for GO only
 * @beamform_flags: beam forming controls
 * @mfp: indicates whether the STA uses management frame protection or not.
 * @mimo: indicates whether the sta uses mimo or not
 * @mimo_protection: indicates whether the sta uses mimo protection or not
 * @ack_enabled: indicates that the AP supports receiving ACK-
 *	enabled AGG, i.e. both BACK and non-BACK frames in a single AGG
 * @trig_rnd_alloc: indicates that trigger based random allocation
 *	is enabled according to UORA element existence
 * @tx_ampdu_spacing: minimum A-MPDU spacing:
 *	4 - 2us density, 5 - 4us density, 6 - 8us density, 7 - 16us density
 * @tx_ampdu_max_size: maximum A-MPDU length: 0 - 8K, 1 - 16K, 2 - 32K,
 *	3 - 64K, 4 - 128K, 5 - 256K, 6 - 512K, 7 - 1024K.
 * @sp_length: the size of the SP in actual number of frames
 * @uapsd_acs:  4 LS bits are trigger enabled ACs, 4 MS bits are the deliver
 *	enabled ACs.
 * @pkt_ext: optional, exists according to PPE-present bit in the HE/EHT-PHY
 *	capa
 * @htc_flags: which features are supported in HTC
 */
struct iwl_sta_cfg_cmd {
	__le32 sta_id;
	__le32 link_id;
	u8 peer_mld_address[ETH_ALEN];
	__le16 reserved_for_peer_mld_address;
	u8 peer_link_address[ETH_ALEN];
	__le16 reserved_for_peer_link_address;
	__le32 station_type;
	__le32 assoc_id;
	__le32 beamform_flags;
	__le32 mfp;
	__le32 mimo;
	__le32 mimo_protection;
	__le32 ack_enabled;
	__le32 trig_rnd_alloc;
	__le32 tx_ampdu_spacing;
	__le32 tx_ampdu_max_size;
	__le32 sp_length;
	__le32 uapsd_acs;
	struct iwl_he_pkt_ext_v2 pkt_ext;
	__le32 htc_flags;
} __packed; /* STA_CMD_API_S_VER_1 */

/**
 * struct iwl_aux_sta_cmd - command for AUX STA configuration
 * ( AUX_STA_CMD = 0xB )
 *
 * @sta_id: index of aux sta to configure
 * @lmac_id: ?
 * @mac_addr: mac addr of the auxilary sta
 * @reserved_for_mac_addr: reserved
 */
struct iwl_aux_sta_cmd {
	__le32 sta_id;
	__le32 lmac_id;
	u8 mac_addr[ETH_ALEN];
	__le16 reserved_for_mac_addr;

} __packed; /* AUX_STA_CMD_API_S_VER_1 */

/**
 * struct iwl_remove_sta_cmd - a cmd structure to remove a sta added by
 *	STA_CONFIG_CMD or AUX_STA_CONFIG_CMD
 * ( STA_REMOVE_CMD = 0xC )
 *
 * @sta_id: index of station to remove
 */
struct iwl_remove_sta_cmd {
	__le32 sta_id;
} __packed; /* REMOVE_STA_API_S_VER_1 */

/**
 * struct iwl_mvm_sta_disable_tx_cmd - disable / re-enable tx to a sta
 * ( STA_DISABLE_TX_CMD = 0xD )
 *
 * @sta_id: index of the station to disable tx to
 * @disable: indicates if to disable or re-enable tx
 */
struct iwl_mvm_sta_disable_tx_cmd {
	__le32 sta_id;
	__le32 disable;
} __packed; /* STA_DISABLE_TX_API_S_VER_1 */

/**
 * enum iwl_mvm_fw_esr_recommendation - FW recommendation code
 * @ESR_RECOMMEND_LEAVE: recommendation to leave EMLSR
 * @ESR_FORCE_LEAVE: force exiting EMLSR
 * @ESR_RECOMMEND_ENTER: recommendation to enter EMLSR
 */
enum iwl_mvm_fw_esr_recommendation {
	ESR_RECOMMEND_LEAVE,
	ESR_FORCE_LEAVE,
	ESR_RECOMMEND_ENTER,
}; /* ESR_MODE_RECOMMENDATION_CODE_API_E_VER_1 */

/**
 * struct iwl_esr_mode_notif_v1 - FW recommendation/force for EMLSR mode
 *
 * @action: the action to apply on EMLSR state.
 *	See &iwl_mvm_fw_esr_recommendation
 */
struct iwl_esr_mode_notif_v1 {
	__le32 action;
} __packed; /* ESR_MODE_RECOMMENDATION_NTFY_API_S_VER_1 */

/**
 * enum iwl_esr_leave_reason - reasons for leaving EMLSR mode
 *
 * @ESR_LEAVE_REASON_OMI_MU_UL_DISALLOWED: OMI MU UL disallowed
 * @ESR_LEAVE_REASON_NO_TRIG_FOR_ESR_STA: No trigger for EMLSR station
 * @ESR_LEAVE_REASON_NO_ESR_STA_IN_MU_DL: No EMLSR station in MU DL
 * @ESR_LEAVE_REASON_BAD_ACTIV_FRAME_TH: Bad activation frame threshold
 * @ESR_LEAVE_REASON_RTS_IN_DUAL_LISTEN: RTS in dual listen
 */
enum iwl_esr_leave_reason {
	ESR_LEAVE_REASON_OMI_MU_UL_DISALLOWED	= BIT(0),
	ESR_LEAVE_REASON_NO_TRIG_FOR_ESR_STA	= BIT(1),
	ESR_LEAVE_REASON_NO_ESR_STA_IN_MU_DL	= BIT(2),
	ESR_LEAVE_REASON_BAD_ACTIV_FRAME_TH	= BIT(3),
	ESR_LEAVE_REASON_RTS_IN_DUAL_LISTEN	= BIT(4),
};

/**
 * struct iwl_esr_mode_notif - FW recommendation/force for EMLSR mode
 *
 * @action: the action to apply on EMLSR state.
 *	See &iwl_mvm_fw_esr_recommendation
 * @leave_reason_mask: mask for various reasons to leave EMLSR mode.
 *	See &iwl_esr_leave_reason
 */
struct iwl_esr_mode_notif {
	__le32 action;
	__le32 leave_reason_mask;
} __packed; /* ESR_MODE_RECOMMENDATION_NTFY_API_S_VER_2 */

/**
 * struct iwl_missed_beacons_notif - sent when by the firmware upon beacon loss
 *  ( MISSED_BEACONS_NOTIF = 0xF6 )
 * @link_id: fw link ID
 * @consec_missed_beacons_since_last_rx: number of consecutive missed
 *	beacons since last RX.
 * @consec_missed_beacons: number of consecutive missed beacons
 * @other_link_id: used in EMLSR only. The fw link ID for
 *	&consec_missed_beacons_other_link. IWL_MVM_FW_LINK_ID_INVALID (0xff) if
 *	invalid.
 * @consec_missed_beacons_other_link: number of consecutive missed beacons on
 *	&other_link_id.
 */
struct iwl_missed_beacons_notif {
	__le32 link_id;
	__le32 consec_missed_beacons_since_last_rx;
	__le32 consec_missed_beacons;
	__le32 other_link_id;
	__le32 consec_missed_beacons_other_link;
} __packed; /* MISSED_BEACON_NTFY_API_S_VER_5 */

/*
 * enum iwl_esr_trans_fail_code: to be used to parse the notif below
 *
 * @ESR_TRANS_FAILED_TX_STATUS_ERROR: failed to TX EML OMN frame
 * @ESR_TRANSITION_FAILED_TX_TIMEOUT: timeout on the EML OMN frame
 * @ESR_TRANSITION_FAILED_BEACONS_NOT_HEARD: can't get a beacon on the new link
 */
enum iwl_esr_trans_fail_code {
	ESR_TRANS_FAILED_TX_STATUS_ERROR,
	ESR_TRANSITION_FAILED_TX_TIMEOUT,
	ESR_TRANSITION_FAILED_BEACONS_NOT_HEARD,
};

/**
 * struct iwl_esr_trans_fail_notif - FW reports a failure in EMLSR transition
 *
 * @link_id: the link_id that still works after the failure
 * @activation: true if the link was activated, false otherwise
 * @err_code: see &enum iwl_esr_trans_fail_code
 */
struct iwl_esr_trans_fail_notif {
	__le32 link_id;
	__le32 activation;
	__le32 err_code;
} __packed; /* ESR_TRANSITION_FAILED_NTFY_API_S_VER_1 */

/*
 * enum iwl_twt_operation_type: TWT operation in a TWT action frame
 *
 * @TWT_OPERATION_REQUEST: TWT Request
 * @TWT_OPERATION_SUGGEST: TWT Suggest
 * @TWT_OPERATION_DEMAND: TWT Demand
 * @TWT_OPERATION_GROUPING: TWT Grouping
 * @TWT_OPERATION_ACCEPT: TWT Accept
 * @TWT_OPERATION_ALTERNATE: TWT Alternate
 * @TWT_OPERATION_DICTATE: TWT Dictate
 * @TWT_OPERATION_REJECT: TWT Reject
 * @TWT_OPERATION_TEARDOWN: TWT Teardown
 * @TWT_OPERATION_UNAVAILABILITY: TWT Unavailability
 */
enum iwl_twt_operation_type {
	TWT_OPERATION_REQUEST,
	TWT_OPERATION_SUGGEST,
	TWT_OPERATION_DEMAND,
	TWT_OPERATION_GROUPING,
	TWT_OPERATION_ACCEPT,
	TWT_OPERATION_ALTERNATE,
	TWT_OPERATION_DICTATE,
	TWT_OPERATION_REJECT,
	TWT_OPERATION_TEARDOWN,
	TWT_OPERATION_UNAVAILABILITY,
	TWT_OPERATION_MAX,
}; /* TWT_OPERATION_TYPE_E_VER_1 */

/**
 * struct iwl_twt_operation_cmd - initiate a TWT session from driver
 *
 * @link_id: FW link id to initiate the TWT
 * @twt_operation: &enum iwl_twt_operation_type
 * @target_wake_time: TSF time to start the TWT
 * @interval_exponent: the exponent for the interval
 * @interval_mantissa: the mantissa for the interval
 * @minimum_wake_duration: the minimum duration for the wake period
 * @trigger: is the TWT triggered or not
 * @flow_type: is the TWT announced (0) or not (1)
 * @flow_id: the TWT flow identifier 0 - 7
 * @twt_protection: is the TWT protected
 * @ndp_paging_indicator: is ndp paging indicator set
 * @responder_pm_mode: is responder pm mode set
 * @negotiation_type: if the responder wants to doze outside the TWT SP
 * @twt_request: 1 for TWT request (STA), 0 for TWT response (AP)
 * @implicit: is TWT implicit
 * @twt_group_assignment: the TWT group assignment
 * @twt_channel: the TWT channel
 * @restricted_info_present: is this a restricted TWT
 * @dl_bitmap_valid: is DL (download) bitmap valid (restricted TWT)
 * @ul_bitmap_valid: is UL (upload) bitmap valid (restricted TWT)
 * @dl_tid_bitmap: DL TID bitmap (restricted TWT)
 * @ul_tid_bitmap: UL TID bitmap (restricted TWT)
 */
struct iwl_twt_operation_cmd {
	__le32 link_id;
	__le32 twt_operation;
	__le64 target_wake_time;
	__le32 interval_exponent;
	__le32 interval_mantissa;
	__le32 minimum_wake_duration;
	u8 trigger;
	u8 flow_type;
	u8 flow_id;
	u8 twt_protection;
	u8 ndp_paging_indicator;
	u8 responder_pm_mode;
	u8 negotiation_type;
	u8 twt_request;
	u8 implicit;
	u8 twt_group_assignment;
	u8 twt_channel;
	u8 restricted_info_present;
	u8 dl_bitmap_valid;
	u8 ul_bitmap_valid;
	u8 dl_tid_bitmap;
	u8 ul_tid_bitmap;
} __packed; /* TWT_OPERATION_API_S_VER_1 */

#endif /* __iwl_fw_api_mac_cfg_h__ */

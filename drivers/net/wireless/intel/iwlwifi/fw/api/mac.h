/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2017        Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2017        Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#ifndef __iwl_fw_api_mac_h__
#define __iwl_fw_api_mac_h__

/*
 * The first MAC indices (starting from 0) are available to the driver,
 * AUX indices follows - 1 for non-CDB, 2 for CDB.
 */
#define MAC_INDEX_AUX		4
#define MAC_INDEX_MIN_DRIVER	0
#define NUM_MAC_INDEX_DRIVER	MAC_INDEX_AUX
#define NUM_MAC_INDEX		(NUM_MAC_INDEX_DRIVER + 1)
#define NUM_MAC_INDEX_CDB	(NUM_MAC_INDEX_DRIVER + 2)

#define IWL_MVM_STATION_COUNT		16
#define IWL_MVM_INVALID_STA		0xFF

enum iwl_ac {
	AC_BK,
	AC_BE,
	AC_VI,
	AC_VO,
	AC_NUM,
};

/**
 * enum iwl_mac_protection_flags - MAC context flags
 * @MAC_PROT_FLG_TGG_PROTECT: 11g protection when transmitting OFDM frames,
 *	this will require CCK RTS/CTS2self.
 *	RTS/CTS will protect full burst time.
 * @MAC_PROT_FLG_HT_PROT: enable HT protection
 * @MAC_PROT_FLG_FAT_PROT: protect 40 MHz transmissions
 * @MAC_PROT_FLG_SELF_CTS_EN: allow CTS2self
 */
enum iwl_mac_protection_flags {
	MAC_PROT_FLG_TGG_PROTECT	= BIT(3),
	MAC_PROT_FLG_HT_PROT		= BIT(23),
	MAC_PROT_FLG_FAT_PROT		= BIT(24),
	MAC_PROT_FLG_SELF_CTS_EN	= BIT(30),
};

#define MAC_FLG_SHORT_SLOT		BIT(4)
#define MAC_FLG_SHORT_PREAMBLE		BIT(5)

/**
 * enum iwl_mac_types - Supported MAC types
 * @FW_MAC_TYPE_FIRST: lowest supported MAC type
 * @FW_MAC_TYPE_AUX: Auxiliary MAC (internal)
 * @FW_MAC_TYPE_LISTENER: monitor MAC type (?)
 * @FW_MAC_TYPE_PIBSS: Pseudo-IBSS
 * @FW_MAC_TYPE_IBSS: IBSS
 * @FW_MAC_TYPE_BSS_STA: BSS (managed) station
 * @FW_MAC_TYPE_P2P_DEVICE: P2P Device
 * @FW_MAC_TYPE_P2P_STA: P2P client
 * @FW_MAC_TYPE_GO: P2P GO
 * @FW_MAC_TYPE_TEST: ?
 * @FW_MAC_TYPE_MAX: highest support MAC type
 */
enum iwl_mac_types {
	FW_MAC_TYPE_FIRST = 1,
	FW_MAC_TYPE_AUX = FW_MAC_TYPE_FIRST,
	FW_MAC_TYPE_LISTENER,
	FW_MAC_TYPE_PIBSS,
	FW_MAC_TYPE_IBSS,
	FW_MAC_TYPE_BSS_STA,
	FW_MAC_TYPE_P2P_DEVICE,
	FW_MAC_TYPE_P2P_STA,
	FW_MAC_TYPE_GO,
	FW_MAC_TYPE_TEST,
	FW_MAC_TYPE_MAX = FW_MAC_TYPE_TEST
}; /* MAC_CONTEXT_TYPE_API_E_VER_1 */

/**
 * enum iwl_tsf_id - TSF hw timer ID
 * @TSF_ID_A: use TSF A
 * @TSF_ID_B: use TSF B
 * @TSF_ID_C: use TSF C
 * @TSF_ID_D: use TSF D
 * @NUM_TSF_IDS: number of TSF timers available
 */
enum iwl_tsf_id {
	TSF_ID_A = 0,
	TSF_ID_B = 1,
	TSF_ID_C = 2,
	TSF_ID_D = 3,
	NUM_TSF_IDS = 4,
}; /* TSF_ID_API_E_VER_1 */

/**
 * struct iwl_mac_data_ap - configuration data for AP MAC context
 * @beacon_time: beacon transmit time in system time
 * @beacon_tsf: beacon transmit time in TSF
 * @bi: beacon interval in TU
 * @reserved1: reserved
 * @dtim_interval: dtim transmit time in TU
 * @reserved2: reserved
 * @mcast_qid: queue ID for multicast traffic.
 *	NOTE: obsolete from VER2 and on
 * @beacon_template: beacon template ID
 */
struct iwl_mac_data_ap {
	__le32 beacon_time;
	__le64 beacon_tsf;
	__le32 bi;
	__le32 reserved1;
	__le32 dtim_interval;
	__le32 reserved2;
	__le32 mcast_qid;
	__le32 beacon_template;
} __packed; /* AP_MAC_DATA_API_S_VER_2 */

/**
 * struct iwl_mac_data_ibss - configuration data for IBSS MAC context
 * @beacon_time: beacon transmit time in system time
 * @beacon_tsf: beacon transmit time in TSF
 * @bi: beacon interval in TU
 * @reserved: reserved
 * @beacon_template: beacon template ID
 */
struct iwl_mac_data_ibss {
	__le32 beacon_time;
	__le64 beacon_tsf;
	__le32 bi;
	__le32 reserved;
	__le32 beacon_template;
} __packed; /* IBSS_MAC_DATA_API_S_VER_1 */

/**
 * enum iwl_mac_data_policy - policy of the data path for this MAC
 * @TWT_SUPPORTED: twt is supported
 */
enum iwl_mac_data_policy {
	TWT_SUPPORTED	= BIT(0),
};

/**
 * struct iwl_mac_data_sta - configuration data for station MAC context
 * @is_assoc: 1 for associated state, 0 otherwise
 * @dtim_time: DTIM arrival time in system time
 * @dtim_tsf: DTIM arrival time in TSF
 * @bi: beacon interval in TU, applicable only when associated
 * @reserved1: reserved
 * @dtim_interval: DTIM interval in TU, applicable only when associated
 * @data_policy: see &enum iwl_mac_data_policy
 * @listen_interval: in beacon intervals, applicable only when associated
 * @assoc_id: unique ID assigned by the AP during association
 * @assoc_beacon_arrive_time: TSF of first beacon after association
 */
struct iwl_mac_data_sta {
	__le32 is_assoc;
	__le32 dtim_time;
	__le64 dtim_tsf;
	__le32 bi;
	__le32 reserved1;
	__le32 dtim_interval;
	__le32 data_policy;
	__le32 listen_interval;
	__le32 assoc_id;
	__le32 assoc_beacon_arrive_time;
} __packed; /* STA_MAC_DATA_API_S_VER_2 */

/**
 * struct iwl_mac_data_go - configuration data for P2P GO MAC context
 * @ap: iwl_mac_data_ap struct with most config data
 * @ctwin: client traffic window in TU (period after TBTT when GO is present).
 *	0 indicates that there is no CT window.
 * @opp_ps_enabled: indicate that opportunistic PS allowed
 */
struct iwl_mac_data_go {
	struct iwl_mac_data_ap ap;
	__le32 ctwin;
	__le32 opp_ps_enabled;
} __packed; /* GO_MAC_DATA_API_S_VER_1 */

/**
 * struct iwl_mac_data_p2p_sta - configuration data for P2P client MAC context
 * @sta: iwl_mac_data_sta struct with most config data
 * @ctwin: client traffic window in TU (period after TBTT when GO is present).
 *	0 indicates that there is no CT window.
 */
struct iwl_mac_data_p2p_sta {
	struct iwl_mac_data_sta sta;
	__le32 ctwin;
} __packed; /* P2P_STA_MAC_DATA_API_S_VER_2 */

/**
 * struct iwl_mac_data_pibss - Pseudo IBSS config data
 * @stats_interval: interval in TU between statistics notifications to host.
 */
struct iwl_mac_data_pibss {
	__le32 stats_interval;
} __packed; /* PIBSS_MAC_DATA_API_S_VER_1 */

/*
 * struct iwl_mac_data_p2p_dev - configuration data for the P2P Device MAC
 * context.
 * @is_disc_extended: if set to true, P2P Device discoverability is enabled on
 *	other channels as well. This should be to true only in case that the
 *	device is discoverable and there is an active GO. Note that setting this
 *	field when not needed, will increase the number of interrupts and have
 *	effect on the platform power, as this setting opens the Rx filters on
 *	all macs.
 */
struct iwl_mac_data_p2p_dev {
	__le32 is_disc_extended;
} __packed; /* _P2P_DEV_MAC_DATA_API_S_VER_1 */

/**
 * enum iwl_mac_filter_flags - MAC context filter flags
 * @MAC_FILTER_IN_PROMISC: accept all data frames
 * @MAC_FILTER_IN_CONTROL_AND_MGMT: pass all management and
 *	control frames to the host
 * @MAC_FILTER_ACCEPT_GRP: accept multicast frames
 * @MAC_FILTER_DIS_DECRYPT: don't decrypt unicast frames
 * @MAC_FILTER_DIS_GRP_DECRYPT: don't decrypt multicast frames
 * @MAC_FILTER_IN_BEACON: transfer foreign BSS's beacons to host
 *	(in station mode when associated)
 * @MAC_FILTER_OUT_BCAST: filter out all broadcast frames
 * @MAC_FILTER_IN_CRC32: extract FCS and append it to frames
 * @MAC_FILTER_IN_PROBE_REQUEST: pass probe requests to host
 */
enum iwl_mac_filter_flags {
	MAC_FILTER_IN_PROMISC		= BIT(0),
	MAC_FILTER_IN_CONTROL_AND_MGMT	= BIT(1),
	MAC_FILTER_ACCEPT_GRP		= BIT(2),
	MAC_FILTER_DIS_DECRYPT		= BIT(3),
	MAC_FILTER_DIS_GRP_DECRYPT	= BIT(4),
	MAC_FILTER_IN_BEACON		= BIT(6),
	MAC_FILTER_OUT_BCAST		= BIT(8),
	MAC_FILTER_IN_CRC32		= BIT(11),
	MAC_FILTER_IN_PROBE_REQUEST	= BIT(12),
	/**
	 * @MAC_FILTER_IN_11AX: mark BSS as supporting 802.11ax
	 */
	MAC_FILTER_IN_11AX		= BIT(14),
};

/**
 * enum iwl_mac_qos_flags - QoS flags
 * @MAC_QOS_FLG_UPDATE_EDCA: ?
 * @MAC_QOS_FLG_TGN: HT is enabled
 * @MAC_QOS_FLG_TXOP_TYPE: ?
 *
 */
enum iwl_mac_qos_flags {
	MAC_QOS_FLG_UPDATE_EDCA	= BIT(0),
	MAC_QOS_FLG_TGN		= BIT(1),
	MAC_QOS_FLG_TXOP_TYPE	= BIT(4),
};

/**
 * struct iwl_ac_qos - QOS timing params for MAC_CONTEXT_CMD
 * @cw_min: Contention window, start value in numbers of slots.
 *	Should be a power-of-2, minus 1.  Device's default is 0x0f.
 * @cw_max: Contention window, max value in numbers of slots.
 *	Should be a power-of-2, minus 1.  Device's default is 0x3f.
 * @aifsn:  Number of slots in Arbitration Interframe Space (before
 *	performing random backoff timing prior to Tx).  Device default 1.
 * @fifos_mask: FIFOs used by this MAC for this AC
 * @edca_txop:  Length of Tx opportunity, in uSecs.  Device default is 0.
 *
 * One instance of this config struct for each of 4 EDCA access categories
 * in struct iwl_qosparam_cmd.
 *
 * Device will automatically increase contention window by (2*CW) + 1 for each
 * transmission retry.  Device uses cw_max as a bit mask, ANDed with new CW
 * value, to cap the CW value.
 */
struct iwl_ac_qos {
	__le16 cw_min;
	__le16 cw_max;
	u8 aifsn;
	u8 fifos_mask;
	__le16 edca_txop;
} __packed; /* AC_QOS_API_S_VER_2 */

/**
 * struct iwl_mac_ctx_cmd - command structure to configure MAC contexts
 * ( MAC_CONTEXT_CMD = 0x28 )
 * @id_and_color: ID and color of the MAC
 * @action: action to perform, one of FW_CTXT_ACTION_*
 * @mac_type: one of &enum iwl_mac_types
 * @tsf_id: TSF HW timer, one of &enum iwl_tsf_id
 * @node_addr: MAC address
 * @reserved_for_node_addr: reserved
 * @bssid_addr: BSSID
 * @reserved_for_bssid_addr: reserved
 * @cck_rates: basic rates available for CCK
 * @ofdm_rates: basic rates available for OFDM
 * @protection_flags: combination of &enum iwl_mac_protection_flags
 * @cck_short_preamble: 0x20 for enabling short preamble, 0 otherwise
 * @short_slot: 0x10 for enabling short slots, 0 otherwise
 * @filter_flags: combination of &enum iwl_mac_filter_flags
 * @qos_flags: from &enum iwl_mac_qos_flags
 * @ac: one iwl_mac_qos configuration for each AC
 */
struct iwl_mac_ctx_cmd {
	/* COMMON_INDEX_HDR_API_S_VER_1 */
	__le32 id_and_color;
	__le32 action;
	/* MAC_CONTEXT_COMMON_DATA_API_S_VER_1 */
	__le32 mac_type;
	__le32 tsf_id;
	u8 node_addr[6];
	__le16 reserved_for_node_addr;
	u8 bssid_addr[6];
	__le16 reserved_for_bssid_addr;
	__le32 cck_rates;
	__le32 ofdm_rates;
	__le32 protection_flags;
	__le32 cck_short_preamble;
	__le32 short_slot;
	__le32 filter_flags;
	/* MAC_QOS_PARAM_API_S_VER_1 */
	__le32 qos_flags;
	struct iwl_ac_qos ac[AC_NUM+1];
	/* MAC_CONTEXT_COMMON_DATA_API_S */
	union {
		struct iwl_mac_data_ap ap;
		struct iwl_mac_data_go go;
		struct iwl_mac_data_sta sta;
		struct iwl_mac_data_p2p_sta p2p_sta;
		struct iwl_mac_data_p2p_dev p2p_dev;
		struct iwl_mac_data_pibss pibss;
		struct iwl_mac_data_ibss ibss;
	};
} __packed; /* MAC_CONTEXT_CMD_API_S_VER_1 */

#define IWL_NONQOS_SEQ_GET	0x1
#define IWL_NONQOS_SEQ_SET	0x2
struct iwl_nonqos_seq_query_cmd {
	__le32 get_set_flag;
	__le32 mac_id_n_color;
	__le16 value;
	__le16 reserved;
} __packed; /* NON_QOS_TX_COUNTER_GET_SET_API_S_VER_1 */

/**
 * struct iwl_missed_beacons_notif - information on missed beacons
 * ( MISSED_BEACONS_NOTIFICATION = 0xa2 )
 * @mac_id: interface ID
 * @consec_missed_beacons_since_last_rx: number of consecutive missed
 *	beacons since last RX.
 * @consec_missed_beacons: number of consecutive missed beacons
 * @num_expected_beacons: number of expected beacons
 * @num_recvd_beacons: number of received beacons
 */
struct iwl_missed_beacons_notif {
	__le32 mac_id;
	__le32 consec_missed_beacons_since_last_rx;
	__le32 consec_missed_beacons;
	__le32 num_expected_beacons;
	__le32 num_recvd_beacons;
} __packed; /* MISSED_BEACON_NTFY_API_S_VER_3 */

/**
 * struct iwl_he_backoff_conf - used for backoff configuration
 * Per each trigger-based AC, (set by MU EDCA Parameter set info-element)
 * used for backoff configuration of TXF5..TXF8 trigger based.
 * The MU-TIMER is reloaded w/ MU_TIME each time a frame from the AC is sent via
 * trigger-based TX.
 * @cwmin: CW min
 * @cwmax: CW max
 * @aifsn: AIFSN
 *	AIFSN=0, means that no backoff from the specified TRIG-BASED AC is
 *	allowed till the MU-TIMER is 0
 * @mu_time: MU time in 8TU units
 */
struct iwl_he_backoff_conf {
	__le16 cwmin;
	__le16 cwmax;
	__le16 aifsn;
	__le16 mu_time;
} __packed; /* AC_QOS_DOT11AX_API_S */

/**
 * enum iwl_he_pkt_ext_constellations - PPE constellation indices
 * @IWL_HE_PKT_EXT_BPSK: BPSK
 * @IWL_HE_PKT_EXT_QPSK:  QPSK
 * @IWL_HE_PKT_EXT_16QAM: 16-QAM
 * @IWL_HE_PKT_EXT_64QAM: 64-QAM
 * @IWL_HE_PKT_EXT_256QAM: 256-QAM
 * @IWL_HE_PKT_EXT_1024QAM: 1024-QAM
 * @IWL_HE_PKT_EXT_RESERVED: reserved value
 * @IWL_HE_PKT_EXT_NONE: not defined
 */
enum iwl_he_pkt_ext_constellations {
	IWL_HE_PKT_EXT_BPSK = 0,
	IWL_HE_PKT_EXT_QPSK,
	IWL_HE_PKT_EXT_16QAM,
	IWL_HE_PKT_EXT_64QAM,
	IWL_HE_PKT_EXT_256QAM,
	IWL_HE_PKT_EXT_1024QAM,
	IWL_HE_PKT_EXT_RESERVED,
	IWL_HE_PKT_EXT_NONE,
};

#define MAX_HE_SUPP_NSS	2
#define MAX_HE_CHANNEL_BW_INDX	4

/**
 * struct iwl_he_pkt_ext - QAM thresholds
 * The required PPE is set via HE Capabilities IE, per Nss x BW x MCS
 * The IE is organized in the following way:
 * Support for Nss x BW (or RU) matrix:
 *	(0=SISO, 1=MIMO2) x (0-20MHz, 1-40MHz, 2-80MHz, 3-160MHz)
 * Each entry contains 2 QAM thresholds for 8us and 16us:
 *	0=BPSK, 1=QPSK, 2=16QAM, 3=64QAM, 4=256QAM, 5=1024QAM, 6=RES, 7=NONE
 * i.e. QAM_th1 < QAM_th2 such if TX uses QAM_tx:
 *	QAM_tx < QAM_th1            --> PPE=0us
 *	QAM_th1 <= QAM_tx < QAM_th2 --> PPE=8us
 *	QAM_th2 <= QAM_tx           --> PPE=16us
 * @pkt_ext_qam_th: QAM thresholds
 *	For each Nss/Bw define 2 QAM thrsholds (0..5)
 *	For rates below the low_th, no need for PPE
 *	For rates between low_th and high_th, need 8us PPE
 *	For rates equal or higher then the high_th, need 16us PPE
 *	Nss (0-siso, 1-mimo2) x BW (0-20MHz, 1-40MHz, 2-80MHz, 3-160MHz) x
 *		(0-low_th, 1-high_th)
 */
struct iwl_he_pkt_ext {
	u8 pkt_ext_qam_th[MAX_HE_SUPP_NSS][MAX_HE_CHANNEL_BW_INDX][2];
} __packed; /* PKT_EXT_DOT11AX_API_S */

/**
 * enum iwl_he_sta_ctxt_flags - HE STA context flags
 * @STA_CTXT_HE_REF_BSSID_VALID: ref bssid addr valid (for receiving specific
 *	control frames such as TRIG, NDPA, BACK)
 * @STA_CTXT_HE_BSS_COLOR_DIS: BSS color disable, don't use the BSS
 *	color for RX filter but use MAC header
 * @STA_CTXT_HE_PARTIAL_BSS_COLOR: partial BSS color allocation
 * @STA_CTXT_HE_32BIT_BA_BITMAP: indicates the receiver supports BA bitmap
 *	of 32-bits
 * @STA_CTXT_HE_PACKET_EXT: indicates that the packet-extension info is valid
 *	and should be used
 * @STA_CTXT_HE_TRIG_RND_ALLOC: indicates that trigger based random allocation
 *	is enabled according to UORA element existence
 * @STA_CTXT_HE_CONST_TRIG_RND_ALLOC: used for AV testing
 * @STA_CTXT_HE_ACK_ENABLED: indicates that the AP supports receiving ACK-
 *	enabled AGG, i.e. both BACK and non-BACK frames in a single AGG
 * @STA_CTXT_HE_MU_EDCA_CW: indicates that there is an element of MU EDCA
 *	parameter set, i.e. the backoff counters for trig-based ACs
 */
enum iwl_he_sta_ctxt_flags {
	STA_CTXT_HE_REF_BSSID_VALID		= BIT(4),
	STA_CTXT_HE_BSS_COLOR_DIS		= BIT(5),
	STA_CTXT_HE_PARTIAL_BSS_COLOR		= BIT(6),
	STA_CTXT_HE_32BIT_BA_BITMAP		= BIT(7),
	STA_CTXT_HE_PACKET_EXT			= BIT(8),
	STA_CTXT_HE_TRIG_RND_ALLOC		= BIT(9),
	STA_CTXT_HE_CONST_TRIG_RND_ALLOC	= BIT(10),
	STA_CTXT_HE_ACK_ENABLED			= BIT(11),
	STA_CTXT_HE_MU_EDCA_CW			= BIT(12),
};

/**
 * enum iwl_he_htc_flags - HE HTC support flags
 * @IWL_HE_HTC_SUPPORT: HE-HTC support
 * @IWL_HE_HTC_UL_MU_RESP_SCHED: HE UL MU response schedule
 *	support via A-control field
 * @IWL_HE_HTC_BSR_SUPP: BSR support in A-control field
 * @IWL_HE_HTC_OMI_SUPP: A-OMI support in A-control field
 * @IWL_HE_HTC_BQR_SUPP: A-BQR support in A-control field
 */
enum iwl_he_htc_flags {
	IWL_HE_HTC_SUPPORT			= BIT(0),
	IWL_HE_HTC_UL_MU_RESP_SCHED		= BIT(3),
	IWL_HE_HTC_BSR_SUPP			= BIT(4),
	IWL_HE_HTC_OMI_SUPP			= BIT(5),
	IWL_HE_HTC_BQR_SUPP			= BIT(6),
};

/*
 * @IWL_HE_HTC_LINK_ADAP_NO_FEEDBACK: the STA does not provide HE MFB
 * @IWL_HE_HTC_LINK_ADAP_UNSOLICITED: the STA provides only unsolicited HE MFB
 * @IWL_HE_HTC_LINK_ADAP_BOTH: the STA is capable of providing HE MFB in
 *      response to HE MRQ and if the STA provides unsolicited HE MFB
 */
#define IWL_HE_HTC_LINK_ADAP_POS		(1)
#define IWL_HE_HTC_LINK_ADAP_NO_FEEDBACK	(0)
#define IWL_HE_HTC_LINK_ADAP_UNSOLICITED	(2 << IWL_HE_HTC_LINK_ADAP_POS)
#define IWL_HE_HTC_LINK_ADAP_BOTH		(3 << IWL_HE_HTC_LINK_ADAP_POS)

/**
 * struct iwl_he_sta_context_cmd - configure FW to work with HE AP
 * @sta_id: STA id
 * @tid_limit: max num of TIDs in TX HE-SU multi-TID agg
 *	0 - bad value, 1 - multi-tid not supported, 2..8 - tid limit
 * @reserved1: reserved byte for future use
 * @reserved2: reserved byte for future use
 * @flags: see %iwl_11ax_sta_ctxt_flags
 * @ref_bssid_addr: reference BSSID used by the AP
 * @reserved0: reserved 2 bytes for aligning the ref_bssid_addr field to 8 bytes
 * @htc_flags: which features are supported in HTC
 * @frag_flags: frag support in A-MSDU
 * @frag_level: frag support level
 * @frag_max_num: max num of "open" MSDUs in the receiver (in power of 2)
 * @frag_min_size: min frag size (except last frag)
 * @pkt_ext: optional, exists according to PPE-present bit in the HE-PHY capa
 * @bss_color: 11ax AP ID that is used in the HE SIG-A to mark inter BSS frame
 * @htc_trig_based_pkt_ext: default PE in 4us units
 * @frame_time_rts_th: HE duration RTS threshold, in units of 32us
 * @rand_alloc_ecwmin: random CWmin = 2**ECWmin-1
 * @rand_alloc_ecwmax: random CWmax = 2**ECWmax-1
 * @reserved3: reserved byte for future use
 * @trig_based_txf: MU EDCA Parameter set for the trigger based traffic queues
 */
struct iwl_he_sta_context_cmd {
	u8 sta_id;
	u8 tid_limit;
	u8 reserved1;
	u8 reserved2;
	__le32 flags;

	/* The below fields are set via Multiple BSSID IE */
	u8 ref_bssid_addr[6];
	__le16 reserved0;

	/* The below fields are set via HE-capabilities IE */
	__le32 htc_flags;

	u8 frag_flags;
	u8 frag_level;
	u8 frag_max_num;
	u8 frag_min_size;

	/* The below fields are set via PPE thresholds element */
	struct iwl_he_pkt_ext pkt_ext;

	/* The below fields are set via HE-Operation IE */
	u8 bss_color;
	u8 htc_trig_based_pkt_ext;
	__le16 frame_time_rts_th;

	/* Random access parameter set (i.e. RAPS) */
	u8 rand_alloc_ecwmin;
	u8 rand_alloc_ecwmax;
	__le16 reserved3;

	/* The below fields are set via MU EDCA parameter set element */
	struct iwl_he_backoff_conf trig_based_txf[AC_NUM];
} __packed; /* STA_CONTEXT_DOT11AX_API_S */

/**
 * struct iwl_he_monitor_cmd - configure air sniffer for HE
 * @bssid: the BSSID to sniff for
 * @reserved1: reserved for dword alignment
 * @aid: the AID to track on for HE MU
 * @reserved2: reserved for future use
 */
struct iwl_he_monitor_cmd {
	u8 bssid[6];
	__le16 reserved1;
	__le16 aid;
	u8 reserved2[6];
} __packed; /* HE_AIR_SNIFFER_CONFIG_CMD_API_S_VER_1 */

#endif /* __iwl_fw_api_mac_h__ */

/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
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

#ifndef __iwl_fw_file_h__
#define __iwl_fw_file_h__

#include <linux/netdevice.h>
#include <linux/nl80211.h>

/* v1/v2 uCode file layout */
struct iwl_ucode_header {
	__le32 ver;	/* major/minor/API/serial */
	union {
		struct {
			__le32 inst_size;	/* bytes of runtime code */
			__le32 data_size;	/* bytes of runtime data */
			__le32 init_size;	/* bytes of init code */
			__le32 init_data_size;	/* bytes of init data */
			__le32 boot_size;	/* bytes of bootstrap code */
			u8 data[0];		/* in same order as sizes */
		} v1;
		struct {
			__le32 build;		/* build number */
			__le32 inst_size;	/* bytes of runtime code */
			__le32 data_size;	/* bytes of runtime data */
			__le32 init_size;	/* bytes of init code */
			__le32 init_data_size;	/* bytes of init data */
			__le32 boot_size;	/* bytes of bootstrap code */
			u8 data[0];		/* in same order as sizes */
		} v2;
	} u;
};

/*
 * new TLV uCode file layout
 *
 * The new TLV file format contains TLVs, that each specify
 * some piece of data.
 */

enum iwl_ucode_tlv_type {
	IWL_UCODE_TLV_INVALID		= 0, /* unused */
	IWL_UCODE_TLV_INST		= 1,
	IWL_UCODE_TLV_DATA		= 2,
	IWL_UCODE_TLV_INIT		= 3,
	IWL_UCODE_TLV_INIT_DATA		= 4,
	IWL_UCODE_TLV_BOOT		= 5,
	IWL_UCODE_TLV_PROBE_MAX_LEN	= 6, /* a u32 value */
	IWL_UCODE_TLV_PAN		= 7,
	IWL_UCODE_TLV_RUNT_EVTLOG_PTR	= 8,
	IWL_UCODE_TLV_RUNT_EVTLOG_SIZE	= 9,
	IWL_UCODE_TLV_RUNT_ERRLOG_PTR	= 10,
	IWL_UCODE_TLV_INIT_EVTLOG_PTR	= 11,
	IWL_UCODE_TLV_INIT_EVTLOG_SIZE	= 12,
	IWL_UCODE_TLV_INIT_ERRLOG_PTR	= 13,
	IWL_UCODE_TLV_ENHANCE_SENS_TBL	= 14,
	IWL_UCODE_TLV_PHY_CALIBRATION_SIZE = 15,
	IWL_UCODE_TLV_WOWLAN_INST	= 16,
	IWL_UCODE_TLV_WOWLAN_DATA	= 17,
	IWL_UCODE_TLV_FLAGS		= 18,
	IWL_UCODE_TLV_SEC_RT		= 19,
	IWL_UCODE_TLV_SEC_INIT		= 20,
	IWL_UCODE_TLV_SEC_WOWLAN	= 21,
	IWL_UCODE_TLV_DEF_CALIB		= 22,
	IWL_UCODE_TLV_PHY_SKU		= 23,
	IWL_UCODE_TLV_SECURE_SEC_RT	= 24,
	IWL_UCODE_TLV_SECURE_SEC_INIT	= 25,
	IWL_UCODE_TLV_SECURE_SEC_WOWLAN	= 26,
	IWL_UCODE_TLV_NUM_OF_CPU	= 27,
	IWL_UCODE_TLV_CSCHEME		= 28,
	IWL_UCODE_TLV_API_CHANGES_SET	= 29,
	IWL_UCODE_TLV_ENABLED_CAPABILITIES	= 30,
	IWL_UCODE_TLV_N_SCAN_CHANNELS		= 31,
	IWL_UCODE_TLV_SEC_RT_USNIFFER	= 34,
	IWL_UCODE_TLV_SDIO_ADMA_ADDR	= 35,
	IWL_UCODE_TLV_FW_VERSION	= 36,
	IWL_UCODE_TLV_FW_DBG_DEST	= 38,
	IWL_UCODE_TLV_FW_DBG_CONF	= 39,
	IWL_UCODE_TLV_FW_DBG_TRIGGER	= 40,
};

struct iwl_ucode_tlv {
	__le32 type;		/* see above */
	__le32 length;		/* not including type/length fields */
	u8 data[0];
};

#define IWL_TLV_UCODE_MAGIC		0x0a4c5749
#define FW_VER_HUMAN_READABLE_SZ	64

struct iwl_tlv_ucode_header {
	/*
	 * The TLV style ucode header is distinguished from
	 * the v1/v2 style header by first four bytes being
	 * zero, as such is an invalid combination of
	 * major/minor/API/serial versions.
	 */
	__le32 zero;
	__le32 magic;
	u8 human_readable[FW_VER_HUMAN_READABLE_SZ];
	/* major/minor/API/serial or major in new format */
	__le32 ver;
	__le32 build;
	__le64 ignore;
	/*
	 * The data contained herein has a TLV layout,
	 * see above for the TLV header and types.
	 * Note that each TLV is padded to a length
	 * that is a multiple of 4 for alignment.
	 */
	u8 data[0];
};

/*
 * ucode TLVs
 *
 * ability to get extension for: flags & capabilities from ucode binaries files
 */
struct iwl_ucode_api {
	__le32 api_index;
	__le32 api_flags;
} __packed;

struct iwl_ucode_capa {
	__le32 api_index;
	__le32 api_capa;
} __packed;

/**
 * enum iwl_ucode_tlv_flag - ucode API flags
 * @IWL_UCODE_TLV_FLAGS_PAN: This is PAN capable microcode; this previously
 *	was a separate TLV but moved here to save space.
 * @IWL_UCODE_TLV_FLAGS_NEWSCAN: new uCode scan behaviour on hidden SSID,
 *	treats good CRC threshold as a boolean
 * @IWL_UCODE_TLV_FLAGS_MFP: This uCode image supports MFP (802.11w).
 * @IWL_UCODE_TLV_FLAGS_P2P: This uCode image supports P2P.
 * @IWL_UCODE_TLV_FLAGS_DW_BC_TABLE: The SCD byte count table is in DWORDS
 * @IWL_UCODE_TLV_FLAGS_UAPSD_SUPPORT: This uCode image supports uAPSD
 * @IWL_UCODE_TLV_FLAGS_SHORT_BL: 16 entries of black list instead of 64 in scan
 *	offload profile config command.
 * @IWL_UCODE_TLV_FLAGS_D3_6_IPV6_ADDRS: D3 image supports up to six
 *	(rather than two) IPv6 addresses
 * @IWL_UCODE_TLV_FLAGS_NO_BASIC_SSID: not sending a probe with the SSID element
 *	from the probe request template.
 * @IWL_UCODE_TLV_FLAGS_NEW_NSOFFL_SMALL: new NS offload (small version)
 * @IWL_UCODE_TLV_FLAGS_NEW_NSOFFL_LARGE: new NS offload (large version)
 * @IWL_UCODE_TLV_FLAGS_P2P_PM: P2P client supports PM as a stand alone MAC
 * @IWL_UCODE_TLV_FLAGS_P2P_BSS_PS_DCM: support power save on BSS station and
 *	P2P client interfaces simultaneously if they are in different bindings.
 * @IWL_UCODE_TLV_FLAGS_P2P_BSS_PS_SCM: support power save on BSS station and
 *	P2P client interfaces simultaneously if they are in same bindings.
 * @IWL_UCODE_TLV_FLAGS_UAPSD_SUPPORT: General support for uAPSD
 * @IWL_UCODE_TLV_FLAGS_P2P_PS_UAPSD: P2P client supports uAPSD power save
 * @IWL_UCODE_TLV_FLAGS_BCAST_FILTERING: uCode supports broadcast filtering.
 * @IWL_UCODE_TLV_FLAGS_GO_UAPSD: AP/GO interfaces support uAPSD clients
 * @IWL_UCODE_TLV_FLAGS_EBS_SUPPORT: this uCode image supports EBS.
 */
enum iwl_ucode_tlv_flag {
	IWL_UCODE_TLV_FLAGS_PAN			= BIT(0),
	IWL_UCODE_TLV_FLAGS_NEWSCAN		= BIT(1),
	IWL_UCODE_TLV_FLAGS_MFP			= BIT(2),
	IWL_UCODE_TLV_FLAGS_P2P			= BIT(3),
	IWL_UCODE_TLV_FLAGS_DW_BC_TABLE		= BIT(4),
	IWL_UCODE_TLV_FLAGS_SHORT_BL		= BIT(7),
	IWL_UCODE_TLV_FLAGS_D3_6_IPV6_ADDRS	= BIT(10),
	IWL_UCODE_TLV_FLAGS_NO_BASIC_SSID	= BIT(12),
	IWL_UCODE_TLV_FLAGS_NEW_NSOFFL_SMALL	= BIT(15),
	IWL_UCODE_TLV_FLAGS_NEW_NSOFFL_LARGE	= BIT(16),
	IWL_UCODE_TLV_FLAGS_P2P_PM		= BIT(21),
	IWL_UCODE_TLV_FLAGS_BSS_P2P_PS_DCM	= BIT(22),
	IWL_UCODE_TLV_FLAGS_BSS_P2P_PS_SCM	= BIT(23),
	IWL_UCODE_TLV_FLAGS_UAPSD_SUPPORT	= BIT(24),
	IWL_UCODE_TLV_FLAGS_EBS_SUPPORT		= BIT(25),
	IWL_UCODE_TLV_FLAGS_P2P_PS_UAPSD	= BIT(26),
	IWL_UCODE_TLV_FLAGS_BCAST_FILTERING	= BIT(29),
	IWL_UCODE_TLV_FLAGS_GO_UAPSD		= BIT(30),
};

/**
 * enum iwl_ucode_tlv_api - ucode api
 * @IWL_UCODE_TLV_API_BT_COEX_SPLIT: new API for BT Coex
 * @IWL_UCODE_TLV_API_FRAGMENTED_SCAN: This ucode supports active dwell time
 *	longer than the passive one, which is essential for fragmented scan.
 * @IWL_UCODE_TLV_API_WIFI_MCC_UPDATE: ucode supports MCC updates with source.
 * IWL_UCODE_TLV_API_HDC_PHASE_0: ucode supports finer configuration of LTR
 * @IWL_UCODE_TLV_API_BASIC_DWELL: use only basic dwell time in scan command,
 *	regardless of the band or the number of the probes. FW will calculate
 *	the actual dwell time.
 * @IWL_UCODE_TLV_API_SCD_CFG: This firmware can configure the scheduler
 *	through the dedicated host command.
 * @IWL_UCODE_TLV_API_SINGLE_SCAN_EBS: EBS is supported for single scans too.
 * @IWL_UCODE_TLV_API_ASYNC_DTM: Async temperature notifications are supported.
 * @IWL_UCODE_TLV_API_LQ_SS_PARAMS: Configure STBC/BFER via LQ CMD ss_params
 * @IWL_UCODE_TLV_API_STATS_V10: uCode supports/uses statistics API version 10
 * @IWL_UCODE_TLV_API_NEW_VERSION: new versioning format
 */
enum iwl_ucode_tlv_api {
	IWL_UCODE_TLV_API_BT_COEX_SPLIT         = BIT(3),
	IWL_UCODE_TLV_API_FRAGMENTED_SCAN	= BIT(8),
	IWL_UCODE_TLV_API_WIFI_MCC_UPDATE	= BIT(9),
	IWL_UCODE_TLV_API_HDC_PHASE_0		= BIT(10),
	IWL_UCODE_TLV_API_BASIC_DWELL		= BIT(13),
	IWL_UCODE_TLV_API_SCD_CFG		= BIT(15),
	IWL_UCODE_TLV_API_SINGLE_SCAN_EBS	= BIT(16),
	IWL_UCODE_TLV_API_ASYNC_DTM		= BIT(17),
	IWL_UCODE_TLV_API_LQ_SS_PARAMS		= BIT(18),
	IWL_UCODE_TLV_API_STATS_V10		= BIT(19),
	IWL_UCODE_TLV_API_NEW_VERSION		= BIT(20),
};

/**
 * enum iwl_ucode_tlv_capa - ucode capabilities
 * @IWL_UCODE_TLV_CAPA_D0I3_SUPPORT: supports D0i3
 * @IWL_UCODE_TLV_CAPA_LAR_SUPPORT: supports Location Aware Regulatory
 * @IWL_UCODE_TLV_CAPA_UMAC_SCAN: supports UMAC scan.
 * @IWL_UCODE_TLV_CAPA_BEAMFORMER: supports Beamformer
 * @IWL_UCODE_TLV_CAPA_TDLS_SUPPORT: support basic TDLS functionality
 * @IWL_UCODE_TLV_CAPA_TXPOWER_INSERTION_SUPPORT: supports insertion of current
 *	tx power value into TPC Report action frame and Link Measurement Report
 *	action frame
 * @IWL_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT: supports updating current
 *	channel in DS parameter set element in probe requests.
 * @IWL_UCODE_TLV_CAPA_WFA_TPC_REP_IE_SUPPORT: supports adding TPC Report IE in
 *	probe requests.
 * @IWL_UCODE_TLV_CAPA_QUIET_PERIOD_SUPPORT: supports Quiet Period requests
 * @IWL_UCODE_TLV_CAPA_DQA_SUPPORT: supports dynamic queue allocation (DQA),
 *	which also implies support for the scheduler configuration command
 * @IWL_UCODE_TLV_CAPA_TDLS_CHANNEL_SWITCH: supports TDLS channel switching
 * @IWL_UCODE_TLV_CAPA_HOTSPOT_SUPPORT: supports Hot Spot Command
 * @IWL_UCODE_TLV_CAPA_RADIO_BEACON_STATS: support radio and beacon statistics
 * @IWL_UCODE_TLV_CAPA_BT_COEX_PLCR: enabled BT Coex packet level co-running
 * @IWL_UCODE_TLV_CAPA_LAR_MULTI_MCC: ucode supports LAR updates with different
 *	sources for the MCC. This TLV bit is a future replacement to
 *	IWL_UCODE_TLV_API_WIFI_MCC_UPDATE. When either is set, multi-source LAR
 *	is supported.
 * @IWL_UCODE_TLV_CAPA_BT_COEX_RRC: supports BT Coex RRC
 */
enum iwl_ucode_tlv_capa {
	IWL_UCODE_TLV_CAPA_D0I3_SUPPORT			= BIT(0),
	IWL_UCODE_TLV_CAPA_LAR_SUPPORT			= BIT(1),
	IWL_UCODE_TLV_CAPA_UMAC_SCAN			= BIT(2),
	IWL_UCODE_TLV_CAPA_BEAMFORMER			= BIT(3),
	IWL_UCODE_TLV_CAPA_TDLS_SUPPORT			= BIT(6),
	IWL_UCODE_TLV_CAPA_TXPOWER_INSERTION_SUPPORT	= BIT(8),
	IWL_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT	= BIT(9),
	IWL_UCODE_TLV_CAPA_WFA_TPC_REP_IE_SUPPORT	= BIT(10),
	IWL_UCODE_TLV_CAPA_QUIET_PERIOD_SUPPORT		= BIT(11),
	IWL_UCODE_TLV_CAPA_DQA_SUPPORT			= BIT(12),
	IWL_UCODE_TLV_CAPA_TDLS_CHANNEL_SWITCH		= BIT(13),
	IWL_UCODE_TLV_CAPA_HOTSPOT_SUPPORT		= BIT(18),
	IWL_UCODE_TLV_CAPA_RADIO_BEACON_STATS		= BIT(22),
	IWL_UCODE_TLV_CAPA_BT_COEX_PLCR			= BIT(28),
	IWL_UCODE_TLV_CAPA_LAR_MULTI_MCC		= BIT(29),
	IWL_UCODE_TLV_CAPA_BT_COEX_RRC			= BIT(30),
};

/* The default calibrate table size if not specified by firmware file */
#define IWL_DEFAULT_STANDARD_PHY_CALIBRATE_TBL_SIZE	18
#define IWL_MAX_STANDARD_PHY_CALIBRATE_TBL_SIZE		19
#define IWL_MAX_PHY_CALIBRATE_TBL_SIZE			253

/* The default max probe length if not specified by the firmware file */
#define IWL_DEFAULT_MAX_PROBE_LENGTH	200

/*
 * For 16.0 uCode and above, there is no differentiation between sections,
 * just an offset to the HW address.
 */
#define IWL_UCODE_SECTION_MAX 12
#define IWL_API_ARRAY_SIZE	1
#define IWL_CAPABILITIES_ARRAY_SIZE	1
#define CPU1_CPU2_SEPARATOR_SECTION	0xFFFFCCCC

/* uCode version contains 4 values: Major/Minor/API/Serial */
#define IWL_UCODE_MAJOR(ver)	(((ver) & 0xFF000000) >> 24)
#define IWL_UCODE_MINOR(ver)	(((ver) & 0x00FF0000) >> 16)
#define IWL_UCODE_API(ver)	(((ver) & 0x0000FF00) >> 8)
#define IWL_UCODE_SERIAL(ver)	((ver) & 0x000000FF)

/*
 * Calibration control struct.
 * Sent as part of the phy configuration command.
 * @flow_trigger: bitmap for which calibrations to perform according to
 *		flow triggers.
 * @event_trigger: bitmap for which calibrations to perform according to
 *		event triggers.
 */
struct iwl_tlv_calib_ctrl {
	__le32 flow_trigger;
	__le32 event_trigger;
} __packed;

enum iwl_fw_phy_cfg {
	FW_PHY_CFG_RADIO_TYPE_POS = 0,
	FW_PHY_CFG_RADIO_TYPE = 0x3 << FW_PHY_CFG_RADIO_TYPE_POS,
	FW_PHY_CFG_RADIO_STEP_POS = 2,
	FW_PHY_CFG_RADIO_STEP = 0x3 << FW_PHY_CFG_RADIO_STEP_POS,
	FW_PHY_CFG_RADIO_DASH_POS = 4,
	FW_PHY_CFG_RADIO_DASH = 0x3 << FW_PHY_CFG_RADIO_DASH_POS,
	FW_PHY_CFG_TX_CHAIN_POS = 16,
	FW_PHY_CFG_TX_CHAIN = 0xf << FW_PHY_CFG_TX_CHAIN_POS,
	FW_PHY_CFG_RX_CHAIN_POS = 20,
	FW_PHY_CFG_RX_CHAIN = 0xf << FW_PHY_CFG_RX_CHAIN_POS,
};

#define IWL_UCODE_MAX_CS		1

/**
 * struct iwl_fw_cipher_scheme - a cipher scheme supported by FW.
 * @cipher: a cipher suite selector
 * @flags: cipher scheme flags (currently reserved for a future use)
 * @hdr_len: a size of MPDU security header
 * @pn_len: a size of PN
 * @pn_off: an offset of pn from the beginning of the security header
 * @key_idx_off: an offset of key index byte in the security header
 * @key_idx_mask: a bit mask of key_idx bits
 * @key_idx_shift: bit shift needed to get key_idx
 * @mic_len: mic length in bytes
 * @hw_cipher: a HW cipher index used in host commands
 */
struct iwl_fw_cipher_scheme {
	__le32 cipher;
	u8 flags;
	u8 hdr_len;
	u8 pn_len;
	u8 pn_off;
	u8 key_idx_off;
	u8 key_idx_mask;
	u8 key_idx_shift;
	u8 mic_len;
	u8 hw_cipher;
} __packed;

enum iwl_fw_dbg_reg_operator {
	CSR_ASSIGN,
	CSR_SETBIT,
	CSR_CLEARBIT,

	PRPH_ASSIGN,
	PRPH_SETBIT,
	PRPH_CLEARBIT,
};

/**
 * struct iwl_fw_dbg_reg_op - an operation on a register
 *
 * @op: %enum iwl_fw_dbg_reg_operator
 * @addr: offset of the register
 * @val: value
 */
struct iwl_fw_dbg_reg_op {
	u8 op;
	u8 reserved[3];
	__le32 addr;
	__le32 val;
} __packed;

/**
 * enum iwl_fw_dbg_monitor_mode - available monitor recording modes
 *
 * @SMEM_MODE: monitor stores the data in SMEM
 * @EXTERNAL_MODE: monitor stores the data in allocated DRAM
 * @MARBH_MODE: monitor stores the data in MARBH buffer
 */
enum iwl_fw_dbg_monitor_mode {
	SMEM_MODE = 0,
	EXTERNAL_MODE = 1,
	MARBH_MODE = 2,
};

/**
 * struct iwl_fw_dbg_dest_tlv - configures the destination of the debug data
 *
 * @version: version of the TLV - currently 0
 * @monitor_mode: %enum iwl_fw_dbg_monitor_mode
 * @base_reg: addr of the base addr register (PRPH)
 * @end_reg:  addr of the end addr register (PRPH)
 * @write_ptr_reg: the addr of the reg of the write pointer
 * @wrap_count: the addr of the reg of the wrap_count
 * @base_shift: shift right of the base addr reg
 * @end_shift: shift right of the end addr reg
 * @reg_ops: array of registers operations
 *
 * This parses IWL_UCODE_TLV_FW_DBG_DEST
 */
struct iwl_fw_dbg_dest_tlv {
	u8 version;
	u8 monitor_mode;
	u8 reserved[2];
	__le32 base_reg;
	__le32 end_reg;
	__le32 write_ptr_reg;
	__le32 wrap_count;
	u8 base_shift;
	u8 end_shift;
	struct iwl_fw_dbg_reg_op reg_ops[0];
} __packed;

struct iwl_fw_dbg_conf_hcmd {
	u8 id;
	u8 reserved;
	__le16 len;
	u8 data[0];
} __packed;

/**
 * enum iwl_fw_dbg_trigger_mode - triggers functionalities
 *
 * @IWL_FW_DBG_TRIGGER_START: when trigger occurs re-conf the dbg mechanism
 * @IWL_FW_DBG_TRIGGER_STOP: when trigger occurs pull the dbg data
 */
enum iwl_fw_dbg_trigger_mode {
	IWL_FW_DBG_TRIGGER_START = BIT(0),
	IWL_FW_DBG_TRIGGER_STOP = BIT(1),
};

/**
 * enum iwl_fw_dbg_trigger_vif_type - define the VIF type for a trigger
 * @IWL_FW_DBG_CONF_VIF_ANY: any vif type
 * @IWL_FW_DBG_CONF_VIF_IBSS: IBSS mode
 * @IWL_FW_DBG_CONF_VIF_STATION: BSS mode
 * @IWL_FW_DBG_CONF_VIF_AP: AP mode
 * @IWL_FW_DBG_CONF_VIF_P2P_CLIENT: P2P Client mode
 * @IWL_FW_DBG_CONF_VIF_P2P_GO: P2P GO mode
 * @IWL_FW_DBG_CONF_VIF_P2P_DEVICE: P2P device
 */
enum iwl_fw_dbg_trigger_vif_type {
	IWL_FW_DBG_CONF_VIF_ANY = NL80211_IFTYPE_UNSPECIFIED,
	IWL_FW_DBG_CONF_VIF_IBSS = NL80211_IFTYPE_ADHOC,
	IWL_FW_DBG_CONF_VIF_STATION = NL80211_IFTYPE_STATION,
	IWL_FW_DBG_CONF_VIF_AP = NL80211_IFTYPE_AP,
	IWL_FW_DBG_CONF_VIF_P2P_CLIENT = NL80211_IFTYPE_P2P_CLIENT,
	IWL_FW_DBG_CONF_VIF_P2P_GO = NL80211_IFTYPE_P2P_GO,
	IWL_FW_DBG_CONF_VIF_P2P_DEVICE = NL80211_IFTYPE_P2P_DEVICE,
};

/**
 * struct iwl_fw_dbg_trigger_tlv - a TLV that describes the trigger
 * @id: %enum iwl_fw_dbg_trigger
 * @vif_type: %enum iwl_fw_dbg_trigger_vif_type
 * @stop_conf_ids: bitmap of configurations this trigger relates to.
 *	if the mode is %IWL_FW_DBG_TRIGGER_STOP, then if the bit corresponding
 *	to the currently running configuration is set, the data should be
 *	collected.
 * @stop_delay: how many milliseconds to wait before collecting the data
 *	after the STOP trigger fires.
 * @mode: %enum iwl_fw_dbg_trigger_mode - can be stop / start of both
 * @start_conf_id: if mode is %IWL_FW_DBG_TRIGGER_START, this defines what
 *	configuration should be applied when the triggers kicks in.
 * @occurrences: number of occurrences. 0 means the trigger will never fire.
 */
struct iwl_fw_dbg_trigger_tlv {
	__le32 id;
	__le32 vif_type;
	__le32 stop_conf_ids;
	__le32 stop_delay;
	u8 mode;
	u8 start_conf_id;
	__le16 occurrences;
	__le32 reserved[2];

	u8 data[0];
} __packed;

#define FW_DBG_START_FROM_ALIVE	0
#define FW_DBG_CONF_MAX		32
#define FW_DBG_INVALID		0xff

/**
 * struct iwl_fw_dbg_trigger_missed_bcon - configures trigger for missed beacons
 * @stop_consec_missed_bcon: stop recording if threshold is crossed.
 * @stop_consec_missed_bcon_since_rx: stop recording if threshold is crossed.
 * @start_consec_missed_bcon: start recording if threshold is crossed.
 * @start_consec_missed_bcon_since_rx: start recording if threshold is crossed.
 * @reserved1: reserved
 * @reserved2: reserved
 */
struct iwl_fw_dbg_trigger_missed_bcon {
	__le32 stop_consec_missed_bcon;
	__le32 stop_consec_missed_bcon_since_rx;
	__le32 reserved2[2];
	__le32 start_consec_missed_bcon;
	__le32 start_consec_missed_bcon_since_rx;
	__le32 reserved1[2];
} __packed;

/**
 * struct iwl_fw_dbg_trigger_cmd - configures trigger for messages from FW.
 * cmds: the list of commands to trigger the collection on
 */
struct iwl_fw_dbg_trigger_cmd {
	struct cmd {
		u8 cmd_id;
		u8 group_id;
	} __packed cmds[16];
} __packed;

/**
 * iwl_fw_dbg_trigger_stats - configures trigger for statistics
 * @stop_offset: the offset of the value to be monitored
 * @stop_threshold: the threshold above which to collect
 * @start_offset: the offset of the value to be monitored
 * @start_threshold: the threshold above which to start recording
 */
struct iwl_fw_dbg_trigger_stats {
	__le32 stop_offset;
	__le32 stop_threshold;
	__le32 start_offset;
	__le32 start_threshold;
} __packed;

/**
 * struct iwl_fw_dbg_trigger_low_rssi - trigger for low beacon RSSI
 * @rssi: RSSI value to trigger at
 */
struct iwl_fw_dbg_trigger_low_rssi {
	__le32 rssi;
} __packed;

/**
 * struct iwl_fw_dbg_conf_tlv - a TLV that describes a debug configuration.
 * @id: conf id
 * @usniffer: should the uSniffer image be used
 * @num_of_hcmds: how many HCMDs to send are present here
 * @hcmd: a variable length host command to be sent to apply the configuration.
 *	If there is more than one HCMD to send, they will appear one after the
 *	other and be sent in the order that they appear in.
 * This parses IWL_UCODE_TLV_FW_DBG_CONF. The user can add up-to
 * %FW_DBG_CONF_MAX configuration per run.
 */
struct iwl_fw_dbg_conf_tlv {
	u8 id;
	u8 usniffer;
	u8 reserved;
	u8 num_of_hcmds;
	struct iwl_fw_dbg_conf_hcmd hcmd;
} __packed;

#endif  /* __iwl_fw_file_h__ */

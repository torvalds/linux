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

#ifndef __iwl_fw_h__
#define __iwl_fw_h__
#include <linux/types.h>
#include <net/mac80211.h>

#include "iwl-fw-file.h"

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
 * @IWL_UCODE_TLV_API_WOWLAN_CONFIG_TID: wowlan config includes tid field.
 * @IWL_UCODE_TLV_CAPA_EXTENDED_BEACON: Support Extended beacon notification
 * @IWL_UCODE_TLV_API_BT_COEX_SPLIT: new API for BT Coex
 * @IWL_UCODE_TLV_API_CSA_FLOW: ucode can do unbind-bind flow for CSA.
 * @IWL_UCODE_TLV_API_DISABLE_STA_TX: ucode supports tx_disable bit.
 * @IWL_UCODE_TLV_API_LMAC_SCAN: This ucode uses LMAC unified scan API.
 * @IWL_UCODE_TLV_API_SF_NO_DUMMY_NOTIF: ucode supports disabling dummy notif.
 * @IWL_UCODE_TLV_API_FRAGMENTED_SCAN: This ucode supports active dwell time
 *	longer than the passive one, which is essential for fragmented scan.
 */
enum iwl_ucode_tlv_api {
	IWL_UCODE_TLV_API_WOWLAN_CONFIG_TID	= BIT(0),
	IWL_UCODE_TLV_CAPA_EXTENDED_BEACON	= BIT(1),
	IWL_UCODE_TLV_API_BT_COEX_SPLIT         = BIT(3),
	IWL_UCODE_TLV_API_CSA_FLOW		= BIT(4),
	IWL_UCODE_TLV_API_DISABLE_STA_TX	= BIT(5),
	IWL_UCODE_TLV_API_LMAC_SCAN		= BIT(6),
	IWL_UCODE_TLV_API_SF_NO_DUMMY_NOTIF	= BIT(7),
	IWL_UCODE_TLV_API_FRAGMENTED_SCAN	= BIT(8),
};

/**
 * enum iwl_ucode_tlv_capa - ucode capabilities
 * @IWL_UCODE_TLV_CAPA_D0I3_SUPPORT: supports D0i3
 * @IWL_UCODE_TLV_CAPA_TXPOWER_INSERTION_SUPPORT: supports insertion of current
 *	tx power value into TPC Report action frame and Link Measurement Report
 *	action frame
 * @IWL_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT: supports adding DS params
 *	element in probe requests.
 * @IWL_UCODE_TLV_CAPA_WFA_TPC_REP_IE_SUPPORT: supports adding TPC Report IE in
 *	probe requests.
 * @IWL_UCODE_TLV_CAPA_QUIET_PERIOD_SUPPORT: supports Quiet Period requests
 * @IWL_UCODE_TLV_CAPA_DQA_SUPPORT: supports dynamic queue allocation (DQA),
 *	which also implies support for the scheduler configuration command
 * @IWL_UCODE_TLV_CAPA_HOTSPOT_SUPPORT: supports Hot Spot Command
 */
enum iwl_ucode_tlv_capa {
	IWL_UCODE_TLV_CAPA_D0I3_SUPPORT			= BIT(0),
	IWL_UCODE_TLV_CAPA_TXPOWER_INSERTION_SUPPORT	= BIT(8),
	IWL_UCODE_TLV_CAPA_DS_PARAM_SET_IE_SUPPORT	= BIT(9),
	IWL_UCODE_TLV_CAPA_WFA_TPC_REP_IE_SUPPORT	= BIT(10),
	IWL_UCODE_TLV_CAPA_QUIET_PERIOD_SUPPORT		= BIT(11),
	IWL_UCODE_TLV_CAPA_DQA_SUPPORT			= BIT(12),
	IWL_UCODE_TLV_CAPA_HOTSPOT_SUPPORT		= BIT(18),
};

/* The default calibrate table size if not specified by firmware file */
#define IWL_DEFAULT_STANDARD_PHY_CALIBRATE_TBL_SIZE	18
#define IWL_MAX_STANDARD_PHY_CALIBRATE_TBL_SIZE		19
#define IWL_MAX_PHY_CALIBRATE_TBL_SIZE			253

/* The default max probe length if not specified by the firmware file */
#define IWL_DEFAULT_MAX_PROBE_LENGTH	200

/**
 * enum iwl_ucode_type
 *
 * The type of ucode.
 *
 * @IWL_UCODE_REGULAR: Normal runtime ucode
 * @IWL_UCODE_INIT: Initial ucode
 * @IWL_UCODE_WOWLAN: Wake on Wireless enabled ucode
 */
enum iwl_ucode_type {
	IWL_UCODE_REGULAR,
	IWL_UCODE_INIT,
	IWL_UCODE_WOWLAN,
	IWL_UCODE_TYPE_MAX,
};

/*
 * enumeration of ucode section.
 * This enumeration is used directly for older firmware (before 16.0).
 * For new firmware, there can be up to 4 sections (see below) but the
 * first one packaged into the firmware file is the DATA section and
 * some debugging code accesses that.
 */
enum iwl_ucode_sec {
	IWL_UCODE_SECTION_DATA,
	IWL_UCODE_SECTION_INST,
};
/*
 * For 16.0 uCode and above, there is no differentiation between sections,
 * just an offset to the HW address.
 */
#define IWL_UCODE_SECTION_MAX 12
#define IWL_API_ARRAY_SIZE	1
#define IWL_CAPABILITIES_ARRAY_SIZE	1
#define CPU1_CPU2_SEPARATOR_SECTION	0xFFFFCCCC

struct iwl_ucode_capabilities {
	u32 max_probe_length;
	u32 n_scan_channels;
	u32 standard_phy_calibration_size;
	u32 flags;
	u32 api[IWL_API_ARRAY_SIZE];
	u32 capa[IWL_CAPABILITIES_ARRAY_SIZE];
};

/* one for each uCode image (inst/data, init/runtime/wowlan) */
struct fw_desc {
	const void *data;	/* vmalloc'ed data */
	u32 len;		/* size in bytes */
	u32 offset;		/* offset in the device */
};

struct fw_img {
	struct fw_desc sec[IWL_UCODE_SECTION_MAX];
	bool is_secure;
	bool is_dual_cpus;
};

struct iwl_sf_region {
	u32 addr;
	u32 size;
};

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

/**
 * struct iwl_fw_cscheme_list - a cipher scheme list
 * @size: a number of entries
 * @cs: cipher scheme entries
 */
struct iwl_fw_cscheme_list {
	u8 size;
	struct iwl_fw_cipher_scheme cs[];
} __packed;

/**
 * struct iwl_fw - variables associated with the firmware
 *
 * @ucode_ver: ucode version from the ucode file
 * @fw_version: firmware version string
 * @img: ucode image like ucode_rt, ucode_init, ucode_wowlan.
 * @ucode_capa: capabilities parsed from the ucode file.
 * @enhance_sensitivity_table: device can do enhanced sensitivity.
 * @init_evtlog_ptr: event log offset for init ucode.
 * @init_evtlog_size: event log size for init ucode.
 * @init_errlog_ptr: error log offfset for init ucode.
 * @inst_evtlog_ptr: event log offset for runtime ucode.
 * @inst_evtlog_size: event log size for runtime ucode.
 * @inst_errlog_ptr: error log offfset for runtime ucode.
 * @mvm_fw: indicates this is MVM firmware
 * @cipher_scheme: optional external cipher scheme.
 */
struct iwl_fw {
	u32 ucode_ver;

	char fw_version[ETHTOOL_FWVERS_LEN];

	/* ucode images */
	struct fw_img img[IWL_UCODE_TYPE_MAX];

	struct iwl_ucode_capabilities ucode_capa;
	bool enhance_sensitivity_table;

	u32 init_evtlog_ptr, init_evtlog_size, init_errlog_ptr;
	u32 inst_evtlog_ptr, inst_evtlog_size, inst_errlog_ptr;

	struct iwl_tlv_calib_ctrl default_calib[IWL_UCODE_TYPE_MAX];
	u32 phy_config;
	u8 valid_tx_ant;
	u8 valid_rx_ant;

	bool mvm_fw;

	struct ieee80211_cipher_scheme cs[IWL_UCODE_MAX_CS];
	u8 human_readable[FW_VER_HUMAN_READABLE_SZ];
};

#endif  /* __iwl_fw_h__ */

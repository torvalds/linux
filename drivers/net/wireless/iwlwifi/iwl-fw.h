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
 * enum iwl_ucode_type
 *
 * The type of ucode.
 *
 * @IWL_UCODE_REGULAR: Normal runtime ucode
 * @IWL_UCODE_INIT: Initial ucode
 * @IWL_UCODE_WOWLAN: Wake on Wireless enabled ucode
 * @IWL_UCODE_REGULAR_USNIFFER: Normal runtime ucode when using usniffer image
 */
enum iwl_ucode_type {
	IWL_UCODE_REGULAR,
	IWL_UCODE_INIT,
	IWL_UCODE_WOWLAN,
	IWL_UCODE_REGULAR_USNIFFER,
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
	bool is_dual_cpus;
};

struct iwl_sf_region {
	u32 addr;
	u32 size;
};

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
 * @human_readable: human readable version
 * @sdio_adma_addr: the default address to set for the ADMA in SDIO mode until
 *	we get the ALIVE from the uCode
 * @dbg_dest_tlv: points to the destination TLV for debug
 * @dbg_conf_tlv: array of pointers to configuration TLVs for debug
 * @dbg_conf_tlv_len: lengths of the @dbg_conf_tlv entries
 * @dbg_dest_reg_num: num of reg_ops in %dbg_dest_tlv
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

	u32 sdio_adma_addr;

	struct iwl_fw_dbg_dest_tlv *dbg_dest_tlv;
	struct iwl_fw_dbg_conf_tlv *dbg_conf_tlv[FW_DBG_MAX];
	size_t dbg_conf_tlv_len[FW_DBG_MAX];

	u8 dbg_dest_reg_num;
};

static inline const char *get_fw_dbg_mode_string(int mode)
{
	switch (mode) {
	case SMEM_MODE:
		return "SMEM";
	case EXTERNAL_MODE:
		return "EXTERNAL_DRAM";
	case MARBH_MODE:
		return "MARBH";
	default:
		return "UNKNOWN";
	}
}

static inline const struct iwl_fw_dbg_trigger *
iwl_fw_dbg_conf_get_trigger(const struct iwl_fw *fw, u8 id)
{
	const struct iwl_fw_dbg_conf_tlv *conf_tlv = fw->dbg_conf_tlv[id];
	u8 *ptr;
	int i;

	if (!conf_tlv)
		return NULL;

	ptr = (void *)&conf_tlv->hcmd;
	for (i = 0; i < conf_tlv->num_of_hcmds; i++) {
		ptr += sizeof(conf_tlv->hcmd);
		ptr += le16_to_cpu(conf_tlv->hcmd.len);
	}

	return (const struct iwl_fw_dbg_trigger *)ptr;
}

static inline bool
iwl_fw_dbg_conf_enabled(const struct iwl_fw *fw, u8 id)
{
	const struct iwl_fw_dbg_trigger *trigger =
		iwl_fw_dbg_conf_get_trigger(fw, id);

	if (!trigger)
		return false;

	return trigger->enabled;
}

static inline bool
iwl_fw_dbg_conf_usniffer(const struct iwl_fw *fw, u8 id)
{
	const struct iwl_fw_dbg_conf_tlv *conf_tlv = fw->dbg_conf_tlv[id];

	if (!conf_tlv)
		return false;

	return conf_tlv->usniffer;
}

#endif  /* __iwl_fw_h__ */

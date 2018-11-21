/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016        Intel Deutschland GmbH
 * Copyright(c) 2018 Intel Corporation
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
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016        Intel Deutschland GmbH
 * Copyright(c) 2018 Intel Corporation
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

#ifndef __iwl_fw_img_h__
#define __iwl_fw_img_h__
#include <linux/types.h>

#include "file.h"
#include "error-dump.h"

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
	unsigned long _api[BITS_TO_LONGS(NUM_IWL_UCODE_TLV_API)];
	unsigned long _capa[BITS_TO_LONGS(NUM_IWL_UCODE_TLV_CAPA)];
};

static inline bool
fw_has_api(const struct iwl_ucode_capabilities *capabilities,
	   iwl_ucode_tlv_api_t api)
{
	return test_bit((__force long)api, capabilities->_api);
}

static inline bool
fw_has_capa(const struct iwl_ucode_capabilities *capabilities,
	    iwl_ucode_tlv_capa_t capa)
{
	return test_bit((__force long)capa, capabilities->_capa);
}

/* one for each uCode image (inst/data, init/runtime/wowlan) */
struct fw_desc {
	const void *data;	/* vmalloc'ed data */
	u32 len;		/* size in bytes */
	u32 offset;		/* offset in the device */
};

struct fw_img {
	struct fw_desc *sec;
	int num_sec;
	bool is_dual_cpus;
	u32 paging_mem_size;
};

/*
 * Block paging calculations
 */
#define PAGE_2_EXP_SIZE 12 /* 4K == 2^12 */
#define FW_PAGING_SIZE BIT(PAGE_2_EXP_SIZE) /* page size is 4KB */
#define PAGE_PER_GROUP_2_EXP_SIZE 3
/* 8 pages per group */
#define NUM_OF_PAGE_PER_GROUP BIT(PAGE_PER_GROUP_2_EXP_SIZE)
/* don't change, support only 32KB size */
#define PAGING_BLOCK_SIZE (NUM_OF_PAGE_PER_GROUP * FW_PAGING_SIZE)
/* 32K == 2^15 */
#define BLOCK_2_EXP_SIZE (PAGE_2_EXP_SIZE + PAGE_PER_GROUP_2_EXP_SIZE)

/*
 * Image paging calculations
 */
#define BLOCK_PER_IMAGE_2_EXP_SIZE 5
/* 2^5 == 32 blocks per image */
#define NUM_OF_BLOCK_PER_IMAGE BIT(BLOCK_PER_IMAGE_2_EXP_SIZE)
/* maximum image size 1024KB */
#define MAX_PAGING_IMAGE_SIZE (NUM_OF_BLOCK_PER_IMAGE * PAGING_BLOCK_SIZE)

/* Virtual address signature */
#define PAGING_ADDR_SIG 0xAA000000

#define PAGING_CMD_IS_SECURED BIT(9)
#define PAGING_CMD_IS_ENABLED BIT(8)
#define PAGING_CMD_NUM_OF_PAGES_IN_LAST_GRP_POS	0
#define PAGING_TLV_SECURE_MASK 1

/**
 * struct iwl_fw_paging
 * @fw_paging_phys: page phy pointer
 * @fw_paging_block: pointer to the allocated block
 * @fw_paging_size: page size
 */
struct iwl_fw_paging {
	dma_addr_t fw_paging_phys;
	struct page *fw_paging_block;
	u32 fw_paging_size;
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
 * struct iwl_gscan_capabilities - gscan capabilities supported by FW
 * @max_scan_cache_size: total space allocated for scan results (in bytes).
 * @max_scan_buckets: maximum number of channel buckets.
 * @max_ap_cache_per_scan: maximum number of APs that can be stored per scan.
 * @max_rssi_sample_size: number of RSSI samples used for averaging RSSI.
 * @max_scan_reporting_threshold: max possible report threshold. in percentage.
 * @max_hotlist_aps: maximum number of entries for hotlist APs.
 * @max_significant_change_aps: maximum number of entries for significant
 *	change APs.
 * @max_bssid_history_entries: number of BSSID/RSSI entries that the device can
 *	hold.
 * @max_hotlist_ssids: maximum number of entries for hotlist SSIDs.
 * @max_number_epno_networks: max number of epno entries.
 * @max_number_epno_networks_by_ssid: max number of epno entries if ssid is
 *	specified.
 * @max_number_of_white_listed_ssid: max number of white listed SSIDs.
 * @max_number_of_black_listed_ssid: max number of black listed SSIDs.
 */
struct iwl_gscan_capabilities {
	u32 max_scan_cache_size;
	u32 max_scan_buckets;
	u32 max_ap_cache_per_scan;
	u32 max_rssi_sample_size;
	u32 max_scan_reporting_threshold;
	u32 max_hotlist_aps;
	u32 max_significant_change_aps;
	u32 max_bssid_history_entries;
	u32 max_hotlist_ssids;
	u32 max_number_epno_networks;
	u32 max_number_epno_networks_by_ssid;
	u32 max_number_of_white_listed_ssid;
	u32 max_number_of_black_listed_ssid;
};

/**
 * enum iwl_fw_type - iwlwifi firmware type
 * @IWL_FW_DVM: DVM firmware
 * @IWL_FW_MVM: MVM firmware
 */
enum iwl_fw_type {
	IWL_FW_DVM,
	IWL_FW_MVM,
};

/**
 * struct iwl_fw - variables associated with the firmware
 *
 * @ucode_ver: ucode version from the ucode file
 * @fw_version: firmware version string
 * @img: ucode image like ucode_rt, ucode_init, ucode_wowlan.
 * @iml_len: length of the image loader image
 * @iml: image loader fw image
 * @ucode_capa: capabilities parsed from the ucode file.
 * @enhance_sensitivity_table: device can do enhanced sensitivity.
 * @init_evtlog_ptr: event log offset for init ucode.
 * @init_evtlog_size: event log size for init ucode.
 * @init_errlog_ptr: error log offfset for init ucode.
 * @inst_evtlog_ptr: event log offset for runtime ucode.
 * @inst_evtlog_size: event log size for runtime ucode.
 * @inst_errlog_ptr: error log offfset for runtime ucode.
 * @type: firmware type (&enum iwl_fw_type)
 * @cipher_scheme: optional external cipher scheme.
 * @human_readable: human readable version
 *	we get the ALIVE from the uCode
 * @dbg_dest_tlv: points to the destination TLV for debug
 * @dbg_conf_tlv: array of pointers to configuration TLVs for debug
 * @dbg_conf_tlv_len: lengths of the @dbg_conf_tlv entries
 * @dbg_trigger_tlv: array of pointers to triggers TLVs
 * @dbg_trigger_tlv_len: lengths of the @dbg_trigger_tlv entries
 * @dbg_dest_reg_num: num of reg_ops in %dbg_dest_tlv
 */
struct iwl_fw {
	u32 ucode_ver;

	char fw_version[ETHTOOL_FWVERS_LEN];

	/* ucode images */
	struct fw_img img[IWL_UCODE_TYPE_MAX];
	size_t iml_len;
	u8 *iml;

	struct iwl_ucode_capabilities ucode_capa;
	bool enhance_sensitivity_table;

	u32 init_evtlog_ptr, init_evtlog_size, init_errlog_ptr;
	u32 inst_evtlog_ptr, inst_evtlog_size, inst_errlog_ptr;

	struct iwl_tlv_calib_ctrl default_calib[IWL_UCODE_TYPE_MAX];
	u32 phy_config;
	u8 valid_tx_ant;
	u8 valid_rx_ant;

	enum iwl_fw_type type;

	struct iwl_fw_cipher_scheme cs[IWL_UCODE_MAX_CS];
	u8 human_readable[FW_VER_HUMAN_READABLE_SZ];

	struct iwl_fw_dbg_dest_tlv_v1 *dbg_dest_tlv;
	struct iwl_fw_dbg_conf_tlv *dbg_conf_tlv[FW_DBG_CONF_MAX];
	size_t dbg_conf_tlv_len[FW_DBG_CONF_MAX];
	struct iwl_fw_dbg_trigger_tlv *dbg_trigger_tlv[FW_DBG_TRIGGER_MAX];
	struct iwl_fw_dbg_mem_seg_tlv *dbg_mem_tlv;
	size_t n_dbg_mem_tlv;
	size_t dbg_trigger_tlv_len[FW_DBG_TRIGGER_MAX];
	u8 dbg_dest_reg_num;
	struct iwl_gscan_capabilities gscan_capa;
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
	case MIPI_MODE:
		return "MIPI";
	default:
		return "UNKNOWN";
	}
}

static inline bool
iwl_fw_dbg_conf_usniffer(const struct iwl_fw *fw, u8 id)
{
	const struct iwl_fw_dbg_conf_tlv *conf_tlv = fw->dbg_conf_tlv[id];

	if (!conf_tlv)
		return false;

	return conf_tlv->usniffer;
}

static inline const struct fw_img *
iwl_get_ucode_image(const struct iwl_fw *fw, enum iwl_ucode_type ucode_type)
{
	if (ucode_type >= IWL_UCODE_TYPE_MAX)
		return NULL;

	return &fw->img[ucode_type];
}

#endif  /* __iwl_fw_img_h__ */

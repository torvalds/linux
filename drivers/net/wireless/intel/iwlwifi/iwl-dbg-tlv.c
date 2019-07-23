/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright (C) 2018 - 2019 Intel Corporation
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
 * along with this program.
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
 * Copyright (C) 2018 - 2019 Intel Corporation
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
 *
 *****************************************************************************/

#include <linux/firmware.h>
#include "iwl-drv.h"
#include "iwl-trans.h"
#include "iwl-dbg-tlv.h"
#include "fw/dbg.h"
#include "fw/runtime.h"

/**
 * enum iwl_dbg_tlv_type - debug TLV types
 * @IWL_DBG_TLV_TYPE_DEBUG_INFO: debug info TLV
 * @IWL_DBG_TLV_TYPE_BUF_ALLOC: buffer allocation TLV
 * @IWL_DBG_TLV_TYPE_HCMD: host command TLV
 * @IWL_DBG_TLV_TYPE_REGION: region TLV
 * @IWL_DBG_TLV_TYPE_TRIGGER: trigger TLV
 * @IWL_DBG_TLV_TYPE_NUM: number of debug TLVs
 */
enum iwl_dbg_tlv_type {
	IWL_DBG_TLV_TYPE_DEBUG_INFO =
		IWL_UCODE_TLV_TYPE_DEBUG_INFO - IWL_UCODE_TLV_DEBUG_BASE,
	IWL_DBG_TLV_TYPE_BUF_ALLOC,
	IWL_DBG_TLV_TYPE_HCMD,
	IWL_DBG_TLV_TYPE_REGION,
	IWL_DBG_TLV_TYPE_TRIGGER,
	IWL_DBG_TLV_TYPE_NUM,
};

/**
 * struct iwl_dbg_tlv_ver_data -  debug TLV version struct
 * @min_ver: min version supported
 * @max_ver: max version supported
 */
struct iwl_dbg_tlv_ver_data {
	int min_ver;
	int max_ver;
};

static const struct iwl_dbg_tlv_ver_data
dbg_ver_table[IWL_DBG_TLV_TYPE_NUM] = {
	[IWL_DBG_TLV_TYPE_DEBUG_INFO]	= {.min_ver = 1, .max_ver = 1,},
	[IWL_DBG_TLV_TYPE_BUF_ALLOC]	= {.min_ver = 1, .max_ver = 1,},
	[IWL_DBG_TLV_TYPE_HCMD]		= {.min_ver = 1, .max_ver = 1,},
	[IWL_DBG_TLV_TYPE_REGION]	= {.min_ver = 1, .max_ver = 1,},
	[IWL_DBG_TLV_TYPE_TRIGGER]	= {.min_ver = 1, .max_ver = 1,},
};

static int iwl_dbg_tlv_add(struct iwl_ucode_tlv *tlv, struct list_head *list)
{
	u32 len = le32_to_cpu(tlv->length);
	struct iwl_dbg_tlv_node *node;

	node = kzalloc(sizeof(*node) + len, GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	memcpy(&node->tlv, tlv, sizeof(node->tlv) + len);
	list_add_tail(&node->list, list);

	return 0;
}

static bool iwl_dbg_tlv_ver_support(struct iwl_ucode_tlv *tlv)
{
	struct iwl_fw_ini_header *hdr = (void *)&tlv->data[0];
	u32 type = le32_to_cpu(tlv->type);
	u32 tlv_idx = type - IWL_UCODE_TLV_DEBUG_BASE;
	u32 ver = le32_to_cpu(hdr->version);

	if (ver < dbg_ver_table[tlv_idx].min_ver ||
	    ver > dbg_ver_table[tlv_idx].max_ver)
		return false;

	return true;
}

static int iwl_dbg_tlv_alloc_debug_info(struct iwl_trans *trans,
					struct iwl_ucode_tlv *tlv)
{
	struct iwl_fw_ini_debug_info_tlv *debug_info = (void *)tlv->data;

	if (le32_to_cpu(tlv->length) != sizeof(*debug_info))
		return -EINVAL;

	IWL_DEBUG_FW(trans, "WRT: Loading debug cfg: %s\n",
		     debug_info->debug_cfg_name);

	return iwl_dbg_tlv_add(tlv, &trans->dbg.debug_info_tlv_list);
}

static int iwl_dbg_tlv_alloc_buf_alloc(struct iwl_trans *trans,
				       struct iwl_ucode_tlv *tlv)
{
	struct iwl_fw_ini_allocation_tlv *alloc = (void *)tlv->data;
	u32 buf_location = le32_to_cpu(alloc->buf_location);
	u32 alloc_id = le32_to_cpu(alloc->alloc_id);

	if (le32_to_cpu(tlv->length) != sizeof(*alloc) ||
	    (buf_location != IWL_FW_INI_LOCATION_SRAM_PATH &&
	     buf_location != IWL_FW_INI_LOCATION_DRAM_PATH))
		return -EINVAL;

	if ((buf_location == IWL_FW_INI_LOCATION_SRAM_PATH &&
	     alloc_id != IWL_FW_INI_ALLOCATION_ID_DBGC1) ||
	    (buf_location == IWL_FW_INI_LOCATION_DRAM_PATH &&
	     (alloc_id == IWL_FW_INI_ALLOCATION_INVALID ||
	      alloc_id >= IWL_FW_INI_ALLOCATION_NUM))) {
		IWL_ERR(trans,
			"WRT: Invalid allocation id %u for allocation TLV\n",
			alloc_id);
		return -EINVAL;
	}

	trans->dbg.fw_mon_cfg[alloc_id] = *alloc;

	return 0;
}

static int iwl_dbg_tlv_alloc_hcmd(struct iwl_trans *trans,
				  struct iwl_ucode_tlv *tlv)
{
	struct iwl_fw_ini_hcmd_tlv *hcmd = (void *)tlv->data;
	u32 tp = le32_to_cpu(hcmd->time_point);

	if (le32_to_cpu(tlv->length) <= sizeof(*hcmd))
		return -EINVAL;

	/* Host commands can not be sent in early time point since the FW
	 * is not ready
	 */
	if (tp == IWL_FW_INI_TIME_POINT_INVALID ||
	    tp >= IWL_FW_INI_TIME_POINT_NUM ||
	    tp == IWL_FW_INI_TIME_POINT_EARLY) {
		IWL_ERR(trans,
			"WRT: Invalid time point %u for host command TLV\n",
			tp);
		return -EINVAL;
	}

	return iwl_dbg_tlv_add(tlv, &trans->dbg.time_point[tp].hcmd_list);
}

static int iwl_dbg_tlv_alloc_region(struct iwl_trans *trans,
				    struct iwl_ucode_tlv *tlv)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)tlv->data;
	struct iwl_ucode_tlv **active_reg;
	u32 id = le32_to_cpu(reg->id);
	u32 type = le32_to_cpu(reg->type);
	u32 tlv_len = sizeof(*tlv) + le32_to_cpu(tlv->length);

	if (le32_to_cpu(tlv->length) < sizeof(*reg))
		return -EINVAL;

	if (id >= IWL_FW_INI_MAX_REGION_ID) {
		IWL_ERR(trans, "WRT: Invalid region id %u\n", id);
		return -EINVAL;
	}

	if (type <= IWL_FW_INI_REGION_INVALID ||
	    type >= IWL_FW_INI_REGION_NUM) {
		IWL_ERR(trans, "WRT: Invalid region type %u\n", type);
		return -EINVAL;
	}

	active_reg = &trans->dbg.active_regions[id];
	if (*active_reg) {
		IWL_WARN(trans, "WRT: Overriding region id %u\n", id);

		kfree(*active_reg);
	}

	*active_reg = kmemdup(tlv, tlv_len, GFP_KERNEL);
	if (!*active_reg)
		return -ENOMEM;

	IWL_DEBUG_FW(trans, "WRT: Enabling region id %u type %u\n", id, type);

	return 0;
}

static int iwl_dbg_tlv_alloc_trigger(struct iwl_trans *trans,
				     struct iwl_ucode_tlv *tlv)
{
	struct iwl_fw_ini_trigger_tlv *trig = (void *)tlv->data;
	u32 tp = le32_to_cpu(trig->time_point);

	if (le32_to_cpu(tlv->length) < sizeof(*trig))
		return -EINVAL;

	if (tp <= IWL_FW_INI_TIME_POINT_INVALID ||
	    tp >= IWL_FW_INI_TIME_POINT_NUM) {
		IWL_ERR(trans,
			"WRT: Invalid time point %u for trigger TLV\n",
			tp);
		return -EINVAL;
	}

	if (!le32_to_cpu(trig->occurrences))
		trig->occurrences = cpu_to_le32(-1);

	return iwl_dbg_tlv_add(tlv, &trans->dbg.time_point[tp].trig_list);
}

static int (*dbg_tlv_alloc[])(struct iwl_trans *trans,
			      struct iwl_ucode_tlv *tlv) = {
	[IWL_DBG_TLV_TYPE_DEBUG_INFO]	= iwl_dbg_tlv_alloc_debug_info,
	[IWL_DBG_TLV_TYPE_BUF_ALLOC]	= iwl_dbg_tlv_alloc_buf_alloc,
	[IWL_DBG_TLV_TYPE_HCMD]		= iwl_dbg_tlv_alloc_hcmd,
	[IWL_DBG_TLV_TYPE_REGION]	= iwl_dbg_tlv_alloc_region,
	[IWL_DBG_TLV_TYPE_TRIGGER]	= iwl_dbg_tlv_alloc_trigger,
};

void iwl_dbg_tlv_alloc(struct iwl_trans *trans, struct iwl_ucode_tlv *tlv,
		       bool ext)
{
	struct iwl_fw_ini_header *hdr = (void *)&tlv->data[0];
	u32 type = le32_to_cpu(tlv->type);
	u32 tlv_idx = type - IWL_UCODE_TLV_DEBUG_BASE;
	enum iwl_ini_cfg_state *cfg_state = ext ?
		&trans->dbg.external_ini_cfg : &trans->dbg.internal_ini_cfg;
	int ret;

	if (tlv_idx >= ARRAY_SIZE(dbg_tlv_alloc) || !dbg_tlv_alloc[tlv_idx]) {
		IWL_ERR(trans, "WRT: Unsupported TLV type 0x%x\n", type);
		goto out_err;
	}

	if (!iwl_dbg_tlv_ver_support(tlv)) {
		IWL_ERR(trans, "WRT: Unsupported TLV 0x%x version %u\n", type,
			le32_to_cpu(hdr->version));
		goto out_err;
	}

	ret = dbg_tlv_alloc[tlv_idx](trans, tlv);
	if (ret) {
		IWL_ERR(trans,
			"WRT: Failed to allocate TLV 0x%x, ret %d, (ext=%d)\n",
			type, ret, ext);
		goto out_err;
	}

	if (*cfg_state == IWL_INI_CFG_STATE_NOT_LOADED)
		*cfg_state = IWL_INI_CFG_STATE_LOADED;

	return;

out_err:
	*cfg_state = IWL_INI_CFG_STATE_CORRUPTED;
}

void iwl_dbg_tlv_del_timers(struct iwl_trans *trans)
{
	/* will be used later */
}
IWL_EXPORT_SYMBOL(iwl_dbg_tlv_del_timers);

void iwl_dbg_tlv_free(struct iwl_trans *trans)
{
	struct iwl_dbg_tlv_node *tlv_node, *tlv_node_tmp;
	int i;

	iwl_dbg_tlv_del_timers(trans);

	for (i = 0; i < ARRAY_SIZE(trans->dbg.active_regions); i++) {
		struct iwl_ucode_tlv **active_reg =
			&trans->dbg.active_regions[i];

		kfree(*active_reg);
		*active_reg = NULL;
	}

	list_for_each_entry_safe(tlv_node, tlv_node_tmp,
				 &trans->dbg.debug_info_tlv_list, list) {
		list_del(&tlv_node->list);
		kfree(tlv_node);
	}

	for (i = 0; i < ARRAY_SIZE(trans->dbg.time_point); i++) {
		struct iwl_dbg_tlv_time_point_data *tp =
			&trans->dbg.time_point[i];

		list_for_each_entry_safe(tlv_node, tlv_node_tmp, &tp->trig_list,
					 list) {
			list_del(&tlv_node->list);
			kfree(tlv_node);
		}

		list_for_each_entry_safe(tlv_node, tlv_node_tmp, &tp->hcmd_list,
					 list) {
			list_del(&tlv_node->list);
			kfree(tlv_node);
		}
	}
}

static int iwl_dbg_tlv_parse_bin(struct iwl_trans *trans, const u8 *data,
				 size_t len)
{
	struct iwl_ucode_tlv *tlv;
	u32 tlv_len;

	while (len >= sizeof(*tlv)) {
		len -= sizeof(*tlv);
		tlv = (void *)data;

		tlv_len = le32_to_cpu(tlv->length);

		if (len < tlv_len) {
			IWL_ERR(trans, "invalid TLV len: %zd/%u\n",
				len, tlv_len);
			return -EINVAL;
		}
		len -= ALIGN(tlv_len, 4);
		data += sizeof(*tlv) + ALIGN(tlv_len, 4);

		iwl_dbg_tlv_alloc(trans, tlv, true);
	}

	return 0;
}

void iwl_dbg_tlv_load_bin(struct device *dev, struct iwl_trans *trans)
{
	const struct firmware *fw;
	int res;

	if (!iwlwifi_mod_params.enable_ini)
		return;

	res = request_firmware(&fw, "iwl-dbg-tlv.ini", dev);
	if (res)
		return;

	iwl_dbg_tlv_parse_bin(trans, fw->data, fw->size);

	release_firmware(fw);
}

void iwl_dbg_tlv_init(struct iwl_trans *trans)
{
	int i;

	INIT_LIST_HEAD(&trans->dbg.debug_info_tlv_list);

	for (i = 0; i < ARRAY_SIZE(trans->dbg.time_point); i++) {
		struct iwl_dbg_tlv_time_point_data *tp =
			&trans->dbg.time_point[i];

		INIT_LIST_HEAD(&tp->trig_list);
		INIT_LIST_HEAD(&tp->hcmd_list);
	}
}

void iwl_dbg_tlv_time_point(struct iwl_fw_runtime *fwrt,
			    enum iwl_fw_ini_time_point tp_id,
			    union iwl_dbg_tlv_tp_data *tp_data)
{
	/* will be used later */
}
IWL_EXPORT_SYMBOL(iwl_dbg_tlv_time_point);

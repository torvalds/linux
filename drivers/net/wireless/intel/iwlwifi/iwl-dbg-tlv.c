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

static int iwl_dbg_tlv_copy(struct iwl_ucode_tlv *tlv, struct list_head *list)
{
	struct iwl_apply_point_data *tlv_copy;
	u32 len = le32_to_cpu(tlv->length);

	tlv_copy = kzalloc(sizeof(*tlv_copy) + len, GFP_KERNEL);
	if (!tlv_copy)
		return -ENOMEM;

	INIT_LIST_HEAD(&tlv_copy->list);
	memcpy(&tlv_copy->tlv, tlv, sizeof(*tlv) + len);

	if (!list->next)
		INIT_LIST_HEAD(list);

	list_add_tail(&tlv_copy->list, list);

	return 0;
}

static bool iwl_dbg_tlv_ver_support(struct iwl_ucode_tlv *tlv)
{
	struct iwl_fw_ini_header *hdr = (void *)&tlv->data[0];
	u32 type = le32_to_cpu(tlv->type);
	u32 tlv_idx = type - IWL_UCODE_TLV_DEBUG_BASE;
	u32 ver = le32_to_cpu(hdr->tlv_version);

	if (ver < dbg_ver_table[tlv_idx].min_ver ||
	    ver > dbg_ver_table[tlv_idx].max_ver)
		return false;

	return true;
}

void iwl_dbg_tlv_alloc(struct iwl_trans *trans, struct iwl_ucode_tlv *tlv,
		       bool ext)
{
	struct iwl_fw_ini_header *hdr = (void *)&tlv->data[0];
	u32 type = le32_to_cpu(tlv->type);
	u32 pnt = le32_to_cpu(hdr->apply_point);
	u32 tlv_idx = type - IWL_UCODE_TLV_DEBUG_BASE;
	enum iwl_ini_cfg_state *cfg_state = ext ?
		&trans->dbg.external_ini_cfg : &trans->dbg.internal_ini_cfg;
	struct list_head *dbg_cfg_list = ext ?
		&trans->dbg.apply_points_ext[pnt].list :
		&trans->dbg.apply_points[pnt].list;

	IWL_DEBUG_FW(trans, "WRT: read TLV 0x%x, apply point %d\n",
		     type, pnt);

	if (tlv_idx >= IWL_DBG_TLV_TYPE_NUM) {
		IWL_ERR(trans, "WRT: Unsupported TLV 0x%x\n", type);
		goto out_err;
	}

	if (pnt >= IWL_FW_INI_APPLY_NUM) {
		IWL_ERR(trans, "WRT: Invalid apply point %d\n", pnt);
		goto out_err;
	}

	if (!iwl_dbg_tlv_ver_support(tlv)) {
		IWL_ERR(trans, "WRT: Unsupported TLV 0x%x version %u\n", type,
			le32_to_cpu(hdr->tlv_version));
		goto out_err;
	}

	if (iwl_dbg_tlv_copy(tlv, dbg_cfg_list)) {
		IWL_ERR(trans,
			"WRT: Failed to allocate TLV 0x%x, apply point %d\n",
			type, pnt);
		goto out_err;
	}

	if (*cfg_state == IWL_INI_CFG_STATE_NOT_LOADED)
		*cfg_state = IWL_INI_CFG_STATE_LOADED;

	return;

out_err:
	*cfg_state = IWL_INI_CFG_STATE_CORRUPTED;
}

static void iwl_dbg_tlv_free_list(struct list_head *list)
{
	if (!list || !list->next)
		return;

	while (!list_empty(list)) {
		struct iwl_apply_point_data *node =
			list_entry(list->next, typeof(*node), list);

		list_del(&node->list);
		kfree(node);
	}
}

void iwl_dbg_tlv_free(struct iwl_trans *trans)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(trans->dbg.apply_points); i++) {
		struct iwl_apply_point_data *data;

		data = &trans->dbg.apply_points[i];
		iwl_dbg_tlv_free_list(&data->list);

		data = &trans->dbg.apply_points_ext[i];
		iwl_dbg_tlv_free_list(&data->list);
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

static void
iwl_dbg_tlv_apply_debug_info(struct iwl_fw_runtime *fwrt,
			     struct iwl_fw_ini_debug_info_tlv *dbg_info,
			     bool ext, enum iwl_fw_ini_apply_point pnt)
{
	u32 img_name_len = le32_to_cpu(dbg_info->img_name_len);
	u32 dbg_cfg_name_len = le32_to_cpu(dbg_info->dbg_cfg_name_len);
	const char err_str[] =
		"WRT: Invalid %s name length %d, expected %d\n";

	if (img_name_len != IWL_FW_INI_MAX_IMG_NAME_LEN) {
		IWL_WARN(fwrt, err_str, "image", img_name_len,
			 IWL_FW_INI_MAX_IMG_NAME_LEN);
		return;
	}

	if (dbg_cfg_name_len != IWL_FW_INI_MAX_DBG_CFG_NAME_LEN) {
		IWL_WARN(fwrt, err_str, "debug cfg", dbg_cfg_name_len,
			 IWL_FW_INI_MAX_DBG_CFG_NAME_LEN);
		return;
	}

	if (ext) {
		memcpy(fwrt->dump.external_dbg_cfg_name, dbg_info->dbg_cfg_name,
		       sizeof(fwrt->dump.external_dbg_cfg_name));
	} else {
		memcpy(fwrt->dump.img_name, dbg_info->img_name,
		       sizeof(fwrt->dump.img_name));
		memcpy(fwrt->dump.internal_dbg_cfg_name, dbg_info->dbg_cfg_name,
		       sizeof(fwrt->dump.internal_dbg_cfg_name));
	}
}

static void iwl_dbg_tlv_alloc_buffer(struct iwl_fw_runtime *fwrt, u32 size)
{
	struct iwl_trans *trans = fwrt->trans;
	void *virtual_addr = NULL;
	dma_addr_t phys_addr;

	if (WARN_ON_ONCE(trans->dbg.num_blocks ==
			 ARRAY_SIZE(trans->dbg.fw_mon)))
		return;

	virtual_addr =
		dma_alloc_coherent(fwrt->trans->dev, size, &phys_addr,
				   GFP_KERNEL | __GFP_NOWARN);

	/* TODO: alloc fragments if needed */
	if (!virtual_addr)
		IWL_ERR(fwrt, "Failed to allocate debug memory\n");

	IWL_DEBUG_FW(trans,
		     "Allocated DRAM buffer[%d], size=0x%x\n",
		     trans->dbg.num_blocks, size);

	trans->dbg.fw_mon[trans->dbg.num_blocks].block = virtual_addr;
	trans->dbg.fw_mon[trans->dbg.num_blocks].physical = phys_addr;
	trans->dbg.fw_mon[trans->dbg.num_blocks].size = size;
	trans->dbg.num_blocks++;
}

static void iwl_dbg_tlv_apply_buffer(struct iwl_fw_runtime *fwrt,
				     struct iwl_fw_ini_allocation_tlv *alloc,
				     enum iwl_fw_ini_apply_point pnt)
{
	struct iwl_trans *trans = fwrt->trans;
	struct iwl_ldbg_config_cmd ldbg_cmd = {
		.type = cpu_to_le32(BUFFER_ALLOCATION),
	};
	struct iwl_buffer_allocation_cmd *cmd = &ldbg_cmd.buffer_allocation;
	struct iwl_host_cmd hcmd = {
		.id = LDBG_CONFIG_CMD,
		.flags = CMD_ASYNC,
		.data[0] = &ldbg_cmd,
		.len[0] = sizeof(ldbg_cmd),
	};
	int block_idx = trans->dbg.num_blocks;
	u32 buf_location = le32_to_cpu(alloc->buffer_location);
	u32 alloc_id = le32_to_cpu(alloc->allocation_id);

	if (alloc_id <= IWL_FW_INI_ALLOCATION_INVALID ||
	    alloc_id >= IWL_FW_INI_ALLOCATION_NUM) {
		IWL_ERR(fwrt, "WRT: Invalid allocation id %d\n", alloc_id);
		return;
	}

	if (fwrt->trans->dbg.ini_dest == IWL_FW_INI_LOCATION_INVALID)
		fwrt->trans->dbg.ini_dest = buf_location;

	if (buf_location != fwrt->trans->dbg.ini_dest) {
		WARN(fwrt,
		     "WRT: attempt to override buffer location on apply point %d\n",
		     pnt);

		return;
	}

	if (buf_location == IWL_FW_INI_LOCATION_SRAM_PATH) {
		IWL_DEBUG_FW(trans, "WRT: Applying SMEM buffer destination\n");
		/* set sram monitor by enabling bit 7 */
		iwl_set_bit(fwrt->trans, CSR_HW_IF_CONFIG_REG,
			    CSR_HW_IF_CONFIG_REG_BIT_MONITOR_SRAM);

		return;
	}

	if (buf_location != IWL_FW_INI_LOCATION_DRAM_PATH)
		return;

	if (!(BIT(alloc_id) & fwrt->trans->dbg.is_alloc)) {
		iwl_dbg_tlv_alloc_buffer(fwrt, le32_to_cpu(alloc->size));
		if (block_idx == trans->dbg.num_blocks)
			return;
		fwrt->trans->dbg.is_alloc |= BIT(alloc_id);
	}

	/* First block is assigned via registers / context info */
	if (trans->dbg.num_blocks == 1)
		return;

	IWL_DEBUG_FW(trans,
		     "WRT: Applying DRAM buffer[%d] destination\n", block_idx);

	cmd->num_frags = cpu_to_le32(1);
	cmd->fragments[0].address =
		cpu_to_le64(trans->dbg.fw_mon[block_idx].physical);
	cmd->fragments[0].size = alloc->size;
	cmd->allocation_id = alloc->allocation_id;
	cmd->buffer_location = alloc->buffer_location;

	iwl_trans_send_cmd(trans, &hcmd);
}

static void iwl_dbg_tlv_apply_hcmd(struct iwl_fw_runtime *fwrt,
				   struct iwl_ucode_tlv *tlv)
{
	struct iwl_fw_ini_hcmd_tlv *hcmd_tlv = (void *)&tlv->data[0];
	struct iwl_fw_ini_hcmd *data = &hcmd_tlv->hcmd;
	u16 len = le32_to_cpu(tlv->length) - sizeof(*hcmd_tlv);

	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(data->group, data->id),
		.len = { len, },
		.data = { data->data, },
	};

	/* currently the driver supports always on domain only */
	if (le32_to_cpu(hcmd_tlv->domain) != IWL_FW_INI_DBG_DOMAIN_ALWAYS_ON)
		return;

	IWL_DEBUG_FW(fwrt, "WRT: Sending host command id=0x%x, group=0x%x\n",
		     data->id, data->group);

	iwl_trans_send_cmd(fwrt->trans, &hcmd);
}

static void iwl_dbg_tlv_apply_region(struct iwl_fw_runtime *fwrt,
				     struct iwl_fw_ini_region_tlv *tlv,
				     enum iwl_fw_ini_apply_point pnt)
{
	void *iter = (void *)tlv->region_config;
	int i, size = le32_to_cpu(tlv->num_regions);
	const char *err_st =
		"WRT: Invalid region %s %d for apply point %d\n";

	for (i = 0; i < size; i++) {
		struct iwl_fw_ini_region_cfg *reg = iter, **active;
		int id = le32_to_cpu(reg->region_id);
		u32 type = le32_to_cpu(reg->region_type);

		if (WARN(id >= ARRAY_SIZE(fwrt->dump.active_regs), err_st, "id",
			 id, pnt))
			break;

		if (WARN(type == 0 || type >= IWL_FW_INI_REGION_NUM, err_st,
			 "type", type, pnt))
			break;

		active = &fwrt->dump.active_regs[id];

		if (*active)
			IWL_WARN(fwrt->trans, "WRT: Region id %d override\n",
				 id);

		IWL_DEBUG_FW(fwrt, "WRT: Activating region id %d\n", id);

		*active = reg;

		if (type == IWL_FW_INI_REGION_TXF ||
		    type == IWL_FW_INI_REGION_RXF)
			iter += le32_to_cpu(reg->fifos.num_of_registers) *
				sizeof(__le32);
		else if (type == IWL_FW_INI_REGION_DEVICE_MEMORY ||
			 type == IWL_FW_INI_REGION_PERIPHERY_MAC ||
			 type == IWL_FW_INI_REGION_PERIPHERY_PHY ||
			 type == IWL_FW_INI_REGION_PERIPHERY_AUX ||
			 type == IWL_FW_INI_REGION_INTERNAL_BUFFER ||
			 type == IWL_FW_INI_REGION_PAGING ||
			 type == IWL_FW_INI_REGION_CSR ||
			 type == IWL_FW_INI_REGION_LMAC_ERROR_TABLE ||
			 type == IWL_FW_INI_REGION_UMAC_ERROR_TABLE)
			iter += le32_to_cpu(reg->internal.num_of_ranges) *
				sizeof(__le32);

		iter += sizeof(*reg);
	}
}

static int iwl_dbg_tlv_trig_realloc(struct iwl_fw_runtime *fwrt,
				    struct iwl_fw_ini_active_triggers *active,
				    u32 id, int size)
{
	void *ptr;

	if (size <= active->size)
		return 0;

	ptr = krealloc(active->trig, size, GFP_KERNEL);
	if (!ptr) {
		IWL_ERR(fwrt, "WRT: Failed to allocate memory for trigger %d\n",
			id);
		return -ENOMEM;
	}
	active->trig = ptr;
	active->size = size;

	return 0;
}

static void iwl_dbg_tlv_apply_trigger(struct iwl_fw_runtime *fwrt,
				      struct iwl_fw_ini_trigger_tlv *tlv,
				      enum iwl_fw_ini_apply_point apply_point)
{
	int i, size = le32_to_cpu(tlv->num_triggers);
	void *iter = (void *)tlv->trigger_config;

	for (i = 0; i < size; i++) {
		struct iwl_fw_ini_trigger *trig = iter;
		struct iwl_fw_ini_active_triggers *active;
		int id = le32_to_cpu(trig->trigger_id);
		u32 trig_regs_size = le32_to_cpu(trig->num_regions) *
			sizeof(__le32);

		if (WARN(id >= ARRAY_SIZE(fwrt->dump.active_trigs),
			 "WRT: Invalid trigger id %d for apply point %d\n", id,
			 apply_point))
			break;

		active = &fwrt->dump.active_trigs[id];

		if (!active->active) {
			size_t trig_size = sizeof(*trig) + trig_regs_size;

			IWL_DEBUG_FW(fwrt, "WRT: Activating trigger %d\n", id);

			if (iwl_dbg_tlv_trig_realloc(fwrt, active, id,
						     trig_size))
				goto next;

			memcpy(active->trig, trig, trig_size);

		} else {
			u32 conf_override =
				!(le32_to_cpu(trig->override_trig) & 0xff);
			u32 region_override =
				!(le32_to_cpu(trig->override_trig) & 0xff00);
			u32 offset = 0;
			u32 active_regs =
				le32_to_cpu(active->trig->num_regions);
			u32 new_regs = le32_to_cpu(trig->num_regions);
			int mem_to_add = trig_regs_size;

			if (region_override) {
				IWL_DEBUG_FW(fwrt,
					     "WRT: Trigger %d regions override\n",
					     id);

				mem_to_add -= active_regs * sizeof(__le32);
			} else {
				IWL_DEBUG_FW(fwrt,
					     "WRT: Trigger %d regions appending\n",
					     id);

				offset += active_regs;
				new_regs += active_regs;
			}

			if (iwl_dbg_tlv_trig_realloc(fwrt, active, id,
						     active->size + mem_to_add))
				goto next;

			if (conf_override) {
				IWL_DEBUG_FW(fwrt,
					     "WRT: Trigger %d configuration override\n",
					     id);

				memcpy(active->trig, trig, sizeof(*trig));
			}

			memcpy(active->trig->data + offset, trig->data,
			       trig_regs_size);
			active->trig->num_regions = cpu_to_le32(new_regs);
		}

		/* Since zero means infinity - just set to -1 */
		if (!le32_to_cpu(active->trig->occurrences))
			active->trig->occurrences = cpu_to_le32(-1);

		active->active = true;

		if (id == IWL_FW_TRIGGER_ID_PERIODIC_TRIGGER) {
			u32 collect_interval = le32_to_cpu(trig->trigger_data);

			/* the minimum allowed interval is 50ms */
			if (collect_interval < 50) {
				collect_interval = 50;
				trig->trigger_data =
					cpu_to_le32(collect_interval);
			}

			mod_timer(&fwrt->dump.periodic_trig,
				  jiffies + msecs_to_jiffies(collect_interval));
		}
next:
		iter += sizeof(*trig) + trig_regs_size;
	}
}

static void _iwl_dbg_tlv_apply_point(struct iwl_fw_runtime *fwrt,
				     struct iwl_apply_point_data *data,
				     enum iwl_fw_ini_apply_point pnt,
				     bool ext)
{
	struct iwl_apply_point_data *iter;

	if (!data->list.next)
		return;

	list_for_each_entry(iter, &data->list, list) {
		struct iwl_ucode_tlv *tlv = &iter->tlv;
		void *ini_tlv = (void *)tlv->data;
		u32 type = le32_to_cpu(tlv->type);
		const char invalid_ap_str[] =
			"WRT: Invalid apply point %d for %s\n";

		switch (type) {
		case IWL_UCODE_TLV_TYPE_DEBUG_INFO:
			iwl_dbg_tlv_apply_debug_info(fwrt, ini_tlv, ext, pnt);
			break;
		case IWL_UCODE_TLV_TYPE_BUFFER_ALLOCATION:
			if (pnt != IWL_FW_INI_APPLY_EARLY) {
				IWL_ERR(fwrt, invalid_ap_str, pnt,
					"buffer allocation");
				break;
			}
			iwl_dbg_tlv_apply_buffer(fwrt, ini_tlv, pnt);
			break;
		case IWL_UCODE_TLV_TYPE_HCMD:
			if (pnt < IWL_FW_INI_APPLY_AFTER_ALIVE) {
				IWL_ERR(fwrt, invalid_ap_str, pnt,
					"host command");
				break;
			}
			iwl_dbg_tlv_apply_hcmd(fwrt, tlv);
			break;
		case IWL_UCODE_TLV_TYPE_REGIONS:
			iwl_dbg_tlv_apply_region(fwrt, ini_tlv, pnt);
			break;
		case IWL_UCODE_TLV_TYPE_TRIGGERS:
			iwl_dbg_tlv_apply_trigger(fwrt, ini_tlv, pnt);
			break;
		default:
			WARN_ONCE(1, "WRT: Invalid TLV 0x%x for apply point\n",
				  type);
			break;
		}
	}
}

static void iwl_dbg_tlv_reset_cfg(struct iwl_fw_runtime *fwrt)
{
	int i;

	for (i = 0; i < IWL_FW_INI_MAX_REGION_ID; i++)
		fwrt->dump.active_regs[i] = NULL;

	/* disable the triggers, used in recovery flow */
	for (i = 0; i < IWL_FW_TRIGGER_ID_NUM; i++)
		fwrt->dump.active_trigs[i].active = false;

	memset(fwrt->dump.img_name, 0,
	       sizeof(fwrt->dump.img_name));
	memset(fwrt->dump.internal_dbg_cfg_name, 0,
	       sizeof(fwrt->dump.internal_dbg_cfg_name));
	memset(fwrt->dump.external_dbg_cfg_name, 0,
	       sizeof(fwrt->dump.external_dbg_cfg_name));

	fwrt->trans->dbg.ini_dest = IWL_FW_INI_LOCATION_INVALID;
}

void iwl_dbg_tlv_apply_point(struct iwl_fw_runtime *fwrt,
			     enum iwl_fw_ini_apply_point apply_point)
{
	void *data;

	if (apply_point == IWL_FW_INI_APPLY_EARLY)
		iwl_dbg_tlv_reset_cfg(fwrt);

	if (fwrt->trans->dbg.internal_ini_cfg != IWL_INI_CFG_STATE_NOT_LOADED) {
		IWL_DEBUG_FW(fwrt,
			     "WRT: Enabling internal configuration apply point %d\n",
			     apply_point);
		data = &fwrt->trans->dbg.apply_points[apply_point];
		_iwl_dbg_tlv_apply_point(fwrt, data, apply_point, false);
	}

	if (fwrt->trans->dbg.external_ini_cfg != IWL_INI_CFG_STATE_NOT_LOADED) {
		IWL_DEBUG_FW(fwrt,
			     "WRT: Enabling external configuration apply point %d\n",
			     apply_point);
		data = &fwrt->trans->dbg.apply_points_ext[apply_point];
		_iwl_dbg_tlv_apply_point(fwrt, data, apply_point, true);
	}
}
IWL_EXPORT_SYMBOL(iwl_dbg_tlv_apply_point);

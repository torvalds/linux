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
#include "iwl-trans.h"
#include "iwl-dbg-tlv.h"

void iwl_dbg_tlv_copy(struct iwl_trans *trans, struct iwl_ucode_tlv *tlv,
		      bool ext)
{
	struct iwl_apply_point_data *dbg_cfg, *tlv_copy;
	struct iwl_fw_ini_header *header = (void *)&tlv->data[0];
	u32 tlv_type = le32_to_cpu(tlv->type);
	u32 len = le32_to_cpu(tlv->length);
	u32 apply_point = le32_to_cpu(header->apply_point);

	if (le32_to_cpu(header->tlv_version) != 1)
		return;

	if (WARN_ONCE(apply_point >= IWL_FW_INI_APPLY_NUM,
		      "Invalid apply point %d\n", apply_point))
		return;

	IWL_DEBUG_FW(trans, "WRT: read TLV 0x%x, apply point %d\n",
		     tlv_type, apply_point);

	if (ext)
		dbg_cfg = &trans->dbg.apply_points_ext[apply_point];
	else
		dbg_cfg = &trans->dbg.apply_points[apply_point];

	tlv_copy = kzalloc(sizeof(*tlv_copy) + len, GFP_KERNEL);
	if (!tlv_copy) {
		IWL_ERR(trans, "WRT: No memory for TLV 0x%x, apply point %d\n",
			tlv_type, apply_point);
		return;
	}

	INIT_LIST_HEAD(&tlv_copy->list);
	memcpy(&tlv_copy->tlv, tlv, sizeof(*tlv) + len);

	if (!dbg_cfg->list.next)
		INIT_LIST_HEAD(&dbg_cfg->list);

	list_add_tail(&tlv_copy->list, &dbg_cfg->list);

	trans->dbg.ini_valid = true;
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
	enum iwl_ucode_tlv_type tlv_type;
	u32 tlv_len;

	while (len >= sizeof(*tlv)) {
		len -= sizeof(*tlv);
		tlv = (void *)data;

		tlv_len = le32_to_cpu(tlv->length);
		tlv_type = le32_to_cpu(tlv->type);

		if (len < tlv_len) {
			IWL_ERR(trans, "invalid TLV len: %zd/%u\n",
				len, tlv_len);
			return -EINVAL;
		}
		len -= ALIGN(tlv_len, 4);
		data += sizeof(*tlv) + ALIGN(tlv_len, 4);

		switch (tlv_type) {
		case IWL_UCODE_TLV_TYPE_DEBUG_INFO:
		case IWL_UCODE_TLV_TYPE_BUFFER_ALLOCATION:
		case IWL_UCODE_TLV_TYPE_HCMD:
		case IWL_UCODE_TLV_TYPE_REGIONS:
		case IWL_UCODE_TLV_TYPE_TRIGGERS:
		case IWL_UCODE_TLV_TYPE_DEBUG_FLOW:
			iwl_dbg_tlv_copy(trans, tlv, true);
			break;
		default:
			WARN_ONCE(1, "Invalid TLV %x\n", tlv_type);
			break;
		}
	}

	return 0;
}

void iwl_dbg_tlv_load_bin(struct device *dev, struct iwl_trans *trans)
{
	const struct firmware *fw;
	int res;

	if (trans->dbg.external_ini_loaded || !iwlwifi_mod_params.enable_ini)
		return;

	res = request_firmware(&fw, "iwl-dbg-tlv.ini", dev);
	if (res)
		return;

	iwl_dbg_tlv_parse_bin(trans, fw->data, fw->size);

	trans->dbg.external_ini_loaded = true;
	release_firmware(fw);
}

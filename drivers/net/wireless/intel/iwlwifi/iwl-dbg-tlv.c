/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright (C) 2018 Intel Corporation
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
 * Copyright (C) 2018 Intel Corporation
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

void iwl_fw_dbg_copy_tlv(struct iwl_trans *trans, struct iwl_ucode_tlv *tlv)
{
	struct iwl_apply_point_data *data;
	struct iwl_fw_ini_header *header = (void *)&tlv->data[0];
	u32 apply_point = le32_to_cpu(header->apply_point);

	int copy_size = le32_to_cpu(tlv->length) + sizeof(*tlv);

	if (WARN_ONCE(apply_point >= IWL_FW_INI_APPLY_NUM,
		      "Invalid apply point id %d\n", apply_point))
		return;

	data = &trans->apply_points[apply_point];

	/*
	 * Make sure we still have room to copy this TLV. Offset points to the
	 * location the last copy ended.
	 */
	if (WARN_ONCE(data->offset + copy_size > data->size,
		      "Not enough memory for apply point %d\n",
		      apply_point))
		return;

	memcpy(data->data + data->offset, (void *)tlv, copy_size);
	data->offset += copy_size;
}

void iwl_alloc_dbg_tlv(struct iwl_trans *trans, size_t len, const u8 *data)
{
	struct iwl_ucode_tlv *tlv;
	u32 size[IWL_FW_INI_APPLY_NUM] = {0};
	int i;

	while (len >= sizeof(*tlv)) {
		u32 tlv_len, tlv_type, apply;
		struct iwl_fw_ini_header *hdr;

		len -= sizeof(*tlv);
		tlv = (void *)data;

		tlv_len = le32_to_cpu(tlv->length);
		tlv_type = le32_to_cpu(tlv->type);

		if (len < tlv_len)
			return;

		len -= ALIGN(tlv_len, 4);
		data += sizeof(*tlv) + ALIGN(tlv_len, 4);

		if (!(tlv_type & IWL_UCODE_INI_TLV_GROUP))
			continue;

		hdr = (void *)&tlv->data[0];
		apply = le32_to_cpu(hdr->apply_point);

		if (WARN_ON(apply >= IWL_FW_INI_APPLY_NUM))
			continue;

		size[apply] += sizeof(*tlv) + tlv_len;
	}

	for (i = 0; i < ARRAY_SIZE(size); i++) {
		void *mem;

		if (!size[i])
			continue;

		mem = kzalloc(size[i], GFP_KERNEL);

		if (!mem) {
			IWL_ERR(trans, "No memory for apply point %d\n", i);
			return;
		}

		trans->apply_points[i].data = mem;
		trans->apply_points[i].size = size[i];
	}
}

void iwl_fw_dbg_free(struct iwl_trans *trans)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(trans->apply_points); i++) {
		kfree(trans->apply_points[i].data);
		trans->apply_points[i].size = 0;
		trans->apply_points[i].offset = 0;
	}
}

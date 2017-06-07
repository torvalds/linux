/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
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
 * along with this program;
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
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
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
#include <linux/devcoredump.h>

#include "fw-dbg.h"
#include "iwl-io.h"
#include "mvm.h"
#include "iwl-prph.h"
#include "iwl-csr.h"

#define RADIO_REG_MAX_READ 0x2ad
static void iwl_mvm_read_radio_reg(struct iwl_mvm *mvm,
				   struct iwl_fw_error_dump_data **dump_data)
{
	u8 *pos = (void *)(*dump_data)->data;
	unsigned long flags;
	int i;

	if (!iwl_trans_grab_nic_access(mvm->trans, &flags))
		return;

	(*dump_data)->type = cpu_to_le32(IWL_FW_ERROR_DUMP_RADIO_REG);
	(*dump_data)->len = cpu_to_le32(RADIO_REG_MAX_READ);

	for (i = 0; i < RADIO_REG_MAX_READ; i++) {
		u32 rd_cmd = RADIO_RSP_RD_CMD;

		rd_cmd |= i << RADIO_RSP_ADDR_POS;
		iwl_write_prph_no_grab(mvm->trans, RSP_RADIO_CMD, rd_cmd);
		*pos =  (u8)iwl_read_prph_no_grab(mvm->trans, RSP_RADIO_RDDAT);

		pos++;
	}

	*dump_data = iwl_fw_error_next_data(*dump_data);

	iwl_trans_release_nic_access(mvm->trans, &flags);
}

static void iwl_mvm_dump_rxf(struct iwl_mvm *mvm,
			     struct iwl_fw_error_dump_data **dump_data,
			     int size, u32 offset, int fifo_num)
{
	struct iwl_fw_error_dump_fifo *fifo_hdr;
	u32 *fifo_data;
	u32 fifo_len;
	int i;

	fifo_hdr = (void *)(*dump_data)->data;
	fifo_data = (void *)fifo_hdr->data;
	fifo_len = size;

	/* No need to try to read the data if the length is 0 */
	if (fifo_len == 0)
		return;

	/* Add a TLV for the RXF */
	(*dump_data)->type = cpu_to_le32(IWL_FW_ERROR_DUMP_RXF);
	(*dump_data)->len = cpu_to_le32(fifo_len + sizeof(*fifo_hdr));

	fifo_hdr->fifo_num = cpu_to_le32(fifo_num);
	fifo_hdr->available_bytes =
		cpu_to_le32(iwl_trans_read_prph(mvm->trans,
						RXF_RD_D_SPACE + offset));
	fifo_hdr->wr_ptr =
		cpu_to_le32(iwl_trans_read_prph(mvm->trans,
						RXF_RD_WR_PTR + offset));
	fifo_hdr->rd_ptr =
		cpu_to_le32(iwl_trans_read_prph(mvm->trans,
						RXF_RD_RD_PTR + offset));
	fifo_hdr->fence_ptr =
		cpu_to_le32(iwl_trans_read_prph(mvm->trans,
						RXF_RD_FENCE_PTR + offset));
	fifo_hdr->fence_mode =
		cpu_to_le32(iwl_trans_read_prph(mvm->trans,
						RXF_SET_FENCE_MODE + offset));

	/* Lock fence */
	iwl_trans_write_prph(mvm->trans, RXF_SET_FENCE_MODE + offset, 0x1);
	/* Set fence pointer to the same place like WR pointer */
	iwl_trans_write_prph(mvm->trans, RXF_LD_WR2FENCE + offset, 0x1);
	/* Set fence offset */
	iwl_trans_write_prph(mvm->trans,
			     RXF_LD_FENCE_OFFSET_ADDR + offset, 0x0);

	/* Read FIFO */
	fifo_len /= sizeof(u32); /* Size in DWORDS */
	for (i = 0; i < fifo_len; i++)
		fifo_data[i] = iwl_trans_read_prph(mvm->trans,
						 RXF_FIFO_RD_FENCE_INC +
						 offset);
	*dump_data = iwl_fw_error_next_data(*dump_data);
}

static void iwl_mvm_dump_txf(struct iwl_mvm *mvm,
			     struct iwl_fw_error_dump_data **dump_data,
			     int size, u32 offset, int fifo_num)
{
	struct iwl_fw_error_dump_fifo *fifo_hdr;
	u32 *fifo_data;
	u32 fifo_len;
	int i;

	fifo_hdr = (void *)(*dump_data)->data;
	fifo_data = (void *)fifo_hdr->data;
	fifo_len = size;

	/* No need to try to read the data if the length is 0 */
	if (fifo_len == 0)
		return;

	/* Add a TLV for the FIFO */
	(*dump_data)->type = cpu_to_le32(IWL_FW_ERROR_DUMP_TXF);
	(*dump_data)->len = cpu_to_le32(fifo_len + sizeof(*fifo_hdr));

	fifo_hdr->fifo_num = cpu_to_le32(fifo_num);
	fifo_hdr->available_bytes =
		cpu_to_le32(iwl_trans_read_prph(mvm->trans,
						TXF_FIFO_ITEM_CNT + offset));
	fifo_hdr->wr_ptr =
		cpu_to_le32(iwl_trans_read_prph(mvm->trans,
						TXF_WR_PTR + offset));
	fifo_hdr->rd_ptr =
		cpu_to_le32(iwl_trans_read_prph(mvm->trans,
						TXF_RD_PTR + offset));
	fifo_hdr->fence_ptr =
		cpu_to_le32(iwl_trans_read_prph(mvm->trans,
						TXF_FENCE_PTR + offset));
	fifo_hdr->fence_mode =
		cpu_to_le32(iwl_trans_read_prph(mvm->trans,
						TXF_LOCK_FENCE + offset));

	/* Set the TXF_READ_MODIFY_ADDR to TXF_WR_PTR */
	iwl_trans_write_prph(mvm->trans, TXF_READ_MODIFY_ADDR + offset,
			     TXF_WR_PTR + offset);

	/* Dummy-read to advance the read pointer to the head */
	iwl_trans_read_prph(mvm->trans, TXF_READ_MODIFY_DATA + offset);

	/* Read FIFO */
	fifo_len /= sizeof(u32); /* Size in DWORDS */
	for (i = 0; i < fifo_len; i++)
		fifo_data[i] = iwl_trans_read_prph(mvm->trans,
						  TXF_READ_MODIFY_DATA +
						  offset);
	*dump_data = iwl_fw_error_next_data(*dump_data);
}

static void iwl_mvm_dump_fifos(struct iwl_mvm *mvm,
			       struct iwl_fw_error_dump_data **dump_data)
{
	struct iwl_fw_error_dump_fifo *fifo_hdr;
	struct iwl_mvm_shared_mem_cfg *cfg = &mvm->smem_cfg;
	u32 *fifo_data;
	u32 fifo_len;
	unsigned long flags;
	int i, j;

	if (!iwl_trans_grab_nic_access(mvm->trans, &flags))
		return;

	/* Pull RXF1 */
	iwl_mvm_dump_rxf(mvm, dump_data, cfg->lmac[0].rxfifo1_size, 0, 0);
	/* Pull RXF2 */
	iwl_mvm_dump_rxf(mvm, dump_data, cfg->rxfifo2_size,
			 RXF_DIFF_FROM_PREV, 1);
	/* Pull LMAC2 RXF1 */
	if (mvm->smem_cfg.num_lmacs > 1)
		iwl_mvm_dump_rxf(mvm, dump_data, cfg->lmac[1].rxfifo1_size,
				 LMAC2_PRPH_OFFSET, 2);

	/* Pull TXF data from LMAC1 */
	for (i = 0; i < mvm->smem_cfg.num_txfifo_entries; i++) {
		/* Mark the number of TXF we're pulling now */
		iwl_trans_write_prph(mvm->trans, TXF_LARC_NUM, i);
		iwl_mvm_dump_txf(mvm, dump_data, cfg->lmac[0].txfifo_size[i],
				 0, i);
	}

	/* Pull TXF data from LMAC2 */
	if (mvm->smem_cfg.num_lmacs > 1) {
		for (i = 0; i < mvm->smem_cfg.num_txfifo_entries; i++) {
			/* Mark the number of TXF we're pulling now */
			iwl_trans_write_prph(mvm->trans,
					     TXF_LARC_NUM + LMAC2_PRPH_OFFSET,
					     i);
			iwl_mvm_dump_txf(mvm, dump_data,
					 cfg->lmac[1].txfifo_size[i],
					 LMAC2_PRPH_OFFSET,
					 i + cfg->num_txfifo_entries);
		}
	}

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_EXTEND_SHARED_MEM_CFG)) {
		/* Pull UMAC internal TXF data from all TXFs */
		for (i = 0;
		     i < ARRAY_SIZE(mvm->smem_cfg.internal_txfifo_size);
		     i++) {
			fifo_hdr = (void *)(*dump_data)->data;
			fifo_data = (void *)fifo_hdr->data;
			fifo_len = mvm->smem_cfg.internal_txfifo_size[i];

			/* No need to try to read the data if the length is 0 */
			if (fifo_len == 0)
				continue;

			/* Add a TLV for the internal FIFOs */
			(*dump_data)->type =
				cpu_to_le32(IWL_FW_ERROR_DUMP_INTERNAL_TXF);
			(*dump_data)->len =
				cpu_to_le32(fifo_len + sizeof(*fifo_hdr));

			fifo_hdr->fifo_num = cpu_to_le32(i);

			/* Mark the number of TXF we're pulling now */
			iwl_trans_write_prph(mvm->trans, TXF_CPU2_NUM, i +
				mvm->smem_cfg.num_txfifo_entries);

			fifo_hdr->available_bytes =
				cpu_to_le32(iwl_trans_read_prph(mvm->trans,
								TXF_CPU2_FIFO_ITEM_CNT));
			fifo_hdr->wr_ptr =
				cpu_to_le32(iwl_trans_read_prph(mvm->trans,
								TXF_CPU2_WR_PTR));
			fifo_hdr->rd_ptr =
				cpu_to_le32(iwl_trans_read_prph(mvm->trans,
								TXF_CPU2_RD_PTR));
			fifo_hdr->fence_ptr =
				cpu_to_le32(iwl_trans_read_prph(mvm->trans,
								TXF_CPU2_FENCE_PTR));
			fifo_hdr->fence_mode =
				cpu_to_le32(iwl_trans_read_prph(mvm->trans,
								TXF_CPU2_LOCK_FENCE));

			/* Set TXF_CPU2_READ_MODIFY_ADDR to TXF_CPU2_WR_PTR */
			iwl_trans_write_prph(mvm->trans,
					     TXF_CPU2_READ_MODIFY_ADDR,
					     TXF_CPU2_WR_PTR);

			/* Dummy-read to advance the read pointer to head */
			iwl_trans_read_prph(mvm->trans,
					    TXF_CPU2_READ_MODIFY_DATA);

			/* Read FIFO */
			fifo_len /= sizeof(u32); /* Size in DWORDS */
			for (j = 0; j < fifo_len; j++)
				fifo_data[j] =
					iwl_trans_read_prph(mvm->trans,
							    TXF_CPU2_READ_MODIFY_DATA);
			*dump_data = iwl_fw_error_next_data(*dump_data);
		}
	}

	iwl_trans_release_nic_access(mvm->trans, &flags);
}

void iwl_mvm_free_fw_dump_desc(struct iwl_mvm *mvm)
{
	if (mvm->fw_dump_desc == &iwl_mvm_dump_desc_assert)
		return;

	kfree(mvm->fw_dump_desc);
	mvm->fw_dump_desc = NULL;
}

#define IWL8260_ICCM_OFFSET		0x44000 /* Only for B-step */
#define IWL8260_ICCM_LEN		0xC000 /* Only for B-step */

struct iwl_prph_range {
	u32 start, end;
};

static const struct iwl_prph_range iwl_prph_dump_addr_comm[] = {
	{ .start = 0x00a00000, .end = 0x00a00000 },
	{ .start = 0x00a0000c, .end = 0x00a00024 },
	{ .start = 0x00a0002c, .end = 0x00a0003c },
	{ .start = 0x00a00410, .end = 0x00a00418 },
	{ .start = 0x00a00420, .end = 0x00a00420 },
	{ .start = 0x00a00428, .end = 0x00a00428 },
	{ .start = 0x00a00430, .end = 0x00a0043c },
	{ .start = 0x00a00444, .end = 0x00a00444 },
	{ .start = 0x00a004c0, .end = 0x00a004cc },
	{ .start = 0x00a004d8, .end = 0x00a004d8 },
	{ .start = 0x00a004e0, .end = 0x00a004f0 },
	{ .start = 0x00a00840, .end = 0x00a00840 },
	{ .start = 0x00a00850, .end = 0x00a00858 },
	{ .start = 0x00a01004, .end = 0x00a01008 },
	{ .start = 0x00a01010, .end = 0x00a01010 },
	{ .start = 0x00a01018, .end = 0x00a01018 },
	{ .start = 0x00a01024, .end = 0x00a01024 },
	{ .start = 0x00a0102c, .end = 0x00a01034 },
	{ .start = 0x00a0103c, .end = 0x00a01040 },
	{ .start = 0x00a01048, .end = 0x00a01094 },
	{ .start = 0x00a01c00, .end = 0x00a01c20 },
	{ .start = 0x00a01c58, .end = 0x00a01c58 },
	{ .start = 0x00a01c7c, .end = 0x00a01c7c },
	{ .start = 0x00a01c28, .end = 0x00a01c54 },
	{ .start = 0x00a01c5c, .end = 0x00a01c5c },
	{ .start = 0x00a01c60, .end = 0x00a01cdc },
	{ .start = 0x00a01ce0, .end = 0x00a01d0c },
	{ .start = 0x00a01d18, .end = 0x00a01d20 },
	{ .start = 0x00a01d2c, .end = 0x00a01d30 },
	{ .start = 0x00a01d40, .end = 0x00a01d5c },
	{ .start = 0x00a01d80, .end = 0x00a01d80 },
	{ .start = 0x00a01d98, .end = 0x00a01d9c },
	{ .start = 0x00a01da8, .end = 0x00a01da8 },
	{ .start = 0x00a01db8, .end = 0x00a01df4 },
	{ .start = 0x00a01dc0, .end = 0x00a01dfc },
	{ .start = 0x00a01e00, .end = 0x00a01e2c },
	{ .start = 0x00a01e40, .end = 0x00a01e60 },
	{ .start = 0x00a01e68, .end = 0x00a01e6c },
	{ .start = 0x00a01e74, .end = 0x00a01e74 },
	{ .start = 0x00a01e84, .end = 0x00a01e90 },
	{ .start = 0x00a01e9c, .end = 0x00a01ec4 },
	{ .start = 0x00a01ed0, .end = 0x00a01ee0 },
	{ .start = 0x00a01f00, .end = 0x00a01f1c },
	{ .start = 0x00a01f44, .end = 0x00a01ffc },
	{ .start = 0x00a02000, .end = 0x00a02048 },
	{ .start = 0x00a02068, .end = 0x00a020f0 },
	{ .start = 0x00a02100, .end = 0x00a02118 },
	{ .start = 0x00a02140, .end = 0x00a0214c },
	{ .start = 0x00a02168, .end = 0x00a0218c },
	{ .start = 0x00a021c0, .end = 0x00a021c0 },
	{ .start = 0x00a02400, .end = 0x00a02410 },
	{ .start = 0x00a02418, .end = 0x00a02420 },
	{ .start = 0x00a02428, .end = 0x00a0242c },
	{ .start = 0x00a02434, .end = 0x00a02434 },
	{ .start = 0x00a02440, .end = 0x00a02460 },
	{ .start = 0x00a02468, .end = 0x00a024b0 },
	{ .start = 0x00a024c8, .end = 0x00a024cc },
	{ .start = 0x00a02500, .end = 0x00a02504 },
	{ .start = 0x00a0250c, .end = 0x00a02510 },
	{ .start = 0x00a02540, .end = 0x00a02554 },
	{ .start = 0x00a02580, .end = 0x00a025f4 },
	{ .start = 0x00a02600, .end = 0x00a0260c },
	{ .start = 0x00a02648, .end = 0x00a02650 },
	{ .start = 0x00a02680, .end = 0x00a02680 },
	{ .start = 0x00a026c0, .end = 0x00a026d0 },
	{ .start = 0x00a02700, .end = 0x00a0270c },
	{ .start = 0x00a02804, .end = 0x00a02804 },
	{ .start = 0x00a02818, .end = 0x00a0281c },
	{ .start = 0x00a02c00, .end = 0x00a02db4 },
	{ .start = 0x00a02df4, .end = 0x00a02fb0 },
	{ .start = 0x00a03000, .end = 0x00a03014 },
	{ .start = 0x00a0301c, .end = 0x00a0302c },
	{ .start = 0x00a03034, .end = 0x00a03038 },
	{ .start = 0x00a03040, .end = 0x00a03048 },
	{ .start = 0x00a03060, .end = 0x00a03068 },
	{ .start = 0x00a03070, .end = 0x00a03074 },
	{ .start = 0x00a0307c, .end = 0x00a0307c },
	{ .start = 0x00a03080, .end = 0x00a03084 },
	{ .start = 0x00a0308c, .end = 0x00a03090 },
	{ .start = 0x00a03098, .end = 0x00a03098 },
	{ .start = 0x00a030a0, .end = 0x00a030a0 },
	{ .start = 0x00a030a8, .end = 0x00a030b4 },
	{ .start = 0x00a030bc, .end = 0x00a030bc },
	{ .start = 0x00a030c0, .end = 0x00a0312c },
	{ .start = 0x00a03c00, .end = 0x00a03c5c },
	{ .start = 0x00a04400, .end = 0x00a04454 },
	{ .start = 0x00a04460, .end = 0x00a04474 },
	{ .start = 0x00a044c0, .end = 0x00a044ec },
	{ .start = 0x00a04500, .end = 0x00a04504 },
	{ .start = 0x00a04510, .end = 0x00a04538 },
	{ .start = 0x00a04540, .end = 0x00a04548 },
	{ .start = 0x00a04560, .end = 0x00a0457c },
	{ .start = 0x00a04590, .end = 0x00a04598 },
	{ .start = 0x00a045c0, .end = 0x00a045f4 },
};

static const struct iwl_prph_range iwl_prph_dump_addr_9000[] = {
	{ .start = 0x00a05c00, .end = 0x00a05c18 },
	{ .start = 0x00a05400, .end = 0x00a056e8 },
	{ .start = 0x00a08000, .end = 0x00a098bc },
	{ .start = 0x00a02400, .end = 0x00a02758 },
};

static void _iwl_read_prph_block(struct iwl_trans *trans, u32 start,
				 u32 len_bytes, __le32 *data)
{
	u32 i;

	for (i = 0; i < len_bytes; i += 4)
		*data++ = cpu_to_le32(iwl_read_prph_no_grab(trans, start + i));
}

static bool iwl_read_prph_block(struct iwl_trans *trans, u32 start,
				u32 len_bytes, __le32 *data)
{
	unsigned long flags;
	bool success = false;

	if (iwl_trans_grab_nic_access(trans, &flags)) {
		success = true;
		_iwl_read_prph_block(trans, start, len_bytes, data);
		iwl_trans_release_nic_access(trans, &flags);
	}

	return success;
}

static void iwl_dump_prph(struct iwl_trans *trans,
			  struct iwl_fw_error_dump_data **data,
			  const struct iwl_prph_range *iwl_prph_dump_addr,
			  u32 range_len)
{
	struct iwl_fw_error_dump_prph *prph;
	unsigned long flags;
	u32 i;

	if (!iwl_trans_grab_nic_access(trans, &flags))
		return;

	for (i = 0; i < range_len; i++) {
		/* The range includes both boundaries */
		int num_bytes_in_chunk = iwl_prph_dump_addr[i].end -
			 iwl_prph_dump_addr[i].start + 4;

		(*data)->type = cpu_to_le32(IWL_FW_ERROR_DUMP_PRPH);
		(*data)->len = cpu_to_le32(sizeof(*prph) +
					num_bytes_in_chunk);
		prph = (void *)(*data)->data;
		prph->prph_start = cpu_to_le32(iwl_prph_dump_addr[i].start);

		_iwl_read_prph_block(trans, iwl_prph_dump_addr[i].start,
				     /* our range is inclusive, hence + 4 */
				     iwl_prph_dump_addr[i].end -
				     iwl_prph_dump_addr[i].start + 4,
				     (void *)prph->data);

		*data = iwl_fw_error_next_data(*data);
	}

	iwl_trans_release_nic_access(trans, &flags);
}

/*
 * alloc_sgtable - allocates scallerlist table in the given size,
 * fills it with pages and returns it
 * @size: the size (in bytes) of the table
*/
static struct scatterlist *alloc_sgtable(int size)
{
	int alloc_size, nents, i;
	struct page *new_page;
	struct scatterlist *iter;
	struct scatterlist *table;

	nents = DIV_ROUND_UP(size, PAGE_SIZE);
	table = kcalloc(nents, sizeof(*table), GFP_KERNEL);
	if (!table)
		return NULL;
	sg_init_table(table, nents);
	iter = table;
	for_each_sg(table, iter, sg_nents(table), i) {
		new_page = alloc_page(GFP_KERNEL);
		if (!new_page) {
			/* release all previous allocated pages in the table */
			iter = table;
			for_each_sg(table, iter, sg_nents(table), i) {
				new_page = sg_page(iter);
				if (new_page)
					__free_page(new_page);
			}
			return NULL;
		}
		alloc_size = min_t(int, size, PAGE_SIZE);
		size -= PAGE_SIZE;
		sg_set_page(iter, new_page, alloc_size, 0);
	}
	return table;
}

void iwl_mvm_fw_error_dump(struct iwl_mvm *mvm)
{
	struct iwl_fw_error_dump_file *dump_file;
	struct iwl_fw_error_dump_data *dump_data;
	struct iwl_fw_error_dump_info *dump_info;
	struct iwl_fw_error_dump_mem *dump_mem;
	struct iwl_fw_error_dump_trigger_desc *dump_trig;
	struct iwl_mvm_dump_ptrs *fw_error_dump;
	struct scatterlist *sg_dump_data;
	u32 sram_len, sram_ofs;
	const struct iwl_fw_dbg_mem_seg_tlv *fw_dbg_mem = mvm->fw->dbg_mem_tlv;
	u32 file_len, fifo_data_len = 0, prph_len = 0, radio_len = 0;
	u32 smem_len = mvm->fw->n_dbg_mem_tlv ? 0 : mvm->cfg->smem_len;
	u32 sram2_len = mvm->fw->n_dbg_mem_tlv ? 0 : mvm->cfg->dccm2_len;
	bool monitor_dump_only = false;
	int i;

	if (!IWL_MVM_COLLECT_FW_ERR_DUMP &&
	    !mvm->trans->dbg_dest_tlv)
		return;

	lockdep_assert_held(&mvm->mutex);

	/* there's no point in fw dump if the bus is dead */
	if (test_bit(STATUS_TRANS_DEAD, &mvm->trans->status)) {
		IWL_ERR(mvm, "Skip fw error dump since bus is dead\n");
		goto out;
	}

	if (mvm->fw_dump_trig &&
	    mvm->fw_dump_trig->mode & IWL_FW_DBG_TRIGGER_MONITOR_ONLY)
		monitor_dump_only = true;

	fw_error_dump = kzalloc(sizeof(*fw_error_dump), GFP_KERNEL);
	if (!fw_error_dump)
		goto out;

	/* SRAM - include stack CCM if driver knows the values for it */
	if (!mvm->cfg->dccm_offset || !mvm->cfg->dccm_len) {
		const struct fw_img *img;

		img = &mvm->fw->img[mvm->cur_ucode];
		sram_ofs = img->sec[IWL_UCODE_SECTION_DATA].offset;
		sram_len = img->sec[IWL_UCODE_SECTION_DATA].len;
	} else {
		sram_ofs = mvm->cfg->dccm_offset;
		sram_len = mvm->cfg->dccm_len;
	}

	/* reading RXF/TXF sizes */
	if (test_bit(STATUS_FW_ERROR, &mvm->trans->status)) {
		struct iwl_mvm_shared_mem_cfg *mem_cfg = &mvm->smem_cfg;

		fifo_data_len = 0;

		/* Count RXF2 size */
		if (mem_cfg->rxfifo2_size) {
			/* Add header info */
			fifo_data_len += mem_cfg->rxfifo2_size +
					 sizeof(*dump_data) +
					 sizeof(struct iwl_fw_error_dump_fifo);
		}

		/* Count RXF1 sizes */
		for (i = 0; i < mem_cfg->num_lmacs; i++) {
			if (!mem_cfg->lmac[i].rxfifo1_size)
				continue;

			/* Add header info */
			fifo_data_len += mem_cfg->lmac[i].rxfifo1_size +
					 sizeof(*dump_data) +
					 sizeof(struct iwl_fw_error_dump_fifo);
		}

		/* Count TXF sizes */
		for (i = 0; i < mem_cfg->num_lmacs; i++) {
			int j;

			for (j = 0; j < mem_cfg->num_txfifo_entries; j++) {
				if (!mem_cfg->lmac[i].txfifo_size[j])
					continue;

				/* Add header info */
				fifo_data_len +=
					mem_cfg->lmac[i].txfifo_size[j] +
					sizeof(*dump_data) +
					sizeof(struct iwl_fw_error_dump_fifo);
			}
		}

		if (fw_has_capa(&mvm->fw->ucode_capa,
				IWL_UCODE_TLV_CAPA_EXTEND_SHARED_MEM_CFG)) {
			for (i = 0;
			     i < ARRAY_SIZE(mem_cfg->internal_txfifo_size);
			     i++) {
				if (!mem_cfg->internal_txfifo_size[i])
					continue;

				/* Add header info */
				fifo_data_len +=
					mem_cfg->internal_txfifo_size[i] +
					sizeof(*dump_data) +
					sizeof(struct iwl_fw_error_dump_fifo);
			}
		}

		/* Make room for PRPH registers */
		for (i = 0; i < ARRAY_SIZE(iwl_prph_dump_addr_comm); i++) {
			/* The range includes both boundaries */
			int num_bytes_in_chunk =
				iwl_prph_dump_addr_comm[i].end -
				iwl_prph_dump_addr_comm[i].start + 4;

			prph_len += sizeof(*dump_data) +
				sizeof(struct iwl_fw_error_dump_prph) +
				num_bytes_in_chunk;
		}

		if (mvm->cfg->mq_rx_supported) {
			for (i = 0; i <
				ARRAY_SIZE(iwl_prph_dump_addr_9000); i++) {
				/* The range includes both boundaries */
				int num_bytes_in_chunk =
					iwl_prph_dump_addr_9000[i].end -
					iwl_prph_dump_addr_9000[i].start + 4;

				prph_len += sizeof(*dump_data) +
					sizeof(struct iwl_fw_error_dump_prph) +
					num_bytes_in_chunk;
			}
		}

		if (mvm->cfg->device_family == IWL_DEVICE_FAMILY_7000)
			radio_len = sizeof(*dump_data) + RADIO_REG_MAX_READ;
	}

	file_len = sizeof(*dump_file) +
		   sizeof(*dump_data) * 2 +
		   fifo_data_len +
		   prph_len +
		   radio_len +
		   sizeof(*dump_info);

	/* Make room for the SMEM, if it exists */
	if (smem_len)
		file_len += sizeof(*dump_data) + sizeof(*dump_mem) + smem_len;

	/* Make room for the secondary SRAM, if it exists */
	if (sram2_len)
		file_len += sizeof(*dump_data) + sizeof(*dump_mem) + sram2_len;

	/* Make room for MEM segments */
	for (i = 0; i < mvm->fw->n_dbg_mem_tlv; i++) {
		file_len += sizeof(*dump_data) + sizeof(*dump_mem) +
			    le32_to_cpu(fw_dbg_mem[i].len);
	}

	/* Make room for fw's virtual image pages, if it exists */
	if (mvm->fw->img[mvm->cur_ucode].paging_mem_size &&
	    mvm->fw_paging_db[0].fw_paging_block)
		file_len += mvm->num_of_paging_blk *
			(sizeof(*dump_data) +
			 sizeof(struct iwl_fw_error_dump_paging) +
			 PAGING_BLOCK_SIZE);

	/* If we only want a monitor dump, reset the file length */
	if (monitor_dump_only) {
		file_len = sizeof(*dump_file) + sizeof(*dump_data) +
			   sizeof(*dump_info);
	}

	/*
	 * In 8000 HW family B-step include the ICCM (which resides separately)
	 */
	if (mvm->cfg->device_family == IWL_DEVICE_FAMILY_8000 &&
	    CSR_HW_REV_STEP(mvm->trans->hw_rev) == SILICON_B_STEP)
		file_len += sizeof(*dump_data) + sizeof(*dump_mem) +
			    IWL8260_ICCM_LEN;

	if (mvm->fw_dump_desc)
		file_len += sizeof(*dump_data) + sizeof(*dump_trig) +
			    mvm->fw_dump_desc->len;

	if (!mvm->fw->n_dbg_mem_tlv)
		file_len += sram_len + sizeof(*dump_mem);

	dump_file = vzalloc(file_len);
	if (!dump_file) {
		kfree(fw_error_dump);
		goto out;
	}

	fw_error_dump->op_mode_ptr = dump_file;

	dump_file->barker = cpu_to_le32(IWL_FW_ERROR_DUMP_BARKER);
	dump_data = (void *)dump_file->data;

	dump_data->type = cpu_to_le32(IWL_FW_ERROR_DUMP_DEV_FW_INFO);
	dump_data->len = cpu_to_le32(sizeof(*dump_info));
	dump_info = (void *)dump_data->data;
	dump_info->device_family =
		mvm->cfg->device_family == IWL_DEVICE_FAMILY_7000 ?
			cpu_to_le32(IWL_FW_ERROR_DUMP_FAMILY_7) :
			cpu_to_le32(IWL_FW_ERROR_DUMP_FAMILY_8);
	dump_info->hw_step = cpu_to_le32(CSR_HW_REV_STEP(mvm->trans->hw_rev));
	memcpy(dump_info->fw_human_readable, mvm->fw->human_readable,
	       sizeof(dump_info->fw_human_readable));
	strncpy(dump_info->dev_human_readable, mvm->cfg->name,
		sizeof(dump_info->dev_human_readable));
	strncpy(dump_info->bus_human_readable, mvm->dev->bus->name,
		sizeof(dump_info->bus_human_readable));

	dump_data = iwl_fw_error_next_data(dump_data);
	/* We only dump the FIFOs if the FW is in error state */
	if (test_bit(STATUS_FW_ERROR, &mvm->trans->status)) {
		iwl_mvm_dump_fifos(mvm, &dump_data);
		if (radio_len)
			iwl_mvm_read_radio_reg(mvm, &dump_data);
	}

	if (mvm->fw_dump_desc) {
		dump_data->type = cpu_to_le32(IWL_FW_ERROR_DUMP_ERROR_INFO);
		dump_data->len = cpu_to_le32(sizeof(*dump_trig) +
					     mvm->fw_dump_desc->len);
		dump_trig = (void *)dump_data->data;
		memcpy(dump_trig, &mvm->fw_dump_desc->trig_desc,
		       sizeof(*dump_trig) + mvm->fw_dump_desc->len);

		dump_data = iwl_fw_error_next_data(dump_data);
	}

	/* In case we only want monitor dump, skip to dump trasport data */
	if (monitor_dump_only)
		goto dump_trans_data;

	if (!mvm->fw->n_dbg_mem_tlv) {
		dump_data->type = cpu_to_le32(IWL_FW_ERROR_DUMP_MEM);
		dump_data->len = cpu_to_le32(sram_len + sizeof(*dump_mem));
		dump_mem = (void *)dump_data->data;
		dump_mem->type = cpu_to_le32(IWL_FW_ERROR_DUMP_MEM_SRAM);
		dump_mem->offset = cpu_to_le32(sram_ofs);
		iwl_trans_read_mem_bytes(mvm->trans, sram_ofs, dump_mem->data,
					 sram_len);
		dump_data = iwl_fw_error_next_data(dump_data);
	}

	for (i = 0; i < mvm->fw->n_dbg_mem_tlv; i++) {
		u32 len = le32_to_cpu(fw_dbg_mem[i].len);
		u32 ofs = le32_to_cpu(fw_dbg_mem[i].ofs);
		bool success;

		dump_data->type = cpu_to_le32(IWL_FW_ERROR_DUMP_MEM);
		dump_data->len = cpu_to_le32(len + sizeof(*dump_mem));
		dump_mem = (void *)dump_data->data;
		dump_mem->type = fw_dbg_mem[i].data_type;
		dump_mem->offset = cpu_to_le32(ofs);

		switch (dump_mem->type & cpu_to_le32(FW_DBG_MEM_TYPE_MASK)) {
		case cpu_to_le32(FW_DBG_MEM_TYPE_REGULAR):
			iwl_trans_read_mem_bytes(mvm->trans, ofs,
						 dump_mem->data,
						 len);
			success = true;
			break;
		case cpu_to_le32(FW_DBG_MEM_TYPE_PRPH):
			success = iwl_read_prph_block(mvm->trans, ofs, len,
						      (void *)dump_mem->data);
			break;
		default:
			/*
			 * shouldn't get here, we ignored this kind
			 * of TLV earlier during the TLV parsing?!
			 */
			WARN_ON(1);
			success = false;
		}

		if (success)
			dump_data = iwl_fw_error_next_data(dump_data);
	}

	if (smem_len) {
		dump_data->type = cpu_to_le32(IWL_FW_ERROR_DUMP_MEM);
		dump_data->len = cpu_to_le32(smem_len + sizeof(*dump_mem));
		dump_mem = (void *)dump_data->data;
		dump_mem->type = cpu_to_le32(IWL_FW_ERROR_DUMP_MEM_SMEM);
		dump_mem->offset = cpu_to_le32(mvm->cfg->smem_offset);
		iwl_trans_read_mem_bytes(mvm->trans, mvm->cfg->smem_offset,
					 dump_mem->data, smem_len);
		dump_data = iwl_fw_error_next_data(dump_data);
	}

	if (sram2_len) {
		dump_data->type = cpu_to_le32(IWL_FW_ERROR_DUMP_MEM);
		dump_data->len = cpu_to_le32(sram2_len + sizeof(*dump_mem));
		dump_mem = (void *)dump_data->data;
		dump_mem->type = cpu_to_le32(IWL_FW_ERROR_DUMP_MEM_SRAM);
		dump_mem->offset = cpu_to_le32(mvm->cfg->dccm2_offset);
		iwl_trans_read_mem_bytes(mvm->trans, mvm->cfg->dccm2_offset,
					 dump_mem->data, sram2_len);
		dump_data = iwl_fw_error_next_data(dump_data);
	}

	if (mvm->cfg->device_family == IWL_DEVICE_FAMILY_8000 &&
	    CSR_HW_REV_STEP(mvm->trans->hw_rev) == SILICON_B_STEP) {
		dump_data->type = cpu_to_le32(IWL_FW_ERROR_DUMP_MEM);
		dump_data->len = cpu_to_le32(IWL8260_ICCM_LEN +
					     sizeof(*dump_mem));
		dump_mem = (void *)dump_data->data;
		dump_mem->type = cpu_to_le32(IWL_FW_ERROR_DUMP_MEM_SRAM);
		dump_mem->offset = cpu_to_le32(IWL8260_ICCM_OFFSET);
		iwl_trans_read_mem_bytes(mvm->trans, IWL8260_ICCM_OFFSET,
					 dump_mem->data, IWL8260_ICCM_LEN);
		dump_data = iwl_fw_error_next_data(dump_data);
	}

	/* Dump fw's virtual image */
	if (mvm->fw->img[mvm->cur_ucode].paging_mem_size &&
	    mvm->fw_paging_db[0].fw_paging_block) {
		for (i = 1; i < mvm->num_of_paging_blk + 1; i++) {
			struct iwl_fw_error_dump_paging *paging;
			struct page *pages =
				mvm->fw_paging_db[i].fw_paging_block;
			dma_addr_t addr = mvm->fw_paging_db[i].fw_paging_phys;

			dump_data->type = cpu_to_le32(IWL_FW_ERROR_DUMP_PAGING);
			dump_data->len = cpu_to_le32(sizeof(*paging) +
						     PAGING_BLOCK_SIZE);
			paging = (void *)dump_data->data;
			paging->index = cpu_to_le32(i);
			dma_sync_single_for_cpu(mvm->trans->dev, addr,
						PAGING_BLOCK_SIZE,
						DMA_BIDIRECTIONAL);
			memcpy(paging->data, page_address(pages),
			       PAGING_BLOCK_SIZE);
			dump_data = iwl_fw_error_next_data(dump_data);
		}
	}

	if (prph_len) {
		iwl_dump_prph(mvm->trans, &dump_data,
			      iwl_prph_dump_addr_comm,
			      ARRAY_SIZE(iwl_prph_dump_addr_comm));

		if (mvm->cfg->mq_rx_supported)
			iwl_dump_prph(mvm->trans, &dump_data,
				      iwl_prph_dump_addr_9000,
				      ARRAY_SIZE(iwl_prph_dump_addr_9000));
	}

dump_trans_data:
	fw_error_dump->trans_ptr = iwl_trans_dump_data(mvm->trans,
						       mvm->fw_dump_trig);
	fw_error_dump->op_mode_len = file_len;
	if (fw_error_dump->trans_ptr)
		file_len += fw_error_dump->trans_ptr->len;
	dump_file->file_len = cpu_to_le32(file_len);

	sg_dump_data = alloc_sgtable(file_len);
	if (sg_dump_data) {
		sg_pcopy_from_buffer(sg_dump_data,
				     sg_nents(sg_dump_data),
				     fw_error_dump->op_mode_ptr,
				     fw_error_dump->op_mode_len, 0);
		if (fw_error_dump->trans_ptr)
			sg_pcopy_from_buffer(sg_dump_data,
					     sg_nents(sg_dump_data),
					     fw_error_dump->trans_ptr->data,
					     fw_error_dump->trans_ptr->len,
					     fw_error_dump->op_mode_len);
		dev_coredumpsg(mvm->trans->dev, sg_dump_data, file_len,
			       GFP_KERNEL);
	}
	vfree(fw_error_dump->op_mode_ptr);
	vfree(fw_error_dump->trans_ptr);
	kfree(fw_error_dump);

out:
	iwl_mvm_free_fw_dump_desc(mvm);
	mvm->fw_dump_trig = NULL;
	clear_bit(IWL_MVM_STATUS_DUMPING_FW_LOG, &mvm->status);
}

const struct iwl_mvm_dump_desc iwl_mvm_dump_desc_assert = {
	.trig_desc = {
		.type = cpu_to_le32(FW_DBG_TRIGGER_FW_ASSERT),
	},
};

int iwl_mvm_fw_dbg_collect_desc(struct iwl_mvm *mvm,
				const struct iwl_mvm_dump_desc *desc,
				const struct iwl_fw_dbg_trigger_tlv *trigger)
{
	unsigned int delay = 0;

	if (trigger)
		delay = msecs_to_jiffies(le32_to_cpu(trigger->stop_delay));

	if (test_and_set_bit(IWL_MVM_STATUS_DUMPING_FW_LOG, &mvm->status))
		return -EBUSY;

	if (WARN_ON(mvm->fw_dump_desc))
		iwl_mvm_free_fw_dump_desc(mvm);

	IWL_WARN(mvm, "Collecting data: trigger %d fired.\n",
		 le32_to_cpu(desc->trig_desc.type));

	mvm->fw_dump_desc = desc;
	mvm->fw_dump_trig = trigger;

	queue_delayed_work(system_wq, &mvm->fw_dump_wk, delay);

	return 0;
}

int iwl_mvm_fw_dbg_collect(struct iwl_mvm *mvm, enum iwl_fw_dbg_trigger trig,
			   const char *str, size_t len,
			   const struct iwl_fw_dbg_trigger_tlv *trigger)
{
	struct iwl_mvm_dump_desc *desc;

	desc = kzalloc(sizeof(*desc) + len, GFP_ATOMIC);
	if (!desc)
		return -ENOMEM;

	desc->len = len;
	desc->trig_desc.type = cpu_to_le32(trig);
	memcpy(desc->trig_desc.data, str, len);

	return iwl_mvm_fw_dbg_collect_desc(mvm, desc, trigger);
}

int iwl_mvm_fw_dbg_collect_trig(struct iwl_mvm *mvm,
				struct iwl_fw_dbg_trigger_tlv *trigger,
				const char *fmt, ...)
{
	u16 occurrences = le16_to_cpu(trigger->occurrences);
	int ret, len = 0;
	char buf[64];

	if (!occurrences)
		return 0;

	if (fmt) {
		va_list ap;

		buf[sizeof(buf) - 1] = '\0';

		va_start(ap, fmt);
		vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);

		/* check for truncation */
		if (WARN_ON_ONCE(buf[sizeof(buf) - 1]))
			buf[sizeof(buf) - 1] = '\0';

		len = strlen(buf) + 1;
	}

	ret = iwl_mvm_fw_dbg_collect(mvm, le32_to_cpu(trigger->id), buf, len,
				     trigger);

	if (ret)
		return ret;

	trigger->occurrences = cpu_to_le16(occurrences - 1);
	return 0;
}

int iwl_mvm_start_fw_dbg_conf(struct iwl_mvm *mvm, u8 conf_id)
{
	u8 *ptr;
	int ret;
	int i;

	if (WARN_ONCE(conf_id >= ARRAY_SIZE(mvm->fw->dbg_conf_tlv),
		      "Invalid configuration %d\n", conf_id))
		return -EINVAL;

	/* EARLY START - firmware's configuration is hard coded */
	if ((!mvm->fw->dbg_conf_tlv[conf_id] ||
	     !mvm->fw->dbg_conf_tlv[conf_id]->num_of_hcmds) &&
	    conf_id == FW_DBG_START_FROM_ALIVE)
		return 0;

	if (!mvm->fw->dbg_conf_tlv[conf_id])
		return -EINVAL;

	if (mvm->fw_dbg_conf != FW_DBG_INVALID)
		IWL_WARN(mvm, "FW already configured (%d) - re-configuring\n",
			 mvm->fw_dbg_conf);

	/* Send all HCMDs for configuring the FW debug */
	ptr = (void *)&mvm->fw->dbg_conf_tlv[conf_id]->hcmd;
	for (i = 0; i < mvm->fw->dbg_conf_tlv[conf_id]->num_of_hcmds; i++) {
		struct iwl_fw_dbg_conf_hcmd *cmd = (void *)ptr;

		ret = iwl_mvm_send_cmd_pdu(mvm, cmd->id, 0,
					   le16_to_cpu(cmd->len), cmd->data);
		if (ret)
			return ret;

		ptr += sizeof(*cmd);
		ptr += le16_to_cpu(cmd->len);
	}

	mvm->fw_dbg_conf = conf_id;

	return 0;
}

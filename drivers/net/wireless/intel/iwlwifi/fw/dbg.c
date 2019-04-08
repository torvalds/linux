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
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
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
 *
 *****************************************************************************/
#include <linux/devcoredump.h>
#include "iwl-drv.h"
#include "runtime.h"
#include "dbg.h"
#include "debugfs.h"
#include "iwl-io.h"
#include "iwl-prph.h"
#include "iwl-csr.h"

/**
 * struct iwl_fw_dump_ptrs - set of pointers needed for the fw-error-dump
 *
 * @fwrt_ptr: pointer to the buffer coming from fwrt
 * @trans_ptr: pointer to struct %iwl_trans_dump_data which contains the
 *	transport's data.
 * @trans_len: length of the valid data in trans_ptr
 * @fwrt_len: length of the valid data in fwrt_ptr
 */
struct iwl_fw_dump_ptrs {
	struct iwl_trans_dump_data *trans_ptr;
	void *fwrt_ptr;
	u32 fwrt_len;
};

#define RADIO_REG_MAX_READ 0x2ad
static void iwl_read_radio_regs(struct iwl_fw_runtime *fwrt,
				struct iwl_fw_error_dump_data **dump_data)
{
	u8 *pos = (void *)(*dump_data)->data;
	unsigned long flags;
	int i;

	IWL_DEBUG_INFO(fwrt, "WRT radio registers dump\n");

	if (!iwl_trans_grab_nic_access(fwrt->trans, &flags))
		return;

	(*dump_data)->type = cpu_to_le32(IWL_FW_ERROR_DUMP_RADIO_REG);
	(*dump_data)->len = cpu_to_le32(RADIO_REG_MAX_READ);

	for (i = 0; i < RADIO_REG_MAX_READ; i++) {
		u32 rd_cmd = RADIO_RSP_RD_CMD;

		rd_cmd |= i << RADIO_RSP_ADDR_POS;
		iwl_write_prph_no_grab(fwrt->trans, RSP_RADIO_CMD, rd_cmd);
		*pos = (u8)iwl_read_prph_no_grab(fwrt->trans, RSP_RADIO_RDDAT);

		pos++;
	}

	*dump_data = iwl_fw_error_next_data(*dump_data);

	iwl_trans_release_nic_access(fwrt->trans, &flags);
}

static void iwl_fwrt_dump_rxf(struct iwl_fw_runtime *fwrt,
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
		cpu_to_le32(iwl_trans_read_prph(fwrt->trans,
						RXF_RD_D_SPACE + offset));
	fifo_hdr->wr_ptr =
		cpu_to_le32(iwl_trans_read_prph(fwrt->trans,
						RXF_RD_WR_PTR + offset));
	fifo_hdr->rd_ptr =
		cpu_to_le32(iwl_trans_read_prph(fwrt->trans,
						RXF_RD_RD_PTR + offset));
	fifo_hdr->fence_ptr =
		cpu_to_le32(iwl_trans_read_prph(fwrt->trans,
						RXF_RD_FENCE_PTR + offset));
	fifo_hdr->fence_mode =
		cpu_to_le32(iwl_trans_read_prph(fwrt->trans,
						RXF_SET_FENCE_MODE + offset));

	/* Lock fence */
	iwl_trans_write_prph(fwrt->trans, RXF_SET_FENCE_MODE + offset, 0x1);
	/* Set fence pointer to the same place like WR pointer */
	iwl_trans_write_prph(fwrt->trans, RXF_LD_WR2FENCE + offset, 0x1);
	/* Set fence offset */
	iwl_trans_write_prph(fwrt->trans,
			     RXF_LD_FENCE_OFFSET_ADDR + offset, 0x0);

	/* Read FIFO */
	fifo_len /= sizeof(u32); /* Size in DWORDS */
	for (i = 0; i < fifo_len; i++)
		fifo_data[i] = iwl_trans_read_prph(fwrt->trans,
						 RXF_FIFO_RD_FENCE_INC +
						 offset);
	*dump_data = iwl_fw_error_next_data(*dump_data);
}

static void iwl_fwrt_dump_txf(struct iwl_fw_runtime *fwrt,
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
		cpu_to_le32(iwl_trans_read_prph(fwrt->trans,
						TXF_FIFO_ITEM_CNT + offset));
	fifo_hdr->wr_ptr =
		cpu_to_le32(iwl_trans_read_prph(fwrt->trans,
						TXF_WR_PTR + offset));
	fifo_hdr->rd_ptr =
		cpu_to_le32(iwl_trans_read_prph(fwrt->trans,
						TXF_RD_PTR + offset));
	fifo_hdr->fence_ptr =
		cpu_to_le32(iwl_trans_read_prph(fwrt->trans,
						TXF_FENCE_PTR + offset));
	fifo_hdr->fence_mode =
		cpu_to_le32(iwl_trans_read_prph(fwrt->trans,
						TXF_LOCK_FENCE + offset));

	/* Set the TXF_READ_MODIFY_ADDR to TXF_WR_PTR */
	iwl_trans_write_prph(fwrt->trans, TXF_READ_MODIFY_ADDR + offset,
			     TXF_WR_PTR + offset);

	/* Dummy-read to advance the read pointer to the head */
	iwl_trans_read_prph(fwrt->trans, TXF_READ_MODIFY_DATA + offset);

	/* Read FIFO */
	fifo_len /= sizeof(u32); /* Size in DWORDS */
	for (i = 0; i < fifo_len; i++)
		fifo_data[i] = iwl_trans_read_prph(fwrt->trans,
						  TXF_READ_MODIFY_DATA +
						  offset);
	*dump_data = iwl_fw_error_next_data(*dump_data);
}

static void iwl_fw_dump_rxf(struct iwl_fw_runtime *fwrt,
			    struct iwl_fw_error_dump_data **dump_data)
{
	struct iwl_fwrt_shared_mem_cfg *cfg = &fwrt->smem_cfg;
	unsigned long flags;

	IWL_DEBUG_INFO(fwrt, "WRT RX FIFO dump\n");

	if (!iwl_trans_grab_nic_access(fwrt->trans, &flags))
		return;

	if (iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_RXF)) {
		/* Pull RXF1 */
		iwl_fwrt_dump_rxf(fwrt, dump_data,
				  cfg->lmac[0].rxfifo1_size, 0, 0);
		/* Pull RXF2 */
		iwl_fwrt_dump_rxf(fwrt, dump_data, cfg->rxfifo2_size,
				  RXF_DIFF_FROM_PREV +
				  fwrt->trans->cfg->umac_prph_offset, 1);
		/* Pull LMAC2 RXF1 */
		if (fwrt->smem_cfg.num_lmacs > 1)
			iwl_fwrt_dump_rxf(fwrt, dump_data,
					  cfg->lmac[1].rxfifo1_size,
					  LMAC2_PRPH_OFFSET, 2);
	}

	iwl_trans_release_nic_access(fwrt->trans, &flags);
}

static void iwl_fw_dump_txf(struct iwl_fw_runtime *fwrt,
			    struct iwl_fw_error_dump_data **dump_data)
{
	struct iwl_fw_error_dump_fifo *fifo_hdr;
	struct iwl_fwrt_shared_mem_cfg *cfg = &fwrt->smem_cfg;
	u32 *fifo_data;
	u32 fifo_len;
	unsigned long flags;
	int i, j;

	IWL_DEBUG_INFO(fwrt, "WRT TX FIFO dump\n");

	if (!iwl_trans_grab_nic_access(fwrt->trans, &flags))
		return;

	if (iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_TXF)) {
		/* Pull TXF data from LMAC1 */
		for (i = 0; i < fwrt->smem_cfg.num_txfifo_entries; i++) {
			/* Mark the number of TXF we're pulling now */
			iwl_trans_write_prph(fwrt->trans, TXF_LARC_NUM, i);
			iwl_fwrt_dump_txf(fwrt, dump_data,
					  cfg->lmac[0].txfifo_size[i], 0, i);
		}

		/* Pull TXF data from LMAC2 */
		if (fwrt->smem_cfg.num_lmacs > 1) {
			for (i = 0; i < fwrt->smem_cfg.num_txfifo_entries;
			     i++) {
				/* Mark the number of TXF we're pulling now */
				iwl_trans_write_prph(fwrt->trans,
						     TXF_LARC_NUM +
						     LMAC2_PRPH_OFFSET, i);
				iwl_fwrt_dump_txf(fwrt, dump_data,
						  cfg->lmac[1].txfifo_size[i],
						  LMAC2_PRPH_OFFSET,
						  i + cfg->num_txfifo_entries);
			}
		}
	}

	if (iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_INTERNAL_TXF) &&
	    fw_has_capa(&fwrt->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_EXTEND_SHARED_MEM_CFG)) {
		/* Pull UMAC internal TXF data from all TXFs */
		for (i = 0;
		     i < ARRAY_SIZE(fwrt->smem_cfg.internal_txfifo_size);
		     i++) {
			fifo_hdr = (void *)(*dump_data)->data;
			fifo_data = (void *)fifo_hdr->data;
			fifo_len = fwrt->smem_cfg.internal_txfifo_size[i];

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
			iwl_trans_write_prph(fwrt->trans, TXF_CPU2_NUM, i +
				fwrt->smem_cfg.num_txfifo_entries);

			fifo_hdr->available_bytes =
				cpu_to_le32(iwl_trans_read_prph(fwrt->trans,
								TXF_CPU2_FIFO_ITEM_CNT));
			fifo_hdr->wr_ptr =
				cpu_to_le32(iwl_trans_read_prph(fwrt->trans,
								TXF_CPU2_WR_PTR));
			fifo_hdr->rd_ptr =
				cpu_to_le32(iwl_trans_read_prph(fwrt->trans,
								TXF_CPU2_RD_PTR));
			fifo_hdr->fence_ptr =
				cpu_to_le32(iwl_trans_read_prph(fwrt->trans,
								TXF_CPU2_FENCE_PTR));
			fifo_hdr->fence_mode =
				cpu_to_le32(iwl_trans_read_prph(fwrt->trans,
								TXF_CPU2_LOCK_FENCE));

			/* Set TXF_CPU2_READ_MODIFY_ADDR to TXF_CPU2_WR_PTR */
			iwl_trans_write_prph(fwrt->trans,
					     TXF_CPU2_READ_MODIFY_ADDR,
					     TXF_CPU2_WR_PTR);

			/* Dummy-read to advance the read pointer to head */
			iwl_trans_read_prph(fwrt->trans,
					    TXF_CPU2_READ_MODIFY_DATA);

			/* Read FIFO */
			fifo_len /= sizeof(u32); /* Size in DWORDS */
			for (j = 0; j < fifo_len; j++)
				fifo_data[j] =
					iwl_trans_read_prph(fwrt->trans,
							    TXF_CPU2_READ_MODIFY_DATA);
			*dump_data = iwl_fw_error_next_data(*dump_data);
		}
	}

	iwl_trans_release_nic_access(fwrt->trans, &flags);
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

static const struct iwl_prph_range iwl_prph_dump_addr_22000[] = {
	{ .start = 0x00a00000, .end = 0x00a00000 },
	{ .start = 0x00a0000c, .end = 0x00a00024 },
	{ .start = 0x00a0002c, .end = 0x00a00034 },
	{ .start = 0x00a0003c, .end = 0x00a0003c },
	{ .start = 0x00a00410, .end = 0x00a00418 },
	{ .start = 0x00a00420, .end = 0x00a00420 },
	{ .start = 0x00a00428, .end = 0x00a00428 },
	{ .start = 0x00a00430, .end = 0x00a0043c },
	{ .start = 0x00a00444, .end = 0x00a00444 },
	{ .start = 0x00a00840, .end = 0x00a00840 },
	{ .start = 0x00a00850, .end = 0x00a00858 },
	{ .start = 0x00a01004, .end = 0x00a01008 },
	{ .start = 0x00a01010, .end = 0x00a01010 },
	{ .start = 0x00a01018, .end = 0x00a01018 },
	{ .start = 0x00a01024, .end = 0x00a01024 },
	{ .start = 0x00a0102c, .end = 0x00a01034 },
	{ .start = 0x00a0103c, .end = 0x00a01040 },
	{ .start = 0x00a01048, .end = 0x00a01050 },
	{ .start = 0x00a01058, .end = 0x00a01058 },
	{ .start = 0x00a01060, .end = 0x00a01070 },
	{ .start = 0x00a0108c, .end = 0x00a0108c },
	{ .start = 0x00a01c20, .end = 0x00a01c28 },
	{ .start = 0x00a01d10, .end = 0x00a01d10 },
	{ .start = 0x00a01e28, .end = 0x00a01e2c },
	{ .start = 0x00a01e60, .end = 0x00a01e60 },
	{ .start = 0x00a01e80, .end = 0x00a01e80 },
	{ .start = 0x00a01ea0, .end = 0x00a01ea0 },
	{ .start = 0x00a02000, .end = 0x00a0201c },
	{ .start = 0x00a02024, .end = 0x00a02024 },
	{ .start = 0x00a02040, .end = 0x00a02048 },
	{ .start = 0x00a020c0, .end = 0x00a020e0 },
	{ .start = 0x00a02400, .end = 0x00a02404 },
	{ .start = 0x00a0240c, .end = 0x00a02414 },
	{ .start = 0x00a0241c, .end = 0x00a0243c },
	{ .start = 0x00a02448, .end = 0x00a024bc },
	{ .start = 0x00a024c4, .end = 0x00a024cc },
	{ .start = 0x00a02508, .end = 0x00a02508 },
	{ .start = 0x00a02510, .end = 0x00a02514 },
	{ .start = 0x00a0251c, .end = 0x00a0251c },
	{ .start = 0x00a0252c, .end = 0x00a0255c },
	{ .start = 0x00a02564, .end = 0x00a025a0 },
	{ .start = 0x00a025a8, .end = 0x00a025b4 },
	{ .start = 0x00a025c0, .end = 0x00a025c0 },
	{ .start = 0x00a025e8, .end = 0x00a025f4 },
	{ .start = 0x00a02c08, .end = 0x00a02c18 },
	{ .start = 0x00a02c2c, .end = 0x00a02c38 },
	{ .start = 0x00a02c68, .end = 0x00a02c78 },
	{ .start = 0x00a03000, .end = 0x00a03000 },
	{ .start = 0x00a03010, .end = 0x00a03014 },
	{ .start = 0x00a0301c, .end = 0x00a0302c },
	{ .start = 0x00a03034, .end = 0x00a03038 },
	{ .start = 0x00a03040, .end = 0x00a03044 },
	{ .start = 0x00a03060, .end = 0x00a03068 },
	{ .start = 0x00a03070, .end = 0x00a03070 },
	{ .start = 0x00a0307c, .end = 0x00a03084 },
	{ .start = 0x00a0308c, .end = 0x00a03090 },
	{ .start = 0x00a03098, .end = 0x00a03098 },
	{ .start = 0x00a030a0, .end = 0x00a030a0 },
	{ .start = 0x00a030a8, .end = 0x00a030b4 },
	{ .start = 0x00a030bc, .end = 0x00a030c0 },
	{ .start = 0x00a030c8, .end = 0x00a030f4 },
	{ .start = 0x00a03100, .end = 0x00a0312c },
	{ .start = 0x00a03c00, .end = 0x00a03c5c },
	{ .start = 0x00a04400, .end = 0x00a04454 },
	{ .start = 0x00a04460, .end = 0x00a04474 },
	{ .start = 0x00a044c0, .end = 0x00a044ec },
	{ .start = 0x00a04500, .end = 0x00a04504 },
	{ .start = 0x00a04510, .end = 0x00a04538 },
	{ .start = 0x00a04540, .end = 0x00a04548 },
	{ .start = 0x00a04560, .end = 0x00a04560 },
	{ .start = 0x00a04570, .end = 0x00a0457c },
	{ .start = 0x00a04590, .end = 0x00a04590 },
	{ .start = 0x00a04598, .end = 0x00a04598 },
	{ .start = 0x00a045c0, .end = 0x00a045f4 },
	{ .start = 0x00a05c18, .end = 0x00a05c1c },
	{ .start = 0x00a0c000, .end = 0x00a0c018 },
	{ .start = 0x00a0c020, .end = 0x00a0c028 },
	{ .start = 0x00a0c038, .end = 0x00a0c094 },
	{ .start = 0x00a0c0c0, .end = 0x00a0c104 },
	{ .start = 0x00a0c10c, .end = 0x00a0c118 },
	{ .start = 0x00a0c150, .end = 0x00a0c174 },
	{ .start = 0x00a0c17c, .end = 0x00a0c188 },
	{ .start = 0x00a0c190, .end = 0x00a0c198 },
	{ .start = 0x00a0c1a0, .end = 0x00a0c1a8 },
	{ .start = 0x00a0c1b0, .end = 0x00a0c1b8 },
};

static const struct iwl_prph_range iwl_prph_dump_addr_ax210[] = {
	{ .start = 0x00d03c00, .end = 0x00d03c64 },
	{ .start = 0x00d05c18, .end = 0x00d05c1c },
	{ .start = 0x00d0c000, .end = 0x00d0c174 },
};

static void iwl_read_prph_block(struct iwl_trans *trans, u32 start,
				u32 len_bytes, __le32 *data)
{
	u32 i;

	for (i = 0; i < len_bytes; i += 4)
		*data++ = cpu_to_le32(iwl_read_prph_no_grab(trans, start + i));
}

static void iwl_dump_prph(struct iwl_fw_runtime *fwrt,
			  const struct iwl_prph_range *iwl_prph_dump_addr,
			  u32 range_len, void *ptr)
{
	struct iwl_fw_error_dump_prph *prph;
	struct iwl_trans *trans = fwrt->trans;
	struct iwl_fw_error_dump_data **data =
		(struct iwl_fw_error_dump_data **)ptr;
	unsigned long flags;
	u32 i;

	if (!data)
		return;

	IWL_DEBUG_INFO(trans, "WRT PRPH dump\n");

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

		iwl_read_prph_block(trans, iwl_prph_dump_addr[i].start,
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

static void iwl_fw_get_prph_len(struct iwl_fw_runtime *fwrt,
				const struct iwl_prph_range *iwl_prph_dump_addr,
				u32 range_len, void *ptr)
{
	u32 *prph_len = (u32 *)ptr;
	int i, num_bytes_in_chunk;

	if (!prph_len)
		return;

	for (i = 0; i < range_len; i++) {
		/* The range includes both boundaries */
		num_bytes_in_chunk =
			iwl_prph_dump_addr[i].end -
			iwl_prph_dump_addr[i].start + 4;

		*prph_len += sizeof(struct iwl_fw_error_dump_data) +
			sizeof(struct iwl_fw_error_dump_prph) +
			num_bytes_in_chunk;
	}
}

static void iwl_fw_prph_handler(struct iwl_fw_runtime *fwrt, void *ptr,
				void (*handler)(struct iwl_fw_runtime *,
						const struct iwl_prph_range *,
						u32, void *))
{
	u32 range_len;

	if (fwrt->trans->cfg->device_family >= IWL_DEVICE_FAMILY_AX210) {
		range_len = ARRAY_SIZE(iwl_prph_dump_addr_ax210);
		handler(fwrt, iwl_prph_dump_addr_ax210, range_len, ptr);
	} else if (fwrt->trans->cfg->device_family >= IWL_DEVICE_FAMILY_22000) {
		range_len = ARRAY_SIZE(iwl_prph_dump_addr_22000);
		handler(fwrt, iwl_prph_dump_addr_22000, range_len, ptr);
	} else {
		range_len = ARRAY_SIZE(iwl_prph_dump_addr_comm);
		handler(fwrt, iwl_prph_dump_addr_comm, range_len, ptr);

		if (fwrt->trans->cfg->mq_rx_supported) {
			range_len = ARRAY_SIZE(iwl_prph_dump_addr_9000);
			handler(fwrt, iwl_prph_dump_addr_9000, range_len, ptr);
		}
	}
}

static void iwl_fw_dump_mem(struct iwl_fw_runtime *fwrt,
			    struct iwl_fw_error_dump_data **dump_data,
			    u32 len, u32 ofs, u32 type)
{
	struct iwl_fw_error_dump_mem *dump_mem;

	if (!len)
		return;

	(*dump_data)->type = cpu_to_le32(IWL_FW_ERROR_DUMP_MEM);
	(*dump_data)->len = cpu_to_le32(len + sizeof(*dump_mem));
	dump_mem = (void *)(*dump_data)->data;
	dump_mem->type = cpu_to_le32(type);
	dump_mem->offset = cpu_to_le32(ofs);
	iwl_trans_read_mem_bytes(fwrt->trans, ofs, dump_mem->data, len);
	*dump_data = iwl_fw_error_next_data(*dump_data);

	IWL_DEBUG_INFO(fwrt, "WRT memory dump. Type=%u\n", dump_mem->type);
}

#define ADD_LEN(len, item_len, const_len) \
	do {size_t item = item_len; len += (!!item) * const_len + item; } \
	while (0)

static int iwl_fw_rxf_len(struct iwl_fw_runtime *fwrt,
			  struct iwl_fwrt_shared_mem_cfg *mem_cfg)
{
	size_t hdr_len = sizeof(struct iwl_fw_error_dump_data) +
			 sizeof(struct iwl_fw_error_dump_fifo);
	u32 fifo_len = 0;
	int i;

	if (!iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_RXF))
		return 0;

	/* Count RXF2 size */
	ADD_LEN(fifo_len, mem_cfg->rxfifo2_size, hdr_len);

	/* Count RXF1 sizes */
	if (WARN_ON(mem_cfg->num_lmacs > MAX_NUM_LMAC))
		mem_cfg->num_lmacs = MAX_NUM_LMAC;

	for (i = 0; i < mem_cfg->num_lmacs; i++)
		ADD_LEN(fifo_len, mem_cfg->lmac[i].rxfifo1_size, hdr_len);

	return fifo_len;
}

static int iwl_fw_txf_len(struct iwl_fw_runtime *fwrt,
			  struct iwl_fwrt_shared_mem_cfg *mem_cfg)
{
	size_t hdr_len = sizeof(struct iwl_fw_error_dump_data) +
			 sizeof(struct iwl_fw_error_dump_fifo);
	u32 fifo_len = 0;
	int i;

	if (!iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_TXF))
		goto dump_internal_txf;

	/* Count TXF sizes */
	if (WARN_ON(mem_cfg->num_lmacs > MAX_NUM_LMAC))
		mem_cfg->num_lmacs = MAX_NUM_LMAC;

	for (i = 0; i < mem_cfg->num_lmacs; i++) {
		int j;

		for (j = 0; j < mem_cfg->num_txfifo_entries; j++)
			ADD_LEN(fifo_len, mem_cfg->lmac[i].txfifo_size[j],
				hdr_len);
	}

dump_internal_txf:
	if (!(iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_INTERNAL_TXF) &&
	      fw_has_capa(&fwrt->fw->ucode_capa,
			  IWL_UCODE_TLV_CAPA_EXTEND_SHARED_MEM_CFG)))
		goto out;

	for (i = 0; i < ARRAY_SIZE(mem_cfg->internal_txfifo_size); i++)
		ADD_LEN(fifo_len, mem_cfg->internal_txfifo_size[i], hdr_len);

out:
	return fifo_len;
}

static void iwl_dump_paging(struct iwl_fw_runtime *fwrt,
			    struct iwl_fw_error_dump_data **data)
{
	int i;

	IWL_DEBUG_INFO(fwrt, "WRT paging dump\n");
	for (i = 1; i < fwrt->num_of_paging_blk + 1; i++) {
		struct iwl_fw_error_dump_paging *paging;
		struct page *pages =
			fwrt->fw_paging_db[i].fw_paging_block;
		dma_addr_t addr = fwrt->fw_paging_db[i].fw_paging_phys;

		(*data)->type = cpu_to_le32(IWL_FW_ERROR_DUMP_PAGING);
		(*data)->len = cpu_to_le32(sizeof(*paging) +
					     PAGING_BLOCK_SIZE);
		paging =  (void *)(*data)->data;
		paging->index = cpu_to_le32(i);
		dma_sync_single_for_cpu(fwrt->trans->dev, addr,
					PAGING_BLOCK_SIZE,
					DMA_BIDIRECTIONAL);
		memcpy(paging->data, page_address(pages),
		       PAGING_BLOCK_SIZE);
		dma_sync_single_for_device(fwrt->trans->dev, addr,
					   PAGING_BLOCK_SIZE,
					   DMA_BIDIRECTIONAL);
		(*data) = iwl_fw_error_next_data(*data);
	}
}

static struct iwl_fw_error_dump_file *
iwl_fw_error_dump_file(struct iwl_fw_runtime *fwrt,
		       struct iwl_fw_dump_ptrs *fw_error_dump)
{
	struct iwl_fw_error_dump_file *dump_file;
	struct iwl_fw_error_dump_data *dump_data;
	struct iwl_fw_error_dump_info *dump_info;
	struct iwl_fw_error_dump_smem_cfg *dump_smem_cfg;
	struct iwl_fw_error_dump_trigger_desc *dump_trig;
	u32 sram_len, sram_ofs;
	const struct iwl_fw_dbg_mem_seg_tlv *fw_mem = fwrt->fw->dbg.mem_tlv;
	struct iwl_fwrt_shared_mem_cfg *mem_cfg = &fwrt->smem_cfg;
	u32 file_len, fifo_len = 0, prph_len = 0, radio_len = 0;
	u32 smem_len = fwrt->fw->dbg.n_mem_tlv ? 0 : fwrt->trans->cfg->smem_len;
	u32 sram2_len = fwrt->fw->dbg.n_mem_tlv ?
				0 : fwrt->trans->cfg->dccm2_len;
	int i;

	/* SRAM - include stack CCM if driver knows the values for it */
	if (!fwrt->trans->cfg->dccm_offset || !fwrt->trans->cfg->dccm_len) {
		const struct fw_img *img;

		if (fwrt->cur_fw_img >= IWL_UCODE_TYPE_MAX)
			return NULL;
		img = &fwrt->fw->img[fwrt->cur_fw_img];
		sram_ofs = img->sec[IWL_UCODE_SECTION_DATA].offset;
		sram_len = img->sec[IWL_UCODE_SECTION_DATA].len;
	} else {
		sram_ofs = fwrt->trans->cfg->dccm_offset;
		sram_len = fwrt->trans->cfg->dccm_len;
	}

	/* reading RXF/TXF sizes */
	if (test_bit(STATUS_FW_ERROR, &fwrt->trans->status)) {
		fifo_len = iwl_fw_rxf_len(fwrt, mem_cfg);
		fifo_len += iwl_fw_txf_len(fwrt, mem_cfg);

		/* Make room for PRPH registers */
		if (iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_PRPH))
			iwl_fw_prph_handler(fwrt, &prph_len,
					    iwl_fw_get_prph_len);

		if (fwrt->trans->cfg->device_family == IWL_DEVICE_FAMILY_7000 &&
		    iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_RADIO_REG))
			radio_len = sizeof(*dump_data) + RADIO_REG_MAX_READ;
	}

	file_len = sizeof(*dump_file) + fifo_len + prph_len + radio_len;

	if (iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_DEV_FW_INFO))
		file_len += sizeof(*dump_data) + sizeof(*dump_info);
	if (iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_MEM_CFG))
		file_len += sizeof(*dump_data) + sizeof(*dump_smem_cfg);

	if (iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_MEM)) {
		size_t hdr_len = sizeof(*dump_data) +
				 sizeof(struct iwl_fw_error_dump_mem);

		/* Dump SRAM only if no mem_tlvs */
		if (!fwrt->fw->dbg.n_mem_tlv)
			ADD_LEN(file_len, sram_len, hdr_len);

		/* Make room for all mem types that exist */
		ADD_LEN(file_len, smem_len, hdr_len);
		ADD_LEN(file_len, sram2_len, hdr_len);

		for (i = 0; i < fwrt->fw->dbg.n_mem_tlv; i++)
			ADD_LEN(file_len, le32_to_cpu(fw_mem[i].len), hdr_len);
	}

	/* Make room for fw's virtual image pages, if it exists */
	if (iwl_fw_dbg_is_paging_enabled(fwrt))
		file_len += fwrt->num_of_paging_blk *
			(sizeof(*dump_data) +
			 sizeof(struct iwl_fw_error_dump_paging) +
			 PAGING_BLOCK_SIZE);

	if (iwl_fw_dbg_is_d3_debug_enabled(fwrt) && fwrt->dump.d3_debug_data) {
		file_len += sizeof(*dump_data) +
			fwrt->trans->cfg->d3_debug_data_length * 2;
	}

	/* If we only want a monitor dump, reset the file length */
	if (fwrt->dump.monitor_only) {
		file_len = sizeof(*dump_file) + sizeof(*dump_data) * 2 +
			   sizeof(*dump_info) + sizeof(*dump_smem_cfg);
	}

	if (iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_ERROR_INFO) &&
	    fwrt->dump.desc)
		file_len += sizeof(*dump_data) + sizeof(*dump_trig) +
			    fwrt->dump.desc->len;

	dump_file = vzalloc(file_len);
	if (!dump_file)
		return NULL;

	fw_error_dump->fwrt_ptr = dump_file;

	dump_file->barker = cpu_to_le32(IWL_FW_ERROR_DUMP_BARKER);
	dump_data = (void *)dump_file->data;

	if (iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_DEV_FW_INFO)) {
		dump_data->type = cpu_to_le32(IWL_FW_ERROR_DUMP_DEV_FW_INFO);
		dump_data->len = cpu_to_le32(sizeof(*dump_info));
		dump_info = (void *)dump_data->data;
		dump_info->hw_type =
			cpu_to_le32(CSR_HW_REV_TYPE(fwrt->trans->hw_rev));
		dump_info->hw_step =
			cpu_to_le32(CSR_HW_REV_STEP(fwrt->trans->hw_rev));
		memcpy(dump_info->fw_human_readable, fwrt->fw->human_readable,
		       sizeof(dump_info->fw_human_readable));
		strncpy(dump_info->dev_human_readable, fwrt->trans->cfg->name,
			sizeof(dump_info->dev_human_readable) - 1);
		strncpy(dump_info->bus_human_readable, fwrt->dev->bus->name,
			sizeof(dump_info->bus_human_readable) - 1);
		dump_info->num_of_lmacs = fwrt->smem_cfg.num_lmacs;
		dump_info->lmac_err_id[0] =
			cpu_to_le32(fwrt->dump.lmac_err_id[0]);
		if (fwrt->smem_cfg.num_lmacs > 1)
			dump_info->lmac_err_id[1] =
				cpu_to_le32(fwrt->dump.lmac_err_id[1]);
		dump_info->umac_err_id = cpu_to_le32(fwrt->dump.umac_err_id);

		dump_data = iwl_fw_error_next_data(dump_data);
	}

	if (iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_MEM_CFG)) {
		/* Dump shared memory configuration */
		dump_data->type = cpu_to_le32(IWL_FW_ERROR_DUMP_MEM_CFG);
		dump_data->len = cpu_to_le32(sizeof(*dump_smem_cfg));
		dump_smem_cfg = (void *)dump_data->data;
		dump_smem_cfg->num_lmacs = cpu_to_le32(mem_cfg->num_lmacs);
		dump_smem_cfg->num_txfifo_entries =
			cpu_to_le32(mem_cfg->num_txfifo_entries);
		for (i = 0; i < MAX_NUM_LMAC; i++) {
			int j;
			u32 *txf_size = mem_cfg->lmac[i].txfifo_size;

			for (j = 0; j < TX_FIFO_MAX_NUM; j++)
				dump_smem_cfg->lmac[i].txfifo_size[j] =
					cpu_to_le32(txf_size[j]);
			dump_smem_cfg->lmac[i].rxfifo1_size =
				cpu_to_le32(mem_cfg->lmac[i].rxfifo1_size);
		}
		dump_smem_cfg->rxfifo2_size =
			cpu_to_le32(mem_cfg->rxfifo2_size);
		dump_smem_cfg->internal_txfifo_addr =
			cpu_to_le32(mem_cfg->internal_txfifo_addr);
		for (i = 0; i < TX_FIFO_INTERNAL_MAX_NUM; i++) {
			dump_smem_cfg->internal_txfifo_size[i] =
				cpu_to_le32(mem_cfg->internal_txfifo_size[i]);
		}

		dump_data = iwl_fw_error_next_data(dump_data);
	}

	/* We only dump the FIFOs if the FW is in error state */
	if (fifo_len) {
		iwl_fw_dump_rxf(fwrt, &dump_data);
		iwl_fw_dump_txf(fwrt, &dump_data);
	}

	if (radio_len)
		iwl_read_radio_regs(fwrt, &dump_data);

	if (iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_ERROR_INFO) &&
	    fwrt->dump.desc) {
		dump_data->type = cpu_to_le32(IWL_FW_ERROR_DUMP_ERROR_INFO);
		dump_data->len = cpu_to_le32(sizeof(*dump_trig) +
					     fwrt->dump.desc->len);
		dump_trig = (void *)dump_data->data;
		memcpy(dump_trig, &fwrt->dump.desc->trig_desc,
		       sizeof(*dump_trig) + fwrt->dump.desc->len);

		dump_data = iwl_fw_error_next_data(dump_data);
	}

	/* In case we only want monitor dump, skip to dump trasport data */
	if (fwrt->dump.monitor_only)
		goto out;

	if (iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_MEM)) {
		const struct iwl_fw_dbg_mem_seg_tlv *fw_dbg_mem =
			fwrt->fw->dbg.mem_tlv;

		if (!fwrt->fw->dbg.n_mem_tlv)
			iwl_fw_dump_mem(fwrt, &dump_data, sram_len, sram_ofs,
					IWL_FW_ERROR_DUMP_MEM_SRAM);

		for (i = 0; i < fwrt->fw->dbg.n_mem_tlv; i++) {
			u32 len = le32_to_cpu(fw_dbg_mem[i].len);
			u32 ofs = le32_to_cpu(fw_dbg_mem[i].ofs);

			iwl_fw_dump_mem(fwrt, &dump_data, len, ofs,
					le32_to_cpu(fw_dbg_mem[i].data_type));
		}

		iwl_fw_dump_mem(fwrt, &dump_data, smem_len,
				fwrt->trans->cfg->smem_offset,
				IWL_FW_ERROR_DUMP_MEM_SMEM);

		iwl_fw_dump_mem(fwrt, &dump_data, sram2_len,
				fwrt->trans->cfg->dccm2_offset,
				IWL_FW_ERROR_DUMP_MEM_SRAM);
	}

	if (iwl_fw_dbg_is_d3_debug_enabled(fwrt) && fwrt->dump.d3_debug_data) {
		u32 addr = fwrt->trans->cfg->d3_debug_data_base_addr;
		size_t data_size = fwrt->trans->cfg->d3_debug_data_length;

		dump_data->type = cpu_to_le32(IWL_FW_ERROR_DUMP_D3_DEBUG_DATA);
		dump_data->len = cpu_to_le32(data_size * 2);

		memcpy(dump_data->data, fwrt->dump.d3_debug_data, data_size);

		kfree(fwrt->dump.d3_debug_data);
		fwrt->dump.d3_debug_data = NULL;

		iwl_trans_read_mem_bytes(fwrt->trans, addr,
					 dump_data->data + data_size,
					 data_size);

		dump_data = iwl_fw_error_next_data(dump_data);
	}

	/* Dump fw's virtual image */
	if (iwl_fw_dbg_is_paging_enabled(fwrt))
		iwl_dump_paging(fwrt, &dump_data);

	if (prph_len)
		iwl_fw_prph_handler(fwrt, &dump_data, iwl_dump_prph);

out:
	dump_file->file_len = cpu_to_le32(file_len);
	return dump_file;
}

static int iwl_dump_ini_prph_iter(struct iwl_fw_runtime *fwrt,
				  struct iwl_fw_ini_region_cfg *reg,
				  void *range_ptr, int idx)
{
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	__le32 *val = range->data;
	u32 prph_val;
	u32 addr = le32_to_cpu(reg->start_addr[idx]) + le32_to_cpu(reg->offset);
	int i;

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = reg->internal.range_data_size;
	for (i = 0; i < le32_to_cpu(reg->internal.range_data_size); i += 4) {
		prph_val = iwl_read_prph(fwrt->trans, addr + i);
		if (prph_val == 0x5a5a5a5a)
			return -EBUSY;
		*val++ = cpu_to_le32(prph_val);
	}

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int iwl_dump_ini_csr_iter(struct iwl_fw_runtime *fwrt,
				 struct iwl_fw_ini_region_cfg *reg,
				 void *range_ptr, int idx)
{
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	__le32 *val = range->data;
	u32 addr = le32_to_cpu(reg->start_addr[idx]) + le32_to_cpu(reg->offset);
	int i;

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = reg->internal.range_data_size;
	for (i = 0; i < le32_to_cpu(reg->internal.range_data_size); i += 4)
		*val++ = cpu_to_le32(iwl_trans_read32(fwrt->trans, addr + i));

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int iwl_dump_ini_dev_mem_iter(struct iwl_fw_runtime *fwrt,
				     struct iwl_fw_ini_region_cfg *reg,
				     void *range_ptr, int idx)
{
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	u32 addr = le32_to_cpu(reg->start_addr[idx]) + le32_to_cpu(reg->offset);

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = reg->internal.range_data_size;
	iwl_trans_read_mem_bytes(fwrt->trans, addr, range->data,
				 le32_to_cpu(reg->internal.range_data_size));

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int
iwl_dump_ini_paging_gen2_iter(struct iwl_fw_runtime *fwrt,
			      struct iwl_fw_ini_region_cfg *reg,
			      void *range_ptr, int idx)
{
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	u32 page_size = fwrt->trans->init_dram.paging[idx].size;

	range->page_num = cpu_to_le32(idx);
	range->range_data_size = cpu_to_le32(page_size);
	memcpy(range->data, fwrt->trans->init_dram.paging[idx].block,
	       page_size);

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int iwl_dump_ini_paging_iter(struct iwl_fw_runtime *fwrt,
				    struct iwl_fw_ini_region_cfg *reg,
				    void *range_ptr, int idx)
{
	/* increase idx by 1 since the pages are from 1 to
	 * fwrt->num_of_paging_blk + 1
	 */
	struct page *page = fwrt->fw_paging_db[++idx].fw_paging_block;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	dma_addr_t addr = fwrt->fw_paging_db[idx].fw_paging_phys;
	u32 page_size = fwrt->fw_paging_db[idx].fw_paging_size;

	range->page_num = cpu_to_le32(idx);
	range->range_data_size = cpu_to_le32(page_size);
	dma_sync_single_for_cpu(fwrt->trans->dev, addr,	page_size,
				DMA_BIDIRECTIONAL);
	memcpy(range->data, page_address(page), page_size);
	dma_sync_single_for_device(fwrt->trans->dev, addr, page_size,
				   DMA_BIDIRECTIONAL);

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int
iwl_dump_ini_mon_dram_iter(struct iwl_fw_runtime *fwrt,
			   struct iwl_fw_ini_region_cfg *reg, void *range_ptr,
			   int idx)
{
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	u32 start_addr = iwl_read_umac_prph(fwrt->trans,
					    MON_BUFF_BASE_ADDR_VER2);

	if (start_addr == 0x5a5a5a5a)
		return -EBUSY;

	range->dram_base_addr = cpu_to_le64(start_addr);
	range->range_data_size = cpu_to_le32(fwrt->trans->fw_mon[idx].size);

	memcpy(range->data, fwrt->trans->fw_mon[idx].block,
	       fwrt->trans->fw_mon[idx].size);

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

struct iwl_ini_txf_iter_data {
	int fifo;
	int lmac;
	u32 fifo_size;
	bool internal_txf;
	bool init;
};

static bool iwl_ini_txf_iter(struct iwl_fw_runtime *fwrt,
			     struct iwl_fw_ini_region_cfg *reg)
{
	struct iwl_ini_txf_iter_data *iter = fwrt->dump.fifo_iter;
	struct iwl_fwrt_shared_mem_cfg *cfg = &fwrt->smem_cfg;
	int txf_num = cfg->num_txfifo_entries;
	int int_txf_num = ARRAY_SIZE(cfg->internal_txfifo_size);
	u32 lmac_bitmap = le32_to_cpu(reg->fifos.fid1);

	if (!iter)
		return false;

	if (iter->init) {
		if (le32_to_cpu(reg->offset) &&
		    WARN_ONCE(cfg->num_lmacs == 1,
			      "Invalid lmac offset: 0x%x\n",
			      le32_to_cpu(reg->offset)))
			return false;

		iter->init = false;
		iter->internal_txf = false;
		iter->fifo_size = 0;
		iter->fifo = -1;
		if (le32_to_cpu(reg->offset))
			iter->lmac = 1;
		else
			iter->lmac = 0;
	}

	if (!iter->internal_txf)
		for (iter->fifo++; iter->fifo < txf_num; iter->fifo++) {
			iter->fifo_size =
				cfg->lmac[iter->lmac].txfifo_size[iter->fifo];
			if (iter->fifo_size && (lmac_bitmap & BIT(iter->fifo)))
				return true;
		}

	iter->internal_txf = true;

	if (!fw_has_capa(&fwrt->fw->ucode_capa,
			 IWL_UCODE_TLV_CAPA_EXTEND_SHARED_MEM_CFG))
		return false;

	for (iter->fifo++; iter->fifo < int_txf_num + txf_num; iter->fifo++) {
		iter->fifo_size =
			cfg->internal_txfifo_size[iter->fifo - txf_num];
		if (iter->fifo_size && (lmac_bitmap & BIT(iter->fifo)))
			return true;
	}

	return false;
}

static int iwl_dump_ini_txf_iter(struct iwl_fw_runtime *fwrt,
				 struct iwl_fw_ini_region_cfg *reg,
				 void *range_ptr, int idx)
{
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	struct iwl_ini_txf_iter_data *iter;
	struct iwl_fw_ini_error_dump_register *reg_dump = (void *)range->data;
	u32 offs = le32_to_cpu(reg->offset), addr;
	u32 registers_size =
		le32_to_cpu(reg->fifos.num_of_registers) * sizeof(*reg_dump);
	__le32 *data;
	unsigned long flags;
	int i;

	if (!iwl_ini_txf_iter(fwrt, reg))
		return -EIO;

	if (!iwl_trans_grab_nic_access(fwrt->trans, &flags))
		return -EBUSY;

	iter = fwrt->dump.fifo_iter;

	range->fifo_hdr.fifo_num = cpu_to_le32(iter->fifo);
	range->fifo_hdr.num_of_registers = reg->fifos.num_of_registers;
	range->range_data_size = cpu_to_le32(iter->fifo_size + registers_size);

	iwl_write_prph_no_grab(fwrt->trans, TXF_LARC_NUM + offs, iter->fifo);

	/*
	 * read txf registers. for each register, write to the dump the
	 * register address and its value
	 */
	for (i = 0; i < le32_to_cpu(reg->fifos.num_of_registers); i++) {
		addr = le32_to_cpu(reg->start_addr[i]) + offs;

		reg_dump->addr = cpu_to_le32(addr);
		reg_dump->data = cpu_to_le32(iwl_read_prph_no_grab(fwrt->trans,
								   addr));

		reg_dump++;
	}

	if (reg->fifos.header_only) {
		range->range_data_size = cpu_to_le32(registers_size);
		goto out;
	}

	/* Set the TXF_READ_MODIFY_ADDR to TXF_WR_PTR */
	iwl_write_prph_no_grab(fwrt->trans, TXF_READ_MODIFY_ADDR + offs,
			       TXF_WR_PTR + offs);

	/* Dummy-read to advance the read pointer to the head */
	iwl_read_prph_no_grab(fwrt->trans, TXF_READ_MODIFY_DATA + offs);

	/* Read FIFO */
	addr = TXF_READ_MODIFY_DATA + offs;
	data = (void *)reg_dump;
	for (i = 0; i < iter->fifo_size; i += sizeof(*data))
		*data++ = cpu_to_le32(iwl_read_prph_no_grab(fwrt->trans, addr));

out:
	iwl_trans_release_nic_access(fwrt->trans, &flags);

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

struct iwl_ini_rxf_data {
	u32 fifo_num;
	u32 size;
	u32 offset;
};

static void iwl_ini_get_rxf_data(struct iwl_fw_runtime *fwrt,
				 struct iwl_fw_ini_region_cfg *reg,
				 struct iwl_ini_rxf_data *data)
{
	u32 fid1 = le32_to_cpu(reg->fifos.fid1);
	u32 fid2 = le32_to_cpu(reg->fifos.fid2);
	u32 fifo_idx;

	if (!data)
		return;

	memset(data, 0, sizeof(*data));

	if (WARN_ON_ONCE((fid1 && fid2) || (!fid1 && !fid2)))
		return;

	fifo_idx = ffs(fid1) - 1;
	if (fid1 && !WARN_ON_ONCE((~BIT(fifo_idx) & fid1) ||
				  fifo_idx >= MAX_NUM_LMAC)) {
		data->size = fwrt->smem_cfg.lmac[fifo_idx].rxfifo1_size;
		data->fifo_num = fifo_idx;
		return;
	}

	fifo_idx = ffs(fid2) - 1;
	if (fid2 && !WARN_ON_ONCE(fifo_idx != 0)) {
		data->size = fwrt->smem_cfg.rxfifo2_size;
		data->offset = RXF_DIFF_FROM_PREV;
		/* use bit 31 to distinguish between umac and lmac rxf while
		 * parsing the dump
		 */
		data->fifo_num = fifo_idx | IWL_RXF_UMAC_BIT;
		return;
	}
}

static int iwl_dump_ini_rxf_iter(struct iwl_fw_runtime *fwrt,
				 struct iwl_fw_ini_region_cfg *reg,
				 void *range_ptr, int idx)
{
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	struct iwl_ini_rxf_data rxf_data;
	struct iwl_fw_ini_error_dump_register *reg_dump = (void *)range->data;
	u32 offs = le32_to_cpu(reg->offset), addr;
	u32 registers_size =
		le32_to_cpu(reg->fifos.num_of_registers) * sizeof(*reg_dump);
	__le32 *data;
	unsigned long flags;
	int i;

	iwl_ini_get_rxf_data(fwrt, reg, &rxf_data);
	if (!rxf_data.size)
		return -EIO;

	if (!iwl_trans_grab_nic_access(fwrt->trans, &flags))
		return -EBUSY;

	range->fifo_hdr.fifo_num = cpu_to_le32(rxf_data.fifo_num);
	range->fifo_hdr.num_of_registers = reg->fifos.num_of_registers;
	range->range_data_size = cpu_to_le32(rxf_data.size + registers_size);

	/*
	 * read rxf registers. for each register, write to the dump the
	 * register address and its value
	 */
	for (i = 0; i < le32_to_cpu(reg->fifos.num_of_registers); i++) {
		addr = le32_to_cpu(reg->start_addr[i]) + offs;

		reg_dump->addr = cpu_to_le32(addr);
		reg_dump->data = cpu_to_le32(iwl_read_prph_no_grab(fwrt->trans,
								   addr));

		reg_dump++;
	}

	if (reg->fifos.header_only) {
		range->range_data_size = cpu_to_le32(registers_size);
		goto out;
	}

	/*
	 * region register have absolute value so apply rxf offset after
	 * reading the registers
	 */
	offs += rxf_data.offset;

	/* Lock fence */
	iwl_write_prph_no_grab(fwrt->trans, RXF_SET_FENCE_MODE + offs, 0x1);
	/* Set fence pointer to the same place like WR pointer */
	iwl_write_prph_no_grab(fwrt->trans, RXF_LD_WR2FENCE + offs, 0x1);
	/* Set fence offset */
	iwl_write_prph_no_grab(fwrt->trans, RXF_LD_FENCE_OFFSET_ADDR + offs,
			       0x0);

	/* Read FIFO */
	addr =  RXF_FIFO_RD_FENCE_INC + offs;
	data = (void *)reg_dump;
	for (i = 0; i < rxf_data.size; i += sizeof(*data))
		*data++ = cpu_to_le32(iwl_read_prph_no_grab(fwrt->trans, addr));

out:
	iwl_trans_release_nic_access(fwrt->trans, &flags);

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static void *iwl_dump_ini_mem_fill_header(struct iwl_fw_runtime *fwrt,
					  struct iwl_fw_ini_region_cfg *reg,
					  void *data)
{
	struct iwl_fw_ini_error_dump *dump = data;

	dump->header.version = cpu_to_le32(IWL_INI_DUMP_VER);

	return dump->ranges;
}

static void
*iwl_dump_ini_mon_fill_header(struct iwl_fw_runtime *fwrt,
			      struct iwl_fw_ini_region_cfg *reg,
			      struct iwl_fw_ini_monitor_dump *data,
			      u32 write_ptr_addr, u32 write_ptr_msk,
			      u32 cycle_cnt_addr, u32 cycle_cnt_msk)
{
	u32 write_ptr, cycle_cnt;
	unsigned long flags;

	if (!iwl_trans_grab_nic_access(fwrt->trans, &flags)) {
		IWL_ERR(fwrt, "Failed to get monitor header\n");
		return NULL;
	}

	write_ptr = iwl_read_prph_no_grab(fwrt->trans, write_ptr_addr);
	cycle_cnt = iwl_read_prph_no_grab(fwrt->trans, cycle_cnt_addr);

	iwl_trans_release_nic_access(fwrt->trans, &flags);

	data->header.version = cpu_to_le32(IWL_INI_DUMP_VER);
	data->write_ptr = cpu_to_le32(write_ptr & write_ptr_msk);
	data->cycle_cnt = cpu_to_le32(cycle_cnt & cycle_cnt_msk);

	return data->ranges;
}

static void
*iwl_dump_ini_mon_dram_fill_header(struct iwl_fw_runtime *fwrt,
				   struct iwl_fw_ini_region_cfg *reg,
				   void *data)
{
	struct iwl_fw_ini_monitor_dump *mon_dump = (void *)data;
	u32 write_ptr_addr, write_ptr_msk, cycle_cnt_addr, cycle_cnt_msk;

	switch (fwrt->trans->cfg->device_family) {
	case IWL_DEVICE_FAMILY_9000:
	case IWL_DEVICE_FAMILY_22000:
		write_ptr_addr = MON_BUFF_WRPTR_VER2;
		write_ptr_msk = -1;
		cycle_cnt_addr = MON_BUFF_CYCLE_CNT_VER2;
		cycle_cnt_msk = -1;
		break;
	default:
		IWL_ERR(fwrt, "Unsupported device family %d\n",
			fwrt->trans->cfg->device_family);
		return NULL;
	}

	return iwl_dump_ini_mon_fill_header(fwrt, reg, mon_dump, write_ptr_addr,
					    write_ptr_msk, cycle_cnt_addr,
					    cycle_cnt_msk);
}

static void
*iwl_dump_ini_mon_smem_fill_header(struct iwl_fw_runtime *fwrt,
				   struct iwl_fw_ini_region_cfg *reg,
				   void *data)
{
	struct iwl_fw_ini_monitor_dump *mon_dump = (void *)data;
	const struct iwl_cfg *cfg = fwrt->trans->cfg;

	if (fwrt->trans->cfg->device_family != IWL_DEVICE_FAMILY_9000 &&
	    fwrt->trans->cfg->device_family != IWL_DEVICE_FAMILY_22000) {
		IWL_ERR(fwrt, "Unsupported device family %d\n",
			fwrt->trans->cfg->device_family);
		return NULL;
	}

	return iwl_dump_ini_mon_fill_header(fwrt, reg, mon_dump,
					    cfg->fw_mon_smem_write_ptr_addr,
					    cfg->fw_mon_smem_write_ptr_msk,
					    cfg->fw_mon_smem_cycle_cnt_ptr_addr,
					    cfg->fw_mon_smem_cycle_cnt_ptr_msk);

}

static u32 iwl_dump_ini_mem_ranges(struct iwl_fw_runtime *fwrt,
				   struct iwl_fw_ini_region_cfg *reg)
{
	return le32_to_cpu(reg->internal.num_of_ranges);
}

static u32 iwl_dump_ini_paging_gen2_ranges(struct iwl_fw_runtime *fwrt,
					   struct iwl_fw_ini_region_cfg *reg)
{
	return fwrt->trans->init_dram.paging_cnt;
}

static u32 iwl_dump_ini_paging_ranges(struct iwl_fw_runtime *fwrt,
				      struct iwl_fw_ini_region_cfg *reg)
{
	return fwrt->num_of_paging_blk;
}

static u32 iwl_dump_ini_mon_dram_ranges(struct iwl_fw_runtime *fwrt,
					struct iwl_fw_ini_region_cfg *reg)
{
	return 1;
}

static u32 iwl_dump_ini_txf_ranges(struct iwl_fw_runtime *fwrt,
				   struct iwl_fw_ini_region_cfg *reg)
{
	struct iwl_ini_txf_iter_data iter = { .init = true };
	void *fifo_iter = fwrt->dump.fifo_iter;
	u32 num_of_fifos = 0;

	fwrt->dump.fifo_iter = &iter;
	while (iwl_ini_txf_iter(fwrt, reg))
		num_of_fifos++;

	fwrt->dump.fifo_iter = fifo_iter;

	return num_of_fifos;
}

static u32 iwl_dump_ini_rxf_ranges(struct iwl_fw_runtime *fwrt,
				   struct iwl_fw_ini_region_cfg *reg)
{
	/* Each Rx fifo needs a different offset and therefore, it's
	 * region can contain only one fifo, i.e. 1 memory range.
	 */
	return 1;
}

static u32 iwl_dump_ini_mem_get_size(struct iwl_fw_runtime *fwrt,
				     struct iwl_fw_ini_region_cfg *reg)
{
	return sizeof(struct iwl_fw_ini_error_dump) +
		iwl_dump_ini_mem_ranges(fwrt, reg) *
		(sizeof(struct iwl_fw_ini_error_dump_range) +
		 le32_to_cpu(reg->internal.range_data_size));
}

static u32 iwl_dump_ini_paging_gen2_get_size(struct iwl_fw_runtime *fwrt,
					     struct iwl_fw_ini_region_cfg *reg)
{
	int i;
	u32 range_header_len = sizeof(struct iwl_fw_ini_error_dump_range);
	u32 size = sizeof(struct iwl_fw_ini_error_dump);

	for (i = 0; i < iwl_dump_ini_paging_gen2_ranges(fwrt, reg); i++)
		size += range_header_len +
			fwrt->trans->init_dram.paging[i].size;

	return size;
}

static u32 iwl_dump_ini_paging_get_size(struct iwl_fw_runtime *fwrt,
					struct iwl_fw_ini_region_cfg *reg)
{
	int i;
	u32 range_header_len = sizeof(struct iwl_fw_ini_error_dump_range);
	u32 size = sizeof(struct iwl_fw_ini_error_dump);

	for (i = 1; i <= iwl_dump_ini_paging_ranges(fwrt, reg); i++)
		size += range_header_len + fwrt->fw_paging_db[i].fw_paging_size;

	return size;
}

static u32 iwl_dump_ini_mon_dram_get_size(struct iwl_fw_runtime *fwrt,
					  struct iwl_fw_ini_region_cfg *reg)
{
	u32 size = sizeof(struct iwl_fw_ini_monitor_dump) +
		sizeof(struct iwl_fw_ini_error_dump_range);

	if (fwrt->trans->num_blocks)
		size += fwrt->trans->fw_mon[0].size;

	return size;
}

static u32 iwl_dump_ini_mon_smem_get_size(struct iwl_fw_runtime *fwrt,
					  struct iwl_fw_ini_region_cfg *reg)
{
	return sizeof(struct iwl_fw_ini_monitor_dump) +
		iwl_dump_ini_mem_ranges(fwrt, reg) *
		(sizeof(struct iwl_fw_ini_error_dump_range) +
		 le32_to_cpu(reg->internal.range_data_size));
}

static u32 iwl_dump_ini_txf_get_size(struct iwl_fw_runtime *fwrt,
				     struct iwl_fw_ini_region_cfg *reg)
{
	struct iwl_ini_txf_iter_data iter = { .init = true };
	void *fifo_iter = fwrt->dump.fifo_iter;
	u32 size = 0;
	u32 fifo_hdr = sizeof(struct iwl_fw_ini_error_dump_range) +
		le32_to_cpu(reg->fifos.num_of_registers) *
		sizeof(struct iwl_fw_ini_error_dump_register);

	fwrt->dump.fifo_iter = &iter;
	while (iwl_ini_txf_iter(fwrt, reg)) {
		size += fifo_hdr;
		if (!reg->fifos.header_only)
			size += iter.fifo_size;
	}

	if (size)
		size += sizeof(struct iwl_fw_ini_error_dump);

	fwrt->dump.fifo_iter = fifo_iter;

	return size;
}

static u32 iwl_dump_ini_rxf_get_size(struct iwl_fw_runtime *fwrt,
				     struct iwl_fw_ini_region_cfg *reg)
{
	struct iwl_ini_rxf_data rx_data;
	u32 size = sizeof(struct iwl_fw_ini_error_dump) +
		sizeof(struct iwl_fw_ini_error_dump_range) +
		le32_to_cpu(reg->fifos.num_of_registers) *
		sizeof(struct iwl_fw_ini_error_dump_register);

	if (reg->fifos.header_only)
		return size;

	iwl_ini_get_rxf_data(fwrt, reg, &rx_data);
	size += rx_data.size;

	return size;
}

/**
 * struct iwl_dump_ini_mem_ops - ini memory dump operations
 * @get_num_of_ranges: returns the number of memory ranges in the region.
 * @get_size: returns the total size of the region.
 * @fill_mem_hdr: fills region type specific headers and returns pointer to
 *	the first range or NULL if failed to fill headers.
 * @fill_range: copies a given memory range into the dump.
 *	Returns the size of the range or negative error value otherwise.
 */
struct iwl_dump_ini_mem_ops {
	u32 (*get_num_of_ranges)(struct iwl_fw_runtime *fwrt,
				 struct iwl_fw_ini_region_cfg *reg);
	u32 (*get_size)(struct iwl_fw_runtime *fwrt,
			struct iwl_fw_ini_region_cfg *reg);
	void *(*fill_mem_hdr)(struct iwl_fw_runtime *fwrt,
			      struct iwl_fw_ini_region_cfg *reg, void *data);
	int (*fill_range)(struct iwl_fw_runtime *fwrt,
			  struct iwl_fw_ini_region_cfg *reg, void *range,
			  int idx);
};

/**
 * iwl_dump_ini_mem - copy a memory region into the dump
 * @fwrt: fw runtime struct.
 * @data: dump memory data.
 * @reg: region to copy to the dump.
 * @ops: memory dump operations.
 */
static void
iwl_dump_ini_mem(struct iwl_fw_runtime *fwrt,
		 struct iwl_fw_error_dump_data **data,
		 struct iwl_fw_ini_region_cfg *reg,
		 struct iwl_dump_ini_mem_ops *ops)
{
	struct iwl_fw_ini_error_dump_header *header = (void *)(*data)->data;
	u32 num_of_ranges, i, type = le32_to_cpu(reg->region_type);
	void *range;

	if (WARN_ON(!ops || !ops->get_num_of_ranges || !ops->get_size ||
		    !ops->fill_mem_hdr || !ops->fill_range))
		return;

	IWL_DEBUG_FW(fwrt, "WRT: collecting region: id=%d, type=%d\n",
		     le32_to_cpu(reg->region_id), type);

	num_of_ranges = ops->get_num_of_ranges(fwrt, reg);

	(*data)->type = cpu_to_le32(type | INI_DUMP_BIT);
	(*data)->len = cpu_to_le32(ops->get_size(fwrt, reg));

	header->region_id = reg->region_id;
	header->num_of_ranges = cpu_to_le32(num_of_ranges);
	header->name_len = cpu_to_le32(min_t(int, IWL_FW_INI_MAX_NAME,
					     le32_to_cpu(reg->name_len)));
	memcpy(header->name, reg->name, le32_to_cpu(header->name_len));

	range = ops->fill_mem_hdr(fwrt, reg, header);
	if (!range) {
		IWL_ERR(fwrt,
			"WRT: failed to fill region header: id=%d, type=%d\n",
			le32_to_cpu(reg->region_id), type);
		memset(*data, 0, le32_to_cpu((*data)->len));
		return;
	}

	for (i = 0; i < num_of_ranges; i++) {
		int range_size = ops->fill_range(fwrt, reg, range, i);

		if (range_size < 0) {
			IWL_ERR(fwrt,
				"WRT: failed to dump region: id=%d, type=%d\n",
				le32_to_cpu(reg->region_id), type);
			memset(*data, 0, le32_to_cpu((*data)->len));
			return;
		}
		range = range + range_size;
	}
	*data = iwl_fw_error_next_data(*data);
}

static int iwl_fw_ini_get_trigger_len(struct iwl_fw_runtime *fwrt,
				      struct iwl_fw_ini_trigger *trigger)
{
	int i, size = 0, hdr_len = sizeof(struct iwl_fw_error_dump_data);

	if (!trigger || !trigger->num_regions)
		return 0;

	for (i = 0; i < le32_to_cpu(trigger->num_regions); i++) {
		u32 reg_id = le32_to_cpu(trigger->data[i]);
		struct iwl_fw_ini_region_cfg *reg;

		if (WARN_ON(reg_id >= ARRAY_SIZE(fwrt->dump.active_regs)))
			continue;

		reg = fwrt->dump.active_regs[reg_id];
		if (!reg) {
			IWL_WARN(fwrt,
				 "WRT: unassigned region id %d, skipping\n",
				 reg_id);
			continue;
		}

		/* currently the driver supports always on domain only */
		if (le32_to_cpu(reg->domain) != IWL_FW_INI_DBG_DOMAIN_ALWAYS_ON)
			continue;

		switch (le32_to_cpu(reg->region_type)) {
		case IWL_FW_INI_REGION_DEVICE_MEMORY:
		case IWL_FW_INI_REGION_PERIPHERY_MAC:
		case IWL_FW_INI_REGION_PERIPHERY_PHY:
		case IWL_FW_INI_REGION_PERIPHERY_AUX:
		case IWL_FW_INI_REGION_CSR:
		case IWL_FW_INI_REGION_LMAC_ERROR_TABLE:
		case IWL_FW_INI_REGION_UMAC_ERROR_TABLE:
			size += hdr_len + iwl_dump_ini_mem_get_size(fwrt, reg);
			break;
		case IWL_FW_INI_REGION_TXF:
			size += hdr_len + iwl_dump_ini_txf_get_size(fwrt, reg);
			break;
		case IWL_FW_INI_REGION_RXF:
			size += hdr_len + iwl_dump_ini_rxf_get_size(fwrt, reg);
			break;
		case IWL_FW_INI_REGION_PAGING:
			size += hdr_len;
			if (iwl_fw_dbg_is_paging_enabled(fwrt)) {
				size += iwl_dump_ini_paging_get_size(fwrt, reg);
			} else {
				size += iwl_dump_ini_paging_gen2_get_size(fwrt,
									  reg);
			}
			break;
		case IWL_FW_INI_REGION_DRAM_BUFFER:
			if (!fwrt->trans->num_blocks)
				break;
			size += hdr_len +
				iwl_dump_ini_mon_dram_get_size(fwrt, reg);
			break;
		case IWL_FW_INI_REGION_INTERNAL_BUFFER:
			size += hdr_len +
				iwl_dump_ini_mon_smem_get_size(fwrt, reg);
			break;
		case IWL_FW_INI_REGION_DRAM_IMR:
			/* Undefined yet */
		default:
			break;
		}
	}
	return size;
}

static void iwl_fw_ini_dump_trigger(struct iwl_fw_runtime *fwrt,
				    struct iwl_fw_ini_trigger *trigger,
				    struct iwl_fw_error_dump_data **data)
{
	int i, num = le32_to_cpu(trigger->num_regions);

	for (i = 0; i < num; i++) {
		u32 reg_id = le32_to_cpu(trigger->data[i]);
		struct iwl_fw_ini_region_cfg *reg;
		struct iwl_dump_ini_mem_ops ops;

		if (reg_id >= ARRAY_SIZE(fwrt->dump.active_regs))
			continue;

		reg = fwrt->dump.active_regs[reg_id];
		/* Don't warn, get_trigger_len already warned */
		if (!reg)
			continue;

		/* currently the driver supports always on domain only */
		if (le32_to_cpu(reg->domain) != IWL_FW_INI_DBG_DOMAIN_ALWAYS_ON)
			continue;

		switch (le32_to_cpu(reg->region_type)) {
		case IWL_FW_INI_REGION_DEVICE_MEMORY:
		case IWL_FW_INI_REGION_LMAC_ERROR_TABLE:
		case IWL_FW_INI_REGION_UMAC_ERROR_TABLE:
			ops.get_num_of_ranges = iwl_dump_ini_mem_ranges;
			ops.get_size = iwl_dump_ini_mem_get_size;
			ops.fill_mem_hdr = iwl_dump_ini_mem_fill_header;
			ops.fill_range = iwl_dump_ini_dev_mem_iter;
			iwl_dump_ini_mem(fwrt, data, reg, &ops);
			break;
		case IWL_FW_INI_REGION_PERIPHERY_MAC:
		case IWL_FW_INI_REGION_PERIPHERY_PHY:
		case IWL_FW_INI_REGION_PERIPHERY_AUX:
			ops.get_num_of_ranges =	iwl_dump_ini_mem_ranges;
			ops.get_size = iwl_dump_ini_mem_get_size;
			ops.fill_mem_hdr = iwl_dump_ini_mem_fill_header;
			ops.fill_range = iwl_dump_ini_prph_iter;
			iwl_dump_ini_mem(fwrt, data, reg, &ops);
			break;
		case IWL_FW_INI_REGION_DRAM_BUFFER:
			ops.get_num_of_ranges = iwl_dump_ini_mon_dram_ranges;
			ops.get_size = iwl_dump_ini_mon_dram_get_size;
			ops.fill_mem_hdr = iwl_dump_ini_mon_dram_fill_header;
			ops.fill_range = iwl_dump_ini_mon_dram_iter;
			iwl_dump_ini_mem(fwrt, data, reg, &ops);
			break;
		case IWL_FW_INI_REGION_INTERNAL_BUFFER:
			ops.get_num_of_ranges = iwl_dump_ini_mem_ranges;
			ops.get_size = iwl_dump_ini_mon_smem_get_size;
			ops.fill_mem_hdr = iwl_dump_ini_mon_smem_fill_header;
			ops.fill_range = iwl_dump_ini_dev_mem_iter;
			iwl_dump_ini_mem(fwrt, data, reg, &ops);
			break;
		case IWL_FW_INI_REGION_PAGING:
			ops.fill_mem_hdr = iwl_dump_ini_mem_fill_header;
			if (iwl_fw_dbg_is_paging_enabled(fwrt)) {
				ops.get_num_of_ranges =
					iwl_dump_ini_paging_ranges;
				ops.get_size = iwl_dump_ini_paging_get_size;
				ops.fill_range = iwl_dump_ini_paging_iter;
			} else {
				ops.get_num_of_ranges =
					iwl_dump_ini_paging_gen2_ranges;
				ops.get_size =
					iwl_dump_ini_paging_gen2_get_size;
				ops.fill_range = iwl_dump_ini_paging_gen2_iter;
			}

			iwl_dump_ini_mem(fwrt, data, reg, &ops);
			break;
		case IWL_FW_INI_REGION_TXF: {
			struct iwl_ini_txf_iter_data iter = { .init = true };
			void *fifo_iter = fwrt->dump.fifo_iter;

			fwrt->dump.fifo_iter = &iter;
			ops.get_num_of_ranges = iwl_dump_ini_txf_ranges;
			ops.get_size = iwl_dump_ini_txf_get_size;
			ops.fill_mem_hdr = iwl_dump_ini_mem_fill_header;
			ops.fill_range = iwl_dump_ini_txf_iter;
			iwl_dump_ini_mem(fwrt, data, reg, &ops);
			fwrt->dump.fifo_iter = fifo_iter;
			break;
		}
		case IWL_FW_INI_REGION_RXF:
			ops.get_num_of_ranges = iwl_dump_ini_rxf_ranges;
			ops.get_size = iwl_dump_ini_rxf_get_size;
			ops.fill_mem_hdr = iwl_dump_ini_mem_fill_header;
			ops.fill_range = iwl_dump_ini_rxf_iter;
			iwl_dump_ini_mem(fwrt, data, reg, &ops);
			break;
		case IWL_FW_INI_REGION_CSR:
			ops.get_num_of_ranges =	iwl_dump_ini_mem_ranges;
			ops.get_size = iwl_dump_ini_mem_get_size;
			ops.fill_mem_hdr = iwl_dump_ini_mem_fill_header;
			ops.fill_range = iwl_dump_ini_csr_iter;
			iwl_dump_ini_mem(fwrt, data, reg, &ops);
			break;
		case IWL_FW_INI_REGION_DRAM_IMR:
			/* This is undefined yet */
		default:
			break;
		}
	}
}

static struct iwl_fw_error_dump_file *
iwl_fw_error_ini_dump_file(struct iwl_fw_runtime *fwrt)
{
	int size;
	struct iwl_fw_error_dump_data *dump_data;
	struct iwl_fw_error_dump_file *dump_file;
	struct iwl_fw_ini_trigger *trigger;
	enum iwl_fw_ini_trigger_id id = fwrt->dump.ini_trig_id;

	if (!iwl_fw_ini_trigger_on(fwrt, id))
		return NULL;

	trigger = fwrt->dump.active_trigs[id].trig;

	size = iwl_fw_ini_get_trigger_len(fwrt, trigger);
	if (!size)
		return NULL;

	size += sizeof(*dump_file);

	dump_file = vzalloc(size);
	if (!dump_file)
		return NULL;

	dump_file->barker = cpu_to_le32(IWL_FW_ERROR_DUMP_BARKER);
	dump_data = (void *)dump_file->data;
	dump_file->file_len = cpu_to_le32(size);

	iwl_fw_ini_dump_trigger(fwrt, trigger, &dump_data);

	return dump_file;
}

static void iwl_fw_error_dump(struct iwl_fw_runtime *fwrt)
{
	struct iwl_fw_dump_ptrs fw_error_dump = {};
	struct iwl_fw_error_dump_file *dump_file;
	struct scatterlist *sg_dump_data;
	u32 file_len;
	u32 dump_mask = fwrt->fw->dbg.dump_mask;

	dump_file = iwl_fw_error_dump_file(fwrt, &fw_error_dump);
	if (!dump_file)
		goto out;

	if (!fwrt->trans->ini_valid && fwrt->dump.monitor_only)
		dump_mask &= IWL_FW_ERROR_DUMP_FW_MONITOR;

	fw_error_dump.trans_ptr = iwl_trans_dump_data(fwrt->trans, dump_mask);
	file_len = le32_to_cpu(dump_file->file_len);
	fw_error_dump.fwrt_len = file_len;

	if (fw_error_dump.trans_ptr) {
		file_len += fw_error_dump.trans_ptr->len;
		dump_file->file_len = cpu_to_le32(file_len);
	}

	sg_dump_data = alloc_sgtable(file_len);
	if (sg_dump_data) {
		sg_pcopy_from_buffer(sg_dump_data,
				     sg_nents(sg_dump_data),
				     fw_error_dump.fwrt_ptr,
				     fw_error_dump.fwrt_len, 0);
		if (fw_error_dump.trans_ptr)
			sg_pcopy_from_buffer(sg_dump_data,
					     sg_nents(sg_dump_data),
					     fw_error_dump.trans_ptr->data,
					     fw_error_dump.trans_ptr->len,
					     fw_error_dump.fwrt_len);
		dev_coredumpsg(fwrt->trans->dev, sg_dump_data, file_len,
			       GFP_KERNEL);
	}
	vfree(fw_error_dump.fwrt_ptr);
	vfree(fw_error_dump.trans_ptr);

out:
	iwl_fw_free_dump_desc(fwrt);
	clear_bit(IWL_FWRT_STATUS_DUMPING, &fwrt->status);
}

static void iwl_fw_error_ini_dump(struct iwl_fw_runtime *fwrt)
{
	struct iwl_fw_error_dump_file *dump_file;
	struct scatterlist *sg_dump_data;
	u32 file_len;

	dump_file = iwl_fw_error_ini_dump_file(fwrt);
	if (!dump_file)
		goto out;

	file_len = le32_to_cpu(dump_file->file_len);

	sg_dump_data = alloc_sgtable(file_len);
	if (sg_dump_data) {
		sg_pcopy_from_buffer(sg_dump_data, sg_nents(sg_dump_data),
				     dump_file, file_len, 0);
		dev_coredumpsg(fwrt->trans->dev, sg_dump_data, file_len,
			       GFP_KERNEL);
	}
	vfree(dump_file);
out:
	fwrt->dump.ini_trig_id = IWL_FW_TRIGGER_ID_INVALID;
	clear_bit(IWL_FWRT_STATUS_DUMPING, &fwrt->status);
}

const struct iwl_fw_dump_desc iwl_dump_desc_assert = {
	.trig_desc = {
		.type = cpu_to_le32(FW_DBG_TRIGGER_FW_ASSERT),
	},
};
IWL_EXPORT_SYMBOL(iwl_dump_desc_assert);

int iwl_fw_dbg_collect_desc(struct iwl_fw_runtime *fwrt,
			    const struct iwl_fw_dump_desc *desc,
			    bool monitor_only,
			    unsigned int delay)
{
	u32 trig_type = le32_to_cpu(desc->trig_desc.type);
	int ret;

	if (fwrt->trans->ini_valid) {
		ret = iwl_fw_dbg_ini_collect(fwrt, trig_type);
		if (!ret)
			iwl_fw_free_dump_desc(fwrt);

		return ret;
	}

	if (test_and_set_bit(IWL_FWRT_STATUS_DUMPING, &fwrt->status))
		return -EBUSY;

	if (WARN_ON(fwrt->dump.desc))
		iwl_fw_free_dump_desc(fwrt);

	IWL_WARN(fwrt, "Collecting data: trigger %d fired.\n",
		 le32_to_cpu(desc->trig_desc.type));

	fwrt->dump.desc = desc;
	fwrt->dump.monitor_only = monitor_only;

	schedule_delayed_work(&fwrt->dump.wk, usecs_to_jiffies(delay));

	return 0;
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_collect_desc);

int iwl_fw_dbg_error_collect(struct iwl_fw_runtime *fwrt,
			     enum iwl_fw_dbg_trigger trig_type)
{
	int ret;
	struct iwl_fw_dump_desc *iwl_dump_error_desc;

	if (!test_bit(STATUS_DEVICE_ENABLED, &fwrt->trans->status))
		return -EIO;

	iwl_dump_error_desc = kmalloc(sizeof(*iwl_dump_error_desc), GFP_KERNEL);
	if (!iwl_dump_error_desc)
		return -ENOMEM;

	iwl_dump_error_desc->trig_desc.type = cpu_to_le32(trig_type);
	iwl_dump_error_desc->len = 0;

	ret = iwl_fw_dbg_collect_desc(fwrt, iwl_dump_error_desc, false, 0);
	if (ret)
		kfree(iwl_dump_error_desc);
	else
		iwl_trans_sync_nmi(fwrt->trans);

	return ret;
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_error_collect);

int iwl_fw_dbg_collect(struct iwl_fw_runtime *fwrt,
		       enum iwl_fw_dbg_trigger trig,
		       const char *str, size_t len,
		       struct iwl_fw_dbg_trigger_tlv *trigger)
{
	struct iwl_fw_dump_desc *desc;
	unsigned int delay = 0;
	bool monitor_only = false;

	if (trigger) {
		u16 occurrences = le16_to_cpu(trigger->occurrences) - 1;

		if (!le16_to_cpu(trigger->occurrences))
			return 0;

		if (trigger->flags & IWL_FW_DBG_FORCE_RESTART) {
			IWL_WARN(fwrt, "Force restart: trigger %d fired.\n",
				 trig);
			iwl_force_nmi(fwrt->trans);
			return 0;
		}

		trigger->occurrences = cpu_to_le16(occurrences);
		monitor_only = trigger->mode & IWL_FW_DBG_TRIGGER_MONITOR_ONLY;

		/* convert msec to usec */
		delay = le32_to_cpu(trigger->stop_delay) * USEC_PER_MSEC;
	}

	desc = kzalloc(sizeof(*desc) + len, GFP_ATOMIC);
	if (!desc)
		return -ENOMEM;


	desc->len = len;
	desc->trig_desc.type = cpu_to_le32(trig);
	memcpy(desc->trig_desc.data, str, len);

	return iwl_fw_dbg_collect_desc(fwrt, desc, monitor_only, delay);
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_collect);

int _iwl_fw_dbg_ini_collect(struct iwl_fw_runtime *fwrt,
			    enum iwl_fw_ini_trigger_id id)
{
	struct iwl_fw_ini_active_triggers *active;
	u32 occur, delay;

	if (WARN_ON(!iwl_fw_ini_trigger_on(fwrt, id)))
		return -EINVAL;

	if (test_and_set_bit(IWL_FWRT_STATUS_DUMPING, &fwrt->status))
		return -EBUSY;

	if (!iwl_fw_ini_trigger_on(fwrt, id)) {
		IWL_WARN(fwrt, "WRT: Trigger %d is not active, aborting dump\n",
			 id);
		return -EINVAL;
	}

	active = &fwrt->dump.active_trigs[id];
	delay = le32_to_cpu(active->trig->dump_delay);
	occur = le32_to_cpu(active->trig->occurrences);
	if (!occur)
		return 0;

	active->trig->occurrences = cpu_to_le32(--occur);

	if (le32_to_cpu(active->trig->force_restart)) {
		IWL_WARN(fwrt, "WRT: force restart: trigger %d fired.\n", id);
		iwl_force_nmi(fwrt->trans);
		return 0;
	}

	if (test_and_set_bit(IWL_FWRT_STATUS_DUMPING, &fwrt->status))
		return -EBUSY;

	fwrt->dump.ini_trig_id = id;

	IWL_WARN(fwrt, "WRT: collecting data: ini trigger %d fired.\n", id);

	schedule_delayed_work(&fwrt->dump.wk, usecs_to_jiffies(delay));

	return 0;
}
IWL_EXPORT_SYMBOL(_iwl_fw_dbg_ini_collect);

int iwl_fw_dbg_ini_collect(struct iwl_fw_runtime *fwrt, u32 legacy_trigger_id)
{
	int id;

	switch (legacy_trigger_id) {
	case FW_DBG_TRIGGER_FW_ASSERT:
	case FW_DBG_TRIGGER_ALIVE_TIMEOUT:
	case FW_DBG_TRIGGER_DRIVER:
		id = IWL_FW_TRIGGER_ID_FW_ASSERT;
		break;
	case FW_DBG_TRIGGER_USER:
		id = IWL_FW_TRIGGER_ID_USER_TRIGGER;
		break;
	default:
		return -EIO;
	}

	return _iwl_fw_dbg_ini_collect(fwrt, id);
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_ini_collect);

int iwl_fw_dbg_collect_trig(struct iwl_fw_runtime *fwrt,
			    struct iwl_fw_dbg_trigger_tlv *trigger,
			    const char *fmt, ...)
{
	int ret, len = 0;
	char buf[64];

	if (fwrt->trans->ini_valid)
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

	ret = iwl_fw_dbg_collect(fwrt, le32_to_cpu(trigger->id), buf, len,
				 trigger);

	if (ret)
		return ret;

	return 0;
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_collect_trig);

int iwl_fw_start_dbg_conf(struct iwl_fw_runtime *fwrt, u8 conf_id)
{
	u8 *ptr;
	int ret;
	int i;

	if (WARN_ONCE(conf_id >= ARRAY_SIZE(fwrt->fw->dbg.conf_tlv),
		      "Invalid configuration %d\n", conf_id))
		return -EINVAL;

	/* EARLY START - firmware's configuration is hard coded */
	if ((!fwrt->fw->dbg.conf_tlv[conf_id] ||
	     !fwrt->fw->dbg.conf_tlv[conf_id]->num_of_hcmds) &&
	    conf_id == FW_DBG_START_FROM_ALIVE)
		return 0;

	if (!fwrt->fw->dbg.conf_tlv[conf_id])
		return -EINVAL;

	if (fwrt->dump.conf != FW_DBG_INVALID)
		IWL_WARN(fwrt, "FW already configured (%d) - re-configuring\n",
			 fwrt->dump.conf);

	/* Send all HCMDs for configuring the FW debug */
	ptr = (void *)&fwrt->fw->dbg.conf_tlv[conf_id]->hcmd;
	for (i = 0; i < fwrt->fw->dbg.conf_tlv[conf_id]->num_of_hcmds; i++) {
		struct iwl_fw_dbg_conf_hcmd *cmd = (void *)ptr;
		struct iwl_host_cmd hcmd = {
			.id = cmd->id,
			.len = { le16_to_cpu(cmd->len), },
			.data = { cmd->data, },
		};

		ret = iwl_trans_send_cmd(fwrt->trans, &hcmd);
		if (ret)
			return ret;

		ptr += sizeof(*cmd);
		ptr += le16_to_cpu(cmd->len);
	}

	fwrt->dump.conf = conf_id;

	return 0;
}
IWL_EXPORT_SYMBOL(iwl_fw_start_dbg_conf);

/* this function assumes dump_start was called beforehand and dump_end will be
 * called afterwards
 */
void iwl_fw_dbg_collect_sync(struct iwl_fw_runtime *fwrt)
{
	struct iwl_fw_dbg_params params = {0};

	if (!test_bit(IWL_FWRT_STATUS_DUMPING, &fwrt->status))
		return;

	if (fwrt->ops && fwrt->ops->fw_running &&
	    !fwrt->ops->fw_running(fwrt->ops_ctx)) {
		IWL_ERR(fwrt, "Firmware not running - cannot dump error\n");
		iwl_fw_free_dump_desc(fwrt);
		clear_bit(IWL_FWRT_STATUS_DUMPING, &fwrt->status);
		return;
	}

	/* there's no point in fw dump if the bus is dead */
	if (test_bit(STATUS_TRANS_DEAD, &fwrt->trans->status)) {
		IWL_ERR(fwrt, "Skip fw error dump since bus is dead\n");
		return;
	}

	iwl_fw_dbg_stop_recording(fwrt, &params);

	IWL_DEBUG_FW_INFO(fwrt, "WRT: data collection start\n");
	if (fwrt->trans->ini_valid)
		iwl_fw_error_ini_dump(fwrt);
	else
		iwl_fw_error_dump(fwrt);
	IWL_DEBUG_FW_INFO(fwrt, "WRT: data collection done\n");

	/* start recording again if the firmware is not crashed */
	if (!test_bit(STATUS_FW_ERROR, &fwrt->trans->status) &&
	    fwrt->fw->dbg.dest_tlv) {
		/* wait before we collect the data till the DBGC stop */
		udelay(500);
		iwl_fw_dbg_restart_recording(fwrt, &params);
	}
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_collect_sync);

void iwl_fw_error_dump_wk(struct work_struct *work)
{
	struct iwl_fw_runtime *fwrt =
		container_of(work, struct iwl_fw_runtime, dump.wk.work);

	if (fwrt->ops && fwrt->ops->dump_start &&
	    fwrt->ops->dump_start(fwrt->ops_ctx))
		return;

	iwl_fw_dbg_collect_sync(fwrt);

	if (fwrt->ops && fwrt->ops->dump_end)
		fwrt->ops->dump_end(fwrt->ops_ctx);
}

void iwl_fw_dbg_read_d3_debug_data(struct iwl_fw_runtime *fwrt)
{
	const struct iwl_cfg *cfg = fwrt->trans->cfg;

	if (!iwl_fw_dbg_is_d3_debug_enabled(fwrt))
		return;

	if (!fwrt->dump.d3_debug_data) {
		fwrt->dump.d3_debug_data = kmalloc(cfg->d3_debug_data_length,
						   GFP_KERNEL);
		if (!fwrt->dump.d3_debug_data) {
			IWL_ERR(fwrt,
				"failed to allocate memory for D3 debug data\n");
			return;
		}
	}

	/* if the buffer holds previous debug data it is overwritten */
	iwl_trans_read_mem_bytes(fwrt->trans, cfg->d3_debug_data_base_addr,
				 fwrt->dump.d3_debug_data,
				 cfg->d3_debug_data_length);
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_read_d3_debug_data);

static void
iwl_fw_dbg_buffer_allocation(struct iwl_fw_runtime *fwrt, u32 size)
{
	struct iwl_trans *trans = fwrt->trans;
	void *virtual_addr = NULL;
	dma_addr_t phys_addr;

	if (WARN_ON_ONCE(trans->num_blocks == ARRAY_SIZE(trans->fw_mon)))
		return;

	virtual_addr =
		dma_alloc_coherent(fwrt->trans->dev, size, &phys_addr,
				   GFP_KERNEL | __GFP_NOWARN | __GFP_ZERO |
				   __GFP_COMP);

	/* TODO: alloc fragments if needed */
	if (!virtual_addr)
		IWL_ERR(fwrt, "Failed to allocate debug memory\n");

	IWL_DEBUG_FW(trans,
		     "Allocated DRAM buffer[%d], size=0x%x\n",
		     trans->num_blocks, size);

	trans->fw_mon[trans->num_blocks].block = virtual_addr;
	trans->fw_mon[trans->num_blocks].physical = phys_addr;
	trans->fw_mon[trans->num_blocks].size = size;
	trans->num_blocks++;
}

static void iwl_fw_dbg_buffer_apply(struct iwl_fw_runtime *fwrt,
				    struct iwl_fw_ini_allocation_data *alloc,
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
	int block_idx = trans->num_blocks;
	u32 buf_location = le32_to_cpu(alloc->tlv.buffer_location);

	if (buf_location == IWL_FW_INI_LOCATION_SRAM_PATH) {
		if (!WARN(pnt != IWL_FW_INI_APPLY_EARLY,
			  "WRT: Invalid apply point %d for SMEM buffer allocation, aborting\n",
			  pnt)) {
			IWL_DEBUG_FW(trans,
				     "WRT: applying SMEM buffer destination\n");

			/* set sram monitor by enabling bit 7 */
			iwl_set_bit(fwrt->trans, CSR_HW_IF_CONFIG_REG,
				    CSR_HW_IF_CONFIG_REG_BIT_MONITOR_SRAM);
		}
		return;
	}

	if (buf_location != IWL_FW_INI_LOCATION_DRAM_PATH)
		return;

	if (!alloc->is_alloc) {
		iwl_fw_dbg_buffer_allocation(fwrt,
					     le32_to_cpu(alloc->tlv.size));
		if (block_idx == trans->num_blocks)
			return;
		alloc->is_alloc = 1;
	}

	/* First block is assigned via registers / context info */
	if (trans->num_blocks == 1)
		return;

	IWL_DEBUG_FW(trans,
		     "WRT: applying DRAM buffer[%d] destination\n", block_idx);

	cmd->num_frags = cpu_to_le32(1);
	cmd->fragments[0].address =
		cpu_to_le64(trans->fw_mon[block_idx].physical);
	cmd->fragments[0].size = alloc->tlv.size;
	cmd->allocation_id = alloc->tlv.allocation_id;
	cmd->buffer_location = alloc->tlv.buffer_location;

	iwl_trans_send_cmd(trans, &hcmd);
}

static void iwl_fw_dbg_send_hcmd(struct iwl_fw_runtime *fwrt,
				 struct iwl_ucode_tlv *tlv,
				 bool ext)
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

	IWL_DEBUG_FW(fwrt,
		     "WRT: ext=%d. Sending host command id=0x%x, group=0x%x\n",
		     ext, data->id, data->group);

	iwl_trans_send_cmd(fwrt->trans, &hcmd);
}

static void iwl_fw_dbg_update_regions(struct iwl_fw_runtime *fwrt,
				      struct iwl_fw_ini_region_tlv *tlv,
				      bool ext, enum iwl_fw_ini_apply_point pnt)
{
	void *iter = (void *)tlv->region_config;
	int i, size = le32_to_cpu(tlv->num_regions);
	const char *err_st =
		"WRT: ext=%d. Invalid region %s %d for apply point %d\n";

	for (i = 0; i < size; i++) {
		struct iwl_fw_ini_region_cfg *reg = iter, **active;
		int id = le32_to_cpu(reg->region_id);
		u32 type = le32_to_cpu(reg->region_type);

		if (WARN(id >= ARRAY_SIZE(fwrt->dump.active_regs), err_st, ext,
			 "id", id, pnt))
			break;

		if (WARN(type == 0 || type >= IWL_FW_INI_REGION_NUM, err_st,
			 ext, "type", type, pnt))
			break;

		active = &fwrt->dump.active_regs[id];

		if (*active)
			IWL_WARN(fwrt->trans,
				 "WRT: ext=%d. Region id %d override\n",
				 ext, id);

		IWL_DEBUG_FW(fwrt,
			     "WRT: ext=%d. Activating region id %d\n",
			     ext, id);

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

static int iwl_fw_dbg_trig_realloc(struct iwl_fw_runtime *fwrt,
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

static void iwl_fw_dbg_update_triggers(struct iwl_fw_runtime *fwrt,
				       struct iwl_fw_ini_trigger_tlv *tlv,
				       bool ext,
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
			 "WRT: ext=%d. Invalid trigger id %d for apply point %d\n",
			 ext, id, apply_point))
			break;

		active = &fwrt->dump.active_trigs[id];

		if (!active->active) {
			size_t trig_size = sizeof(*trig) + trig_regs_size;

			IWL_DEBUG_FW(fwrt,
				     "WRT: ext=%d. Activating trigger %d\n",
				     ext, id);

			if (iwl_fw_dbg_trig_realloc(fwrt, active, id,
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
					     "WRT: ext=%d. Trigger %d regions override\n",
					     ext, id);

				mem_to_add -= active_regs * sizeof(__le32);
			} else {
				IWL_DEBUG_FW(fwrt,
					     "WRT: ext=%d. Trigger %d regions appending\n",
					     ext, id);

				offset += active_regs;
				new_regs += active_regs;
			}

			if (iwl_fw_dbg_trig_realloc(fwrt, active, id,
						    active->size + mem_to_add))
				goto next;

			if (conf_override) {
				IWL_DEBUG_FW(fwrt,
					     "WRT: ext=%d. Trigger %d configuration override\n",
					     ext, id);

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

static void _iwl_fw_dbg_apply_point(struct iwl_fw_runtime *fwrt,
				    struct iwl_apply_point_data *data,
				    enum iwl_fw_ini_apply_point pnt,
				    bool ext)
{
	void *iter = data->data;

	while (iter && iter < data->data + data->size) {
		struct iwl_ucode_tlv *tlv = iter;
		void *ini_tlv = (void *)tlv->data;
		u32 type = le32_to_cpu(tlv->type);

		switch (type) {
		case IWL_UCODE_TLV_TYPE_BUFFER_ALLOCATION: {
			struct iwl_fw_ini_allocation_data *buf_alloc = ini_tlv;

			iwl_fw_dbg_buffer_apply(fwrt, ini_tlv, pnt);
			iter += sizeof(buf_alloc->is_alloc);
			break;
		}
		case IWL_UCODE_TLV_TYPE_HCMD:
			if (pnt < IWL_FW_INI_APPLY_AFTER_ALIVE) {
				IWL_ERR(fwrt,
					"WRT: ext=%d. Invalid apply point %d for host command\n",
					ext, pnt);
				goto next;
			}
			iwl_fw_dbg_send_hcmd(fwrt, tlv, ext);
			break;
		case IWL_UCODE_TLV_TYPE_REGIONS:
			iwl_fw_dbg_update_regions(fwrt, ini_tlv, ext, pnt);
			break;
		case IWL_UCODE_TLV_TYPE_TRIGGERS:
			iwl_fw_dbg_update_triggers(fwrt, ini_tlv, ext, pnt);
			break;
		case IWL_UCODE_TLV_TYPE_DEBUG_FLOW:
			break;
		default:
			WARN_ONCE(1,
				  "WRT: ext=%d. Invalid TLV 0x%x for apply point\n",
				  ext, type);
			break;
		}
next:
		iter += sizeof(*tlv) + le32_to_cpu(tlv->length);
	}
}

void iwl_fw_dbg_apply_point(struct iwl_fw_runtime *fwrt,
			    enum iwl_fw_ini_apply_point apply_point)
{
	void *data = &fwrt->trans->apply_points[apply_point];
	int i;

	IWL_DEBUG_FW(fwrt, "WRT: enabling apply point %d\n", apply_point);

	if (apply_point == IWL_FW_INI_APPLY_EARLY) {
		for (i = 0; i < IWL_FW_INI_MAX_REGION_ID; i++)
			fwrt->dump.active_regs[i] = NULL;

		/* disable the triggers, used in recovery flow */
		for (i = 0; i < IWL_FW_TRIGGER_ID_NUM; i++)
			fwrt->dump.active_trigs[i].active = false;
	}

	_iwl_fw_dbg_apply_point(fwrt, data, apply_point, false);

	data = &fwrt->trans->apply_points_ext[apply_point];
	_iwl_fw_dbg_apply_point(fwrt, data, apply_point, true);
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_apply_point);

void iwl_fwrt_stop_device(struct iwl_fw_runtime *fwrt)
{
	del_timer(&fwrt->dump.periodic_trig);
	iwl_fw_dbg_collect_sync(fwrt);

	iwl_trans_stop_device(fwrt->trans);
}
IWL_EXPORT_SYMBOL(iwl_fwrt_stop_device);

void iwl_fw_dbg_periodic_trig_handler(struct timer_list *t)
{
	struct iwl_fw_runtime *fwrt;
	enum iwl_fw_ini_trigger_id id = IWL_FW_TRIGGER_ID_PERIODIC_TRIGGER;
	int ret;
	typeof(fwrt->dump) *dump_ptr = container_of(t, typeof(fwrt->dump),
						    periodic_trig);

	fwrt = container_of(dump_ptr, typeof(*fwrt), dump);

	ret = _iwl_fw_dbg_ini_collect(fwrt, id);
	if (!ret || ret == -EBUSY) {
		struct iwl_fw_ini_trigger *trig =
			fwrt->dump.active_trigs[id].trig;
		u32 occur = le32_to_cpu(trig->occurrences);
		u32 collect_interval = le32_to_cpu(trig->trigger_data);

		if (!occur)
			return;

		mod_timer(&fwrt->dump.periodic_trig,
			  jiffies + msecs_to_jiffies(collect_interval));
	}
}

#define FSEQ_REG(x) { .addr = (x), .str = #x, }

void iwl_fw_error_print_fseq_regs(struct iwl_fw_runtime *fwrt)
{
	struct iwl_trans *trans = fwrt->trans;
	unsigned long flags;
	int i;
	struct {
		u32 addr;
		const char *str;
	} fseq_regs[] = {
		FSEQ_REG(FSEQ_ERROR_CODE),
		FSEQ_REG(FSEQ_TOP_INIT_VERSION),
		FSEQ_REG(FSEQ_CNVIO_INIT_VERSION),
		FSEQ_REG(FSEQ_OTP_VERSION),
		FSEQ_REG(FSEQ_TOP_CONTENT_VERSION),
		FSEQ_REG(FSEQ_ALIVE_TOKEN),
		FSEQ_REG(FSEQ_CNVI_ID),
		FSEQ_REG(FSEQ_CNVR_ID),
		FSEQ_REG(CNVI_AUX_MISC_CHIP),
		FSEQ_REG(CNVR_AUX_MISC_CHIP),
		FSEQ_REG(CNVR_SCU_SD_REGS_SD_REG_DIG_DCDC_VTRIM),
		FSEQ_REG(CNVR_SCU_SD_REGS_SD_REG_ACTIVE_VDIG_MIRROR),
	};

	if (!iwl_trans_grab_nic_access(trans, &flags))
		return;

	IWL_ERR(fwrt, "Fseq Registers:\n");

	for (i = 0; i < ARRAY_SIZE(fseq_regs); i++)
		IWL_ERR(fwrt, "0x%08X | %s\n",
			iwl_read_prph_no_grab(trans, fseq_regs[i].addr),
			fseq_regs[i].str);

	iwl_trans_release_nic_access(trans, &flags);
}
IWL_EXPORT_SYMBOL(iwl_fw_error_print_fseq_regs);

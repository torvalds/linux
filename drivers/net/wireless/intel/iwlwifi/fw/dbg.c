// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2005-2014, 2018-2024 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 */
#include <linux/devcoredump.h>
#include "iwl-drv.h"
#include "runtime.h"
#include "dbg.h"
#include "debugfs.h"
#include "iwl-io.h"
#include "iwl-prph.h"
#include "iwl-csr.h"
#include "iwl-fh.h"
/**
 * struct iwl_fw_dump_ptrs - set of pointers needed for the fw-error-dump
 *
 * @fwrt_ptr: pointer to the buffer coming from fwrt
 * @trans_ptr: pointer to struct %iwl_trans_dump_data which contains the
 *	transport's data.
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
	int i;

	IWL_DEBUG_INFO(fwrt, "WRT radio registers dump\n");

	if (!iwl_trans_grab_nic_access(fwrt->trans))
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

	iwl_trans_release_nic_access(fwrt->trans);
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
	for (i = 0; i < fifo_len / sizeof(u32); i++)
		fifo_data[i] = iwl_trans_read_prph(fwrt->trans,
						  TXF_READ_MODIFY_DATA +
						  offset);

	if (fwrt->sanitize_ops && fwrt->sanitize_ops->frob_txf)
		fwrt->sanitize_ops->frob_txf(fwrt->sanitize_ctx,
					     fifo_data, fifo_len);

	*dump_data = iwl_fw_error_next_data(*dump_data);
}

static void iwl_fw_dump_rxf(struct iwl_fw_runtime *fwrt,
			    struct iwl_fw_error_dump_data **dump_data)
{
	struct iwl_fwrt_shared_mem_cfg *cfg = &fwrt->smem_cfg;

	IWL_DEBUG_INFO(fwrt, "WRT RX FIFO dump\n");

	if (!iwl_trans_grab_nic_access(fwrt->trans))
		return;

	if (iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_RXF)) {
		/* Pull RXF1 */
		iwl_fwrt_dump_rxf(fwrt, dump_data,
				  cfg->lmac[0].rxfifo1_size, 0, 0);
		/* Pull RXF2 */
		iwl_fwrt_dump_rxf(fwrt, dump_data, cfg->rxfifo2_size,
				  RXF_DIFF_FROM_PREV +
				  fwrt->trans->trans_cfg->umac_prph_offset, 1);
		/* Pull LMAC2 RXF1 */
		if (fwrt->smem_cfg.num_lmacs > 1)
			iwl_fwrt_dump_rxf(fwrt, dump_data,
					  cfg->lmac[1].rxfifo1_size,
					  LMAC2_PRPH_OFFSET, 2);
	}

	iwl_trans_release_nic_access(fwrt->trans);
}

static void iwl_fw_dump_txf(struct iwl_fw_runtime *fwrt,
			    struct iwl_fw_error_dump_data **dump_data)
{
	struct iwl_fw_error_dump_fifo *fifo_hdr;
	struct iwl_fwrt_shared_mem_cfg *cfg = &fwrt->smem_cfg;
	u32 *fifo_data;
	u32 fifo_len;
	int i, j;

	IWL_DEBUG_INFO(fwrt, "WRT TX FIFO dump\n");

	if (!iwl_trans_grab_nic_access(fwrt->trans))
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

	iwl_trans_release_nic_access(fwrt->trans);
}

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
	{ .start = 0x00a04764, .end = 0x00a0476c },
	{ .start = 0x00a04770, .end = 0x00a04774 },
	{ .start = 0x00a04620, .end = 0x00a04624 },
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
	u32 i;

	if (!data)
		return;

	IWL_DEBUG_INFO(trans, "WRT PRPH dump\n");

	if (!iwl_trans_grab_nic_access(trans))
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

	iwl_trans_release_nic_access(trans);
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
			kfree(table);
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

	if (fwrt->trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_AX210) {
		range_len = ARRAY_SIZE(iwl_prph_dump_addr_ax210);
		handler(fwrt, iwl_prph_dump_addr_ax210, range_len, ptr);
	} else if (fwrt->trans->trans_cfg->device_family >=
		   IWL_DEVICE_FAMILY_22000) {
		range_len = ARRAY_SIZE(iwl_prph_dump_addr_22000);
		handler(fwrt, iwl_prph_dump_addr_22000, range_len, ptr);
	} else {
		range_len = ARRAY_SIZE(iwl_prph_dump_addr_comm);
		handler(fwrt, iwl_prph_dump_addr_comm, range_len, ptr);

		if (fwrt->trans->trans_cfg->mq_rx_supported) {
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

	if (fwrt->sanitize_ops && fwrt->sanitize_ops->frob_mem)
		fwrt->sanitize_ops->frob_mem(fwrt->sanitize_ctx, ofs,
					     dump_mem->data, len);

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

		if (fwrt->sanitize_ops && fwrt->sanitize_ops->frob_mem)
			fwrt->sanitize_ops->frob_mem(fwrt->sanitize_ctx,
						     fwrt->fw_paging_db[i].fw_offs,
						     paging->data,
						     PAGING_BLOCK_SIZE);
	}
}

static struct iwl_fw_error_dump_file *
iwl_fw_error_dump_file(struct iwl_fw_runtime *fwrt,
		       struct iwl_fw_dump_ptrs *fw_error_dump,
		       struct iwl_fwrt_dump_data *data)
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

		if (fwrt->trans->trans_cfg->device_family ==
		    IWL_DEVICE_FAMILY_7000 &&
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
	if (data->monitor_only) {
		file_len = sizeof(*dump_file) + sizeof(*dump_data) * 2 +
			   sizeof(*dump_info) + sizeof(*dump_smem_cfg);
	}

	if (iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_ERROR_INFO) &&
	    data->desc)
		file_len += sizeof(*dump_data) + sizeof(*dump_trig) +
			data->desc->len;

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
			cpu_to_le32(fwrt->trans->hw_rev_step);
		memcpy(dump_info->fw_human_readable, fwrt->fw->human_readable,
		       sizeof(dump_info->fw_human_readable));
		strscpy_pad(dump_info->dev_human_readable, fwrt->trans->name,
			sizeof(dump_info->dev_human_readable));
		strscpy_pad(dump_info->bus_human_readable, fwrt->dev->bus->name,
			sizeof(dump_info->bus_human_readable));
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
	    data->desc) {
		dump_data->type = cpu_to_le32(IWL_FW_ERROR_DUMP_ERROR_INFO);
		dump_data->len = cpu_to_le32(sizeof(*dump_trig) +
					     data->desc->len);
		dump_trig = (void *)dump_data->data;
		memcpy(dump_trig, &data->desc->trig_desc,
		       sizeof(*dump_trig) + data->desc->len);

		dump_data = iwl_fw_error_next_data(dump_data);
	}

	/* In case we only want monitor dump, skip to dump trasport data */
	if (data->monitor_only)
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

		if (fwrt->sanitize_ops && fwrt->sanitize_ops->frob_mem)
			fwrt->sanitize_ops->frob_mem(fwrt->sanitize_ctx, addr,
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

/**
 * struct iwl_dump_ini_region_data - region data
 * @reg_tlv: region TLV
 * @dump_data: dump data
 */
struct iwl_dump_ini_region_data {
	struct iwl_ucode_tlv *reg_tlv;
	struct iwl_fwrt_dump_data *dump_data;
};

static int iwl_dump_ini_prph_mac_iter_common(struct iwl_fw_runtime *fwrt,
					     void *range_ptr, u32 addr,
					     __le32 size)
{
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	__le32 *val = range->data;
	u32 prph_val;
	int i;

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = size;
	for (i = 0; i < le32_to_cpu(size); i += 4) {
		prph_val = iwl_read_prph(fwrt->trans, addr + i);
		if (iwl_trans_is_hw_error_value(prph_val))
			return -EBUSY;
		*val++ = cpu_to_le32(prph_val);
	}

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int
iwl_dump_ini_prph_mac_iter(struct iwl_fw_runtime *fwrt,
			   struct iwl_dump_ini_region_data *reg_data,
			   void *range_ptr, u32 range_len, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	u32 addr = le32_to_cpu(reg->addrs[idx]) +
		   le32_to_cpu(reg->dev_addr.offset);

	return iwl_dump_ini_prph_mac_iter_common(fwrt, range_ptr, addr,
						 reg->dev_addr.size);
}

static int
iwl_dump_ini_prph_mac_block_iter(struct iwl_fw_runtime *fwrt,
				 struct iwl_dump_ini_region_data *reg_data,
				 void *range_ptr, u32 range_len, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_addr_size *pairs = (void *)reg->addrs;
	u32 addr = le32_to_cpu(reg->dev_addr_range.offset) +
		   le32_to_cpu(pairs[idx].addr);

	return iwl_dump_ini_prph_mac_iter_common(fwrt, range_ptr, addr,
						 pairs[idx].size);
}

static int iwl_dump_ini_prph_phy_iter_common(struct iwl_fw_runtime *fwrt,
					     void *range_ptr, u32 addr,
					     __le32 size, __le32 offset)
{
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	__le32 *val = range->data;
	u32 indirect_wr_addr = WMAL_INDRCT_RD_CMD1;
	u32 indirect_rd_addr = WMAL_MRSPF_1;
	u32 prph_val;
	u32 dphy_state;
	u32 dphy_addr;
	int i;

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = size;

	if (fwrt->trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_AX210)
		indirect_wr_addr = WMAL_INDRCT_CMD1;

	indirect_wr_addr += le32_to_cpu(offset);
	indirect_rd_addr += le32_to_cpu(offset);

	if (!iwl_trans_grab_nic_access(fwrt->trans))
		return -EBUSY;

	dphy_addr = (offset) ? WFPM_LMAC2_PS_CTL_RW : WFPM_LMAC1_PS_CTL_RW;
	dphy_state = iwl_read_umac_prph_no_grab(fwrt->trans, dphy_addr);

	for (i = 0; i < le32_to_cpu(size); i += 4) {
		if (dphy_state == HBUS_TIMEOUT ||
		    (dphy_state & WFPM_PS_CTL_RW_PHYRF_PD_FSM_CURSTATE_MSK) !=
		    WFPM_PHYRF_STATE_ON) {
			*val++ = cpu_to_le32(WFPM_DPHY_OFF);
			continue;
		}

		iwl_write_prph_no_grab(fwrt->trans, indirect_wr_addr,
				       WMAL_INDRCT_CMD(addr + i));
		prph_val = iwl_read_prph_no_grab(fwrt->trans,
						 indirect_rd_addr);
		*val++ = cpu_to_le32(prph_val);
	}

	iwl_trans_release_nic_access(fwrt->trans);
	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int
iwl_dump_ini_prph_phy_iter(struct iwl_fw_runtime *fwrt,
			   struct iwl_dump_ini_region_data *reg_data,
			   void *range_ptr, u32 range_len, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	u32 addr = le32_to_cpu(reg->addrs[idx]);

	return iwl_dump_ini_prph_phy_iter_common(fwrt, range_ptr, addr,
						 reg->dev_addr.size,
						 reg->dev_addr.offset);
}

static int
iwl_dump_ini_prph_phy_block_iter(struct iwl_fw_runtime *fwrt,
				 struct iwl_dump_ini_region_data *reg_data,
				 void *range_ptr, u32 range_len, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_addr_size *pairs = (void *)reg->addrs;
	u32 addr = le32_to_cpu(pairs[idx].addr);

	return iwl_dump_ini_prph_phy_iter_common(fwrt, range_ptr, addr,
						 pairs[idx].size,
						 reg->dev_addr_range.offset);
}

static int iwl_dump_ini_csr_iter(struct iwl_fw_runtime *fwrt,
				 struct iwl_dump_ini_region_data *reg_data,
				 void *range_ptr, u32 range_len, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	__le32 *val = range->data;
	u32 addr = le32_to_cpu(reg->addrs[idx]) +
		   le32_to_cpu(reg->dev_addr.offset);
	int i;

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = reg->dev_addr.size;
	for (i = 0; i < le32_to_cpu(reg->dev_addr.size); i += 4)
		*val++ = cpu_to_le32(iwl_trans_read32(fwrt->trans, addr + i));

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int iwl_dump_ini_config_iter(struct iwl_fw_runtime *fwrt,
				    struct iwl_dump_ini_region_data *reg_data,
				    void *range_ptr, u32 range_len, int idx)
{
	struct iwl_trans *trans = fwrt->trans;
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	__le32 *val = range->data;
	u32 addr = le32_to_cpu(reg->addrs[idx]) +
		   le32_to_cpu(reg->dev_addr.offset);
	int i;

	/* we shouldn't get here if the trans doesn't have read_config32 */
	if (WARN_ON_ONCE(!trans->ops->read_config32))
		return -EOPNOTSUPP;

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = reg->dev_addr.size;
	for (i = 0; i < le32_to_cpu(reg->dev_addr.size); i += 4) {
		int ret;
		u32 tmp;

		ret = trans->ops->read_config32(trans, addr + i, &tmp);
		if (ret < 0)
			return ret;

		*val++ = cpu_to_le32(tmp);
	}

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int iwl_dump_ini_dev_mem_iter(struct iwl_fw_runtime *fwrt,
				     struct iwl_dump_ini_region_data *reg_data,
				     void *range_ptr, u32 range_len, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	u32 addr = le32_to_cpu(reg->addrs[idx]) +
		   le32_to_cpu(reg->dev_addr.offset);

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = reg->dev_addr.size;
	iwl_trans_read_mem_bytes(fwrt->trans, addr, range->data,
				 le32_to_cpu(reg->dev_addr.size));

	if (reg->sub_type == IWL_FW_INI_REGION_DEVICE_MEMORY_SUBTYPE_HW_SMEM &&
	    fwrt->sanitize_ops && fwrt->sanitize_ops->frob_txf)
		fwrt->sanitize_ops->frob_txf(fwrt->sanitize_ctx,
					     range->data,
					     le32_to_cpu(reg->dev_addr.size));

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int _iwl_dump_ini_paging_iter(struct iwl_fw_runtime *fwrt,
				     void *range_ptr, u32 range_len, int idx)
{
	struct page *page = fwrt->fw_paging_db[idx].fw_paging_block;
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

static int iwl_dump_ini_paging_iter(struct iwl_fw_runtime *fwrt,
				    struct iwl_dump_ini_region_data *reg_data,
				    void *range_ptr, u32 range_len, int idx)
{
	struct iwl_fw_ini_error_dump_range *range;
	u32 page_size;

	/* all paged index start from 1 to skip CSS section */
	idx++;

	if (!fwrt->trans->trans_cfg->gen2)
		return _iwl_dump_ini_paging_iter(fwrt, range_ptr, range_len, idx);

	range = range_ptr;
	page_size = fwrt->trans->init_dram.paging[idx].size;

	range->page_num = cpu_to_le32(idx);
	range->range_data_size = cpu_to_le32(page_size);
	memcpy(range->data, fwrt->trans->init_dram.paging[idx].block,
	       page_size);

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int
iwl_dump_ini_mon_dram_iter(struct iwl_fw_runtime *fwrt,
			   struct iwl_dump_ini_region_data *reg_data,
			   void *range_ptr, u32 range_len, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	struct iwl_dram_data *frag;
	u32 alloc_id = le32_to_cpu(reg->dram_alloc_id);

	frag = &fwrt->trans->dbg.fw_mon_ini[alloc_id].frags[idx];

	range->dram_base_addr = cpu_to_le64(frag->physical);
	range->range_data_size = cpu_to_le32(frag->size);

	memcpy(range->data, frag->block, frag->size);

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int iwl_dump_ini_mon_smem_iter(struct iwl_fw_runtime *fwrt,
				      struct iwl_dump_ini_region_data *reg_data,
				      void *range_ptr, u32 range_len, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	u32 addr = le32_to_cpu(reg->internal_buffer.base_addr);

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = reg->internal_buffer.size;
	iwl_trans_read_mem_bytes(fwrt->trans, addr, range->data,
				 le32_to_cpu(reg->internal_buffer.size));

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static bool iwl_ini_txf_iter(struct iwl_fw_runtime *fwrt,
			     struct iwl_dump_ini_region_data *reg_data, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_txf_iter_data *iter = &fwrt->dump.txf_iter_data;
	struct iwl_fwrt_shared_mem_cfg *cfg = &fwrt->smem_cfg;
	int txf_num = cfg->num_txfifo_entries;
	int int_txf_num = ARRAY_SIZE(cfg->internal_txfifo_size);
	u32 lmac_bitmap = le32_to_cpu(reg->fifos.fid[0]);

	if (!idx) {
		if (le32_to_cpu(reg->fifos.offset) && cfg->num_lmacs == 1) {
			IWL_ERR(fwrt, "WRT: Invalid lmac offset 0x%x\n",
				le32_to_cpu(reg->fifos.offset));
			return false;
		}

		iter->internal_txf = 0;
		iter->fifo_size = 0;
		iter->fifo = -1;
		if (le32_to_cpu(reg->fifos.offset))
			iter->lmac = 1;
		else
			iter->lmac = 0;
	}

	if (!iter->internal_txf) {
		for (iter->fifo++; iter->fifo < txf_num; iter->fifo++) {
			iter->fifo_size =
				cfg->lmac[iter->lmac].txfifo_size[iter->fifo];
			if (iter->fifo_size && (lmac_bitmap & BIT(iter->fifo)))
				return true;
		}
		iter->fifo--;
	}

	iter->internal_txf = 1;

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
				 struct iwl_dump_ini_region_data *reg_data,
				 void *range_ptr, u32 range_len, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	struct iwl_txf_iter_data *iter = &fwrt->dump.txf_iter_data;
	struct iwl_fw_ini_error_dump_register *reg_dump = (void *)range->data;
	u32 offs = le32_to_cpu(reg->fifos.offset), addr;
	u32 registers_num = iwl_tlv_array_len(reg_data->reg_tlv, reg, addrs);
	u32 registers_size = registers_num * sizeof(*reg_dump);
	__le32 *data;
	int i;

	if (!iwl_ini_txf_iter(fwrt, reg_data, idx))
		return -EIO;

	if (!iwl_trans_grab_nic_access(fwrt->trans))
		return -EBUSY;

	range->fifo_hdr.fifo_num = cpu_to_le32(iter->fifo);
	range->fifo_hdr.num_of_registers = cpu_to_le32(registers_num);
	range->range_data_size = cpu_to_le32(iter->fifo_size + registers_size);

	iwl_write_prph_no_grab(fwrt->trans, TXF_LARC_NUM + offs, iter->fifo);

	/*
	 * read txf registers. for each register, write to the dump the
	 * register address and its value
	 */
	for (i = 0; i < registers_num; i++) {
		addr = le32_to_cpu(reg->addrs[i]) + offs;

		reg_dump->addr = cpu_to_le32(addr);
		reg_dump->data = cpu_to_le32(iwl_read_prph_no_grab(fwrt->trans,
								   addr));

		reg_dump++;
	}

	if (reg->fifos.hdr_only) {
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

	if (fwrt->sanitize_ops && fwrt->sanitize_ops->frob_txf)
		fwrt->sanitize_ops->frob_txf(fwrt->sanitize_ctx,
					     reg_dump, iter->fifo_size);

out:
	iwl_trans_release_nic_access(fwrt->trans);

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int
iwl_dump_ini_prph_snps_dphyip_iter(struct iwl_fw_runtime *fwrt,
				   struct iwl_dump_ini_region_data *reg_data,
				   void *range_ptr, u32 range_len, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	__le32 *val = range->data;
	__le32 offset = reg->dev_addr.offset;
	u32 indirect_rd_wr_addr = DPHYIP_INDIRECT;
	u32 addr = le32_to_cpu(reg->addrs[idx]);
	u32 dphy_state, dphy_addr, prph_val;
	int i;

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = reg->dev_addr.size;

	if (!iwl_trans_grab_nic_access(fwrt->trans))
		return -EBUSY;

	indirect_rd_wr_addr += le32_to_cpu(offset);

	dphy_addr = offset ? WFPM_LMAC2_PS_CTL_RW : WFPM_LMAC1_PS_CTL_RW;
	dphy_state = iwl_read_umac_prph_no_grab(fwrt->trans, dphy_addr);

	for (i = 0; i < le32_to_cpu(reg->dev_addr.size); i += 4) {
		if (dphy_state == HBUS_TIMEOUT ||
		    (dphy_state & WFPM_PS_CTL_RW_PHYRF_PD_FSM_CURSTATE_MSK) !=
		    WFPM_PHYRF_STATE_ON) {
			*val++ = cpu_to_le32(WFPM_DPHY_OFF);
			continue;
		}

		iwl_write_prph_no_grab(fwrt->trans, indirect_rd_wr_addr,
				       addr + i);
		/* wait a bit for value to be ready in register */
		udelay(1);
		prph_val = iwl_read_prph_no_grab(fwrt->trans,
						 indirect_rd_wr_addr);
		*val++ = cpu_to_le32((prph_val & DPHYIP_INDIRECT_RD_MSK) >>
				     DPHYIP_INDIRECT_RD_SHIFT);
	}

	iwl_trans_release_nic_access(fwrt->trans);
	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

struct iwl_ini_rxf_data {
	u32 fifo_num;
	u32 size;
	u32 offset;
};

static void iwl_ini_get_rxf_data(struct iwl_fw_runtime *fwrt,
				 struct iwl_dump_ini_region_data *reg_data,
				 struct iwl_ini_rxf_data *data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	u32 fid1 = le32_to_cpu(reg->fifos.fid[0]);
	u32 fid2 = le32_to_cpu(reg->fifos.fid[1]);
	u8 fifo_idx;

	if (!data)
		return;

	memset(data, 0, sizeof(*data));

	/* make sure only one bit is set in only one fid */
	if (WARN_ONCE(hweight_long(fid1) + hweight_long(fid2) != 1,
		      "fid1=%x, fid2=%x\n", fid1, fid2))
		return;

	if (fid1) {
		fifo_idx = ffs(fid1) - 1;
		if (WARN_ONCE(fifo_idx >= MAX_NUM_LMAC, "fifo_idx=%d\n",
			      fifo_idx))
			return;

		data->size = fwrt->smem_cfg.lmac[fifo_idx].rxfifo1_size;
		data->fifo_num = fifo_idx;
	} else {
		u8 max_idx;

		fifo_idx = ffs(fid2) - 1;
		if (iwl_fw_lookup_notif_ver(fwrt->fw, SYSTEM_GROUP,
					    SHARED_MEM_CFG_CMD, 0) <= 3)
			max_idx = 0;
		else
			max_idx = 1;

		if (WARN_ONCE(fifo_idx > max_idx,
			      "invalid umac fifo idx %d", fifo_idx))
			return;

		/* use bit 31 to distinguish between umac and lmac rxf while
		 * parsing the dump
		 */
		data->fifo_num = fifo_idx | IWL_RXF_UMAC_BIT;

		switch (fifo_idx) {
		case 0:
			data->size = fwrt->smem_cfg.rxfifo2_size;
			data->offset = iwl_umac_prph(fwrt->trans,
						     RXF_DIFF_FROM_PREV);
			break;
		case 1:
			data->size = fwrt->smem_cfg.rxfifo2_control_size;
			data->offset = iwl_umac_prph(fwrt->trans,
						     RXF2C_DIFF_FROM_PREV);
			break;
		}
	}
}

static int iwl_dump_ini_rxf_iter(struct iwl_fw_runtime *fwrt,
				 struct iwl_dump_ini_region_data *reg_data,
				 void *range_ptr, u32 range_len, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	struct iwl_ini_rxf_data rxf_data;
	struct iwl_fw_ini_error_dump_register *reg_dump = (void *)range->data;
	u32 offs = le32_to_cpu(reg->fifos.offset), addr;
	u32 registers_num = iwl_tlv_array_len(reg_data->reg_tlv, reg, addrs);
	u32 registers_size = registers_num * sizeof(*reg_dump);
	__le32 *data;
	int i;

	iwl_ini_get_rxf_data(fwrt, reg_data, &rxf_data);
	if (!rxf_data.size)
		return -EIO;

	if (!iwl_trans_grab_nic_access(fwrt->trans))
		return -EBUSY;

	range->fifo_hdr.fifo_num = cpu_to_le32(rxf_data.fifo_num);
	range->fifo_hdr.num_of_registers = cpu_to_le32(registers_num);
	range->range_data_size = cpu_to_le32(rxf_data.size + registers_size);

	/*
	 * read rxf registers. for each register, write to the dump the
	 * register address and its value
	 */
	for (i = 0; i < registers_num; i++) {
		addr = le32_to_cpu(reg->addrs[i]) + offs;

		reg_dump->addr = cpu_to_le32(addr);
		reg_dump->data = cpu_to_le32(iwl_read_prph_no_grab(fwrt->trans,
								   addr));

		reg_dump++;
	}

	if (reg->fifos.hdr_only) {
		range->range_data_size = cpu_to_le32(registers_size);
		goto out;
	}

	offs = rxf_data.offset;

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
	iwl_trans_release_nic_access(fwrt->trans);

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int
iwl_dump_ini_err_table_iter(struct iwl_fw_runtime *fwrt,
			    struct iwl_dump_ini_region_data *reg_data,
			    void *range_ptr, u32 range_len, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_region_err_table *err_table = &reg->err_table;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	u32 addr = le32_to_cpu(err_table->base_addr) +
		   le32_to_cpu(err_table->offset);

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = err_table->size;
	iwl_trans_read_mem_bytes(fwrt->trans, addr, range->data,
				 le32_to_cpu(err_table->size));

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int
iwl_dump_ini_special_mem_iter(struct iwl_fw_runtime *fwrt,
			      struct iwl_dump_ini_region_data *reg_data,
			      void *range_ptr, u32 range_len, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_region_special_device_memory *special_mem =
		&reg->special_mem;

	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	u32 addr = le32_to_cpu(special_mem->base_addr) +
		   le32_to_cpu(special_mem->offset);

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = special_mem->size;
	iwl_trans_read_mem_bytes(fwrt->trans, addr, range->data,
				 le32_to_cpu(special_mem->size));

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int
iwl_dump_ini_dbgi_sram_iter(struct iwl_fw_runtime *fwrt,
			    struct iwl_dump_ini_region_data *reg_data,
			    void *range_ptr, u32 range_len, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	__le32 *val = range->data;
	u32 prph_data;
	int i;

	if (!iwl_trans_grab_nic_access(fwrt->trans))
		return -EBUSY;

	range->range_data_size = reg->dev_addr.size;
	for (i = 0; i < (le32_to_cpu(reg->dev_addr.size) / 4); i++) {
		prph_data = iwl_read_prph_no_grab(fwrt->trans, (i % 2) ?
					  DBGI_SRAM_TARGET_ACCESS_RDATA_MSB :
					  DBGI_SRAM_TARGET_ACCESS_RDATA_LSB);
		if (iwl_trans_is_hw_error_value(prph_data)) {
			iwl_trans_release_nic_access(fwrt->trans);
			return -EBUSY;
		}
		*val++ = cpu_to_le32(prph_data);
	}
	iwl_trans_release_nic_access(fwrt->trans);
	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int iwl_dump_ini_fw_pkt_iter(struct iwl_fw_runtime *fwrt,
				    struct iwl_dump_ini_region_data *reg_data,
				    void *range_ptr, u32 range_len, int idx)
{
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	struct iwl_rx_packet *pkt = reg_data->dump_data->fw_pkt;
	u32 pkt_len;

	if (!pkt)
		return -EIO;

	pkt_len = iwl_rx_packet_payload_len(pkt);

	memcpy(&range->fw_pkt_hdr, &pkt->hdr, sizeof(range->fw_pkt_hdr));
	range->range_data_size = cpu_to_le32(pkt_len);

	memcpy(range->data, pkt->data, pkt_len);

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int iwl_dump_ini_imr_iter(struct iwl_fw_runtime *fwrt,
				 struct iwl_dump_ini_region_data *reg_data,
				 void *range_ptr, u32 range_len, int idx)
{
	/* read the IMR memory and DMA it to SRAM */
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	u64 imr_curr_addr = fwrt->trans->dbg.imr_data.imr_curr_addr;
	u32 imr_rem_bytes = fwrt->trans->dbg.imr_data.imr2sram_remainbyte;
	u32 sram_addr = fwrt->trans->dbg.imr_data.sram_addr;
	u32 sram_size = fwrt->trans->dbg.imr_data.sram_size;
	u32 size_to_dump = (imr_rem_bytes > sram_size) ? sram_size : imr_rem_bytes;

	range->range_data_size = cpu_to_le32(size_to_dump);
	if (iwl_trans_write_imr_mem(fwrt->trans, sram_addr,
				    imr_curr_addr, size_to_dump)) {
		IWL_ERR(fwrt, "WRT_DEBUG: IMR Memory transfer failed\n");
		return -1;
	}

	fwrt->trans->dbg.imr_data.imr_curr_addr = imr_curr_addr + size_to_dump;
	fwrt->trans->dbg.imr_data.imr2sram_remainbyte -= size_to_dump;

	iwl_trans_read_mem_bytes(fwrt->trans, sram_addr, range->data,
				 size_to_dump);
	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static void *
iwl_dump_ini_mem_fill_header(struct iwl_fw_runtime *fwrt,
			     struct iwl_dump_ini_region_data *reg_data,
			     void *data, u32 data_len)
{
	struct iwl_fw_ini_error_dump *dump = data;

	dump->header.version = cpu_to_le32(IWL_INI_DUMP_VER);

	return dump->data;
}

/**
 * mask_apply_and_normalize - applies mask on val and normalize the result
 *
 * The normalization is based on the first set bit in the mask
 *
 * @val: value
 * @mask: mask to apply and to normalize with
 */
static u32 mask_apply_and_normalize(u32 val, u32 mask)
{
	return (val & mask) >> (ffs(mask) - 1);
}

static __le32 iwl_get_mon_reg(struct iwl_fw_runtime *fwrt, u32 alloc_id,
			      const struct iwl_fw_mon_reg *reg_info)
{
	u32 val, offs;

	/* The header addresses of DBGCi is calculate as follows:
	 * DBGC1 address + (0x100 * i)
	 */
	offs = (alloc_id - IWL_FW_INI_ALLOCATION_ID_DBGC1) * 0x100;

	if (!reg_info || !reg_info->addr || !reg_info->mask)
		return 0;

	val = iwl_read_prph_no_grab(fwrt->trans, reg_info->addr + offs);

	return cpu_to_le32(mask_apply_and_normalize(val, reg_info->mask));
}

static void *
iwl_dump_ini_mon_fill_header(struct iwl_fw_runtime *fwrt, u32 alloc_id,
			     struct iwl_fw_ini_monitor_dump *data,
			     const struct iwl_fw_mon_regs *addrs)
{
	if (!iwl_trans_grab_nic_access(fwrt->trans)) {
		IWL_ERR(fwrt, "Failed to get monitor header\n");
		return NULL;
	}

	data->write_ptr = iwl_get_mon_reg(fwrt, alloc_id,
					  &addrs->write_ptr);
	if (fwrt->trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_AX210) {
		u32 wrt_ptr = le32_to_cpu(data->write_ptr);

		data->write_ptr = cpu_to_le32(wrt_ptr >> 2);
	}
	data->cycle_cnt = iwl_get_mon_reg(fwrt, alloc_id,
					  &addrs->cycle_cnt);
	data->cur_frag = iwl_get_mon_reg(fwrt, alloc_id,
					 &addrs->cur_frag);

	iwl_trans_release_nic_access(fwrt->trans);

	data->header.version = cpu_to_le32(IWL_INI_DUMP_VER);

	return data->data;
}

static void *
iwl_dump_ini_mon_dram_fill_header(struct iwl_fw_runtime *fwrt,
				  struct iwl_dump_ini_region_data *reg_data,
				  void *data, u32 data_len)
{
	struct iwl_fw_ini_monitor_dump *mon_dump = (void *)data;
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	u32 alloc_id = le32_to_cpu(reg->dram_alloc_id);

	return iwl_dump_ini_mon_fill_header(fwrt, alloc_id, mon_dump,
					    &fwrt->trans->cfg->mon_dram_regs);
}

static void *
iwl_dump_ini_mon_smem_fill_header(struct iwl_fw_runtime *fwrt,
				  struct iwl_dump_ini_region_data *reg_data,
				  void *data, u32 data_len)
{
	struct iwl_fw_ini_monitor_dump *mon_dump = (void *)data;
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	u32 alloc_id = le32_to_cpu(reg->internal_buffer.alloc_id);

	return iwl_dump_ini_mon_fill_header(fwrt, alloc_id, mon_dump,
					    &fwrt->trans->cfg->mon_smem_regs);
}

static void *
iwl_dump_ini_mon_dbgi_fill_header(struct iwl_fw_runtime *fwrt,
				  struct iwl_dump_ini_region_data *reg_data,
				  void *data, u32 data_len)
{
	struct iwl_fw_ini_monitor_dump *mon_dump = (void *)data;

	return iwl_dump_ini_mon_fill_header(fwrt,
					    /* no offset calculation later */
					    IWL_FW_INI_ALLOCATION_ID_DBGC1,
					    mon_dump,
					    &fwrt->trans->cfg->mon_dbgi_regs);
}

static void *
iwl_dump_ini_err_table_fill_header(struct iwl_fw_runtime *fwrt,
				   struct iwl_dump_ini_region_data *reg_data,
				   void *data, u32 data_len)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_err_table_dump *dump = data;

	dump->header.version = cpu_to_le32(IWL_INI_DUMP_VER);
	dump->version = reg->err_table.version;

	return dump->data;
}

static void *
iwl_dump_ini_special_mem_fill_header(struct iwl_fw_runtime *fwrt,
				     struct iwl_dump_ini_region_data *reg_data,
				     void *data, u32 data_len)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_special_device_memory *dump = data;

	dump->header.version = cpu_to_le32(IWL_INI_DUMP_VER);
	dump->type = reg->special_mem.type;
	dump->version = reg->special_mem.version;

	return dump->data;
}

static void *
iwl_dump_ini_imr_fill_header(struct iwl_fw_runtime *fwrt,
			     struct iwl_dump_ini_region_data *reg_data,
			     void *data, u32 data_len)
{
	struct iwl_fw_ini_error_dump *dump = data;

	dump->header.version = cpu_to_le32(IWL_INI_DUMP_VER);

	return dump->data;
}

static u32 iwl_dump_ini_mem_ranges(struct iwl_fw_runtime *fwrt,
				   struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;

	return iwl_tlv_array_len(reg_data->reg_tlv, reg, addrs);
}

static u32
iwl_dump_ini_mem_block_ranges(struct iwl_fw_runtime *fwrt,
			      struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	size_t size = sizeof(struct iwl_fw_ini_addr_size);

	return iwl_tlv_array_len_with_size(reg_data->reg_tlv, reg, size);
}

static u32 iwl_dump_ini_paging_ranges(struct iwl_fw_runtime *fwrt,
				      struct iwl_dump_ini_region_data *reg_data)
{
	if (fwrt->trans->trans_cfg->gen2) {
		if (fwrt->trans->init_dram.paging_cnt)
			return fwrt->trans->init_dram.paging_cnt - 1;
		else
			return 0;
	}

	return fwrt->num_of_paging_blk;
}

static u32
iwl_dump_ini_mon_dram_ranges(struct iwl_fw_runtime *fwrt,
			     struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_mon *fw_mon;
	u32 ranges = 0, alloc_id = le32_to_cpu(reg->dram_alloc_id);
	int i;

	fw_mon = &fwrt->trans->dbg.fw_mon_ini[alloc_id];

	for (i = 0; i < fw_mon->num_frags; i++) {
		if (!fw_mon->frags[i].size)
			break;

		ranges++;
	}

	return ranges;
}

static u32 iwl_dump_ini_txf_ranges(struct iwl_fw_runtime *fwrt,
				   struct iwl_dump_ini_region_data *reg_data)
{
	u32 num_of_fifos = 0;

	while (iwl_ini_txf_iter(fwrt, reg_data, num_of_fifos))
		num_of_fifos++;

	return num_of_fifos;
}

static u32 iwl_dump_ini_single_range(struct iwl_fw_runtime *fwrt,
				     struct iwl_dump_ini_region_data *reg_data)
{
	return 1;
}

static u32 iwl_dump_ini_imr_ranges(struct iwl_fw_runtime *fwrt,
				   struct iwl_dump_ini_region_data *reg_data)
{
	/* range is total number of pages need to copied from
	 *IMR memory to SRAM and later from SRAM to DRAM
	 */
	u32 imr_enable = fwrt->trans->dbg.imr_data.imr_enable;
	u32 imr_size = fwrt->trans->dbg.imr_data.imr_size;
	u32 sram_size = fwrt->trans->dbg.imr_data.sram_size;

	if (imr_enable == 0 || imr_size == 0 || sram_size == 0) {
		IWL_DEBUG_INFO(fwrt,
			       "WRT: Invalid imr data enable: %d, imr_size: %d, sram_size: %d\n",
			       imr_enable, imr_size, sram_size);
		return 0;
	}

	return((imr_size % sram_size) ? (imr_size / sram_size + 1) : (imr_size / sram_size));
}

static u32 iwl_dump_ini_mem_get_size(struct iwl_fw_runtime *fwrt,
				     struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	u32 size = le32_to_cpu(reg->dev_addr.size);
	u32 ranges = iwl_dump_ini_mem_ranges(fwrt, reg_data);

	if (!size || !ranges)
		return 0;

	return sizeof(struct iwl_fw_ini_error_dump) + ranges *
		(size + sizeof(struct iwl_fw_ini_error_dump_range));
}

static u32
iwl_dump_ini_mem_block_get_size(struct iwl_fw_runtime *fwrt,
				struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_addr_size *pairs = (void *)reg->addrs;
	u32 ranges = iwl_dump_ini_mem_block_ranges(fwrt, reg_data);
	u32 size = sizeof(struct iwl_fw_ini_error_dump);
	int range;

	if (!ranges)
		return 0;

	for (range = 0; range < ranges; range++)
		size += le32_to_cpu(pairs[range].size);

	return size + ranges * sizeof(struct iwl_fw_ini_error_dump_range);
}

static u32
iwl_dump_ini_paging_get_size(struct iwl_fw_runtime *fwrt,
			     struct iwl_dump_ini_region_data *reg_data)
{
	int i;
	u32 range_header_len = sizeof(struct iwl_fw_ini_error_dump_range);
	u32 size = sizeof(struct iwl_fw_ini_error_dump);

	/* start from 1 to skip CSS section */
	for (i = 1; i <= iwl_dump_ini_paging_ranges(fwrt, reg_data); i++) {
		size += range_header_len;
		if (fwrt->trans->trans_cfg->gen2)
			size += fwrt->trans->init_dram.paging[i].size;
		else
			size += fwrt->fw_paging_db[i].fw_paging_size;
	}

	return size;
}

static u32
iwl_dump_ini_mon_dram_get_size(struct iwl_fw_runtime *fwrt,
			       struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_mon *fw_mon;
	u32 size = 0, alloc_id = le32_to_cpu(reg->dram_alloc_id);
	int i;

	fw_mon = &fwrt->trans->dbg.fw_mon_ini[alloc_id];

	for (i = 0; i < fw_mon->num_frags; i++) {
		struct iwl_dram_data *frag = &fw_mon->frags[i];

		if (!frag->size)
			break;

		size += sizeof(struct iwl_fw_ini_error_dump_range) + frag->size;
	}

	if (size)
		size += sizeof(struct iwl_fw_ini_monitor_dump);

	return size;
}

static u32
iwl_dump_ini_mon_smem_get_size(struct iwl_fw_runtime *fwrt,
			       struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	u32 size;

	size = le32_to_cpu(reg->internal_buffer.size);
	if (!size)
		return 0;

	size += sizeof(struct iwl_fw_ini_monitor_dump) +
		sizeof(struct iwl_fw_ini_error_dump_range);

	return size;
}

static u32 iwl_dump_ini_mon_dbgi_get_size(struct iwl_fw_runtime *fwrt,
					  struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	u32 size = le32_to_cpu(reg->dev_addr.size);
	u32 ranges = iwl_dump_ini_mem_ranges(fwrt, reg_data);

	if (!size || !ranges)
		return 0;

	return sizeof(struct iwl_fw_ini_monitor_dump) + ranges *
		(size + sizeof(struct iwl_fw_ini_error_dump_range));
}

static u32 iwl_dump_ini_txf_get_size(struct iwl_fw_runtime *fwrt,
				     struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_txf_iter_data *iter = &fwrt->dump.txf_iter_data;
	u32 registers_num = iwl_tlv_array_len(reg_data->reg_tlv, reg, addrs);
	u32 size = 0;
	u32 fifo_hdr = sizeof(struct iwl_fw_ini_error_dump_range) +
		       registers_num *
		       sizeof(struct iwl_fw_ini_error_dump_register);

	while (iwl_ini_txf_iter(fwrt, reg_data, size)) {
		size += fifo_hdr;
		if (!reg->fifos.hdr_only)
			size += iter->fifo_size;
	}

	if (!size)
		return 0;

	return size + sizeof(struct iwl_fw_ini_error_dump);
}

static u32 iwl_dump_ini_rxf_get_size(struct iwl_fw_runtime *fwrt,
				     struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_ini_rxf_data rx_data;
	u32 registers_num = iwl_tlv_array_len(reg_data->reg_tlv, reg, addrs);
	u32 size = sizeof(struct iwl_fw_ini_error_dump) +
		sizeof(struct iwl_fw_ini_error_dump_range) +
		registers_num * sizeof(struct iwl_fw_ini_error_dump_register);

	if (reg->fifos.hdr_only)
		return size;

	iwl_ini_get_rxf_data(fwrt, reg_data, &rx_data);
	size += rx_data.size;

	return size;
}

static u32
iwl_dump_ini_err_table_get_size(struct iwl_fw_runtime *fwrt,
				struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	u32 size = le32_to_cpu(reg->err_table.size);

	if (size)
		size += sizeof(struct iwl_fw_ini_err_table_dump) +
			sizeof(struct iwl_fw_ini_error_dump_range);

	return size;
}

static u32
iwl_dump_ini_special_mem_get_size(struct iwl_fw_runtime *fwrt,
				  struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	u32 size = le32_to_cpu(reg->special_mem.size);

	if (size)
		size += sizeof(struct iwl_fw_ini_special_device_memory) +
			sizeof(struct iwl_fw_ini_error_dump_range);

	return size;
}

static u32
iwl_dump_ini_fw_pkt_get_size(struct iwl_fw_runtime *fwrt,
			     struct iwl_dump_ini_region_data *reg_data)
{
	u32 size = 0;

	if (!reg_data->dump_data->fw_pkt)
		return 0;

	size += iwl_rx_packet_payload_len(reg_data->dump_data->fw_pkt);
	if (size)
		size += sizeof(struct iwl_fw_ini_error_dump) +
			sizeof(struct iwl_fw_ini_error_dump_range);

	return size;
}

static u32
iwl_dump_ini_imr_get_size(struct iwl_fw_runtime *fwrt,
			  struct iwl_dump_ini_region_data *reg_data)
{
	u32 ranges = 0;
	u32 imr_enable = fwrt->trans->dbg.imr_data.imr_enable;
	u32 imr_size = fwrt->trans->dbg.imr_data.imr_size;
	u32 sram_size = fwrt->trans->dbg.imr_data.sram_size;

	if (imr_enable == 0 || imr_size == 0 || sram_size == 0) {
		IWL_DEBUG_INFO(fwrt,
			       "WRT: Invalid imr data enable: %d, imr_size: %d, sram_size: %d\n",
			       imr_enable, imr_size, sram_size);
		return 0;
	}
	ranges = iwl_dump_ini_imr_ranges(fwrt, reg_data);
	if (!ranges) {
		IWL_ERR(fwrt, "WRT: ranges :=%d\n", ranges);
		return 0;
	}
	imr_size += sizeof(struct iwl_fw_ini_error_dump) +
		ranges * sizeof(struct iwl_fw_ini_error_dump_range);
	return imr_size;
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
				 struct iwl_dump_ini_region_data *reg_data);
	u32 (*get_size)(struct iwl_fw_runtime *fwrt,
			struct iwl_dump_ini_region_data *reg_data);
	void *(*fill_mem_hdr)(struct iwl_fw_runtime *fwrt,
			      struct iwl_dump_ini_region_data *reg_data,
			      void *data, u32 data_len);
	int (*fill_range)(struct iwl_fw_runtime *fwrt,
			  struct iwl_dump_ini_region_data *reg_data,
			  void *range, u32 range_len, int idx);
};

/**
 * iwl_dump_ini_mem
 *
 * Creates a dump tlv and copy a memory region into it.
 * Returns the size of the current dump tlv or 0 if failed
 *
 * @fwrt: fw runtime struct
 * @list: list to add the dump tlv to
 * @reg_data: memory region
 * @ops: memory dump operations
 */
static u32 iwl_dump_ini_mem(struct iwl_fw_runtime *fwrt, struct list_head *list,
			    struct iwl_dump_ini_region_data *reg_data,
			    const struct iwl_dump_ini_mem_ops *ops)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_dump_entry *entry;
	struct iwl_fw_ini_error_dump_data *tlv;
	struct iwl_fw_ini_error_dump_header *header;
	u32 type = reg->type;
	u32 id = le32_get_bits(reg->id, IWL_FW_INI_REGION_ID_MASK);
	u32 num_of_ranges, i, size;
	u8 *range;
	u32 free_size;
	u64 header_size;
	u32 dump_policy = IWL_FW_INI_DUMP_VERBOSE;

	IWL_DEBUG_FW(fwrt, "WRT: Collecting region: dump type=%d, id=%d, type=%d\n",
		     dump_policy, id, type);

	if (le32_to_cpu(reg->hdr.version) >= 2) {
		u32 dp = le32_get_bits(reg->id,
				       IWL_FW_INI_REGION_DUMP_POLICY_MASK);

		if (dump_policy == IWL_FW_INI_DUMP_VERBOSE &&
		    !(dp & IWL_FW_INI_DEBUG_DUMP_POLICY_NO_LIMIT)) {
			IWL_DEBUG_FW(fwrt,
				     "WRT: no dump - type %d and policy mismatch=%d\n",
				     dump_policy, dp);
			return 0;
		} else if (dump_policy == IWL_FW_INI_DUMP_MEDIUM &&
			   !(dp & IWL_FW_IWL_DEBUG_DUMP_POLICY_MAX_LIMIT_5MB)) {
			IWL_DEBUG_FW(fwrt,
				     "WRT: no dump - type %d and policy mismatch=%d\n",
				     dump_policy, dp);
			return 0;
		} else if (dump_policy == IWL_FW_INI_DUMP_BRIEF &&
			   !(dp & IWL_FW_INI_DEBUG_DUMP_POLICY_MAX_LIMIT_600KB)) {
			IWL_DEBUG_FW(fwrt,
				     "WRT: no dump - type %d and policy mismatch=%d\n",
				     dump_policy, dp);
			return 0;
		}
	}

	if (!ops->get_num_of_ranges || !ops->get_size || !ops->fill_mem_hdr ||
	    !ops->fill_range) {
		IWL_DEBUG_FW(fwrt, "WRT: no ops for collecting data\n");
		return 0;
	}

	size = ops->get_size(fwrt, reg_data);

	if (size < sizeof(*header)) {
		IWL_DEBUG_FW(fwrt, "WRT: size didn't include space for header\n");
		return 0;
	}

	entry = vzalloc(sizeof(*entry) + sizeof(*tlv) + size);
	if (!entry)
		return 0;

	entry->size = sizeof(*tlv) + size;

	tlv = (void *)entry->data;
	tlv->type = reg->type;
	tlv->sub_type = reg->sub_type;
	tlv->sub_type_ver = reg->sub_type_ver;
	tlv->reserved = reg->reserved;
	tlv->len = cpu_to_le32(size);

	num_of_ranges = ops->get_num_of_ranges(fwrt, reg_data);

	header = (void *)tlv->data;
	header->region_id = cpu_to_le32(id);
	header->num_of_ranges = cpu_to_le32(num_of_ranges);
	header->name_len = cpu_to_le32(IWL_FW_INI_MAX_NAME);
	memcpy(header->name, reg->name, IWL_FW_INI_MAX_NAME);

	free_size = size;
	range = ops->fill_mem_hdr(fwrt, reg_data, header, free_size);
	if (!range) {
		IWL_ERR(fwrt,
			"WRT: Failed to fill region header: id=%d, type=%d\n",
			id, type);
		goto out_err;
	}

	header_size = range - (u8 *)header;

	if (WARN(header_size > free_size,
		 "header size %llu > free_size %d",
		 header_size, free_size)) {
		IWL_ERR(fwrt,
			"WRT: fill_mem_hdr used more than given free_size\n");
		goto out_err;
	}

	free_size -= header_size;

	for (i = 0; i < num_of_ranges; i++) {
		int range_size = ops->fill_range(fwrt, reg_data, range,
						 free_size, i);

		if (range_size < 0) {
			IWL_ERR(fwrt,
				"WRT: Failed to dump region: id=%d, type=%d\n",
				id, type);
			goto out_err;
		}

		if (WARN(range_size > free_size, "range_size %d > free_size %d",
			 range_size, free_size)) {
			IWL_ERR(fwrt,
				"WRT: fill_raged used more than given free_size\n");
			goto out_err;
		}

		free_size -= range_size;
		range = range + range_size;
	}

	list_add_tail(&entry->list, list);

	return entry->size;

out_err:
	vfree(entry);

	return 0;
}

static u32 iwl_dump_ini_info(struct iwl_fw_runtime *fwrt,
			     struct iwl_fw_ini_trigger_tlv *trigger,
			     struct list_head *list)
{
	struct iwl_fw_ini_dump_entry *entry;
	struct iwl_fw_error_dump_data *tlv;
	struct iwl_fw_ini_dump_info *dump;
	struct iwl_dbg_tlv_node *node;
	struct iwl_fw_ini_dump_cfg_name *cfg_name;
	u32 size = sizeof(*tlv) + sizeof(*dump);
	u32 num_of_cfg_names = 0;
	u32 hw_type;

	list_for_each_entry(node, &fwrt->trans->dbg.debug_info_tlv_list, list) {
		size += sizeof(*cfg_name);
		num_of_cfg_names++;
	}

	entry = vzalloc(sizeof(*entry) + size);
	if (!entry)
		return 0;

	entry->size = size;

	tlv = (void *)entry->data;
	tlv->type = cpu_to_le32(IWL_INI_DUMP_INFO_TYPE);
	tlv->len = cpu_to_le32(size - sizeof(*tlv));

	dump = (void *)tlv->data;

	dump->version = cpu_to_le32(IWL_INI_DUMP_VER);
	dump->time_point = trigger->time_point;
	dump->trigger_reason = trigger->trigger_reason;
	dump->external_cfg_state =
		cpu_to_le32(fwrt->trans->dbg.external_ini_cfg);

	dump->ver_type = cpu_to_le32(fwrt->dump.fw_ver.type);
	dump->ver_subtype = cpu_to_le32(fwrt->dump.fw_ver.subtype);

	dump->hw_step = cpu_to_le32(fwrt->trans->hw_rev_step);

	/*
	 * Several HWs all have type == 0x42, so we'll override this value
	 * according to the detected HW
	 */
	hw_type = CSR_HW_REV_TYPE(fwrt->trans->hw_rev);
	if (hw_type == IWL_AX210_HW_TYPE) {
		u32 prph_val = iwl_read_umac_prph(fwrt->trans, WFPM_OTP_CFG1_ADDR);
		u32 is_jacket = !!(prph_val & WFPM_OTP_CFG1_IS_JACKET_BIT);
		u32 is_cdb = !!(prph_val & WFPM_OTP_CFG1_IS_CDB_BIT);
		u32 masked_bits = is_jacket | (is_cdb << 1);

		/*
		 * The HW type depends on certain bits in this case, so add
		 * these bits to the HW type. We won't have collisions since we
		 * add these bits after the highest possible bit in the mask.
		 */
		hw_type |= masked_bits << IWL_AX210_HW_TYPE_ADDITION_SHIFT;
	}
	dump->hw_type = cpu_to_le32(hw_type);

	dump->rf_id_flavor =
		cpu_to_le32(CSR_HW_RFID_FLAVOR(fwrt->trans->hw_rf_id));
	dump->rf_id_dash = cpu_to_le32(CSR_HW_RFID_DASH(fwrt->trans->hw_rf_id));
	dump->rf_id_step = cpu_to_le32(CSR_HW_RFID_STEP(fwrt->trans->hw_rf_id));
	dump->rf_id_type = cpu_to_le32(CSR_HW_RFID_TYPE(fwrt->trans->hw_rf_id));

	dump->lmac_major = cpu_to_le32(fwrt->dump.fw_ver.lmac_major);
	dump->lmac_minor = cpu_to_le32(fwrt->dump.fw_ver.lmac_minor);
	dump->umac_major = cpu_to_le32(fwrt->dump.fw_ver.umac_major);
	dump->umac_minor = cpu_to_le32(fwrt->dump.fw_ver.umac_minor);

	dump->fw_mon_mode = cpu_to_le32(fwrt->trans->dbg.ini_dest);
	dump->regions_mask = trigger->regions_mask &
			     ~cpu_to_le64(fwrt->trans->dbg.unsupported_region_msk);

	dump->build_tag_len = cpu_to_le32(sizeof(dump->build_tag));
	memcpy(dump->build_tag, fwrt->fw->human_readable,
	       sizeof(dump->build_tag));

	cfg_name = dump->cfg_names;
	dump->num_of_cfg_names = cpu_to_le32(num_of_cfg_names);
	list_for_each_entry(node, &fwrt->trans->dbg.debug_info_tlv_list, list) {
		struct iwl_fw_ini_debug_info_tlv *debug_info =
			(void *)node->tlv.data;

		cfg_name->image_type = debug_info->image_type;
		cfg_name->cfg_name_len =
			cpu_to_le32(IWL_FW_INI_MAX_CFG_NAME);
		memcpy(cfg_name->cfg_name, debug_info->debug_cfg_name,
		       sizeof(cfg_name->cfg_name));
		cfg_name++;
	}

	/* add dump info TLV to the beginning of the list since it needs to be
	 * the first TLV in the dump
	 */
	list_add(&entry->list, list);

	return entry->size;
}

static u32 iwl_dump_ini_file_name_info(struct iwl_fw_runtime *fwrt,
				       struct list_head *list)
{
	struct iwl_fw_ini_dump_entry *entry;
	struct iwl_dump_file_name_info *tlv;
	u32 len = strnlen(fwrt->trans->dbg.dump_file_name_ext,
			  IWL_FW_INI_MAX_NAME);

	if (!fwrt->trans->dbg.dump_file_name_ext_valid)
		return 0;

	entry = vzalloc(sizeof(*entry) + sizeof(*tlv) + len);
	if (!entry)
		return 0;

	entry->size = sizeof(*tlv) + len;

	tlv = (void *)entry->data;
	tlv->type = cpu_to_le32(IWL_INI_DUMP_NAME_TYPE);
	tlv->len = cpu_to_le32(len);
	memcpy(tlv->data, fwrt->trans->dbg.dump_file_name_ext, len);

	/* add the dump file name extension tlv to the list */
	list_add_tail(&entry->list, list);

	fwrt->trans->dbg.dump_file_name_ext_valid = false;

	return entry->size;
}

static const struct iwl_dump_ini_mem_ops iwl_dump_ini_region_ops[] = {
	[IWL_FW_INI_REGION_INVALID] = {},
	[IWL_FW_INI_REGION_INTERNAL_BUFFER] = {
		.get_num_of_ranges = iwl_dump_ini_single_range,
		.get_size = iwl_dump_ini_mon_smem_get_size,
		.fill_mem_hdr = iwl_dump_ini_mon_smem_fill_header,
		.fill_range = iwl_dump_ini_mon_smem_iter,
	},
	[IWL_FW_INI_REGION_DRAM_BUFFER] = {
		.get_num_of_ranges = iwl_dump_ini_mon_dram_ranges,
		.get_size = iwl_dump_ini_mon_dram_get_size,
		.fill_mem_hdr = iwl_dump_ini_mon_dram_fill_header,
		.fill_range = iwl_dump_ini_mon_dram_iter,
	},
	[IWL_FW_INI_REGION_TXF] = {
		.get_num_of_ranges = iwl_dump_ini_txf_ranges,
		.get_size = iwl_dump_ini_txf_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_txf_iter,
	},
	[IWL_FW_INI_REGION_RXF] = {
		.get_num_of_ranges = iwl_dump_ini_single_range,
		.get_size = iwl_dump_ini_rxf_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_rxf_iter,
	},
	[IWL_FW_INI_REGION_LMAC_ERROR_TABLE] = {
		.get_num_of_ranges = iwl_dump_ini_single_range,
		.get_size = iwl_dump_ini_err_table_get_size,
		.fill_mem_hdr = iwl_dump_ini_err_table_fill_header,
		.fill_range = iwl_dump_ini_err_table_iter,
	},
	[IWL_FW_INI_REGION_UMAC_ERROR_TABLE] = {
		.get_num_of_ranges = iwl_dump_ini_single_range,
		.get_size = iwl_dump_ini_err_table_get_size,
		.fill_mem_hdr = iwl_dump_ini_err_table_fill_header,
		.fill_range = iwl_dump_ini_err_table_iter,
	},
	[IWL_FW_INI_REGION_RSP_OR_NOTIF] = {
		.get_num_of_ranges = iwl_dump_ini_single_range,
		.get_size = iwl_dump_ini_fw_pkt_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_fw_pkt_iter,
	},
	[IWL_FW_INI_REGION_DEVICE_MEMORY] = {
		.get_num_of_ranges = iwl_dump_ini_mem_ranges,
		.get_size = iwl_dump_ini_mem_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_dev_mem_iter,
	},
	[IWL_FW_INI_REGION_PERIPHERY_MAC] = {
		.get_num_of_ranges = iwl_dump_ini_mem_ranges,
		.get_size = iwl_dump_ini_mem_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_prph_mac_iter,
	},
	[IWL_FW_INI_REGION_PERIPHERY_PHY] = {
		.get_num_of_ranges = iwl_dump_ini_mem_ranges,
		.get_size = iwl_dump_ini_mem_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_prph_phy_iter,
	},
	[IWL_FW_INI_REGION_PERIPHERY_MAC_RANGE] = {
		.get_num_of_ranges = iwl_dump_ini_mem_block_ranges,
		.get_size = iwl_dump_ini_mem_block_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_prph_mac_block_iter,
	},
	[IWL_FW_INI_REGION_PERIPHERY_PHY_RANGE] = {
		.get_num_of_ranges = iwl_dump_ini_mem_block_ranges,
		.get_size = iwl_dump_ini_mem_block_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_prph_phy_block_iter,
	},
	[IWL_FW_INI_REGION_PERIPHERY_AUX] = {},
	[IWL_FW_INI_REGION_PAGING] = {
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.get_num_of_ranges = iwl_dump_ini_paging_ranges,
		.get_size = iwl_dump_ini_paging_get_size,
		.fill_range = iwl_dump_ini_paging_iter,
	},
	[IWL_FW_INI_REGION_CSR] = {
		.get_num_of_ranges = iwl_dump_ini_mem_ranges,
		.get_size = iwl_dump_ini_mem_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_csr_iter,
	},
	[IWL_FW_INI_REGION_DRAM_IMR] = {
		.get_num_of_ranges = iwl_dump_ini_imr_ranges,
		.get_size = iwl_dump_ini_imr_get_size,
		.fill_mem_hdr = iwl_dump_ini_imr_fill_header,
		.fill_range = iwl_dump_ini_imr_iter,
	},
	[IWL_FW_INI_REGION_PCI_IOSF_CONFIG] = {
		.get_num_of_ranges = iwl_dump_ini_mem_ranges,
		.get_size = iwl_dump_ini_mem_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_config_iter,
	},
	[IWL_FW_INI_REGION_SPECIAL_DEVICE_MEMORY] = {
		.get_num_of_ranges = iwl_dump_ini_single_range,
		.get_size = iwl_dump_ini_special_mem_get_size,
		.fill_mem_hdr = iwl_dump_ini_special_mem_fill_header,
		.fill_range = iwl_dump_ini_special_mem_iter,
	},
	[IWL_FW_INI_REGION_DBGI_SRAM] = {
		.get_num_of_ranges = iwl_dump_ini_mem_ranges,
		.get_size = iwl_dump_ini_mon_dbgi_get_size,
		.fill_mem_hdr = iwl_dump_ini_mon_dbgi_fill_header,
		.fill_range = iwl_dump_ini_dbgi_sram_iter,
	},
	[IWL_FW_INI_REGION_PERIPHERY_SNPS_DPHYIP] = {
		.get_num_of_ranges = iwl_dump_ini_mem_ranges,
		.get_size = iwl_dump_ini_mem_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_prph_snps_dphyip_iter,
	},
};

static u32 iwl_dump_ini_trigger(struct iwl_fw_runtime *fwrt,
				struct iwl_fwrt_dump_data *dump_data,
				struct list_head *list)
{
	struct iwl_fw_ini_trigger_tlv *trigger = dump_data->trig;
	enum iwl_fw_ini_time_point tp_id = le32_to_cpu(trigger->time_point);
	struct iwl_dump_ini_region_data reg_data = {
		.dump_data = dump_data,
	};
	struct iwl_dump_ini_region_data imr_reg_data = {
		.dump_data = dump_data,
	};
	int i;
	u32 size = 0;
	u64 regions_mask = le64_to_cpu(trigger->regions_mask) &
			   ~(fwrt->trans->dbg.unsupported_region_msk);

	BUILD_BUG_ON(sizeof(trigger->regions_mask) != sizeof(regions_mask));
	BUILD_BUG_ON((sizeof(trigger->regions_mask) * BITS_PER_BYTE) <
		     ARRAY_SIZE(fwrt->trans->dbg.active_regions));

	for (i = 0; i < ARRAY_SIZE(fwrt->trans->dbg.active_regions); i++) {
		u32 reg_type;
		struct iwl_fw_ini_region_tlv *reg;

		if (!(BIT_ULL(i) & regions_mask))
			continue;

		reg_data.reg_tlv = fwrt->trans->dbg.active_regions[i];
		if (!reg_data.reg_tlv) {
			IWL_WARN(fwrt,
				 "WRT: Unassigned region id %d, skipping\n", i);
			continue;
		}

		reg = (void *)reg_data.reg_tlv->data;
		reg_type = reg->type;
		if (reg_type >= ARRAY_SIZE(iwl_dump_ini_region_ops))
			continue;

		if ((reg_type == IWL_FW_INI_REGION_PERIPHERY_PHY ||
		     reg_type == IWL_FW_INI_REGION_PERIPHERY_PHY_RANGE ||
		     reg_type == IWL_FW_INI_REGION_PERIPHERY_SNPS_DPHYIP) &&
		    tp_id != IWL_FW_INI_TIME_POINT_FW_ASSERT) {
			IWL_WARN(fwrt,
				 "WRT: trying to collect phy prph at time point: %d, skipping\n",
				 tp_id);
			continue;
		}
		/*
		 * DRAM_IMR can be collected only for FW/HW error timepoint
		 * when fw is not alive. In addition, it must be collected
		 * lastly as it overwrites SRAM that can possibly contain
		 * debug data which also need to be collected.
		 */
		if (reg_type == IWL_FW_INI_REGION_DRAM_IMR) {
			if (tp_id == IWL_FW_INI_TIME_POINT_FW_ASSERT ||
			    tp_id == IWL_FW_INI_TIME_POINT_FW_HW_ERROR)
				imr_reg_data.reg_tlv = fwrt->trans->dbg.active_regions[i];
			else
				IWL_INFO(fwrt,
					 "WRT: trying to collect DRAM_IMR at time point: %d, skipping\n",
					 tp_id);
		/* continue to next region */
			continue;
		}


		size += iwl_dump_ini_mem(fwrt, list, &reg_data,
					 &iwl_dump_ini_region_ops[reg_type]);
	}
	/* collect DRAM_IMR region in the last */
	if (imr_reg_data.reg_tlv)
		size += iwl_dump_ini_mem(fwrt, list, &reg_data,
					 &iwl_dump_ini_region_ops[IWL_FW_INI_REGION_DRAM_IMR]);

	if (size) {
		size += iwl_dump_ini_file_name_info(fwrt, list);
		size += iwl_dump_ini_info(fwrt, trigger, list);
	}

	return size;
}

static bool iwl_fw_ini_trigger_on(struct iwl_fw_runtime *fwrt,
				  struct iwl_fw_ini_trigger_tlv *trig)
{
	enum iwl_fw_ini_time_point tp_id = le32_to_cpu(trig->time_point);
	u32 usec = le32_to_cpu(trig->ignore_consec);

	if (!iwl_trans_dbg_ini_valid(fwrt->trans) ||
	    tp_id == IWL_FW_INI_TIME_POINT_INVALID ||
	    tp_id >= IWL_FW_INI_TIME_POINT_NUM ||
	    iwl_fw_dbg_no_trig_window(fwrt, tp_id, usec))
		return false;

	return true;
}

static u32 iwl_dump_ini_file_gen(struct iwl_fw_runtime *fwrt,
				 struct iwl_fwrt_dump_data *dump_data,
				 struct list_head *list)
{
	struct iwl_fw_ini_trigger_tlv *trigger = dump_data->trig;
	struct iwl_fw_ini_dump_entry *entry;
	struct iwl_fw_ini_dump_file_hdr *hdr;
	u32 size;

	if (!trigger || !iwl_fw_ini_trigger_on(fwrt, trigger) ||
	    !le64_to_cpu(trigger->regions_mask))
		return 0;

	entry = vzalloc(sizeof(*entry) + sizeof(*hdr));
	if (!entry)
		return 0;

	entry->size = sizeof(*hdr);

	size = iwl_dump_ini_trigger(fwrt, dump_data, list);
	if (!size) {
		vfree(entry);
		return 0;
	}

	hdr = (void *)entry->data;
	hdr->barker = cpu_to_le32(IWL_FW_INI_ERROR_DUMP_BARKER);
	hdr->file_len = cpu_to_le32(size + entry->size);

	list_add(&entry->list, list);

	return le32_to_cpu(hdr->file_len);
}

static inline void iwl_fw_free_dump_desc(struct iwl_fw_runtime *fwrt,
					 const struct iwl_fw_dump_desc *desc)
{
	if (desc && desc != &iwl_dump_desc_assert)
		kfree(desc);

	fwrt->dump.lmac_err_id[0] = 0;
	if (fwrt->smem_cfg.num_lmacs > 1)
		fwrt->dump.lmac_err_id[1] = 0;
	fwrt->dump.umac_err_id = 0;
}

static void iwl_fw_error_dump(struct iwl_fw_runtime *fwrt,
			      struct iwl_fwrt_dump_data *dump_data)
{
	struct iwl_fw_dump_ptrs fw_error_dump = {};
	struct iwl_fw_error_dump_file *dump_file;
	struct scatterlist *sg_dump_data;
	u32 file_len;
	u32 dump_mask = fwrt->fw->dbg.dump_mask;

	dump_file = iwl_fw_error_dump_file(fwrt, &fw_error_dump, dump_data);
	if (!dump_file)
		return;

	if (dump_data->monitor_only)
		dump_mask &= BIT(IWL_FW_ERROR_DUMP_FW_MONITOR);

	fw_error_dump.trans_ptr = iwl_trans_dump_data(fwrt->trans, dump_mask,
						      fwrt->sanitize_ops,
						      fwrt->sanitize_ctx);
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
}

static void iwl_dump_ini_list_free(struct list_head *list)
{
	while (!list_empty(list)) {
		struct iwl_fw_ini_dump_entry *entry =
			list_entry(list->next, typeof(*entry), list);

		list_del(&entry->list);
		vfree(entry);
	}
}

static void iwl_fw_error_dump_data_free(struct iwl_fwrt_dump_data *dump_data)
{
	dump_data->trig = NULL;
	kfree(dump_data->fw_pkt);
	dump_data->fw_pkt = NULL;
}

static void iwl_fw_error_ini_dump(struct iwl_fw_runtime *fwrt,
				  struct iwl_fwrt_dump_data *dump_data)
{
	LIST_HEAD(dump_list);
	struct scatterlist *sg_dump_data;
	u32 file_len = iwl_dump_ini_file_gen(fwrt, dump_data, &dump_list);

	if (!file_len)
		return;

	sg_dump_data = alloc_sgtable(file_len);
	if (sg_dump_data) {
		struct iwl_fw_ini_dump_entry *entry;
		int sg_entries = sg_nents(sg_dump_data);
		u32 offs = 0;

		list_for_each_entry(entry, &dump_list, list) {
			sg_pcopy_from_buffer(sg_dump_data, sg_entries,
					     entry->data, entry->size, offs);
			offs += entry->size;
		}
		dev_coredumpsg(fwrt->trans->dev, sg_dump_data, file_len,
			       GFP_KERNEL);
	}
	iwl_dump_ini_list_free(&dump_list);
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
	struct iwl_fwrt_wk_data *wk_data;
	unsigned long idx;

	if (iwl_trans_dbg_ini_valid(fwrt->trans)) {
		iwl_fw_free_dump_desc(fwrt, desc);
		return 0;
	}

	/*
	 * Check there is an available worker.
	 * ffz return value is undefined if no zero exists,
	 * so check against ~0UL first.
	 */
	if (fwrt->dump.active_wks == ~0UL)
		return -EBUSY;

	idx = ffz(fwrt->dump.active_wks);

	if (idx >= IWL_FW_RUNTIME_DUMP_WK_NUM ||
	    test_and_set_bit(fwrt->dump.wks[idx].idx, &fwrt->dump.active_wks))
		return -EBUSY;

	wk_data = &fwrt->dump.wks[idx];

	if (WARN_ON(wk_data->dump_data.desc))
		iwl_fw_free_dump_desc(fwrt, wk_data->dump_data.desc);

	wk_data->dump_data.desc = desc;
	wk_data->dump_data.monitor_only = monitor_only;

	IWL_WARN(fwrt, "Collecting data: trigger %d fired.\n",
		 le32_to_cpu(desc->trig_desc.type));

	schedule_delayed_work(&wk_data->wk, usecs_to_jiffies(delay));

	return 0;
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_collect_desc);

int iwl_fw_dbg_error_collect(struct iwl_fw_runtime *fwrt,
			     enum iwl_fw_dbg_trigger trig_type)
{
	if (!test_bit(STATUS_DEVICE_ENABLED, &fwrt->trans->status))
		return -EIO;

	if (iwl_trans_dbg_ini_valid(fwrt->trans)) {
		if (trig_type != FW_DBG_TRIGGER_ALIVE_TIMEOUT &&
		    trig_type != FW_DBG_TRIGGER_DRIVER)
			return -EIO;

		iwl_dbg_tlv_time_point(fwrt,
				       IWL_FW_INI_TIME_POINT_HOST_ALIVE_TIMEOUT,
				       NULL);
	} else {
		struct iwl_fw_dump_desc *iwl_dump_error_desc;
		int ret;

		iwl_dump_error_desc =
			kmalloc(sizeof(*iwl_dump_error_desc), GFP_KERNEL);

		if (!iwl_dump_error_desc)
			return -ENOMEM;

		iwl_dump_error_desc->trig_desc.type = cpu_to_le32(trig_type);
		iwl_dump_error_desc->len = 0;

		ret = iwl_fw_dbg_collect_desc(fwrt, iwl_dump_error_desc,
					      false, 0);
		if (ret) {
			kfree(iwl_dump_error_desc);
			return ret;
		}
	}

	iwl_trans_sync_nmi(fwrt->trans);

	return 0;
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

	desc = kzalloc(struct_size(desc, trig_desc.data, len), GFP_ATOMIC);
	if (!desc)
		return -ENOMEM;


	desc->len = len;
	desc->trig_desc.type = cpu_to_le32(trig);
	memcpy(desc->trig_desc.data, str, len);

	return iwl_fw_dbg_collect_desc(fwrt, desc, monitor_only, delay);
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_collect);

int iwl_fw_dbg_collect_trig(struct iwl_fw_runtime *fwrt,
			    struct iwl_fw_dbg_trigger_tlv *trigger,
			    const char *fmt, ...)
{
	int ret, len = 0;
	char buf[64];

	if (iwl_trans_dbg_ini_valid(fwrt->trans))
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
		IWL_INFO(fwrt, "FW already configured (%d) - re-configuring\n",
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

void iwl_send_dbg_dump_complete_cmd(struct iwl_fw_runtime *fwrt,
				    u32 timepoint,
				    u32 timepoint_data)
{
	struct iwl_dbg_dump_complete_cmd hcmd_data;
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(DEBUG_GROUP, FW_DUMP_COMPLETE_CMD),
		.data[0] = &hcmd_data,
		.len[0] = sizeof(hcmd_data),
	};

	if (test_bit(STATUS_FW_ERROR, &fwrt->trans->status))
		return;

	if (fw_has_capa(&fwrt->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_DUMP_COMPLETE_SUPPORT)) {
		hcmd_data.tp = cpu_to_le32(timepoint);
		hcmd_data.tp_data = cpu_to_le32(timepoint_data);
		iwl_trans_send_cmd(fwrt->trans, &hcmd);
	}
}

/* this function assumes dump_start was called beforehand and dump_end will be
 * called afterwards
 */
static void iwl_fw_dbg_collect_sync(struct iwl_fw_runtime *fwrt, u8 wk_idx)
{
	struct iwl_fw_dbg_params params = {0};
	struct iwl_fwrt_dump_data *dump_data =
		&fwrt->dump.wks[wk_idx].dump_data;
	u32 policy;
	u32 time_point;
	if (!test_bit(wk_idx, &fwrt->dump.active_wks))
		return;

	if (!dump_data->trig) {
		IWL_ERR(fwrt, "dump trigger data is not set\n");
		goto out;
	}

	if (!test_bit(STATUS_DEVICE_ENABLED, &fwrt->trans->status)) {
		IWL_ERR(fwrt, "Device is not enabled - cannot dump error\n");
		goto out;
	}

	/* there's no point in fw dump if the bus is dead */
	if (test_bit(STATUS_TRANS_DEAD, &fwrt->trans->status)) {
		IWL_ERR(fwrt, "Skip fw error dump since bus is dead\n");
		goto out;
	}

	iwl_fw_dbg_stop_restart_recording(fwrt, &params, true);

	IWL_DEBUG_FW_INFO(fwrt, "WRT: Data collection start\n");
	if (iwl_trans_dbg_ini_valid(fwrt->trans))
		iwl_fw_error_ini_dump(fwrt, &fwrt->dump.wks[wk_idx].dump_data);
	else
		iwl_fw_error_dump(fwrt, &fwrt->dump.wks[wk_idx].dump_data);
	IWL_DEBUG_FW_INFO(fwrt, "WRT: Data collection done\n");

	iwl_fw_dbg_stop_restart_recording(fwrt, &params, false);

	policy = le32_to_cpu(dump_data->trig->apply_policy);
	time_point = le32_to_cpu(dump_data->trig->time_point);

	if (policy & IWL_FW_INI_APPLY_POLICY_DUMP_COMPLETE_CMD) {
		IWL_DEBUG_FW_INFO(fwrt, "WRT: sending dump complete\n");
		iwl_send_dbg_dump_complete_cmd(fwrt, time_point, 0);
	}
	if (fwrt->trans->dbg.last_tp_resetfw == IWL_FW_INI_RESET_FW_MODE_STOP_FW_ONLY)
		iwl_force_nmi(fwrt->trans);

out:
	if (iwl_trans_dbg_ini_valid(fwrt->trans)) {
		iwl_fw_error_dump_data_free(dump_data);
	} else {
		iwl_fw_free_dump_desc(fwrt, dump_data->desc);
		dump_data->desc = NULL;
	}

	clear_bit(wk_idx, &fwrt->dump.active_wks);
}

int iwl_fw_dbg_ini_collect(struct iwl_fw_runtime *fwrt,
			   struct iwl_fwrt_dump_data *dump_data,
			   bool sync)
{
	struct iwl_fw_ini_trigger_tlv *trig = dump_data->trig;
	enum iwl_fw_ini_time_point tp_id = le32_to_cpu(trig->time_point);
	u32 occur, delay;
	unsigned long idx;

	if (!iwl_fw_ini_trigger_on(fwrt, trig)) {
		IWL_WARN(fwrt, "WRT: Trigger %d is not active, aborting dump\n",
			 tp_id);
		return -EINVAL;
	}

	delay = le32_to_cpu(trig->dump_delay);
	occur = le32_to_cpu(trig->occurrences);
	if (!occur)
		return 0;

	trig->occurrences = cpu_to_le32(--occur);

	/* Check there is an available worker.
	 * ffz return value is undefined if no zero exists,
	 * so check against ~0UL first.
	 */
	if (fwrt->dump.active_wks == ~0UL)
		return -EBUSY;

	idx = ffz(fwrt->dump.active_wks);

	if (idx >= IWL_FW_RUNTIME_DUMP_WK_NUM ||
	    test_and_set_bit(fwrt->dump.wks[idx].idx, &fwrt->dump.active_wks))
		return -EBUSY;

	fwrt->dump.wks[idx].dump_data = *dump_data;

	if (sync)
		delay = 0;

	IWL_WARN(fwrt,
		 "WRT: Collecting data: ini trigger %d fired (delay=%dms).\n",
		 tp_id, (u32)(delay / USEC_PER_MSEC));

	if (sync)
		iwl_fw_dbg_collect_sync(fwrt, idx);
	else
		schedule_delayed_work(&fwrt->dump.wks[idx].wk, usecs_to_jiffies(delay));

	return 0;
}

void iwl_fw_error_dump_wk(struct work_struct *work)
{
	struct iwl_fwrt_wk_data *wks =
		container_of(work, typeof(*wks), wk.work);
	struct iwl_fw_runtime *fwrt =
		container_of(wks, typeof(*fwrt), dump.wks[wks->idx]);

	/* assumes the op mode mutex is locked in dump_start since
	 * iwl_fw_dbg_collect_sync can't run in parallel
	 */
	if (fwrt->ops && fwrt->ops->dump_start)
		fwrt->ops->dump_start(fwrt->ops_ctx);

	iwl_fw_dbg_collect_sync(fwrt, wks->idx);

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

	if (fwrt->sanitize_ops && fwrt->sanitize_ops->frob_mem)
		fwrt->sanitize_ops->frob_mem(fwrt->sanitize_ctx,
					     cfg->d3_debug_data_base_addr,
					     fwrt->dump.d3_debug_data,
					     cfg->d3_debug_data_length);
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_read_d3_debug_data);

void iwl_fw_dbg_stop_sync(struct iwl_fw_runtime *fwrt)
{
	int i;

	iwl_dbg_tlv_del_timers(fwrt->trans);
	for (i = 0; i < IWL_FW_RUNTIME_DUMP_WK_NUM; i++)
		iwl_fw_dbg_collect_sync(fwrt, i);

	iwl_fw_dbg_stop_restart_recording(fwrt, NULL, true);
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_stop_sync);

static int iwl_fw_dbg_suspend_resume_hcmd(struct iwl_trans *trans, bool suspend)
{
	struct iwl_dbg_suspend_resume_cmd cmd = {
		.operation = suspend ?
			cpu_to_le32(DBGC_SUSPEND_CMD) :
			cpu_to_le32(DBGC_RESUME_CMD),
	};
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(DEBUG_GROUP, DBGC_SUSPEND_RESUME),
		.data[0] = &cmd,
		.len[0] = sizeof(cmd),
	};

	return iwl_trans_send_cmd(trans, &hcmd);
}

static void iwl_fw_dbg_stop_recording(struct iwl_trans *trans,
				      struct iwl_fw_dbg_params *params)
{
	if (trans->trans_cfg->device_family == IWL_DEVICE_FAMILY_7000) {
		iwl_set_bits_prph(trans, MON_BUFF_SAMPLE_CTL, 0x100);
		return;
	}

	if (params) {
		params->in_sample = iwl_read_umac_prph(trans, DBGC_IN_SAMPLE);
		params->out_ctrl = iwl_read_umac_prph(trans, DBGC_OUT_CTRL);
	}

	iwl_write_umac_prph(trans, DBGC_IN_SAMPLE, 0);
	/* wait for the DBGC to finish writing the internal buffer to DRAM to
	 * avoid halting the HW while writing
	 */
	usleep_range(700, 1000);
	iwl_write_umac_prph(trans, DBGC_OUT_CTRL, 0);
}

static int iwl_fw_dbg_restart_recording(struct iwl_trans *trans,
					struct iwl_fw_dbg_params *params)
{
	if (!params)
		return -EIO;

	if (trans->trans_cfg->device_family == IWL_DEVICE_FAMILY_7000) {
		iwl_clear_bits_prph(trans, MON_BUFF_SAMPLE_CTL, 0x100);
		iwl_clear_bits_prph(trans, MON_BUFF_SAMPLE_CTL, 0x1);
		iwl_set_bits_prph(trans, MON_BUFF_SAMPLE_CTL, 0x1);
	} else {
		iwl_write_umac_prph(trans, DBGC_IN_SAMPLE, params->in_sample);
		iwl_write_umac_prph(trans, DBGC_OUT_CTRL, params->out_ctrl);
	}

	return 0;
}

int iwl_fw_send_timestamp_marker_cmd(struct iwl_fw_runtime *fwrt)
{
	struct iwl_mvm_marker marker = {
		.dw_len = sizeof(struct iwl_mvm_marker) / 4,
		.marker_id = MARKER_ID_SYNC_CLOCK,
	};
	struct iwl_host_cmd hcmd = {
		.flags = CMD_ASYNC,
		.id = WIDE_ID(LONG_GROUP, MARKER_CMD),
		.dataflags = {},
	};
	struct iwl_mvm_marker_rsp *resp;
	int cmd_ver = iwl_fw_lookup_cmd_ver(fwrt->fw,
					    WIDE_ID(LONG_GROUP, MARKER_CMD),
					    IWL_FW_CMD_VER_UNKNOWN);
	int ret;

	if (cmd_ver == 1) {
		/* the real timestamp is taken from the ftrace clock
		 * this is for finding the match between fw and kernel logs
		 */
		marker.timestamp = cpu_to_le64(fwrt->timestamp.seq++);
	} else if (cmd_ver == 2) {
		marker.timestamp = cpu_to_le64(ktime_get_boottime_ns());
	} else {
		IWL_DEBUG_INFO(fwrt,
			       "Invalid version of Marker CMD. Ver = %d\n",
			       cmd_ver);
		return -EINVAL;
	}

	hcmd.data[0] = &marker;
	hcmd.len[0] = sizeof(marker);

	ret = iwl_trans_send_cmd(fwrt->trans, &hcmd);

	if (cmd_ver > 1 && hcmd.resp_pkt) {
		resp = (void *)hcmd.resp_pkt->data;
		IWL_DEBUG_INFO(fwrt, "FW GP2 time: %u\n",
			       le32_to_cpu(resp->gp2));
	}

	return ret;
}

void iwl_fw_dbg_stop_restart_recording(struct iwl_fw_runtime *fwrt,
				       struct iwl_fw_dbg_params *params,
				       bool stop)
{
	int ret __maybe_unused = 0;

	if (test_bit(STATUS_FW_ERROR, &fwrt->trans->status))
		return;

	if (fw_has_capa(&fwrt->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_DBG_SUSPEND_RESUME_CMD_SUPP)) {
		if (stop)
			iwl_fw_send_timestamp_marker_cmd(fwrt);
		ret = iwl_fw_dbg_suspend_resume_hcmd(fwrt->trans, stop);
	} else if (stop) {
		iwl_fw_dbg_stop_recording(fwrt->trans, params);
	} else {
		ret = iwl_fw_dbg_restart_recording(fwrt->trans, params);
	}
#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (!ret) {
		if (stop)
			fwrt->trans->dbg.rec_on = false;
		else
			iwl_fw_set_dbg_rec_on(fwrt);
	}
#endif
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_stop_restart_recording);

void iwl_fw_disable_dbg_asserts(struct iwl_fw_runtime *fwrt)
{
	struct iwl_fw_dbg_config_cmd cmd = {
		.type = cpu_to_le32(DEBUG_TOKEN_CONFIG_TYPE),
		.conf = cpu_to_le32(IWL_FW_DBG_CONFIG_TOKEN),
	};
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(LONG_GROUP, LDBG_CONFIG_CMD),
		.data[0] = &cmd,
		.len[0] = sizeof(cmd),
	};
	u32 preset = u32_get_bits(fwrt->trans->dbg.domains_bitmap,
				  GENMASK(31, IWL_FW_DBG_DOMAIN_POS + 1));

	/* supported starting from 9000 devices */
	if (fwrt->trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_9000)
		return;

	if (fwrt->trans->dbg.yoyo_bin_loaded || (preset && preset != 1))
		return;

	iwl_trans_send_cmd(fwrt->trans, &hcmd);
}
IWL_EXPORT_SYMBOL(iwl_fw_disable_dbg_asserts);

void iwl_fw_dbg_clear_monitor_buf(struct iwl_fw_runtime *fwrt)
{
	struct iwl_fw_dbg_params params = {0};

	iwl_fw_dbg_stop_sync(fwrt);

	if (fw_has_api(&fwrt->fw->ucode_capa,
		       IWL_UCODE_TLV_API_INT_DBG_BUF_CLEAR)) {
		struct iwl_host_cmd hcmd = {
			.id = WIDE_ID(DEBUG_GROUP, FW_CLEAR_BUFFER),
		};
		iwl_trans_send_cmd(fwrt->trans, &hcmd);
	}

	iwl_dbg_tlv_init_cfg(fwrt);
	iwl_fw_dbg_stop_restart_recording(fwrt, &params, false);
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_clear_monitor_buf);

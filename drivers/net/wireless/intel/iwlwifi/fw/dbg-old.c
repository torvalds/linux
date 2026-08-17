// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2005-2014, 2018-2026 Intel Corporation
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
		iwl_trans_write_prph(fwrt->trans, RSP_RADIO_CMD, rd_cmd);
		*pos = (u8)iwl_trans_read_prph(fwrt->trans, RSP_RADIO_RDDAT);

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
				  fwrt->trans->mac_cfg->umac_prph_offset, 1);
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
		*data++ = cpu_to_le32(iwl_trans_read_prph(trans, start + i));
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

	if (fwrt->trans->mac_cfg->device_family >= IWL_DEVICE_FAMILY_AX210) {
		range_len = ARRAY_SIZE(iwl_prph_dump_addr_ax210);
		handler(fwrt, iwl_prph_dump_addr_ax210, range_len, ptr);
	} else if (fwrt->trans->mac_cfg->device_family >=
		   IWL_DEVICE_FAMILY_22000) {
		range_len = ARRAY_SIZE(iwl_prph_dump_addr_22000);
		handler(fwrt, iwl_prph_dump_addr_22000, range_len, ptr);
	} else {
		range_len = ARRAY_SIZE(iwl_prph_dump_addr_comm);
		handler(fwrt, iwl_prph_dump_addr_comm, range_len, ptr);

		if (fwrt->trans->mac_cfg->mq_rx_supported) {
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
	u32 smem_len = fwrt->fw->dbg.n_mem_tlv ? 0 : fwrt->trans->mac_cfg->base->smem_len;
	u32 sram2_len = fwrt->fw->dbg.n_mem_tlv ?
				0 : fwrt->trans->cfg->dccm2_len;
	int i;

	/* SRAM - include stack CCM if driver knows the values for it */
	if (!fwrt->trans->cfg->dccm_offset ||
	    !fwrt->trans->cfg->dccm_len) {
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
	if (iwl_trans_is_fw_error(fwrt->trans)) {
		fifo_len = iwl_fw_rxf_len(fwrt, mem_cfg);
		fifo_len += iwl_fw_txf_len(fwrt, mem_cfg);

		/* Make room for PRPH registers */
		if (iwl_fw_dbg_type_on(fwrt, IWL_FW_ERROR_DUMP_PRPH))
			iwl_fw_prph_handler(fwrt, &prph_len,
					    iwl_fw_get_prph_len);

		if (fwrt->trans->mac_cfg->device_family ==
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
			fwrt->trans->mac_cfg->base->d3_debug_data_length * 2;
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
			cpu_to_le32(CSR_HW_REV_TYPE(fwrt->trans->info.hw_rev));
		dump_info->hw_step =
			cpu_to_le32(fwrt->trans->info.hw_rev_step);
		memcpy(dump_info->fw_human_readable, fwrt->fw->human_readable,
		       sizeof(dump_info->fw_human_readable));
		strscpy_pad(dump_info->dev_human_readable,
			    fwrt->trans->info.name,
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

	/* In case we only want monitor dump, skip to dump transport data */
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
				fwrt->trans->mac_cfg->base->smem_offset,
				IWL_FW_ERROR_DUMP_MEM_SMEM);

		iwl_fw_dump_mem(fwrt, &dump_data, sram2_len,
				fwrt->trans->cfg->dccm2_offset,
				IWL_FW_ERROR_DUMP_MEM_SRAM);
	}

	if (iwl_fw_dbg_is_d3_debug_enabled(fwrt) && fwrt->dump.d3_debug_data) {
		u32 addr = fwrt->trans->mac_cfg->base->d3_debug_data_base_addr;
		size_t data_size = fwrt->trans->mac_cfg->base->d3_debug_data_length;

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

void iwl_fw_error_dump(struct iwl_fw_runtime *fwrt,
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

	sg_dump_data = iwl_fw_dbg_alloc_sgtable(file_len);
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

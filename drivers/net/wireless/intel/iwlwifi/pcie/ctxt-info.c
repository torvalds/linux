// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2025 Intel Corporation
 */
#include "iwl-trans.h"
#include "iwl-fh.h"
#include "iwl-context-info.h"
#include "internal.h"
#include "iwl-prph.h"

static void *_iwl_pcie_ctxt_info_dma_alloc_coherent(struct iwl_trans *trans,
						    size_t size,
						    dma_addr_t *phys,
						    int depth)
{
	void *result;

	if (WARN(depth > 2,
		 "failed to allocate DMA memory not crossing 2^32 boundary"))
		return NULL;

	result = dma_alloc_coherent(trans->dev, size, phys, GFP_KERNEL);

	if (!result)
		return NULL;

	if (unlikely(iwl_txq_crosses_4g_boundary(*phys, size))) {
		void *old = result;
		dma_addr_t oldphys = *phys;

		result = _iwl_pcie_ctxt_info_dma_alloc_coherent(trans, size,
								phys,
								depth + 1);
		dma_free_coherent(trans->dev, size, old, oldphys);
	}

	return result;
}

void *iwl_pcie_ctxt_info_dma_alloc_coherent(struct iwl_trans *trans,
					    size_t size,
					    dma_addr_t *phys)
{
	return _iwl_pcie_ctxt_info_dma_alloc_coherent(trans, size, phys, 0);
}

int iwl_pcie_ctxt_info_alloc_dma(struct iwl_trans *trans,
				 const void *data, u32 len,
				 struct iwl_dram_data *dram)
{
	dram->block = iwl_pcie_ctxt_info_dma_alloc_coherent(trans, len,
							    &dram->physical);
	if (!dram->block)
		return -ENOMEM;

	dram->size = len;
	memcpy(dram->block, data, len);

	return 0;
}

void iwl_pcie_ctxt_info_free_paging(struct iwl_trans *trans)
{
	struct iwl_self_init_dram *dram = &trans->init_dram;
	int i;

	if (!dram->paging) {
		WARN_ON(dram->paging_cnt);
		return;
	}

	/* free paging*/
	for (i = 0; i < dram->paging_cnt; i++)
		dma_free_coherent(trans->dev, dram->paging[i].size,
				  dram->paging[i].block,
				  dram->paging[i].physical);

	kfree(dram->paging);
	dram->paging_cnt = 0;
	dram->paging = NULL;
}

int iwl_pcie_init_fw_sec(struct iwl_trans *trans,
			 const struct fw_img *fw,
			 struct iwl_context_info_dram_nonfseq *ctxt_dram)
{
	struct iwl_self_init_dram *dram = &trans->init_dram;
	int i, ret, lmac_cnt, umac_cnt, paging_cnt;

	if (WARN(dram->paging,
		 "paging shouldn't already be initialized (%d pages)\n",
		 dram->paging_cnt))
		iwl_pcie_ctxt_info_free_paging(trans);

	lmac_cnt = iwl_pcie_get_num_sections(fw, 0);
	/* add 1 due to separator */
	umac_cnt = iwl_pcie_get_num_sections(fw, lmac_cnt + 1);
	/* add 2 due to separators */
	paging_cnt = iwl_pcie_get_num_sections(fw, lmac_cnt + umac_cnt + 2);

	dram->fw = kcalloc(umac_cnt + lmac_cnt, sizeof(*dram->fw), GFP_KERNEL);
	if (!dram->fw)
		return -ENOMEM;
	dram->paging = kcalloc(paging_cnt, sizeof(*dram->paging), GFP_KERNEL);
	if (!dram->paging)
		return -ENOMEM;

	/* initialize lmac sections */
	for (i = 0; i < lmac_cnt; i++) {
		ret = iwl_pcie_ctxt_info_alloc_dma(trans, fw->sec[i].data,
						   fw->sec[i].len,
						   &dram->fw[dram->fw_cnt]);
		if (ret)
			return ret;
		ctxt_dram->lmac_img[i] =
			cpu_to_le64(dram->fw[dram->fw_cnt].physical);
		dram->fw_cnt++;
	}

	/* initialize umac sections */
	for (i = 0; i < umac_cnt; i++) {
		/* access FW with +1 to make up for lmac separator */
		ret = iwl_pcie_ctxt_info_alloc_dma(trans,
						   fw->sec[dram->fw_cnt + 1].data,
						   fw->sec[dram->fw_cnt + 1].len,
						   &dram->fw[dram->fw_cnt]);
		if (ret)
			return ret;
		ctxt_dram->umac_img[i] =
			cpu_to_le64(dram->fw[dram->fw_cnt].physical);
		dram->fw_cnt++;
	}

	/*
	 * Initialize paging.
	 * Paging memory isn't stored in dram->fw as the umac and lmac - it is
	 * stored separately.
	 * This is since the timing of its release is different -
	 * while fw memory can be released on alive, the paging memory can be
	 * freed only when the device goes down.
	 * Given that, the logic here in accessing the fw image is a bit
	 * different - fw_cnt isn't changing so loop counter is added to it.
	 */
	for (i = 0; i < paging_cnt; i++) {
		/* access FW with +2 to make up for lmac & umac separators */
		int fw_idx = dram->fw_cnt + i + 2;

		ret = iwl_pcie_ctxt_info_alloc_dma(trans, fw->sec[fw_idx].data,
						   fw->sec[fw_idx].len,
						   &dram->paging[i]);
		if (ret)
			return ret;

		ctxt_dram->virtual_img[i] =
			cpu_to_le64(dram->paging[i].physical);
		dram->paging_cnt++;
	}

	return 0;
}

int iwl_pcie_ctxt_info_init(struct iwl_trans *trans,
			    const struct fw_img *img)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_context_info *ctxt_info;
	struct iwl_context_info_rbd_cfg *rx_cfg;
	u32 control_flags = 0, rb_size;
	dma_addr_t phys;
	int ret;

	ctxt_info = iwl_pcie_ctxt_info_dma_alloc_coherent(trans,
							  sizeof(*ctxt_info),
							  &phys);
	if (!ctxt_info)
		return -ENOMEM;

	trans_pcie->ctxt_info_dma_addr = phys;

	ctxt_info->version.version = 0;
	ctxt_info->version.mac_id =
		cpu_to_le16((u16)trans->info.hw_rev);
	/* size is in DWs */
	ctxt_info->version.size = cpu_to_le16(sizeof(*ctxt_info) / 4);

	switch (trans->conf.rx_buf_size) {
	case IWL_AMSDU_2K:
		rb_size = IWL_CTXT_INFO_RB_SIZE_2K;
		break;
	case IWL_AMSDU_4K:
		rb_size = IWL_CTXT_INFO_RB_SIZE_4K;
		break;
	case IWL_AMSDU_8K:
		rb_size = IWL_CTXT_INFO_RB_SIZE_8K;
		break;
	case IWL_AMSDU_12K:
		rb_size = IWL_CTXT_INFO_RB_SIZE_16K;
		break;
	default:
		WARN_ON(1);
		rb_size = IWL_CTXT_INFO_RB_SIZE_4K;
	}

	WARN_ON(RX_QUEUE_CB_SIZE(iwl_trans_get_num_rbds(trans)) > 12);
	control_flags = IWL_CTXT_INFO_TFD_FORMAT_LONG;
	control_flags |=
		u32_encode_bits(RX_QUEUE_CB_SIZE(iwl_trans_get_num_rbds(trans)),
				IWL_CTXT_INFO_RB_CB_SIZE);
	control_flags |= u32_encode_bits(rb_size, IWL_CTXT_INFO_RB_SIZE);
	ctxt_info->control.control_flags = cpu_to_le32(control_flags);

	/* initialize RX default queue */
	rx_cfg = &ctxt_info->rbd_cfg;
	rx_cfg->free_rbd_addr = cpu_to_le64(trans_pcie->rxq->bd_dma);
	rx_cfg->used_rbd_addr = cpu_to_le64(trans_pcie->rxq->used_bd_dma);
	rx_cfg->status_wr_ptr = cpu_to_le64(trans_pcie->rxq->rb_stts_dma);

	/* initialize TX command queue */
	ctxt_info->hcmd_cfg.cmd_queue_addr =
		cpu_to_le64(trans_pcie->txqs.txq[trans->conf.cmd_queue]->dma_addr);
	ctxt_info->hcmd_cfg.cmd_queue_size =
		TFD_QUEUE_CB_SIZE(IWL_CMD_QUEUE_SIZE);

	/* allocate ucode sections in dram and set addresses */
	ret = iwl_pcie_init_fw_sec(trans, img, &ctxt_info->dram);
	if (ret) {
		dma_free_coherent(trans->dev, sizeof(*trans_pcie->ctxt_info),
				  ctxt_info, trans_pcie->ctxt_info_dma_addr);
		return ret;
	}

	trans_pcie->ctxt_info = ctxt_info;

	iwl_enable_fw_load_int_ctx_info(trans, false);

	/* Configure debug, if exists */
	if (iwl_pcie_dbg_on(trans))
		iwl_pcie_apply_destination(trans);

	/* kick FW self load */
	iwl_write64(trans, CSR_CTXT_INFO_BA, trans_pcie->ctxt_info_dma_addr);

	/* Context info will be released upon alive or failure to get one */

	return 0;
}

void iwl_pcie_ctxt_info_free(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (!trans_pcie->ctxt_info)
		return;

	dma_free_coherent(trans->dev, sizeof(*trans_pcie->ctxt_info),
			  trans_pcie->ctxt_info,
			  trans_pcie->ctxt_info_dma_addr);
	trans_pcie->ctxt_info_dma_addr = 0;
	trans_pcie->ctxt_info = NULL;

	iwl_pcie_ctxt_info_free_fw_img(trans);
}

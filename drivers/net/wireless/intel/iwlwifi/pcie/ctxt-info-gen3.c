// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2018-2022 Intel Corporation
 */
#include "iwl-trans.h"
#include "iwl-fh.h"
#include "iwl-context-info-gen3.h"
#include "internal.h"
#include "iwl-prph.h"

static void
iwl_pcie_ctxt_info_dbg_enable(struct iwl_trans *trans,
			      struct iwl_prph_scratch_hwm_cfg *dbg_cfg,
			      u32 *control_flags)
{
	enum iwl_fw_ini_allocation_id alloc_id = IWL_FW_INI_ALLOCATION_ID_DBGC1;
	struct iwl_fw_ini_allocation_tlv *fw_mon_cfg;
	u32 dbg_flags = 0;

	if (!iwl_trans_dbg_ini_valid(trans)) {
		struct iwl_dram_data *fw_mon = &trans->dbg.fw_mon;

		iwl_pcie_alloc_fw_monitor(trans, 0);

		if (fw_mon->size) {
			dbg_flags |= IWL_PRPH_SCRATCH_EDBG_DEST_DRAM;

			IWL_DEBUG_FW(trans,
				     "WRT: Applying DRAM buffer destination\n");

			dbg_cfg->hwm_base_addr = cpu_to_le64(fw_mon->physical);
			dbg_cfg->hwm_size = cpu_to_le32(fw_mon->size);
		}

		goto out;
	}

	fw_mon_cfg = &trans->dbg.fw_mon_cfg[alloc_id];

	switch (le32_to_cpu(fw_mon_cfg->buf_location)) {
	case IWL_FW_INI_LOCATION_SRAM_PATH:
		dbg_flags |= IWL_PRPH_SCRATCH_EDBG_DEST_INTERNAL;
		IWL_DEBUG_FW(trans,
				"WRT: Applying SMEM buffer destination\n");
		break;

	case IWL_FW_INI_LOCATION_NPK_PATH:
		dbg_flags |= IWL_PRPH_SCRATCH_EDBG_DEST_TB22DTF;
		IWL_DEBUG_FW(trans,
			     "WRT: Applying NPK buffer destination\n");
		break;

	case IWL_FW_INI_LOCATION_DRAM_PATH:
		if (trans->dbg.fw_mon_ini[alloc_id].num_frags) {
			struct iwl_dram_data *frag =
				&trans->dbg.fw_mon_ini[alloc_id].frags[0];
			dbg_flags |= IWL_PRPH_SCRATCH_EDBG_DEST_DRAM;
			dbg_cfg->hwm_base_addr = cpu_to_le64(frag->physical);
			dbg_cfg->hwm_size = cpu_to_le32(frag->size);
			dbg_cfg->debug_token_config = cpu_to_le32(trans->dbg.ucode_preset);
			IWL_DEBUG_FW(trans,
				     "WRT: Applying DRAM destination (debug_token_config=%u)\n",
				     dbg_cfg->debug_token_config);
			IWL_DEBUG_FW(trans,
				     "WRT: Applying DRAM destination (alloc_id=%u, num_frags=%u)\n",
				     alloc_id,
				     trans->dbg.fw_mon_ini[alloc_id].num_frags);
		}
		break;
	default:
		IWL_DEBUG_FW(trans, "WRT: Invalid buffer destination (%d)\n",
			     le32_to_cpu(fw_mon_cfg->buf_location));
	}
out:
	if (dbg_flags)
		*control_flags |= IWL_PRPH_SCRATCH_EARLY_DEBUG_EN | dbg_flags;
}

int iwl_pcie_ctxt_info_gen3_init(struct iwl_trans *trans,
				 const struct fw_img *fw)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_context_info_gen3 *ctxt_info_gen3;
	struct iwl_prph_scratch *prph_scratch;
	struct iwl_prph_scratch_ctrl_cfg *prph_sc_ctrl;
	struct iwl_prph_info *prph_info;
	u32 control_flags = 0;
	int ret;
	int cmdq_size = max_t(u32, IWL_CMD_QUEUE_SIZE,
			      trans->cfg->min_txq_size);

	switch (trans_pcie->rx_buf_size) {
	case IWL_AMSDU_DEF:
		return -EINVAL;
	case IWL_AMSDU_2K:
		break;
	case IWL_AMSDU_4K:
		control_flags |= IWL_PRPH_SCRATCH_RB_SIZE_4K;
		break;
	case IWL_AMSDU_8K:
		control_flags |= IWL_PRPH_SCRATCH_RB_SIZE_4K;
		/* if firmware supports the ext size, tell it */
		control_flags |= IWL_PRPH_SCRATCH_RB_SIZE_EXT_8K;
		break;
	case IWL_AMSDU_12K:
		control_flags |= IWL_PRPH_SCRATCH_RB_SIZE_4K;
		/* if firmware supports the ext size, tell it */
		control_flags |= IWL_PRPH_SCRATCH_RB_SIZE_EXT_16K;
		break;
	}

	/* Allocate prph scratch */
	prph_scratch = dma_alloc_coherent(trans->dev, sizeof(*prph_scratch),
					  &trans_pcie->prph_scratch_dma_addr,
					  GFP_KERNEL);
	if (!prph_scratch)
		return -ENOMEM;

	prph_sc_ctrl = &prph_scratch->ctrl_cfg;

	prph_sc_ctrl->version.version = 0;
	prph_sc_ctrl->version.mac_id =
		cpu_to_le16((u16)iwl_read32(trans, CSR_HW_REV));
	prph_sc_ctrl->version.size = cpu_to_le16(sizeof(*prph_scratch) / 4);

	control_flags |= IWL_PRPH_SCRATCH_MTR_MODE;
	control_flags |= IWL_PRPH_MTR_FORMAT_256B & IWL_PRPH_SCRATCH_MTR_FORMAT;

	if (trans->trans_cfg->imr_enabled)
		control_flags |= IWL_PRPH_SCRATCH_IMR_DEBUG_EN;

	/* initialize RX default queue */
	prph_sc_ctrl->rbd_cfg.free_rbd_addr =
		cpu_to_le64(trans_pcie->rxq->bd_dma);

	iwl_pcie_ctxt_info_dbg_enable(trans, &prph_sc_ctrl->hwm_cfg,
				      &control_flags);
	prph_sc_ctrl->control.control_flags = cpu_to_le32(control_flags);

	/* allocate ucode sections in dram and set addresses */
	ret = iwl_pcie_init_fw_sec(trans, fw, &prph_scratch->dram);
	if (ret)
		goto err_free_prph_scratch;


	/* Allocate prph information
	 * currently we don't assign to the prph info anything, but it would get
	 * assigned later
	 *
	 * We also use the second half of this page to give the device some
	 * dummy TR/CR tail pointers - which shouldn't be necessary as we don't
	 * use this, but the hardware still reads/writes there and we can't let
	 * it go do that with a NULL pointer.
	 */
	BUILD_BUG_ON(sizeof(*prph_info) > PAGE_SIZE / 2);
	prph_info = dma_alloc_coherent(trans->dev, PAGE_SIZE,
				       &trans_pcie->prph_info_dma_addr,
				       GFP_KERNEL);
	if (!prph_info) {
		ret = -ENOMEM;
		goto err_free_prph_scratch;
	}

	/* Allocate context info */
	ctxt_info_gen3 = dma_alloc_coherent(trans->dev,
					    sizeof(*ctxt_info_gen3),
					    &trans_pcie->ctxt_info_dma_addr,
					    GFP_KERNEL);
	if (!ctxt_info_gen3) {
		ret = -ENOMEM;
		goto err_free_prph_info;
	}

	ctxt_info_gen3->prph_info_base_addr =
		cpu_to_le64(trans_pcie->prph_info_dma_addr);
	ctxt_info_gen3->prph_scratch_base_addr =
		cpu_to_le64(trans_pcie->prph_scratch_dma_addr);
	ctxt_info_gen3->prph_scratch_size =
		cpu_to_le32(sizeof(*prph_scratch));
	ctxt_info_gen3->cr_head_idx_arr_base_addr =
		cpu_to_le64(trans_pcie->rxq->rb_stts_dma);
	ctxt_info_gen3->tr_tail_idx_arr_base_addr =
		cpu_to_le64(trans_pcie->prph_info_dma_addr + PAGE_SIZE / 2);
	ctxt_info_gen3->cr_tail_idx_arr_base_addr =
		cpu_to_le64(trans_pcie->prph_info_dma_addr + 3 * PAGE_SIZE / 4);
	ctxt_info_gen3->mtr_base_addr =
		cpu_to_le64(trans->txqs.txq[trans->txqs.cmd.q_id]->dma_addr);
	ctxt_info_gen3->mcr_base_addr =
		cpu_to_le64(trans_pcie->rxq->used_bd_dma);
	ctxt_info_gen3->mtr_size =
		cpu_to_le16(TFD_QUEUE_CB_SIZE(cmdq_size));
	ctxt_info_gen3->mcr_size =
		cpu_to_le16(RX_QUEUE_CB_SIZE(trans->cfg->num_rbds));

	trans_pcie->ctxt_info_gen3 = ctxt_info_gen3;
	trans_pcie->prph_info = prph_info;
	trans_pcie->prph_scratch = prph_scratch;

	/* Allocate IML */
	trans_pcie->iml = dma_alloc_coherent(trans->dev, trans->iml_len,
					     &trans_pcie->iml_dma_addr,
					     GFP_KERNEL);
	if (!trans_pcie->iml) {
		ret = -ENOMEM;
		goto err_free_ctxt_info;
	}

	memcpy(trans_pcie->iml, trans->iml, trans->iml_len);

	iwl_enable_fw_load_int_ctx_info(trans);

	/* kick FW self load */
	iwl_write64(trans, CSR_CTXT_INFO_ADDR,
		    trans_pcie->ctxt_info_dma_addr);
	iwl_write64(trans, CSR_IML_DATA_ADDR,
		    trans_pcie->iml_dma_addr);
	iwl_write32(trans, CSR_IML_SIZE_ADDR, trans->iml_len);

	iwl_set_bit(trans, CSR_CTXT_INFO_BOOT_CTRL,
		    CSR_AUTO_FUNC_BOOT_ENA);

	return 0;

err_free_ctxt_info:
	dma_free_coherent(trans->dev, sizeof(*trans_pcie->ctxt_info_gen3),
			  trans_pcie->ctxt_info_gen3,
			  trans_pcie->ctxt_info_dma_addr);
	trans_pcie->ctxt_info_gen3 = NULL;
err_free_prph_info:
	dma_free_coherent(trans->dev, PAGE_SIZE, prph_info,
			  trans_pcie->prph_info_dma_addr);

err_free_prph_scratch:
	dma_free_coherent(trans->dev,
			  sizeof(*prph_scratch),
			prph_scratch,
			trans_pcie->prph_scratch_dma_addr);
	return ret;

}

void iwl_pcie_ctxt_info_gen3_free(struct iwl_trans *trans, bool alive)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (trans_pcie->iml) {
		dma_free_coherent(trans->dev, trans->iml_len, trans_pcie->iml,
				  trans_pcie->iml_dma_addr);
		trans_pcie->iml_dma_addr = 0;
		trans_pcie->iml = NULL;
	}

	iwl_pcie_ctxt_info_free_fw_img(trans);

	if (alive)
		return;

	if (!trans_pcie->ctxt_info_gen3)
		return;

	/* ctxt_info_gen3 and prph_scratch are still needed for PNVM load */
	dma_free_coherent(trans->dev, sizeof(*trans_pcie->ctxt_info_gen3),
			  trans_pcie->ctxt_info_gen3,
			  trans_pcie->ctxt_info_dma_addr);
	trans_pcie->ctxt_info_dma_addr = 0;
	trans_pcie->ctxt_info_gen3 = NULL;

	dma_free_coherent(trans->dev, sizeof(*trans_pcie->prph_scratch),
			  trans_pcie->prph_scratch,
			  trans_pcie->prph_scratch_dma_addr);
	trans_pcie->prph_scratch_dma_addr = 0;
	trans_pcie->prph_scratch = NULL;

	/* this is needed for the entire lifetime */
	dma_free_coherent(trans->dev, PAGE_SIZE, trans_pcie->prph_info,
			  trans_pcie->prph_info_dma_addr);
	trans_pcie->prph_info_dma_addr = 0;
	trans_pcie->prph_info = NULL;
}

int iwl_trans_pcie_ctx_info_gen3_set_pnvm(struct iwl_trans *trans,
					  const void *data, u32 len)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_prph_scratch_ctrl_cfg *prph_sc_ctrl =
		&trans_pcie->prph_scratch->ctrl_cfg;
	int ret;

	if (trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_AX210)
		return 0;

	/* only allocate the DRAM if not allocated yet */
	if (!trans->pnvm_loaded) {
		if (WARN_ON(prph_sc_ctrl->pnvm_cfg.pnvm_size))
			return -EBUSY;

		ret = iwl_pcie_ctxt_info_alloc_dma(trans, data, len,
						   &trans_pcie->pnvm_dram);
		if (ret < 0) {
			IWL_DEBUG_FW(trans, "Failed to allocate PNVM DMA %d.\n",
				     ret);
			return ret;
		}
	}

	prph_sc_ctrl->pnvm_cfg.pnvm_base_addr =
		cpu_to_le64(trans_pcie->pnvm_dram.physical);
	prph_sc_ctrl->pnvm_cfg.pnvm_size =
		cpu_to_le32(trans_pcie->pnvm_dram.size);

	return 0;
}

int iwl_trans_pcie_ctx_info_gen3_set_reduce_power(struct iwl_trans *trans,
						  const void *data, u32 len)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_prph_scratch_ctrl_cfg *prph_sc_ctrl =
		&trans_pcie->prph_scratch->ctrl_cfg;
	int ret;

	if (trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_AX210)
		return 0;

	/* only allocate the DRAM if not allocated yet */
	if (!trans->reduce_power_loaded) {
		if (WARN_ON(prph_sc_ctrl->reduce_power_cfg.size))
			return -EBUSY;

		ret = iwl_pcie_ctxt_info_alloc_dma(trans, data, len,
					   &trans_pcie->reduce_power_dram);
		if (ret < 0) {
			IWL_DEBUG_FW(trans,
				     "Failed to allocate reduce power DMA %d.\n",
				     ret);
			return ret;
		}
	}

	prph_sc_ctrl->reduce_power_cfg.base_addr =
		cpu_to_le64(trans_pcie->reduce_power_dram.physical);
	prph_sc_ctrl->reduce_power_cfg.size =
		cpu_to_le32(trans_pcie->reduce_power_dram.size);

	return 0;
}

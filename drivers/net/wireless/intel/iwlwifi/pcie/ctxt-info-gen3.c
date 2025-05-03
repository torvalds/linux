// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2018-2025 Intel Corporation
 */
#include <linux/dmi.h>
#include "iwl-trans.h"
#include "iwl-fh.h"
#include "iwl-context-info-gen3.h"
#include "internal.h"
#include "iwl-prph.h"

static const struct dmi_system_id dmi_force_scu_active_approved_list[] = {
	{ .ident = "DELL",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		},
	},
	{ .ident = "DELL",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
		},
	},
	/* keep last */
	{}
};

static bool iwl_is_force_scu_active_approved(void)
{
	return !!dmi_check_system(dmi_force_scu_active_approved_list);
}

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

int iwl_pcie_ctxt_info_gen3_alloc(struct iwl_trans *trans,
				  const struct iwl_fw *fw,
				  const struct fw_img *img)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_context_info_gen3 *ctxt_info_gen3;
	struct iwl_prph_scratch *prph_scratch;
	struct iwl_prph_scratch_ctrl_cfg *prph_sc_ctrl;
	struct iwl_prph_info *prph_info;
	u32 control_flags = 0;
	u32 control_flags_ext = 0;
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

	if (trans->dsbr_urm_fw_dependent)
		control_flags_ext |= IWL_PRPH_SCRATCH_EXT_URM_FW;

	if (trans->dsbr_urm_permanent)
		control_flags_ext |= IWL_PRPH_SCRATCH_EXT_URM_PERM;

	if (trans->ext_32khz_clock_valid)
		control_flags_ext |= IWL_PRPH_SCRATCH_EXT_32KHZ_CLK_VALID;

	/* Allocate prph scratch */
	prph_scratch = dma_alloc_coherent(trans->dev, sizeof(*prph_scratch),
					  &trans_pcie->prph_scratch_dma_addr,
					  GFP_KERNEL);
	if (!prph_scratch)
		return -ENOMEM;

	prph_sc_ctrl = &prph_scratch->ctrl_cfg;

	prph_sc_ctrl->version.version = 0;
	prph_sc_ctrl->version.mac_id =
		cpu_to_le16((u16)trans->hw_rev);
	prph_sc_ctrl->version.size = cpu_to_le16(sizeof(*prph_scratch) / 4);

	control_flags |= IWL_PRPH_SCRATCH_MTR_MODE;
	control_flags |= IWL_PRPH_MTR_FORMAT_256B & IWL_PRPH_SCRATCH_MTR_FORMAT;

	if (trans->trans_cfg->imr_enabled)
		control_flags |= IWL_PRPH_SCRATCH_IMR_DEBUG_EN;

	if (CSR_HW_REV_TYPE(trans->hw_rev) == IWL_CFG_MAC_TYPE_GL &&
	    iwl_is_force_scu_active_approved()) {
		control_flags |= IWL_PRPH_SCRATCH_SCU_FORCE_ACTIVE;
		IWL_DEBUG_FW(trans,
			     "Context Info: Set SCU_FORCE_ACTIVE (0x%x) in control_flags\n",
			     IWL_PRPH_SCRATCH_SCU_FORCE_ACTIVE);
	}

	if (trans->do_top_reset) {
		WARN_ON(trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_SC);
		control_flags |= IWL_PRPH_SCRATCH_TOP_RESET;
	}

	/* initialize RX default queue */
	prph_sc_ctrl->rbd_cfg.free_rbd_addr =
		cpu_to_le64(trans_pcie->rxq->bd_dma);

	iwl_pcie_ctxt_info_dbg_enable(trans, &prph_sc_ctrl->hwm_cfg,
				      &control_flags);
	prph_sc_ctrl->control.control_flags = cpu_to_le32(control_flags);
	prph_sc_ctrl->control.control_flags_ext = cpu_to_le32(control_flags_ext);

	/* initialize the Step equalizer data */
	prph_sc_ctrl->step_cfg.mbx_addr_0 = cpu_to_le32(trans->mbx_addr_0_step);
	prph_sc_ctrl->step_cfg.mbx_addr_1 = cpu_to_le32(trans->mbx_addr_1_step);

	/* allocate ucode sections in dram and set addresses */
	ret = iwl_pcie_init_fw_sec(trans, img, &prph_scratch->dram.common);
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

	/*
	 * This code assumes the FSEQ is last and we can make that
	 * optional; old devices _should_ be fine with a bigger size,
	 * but in simulation we check the size more precisely.
	 */
	BUILD_BUG_ON(offsetofend(typeof(*prph_scratch), dram.common) +
		     sizeof(prph_scratch->dram.fseq_img) !=
		     sizeof(*prph_scratch));
	if (control_flags_ext & IWL_PRPH_SCRATCH_EXT_EXT_FSEQ)
		ctxt_info_gen3->prph_scratch_size =
			cpu_to_le32(sizeof(*prph_scratch));
	else
		ctxt_info_gen3->prph_scratch_size =
			cpu_to_le32(offsetofend(typeof(*prph_scratch),
						dram.common));

	ctxt_info_gen3->cr_head_idx_arr_base_addr =
		cpu_to_le64(trans_pcie->rxq->rb_stts_dma);
	ctxt_info_gen3->tr_tail_idx_arr_base_addr =
		cpu_to_le64(trans_pcie->prph_info_dma_addr + PAGE_SIZE / 2);
	ctxt_info_gen3->cr_tail_idx_arr_base_addr =
		cpu_to_le64(trans_pcie->prph_info_dma_addr + 3 * PAGE_SIZE / 4);
	ctxt_info_gen3->mtr_base_addr =
		cpu_to_le64(trans_pcie->txqs.txq[trans_pcie->txqs.cmd.q_id]->dma_addr);
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
	trans_pcie->iml_len = fw->iml_len;
	trans_pcie->iml = dma_alloc_coherent(trans->dev, fw->iml_len,
					     &trans_pcie->iml_dma_addr,
					     GFP_KERNEL);
	if (!trans_pcie->iml) {
		ret = -ENOMEM;
		goto err_free_ctxt_info;
	}

	memcpy(trans_pcie->iml, fw->iml, fw->iml_len);

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

void iwl_pcie_ctxt_info_gen3_kick(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	iwl_enable_fw_load_int_ctx_info(trans, trans->do_top_reset);

	/* kick FW self load */
	iwl_write64(trans, CSR_CTXT_INFO_ADDR, trans_pcie->ctxt_info_dma_addr);
	iwl_write64(trans, CSR_IML_DATA_ADDR, trans_pcie->iml_dma_addr);
	iwl_write32(trans, CSR_IML_SIZE_ADDR, trans_pcie->iml_len);

	iwl_set_bit(trans, CSR_CTXT_INFO_BOOT_CTRL,
		    CSR_AUTO_FUNC_BOOT_ENA);
}

void iwl_pcie_ctxt_info_gen3_free(struct iwl_trans *trans, bool alive)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (trans_pcie->iml) {
		dma_free_coherent(trans->dev, trans_pcie->iml_len,
				  trans_pcie->iml,
				  trans_pcie->iml_dma_addr);
		trans_pcie->iml_dma_addr = 0;
		trans_pcie->iml_len = 0;
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

static int iwl_pcie_load_payloads_contig(struct iwl_trans *trans,
					 const struct iwl_pnvm_image *pnvm_data,
					 struct iwl_dram_data *dram)
{
	u32 len, len0, len1;

	if (pnvm_data->n_chunks != UNFRAGMENTED_PNVM_PAYLOADS_NUMBER) {
		IWL_DEBUG_FW(trans, "expected 2 payloads, got %d.\n",
			     pnvm_data->n_chunks);
		return -EINVAL;
	}

	len0 = pnvm_data->chunks[0].len;
	len1 = pnvm_data->chunks[1].len;
	if (len1 > 0xFFFFFFFF - len0) {
		IWL_DEBUG_FW(trans, "sizes of payloads overflow.\n");
		return -EINVAL;
	}
	len = len0 + len1;

	dram->block = iwl_pcie_ctxt_info_dma_alloc_coherent(trans, len,
							    &dram->physical);
	if (!dram->block) {
		IWL_DEBUG_FW(trans, "Failed to allocate PNVM DMA.\n");
		return -ENOMEM;
	}

	dram->size = len;
	memcpy(dram->block, pnvm_data->chunks[0].data, len0);
	memcpy((u8 *)dram->block + len0, pnvm_data->chunks[1].data, len1);

	return 0;
}

static int iwl_pcie_load_payloads_segments
				(struct iwl_trans *trans,
				 struct iwl_dram_regions *dram_regions,
				 const struct iwl_pnvm_image *pnvm_data)
{
	struct iwl_dram_data *cur_payload_dram = &dram_regions->drams[0];
	struct iwl_dram_data *desc_dram = &dram_regions->prph_scratch_mem_desc;
	struct iwl_prph_scrath_mem_desc_addr_array *addresses;
	const void *data;
	u32 len;
	int i;

	/* allocate and init DRAM descriptors array */
	len = sizeof(struct iwl_prph_scrath_mem_desc_addr_array);
	desc_dram->block = iwl_pcie_ctxt_info_dma_alloc_coherent
						(trans,
						 len,
						 &desc_dram->physical);
	if (!desc_dram->block) {
		IWL_DEBUG_FW(trans, "Failed to allocate PNVM DMA.\n");
		return -ENOMEM;
	}
	desc_dram->size = len;
	memset(desc_dram->block, 0, len);

	/* allocate DRAM region for each payload */
	dram_regions->n_regions = 0;
	for (i = 0; i < pnvm_data->n_chunks; i++) {
		len = pnvm_data->chunks[i].len;
		data = pnvm_data->chunks[i].data;

		if (iwl_pcie_ctxt_info_alloc_dma(trans,
						 data,
						 len,
						 cur_payload_dram)) {
			iwl_trans_pcie_free_pnvm_dram_regions(dram_regions,
							      trans->dev);
			return -ENOMEM;
		}

		dram_regions->n_regions++;
		cur_payload_dram++;
	}

	/* fill desc with the DRAM payloads addresses */
	addresses = desc_dram->block;
	for (i = 0; i < pnvm_data->n_chunks; i++) {
		addresses->mem_descs[i] =
			cpu_to_le64(dram_regions->drams[i].physical);
	}

	return 0;

}

int iwl_trans_pcie_ctx_info_gen3_load_pnvm(struct iwl_trans *trans,
					   const struct iwl_pnvm_image *pnvm_payloads,
					   const struct iwl_ucode_capabilities *capa)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_prph_scratch_ctrl_cfg *prph_sc_ctrl =
		&trans_pcie->prph_scratch->ctrl_cfg;
	struct iwl_dram_regions *dram_regions = &trans_pcie->pnvm_data;
	int ret = 0;

	/* only allocate the DRAM if not allocated yet */
	if (trans->pnvm_loaded)
		return 0;

	if (WARN_ON(prph_sc_ctrl->pnvm_cfg.pnvm_size))
		return -EBUSY;

	if (trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_AX210)
		return 0;

	if (!pnvm_payloads->n_chunks) {
		IWL_DEBUG_FW(trans, "no payloads\n");
		return -EINVAL;
	}

	/* save payloads in several DRAM sections */
	if (fw_has_capa(capa, IWL_UCODE_TLV_CAPA_FRAGMENTED_PNVM_IMG)) {
		ret = iwl_pcie_load_payloads_segments(trans,
						      dram_regions,
						      pnvm_payloads);
		if (!ret)
			trans->pnvm_loaded = true;
	} else {
		/* save only in one DRAM section */
		ret = iwl_pcie_load_payloads_contig(trans, pnvm_payloads,
						    &dram_regions->drams[0]);
		if (!ret) {
			dram_regions->n_regions = 1;
			trans->pnvm_loaded = true;
		}
	}

	return ret;
}

static inline size_t
iwl_dram_regions_size(const struct iwl_dram_regions *dram_regions)
{
	size_t total_size = 0;
	int i;

	for (i = 0; i < dram_regions->n_regions; i++)
		total_size += dram_regions->drams[i].size;

	return total_size;
}

static void iwl_pcie_set_pnvm_segments(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_prph_scratch_ctrl_cfg *prph_sc_ctrl =
		&trans_pcie->prph_scratch->ctrl_cfg;
	struct iwl_dram_regions *dram_regions = &trans_pcie->pnvm_data;

	prph_sc_ctrl->pnvm_cfg.pnvm_base_addr =
		cpu_to_le64(dram_regions->prph_scratch_mem_desc.physical);
	prph_sc_ctrl->pnvm_cfg.pnvm_size =
		cpu_to_le32(iwl_dram_regions_size(dram_regions));
}

static void iwl_pcie_set_contig_pnvm(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_prph_scratch_ctrl_cfg *prph_sc_ctrl =
		&trans_pcie->prph_scratch->ctrl_cfg;

	prph_sc_ctrl->pnvm_cfg.pnvm_base_addr =
		cpu_to_le64(trans_pcie->pnvm_data.drams[0].physical);
	prph_sc_ctrl->pnvm_cfg.pnvm_size =
		cpu_to_le32(trans_pcie->pnvm_data.drams[0].size);
}

void iwl_trans_pcie_ctx_info_gen3_set_pnvm(struct iwl_trans *trans,
					   const struct iwl_ucode_capabilities *capa)
{
	if (trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_AX210)
		return;

	if (fw_has_capa(capa, IWL_UCODE_TLV_CAPA_FRAGMENTED_PNVM_IMG))
		iwl_pcie_set_pnvm_segments(trans);
	else
		iwl_pcie_set_contig_pnvm(trans);
}

int iwl_trans_pcie_ctx_info_gen3_load_reduce_power(struct iwl_trans *trans,
						   const struct iwl_pnvm_image *payloads,
						   const struct iwl_ucode_capabilities *capa)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_prph_scratch_ctrl_cfg *prph_sc_ctrl =
		&trans_pcie->prph_scratch->ctrl_cfg;
	struct iwl_dram_regions *dram_regions = &trans_pcie->reduced_tables_data;
	int ret = 0;

	/* only allocate the DRAM if not allocated yet */
	if (trans->reduce_power_loaded)
		return 0;

	if (trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_AX210)
		return 0;

	if (WARN_ON(prph_sc_ctrl->reduce_power_cfg.size))
		return -EBUSY;

	if (!payloads->n_chunks) {
		IWL_DEBUG_FW(trans, "no payloads\n");
		return -EINVAL;
	}

	/* save payloads in several DRAM sections */
	if (fw_has_capa(capa, IWL_UCODE_TLV_CAPA_FRAGMENTED_PNVM_IMG)) {
		ret = iwl_pcie_load_payloads_segments(trans,
						      dram_regions,
						      payloads);
		if (!ret)
			trans->reduce_power_loaded = true;
	} else {
		/* save only in one DRAM section */
		ret = iwl_pcie_load_payloads_contig(trans, payloads,
						    &dram_regions->drams[0]);
		if (!ret) {
			dram_regions->n_regions = 1;
			trans->reduce_power_loaded = true;
		}
	}

	return ret;
}

static void iwl_pcie_set_reduce_power_segments(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_prph_scratch_ctrl_cfg *prph_sc_ctrl =
		&trans_pcie->prph_scratch->ctrl_cfg;
	struct iwl_dram_regions *dram_regions = &trans_pcie->reduced_tables_data;

	prph_sc_ctrl->reduce_power_cfg.base_addr =
		cpu_to_le64(dram_regions->prph_scratch_mem_desc.physical);
	prph_sc_ctrl->reduce_power_cfg.size =
		cpu_to_le32(iwl_dram_regions_size(dram_regions));
}

static void iwl_pcie_set_contig_reduce_power(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_prph_scratch_ctrl_cfg *prph_sc_ctrl =
		&trans_pcie->prph_scratch->ctrl_cfg;

	prph_sc_ctrl->reduce_power_cfg.base_addr =
		cpu_to_le64(trans_pcie->reduced_tables_data.drams[0].physical);
	prph_sc_ctrl->reduce_power_cfg.size =
		cpu_to_le32(trans_pcie->reduced_tables_data.drams[0].size);
}

void
iwl_trans_pcie_ctx_info_gen3_set_reduce_power(struct iwl_trans *trans,
					      const struct iwl_ucode_capabilities *capa)
{
	if (trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_AX210)
		return;

	if (fw_has_capa(capa, IWL_UCODE_TLV_CAPA_FRAGMENTED_PNVM_IMG))
		iwl_pcie_set_reduce_power_segments(trans);
	else
		iwl_pcie_set_contig_reduce_power(trans);
}


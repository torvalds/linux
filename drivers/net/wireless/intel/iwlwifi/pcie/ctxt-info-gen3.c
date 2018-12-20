/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2018 Intel Corporation
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
 * BSD LICENSE
 *
 * Copyright(c) 2018 Intel Corporation
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

#include "iwl-trans.h"
#include "iwl-fh.h"
#include "iwl-context-info-gen3.h"
#include "internal.h"
#include "iwl-prph.h"

int iwl_pcie_ctxt_info_gen3_init(struct iwl_trans *trans,
				 const struct fw_img *fw)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_context_info_gen3 *ctxt_info_gen3;
	struct iwl_prph_scratch *prph_scratch;
	struct iwl_prph_scratch_ctrl_cfg *prph_sc_ctrl;
	struct iwl_prph_info *prph_info;
	void *iml_img;
	u32 control_flags = 0;
	int ret;

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

	control_flags = IWL_PRPH_SCRATCH_RB_SIZE_4K |
			IWL_PRPH_SCRATCH_MTR_MODE |
			(IWL_PRPH_MTR_FORMAT_256B &
			 IWL_PRPH_SCRATCH_MTR_FORMAT) |
			IWL_PRPH_SCRATCH_EARLY_DEBUG_EN |
			IWL_PRPH_SCRATCH_EDBG_DEST_DRAM;
	prph_sc_ctrl->control.control_flags = cpu_to_le32(control_flags);

	/* initialize RX default queue */
	prph_sc_ctrl->rbd_cfg.free_rbd_addr =
		cpu_to_le64(trans_pcie->rxq->bd_dma);

	/* Configure debug, for integration */
	iwl_pcie_alloc_fw_monitor(trans, 0);
	prph_sc_ctrl->hwm_cfg.hwm_base_addr =
		cpu_to_le64(trans->fw_mon[0].physical);
	prph_sc_ctrl->hwm_cfg.hwm_size =
		cpu_to_le32(trans->fw_mon[0].size);

	/* allocate ucode sections in dram and set addresses */
	ret = iwl_pcie_init_fw_sec(trans, fw, &prph_scratch->dram);
	if (ret) {
		dma_free_coherent(trans->dev,
				  sizeof(*prph_scratch),
				  prph_scratch,
				  trans_pcie->prph_scratch_dma_addr);
		return ret;
	}

	/* Allocate prph information
	 * currently we don't assign to the prph info anything, but it would get
	 * assigned later */
	prph_info = dma_alloc_coherent(trans->dev, sizeof(*prph_info),
				       &trans_pcie->prph_info_dma_addr,
				       GFP_KERNEL);
	if (!prph_info)
		return -ENOMEM;

	/* Allocate context info */
	ctxt_info_gen3 = dma_alloc_coherent(trans->dev,
					    sizeof(*ctxt_info_gen3),
					    &trans_pcie->ctxt_info_dma_addr,
					    GFP_KERNEL);
	if (!ctxt_info_gen3)
		return -ENOMEM;

	ctxt_info_gen3->prph_info_base_addr =
		cpu_to_le64(trans_pcie->prph_info_dma_addr);
	ctxt_info_gen3->prph_scratch_base_addr =
		cpu_to_le64(trans_pcie->prph_scratch_dma_addr);
	ctxt_info_gen3->prph_scratch_size =
		cpu_to_le32(sizeof(*prph_scratch));
	ctxt_info_gen3->cr_head_idx_arr_base_addr =
		cpu_to_le64(trans_pcie->rxq->rb_stts_dma);
	ctxt_info_gen3->tr_tail_idx_arr_base_addr =
		cpu_to_le64(trans_pcie->rxq->tr_tail_dma);
	ctxt_info_gen3->cr_tail_idx_arr_base_addr =
		cpu_to_le64(trans_pcie->rxq->cr_tail_dma);
	ctxt_info_gen3->cr_idx_arr_size =
		cpu_to_le16(IWL_NUM_OF_COMPLETION_RINGS);
	ctxt_info_gen3->tr_idx_arr_size =
		cpu_to_le16(IWL_NUM_OF_TRANSFER_RINGS);
	ctxt_info_gen3->mtr_base_addr =
		cpu_to_le64(trans_pcie->txq[trans_pcie->cmd_queue]->dma_addr);
	ctxt_info_gen3->mcr_base_addr =
		cpu_to_le64(trans_pcie->rxq->used_bd_dma);
	ctxt_info_gen3->mtr_size =
		cpu_to_le16(TFD_QUEUE_CB_SIZE(TFD_CMD_SLOTS));
	ctxt_info_gen3->mcr_size =
		cpu_to_le16(RX_QUEUE_CB_SIZE(MQ_RX_TABLE_SIZE));

	trans_pcie->ctxt_info_gen3 = ctxt_info_gen3;
	trans_pcie->prph_info = prph_info;
	trans_pcie->prph_scratch = prph_scratch;

	/* Allocate IML */
	iml_img = dma_alloc_coherent(trans->dev, trans->iml_len,
				     &trans_pcie->iml_dma_addr, GFP_KERNEL);
	if (!iml_img)
		return -ENOMEM;

	memcpy(iml_img, trans->iml, trans->iml_len);

	iwl_enable_interrupts(trans);

	/* kick FW self load */
	iwl_write64(trans, CSR_CTXT_INFO_ADDR,
		    trans_pcie->ctxt_info_dma_addr);
	iwl_write64(trans, CSR_IML_DATA_ADDR,
		    trans_pcie->iml_dma_addr);
	iwl_write32(trans, CSR_IML_SIZE_ADDR, trans->iml_len);
	iwl_set_bit(trans, CSR_CTXT_INFO_BOOT_CTRL, CSR_AUTO_FUNC_BOOT_ENA);
	iwl_set_bit(trans, CSR_GP_CNTRL, CSR_AUTO_FUNC_INIT);

	return 0;
}

void iwl_pcie_ctxt_info_gen3_free(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (!trans_pcie->ctxt_info_gen3)
		return;

	dma_free_coherent(trans->dev, sizeof(*trans_pcie->ctxt_info_gen3),
			  trans_pcie->ctxt_info_gen3,
			  trans_pcie->ctxt_info_dma_addr);
	trans_pcie->ctxt_info_dma_addr = 0;
	trans_pcie->ctxt_info_gen3 = NULL;

	iwl_pcie_ctxt_info_free_fw_img(trans);

	dma_free_coherent(trans->dev, sizeof(*trans_pcie->prph_scratch),
			  trans_pcie->prph_scratch,
			  trans_pcie->prph_scratch_dma_addr);
	trans_pcie->prph_scratch_dma_addr = 0;
	trans_pcie->prph_scratch = NULL;

	dma_free_coherent(trans->dev, sizeof(*trans_pcie->prph_info),
			  trans_pcie->prph_info,
			  trans_pcie->prph_info_dma_addr);
	trans_pcie->prph_info_dma_addr = 0;
	trans_pcie->prph_info = NULL;
}

/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2017 Intel Deutschland GmbH
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
 * Copyright(c) 2017 Intel Deutschland GmbH
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
#include "iwl-context-info.h"
#include "internal.h"
#include "iwl-prph.h"

static int iwl_pcie_get_num_sections(const struct fw_img *fw,
				     int start)
{
	int i = 0;

	while (start < fw->num_sec &&
	       fw->sec[start].offset != CPU1_CPU2_SEPARATOR_SECTION &&
	       fw->sec[start].offset != PAGING_SEPARATOR_SECTION) {
		start++;
		i++;
	}

	return i;
}

static int iwl_pcie_ctxt_info_alloc_dma(struct iwl_trans *trans,
					const struct fw_desc *sec,
					struct iwl_dram_data *dram)
{
	dram->block = dma_alloc_coherent(trans->dev, sec->len,
					 &dram->physical,
					 GFP_KERNEL);
	if (!dram->block)
		return -ENOMEM;

	dram->size = sec->len;
	memcpy(dram->block, sec->data, sec->len);

	return 0;
}

static void iwl_pcie_ctxt_info_free_fw_img(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_self_init_dram *dram = &trans_pcie->init_dram;
	int i;

	if (!dram->fw) {
		WARN_ON(dram->fw_cnt);
		return;
	}

	for (i = 0; i < dram->fw_cnt; i++)
		dma_free_coherent(trans->dev, dram->fw[i].size,
				  dram->fw[i].block, dram->fw[i].physical);

	kfree(dram->fw);
	dram->fw_cnt = 0;
	dram->fw = NULL;
}

void iwl_pcie_ctxt_info_free_paging(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_self_init_dram *dram = &trans_pcie->init_dram;
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

static int iwl_pcie_ctxt_info_init_fw_sec(struct iwl_trans *trans,
					  const struct fw_img *fw,
					  struct iwl_context_info *ctxt_info)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_self_init_dram *dram = &trans_pcie->init_dram;
	struct iwl_context_info_dram *ctxt_dram = &ctxt_info->dram;
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
		ret = iwl_pcie_ctxt_info_alloc_dma(trans, &fw->sec[i],
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
						   &fw->sec[dram->fw_cnt + 1],
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

		ret = iwl_pcie_ctxt_info_alloc_dma(trans, &fw->sec[fw_idx],
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
			    const struct fw_img *fw)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_context_info *ctxt_info;
	struct iwl_context_info_rbd_cfg *rx_cfg;
	u32 control_flags = 0;
	int ret;

	ctxt_info = dma_alloc_coherent(trans->dev, sizeof(*ctxt_info),
				       &trans_pcie->ctxt_info_dma_addr,
				       GFP_KERNEL);
	if (!ctxt_info)
		return -ENOMEM;

	ctxt_info->version.version = 0;
	ctxt_info->version.mac_id =
		cpu_to_le16((u16)iwl_read32(trans, CSR_HW_REV));
	/* size is in DWs */
	ctxt_info->version.size = cpu_to_le16(sizeof(*ctxt_info) / 4);

	BUILD_BUG_ON(RX_QUEUE_CB_SIZE(MQ_RX_TABLE_SIZE) > 0xF);
	control_flags = IWL_CTXT_INFO_RB_SIZE_4K |
			IWL_CTXT_INFO_TFD_FORMAT_LONG |
			RX_QUEUE_CB_SIZE(MQ_RX_TABLE_SIZE) <<
			IWL_CTXT_INFO_RB_CB_SIZE_POS;
	ctxt_info->control.control_flags = cpu_to_le32(control_flags);

	/* initialize RX default queue */
	rx_cfg = &ctxt_info->rbd_cfg;
	rx_cfg->free_rbd_addr = cpu_to_le64(trans_pcie->rxq->bd_dma);
	rx_cfg->used_rbd_addr = cpu_to_le64(trans_pcie->rxq->used_bd_dma);
	rx_cfg->status_wr_ptr = cpu_to_le64(trans_pcie->rxq->rb_stts_dma);

	/* initialize TX command queue */
	ctxt_info->hcmd_cfg.cmd_queue_addr =
		cpu_to_le64(trans_pcie->txq[trans_pcie->cmd_queue]->dma_addr);
	ctxt_info->hcmd_cfg.cmd_queue_size =
		TFD_QUEUE_CB_SIZE(trans_pcie->tx_cmd_queue_size);

	/* allocate ucode sections in dram and set addresses */
	ret = iwl_pcie_ctxt_info_init_fw_sec(trans, fw, ctxt_info);
	if (ret) {
		dma_free_coherent(trans->dev, sizeof(*trans_pcie->ctxt_info),
				  ctxt_info, trans_pcie->ctxt_info_dma_addr);
		return ret;
	}

	trans_pcie->ctxt_info = ctxt_info;

	iwl_enable_interrupts(trans);

	/* Configure debug, if exists */
	if (trans->dbg_dest_tlv)
		iwl_pcie_apply_destination(trans);

	/* kick FW self load */
	iwl_write64(trans, CSR_CTXT_INFO_BA, trans_pcie->ctxt_info_dma_addr);
	iwl_write_prph(trans, UREG_CPU_INIT_RUN, 1);

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

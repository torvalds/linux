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

#include "iwl-debug.h"
#include "iwl-csr.h"
#include "iwl-io.h"
#include "internal.h"

/*
 * iwl_pcie_gen2_txq_unmap -  Unmap any remaining DMA mappings and free skb's
 */
void iwl_pcie_gen2_txq_unmap(struct iwl_trans *trans, int txq_id)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq = &trans_pcie->txq[txq_id];

	spin_lock_bh(&txq->lock);
	while (txq->write_ptr != txq->read_ptr) {
		IWL_DEBUG_TX_REPLY(trans, "Q %d Free %d\n",
				   txq_id, txq->read_ptr);

		iwl_pcie_txq_free_tfd(trans, txq);
		txq->read_ptr = iwl_queue_inc_wrap(txq->read_ptr);

		if (txq->read_ptr == txq->write_ptr) {
			unsigned long flags;

			spin_lock_irqsave(&trans_pcie->reg_lock, flags);
			if (txq_id != trans_pcie->cmd_queue) {
				IWL_DEBUG_RPM(trans, "Q %d - last tx freed\n",
					      txq->id);
				iwl_trans_unref(trans);
			} else if (trans_pcie->ref_cmd_in_flight) {
				trans_pcie->ref_cmd_in_flight = false;
				IWL_DEBUG_RPM(trans,
					      "clear ref_cmd_in_flight\n");
				iwl_trans_unref(trans);
			}
			spin_unlock_irqrestore(&trans_pcie->reg_lock, flags);
		}
	}
	spin_unlock_bh(&txq->lock);

	/* just in case - this queue may have been stopped */
	iwl_wake_queue(trans, txq);
}

int iwl_trans_pcie_dyn_txq_alloc(struct iwl_trans *trans,
				 struct iwl_tx_queue_cfg_cmd *cmd,
				 int cmd_id,
				 unsigned int timeout)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_txq *txq = &trans_pcie->txq[cmd->scd_queue];
	struct iwl_host_cmd hcmd = {
		.id = cmd_id,
		.len = { sizeof(*cmd) },
		.data = { cmd, },
		.flags = 0,
	};
	u16 ssn = le16_to_cpu(cmd->ssn);

	if (test_and_set_bit(cmd->scd_queue, trans_pcie->queue_used)) {
		WARN_ONCE(1, "queue %d already used", cmd->scd_queue);
		return -EINVAL;
	}

	txq->wd_timeout = msecs_to_jiffies(timeout);

	/*
	 * Place first TFD at index corresponding to start sequence number.
	 * Assumes that ssn_idx is valid (!= 0xFFF)
	 */
	txq->read_ptr = (ssn & 0xff);
	txq->write_ptr = (ssn & 0xff);
	iwl_write_direct32(trans, HBUS_TARG_WRPTR,
			   (ssn & 0xff) | (cmd->scd_queue << 8));

	IWL_DEBUG_TX_QUEUES(trans, "Activate queue %d WrPtr: %d\n",
			    cmd->scd_queue, ssn & 0xff);

	cmd->tfdq_addr = cpu_to_le64(txq->dma_addr);
	cmd->byte_cnt_addr = cpu_to_le64(trans_pcie->scd_bc_tbls.dma +
					 cmd->scd_queue *
					 sizeof(struct iwlagn_scd_bc_tbl));
	cmd->cb_size = cpu_to_le64(TFD_QUEUE_CB_SIZE(TFD_QUEUE_SIZE_MAX));

	return iwl_trans_send_cmd(trans, &hcmd);
}

void iwl_trans_pcie_dyn_txq_free(struct iwl_trans *trans, int queue)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	trans_pcie->txq[queue].frozen_expiry_remainder = 0;
	trans_pcie->txq[queue].frozen = false;

	/*
	 * Upon HW Rfkill - we stop the device, and then stop the queues
	 * in the op_mode. Just for the sake of the simplicity of the op_mode,
	 * allow the op_mode to call txq_disable after it already called
	 * stop_device.
	 */
	if (!test_and_clear_bit(queue, trans_pcie->queue_used)) {
		WARN_ONCE(test_bit(STATUS_DEVICE_ENABLED, &trans->status),
			  "queue %d not used", queue);
		return;
	}

	iwl_pcie_gen2_txq_unmap(trans, queue);

	IWL_DEBUG_TX_QUEUES(trans, "Deactivate queue %d\n", queue);
}


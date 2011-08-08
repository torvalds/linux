/******************************************************************************
 *
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 *
 * Portions of this file are derived from the ipw3945 project, as well
 * as portions of the ieee80211 subsystem header files.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/
#ifndef __iwl_trans_int_pcie_h__
#define __iwl_trans_int_pcie_h__

/*This file includes the declaration that are internal to the
 * trans_pcie layer */

/*****************************************************
* RX
******************************************************/
void iwl_bg_rx_replenish(struct work_struct *data);
void iwl_irq_tasklet(struct iwl_priv *priv);
void iwlagn_rx_replenish(struct iwl_priv *priv);
void iwl_rx_queue_update_write_ptr(struct iwl_priv *priv,
			struct iwl_rx_queue *q);

/*****************************************************
* ICT
******************************************************/
int iwl_reset_ict(struct iwl_priv *priv);
void iwl_disable_ict(struct iwl_priv *priv);
int iwl_alloc_isr_ict(struct iwl_priv *priv);
void iwl_free_isr_ict(struct iwl_priv *priv);
irqreturn_t iwl_isr_ict(int irq, void *data);


/*****************************************************
* TX / HCMD
******************************************************/
void iwl_txq_update_write_ptr(struct iwl_priv *priv, struct iwl_tx_queue *txq);
void iwlagn_txq_free_tfd(struct iwl_priv *priv, struct iwl_tx_queue *txq,
				int index);
int iwlagn_txq_attach_buf_to_tfd(struct iwl_priv *priv,
				 struct iwl_tx_queue *txq,
				 dma_addr_t addr, u16 len, u8 reset);
int iwl_queue_init(struct iwl_priv *priv, struct iwl_queue *q,
			  int count, int slots_num, u32 id);
int iwl_send_cmd(struct iwl_priv *priv, struct iwl_host_cmd *cmd);
int __must_check iwl_send_cmd_pdu(struct iwl_priv *priv, u8 id, u32 flags,
			u16 len, const void *data);
void iwl_tx_cmd_complete(struct iwl_priv *priv, struct iwl_rx_mem_buffer *rxb);
void iwl_trans_txq_update_byte_cnt_tbl(struct iwl_priv *priv,
					   struct iwl_tx_queue *txq,
					   u16 byte_cnt);
int iwl_trans_txq_agg_disable(struct iwl_priv *priv, u16 txq_id,
				  u16 ssn_idx, u8 tx_fifo);
void iwl_trans_set_wr_ptrs(struct iwl_priv *priv,
		     int txq_id, u32 index);
void iwl_trans_tx_queue_set_status(struct iwl_priv *priv,
			     struct iwl_tx_queue *txq,
			     int tx_fifo_id, int scd_retry);
void iwl_trans_txq_agg_setup(struct iwl_priv *priv, int sta_id, int tid,
						int frame_limit);

#endif /* __iwl_trans_int_pcie_h__ */

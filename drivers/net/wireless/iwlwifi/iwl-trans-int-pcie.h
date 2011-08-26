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

#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/skbuff.h>

#include "iwl-fh.h"
#include "iwl-csr.h"
#include "iwl-shared.h"
#include "iwl-trans.h"
#include "iwl-debug.h"
#include "iwl-io.h"

struct iwl_tx_queue;
struct iwl_queue;
struct iwl_host_cmd;

/*This file includes the declaration that are internal to the
 * trans_pcie layer */

/**
 * struct isr_statistics - interrupt statistics
 *
 */
struct isr_statistics {
	u32 hw;
	u32 sw;
	u32 err_code;
	u32 sch;
	u32 alive;
	u32 rfkill;
	u32 ctkill;
	u32 wakeup;
	u32 rx;
	u32 tx;
	u32 unhandled;
};

/**
 * struct iwl_rx_queue - Rx queue
 * @bd: driver's pointer to buffer of receive buffer descriptors (rbd)
 * @bd_dma: bus address of buffer of receive buffer descriptors (rbd)
 * @pool:
 * @queue:
 * @read: Shared index to newest available Rx buffer
 * @write: Shared index to oldest written Rx packet
 * @free_count: Number of pre-allocated buffers in rx_free
 * @write_actual:
 * @rx_free: list of free SKBs for use
 * @rx_used: List of Rx buffers with no SKB
 * @need_update: flag to indicate we need to update read/write index
 * @rb_stts: driver's pointer to receive buffer status
 * @rb_stts_dma: bus address of receive buffer status
 * @lock:
 *
 * NOTE:  rx_free and rx_used are used as a FIFO for iwl_rx_mem_buffers
 */
struct iwl_rx_queue {
	__le32 *bd;
	dma_addr_t bd_dma;
	struct iwl_rx_mem_buffer pool[RX_QUEUE_SIZE + RX_FREE_BUFFERS];
	struct iwl_rx_mem_buffer *queue[RX_QUEUE_SIZE];
	u32 read;
	u32 write;
	u32 free_count;
	u32 write_actual;
	struct list_head rx_free;
	struct list_head rx_used;
	int need_update;
	struct iwl_rb_status *rb_stts;
	dma_addr_t rb_stts_dma;
	spinlock_t lock;
};

struct iwl_dma_ptr {
	dma_addr_t dma;
	void *addr;
	size_t size;
};

/*
 * This queue number is required for proper operation
 * because the ucode will stop/start the scheduler as
 * required.
 */
#define IWL_IPAN_MCAST_QUEUE		8

/**
 * struct iwl_trans_pcie - PCIe transport specific data
 * @rxq: all the RX queue data
 * @rx_replenish: work that will be called when buffers need to be allocated
 * @trans: pointer to the generic transport area
 * @scd_base_addr: scheduler sram base address in SRAM
 * @scd_bc_tbls: pointer to the byte count table of the scheduler
 * @kw: keep warm address
 * @ac_to_fifo: to what fifo is a specifc AC mapped ?
 * @ac_to_queue: to what tx queue  is a specifc AC mapped ?
 * @mcast_queue:
 */
struct iwl_trans_pcie {
	struct iwl_rx_queue rxq;
	struct work_struct rx_replenish;
	struct iwl_trans *trans;

	/* INT ICT Table */
	__le32 *ict_tbl;
	void *ict_tbl_vir;
	dma_addr_t ict_tbl_dma;
	dma_addr_t aligned_ict_tbl_dma;
	int ict_index;
	u32 inta;
	bool use_ict;
	struct tasklet_struct irq_tasklet;
	struct isr_statistics isr_stats;

	u32 inta_mask;
	u32 scd_base_addr;
	struct iwl_dma_ptr scd_bc_tbls;
	struct iwl_dma_ptr kw;

	const u8 *ac_to_fifo[NUM_IWL_RXON_CTX];
	const u8 *ac_to_queue[NUM_IWL_RXON_CTX];
	u8 mcast_queue[NUM_IWL_RXON_CTX];
};

#define IWL_TRANS_GET_PCIE_TRANS(_iwl_trans) \
	((struct iwl_trans_pcie *) ((_iwl_trans)->trans_specific))

/*****************************************************
* RX
******************************************************/
void iwl_bg_rx_replenish(struct work_struct *data);
void iwl_irq_tasklet(struct iwl_trans *trans);
void iwlagn_rx_replenish(struct iwl_trans *trans);
void iwl_rx_queue_update_write_ptr(struct iwl_trans *trans,
			struct iwl_rx_queue *q);

/*****************************************************
* ICT
******************************************************/
int iwl_reset_ict(struct iwl_trans *trans);
void iwl_disable_ict(struct iwl_trans *trans);
int iwl_alloc_isr_ict(struct iwl_trans *trans);
void iwl_free_isr_ict(struct iwl_trans *trans);
irqreturn_t iwl_isr_ict(int irq, void *data);

/*****************************************************
* TX / HCMD
******************************************************/
void iwl_txq_update_write_ptr(struct iwl_trans *trans,
			struct iwl_tx_queue *txq);
int iwlagn_txq_attach_buf_to_tfd(struct iwl_trans *trans,
				 struct iwl_tx_queue *txq,
				 dma_addr_t addr, u16 len, u8 reset);
int iwl_queue_init(struct iwl_queue *q, int count, int slots_num, u32 id);
int iwl_trans_pcie_send_cmd(struct iwl_trans *trans, struct iwl_host_cmd *cmd);
int __must_check iwl_trans_pcie_send_cmd_pdu(struct iwl_trans *trans, u8 id,
			u32 flags, u16 len, const void *data);
void iwl_tx_cmd_complete(struct iwl_priv *priv, struct iwl_rx_mem_buffer *rxb);
void iwl_trans_txq_update_byte_cnt_tbl(struct iwl_trans *trans,
					   struct iwl_tx_queue *txq,
					   u16 byte_cnt);
int iwl_trans_pcie_txq_agg_disable(struct iwl_priv *priv, u16 txq_id);
void iwl_trans_set_wr_ptrs(struct iwl_trans *trans, int txq_id, u32 index);
void iwl_trans_tx_queue_set_status(struct iwl_priv *priv,
			     struct iwl_tx_queue *txq,
			     int tx_fifo_id, int scd_retry);
void iwl_trans_pcie_txq_agg_setup(struct iwl_priv *priv,
					enum iwl_rxon_context_id ctx,
					int sta_id, int tid, int frame_limit);
void iwlagn_txq_free_tfd(struct iwl_trans *trans, struct iwl_tx_queue *txq,
	int index);
void iwl_tx_queue_reclaim(struct iwl_trans *trans, int txq_id, int index,
			  struct sk_buff_head *skbs);

/*****************************************************
* Error handling
******************************************************/
int iwl_dump_nic_event_log(struct iwl_trans *trans, bool full_log,
			    char **buf, bool display);
int iwl_dump_fh(struct iwl_trans *trans, char **buf, bool display);
void iwl_dump_csr(struct iwl_trans *trans);

static inline void iwl_disable_interrupts(struct iwl_trans *trans)
{
	clear_bit(STATUS_INT_ENABLED, &trans->shrd->status);

	/* disable interrupts from uCode/NIC to host */
	iwl_write32(bus(trans), CSR_INT_MASK, 0x00000000);

	/* acknowledge/clear/reset any interrupts still pending
	 * from uCode or flow handler (Rx/Tx DMA) */
	iwl_write32(bus(trans), CSR_INT, 0xffffffff);
	iwl_write32(bus(trans), CSR_FH_INT_STATUS, 0xffffffff);
	IWL_DEBUG_ISR(trans, "Disabled interrupts\n");
}

static inline void iwl_enable_interrupts(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie =
		IWL_TRANS_GET_PCIE_TRANS(trans);

	IWL_DEBUG_ISR(trans, "Enabling interrupts\n");
	set_bit(STATUS_INT_ENABLED, &trans->shrd->status);
	iwl_write32(bus(trans), CSR_INT_MASK, trans_pcie->inta_mask);
}

#endif /* __iwl_trans_int_pcie_h__ */

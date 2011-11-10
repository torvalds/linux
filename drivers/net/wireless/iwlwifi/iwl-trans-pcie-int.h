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
#include <linux/pci.h>

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

struct iwl_cmd_meta {
	/* only for SYNC commands, iff the reply skb is wanted */
	struct iwl_host_cmd *source;

	u32 flags;

	DEFINE_DMA_UNMAP_ADDR(mapping);
	DEFINE_DMA_UNMAP_LEN(len);
};

/*
 * Generic queue structure
 *
 * Contains common data for Rx and Tx queues.
 *
 * Note the difference between n_bd and n_window: the hardware
 * always assumes 256 descriptors, so n_bd is always 256 (unless
 * there might be HW changes in the future). For the normal TX
 * queues, n_window, which is the size of the software queue data
 * is also 256; however, for the command queue, n_window is only
 * 32 since we don't need so many commands pending. Since the HW
 * still uses 256 BDs for DMA though, n_bd stays 256. As a result,
 * the software buffers (in the variables @meta, @txb in struct
 * iwl_tx_queue) only have 32 entries, while the HW buffers (@tfds
 * in the same struct) have 256.
 * This means that we end up with the following:
 *  HW entries: | 0 | ... | N * 32 | ... | N * 32 + 31 | ... | 255 |
 *  SW entries:           | 0      | ... | 31          |
 * where N is a number between 0 and 7. This means that the SW
 * data is a window overlayed over the HW queue.
 */
struct iwl_queue {
	int n_bd;              /* number of BDs in this queue */
	int write_ptr;       /* 1-st empty entry (index) host_w*/
	int read_ptr;         /* last used entry (index) host_r*/
	/* use for monitoring and recovering the stuck queue */
	dma_addr_t dma_addr;   /* physical addr for BD's */
	int n_window;	       /* safe queue window */
	u32 id;
	int low_mark;	       /* low watermark, resume queue if free
				* space more than this */
	int high_mark;         /* high watermark, stop queue if free
				* space less than this */
};

/**
 * struct iwl_tx_queue - Tx Queue for DMA
 * @q: generic Rx/Tx queue descriptor
 * @bd: base of circular buffer of TFDs
 * @cmd: array of command/TX buffer pointers
 * @meta: array of meta data for each command/tx buffer
 * @dma_addr_cmd: physical address of cmd/tx buffer array
 * @txb: array of per-TFD driver data
 * @time_stamp: time (in jiffies) of last read_ptr change
 * @need_update: indicates need to update read/write index
 * @sched_retry: indicates queue is high-throughput aggregation (HT AGG) enabled
 * @sta_id: valid if sched_retry is set
 * @tid: valid if sched_retry is set
 *
 * A Tx queue consists of circular buffer of BDs (a.k.a. TFDs, transmit frame
 * descriptors) and required locking structures.
 */
#define TFD_TX_CMD_SLOTS 256
#define TFD_CMD_SLOTS 32

struct iwl_tx_queue {
	struct iwl_queue q;
	struct iwl_tfd *tfds;
	struct iwl_device_cmd **cmd;
	struct iwl_cmd_meta *meta;
	struct sk_buff **skbs;
	unsigned long time_stamp;
	u8 need_update;
	u8 sched_retry;
	u8 active;
	u8 swq_id;

	u16 sta_id;
	u16 tid;
};

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
 * @txq: Tx DMA processing queues
 * @txq_ctx_active_msk: what queue is active
 * queue_stopped: tracks what queue is stopped
 * queue_stop_count: tracks what SW queue is stopped
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

	struct iwl_tx_queue *txq;
	unsigned long txq_ctx_active_msk;
#define IWL_MAX_HW_QUEUES	32
	unsigned long queue_stopped[BITS_TO_LONGS(IWL_MAX_HW_QUEUES)];
	atomic_t queue_stop_count[4];
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
void iwl_tx_cmd_complete(struct iwl_trans *trans,
			 struct iwl_rx_mem_buffer *rxb, int handler_status);
void iwl_trans_txq_update_byte_cnt_tbl(struct iwl_trans *trans,
					   struct iwl_tx_queue *txq,
					   u16 byte_cnt);
void iwl_trans_pcie_txq_agg_disable(struct iwl_trans *trans, int txq_id);
int iwl_trans_pcie_tx_agg_disable(struct iwl_trans *trans,
				  enum iwl_rxon_context_id ctx, int sta_id,
				  int tid);
void iwl_trans_set_wr_ptrs(struct iwl_trans *trans, int txq_id, u32 index);
void iwl_trans_tx_queue_set_status(struct iwl_trans *trans,
			     struct iwl_tx_queue *txq,
			     int tx_fifo_id, int scd_retry);
int iwl_trans_pcie_tx_agg_alloc(struct iwl_trans *trans,
				enum iwl_rxon_context_id ctx, int sta_id,
				int tid, u16 *ssn);
void iwl_trans_pcie_tx_agg_setup(struct iwl_trans *trans,
				 enum iwl_rxon_context_id ctx,
				 int sta_id, int tid, int frame_limit);
void iwlagn_txq_free_tfd(struct iwl_trans *trans, struct iwl_tx_queue *txq,
	int index, enum dma_data_direction dma_dir);
int iwl_tx_queue_reclaim(struct iwl_trans *trans, int txq_id, int index,
			 struct sk_buff_head *skbs);
int iwl_queue_space(const struct iwl_queue *q);

/*****************************************************
* Error handling
******************************************************/
int iwl_dump_nic_event_log(struct iwl_trans *trans, bool full_log,
			    char **buf, bool display);
int iwl_dump_fh(struct iwl_trans *trans, char **buf, bool display);
void iwl_dump_csr(struct iwl_trans *trans);

/*****************************************************
* Helpers
******************************************************/
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

/*
 * we have 8 bits used like this:
 *
 * 7 6 5 4 3 2 1 0
 * | | | | | | | |
 * | | | | | | +-+-------- AC queue (0-3)
 * | | | | | |
 * | +-+-+-+-+------------ HW queue ID
 * |
 * +---------------------- unused
 */
static inline void iwl_set_swq_id(struct iwl_tx_queue *txq, u8 ac, u8 hwq)
{
	BUG_ON(ac > 3);   /* only have 2 bits */
	BUG_ON(hwq > 31); /* only use 5 bits */

	txq->swq_id = (hwq << 2) | ac;
}

static inline void iwl_wake_queue(struct iwl_trans *trans,
				  struct iwl_tx_queue *txq, const char *msg)
{
	u8 queue = txq->swq_id;
	u8 ac = queue & 3;
	u8 hwq = (queue >> 2) & 0x1f;
	struct iwl_trans_pcie *trans_pcie =
		IWL_TRANS_GET_PCIE_TRANS(trans);

	if (test_and_clear_bit(hwq, trans_pcie->queue_stopped)) {
		if (atomic_dec_return(&trans_pcie->queue_stop_count[ac]) <= 0) {
			iwl_wake_sw_queue(priv(trans), ac);
			IWL_DEBUG_TX_QUEUES(trans, "Wake hwq %d ac %d. %s",
					    hwq, ac, msg);
		} else {
			IWL_DEBUG_TX_QUEUES(trans, "Don't wake hwq %d ac %d"
					    " stop count %d. %s",
					    hwq, ac, atomic_read(&trans_pcie->
					    queue_stop_count[ac]), msg);
		}
	}
}

static inline void iwl_stop_queue(struct iwl_trans *trans,
				  struct iwl_tx_queue *txq, const char *msg)
{
	u8 queue = txq->swq_id;
	u8 ac = queue & 3;
	u8 hwq = (queue >> 2) & 0x1f;
	struct iwl_trans_pcie *trans_pcie =
		IWL_TRANS_GET_PCIE_TRANS(trans);

	if (!test_and_set_bit(hwq, trans_pcie->queue_stopped)) {
		if (atomic_inc_return(&trans_pcie->queue_stop_count[ac]) > 0) {
			iwl_stop_sw_queue(priv(trans), ac);
			IWL_DEBUG_TX_QUEUES(trans, "Stop hwq %d ac %d"
					    " stop count %d. %s",
					    hwq, ac, atomic_read(&trans_pcie->
					    queue_stop_count[ac]), msg);
		} else {
			IWL_DEBUG_TX_QUEUES(trans, "Don't stop hwq %d ac %d"
					    " stop count %d. %s",
					    hwq, ac, atomic_read(&trans_pcie->
					    queue_stop_count[ac]), msg);
		}
	} else {
		IWL_DEBUG_TX_QUEUES(trans, "stop hwq %d, but it is stopped/ %s",
				    hwq, msg);
	}
}

#ifdef ieee80211_stop_queue
#undef ieee80211_stop_queue
#endif

#define ieee80211_stop_queue DO_NOT_USE_ieee80211_stop_queue

#ifdef ieee80211_wake_queue
#undef ieee80211_wake_queue
#endif

#define ieee80211_wake_queue DO_NOT_USE_ieee80211_wake_queue

static inline void iwl_txq_ctx_activate(struct iwl_trans_pcie *trans_pcie,
					int txq_id)
{
	set_bit(txq_id, &trans_pcie->txq_ctx_active_msk);
}

static inline void iwl_txq_ctx_deactivate(struct iwl_trans_pcie *trans_pcie,
					  int txq_id)
{
	clear_bit(txq_id, &trans_pcie->txq_ctx_active_msk);
}

static inline int iwl_queue_used(const struct iwl_queue *q, int i)
{
	return q->write_ptr >= q->read_ptr ?
		(i >= q->read_ptr && i < q->write_ptr) :
		!(i < q->read_ptr && i >= q->write_ptr);
}

static inline u8 get_cmd_index(struct iwl_queue *q, u32 index)
{
	return index & (q->n_window - 1);
}

#define IWL_TX_FIFO_BK		0	/* shared */
#define IWL_TX_FIFO_BE		1
#define IWL_TX_FIFO_VI		2	/* shared */
#define IWL_TX_FIFO_VO		3
#define IWL_TX_FIFO_BK_IPAN	IWL_TX_FIFO_BK
#define IWL_TX_FIFO_BE_IPAN	4
#define IWL_TX_FIFO_VI_IPAN	IWL_TX_FIFO_VI
#define IWL_TX_FIFO_VO_IPAN	5
/* re-uses the VO FIFO, uCode will properly flush/schedule */
#define IWL_TX_FIFO_AUX		5
#define IWL_TX_FIFO_UNUSED	-1

/* AUX (TX during scan dwell) queue */
#define IWL_AUX_QUEUE		10

#endif /* __iwl_trans_int_pcie_h__ */

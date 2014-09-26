/******************************************************************************
 *
 * Copyright(c) 2003 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
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
#include <linux/wait.h>
#include <linux/pci.h>
#include <linux/timer.h>

#include "iwl-fh.h"
#include "iwl-csr.h"
#include "iwl-trans.h"
#include "iwl-debug.h"
#include "iwl-io.h"
#include "iwl-op-mode.h"

struct iwl_host_cmd;

/*This file includes the declaration that are internal to the
 * trans_pcie layer */

struct iwl_rx_mem_buffer {
	dma_addr_t page_dma;
	struct page *page;
	struct list_head list;
};

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
 * struct iwl_rxq - Rx queue
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
struct iwl_rxq {
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
	bool need_update;
	struct iwl_rb_status *rb_stts;
	dma_addr_t rb_stts_dma;
	spinlock_t lock;
};

struct iwl_dma_ptr {
	dma_addr_t dma;
	void *addr;
	size_t size;
};

/**
 * iwl_queue_inc_wrap - increment queue index, wrap back to beginning
 * @index -- current index
 */
static inline int iwl_queue_inc_wrap(int index)
{
	return ++index & (TFD_QUEUE_SIZE_MAX - 1);
}

/**
 * iwl_queue_dec_wrap - decrement queue index, wrap back to end
 * @index -- current index
 */
static inline int iwl_queue_dec_wrap(int index)
{
	return --index & (TFD_QUEUE_SIZE_MAX - 1);
}

struct iwl_cmd_meta {
	/* only for SYNC commands, iff the reply skb is wanted */
	struct iwl_host_cmd *source;
	u32 flags;
};

/*
 * Generic queue structure
 *
 * Contains common data for Rx and Tx queues.
 *
 * Note the difference between TFD_QUEUE_SIZE_MAX and n_window: the hardware
 * always assumes 256 descriptors, so TFD_QUEUE_SIZE_MAX is always 256 (unless
 * there might be HW changes in the future). For the normal TX
 * queues, n_window, which is the size of the software queue data
 * is also 256; however, for the command queue, n_window is only
 * 32 since we don't need so many commands pending. Since the HW
 * still uses 256 BDs for DMA though, TFD_QUEUE_SIZE_MAX stays 256. As a result,
 * the software buffers (in the variables @meta, @txb in struct
 * iwl_txq) only have 32 entries, while the HW buffers (@tfds in
 * the same struct) have 256.
 * This means that we end up with the following:
 *  HW entries: | 0 | ... | N * 32 | ... | N * 32 + 31 | ... | 255 |
 *  SW entries:           | 0      | ... | 31          |
 * where N is a number between 0 and 7. This means that the SW
 * data is a window overlayed over the HW queue.
 */
struct iwl_queue {
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

#define TFD_TX_CMD_SLOTS 256
#define TFD_CMD_SLOTS 32

/*
 * The FH will write back to the first TB only, so we need
 * to copy some data into the buffer regardless of whether
 * it should be mapped or not. This indicates how big the
 * first TB must be to include the scratch buffer. Since
 * the scratch is 4 bytes at offset 12, it's 16 now. If we
 * make it bigger then allocations will be bigger and copy
 * slower, so that's probably not useful.
 */
#define IWL_HCMD_SCRATCHBUF_SIZE	16

struct iwl_pcie_txq_entry {
	struct iwl_device_cmd *cmd;
	struct sk_buff *skb;
	/* buffer to free after command completes */
	const void *free_buf;
	struct iwl_cmd_meta meta;
};

struct iwl_pcie_txq_scratch_buf {
	struct iwl_cmd_header hdr;
	u8 buf[8];
	__le32 scratch;
};

/**
 * struct iwl_txq - Tx Queue for DMA
 * @q: generic Rx/Tx queue descriptor
 * @tfds: transmit frame descriptors (DMA memory)
 * @scratchbufs: start of command headers, including scratch buffers, for
 *	the writeback -- this is DMA memory and an array holding one buffer
 *	for each command on the queue
 * @scratchbufs_dma: DMA address for the scratchbufs start
 * @entries: transmit entries (driver state)
 * @lock: queue lock
 * @stuck_timer: timer that fires if queue gets stuck
 * @trans_pcie: pointer back to transport (for timer)
 * @need_update: indicates need to update read/write index
 * @active: stores if queue is active
 * @ampdu: true if this queue is an ampdu queue for an specific RA/TID
 *
 * A Tx queue consists of circular buffer of BDs (a.k.a. TFDs, transmit frame
 * descriptors) and required locking structures.
 */
struct iwl_txq {
	struct iwl_queue q;
	struct iwl_tfd *tfds;
	struct iwl_pcie_txq_scratch_buf *scratchbufs;
	dma_addr_t scratchbufs_dma;
	struct iwl_pcie_txq_entry *entries;
	spinlock_t lock;
	struct timer_list stuck_timer;
	struct iwl_trans_pcie *trans_pcie;
	bool need_update;
	u8 active;
	bool ampdu;
};

static inline dma_addr_t
iwl_pcie_get_scratchbuf_dma(struct iwl_txq *txq, int idx)
{
	return txq->scratchbufs_dma +
	       sizeof(struct iwl_pcie_txq_scratch_buf) * idx;
}

/**
 * struct iwl_trans_pcie - PCIe transport specific data
 * @rxq: all the RX queue data
 * @rx_replenish: work that will be called when buffers need to be allocated
 * @drv - pointer to iwl_drv
 * @trans: pointer to the generic transport area
 * @scd_base_addr: scheduler sram base address in SRAM
 * @scd_bc_tbls: pointer to the byte count table of the scheduler
 * @kw: keep warm address
 * @pci_dev: basic pci-network driver stuff
 * @hw_base: pci hardware address support
 * @ucode_write_complete: indicates that the ucode has been copied.
 * @ucode_write_waitq: wait queue for uCode load
 * @cmd_queue - command queue number
 * @rx_buf_size_8k: 8 kB RX buffer size
 * @bc_table_dword: true if the BC table expects DWORD (as opposed to bytes)
 * @scd_set_active: should the transport configure the SCD for HCMD queue
 * @rx_page_order: page order for receive buffer size
 * @wd_timeout: queue watchdog timeout (jiffies)
 * @reg_lock: protect hw register access
 * @cmd_in_flight: true when we have a host command in flight
 * @fw_mon_phys: physical address of the buffer for the firmware monitor
 * @fw_mon_page: points to the first page of the buffer for the firmware monitor
 * @fw_mon_size: size of the buffer for the firmware monitor
 */
struct iwl_trans_pcie {
	struct iwl_rxq rxq;
	struct work_struct rx_replenish;
	struct iwl_trans *trans;
	struct iwl_drv *drv;

	struct net_device napi_dev;
	struct napi_struct napi;

	/* INT ICT Table */
	__le32 *ict_tbl;
	dma_addr_t ict_tbl_dma;
	int ict_index;
	bool use_ict;
	struct isr_statistics isr_stats;

	spinlock_t irq_lock;
	u32 inta_mask;
	u32 scd_base_addr;
	struct iwl_dma_ptr scd_bc_tbls;
	struct iwl_dma_ptr kw;

	struct iwl_txq *txq;
	unsigned long queue_used[BITS_TO_LONGS(IWL_MAX_HW_QUEUES)];
	unsigned long queue_stopped[BITS_TO_LONGS(IWL_MAX_HW_QUEUES)];

	/* PCI bus related data */
	struct pci_dev *pci_dev;
	void __iomem *hw_base;

	bool ucode_write_complete;
	wait_queue_head_t ucode_write_waitq;
	wait_queue_head_t wait_command_queue;

	u8 cmd_queue;
	u8 cmd_fifo;
	u8 n_no_reclaim_cmds;
	u8 no_reclaim_cmds[MAX_NO_RECLAIM_CMDS];

	bool rx_buf_size_8k;
	bool bc_table_dword;
	bool scd_set_active;
	u32 rx_page_order;

	const char *const *command_names;

	/* queue watchdog */
	unsigned long wd_timeout;

	/*protect hw register */
	spinlock_t reg_lock;
	bool cmd_in_flight;

	dma_addr_t fw_mon_phys;
	struct page *fw_mon_page;
	u32 fw_mon_size;
};

#define IWL_TRANS_GET_PCIE_TRANS(_iwl_trans) \
	((struct iwl_trans_pcie *) ((_iwl_trans)->trans_specific))

static inline struct iwl_trans *
iwl_trans_pcie_get_trans(struct iwl_trans_pcie *trans_pcie)
{
	return container_of((void *)trans_pcie, struct iwl_trans,
			    trans_specific);
}

/*
 * Convention: trans API functions: iwl_trans_pcie_XXX
 *	Other functions: iwl_pcie_XXX
 */
struct iwl_trans *iwl_trans_pcie_alloc(struct pci_dev *pdev,
				       const struct pci_device_id *ent,
				       const struct iwl_cfg *cfg);
void iwl_trans_pcie_free(struct iwl_trans *trans);

/*****************************************************
* RX
******************************************************/
int iwl_pcie_rx_init(struct iwl_trans *trans);
irqreturn_t iwl_pcie_irq_handler(int irq, void *dev_id);
int iwl_pcie_rx_stop(struct iwl_trans *trans);
void iwl_pcie_rx_free(struct iwl_trans *trans);

/*****************************************************
* ICT - interrupt handling
******************************************************/
irqreturn_t iwl_pcie_isr(int irq, void *data);
int iwl_pcie_alloc_ict(struct iwl_trans *trans);
void iwl_pcie_free_ict(struct iwl_trans *trans);
void iwl_pcie_reset_ict(struct iwl_trans *trans);
void iwl_pcie_disable_ict(struct iwl_trans *trans);

/*****************************************************
* TX / HCMD
******************************************************/
int iwl_pcie_tx_init(struct iwl_trans *trans);
void iwl_pcie_tx_start(struct iwl_trans *trans, u32 scd_base_addr);
int iwl_pcie_tx_stop(struct iwl_trans *trans);
void iwl_pcie_tx_free(struct iwl_trans *trans);
void iwl_trans_pcie_txq_enable(struct iwl_trans *trans, int queue, u16 ssn,
			       const struct iwl_trans_txq_scd_cfg *cfg);
void iwl_trans_pcie_txq_disable(struct iwl_trans *trans, int queue,
				bool configure_scd);
int iwl_trans_pcie_tx(struct iwl_trans *trans, struct sk_buff *skb,
		      struct iwl_device_cmd *dev_cmd, int txq_id);
void iwl_pcie_txq_check_wrptrs(struct iwl_trans *trans);
int iwl_trans_pcie_send_hcmd(struct iwl_trans *trans, struct iwl_host_cmd *cmd);
void iwl_pcie_hcmd_complete(struct iwl_trans *trans,
			    struct iwl_rx_cmd_buffer *rxb, int handler_status);
void iwl_trans_pcie_reclaim(struct iwl_trans *trans, int txq_id, int ssn,
			    struct sk_buff_head *skbs);
void iwl_trans_pcie_tx_reset(struct iwl_trans *trans);

static inline u16 iwl_pcie_tfd_tb_get_len(struct iwl_tfd *tfd, u8 idx)
{
	struct iwl_tfd_tb *tb = &tfd->tbs[idx];

	return le16_to_cpu(tb->hi_n_len) >> 4;
}

/*****************************************************
* Error handling
******************************************************/
void iwl_pcie_dump_csr(struct iwl_trans *trans);

/*****************************************************
* Helpers
******************************************************/
static inline void iwl_disable_interrupts(struct iwl_trans *trans)
{
	clear_bit(STATUS_INT_ENABLED, &trans->status);

	/* disable interrupts from uCode/NIC to host */
	iwl_write32(trans, CSR_INT_MASK, 0x00000000);

	/* acknowledge/clear/reset any interrupts still pending
	 * from uCode or flow handler (Rx/Tx DMA) */
	iwl_write32(trans, CSR_INT, 0xffffffff);
	iwl_write32(trans, CSR_FH_INT_STATUS, 0xffffffff);
	IWL_DEBUG_ISR(trans, "Disabled interrupts\n");
}

static inline void iwl_enable_interrupts(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	IWL_DEBUG_ISR(trans, "Enabling interrupts\n");
	set_bit(STATUS_INT_ENABLED, &trans->status);
	trans_pcie->inta_mask = CSR_INI_SET_MASK;
	iwl_write32(trans, CSR_INT_MASK, trans_pcie->inta_mask);
}

static inline void iwl_enable_rfkill_int(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	IWL_DEBUG_ISR(trans, "Enabling rfkill interrupt\n");
	trans_pcie->inta_mask = CSR_INT_BIT_RF_KILL;
	iwl_write32(trans, CSR_INT_MASK, trans_pcie->inta_mask);
}

static inline void iwl_wake_queue(struct iwl_trans *trans,
				  struct iwl_txq *txq)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (test_and_clear_bit(txq->q.id, trans_pcie->queue_stopped)) {
		IWL_DEBUG_TX_QUEUES(trans, "Wake hwq %d\n", txq->q.id);
		iwl_op_mode_queue_not_full(trans->op_mode, txq->q.id);
	}
}

static inline void iwl_stop_queue(struct iwl_trans *trans,
				  struct iwl_txq *txq)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (!test_and_set_bit(txq->q.id, trans_pcie->queue_stopped)) {
		iwl_op_mode_queue_full(trans->op_mode, txq->q.id);
		IWL_DEBUG_TX_QUEUES(trans, "Stop hwq %d\n", txq->q.id);
	} else
		IWL_DEBUG_TX_QUEUES(trans, "hwq %d already stopped\n",
				    txq->q.id);
}

static inline bool iwl_queue_used(const struct iwl_queue *q, int i)
{
	return q->write_ptr >= q->read_ptr ?
		(i >= q->read_ptr && i < q->write_ptr) :
		!(i < q->read_ptr && i >= q->write_ptr);
}

static inline u8 get_cmd_index(struct iwl_queue *q, u32 index)
{
	return index & (q->n_window - 1);
}

static inline const char *get_cmd_string(struct iwl_trans_pcie *trans_pcie,
					 u8 cmd)
{
	if (!trans_pcie->command_names || !trans_pcie->command_names[cmd])
		return "UNKNOWN";
	return trans_pcie->command_names[cmd];
}

static inline bool iwl_is_rfkill_set(struct iwl_trans *trans)
{
	return !(iwl_read32(trans, CSR_GP_CNTRL) &
		CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW);
}

static inline void __iwl_trans_pcie_set_bits_mask(struct iwl_trans *trans,
						  u32 reg, u32 mask, u32 value)
{
	u32 v;

#ifdef CONFIG_IWLWIFI_DEBUG
	WARN_ON_ONCE(value & ~mask);
#endif

	v = iwl_read32(trans, reg);
	v &= ~mask;
	v |= value;
	iwl_write32(trans, reg, v);
}

static inline void __iwl_trans_pcie_clear_bit(struct iwl_trans *trans,
					      u32 reg, u32 mask)
{
	__iwl_trans_pcie_set_bits_mask(trans, reg, mask, 0);
}

static inline void __iwl_trans_pcie_set_bit(struct iwl_trans *trans,
					    u32 reg, u32 mask)
{
	__iwl_trans_pcie_set_bits_mask(trans, reg, mask, mask);
}

void iwl_trans_pcie_rf_kill(struct iwl_trans *trans, bool state);

#endif /* __iwl_trans_int_pcie_h__ */

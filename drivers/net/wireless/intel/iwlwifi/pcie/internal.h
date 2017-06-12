/******************************************************************************
 *
 * Copyright(c) 2003 - 2015 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
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
 *  Intel Linux Wireless <linuxwifi@intel.com>
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
#include <linux/cpu.h>

#include "iwl-fh.h"
#include "iwl-csr.h"
#include "iwl-trans.h"
#include "iwl-debug.h"
#include "iwl-io.h"
#include "iwl-op-mode.h"

/* We need 2 entries for the TX command and header, and another one might
 * be needed for potential data in the SKB's head. The remaining ones can
 * be used for frags.
 */
#define IWL_PCIE_MAX_FRAGS(x) (x->max_tbs - 3)

/*
 * RX related structures and functions
 */
#define RX_NUM_QUEUES 1
#define RX_POST_REQ_ALLOC 2
#define RX_CLAIM_REQ_ALLOC 8
#define RX_PENDING_WATERMARK 16

struct iwl_host_cmd;

/*This file includes the declaration that are internal to the
 * trans_pcie layer */

/**
 * struct iwl_rx_mem_buffer
 * @page_dma: bus address of rxb page
 * @page: driver's pointer to the rxb page
 * @invalid: rxb is in driver ownership - not owned by HW
 * @vid: index of this rxb in the global table
 */
struct iwl_rx_mem_buffer {
	dma_addr_t page_dma;
	struct page *page;
	u16 vid;
	bool invalid;
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
 * @id: queue index
 * @bd: driver's pointer to buffer of receive buffer descriptors (rbd).
 *	Address size is 32 bit in pre-9000 devices and 64 bit in 9000 devices.
 * @bd_dma: bus address of buffer of receive buffer descriptors (rbd)
 * @ubd: driver's pointer to buffer of used receive buffer descriptors (rbd)
 * @ubd_dma: physical address of buffer of used receive buffer descriptors (rbd)
 * @read: Shared index to newest available Rx buffer
 * @write: Shared index to oldest written Rx packet
 * @free_count: Number of pre-allocated buffers in rx_free
 * @used_count: Number of RBDs handled to allocator to use for allocation
 * @write_actual:
 * @rx_free: list of RBDs with allocated RB ready for use
 * @rx_used: list of RBDs with no RB attached
 * @need_update: flag to indicate we need to update read/write index
 * @rb_stts: driver's pointer to receive buffer status
 * @rb_stts_dma: bus address of receive buffer status
 * @lock:
 * @queue: actual rx queue. Not used for multi-rx queue.
 *
 * NOTE:  rx_free and rx_used are used as a FIFO for iwl_rx_mem_buffers
 */
struct iwl_rxq {
	int id;
	void *bd;
	dma_addr_t bd_dma;
	__le32 *used_bd;
	dma_addr_t used_bd_dma;
	u32 read;
	u32 write;
	u32 free_count;
	u32 used_count;
	u32 write_actual;
	u32 queue_size;
	struct list_head rx_free;
	struct list_head rx_used;
	bool need_update;
	struct iwl_rb_status *rb_stts;
	dma_addr_t rb_stts_dma;
	spinlock_t lock;
	struct napi_struct napi;
	struct iwl_rx_mem_buffer *queue[RX_QUEUE_SIZE];
};

/**
 * struct iwl_rb_allocator - Rx allocator
 * @req_pending: number of requests the allcator had not processed yet
 * @req_ready: number of requests honored and ready for claiming
 * @rbd_allocated: RBDs with pages allocated and ready to be handled to
 *	the queue. This is a list of &struct iwl_rx_mem_buffer
 * @rbd_empty: RBDs with no page attached for allocator use. This is a list
 *	of &struct iwl_rx_mem_buffer
 * @lock: protects the rbd_allocated and rbd_empty lists
 * @alloc_wq: work queue for background calls
 * @rx_alloc: work struct for background calls
 */
struct iwl_rb_allocator {
	atomic_t req_pending;
	atomic_t req_ready;
	struct list_head rbd_allocated;
	struct list_head rbd_empty;
	spinlock_t lock;
	struct workqueue_struct *alloc_wq;
	struct work_struct rx_alloc;
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
	u32 tbs;
};


#define TFD_TX_CMD_SLOTS 256
#define TFD_CMD_SLOTS 32

/*
 * The FH will write back to the first TB only, so we need to copy some data
 * into the buffer regardless of whether it should be mapped or not.
 * This indicates how big the first TB must be to include the scratch buffer
 * and the assigned PN.
 * Since PN location is 8 bytes at offset 12, it's 20 now.
 * If we make it bigger then allocations will be bigger and copy slower, so
 * that's probably not useful.
 */
#define IWL_FIRST_TB_SIZE	20
#define IWL_FIRST_TB_SIZE_ALIGN ALIGN(IWL_FIRST_TB_SIZE, 64)

struct iwl_pcie_txq_entry {
	struct iwl_device_cmd *cmd;
	struct sk_buff *skb;
	/* buffer to free after command completes */
	const void *free_buf;
	struct iwl_cmd_meta meta;
};

struct iwl_pcie_first_tb_buf {
	u8 buf[IWL_FIRST_TB_SIZE_ALIGN];
};

/**
 * struct iwl_txq - Tx Queue for DMA
 * @q: generic Rx/Tx queue descriptor
 * @tfds: transmit frame descriptors (DMA memory)
 * @first_tb_bufs: start of command headers, including scratch buffers, for
 *	the writeback -- this is DMA memory and an array holding one buffer
 *	for each command on the queue
 * @first_tb_dma: DMA address for the first_tb_bufs start
 * @entries: transmit entries (driver state)
 * @lock: queue lock
 * @stuck_timer: timer that fires if queue gets stuck
 * @trans_pcie: pointer back to transport (for timer)
 * @need_update: indicates need to update read/write index
 * @ampdu: true if this queue is an ampdu queue for an specific RA/TID
 * @wd_timeout: queue watchdog timeout (jiffies) - per queue
 * @frozen: tx stuck queue timer is frozen
 * @frozen_expiry_remainder: remember how long until the timer fires
 * @bc_tbl: byte count table of the queue (relevant only for gen2 transport)
 * @write_ptr: 1-st empty entry (index) host_w
 * @read_ptr: last used entry (index) host_r
 * @dma_addr:  physical addr for BD's
 * @n_window: safe queue window
 * @id: queue id
 * @low_mark: low watermark, resume queue if free space more than this
 * @high_mark: high watermark, stop queue if free space less than this
 *
 * A Tx queue consists of circular buffer of BDs (a.k.a. TFDs, transmit frame
 * descriptors) and required locking structures.
 *
 * Note the difference between TFD_QUEUE_SIZE_MAX and n_window: the hardware
 * always assumes 256 descriptors, so TFD_QUEUE_SIZE_MAX is always 256 (unless
 * there might be HW changes in the future). For the normal TX
 * queues, n_window, which is the size of the software queue data
 * is also 256; however, for the command queue, n_window is only
 * 32 since we don't need so many commands pending. Since the HW
 * still uses 256 BDs for DMA though, TFD_QUEUE_SIZE_MAX stays 256.
 * This means that we end up with the following:
 *  HW entries: | 0 | ... | N * 32 | ... | N * 32 + 31 | ... | 255 |
 *  SW entries:           | 0      | ... | 31          |
 * where N is a number between 0 and 7. This means that the SW
 * data is a window overlayed over the HW queue.
 */
struct iwl_txq {
	void *tfds;
	struct iwl_pcie_first_tb_buf *first_tb_bufs;
	dma_addr_t first_tb_dma;
	struct iwl_pcie_txq_entry *entries;
	spinlock_t lock;
	unsigned long frozen_expiry_remainder;
	struct timer_list stuck_timer;
	struct iwl_trans_pcie *trans_pcie;
	bool need_update;
	bool frozen;
	bool ampdu;
	int block;
	unsigned long wd_timeout;
	struct sk_buff_head overflow_q;
	struct iwl_dma_ptr bc_tbl;

	int write_ptr;
	int read_ptr;
	dma_addr_t dma_addr;
	int n_window;
	u32 id;
	int low_mark;
	int high_mark;
};

static inline dma_addr_t
iwl_pcie_get_first_tb_dma(struct iwl_txq *txq, int idx)
{
	return txq->first_tb_dma +
	       sizeof(struct iwl_pcie_first_tb_buf) * idx;
}

struct iwl_tso_hdr_page {
	struct page *page;
	u8 *pos;
};

/**
 * enum iwl_shared_irq_flags - level of sharing for irq
 * @IWL_SHARED_IRQ_NON_RX: interrupt vector serves non rx causes.
 * @IWL_SHARED_IRQ_FIRST_RSS: interrupt vector serves first RSS queue.
 */
enum iwl_shared_irq_flags {
	IWL_SHARED_IRQ_NON_RX		= BIT(0),
	IWL_SHARED_IRQ_FIRST_RSS	= BIT(1),
};

/**
 * struct iwl_dram_data
 * @physical: page phy pointer
 * @block: pointer to the allocated block/page
 * @size: size of the block/page
 */
struct iwl_dram_data {
	dma_addr_t physical;
	void *block;
	int size;
};

/**
 * struct iwl_self_init_dram - dram data used by self init process
 * @fw: lmac and umac dram data
 * @fw_cnt: total number of items in array
 * @paging: paging dram data
 * @paging_cnt: total number of items in array
 */
struct iwl_self_init_dram {
	struct iwl_dram_data *fw;
	int fw_cnt;
	struct iwl_dram_data *paging;
	int paging_cnt;
};

/**
 * struct iwl_trans_pcie - PCIe transport specific data
 * @rxq: all the RX queue data
 * @rx_pool: initial pool of iwl_rx_mem_buffer for all the queues
 * @global_table: table mapping received VID from hw to rxb
 * @rba: allocator for RX replenishing
 * @ctxt_info: context information for FW self init
 * @ctxt_info_dma_addr: dma addr of context information
 * @init_dram: DRAM data of firmware image (including paging).
 *	Context information addresses will be taken from here.
 *	This is driver's local copy for keeping track of size and
 *	count for allocating and freeing the memory.
 * @trans: pointer to the generic transport area
 * @scd_base_addr: scheduler sram base address in SRAM
 * @scd_bc_tbls: pointer to the byte count table of the scheduler
 * @kw: keep warm address
 * @pci_dev: basic pci-network driver stuff
 * @hw_base: pci hardware address support
 * @ucode_write_complete: indicates that the ucode has been copied.
 * @ucode_write_waitq: wait queue for uCode load
 * @cmd_queue - command queue number
 * @rx_buf_size: Rx buffer size
 * @bc_table_dword: true if the BC table expects DWORD (as opposed to bytes)
 * @scd_set_active: should the transport configure the SCD for HCMD queue
 * @sw_csum_tx: if true, then the transport will compute the csum of the TXed
 *	frame.
 * @rx_page_order: page order for receive buffer size
 * @reg_lock: protect hw register access
 * @mutex: to protect stop_device / start_fw / start_hw
 * @cmd_in_flight: true when we have a host command in flight
 * @fw_mon_phys: physical address of the buffer for the firmware monitor
 * @fw_mon_page: points to the first page of the buffer for the firmware monitor
 * @fw_mon_size: size of the buffer for the firmware monitor
 * @msix_entries: array of MSI-X entries
 * @msix_enabled: true if managed to enable MSI-X
 * @shared_vec_mask: the type of causes the shared vector handles
 *	(see iwl_shared_irq_flags).
 * @alloc_vecs: the number of interrupt vectors allocated by the OS
 * @def_irq: default irq for non rx causes
 * @fh_init_mask: initial unmasked fh causes
 * @hw_init_mask: initial unmasked hw causes
 * @fh_mask: current unmasked fh causes
 * @hw_mask: current unmasked hw causes
 */
struct iwl_trans_pcie {
	struct iwl_rxq *rxq;
	struct iwl_rx_mem_buffer rx_pool[RX_POOL_SIZE];
	struct iwl_rx_mem_buffer *global_table[RX_POOL_SIZE];
	struct iwl_rb_allocator rba;
	struct iwl_context_info *ctxt_info;
	dma_addr_t ctxt_info_dma_addr;
	struct iwl_self_init_dram init_dram;
	struct iwl_trans *trans;

	struct net_device napi_dev;

	struct __percpu iwl_tso_hdr_page *tso_hdr_page;

	/* INT ICT Table */
	__le32 *ict_tbl;
	dma_addr_t ict_tbl_dma;
	int ict_index;
	bool use_ict;
	bool is_down;
	struct isr_statistics isr_stats;

	spinlock_t irq_lock;
	struct mutex mutex;
	u32 inta_mask;
	u32 scd_base_addr;
	struct iwl_dma_ptr scd_bc_tbls;
	struct iwl_dma_ptr kw;

	struct iwl_txq *txq_memory;
	struct iwl_txq *txq[IWL_MAX_TVQM_QUEUES];
	unsigned long queue_used[BITS_TO_LONGS(IWL_MAX_TVQM_QUEUES)];
	unsigned long queue_stopped[BITS_TO_LONGS(IWL_MAX_TVQM_QUEUES)];

	/* PCI bus related data */
	struct pci_dev *pci_dev;
	void __iomem *hw_base;

	bool ucode_write_complete;
	wait_queue_head_t ucode_write_waitq;
	wait_queue_head_t wait_command_queue;
	wait_queue_head_t d0i3_waitq;

	u8 page_offs, dev_cmd_offs;

	u8 cmd_queue;
	u8 cmd_fifo;
	unsigned int cmd_q_wdg_timeout;
	u8 n_no_reclaim_cmds;
	u8 no_reclaim_cmds[MAX_NO_RECLAIM_CMDS];
	u8 max_tbs;
	u16 tfd_size;

	enum iwl_amsdu_size rx_buf_size;
	bool bc_table_dword;
	bool scd_set_active;
	bool sw_csum_tx;
	u32 rx_page_order;

	/*protect hw register */
	spinlock_t reg_lock;
	bool cmd_hold_nic_awake;
	bool ref_cmd_in_flight;

	dma_addr_t fw_mon_phys;
	struct page *fw_mon_page;
	u32 fw_mon_size;

	struct msix_entry msix_entries[IWL_MAX_RX_HW_QUEUES];
	bool msix_enabled;
	u8 shared_vec_mask;
	u32 alloc_vecs;
	u32 def_irq;
	u32 fh_init_mask;
	u32 hw_init_mask;
	u32 fh_mask;
	u32 hw_mask;
	cpumask_t affinity_mask[IWL_MAX_RX_HW_QUEUES];
};

static inline struct iwl_trans_pcie *
IWL_TRANS_GET_PCIE_TRANS(struct iwl_trans *trans)
{
	return (void *)trans->trans_specific;
}

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
int iwl_pcie_gen2_rx_init(struct iwl_trans *trans);
irqreturn_t iwl_pcie_msix_isr(int irq, void *data);
irqreturn_t iwl_pcie_irq_handler(int irq, void *dev_id);
irqreturn_t iwl_pcie_irq_msix_handler(int irq, void *dev_id);
irqreturn_t iwl_pcie_irq_rx_msix_handler(int irq, void *dev_id);
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
int iwl_pcie_gen2_tx_init(struct iwl_trans *trans);
void iwl_pcie_tx_start(struct iwl_trans *trans, u32 scd_base_addr);
int iwl_pcie_tx_stop(struct iwl_trans *trans);
void iwl_pcie_tx_free(struct iwl_trans *trans);
void iwl_trans_pcie_txq_enable(struct iwl_trans *trans, int queue, u16 ssn,
			       const struct iwl_trans_txq_scd_cfg *cfg,
			       unsigned int wdg_timeout);
void iwl_trans_pcie_txq_disable(struct iwl_trans *trans, int queue,
				bool configure_scd);
void iwl_trans_pcie_txq_set_shared_mode(struct iwl_trans *trans, u32 txq_id,
					bool shared_mode);
void iwl_trans_pcie_log_scd_error(struct iwl_trans *trans,
				  struct iwl_txq *txq);
int iwl_trans_pcie_tx(struct iwl_trans *trans, struct sk_buff *skb,
		      struct iwl_device_cmd *dev_cmd, int txq_id);
void iwl_pcie_txq_check_wrptrs(struct iwl_trans *trans);
int iwl_trans_pcie_send_hcmd(struct iwl_trans *trans, struct iwl_host_cmd *cmd);
void iwl_pcie_hcmd_complete(struct iwl_trans *trans,
			    struct iwl_rx_cmd_buffer *rxb);
void iwl_trans_pcie_reclaim(struct iwl_trans *trans, int txq_id, int ssn,
			    struct sk_buff_head *skbs);
void iwl_trans_pcie_tx_reset(struct iwl_trans *trans);

static inline u16 iwl_pcie_tfd_tb_get_len(struct iwl_trans *trans, void *_tfd,
					  u8 idx)
{
	if (trans->cfg->use_tfh) {
		struct iwl_tfh_tfd *tfd = _tfd;
		struct iwl_tfh_tb *tb = &tfd->tbs[idx];

		return le16_to_cpu(tb->tb_len);
	} else {
		struct iwl_tfd *tfd = _tfd;
		struct iwl_tfd_tb *tb = &tfd->tbs[idx];

		return le16_to_cpu(tb->hi_n_len) >> 4;
	}
}

/*****************************************************
* Error handling
******************************************************/
void iwl_pcie_dump_csr(struct iwl_trans *trans);

/*****************************************************
* Helpers
******************************************************/
static inline void _iwl_disable_interrupts(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	clear_bit(STATUS_INT_ENABLED, &trans->status);
	if (!trans_pcie->msix_enabled) {
		/* disable interrupts from uCode/NIC to host */
		iwl_write32(trans, CSR_INT_MASK, 0x00000000);

		/* acknowledge/clear/reset any interrupts still pending
		 * from uCode or flow handler (Rx/Tx DMA) */
		iwl_write32(trans, CSR_INT, 0xffffffff);
		iwl_write32(trans, CSR_FH_INT_STATUS, 0xffffffff);
	} else {
		/* disable all the interrupt we might use */
		iwl_write32(trans, CSR_MSIX_FH_INT_MASK_AD,
			    trans_pcie->fh_init_mask);
		iwl_write32(trans, CSR_MSIX_HW_INT_MASK_AD,
			    trans_pcie->hw_init_mask);
	}
	IWL_DEBUG_ISR(trans, "Disabled interrupts\n");
}

static inline void iwl_disable_interrupts(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	spin_lock(&trans_pcie->irq_lock);
	_iwl_disable_interrupts(trans);
	spin_unlock(&trans_pcie->irq_lock);
}

static inline void _iwl_enable_interrupts(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	IWL_DEBUG_ISR(trans, "Enabling interrupts\n");
	set_bit(STATUS_INT_ENABLED, &trans->status);
	if (!trans_pcie->msix_enabled) {
		trans_pcie->inta_mask = CSR_INI_SET_MASK;
		iwl_write32(trans, CSR_INT_MASK, trans_pcie->inta_mask);
	} else {
		/*
		 * fh/hw_mask keeps all the unmasked causes.
		 * Unlike msi, in msix cause is enabled when it is unset.
		 */
		trans_pcie->hw_mask = trans_pcie->hw_init_mask;
		trans_pcie->fh_mask = trans_pcie->fh_init_mask;
		iwl_write32(trans, CSR_MSIX_FH_INT_MASK_AD,
			    ~trans_pcie->fh_mask);
		iwl_write32(trans, CSR_MSIX_HW_INT_MASK_AD,
			    ~trans_pcie->hw_mask);
	}
}

static inline void iwl_enable_interrupts(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	spin_lock(&trans_pcie->irq_lock);
	_iwl_enable_interrupts(trans);
	spin_unlock(&trans_pcie->irq_lock);
}
static inline void iwl_enable_hw_int_msk_msix(struct iwl_trans *trans, u32 msk)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	iwl_write32(trans, CSR_MSIX_HW_INT_MASK_AD, ~msk);
	trans_pcie->hw_mask = msk;
}

static inline void iwl_enable_fh_int_msk_msix(struct iwl_trans *trans, u32 msk)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	iwl_write32(trans, CSR_MSIX_FH_INT_MASK_AD, ~msk);
	trans_pcie->fh_mask = msk;
}

static inline void iwl_enable_fw_load_int(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	IWL_DEBUG_ISR(trans, "Enabling FW load interrupt\n");
	if (!trans_pcie->msix_enabled) {
		trans_pcie->inta_mask = CSR_INT_BIT_FH_TX;
		iwl_write32(trans, CSR_INT_MASK, trans_pcie->inta_mask);
	} else {
		iwl_write32(trans, CSR_MSIX_HW_INT_MASK_AD,
			    trans_pcie->hw_init_mask);
		iwl_enable_fh_int_msk_msix(trans,
					   MSIX_FH_INT_CAUSES_D2S_CH0_NUM);
	}
}

static inline void *iwl_pcie_get_tfd(struct iwl_trans_pcie *trans_pcie,
				     struct iwl_txq *txq, int idx)
{
	return txq->tfds + trans_pcie->tfd_size * idx;
}

static inline void iwl_enable_rfkill_int(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	IWL_DEBUG_ISR(trans, "Enabling rfkill interrupt\n");
	if (!trans_pcie->msix_enabled) {
		trans_pcie->inta_mask = CSR_INT_BIT_RF_KILL;
		iwl_write32(trans, CSR_INT_MASK, trans_pcie->inta_mask);
	} else {
		iwl_write32(trans, CSR_MSIX_FH_INT_MASK_AD,
			    trans_pcie->fh_init_mask);
		iwl_enable_hw_int_msk_msix(trans,
					   MSIX_HW_INT_CAUSES_REG_RF_KILL);
	}
}

static inline void iwl_wake_queue(struct iwl_trans *trans,
				  struct iwl_txq *txq)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (test_and_clear_bit(txq->id, trans_pcie->queue_stopped)) {
		IWL_DEBUG_TX_QUEUES(trans, "Wake hwq %d\n", txq->id);
		iwl_op_mode_queue_not_full(trans->op_mode, txq->id);
	}
}

static inline void iwl_stop_queue(struct iwl_trans *trans,
				  struct iwl_txq *txq)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (!test_and_set_bit(txq->id, trans_pcie->queue_stopped)) {
		iwl_op_mode_queue_full(trans->op_mode, txq->id);
		IWL_DEBUG_TX_QUEUES(trans, "Stop hwq %d\n", txq->id);
	} else
		IWL_DEBUG_TX_QUEUES(trans, "hwq %d already stopped\n",
				    txq->id);
}

static inline bool iwl_queue_used(const struct iwl_txq *q, int i)
{
	return q->write_ptr >= q->read_ptr ?
		(i >= q->read_ptr && i < q->write_ptr) :
		!(i < q->read_ptr && i >= q->write_ptr);
}

static inline u8 get_cmd_index(struct iwl_txq *q, u32 index)
{
	return index & (q->n_window - 1);
}

static inline bool iwl_is_rfkill_set(struct iwl_trans *trans)
{
	lockdep_assert_held(&IWL_TRANS_GET_PCIE_TRANS(trans)->mutex);

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

#ifdef CONFIG_IWLWIFI_DEBUGFS
int iwl_trans_pcie_dbgfs_register(struct iwl_trans *trans);
#else
static inline int iwl_trans_pcie_dbgfs_register(struct iwl_trans *trans)
{
	return 0;
}
#endif

int iwl_pci_fw_exit_d0i3(struct iwl_trans *trans);
int iwl_pci_fw_enter_d0i3(struct iwl_trans *trans);

void iwl_pcie_enable_rx_wake(struct iwl_trans *trans, bool enable);

/* common functions that are used by gen2 transport */
void iwl_pcie_apm_config(struct iwl_trans *trans);
int iwl_pcie_prepare_card_hw(struct iwl_trans *trans);
void iwl_pcie_synchronize_irqs(struct iwl_trans *trans);
bool iwl_trans_check_hw_rf_kill(struct iwl_trans *trans);
void iwl_pcie_txq_free_tfd(struct iwl_trans *trans, struct iwl_txq *txq);
int iwl_queue_space(const struct iwl_txq *q);
int iwl_pcie_apm_stop_master(struct iwl_trans *trans);
void iwl_pcie_conf_msix_hw(struct iwl_trans_pcie *trans_pcie);
int iwl_pcie_txq_init(struct iwl_trans *trans, struct iwl_txq *txq,
		      int slots_num, bool cmd_queue);
int iwl_pcie_txq_alloc(struct iwl_trans *trans,
		       struct iwl_txq *txq, int slots_num,  bool cmd_queue);
int iwl_pcie_alloc_dma_ptr(struct iwl_trans *trans,
			   struct iwl_dma_ptr *ptr, size_t size);
void iwl_pcie_free_dma_ptr(struct iwl_trans *trans, struct iwl_dma_ptr *ptr);
void iwl_pcie_apply_destination(struct iwl_trans *trans);
#ifdef CONFIG_INET
struct iwl_tso_hdr_page *get_page_hdr(struct iwl_trans *trans, size_t len);
#endif

/* transport gen 2 exported functions */
int iwl_trans_pcie_gen2_start_fw(struct iwl_trans *trans,
				 const struct fw_img *fw, bool run_in_rfkill);
void iwl_trans_pcie_gen2_fw_alive(struct iwl_trans *trans, u32 scd_addr);
int iwl_trans_pcie_dyn_txq_alloc(struct iwl_trans *trans,
				 struct iwl_tx_queue_cfg_cmd *cmd,
				 int cmd_id,
				 unsigned int timeout);
void iwl_trans_pcie_dyn_txq_free(struct iwl_trans *trans, int queue);
int iwl_trans_pcie_gen2_tx(struct iwl_trans *trans, struct sk_buff *skb,
			   struct iwl_device_cmd *dev_cmd, int txq_id);
int iwl_trans_pcie_gen2_send_hcmd(struct iwl_trans *trans,
				  struct iwl_host_cmd *cmd);
void iwl_trans_pcie_gen2_stop_device(struct iwl_trans *trans,
				     bool low_power);
void _iwl_trans_pcie_gen2_stop_device(struct iwl_trans *trans, bool low_power);
void iwl_pcie_gen2_txq_unmap(struct iwl_trans *trans, int txq_id);
void iwl_pcie_gen2_tx_free(struct iwl_trans *trans);
void iwl_pcie_gen2_tx_stop(struct iwl_trans *trans);
#endif /* __iwl_trans_int_pcie_h__ */

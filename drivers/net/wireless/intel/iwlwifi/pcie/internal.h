/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2003-2015, 2018-2021 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
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
#include "iwl-drv.h"
#include "queue/tx.h"

/*
 * RX related structures and functions
 */
#define RX_NUM_QUEUES 1
#define RX_POST_REQ_ALLOC 2
#define RX_CLAIM_REQ_ALLOC 8
#define RX_PENDING_WATERMARK 16
#define FIRST_RX_QUEUE 512

struct iwl_host_cmd;

/*This file includes the declaration that are internal to the
 * trans_pcie layer */

/**
 * struct iwl_rx_mem_buffer
 * @page_dma: bus address of rxb page
 * @page: driver's pointer to the rxb page
 * @invalid: rxb is in driver ownership - not owned by HW
 * @vid: index of this rxb in the global table
 * @offset: indicates which offset of the page (in bytes)
 *	this buffer uses (if multiple RBs fit into one page)
 */
struct iwl_rx_mem_buffer {
	dma_addr_t page_dma;
	struct page *page;
	u16 vid;
	bool invalid;
	struct list_head list;
	u32 offset;
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
 * struct iwl_rx_transfer_desc - transfer descriptor
 * @addr: ptr to free buffer start address
 * @rbid: unique tag of the buffer
 * @reserved: reserved
 */
struct iwl_rx_transfer_desc {
	__le16 rbid;
	__le16 reserved[3];
	__le64 addr;
} __packed;

#define IWL_RX_CD_FLAGS_FRAGMENTED	BIT(0)

/**
 * struct iwl_rx_completion_desc - completion descriptor
 * @reserved1: reserved
 * @rbid: unique tag of the received buffer
 * @flags: flags (0: fragmented, all others: reserved)
 * @reserved2: reserved
 */
struct iwl_rx_completion_desc {
	__le32 reserved1;
	__le16 rbid;
	u8 flags;
	u8 reserved2[25];
} __packed;

/**
 * struct iwl_rxq - Rx queue
 * @id: queue index
 * @bd: driver's pointer to buffer of receive buffer descriptors (rbd).
 *	Address size is 32 bit in pre-9000 devices and 64 bit in 9000 devices.
 *	In AX210 devices it is a pointer to a list of iwl_rx_transfer_desc's
 * @bd_dma: bus address of buffer of receive buffer descriptors (rbd)
 * @used_bd: driver's pointer to buffer of used receive buffer descriptors (rbd)
 * @used_bd_dma: physical address of buffer of used receive buffer descriptors (rbd)
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
 * @next_rb_is_fragment: indicates that the previous RB that we handled set
 *	the fragmented flag, so the next one is still another fragment
 *
 * NOTE:  rx_free and rx_used are used as a FIFO for iwl_rx_mem_buffers
 */
struct iwl_rxq {
	int id;
	void *bd;
	dma_addr_t bd_dma;
	union {
		void *used_bd;
		__le32 *bd_32;
		struct iwl_rx_completion_desc *cd;
	};
	dma_addr_t used_bd_dma;
	u32 read;
	u32 write;
	u32 free_count;
	u32 used_count;
	u32 write_actual;
	u32 queue_size;
	struct list_head rx_free;
	struct list_head rx_used;
	bool need_update, next_rb_is_fragment;
	void *rb_stts;
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

/**
 * iwl_get_closed_rb_stts - get closed rb stts from different structs
 * @rxq - the rxq to get the rb stts from
 */
static inline __le16 iwl_get_closed_rb_stts(struct iwl_trans *trans,
					    struct iwl_rxq *rxq)
{
	if (trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_AX210) {
		__le16 *rb_stts = rxq->rb_stts;

		return READ_ONCE(*rb_stts);
	} else {
		struct iwl_rb_status *rb_stts = rxq->rb_stts;

		return READ_ONCE(rb_stts->closed_rb_num);
	}
}

#ifdef CONFIG_IWLWIFI_DEBUGFS
/**
 * enum iwl_fw_mon_dbgfs_state - the different states of the monitor_data
 * debugfs file
 *
 * @IWL_FW_MON_DBGFS_STATE_CLOSED: the file is closed.
 * @IWL_FW_MON_DBGFS_STATE_OPEN: the file is open.
 * @IWL_FW_MON_DBGFS_STATE_DISABLED: the file is disabled, once this state is
 *	set the file can no longer be used.
 */
enum iwl_fw_mon_dbgfs_state {
	IWL_FW_MON_DBGFS_STATE_CLOSED,
	IWL_FW_MON_DBGFS_STATE_OPEN,
	IWL_FW_MON_DBGFS_STATE_DISABLED,
};
#endif

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
 * enum iwl_image_response_code - image response values
 * @IWL_IMAGE_RESP_DEF: the default value of the register
 * @IWL_IMAGE_RESP_SUCCESS: iml was read successfully
 * @IWL_IMAGE_RESP_FAIL: iml reading failed
 */
enum iwl_image_response_code {
	IWL_IMAGE_RESP_DEF		= 0,
	IWL_IMAGE_RESP_SUCCESS		= 1,
	IWL_IMAGE_RESP_FAIL		= 2,
};

/**
 * struct cont_rec: continuous recording data structure
 * @prev_wr_ptr: the last address that was read in monitor_data
 *	debugfs file
 * @prev_wrap_cnt: the wrap count that was used during the last read in
 *	monitor_data debugfs file
 * @state: the state of monitor_data debugfs file as described
 *	in &iwl_fw_mon_dbgfs_state enum
 * @mutex: locked while reading from monitor_data debugfs file
 */
#ifdef CONFIG_IWLWIFI_DEBUGFS
struct cont_rec {
	u32 prev_wr_ptr;
	u32 prev_wrap_cnt;
	u8  state;
	/* Used to sync monitor_data debugfs file with driver unload flow */
	struct mutex mutex;
};
#endif

/**
 * struct iwl_trans_pcie - PCIe transport specific data
 * @rxq: all the RX queue data
 * @rx_pool: initial pool of iwl_rx_mem_buffer for all the queues
 * @global_table: table mapping received VID from hw to rxb
 * @rba: allocator for RX replenishing
 * @ctxt_info: context information for FW self init
 * @ctxt_info_gen3: context information for gen3 devices
 * @prph_info: prph info for self init
 * @prph_scratch: prph scratch for self init
 * @ctxt_info_dma_addr: dma addr of context information
 * @prph_info_dma_addr: dma addr of prph info
 * @prph_scratch_dma_addr: dma addr of prph scratch
 * @ctxt_info_dma_addr: dma addr of context information
 * @init_dram: DRAM data of firmware image (including paging).
 *	Context information addresses will be taken from here.
 *	This is driver's local copy for keeping track of size and
 *	count for allocating and freeing the memory.
 * @iml: image loader image virtual address
 * @iml_dma_addr: image loader image DMA address
 * @trans: pointer to the generic transport area
 * @scd_base_addr: scheduler sram base address in SRAM
 * @kw: keep warm address
 * @pnvm_dram: DRAM area that contains the PNVM data
 * @pci_dev: basic pci-network driver stuff
 * @hw_base: pci hardware address support
 * @ucode_write_complete: indicates that the ucode has been copied.
 * @ucode_write_waitq: wait queue for uCode load
 * @cmd_queue - command queue number
 * @def_rx_queue - default rx queue number
 * @rx_buf_size: Rx buffer size
 * @scd_set_active: should the transport configure the SCD for HCMD queue
 * @rx_page_order: page order for receive buffer size
 * @rx_buf_bytes: RX buffer (RB) size in bytes
 * @reg_lock: protect hw register access
 * @mutex: to protect stop_device / start_fw / start_hw
 * @cmd_in_flight: true when we have a host command in flight
#ifdef CONFIG_IWLWIFI_DEBUGFS
 * @fw_mon_data: fw continuous recording data
#endif
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
 * @in_rescan: true if we have triggered a device rescan
 * @base_rb_stts: base virtual address of receive buffer status for all queues
 * @base_rb_stts_dma: base physical address of receive buffer status
 * @supported_dma_mask: DMA mask to validate the actual address against,
 *	will be DMA_BIT_MASK(11) or DMA_BIT_MASK(12) depending on the device
 * @alloc_page_lock: spinlock for the page allocator
 * @alloc_page: allocated page to still use parts of
 * @alloc_page_used: how much of the allocated page was already used (bytes)
 * @rf_name: name/version of the CRF, if any
 */
struct iwl_trans_pcie {
	struct iwl_rxq *rxq;
	struct iwl_rx_mem_buffer *rx_pool;
	struct iwl_rx_mem_buffer **global_table;
	struct iwl_rb_allocator rba;
	union {
		struct iwl_context_info *ctxt_info;
		struct iwl_context_info_gen3 *ctxt_info_gen3;
	};
	struct iwl_prph_info *prph_info;
	struct iwl_prph_scratch *prph_scratch;
	void *iml;
	dma_addr_t ctxt_info_dma_addr;
	dma_addr_t prph_info_dma_addr;
	dma_addr_t prph_scratch_dma_addr;
	dma_addr_t iml_dma_addr;
	struct iwl_trans *trans;

	struct net_device napi_dev;

	/* INT ICT Table */
	__le32 *ict_tbl;
	dma_addr_t ict_tbl_dma;
	int ict_index;
	bool use_ict;
	bool is_down, opmode_down;
	s8 debug_rfkill;
	struct isr_statistics isr_stats;

	spinlock_t irq_lock;
	struct mutex mutex;
	u32 inta_mask;
	u32 scd_base_addr;
	struct iwl_dma_ptr kw;

	struct iwl_dram_data pnvm_dram;
	struct iwl_dram_data reduce_power_dram;

	struct iwl_txq *txq_memory;

	/* PCI bus related data */
	struct pci_dev *pci_dev;
	void __iomem *hw_base;

	bool ucode_write_complete;
	bool sx_complete;
	wait_queue_head_t ucode_write_waitq;
	wait_queue_head_t sx_waitq;

	u8 def_rx_queue;
	u8 n_no_reclaim_cmds;
	u8 no_reclaim_cmds[MAX_NO_RECLAIM_CMDS];
	u16 num_rx_bufs;

	enum iwl_amsdu_size rx_buf_size;
	bool scd_set_active;
	bool pcie_dbg_dumped_once;
	u32 rx_page_order;
	u32 rx_buf_bytes;
	u32 supported_dma_mask;

	/* allocator lock for the two values below */
	spinlock_t alloc_page_lock;
	struct page *alloc_page;
	u32 alloc_page_used;

	/*protect hw register */
	spinlock_t reg_lock;
	bool cmd_hold_nic_awake;

#ifdef CONFIG_IWLWIFI_DEBUGFS
	struct cont_rec fw_mon_data;
#endif

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
	u16 tx_cmd_queue_size;
	bool in_rescan;

	void *base_rb_stts;
	dma_addr_t base_rb_stts_dma;

	bool fw_reset_handshake;
	bool fw_reset_done;
	wait_queue_head_t fw_reset_waitq;

	char rf_name[32];
};

static inline struct iwl_trans_pcie *
IWL_TRANS_GET_PCIE_TRANS(struct iwl_trans *trans)
{
	return (void *)trans->trans_specific;
}

static inline void iwl_pcie_clear_irq(struct iwl_trans *trans, int queue)
{
	/*
	 * Before sending the interrupt the HW disables it to prevent
	 * a nested interrupt. This is done by writing 1 to the corresponding
	 * bit in the mask register. After handling the interrupt, it should be
	 * re-enabled by clearing this bit. This register is defined as
	 * write 1 clear (W1C) register, meaning that it's being clear
	 * by writing 1 to the bit.
	 */
	iwl_write32(trans, CSR_MSIX_AUTOMASK_ST_AD, BIT(queue));
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
struct iwl_trans
*iwl_trans_pcie_alloc(struct pci_dev *pdev,
		      const struct pci_device_id *ent,
		      const struct iwl_cfg_trans_params *cfg_trans);
void iwl_trans_pcie_free(struct iwl_trans *trans);

bool __iwl_trans_pcie_grab_nic_access(struct iwl_trans *trans);
#define _iwl_trans_pcie_grab_nic_access(trans)			\
	__cond_lock(nic_access_nobh,				\
		    likely(__iwl_trans_pcie_grab_nic_access(trans)))

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
void iwl_pcie_free_rbs_pool(struct iwl_trans *trans);
void iwl_pcie_rx_init_rxb_lists(struct iwl_rxq *rxq);
void iwl_pcie_rxq_alloc_rbs(struct iwl_trans *trans, gfp_t priority,
			    struct iwl_rxq *rxq);

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
bool iwl_trans_pcie_txq_enable(struct iwl_trans *trans, int queue, u16 ssn,
			       const struct iwl_trans_txq_scd_cfg *cfg,
			       unsigned int wdg_timeout);
void iwl_trans_pcie_txq_disable(struct iwl_trans *trans, int queue,
				bool configure_scd);
void iwl_trans_pcie_txq_set_shared_mode(struct iwl_trans *trans, u32 txq_id,
					bool shared_mode);
int iwl_trans_pcie_tx(struct iwl_trans *trans, struct sk_buff *skb,
		      struct iwl_device_tx_cmd *dev_cmd, int txq_id);
void iwl_pcie_txq_check_wrptrs(struct iwl_trans *trans);
int iwl_trans_pcie_send_hcmd(struct iwl_trans *trans, struct iwl_host_cmd *cmd);
void iwl_pcie_hcmd_complete(struct iwl_trans *trans,
			    struct iwl_rx_cmd_buffer *rxb);
void iwl_trans_pcie_tx_reset(struct iwl_trans *trans);

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

static inline int iwl_pcie_get_num_sections(const struct fw_img *fw,
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

static inline void iwl_pcie_ctxt_info_free_fw_img(struct iwl_trans *trans)
{
	struct iwl_self_init_dram *dram = &trans->init_dram;
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

static inline void iwl_disable_interrupts(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	spin_lock_bh(&trans_pcie->irq_lock);
	_iwl_disable_interrupts(trans);
	spin_unlock_bh(&trans_pcie->irq_lock);
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

	spin_lock_bh(&trans_pcie->irq_lock);
	_iwl_enable_interrupts(trans);
	spin_unlock_bh(&trans_pcie->irq_lock);
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

static inline void iwl_enable_fw_load_int_ctx_info(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	IWL_DEBUG_ISR(trans, "Enabling ALIVE interrupt only\n");

	if (!trans_pcie->msix_enabled) {
		/*
		 * When we'll receive the ALIVE interrupt, the ISR will call
		 * iwl_enable_fw_load_int_ctx_info again to set the ALIVE
		 * interrupt (which is not really needed anymore) but also the
		 * RX interrupt which will allow us to receive the ALIVE
		 * notification (which is Rx) and continue the flow.
		 */
		trans_pcie->inta_mask =  CSR_INT_BIT_ALIVE | CSR_INT_BIT_FH_RX;
		iwl_write32(trans, CSR_INT_MASK, trans_pcie->inta_mask);
	} else {
		iwl_enable_hw_int_msk_msix(trans,
					   MSIX_HW_INT_CAUSES_REG_ALIVE);
		/*
		 * Leave all the FH causes enabled to get the ALIVE
		 * notification.
		 */
		iwl_enable_fh_int_msk_msix(trans, trans_pcie->fh_init_mask);
	}
}

static inline const char *queue_name(struct device *dev,
				     struct iwl_trans_pcie *trans_p, int i)
{
	if (trans_p->shared_vec_mask) {
		int vec = trans_p->shared_vec_mask &
			  IWL_SHARED_IRQ_FIRST_RSS ? 1 : 0;

		if (i == 0)
			return DRV_NAME ": shared IRQ";

		return devm_kasprintf(dev, GFP_KERNEL,
				      DRV_NAME ": queue %d", i + vec);
	}
	if (i == 0)
		return DRV_NAME ": default queue";

	if (i == trans_p->alloc_vecs - 1)
		return DRV_NAME ": exception";

	return devm_kasprintf(dev, GFP_KERNEL,
			      DRV_NAME  ": queue %d", i);
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

	if (trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_9000) {
		/*
		 * On 9000-series devices this bit isn't enabled by default, so
		 * when we power down the device we need set the bit to allow it
		 * to wake up the PCI-E bus for RF-kill interrupts.
		 */
		iwl_set_bit(trans, CSR_GP_CNTRL,
			    CSR_GP_CNTRL_REG_FLAG_RFKILL_WAKE_L1A_EN);
	}
}

void iwl_pcie_handle_rfkill_irq(struct iwl_trans *trans);

static inline bool iwl_is_rfkill_set(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	lockdep_assert_held(&trans_pcie->mutex);

	if (trans_pcie->debug_rfkill == 1)
		return true;

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

static inline bool iwl_pcie_dbg_on(struct iwl_trans *trans)
{
	return (trans->dbg.dest_tlv || iwl_trans_dbg_ini_valid(trans));
}

void iwl_trans_pcie_rf_kill(struct iwl_trans *trans, bool state);
void iwl_trans_pcie_dump_regs(struct iwl_trans *trans);

#ifdef CONFIG_IWLWIFI_DEBUGFS
void iwl_trans_pcie_dbgfs_register(struct iwl_trans *trans);
#else
static inline void iwl_trans_pcie_dbgfs_register(struct iwl_trans *trans) { }
#endif

void iwl_pcie_rx_allocator_work(struct work_struct *data);

/* common functions that are used by gen2 transport */
int iwl_pcie_gen2_apm_init(struct iwl_trans *trans);
void iwl_pcie_apm_config(struct iwl_trans *trans);
int iwl_pcie_prepare_card_hw(struct iwl_trans *trans);
void iwl_pcie_synchronize_irqs(struct iwl_trans *trans);
bool iwl_pcie_check_hw_rf_kill(struct iwl_trans *trans);
void iwl_trans_pcie_handle_stop_rfkill(struct iwl_trans *trans,
				       bool was_in_rfkill);
void iwl_pcie_apm_stop_master(struct iwl_trans *trans);
void iwl_pcie_conf_msix_hw(struct iwl_trans_pcie *trans_pcie);
int iwl_pcie_alloc_dma_ptr(struct iwl_trans *trans,
			   struct iwl_dma_ptr *ptr, size_t size);
void iwl_pcie_free_dma_ptr(struct iwl_trans *trans, struct iwl_dma_ptr *ptr);
void iwl_pcie_apply_destination(struct iwl_trans *trans);

/* common functions that are used by gen3 transport */
void iwl_pcie_alloc_fw_monitor(struct iwl_trans *trans, u8 max_power);

/* transport gen 2 exported functions */
int iwl_trans_pcie_gen2_start_fw(struct iwl_trans *trans,
				 const struct fw_img *fw, bool run_in_rfkill);
void iwl_trans_pcie_gen2_fw_alive(struct iwl_trans *trans, u32 scd_addr);
int iwl_trans_pcie_gen2_send_hcmd(struct iwl_trans *trans,
				  struct iwl_host_cmd *cmd);
void iwl_trans_pcie_gen2_stop_device(struct iwl_trans *trans);
void _iwl_trans_pcie_gen2_stop_device(struct iwl_trans *trans);
void iwl_pcie_d3_complete_suspend(struct iwl_trans *trans,
				  bool test, bool reset);
int iwl_pcie_gen2_enqueue_hcmd(struct iwl_trans *trans,
			       struct iwl_host_cmd *cmd);
int iwl_pcie_enqueue_hcmd(struct iwl_trans *trans,
			  struct iwl_host_cmd *cmd);
#endif /* __iwl_trans_int_pcie_h__ */

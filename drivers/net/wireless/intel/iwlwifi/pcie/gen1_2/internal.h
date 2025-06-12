/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2003-2015, 2018-2025 Intel Corporation
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
#include "pcie/iwl-context-info.h"

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
 * struct iwl_rx_mem_buffer - driver-side RX buffer descriptor
 * @page_dma: bus address of rxb page
 * @page: driver's pointer to the rxb page
 * @list: list entry for the membuffer
 * @invalid: rxb is in driver ownership - not owned by HW
 * @vid: index of this rxb in the global table
 * @offset: indicates which offset of the page (in bytes)
 *	this buffer uses (if multiple RBs fit into one page)
 */
struct iwl_rx_mem_buffer {
	dma_addr_t page_dma;
	struct page *page;
	struct list_head list;
	u32 offset;
	u16 vid;
	bool invalid;
};

/* interrupt statistics */
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
 * struct iwl_rx_completion_desc_bz - Bz completion descriptor
 * @rbid: unique tag of the received buffer
 * @flags: flags (0: fragmented, all others: reserved)
 * @reserved: reserved
 */
struct iwl_rx_completion_desc_bz {
	__le16 rbid;
	u8 flags;
	u8 reserved[1];
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
 * @write_actual: actual write pointer written to device, since we update in
 *	blocks of 8 only
 * @free_count: Number of pre-allocated buffers in rx_free
 * @used_count: Number of RBDs handled to allocator to use for allocation
 * @write_actual:
 * @rx_free: list of RBDs with allocated RB ready for use
 * @rx_used: list of RBDs with no RB attached
 * @need_update: flag to indicate we need to update read/write index
 * @rb_stts: driver's pointer to receive buffer status
 * @rb_stts_dma: bus address of receive buffer status
 * @lock: per-queue lock
 * @queue: actual rx queue. Not used for multi-rx queue.
 * @next_rb_is_fragment: indicates that the previous RB that we handled set
 *	the fragmented flag, so the next one is still another fragment
 * @napi: NAPI struct for this queue
 * @queue_size: size of this queue
 *
 * NOTE:  rx_free and rx_used are used as a FIFO for iwl_rx_mem_buffers
 */
struct iwl_rxq {
	int id;
	void *bd;
	dma_addr_t bd_dma;
	void *used_bd;
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
 * @trans: transport pointer (for configuration)
 * @rxq: the rxq to get the rb stts from
 * Return: last closed RB index
 */
static inline u16 iwl_get_closed_rb_stts(struct iwl_trans *trans,
					 struct iwl_rxq *rxq)
{
	if (trans->mac_cfg->device_family >= IWL_DEVICE_FAMILY_AX210) {
		__le16 *rb_stts = rxq->rb_stts;

		return le16_to_cpu(READ_ONCE(*rb_stts));
	} else {
		struct iwl_rb_status *rb_stts = rxq->rb_stts;

		return le16_to_cpu(READ_ONCE(rb_stts->closed_rb_num)) & 0xFFF;
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

#ifdef CONFIG_IWLWIFI_DEBUGFS
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
struct cont_rec {
	u32 prev_wr_ptr;
	u32 prev_wrap_cnt;
	u8  state;
	/* Used to sync monitor_data debugfs file with driver unload flow */
	struct mutex mutex;
};
#endif

enum iwl_pcie_fw_reset_state {
	FW_RESET_IDLE,
	FW_RESET_REQUESTED,
	FW_RESET_OK,
	FW_RESET_ERROR,
	FW_RESET_TOP_REQUESTED,
};

/**
 * enum iwl_pcie_imr_status - imr dma transfer state
 * @IMR_D2S_IDLE: default value of the dma transfer
 * @IMR_D2S_REQUESTED: dma transfer requested
 * @IMR_D2S_COMPLETED: dma transfer completed
 * @IMR_D2S_ERROR: dma transfer error
 */
enum iwl_pcie_imr_status {
	IMR_D2S_IDLE,
	IMR_D2S_REQUESTED,
	IMR_D2S_COMPLETED,
	IMR_D2S_ERROR,
};

/**
 * struct iwl_pcie_txqs - TX queues data
 *
 * @queue_used: bit mask of used queues
 * @queue_stopped: bit mask of stopped queues
 * @txq: array of TXQ data structures representing the TXQs
 * @scd_bc_tbls: gen1 pointer to the byte count table of the scheduler
 * @bc_pool: bytecount DMA allocations pool
 * @bc_tbl_size: bytecount table size
 * @tso_hdr_page: page allocated (per CPU) for A-MSDU headers when doing TSO
 *	(and similar usage)
 * @tfd: TFD data
 * @tfd.max_tbs: max number of buffers per TFD
 * @tfd.size: TFD size
 * @tfd.addr_size: TFD/TB address size
 */
struct iwl_pcie_txqs {
	unsigned long queue_used[BITS_TO_LONGS(IWL_MAX_TVQM_QUEUES)];
	unsigned long queue_stopped[BITS_TO_LONGS(IWL_MAX_TVQM_QUEUES)];
	struct iwl_txq *txq[IWL_MAX_TVQM_QUEUES];
	struct dma_pool *bc_pool;
	size_t bc_tbl_size;
	struct iwl_tso_hdr_page __percpu *tso_hdr_page;

	struct {
		u8 max_tbs;
		u16 size;
		u8 addr_size;
	} tfd;

	struct iwl_dma_ptr scd_bc_tbls;
};

/**
 * struct iwl_trans_pcie - PCIe transport specific data
 * @rxq: all the RX queue data
 * @rx_pool: initial pool of iwl_rx_mem_buffer for all the queues
 * @global_table: table mapping received VID from hw to rxb
 * @rba: allocator for RX replenishing
 * @ctxt_info: context information for FW self init
 * @ctxt_info_v2: context information for v1 devices
 * @prph_info: prph info for self init
 * @prph_scratch: prph scratch for self init
 * @ctxt_info_dma_addr: dma addr of context information
 * @prph_info_dma_addr: dma addr of prph info
 * @prph_scratch_dma_addr: dma addr of prph scratch
 * @ctxt_info_dma_addr: dma addr of context information
 * @iml: image loader image virtual address
 * @iml_len: image loader image size
 * @iml_dma_addr: image loader image DMA address
 * @trans: pointer to the generic transport area
 * @scd_base_addr: scheduler sram base address in SRAM
 * @kw: keep warm address
 * @pnvm_data: holds info about pnvm payloads allocated in DRAM
 * @reduced_tables_data: holds info about power reduced tablse
 *	payloads allocated in DRAM
 * @pci_dev: basic pci-network driver stuff
 * @hw_base: pci hardware address support
 * @ucode_write_complete: indicates that the ucode has been copied.
 * @ucode_write_waitq: wait queue for uCode load
 * @rx_page_order: page order for receive buffer size
 * @rx_buf_bytes: RX buffer (RB) size in bytes
 * @reg_lock: protect hw register access
 * @mutex: to protect stop_device / start_fw / start_hw
 * @fw_mon_data: fw continuous recording data
 * @cmd_hold_nic_awake: indicates NIC is held awake for APMG workaround
 *	during commands in flight
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
 * @imr_status: imr dma state machine
 * @imr_waitq: imr wait queue for dma completion
 * @rf_name: name/version of the CRF, if any
 * @use_ict: whether or not ICT (interrupt table) is used
 * @ict_index: current ICT read index
 * @ict_tbl: ICT table pointer
 * @ict_tbl_dma: ICT table DMA address
 * @inta_mask: interrupt (INT-A) mask
 * @irq_lock: lock to synchronize IRQ handling
 * @txq_memory: TXQ allocation array
 * @sx_waitq: waitqueue for Sx transitions
 * @sx_state: state tracking Sx transitions
 * @opmode_down: indicates opmode went away
 * @num_rx_bufs: number of RX buffers to allocate/use
 * @affinity_mask: IRQ affinity mask for each RX queue
 * @debug_rfkill: RF-kill debugging state, -1 for unset, 0/1 for radio
 *	enable/disable
 * @fw_reset_state: state of FW reset handshake
 * @fw_reset_waitq: waitqueue for FW reset handshake
 * @is_down: indicates the NIC is down
 * @isr_stats: interrupt statistics
 * @napi_dev: (fake) netdev for NAPI registration
 * @txqs: transport tx queues data.
 * @me_present: WiAMT/CSME is detected as present (1), not present (0)
 *	or unknown (-1, so can still use it as a boolean safely)
 * @me_recheck_wk: worker to recheck WiAMT/CSME presence
 * @invalid_tx_cmd: invalid TX command buffer
 * @wait_command_queue: wait queue for sync commands
 */
struct iwl_trans_pcie {
	struct iwl_rxq *rxq;
	struct iwl_rx_mem_buffer *rx_pool;
	struct iwl_rx_mem_buffer **global_table;
	struct iwl_rb_allocator rba;
	union {
		struct iwl_context_info *ctxt_info;
		struct iwl_context_info_v2 *ctxt_info_v2;
	};
	struct iwl_prph_info *prph_info;
	struct iwl_prph_scratch *prph_scratch;
	void *iml;
	size_t iml_len;
	dma_addr_t ctxt_info_dma_addr;
	dma_addr_t prph_info_dma_addr;
	dma_addr_t prph_scratch_dma_addr;
	dma_addr_t iml_dma_addr;
	struct iwl_trans *trans;

	struct net_device *napi_dev;

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

	/* pnvm data */
	struct iwl_dram_regions pnvm_data;
	struct iwl_dram_regions reduced_tables_data;

	struct iwl_txq *txq_memory;

	/* PCI bus related data */
	struct pci_dev *pci_dev;
	u8 __iomem *hw_base;

	bool ucode_write_complete;
	enum {
		IWL_SX_INVALID = 0,
		IWL_SX_WAITING,
		IWL_SX_ERROR,
		IWL_SX_COMPLETE,
	} sx_state;
	wait_queue_head_t ucode_write_waitq;
	wait_queue_head_t sx_waitq;

	u16 num_rx_bufs;

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

	enum iwl_pcie_fw_reset_state fw_reset_state;
	wait_queue_head_t fw_reset_waitq;
	enum iwl_pcie_imr_status imr_status;
	wait_queue_head_t imr_waitq;
	char rf_name[32];

	struct iwl_pcie_txqs txqs;

	s8 me_present;
	struct delayed_work me_recheck_wk;

	struct iwl_dma_ptr invalid_tx_cmd;

	wait_queue_head_t wait_command_queue;
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
		      const struct iwl_mac_cfg *mac_cfg,
		      struct iwl_trans_info *info);
void iwl_trans_pcie_free(struct iwl_trans *trans);
void iwl_trans_pcie_free_pnvm_dram_regions(struct iwl_dram_regions *dram_regions,
					   struct device *dev);

bool __iwl_trans_pcie_grab_nic_access(struct iwl_trans *trans, bool silent);
#define _iwl_trans_pcie_grab_nic_access(trans, silent)		\
	__cond_lock(nic_access_nobh,				\
		    likely(__iwl_trans_pcie_grab_nic_access(trans, silent)))

void iwl_trans_pcie_check_product_reset_status(struct pci_dev *pdev);
void iwl_trans_pcie_check_product_reset_mode(struct pci_dev *pdev);

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
void iwl_pcie_rx_napi_sync(struct iwl_trans *trans);
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
/* We need 2 entries for the TX command and header, and another one might
 * be needed for potential data in the SKB's head. The remaining ones can
 * be used for frags.
 */
#define IWL_TRANS_PCIE_MAX_FRAGS(trans_pcie) ((trans_pcie)->txqs.tfd.max_tbs - 3)

struct iwl_tso_hdr_page {
	struct page *page;
	u8 *pos;
};

/*
 * Note that we put this struct *last* in the page. By doing that, we ensure
 * that no TB referencing this page can trigger the 32-bit boundary hardware
 * bug.
 */
struct iwl_tso_page_info {
	dma_addr_t dma_addr;
	struct page *next;
	refcount_t use_count;
};

#define IWL_TSO_PAGE_DATA_SIZE	(PAGE_SIZE - sizeof(struct iwl_tso_page_info))
#define IWL_TSO_PAGE_INFO(addr)	\
	((struct iwl_tso_page_info *)(((unsigned long)addr & PAGE_MASK) + \
				      IWL_TSO_PAGE_DATA_SIZE))

int iwl_pcie_tx_init(struct iwl_trans *trans);
void iwl_pcie_tx_start(struct iwl_trans *trans);
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
void iwl_pcie_hcmd_complete(struct iwl_trans *trans,
			    struct iwl_rx_cmd_buffer *rxb);
void iwl_trans_pcie_tx_reset(struct iwl_trans *trans);
int iwl_pcie_txq_alloc(struct iwl_trans *trans, struct iwl_txq *txq,
		       int slots_num, bool cmd_queue);

dma_addr_t iwl_pcie_get_sgt_tb_phys(struct sg_table *sgt, unsigned int offset,
				    unsigned int len);
struct sg_table *iwl_pcie_prep_tso(struct iwl_trans *trans, struct sk_buff *skb,
				   struct iwl_cmd_meta *cmd_meta,
				   u8 **hdr, unsigned int hdr_room,
				   unsigned int offset);

void iwl_pcie_free_tso_pages(struct iwl_trans *trans, struct sk_buff *skb,
			     struct iwl_cmd_meta *cmd_meta);

static inline dma_addr_t iwl_pcie_get_tso_page_phys(void *addr)
{
	dma_addr_t res;

	res = IWL_TSO_PAGE_INFO(addr)->dma_addr;
	res += (unsigned long)addr & ~PAGE_MASK;

	return res;
}

static inline dma_addr_t
iwl_txq_get_first_tb_dma(struct iwl_txq *txq, int idx)
{
	return txq->first_tb_dma +
	       sizeof(struct iwl_pcie_first_tb_buf) * idx;
}

static inline u16 iwl_txq_get_cmd_index(const struct iwl_txq *q, u32 index)
{
	return index & (q->n_window - 1);
}

static inline void *iwl_txq_get_tfd(struct iwl_trans *trans,
				    struct iwl_txq *txq, int idx)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (trans->mac_cfg->gen2)
		idx = iwl_txq_get_cmd_index(txq, idx);

	return (u8 *)txq->tfds + trans_pcie->txqs.tfd.size * idx;
}

/*
 * We need this inline in case dma_addr_t is only 32-bits - since the
 * hardware is always 64-bit, the issue can still occur in that case,
 * so use u64 for 'phys' here to force the addition in 64-bit.
 */
static inline bool iwl_txq_crosses_4g_boundary(u64 phys, u16 len)
{
	return upper_32_bits(phys) != upper_32_bits(phys + len);
}

int iwl_txq_space(struct iwl_trans *trans, const struct iwl_txq *q);

static inline void iwl_txq_stop(struct iwl_trans *trans, struct iwl_txq *txq)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (!test_and_set_bit(txq->id, trans_pcie->txqs.queue_stopped)) {
		iwl_op_mode_queue_full(trans->op_mode, txq->id);
		IWL_DEBUG_TX_QUEUES(trans, "Stop hwq %d\n", txq->id);
	} else {
		IWL_DEBUG_TX_QUEUES(trans, "hwq %d already stopped\n",
				    txq->id);
	}
}

/**
 * iwl_txq_inc_wrap - increment queue index, wrap back to beginning
 * @trans: the transport (for configuration data)
 * @index: current index
 * Return: the queue index incremented, subject to wrapping
 */
static inline int iwl_txq_inc_wrap(struct iwl_trans *trans, int index)
{
	return ++index &
		(trans->mac_cfg->base->max_tfd_queue_size - 1);
}

/**
 * iwl_txq_dec_wrap - decrement queue index, wrap back to end
 * @trans: the transport (for configuration data)
 * @index: current index
 * Return: the queue index decremented, subject to wrapping
 */
static inline int iwl_txq_dec_wrap(struct iwl_trans *trans, int index)
{
	return --index &
		(trans->mac_cfg->base->max_tfd_queue_size - 1);
}

void iwl_txq_log_scd_error(struct iwl_trans *trans, struct iwl_txq *txq);

static inline void
iwl_trans_pcie_wake_queue(struct iwl_trans *trans, struct iwl_txq *txq)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (test_and_clear_bit(txq->id, trans_pcie->txqs.queue_stopped)) {
		IWL_DEBUG_TX_QUEUES(trans, "Wake hwq %d\n", txq->id);
		iwl_op_mode_queue_not_full(trans->op_mode, txq->id);
	}
}

int iwl_txq_gen2_set_tb(struct iwl_trans *trans,
			struct iwl_tfh_tfd *tfd, dma_addr_t addr,
			u16 len);

static inline void iwl_txq_set_tfd_invalid_gen2(struct iwl_trans *trans,
						struct iwl_tfh_tfd *tfd)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	tfd->num_tbs = 0;

	iwl_txq_gen2_set_tb(trans, tfd, trans_pcie->invalid_tx_cmd.dma,
			    trans_pcie->invalid_tx_cmd.size);
}

void iwl_txq_gen2_tfd_unmap(struct iwl_trans *trans,
			    struct iwl_cmd_meta *meta,
			    struct iwl_tfh_tfd *tfd);

int iwl_txq_dyn_alloc(struct iwl_trans *trans, u32 flags,
		      u32 sta_mask, u8 tid,
		      int size, unsigned int timeout);

int iwl_txq_gen2_tx(struct iwl_trans *trans, struct sk_buff *skb,
		    struct iwl_device_tx_cmd *dev_cmd, int txq_id);

void iwl_txq_dyn_free(struct iwl_trans *trans, int queue);
void iwl_txq_gen2_tx_free(struct iwl_trans *trans);
int iwl_txq_init(struct iwl_trans *trans, struct iwl_txq *txq,
		 int slots_num, bool cmd_queue);
int iwl_txq_gen2_init(struct iwl_trans *trans, int txq_id,
		      int queue_size);

static inline u16 iwl_txq_gen1_tfd_tb_get_len(struct iwl_trans *trans,
					      void *_tfd, u8 idx)
{
	struct iwl_tfd *tfd;
	struct iwl_tfd_tb *tb;

	if (trans->mac_cfg->gen2) {
		struct iwl_tfh_tfd *tfh_tfd = _tfd;
		struct iwl_tfh_tb *tfh_tb = &tfh_tfd->tbs[idx];

		return le16_to_cpu(tfh_tb->tb_len);
	}

	tfd = (struct iwl_tfd *)_tfd;
	tb = &tfd->tbs[idx];

	return le16_to_cpu(tb->hi_n_len) >> 4;
}

void iwl_pcie_reclaim(struct iwl_trans *trans, int txq_id, int ssn,
		      struct sk_buff_head *skbs, bool is_flush);
void iwl_pcie_set_q_ptrs(struct iwl_trans *trans, int txq_id, int ptr);
void iwl_pcie_freeze_txq_timer(struct iwl_trans *trans,
			       unsigned long txqs, bool freeze);
int iwl_trans_pcie_wait_txq_empty(struct iwl_trans *trans, int txq_idx);
int iwl_trans_pcie_wait_txqs_empty(struct iwl_trans *trans, u32 txq_bm);

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

static inline void iwl_enable_fw_load_int_ctx_info(struct iwl_trans *trans,
						   bool top_reset)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	IWL_DEBUG_ISR(trans, "Enabling %s interrupt only\n",
		      top_reset ? "RESET" : "ALIVE");

	if (!trans_pcie->msix_enabled) {
		/*
		 * When we'll receive the ALIVE interrupt, the ISR will call
		 * iwl_enable_fw_load_int_ctx_info again to set the ALIVE
		 * interrupt (which is not really needed anymore) but also the
		 * RX interrupt which will allow us to receive the ALIVE
		 * notification (which is Rx) and continue the flow.
		 */
		if (top_reset)
			trans_pcie->inta_mask =  CSR_INT_BIT_RESET_DONE;
		else
			trans_pcie->inta_mask =  CSR_INT_BIT_ALIVE |
						 CSR_INT_BIT_FH_RX;
		iwl_write32(trans, CSR_INT_MASK, trans_pcie->inta_mask);
	} else {
		u32 val = top_reset ? MSIX_HW_INT_CAUSES_REG_RESET_DONE
				    : MSIX_HW_INT_CAUSES_REG_ALIVE;

		iwl_enable_hw_int_msk_msix(trans, val);

		if (top_reset)
			return;
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
			return DRV_NAME ":shared_IRQ";

		return devm_kasprintf(dev, GFP_KERNEL,
				      DRV_NAME ":queue_%d", i + vec);
	}
	if (i == 0)
		return DRV_NAME ":default_queue";

	if (i == trans_p->alloc_vecs - 1)
		return DRV_NAME ":exception";

	return devm_kasprintf(dev, GFP_KERNEL,
			      DRV_NAME  ":queue_%d", i);
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

	if (trans->mac_cfg->device_family >= IWL_DEVICE_FAMILY_9000) {
		/*
		 * On 9000-series devices this bit isn't enabled by default, so
		 * when we power down the device we need set the bit to allow it
		 * to wake up the PCI-E bus for RF-kill interrupts.
		 */
		iwl_set_bit(trans, CSR_GP_CNTRL,
			    CSR_GP_CNTRL_REG_FLAG_RFKILL_WAKE_L1A_EN);
	}
}

void iwl_pcie_handle_rfkill_irq(struct iwl_trans *trans, bool from_irq);

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

void iwl_trans_pcie_rf_kill(struct iwl_trans *trans, bool state, bool from_irq);

#ifdef CONFIG_IWLWIFI_DEBUGFS
void iwl_trans_pcie_dbgfs_register(struct iwl_trans *trans);
void iwl_trans_pcie_debugfs_cleanup(struct iwl_trans *trans);
#else
static inline void iwl_trans_pcie_dbgfs_register(struct iwl_trans *trans) { }
#endif

void iwl_pcie_rx_allocator_work(struct work_struct *data);

/* common trans ops for all generations transports */
void iwl_trans_pcie_op_mode_enter(struct iwl_trans *trans);
int _iwl_trans_pcie_start_hw(struct iwl_trans *trans);
int iwl_trans_pcie_start_hw(struct iwl_trans *trans);
void iwl_trans_pcie_op_mode_leave(struct iwl_trans *trans);
void iwl_trans_pcie_write8(struct iwl_trans *trans, u32 ofs, u8 val);
void iwl_trans_pcie_write32(struct iwl_trans *trans, u32 ofs, u32 val);
u32 iwl_trans_pcie_read32(struct iwl_trans *trans, u32 ofs);
u32 iwl_trans_pcie_read_prph(struct iwl_trans *trans, u32 reg);
void iwl_trans_pcie_write_prph(struct iwl_trans *trans, u32 addr, u32 val);
int iwl_trans_pcie_read_mem(struct iwl_trans *trans, u32 addr,
			    void *buf, int dwords);
int iwl_trans_pcie_write_mem(struct iwl_trans *trans, u32 addr,
			     const void *buf, int dwords);
int iwl_trans_pcie_sw_reset(struct iwl_trans *trans, bool retake_ownership);
struct iwl_trans_dump_data *
iwl_trans_pcie_dump_data(struct iwl_trans *trans, u32 dump_mask,
			 const struct iwl_dump_sanitize_ops *sanitize_ops,
			 void *sanitize_ctx);
int iwl_trans_pcie_d3_resume(struct iwl_trans *trans,
			     enum iwl_d3_status *status,
			     bool test,  bool reset);
int iwl_trans_pcie_d3_suspend(struct iwl_trans *trans, bool test, bool reset);
void iwl_trans_pci_interrupts(struct iwl_trans *trans, bool enable);
void iwl_trans_pcie_sync_nmi(struct iwl_trans *trans);
void iwl_trans_pcie_set_bits_mask(struct iwl_trans *trans, u32 reg,
				  u32 mask, u32 value);
int iwl_trans_pcie_read_config32(struct iwl_trans *trans, u32 ofs,
				 u32 *val);
bool iwl_trans_pcie_grab_nic_access(struct iwl_trans *trans);
void __releases(nic_access_nobh)
iwl_trans_pcie_release_nic_access(struct iwl_trans *trans);
void iwl_pcie_alloc_fw_monitor(struct iwl_trans *trans, u8 max_power);

/* transport gen 1 exported functions */
void iwl_trans_pcie_fw_alive(struct iwl_trans *trans);
int iwl_trans_pcie_start_fw(struct iwl_trans *trans,
			    const struct iwl_fw *fw,
			    const struct fw_img *img,
			    bool run_in_rfkill);
void iwl_trans_pcie_stop_device(struct iwl_trans *trans);

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

/* transport gen 2 exported functions */
int iwl_trans_pcie_gen2_start_fw(struct iwl_trans *trans,
				 const struct iwl_fw *fw,
				 const struct fw_img *img,
				 bool run_in_rfkill);
void iwl_trans_pcie_gen2_fw_alive(struct iwl_trans *trans);
void iwl_trans_pcie_gen2_stop_device(struct iwl_trans *trans);
int iwl_pcie_gen2_enqueue_hcmd(struct iwl_trans *trans,
			       struct iwl_host_cmd *cmd);
int iwl_pcie_enqueue_hcmd(struct iwl_trans *trans,
			  struct iwl_host_cmd *cmd);
void iwl_trans_pcie_copy_imr_fh(struct iwl_trans *trans,
				u32 dst_addr, u64 src_addr, u32 byte_cnt);
int iwl_trans_pcie_copy_imr(struct iwl_trans *trans,
			    u32 dst_addr, u64 src_addr, u32 byte_cnt);
int iwl_trans_pcie_rxq_dma_data(struct iwl_trans *trans, int queue,
				struct iwl_trans_rxq_dma_data *data);

#endif /* __iwl_trans_int_pcie_h__ */

/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef HABANALABSP_H_
#define HABANALABSP_H_

#include "../include/common/cpucp_if.h"
#include "../include/common/qman_if.h"
#include "../include/hw_ip/mmu/mmu_general.h"
#include <uapi/drm/habanalabs_accel.h>

#include <linux/cdev.h>
#include <linux/iopoll.h>
#include <linux/irqreturn.h>
#include <linux/dma-direction.h>
#include <linux/scatterlist.h>
#include <linux/hashtable.h>
#include <linux/debugfs.h>
#include <linux/rwsem.h>
#include <linux/eventfd.h>
#include <linux/bitfield.h>
#include <linux/genalloc.h>
#include <linux/sched/signal.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/coresight.h>
#include <linux/dma-buf.h>

#include "security.h"

#define HL_NAME				"habanalabs"

struct hl_device;
struct hl_fpriv;

#define PCI_VENDOR_ID_HABANALABS	0x1da3

/* Use upper bits of mmap offset to store habana driver specific information.
 * bits[63:59] - Encode mmap type
 * bits[45:0]  - mmap offset value
 *
 * NOTE: struct vm_area_struct.vm_pgoff uses offset in pages. Hence, these
 *  defines are w.r.t to PAGE_SIZE
 */
#define HL_MMAP_TYPE_SHIFT		(59 - PAGE_SHIFT)
#define HL_MMAP_TYPE_MASK		(0x1full << HL_MMAP_TYPE_SHIFT)
#define HL_MMAP_TYPE_TS_BUFF		(0x10ull << HL_MMAP_TYPE_SHIFT)
#define HL_MMAP_TYPE_BLOCK		(0x4ull << HL_MMAP_TYPE_SHIFT)
#define HL_MMAP_TYPE_CB			(0x2ull << HL_MMAP_TYPE_SHIFT)

#define HL_MMAP_OFFSET_VALUE_MASK	(0x1FFFFFFFFFFFull >> PAGE_SHIFT)
#define HL_MMAP_OFFSET_VALUE_GET(off)	(off & HL_MMAP_OFFSET_VALUE_MASK)

#define HL_PENDING_RESET_PER_SEC		10
#define HL_PENDING_RESET_MAX_TRIALS		60 /* 10 minutes */
#define HL_PENDING_RESET_LONG_SEC		60
/*
 * In device fini, wait 10 minutes for user processes to be terminated after we kill them.
 * This is needed to prevent situation of clearing resources while user processes are still alive.
 */
#define HL_WAIT_PROCESS_KILL_ON_DEVICE_FINI	600

#define HL_HARD_RESET_MAX_TIMEOUT	120
#define HL_PLDM_HARD_RESET_MAX_TIMEOUT	(HL_HARD_RESET_MAX_TIMEOUT * 3)

#define HL_DEVICE_TIMEOUT_USEC		1000000 /* 1 s */

#define HL_HEARTBEAT_PER_USEC		5000000 /* 5 s */

#define HL_PLL_LOW_JOB_FREQ_USEC	5000000 /* 5 s */

#define HL_CPUCP_INFO_TIMEOUT_USEC	10000000 /* 10s */
#define HL_CPUCP_EEPROM_TIMEOUT_USEC	10000000 /* 10s */
#define HL_CPUCP_MON_DUMP_TIMEOUT_USEC	10000000 /* 10s */
#define HL_CPUCP_SEC_ATTEST_INFO_TINEOUT_USEC 10000000 /* 10s */

#define HL_FW_STATUS_POLL_INTERVAL_USEC		10000 /* 10ms */
#define HL_FW_COMMS_STATUS_PLDM_POLL_INTERVAL_USEC	1000000 /* 1s */

#define HL_PCI_ELBI_TIMEOUT_MSEC	10 /* 10ms */

#define HL_SIM_MAX_TIMEOUT_US		100000000 /* 100s */

#define HL_INVALID_QUEUE		UINT_MAX

#define HL_COMMON_USER_CQ_INTERRUPT_ID	0xFFF
#define HL_COMMON_DEC_INTERRUPT_ID	0xFFE

#define HL_STATE_DUMP_HIST_LEN		5

/* Default value for device reset trigger , an invalid value */
#define HL_RESET_TRIGGER_DEFAULT	0xFF

#define OBJ_NAMES_HASH_TABLE_BITS	7 /* 1 << 7 buckets */
#define SYNC_TO_ENGINE_HASH_TABLE_BITS	7 /* 1 << 7 buckets */

/* Memory */
#define MEM_HASH_TABLE_BITS		7 /* 1 << 7 buckets */

/* MMU */
#define MMU_HASH_TABLE_BITS		7 /* 1 << 7 buckets */

/**
 * enum hl_mmu_page_table_location - mmu page table location
 * @MMU_DR_PGT: page-table is located on device DRAM.
 * @MMU_HR_PGT: page-table is located on host memory.
 * @MMU_NUM_PGT_LOCATIONS: number of page-table locations currently supported.
 */
enum hl_mmu_page_table_location {
	MMU_DR_PGT = 0,		/* device-dram-resident MMU PGT */
	MMU_HR_PGT,		/* host resident MMU PGT */
	MMU_NUM_PGT_LOCATIONS	/* num of PGT locations */
};

/*
 * HL_RSVD_SOBS 'sync stream' reserved sync objects per QMAN stream
 * HL_RSVD_MONS 'sync stream' reserved monitors per QMAN stream
 */
#define HL_RSVD_SOBS			2
#define HL_RSVD_MONS			1

/*
 * HL_COLLECTIVE_RSVD_MSTR_MONS 'collective' reserved monitors per QMAN stream
 */
#define HL_COLLECTIVE_RSVD_MSTR_MONS	2

#define HL_MAX_SOB_VAL			(1 << 15)

#define IS_POWER_OF_2(n)		(n != 0 && ((n & (n - 1)) == 0))
#define IS_MAX_PENDING_CS_VALID(n)	(IS_POWER_OF_2(n) && (n > 1))

#define HL_PCI_NUM_BARS			6

/* Completion queue entry relates to completed job */
#define HL_COMPLETION_MODE_JOB		0
/* Completion queue entry relates to completed command submission */
#define HL_COMPLETION_MODE_CS		1

#define HL_MAX_DCORES			8

/* DMA alloc/free wrappers */
#define hl_asic_dma_alloc_coherent(hdev, size, dma_handle, flags) \
	hl_asic_dma_alloc_coherent_caller(hdev, size, dma_handle, flags, __func__)

#define hl_asic_dma_pool_zalloc(hdev, size, mem_flags, dma_handle) \
	hl_asic_dma_pool_zalloc_caller(hdev, size, mem_flags, dma_handle, __func__)

#define hl_asic_dma_free_coherent(hdev, size, cpu_addr, dma_handle) \
	hl_asic_dma_free_coherent_caller(hdev, size, cpu_addr, dma_handle, __func__)

#define hl_asic_dma_pool_free(hdev, vaddr, dma_addr) \
	hl_asic_dma_pool_free_caller(hdev, vaddr, dma_addr, __func__)

/*
 * Reset Flags
 *
 * - HL_DRV_RESET_HARD
 *       If set do hard reset to all engines. If not set reset just
 *       compute/DMA engines.
 *
 * - HL_DRV_RESET_FROM_RESET_THR
 *       Set if the caller is the hard-reset thread
 *
 * - HL_DRV_RESET_HEARTBEAT
 *       Set if reset is due to heartbeat
 *
 * - HL_DRV_RESET_TDR
 *       Set if reset is due to TDR
 *
 * - HL_DRV_RESET_DEV_RELEASE
 *       Set if reset is due to device release
 *
 * - HL_DRV_RESET_BYPASS_REQ_TO_FW
 *       F/W will perform the reset. No need to ask it to reset the device. This is relevant
 *       only when running with secured f/w
 *
 * - HL_DRV_RESET_FW_FATAL_ERR
 *       Set if reset is due to a fatal error from FW
 *
 * - HL_DRV_RESET_DELAY
 *       Set if a delay should be added before the reset
 *
 * - HL_DRV_RESET_FROM_WD_THR
 *       Set if the caller is the device release watchdog thread
 */

#define HL_DRV_RESET_HARD		(1 << 0)
#define HL_DRV_RESET_FROM_RESET_THR	(1 << 1)
#define HL_DRV_RESET_HEARTBEAT		(1 << 2)
#define HL_DRV_RESET_TDR		(1 << 3)
#define HL_DRV_RESET_DEV_RELEASE	(1 << 4)
#define HL_DRV_RESET_BYPASS_REQ_TO_FW	(1 << 5)
#define HL_DRV_RESET_FW_FATAL_ERR	(1 << 6)
#define HL_DRV_RESET_DELAY		(1 << 7)
#define HL_DRV_RESET_FROM_WD_THR	(1 << 8)

/*
 * Security
 */

#define HL_PB_SHARED		1
#define HL_PB_NA		0
#define HL_PB_SINGLE_INSTANCE	1
#define HL_BLOCK_SIZE		0x1000
#define HL_BLOCK_GLBL_ERR_MASK	0xF40
#define HL_BLOCK_GLBL_ERR_ADDR	0xF44
#define HL_BLOCK_GLBL_ERR_CAUSE	0xF48
#define HL_BLOCK_GLBL_SEC_OFFS	0xF80
#define HL_BLOCK_GLBL_SEC_SIZE	(HL_BLOCK_SIZE - HL_BLOCK_GLBL_SEC_OFFS)
#define HL_BLOCK_GLBL_SEC_LEN	(HL_BLOCK_GLBL_SEC_SIZE / sizeof(u32))
#define UNSET_GLBL_SEC_BIT(array, b) ((array)[((b) / 32)] |= (1 << ((b) % 32)))

enum hl_protection_levels {
	SECURED_LVL,
	PRIVILEGED_LVL,
	NON_SECURED_LVL
};

/**
 * struct iterate_module_ctx - HW module iterator
 * @fn: function to apply to each HW module instance
 * @data: optional internal data to the function iterator
 * @rc: return code for optional use of iterator/iterator-caller
 */
struct iterate_module_ctx {
	/*
	 * callback for the HW module iterator
	 * @hdev: pointer to the habanalabs device structure
	 * @block: block (ASIC specific definition can be dcore/hdcore)
	 * @inst: HW module instance within the block
	 * @offset: current HW module instance offset from the 1-st HW module instance
	 *          in the 1-st block
	 * @ctx: the iterator context.
	 */
	void (*fn)(struct hl_device *hdev, int block, int inst, u32 offset,
			struct iterate_module_ctx *ctx);
	void *data;
	int rc;
};

struct hl_block_glbl_sec {
	u32 sec_array[HL_BLOCK_GLBL_SEC_LEN];
};

#define HL_MAX_SOBS_PER_MONITOR	8

/**
 * struct hl_gen_wait_properties - properties for generating a wait CB
 * @data: command buffer
 * @q_idx: queue id is used to extract fence register address
 * @size: offset in command buffer
 * @sob_base: SOB base to use in this wait CB
 * @sob_val: SOB value to wait for
 * @mon_id: monitor to use in this wait CB
 * @sob_mask: each bit represents a SOB offset from sob_base to be used
 */
struct hl_gen_wait_properties {
	void	*data;
	u32	q_idx;
	u32	size;
	u16	sob_base;
	u16	sob_val;
	u16	mon_id;
	u8	sob_mask;
};

/**
 * struct pgt_info - MMU hop page info.
 * @node: hash linked-list node for the pgts on host (shadow pgts for device resident MMU and
 *        actual pgts for host resident MMU).
 * @phys_addr: physical address of the pgt.
 * @virt_addr: host virtual address of the pgt (see above device/host resident).
 * @shadow_addr: shadow hop in the host for device resident MMU.
 * @ctx: pointer to the owner ctx.
 * @num_of_ptes: indicates how many ptes are used in the pgt. used only for dynamically
 *               allocated HOPs (all HOPs but HOP0)
 *
 * The MMU page tables hierarchy can be placed either on the device's DRAM (in which case shadow
 * pgts will be stored on host memory) or on host memory (in which case no shadow is required).
 *
 * When a new level (hop) is needed during mapping this structure will be used to describe
 * the newly allocated hop as well as to track number of PTEs in it.
 * During unmapping, if no valid PTEs remained in the page of a newly allocated hop, it is
 * freed with its pgt_info structure.
 */
struct pgt_info {
	struct hlist_node	node;
	u64			phys_addr;
	u64			virt_addr;
	u64			shadow_addr;
	struct hl_ctx		*ctx;
	int			num_of_ptes;
};

/**
 * enum hl_pci_match_mode - pci match mode per region
 * @PCI_ADDRESS_MATCH_MODE: address match mode
 * @PCI_BAR_MATCH_MODE: bar match mode
 */
enum hl_pci_match_mode {
	PCI_ADDRESS_MATCH_MODE,
	PCI_BAR_MATCH_MODE
};

/**
 * enum hl_fw_component - F/W components to read version through registers.
 * @FW_COMP_BOOT_FIT: boot fit.
 * @FW_COMP_PREBOOT: preboot.
 * @FW_COMP_LINUX: linux.
 */
enum hl_fw_component {
	FW_COMP_BOOT_FIT,
	FW_COMP_PREBOOT,
	FW_COMP_LINUX,
};

/**
 * enum hl_fw_types - F/W types present in the system
 * @FW_TYPE_NONE: no FW component indication
 * @FW_TYPE_LINUX: Linux image for device CPU
 * @FW_TYPE_BOOT_CPU: Boot image for device CPU
 * @FW_TYPE_PREBOOT_CPU: Indicates pre-loaded CPUs are present in the system
 *                       (preboot, ppboot etc...)
 * @FW_TYPE_ALL_TYPES: Mask for all types
 */
enum hl_fw_types {
	FW_TYPE_NONE = 0x0,
	FW_TYPE_LINUX = 0x1,
	FW_TYPE_BOOT_CPU = 0x2,
	FW_TYPE_PREBOOT_CPU = 0x4,
	FW_TYPE_ALL_TYPES =
		(FW_TYPE_LINUX | FW_TYPE_BOOT_CPU | FW_TYPE_PREBOOT_CPU)
};

/**
 * enum hl_queue_type - Supported QUEUE types.
 * @QUEUE_TYPE_NA: queue is not available.
 * @QUEUE_TYPE_EXT: external queue which is a DMA channel that may access the
 *                  host.
 * @QUEUE_TYPE_INT: internal queue that performs DMA inside the device's
 *			memories and/or operates the compute engines.
 * @QUEUE_TYPE_CPU: S/W queue for communication with the device's CPU.
 * @QUEUE_TYPE_HW: queue of DMA and compute engines jobs, for which completion
 *                 notifications are sent by H/W.
 */
enum hl_queue_type {
	QUEUE_TYPE_NA,
	QUEUE_TYPE_EXT,
	QUEUE_TYPE_INT,
	QUEUE_TYPE_CPU,
	QUEUE_TYPE_HW
};

enum hl_cs_type {
	CS_TYPE_DEFAULT,
	CS_TYPE_SIGNAL,
	CS_TYPE_WAIT,
	CS_TYPE_COLLECTIVE_WAIT,
	CS_RESERVE_SIGNALS,
	CS_UNRESERVE_SIGNALS,
	CS_TYPE_ENGINE_CORE,
	CS_TYPE_ENGINES,
	CS_TYPE_FLUSH_PCI_HBW_WRITES,
};

/*
 * struct hl_inbound_pci_region - inbound region descriptor
 * @mode: pci match mode for this region
 * @addr: region target address
 * @size: region size in bytes
 * @offset_in_bar: offset within bar (address match mode)
 * @bar: bar id
 */
struct hl_inbound_pci_region {
	enum hl_pci_match_mode	mode;
	u64			addr;
	u64			size;
	u64			offset_in_bar;
	u8			bar;
};

/*
 * struct hl_outbound_pci_region - outbound region descriptor
 * @addr: region target address
 * @size: region size in bytes
 */
struct hl_outbound_pci_region {
	u64	addr;
	u64	size;
};

/*
 * enum queue_cb_alloc_flags - Indicates queue support for CBs that
 * allocated by Kernel or by User
 * @CB_ALLOC_KERNEL: support only CBs that allocated by Kernel
 * @CB_ALLOC_USER: support only CBs that allocated by User
 */
enum queue_cb_alloc_flags {
	CB_ALLOC_KERNEL = 0x1,
	CB_ALLOC_USER   = 0x2
};

/*
 * struct hl_hw_sob - H/W SOB info.
 * @hdev: habanalabs device structure.
 * @kref: refcount of this SOB. The SOB will reset once the refcount is zero.
 * @sob_id: id of this SOB.
 * @sob_addr: the sob offset from the base address.
 * @q_idx: the H/W queue that uses this SOB.
 * @need_reset: reset indication set when switching to the other sob.
 */
struct hl_hw_sob {
	struct hl_device	*hdev;
	struct kref		kref;
	u32			sob_id;
	u32			sob_addr;
	u32			q_idx;
	bool			need_reset;
};

enum hl_collective_mode {
	HL_COLLECTIVE_NOT_SUPPORTED = 0x0,
	HL_COLLECTIVE_MASTER = 0x1,
	HL_COLLECTIVE_SLAVE = 0x2
};

/**
 * struct hw_queue_properties - queue information.
 * @type: queue type.
 * @cb_alloc_flags: bitmap which indicates if the hw queue supports CB
 *                  that allocated by the Kernel driver and therefore,
 *                  a CB handle can be provided for jobs on this queue.
 *                  Otherwise, a CB address must be provided.
 * @collective_mode: collective mode of current queue
 * @driver_only: true if only the driver is allowed to send a job to this queue,
 *               false otherwise.
 * @binned: True if the queue is binned out and should not be used
 * @supports_sync_stream: True if queue supports sync stream
 */
struct hw_queue_properties {
	enum hl_queue_type		type;
	enum queue_cb_alloc_flags	cb_alloc_flags;
	enum hl_collective_mode		collective_mode;
	u8				driver_only;
	u8				binned;
	u8				supports_sync_stream;
};

/**
 * enum vm_type - virtual memory mapping request information.
 * @VM_TYPE_USERPTR: mapping of user memory to device virtual address.
 * @VM_TYPE_PHYS_PACK: mapping of DRAM memory to device virtual address.
 */
enum vm_type {
	VM_TYPE_USERPTR = 0x1,
	VM_TYPE_PHYS_PACK = 0x2
};

/**
 * enum mmu_op_flags - mmu operation relevant information.
 * @MMU_OP_USERPTR: operation on user memory (host resident).
 * @MMU_OP_PHYS_PACK: operation on DRAM (device resident).
 * @MMU_OP_CLEAR_MEMCACHE: operation has to clear memcache.
 * @MMU_OP_SKIP_LOW_CACHE_INV: operation is allowed to skip parts of cache invalidation.
 */
enum mmu_op_flags {
	MMU_OP_USERPTR = 0x1,
	MMU_OP_PHYS_PACK = 0x2,
	MMU_OP_CLEAR_MEMCACHE = 0x4,
	MMU_OP_SKIP_LOW_CACHE_INV = 0x8,
};


/**
 * enum hl_device_hw_state - H/W device state. use this to understand whether
 *                           to do reset before hw_init or not
 * @HL_DEVICE_HW_STATE_CLEAN: H/W state is clean. i.e. after hard reset
 * @HL_DEVICE_HW_STATE_DIRTY: H/W state is dirty. i.e. we started to execute
 *                            hw_init
 */
enum hl_device_hw_state {
	HL_DEVICE_HW_STATE_CLEAN = 0,
	HL_DEVICE_HW_STATE_DIRTY
};

#define HL_MMU_VA_ALIGNMENT_NOT_NEEDED 0

/**
 * struct hl_mmu_properties - ASIC specific MMU address translation properties.
 * @start_addr: virtual start address of the memory region.
 * @end_addr: virtual end address of the memory region.
 * @hop_shifts: array holds HOPs shifts.
 * @hop_masks: array holds HOPs masks.
 * @last_mask: mask to get the bit indicating this is the last hop.
 * @pgt_size: size for page tables.
 * @supported_pages_mask: bitmask for supported page size (relevant only for MMUs
 *                        supporting multiple page size).
 * @page_size: default page size used to allocate memory.
 * @num_hops: The amount of hops supported by the translation table.
 * @hop_table_size: HOP table size.
 * @hop0_tables_total_size: total size for all HOP0 tables.
 * @host_resident: Should the MMU page table reside in host memory or in the
 *                 device DRAM.
 */
struct hl_mmu_properties {
	u64	start_addr;
	u64	end_addr;
	u64	hop_shifts[MMU_HOP_MAX];
	u64	hop_masks[MMU_HOP_MAX];
	u64	last_mask;
	u64	pgt_size;
	u64	supported_pages_mask;
	u32	page_size;
	u32	num_hops;
	u32	hop_table_size;
	u32	hop0_tables_total_size;
	u8	host_resident;
};

/**
 * struct hl_hints_range - hint addresses reserved va range.
 * @start_addr: start address of the va range.
 * @end_addr: end address of the va range.
 */
struct hl_hints_range {
	u64 start_addr;
	u64 end_addr;
};

/**
 * struct asic_fixed_properties - ASIC specific immutable properties.
 * @hw_queues_props: H/W queues properties.
 * @special_blocks: points to an array containing special blocks info.
 * @skip_special_blocks_cfg: special blocks skip configs.
 * @cpucp_info: received various information from CPU-CP regarding the H/W, e.g.
 *		available sensors.
 * @uboot_ver: F/W U-boot version.
 * @preboot_ver: F/W Preboot version.
 * @dmmu: DRAM MMU address translation properties.
 * @pmmu: PCI (host) MMU address translation properties.
 * @pmmu_huge: PCI (host) MMU address translation properties for memory
 *              allocated with huge pages.
 * @hints_dram_reserved_va_range: dram hint addresses reserved range.
 * @hints_host_reserved_va_range: host hint addresses reserved range.
 * @hints_host_hpage_reserved_va_range: host huge page hint addresses reserved
 *                                      range.
 * @sram_base_address: SRAM physical start address.
 * @sram_end_address: SRAM physical end address.
 * @sram_user_base_address - SRAM physical start address for user access.
 * @dram_base_address: DRAM physical start address.
 * @dram_end_address: DRAM physical end address.
 * @dram_user_base_address: DRAM physical start address for user access.
 * @dram_size: DRAM total size.
 * @dram_pci_bar_size: size of PCI bar towards DRAM.
 * @max_power_default: max power of the device after reset.
 * @dc_power_default: power consumed by the device in mode idle.
 * @dram_size_for_default_page_mapping: DRAM size needed to map to avoid page
 *                                      fault.
 * @pcie_dbi_base_address: Base address of the PCIE_DBI block.
 * @pcie_aux_dbi_reg_addr: Address of the PCIE_AUX DBI register.
 * @mmu_pgt_addr: base physical address in DRAM of MMU page tables.
 * @mmu_dram_default_page_addr: DRAM default page physical address.
 * @tpc_enabled_mask: which TPCs are enabled.
 * @tpc_binning_mask: which TPCs are binned. 0 means usable and 1 means binned.
 * @dram_enabled_mask: which DRAMs are enabled.
 * @dram_binning_mask: which DRAMs are binned. 0 means usable, 1 means binned.
 * @dram_hints_align_mask: dram va hint addresses alignment mask which is used
 *                  for hints validity check.
 * @cfg_base_address: config space base address.
 * @mmu_cache_mng_addr: address of the MMU cache.
 * @mmu_cache_mng_size: size of the MMU cache.
 * @device_dma_offset_for_host_access: the offset to add to host DMA addresses
 *                                     to enable the device to access them.
 * @host_base_address: host physical start address for host DMA from device
 * @host_end_address: host physical end address for host DMA from device
 * @max_freq_value: current max clk frequency.
 * @engine_core_interrupt_reg_addr: interrupt register address for engine core to use
 *                                  in order to raise events toward FW.
 * @clk_pll_index: clock PLL index that specify which PLL determines the clock
 *                 we display to the user
 * @mmu_pgt_size: MMU page tables total size.
 * @mmu_pte_size: PTE size in MMU page tables.
 * @mmu_hop_table_size: MMU hop table size.
 * @mmu_hop0_tables_total_size: total size of MMU hop0 tables.
 * @dram_page_size: page size for MMU DRAM allocation.
 * @cfg_size: configuration space size on SRAM.
 * @sram_size: total size of SRAM.
 * @max_asid: maximum number of open contexts (ASIDs).
 * @num_of_events: number of possible internal H/W IRQs.
 * @psoc_pci_pll_nr: PCI PLL NR value.
 * @psoc_pci_pll_nf: PCI PLL NF value.
 * @psoc_pci_pll_od: PCI PLL OD value.
 * @psoc_pci_pll_div_factor: PCI PLL DIV FACTOR 1 value.
 * @psoc_timestamp_frequency: frequency of the psoc timestamp clock.
 * @high_pll: high PLL frequency used by the device.
 * @cb_pool_cb_cnt: number of CBs in the CB pool.
 * @cb_pool_cb_size: size of each CB in the CB pool.
 * @decoder_enabled_mask: which decoders are enabled.
 * @decoder_binning_mask: which decoders are binned, 0 means usable and 1 means binned.
 * @rotator_enabled_mask: which rotators are enabled.
 * @edma_enabled_mask: which EDMAs are enabled.
 * @edma_binning_mask: which EDMAs are binned, 0 means usable and 1 means
 *                     binned (at most one binned DMA).
 * @max_pending_cs: maximum of concurrent pending command submissions
 * @max_queues: maximum amount of queues in the system
 * @fw_preboot_cpu_boot_dev_sts0: bitmap representation of preboot cpu
 *                                capabilities reported by FW, bit description
 *                                can be found in CPU_BOOT_DEV_STS0
 * @fw_preboot_cpu_boot_dev_sts1: bitmap representation of preboot cpu
 *                                capabilities reported by FW, bit description
 *                                can be found in CPU_BOOT_DEV_STS1
 * @fw_bootfit_cpu_boot_dev_sts0: bitmap representation of boot cpu security
 *                                status reported by FW, bit description can be
 *                                found in CPU_BOOT_DEV_STS0
 * @fw_bootfit_cpu_boot_dev_sts1: bitmap representation of boot cpu security
 *                                status reported by FW, bit description can be
 *                                found in CPU_BOOT_DEV_STS1
 * @fw_app_cpu_boot_dev_sts0: bitmap representation of application security
 *                            status reported by FW, bit description can be
 *                            found in CPU_BOOT_DEV_STS0
 * @fw_app_cpu_boot_dev_sts1: bitmap representation of application security
 *                            status reported by FW, bit description can be
 *                            found in CPU_BOOT_DEV_STS1
 * @max_dec: maximum number of decoders
 * @hmmu_hif_enabled_mask: mask of HMMUs/HIFs that are not isolated (enabled)
 *                         1- enabled, 0- isolated.
 * @faulty_dram_cluster_map: mask of faulty DRAM cluster.
 *                         1- faulty cluster, 0- good cluster.
 * @xbar_edge_enabled_mask: mask of XBAR_EDGEs that are not isolated (enabled)
 *                          1- enabled, 0- isolated.
 * @device_mem_alloc_default_page_size: may be different than dram_page_size only for ASICs for
 *                                      which the property supports_user_set_page_size is true
 *                                      (i.e. the DRAM supports multiple page sizes), otherwise
 *                                      it will shall  be equal to dram_page_size.
 * @num_engine_cores: number of engine cpu cores.
 * @max_num_of_engines: maximum number of all engines in the ASIC.
 * @num_of_special_blocks: special_blocks array size.
 * @glbl_err_cause_num: global err cause number.
 * @hbw_flush_reg: register to read to generate HBW flush. value of 0 means HBW flush is
 *                 not supported.
 * @collective_first_sob: first sync object available for collective use
 * @collective_first_mon: first monitor available for collective use
 * @sync_stream_first_sob: first sync object available for sync stream use
 * @sync_stream_first_mon: first monitor available for sync stream use
 * @first_available_user_sob: first sob available for the user
 * @first_available_user_mon: first monitor available for the user
 * @first_available_user_interrupt: first available interrupt reserved for the user
 * @first_available_cq: first available CQ for the user.
 * @user_interrupt_count: number of user interrupts.
 * @user_dec_intr_count: number of decoder interrupts exposed to user.
 * @tpc_interrupt_id: interrupt id for TPC to use in order to raise events towards the host.
 * @eq_interrupt_id: interrupt id for EQ, uses to synchronize EQ interrupts in hard-reset.
 * @cache_line_size: device cache line size.
 * @server_type: Server type that the ASIC is currently installed in.
 *               The value is according to enum hl_server_type in uapi file.
 * @completion_queues_count: number of completion queues.
 * @completion_mode: 0 - job based completion, 1 - cs based completion
 * @mme_master_slave_mode: 0 - Each MME works independently, 1 - MME works
 *                         in Master/Slave mode
 * @fw_security_enabled: true if security measures are enabled in firmware,
 *                       false otherwise
 * @fw_cpu_boot_dev_sts0_valid: status bits are valid and can be fetched from
 *                              BOOT_DEV_STS0
 * @fw_cpu_boot_dev_sts1_valid: status bits are valid and can be fetched from
 *                              BOOT_DEV_STS1
 * @dram_supports_virtual_memory: is there an MMU towards the DRAM
 * @hard_reset_done_by_fw: true if firmware is handling hard reset flow
 * @num_functional_hbms: number of functional HBMs in each DCORE.
 * @hints_range_reservation: device support hint addresses range reservation.
 * @iatu_done_by_fw: true if iATU configuration is being done by FW.
 * @dynamic_fw_load: is dynamic FW load is supported.
 * @gic_interrupts_enable: true if FW is not blocking GIC controller,
 *                         false otherwise.
 * @use_get_power_for_reset_history: To support backward compatibility for Goya
 *                                   and Gaudi
 * @supports_compute_reset: is a reset which is not a hard-reset supported by this asic.
 * @allow_inference_soft_reset: true if the ASIC supports soft reset that is
 *                              initiated by user or TDR. This is only true
 *                              in inference ASICs, as there is no real-world
 *                              use-case of doing soft-reset in training (due
 *                              to the fact that training runs on multiple
 *                              devices)
 * @configurable_stop_on_err: is stop-on-error option configurable via debugfs.
 * @set_max_power_on_device_init: true if need to set max power in F/W on device init.
 * @supports_user_set_page_size: true if user can set the allocation page size.
 * @dma_mask: the dma mask to be set for this device
 * @supports_advanced_cpucp_rc: true if new cpucp opcodes are supported.
 * @supports_engine_modes: true if changing engines/engine_cores modes is supported.
 */
struct asic_fixed_properties {
	struct hw_queue_properties	*hw_queues_props;
	struct hl_special_block_info	*special_blocks;
	struct hl_skip_blocks_cfg	skip_special_blocks_cfg;
	struct cpucp_info		cpucp_info;
	char				uboot_ver[VERSION_MAX_LEN];
	char				preboot_ver[VERSION_MAX_LEN];
	struct hl_mmu_properties	dmmu;
	struct hl_mmu_properties	pmmu;
	struct hl_mmu_properties	pmmu_huge;
	struct hl_hints_range		hints_dram_reserved_va_range;
	struct hl_hints_range		hints_host_reserved_va_range;
	struct hl_hints_range		hints_host_hpage_reserved_va_range;
	u64				sram_base_address;
	u64				sram_end_address;
	u64				sram_user_base_address;
	u64				dram_base_address;
	u64				dram_end_address;
	u64				dram_user_base_address;
	u64				dram_size;
	u64				dram_pci_bar_size;
	u64				max_power_default;
	u64				dc_power_default;
	u64				dram_size_for_default_page_mapping;
	u64				pcie_dbi_base_address;
	u64				pcie_aux_dbi_reg_addr;
	u64				mmu_pgt_addr;
	u64				mmu_dram_default_page_addr;
	u64				tpc_enabled_mask;
	u64				tpc_binning_mask;
	u64				dram_enabled_mask;
	u64				dram_binning_mask;
	u64				dram_hints_align_mask;
	u64				cfg_base_address;
	u64				mmu_cache_mng_addr;
	u64				mmu_cache_mng_size;
	u64				device_dma_offset_for_host_access;
	u64				host_base_address;
	u64				host_end_address;
	u64				max_freq_value;
	u64				engine_core_interrupt_reg_addr;
	u32				clk_pll_index;
	u32				mmu_pgt_size;
	u32				mmu_pte_size;
	u32				mmu_hop_table_size;
	u32				mmu_hop0_tables_total_size;
	u32				dram_page_size;
	u32				cfg_size;
	u32				sram_size;
	u32				max_asid;
	u32				num_of_events;
	u32				psoc_pci_pll_nr;
	u32				psoc_pci_pll_nf;
	u32				psoc_pci_pll_od;
	u32				psoc_pci_pll_div_factor;
	u32				psoc_timestamp_frequency;
	u32				high_pll;
	u32				cb_pool_cb_cnt;
	u32				cb_pool_cb_size;
	u32				decoder_enabled_mask;
	u32				decoder_binning_mask;
	u32				rotator_enabled_mask;
	u32				edma_enabled_mask;
	u32				edma_binning_mask;
	u32				max_pending_cs;
	u32				max_queues;
	u32				fw_preboot_cpu_boot_dev_sts0;
	u32				fw_preboot_cpu_boot_dev_sts1;
	u32				fw_bootfit_cpu_boot_dev_sts0;
	u32				fw_bootfit_cpu_boot_dev_sts1;
	u32				fw_app_cpu_boot_dev_sts0;
	u32				fw_app_cpu_boot_dev_sts1;
	u32				max_dec;
	u32				hmmu_hif_enabled_mask;
	u32				faulty_dram_cluster_map;
	u32				xbar_edge_enabled_mask;
	u32				device_mem_alloc_default_page_size;
	u32				num_engine_cores;
	u32				max_num_of_engines;
	u32				num_of_special_blocks;
	u32				glbl_err_cause_num;
	u32				hbw_flush_reg;
	u16				collective_first_sob;
	u16				collective_first_mon;
	u16				sync_stream_first_sob;
	u16				sync_stream_first_mon;
	u16				first_available_user_sob[HL_MAX_DCORES];
	u16				first_available_user_mon[HL_MAX_DCORES];
	u16				first_available_user_interrupt;
	u16				first_available_cq[HL_MAX_DCORES];
	u16				user_interrupt_count;
	u16				user_dec_intr_count;
	u16				tpc_interrupt_id;
	u16				eq_interrupt_id;
	u16				cache_line_size;
	u16				server_type;
	u8				completion_queues_count;
	u8				completion_mode;
	u8				mme_master_slave_mode;
	u8				fw_security_enabled;
	u8				fw_cpu_boot_dev_sts0_valid;
	u8				fw_cpu_boot_dev_sts1_valid;
	u8				dram_supports_virtual_memory;
	u8				hard_reset_done_by_fw;
	u8				num_functional_hbms;
	u8				hints_range_reservation;
	u8				iatu_done_by_fw;
	u8				dynamic_fw_load;
	u8				gic_interrupts_enable;
	u8				use_get_power_for_reset_history;
	u8				supports_compute_reset;
	u8				allow_inference_soft_reset;
	u8				configurable_stop_on_err;
	u8				set_max_power_on_device_init;
	u8				supports_user_set_page_size;
	u8				dma_mask;
	u8				supports_advanced_cpucp_rc;
	u8				supports_engine_modes;
};

/**
 * struct hl_fence - software synchronization primitive
 * @completion: fence is implemented using completion
 * @refcount: refcount for this fence
 * @cs_sequence: sequence of the corresponding command submission
 * @stream_master_qid_map: streams masters QID bitmap to represent all streams
 *                         masters QIDs that multi cs is waiting on
 * @error: mark this fence with error
 * @timestamp: timestamp upon completion
 * @mcs_handling_done: indicates that corresponding command submission has
 *                     finished msc handling, this does not mean it was part
 *                     of the mcs
 */
struct hl_fence {
	struct completion	completion;
	struct kref		refcount;
	u64			cs_sequence;
	u32			stream_master_qid_map;
	int			error;
	ktime_t			timestamp;
	u8			mcs_handling_done;
};

/**
 * struct hl_cs_compl - command submission completion object.
 * @base_fence: hl fence object.
 * @lock: spinlock to protect fence.
 * @hdev: habanalabs device structure.
 * @hw_sob: the H/W SOB used in this signal/wait CS.
 * @encaps_sig_hdl: encaps signals handler.
 * @cs_seq: command submission sequence number.
 * @type: type of the CS - signal/wait.
 * @sob_val: the SOB value that is used in this signal/wait CS.
 * @sob_group: the SOB group that is used in this collective wait CS.
 * @encaps_signals: indication whether it's a completion object of cs with
 * encaps signals or not.
 */
struct hl_cs_compl {
	struct hl_fence		base_fence;
	spinlock_t		lock;
	struct hl_device	*hdev;
	struct hl_hw_sob	*hw_sob;
	struct hl_cs_encaps_sig_handle *encaps_sig_hdl;
	u64			cs_seq;
	enum hl_cs_type		type;
	u16			sob_val;
	u16			sob_group;
	bool			encaps_signals;
};

/*
 * Command Buffers
 */

/**
 * struct hl_ts_buff - describes a timestamp buffer.
 * @kernel_buff_address: Holds the internal buffer's kernel virtual address.
 * @user_buff_address: Holds the user buffer's kernel virtual address.
 * @kernel_buff_size: Holds the internal kernel buffer size.
 */
struct hl_ts_buff {
	void			*kernel_buff_address;
	void			*user_buff_address;
	u32			kernel_buff_size;
};

struct hl_mmap_mem_buf;

/**
 * struct hl_mem_mgr - describes unified memory manager for mappable memory chunks.
 * @dev: back pointer to the owning device
 * @lock: protects handles
 * @handles: an idr holding all active handles to the memory buffers in the system.
 */
struct hl_mem_mgr {
	struct device *dev;
	spinlock_t lock;
	struct idr handles;
};

/**
 * struct hl_mmap_mem_buf_behavior - describes unified memory manager buffer behavior
 * @topic: string identifier used for logging
 * @mem_id: memory type identifier, embedded in the handle and used to identify
 *          the memory type by handle.
 * @alloc: callback executed on buffer allocation, shall allocate the memory,
 *         set it under buffer private, and set mappable size.
 * @mmap: callback executed on mmap, must map the buffer to vma
 * @release: callback executed on release, must free the resources used by the buffer
 */
struct hl_mmap_mem_buf_behavior {
	const char *topic;
	u64 mem_id;

	int (*alloc)(struct hl_mmap_mem_buf *buf, gfp_t gfp, void *args);
	int (*mmap)(struct hl_mmap_mem_buf *buf, struct vm_area_struct *vma, void *args);
	void (*release)(struct hl_mmap_mem_buf *buf);
};

/**
 * struct hl_mmap_mem_buf - describes a single unified memory buffer
 * @behavior: buffer behavior
 * @mmg: back pointer to the unified memory manager
 * @refcount: reference counter for buffer users
 * @private: pointer to buffer behavior private data
 * @mmap: atomic boolean indicating whether or not the buffer is mapped right now
 * @real_mapped_size: the actual size of buffer mapped, after part of it may be released,
 *                   may change at runtime.
 * @mappable_size: the original mappable size of the buffer, does not change after
 *                 the allocation.
 * @handle: the buffer id in mmg handles store
 */
struct hl_mmap_mem_buf {
	struct hl_mmap_mem_buf_behavior *behavior;
	struct hl_mem_mgr *mmg;
	struct kref refcount;
	void *private;
	atomic_t mmap;
	u64 real_mapped_size;
	u64 mappable_size;
	u64 handle;
};

/**
 * struct hl_cb - describes a Command Buffer.
 * @hdev: pointer to device this CB belongs to.
 * @ctx: pointer to the CB owner's context.
 * @buf: back pointer to the parent mappable memory buffer
 * @debugfs_list: node in debugfs list of command buffers.
 * @pool_list: node in pool list of command buffers.
 * @kernel_address: Holds the CB's kernel virtual address.
 * @virtual_addr: Holds the CB's virtual address.
 * @bus_address: Holds the CB's DMA address.
 * @size: holds the CB's size.
 * @roundup_size: holds the cb size after roundup to page size.
 * @cs_cnt: holds number of CS that this CB participates in.
 * @is_handle_destroyed: atomic boolean indicating whether or not the CB handle was destroyed.
 * @is_pool: true if CB was acquired from the pool, false otherwise.
 * @is_internal: internally allocated
 * @is_mmu_mapped: true if the CB is mapped to the device's MMU.
 */
struct hl_cb {
	struct hl_device	*hdev;
	struct hl_ctx		*ctx;
	struct hl_mmap_mem_buf	*buf;
	struct list_head	debugfs_list;
	struct list_head	pool_list;
	void			*kernel_address;
	u64			virtual_addr;
	dma_addr_t		bus_address;
	u32			size;
	u32			roundup_size;
	atomic_t		cs_cnt;
	atomic_t		is_handle_destroyed;
	u8			is_pool;
	u8			is_internal;
	u8			is_mmu_mapped;
};


/*
 * QUEUES
 */

struct hl_cs_job;

/* Queue length of external and HW queues */
#define HL_QUEUE_LENGTH			4096
#define HL_QUEUE_SIZE_IN_BYTES		(HL_QUEUE_LENGTH * HL_BD_SIZE)

#if (HL_MAX_JOBS_PER_CS > HL_QUEUE_LENGTH)
#error "HL_QUEUE_LENGTH must be greater than HL_MAX_JOBS_PER_CS"
#endif

/* HL_CQ_LENGTH is in units of struct hl_cq_entry */
#define HL_CQ_LENGTH			HL_QUEUE_LENGTH
#define HL_CQ_SIZE_IN_BYTES		(HL_CQ_LENGTH * HL_CQ_ENTRY_SIZE)

/* Must be power of 2 */
#define HL_EQ_LENGTH			64
#define HL_EQ_SIZE_IN_BYTES		(HL_EQ_LENGTH * HL_EQ_ENTRY_SIZE)

/* Host <-> CPU-CP shared memory size */
#define HL_CPU_ACCESSIBLE_MEM_SIZE	SZ_2M

/**
 * struct hl_sync_stream_properties -
 *     describes a H/W queue sync stream properties
 * @hw_sob: array of the used H/W SOBs by this H/W queue.
 * @next_sob_val: the next value to use for the currently used SOB.
 * @base_sob_id: the base SOB id of the SOBs used by this queue.
 * @base_mon_id: the base MON id of the MONs used by this queue.
 * @collective_mstr_mon_id: the MON ids of the MONs used by this master queue
 *                          in order to sync with all slave queues.
 * @collective_slave_mon_id: the MON id used by this slave queue in order to
 *                           sync with its master queue.
 * @collective_sob_id: current SOB id used by this collective slave queue
 *                     to signal its collective master queue upon completion.
 * @curr_sob_offset: the id offset to the currently used SOB from the
 *                   HL_RSVD_SOBS that are being used by this queue.
 */
struct hl_sync_stream_properties {
	struct hl_hw_sob hw_sob[HL_RSVD_SOBS];
	u16		next_sob_val;
	u16		base_sob_id;
	u16		base_mon_id;
	u16		collective_mstr_mon_id[HL_COLLECTIVE_RSVD_MSTR_MONS];
	u16		collective_slave_mon_id;
	u16		collective_sob_id;
	u8		curr_sob_offset;
};

/**
 * struct hl_encaps_signals_mgr - describes sync stream encapsulated signals
 * handlers manager
 * @lock: protects handles.
 * @handles: an idr to hold all encapsulated signals handles.
 */
struct hl_encaps_signals_mgr {
	spinlock_t		lock;
	struct idr		handles;
};

/**
 * struct hl_hw_queue - describes a H/W transport queue.
 * @shadow_queue: pointer to a shadow queue that holds pointers to jobs.
 * @sync_stream_prop: sync stream queue properties
 * @queue_type: type of queue.
 * @collective_mode: collective mode of current queue
 * @kernel_address: holds the queue's kernel virtual address.
 * @bus_address: holds the queue's DMA address.
 * @pi: holds the queue's pi value.
 * @ci: holds the queue's ci value, AS CALCULATED BY THE DRIVER (not real ci).
 * @hw_queue_id: the id of the H/W queue.
 * @cq_id: the id for the corresponding CQ for this H/W queue.
 * @msi_vec: the IRQ number of the H/W queue.
 * @int_queue_len: length of internal queue (number of entries).
 * @valid: is the queue valid (we have array of 32 queues, not all of them
 *         exist).
 * @supports_sync_stream: True if queue supports sync stream
 */
struct hl_hw_queue {
	struct hl_cs_job			**shadow_queue;
	struct hl_sync_stream_properties	sync_stream_prop;
	enum hl_queue_type			queue_type;
	enum hl_collective_mode			collective_mode;
	void					*kernel_address;
	dma_addr_t				bus_address;
	u32					pi;
	atomic_t				ci;
	u32					hw_queue_id;
	u32					cq_id;
	u32					msi_vec;
	u16					int_queue_len;
	u8					valid;
	u8					supports_sync_stream;
};

/**
 * struct hl_cq - describes a completion queue
 * @hdev: pointer to the device structure
 * @kernel_address: holds the queue's kernel virtual address
 * @bus_address: holds the queue's DMA address
 * @cq_idx: completion queue index in array
 * @hw_queue_id: the id of the matching H/W queue
 * @ci: ci inside the queue
 * @pi: pi inside the queue
 * @free_slots_cnt: counter of free slots in queue
 */
struct hl_cq {
	struct hl_device	*hdev;
	void			*kernel_address;
	dma_addr_t		bus_address;
	u32			cq_idx;
	u32			hw_queue_id;
	u32			ci;
	u32			pi;
	atomic_t		free_slots_cnt;
};

enum hl_user_interrupt_type {
	HL_USR_INTERRUPT_CQ = 0,
	HL_USR_INTERRUPT_DECODER,
	HL_USR_INTERRUPT_TPC,
	HL_USR_INTERRUPT_UNEXPECTED
};

/**
 * struct hl_user_interrupt - holds user interrupt information
 * @hdev: pointer to the device structure
 * @type: user interrupt type
 * @wait_list_head: head to the list of user threads pending on this interrupt
 * @wait_list_lock: protects wait_list_head
 * @timestamp: last timestamp taken upon interrupt
 * @interrupt_id: msix interrupt id
 */
struct hl_user_interrupt {
	struct hl_device		*hdev;
	enum hl_user_interrupt_type	type;
	struct list_head		wait_list_head;
	spinlock_t			wait_list_lock;
	ktime_t				timestamp;
	u32				interrupt_id;
};

/**
 * struct timestamp_reg_free_node - holds the timestamp registration free objects node
 * @free_objects_node: node in the list free_obj_jobs
 * @cq_cb: pointer to cq command buffer to be freed
 * @buf: pointer to timestamp buffer to be freed
 */
struct timestamp_reg_free_node {
	struct list_head	free_objects_node;
	struct hl_cb		*cq_cb;
	struct hl_mmap_mem_buf	*buf;
};

/* struct timestamp_reg_work_obj - holds the timestamp registration free objects job
 * the job will be to pass over the free_obj_jobs list and put refcount to objects
 * in each node of the list
 * @free_obj: workqueue object to free timestamp registration node objects
 * @hdev: pointer to the device structure
 * @free_obj_head: list of free jobs nodes (node type timestamp_reg_free_node)
 */
struct timestamp_reg_work_obj {
	struct work_struct	free_obj;
	struct hl_device	*hdev;
	struct list_head	*free_obj_head;
};

/* struct timestamp_reg_info - holds the timestamp registration related data.
 * @buf: pointer to the timestamp buffer which include both user/kernel buffers.
 *       relevant only when doing timestamps records registration.
 * @cq_cb: pointer to CQ counter CB.
 * @timestamp_kernel_addr: timestamp handle address, where to set timestamp
 *                         relevant only when doing timestamps records
 *                         registration.
 * @in_use: indicates if the node already in use. relevant only when doing
 *          timestamps records registration, since in this case the driver
 *          will have it's own buffer which serve as a records pool instead of
 *          allocating records dynamically.
 */
struct timestamp_reg_info {
	struct hl_mmap_mem_buf	*buf;
	struct hl_cb		*cq_cb;
	u64			*timestamp_kernel_addr;
	u8			in_use;
};

/**
 * struct hl_user_pending_interrupt - holds a context to a user thread
 *                                    pending on an interrupt
 * @ts_reg_info: holds the timestamps registration nodes info
 * @wait_list_node: node in the list of user threads pending on an interrupt
 * @fence: hl fence object for interrupt completion
 * @cq_target_value: CQ target value
 * @cq_kernel_addr: CQ kernel address, to be used in the cq interrupt
 *                  handler for target value comparison
 */
struct hl_user_pending_interrupt {
	struct timestamp_reg_info	ts_reg_info;
	struct list_head		wait_list_node;
	struct hl_fence			fence;
	u64				cq_target_value;
	u64				*cq_kernel_addr;
};

/**
 * struct hl_eq - describes the event queue (single one per device)
 * @hdev: pointer to the device structure
 * @kernel_address: holds the queue's kernel virtual address
 * @bus_address: holds the queue's DMA address
 * @ci: ci inside the queue
 * @prev_eqe_index: the index of the previous event queue entry. The index of
 *                  the current entry's index must be +1 of the previous one.
 * @check_eqe_index: do we need to check the index of the current entry vs. the
 *                   previous one. This is for backward compatibility with older
 *                   firmwares
 */
struct hl_eq {
	struct hl_device	*hdev;
	void			*kernel_address;
	dma_addr_t		bus_address;
	u32			ci;
	u32			prev_eqe_index;
	bool			check_eqe_index;
};

/**
 * struct hl_dec - describes a decoder sw instance.
 * @hdev: pointer to the device structure.
 * @abnrm_intr_work: workqueue work item to run when decoder generates an error interrupt.
 * @core_id: ID of the decoder.
 * @base_addr: base address of the decoder.
 */
struct hl_dec {
	struct hl_device	*hdev;
	struct work_struct	abnrm_intr_work;
	u32			core_id;
	u32			base_addr;
};

/**
 * enum hl_asic_type - supported ASIC types.
 * @ASIC_INVALID: Invalid ASIC type.
 * @ASIC_GOYA: Goya device (HL-1000).
 * @ASIC_GAUDI: Gaudi device (HL-2000).
 * @ASIC_GAUDI_SEC: Gaudi secured device (HL-2000).
 * @ASIC_GAUDI2: Gaudi2 device.
 * @ASIC_GAUDI2B: Gaudi2B device.
 */
enum hl_asic_type {
	ASIC_INVALID,
	ASIC_GOYA,
	ASIC_GAUDI,
	ASIC_GAUDI_SEC,
	ASIC_GAUDI2,
	ASIC_GAUDI2B,
};

struct hl_cs_parser;

/**
 * enum hl_pm_mng_profile - power management profile.
 * @PM_AUTO: internal clock is set by the Linux driver.
 * @PM_MANUAL: internal clock is set by the user.
 * @PM_LAST: last power management type.
 */
enum hl_pm_mng_profile {
	PM_AUTO = 1,
	PM_MANUAL,
	PM_LAST
};

/**
 * enum hl_pll_frequency - PLL frequency.
 * @PLL_HIGH: high frequency.
 * @PLL_LOW: low frequency.
 * @PLL_LAST: last frequency values that were configured by the user.
 */
enum hl_pll_frequency {
	PLL_HIGH = 1,
	PLL_LOW,
	PLL_LAST
};

#define PLL_REF_CLK 50

enum div_select_defs {
	DIV_SEL_REF_CLK = 0,
	DIV_SEL_PLL_CLK = 1,
	DIV_SEL_DIVIDED_REF = 2,
	DIV_SEL_DIVIDED_PLL = 3,
};

enum debugfs_access_type {
	DEBUGFS_READ8,
	DEBUGFS_WRITE8,
	DEBUGFS_READ32,
	DEBUGFS_WRITE32,
	DEBUGFS_READ64,
	DEBUGFS_WRITE64,
};

enum pci_region {
	PCI_REGION_CFG,
	PCI_REGION_SRAM,
	PCI_REGION_DRAM,
	PCI_REGION_SP_SRAM,
	PCI_REGION_NUMBER,
};

/**
 * struct pci_mem_region - describe memory region in a PCI bar
 * @region_base: region base address
 * @region_size: region size
 * @bar_size: size of the BAR
 * @offset_in_bar: region offset into the bar
 * @bar_id: bar ID of the region
 * @used: if used 1, otherwise 0
 */
struct pci_mem_region {
	u64 region_base;
	u64 region_size;
	u64 bar_size;
	u64 offset_in_bar;
	u8 bar_id;
	u8 used;
};

/**
 * struct static_fw_load_mgr - static FW load manager
 * @preboot_version_max_off: max offset to preboot version
 * @boot_fit_version_max_off: max offset to boot fit version
 * @kmd_msg_to_cpu_reg: register address for KDM->CPU messages
 * @cpu_cmd_status_to_host_reg: register address for CPU command status response
 * @cpu_boot_status_reg: boot status register
 * @cpu_boot_dev_status0_reg: boot device status register 0
 * @cpu_boot_dev_status1_reg: boot device status register 1
 * @boot_err0_reg: boot error register 0
 * @boot_err1_reg: boot error register 1
 * @preboot_version_offset_reg: SRAM offset to preboot version register
 * @boot_fit_version_offset_reg: SRAM offset to boot fit version register
 * @sram_offset_mask: mask for getting offset into the SRAM
 * @cpu_reset_wait_msec: used when setting WFE via kmd_msg_to_cpu_reg
 */
struct static_fw_load_mgr {
	u64 preboot_version_max_off;
	u64 boot_fit_version_max_off;
	u32 kmd_msg_to_cpu_reg;
	u32 cpu_cmd_status_to_host_reg;
	u32 cpu_boot_status_reg;
	u32 cpu_boot_dev_status0_reg;
	u32 cpu_boot_dev_status1_reg;
	u32 boot_err0_reg;
	u32 boot_err1_reg;
	u32 preboot_version_offset_reg;
	u32 boot_fit_version_offset_reg;
	u32 sram_offset_mask;
	u32 cpu_reset_wait_msec;
};

/**
 * struct fw_response - FW response to LKD command
 * @ram_offset: descriptor offset into the RAM
 * @ram_type: RAM type containing the descriptor (SRAM/DRAM)
 * @status: command status
 */
struct fw_response {
	u32 ram_offset;
	u8 ram_type;
	u8 status;
};

/**
 * struct dynamic_fw_load_mgr - dynamic FW load manager
 * @response: FW to LKD response
 * @comm_desc: the communication descriptor with FW
 * @image_region: region to copy the FW image to
 * @fw_image_size: size of FW image to load
 * @wait_for_bl_timeout: timeout for waiting for boot loader to respond
 * @fw_desc_valid: true if FW descriptor has been validated and hence the data can be used
 */
struct dynamic_fw_load_mgr {
	struct fw_response response;
	struct lkd_fw_comms_desc comm_desc;
	struct pci_mem_region *image_region;
	size_t fw_image_size;
	u32 wait_for_bl_timeout;
	bool fw_desc_valid;
};

/**
 * struct pre_fw_load_props - needed properties for pre-FW load
 * @cpu_boot_status_reg: cpu_boot_status register address
 * @sts_boot_dev_sts0_reg: sts_boot_dev_sts0 register address
 * @sts_boot_dev_sts1_reg: sts_boot_dev_sts1 register address
 * @boot_err0_reg: boot_err0 register address
 * @boot_err1_reg: boot_err1 register address
 * @wait_for_preboot_timeout: timeout to poll for preboot ready
 */
struct pre_fw_load_props {
	u32 cpu_boot_status_reg;
	u32 sts_boot_dev_sts0_reg;
	u32 sts_boot_dev_sts1_reg;
	u32 boot_err0_reg;
	u32 boot_err1_reg;
	u32 wait_for_preboot_timeout;
};

/**
 * struct fw_image_props - properties of FW image
 * @image_name: name of the image
 * @src_off: offset in src FW to copy from
 * @copy_size: amount of bytes to copy (0 to copy the whole binary)
 */
struct fw_image_props {
	char *image_name;
	u32 src_off;
	u32 copy_size;
};

/**
 * struct fw_load_mgr - manager FW loading process
 * @dynamic_loader: specific structure for dynamic load
 * @static_loader: specific structure for static load
 * @pre_fw_load_props: parameter for pre FW load
 * @boot_fit_img: boot fit image properties
 * @linux_img: linux image properties
 * @cpu_timeout: CPU response timeout in usec
 * @boot_fit_timeout: Boot fit load timeout in usec
 * @skip_bmc: should BMC be skipped
 * @sram_bar_id: SRAM bar ID
 * @dram_bar_id: DRAM bar ID
 * @fw_comp_loaded: bitmask of loaded FW components. set bit meaning loaded
 *                  component. values are set according to enum hl_fw_types.
 */
struct fw_load_mgr {
	union {
		struct dynamic_fw_load_mgr dynamic_loader;
		struct static_fw_load_mgr static_loader;
	};
	struct pre_fw_load_props pre_fw_load;
	struct fw_image_props boot_fit_img;
	struct fw_image_props linux_img;
	u32 cpu_timeout;
	u32 boot_fit_timeout;
	u8 skip_bmc;
	u8 sram_bar_id;
	u8 dram_bar_id;
	u8 fw_comp_loaded;
};

struct hl_cs;

/**
 * struct engines_data - asic engines data
 * @buf: buffer for engines data in ascii
 * @actual_size: actual size of data that was written by the driver to the allocated buffer
 * @allocated_buf_size: total size of allocated buffer
 */
struct engines_data {
	char *buf;
	int actual_size;
	u32 allocated_buf_size;
};

/**
 * struct hl_asic_funcs - ASIC specific functions that are can be called from
 *                        common code.
 * @early_init: sets up early driver state (pre sw_init), doesn't configure H/W.
 * @early_fini: tears down what was done in early_init.
 * @late_init: sets up late driver/hw state (post hw_init) - Optional.
 * @late_fini: tears down what was done in late_init (pre hw_fini) - Optional.
 * @sw_init: sets up driver state, does not configure H/W.
 * @sw_fini: tears down driver state, does not configure H/W.
 * @hw_init: sets up the H/W state.
 * @hw_fini: tears down the H/W state.
 * @halt_engines: halt engines, needed for reset sequence. This also disables
 *                interrupts from the device. Should be called before
 *                hw_fini and before CS rollback.
 * @suspend: handles IP specific H/W or SW changes for suspend.
 * @resume: handles IP specific H/W or SW changes for resume.
 * @mmap: maps a memory.
 * @ring_doorbell: increment PI on a given QMAN.
 * @pqe_write: Write the PQ entry to the PQ. This is ASIC-specific
 *             function because the PQs are located in different memory areas
 *             per ASIC (SRAM, DRAM, Host memory) and therefore, the method of
 *             writing the PQE must match the destination memory area
 *             properties.
 * @asic_dma_alloc_coherent: Allocate coherent DMA memory by calling
 *                           dma_alloc_coherent(). This is ASIC function because
 *                           its implementation is not trivial when the driver
 *                           is loaded in simulation mode (not upstreamed).
 * @asic_dma_free_coherent:  Free coherent DMA memory by calling
 *                           dma_free_coherent(). This is ASIC function because
 *                           its implementation is not trivial when the driver
 *                           is loaded in simulation mode (not upstreamed).
 * @scrub_device_mem: Scrub the entire SRAM and DRAM.
 * @scrub_device_dram: Scrub the dram memory of the device.
 * @get_int_queue_base: get the internal queue base address.
 * @test_queues: run simple test on all queues for sanity check.
 * @asic_dma_pool_zalloc: small DMA allocation of coherent memory from DMA pool.
 *                        size of allocation is HL_DMA_POOL_BLK_SIZE.
 * @asic_dma_pool_free: free small DMA allocation from pool.
 * @cpu_accessible_dma_pool_alloc: allocate CPU PQ packet from DMA pool.
 * @cpu_accessible_dma_pool_free: free CPU PQ packet from DMA pool.
 * @asic_dma_unmap_single: unmap a single DMA buffer
 * @asic_dma_map_single: map a single buffer to a DMA
 * @hl_dma_unmap_sgtable: DMA unmap scatter-gather table.
 * @cs_parser: parse Command Submission.
 * @asic_dma_map_sgtable: DMA map scatter-gather table.
 * @add_end_of_cb_packets: Add packets to the end of CB, if device requires it.
 * @update_eq_ci: update event queue CI.
 * @context_switch: called upon ASID context switch.
 * @restore_phase_topology: clear all SOBs amd MONs.
 * @debugfs_read_dma: debug interface for reading up to 2MB from the device's
 *                    internal memory via DMA engine.
 * @add_device_attr: add ASIC specific device attributes.
 * @handle_eqe: handle event queue entry (IRQ) from CPU-CP.
 * @get_events_stat: retrieve event queue entries histogram.
 * @read_pte: read MMU page table entry from DRAM.
 * @write_pte: write MMU page table entry to DRAM.
 * @mmu_invalidate_cache: flush MMU STLB host/DRAM cache, either with soft
 *                        (L1 only) or hard (L0 & L1) flush.
 * @mmu_invalidate_cache_range: flush specific MMU STLB cache lines with ASID-VA-size mask.
 * @mmu_prefetch_cache_range: pre-fetch specific MMU STLB cache lines with ASID-VA-size mask.
 * @send_heartbeat: send is-alive packet to CPU-CP and verify response.
 * @debug_coresight: perform certain actions on Coresight for debugging.
 * @is_device_idle: return true if device is idle, false otherwise.
 * @compute_reset_late_init: perform certain actions needed after a compute reset
 * @hw_queues_lock: acquire H/W queues lock.
 * @hw_queues_unlock: release H/W queues lock.
 * @get_pci_id: retrieve PCI ID.
 * @get_eeprom_data: retrieve EEPROM data from F/W.
 * @get_monitor_dump: retrieve monitor registers dump from F/W.
 * @send_cpu_message: send message to F/W. If the message is timedout, the
 *                    driver will eventually reset the device. The timeout can
 *                    be determined by the calling function or it can be 0 and
 *                    then the timeout is the default timeout for the specific
 *                    ASIC
 * @get_hw_state: retrieve the H/W state
 * @pci_bars_map: Map PCI BARs.
 * @init_iatu: Initialize the iATU unit inside the PCI controller.
 * @rreg: Read a register. Needed for simulator support.
 * @wreg: Write a register. Needed for simulator support.
 * @halt_coresight: stop the ETF and ETR traces.
 * @ctx_init: context dependent initialization.
 * @ctx_fini: context dependent cleanup.
 * @pre_schedule_cs: Perform pre-CS-scheduling operations.
 * @get_queue_id_for_cq: Get the H/W queue id related to the given CQ index.
 * @load_firmware_to_device: load the firmware to the device's memory
 * @load_boot_fit_to_device: load boot fit to device's memory
 * @get_signal_cb_size: Get signal CB size.
 * @get_wait_cb_size: Get wait CB size.
 * @gen_signal_cb: Generate a signal CB.
 * @gen_wait_cb: Generate a wait CB.
 * @reset_sob: Reset a SOB.
 * @reset_sob_group: Reset SOB group
 * @get_device_time: Get the device time.
 * @pb_print_security_errors: print security errors according block and cause
 * @collective_wait_init_cs: Generate collective master/slave packets
 *                           and place them in the relevant cs jobs
 * @collective_wait_create_jobs: allocate collective wait cs jobs
 * @get_dec_base_addr: get the base address of a given decoder.
 * @scramble_addr: Routine to scramble the address prior of mapping it
 *                 in the MMU.
 * @descramble_addr: Routine to de-scramble the address prior of
 *                   showing it to users.
 * @ack_protection_bits_errors: ack and dump all security violations
 * @get_hw_block_id: retrieve a HW block id to be used by the user to mmap it.
 *                   also returns the size of the block if caller supplies
 *                   a valid pointer for it
 * @hw_block_mmap: mmap a HW block with a given id.
 * @enable_events_from_fw: send interrupt to firmware to notify them the
 *                         driver is ready to receive asynchronous events. This
 *                         function should be called during the first init and
 *                         after every hard-reset of the device
 * @ack_mmu_errors: check and ack mmu errors, page fault, access violation.
 * @get_msi_info: Retrieve asic-specific MSI ID of the f/w async event
 * @map_pll_idx_to_fw_idx: convert driver specific per asic PLL index to
 *                         generic f/w compatible PLL Indexes
 * @init_firmware_preload_params: initialize pre FW-load parameters.
 * @init_firmware_loader: initialize data for FW loader.
 * @init_cpu_scrambler_dram: Enable CPU specific DRAM scrambling
 * @state_dump_init: initialize constants required for state dump
 * @get_sob_addr: get SOB base address offset.
 * @set_pci_memory_regions: setting properties of PCI memory regions
 * @get_stream_master_qid_arr: get pointer to stream masters QID array
 * @check_if_razwi_happened: check if there was a razwi due to RR violation.
 * @access_dev_mem: access device memory
 * @set_dram_bar_base: set the base of the DRAM BAR
 * @set_engine_cores: set a config command to engine cores
 * @set_engines: set a config command to user engines
 * @send_device_activity: indication to FW about device availability
 * @set_dram_properties: set DRAM related properties.
 * @set_binning_masks: set binning/enable masks for all relevant components.
 */
struct hl_asic_funcs {
	int (*early_init)(struct hl_device *hdev);
	int (*early_fini)(struct hl_device *hdev);
	int (*late_init)(struct hl_device *hdev);
	void (*late_fini)(struct hl_device *hdev);
	int (*sw_init)(struct hl_device *hdev);
	int (*sw_fini)(struct hl_device *hdev);
	int (*hw_init)(struct hl_device *hdev);
	int (*hw_fini)(struct hl_device *hdev, bool hard_reset, bool fw_reset);
	void (*halt_engines)(struct hl_device *hdev, bool hard_reset, bool fw_reset);
	int (*suspend)(struct hl_device *hdev);
	int (*resume)(struct hl_device *hdev);
	int (*mmap)(struct hl_device *hdev, struct vm_area_struct *vma,
			void *cpu_addr, dma_addr_t dma_addr, size_t size);
	void (*ring_doorbell)(struct hl_device *hdev, u32 hw_queue_id, u32 pi);
	void (*pqe_write)(struct hl_device *hdev, __le64 *pqe,
			struct hl_bd *bd);
	void* (*asic_dma_alloc_coherent)(struct hl_device *hdev, size_t size,
					dma_addr_t *dma_handle, gfp_t flag);
	void (*asic_dma_free_coherent)(struct hl_device *hdev, size_t size,
					void *cpu_addr, dma_addr_t dma_handle);
	int (*scrub_device_mem)(struct hl_device *hdev);
	int (*scrub_device_dram)(struct hl_device *hdev, u64 val);
	void* (*get_int_queue_base)(struct hl_device *hdev, u32 queue_id,
				dma_addr_t *dma_handle, u16 *queue_len);
	int (*test_queues)(struct hl_device *hdev);
	void* (*asic_dma_pool_zalloc)(struct hl_device *hdev, size_t size,
				gfp_t mem_flags, dma_addr_t *dma_handle);
	void (*asic_dma_pool_free)(struct hl_device *hdev, void *vaddr,
				dma_addr_t dma_addr);
	void* (*cpu_accessible_dma_pool_alloc)(struct hl_device *hdev,
				size_t size, dma_addr_t *dma_handle);
	void (*cpu_accessible_dma_pool_free)(struct hl_device *hdev,
				size_t size, void *vaddr);
	void (*asic_dma_unmap_single)(struct hl_device *hdev,
				dma_addr_t dma_addr, int len,
				enum dma_data_direction dir);
	dma_addr_t (*asic_dma_map_single)(struct hl_device *hdev,
				void *addr, int len,
				enum dma_data_direction dir);
	void (*hl_dma_unmap_sgtable)(struct hl_device *hdev,
				struct sg_table *sgt,
				enum dma_data_direction dir);
	int (*cs_parser)(struct hl_device *hdev, struct hl_cs_parser *parser);
	int (*asic_dma_map_sgtable)(struct hl_device *hdev, struct sg_table *sgt,
				enum dma_data_direction dir);
	void (*add_end_of_cb_packets)(struct hl_device *hdev,
					void *kernel_address, u32 len,
					u32 original_len,
					u64 cq_addr, u32 cq_val, u32 msix_num,
					bool eb);
	void (*update_eq_ci)(struct hl_device *hdev, u32 val);
	int (*context_switch)(struct hl_device *hdev, u32 asid);
	void (*restore_phase_topology)(struct hl_device *hdev);
	int (*debugfs_read_dma)(struct hl_device *hdev, u64 addr, u32 size,
				void *blob_addr);
	void (*add_device_attr)(struct hl_device *hdev, struct attribute_group *dev_clk_attr_grp,
				struct attribute_group *dev_vrm_attr_grp);
	void (*handle_eqe)(struct hl_device *hdev,
				struct hl_eq_entry *eq_entry);
	void* (*get_events_stat)(struct hl_device *hdev, bool aggregate,
				u32 *size);
	u64 (*read_pte)(struct hl_device *hdev, u64 addr);
	void (*write_pte)(struct hl_device *hdev, u64 addr, u64 val);
	int (*mmu_invalidate_cache)(struct hl_device *hdev, bool is_hard,
					u32 flags);
	int (*mmu_invalidate_cache_range)(struct hl_device *hdev, bool is_hard,
				u32 flags, u32 asid, u64 va, u64 size);
	int (*mmu_prefetch_cache_range)(struct hl_ctx *ctx, u32 flags, u32 asid, u64 va, u64 size);
	int (*send_heartbeat)(struct hl_device *hdev);
	int (*debug_coresight)(struct hl_device *hdev, struct hl_ctx *ctx, void *data);
	bool (*is_device_idle)(struct hl_device *hdev, u64 *mask_arr, u8 mask_len,
				struct engines_data *e);
	int (*compute_reset_late_init)(struct hl_device *hdev);
	void (*hw_queues_lock)(struct hl_device *hdev);
	void (*hw_queues_unlock)(struct hl_device *hdev);
	u32 (*get_pci_id)(struct hl_device *hdev);
	int (*get_eeprom_data)(struct hl_device *hdev, void *data, size_t max_size);
	int (*get_monitor_dump)(struct hl_device *hdev, void *data);
	int (*send_cpu_message)(struct hl_device *hdev, u32 *msg,
				u16 len, u32 timeout, u64 *result);
	int (*pci_bars_map)(struct hl_device *hdev);
	int (*init_iatu)(struct hl_device *hdev);
	u32 (*rreg)(struct hl_device *hdev, u32 reg);
	void (*wreg)(struct hl_device *hdev, u32 reg, u32 val);
	void (*halt_coresight)(struct hl_device *hdev, struct hl_ctx *ctx);
	int (*ctx_init)(struct hl_ctx *ctx);
	void (*ctx_fini)(struct hl_ctx *ctx);
	int (*pre_schedule_cs)(struct hl_cs *cs);
	u32 (*get_queue_id_for_cq)(struct hl_device *hdev, u32 cq_idx);
	int (*load_firmware_to_device)(struct hl_device *hdev);
	int (*load_boot_fit_to_device)(struct hl_device *hdev);
	u32 (*get_signal_cb_size)(struct hl_device *hdev);
	u32 (*get_wait_cb_size)(struct hl_device *hdev);
	u32 (*gen_signal_cb)(struct hl_device *hdev, void *data, u16 sob_id,
			u32 size, bool eb);
	u32 (*gen_wait_cb)(struct hl_device *hdev,
			struct hl_gen_wait_properties *prop);
	void (*reset_sob)(struct hl_device *hdev, void *data);
	void (*reset_sob_group)(struct hl_device *hdev, u16 sob_group);
	u64 (*get_device_time)(struct hl_device *hdev);
	void (*pb_print_security_errors)(struct hl_device *hdev,
			u32 block_addr, u32 cause, u32 offended_addr);
	int (*collective_wait_init_cs)(struct hl_cs *cs);
	int (*collective_wait_create_jobs)(struct hl_device *hdev,
			struct hl_ctx *ctx, struct hl_cs *cs,
			u32 wait_queue_id, u32 collective_engine_id,
			u32 encaps_signal_offset);
	u32 (*get_dec_base_addr)(struct hl_device *hdev, u32 core_id);
	u64 (*scramble_addr)(struct hl_device *hdev, u64 addr);
	u64 (*descramble_addr)(struct hl_device *hdev, u64 addr);
	void (*ack_protection_bits_errors)(struct hl_device *hdev);
	int (*get_hw_block_id)(struct hl_device *hdev, u64 block_addr,
				u32 *block_size, u32 *block_id);
	int (*hw_block_mmap)(struct hl_device *hdev, struct vm_area_struct *vma,
			u32 block_id, u32 block_size);
	void (*enable_events_from_fw)(struct hl_device *hdev);
	int (*ack_mmu_errors)(struct hl_device *hdev, u64 mmu_cap_mask);
	void (*get_msi_info)(__le32 *table);
	int (*map_pll_idx_to_fw_idx)(u32 pll_idx);
	void (*init_firmware_preload_params)(struct hl_device *hdev);
	void (*init_firmware_loader)(struct hl_device *hdev);
	void (*init_cpu_scrambler_dram)(struct hl_device *hdev);
	void (*state_dump_init)(struct hl_device *hdev);
	u32 (*get_sob_addr)(struct hl_device *hdev, u32 sob_id);
	void (*set_pci_memory_regions)(struct hl_device *hdev);
	u32* (*get_stream_master_qid_arr)(void);
	void (*check_if_razwi_happened)(struct hl_device *hdev);
	int (*mmu_get_real_page_size)(struct hl_device *hdev, struct hl_mmu_properties *mmu_prop,
					u32 page_size, u32 *real_page_size, bool is_dram_addr);
	int (*access_dev_mem)(struct hl_device *hdev, enum pci_region region_type,
				u64 addr, u64 *val, enum debugfs_access_type acc_type);
	u64 (*set_dram_bar_base)(struct hl_device *hdev, u64 addr);
	int (*set_engine_cores)(struct hl_device *hdev, u32 *core_ids,
					u32 num_cores, u32 core_command);
	int (*set_engines)(struct hl_device *hdev, u32 *engine_ids,
					u32 num_engines, u32 engine_command);
	int (*send_device_activity)(struct hl_device *hdev, bool open);
	int (*set_dram_properties)(struct hl_device *hdev);
	int (*set_binning_masks)(struct hl_device *hdev);
};


/*
 * CONTEXTS
 */

#define HL_KERNEL_ASID_ID	0

/**
 * enum hl_va_range_type - virtual address range type.
 * @HL_VA_RANGE_TYPE_HOST: range type of host pages
 * @HL_VA_RANGE_TYPE_HOST_HUGE: range type of host huge pages
 * @HL_VA_RANGE_TYPE_DRAM: range type of dram pages
 */
enum hl_va_range_type {
	HL_VA_RANGE_TYPE_HOST,
	HL_VA_RANGE_TYPE_HOST_HUGE,
	HL_VA_RANGE_TYPE_DRAM,
	HL_VA_RANGE_TYPE_MAX
};

/**
 * struct hl_va_range - virtual addresses range.
 * @lock: protects the virtual addresses list.
 * @list: list of virtual addresses blocks available for mappings.
 * @start_addr: range start address.
 * @end_addr: range end address.
 * @page_size: page size of this va range.
 */
struct hl_va_range {
	struct mutex		lock;
	struct list_head	list;
	u64			start_addr;
	u64			end_addr;
	u32			page_size;
};

/**
 * struct hl_cs_counters_atomic - command submission counters
 * @out_of_mem_drop_cnt: dropped due to memory allocation issue
 * @parsing_drop_cnt: dropped due to error in packet parsing
 * @queue_full_drop_cnt: dropped due to queue full
 * @device_in_reset_drop_cnt: dropped due to device in reset
 * @max_cs_in_flight_drop_cnt: dropped due to maximum CS in-flight
 * @validation_drop_cnt: dropped due to error in validation
 */
struct hl_cs_counters_atomic {
	atomic64_t out_of_mem_drop_cnt;
	atomic64_t parsing_drop_cnt;
	atomic64_t queue_full_drop_cnt;
	atomic64_t device_in_reset_drop_cnt;
	atomic64_t max_cs_in_flight_drop_cnt;
	atomic64_t validation_drop_cnt;
};

/**
 * struct hl_dmabuf_priv - a dma-buf private object.
 * @dmabuf: pointer to dma-buf object.
 * @ctx: pointer to the dma-buf owner's context.
 * @phys_pg_pack: pointer to physical page pack if the dma-buf was exported
 *                where virtual memory is supported.
 * @memhash_hnode: pointer to the memhash node. this object holds the export count.
 * @device_address: physical address of the device's memory. Relevant only
 *                  if phys_pg_pack is NULL (dma-buf was exported from address).
 *                  The total size can be taken from the dmabuf object.
 */
struct hl_dmabuf_priv {
	struct dma_buf			*dmabuf;
	struct hl_ctx			*ctx;
	struct hl_vm_phys_pg_pack	*phys_pg_pack;
	struct hl_vm_hash_node		*memhash_hnode;
	uint64_t			device_address;
};

#define HL_CS_OUTCOME_HISTORY_LEN 256

/**
 * struct hl_cs_outcome - represents a single completed CS outcome
 * @list_link: link to either container's used list or free list
 * @map_link: list to the container hash map
 * @ts: completion ts
 * @seq: the original cs sequence
 * @error: error code cs completed with, if any
 */
struct hl_cs_outcome {
	struct list_head list_link;
	struct hlist_node map_link;
	ktime_t ts;
	u64 seq;
	int error;
};

/**
 * struct hl_cs_outcome_store - represents a limited store of completed CS outcomes
 * @outcome_map: index of completed CS searchable by sequence number
 * @used_list: list of outcome objects currently in use
 * @free_list: list of outcome objects currently not in use
 * @nodes_pool: a static pool of pre-allocated outcome objects
 * @db_lock: any operation on the store must take this lock
 */
struct hl_cs_outcome_store {
	DECLARE_HASHTABLE(outcome_map, 8);
	struct list_head used_list;
	struct list_head free_list;
	struct hl_cs_outcome nodes_pool[HL_CS_OUTCOME_HISTORY_LEN];
	spinlock_t db_lock;
};

/**
 * struct hl_ctx - user/kernel context.
 * @mem_hash: holds mapping from virtual address to virtual memory area
 *		descriptor (hl_vm_phys_pg_list or hl_userptr).
 * @mmu_shadow_hash: holds a mapping from shadow address to pgt_info structure.
 * @hr_mmu_phys_hash: if host-resident MMU is used, holds a mapping from
 *                    MMU-hop-page physical address to its host-resident
 *                    pgt_info structure.
 * @hpriv: pointer to the private (Kernel Driver) data of the process (fd).
 * @hdev: pointer to the device structure.
 * @refcount: reference counter for the context. Context is released only when
 *		this hits 0. It is incremented on CS and CS_WAIT.
 * @cs_pending: array of hl fence objects representing pending CS.
 * @outcome_store: storage data structure used to remember outcomes of completed
 *                 command submissions for a long time after CS id wraparound.
 * @va_range: holds available virtual addresses for host and dram mappings.
 * @mem_hash_lock: protects the mem_hash.
 * @hw_block_list_lock: protects the HW block memory list.
 * @debugfs_list: node in debugfs list of contexts.
 * @hw_block_mem_list: list of HW block virtual mapped addresses.
 * @cs_counters: context command submission counters.
 * @cb_va_pool: device VA pool for command buffers which are mapped to the
 *              device's MMU.
 * @sig_mgr: encaps signals handle manager.
 * @cb_va_pool_base: the base address for the device VA pool
 * @cs_sequence: sequence number for CS. Value is assigned to a CS and passed
 *			to user so user could inquire about CS. It is used as
 *			index to cs_pending array.
 * @dram_default_hops: array that holds all hops addresses needed for default
 *                     DRAM mapping.
 * @cs_lock: spinlock to protect cs_sequence.
 * @dram_phys_mem: amount of used physical DRAM memory by this context.
 * @thread_ctx_switch_token: token to prevent multiple threads of the same
 *				context	from running the context switch phase.
 *				Only a single thread should run it.
 * @thread_ctx_switch_wait_token: token to prevent the threads that didn't run
 *				the context switch phase from moving to their
 *				execution phase before the context switch phase
 *				has finished.
 * @asid: context's unique address space ID in the device's MMU.
 * @handle: context's opaque handle for user
 */
struct hl_ctx {
	DECLARE_HASHTABLE(mem_hash, MEM_HASH_TABLE_BITS);
	DECLARE_HASHTABLE(mmu_shadow_hash, MMU_HASH_TABLE_BITS);
	DECLARE_HASHTABLE(hr_mmu_phys_hash, MMU_HASH_TABLE_BITS);
	struct hl_fpriv			*hpriv;
	struct hl_device		*hdev;
	struct kref			refcount;
	struct hl_fence			**cs_pending;
	struct hl_cs_outcome_store	outcome_store;
	struct hl_va_range		*va_range[HL_VA_RANGE_TYPE_MAX];
	struct mutex			mem_hash_lock;
	struct mutex			hw_block_list_lock;
	struct list_head		debugfs_list;
	struct list_head		hw_block_mem_list;
	struct hl_cs_counters_atomic	cs_counters;
	struct gen_pool			*cb_va_pool;
	struct hl_encaps_signals_mgr	sig_mgr;
	u64				cb_va_pool_base;
	u64				cs_sequence;
	u64				*dram_default_hops;
	spinlock_t			cs_lock;
	atomic64_t			dram_phys_mem;
	atomic_t			thread_ctx_switch_token;
	u32				thread_ctx_switch_wait_token;
	u32				asid;
	u32				handle;
};

/**
 * struct hl_ctx_mgr - for handling multiple contexts.
 * @lock: protects ctx_handles.
 * @handles: idr to hold all ctx handles.
 */
struct hl_ctx_mgr {
	struct mutex	lock;
	struct idr	handles;
};


/*
 * COMMAND SUBMISSIONS
 */

/**
 * struct hl_userptr - memory mapping chunk information
 * @vm_type: type of the VM.
 * @job_node: linked-list node for hanging the object on the Job's list.
 * @pages: pointer to struct page array
 * @npages: size of @pages array
 * @sgt: pointer to the scatter-gather table that holds the pages.
 * @dir: for DMA unmapping, the direction must be supplied, so save it.
 * @debugfs_list: node in debugfs list of command submissions.
 * @pid: the pid of the user process owning the memory
 * @addr: user-space virtual address of the start of the memory area.
 * @size: size of the memory area to pin & map.
 * @dma_mapped: true if the SG was mapped to DMA addresses, false otherwise.
 */
struct hl_userptr {
	enum vm_type		vm_type; /* must be first */
	struct list_head	job_node;
	struct page		**pages;
	unsigned int		npages;
	struct sg_table		*sgt;
	enum dma_data_direction dir;
	struct list_head	debugfs_list;
	pid_t			pid;
	u64			addr;
	u64			size;
	u8			dma_mapped;
};

/**
 * struct hl_cs - command submission.
 * @jobs_in_queue_cnt: per each queue, maintain counter of submitted jobs.
 * @ctx: the context this CS belongs to.
 * @job_list: list of the CS's jobs in the various queues.
 * @job_lock: spinlock for the CS's jobs list. Needed for free_job.
 * @refcount: reference counter for usage of the CS.
 * @fence: pointer to the fence object of this CS.
 * @signal_fence: pointer to the fence object of the signal CS (used by wait
 *                CS only).
 * @finish_work: workqueue object to run when CS is completed by H/W.
 * @work_tdr: delayed work node for TDR.
 * @mirror_node : node in device mirror list of command submissions.
 * @staged_cs_node: node in the staged cs list.
 * @debugfs_list: node in debugfs list of command submissions.
 * @encaps_sig_hdl: holds the encaps signals handle.
 * @sequence: the sequence number of this CS.
 * @staged_sequence: the sequence of the staged submission this CS is part of,
 *                   relevant only if staged_cs is set.
 * @timeout_jiffies: cs timeout in jiffies.
 * @submission_time_jiffies: submission time of the cs
 * @type: CS_TYPE_*.
 * @jobs_cnt: counter of submitted jobs on all queues.
 * @encaps_sig_hdl_id: encaps signals handle id, set for the first staged cs.
 * @completion_timestamp: timestamp of the last completed cs job.
 * @sob_addr_offset: sob offset from the configuration base address.
 * @initial_sob_count: count of completed signals in SOB before current submission of signal or
 *                     cs with encaps signals.
 * @submitted: true if CS was submitted to H/W.
 * @completed: true if CS was completed by device.
 * @timedout : true if CS was timedout.
 * @tdr_active: true if TDR was activated for this CS (to prevent
 *		double TDR activation).
 * @aborted: true if CS was aborted due to some device error.
 * @timestamp: true if a timestamp must be captured upon completion.
 * @staged_last: true if this is the last staged CS and needs completion.
 * @staged_first: true if this is the first staged CS and we need to receive
 *                timeout for this CS.
 * @staged_cs: true if this CS is part of a staged submission.
 * @skip_reset_on_timeout: true if we shall not reset the device in case
 *                         timeout occurs (debug scenario).
 * @encaps_signals: true if this CS has encaps reserved signals.
 */
struct hl_cs {
	u16			*jobs_in_queue_cnt;
	struct hl_ctx		*ctx;
	struct list_head	job_list;
	spinlock_t		job_lock;
	struct kref		refcount;
	struct hl_fence		*fence;
	struct hl_fence		*signal_fence;
	struct work_struct	finish_work;
	struct delayed_work	work_tdr;
	struct list_head	mirror_node;
	struct list_head	staged_cs_node;
	struct list_head	debugfs_list;
	struct hl_cs_encaps_sig_handle *encaps_sig_hdl;
	ktime_t			completion_timestamp;
	u64			sequence;
	u64			staged_sequence;
	u64			timeout_jiffies;
	u64			submission_time_jiffies;
	enum hl_cs_type		type;
	u32			jobs_cnt;
	u32			encaps_sig_hdl_id;
	u32			sob_addr_offset;
	u16			initial_sob_count;
	u8			submitted;
	u8			completed;
	u8			timedout;
	u8			tdr_active;
	u8			aborted;
	u8			timestamp;
	u8			staged_last;
	u8			staged_first;
	u8			staged_cs;
	u8			skip_reset_on_timeout;
	u8			encaps_signals;
};

/**
 * struct hl_cs_job - command submission job.
 * @cs_node: the node to hang on the CS jobs list.
 * @cs: the CS this job belongs to.
 * @user_cb: the CB we got from the user.
 * @patched_cb: in case of patching, this is internal CB which is submitted on
 *		the queue instead of the CB we got from the IOCTL.
 * @finish_work: workqueue object to run when job is completed.
 * @userptr_list: linked-list of userptr mappings that belong to this job and
 *			wait for completion.
 * @debugfs_list: node in debugfs list of command submission jobs.
 * @refcount: reference counter for usage of the CS job.
 * @queue_type: the type of the H/W queue this job is submitted to.
 * @timestamp: timestamp upon job completion
 * @id: the id of this job inside a CS.
 * @hw_queue_id: the id of the H/W queue this job is submitted to.
 * @user_cb_size: the actual size of the CB we got from the user.
 * @job_cb_size: the actual size of the CB that we put on the queue.
 * @encaps_sig_wait_offset: encapsulated signals offset, which allow user
 *                          to wait on part of the reserved signals.
 * @is_kernel_allocated_cb: true if the CB handle we got from the user holds a
 *                          handle to a kernel-allocated CB object, false
 *                          otherwise (SRAM/DRAM/host address).
 * @contains_dma_pkt: whether the JOB contains at least one DMA packet. This
 *                    info is needed later, when adding the 2xMSG_PROT at the
 *                    end of the JOB, to know which barriers to put in the
 *                    MSG_PROT packets. Relevant only for GAUDI as GOYA doesn't
 *                    have streams so the engine can't be busy by another
 *                    stream.
 */
struct hl_cs_job {
	struct list_head	cs_node;
	struct hl_cs		*cs;
	struct hl_cb		*user_cb;
	struct hl_cb		*patched_cb;
	struct work_struct	finish_work;
	struct list_head	userptr_list;
	struct list_head	debugfs_list;
	struct kref		refcount;
	enum hl_queue_type	queue_type;
	ktime_t			timestamp;
	u32			id;
	u32			hw_queue_id;
	u32			user_cb_size;
	u32			job_cb_size;
	u32			encaps_sig_wait_offset;
	u8			is_kernel_allocated_cb;
	u8			contains_dma_pkt;
};

/**
 * struct hl_cs_parser - command submission parser properties.
 * @user_cb: the CB we got from the user.
 * @patched_cb: in case of patching, this is internal CB which is submitted on
 *		the queue instead of the CB we got from the IOCTL.
 * @job_userptr_list: linked-list of userptr mappings that belong to the related
 *			job and wait for completion.
 * @cs_sequence: the sequence number of the related CS.
 * @queue_type: the type of the H/W queue this job is submitted to.
 * @ctx_id: the ID of the context the related CS belongs to.
 * @hw_queue_id: the id of the H/W queue this job is submitted to.
 * @user_cb_size: the actual size of the CB we got from the user.
 * @patched_cb_size: the size of the CB after parsing.
 * @job_id: the id of the related job inside the related CS.
 * @is_kernel_allocated_cb: true if the CB handle we got from the user holds a
 *                          handle to a kernel-allocated CB object, false
 *                          otherwise (SRAM/DRAM/host address).
 * @contains_dma_pkt: whether the JOB contains at least one DMA packet. This
 *                    info is needed later, when adding the 2xMSG_PROT at the
 *                    end of the JOB, to know which barriers to put in the
 *                    MSG_PROT packets. Relevant only for GAUDI as GOYA doesn't
 *                    have streams so the engine can't be busy by another
 *                    stream.
 * @completion: true if we need completion for this CS.
 */
struct hl_cs_parser {
	struct hl_cb		*user_cb;
	struct hl_cb		*patched_cb;
	struct list_head	*job_userptr_list;
	u64			cs_sequence;
	enum hl_queue_type	queue_type;
	u32			ctx_id;
	u32			hw_queue_id;
	u32			user_cb_size;
	u32			patched_cb_size;
	u8			job_id;
	u8			is_kernel_allocated_cb;
	u8			contains_dma_pkt;
	u8			completion;
};

/*
 * MEMORY STRUCTURE
 */

/**
 * struct hl_vm_hash_node - hash element from virtual address to virtual
 *				memory area descriptor (hl_vm_phys_pg_list or
 *				hl_userptr).
 * @node: node to hang on the hash table in context object.
 * @vaddr: key virtual address.
 * @handle: memory handle for device memory allocation.
 * @ptr: value pointer (hl_vm_phys_pg_list or hl_userptr).
 * @export_cnt: number of exports from within the VA block.
 */
struct hl_vm_hash_node {
	struct hlist_node	node;
	u64			vaddr;
	u64			handle;
	void			*ptr;
	int			export_cnt;
};

/**
 * struct hl_vm_hw_block_list_node - list element from user virtual address to
 *				HW block id.
 * @node: node to hang on the list in context object.
 * @ctx: the context this node belongs to.
 * @vaddr: virtual address of the HW block.
 * @block_size: size of the block.
 * @mapped_size: size of the block which is mapped. May change if partial un-mappings are done.
 * @id: HW block id (handle).
 */
struct hl_vm_hw_block_list_node {
	struct list_head	node;
	struct hl_ctx		*ctx;
	unsigned long		vaddr;
	u32			block_size;
	u32			mapped_size;
	u32			id;
};

/**
 * struct hl_vm_phys_pg_pack - physical page pack.
 * @vm_type: describes the type of the virtual area descriptor.
 * @pages: the physical page array.
 * @npages: num physical pages in the pack.
 * @total_size: total size of all the pages in this list.
 * @exported_size: buffer exported size.
 * @node: used to attach to deletion list that is used when all the allocations are cleared
 *        at the teardown of the context.
 * @mapping_cnt: number of shared mappings.
 * @asid: the context related to this list.
 * @page_size: size of each page in the pack.
 * @flags: HL_MEM_* flags related to this list.
 * @handle: the provided handle related to this list.
 * @offset: offset from the first page.
 * @contiguous: is contiguous physical memory.
 * @created_from_userptr: is product of host virtual address.
 */
struct hl_vm_phys_pg_pack {
	enum vm_type		vm_type; /* must be first */
	u64			*pages;
	u64			npages;
	u64			total_size;
	u64			exported_size;
	struct list_head	node;
	atomic_t		mapping_cnt;
	u32			asid;
	u32			page_size;
	u32			flags;
	u32			handle;
	u32			offset;
	u8			contiguous;
	u8			created_from_userptr;
};

/**
 * struct hl_vm_va_block - virtual range block information.
 * @node: node to hang on the virtual range list in context object.
 * @start: virtual range start address.
 * @end: virtual range end address.
 * @size: virtual range size.
 */
struct hl_vm_va_block {
	struct list_head	node;
	u64			start;
	u64			end;
	u64			size;
};

/**
 * struct hl_vm - virtual memory manager for MMU.
 * @dram_pg_pool: pool for DRAM physical pages of 2MB.
 * @dram_pg_pool_refcount: reference counter for the pool usage.
 * @idr_lock: protects the phys_pg_list_handles.
 * @phys_pg_pack_handles: idr to hold all device allocations handles.
 * @init_done: whether initialization was done. We need this because VM
 *		initialization might be skipped during device initialization.
 */
struct hl_vm {
	struct gen_pool		*dram_pg_pool;
	struct kref		dram_pg_pool_refcount;
	spinlock_t		idr_lock;
	struct idr		phys_pg_pack_handles;
	u8			init_done;
};


/*
 * DEBUG, PROFILING STRUCTURE
 */

/**
 * struct hl_debug_params - Coresight debug parameters.
 * @input: pointer to component specific input parameters.
 * @output: pointer to component specific output parameters.
 * @output_size: size of output buffer.
 * @reg_idx: relevant register ID.
 * @op: component operation to execute.
 * @enable: true if to enable component debugging, false otherwise.
 */
struct hl_debug_params {
	void *input;
	void *output;
	u32 output_size;
	u32 reg_idx;
	u32 op;
	bool enable;
};

/**
 * struct hl_notifier_event - holds the notifier data structure
 * @eventfd: the event file descriptor to raise the notifications
 * @lock: mutex lock to protect the notifier data flows
 * @events_mask: indicates the bitmap events
 */
struct hl_notifier_event {
	struct eventfd_ctx	*eventfd;
	struct mutex		lock;
	u64			events_mask;
};

/*
 * FILE PRIVATE STRUCTURE
 */

/**
 * struct hl_fpriv - process information stored in FD private data.
 * @hdev: habanalabs device structure.
 * @filp: pointer to the given file structure.
 * @taskpid: current process ID.
 * @ctx: current executing context. TODO: remove for multiple ctx per process
 * @ctx_mgr: context manager to handle multiple context for this FD.
 * @mem_mgr: manager descriptor for memory exportable via mmap
 * @notifier_event: notifier eventfd towards user process
 * @debugfs_list: list of relevant ASIC debugfs.
 * @dev_node: node in the device list of file private data
 * @refcount: number of related contexts.
 * @restore_phase_mutex: lock for context switch and restore phase.
 * @ctx_lock: protects the pointer to current executing context pointer. TODO: remove for multiple
 *            ctx per process.
 */
struct hl_fpriv {
	struct hl_device		*hdev;
	struct file			*filp;
	struct pid			*taskpid;
	struct hl_ctx			*ctx;
	struct hl_ctx_mgr		ctx_mgr;
	struct hl_mem_mgr		mem_mgr;
	struct hl_notifier_event	notifier_event;
	struct list_head		debugfs_list;
	struct list_head		dev_node;
	struct kref			refcount;
	struct mutex			restore_phase_mutex;
	struct mutex			ctx_lock;
};


/*
 * DebugFS
 */

/**
 * struct hl_info_list - debugfs file ops.
 * @name: file name.
 * @show: function to output information.
 * @write: function to write to the file.
 */
struct hl_info_list {
	const char	*name;
	int		(*show)(struct seq_file *s, void *data);
	ssize_t		(*write)(struct file *file, const char __user *buf,
				size_t count, loff_t *f_pos);
};

/**
 * struct hl_debugfs_entry - debugfs dentry wrapper.
 * @info_ent: dentry related ops.
 * @dev_entry: ASIC specific debugfs manager.
 */
struct hl_debugfs_entry {
	const struct hl_info_list	*info_ent;
	struct hl_dbg_device_entry	*dev_entry;
};

/**
 * struct hl_dbg_device_entry - ASIC specific debugfs manager.
 * @root: root dentry.
 * @hdev: habanalabs device structure.
 * @entry_arr: array of available hl_debugfs_entry.
 * @file_list: list of available debugfs files.
 * @file_mutex: protects file_list.
 * @cb_list: list of available CBs.
 * @cb_spinlock: protects cb_list.
 * @cs_list: list of available CSs.
 * @cs_spinlock: protects cs_list.
 * @cs_job_list: list of available CB jobs.
 * @cs_job_spinlock: protects cs_job_list.
 * @userptr_list: list of available userptrs (virtual memory chunk descriptor).
 * @userptr_spinlock: protects userptr_list.
 * @ctx_mem_hash_list: list of available contexts with MMU mappings.
 * @ctx_mem_hash_mutex: protects list of available contexts with MMU mappings.
 * @data_dma_blob_desc: data DMA descriptor of blob.
 * @mon_dump_blob_desc: monitor dump descriptor of blob.
 * @state_dump: data of the system states in case of a bad cs.
 * @state_dump_sem: protects state_dump.
 * @addr: next address to read/write from/to in read/write32.
 * @mmu_addr: next virtual address to translate to physical address in mmu_show.
 * @mmu_cap_mask: mmu hw capability mask, to be used in mmu_ack_error.
 * @userptr_lookup: the target user ptr to look up for on demand.
 * @mmu_asid: ASID to use while translating in mmu_show.
 * @state_dump_head: index of the latest state dump
 * @i2c_bus: generic u8 debugfs file for bus value to use in i2c_data_read.
 * @i2c_addr: generic u8 debugfs file for address value to use in i2c_data_read.
 * @i2c_reg: generic u8 debugfs file for register value to use in i2c_data_read.
 * @i2c_len: generic u8 debugfs file for length value to use in i2c_data_read.
 */
struct hl_dbg_device_entry {
	struct dentry			*root;
	struct hl_device		*hdev;
	struct hl_debugfs_entry		*entry_arr;
	struct list_head		file_list;
	struct mutex			file_mutex;
	struct list_head		cb_list;
	spinlock_t			cb_spinlock;
	struct list_head		cs_list;
	spinlock_t			cs_spinlock;
	struct list_head		cs_job_list;
	spinlock_t			cs_job_spinlock;
	struct list_head		userptr_list;
	spinlock_t			userptr_spinlock;
	struct list_head		ctx_mem_hash_list;
	struct mutex			ctx_mem_hash_mutex;
	struct debugfs_blob_wrapper	data_dma_blob_desc;
	struct debugfs_blob_wrapper	mon_dump_blob_desc;
	char				*state_dump[HL_STATE_DUMP_HIST_LEN];
	struct rw_semaphore		state_dump_sem;
	u64				addr;
	u64				mmu_addr;
	u64				mmu_cap_mask;
	u64				userptr_lookup;
	u32				mmu_asid;
	u32				state_dump_head;
	u8				i2c_bus;
	u8				i2c_addr;
	u8				i2c_reg;
	u8				i2c_len;
};

/**
 * struct hl_hw_obj_name_entry - single hw object name, member of
 * hl_state_dump_specs
 * @node: link to the containing hash table
 * @name: hw object name
 * @id: object identifier
 */
struct hl_hw_obj_name_entry {
	struct hlist_node	node;
	const char		*name;
	u32			id;
};

enum hl_state_dump_specs_props {
	SP_SYNC_OBJ_BASE_ADDR,
	SP_NEXT_SYNC_OBJ_ADDR,
	SP_SYNC_OBJ_AMOUNT,
	SP_MON_OBJ_WR_ADDR_LOW,
	SP_MON_OBJ_WR_ADDR_HIGH,
	SP_MON_OBJ_WR_DATA,
	SP_MON_OBJ_ARM_DATA,
	SP_MON_OBJ_STATUS,
	SP_MONITORS_AMOUNT,
	SP_TPC0_CMDQ,
	SP_TPC0_CFG_SO,
	SP_NEXT_TPC,
	SP_MME_CMDQ,
	SP_MME_CFG_SO,
	SP_NEXT_MME,
	SP_DMA_CMDQ,
	SP_DMA_CFG_SO,
	SP_DMA_QUEUES_OFFSET,
	SP_NUM_OF_MME_ENGINES,
	SP_SUB_MME_ENG_NUM,
	SP_NUM_OF_DMA_ENGINES,
	SP_NUM_OF_TPC_ENGINES,
	SP_ENGINE_NUM_OF_QUEUES,
	SP_ENGINE_NUM_OF_STREAMS,
	SP_ENGINE_NUM_OF_FENCES,
	SP_FENCE0_CNT_OFFSET,
	SP_FENCE0_RDATA_OFFSET,
	SP_CP_STS_OFFSET,
	SP_NUM_CORES,

	SP_MAX
};

enum hl_sync_engine_type {
	ENGINE_TPC,
	ENGINE_DMA,
	ENGINE_MME,
};

/**
 * struct hl_mon_state_dump - represents a state dump of a single monitor
 * @id: monitor id
 * @wr_addr_low: address monitor will write to, low bits
 * @wr_addr_high: address monitor will write to, high bits
 * @wr_data: data monitor will write
 * @arm_data: register value containing monitor configuration
 * @status: monitor status
 */
struct hl_mon_state_dump {
	u32		id;
	u32		wr_addr_low;
	u32		wr_addr_high;
	u32		wr_data;
	u32		arm_data;
	u32		status;
};

/**
 * struct hl_sync_to_engine_map_entry - sync object id to engine mapping entry
 * @engine_type: type of the engine
 * @engine_id: id of the engine
 * @sync_id: id of the sync object
 */
struct hl_sync_to_engine_map_entry {
	struct hlist_node		node;
	enum hl_sync_engine_type	engine_type;
	u32				engine_id;
	u32				sync_id;
};

/**
 * struct hl_sync_to_engine_map - maps sync object id to associated engine id
 * @tb: hash table containing the mapping, each element is of type
 *      struct hl_sync_to_engine_map_entry
 */
struct hl_sync_to_engine_map {
	DECLARE_HASHTABLE(tb, SYNC_TO_ENGINE_HASH_TABLE_BITS);
};

/**
 * struct hl_state_dump_specs_funcs - virtual functions used by the state dump
 * @gen_sync_to_engine_map: generate a hash map from sync obj id to its engine
 * @print_single_monitor: format monitor data as string
 * @monitor_valid: return true if given monitor dump is valid
 * @print_fences_single_engine: format fences data as string
 */
struct hl_state_dump_specs_funcs {
	int (*gen_sync_to_engine_map)(struct hl_device *hdev,
				struct hl_sync_to_engine_map *map);
	int (*print_single_monitor)(char **buf, size_t *size, size_t *offset,
				    struct hl_device *hdev,
				    struct hl_mon_state_dump *mon);
	int (*monitor_valid)(struct hl_mon_state_dump *mon);
	int (*print_fences_single_engine)(struct hl_device *hdev,
					u64 base_offset,
					u64 status_base_offset,
					enum hl_sync_engine_type engine_type,
					u32 engine_id, char **buf,
					size_t *size, size_t *offset);
};

/**
 * struct hl_state_dump_specs - defines ASIC known hw objects names
 * @so_id_to_str_tb: sync objects names index table
 * @monitor_id_to_str_tb: monitors names index table
 * @funcs: virtual functions used for state dump
 * @sync_namager_names: readable names for sync manager if available (ex: N_E)
 * @props: pointer to a per asic const props array required for state dump
 */
struct hl_state_dump_specs {
	DECLARE_HASHTABLE(so_id_to_str_tb, OBJ_NAMES_HASH_TABLE_BITS);
	DECLARE_HASHTABLE(monitor_id_to_str_tb, OBJ_NAMES_HASH_TABLE_BITS);
	struct hl_state_dump_specs_funcs	funcs;
	const char * const			*sync_namager_names;
	s64					*props;
};


/*
 * DEVICES
 */

#define HL_STR_MAX	32

#define HL_DEV_STS_MAX (HL_DEVICE_STATUS_LAST + 1)

/* Theoretical limit only. A single host can only contain up to 4 or 8 PCIe
 * x16 cards. In extreme cases, there are hosts that can accommodate 16 cards.
 */
#define HL_MAX_MINORS	256

/*
 * Registers read & write functions.
 */

u32 hl_rreg(struct hl_device *hdev, u32 reg);
void hl_wreg(struct hl_device *hdev, u32 reg, u32 val);

#define RREG32(reg) hdev->asic_funcs->rreg(hdev, (reg))
#define WREG32(reg, v) hdev->asic_funcs->wreg(hdev, (reg), (v))
#define DREG32(reg) pr_info("REGISTER: " #reg " : 0x%08X\n",	\
			hdev->asic_funcs->rreg(hdev, (reg)))

#define WREG32_P(reg, val, mask)				\
	do {							\
		u32 tmp_ = RREG32(reg);				\
		tmp_ &= (mask);					\
		tmp_ |= ((val) & ~(mask));			\
		WREG32(reg, tmp_);				\
	} while (0)
#define WREG32_AND(reg, and) WREG32_P(reg, 0, and)
#define WREG32_OR(reg, or) WREG32_P(reg, or, ~(or))

#define RMWREG32_SHIFTED(reg, val, mask) WREG32_P(reg, val, ~(mask))

#define RMWREG32(reg, val, mask) RMWREG32_SHIFTED(reg, (val) << __ffs(mask), mask)

#define RREG32_MASK(reg, mask) ((RREG32(reg) & mask) >> __ffs(mask))

#define REG_FIELD_SHIFT(reg, field) reg##_##field##_SHIFT
#define REG_FIELD_MASK(reg, field) reg##_##field##_MASK
#define WREG32_FIELD(reg, offset, field, val)	\
	WREG32(mm##reg + offset, (RREG32(mm##reg + offset) & \
				~REG_FIELD_MASK(reg, field)) | \
				(val) << REG_FIELD_SHIFT(reg, field))

/* Timeout should be longer when working with simulator but cap the
 * increased timeout to some maximum
 */
#define hl_poll_timeout_common(hdev, addr, val, cond, sleep_us, timeout_us, elbi) \
({ \
	ktime_t __timeout; \
	u32 __elbi_read; \
	int __rc = 0; \
	__timeout = ktime_add_us(ktime_get(), timeout_us); \
	might_sleep_if(sleep_us); \
	for (;;) { \
		if (elbi) { \
			__rc = hl_pci_elbi_read(hdev, addr, &__elbi_read); \
			if (__rc) \
				break; \
			(val) = __elbi_read; \
		} else {\
			(val) = RREG32(lower_32_bits(addr)); \
		} \
		if (cond) \
			break; \
		if (timeout_us && ktime_compare(ktime_get(), __timeout) > 0) { \
			if (elbi) { \
				__rc = hl_pci_elbi_read(hdev, addr, &__elbi_read); \
				if (__rc) \
					break; \
				(val) = __elbi_read; \
			} else {\
				(val) = RREG32(lower_32_bits(addr)); \
			} \
			break; \
		} \
		if (sleep_us) \
			usleep_range((sleep_us >> 2) + 1, sleep_us); \
	} \
	__rc ? __rc : ((cond) ? 0 : -ETIMEDOUT); \
})

#define hl_poll_timeout(hdev, addr, val, cond, sleep_us, timeout_us) \
		hl_poll_timeout_common(hdev, addr, val, cond, sleep_us, timeout_us, false)

#define hl_poll_timeout_elbi(hdev, addr, val, cond, sleep_us, timeout_us) \
		hl_poll_timeout_common(hdev, addr, val, cond, sleep_us, timeout_us, true)

/*
 * poll array of register addresses.
 * condition is satisfied if all registers values match the expected value.
 * once some register in the array satisfies the condition it will not be polled again,
 * this is done both for efficiency and due to some registers are "clear on read".
 * TODO: use read from PCI bar in other places in the code (SW-91406)
 */
#define hl_poll_reg_array_timeout_common(hdev, addr_arr, arr_size, expected_val, sleep_us, \
						timeout_us, elbi) \
({ \
	ktime_t __timeout; \
	u64 __elem_bitmask; \
	u32 __read_val;	\
	u8 __arr_idx;	\
	int __rc = 0; \
	\
	__timeout = ktime_add_us(ktime_get(), timeout_us); \
	might_sleep_if(sleep_us); \
	if (arr_size >= 64) \
		__rc = -EINVAL; \
	else \
		__elem_bitmask = BIT_ULL(arr_size) - 1; \
	for (;;) { \
		if (__rc) \
			break; \
		for (__arr_idx = 0; __arr_idx < (arr_size); __arr_idx++) {	\
			if (!(__elem_bitmask & BIT_ULL(__arr_idx)))	\
				continue;	\
			if (elbi) { \
				__rc = hl_pci_elbi_read(hdev, (addr_arr)[__arr_idx], &__read_val); \
				if (__rc) \
					break; \
			} else { \
				__read_val = RREG32(lower_32_bits(addr_arr[__arr_idx])); \
			} \
			if (__read_val == (expected_val))	\
				__elem_bitmask &= ~BIT_ULL(__arr_idx);	\
		}	\
		if (__rc || (__elem_bitmask == 0)) \
			break; \
		if (timeout_us && ktime_compare(ktime_get(), __timeout) > 0) \
			break; \
		if (sleep_us) \
			usleep_range((sleep_us >> 2) + 1, sleep_us); \
	} \
	__rc ? __rc : ((__elem_bitmask == 0) ? 0 : -ETIMEDOUT); \
})

#define hl_poll_reg_array_timeout(hdev, addr_arr, arr_size, expected_val, sleep_us, \
					timeout_us) \
	hl_poll_reg_array_timeout_common(hdev, addr_arr, arr_size, expected_val, sleep_us, \
						timeout_us, false)

#define hl_poll_reg_array_timeout_elbi(hdev, addr_arr, arr_size, expected_val, sleep_us, \
					timeout_us) \
	hl_poll_reg_array_timeout_common(hdev, addr_arr, arr_size, expected_val, sleep_us, \
						timeout_us, true)

/*
 * address in this macro points always to a memory location in the
 * host's (server's) memory. That location is updated asynchronously
 * either by the direct access of the device or by another core.
 *
 * To work both in LE and BE architectures, we need to distinguish between the
 * two states (device or another core updates the memory location). Therefore,
 * if mem_written_by_device is true, the host memory being polled will be
 * updated directly by the device. If false, the host memory being polled will
 * be updated by host CPU. Required so host knows whether or not the memory
 * might need to be byte-swapped before returning value to caller.
 */
#define hl_poll_timeout_memory(hdev, addr, val, cond, sleep_us, timeout_us, \
				mem_written_by_device) \
({ \
	ktime_t __timeout; \
	\
	__timeout = ktime_add_us(ktime_get(), timeout_us); \
	might_sleep_if(sleep_us); \
	for (;;) { \
		/* Verify we read updates done by other cores or by device */ \
		mb(); \
		(val) = *((u32 *)(addr)); \
		if (mem_written_by_device) \
			(val) = le32_to_cpu(*(__le32 *) &(val)); \
		if (cond) \
			break; \
		if (timeout_us && ktime_compare(ktime_get(), __timeout) > 0) { \
			(val) = *((u32 *)(addr)); \
			if (mem_written_by_device) \
				(val) = le32_to_cpu(*(__le32 *) &(val)); \
			break; \
		} \
		if (sleep_us) \
			usleep_range((sleep_us >> 2) + 1, sleep_us); \
	} \
	(cond) ? 0 : -ETIMEDOUT; \
})

#define HL_USR_MAPPED_BLK_INIT(blk, base, sz) \
({ \
	struct user_mapped_block *p = blk; \
\
	p->address = base; \
	p->size = sz; \
})

#define HL_USR_INTR_STRUCT_INIT(usr_intr, hdev, intr_id, intr_type) \
({ \
	usr_intr.hdev = hdev; \
	usr_intr.interrupt_id = intr_id; \
	usr_intr.type = intr_type; \
	INIT_LIST_HEAD(&usr_intr.wait_list_head); \
	spin_lock_init(&usr_intr.wait_list_lock); \
})

struct hwmon_chip_info;

/**
 * struct hl_device_reset_work - reset work wrapper.
 * @reset_work: reset work to be done.
 * @hdev: habanalabs device structure.
 * @flags: reset flags.
 */
struct hl_device_reset_work {
	struct delayed_work	reset_work;
	struct hl_device	*hdev;
	u32			flags;
};

/**
 * struct hl_mmu_hr_pgt_priv - used for holding per-device mmu host-resident
 * page-table internal information.
 * @mmu_pgt_pool: pool of page tables used by a host-resident MMU for
 *                allocating hops.
 * @mmu_asid_hop0: per-ASID array of host-resident hop0 tables.
 */
struct hl_mmu_hr_priv {
	struct gen_pool	*mmu_pgt_pool;
	struct pgt_info	*mmu_asid_hop0;
};

/**
 * struct hl_mmu_dr_pgt_priv - used for holding per-device mmu device-resident
 * page-table internal information.
 * @mmu_pgt_pool: pool of page tables used by MMU for allocating hops.
 * @mmu_shadow_hop0: shadow array of hop0 tables.
 */
struct hl_mmu_dr_priv {
	struct gen_pool *mmu_pgt_pool;
	void *mmu_shadow_hop0;
};

/**
 * struct hl_mmu_priv - used for holding per-device mmu internal information.
 * @dr: information on the device-resident MMU, when exists.
 * @hr: information on the host-resident MMU, when exists.
 */
struct hl_mmu_priv {
	struct hl_mmu_dr_priv dr;
	struct hl_mmu_hr_priv hr;
};

/**
 * struct hl_mmu_per_hop_info - A structure describing one TLB HOP and its entry
 *                that was created in order to translate a virtual address to a
 *                physical one.
 * @hop_addr: The address of the hop.
 * @hop_pte_addr: The address of the hop entry.
 * @hop_pte_val: The value in the hop entry.
 */
struct hl_mmu_per_hop_info {
	u64 hop_addr;
	u64 hop_pte_addr;
	u64 hop_pte_val;
};

/**
 * struct hl_mmu_hop_info - A structure describing the TLB hops and their
 * hop-entries that were created in order to translate a virtual address to a
 * physical one.
 * @scrambled_vaddr: The value of the virtual address after scrambling. This
 *                   address replaces the original virtual-address when mapped
 *                   in the MMU tables.
 * @unscrambled_paddr: The un-scrambled physical address.
 * @hop_info: Array holding the per-hop information used for the translation.
 * @used_hops: The number of hops used for the translation.
 * @range_type: virtual address range type.
 */
struct hl_mmu_hop_info {
	u64 scrambled_vaddr;
	u64 unscrambled_paddr;
	struct hl_mmu_per_hop_info hop_info[MMU_ARCH_6_HOPS];
	u32 used_hops;
	enum hl_va_range_type range_type;
};

/**
 * struct hl_hr_mmu_funcs - Device related host resident MMU functions.
 * @get_hop0_pgt_info: get page table info structure for HOP0.
 * @get_pgt_info: get page table info structure for HOP other than HOP0.
 * @add_pgt_info: add page table info structure to hash.
 * @get_tlb_mapping_params: get mapping parameters needed for getting TLB info for specific mapping.
 */
struct hl_hr_mmu_funcs {
	struct pgt_info *(*get_hop0_pgt_info)(struct hl_ctx *ctx);
	struct pgt_info *(*get_pgt_info)(struct hl_ctx *ctx, u64 phys_hop_addr);
	void (*add_pgt_info)(struct hl_ctx *ctx, struct pgt_info *pgt_info, dma_addr_t phys_addr);
	int (*get_tlb_mapping_params)(struct hl_device *hdev, struct hl_mmu_properties **mmu_prop,
								struct hl_mmu_hop_info *hops,
								u64 virt_addr, bool *is_huge);
};

/**
 * struct hl_mmu_funcs - Device related MMU functions.
 * @init: initialize the MMU module.
 * @fini: release the MMU module.
 * @ctx_init: Initialize a context for using the MMU module.
 * @ctx_fini: disable a ctx from using the mmu module.
 * @map: maps a virtual address to physical address for a context.
 * @unmap: unmap a virtual address of a context.
 * @flush: flush all writes from all cores to reach device MMU.
 * @swap_out: marks all mapping of the given context as swapped out.
 * @swap_in: marks all mapping of the given context as swapped in.
 * @get_tlb_info: returns the list of hops and hop-entries used that were
 *                created in order to translate the giver virtual address to a
 *                physical one.
 * @hr_funcs: functions specific to host resident MMU.
 */
struct hl_mmu_funcs {
	int (*init)(struct hl_device *hdev);
	void (*fini)(struct hl_device *hdev);
	int (*ctx_init)(struct hl_ctx *ctx);
	void (*ctx_fini)(struct hl_ctx *ctx);
	int (*map)(struct hl_ctx *ctx, u64 virt_addr, u64 phys_addr, u32 page_size,
				bool is_dram_addr);
	int (*unmap)(struct hl_ctx *ctx, u64 virt_addr, bool is_dram_addr);
	void (*flush)(struct hl_ctx *ctx);
	void (*swap_out)(struct hl_ctx *ctx);
	void (*swap_in)(struct hl_ctx *ctx);
	int (*get_tlb_info)(struct hl_ctx *ctx, u64 virt_addr, struct hl_mmu_hop_info *hops);
	struct hl_hr_mmu_funcs hr_funcs;
};

/**
 * struct hl_prefetch_work - prefetch work structure handler
 * @prefetch_work: actual work struct.
 * @ctx: compute context.
 * @va: virtual address to pre-fetch.
 * @size: pre-fetch size.
 * @flags: operation flags.
 * @asid: ASID for maintenance operation.
 */
struct hl_prefetch_work {
	struct work_struct	prefetch_work;
	struct hl_ctx		*ctx;
	u64			va;
	u64			size;
	u32			flags;
	u32			asid;
};

/*
 * number of user contexts allowed to call wait_for_multi_cs ioctl in
 * parallel
 */
#define MULTI_CS_MAX_USER_CTX	2

/**
 * struct multi_cs_completion - multi CS wait completion.
 * @completion: completion of any of the CS in the list
 * @lock: spinlock for the completion structure
 * @timestamp: timestamp for the multi-CS completion
 * @stream_master_qid_map: bitmap of all stream masters on which the multi-CS
 *                        is waiting
 * @used: 1 if in use, otherwise 0
 */
struct multi_cs_completion {
	struct completion	completion;
	spinlock_t		lock;
	s64			timestamp;
	u32			stream_master_qid_map;
	u8			used;
};

/**
 * struct multi_cs_data - internal data for multi CS call
 * @ctx: pointer to the context structure
 * @fence_arr: array of fences of all CSs
 * @seq_arr: array of CS sequence numbers
 * @timeout_jiffies: timeout in jiffies for waiting for CS to complete
 * @timestamp: timestamp of first completed CS
 * @wait_status: wait for CS status
 * @completion_bitmap: bitmap of completed CSs (1- completed, otherwise 0)
 * @arr_len: fence_arr and seq_arr array length
 * @gone_cs: indication of gone CS (1- there was gone CS, otherwise 0)
 * @update_ts: update timestamp. 1- update the timestamp, otherwise 0.
 */
struct multi_cs_data {
	struct hl_ctx	*ctx;
	struct hl_fence	**fence_arr;
	u64		*seq_arr;
	s64		timeout_jiffies;
	s64		timestamp;
	long		wait_status;
	u32		completion_bitmap;
	u8		arr_len;
	u8		gone_cs;
	u8		update_ts;
};

/**
 * struct hl_clk_throttle_timestamp - current/last clock throttling timestamp
 * @start: timestamp taken when 'start' event is received in driver
 * @end: timestamp taken when 'end' event is received in driver
 */
struct hl_clk_throttle_timestamp {
	ktime_t		start;
	ktime_t		end;
};

/**
 * struct hl_clk_throttle - keeps current/last clock throttling timestamps
 * @timestamp: timestamp taken by driver and firmware, index 0 refers to POWER
 *             index 1 refers to THERMAL
 * @lock: protects this structure as it can be accessed from both event queue
 *        context and info_ioctl context
 * @current_reason: bitmask represents the current clk throttling reasons
 * @aggregated_reason: bitmask represents aggregated clk throttling reasons since driver load
 */
struct hl_clk_throttle {
	struct hl_clk_throttle_timestamp timestamp[HL_CLK_THROTTLE_TYPE_MAX];
	struct mutex	lock;
	u32		current_reason;
	u32		aggregated_reason;
};

/**
 * struct user_mapped_block - describes a hw block allowed to be mmapped by user
 * @address: physical HW block address
 * @size: allowed size for mmap
 */
struct user_mapped_block {
	u32 address;
	u32 size;
};

/**
 * struct cs_timeout_info - info of last CS timeout occurred.
 * @timestamp: CS timeout timestamp.
 * @write_enable: if set writing to CS parameters in the structure is enabled. otherwise - disabled,
 *                so the first (root cause) CS timeout will not be overwritten.
 * @seq: CS timeout sequence number.
 */
struct cs_timeout_info {
	ktime_t		timestamp;
	atomic_t	write_enable;
	u64		seq;
};

#define MAX_QMAN_STREAMS_INFO		4
#define OPCODE_INFO_MAX_ADDR_SIZE	8
/**
 * struct undefined_opcode_info - info about last undefined opcode error
 * @timestamp: timestamp of the undefined opcode error
 * @cb_addr_streams: CB addresses (per stream) that are currently exists in the PQ
 *                   entries. In case all streams array entries are
 *                   filled with values, it means the execution was in Lower-CP.
 * @cq_addr: the address of the current handled command buffer
 * @cq_size: the size of the current handled command buffer
 * @cb_addr_streams_len: num of streams - actual len of cb_addr_streams array.
 *                       should be equal to 1 in case of undefined opcode
 *                       in Upper-CP (specific stream) and equal to 4 in case
 *                       of undefined opcode in Lower-CP.
 * @engine_id: engine-id that the error occurred on
 * @stream_id: the stream id the error occurred on. In case the stream equals to
 *             MAX_QMAN_STREAMS_INFO it means the error occurred on a Lower-CP.
 * @write_enable: if set, writing to undefined opcode parameters in the structure
 *                 is enable so the first (root cause) undefined opcode will not be
 *                 overwritten.
 */
struct undefined_opcode_info {
	ktime_t timestamp;
	u64 cb_addr_streams[MAX_QMAN_STREAMS_INFO][OPCODE_INFO_MAX_ADDR_SIZE];
	u64 cq_addr;
	u32 cq_size;
	u32 cb_addr_streams_len;
	u32 engine_id;
	u32 stream_id;
	bool write_enable;
};

/**
 * struct page_fault_info - page fault information.
 * @page_fault: holds information collected during a page fault.
 * @user_mappings: buffer containing user mappings.
 * @num_of_user_mappings: number of user mappings.
 * @page_fault_detected: if set as 1, then a page-fault was discovered for the
 *                       first time after the driver has finished booting-up.
 *                       Since we're looking for the page-fault's root cause,
 *                       we don't care of the others that might follow it-
 *                       so once changed to 1, it will remain that way.
 * @page_fault_info_available: indicates that a page fault info is now available.
 */
struct page_fault_info {
	struct hl_page_fault_info	page_fault;
	struct hl_user_mapping		*user_mappings;
	u64				num_of_user_mappings;
	atomic_t			page_fault_detected;
	bool				page_fault_info_available;
};

/**
 * struct razwi_info - RAZWI information.
 * @razwi: holds information collected during a RAZWI
 * @razwi_detected: if set as 1, then a RAZWI was discovered for the
 *                  first time after the driver has finished booting-up.
 *                  Since we're looking for the RAZWI's root cause,
 *                  we don't care of the others that might follow it-
 *                  so once changed to 1, it will remain that way.
 * @razwi_info_available: indicates that a RAZWI info is now available.
 */
struct razwi_info {
	struct hl_info_razwi_event	razwi;
	atomic_t			razwi_detected;
	bool				razwi_info_available;
};

/**
 * struct hw_err_info - HW error information.
 * @event: holds information on the event.
 * @event_detected: if set as 1, then a HW event was discovered for the
 *                  first time after the driver has finished booting-up.
 *                  currently we assume that only fatal events (that require hard-reset) are
 *                  reported so we don't care of the others that might follow it.
 *                  so once changed to 1, it will remain that way.
 *                  TODO: support multiple events.
 * @event_info_available: indicates that a HW event info is now available.
 */
struct hw_err_info {
	struct hl_info_hw_err_event	event;
	atomic_t			event_detected;
	bool				event_info_available;
};

/**
 * struct fw_err_info - FW error information.
 * @event: holds information on the event.
 * @event_detected: if set as 1, then a FW event was discovered for the
 *                  first time after the driver has finished booting-up.
 *                  currently we assume that only fatal events (that require hard-reset) are
 *                  reported so we don't care of the others that might follow it.
 *                  so once changed to 1, it will remain that way.
 *                  TODO: support multiple events.
 * @event_info_available: indicates that a HW event info is now available.
 */
struct fw_err_info {
	struct hl_info_fw_err_event	event;
	atomic_t			event_detected;
	bool				event_info_available;
};

/**
 * struct hl_error_info - holds information collected during an error.
 * @cs_timeout: CS timeout error information.
 * @razwi_info: RAZWI information.
 * @undef_opcode: undefined opcode information.
 * @page_fault_info: page fault information.
 * @hw_err: (fatal) hardware error information.
 * @fw_err: firmware error information.
 */
struct hl_error_info {
	struct cs_timeout_info		cs_timeout;
	struct razwi_info		razwi_info;
	struct undefined_opcode_info	undef_opcode;
	struct page_fault_info		page_fault_info;
	struct hw_err_info		hw_err;
	struct fw_err_info		fw_err;
};

/**
 * struct hl_reset_info - holds current device reset information.
 * @lock: lock to protect critical reset flows.
 * @compute_reset_cnt: number of compute resets since the driver was loaded.
 * @hard_reset_cnt: number of hard resets since the driver was loaded.
 * @hard_reset_schedule_flags: hard reset is scheduled to after current compute reset,
 *                             here we hold the hard reset flags.
 * @in_reset: is device in reset flow.
 * @in_compute_reset: Device is currently in reset but not in hard-reset.
 * @needs_reset: true if reset_on_lockup is false and device should be reset
 *               due to lockup.
 * @hard_reset_pending: is there a hard reset work pending.
 * @curr_reset_cause: saves an enumerated reset cause when a hard reset is
 *                    triggered, and cleared after it is shared with preboot.
 * @prev_reset_trigger: saves the previous trigger which caused a reset, overridden
 *                      with a new value on next reset
 * @reset_trigger_repeated: set if device reset is triggered more than once with
 *                          same cause.
 * @skip_reset_on_timeout: Skip device reset if CS has timed out, wait for it to
 *                         complete instead.
 * @watchdog_active: true if a device release watchdog work is scheduled.
 */
struct hl_reset_info {
	spinlock_t	lock;
	u32		compute_reset_cnt;
	u32		hard_reset_cnt;
	u32		hard_reset_schedule_flags;
	u8		in_reset;
	u8		in_compute_reset;
	u8		needs_reset;
	u8		hard_reset_pending;
	u8		curr_reset_cause;
	u8		prev_reset_trigger;
	u8		reset_trigger_repeated;
	u8		skip_reset_on_timeout;
	u8		watchdog_active;
};

/**
 * struct hl_device - habanalabs device structure.
 * @pdev: pointer to PCI device, can be NULL in case of simulator device.
 * @pcie_bar_phys: array of available PCIe bars physical addresses.
 *		   (required only for PCI address match mode)
 * @pcie_bar: array of available PCIe bars virtual addresses.
 * @rmmio: configuration area address on SRAM.
 * @hclass: pointer to the habanalabs class.
 * @cdev: related char device.
 * @cdev_ctrl: char device for control operations only (INFO IOCTL)
 * @dev: related kernel basic device structure.
 * @dev_ctrl: related kernel device structure for the control device
 * @work_heartbeat: delayed work for CPU-CP is-alive check.
 * @device_reset_work: delayed work which performs hard reset
 * @device_release_watchdog_work: watchdog work that performs hard reset if user doesn't release
 *                                device upon certain error cases.
 * @asic_name: ASIC specific name.
 * @asic_type: ASIC specific type.
 * @completion_queue: array of hl_cq.
 * @user_interrupt: array of hl_user_interrupt. upon the corresponding user
 *                  interrupt, driver will monitor the list of fences
 *                  registered to this interrupt.
 * @tpc_interrupt: single TPC interrupt for all TPCs.
 * @unexpected_error_interrupt: single interrupt for unexpected user error indication.
 * @common_user_cq_interrupt: common user CQ interrupt for all user CQ interrupts.
 *                         upon any user CQ interrupt, driver will monitor the
 *                         list of fences registered to this common structure.
 * @common_decoder_interrupt: common decoder interrupt for all user decoder interrupts.
 * @shadow_cs_queue: pointer to a shadow queue that holds pointers to
 *                   outstanding command submissions.
 * @cq_wq: work queues of completion queues for executing work in process
 *         context.
 * @eq_wq: work queue of event queue for executing work in process context.
 * @cs_cmplt_wq: work queue of CS completions for executing work in process
 *               context.
 * @ts_free_obj_wq: work queue for timestamp registration objects release.
 * @prefetch_wq: work queue for MMU pre-fetch operations.
 * @reset_wq: work queue for device reset procedure.
 * @kernel_ctx: Kernel driver context structure.
 * @kernel_queues: array of hl_hw_queue.
 * @cs_mirror_list: CS mirror list for TDR.
 * @cs_mirror_lock: protects cs_mirror_list.
 * @kernel_mem_mgr: memory manager for memory buffers with lifespan of driver.
 * @event_queue: event queue for IRQ from CPU-CP.
 * @dma_pool: DMA pool for small allocations.
 * @cpu_accessible_dma_mem: Host <-> CPU-CP shared memory CPU address.
 * @cpu_accessible_dma_address: Host <-> CPU-CP shared memory DMA address.
 * @cpu_accessible_dma_pool: Host <-> CPU-CP shared memory pool.
 * @asid_bitmap: holds used/available ASIDs.
 * @asid_mutex: protects asid_bitmap.
 * @send_cpu_message_lock: enforces only one message in Host <-> CPU-CP queue.
 * @debug_lock: protects critical section of setting debug mode for device
 * @mmu_lock: protects the MMU page tables and invalidation h/w. Although the
 *            page tables are per context, the invalidation h/w is per MMU.
 *            Therefore, we can't allow multiple contexts (we only have two,
 *            user and kernel) to access the invalidation h/w at the same time.
 *            In addition, any change to the PGT, modifying the MMU hash or
 *            walking the PGT requires talking this lock.
 * @asic_prop: ASIC specific immutable properties.
 * @asic_funcs: ASIC specific functions.
 * @asic_specific: ASIC specific information to use only from ASIC files.
 * @vm: virtual memory manager for MMU.
 * @hwmon_dev: H/W monitor device.
 * @hl_chip_info: ASIC's sensors information.
 * @device_status_description: device status description.
 * @hl_debugfs: device's debugfs manager.
 * @cb_pool: list of pre allocated CBs.
 * @cb_pool_lock: protects the CB pool.
 * @internal_cb_pool_virt_addr: internal command buffer pool virtual address.
 * @internal_cb_pool_dma_addr: internal command buffer pool dma address.
 * @internal_cb_pool: internal command buffer memory pool.
 * @internal_cb_va_base: internal cb pool mmu virtual address base
 * @fpriv_list: list of file private data structures. Each structure is created
 *              when a user opens the device
 * @fpriv_ctrl_list: list of file private data structures. Each structure is created
 *              when a user opens the control device
 * @fpriv_list_lock: protects the fpriv_list
 * @fpriv_ctrl_list_lock: protects the fpriv_ctrl_list
 * @aggregated_cs_counters: aggregated cs counters among all contexts
 * @mmu_priv: device-specific MMU data.
 * @mmu_func: device-related MMU functions.
 * @dec: list of decoder sw instance
 * @fw_loader: FW loader manager.
 * @pci_mem_region: array of memory regions in the PCI
 * @state_dump_specs: constants and dictionaries needed to dump system state.
 * @multi_cs_completion: array of multi-CS completion.
 * @clk_throttling: holds information about current/previous clock throttling events
 * @captured_err_info: holds information about errors.
 * @reset_info: holds current device reset information.
 * @stream_master_qid_arr: pointer to array with QIDs of master streams.
 * @fw_inner_major_ver: the major of current loaded preboot inner version.
 * @fw_inner_minor_ver: the minor of current loaded preboot inner version.
 * @fw_sw_major_ver: the major of current loaded preboot SW version.
 * @fw_sw_minor_ver: the minor of current loaded preboot SW version.
 * @fw_sw_sub_minor_ver: the sub-minor of current loaded preboot SW version.
 * @dram_used_mem: current DRAM memory consumption.
 * @memory_scrub_val: the value to which the dram will be scrubbed to using cb scrub_device_dram
 * @timeout_jiffies: device CS timeout value.
 * @max_power: the max power of the device, as configured by the sysadmin. This
 *             value is saved so in case of hard-reset, the driver will restore
 *             this value and update the F/W after the re-initialization
 * @boot_error_status_mask: contains a mask of the device boot error status.
 *                          Each bit represents a different error, according to
 *                          the defines in hl_boot_if.h. If the bit is cleared,
 *                          the error will be ignored by the driver during
 *                          device initialization. Mainly used to debug and
 *                          workaround firmware bugs
 * @dram_pci_bar_start: start bus address of PCIe bar towards DRAM.
 * @last_successful_open_ktime: timestamp (ktime) of the last successful device open.
 * @last_successful_open_jif: timestamp (jiffies) of the last successful
 *                            device open.
 * @last_open_session_duration_jif: duration (jiffies) of the last device open
 *                                  session.
 * @open_counter: number of successful device open operations.
 * @fw_poll_interval_usec: FW status poll interval in usec.
 *                         used for CPU boot status
 * @fw_comms_poll_interval_usec: FW comms/protocol poll interval in usec.
 *                                  used for COMMs protocols cmds(COMMS_STS_*)
 * @dram_binning: contains mask of drams that is received from the f/w which indicates which
 *                drams are binned-out
 * @tpc_binning: contains mask of tpc engines that is received from the f/w which indicates which
 *               tpc engines are binned-out
 * @dmabuf_export_cnt: number of dma-buf exporting.
 * @card_type: Various ASICs have several card types. This indicates the card
 *             type of the current device.
 * @major: habanalabs kernel driver major.
 * @high_pll: high PLL profile frequency.
 * @decoder_binning: contains mask of decoder engines that is received from the f/w which
 *                   indicates which decoder engines are binned-out
 * @edma_binning: contains mask of edma engines that is received from the f/w which
 *                   indicates which edma engines are binned-out
 * @device_release_watchdog_timeout_sec: device release watchdog timeout value in seconds.
 * @rotator_binning: contains mask of rotators engines that is received from the f/w
 *			which indicates which rotator engines are binned-out(Gaudi3 and above).
 * @id: device minor.
 * @id_control: minor of the control device.
 * @cdev_idx: char device index. Used for setting its name.
 * @cpu_pci_msb_addr: 50-bit extension bits for the device CPU's 40-bit
 *                    addresses.
 * @is_in_dram_scrub: true if dram scrub operation is on going.
 * @disabled: is device disabled.
 * @late_init_done: is late init stage was done during initialization.
 * @hwmon_initialized: is H/W monitor sensors was initialized.
 * @reset_on_lockup: true if a reset should be done in case of stuck CS, false
 *                   otherwise.
 * @dram_default_page_mapping: is DRAM default page mapping enabled.
 * @memory_scrub: true to perform device memory scrub in various locations,
 *                such as context-switch, context close, page free, etc.
 * @pmmu_huge_range: is a different virtual addresses range used for PMMU with
 *                   huge pages.
 * @init_done: is the initialization of the device done.
 * @device_cpu_disabled: is the device CPU disabled (due to timeouts)
 * @in_debug: whether the device is in a state where the profiling/tracing infrastructure
 *            can be used. This indication is needed because in some ASICs we need to do
 *            specific operations to enable that infrastructure.
 * @cdev_sysfs_debugfs_created: were char devices and sysfs/debugfs files created.
 * @stop_on_err: true if engines should stop on error.
 * @supports_sync_stream: is sync stream supported.
 * @sync_stream_queue_idx: helper index for sync stream queues initialization.
 * @collective_mon_idx: helper index for collective initialization
 * @supports_coresight: is CoreSight supported.
 * @supports_cb_mapping: is mapping a CB to the device's MMU supported.
 * @process_kill_trial_cnt: number of trials reset thread tried killing
 *                          user processes
 * @device_fini_pending: true if device_fini was called and might be
 *                       waiting for the reset thread to finish
 * @supports_staged_submission: true if staged submissions are supported
 * @device_cpu_is_halted: Flag to indicate whether the device CPU was already
 *                        halted. We can't halt it again because the COMMS
 *                        protocol will throw an error. Relevant only for
 *                        cases where Linux was not loaded to device CPU
 * @supports_wait_for_multi_cs: true if wait for multi CS is supported
 * @is_compute_ctx_active: Whether there is an active compute context executing.
 * @compute_ctx_in_release: true if the current compute context is being released.
 * @supports_mmu_prefetch: true if prefetch is supported, otherwise false.
 * @reset_upon_device_release: reset the device when the user closes the file descriptor of the
 *                             device.
 * @supports_ctx_switch: true if a ctx switch is required upon first submission.
 * @support_preboot_binning: true if we support read binning info from preboot.
 * @nic_ports_mask: Controls which NIC ports are enabled. Used only for testing.
 * @fw_components: Controls which f/w components to load to the device. There are multiple f/w
 *                 stages and sometimes we want to stop at a certain stage. Used only for testing.
 * @mmu_disable: Disable the device MMU(s). Used only for testing.
 * @cpu_queues_enable: Whether to enable queues communication vs. the f/w. Used only for testing.
 * @pldm: Whether we are running in Palladium environment. Used only for testing.
 * @hard_reset_on_fw_events: Whether to do device hard-reset when a fatal event is received from
 *                           the f/w. Used only for testing.
 * @bmc_enable: Whether we are running in a box with BMC. Used only for testing.
 * @reset_on_preboot_fail: Whether to reset the device if preboot f/w fails to load.
 *                         Used only for testing.
 * @heartbeat: Controls if we want to enable the heartbeat mechanism vs. the f/w, which verifies
 *             that the f/w is always alive. Used only for testing.
 */
struct hl_device {
	struct pci_dev			*pdev;
	u64				pcie_bar_phys[HL_PCI_NUM_BARS];
	void __iomem			*pcie_bar[HL_PCI_NUM_BARS];
	void __iomem			*rmmio;
	struct class			*hclass;
	struct cdev			cdev;
	struct cdev			cdev_ctrl;
	struct device			*dev;
	struct device			*dev_ctrl;
	struct delayed_work		work_heartbeat;
	struct hl_device_reset_work	device_reset_work;
	struct hl_device_reset_work	device_release_watchdog_work;
	char				asic_name[HL_STR_MAX];
	char				status[HL_DEV_STS_MAX][HL_STR_MAX];
	enum hl_asic_type		asic_type;
	struct hl_cq			*completion_queue;
	struct hl_user_interrupt	*user_interrupt;
	struct hl_user_interrupt	tpc_interrupt;
	struct hl_user_interrupt	unexpected_error_interrupt;
	struct hl_user_interrupt	common_user_cq_interrupt;
	struct hl_user_interrupt	common_decoder_interrupt;
	struct hl_cs			**shadow_cs_queue;
	struct workqueue_struct		**cq_wq;
	struct workqueue_struct		*eq_wq;
	struct workqueue_struct		*cs_cmplt_wq;
	struct workqueue_struct		*ts_free_obj_wq;
	struct workqueue_struct		*prefetch_wq;
	struct workqueue_struct		*reset_wq;
	struct hl_ctx			*kernel_ctx;
	struct hl_hw_queue		*kernel_queues;
	struct list_head		cs_mirror_list;
	spinlock_t			cs_mirror_lock;
	struct hl_mem_mgr		kernel_mem_mgr;
	struct hl_eq			event_queue;
	struct dma_pool			*dma_pool;
	void				*cpu_accessible_dma_mem;
	dma_addr_t			cpu_accessible_dma_address;
	struct gen_pool			*cpu_accessible_dma_pool;
	unsigned long			*asid_bitmap;
	struct mutex			asid_mutex;
	struct mutex			send_cpu_message_lock;
	struct mutex			debug_lock;
	struct mutex			mmu_lock;
	struct asic_fixed_properties	asic_prop;
	const struct hl_asic_funcs	*asic_funcs;
	void				*asic_specific;
	struct hl_vm			vm;
	struct device			*hwmon_dev;
	struct hwmon_chip_info		*hl_chip_info;

	struct hl_dbg_device_entry	hl_debugfs;

	struct list_head		cb_pool;
	spinlock_t			cb_pool_lock;

	void				*internal_cb_pool_virt_addr;
	dma_addr_t			internal_cb_pool_dma_addr;
	struct gen_pool			*internal_cb_pool;
	u64				internal_cb_va_base;

	struct list_head		fpriv_list;
	struct list_head		fpriv_ctrl_list;
	struct mutex			fpriv_list_lock;
	struct mutex			fpriv_ctrl_list_lock;

	struct hl_cs_counters_atomic	aggregated_cs_counters;

	struct hl_mmu_priv		mmu_priv;
	struct hl_mmu_funcs		mmu_func[MMU_NUM_PGT_LOCATIONS];

	struct hl_dec			*dec;

	struct fw_load_mgr		fw_loader;

	struct pci_mem_region		pci_mem_region[PCI_REGION_NUMBER];

	struct hl_state_dump_specs	state_dump_specs;

	struct multi_cs_completion	multi_cs_completion[
							MULTI_CS_MAX_USER_CTX];
	struct hl_clk_throttle		clk_throttling;
	struct hl_error_info		captured_err_info;

	struct hl_reset_info		reset_info;

	u32				*stream_master_qid_arr;
	u32				fw_inner_major_ver;
	u32				fw_inner_minor_ver;
	u32				fw_sw_major_ver;
	u32				fw_sw_minor_ver;
	u32				fw_sw_sub_minor_ver;
	atomic64_t			dram_used_mem;
	u64				memory_scrub_val;
	u64				timeout_jiffies;
	u64				max_power;
	u64				boot_error_status_mask;
	u64				dram_pci_bar_start;
	u64				last_successful_open_jif;
	u64				last_open_session_duration_jif;
	u64				open_counter;
	u64				fw_poll_interval_usec;
	ktime_t				last_successful_open_ktime;
	u64				fw_comms_poll_interval_usec;
	u64				dram_binning;
	u64				tpc_binning;
	atomic_t			dmabuf_export_cnt;
	enum cpucp_card_types		card_type;
	u32				major;
	u32				high_pll;
	u32				decoder_binning;
	u32				edma_binning;
	u32				device_release_watchdog_timeout_sec;
	u32				rotator_binning;
	u16				id;
	u16				id_control;
	u16				cdev_idx;
	u16				cpu_pci_msb_addr;
	u8				is_in_dram_scrub;
	u8				disabled;
	u8				late_init_done;
	u8				hwmon_initialized;
	u8				reset_on_lockup;
	u8				dram_default_page_mapping;
	u8				memory_scrub;
	u8				pmmu_huge_range;
	u8				init_done;
	u8				device_cpu_disabled;
	u8				in_debug;
	u8				cdev_sysfs_debugfs_created;
	u8				stop_on_err;
	u8				supports_sync_stream;
	u8				sync_stream_queue_idx;
	u8				collective_mon_idx;
	u8				supports_coresight;
	u8				supports_cb_mapping;
	u8				process_kill_trial_cnt;
	u8				device_fini_pending;
	u8				supports_staged_submission;
	u8				device_cpu_is_halted;
	u8				supports_wait_for_multi_cs;
	u8				stream_master_qid_arr_size;
	u8				is_compute_ctx_active;
	u8				compute_ctx_in_release;
	u8				supports_mmu_prefetch;
	u8				reset_upon_device_release;
	u8				supports_ctx_switch;
	u8				support_preboot_binning;

	/* Parameters for bring-up to be upstreamed */
	u64				nic_ports_mask;
	u64				fw_components;
	u8				mmu_disable;
	u8				cpu_queues_enable;
	u8				pldm;
	u8				hard_reset_on_fw_events;
	u8				bmc_enable;
	u8				reset_on_preboot_fail;
	u8				heartbeat;
};


/**
 * struct hl_cs_encaps_sig_handle - encapsulated signals handle structure
 * @refcount: refcount used to protect removing this id when several
 *            wait cs are used to wait of the reserved encaps signals.
 * @hdev: pointer to habanalabs device structure.
 * @hw_sob: pointer to  H/W SOB used in the reservation.
 * @ctx: pointer to the user's context data structure
 * @cs_seq: staged cs sequence which contains encapsulated signals
 * @id: idr handler id to be used to fetch the handler info
 * @q_idx: stream queue index
 * @pre_sob_val: current SOB value before reservation
 * @count: signals number
 */
struct hl_cs_encaps_sig_handle {
	struct kref refcount;
	struct hl_device *hdev;
	struct hl_hw_sob *hw_sob;
	struct hl_ctx *ctx;
	u64  cs_seq;
	u32  id;
	u32  q_idx;
	u32  pre_sob_val;
	u32  count;
};

/**
 * struct hl_info_fw_err_info - firmware error information structure
 * @err_type: The type of error detected (or reported).
 * @event_mask: Pointer to the event mask to be modified with the detected error flag
 *              (can be NULL)
 * @event_id: The id of the event that reported the error
 *            (applicable when err_type is HL_INFO_FW_REPORTED_ERR).
 */
struct hl_info_fw_err_info {
	enum hl_info_fw_err_type err_type;
	u64 *event_mask;
	u16 event_id;
};

/*
 * IOCTLs
 */

/**
 * typedef hl_ioctl_t - typedef for ioctl function in the driver
 * @hpriv: pointer to the FD's private data, which contains state of
 *		user process
 * @data: pointer to the input/output arguments structure of the IOCTL
 *
 * Return: 0 for success, negative value for error
 */
typedef int hl_ioctl_t(struct hl_fpriv *hpriv, void *data);

/**
 * struct hl_ioctl_desc - describes an IOCTL entry of the driver.
 * @cmd: the IOCTL code as created by the kernel macros.
 * @func: pointer to the driver's function that should be called for this IOCTL.
 */
struct hl_ioctl_desc {
	unsigned int cmd;
	hl_ioctl_t *func;
};

static inline bool hl_is_fw_sw_ver_below(struct hl_device *hdev, u32 fw_sw_major, u32 fw_sw_minor)
{
	if (hdev->fw_sw_major_ver < fw_sw_major)
		return true;
	if (hdev->fw_sw_major_ver > fw_sw_major)
		return false;
	if (hdev->fw_sw_minor_ver < fw_sw_minor)
		return true;
	return false;
}

/*
 * Kernel module functions that can be accessed by entire module
 */

/**
 * hl_get_sg_info() - get number of pages and the DMA address from SG list.
 * @sg: the SG list.
 * @dma_addr: pointer to DMA address to return.
 *
 * Calculate the number of consecutive pages described by the SG list. Take the
 * offset of the address in the first page, add to it the length and round it up
 * to the number of needed pages.
 */
static inline u32 hl_get_sg_info(struct scatterlist *sg, dma_addr_t *dma_addr)
{
	*dma_addr = sg_dma_address(sg);

	return ((((*dma_addr) & (PAGE_SIZE - 1)) + sg_dma_len(sg)) +
			(PAGE_SIZE - 1)) >> PAGE_SHIFT;
}

/**
 * hl_mem_area_inside_range() - Checks whether address+size are inside a range.
 * @address: The start address of the area we want to validate.
 * @size: The size in bytes of the area we want to validate.
 * @range_start_address: The start address of the valid range.
 * @range_end_address: The end address of the valid range.
 *
 * Return: true if the area is inside the valid range, false otherwise.
 */
static inline bool hl_mem_area_inside_range(u64 address, u64 size,
				u64 range_start_address, u64 range_end_address)
{
	u64 end_address = address + size;

	if ((address >= range_start_address) &&
			(end_address <= range_end_address) &&
			(end_address > address))
		return true;

	return false;
}

/**
 * hl_mem_area_crosses_range() - Checks whether address+size crossing a range.
 * @address: The start address of the area we want to validate.
 * @size: The size in bytes of the area we want to validate.
 * @range_start_address: The start address of the valid range.
 * @range_end_address: The end address of the valid range.
 *
 * Return: true if the area overlaps part or all of the valid range,
 *		false otherwise.
 */
static inline bool hl_mem_area_crosses_range(u64 address, u32 size,
				u64 range_start_address, u64 range_end_address)
{
	u64 end_address = address + size - 1;

	return ((address <= range_end_address) && (range_start_address <= end_address));
}

uint64_t hl_set_dram_bar_default(struct hl_device *hdev, u64 addr);
void *hl_cpu_accessible_dma_pool_alloc(struct hl_device *hdev, size_t size, dma_addr_t *dma_handle);
void hl_cpu_accessible_dma_pool_free(struct hl_device *hdev, size_t size, void *vaddr);
void *hl_asic_dma_alloc_coherent_caller(struct hl_device *hdev, size_t size, dma_addr_t *dma_handle,
					gfp_t flag, const char *caller);
void hl_asic_dma_free_coherent_caller(struct hl_device *hdev, size_t size, void *cpu_addr,
					dma_addr_t dma_handle, const char *caller);
void *hl_asic_dma_pool_zalloc_caller(struct hl_device *hdev, size_t size, gfp_t mem_flags,
					dma_addr_t *dma_handle, const char *caller);
void hl_asic_dma_pool_free_caller(struct hl_device *hdev, void *vaddr, dma_addr_t dma_addr,
					const char *caller);
int hl_dma_map_sgtable(struct hl_device *hdev, struct sg_table *sgt, enum dma_data_direction dir);
void hl_dma_unmap_sgtable(struct hl_device *hdev, struct sg_table *sgt,
				enum dma_data_direction dir);
int hl_access_sram_dram_region(struct hl_device *hdev, u64 addr, u64 *val,
	enum debugfs_access_type acc_type, enum pci_region region_type, bool set_dram_bar);
int hl_access_cfg_region(struct hl_device *hdev, u64 addr, u64 *val,
	enum debugfs_access_type acc_type);
int hl_access_dev_mem(struct hl_device *hdev, enum pci_region region_type,
			u64 addr, u64 *val, enum debugfs_access_type acc_type);
int hl_device_open(struct inode *inode, struct file *filp);
int hl_device_open_ctrl(struct inode *inode, struct file *filp);
bool hl_device_operational(struct hl_device *hdev,
		enum hl_device_status *status);
bool hl_ctrl_device_operational(struct hl_device *hdev,
		enum hl_device_status *status);
enum hl_device_status hl_device_status(struct hl_device *hdev);
int hl_device_set_debug_mode(struct hl_device *hdev, struct hl_ctx *ctx, bool enable);
int hl_hw_queues_create(struct hl_device *hdev);
void hl_hw_queues_destroy(struct hl_device *hdev);
int hl_hw_queue_send_cb_no_cmpl(struct hl_device *hdev, u32 hw_queue_id,
		u32 cb_size, u64 cb_ptr);
void hl_hw_queue_submit_bd(struct hl_device *hdev, struct hl_hw_queue *q,
		u32 ctl, u32 len, u64 ptr);
int hl_hw_queue_schedule_cs(struct hl_cs *cs);
u32 hl_hw_queue_add_ptr(u32 ptr, u16 val);
void hl_hw_queue_inc_ci_kernel(struct hl_device *hdev, u32 hw_queue_id);
void hl_hw_queue_update_ci(struct hl_cs *cs);
void hl_hw_queue_reset(struct hl_device *hdev, bool hard_reset);

#define hl_queue_inc_ptr(p)		hl_hw_queue_add_ptr(p, 1)
#define hl_pi_2_offset(pi)		((pi) & (HL_QUEUE_LENGTH - 1))

int hl_cq_init(struct hl_device *hdev, struct hl_cq *q, u32 hw_queue_id);
void hl_cq_fini(struct hl_device *hdev, struct hl_cq *q);
int hl_eq_init(struct hl_device *hdev, struct hl_eq *q);
void hl_eq_fini(struct hl_device *hdev, struct hl_eq *q);
void hl_cq_reset(struct hl_device *hdev, struct hl_cq *q);
void hl_eq_reset(struct hl_device *hdev, struct hl_eq *q);
irqreturn_t hl_irq_handler_cq(int irq, void *arg);
irqreturn_t hl_irq_handler_eq(int irq, void *arg);
irqreturn_t hl_irq_handler_dec_abnrm(int irq, void *arg);
irqreturn_t hl_irq_handler_user_interrupt(int irq, void *arg);
irqreturn_t hl_irq_user_interrupt_thread_handler(int irq, void *arg);
u32 hl_cq_inc_ptr(u32 ptr);

int hl_asid_init(struct hl_device *hdev);
void hl_asid_fini(struct hl_device *hdev);
unsigned long hl_asid_alloc(struct hl_device *hdev);
void hl_asid_free(struct hl_device *hdev, unsigned long asid);

int hl_ctx_create(struct hl_device *hdev, struct hl_fpriv *hpriv);
void hl_ctx_free(struct hl_device *hdev, struct hl_ctx *ctx);
int hl_ctx_init(struct hl_device *hdev, struct hl_ctx *ctx, bool is_kernel_ctx);
void hl_ctx_do_release(struct kref *ref);
void hl_ctx_get(struct hl_ctx *ctx);
int hl_ctx_put(struct hl_ctx *ctx);
struct hl_ctx *hl_get_compute_ctx(struct hl_device *hdev);
struct hl_fence *hl_ctx_get_fence(struct hl_ctx *ctx, u64 seq);
int hl_ctx_get_fences(struct hl_ctx *ctx, u64 *seq_arr,
				struct hl_fence **fence, u32 arr_len);
void hl_ctx_mgr_init(struct hl_ctx_mgr *mgr);
void hl_ctx_mgr_fini(struct hl_device *hdev, struct hl_ctx_mgr *mgr);

int hl_device_init(struct hl_device *hdev);
void hl_device_fini(struct hl_device *hdev);
int hl_device_suspend(struct hl_device *hdev);
int hl_device_resume(struct hl_device *hdev);
int hl_device_reset(struct hl_device *hdev, u32 flags);
int hl_device_cond_reset(struct hl_device *hdev, u32 flags, u64 event_mask);
void hl_hpriv_get(struct hl_fpriv *hpriv);
int hl_hpriv_put(struct hl_fpriv *hpriv);
int hl_device_utilization(struct hl_device *hdev, u32 *utilization);

int hl_build_hwmon_channel_info(struct hl_device *hdev,
		struct cpucp_sensor *sensors_arr);

void hl_notifier_event_send_all(struct hl_device *hdev, u64 event_mask);

int hl_sysfs_init(struct hl_device *hdev);
void hl_sysfs_fini(struct hl_device *hdev);

int hl_hwmon_init(struct hl_device *hdev);
void hl_hwmon_fini(struct hl_device *hdev);
void hl_hwmon_release_resources(struct hl_device *hdev);

int hl_cb_create(struct hl_device *hdev, struct hl_mem_mgr *mmg,
			struct hl_ctx *ctx, u32 cb_size, bool internal_cb,
			bool map_cb, u64 *handle);
int hl_cb_destroy(struct hl_mem_mgr *mmg, u64 cb_handle);
int hl_hw_block_mmap(struct hl_fpriv *hpriv, struct vm_area_struct *vma);
struct hl_cb *hl_cb_get(struct hl_mem_mgr *mmg, u64 handle);
void hl_cb_put(struct hl_cb *cb);
struct hl_cb *hl_cb_kernel_create(struct hl_device *hdev, u32 cb_size,
					bool internal_cb);
int hl_cb_pool_init(struct hl_device *hdev);
int hl_cb_pool_fini(struct hl_device *hdev);
int hl_cb_va_pool_init(struct hl_ctx *ctx);
void hl_cb_va_pool_fini(struct hl_ctx *ctx);

void hl_cs_rollback_all(struct hl_device *hdev, bool skip_wq_flush);
struct hl_cs_job *hl_cs_allocate_job(struct hl_device *hdev,
		enum hl_queue_type queue_type, bool is_kernel_allocated_cb);
void hl_sob_reset_error(struct kref *ref);
int hl_gen_sob_mask(u16 sob_base, u8 sob_mask, u8 *mask);
void hl_fence_put(struct hl_fence *fence);
void hl_fences_put(struct hl_fence **fence, int len);
void hl_fence_get(struct hl_fence *fence);
void cs_get(struct hl_cs *cs);
bool cs_needs_completion(struct hl_cs *cs);
bool cs_needs_timeout(struct hl_cs *cs);
bool is_staged_cs_last_exists(struct hl_device *hdev, struct hl_cs *cs);
struct hl_cs *hl_staged_cs_find_first(struct hl_device *hdev, u64 cs_seq);
void hl_multi_cs_completion_init(struct hl_device *hdev);
u32 hl_get_active_cs_num(struct hl_device *hdev);

void goya_set_asic_funcs(struct hl_device *hdev);
void gaudi_set_asic_funcs(struct hl_device *hdev);
void gaudi2_set_asic_funcs(struct hl_device *hdev);

int hl_vm_ctx_init(struct hl_ctx *ctx);
void hl_vm_ctx_fini(struct hl_ctx *ctx);

int hl_vm_init(struct hl_device *hdev);
void hl_vm_fini(struct hl_device *hdev);

void hl_hw_block_mem_init(struct hl_ctx *ctx);
void hl_hw_block_mem_fini(struct hl_ctx *ctx);

u64 hl_reserve_va_block(struct hl_device *hdev, struct hl_ctx *ctx,
		enum hl_va_range_type type, u64 size, u32 alignment);
int hl_unreserve_va_block(struct hl_device *hdev, struct hl_ctx *ctx,
		u64 start_addr, u64 size);
int hl_pin_host_memory(struct hl_device *hdev, u64 addr, u64 size,
			struct hl_userptr *userptr);
void hl_unpin_host_memory(struct hl_device *hdev, struct hl_userptr *userptr);
void hl_userptr_delete_list(struct hl_device *hdev,
				struct list_head *userptr_list);
bool hl_userptr_is_pinned(struct hl_device *hdev, u64 addr, u32 size,
				struct list_head *userptr_list,
				struct hl_userptr **userptr);

int hl_mmu_init(struct hl_device *hdev);
void hl_mmu_fini(struct hl_device *hdev);
int hl_mmu_ctx_init(struct hl_ctx *ctx);
void hl_mmu_ctx_fini(struct hl_ctx *ctx);
int hl_mmu_map_page(struct hl_ctx *ctx, u64 virt_addr, u64 phys_addr,
		u32 page_size, bool flush_pte);
int hl_mmu_get_real_page_size(struct hl_device *hdev, struct hl_mmu_properties *mmu_prop,
				u32 page_size, u32 *real_page_size, bool is_dram_addr);
int hl_mmu_unmap_page(struct hl_ctx *ctx, u64 virt_addr, u32 page_size,
		bool flush_pte);
int hl_mmu_map_contiguous(struct hl_ctx *ctx, u64 virt_addr,
					u64 phys_addr, u32 size);
int hl_mmu_unmap_contiguous(struct hl_ctx *ctx, u64 virt_addr, u32 size);
int hl_mmu_invalidate_cache(struct hl_device *hdev, bool is_hard, u32 flags);
int hl_mmu_invalidate_cache_range(struct hl_device *hdev, bool is_hard,
					u32 flags, u32 asid, u64 va, u64 size);
int hl_mmu_prefetch_cache_range(struct hl_ctx *ctx, u32 flags, u32 asid, u64 va, u64 size);
u64 hl_mmu_get_next_hop_addr(struct hl_ctx *ctx, u64 curr_pte);
u64 hl_mmu_get_hop_pte_phys_addr(struct hl_ctx *ctx, struct hl_mmu_properties *mmu_prop,
					u8 hop_idx, u64 hop_addr, u64 virt_addr);
void hl_mmu_hr_flush(struct hl_ctx *ctx);
int hl_mmu_hr_init(struct hl_device *hdev, struct hl_mmu_hr_priv *hr_priv, u32 hop_table_size,
			u64 pgt_size);
void hl_mmu_hr_fini(struct hl_device *hdev, struct hl_mmu_hr_priv *hr_priv, u32 hop_table_size);
void hl_mmu_hr_free_hop_remove_pgt(struct pgt_info *pgt_info, struct hl_mmu_hr_priv *hr_priv,
				u32 hop_table_size);
u64 hl_mmu_hr_pte_phys_to_virt(struct hl_ctx *ctx, struct pgt_info *pgt, u64 phys_pte_addr,
							u32 hop_table_size);
void hl_mmu_hr_write_pte(struct hl_ctx *ctx, struct pgt_info *pgt_info, u64 phys_pte_addr,
							u64 val, u32 hop_table_size);
void hl_mmu_hr_clear_pte(struct hl_ctx *ctx, struct pgt_info *pgt_info, u64 phys_pte_addr,
							u32 hop_table_size);
int hl_mmu_hr_put_pte(struct hl_ctx *ctx, struct pgt_info *pgt_info, struct hl_mmu_hr_priv *hr_priv,
							u32 hop_table_size);
void hl_mmu_hr_get_pte(struct hl_ctx *ctx, struct hl_hr_mmu_funcs *hr_func, u64 phys_hop_addr);
struct pgt_info *hl_mmu_hr_get_next_hop_pgt_info(struct hl_ctx *ctx,
							struct hl_hr_mmu_funcs *hr_func,
							u64 curr_pte);
struct pgt_info *hl_mmu_hr_alloc_hop(struct hl_ctx *ctx, struct hl_mmu_hr_priv *hr_priv,
							struct hl_hr_mmu_funcs *hr_func,
							struct hl_mmu_properties *mmu_prop);
struct pgt_info *hl_mmu_hr_get_alloc_next_hop(struct hl_ctx *ctx,
							struct hl_mmu_hr_priv *hr_priv,
							struct hl_hr_mmu_funcs *hr_func,
							struct hl_mmu_properties *mmu_prop,
							u64 curr_pte, bool *is_new_hop);
int hl_mmu_hr_get_tlb_info(struct hl_ctx *ctx, u64 virt_addr, struct hl_mmu_hop_info *hops,
							struct hl_hr_mmu_funcs *hr_func);
int hl_mmu_if_set_funcs(struct hl_device *hdev);
void hl_mmu_v1_set_funcs(struct hl_device *hdev, struct hl_mmu_funcs *mmu);
void hl_mmu_v2_hr_set_funcs(struct hl_device *hdev, struct hl_mmu_funcs *mmu);
int hl_mmu_va_to_pa(struct hl_ctx *ctx, u64 virt_addr, u64 *phys_addr);
int hl_mmu_get_tlb_info(struct hl_ctx *ctx, u64 virt_addr,
			struct hl_mmu_hop_info *hops);
u64 hl_mmu_scramble_addr(struct hl_device *hdev, u64 addr);
u64 hl_mmu_descramble_addr(struct hl_device *hdev, u64 addr);
bool hl_is_dram_va(struct hl_device *hdev, u64 virt_addr);

int hl_fw_load_fw_to_device(struct hl_device *hdev, const char *fw_name,
				void __iomem *dst, u32 src_offset, u32 size);
int hl_fw_send_pci_access_msg(struct hl_device *hdev, u32 opcode, u64 value);
int hl_fw_send_cpu_message(struct hl_device *hdev, u32 hw_queue_id, u32 *msg,
				u16 len, u32 timeout, u64 *result);
int hl_fw_unmask_irq(struct hl_device *hdev, u16 event_type);
int hl_fw_unmask_irq_arr(struct hl_device *hdev, const u32 *irq_arr,
		size_t irq_arr_size);
int hl_fw_test_cpu_queue(struct hl_device *hdev);
void *hl_fw_cpu_accessible_dma_pool_alloc(struct hl_device *hdev, size_t size,
						dma_addr_t *dma_handle);
void hl_fw_cpu_accessible_dma_pool_free(struct hl_device *hdev, size_t size,
					void *vaddr);
int hl_fw_send_heartbeat(struct hl_device *hdev);
int hl_fw_cpucp_info_get(struct hl_device *hdev,
				u32 sts_boot_dev_sts0_reg,
				u32 sts_boot_dev_sts1_reg, u32 boot_err0_reg,
				u32 boot_err1_reg);
int hl_fw_cpucp_handshake(struct hl_device *hdev,
				u32 sts_boot_dev_sts0_reg,
				u32 sts_boot_dev_sts1_reg, u32 boot_err0_reg,
				u32 boot_err1_reg);
int hl_fw_get_eeprom_data(struct hl_device *hdev, void *data, size_t max_size);
int hl_fw_get_monitor_dump(struct hl_device *hdev, void *data);
int hl_fw_cpucp_pci_counters_get(struct hl_device *hdev,
		struct hl_info_pci_counters *counters);
int hl_fw_cpucp_total_energy_get(struct hl_device *hdev,
			u64 *total_energy);
int get_used_pll_index(struct hl_device *hdev, u32 input_pll_index,
						enum pll_index *pll_index);
int hl_fw_cpucp_pll_info_get(struct hl_device *hdev, u32 pll_index,
		u16 *pll_freq_arr);
int hl_fw_cpucp_power_get(struct hl_device *hdev, u64 *power);
void hl_fw_ask_hard_reset_without_linux(struct hl_device *hdev);
void hl_fw_ask_halt_machine_without_linux(struct hl_device *hdev);
int hl_fw_init_cpu(struct hl_device *hdev);
int hl_fw_wait_preboot_ready(struct hl_device *hdev);
int hl_fw_read_preboot_status(struct hl_device *hdev);
int hl_fw_dynamic_send_protocol_cmd(struct hl_device *hdev,
				struct fw_load_mgr *fw_loader,
				enum comms_cmd cmd, unsigned int size,
				bool wait_ok, u32 timeout);
int hl_fw_dram_replaced_row_get(struct hl_device *hdev,
				struct cpucp_hbm_row_info *info);
int hl_fw_dram_pending_row_get(struct hl_device *hdev, u32 *pend_rows_num);
int hl_fw_cpucp_engine_core_asid_set(struct hl_device *hdev, u32 asid);
int hl_fw_send_device_activity(struct hl_device *hdev, bool open);
int hl_fw_send_soft_reset(struct hl_device *hdev);
int hl_pci_bars_map(struct hl_device *hdev, const char * const name[3],
			bool is_wc[3]);
int hl_pci_elbi_read(struct hl_device *hdev, u64 addr, u32 *data);
int hl_pci_iatu_write(struct hl_device *hdev, u32 addr, u32 data);
int hl_pci_set_inbound_region(struct hl_device *hdev, u8 region,
		struct hl_inbound_pci_region *pci_region);
int hl_pci_set_outbound_region(struct hl_device *hdev,
		struct hl_outbound_pci_region *pci_region);
enum pci_region hl_get_pci_memory_region(struct hl_device *hdev, u64 addr);
int hl_pci_init(struct hl_device *hdev);
void hl_pci_fini(struct hl_device *hdev);

long hl_fw_get_frequency(struct hl_device *hdev, u32 pll_index, bool curr);
void hl_fw_set_frequency(struct hl_device *hdev, u32 pll_index, u64 freq);
int hl_get_temperature(struct hl_device *hdev, int sensor_index, u32 attr, long *value);
int hl_set_temperature(struct hl_device *hdev, int sensor_index, u32 attr, long value);
int hl_get_voltage(struct hl_device *hdev, int sensor_index, u32 attr, long *value);
int hl_get_current(struct hl_device *hdev, int sensor_index, u32 attr, long *value);
int hl_get_fan_speed(struct hl_device *hdev, int sensor_index, u32 attr, long *value);
int hl_get_pwm_info(struct hl_device *hdev, int sensor_index, u32 attr, long *value);
void hl_set_pwm_info(struct hl_device *hdev, int sensor_index, u32 attr, long value);
long hl_fw_get_max_power(struct hl_device *hdev);
void hl_fw_set_max_power(struct hl_device *hdev);
int hl_fw_get_sec_attest_info(struct hl_device *hdev, struct cpucp_sec_attest_info *sec_attest_info,
				u32 nonce);
int hl_set_voltage(struct hl_device *hdev, int sensor_index, u32 attr, long value);
int hl_set_current(struct hl_device *hdev, int sensor_index, u32 attr, long value);
int hl_set_power(struct hl_device *hdev, int sensor_index, u32 attr, long value);
int hl_get_power(struct hl_device *hdev, int sensor_index, u32 attr, long *value);
int hl_fw_get_clk_rate(struct hl_device *hdev, u32 *cur_clk, u32 *max_clk);
void hl_fw_set_pll_profile(struct hl_device *hdev);
void hl_sysfs_add_dev_clk_attr(struct hl_device *hdev, struct attribute_group *dev_clk_attr_grp);
void hl_sysfs_add_dev_vrm_attr(struct hl_device *hdev, struct attribute_group *dev_vrm_attr_grp);
int hl_fw_send_generic_request(struct hl_device *hdev, enum hl_passthrough_type sub_opcode,
						dma_addr_t buff, u32 *size);

void hw_sob_get(struct hl_hw_sob *hw_sob);
void hw_sob_put(struct hl_hw_sob *hw_sob);
void hl_encaps_release_handle_and_put_ctx(struct kref *ref);
void hl_encaps_release_handle_and_put_sob_ctx(struct kref *ref);
void hl_hw_queue_encaps_sig_set_sob_info(struct hl_device *hdev,
			struct hl_cs *cs, struct hl_cs_job *job,
			struct hl_cs_compl *cs_cmpl);

int hl_dec_init(struct hl_device *hdev);
void hl_dec_fini(struct hl_device *hdev);
void hl_dec_ctx_fini(struct hl_ctx *ctx);

void hl_release_pending_user_interrupts(struct hl_device *hdev);
void hl_abort_waiting_for_cs_completions(struct hl_device *hdev);
int hl_cs_signal_sob_wraparound_handler(struct hl_device *hdev, u32 q_idx,
			struct hl_hw_sob **hw_sob, u32 count, bool encaps_sig);

int hl_state_dump(struct hl_device *hdev);
const char *hl_state_dump_get_sync_name(struct hl_device *hdev, u32 sync_id);
const char *hl_state_dump_get_monitor_name(struct hl_device *hdev,
					struct hl_mon_state_dump *mon);
void hl_state_dump_free_sync_to_engine_map(struct hl_sync_to_engine_map *map);
__printf(4, 5) int hl_snprintf_resize(char **buf, size_t *size, size_t *offset,
					const char *format, ...);
char *hl_format_as_binary(char *buf, size_t buf_len, u32 n);
const char *hl_sync_engine_to_string(enum hl_sync_engine_type engine_type);

void hl_mem_mgr_init(struct device *dev, struct hl_mem_mgr *mmg);
void hl_mem_mgr_fini(struct hl_mem_mgr *mmg);
void hl_mem_mgr_idr_destroy(struct hl_mem_mgr *mmg);
int hl_mem_mgr_mmap(struct hl_mem_mgr *mmg, struct vm_area_struct *vma,
		    void *args);
struct hl_mmap_mem_buf *hl_mmap_mem_buf_get(struct hl_mem_mgr *mmg,
						   u64 handle);
int hl_mmap_mem_buf_put_handle(struct hl_mem_mgr *mmg, u64 handle);
int hl_mmap_mem_buf_put(struct hl_mmap_mem_buf *buf);
struct hl_mmap_mem_buf *
hl_mmap_mem_buf_alloc(struct hl_mem_mgr *mmg,
		      struct hl_mmap_mem_buf_behavior *behavior, gfp_t gfp,
		      void *args);
__printf(2, 3) void hl_engine_data_sprintf(struct engines_data *e, const char *fmt, ...);
void hl_capture_razwi(struct hl_device *hdev, u64 addr, u16 *engine_id, u16 num_of_engines,
			u8 flags);
void hl_handle_razwi(struct hl_device *hdev, u64 addr, u16 *engine_id, u16 num_of_engines,
			u8 flags, u64 *event_mask);
void hl_capture_page_fault(struct hl_device *hdev, u64 addr, u16 eng_id, bool is_pmmu);
void hl_handle_page_fault(struct hl_device *hdev, u64 addr, u16 eng_id, bool is_pmmu,
				u64 *event_mask);
void hl_handle_critical_hw_err(struct hl_device *hdev, u16 event_id, u64 *event_mask);
void hl_handle_fw_err(struct hl_device *hdev, struct hl_info_fw_err_info *info);
void hl_enable_err_info_capture(struct hl_error_info *captured_err_info);

#ifdef CONFIG_DEBUG_FS

void hl_debugfs_init(void);
void hl_debugfs_fini(void);
int hl_debugfs_device_init(struct hl_device *hdev);
void hl_debugfs_device_fini(struct hl_device *hdev);
void hl_debugfs_add_device(struct hl_device *hdev);
void hl_debugfs_remove_device(struct hl_device *hdev);
void hl_debugfs_add_file(struct hl_fpriv *hpriv);
void hl_debugfs_remove_file(struct hl_fpriv *hpriv);
void hl_debugfs_add_cb(struct hl_cb *cb);
void hl_debugfs_remove_cb(struct hl_cb *cb);
void hl_debugfs_add_cs(struct hl_cs *cs);
void hl_debugfs_remove_cs(struct hl_cs *cs);
void hl_debugfs_add_job(struct hl_device *hdev, struct hl_cs_job *job);
void hl_debugfs_remove_job(struct hl_device *hdev, struct hl_cs_job *job);
void hl_debugfs_add_userptr(struct hl_device *hdev, struct hl_userptr *userptr);
void hl_debugfs_remove_userptr(struct hl_device *hdev,
				struct hl_userptr *userptr);
void hl_debugfs_add_ctx_mem_hash(struct hl_device *hdev, struct hl_ctx *ctx);
void hl_debugfs_remove_ctx_mem_hash(struct hl_device *hdev, struct hl_ctx *ctx);
void hl_debugfs_set_state_dump(struct hl_device *hdev, char *data,
					unsigned long length);

#else

static inline void __init hl_debugfs_init(void)
{
}

static inline void hl_debugfs_fini(void)
{
}

static inline void hl_debugfs_add_device(struct hl_device *hdev)
{
}

static inline void hl_debugfs_remove_device(struct hl_device *hdev)
{
}

static inline void hl_debugfs_add_file(struct hl_fpriv *hpriv)
{
}

static inline void hl_debugfs_remove_file(struct hl_fpriv *hpriv)
{
}

static inline void hl_debugfs_add_cb(struct hl_cb *cb)
{
}

static inline void hl_debugfs_remove_cb(struct hl_cb *cb)
{
}

static inline void hl_debugfs_add_cs(struct hl_cs *cs)
{
}

static inline void hl_debugfs_remove_cs(struct hl_cs *cs)
{
}

static inline void hl_debugfs_add_job(struct hl_device *hdev,
					struct hl_cs_job *job)
{
}

static inline void hl_debugfs_remove_job(struct hl_device *hdev,
					struct hl_cs_job *job)
{
}

static inline void hl_debugfs_add_userptr(struct hl_device *hdev,
					struct hl_userptr *userptr)
{
}

static inline void hl_debugfs_remove_userptr(struct hl_device *hdev,
					struct hl_userptr *userptr)
{
}

static inline void hl_debugfs_add_ctx_mem_hash(struct hl_device *hdev,
					struct hl_ctx *ctx)
{
}

static inline void hl_debugfs_remove_ctx_mem_hash(struct hl_device *hdev,
					struct hl_ctx *ctx)
{
}

static inline void hl_debugfs_set_state_dump(struct hl_device *hdev,
					char *data, unsigned long length)
{
}

#endif

/* Security */
int hl_unsecure_register(struct hl_device *hdev, u32 mm_reg_addr, int offset,
		const u32 pb_blocks[], struct hl_block_glbl_sec sgs_array[],
		int array_size);
int hl_unsecure_registers(struct hl_device *hdev, const u32 mm_reg_array[],
		int mm_array_size, int offset, const u32 pb_blocks[],
		struct hl_block_glbl_sec sgs_array[], int blocks_array_size);
void hl_config_glbl_sec(struct hl_device *hdev, const u32 pb_blocks[],
		struct hl_block_glbl_sec sgs_array[], u32 block_offset,
		int array_size);
void hl_secure_block(struct hl_device *hdev,
		struct hl_block_glbl_sec sgs_array[], int array_size);
int hl_init_pb_with_mask(struct hl_device *hdev, u32 num_dcores,
		u32 dcore_offset, u32 num_instances, u32 instance_offset,
		const u32 pb_blocks[], u32 blocks_array_size,
		const u32 *regs_array, u32 regs_array_size, u64 mask);
int hl_init_pb(struct hl_device *hdev, u32 num_dcores, u32 dcore_offset,
		u32 num_instances, u32 instance_offset,
		const u32 pb_blocks[], u32 blocks_array_size,
		const u32 *regs_array, u32 regs_array_size);
int hl_init_pb_ranges_with_mask(struct hl_device *hdev, u32 num_dcores,
		u32 dcore_offset, u32 num_instances, u32 instance_offset,
		const u32 pb_blocks[], u32 blocks_array_size,
		const struct range *regs_range_array, u32 regs_range_array_size,
		u64 mask);
int hl_init_pb_ranges(struct hl_device *hdev, u32 num_dcores,
		u32 dcore_offset, u32 num_instances, u32 instance_offset,
		const u32 pb_blocks[], u32 blocks_array_size,
		const struct range *regs_range_array,
		u32 regs_range_array_size);
int hl_init_pb_single_dcore(struct hl_device *hdev, u32 dcore_offset,
		u32 num_instances, u32 instance_offset,
		const u32 pb_blocks[], u32 blocks_array_size,
		const u32 *regs_array, u32 regs_array_size);
int hl_init_pb_ranges_single_dcore(struct hl_device *hdev, u32 dcore_offset,
		u32 num_instances, u32 instance_offset,
		const u32 pb_blocks[], u32 blocks_array_size,
		const struct range *regs_range_array,
		u32 regs_range_array_size);
void hl_ack_pb(struct hl_device *hdev, u32 num_dcores, u32 dcore_offset,
		u32 num_instances, u32 instance_offset,
		const u32 pb_blocks[], u32 blocks_array_size);
void hl_ack_pb_with_mask(struct hl_device *hdev, u32 num_dcores,
		u32 dcore_offset, u32 num_instances, u32 instance_offset,
		const u32 pb_blocks[], u32 blocks_array_size, u64 mask);
void hl_ack_pb_single_dcore(struct hl_device *hdev, u32 dcore_offset,
		u32 num_instances, u32 instance_offset,
		const u32 pb_blocks[], u32 blocks_array_size);

/* IOCTLs */
long hl_ioctl(struct file *filep, unsigned int cmd, unsigned long arg);
long hl_ioctl_control(struct file *filep, unsigned int cmd, unsigned long arg);
int hl_cb_ioctl(struct hl_fpriv *hpriv, void *data);
int hl_cs_ioctl(struct hl_fpriv *hpriv, void *data);
int hl_wait_ioctl(struct hl_fpriv *hpriv, void *data);
int hl_mem_ioctl(struct hl_fpriv *hpriv, void *data);

#endif /* HABANALABSP_H_ */

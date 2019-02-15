/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef HABANALABSP_H_
#define HABANALABSP_H_

#include "include/armcp_if.h"
#include "include/qman_if.h"

#define pr_fmt(fmt)			"habanalabs: " fmt

#include <linux/cdev.h>
#include <linux/iopoll.h>

#define HL_NAME				"habanalabs"

#define HL_MMAP_CB_MASK			(0x8000000000000000ull >> PAGE_SHIFT)

#define HL_DEVICE_TIMEOUT_USEC		1000000 /* 1 s */

#define HL_MAX_QUEUES			128

struct hl_device;
struct hl_fpriv;

/**
 * enum hl_queue_type - Supported QUEUE types.
 * @QUEUE_TYPE_NA: queue is not available.
 * @QUEUE_TYPE_EXT: external queue which is a DMA channel that may access the
 *                  host.
 * @QUEUE_TYPE_INT: internal queue that performs DMA inside the device's
 *			memories and/or operates the compute engines.
 * @QUEUE_TYPE_CPU: S/W queue for communication with the device's CPU.
 */
enum hl_queue_type {
	QUEUE_TYPE_NA,
	QUEUE_TYPE_EXT,
	QUEUE_TYPE_INT,
	QUEUE_TYPE_CPU
};

/**
 * struct hw_queue_properties - queue information.
 * @type: queue type.
 * @kmd_only: true if only KMD is allowed to send a job to this queue, false
 *            otherwise.
 */
struct hw_queue_properties {
	enum hl_queue_type	type;
	u8			kmd_only;
};

/**
 * struct asic_fixed_properties - ASIC specific immutable properties.
 * @hw_queues_props: H/W queues properties.
 * @uboot_ver: F/W U-boot version.
 * @preboot_ver: F/W Preboot version.
 * @sram_base_address: SRAM physical start address.
 * @sram_end_address: SRAM physical end address.
 * @sram_user_base_address - SRAM physical start address for user access.
 * @dram_base_address: DRAM physical start address.
 * @dram_end_address: DRAM physical end address.
 * @dram_user_base_address: DRAM physical start address for user access.
 * @dram_size: DRAM total size.
 * @dram_pci_bar_size: size of PCI bar towards DRAM.
 * @host_phys_base_address: base physical address of host memory for
 *				transactions that the device generates.
 * @va_space_host_start_address: base address of virtual memory range for
 *                               mapping host memory.
 * @va_space_host_end_address: end address of virtual memory range for
 *                             mapping host memory.
 * @va_space_dram_start_address: base address of virtual memory range for
 *                               mapping DRAM memory.
 * @va_space_dram_end_address: end address of virtual memory range for
 *                             mapping DRAM memory.
 * @cfg_size: configuration space size on SRAM.
 * @sram_size: total size of SRAM.
 * @max_asid: maximum number of open contexts (ASIDs).
 * @completion_queues_count: number of completion queues.
 * @high_pll: high PLL frequency used by the device.
 * @cb_pool_cb_cnt: number of CBs in the CB pool.
 * @cb_pool_cb_size: size of each CB in the CB pool.
 * @tpc_enabled_mask: which TPCs are enabled.
 */
struct asic_fixed_properties {
	struct hw_queue_properties	hw_queues_props[HL_MAX_QUEUES];
	char			uboot_ver[VERSION_MAX_LEN];
	char			preboot_ver[VERSION_MAX_LEN];
	u64			sram_base_address;
	u64			sram_end_address;
	u64			sram_user_base_address;
	u64			dram_base_address;
	u64			dram_end_address;
	u64			dram_user_base_address;
	u64			dram_size;
	u64			dram_pci_bar_size;
	u64			host_phys_base_address;
	u64			va_space_host_start_address;
	u64			va_space_host_end_address;
	u64			va_space_dram_start_address;
	u64			va_space_dram_end_address;
	u32			cfg_size;
	u32			sram_size;
	u32			max_asid;
	u32			high_pll;
	u32			cb_pool_cb_cnt;
	u32			cb_pool_cb_size;
	u8			completion_queues_count;
	u8			tpc_enabled_mask;
};


/*
 * Command Buffers
 */

#define HL_MAX_CB_SIZE		0x200000	/* 2MB */

/**
 * struct hl_cb_mgr - describes a Command Buffer Manager.
 * @cb_lock: protects cb_handles.
 * @cb_handles: an idr to hold all command buffer handles.
 */
struct hl_cb_mgr {
	spinlock_t		cb_lock;
	struct idr		cb_handles; /* protected by cb_lock */
};

/**
 * struct hl_cb - describes a Command Buffer.
 * @refcount: reference counter for usage of the CB.
 * @hdev: pointer to device this CB belongs to.
 * @lock: spinlock to protect mmap/cs flows.
 * @pool_list: node in pool list of command buffers.
 * @kernel_address: Holds the CB's kernel virtual address.
 * @bus_address: Holds the CB's DMA address.
 * @mmap_size: Holds the CB's size that was mmaped.
 * @size: holds the CB's size.
 * @id: the CB's ID.
 * @ctx_id: holds the ID of the owner's context.
 * @mmap: true if the CB is currently mmaped to user.
 * @is_pool: true if CB was acquired from the pool, false otherwise.
 */
struct hl_cb {
	struct kref		refcount;
	struct hl_device	*hdev;
	spinlock_t		lock;
	struct list_head	pool_list;
	u64			kernel_address;
	dma_addr_t		bus_address;
	u32			mmap_size;
	u32			size;
	u32			id;
	u32			ctx_id;
	u8			mmap;
	u8			is_pool;
};


/*
 * QUEUES
 */

struct hl_cs_job;

/*
 * Currently, there are two limitations on the maximum length of a queue:
 *
 * 1. The memory footprint of the queue. The current allocated space for the
 *    queue is PAGE_SIZE. Because each entry in the queue is HL_BD_SIZE,
 *    the maximum length of the queue can be PAGE_SIZE / HL_BD_SIZE,
 *    which currently is 4096/16 = 256 entries.
 *
 *    To increase that, we need either to decrease the size of the
 *    BD (difficult), or allocate more than a single page (easier).
 *
 * 2. Because the size of the JOB handle field in the BD CTL / completion queue
 *    is 10-bit, we can have up to 1024 open jobs per hardware queue.
 *    Therefore, each queue can hold up to 1024 entries.
 *
 * HL_QUEUE_LENGTH is in units of struct hl_bd.
 * HL_QUEUE_LENGTH * sizeof(struct hl_bd) should be <= HL_PAGE_SIZE
 */

#define HL_PAGE_SIZE			4096 /* minimum page size */
/* Must be power of 2 (HL_PAGE_SIZE / HL_BD_SIZE) */
#define HL_QUEUE_LENGTH			256
#define HL_QUEUE_SIZE_IN_BYTES		(HL_QUEUE_LENGTH * HL_BD_SIZE)

/*
 * HL_CQ_LENGTH is in units of struct hl_cq_entry.
 * HL_CQ_LENGTH should be <= HL_PAGE_SIZE
 */
#define HL_CQ_LENGTH			HL_QUEUE_LENGTH
#define HL_CQ_SIZE_IN_BYTES		(HL_CQ_LENGTH * HL_CQ_ENTRY_SIZE)



/**
 * struct hl_hw_queue - describes a H/W transport queue.
 * @shadow_queue: pointer to a shadow queue that holds pointers to jobs.
 * @queue_type: type of queue.
 * @kernel_address: holds the queue's kernel virtual address.
 * @bus_address: holds the queue's DMA address.
 * @pi: holds the queue's pi value.
 * @ci: holds the queue's ci value, AS CALCULATED BY THE DRIVER (not real ci).
 * @hw_queue_id: the id of the H/W queue.
 * @int_queue_len: length of internal queue (number of entries).
 * @valid: is the queue valid (we have array of 32 queues, not all of them
 *		exists).
 */
struct hl_hw_queue {
	struct hl_cs_job	**shadow_queue;
	enum hl_queue_type	queue_type;
	u64			kernel_address;
	dma_addr_t		bus_address;
	u32			pi;
	u32			ci;
	u32			hw_queue_id;
	u16			int_queue_len;
	u8			valid;
};

/**
 * struct hl_cq - describes a completion queue
 * @hdev: pointer to the device structure
 * @kernel_address: holds the queue's kernel virtual address
 * @bus_address: holds the queue's DMA address
 * @hw_queue_id: the id of the matching H/W queue
 * @ci: ci inside the queue
 * @pi: pi inside the queue
 * @free_slots_cnt: counter of free slots in queue
 */
struct hl_cq {
	struct hl_device	*hdev;
	u64			kernel_address;
	dma_addr_t		bus_address;
	u32			hw_queue_id;
	u32			ci;
	u32			pi;
	atomic_t		free_slots_cnt;
};


/*
 * ASICs
 */

/**
 * enum hl_asic_type - supported ASIC types.
 * @ASIC_AUTO_DETECT: ASIC type will be automatically set.
 * @ASIC_GOYA: Goya device.
 * @ASIC_INVALID: Invalid ASIC type.
 */
enum hl_asic_type {
	ASIC_AUTO_DETECT,
	ASIC_GOYA,
	ASIC_INVALID
};

/**
 * struct hl_asic_funcs - ASIC specific functions that are can be called from
 *                        common code.
 * @early_init: sets up early driver state (pre sw_init), doesn't configure H/W.
 * @early_fini: tears down what was done in early_init.
 * @sw_init: sets up driver state, does not configure H/W.
 * @sw_fini: tears down driver state, does not configure H/W.
 * @hw_init: sets up the H/W state.
 * @hw_fini: tears down the H/W state.
 * @suspend: handles IP specific H/W or SW changes for suspend.
 * @resume: handles IP specific H/W or SW changes for resume.
 * @mmap: mmap function, does nothing.
 * @cb_mmap: maps a CB.
 * @ring_doorbell: increment PI on a given QMAN.
 * @flush_pq_write: flush PQ entry write if necessary, WARN if flushing failed.
 * @dma_alloc_coherent: Allocate coherent DMA memory by calling
 *                      dma_alloc_coherent(). This is ASIC function because its
 *                      implementation is not trivial when the driver is loaded
 *                      in simulation mode (not upstreamed).
 * @dma_free_coherent: Free coherent DMA memory by calling dma_free_coherent().
 *                     This is ASIC function because its implementation is not
 *                     trivial when the driver is loaded in simulation mode
 *                     (not upstreamed).
 * @get_int_queue_base: get the internal queue base address.
 * @test_queues: run simple test on all queues for sanity check.
 * @dma_pool_zalloc: small DMA allocation of coherent memory from DMA pool.
 *                   size of allocation is HL_DMA_POOL_BLK_SIZE.
 * @dma_pool_free: free small DMA allocation from pool.
 * @cpu_accessible_dma_pool_alloc: allocate CPU PQ packet from DMA pool.
 * @cpu_accessible_dma_pool_free: free CPU PQ packet from DMA pool.
 * @hw_queues_lock: acquire H/W queues lock.
 * @hw_queues_unlock: release H/W queues lock.
 * @send_cpu_message: send buffer to ArmCP.
 */
struct hl_asic_funcs {
	int (*early_init)(struct hl_device *hdev);
	int (*early_fini)(struct hl_device *hdev);
	int (*sw_init)(struct hl_device *hdev);
	int (*sw_fini)(struct hl_device *hdev);
	int (*hw_init)(struct hl_device *hdev);
	void (*hw_fini)(struct hl_device *hdev, bool hard_reset);
	int (*suspend)(struct hl_device *hdev);
	int (*resume)(struct hl_device *hdev);
	int (*mmap)(struct hl_fpriv *hpriv, struct vm_area_struct *vma);
	int (*cb_mmap)(struct hl_device *hdev, struct vm_area_struct *vma,
			u64 kaddress, phys_addr_t paddress, u32 size);
	void (*ring_doorbell)(struct hl_device *hdev, u32 hw_queue_id, u32 pi);
	void (*flush_pq_write)(struct hl_device *hdev, u64 *pq, u64 exp_val);
	void* (*dma_alloc_coherent)(struct hl_device *hdev, size_t size,
					dma_addr_t *dma_handle, gfp_t flag);
	void (*dma_free_coherent)(struct hl_device *hdev, size_t size,
					void *cpu_addr, dma_addr_t dma_handle);
	void* (*get_int_queue_base)(struct hl_device *hdev, u32 queue_id,
				dma_addr_t *dma_handle, u16 *queue_len);
	int (*test_queues)(struct hl_device *hdev);
	void* (*dma_pool_zalloc)(struct hl_device *hdev, size_t size,
				gfp_t mem_flags, dma_addr_t *dma_handle);
	void (*dma_pool_free)(struct hl_device *hdev, void *vaddr,
				dma_addr_t dma_addr);
	void* (*cpu_accessible_dma_pool_alloc)(struct hl_device *hdev,
				size_t size, dma_addr_t *dma_handle);
	void (*cpu_accessible_dma_pool_free)(struct hl_device *hdev,
				size_t size, void *vaddr);
	void (*hw_queues_lock)(struct hl_device *hdev);
	void (*hw_queues_unlock)(struct hl_device *hdev);
	int (*send_cpu_message)(struct hl_device *hdev, u32 *msg,
				u16 len, u32 timeout, long *result);
};


/*
 * CONTEXTS
 */

#define HL_KERNEL_ASID_ID	0

/**
 * struct hl_ctx - user/kernel context.
 * @hpriv: pointer to the private (KMD) data of the process (fd).
 * @hdev: pointer to the device structure.
 * @refcount: reference counter for the context. Context is released only when
 *		this hits 0l. It is incremented on CS and CS_WAIT.
 * @asid: context's unique address space ID in the device's MMU.
 */
struct hl_ctx {
	struct hl_fpriv		*hpriv;
	struct hl_device	*hdev;
	struct kref		refcount;
	u32			asid;
};

/**
 * struct hl_ctx_mgr - for handling multiple contexts.
 * @ctx_lock: protects ctx_handles.
 * @ctx_handles: idr to hold all ctx handles.
 */
struct hl_ctx_mgr {
	struct mutex		ctx_lock;
	struct idr		ctx_handles;
};




/**
 * struct hl_cs_job - command submission job.
 * @finish_work: workqueue object to run when job is completed.
 * @id: the id of this job inside a CS.
 */
struct hl_cs_job {
	struct work_struct	finish_work;
	u32			id;
};
/*
 * FILE PRIVATE STRUCTURE
 */

/**
 * struct hl_fpriv - process information stored in FD private data.
 * @hdev: habanalabs device structure.
 * @filp: pointer to the given file structure.
 * @taskpid: current process ID.
 * @ctx: current executing context.
 * @ctx_mgr: context manager to handle multiple context for this FD.
 * @cb_mgr: command buffer manager to handle multiple buffers for this FD.
 * @refcount: number of related contexts.
 */
struct hl_fpriv {
	struct hl_device	*hdev;
	struct file		*filp;
	struct pid		*taskpid;
	struct hl_ctx		*ctx; /* TODO: remove for multiple ctx */
	struct hl_ctx_mgr	ctx_mgr;
	struct hl_cb_mgr	cb_mgr;
	struct kref		refcount;
};


/*
 * DEVICES
 */

/* Theoretical limit only. A single host can only contain up to 4 or 8 PCIe
 * x16 cards. In extereme cases, there are hosts that can accommodate 16 cards
 */
#define HL_MAX_MINORS	256

/*
 * Registers read & write functions.
 */

u32 hl_rreg(struct hl_device *hdev, u32 reg);
void hl_wreg(struct hl_device *hdev, u32 reg, u32 val);

#define hl_poll_timeout(hdev, addr, val, cond, sleep_us, timeout_us) \
	readl_poll_timeout(hdev->rmmio + addr, val, cond, sleep_us, timeout_us)

#define RREG32(reg) hl_rreg(hdev, (reg))
#define WREG32(reg, v) hl_wreg(hdev, (reg), (v))
#define DREG32(reg) pr_info("REGISTER: " #reg " : 0x%08X\n",	\
				hl_rreg(hdev, (reg)))

#define WREG32_P(reg, val, mask)				\
	do {							\
		u32 tmp_ = RREG32(reg);				\
		tmp_ &= (mask);					\
		tmp_ |= ((val) & ~(mask));			\
		WREG32(reg, tmp_);				\
	} while (0)
#define WREG32_AND(reg, and) WREG32_P(reg, 0, and)
#define WREG32_OR(reg, or) WREG32_P(reg, or, ~(or))

#define REG_FIELD_SHIFT(reg, field) reg##_##field##_SHIFT
#define REG_FIELD_MASK(reg, field) reg##_##field##_MASK
#define WREG32_FIELD(reg, field, val)	\
	WREG32(mm##reg, (RREG32(mm##reg) & ~REG_FIELD_MASK(reg, field)) | \
			(val) << REG_FIELD_SHIFT(reg, field))

/**
 * struct hl_device - habanalabs device structure.
 * @pdev: pointer to PCI device, can be NULL in case of simulator device.
 * @pcie_bar: array of available PCIe bars.
 * @rmmio: configuration area address on SRAM.
 * @cdev: related char device.
 * @dev: realted kernel basic device structure.
 * @asic_name: ASIC specific nmae.
 * @asic_type: ASIC specific type.
 * @completion_queue: array of hl_cq.
 * @cq_wq: work queue of completion queues for executing work in process context
 * @eq_wq: work queue of event queue for executing work in process context.
 * @kernel_ctx: KMD context structure.
 * @kernel_queues: array of hl_hw_queue.
 * @kernel_cb_mgr: command buffer manager for creating/destroying/handling CGs.
 * @dma_pool: DMA pool for small allocations.
 * @cpu_accessible_dma_mem: KMD <-> ArmCP shared memory CPU address.
 * @cpu_accessible_dma_address: KMD <-> ArmCP shared memory DMA address.
 * @cpu_accessible_dma_pool: KMD <-> ArmCP shared memory pool.
 * @asid_bitmap: holds used/available ASIDs.
 * @asid_mutex: protects asid_bitmap.
 * @fd_open_cnt_lock: lock for updating fd_open_cnt in hl_device_open. Although
 *                    fd_open_cnt is atomic, we need this lock to serialize
 *                    the open function because the driver currently supports
 *                    only a single process at a time. In addition, we need a
 *                    lock here so we can flush user processes which are opening
 *                    the device while we are trying to hard reset it
 * @send_cpu_message_lock: enforces only one message in KMD <-> ArmCP queue.
 * @asic_prop: ASIC specific immutable properties.
 * @asic_funcs: ASIC specific functions.
 * @asic_specific: ASIC specific information to use only from ASIC files.
 * @cb_pool: list of preallocated CBs.
 * @cb_pool_lock: protects the CB pool.
 * @user_ctx: current user context executing.
 * @fd_open_cnt: number of open user processes.
 * @major: habanalabs KMD major.
 * @id: device minor.
 * @disabled: is device disabled.
 */
struct hl_device {
	struct pci_dev			*pdev;
	void __iomem			*pcie_bar[6];
	void __iomem			*rmmio;
	struct cdev			cdev;
	struct device			*dev;
	char				asic_name[16];
	enum hl_asic_type		asic_type;
	struct hl_cq			*completion_queue;
	struct workqueue_struct		*cq_wq;
	struct hl_ctx			*kernel_ctx;
	struct hl_hw_queue		*kernel_queues;
	struct hl_cb_mgr		kernel_cb_mgr;
	struct dma_pool			*dma_pool;
	void				*cpu_accessible_dma_mem;
	dma_addr_t			cpu_accessible_dma_address;
	struct gen_pool			*cpu_accessible_dma_pool;
	unsigned long			*asid_bitmap;
	struct mutex			asid_mutex;
	/* TODO: remove fd_open_cnt_lock for multiple process support */
	struct mutex			fd_open_cnt_lock;
	struct mutex			send_cpu_message_lock;
	struct asic_fixed_properties	asic_prop;
	const struct hl_asic_funcs	*asic_funcs;
	void				*asic_specific;

	struct list_head		cb_pool;
	spinlock_t			cb_pool_lock;

	/* TODO: remove user_ctx for multiple process support */
	struct hl_ctx			*user_ctx;
	atomic_t			fd_open_cnt;
	u32				major;
	u16				id;
	u8				disabled;

	/* Parameters for bring-up */
	u8				cpu_enable;
	u8				reset_pcilink;
	u8				cpu_queues_enable;
	u8				fw_loading;
	u8				pldm;
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


/*
 * Kernel module functions that can be accessed by entire module
 */

int hl_device_open(struct inode *inode, struct file *filp);
int create_hdev(struct hl_device **dev, struct pci_dev *pdev,
		enum hl_asic_type asic_type, int minor);
void destroy_hdev(struct hl_device *hdev);
int hl_poll_timeout_memory(struct hl_device *hdev, u64 addr, u32 timeout_us,
				u32 *val);
int hl_poll_timeout_device_memory(struct hl_device *hdev, void __iomem *addr,
				u32 timeout_us, u32 *val);
int hl_hw_queues_create(struct hl_device *hdev);
void hl_hw_queues_destroy(struct hl_device *hdev);
int hl_hw_queue_send_cb_no_cmpl(struct hl_device *hdev, u32 hw_queue_id,
				u32 cb_size, u64 cb_ptr);
u32 hl_hw_queue_add_ptr(u32 ptr, u16 val);
void hl_hw_queue_inc_ci_kernel(struct hl_device *hdev, u32 hw_queue_id);

#define hl_queue_inc_ptr(p)		hl_hw_queue_add_ptr(p, 1)
#define hl_pi_2_offset(pi)		((pi) & (HL_QUEUE_LENGTH - 1))

int hl_cq_init(struct hl_device *hdev, struct hl_cq *q, u32 hw_queue_id);
void hl_cq_fini(struct hl_device *hdev, struct hl_cq *q);
int hl_asid_init(struct hl_device *hdev);
void hl_asid_fini(struct hl_device *hdev);
unsigned long hl_asid_alloc(struct hl_device *hdev);
void hl_asid_free(struct hl_device *hdev, unsigned long asid);

int hl_ctx_create(struct hl_device *hdev, struct hl_fpriv *hpriv);
void hl_ctx_free(struct hl_device *hdev, struct hl_ctx *ctx);
int hl_ctx_init(struct hl_device *hdev, struct hl_ctx *ctx, bool is_kernel_ctx);
int hl_ctx_put(struct hl_ctx *ctx);
void hl_ctx_mgr_init(struct hl_ctx_mgr *mgr);
void hl_ctx_mgr_fini(struct hl_device *hdev, struct hl_ctx_mgr *mgr);
int hl_device_init(struct hl_device *hdev, struct class *hclass);
void hl_device_fini(struct hl_device *hdev);
int hl_device_suspend(struct hl_device *hdev);
int hl_device_resume(struct hl_device *hdev);
void hl_hpriv_get(struct hl_fpriv *hpriv);
void hl_hpriv_put(struct hl_fpriv *hpriv);

int hl_cb_create(struct hl_device *hdev, struct hl_cb_mgr *mgr, u32 cb_size,
		u64 *handle, int ctx_id);
int hl_cb_destroy(struct hl_device *hdev, struct hl_cb_mgr *mgr, u64 cb_handle);
int hl_cb_mmap(struct hl_fpriv *hpriv, struct vm_area_struct *vma);
struct hl_cb *hl_cb_get(struct hl_device *hdev,	struct hl_cb_mgr *mgr,
			u32 handle);
void hl_cb_put(struct hl_cb *cb);
void hl_cb_mgr_init(struct hl_cb_mgr *mgr);
void hl_cb_mgr_fini(struct hl_device *hdev, struct hl_cb_mgr *mgr);
struct hl_cb *hl_cb_kernel_create(struct hl_device *hdev, u32 cb_size);
int hl_cb_pool_init(struct hl_device *hdev);
int hl_cb_pool_fini(struct hl_device *hdev);

void goya_set_asic_funcs(struct hl_device *hdev);

/* IOCTLs */
long hl_ioctl(struct file *filep, unsigned int cmd, unsigned long arg);
int hl_cb_ioctl(struct hl_fpriv *hpriv, void *data);

#endif /* HABANALABSP_H_ */

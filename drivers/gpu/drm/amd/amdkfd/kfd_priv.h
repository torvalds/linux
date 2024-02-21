/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright 2014-2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef KFD_PRIV_H_INCLUDED
#define KFD_PRIV_H_INCLUDED

#include <linux/hashtable.h>
#include <linux/mmu_notifier.h>
#include <linux/memremap.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/kfd_ioctl.h>
#include <linux/idr.h>
#include <linux/kfifo.h>
#include <linux/seq_file.h>
#include <linux/kref.h>
#include <linux/sysfs.h>
#include <linux/device_cgroup.h>
#include <drm/drm_file.h>
#include <drm/drm_drv.h>
#include <drm/drm_device.h>
#include <drm/drm_ioctl.h>
#include <kgd_kfd_interface.h>
#include <linux/swap.h>

#include "amd_shared.h"
#include "amdgpu.h"

#define KFD_MAX_RING_ENTRY_SIZE	8

#define KFD_SYSFS_FILE_MODE 0444

/* GPU ID hash width in bits */
#define KFD_GPU_ID_HASH_WIDTH 16

/* Use upper bits of mmap offset to store KFD driver specific information.
 * BITS[63:62] - Encode MMAP type
 * BITS[61:46] - Encode gpu_id. To identify to which GPU the offset belongs to
 * BITS[45:0]  - MMAP offset value
 *
 * NOTE: struct vm_area_struct.vm_pgoff uses offset in pages. Hence, these
 *  defines are w.r.t to PAGE_SIZE
 */
#define KFD_MMAP_TYPE_SHIFT	62
#define KFD_MMAP_TYPE_MASK	(0x3ULL << KFD_MMAP_TYPE_SHIFT)
#define KFD_MMAP_TYPE_DOORBELL	(0x3ULL << KFD_MMAP_TYPE_SHIFT)
#define KFD_MMAP_TYPE_EVENTS	(0x2ULL << KFD_MMAP_TYPE_SHIFT)
#define KFD_MMAP_TYPE_RESERVED_MEM	(0x1ULL << KFD_MMAP_TYPE_SHIFT)
#define KFD_MMAP_TYPE_MMIO	(0x0ULL << KFD_MMAP_TYPE_SHIFT)

#define KFD_MMAP_GPU_ID_SHIFT 46
#define KFD_MMAP_GPU_ID_MASK (((1ULL << KFD_GPU_ID_HASH_WIDTH) - 1) \
				<< KFD_MMAP_GPU_ID_SHIFT)
#define KFD_MMAP_GPU_ID(gpu_id) ((((uint64_t)gpu_id) << KFD_MMAP_GPU_ID_SHIFT)\
				& KFD_MMAP_GPU_ID_MASK)
#define KFD_MMAP_GET_GPU_ID(offset)    ((offset & KFD_MMAP_GPU_ID_MASK) \
				>> KFD_MMAP_GPU_ID_SHIFT)

/*
 * When working with cp scheduler we should assign the HIQ manually or via
 * the amdgpu driver to a fixed hqd slot, here are the fixed HIQ hqd slot
 * definitions for Kaveri. In Kaveri only the first ME queues participates
 * in the cp scheduling taking that in mind we set the HIQ slot in the
 * second ME.
 */
#define KFD_CIK_HIQ_PIPE 4
#define KFD_CIK_HIQ_QUEUE 0

/* Macro for allocating structures */
#define kfd_alloc_struct(ptr_to_struct)	\
	((typeof(ptr_to_struct)) kzalloc(sizeof(*ptr_to_struct), GFP_KERNEL))

#define KFD_MAX_NUM_OF_PROCESSES 512
#define KFD_MAX_NUM_OF_QUEUES_PER_PROCESS 1024

/*
 * Size of the per-process TBA+TMA buffer: 2 pages
 *
 * The first chunk is the TBA used for the CWSR ISA code. The second
 * chunk is used as TMA for user-mode trap handler setup in daisy-chain mode.
 */
#define KFD_CWSR_TBA_TMA_SIZE (PAGE_SIZE * 2)
#define KFD_CWSR_TMA_OFFSET (PAGE_SIZE + 2048)

#define KFD_MAX_NUM_OF_QUEUES_PER_DEVICE		\
	(KFD_MAX_NUM_OF_PROCESSES *			\
			KFD_MAX_NUM_OF_QUEUES_PER_PROCESS)

#define KFD_KERNEL_QUEUE_SIZE 2048

#define KFD_UNMAP_LATENCY_MS	(4000)

#define KFD_MAX_SDMA_QUEUES	128

/*
 * 512 = 0x200
 * The doorbell index distance between SDMA RLC (2*i) and (2*i+1) in the
 * same SDMA engine on SOC15, which has 8-byte doorbells for SDMA.
 * 512 8-byte doorbell distance (i.e. one page away) ensures that SDMA RLC
 * (2*i+1) doorbells (in terms of the lower 12 bit address) lie exactly in
 * the OFFSET and SIZE set in registers like BIF_SDMA0_DOORBELL_RANGE.
 */
#define KFD_QUEUE_DOORBELL_MIRROR_OFFSET 512

/**
 * enum kfd_ioctl_flags - KFD ioctl flags
 * Various flags that can be set in &amdkfd_ioctl_desc.flags to control how
 * userspace can use a given ioctl.
 */
enum kfd_ioctl_flags {
	/*
	 * @KFD_IOC_FLAG_CHECKPOINT_RESTORE:
	 * Certain KFD ioctls such as AMDKFD_IOC_CRIU_OP can potentially
	 * perform privileged operations and load arbitrary data into MQDs and
	 * eventually HQD registers when the queue is mapped by HWS. In order to
	 * prevent this we should perform additional security checks.
	 *
	 * This is equivalent to callers with the CHECKPOINT_RESTORE capability.
	 *
	 * Note: Since earlier versions of docker do not support CHECKPOINT_RESTORE,
	 * we also allow ioctls with SYS_ADMIN capability.
	 */
	KFD_IOC_FLAG_CHECKPOINT_RESTORE = BIT(0),
};
/*
 * Kernel module parameter to specify maximum number of supported queues per
 * device
 */
extern int max_num_of_queues_per_device;


/* Kernel module parameter to specify the scheduling policy */
extern int sched_policy;

/*
 * Kernel module parameter to specify the maximum process
 * number per HW scheduler
 */
extern int hws_max_conc_proc;

extern int cwsr_enable;

/*
 * Kernel module parameter to specify whether to send sigterm to HSA process on
 * unhandled exception
 */
extern int send_sigterm;

/*
 * This kernel module is used to simulate large bar machine on non-large bar
 * enabled machines.
 */
extern int debug_largebar;

/* Set sh_mem_config.retry_disable on GFX v9 */
extern int amdgpu_noretry;

/* Halt if HWS hang is detected */
extern int halt_if_hws_hang;

/* Whether MEC FW support GWS barriers */
extern bool hws_gws_support;

/* Queue preemption timeout in ms */
extern int queue_preemption_timeout_ms;

/*
 * Don't evict process queues on vm fault
 */
extern int amdgpu_no_queue_eviction_on_vm_fault;

/* Enable eviction debug messages */
extern bool debug_evictions;

extern struct mutex kfd_processes_mutex;

enum cache_policy {
	cache_policy_coherent,
	cache_policy_noncoherent
};

#define KFD_GC_VERSION(dev) (amdgpu_ip_version((dev)->adev, GC_HWIP, 0))
#define KFD_IS_SOC15(dev)   ((KFD_GC_VERSION(dev)) >= (IP_VERSION(9, 0, 1)))
#define KFD_SUPPORT_XNACK_PER_PROCESS(dev)\
	((KFD_GC_VERSION(dev) == IP_VERSION(9, 4, 2)) ||	\
	 (KFD_GC_VERSION(dev) == IP_VERSION(9, 4, 3)) ||	\
	 (KFD_GC_VERSION(dev) == IP_VERSION(9, 4, 4)) ||	\
	 (KFD_GC_VERSION(dev) == IP_VERSION(9, 5, 0)))

struct kfd_node;

struct kfd_event_interrupt_class {
	bool (*interrupt_isr)(struct kfd_node *dev,
			const uint32_t *ih_ring_entry, uint32_t *patched_ihre,
			bool *patched_flag);
	void (*interrupt_wq)(struct kfd_node *dev,
			const uint32_t *ih_ring_entry);
};

struct kfd_device_info {
	uint32_t gfx_target_version;
	const struct kfd_event_interrupt_class *event_interrupt_class;
	unsigned int max_pasid_bits;
	unsigned int max_no_of_hqd;
	unsigned int doorbell_size;
	size_t ih_ring_entry_size;
	uint8_t num_of_watch_points;
	uint16_t mqd_size_aligned;
	bool supports_cwsr;
	bool needs_pci_atomics;
	uint32_t no_atomic_fw_version;
	unsigned int num_sdma_queues_per_engine;
	unsigned int num_reserved_sdma_queues_per_engine;
	DECLARE_BITMAP(reserved_sdma_queues_bitmap, KFD_MAX_SDMA_QUEUES);
};

unsigned int kfd_get_num_sdma_engines(struct kfd_node *kdev);
unsigned int kfd_get_num_xgmi_sdma_engines(struct kfd_node *kdev);

struct kfd_mem_obj {
	uint32_t range_start;
	uint32_t range_end;
	uint64_t gpu_addr;
	uint32_t *cpu_ptr;
	void *gtt_mem;
};

struct kfd_vmid_info {
	uint32_t first_vmid_kfd;
	uint32_t last_vmid_kfd;
	uint32_t vmid_num_kfd;
};

#define MAX_KFD_NODES	8

struct kfd_dev;

struct kfd_node {
	unsigned int node_id;
	struct amdgpu_device *adev;     /* Duplicated here along with keeping
					 * a copy in kfd_dev to save a hop
					 */
	const struct kfd2kgd_calls *kfd2kgd; /* Duplicated here along with
					      * keeping a copy in kfd_dev to
					      * save a hop
					      */
	struct kfd_vmid_info vm_info;
	unsigned int id;                /* topology stub index */
	uint32_t xcc_mask; /* Instance mask of XCCs present */
	struct amdgpu_xcp *xcp;

	/* Interrupts */
	struct kfifo ih_fifo;
	struct workqueue_struct *ih_wq;
	struct work_struct interrupt_work;
	spinlock_t interrupt_lock;

	/*
	 * Interrupts of interest to KFD are copied
	 * from the HW ring into a SW ring.
	 */
	bool interrupts_active;
	uint32_t interrupt_bitmap; /* Only used for GFX 9.4.3 */

	/* QCM Device instance */
	struct device_queue_manager *dqm;

	/* Global GWS resource shared between processes */
	void *gws;
	bool gws_debug_workaround;

	/* Clients watching SMI events */
	struct list_head smi_clients;
	spinlock_t smi_lock;
	uint32_t reset_seq_num;

	/* SRAM ECC flag */
	atomic_t sram_ecc_flag;

	/*spm process id */
	unsigned int spm_pasid;

	/* Maximum process number mapped to HW scheduler */
	unsigned int max_proc_per_quantum;

	unsigned int compute_vmid_bitmap;

	struct kfd_local_mem_info local_mem_info;

	struct kfd_dev *kfd;

	/* Track per device allocated watch points */
	uint32_t alloc_watch_ids;
	spinlock_t watch_points_lock;
};

struct kfd_dev {
	struct amdgpu_device *adev;

	struct kfd_device_info device_info;

	u32 __iomem *doorbell_kernel_ptr; /* This is a pointer for a doorbells
					   * page used by kernel queue
					   */

	struct kgd2kfd_shared_resources shared_resources;

	const struct kfd2kgd_calls *kfd2kgd;
	struct mutex doorbell_mutex;

	void *gtt_mem;
	uint64_t gtt_start_gpu_addr;
	void *gtt_start_cpu_ptr;
	void *gtt_sa_bitmap;
	struct mutex gtt_sa_lock;
	unsigned int gtt_sa_chunk_size;
	unsigned int gtt_sa_num_of_chunks;

	bool init_complete;

	/* Firmware versions */
	uint16_t mec_fw_version;
	uint16_t mec2_fw_version;
	uint16_t sdma_fw_version;

	/* CWSR */
	bool cwsr_enabled;
	const void *cwsr_isa;
	unsigned int cwsr_isa_size;

	/* xGMI */
	uint64_t hive_id;

	bool pci_atomic_requested;

	/* Compute Profile ref. count */
	atomic_t compute_profile;

	struct ida doorbell_ida;
	unsigned int max_doorbell_slices;

	int noretry;

	struct kfd_node *nodes[MAX_KFD_NODES];
	unsigned int num_nodes;

	/* Kernel doorbells for KFD device */
	struct amdgpu_bo *doorbells;

	/* bitmap for dynamic doorbell allocation from doorbell object */
	unsigned long *doorbell_bitmap;
};

enum kfd_mempool {
	KFD_MEMPOOL_SYSTEM_CACHEABLE = 1,
	KFD_MEMPOOL_SYSTEM_WRITECOMBINE = 2,
	KFD_MEMPOOL_FRAMEBUFFER = 3,
};

/* Character device interface */
int kfd_chardev_init(void);
void kfd_chardev_exit(void);

/**
 * enum kfd_unmap_queues_filter - Enum for queue filters.
 *
 * @KFD_UNMAP_QUEUES_FILTER_ALL_QUEUES: Preempts all queues in the
 *						running queues list.
 *
 * @KFD_UNMAP_QUEUES_FILTER_DYNAMIC_QUEUES: Preempts all non-static queues
 *						in the run list.
 *
 * @KFD_UNMAP_QUEUES_FILTER_BY_PASID: Preempts queues that belongs to
 *						specific process.
 *
 */
enum kfd_unmap_queues_filter {
	KFD_UNMAP_QUEUES_FILTER_ALL_QUEUES = 1,
	KFD_UNMAP_QUEUES_FILTER_DYNAMIC_QUEUES = 2,
	KFD_UNMAP_QUEUES_FILTER_BY_PASID = 3
};

/**
 * enum kfd_queue_type - Enum for various queue types.
 *
 * @KFD_QUEUE_TYPE_COMPUTE: Regular user mode queue type.
 *
 * @KFD_QUEUE_TYPE_SDMA: SDMA user mode queue type.
 *
 * @KFD_QUEUE_TYPE_HIQ: HIQ queue type.
 *
 * @KFD_QUEUE_TYPE_DIQ: DIQ queue type.
 *
 * @KFD_QUEUE_TYPE_SDMA_XGMI: Special SDMA queue for XGMI interface.
 *
 * @KFD_QUEUE_TYPE_SDMA_BY_ENG_ID:  SDMA user mode queue with target SDMA engine ID.
 */
enum kfd_queue_type  {
	KFD_QUEUE_TYPE_COMPUTE,
	KFD_QUEUE_TYPE_SDMA,
	KFD_QUEUE_TYPE_HIQ,
	KFD_QUEUE_TYPE_DIQ,
	KFD_QUEUE_TYPE_SDMA_XGMI,
	KFD_QUEUE_TYPE_SDMA_BY_ENG_ID
};

enum kfd_queue_format {
	KFD_QUEUE_FORMAT_PM4,
	KFD_QUEUE_FORMAT_AQL
};

enum KFD_QUEUE_PRIORITY {
	KFD_QUEUE_PRIORITY_MINIMUM = 0,
	KFD_QUEUE_PRIORITY_MAXIMUM = 15
};

/**
 * struct queue_properties
 *
 * @type: The queue type.
 *
 * @queue_id: Queue identifier.
 *
 * @queue_address: Queue ring buffer address.
 *
 * @queue_size: Queue ring buffer size.
 *
 * @priority: Defines the queue priority relative to other queues in the
 * process.
 * This is just an indication and HW scheduling may override the priority as
 * necessary while keeping the relative prioritization.
 * the priority granularity is from 0 to f which f is the highest priority.
 * currently all queues are initialized with the highest priority.
 *
 * @queue_percent: This field is partially implemented and currently a zero in
 * this field defines that the queue is non active.
 *
 * @read_ptr: User space address which points to the number of dwords the
 * cp read from the ring buffer. This field updates automatically by the H/W.
 *
 * @write_ptr: Defines the number of dwords written to the ring buffer.
 *
 * @doorbell_ptr: Notifies the H/W of new packet written to the queue ring
 * buffer. This field should be similar to write_ptr and the user should
 * update this field after updating the write_ptr.
 *
 * @doorbell_off: The doorbell offset in the doorbell pci-bar.
 *
 * @is_interop: Defines if this is a interop queue. Interop queue means that
 * the queue can access both graphics and compute resources.
 *
 * @is_evicted: Defines if the queue is evicted. Only active queues
 * are evicted, rendering them inactive.
 *
 * @is_active: Defines if the queue is active or not. @is_active and
 * @is_evicted are protected by the DQM lock.
 *
 * @is_gws: Defines if the queue has been updated to be GWS-capable or not.
 * @is_gws should be protected by the DQM lock, since changing it can yield the
 * possibility of updating DQM state on number of GWS queues.
 *
 * @vmid: If the scheduling mode is no cp scheduling the field defines the vmid
 * of the queue.
 *
 * This structure represents the queue properties for each queue no matter if
 * it's user mode or kernel mode queue.
 *
 */

struct queue_properties {
	enum kfd_queue_type type;
	enum kfd_queue_format format;
	unsigned int queue_id;
	uint64_t queue_address;
	uint64_t  queue_size;
	uint32_t priority;
	uint32_t queue_percent;
	void __user *read_ptr;
	void __user *write_ptr;
	void __iomem *doorbell_ptr;
	uint32_t doorbell_off;
	bool is_interop;
	bool is_evicted;
	bool is_suspended;
	bool is_being_destroyed;
	bool is_active;
	bool is_gws;
	uint32_t pm4_target_xcc;
	bool is_dbg_wa;
	bool is_user_cu_masked;
	/* Not relevant for user mode queues in cp scheduling */
	unsigned int vmid;
	/* Relevant only for sdma queues*/
	uint32_t sdma_engine_id;
	uint32_t sdma_queue_id;
	uint32_t sdma_vm_addr;
	/* Relevant only for VI */
	uint64_t eop_ring_buffer_address;
	uint32_t eop_ring_buffer_size;
	uint64_t ctx_save_restore_area_address;
	uint32_t ctx_save_restore_area_size;
	uint32_t ctl_stack_size;
	uint64_t tba_addr;
	uint64_t tma_addr;
	uint64_t exception_status;

	struct amdgpu_bo *wptr_bo;
	struct amdgpu_bo *rptr_bo;
	struct amdgpu_bo *ring_bo;
	struct amdgpu_bo *eop_buf_bo;
	struct amdgpu_bo *cwsr_bo;
};

#define QUEUE_IS_ACTIVE(q) ((q).queue_size > 0 &&	\
			    (q).queue_address != 0 &&	\
			    (q).queue_percent > 0 &&	\
			    !(q).is_evicted &&		\
			    !(q).is_suspended)

enum mqd_update_flag {
	UPDATE_FLAG_DBG_WA_ENABLE = 1,
	UPDATE_FLAG_DBG_WA_DISABLE = 2,
	UPDATE_FLAG_IS_GWS = 4, /* quirk for gfx9 IP */
};

struct mqd_update_info {
	union {
		struct {
			uint32_t count; /* Must be a multiple of 32 */
			uint32_t *ptr;
		} cu_mask;
	};
	enum mqd_update_flag update_flag;
};

/**
 * struct queue
 *
 * @list: Queue linked list.
 *
 * @mqd: The queue MQD (memory queue descriptor).
 *
 * @mqd_mem_obj: The MQD local gpu memory object.
 *
 * @gart_mqd_addr: The MQD gart mc address.
 *
 * @properties: The queue properties.
 *
 * @mec: Used only in no cp scheduling mode and identifies to micro engine id
 *	 that the queue should be executed on.
 *
 * @pipe: Used only in no cp scheduling mode and identifies the queue's pipe
 *	  id.
 *
 * @queue: Used only in no cp scheduliong mode and identifies the queue's slot.
 *
 * @process: The kfd process that created this queue.
 *
 * @device: The kfd device that created this queue.
 *
 * @gws: Pointing to gws kgd_mem if this is a gws control queue; NULL
 * otherwise.
 *
 * This structure represents user mode compute queues.
 * It contains all the necessary data to handle such queues.
 *
 */

struct queue {
	struct list_head list;
	void *mqd;
	struct kfd_mem_obj *mqd_mem_obj;
	uint64_t gart_mqd_addr;
	struct queue_properties properties;

	uint32_t mec;
	uint32_t pipe;
	uint32_t queue;

	unsigned int sdma_id;
	unsigned int doorbell_id;

	struct kfd_process	*process;
	struct kfd_node		*device;
	void *gws;

	/* procfs */
	struct kobject kobj;

	void *gang_ctx_bo;
	uint64_t gang_ctx_gpu_addr;
	void *gang_ctx_cpu_ptr;

	struct amdgpu_bo *wptr_bo_gart;
};

enum KFD_MQD_TYPE {
	KFD_MQD_TYPE_HIQ = 0,		/* for hiq */
	KFD_MQD_TYPE_CP,		/* for cp queues and diq */
	KFD_MQD_TYPE_SDMA,		/* for sdma queues */
	KFD_MQD_TYPE_DIQ,		/* for diq */
	KFD_MQD_TYPE_MAX
};

enum KFD_PIPE_PRIORITY {
	KFD_PIPE_PRIORITY_CS_LOW = 0,
	KFD_PIPE_PRIORITY_CS_MEDIUM,
	KFD_PIPE_PRIORITY_CS_HIGH
};

struct scheduling_resources {
	unsigned int vmid_mask;
	enum kfd_queue_type type;
	uint64_t queue_mask;
	uint64_t gws_mask;
	uint32_t oac_mask;
	uint32_t gds_heap_base;
	uint32_t gds_heap_size;
};

struct process_queue_manager {
	/* data */
	struct kfd_process	*process;
	struct list_head	queues;
	unsigned long		*queue_slot_bitmap;
};

struct qcm_process_device {
	/* The Device Queue Manager that owns this data */
	struct device_queue_manager *dqm;
	struct process_queue_manager *pqm;
	/* Queues list */
	struct list_head queues_list;
	struct list_head priv_queue_list;

	unsigned int queue_count;
	unsigned int vmid;
	bool is_debug;
	unsigned int evicted; /* eviction counter, 0=active */

	/* This flag tells if we should reset all wavefronts on
	 * process termination
	 */
	bool reset_wavefronts;

	/* This flag tells us if this process has a GWS-capable
	 * queue that will be mapped into the runlist. It's
	 * possible to request a GWS BO, but not have the queue
	 * currently mapped, and this changes how the MAP_PROCESS
	 * PM4 packet is configured.
	 */
	bool mapped_gws_queue;

	/* All the memory management data should be here too */
	uint64_t gds_context_area;
	/* Contains page table flags such as AMDGPU_PTE_VALID since gfx9 */
	uint64_t page_table_base;
	uint32_t sh_mem_config;
	uint32_t sh_mem_bases;
	uint32_t sh_mem_ape1_base;
	uint32_t sh_mem_ape1_limit;
	uint32_t gds_size;
	uint32_t num_gws;
	uint32_t num_oac;
	uint32_t sh_hidden_private_base;

	/* CWSR memory */
	struct kgd_mem *cwsr_mem;
	void *cwsr_kaddr;
	uint64_t cwsr_base;
	uint64_t tba_addr;
	uint64_t tma_addr;

	/* IB memory */
	struct kgd_mem *ib_mem;
	uint64_t ib_base;
	void *ib_kaddr;

	/* doorbells for kfd process */
	struct amdgpu_bo *proc_doorbells;

	/* bitmap for dynamic doorbell allocation from the bo */
	unsigned long *doorbell_bitmap;
};

/* KFD Memory Eviction */

/* Approx. wait time before attempting to restore evicted BOs */
#define PROCESS_RESTORE_TIME_MS 100
/* Approx. back off time if restore fails due to lack of memory */
#define PROCESS_BACK_OFF_TIME_MS 100
/* Approx. time before evicting the process again */
#define PROCESS_ACTIVE_TIME_MS 10

/* 8 byte handle containing GPU ID in the most significant 4 bytes and
 * idr_handle in the least significant 4 bytes
 */
#define MAKE_HANDLE(gpu_id, idr_handle) \
	(((uint64_t)(gpu_id) << 32) + idr_handle)
#define GET_GPU_ID(handle) (handle >> 32)
#define GET_IDR_HANDLE(handle) (handle & 0xFFFFFFFF)

enum kfd_pdd_bound {
	PDD_UNBOUND = 0,
	PDD_BOUND,
	PDD_BOUND_SUSPENDED,
};

#define MAX_SYSFS_FILENAME_LEN 15

/*
 * SDMA counter runs at 100MHz frequency.
 * We display SDMA activity in microsecond granularity in sysfs.
 * As a result, the divisor is 100.
 */
#define SDMA_ACTIVITY_DIVISOR  100

/* Data that is per-process-per device. */
struct kfd_process_device {
	/* The device that owns this data. */
	struct kfd_node *dev;

	/* The process that owns this kfd_process_device. */
	struct kfd_process *process;

	/* per-process-per device QCM data structure */
	struct qcm_process_device qpd;

	/*Apertures*/
	uint64_t lds_base;
	uint64_t lds_limit;
	uint64_t gpuvm_base;
	uint64_t gpuvm_limit;
	uint64_t scratch_base;
	uint64_t scratch_limit;

	/* VM context for GPUVM allocations */
	struct file *drm_file;
	void *drm_priv;

	/* GPUVM allocations storage */
	struct idr alloc_idr;

	/* Flag used to tell the pdd has dequeued from the dqm.
	 * This is used to prevent dev->dqm->ops.process_termination() from
	 * being called twice when it is already called in IOMMU callback
	 * function.
	 */
	bool already_dequeued;
	bool runtime_inuse;

	/* Is this process/pasid bound to this device? (amd_iommu_bind_pasid) */
	enum kfd_pdd_bound bound;

	/* VRAM usage */
	atomic64_t vram_usage;
	struct attribute attr_vram;
	char vram_filename[MAX_SYSFS_FILENAME_LEN];

	/* SDMA activity tracking */
	uint64_t sdma_past_activity_counter;
	struct attribute attr_sdma;
	char sdma_filename[MAX_SYSFS_FILENAME_LEN];

	/* Eviction activity tracking */
	uint64_t last_evict_timestamp;
	atomic64_t evict_duration_counter;
	struct attribute attr_evict;

	struct kobject *kobj_stats;

	/*
	 * @cu_occupancy: Reports occupancy of Compute Units (CU) of a process
	 * that is associated with device encoded by "this" struct instance. The
	 * value reflects CU usage by all of the waves launched by this process
	 * on this device. A very important property of occupancy parameter is
	 * that its value is a snapshot of current use.
	 *
	 * Following is to be noted regarding how this parameter is reported:
	 *
	 *  The number of waves that a CU can launch is limited by couple of
	 *  parameters. These are encoded by struct amdgpu_cu_info instance
	 *  that is part of every device definition. For GFX9 devices this
	 *  translates to 40 waves (simd_per_cu * max_waves_per_simd) when waves
	 *  do not use scratch memory and 32 waves (max_scratch_slots_per_cu)
	 *  when they do use scratch memory. This could change for future
	 *  devices and therefore this example should be considered as a guide.
	 *
	 *  All CU's of a device are available for the process. This may not be true
	 *  under certain conditions - e.g. CU masking.
	 *
	 *  Finally number of CU's that are occupied by a process is affected by both
	 *  number of CU's a device has along with number of other competing processes
	 */
	struct attribute attr_cu_occupancy;

	/* sysfs counters for GPU retry fault and page migration tracking */
	struct kobject *kobj_counters;
	struct attribute attr_faults;
	struct attribute attr_page_in;
	struct attribute attr_page_out;
	uint64_t faults;
	uint64_t page_in;
	uint64_t page_out;

	/* Exception code status*/
	uint64_t exception_status;
	void *vm_fault_exc_data;
	size_t vm_fault_exc_data_size;

	/* Tracks debug per-vmid request settings */
	uint32_t spi_dbg_override;
	uint32_t spi_dbg_launch_mode;
	uint32_t watch_points[4];
	uint32_t alloc_watch_ids;

	/*
	 * If this process has been checkpointed before, then the user
	 * application will use the original gpu_id on the
	 * checkpointed node to refer to this device.
	 */
	uint32_t user_gpu_id;

	void *proc_ctx_bo;
	uint64_t proc_ctx_gpu_addr;
	void *proc_ctx_cpu_ptr;

	/* Tracks queue reset status */
	bool has_reset_queue;
};

#define qpd_to_pdd(x) container_of(x, struct kfd_process_device, qpd)

struct svm_range_list {
	struct mutex			lock;
	struct rb_root_cached		objects;
	struct list_head		list;
	struct work_struct		deferred_list_work;
	struct list_head		deferred_range_list;
	struct list_head                criu_svm_metadata_list;
	spinlock_t			deferred_list_lock;
	atomic_t			evicted_ranges;
	atomic_t			drain_pagefaults;
	struct delayed_work		restore_work;
	DECLARE_BITMAP(bitmap_supported, MAX_GPU_INSTANCE);
	struct task_struct		*faulting_task;
	/* check point ts decides if page fault recovery need be dropped */
	uint64_t			checkpoint_ts[MAX_GPU_INSTANCE];

	/* Default granularity to use in buffer migration
	 * and restoration of backing memory while handling
	 * recoverable page faults
	 */
	uint8_t default_granularity;
};

/* Process data */
struct kfd_process {
	/*
	 * kfd_process are stored in an mm_struct*->kfd_process*
	 * hash table (kfd_processes in kfd_process.c)
	 */
	struct hlist_node kfd_processes;

	/*
	 * Opaque pointer to mm_struct. We don't hold a reference to
	 * it so it should never be dereferenced from here. This is
	 * only used for looking up processes by their mm.
	 */
	void *mm;

	struct kref ref;
	struct work_struct release_work;

	struct mutex mutex;

	/*
	 * In any process, the thread that started main() is the lead
	 * thread and outlives the rest.
	 * It is here because amd_iommu_bind_pasid wants a task_struct.
	 * It can also be used for safely getting a reference to the
	 * mm_struct of the process.
	 */
	struct task_struct *lead_thread;

	/* We want to receive a notification when the mm_struct is destroyed */
	struct mmu_notifier mmu_notifier;

	u32 pasid;

	/*
	 * Array of kfd_process_device pointers,
	 * one for each device the process is using.
	 */
	struct kfd_process_device *pdds[MAX_GPU_INSTANCE];
	uint32_t n_pdds;

	struct process_queue_manager pqm;

	/*Is the user space process 32 bit?*/
	bool is_32bit_user_mode;

	/* Event-related data */
	struct mutex event_mutex;
	/* Event ID allocator and lookup */
	struct idr event_idr;
	/* Event page */
	u64 signal_handle;
	struct kfd_signal_page *signal_page;
	size_t signal_mapped_size;
	size_t signal_event_count;
	bool signal_event_limit_reached;

	/* Information used for memory eviction */
	void *kgd_process_info;
	/* Eviction fence that is attached to all the BOs of this process. The
	 * fence will be triggered during eviction and new one will be created
	 * during restore
	 */
	struct dma_fence __rcu *ef;

	/* Work items for evicting and restoring BOs */
	struct delayed_work eviction_work;
	struct delayed_work restore_work;
	/* seqno of the last scheduled eviction */
	unsigned int last_eviction_seqno;
	/* Approx. the last timestamp (in jiffies) when the process was
	 * restored after an eviction
	 */
	unsigned long last_restore_timestamp;

	/* Indicates device process is debug attached with reserved vmid. */
	bool debug_trap_enabled;

	/* per-process-per device debug event fd file */
	struct file *dbg_ev_file;

	/* If the process is a kfd debugger, we need to know so we can clean
	 * up at exit time.  If a process enables debugging on itself, it does
	 * its own clean-up, so we don't set the flag here.  We track this by
	 * counting the number of processes this process is debugging.
	 */
	atomic_t debugged_process_count;

	/* If the process is a debugged, this is the debugger process */
	struct kfd_process *debugger_process;

	/* Kobj for our procfs */
	struct kobject *kobj;
	struct kobject *kobj_queues;
	struct attribute attr_pasid;

	/* Keep track cwsr init */
	bool has_cwsr;

	/* Exception code enable mask and status */
	uint64_t exception_enable_mask;
	uint64_t exception_status;

	/* Used to drain stale interrupts */
	wait_queue_head_t wait_irq_drain;
	bool irq_drain_is_open;

	/* shared virtual memory registered by this process */
	struct svm_range_list svms;

	bool xnack_enabled;

	/* Work area for debugger event writer worker. */
	struct work_struct debug_event_workarea;

	/* Tracks debug per-vmid request for debug flags */
	u32 dbg_flags;

	atomic_t poison;
	/* Queues are in paused stated because we are in the process of doing a CRIU checkpoint */
	bool queues_paused;

	/* Tracks runtime enable status */
	struct semaphore runtime_enable_sema;
	bool is_runtime_retry;
	struct kfd_runtime_info runtime_info;
};

#define KFD_PROCESS_TABLE_SIZE 5 /* bits: 32 entries */
extern DECLARE_HASHTABLE(kfd_processes_table, KFD_PROCESS_TABLE_SIZE);
extern struct srcu_struct kfd_processes_srcu;

/**
 * typedef amdkfd_ioctl_t - typedef for ioctl function pointer.
 *
 * @filep: pointer to file structure.
 * @p: amdkfd process pointer.
 * @data: pointer to arg that was copied from user.
 *
 * Return: returns ioctl completion code.
 */
typedef int amdkfd_ioctl_t(struct file *filep, struct kfd_process *p,
				void *data);

struct amdkfd_ioctl_desc {
	unsigned int cmd;
	int flags;
	amdkfd_ioctl_t *func;
	unsigned int cmd_drv;
	const char *name;
};
bool kfd_dev_is_large_bar(struct kfd_node *dev);

int kfd_process_create_wq(void);
void kfd_process_destroy_wq(void);
void kfd_cleanup_processes(void);
struct kfd_process *kfd_create_process(struct task_struct *thread);
struct kfd_process *kfd_get_process(const struct task_struct *task);
struct kfd_process *kfd_lookup_process_by_pasid(u32 pasid);
struct kfd_process *kfd_lookup_process_by_mm(const struct mm_struct *mm);

int kfd_process_gpuidx_from_gpuid(struct kfd_process *p, uint32_t gpu_id);
int kfd_process_gpuid_from_node(struct kfd_process *p, struct kfd_node *node,
				uint32_t *gpuid, uint32_t *gpuidx);
static inline int kfd_process_gpuid_from_gpuidx(struct kfd_process *p,
				uint32_t gpuidx, uint32_t *gpuid) {
	return gpuidx < p->n_pdds ? p->pdds[gpuidx]->dev->id : -EINVAL;
}
static inline struct kfd_process_device *kfd_process_device_from_gpuidx(
				struct kfd_process *p, uint32_t gpuidx) {
	return gpuidx < p->n_pdds ? p->pdds[gpuidx] : NULL;
}

void kfd_unref_process(struct kfd_process *p);
int kfd_process_evict_queues(struct kfd_process *p, uint32_t trigger);
int kfd_process_restore_queues(struct kfd_process *p);
void kfd_suspend_all_processes(void);
int kfd_resume_all_processes(void);

struct kfd_process_device *kfd_process_device_data_by_id(struct kfd_process *process,
							 uint32_t gpu_id);

int kfd_process_get_user_gpu_id(struct kfd_process *p, uint32_t actual_gpu_id);

int kfd_process_device_init_vm(struct kfd_process_device *pdd,
			       struct file *drm_file);
struct kfd_process_device *kfd_bind_process_to_device(struct kfd_node *dev,
						struct kfd_process *p);
struct kfd_process_device *kfd_get_process_device_data(struct kfd_node *dev,
							struct kfd_process *p);
struct kfd_process_device *kfd_create_process_device_data(struct kfd_node *dev,
							struct kfd_process *p);

bool kfd_process_xnack_mode(struct kfd_process *p, bool supported);

int kfd_reserved_mem_mmap(struct kfd_node *dev, struct kfd_process *process,
			  struct vm_area_struct *vma);

/* KFD process API for creating and translating handles */
int kfd_process_device_create_obj_handle(struct kfd_process_device *pdd,
					void *mem);
void *kfd_process_device_translate_handle(struct kfd_process_device *p,
					int handle);
void kfd_process_device_remove_obj_handle(struct kfd_process_device *pdd,
					int handle);
struct kfd_process *kfd_lookup_process_by_pid(struct pid *pid);

/* PASIDs */
int kfd_pasid_init(void);
void kfd_pasid_exit(void);
bool kfd_set_pasid_limit(unsigned int new_limit);
unsigned int kfd_get_pasid_limit(void);
u32 kfd_pasid_alloc(void);
void kfd_pasid_free(u32 pasid);

/* Doorbells */
size_t kfd_doorbell_process_slice(struct kfd_dev *kfd);
int kfd_doorbell_init(struct kfd_dev *kfd);
void kfd_doorbell_fini(struct kfd_dev *kfd);
int kfd_doorbell_mmap(struct kfd_node *dev, struct kfd_process *process,
		      struct vm_area_struct *vma);
void __iomem *kfd_get_kernel_doorbell(struct kfd_dev *kfd,
					unsigned int *doorbell_off);
void kfd_release_kernel_doorbell(struct kfd_dev *kfd, u32 __iomem *db_addr);
u32 read_kernel_doorbell(u32 __iomem *db);
void write_kernel_doorbell(void __iomem *db, u32 value);
void write_kernel_doorbell64(void __iomem *db, u64 value);
unsigned int kfd_get_doorbell_dw_offset_in_bar(struct kfd_dev *kfd,
					struct kfd_process_device *pdd,
					unsigned int doorbell_id);
phys_addr_t kfd_get_process_doorbells(struct kfd_process_device *pdd);
int kfd_alloc_process_doorbells(struct kfd_dev *kfd,
				struct kfd_process_device *pdd);
void kfd_free_process_doorbells(struct kfd_dev *kfd,
				struct kfd_process_device *pdd);
/* GTT Sub-Allocator */

int kfd_gtt_sa_allocate(struct kfd_node *node, unsigned int size,
			struct kfd_mem_obj **mem_obj);

int kfd_gtt_sa_free(struct kfd_node *node, struct kfd_mem_obj *mem_obj);

extern struct device *kfd_device;

/* KFD's procfs */
void kfd_procfs_init(void);
void kfd_procfs_shutdown(void);
int kfd_procfs_add_queue(struct queue *q);
void kfd_procfs_del_queue(struct queue *q);

/* Topology */
int kfd_topology_init(void);
void kfd_topology_shutdown(void);
int kfd_topology_add_device(struct kfd_node *gpu);
int kfd_topology_remove_device(struct kfd_node *gpu);
struct kfd_topology_device *kfd_topology_device_by_proximity_domain(
						uint32_t proximity_domain);
struct kfd_topology_device *kfd_topology_device_by_proximity_domain_no_lock(
						uint32_t proximity_domain);
struct kfd_topology_device *kfd_topology_device_by_id(uint32_t gpu_id);
struct kfd_node *kfd_device_by_id(uint32_t gpu_id);
struct kfd_node *kfd_device_by_pci_dev(const struct pci_dev *pdev);
static inline bool kfd_irq_is_from_node(struct kfd_node *node, uint32_t node_id,
					uint32_t vmid)
{
	return (node->interrupt_bitmap & (1 << node_id)) != 0 &&
	       (node->compute_vmid_bitmap & (1 << vmid)) != 0;
}
static inline struct kfd_node *kfd_node_by_irq_ids(struct amdgpu_device *adev,
					uint32_t node_id, uint32_t vmid) {
	struct kfd_dev *dev = adev->kfd.dev;
	uint32_t i;

	if (KFD_GC_VERSION(dev) != IP_VERSION(9, 4, 3) &&
	    KFD_GC_VERSION(dev) != IP_VERSION(9, 4, 4) &&
	    KFD_GC_VERSION(dev) != IP_VERSION(9, 5, 0))
		return dev->nodes[0];

	for (i = 0; i < dev->num_nodes; i++)
		if (kfd_irq_is_from_node(dev->nodes[i], node_id, vmid))
			return dev->nodes[i];

	return NULL;
}
int kfd_topology_enum_kfd_devices(uint8_t idx, struct kfd_node **kdev);
int kfd_numa_node_to_apic_id(int numa_node_id);

/* Interrupts */
#define	KFD_IRQ_FENCE_CLIENTID	0xff
#define	KFD_IRQ_FENCE_SOURCEID	0xff
#define	KFD_IRQ_IS_FENCE(client, source)				\
				((client) == KFD_IRQ_FENCE_CLIENTID &&	\
				(source) == KFD_IRQ_FENCE_SOURCEID)
int kfd_interrupt_init(struct kfd_node *dev);
void kfd_interrupt_exit(struct kfd_node *dev);
bool enqueue_ih_ring_entry(struct kfd_node *kfd, const void *ih_ring_entry);
bool interrupt_is_wanted(struct kfd_node *dev,
				const uint32_t *ih_ring_entry,
				uint32_t *patched_ihre, bool *flag);
int kfd_process_drain_interrupts(struct kfd_process_device *pdd);
void kfd_process_close_interrupt_drain(unsigned int pasid);

/* amdkfd Apertures */
int kfd_init_apertures(struct kfd_process *process);

void kfd_process_set_trap_handler(struct qcm_process_device *qpd,
				  uint64_t tba_addr,
				  uint64_t tma_addr);
void kfd_process_set_trap_debug_flag(struct qcm_process_device *qpd,
				     bool enabled);

/* CWSR initialization */
int kfd_process_init_cwsr_apu(struct kfd_process *process, struct file *filep);

/* CRIU */
/*
 * Need to increment KFD_CRIU_PRIV_VERSION each time a change is made to any of the CRIU private
 * structures:
 * kfd_criu_process_priv_data
 * kfd_criu_device_priv_data
 * kfd_criu_bo_priv_data
 * kfd_criu_queue_priv_data
 * kfd_criu_event_priv_data
 * kfd_criu_svm_range_priv_data
 */

#define KFD_CRIU_PRIV_VERSION 1

struct kfd_criu_process_priv_data {
	uint32_t version;
	uint32_t xnack_mode;
};

struct kfd_criu_device_priv_data {
	/* For future use */
	uint64_t reserved;
};

struct kfd_criu_bo_priv_data {
	uint64_t user_addr;
	uint32_t idr_handle;
	uint32_t mapped_gpuids[MAX_GPU_INSTANCE];
};

/*
 * The first 4 bytes of kfd_criu_queue_priv_data, kfd_criu_event_priv_data,
 * kfd_criu_svm_range_priv_data is the object type
 */
enum kfd_criu_object_type {
	KFD_CRIU_OBJECT_TYPE_QUEUE,
	KFD_CRIU_OBJECT_TYPE_EVENT,
	KFD_CRIU_OBJECT_TYPE_SVM_RANGE,
};

struct kfd_criu_svm_range_priv_data {
	uint32_t object_type;
	uint64_t start_addr;
	uint64_t size;
	/* Variable length array of attributes */
	struct kfd_ioctl_svm_attribute attrs[];
};

struct kfd_criu_queue_priv_data {
	uint32_t object_type;
	uint64_t q_address;
	uint64_t q_size;
	uint64_t read_ptr_addr;
	uint64_t write_ptr_addr;
	uint64_t doorbell_off;
	uint64_t eop_ring_buffer_address;
	uint64_t ctx_save_restore_area_address;
	uint32_t gpu_id;
	uint32_t type;
	uint32_t format;
	uint32_t q_id;
	uint32_t priority;
	uint32_t q_percent;
	uint32_t doorbell_id;
	uint32_t gws;
	uint32_t sdma_id;
	uint32_t eop_ring_buffer_size;
	uint32_t ctx_save_restore_area_size;
	uint32_t ctl_stack_size;
	uint32_t mqd_size;
};

struct kfd_criu_event_priv_data {
	uint32_t object_type;
	uint64_t user_handle;
	uint32_t event_id;
	uint32_t auto_reset;
	uint32_t type;
	uint32_t signaled;

	union {
		struct kfd_hsa_memory_exception_data memory_exception_data;
		struct kfd_hsa_hw_exception_data hw_exception_data;
	};
};

int kfd_process_get_queue_info(struct kfd_process *p,
			       uint32_t *num_queues,
			       uint64_t *priv_data_sizes);

int kfd_criu_checkpoint_queues(struct kfd_process *p,
			 uint8_t __user *user_priv_data,
			 uint64_t *priv_data_offset);

int kfd_criu_restore_queue(struct kfd_process *p,
			   uint8_t __user *user_priv_data,
			   uint64_t *priv_data_offset,
			   uint64_t max_priv_data_size);

int kfd_criu_checkpoint_events(struct kfd_process *p,
			 uint8_t __user *user_priv_data,
			 uint64_t *priv_data_offset);

int kfd_criu_restore_event(struct file *devkfd,
			   struct kfd_process *p,
			   uint8_t __user *user_priv_data,
			   uint64_t *priv_data_offset,
			   uint64_t max_priv_data_size);
/* CRIU - End */

/* Queue Context Management */
int init_queue(struct queue **q, const struct queue_properties *properties);
void uninit_queue(struct queue *q);
void print_queue_properties(struct queue_properties *q);
void print_queue(struct queue *q);
int kfd_queue_buffer_get(struct amdgpu_vm *vm, void __user *addr, struct amdgpu_bo **pbo,
			 u64 expected_size);
void kfd_queue_buffer_put(struct amdgpu_bo **bo);
int kfd_queue_acquire_buffers(struct kfd_process_device *pdd, struct queue_properties *properties);
int kfd_queue_release_buffers(struct kfd_process_device *pdd, struct queue_properties *properties);
void kfd_queue_unref_bo_va(struct amdgpu_vm *vm, struct amdgpu_bo **bo);
int kfd_queue_unref_bo_vas(struct kfd_process_device *pdd,
			   struct queue_properties *properties);
void kfd_queue_ctx_save_restore_size(struct kfd_topology_device *dev);

struct mqd_manager *mqd_manager_init_cik(enum KFD_MQD_TYPE type,
		struct kfd_node *dev);
struct mqd_manager *mqd_manager_init_vi(enum KFD_MQD_TYPE type,
		struct kfd_node *dev);
struct mqd_manager *mqd_manager_init_v9(enum KFD_MQD_TYPE type,
		struct kfd_node *dev);
struct mqd_manager *mqd_manager_init_v10(enum KFD_MQD_TYPE type,
		struct kfd_node *dev);
struct mqd_manager *mqd_manager_init_v11(enum KFD_MQD_TYPE type,
		struct kfd_node *dev);
struct mqd_manager *mqd_manager_init_v12(enum KFD_MQD_TYPE type,
		struct kfd_node *dev);
struct device_queue_manager *device_queue_manager_init(struct kfd_node *dev);
void device_queue_manager_uninit(struct device_queue_manager *dqm);
struct kernel_queue *kernel_queue_init(struct kfd_node *dev,
					enum kfd_queue_type type);
void kernel_queue_uninit(struct kernel_queue *kq);
int kfd_dqm_evict_pasid(struct device_queue_manager *dqm, u32 pasid);
int kfd_dqm_suspend_bad_queue_mes(struct kfd_node *knode, u32 pasid, u32 doorbell_id);

/* Process Queue Manager */
struct process_queue_node {
	struct queue *q;
	struct kernel_queue *kq;
	struct list_head process_queue_list;
};

void kfd_process_dequeue_from_device(struct kfd_process_device *pdd);
void kfd_process_dequeue_from_all_devices(struct kfd_process *p);
int pqm_init(struct process_queue_manager *pqm, struct kfd_process *p);
void pqm_uninit(struct process_queue_manager *pqm);
int pqm_create_queue(struct process_queue_manager *pqm,
			    struct kfd_node *dev,
			    struct queue_properties *properties,
			    unsigned int *qid,
			    const struct kfd_criu_queue_priv_data *q_data,
			    const void *restore_mqd,
			    const void *restore_ctl_stack,
			    uint32_t *p_doorbell_offset_in_process);
int pqm_destroy_queue(struct process_queue_manager *pqm, unsigned int qid);
int pqm_update_queue_properties(struct process_queue_manager *pqm, unsigned int qid,
			struct queue_properties *p);
int pqm_update_mqd(struct process_queue_manager *pqm, unsigned int qid,
			struct mqd_update_info *minfo);
int pqm_set_gws(struct process_queue_manager *pqm, unsigned int qid,
			void *gws);
struct kernel_queue *pqm_get_kernel_queue(struct process_queue_manager *pqm,
						unsigned int qid);
struct queue *pqm_get_user_queue(struct process_queue_manager *pqm,
						unsigned int qid);
int pqm_get_wave_state(struct process_queue_manager *pqm,
		       unsigned int qid,
		       void __user *ctl_stack,
		       u32 *ctl_stack_used_size,
		       u32 *save_area_used_size);
int pqm_get_queue_snapshot(struct process_queue_manager *pqm,
			   uint64_t exception_clear_mask,
			   void __user *buf,
			   int *num_qss_entries,
			   uint32_t *entry_size);

int amdkfd_fence_wait_timeout(struct device_queue_manager *dqm,
			      uint64_t fence_value,
			      unsigned int timeout_ms);

int pqm_get_queue_checkpoint_info(struct process_queue_manager *pqm,
				  unsigned int qid,
				  u32 *mqd_size,
				  u32 *ctl_stack_size);
/* Packet Manager */

#define KFD_FENCE_COMPLETED (100)
#define KFD_FENCE_INIT   (10)

struct packet_manager {
	struct device_queue_manager *dqm;
	struct kernel_queue *priv_queue;
	struct mutex lock;
	bool allocated;
	struct kfd_mem_obj *ib_buffer_obj;
	unsigned int ib_size_bytes;
	bool is_over_subscription;

	const struct packet_manager_funcs *pmf;
};

struct packet_manager_funcs {
	/* Support ASIC-specific packet formats for PM4 packets */
	int (*map_process)(struct packet_manager *pm, uint32_t *buffer,
			struct qcm_process_device *qpd);
	int (*runlist)(struct packet_manager *pm, uint32_t *buffer,
			uint64_t ib, size_t ib_size_in_dwords, bool chain);
	int (*set_resources)(struct packet_manager *pm, uint32_t *buffer,
			struct scheduling_resources *res);
	int (*map_queues)(struct packet_manager *pm, uint32_t *buffer,
			struct queue *q, bool is_static);
	int (*unmap_queues)(struct packet_manager *pm, uint32_t *buffer,
			enum kfd_unmap_queues_filter mode,
			uint32_t filter_param, bool reset);
	int (*set_grace_period)(struct packet_manager *pm, uint32_t *buffer,
			uint32_t grace_period);
	int (*query_status)(struct packet_manager *pm, uint32_t *buffer,
			uint64_t fence_address,	uint64_t fence_value);
	int (*release_mem)(uint64_t gpu_addr, uint32_t *buffer);

	/* Packet sizes */
	int map_process_size;
	int runlist_size;
	int set_resources_size;
	int map_queues_size;
	int unmap_queues_size;
	int set_grace_period_size;
	int query_status_size;
	int release_mem_size;
};

extern const struct packet_manager_funcs kfd_vi_pm_funcs;
extern const struct packet_manager_funcs kfd_v9_pm_funcs;
extern const struct packet_manager_funcs kfd_aldebaran_pm_funcs;

int pm_init(struct packet_manager *pm, struct device_queue_manager *dqm);
void pm_uninit(struct packet_manager *pm);
int pm_send_set_resources(struct packet_manager *pm,
				struct scheduling_resources *res);
int pm_send_runlist(struct packet_manager *pm, struct list_head *dqm_queues);
int pm_send_query_status(struct packet_manager *pm, uint64_t fence_address,
				uint64_t fence_value);

int pm_send_unmap_queue(struct packet_manager *pm,
			enum kfd_unmap_queues_filter mode,
			uint32_t filter_param, bool reset);

void pm_release_ib(struct packet_manager *pm);

int pm_update_grace_period(struct packet_manager *pm, uint32_t grace_period);

/* Following PM funcs can be shared among VI and AI */
unsigned int pm_build_pm4_header(unsigned int opcode, size_t packet_size);

uint64_t kfd_get_number_elems(struct kfd_dev *kfd);

/* Events */
extern const struct kfd_event_interrupt_class event_interrupt_class_cik;
extern const struct kfd_event_interrupt_class event_interrupt_class_v9;
extern const struct kfd_event_interrupt_class event_interrupt_class_v9_4_3;
extern const struct kfd_event_interrupt_class event_interrupt_class_v10;
extern const struct kfd_event_interrupt_class event_interrupt_class_v11;

extern const struct kfd_device_global_init_class device_global_init_class_cik;

int kfd_event_init_process(struct kfd_process *p);
void kfd_event_free_process(struct kfd_process *p);
int kfd_event_mmap(struct kfd_process *process, struct vm_area_struct *vma);
int kfd_wait_on_events(struct kfd_process *p,
		       uint32_t num_events, void __user *data,
		       bool all, uint32_t *user_timeout_ms,
		       uint32_t *wait_result);
void kfd_signal_event_interrupt(u32 pasid, uint32_t partial_id,
				uint32_t valid_id_bits);
void kfd_signal_hw_exception_event(u32 pasid);
int kfd_set_event(struct kfd_process *p, uint32_t event_id);
int kfd_reset_event(struct kfd_process *p, uint32_t event_id);
int kfd_kmap_event_page(struct kfd_process *p, uint64_t event_page_offset);

int kfd_event_create(struct file *devkfd, struct kfd_process *p,
		     uint32_t event_type, bool auto_reset, uint32_t node_id,
		     uint32_t *event_id, uint32_t *event_trigger_data,
		     uint64_t *event_page_offset, uint32_t *event_slot_index);

int kfd_get_num_events(struct kfd_process *p);
int kfd_event_destroy(struct kfd_process *p, uint32_t event_id);

void kfd_signal_vm_fault_event(struct kfd_node *dev, u32 pasid,
				struct kfd_vm_fault_info *info,
				struct kfd_hsa_memory_exception_data *data);

void kfd_signal_reset_event(struct kfd_node *dev);

void kfd_signal_poison_consumed_event(struct kfd_node *dev, u32 pasid);

static inline void kfd_flush_tlb(struct kfd_process_device *pdd,
				 enum TLB_FLUSH_TYPE type)
{
	struct amdgpu_device *adev = pdd->dev->adev;
	struct amdgpu_vm *vm = drm_priv_to_vm(pdd->drm_priv);

	amdgpu_vm_flush_compute_tlb(adev, vm, type, pdd->dev->xcc_mask);
}

static inline bool kfd_flush_tlb_after_unmap(struct kfd_dev *dev)
{
	return KFD_GC_VERSION(dev) >= IP_VERSION(9, 4, 2) ||
	       (KFD_GC_VERSION(dev) == IP_VERSION(9, 4, 1) && dev->sdma_fw_version >= 18) ||
	       KFD_GC_VERSION(dev) == IP_VERSION(9, 4, 0);
}

int kfd_send_exception_to_runtime(struct kfd_process *p,
				unsigned int queue_id,
				uint64_t error_reason);
bool kfd_is_locked(void);

/* Compute profile */
void kfd_inc_compute_active(struct kfd_node *dev);
void kfd_dec_compute_active(struct kfd_node *dev);

/* Cgroup Support */
/* Check with device cgroup if @kfd device is accessible */
static inline int kfd_devcgroup_check_permission(struct kfd_node *node)
{
#if defined(CONFIG_CGROUP_DEVICE) || defined(CONFIG_CGROUP_BPF)
	struct drm_device *ddev;

	if (node->xcp)
		ddev = node->xcp->ddev;
	else
		ddev = adev_to_drm(node->adev);

	return devcgroup_check_permission(DEVCG_DEV_CHAR, DRM_MAJOR,
					  ddev->render->index,
					  DEVCG_ACC_WRITE | DEVCG_ACC_READ);
#else
	return 0;
#endif
}

static inline bool kfd_is_first_node(struct kfd_node *node)
{
	return (node == node->kfd->nodes[0]);
}

/* Debugfs */
#if defined(CONFIG_DEBUG_FS)

void kfd_debugfs_init(void);
void kfd_debugfs_fini(void);
int kfd_debugfs_mqds_by_process(struct seq_file *m, void *data);
int pqm_debugfs_mqds(struct seq_file *m, void *data);
int kfd_debugfs_hqds_by_device(struct seq_file *m, void *data);
int dqm_debugfs_hqds(struct seq_file *m, void *data);
int kfd_debugfs_rls_by_device(struct seq_file *m, void *data);
int pm_debugfs_runlist(struct seq_file *m, void *data);

int kfd_debugfs_hang_hws(struct kfd_node *dev);
int pm_debugfs_hang_hws(struct packet_manager *pm);
int dqm_debugfs_hang_hws(struct device_queue_manager *dqm);

#else

static inline void kfd_debugfs_init(void) {}
static inline void kfd_debugfs_fini(void) {}

#endif

#endif

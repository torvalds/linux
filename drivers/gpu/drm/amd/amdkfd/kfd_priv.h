/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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
#include <kgd_kfd_interface.h>

#include "amd_shared.h"

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
#define KFD_MMAP_TYPE_SHIFT	(62 - PAGE_SHIFT)
#define KFD_MMAP_TYPE_MASK	(0x3ULL << KFD_MMAP_TYPE_SHIFT)
#define KFD_MMAP_TYPE_DOORBELL	(0x3ULL << KFD_MMAP_TYPE_SHIFT)
#define KFD_MMAP_TYPE_EVENTS	(0x2ULL << KFD_MMAP_TYPE_SHIFT)
#define KFD_MMAP_TYPE_RESERVED_MEM	(0x1ULL << KFD_MMAP_TYPE_SHIFT)

#define KFD_MMAP_GPU_ID_SHIFT (46 - PAGE_SHIFT)
#define KFD_MMAP_GPU_ID_MASK (((1ULL << KFD_GPU_ID_HASH_WIDTH) - 1) \
				<< KFD_MMAP_GPU_ID_SHIFT)
#define KFD_MMAP_GPU_ID(gpu_id) ((((uint64_t)gpu_id) << KFD_MMAP_GPU_ID_SHIFT)\
				& KFD_MMAP_GPU_ID_MASK)
#define KFD_MMAP_GPU_ID_GET(offset)    ((offset & KFD_MMAP_GPU_ID_MASK) \
				>> KFD_MMAP_GPU_ID_SHIFT)

#define KFD_MMAP_OFFSET_VALUE_MASK	(0x3FFFFFFFFFFFULL >> PAGE_SHIFT)
#define KFD_MMAP_OFFSET_VALUE_GET(offset) (offset & KFD_MMAP_OFFSET_VALUE_MASK)

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
 * The first page is the TBA used for the CWSR ISA code. The second
 * page is used as TMA for daisy changing a user-mode trap handler.
 */
#define KFD_CWSR_TBA_TMA_SIZE (PAGE_SIZE * 2)
#define KFD_CWSR_TMA_OFFSET PAGE_SIZE

#define KFD_MAX_NUM_OF_QUEUES_PER_DEVICE		\
	(KFD_MAX_NUM_OF_PROCESSES *			\
			KFD_MAX_NUM_OF_QUEUES_PER_PROCESS)

#define KFD_KERNEL_QUEUE_SIZE 2048

/*
 * 512 = 0x200
 * The doorbell index distance between SDMA RLC (2*i) and (2*i+1) in the
 * same SDMA engine on SOC15, which has 8-byte doorbells for SDMA.
 * 512 8-byte doorbell distance (i.e. one page away) ensures that SDMA RLC
 * (2*i+1) doorbells (in terms of the lower 12 bit address) lie exactly in
 * the OFFSET and SIZE set in registers like BIF_SDMA0_DOORBELL_RANGE.
 */
#define KFD_QUEUE_DOORBELL_MIRROR_OFFSET 512


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

/*
 * Ignore CRAT table during KFD initialization, can be used to work around
 * broken CRAT tables on some AMD systems
 */
extern int ignore_crat;

/*
 * Set sh_mem_config.retry_disable on Vega10
 */
extern int noretry;

/*
 * Halt if HWS hang is detected
 */
extern int halt_if_hws_hang;

enum cache_policy {
	cache_policy_coherent,
	cache_policy_noncoherent
};

#define KFD_IS_SOC15(chip) ((chip) >= CHIP_VEGA10)

struct kfd_event_interrupt_class {
	bool (*interrupt_isr)(struct kfd_dev *dev,
			const uint32_t *ih_ring_entry, uint32_t *patched_ihre,
			bool *patched_flag);
	void (*interrupt_wq)(struct kfd_dev *dev,
			const uint32_t *ih_ring_entry);
};

struct kfd_device_info {
	enum amd_asic_type asic_family;
	const struct kfd_event_interrupt_class *event_interrupt_class;
	unsigned int max_pasid_bits;
	unsigned int max_no_of_hqd;
	unsigned int doorbell_size;
	size_t ih_ring_entry_size;
	uint8_t num_of_watch_points;
	uint16_t mqd_size_aligned;
	bool supports_cwsr;
	bool needs_iommu_device;
	bool needs_pci_atomics;
	unsigned int num_sdma_engines;
	unsigned int num_sdma_queues_per_engine;
};

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

struct kfd_dev {
	struct kgd_dev *kgd;

	const struct kfd_device_info *device_info;
	struct pci_dev *pdev;

	unsigned int id;		/* topology stub index */

	phys_addr_t doorbell_base;	/* Start of actual doorbells used by
					 * KFD. It is aligned for mapping
					 * into user mode
					 */
	size_t doorbell_id_offset;	/* Doorbell offset (from KFD doorbell
					 * to HW doorbell, GFX reserved some
					 * at the start)
					 */
	u32 __iomem *doorbell_kernel_ptr; /* This is a pointer for a doorbells
					   * page used by kernel queue
					   */

	struct kgd2kfd_shared_resources shared_resources;
	struct kfd_vmid_info vm_info;

	const struct kfd2kgd_calls *kfd2kgd;
	struct mutex doorbell_mutex;
	DECLARE_BITMAP(doorbell_available_index,
			KFD_MAX_NUM_OF_QUEUES_PER_PROCESS);

	void *gtt_mem;
	uint64_t gtt_start_gpu_addr;
	void *gtt_start_cpu_ptr;
	void *gtt_sa_bitmap;
	struct mutex gtt_sa_lock;
	unsigned int gtt_sa_chunk_size;
	unsigned int gtt_sa_num_of_chunks;

	/* Interrupts */
	struct kfifo ih_fifo;
	struct workqueue_struct *ih_wq;
	struct work_struct interrupt_work;
	spinlock_t interrupt_lock;

	/* QCM Device instance */
	struct device_queue_manager *dqm;

	bool init_complete;
	/*
	 * Interrupts of interest to KFD are copied
	 * from the HW ring into a SW ring.
	 */
	bool interrupts_active;

	/* Debug manager */
	struct kfd_dbgmgr           *dbgmgr;

	/* Firmware versions */
	uint16_t mec_fw_version;
	uint16_t sdma_fw_version;

	/* Maximum process number mapped to HW scheduler */
	unsigned int max_proc_per_quantum;

	/* CWSR */
	bool cwsr_enabled;
	const void *cwsr_isa;
	unsigned int cwsr_isa_size;

	/* xGMI */
	uint64_t hive_id;

	bool pci_atomic_requested;

	/* SRAM ECC flag */
	atomic_t sram_ecc_flag;

	/* Compute Profile ref. count */
	atomic_t compute_profile;
};

enum kfd_mempool {
	KFD_MEMPOOL_SYSTEM_CACHEABLE = 1,
	KFD_MEMPOOL_SYSTEM_WRITECOMBINE = 2,
	KFD_MEMPOOL_FRAMEBUFFER = 3,
};

/* Character device interface */
int kfd_chardev_init(void);
void kfd_chardev_exit(void);
struct device *kfd_chardev(void);

/**
 * enum kfd_unmap_queues_filter
 *
 * @KFD_UNMAP_QUEUES_FILTER_SINGLE_QUEUE: Preempts single queue.
 *
 * @KFD_UNMAP_QUEUES_FILTER_ALL_QUEUES: Preempts all queues in the
 *						running queues list.
 *
 * @KFD_UNMAP_QUEUES_FILTER_BY_PASID: Preempts queues that belongs to
 *						specific process.
 *
 */
enum kfd_unmap_queues_filter {
	KFD_UNMAP_QUEUES_FILTER_SINGLE_QUEUE,
	KFD_UNMAP_QUEUES_FILTER_ALL_QUEUES,
	KFD_UNMAP_QUEUES_FILTER_DYNAMIC_QUEUES,
	KFD_UNMAP_QUEUES_FILTER_BY_PASID
};

/**
 * enum kfd_queue_type
 *
 * @KFD_QUEUE_TYPE_COMPUTE: Regular user mode queue type.
 *
 * @KFD_QUEUE_TYPE_SDMA: Sdma user mode queue type.
 *
 * @KFD_QUEUE_TYPE_HIQ: HIQ queue type.
 *
 * @KFD_QUEUE_TYPE_DIQ: DIQ queue type.
 */
enum kfd_queue_type  {
	KFD_QUEUE_TYPE_COMPUTE,
	KFD_QUEUE_TYPE_SDMA,
	KFD_QUEUE_TYPE_HIQ,
	KFD_QUEUE_TYPE_DIQ
};

enum kfd_queue_format {
	KFD_QUEUE_FORMAT_PM4,
	KFD_QUEUE_FORMAT_AQL
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
 * @doorbell_ptr: This field aim is to notify the H/W of new packet written to
 * the queue ring buffer. This field should be similar to write_ptr and the
 * user should update this field after he updated the write_ptr.
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
	uint32_t *read_ptr;
	uint32_t *write_ptr;
	void __iomem *doorbell_ptr;
	uint32_t doorbell_off;
	bool is_interop;
	bool is_evicted;
	bool is_active;
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
	/* Relevant for CU */
	uint32_t cu_mask_count; /* Must be a multiple of 32 */
	uint32_t *cu_mask;
};

/**
 * struct queue
 *
 * @list: Queue linked list.
 *
 * @mqd: The queue MQD.
 *
 * @mqd_mem_obj: The MQD local gpu memory object.
 *
 * @gart_mqd_addr: The MQD gart mc address.
 *
 * @properties: The queue properties.
 *
 * @mec: Used only in no cp scheduling mode and identifies to micro engine id
 *	 that the queue should be execute on.
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
	struct kfd_dev		*device;
};

/*
 * Please read the kfd_mqd_manager.h description.
 */
enum KFD_MQD_TYPE {
	KFD_MQD_TYPE_COMPUTE = 0,	/* for no cp scheduling */
	KFD_MQD_TYPE_HIQ,		/* for hiq */
	KFD_MQD_TYPE_CP,		/* for cp queues and diq */
	KFD_MQD_TYPE_SDMA,		/* for sdma queues */
	KFD_MQD_TYPE_MAX
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

	/*
	 * All the memory management data should be here too
	 */
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
	void *cwsr_kaddr;
	uint64_t cwsr_base;
	uint64_t tba_addr;
	uint64_t tma_addr;

	/* IB memory */
	uint64_t ib_base;
	void *ib_kaddr;

	/* doorbell resources per process per device */
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

/* Data that is per-process-per device. */
struct kfd_process_device {
	/*
	 * List of all per-device data for a process.
	 * Starts from kfd_process.per_device_data.
	 */
	struct list_head per_device_list;

	/* The device that owns this data. */
	struct kfd_dev *dev;

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
	void *vm;

	/* GPUVM allocations storage */
	struct idr alloc_idr;

	/* Flag used to tell the pdd has dequeued from the dqm.
	 * This is used to prevent dev->dqm->ops.process_termination() from
	 * being called twice when it is already called in IOMMU callback
	 * function.
	 */
	bool already_dequeued;

	/* Is this process/pasid bound to this device? (amd_iommu_bind_pasid) */
	enum kfd_pdd_bound bound;
};

#define qpd_to_pdd(x) container_of(x, struct kfd_process_device, qpd)

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

	/* Use for delayed freeing of kfd_process structure */
	struct rcu_head	rcu;

	unsigned int pasid;
	unsigned int doorbell_index;

	/*
	 * List of kfd_process_device structures,
	 * one for each device the process is using.
	 */
	struct list_head per_device_data;

	struct process_queue_manager pqm;

	/*Is the user space process 32 bit?*/
	bool is_32bit_user_mode;

	/* Event-related data */
	struct mutex event_mutex;
	/* Event ID allocator and lookup */
	struct idr event_idr;
	/* Event page */
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
	struct dma_fence *ef;

	/* Work items for evicting and restoring BOs */
	struct delayed_work eviction_work;
	struct delayed_work restore_work;
	/* seqno of the last scheduled eviction */
	unsigned int last_eviction_seqno;
	/* Approx. the last timestamp (in jiffies) when the process was
	 * restored after an eviction
	 */
	unsigned long last_restore_timestamp;
};

#define KFD_PROCESS_TABLE_SIZE 5 /* bits: 32 entries */
extern DECLARE_HASHTABLE(kfd_processes_table, KFD_PROCESS_TABLE_SIZE);
extern struct srcu_struct kfd_processes_srcu;

/**
 * Ioctl function type.
 *
 * \param filep pointer to file structure.
 * \param p amdkfd process pointer.
 * \param data pointer to arg that was copied from user.
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
bool kfd_dev_is_large_bar(struct kfd_dev *dev);

int kfd_process_create_wq(void);
void kfd_process_destroy_wq(void);
struct kfd_process *kfd_create_process(struct file *filep);
struct kfd_process *kfd_get_process(const struct task_struct *);
struct kfd_process *kfd_lookup_process_by_pasid(unsigned int pasid);
struct kfd_process *kfd_lookup_process_by_mm(const struct mm_struct *mm);
void kfd_unref_process(struct kfd_process *p);
int kfd_process_evict_queues(struct kfd_process *p);
int kfd_process_restore_queues(struct kfd_process *p);
void kfd_suspend_all_processes(void);
int kfd_resume_all_processes(void);

int kfd_process_device_init_vm(struct kfd_process_device *pdd,
			       struct file *drm_file);
struct kfd_process_device *kfd_bind_process_to_device(struct kfd_dev *dev,
						struct kfd_process *p);
struct kfd_process_device *kfd_get_process_device_data(struct kfd_dev *dev,
							struct kfd_process *p);
struct kfd_process_device *kfd_create_process_device_data(struct kfd_dev *dev,
							struct kfd_process *p);

int kfd_reserved_mem_mmap(struct kfd_dev *dev, struct kfd_process *process,
			  struct vm_area_struct *vma);

/* KFD process API for creating and translating handles */
int kfd_process_device_create_obj_handle(struct kfd_process_device *pdd,
					void *mem);
void *kfd_process_device_translate_handle(struct kfd_process_device *p,
					int handle);
void kfd_process_device_remove_obj_handle(struct kfd_process_device *pdd,
					int handle);

/* Process device data iterator */
struct kfd_process_device *kfd_get_first_process_device_data(
							struct kfd_process *p);
struct kfd_process_device *kfd_get_next_process_device_data(
						struct kfd_process *p,
						struct kfd_process_device *pdd);
bool kfd_has_process_device_data(struct kfd_process *p);

/* PASIDs */
int kfd_pasid_init(void);
void kfd_pasid_exit(void);
bool kfd_set_pasid_limit(unsigned int new_limit);
unsigned int kfd_get_pasid_limit(void);
unsigned int kfd_pasid_alloc(void);
void kfd_pasid_free(unsigned int pasid);

/* Doorbells */
size_t kfd_doorbell_process_slice(struct kfd_dev *kfd);
int kfd_doorbell_init(struct kfd_dev *kfd);
void kfd_doorbell_fini(struct kfd_dev *kfd);
int kfd_doorbell_mmap(struct kfd_dev *dev, struct kfd_process *process,
		      struct vm_area_struct *vma);
void __iomem *kfd_get_kernel_doorbell(struct kfd_dev *kfd,
					unsigned int *doorbell_off);
void kfd_release_kernel_doorbell(struct kfd_dev *kfd, u32 __iomem *db_addr);
u32 read_kernel_doorbell(u32 __iomem *db);
void write_kernel_doorbell(void __iomem *db, u32 value);
void write_kernel_doorbell64(void __iomem *db, u64 value);
unsigned int kfd_doorbell_id_to_offset(struct kfd_dev *kfd,
					struct kfd_process *process,
					unsigned int doorbell_id);
phys_addr_t kfd_get_process_doorbells(struct kfd_dev *dev,
					struct kfd_process *process);
int kfd_alloc_process_doorbells(struct kfd_process *process);
void kfd_free_process_doorbells(struct kfd_process *process);

/* GTT Sub-Allocator */

int kfd_gtt_sa_allocate(struct kfd_dev *kfd, unsigned int size,
			struct kfd_mem_obj **mem_obj);

int kfd_gtt_sa_free(struct kfd_dev *kfd, struct kfd_mem_obj *mem_obj);

extern struct device *kfd_device;

/* Topology */
int kfd_topology_init(void);
void kfd_topology_shutdown(void);
int kfd_topology_add_device(struct kfd_dev *gpu);
int kfd_topology_remove_device(struct kfd_dev *gpu);
struct kfd_topology_device *kfd_topology_device_by_proximity_domain(
						uint32_t proximity_domain);
struct kfd_topology_device *kfd_topology_device_by_id(uint32_t gpu_id);
struct kfd_dev *kfd_device_by_id(uint32_t gpu_id);
struct kfd_dev *kfd_device_by_pci_dev(const struct pci_dev *pdev);
struct kfd_dev *kfd_device_by_kgd(const struct kgd_dev *kgd);
int kfd_topology_enum_kfd_devices(uint8_t idx, struct kfd_dev **kdev);
int kfd_numa_node_to_apic_id(int numa_node_id);

/* Interrupts */
int kfd_interrupt_init(struct kfd_dev *dev);
void kfd_interrupt_exit(struct kfd_dev *dev);
bool enqueue_ih_ring_entry(struct kfd_dev *kfd,	const void *ih_ring_entry);
bool interrupt_is_wanted(struct kfd_dev *dev,
				const uint32_t *ih_ring_entry,
				uint32_t *patched_ihre, bool *flag);

/* amdkfd Apertures */
int kfd_init_apertures(struct kfd_process *process);

/* Queue Context Management */
int init_queue(struct queue **q, const struct queue_properties *properties);
void uninit_queue(struct queue *q);
void print_queue_properties(struct queue_properties *q);
void print_queue(struct queue *q);

struct mqd_manager *mqd_manager_init(enum KFD_MQD_TYPE type,
					struct kfd_dev *dev);
struct mqd_manager *mqd_manager_init_cik(enum KFD_MQD_TYPE type,
		struct kfd_dev *dev);
struct mqd_manager *mqd_manager_init_cik_hawaii(enum KFD_MQD_TYPE type,
		struct kfd_dev *dev);
struct mqd_manager *mqd_manager_init_vi(enum KFD_MQD_TYPE type,
		struct kfd_dev *dev);
struct mqd_manager *mqd_manager_init_vi_tonga(enum KFD_MQD_TYPE type,
		struct kfd_dev *dev);
struct mqd_manager *mqd_manager_init_v9(enum KFD_MQD_TYPE type,
		struct kfd_dev *dev);
struct device_queue_manager *device_queue_manager_init(struct kfd_dev *dev);
void device_queue_manager_uninit(struct device_queue_manager *dqm);
struct kernel_queue *kernel_queue_init(struct kfd_dev *dev,
					enum kfd_queue_type type);
void kernel_queue_uninit(struct kernel_queue *kq);
int kfd_process_vm_fault(struct device_queue_manager *dqm, unsigned int pasid);

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
			    struct kfd_dev *dev,
			    struct file *f,
			    struct queue_properties *properties,
			    unsigned int *qid);
int pqm_destroy_queue(struct process_queue_manager *pqm, unsigned int qid);
int pqm_update_queue(struct process_queue_manager *pqm, unsigned int qid,
			struct queue_properties *p);
int pqm_set_cu_mask(struct process_queue_manager *pqm, unsigned int qid,
			struct queue_properties *p);
struct kernel_queue *pqm_get_kernel_queue(struct process_queue_manager *pqm,
						unsigned int qid);
int pqm_get_wave_state(struct process_queue_manager *pqm,
		       unsigned int qid,
		       void __user *ctl_stack,
		       u32 *ctl_stack_used_size,
		       u32 *save_area_used_size);

int amdkfd_fence_wait_timeout(unsigned int *fence_addr,
				unsigned int fence_value,
				unsigned int timeout_ms);

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
			enum kfd_queue_type type,
			enum kfd_unmap_queues_filter mode,
			uint32_t filter_param, bool reset,
			unsigned int sdma_engine);
	int (*query_status)(struct packet_manager *pm, uint32_t *buffer,
			uint64_t fence_address,	uint32_t fence_value);
	int (*release_mem)(uint64_t gpu_addr, uint32_t *buffer);

	/* Packet sizes */
	int map_process_size;
	int runlist_size;
	int set_resources_size;
	int map_queues_size;
	int unmap_queues_size;
	int query_status_size;
	int release_mem_size;
};

extern const struct packet_manager_funcs kfd_vi_pm_funcs;
extern const struct packet_manager_funcs kfd_v9_pm_funcs;

int pm_init(struct packet_manager *pm, struct device_queue_manager *dqm);
void pm_uninit(struct packet_manager *pm);
int pm_send_set_resources(struct packet_manager *pm,
				struct scheduling_resources *res);
int pm_send_runlist(struct packet_manager *pm, struct list_head *dqm_queues);
int pm_send_query_status(struct packet_manager *pm, uint64_t fence_address,
				uint32_t fence_value);

int pm_send_unmap_queue(struct packet_manager *pm, enum kfd_queue_type type,
			enum kfd_unmap_queues_filter mode,
			uint32_t filter_param, bool reset,
			unsigned int sdma_engine);

void pm_release_ib(struct packet_manager *pm);

/* Following PM funcs can be shared among VI and AI */
unsigned int pm_build_pm4_header(unsigned int opcode, size_t packet_size);
int pm_set_resources_vi(struct packet_manager *pm, uint32_t *buffer,
				struct scheduling_resources *res);

uint64_t kfd_get_number_elems(struct kfd_dev *kfd);

/* Events */
extern const struct kfd_event_interrupt_class event_interrupt_class_cik;
extern const struct kfd_event_interrupt_class event_interrupt_class_v9;

extern const struct kfd_device_global_init_class device_global_init_class_cik;

void kfd_event_init_process(struct kfd_process *p);
void kfd_event_free_process(struct kfd_process *p);
int kfd_event_mmap(struct kfd_process *process, struct vm_area_struct *vma);
int kfd_wait_on_events(struct kfd_process *p,
		       uint32_t num_events, void __user *data,
		       bool all, uint32_t user_timeout_ms,
		       uint32_t *wait_result);
void kfd_signal_event_interrupt(unsigned int pasid, uint32_t partial_id,
				uint32_t valid_id_bits);
void kfd_signal_iommu_event(struct kfd_dev *dev,
		unsigned int pasid, unsigned long address,
		bool is_write_requested, bool is_execute_requested);
void kfd_signal_hw_exception_event(unsigned int pasid);
int kfd_set_event(struct kfd_process *p, uint32_t event_id);
int kfd_reset_event(struct kfd_process *p, uint32_t event_id);
int kfd_event_page_set(struct kfd_process *p, void *kernel_address,
		       uint64_t size);
int kfd_event_create(struct file *devkfd, struct kfd_process *p,
		     uint32_t event_type, bool auto_reset, uint32_t node_id,
		     uint32_t *event_id, uint32_t *event_trigger_data,
		     uint64_t *event_page_offset, uint32_t *event_slot_index);
int kfd_event_destroy(struct kfd_process *p, uint32_t event_id);

void kfd_signal_vm_fault_event(struct kfd_dev *dev, unsigned int pasid,
				struct kfd_vm_fault_info *info);

void kfd_signal_reset_event(struct kfd_dev *dev);

void kfd_flush_tlb(struct kfd_process_device *pdd);

int dbgdev_wave_reset_wavefronts(struct kfd_dev *dev, struct kfd_process *p);

bool kfd_is_locked(void);

/* Compute profile */
void kfd_inc_compute_active(struct kfd_dev *dev);
void kfd_dec_compute_active(struct kfd_dev *dev);

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

int kfd_debugfs_hang_hws(struct kfd_dev *dev);
int pm_debugfs_hang_hws(struct packet_manager *pm);
int dqm_debugfs_execute_queues(struct device_queue_manager *dqm);

#else

static inline void kfd_debugfs_init(void) {}
static inline void kfd_debugfs_fini(void) {}

#endif

#endif

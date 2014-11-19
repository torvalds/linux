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
#include <kgd_kfd_interface.h>

#define KFD_SYSFS_FILE_MODE 0444

/*
 * When working with cp scheduler we should assign the HIQ manually or via
 * the radeon driver to a fixed hqd slot, here are the fixed HIQ hqd slot
 * definitions for Kaveri. In Kaveri only the first ME queues participates
 * in the cp scheduling taking that in mind we set the HIQ slot in the
 * second ME.
 */
#define KFD_CIK_HIQ_PIPE 4
#define KFD_CIK_HIQ_QUEUE 0

/* GPU ID hash width in bits */
#define KFD_GPU_ID_HASH_WIDTH 16

/* Macro for allocating structures */
#define kfd_alloc_struct(ptr_to_struct)	\
	((typeof(ptr_to_struct)) kzalloc(sizeof(*ptr_to_struct), GFP_KERNEL))

/* Kernel module parameter to specify maximum number of supported processes */
extern int max_num_of_processes;

#define KFD_MAX_NUM_OF_PROCESSES_DEFAULT 32
#define KFD_MAX_NUM_OF_PROCESSES 512

/*
 * Kernel module parameter to specify maximum number of supported queues
 * per process
 */
extern int max_num_of_queues_per_process;

#define KFD_MAX_NUM_OF_QUEUES_PER_PROCESS_DEFAULT 128
#define KFD_MAX_NUM_OF_QUEUES_PER_PROCESS 1024

#define KFD_KERNEL_QUEUE_SIZE 2048

/* Kernel module parameter to specify the scheduling policy */
extern int sched_policy;

/**
 * enum kfd_sched_policy
 *
 * @KFD_SCHED_POLICY_HWS: H/W scheduling policy known as command processor (cp)
 * scheduling. In this scheduling mode we're using the firmware code to
 * schedule the user mode queues and kernel queues such as HIQ and DIQ.
 * the HIQ queue is used as a special queue that dispatches the configuration
 * to the cp and the user mode queues list that are currently running.
 * the DIQ queue is a debugging queue that dispatches debugging commands to the
 * firmware.
 * in this scheduling mode user mode queues over subscription feature is
 * enabled.
 *
 * @KFD_SCHED_POLICY_HWS_NO_OVERSUBSCRIPTION: The same as above but the over
 * subscription feature disabled.
 *
 * @KFD_SCHED_POLICY_NO_HWS: no H/W scheduling policy is a mode which directly
 * set the command processor registers and sets the queues "manually". This
 * mode is used *ONLY* for debugging proposes.
 *
 */
enum kfd_sched_policy {
	KFD_SCHED_POLICY_HWS = 0,
	KFD_SCHED_POLICY_HWS_NO_OVERSUBSCRIPTION,
	KFD_SCHED_POLICY_NO_HWS
};

enum cache_policy {
	cache_policy_coherent,
	cache_policy_noncoherent
};

struct kfd_device_info {
	unsigned int max_pasid_bits;
	size_t ih_ring_entry_size;
	uint16_t mqd_size_aligned;
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
	size_t doorbell_process_limit;	/* Number of processes we have doorbell
					 * space for.
					 */
	u32 __iomem *doorbell_kernel_ptr; /* This is a pointer for a doorbells
					   * page used by kernel queue
					   */

	struct kgd2kfd_shared_resources shared_resources;

	void *interrupt_ring;
	size_t interrupt_ring_size;
	atomic_t interrupt_ring_rptr;
	atomic_t interrupt_ring_wptr;
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
};

/* KGD2KFD callbacks */
void kgd2kfd_exit(void);
struct kfd_dev *kgd2kfd_probe(struct kgd_dev *kgd, struct pci_dev *pdev);
bool kgd2kfd_device_init(struct kfd_dev *kfd,
			 const struct kgd2kfd_shared_resources *gpu_resources);
void kgd2kfd_device_exit(struct kfd_dev *kfd);

extern const struct kfd2kgd_calls *kfd2kgd;

struct kfd_mem_obj {
	void *bo;
	uint64_t gpu_addr;
	uint32_t *cpu_ptr;
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
 * enum kfd_preempt_type_filter
 *
 * @KFD_PREEMPT_TYPE_FILTER_SINGLE_QUEUE: Preempts single queue.
 *
 * @KFD_PRERMPT_TYPE_FILTER_ALL_QUEUES: Preempts all queues in the
 *						running queues list.
 *
 * @KFD_PRERMPT_TYPE_FILTER_BY_PASID: Preempts queues that belongs to
 *						specific process.
 *
 */
enum kfd_preempt_type_filter {
	KFD_PREEMPT_TYPE_FILTER_SINGLE_QUEUE,
	KFD_PREEMPT_TYPE_FILTER_ALL_QUEUES,
	KFD_PREEMPT_TYPE_FILTER_BY_PASID
};

enum kfd_preempt_type {
	KFD_PREEMPT_TYPE_WAVEFRONT,
	KFD_PREEMPT_TYPE_WAVEFRONT_RESET
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
 * the queue ring buffer. This field should be similar to write_ptr and the user
 * should update this field after he updated the write_ptr.
 *
 * @doorbell_off: The doorbell offset in the doorbell pci-bar.
 *
 * @is_interop: Defines if this is a interop queue. Interop queue means that the
 * queue can access both graphics and compute resources.
 *
 * @is_active: Defines if the queue is active or not.
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
	uint32_t __iomem *doorbell_ptr;
	uint32_t doorbell_off;
	bool is_interop;
	bool is_active;
	/* Not relevant for user mode queues in cp scheduling */
	unsigned int vmid;
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
 * that the queue should be execute on.
 *
 * @pipe: Used only in no cp scheduling mode and identifies the queue's pipe id.
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

	struct kfd_process	*process;
	struct kfd_dev		*device;
};

/*
 * Please read the kfd_mqd_manager.h description.
 */
enum KFD_MQD_TYPE {
	KFD_MQD_TYPE_CIK_COMPUTE = 0, /* for no cp scheduling */
	KFD_MQD_TYPE_CIK_HIQ, /* for hiq */
	KFD_MQD_TYPE_CIK_CP, /* for cp queues and diq */
	KFD_MQD_TYPE_CIK_SDMA, /* for sdma queues */
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
	unsigned int		num_concurrent_processes;
	struct list_head	queues;
	unsigned long		*queue_slot_bitmap;
};

struct qcm_process_device {
	/* The Device Queue Manager that owns this data */
	struct device_queue_manager *dqm;
	struct process_queue_manager *pqm;
	/* Device Queue Manager lock */
	struct mutex *lock;
	/* Queues list */
	struct list_head queues_list;
	struct list_head priv_queue_list;

	unsigned int queue_count;
	unsigned int vmid;
	bool is_debug;
	/*
	 * All the memory management data should be here too
	 */
	uint64_t gds_context_area;
	uint32_t sh_mem_config;
	uint32_t sh_mem_bases;
	uint32_t sh_mem_ape1_base;
	uint32_t sh_mem_ape1_limit;
	uint32_t page_table_base;
	uint32_t gds_size;
	uint32_t num_gws;
	uint32_t num_oac;
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


	/* per-process-per device QCM data structure */
	struct qcm_process_device qpd;

	/*Apertures*/
	uint64_t lds_base;
	uint64_t lds_limit;
	uint64_t gpuvm_base;
	uint64_t gpuvm_limit;
	uint64_t scratch_base;
	uint64_t scratch_limit;

	/* Is this process/pasid bound to this device? (amd_iommu_bind_pasid) */
	bool bound;
};

#define qpd_to_pdd(x) container_of(x, struct kfd_process_device, qpd)

/* Process data */
struct kfd_process {
	/*
	 * kfd_process are stored in an mm_struct*->kfd_process*
	 * hash table (kfd_processes in kfd_process.c)
	 */
	struct hlist_node kfd_processes;

	struct mm_struct *mm;

	struct mutex mutex;

	/*
	 * In any process, the thread that started main() is the lead
	 * thread and outlives the rest.
	 * It is here because amd_iommu_bind_pasid wants a task_struct.
	 */
	struct task_struct *lead_thread;

	/* We want to receive a notification when the mm_struct is destroyed */
	struct mmu_notifier mmu_notifier;

	/* Use for delayed freeing of kfd_process structure */
	struct rcu_head	rcu;

	unsigned int pasid;

	/*
	 * List of kfd_process_device structures,
	 * one for each device the process is using.
	 */
	struct list_head per_device_data;

	struct process_queue_manager pqm;

	/* The process's queues. */
	size_t queue_array_size;

	/* Size is queue_array_size, up to MAX_PROCESS_QUEUES. */
	struct kfd_queue **queues;

	unsigned long allocated_queue_bitmap[DIV_ROUND_UP(KFD_MAX_NUM_OF_QUEUES_PER_PROCESS, BITS_PER_LONG)];

	/*Is the user space process 32 bit?*/
	bool is_32bit_user_mode;
};

void kfd_process_create_wq(void);
void kfd_process_destroy_wq(void);
struct kfd_process *kfd_create_process(const struct task_struct *);
struct kfd_process *kfd_get_process(const struct task_struct *);

struct kfd_process_device *kfd_bind_process_to_device(struct kfd_dev *dev,
							struct kfd_process *p);
void kfd_unbind_process_from_device(struct kfd_dev *dev, unsigned int pasid);
struct kfd_process_device *kfd_get_process_device_data(struct kfd_dev *dev,
							struct kfd_process *p,
							int create_pdd);

/* Process device data iterator */
struct kfd_process_device *kfd_get_first_process_device_data(struct kfd_process *p);
struct kfd_process_device *kfd_get_next_process_device_data(struct kfd_process *p,
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
void kfd_doorbell_init(struct kfd_dev *kfd);
int kfd_doorbell_mmap(struct kfd_process *process, struct vm_area_struct *vma);
u32 __iomem *kfd_get_kernel_doorbell(struct kfd_dev *kfd,
					unsigned int *doorbell_off);
void kfd_release_kernel_doorbell(struct kfd_dev *kfd, u32 __iomem *db_addr);
u32 read_kernel_doorbell(u32 __iomem *db);
void write_kernel_doorbell(u32 __iomem *db, u32 value);
unsigned int kfd_queue_id_to_doorbell(struct kfd_dev *kfd,
					struct kfd_process *process,
					unsigned int queue_id);

extern struct device *kfd_device;

/* Topology */
int kfd_topology_init(void);
void kfd_topology_shutdown(void);
int kfd_topology_add_device(struct kfd_dev *gpu);
int kfd_topology_remove_device(struct kfd_dev *gpu);
struct kfd_dev *kfd_device_by_id(uint32_t gpu_id);
struct kfd_dev *kfd_device_by_pci_dev(const struct pci_dev *pdev);
struct kfd_dev *kfd_topology_enum_kfd_devices(uint8_t idx);

/* Interrupts */
int kfd_interrupt_init(struct kfd_dev *dev);
void kfd_interrupt_exit(struct kfd_dev *dev);
void kgd2kfd_interrupt(struct kfd_dev *kfd, const void *ih_ring_entry);
bool enqueue_ih_ring_entry(struct kfd_dev *kfd,	const void *ih_ring_entry);

/* Power Management */
void kgd2kfd_suspend(struct kfd_dev *kfd);
int kgd2kfd_resume(struct kfd_dev *kfd);

/* amdkfd Apertures */
int kfd_init_apertures(struct kfd_process *process);

/* Queue Context Management */
inline uint32_t lower_32(uint64_t x);
inline uint32_t upper_32(uint64_t x);

int init_queue(struct queue **q, struct queue_properties properties);
void uninit_queue(struct queue *q);
void print_queue_properties(struct queue_properties *q);
void print_queue(struct queue *q);

struct mqd_manager *mqd_manager_init(enum KFD_MQD_TYPE type,
					struct kfd_dev *dev);
struct device_queue_manager *device_queue_manager_init(struct kfd_dev *dev);
void device_queue_manager_uninit(struct device_queue_manager *dqm);
struct kernel_queue *kernel_queue_init(struct kfd_dev *dev,
					enum kfd_queue_type type);
void kernel_queue_uninit(struct kernel_queue *kq);

/* Process Queue Manager */
struct process_queue_node {
	struct queue *q;
	struct kernel_queue *kq;
	struct list_head process_queue_list;
};

int pqm_init(struct process_queue_manager *pqm, struct kfd_process *p);
void pqm_uninit(struct process_queue_manager *pqm);
int pqm_create_queue(struct process_queue_manager *pqm,
			    struct kfd_dev *dev,
			    struct file *f,
			    struct queue_properties *properties,
			    unsigned int flags,
			    enum kfd_queue_type type,
			    unsigned int *qid);
int pqm_destroy_queue(struct process_queue_manager *pqm, unsigned int qid);
int pqm_update_queue(struct process_queue_manager *pqm, unsigned int qid,
			struct queue_properties *p);

/* Packet Manager */

#define KFD_HIQ_TIMEOUT (500)

#define KFD_FENCE_COMPLETED (100)
#define KFD_FENCE_INIT   (10)
#define KFD_UNMAP_LATENCY (150)

struct packet_manager {
	struct device_queue_manager *dqm;
	struct kernel_queue *priv_queue;
	struct mutex lock;
	bool allocated;
	struct kfd_mem_obj *ib_buffer_obj;
};

int pm_init(struct packet_manager *pm, struct device_queue_manager *dqm);
void pm_uninit(struct packet_manager *pm);
int pm_send_set_resources(struct packet_manager *pm,
				struct scheduling_resources *res);
int pm_send_runlist(struct packet_manager *pm, struct list_head *dqm_queues);
int pm_send_query_status(struct packet_manager *pm, uint64_t fence_address,
				uint32_t fence_value);

int pm_send_unmap_queue(struct packet_manager *pm, enum kfd_queue_type type,
			enum kfd_preempt_type_filter mode,
			uint32_t filter_param, bool reset,
			unsigned int sdma_engine);

void pm_release_ib(struct packet_manager *pm);

uint64_t kfd_get_number_elems(struct kfd_dev *kfd);
phys_addr_t kfd_get_process_doorbells(struct kfd_dev *dev,
					struct kfd_process *process);

#endif

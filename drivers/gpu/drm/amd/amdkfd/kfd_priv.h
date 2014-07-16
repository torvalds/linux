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

	bool init_complete;

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


/* Data that is per-process-per device. */
struct kfd_process_device {
	/*
	 * List of all per-device data for a process.
	 * Starts from kfd_process.per_device_data.
	 */
	struct list_head per_device_list;

	/* The device that owns this data. */
	struct kfd_dev *dev;


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

void kfd_unbind_process_from_device(struct kfd_dev *dev, unsigned int pasid);
struct kfd_process_device *kfd_get_process_device_data(struct kfd_dev *dev,
							struct kfd_process *p,
							int create_pdd);

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
void kgd2kfd_interrupt(struct kfd_dev *dev, const void *ih_ring_entry);

/* Power Management */
void kgd2kfd_suspend(struct kfd_dev *dev);
int kgd2kfd_resume(struct kfd_dev *dev);

/* amdkfd Apertures */
int kfd_init_apertures(struct kfd_process *process);

uint64_t kfd_get_number_elems(struct kfd_dev *kfd);
phys_addr_t kfd_get_process_doorbells(struct kfd_dev *dev,
					struct kfd_process *process);

#endif

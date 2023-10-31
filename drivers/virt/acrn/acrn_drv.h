/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ACRN_HSM_DRV_H
#define __ACRN_HSM_DRV_H

#include <linux/acrn.h>
#include <linux/dev_printk.h>
#include <linux/miscdevice.h>
#include <linux/types.h>

#include "hypercall.h"

extern struct miscdevice acrn_dev;

#define ACRN_NAME_LEN		16
#define ACRN_MEM_MAPPING_MAX	256

#define ACRN_MEM_REGION_ADD	0
#define ACRN_MEM_REGION_DEL	2

struct acrn_vm;
struct acrn_ioreq_client;

/**
 * struct vm_memory_region_op - Hypervisor memory operation
 * @type:		Operation type (ACRN_MEM_REGION_*)
 * @attr:		Memory attribute (ACRN_MEM_TYPE_* | ACRN_MEM_ACCESS_*)
 * @user_vm_pa:		Physical address of User VM to be mapped.
 * @service_vm_pa:	Physical address of Service VM to be mapped.
 * @size:		Size of this region.
 *
 * Structure containing needed information that is provided to ACRN Hypervisor
 * to manage the EPT mappings of a single memory region of the User VM. Several
 * &struct vm_memory_region_op can be batched to ACRN Hypervisor, see &struct
 * vm_memory_region_batch.
 */
struct vm_memory_region_op {
	u32	type;
	u32	attr;
	u64	user_vm_pa;
	u64	service_vm_pa;
	u64	size;
};

/**
 * struct vm_memory_region_batch - A batch of vm_memory_region_op.
 * @vmid:		A User VM ID.
 * @reserved:		Reserved.
 * @regions_num:	The number of vm_memory_region_op.
 * @regions_gpa:	Physical address of a vm_memory_region_op array.
 * @regions_op:		Flexible array of vm_memory_region_op.
 *
 * HC_VM_SET_MEMORY_REGIONS uses this structure to manage EPT mappings of
 * multiple memory regions of a User VM. A &struct vm_memory_region_batch
 * contains multiple &struct vm_memory_region_op for batch processing in the
 * ACRN Hypervisor.
 */
struct vm_memory_region_batch {
	u16			   vmid;
	u16			   reserved[3];
	u32			   regions_num;
	u64			   regions_gpa;
	struct vm_memory_region_op regions_op[] __counted_by(regions_num);
};

/**
 * struct vm_memory_mapping - Memory map between a User VM and the Service VM
 * @pages:		Pages in Service VM kernel.
 * @npages:		Number of pages.
 * @service_vm_va:	Virtual address in Service VM kernel.
 * @user_vm_pa:		Physical address in User VM.
 * @size:		Size of this memory region.
 *
 * HSM maintains memory mappings between a User VM GPA and the Service VM
 * kernel VA for accelerating the User VM GPA translation.
 */
struct vm_memory_mapping {
	struct page	**pages;
	int		npages;
	void		*service_vm_va;
	u64		user_vm_pa;
	size_t		size;
};

/**
 * struct acrn_ioreq_buffer - Data for setting the ioreq buffer of User VM
 * @ioreq_buf:	The GPA of the IO request shared buffer of a VM
 *
 * The parameter for the HC_SET_IOREQ_BUFFER hypercall used to set up
 * the shared I/O request buffer between Service VM and ACRN hypervisor.
 */
struct acrn_ioreq_buffer {
	u64	ioreq_buf;
};

struct acrn_ioreq_range {
	struct list_head	list;
	u32			type;
	u64			start;
	u64			end;
};

#define ACRN_IOREQ_CLIENT_DESTROYING	0U
typedef	int (*ioreq_handler_t)(struct acrn_ioreq_client *client,
			       struct acrn_io_request *req);
/**
 * struct acrn_ioreq_client - Structure of I/O client.
 * @name:	Client name
 * @vm:		The VM that the client belongs to
 * @list:	List node for this acrn_ioreq_client
 * @is_default:	If this client is the default one
 * @flags:	Flags (ACRN_IOREQ_CLIENT_*)
 * @range_list:	I/O ranges
 * @range_lock:	Lock to protect range_list
 * @ioreqs_map:	The pending I/O requests bitmap.
 * @handler:	I/O requests handler of this client
 * @thread:	The thread which executes the handler
 * @wq:		The wait queue for the handler thread parking
 * @priv:	Data for the thread
 */
struct acrn_ioreq_client {
	char			name[ACRN_NAME_LEN];
	struct acrn_vm		*vm;
	struct list_head	list;
	bool			is_default;
	unsigned long		flags;
	struct list_head	range_list;
	rwlock_t		range_lock;
	DECLARE_BITMAP(ioreqs_map, ACRN_IO_REQUEST_MAX);
	ioreq_handler_t		handler;
	struct task_struct	*thread;
	wait_queue_head_t	wq;
	void			*priv;
};

#define ACRN_INVALID_VMID (0xffffU)

#define ACRN_VM_FLAG_DESTROYED		0U
#define ACRN_VM_FLAG_CLEARING_IOREQ	1U
extern struct list_head acrn_vm_list;
extern rwlock_t acrn_vm_list_lock;
/**
 * struct acrn_vm - Properties of ACRN User VM.
 * @list:			Entry within global list of all VMs.
 * @vmid:			User VM ID.
 * @vcpu_num:			Number of virtual CPUs in the VM.
 * @flags:			Flags (ACRN_VM_FLAG_*) of the VM. This is VM
 *				flag management in HSM which is different
 *				from the &acrn_vm_creation.vm_flag.
 * @regions_mapping_lock:	Lock to protect &acrn_vm.regions_mapping and
 *				&acrn_vm.regions_mapping_count.
 * @regions_mapping:		Memory mappings of this VM.
 * @regions_mapping_count:	Number of memory mapping of this VM.
 * @ioreq_clients_lock:		Lock to protect ioreq_clients and default_client
 * @ioreq_clients:		The I/O request clients list of this VM
 * @default_client:		The default I/O request client
 * @ioreq_buf:			I/O request shared buffer
 * @ioreq_page:			The page of the I/O request shared buffer
 * @pci_conf_addr:		Address of a PCI configuration access emulation
 * @monitor_page:		Page of interrupt statistics of User VM
 * @ioeventfds_lock:		Lock to protect ioeventfds list
 * @ioeventfds:			List to link all hsm_ioeventfd
 * @ioeventfd_client:		I/O client for ioeventfds of the VM
 * @irqfds_lock:		Lock to protect irqfds list
 * @irqfds:			List to link all hsm_irqfd
 * @irqfd_wq:			Workqueue for irqfd async shutdown
 */
struct acrn_vm {
	struct list_head		list;
	u16				vmid;
	int				vcpu_num;
	unsigned long			flags;
	struct mutex			regions_mapping_lock;
	struct vm_memory_mapping	regions_mapping[ACRN_MEM_MAPPING_MAX];
	int				regions_mapping_count;
	spinlock_t			ioreq_clients_lock;
	struct list_head		ioreq_clients;
	struct acrn_ioreq_client	*default_client;
	struct acrn_io_request_buffer	*ioreq_buf;
	struct page			*ioreq_page;
	u32				pci_conf_addr;
	struct page			*monitor_page;
	struct mutex			ioeventfds_lock;
	struct list_head		ioeventfds;
	struct acrn_ioreq_client	*ioeventfd_client;
	struct mutex			irqfds_lock;
	struct list_head		irqfds;
	struct workqueue_struct		*irqfd_wq;
};

struct acrn_vm *acrn_vm_create(struct acrn_vm *vm,
			       struct acrn_vm_creation *vm_param);
int acrn_vm_destroy(struct acrn_vm *vm);
int acrn_mm_region_add(struct acrn_vm *vm, u64 user_gpa, u64 service_gpa,
		       u64 size, u32 mem_type, u32 mem_access_right);
int acrn_mm_region_del(struct acrn_vm *vm, u64 user_gpa, u64 size);
int acrn_vm_memseg_map(struct acrn_vm *vm, struct acrn_vm_memmap *memmap);
int acrn_vm_memseg_unmap(struct acrn_vm *vm, struct acrn_vm_memmap *memmap);
int acrn_vm_ram_map(struct acrn_vm *vm, struct acrn_vm_memmap *memmap);
void acrn_vm_all_ram_unmap(struct acrn_vm *vm);

int acrn_ioreq_init(struct acrn_vm *vm, u64 buf_vma);
void acrn_ioreq_deinit(struct acrn_vm *vm);
int acrn_ioreq_intr_setup(void);
void acrn_ioreq_intr_remove(void);
void acrn_ioreq_request_clear(struct acrn_vm *vm);
int acrn_ioreq_client_wait(struct acrn_ioreq_client *client);
int acrn_ioreq_request_default_complete(struct acrn_vm *vm, u16 vcpu);
struct acrn_ioreq_client *acrn_ioreq_client_create(struct acrn_vm *vm,
						   ioreq_handler_t handler,
						   void *data, bool is_default,
						   const char *name);
void acrn_ioreq_client_destroy(struct acrn_ioreq_client *client);
int acrn_ioreq_range_add(struct acrn_ioreq_client *client,
			 u32 type, u64 start, u64 end);
void acrn_ioreq_range_del(struct acrn_ioreq_client *client,
			  u32 type, u64 start, u64 end);

int acrn_msi_inject(struct acrn_vm *vm, u64 msi_addr, u64 msi_data);

int acrn_ioeventfd_init(struct acrn_vm *vm);
int acrn_ioeventfd_config(struct acrn_vm *vm, struct acrn_ioeventfd *args);
void acrn_ioeventfd_deinit(struct acrn_vm *vm);

int acrn_irqfd_init(struct acrn_vm *vm);
int acrn_irqfd_config(struct acrn_vm *vm, struct acrn_irqfd *args);
void acrn_irqfd_deinit(struct acrn_vm *vm);

#endif /* __ACRN_HSM_DRV_H */

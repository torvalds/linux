/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ACRN_HSM_DRV_H
#define __ACRN_HSM_DRV_H

#include <linux/acrn.h>
#include <linux/dev_printk.h>
#include <linux/miscdevice.h>
#include <linux/types.h>

#include "hypercall.h"

extern struct miscdevice acrn_dev;

#define ACRN_MEM_MAPPING_MAX	256

#define ACRN_MEM_REGION_ADD	0
#define ACRN_MEM_REGION_DEL	2
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
 *
 * HC_VM_SET_MEMORY_REGIONS uses this structure to manage EPT mappings of
 * multiple memory regions of a User VM. A &struct vm_memory_region_batch
 * contains multiple &struct vm_memory_region_op for batch processing in the
 * ACRN Hypervisor.
 */
struct vm_memory_region_batch {
	u16	vmid;
	u16	reserved[3];
	u32	regions_num;
	u64	regions_gpa;
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

#define ACRN_INVALID_VMID (0xffffU)

#define ACRN_VM_FLAG_DESTROYED		0U
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
 */
struct acrn_vm {
	struct list_head		list;
	u16				vmid;
	int				vcpu_num;
	unsigned long			flags;
	struct mutex			regions_mapping_lock;
	struct vm_memory_mapping	regions_mapping[ACRN_MEM_MAPPING_MAX];
	int				regions_mapping_count;
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

#endif /* __ACRN_HSM_DRV_H */

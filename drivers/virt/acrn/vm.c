// SPDX-License-Identifier: GPL-2.0
/*
 * ACRN_HSM: Virtual Machine management
 *
 * Copyright (C) 2020 Intel Corporation. All rights reserved.
 *
 * Authors:
 *	Jason Chen CJ <jason.cj.chen@intel.com>
 *	Yakui Zhao <yakui.zhao@intel.com>
 */
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include "acrn_drv.h"

/* List of VMs */
LIST_HEAD(acrn_vm_list);
/*
 * acrn_vm_list is read in a worker thread which dispatch I/O requests and
 * is wrote in VM creation ioctl. Use the rwlock mechanism to protect it.
 */
DEFINE_RWLOCK(acrn_vm_list_lock);

struct acrn_vm *acrn_vm_create(struct acrn_vm *vm,
			       struct acrn_vm_creation *vm_param)
{
	int ret;

	ret = hcall_create_vm(virt_to_phys(vm_param));
	if (ret < 0 || vm_param->vmid == ACRN_INVALID_VMID) {
		dev_err(acrn_dev.this_device,
			"Failed to create VM! Error: %d\n", ret);
		return NULL;
	}

	mutex_init(&vm->regions_mapping_lock);
	INIT_LIST_HEAD(&vm->ioreq_clients);
	spin_lock_init(&vm->ioreq_clients_lock);
	vm->vmid = vm_param->vmid;
	vm->vcpu_num = vm_param->vcpu_num;

	if (acrn_ioreq_init(vm, vm_param->ioreq_buf) < 0) {
		hcall_destroy_vm(vm_param->vmid);
		vm->vmid = ACRN_INVALID_VMID;
		return NULL;
	}

	write_lock_bh(&acrn_vm_list_lock);
	list_add(&vm->list, &acrn_vm_list);
	write_unlock_bh(&acrn_vm_list_lock);

	dev_dbg(acrn_dev.this_device, "VM %u created.\n", vm->vmid);
	return vm;
}

int acrn_vm_destroy(struct acrn_vm *vm)
{
	int ret;

	if (vm->vmid == ACRN_INVALID_VMID ||
	    test_and_set_bit(ACRN_VM_FLAG_DESTROYED, &vm->flags))
		return 0;

	/* Remove from global VM list */
	write_lock_bh(&acrn_vm_list_lock);
	list_del_init(&vm->list);
	write_unlock_bh(&acrn_vm_list_lock);

	acrn_ioreq_deinit(vm);

	ret = hcall_destroy_vm(vm->vmid);
	if (ret < 0) {
		dev_err(acrn_dev.this_device,
			"Failed to destroy VM %u\n", vm->vmid);
		clear_bit(ACRN_VM_FLAG_DESTROYED, &vm->flags);
		return ret;
	}

	acrn_vm_all_ram_unmap(vm);

	dev_dbg(acrn_dev.this_device, "VM %u destroyed.\n", vm->vmid);
	vm->vmid = ACRN_INVALID_VMID;
	return 0;
}

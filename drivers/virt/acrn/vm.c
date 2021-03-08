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

	acrn_ioeventfd_init(vm);
	acrn_irqfd_init(vm);
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

	acrn_ioeventfd_deinit(vm);
	acrn_irqfd_deinit(vm);
	acrn_ioreq_deinit(vm);

	if (vm->monitor_page) {
		put_page(vm->monitor_page);
		vm->monitor_page = NULL;
	}

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

/**
 * acrn_inject_msi() - Inject a MSI interrupt into a User VM
 * @vm:		User VM
 * @msi_addr:	The MSI address
 * @msi_data:	The MSI data
 *
 * Return: 0 on success, <0 on error
 */
int acrn_msi_inject(struct acrn_vm *vm, u64 msi_addr, u64 msi_data)
{
	struct acrn_msi_entry *msi;
	int ret;

	/* might be used in interrupt context, so use GFP_ATOMIC */
	msi = kzalloc(sizeof(*msi), GFP_ATOMIC);
	if (!msi)
		return -ENOMEM;

	/*
	 * msi_addr: addr[19:12] with dest vcpu id
	 * msi_data: data[7:0] with vector
	 */
	msi->msi_addr = msi_addr;
	msi->msi_data = msi_data;
	ret = hcall_inject_msi(vm->vmid, virt_to_phys(msi));
	if (ret < 0)
		dev_err(acrn_dev.this_device,
			"Failed to inject MSI to VM %u!\n", vm->vmid);
	kfree(msi);
	return ret;
}

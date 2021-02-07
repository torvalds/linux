/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ACRN_HSM_DRV_H
#define __ACRN_HSM_DRV_H

#include <linux/acrn.h>
#include <linux/dev_printk.h>
#include <linux/miscdevice.h>
#include <linux/types.h>

#include "hypercall.h"

extern struct miscdevice acrn_dev;

#define ACRN_INVALID_VMID (0xffffU)

#define ACRN_VM_FLAG_DESTROYED		0U
/**
 * struct acrn_vm - Properties of ACRN User VM.
 * @list:	Entry within global list of all VMs
 * @vmid:	User VM ID
 * @vcpu_num:	Number of virtual CPUs in the VM
 * @flags:	Flags (ACRN_VM_FLAG_*) of the VM. This is VM flag management
 *		in HSM which is different from the &acrn_vm_creation.vm_flag.
 */
struct acrn_vm {
	struct list_head	list;
	u16			vmid;
	int			vcpu_num;
	unsigned long		flags;
};

struct acrn_vm *acrn_vm_create(struct acrn_vm *vm,
			       struct acrn_vm_creation *vm_param);
int acrn_vm_destroy(struct acrn_vm *vm);

#endif /* __ACRN_HSM_DRV_H */

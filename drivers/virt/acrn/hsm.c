// SPDX-License-Identifier: GPL-2.0
/*
 * ACRN Hypervisor Service Module (HSM)
 *
 * Copyright (C) 2020 Intel Corporation. All rights reserved.
 *
 * Authors:
 *	Fengwei Yin <fengwei.yin@intel.com>
 *	Yakui Zhao <yakui.zhao@intel.com>
 */

#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <asm/acrn.h>
#include <asm/hypervisor.h>

#include "acrn_drv.h"

/*
 * When /dev/acrn_hsm is opened, a 'struct acrn_vm' object is created to
 * represent a VM instance and continues to be associated with the opened file
 * descriptor. All ioctl operations on this file descriptor will be targeted to
 * the VM instance. Release of this file descriptor will destroy the object.
 */
static int acrn_dev_open(struct inode *inode, struct file *filp)
{
	struct acrn_vm *vm;

	vm = kzalloc(sizeof(*vm), GFP_KERNEL);
	if (!vm)
		return -ENOMEM;

	vm->vmid = ACRN_INVALID_VMID;
	filp->private_data = vm;
	return 0;
}

/*
 * HSM relies on hypercall layer of the ACRN hypervisor to do the
 * sanity check against the input parameters.
 */
static long acrn_dev_ioctl(struct file *filp, unsigned int cmd,
			   unsigned long ioctl_param)
{
	struct acrn_vm *vm = filp->private_data;
	struct acrn_vm_creation *vm_param;
	struct acrn_vcpu_regs *cpu_regs;
	int i, ret = 0;

	if (vm->vmid == ACRN_INVALID_VMID && cmd != ACRN_IOCTL_CREATE_VM) {
		dev_dbg(acrn_dev.this_device,
			"ioctl 0x%x: Invalid VM state!\n", cmd);
		return -EINVAL;
	}

	switch (cmd) {
	case ACRN_IOCTL_CREATE_VM:
		vm_param = memdup_user((void __user *)ioctl_param,
				       sizeof(struct acrn_vm_creation));
		if (IS_ERR(vm_param))
			return PTR_ERR(vm_param);

		if ((vm_param->reserved0 | vm_param->reserved1) != 0)
			return -EINVAL;

		vm = acrn_vm_create(vm, vm_param);
		if (!vm) {
			ret = -EINVAL;
			kfree(vm_param);
			break;
		}

		if (copy_to_user((void __user *)ioctl_param, vm_param,
				 sizeof(struct acrn_vm_creation))) {
			acrn_vm_destroy(vm);
			ret = -EFAULT;
		}

		kfree(vm_param);
		break;
	case ACRN_IOCTL_START_VM:
		ret = hcall_start_vm(vm->vmid);
		if (ret < 0)
			dev_dbg(acrn_dev.this_device,
				"Failed to start VM %u!\n", vm->vmid);
		break;
	case ACRN_IOCTL_PAUSE_VM:
		ret = hcall_pause_vm(vm->vmid);
		if (ret < 0)
			dev_dbg(acrn_dev.this_device,
				"Failed to pause VM %u!\n", vm->vmid);
		break;
	case ACRN_IOCTL_RESET_VM:
		ret = hcall_reset_vm(vm->vmid);
		if (ret < 0)
			dev_dbg(acrn_dev.this_device,
				"Failed to restart VM %u!\n", vm->vmid);
		break;
	case ACRN_IOCTL_DESTROY_VM:
		ret = acrn_vm_destroy(vm);
		break;
	case ACRN_IOCTL_SET_VCPU_REGS:
		cpu_regs = memdup_user((void __user *)ioctl_param,
				       sizeof(struct acrn_vcpu_regs));
		if (IS_ERR(cpu_regs))
			return PTR_ERR(cpu_regs);

		for (i = 0; i < ARRAY_SIZE(cpu_regs->reserved); i++)
			if (cpu_regs->reserved[i])
				return -EINVAL;

		for (i = 0; i < ARRAY_SIZE(cpu_regs->vcpu_regs.reserved_32); i++)
			if (cpu_regs->vcpu_regs.reserved_32[i])
				return -EINVAL;

		for (i = 0; i < ARRAY_SIZE(cpu_regs->vcpu_regs.reserved_64); i++)
			if (cpu_regs->vcpu_regs.reserved_64[i])
				return -EINVAL;

		for (i = 0; i < ARRAY_SIZE(cpu_regs->vcpu_regs.gdt.reserved); i++)
			if (cpu_regs->vcpu_regs.gdt.reserved[i] |
			    cpu_regs->vcpu_regs.idt.reserved[i])
				return -EINVAL;

		ret = hcall_set_vcpu_regs(vm->vmid, virt_to_phys(cpu_regs));
		if (ret < 0)
			dev_dbg(acrn_dev.this_device,
				"Failed to set regs state of VM%u!\n",
				vm->vmid);
		kfree(cpu_regs);
		break;
	default:
		dev_dbg(acrn_dev.this_device, "Unknown IOCTL 0x%x!\n", cmd);
		ret = -ENOTTY;
	}

	return ret;
}

static int acrn_dev_release(struct inode *inode, struct file *filp)
{
	struct acrn_vm *vm = filp->private_data;

	acrn_vm_destroy(vm);
	kfree(vm);
	return 0;
}

static const struct file_operations acrn_fops = {
	.owner		= THIS_MODULE,
	.open		= acrn_dev_open,
	.release	= acrn_dev_release,
	.unlocked_ioctl = acrn_dev_ioctl,
};

struct miscdevice acrn_dev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "acrn_hsm",
	.fops	= &acrn_fops,
};

static int __init hsm_init(void)
{
	int ret;

	if (x86_hyper_type != X86_HYPER_ACRN)
		return -ENODEV;

	if (!(cpuid_eax(ACRN_CPUID_FEATURES) & ACRN_FEATURE_PRIVILEGED_VM))
		return -EPERM;

	ret = misc_register(&acrn_dev);
	if (ret)
		pr_err("Create misc dev failed!\n");

	return ret;
}

static void __exit hsm_exit(void)
{
	misc_deregister(&acrn_dev);
}
module_init(hsm_init);
module_exit(hsm_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ACRN Hypervisor Service Module (HSM)");

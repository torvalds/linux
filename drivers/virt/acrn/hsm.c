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

#include <linux/miscdevice.h>
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

static int acrn_dev_release(struct inode *inode, struct file *filp)
{
	struct acrn_vm *vm = filp->private_data;

	kfree(vm);
	return 0;
}

static const struct file_operations acrn_fops = {
	.owner		= THIS_MODULE,
	.open		= acrn_dev_open,
	.release	= acrn_dev_release,
};

static struct miscdevice acrn_dev = {
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

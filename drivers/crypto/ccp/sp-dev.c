/*
 * AMD Secure Processor driver
 *
 * Copyright (C) 2017 Advanced Micro Devices, Inc.
 *
 * Author: Tom Lendacky <thomas.lendacky@amd.com>
 * Author: Gary R Hook <gary.hook@amd.com>
 * Author: Brijesh Singh <brijesh.singh@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>
#include <linux/ccp.h>

#include "ccp-dev.h"
#include "sp-dev.h"

MODULE_AUTHOR("Tom Lendacky <thomas.lendacky@amd.com>");
MODULE_AUTHOR("Gary R Hook <gary.hook@amd.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.1.0");
MODULE_DESCRIPTION("AMD Secure Processor driver");

/* List of SPs, SP count, read-write access lock, and access functions
 *
 * Lock structure: get sp_unit_lock for reading whenever we need to
 * examine the SP list.
 */
static DEFINE_RWLOCK(sp_unit_lock);
static LIST_HEAD(sp_units);

/* Ever-increasing value to produce unique unit numbers */
static atomic_t sp_ordinal;

static void sp_add_device(struct sp_device *sp)
{
	unsigned long flags;

	write_lock_irqsave(&sp_unit_lock, flags);

	list_add_tail(&sp->entry, &sp_units);

	write_unlock_irqrestore(&sp_unit_lock, flags);
}

static void sp_del_device(struct sp_device *sp)
{
	unsigned long flags;

	write_lock_irqsave(&sp_unit_lock, flags);

	list_del(&sp->entry);

	write_unlock_irqrestore(&sp_unit_lock, flags);
}

/**
 * sp_alloc_struct - allocate and initialize the sp_device struct
 *
 * @dev: device struct of the SP
 */
struct sp_device *sp_alloc_struct(struct device *dev)
{
	struct sp_device *sp;

	sp = devm_kzalloc(dev, sizeof(*sp), GFP_KERNEL);
	if (!sp)
		return NULL;

	sp->dev = dev;
	sp->ord = atomic_inc_return(&sp_ordinal);
	snprintf(sp->name, SP_MAX_NAME_LEN, "sp-%u", sp->ord);

	return sp;
}

int sp_init(struct sp_device *sp)
{
	sp_add_device(sp);

	if (sp->dev_vdata->ccp_vdata)
		ccp_dev_init(sp);

	return 0;
}

void sp_destroy(struct sp_device *sp)
{
	if (sp->dev_vdata->ccp_vdata)
		ccp_dev_destroy(sp);

	sp_del_device(sp);
}

#ifdef CONFIG_PM
int sp_suspend(struct sp_device *sp, pm_message_t state)
{
	int ret;

	if (sp->dev_vdata->ccp_vdata) {
		ret = ccp_dev_suspend(sp, state);
		if (ret)
			return ret;
	}

	return 0;
}

int sp_resume(struct sp_device *sp)
{
	int ret;

	if (sp->dev_vdata->ccp_vdata) {
		ret = ccp_dev_resume(sp);
		if (ret)
			return ret;
	}

	return 0;
}
#endif

static int __init sp_mod_init(void)
{
#ifdef CONFIG_X86
	int ret;

	ret = ccp_pci_init();
	if (ret)
		return ret;

	/* Don't leave the driver loaded if init failed */
	if (ccp_present() != 0) {
		ccp_pci_exit();
		return -ENODEV;
	}

	return 0;
#endif

#ifdef CONFIG_ARM64
	int ret;

	ret = ccp_platform_init();
	if (ret)
		return ret;

	/* Don't leave the driver loaded if init failed */
	if (ccp_present() != 0) {
		ccp_platform_exit();
		return -ENODEV;
	}

	return 0;
#endif

	return -ENODEV;
}

static void __exit sp_mod_exit(void)
{
#ifdef CONFIG_X86
	ccp_pci_exit();
#endif

#ifdef CONFIG_ARM64
	ccp_platform_exit();
#endif
}

module_init(sp_mod_init);
module_exit(sp_mod_exit);

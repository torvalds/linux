/*
 * linux/arch/unicore32/kernel/pm.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 *	Maintained by GUAN Xue-tao <gxt@mprc.pku.edu.cn>
 *	Copyright (C) 2001-2010 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/suspend.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/pm.h>

#include "setup.h"

struct puv3_cpu_pm_fns *puv3_cpu_pm_fns;
static unsigned long *sleep_save;

int puv3_pm_enter(suspend_state_t state)
{
	unsigned long sleep_save_checksum = 0, checksum = 0;
	int i;

	/* skip registers saving for standby */
	if (state != PM_SUSPEND_STANDBY) {
		puv3_cpu_pm_fns->save(sleep_save);
		/* before sleeping, calculate and save a checksum */
		for (i = 0; i < puv3_cpu_pm_fns->save_count - 1; i++)
			sleep_save_checksum += sleep_save[i];
	}

	/* *** go zzz *** */
	puv3_cpu_pm_fns->enter(state);
	cpu_init();
#ifdef CONFIG_INPUT_KEYBOARD
	puv3_ps2_init();
#endif
#ifdef CONFIG_PCI
	pci_puv3_preinit();
#endif
	if (state != PM_SUSPEND_STANDBY) {
		/* after sleeping, validate the checksum */
		for (i = 0; i < puv3_cpu_pm_fns->save_count - 1; i++)
			checksum += sleep_save[i];

		/* if invalid, display message and wait for a hardware reset */
		if (checksum != sleep_save_checksum) {
			while (1)
				puv3_cpu_pm_fns->enter(state);
		}
		puv3_cpu_pm_fns->restore(sleep_save);
	}

	pr_debug("*** made it back from resume\n");

	return 0;
}
EXPORT_SYMBOL_GPL(puv3_pm_enter);

unsigned long sleep_phys_sp(void *sp)
{
	return virt_to_phys(sp);
}

static int puv3_pm_valid(suspend_state_t state)
{
	if (puv3_cpu_pm_fns)
		return puv3_cpu_pm_fns->valid(state);

	return -EINVAL;
}

static int puv3_pm_prepare(void)
{
	int ret = 0;

	if (puv3_cpu_pm_fns && puv3_cpu_pm_fns->prepare)
		ret = puv3_cpu_pm_fns->prepare();

	return ret;
}

static void puv3_pm_finish(void)
{
	if (puv3_cpu_pm_fns && puv3_cpu_pm_fns->finish)
		puv3_cpu_pm_fns->finish();
}

static struct platform_suspend_ops puv3_pm_ops = {
	.valid		= puv3_pm_valid,
	.enter		= puv3_pm_enter,
	.prepare	= puv3_pm_prepare,
	.finish		= puv3_pm_finish,
};

static int __init puv3_pm_init(void)
{
	if (!puv3_cpu_pm_fns) {
		printk(KERN_ERR "no valid puv3_cpu_pm_fns defined\n");
		return -EINVAL;
	}

	sleep_save = kmalloc(puv3_cpu_pm_fns->save_count
				* sizeof(unsigned long), GFP_KERNEL);
	if (!sleep_save) {
		printk(KERN_ERR "failed to alloc memory for pm save\n");
		return -ENOMEM;
	}

	suspend_set_ops(&puv3_pm_ops);
	return 0;
}

device_initcall(puv3_pm_init);

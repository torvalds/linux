/* linux/arch/arm/mach-s3c2412/pm.c
 *
 * Copyright (c) 2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * http://armlinux.simtec.co.uk/.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/syscore_ops.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <asm/cacheflush.h>
#include <asm/irq.h>

#include <mach/hardware.h>
#include <mach/regs-gpio.h>

#include <plat/cpu.h>
#include <plat/pm.h>
#include <plat/s3c2412.h>

#include "regs-dsc.h"
#include "s3c2412-power.h"

extern void s3c2412_sleep_enter(void);

static int s3c2412_cpu_suspend(unsigned long arg)
{
	unsigned long tmp;

	/* set our standby method to sleep */

	tmp = __raw_readl(S3C2412_PWRCFG);
	tmp |= S3C2412_PWRCFG_STANDBYWFI_SLEEP;
	__raw_writel(tmp, S3C2412_PWRCFG);

	s3c2412_sleep_enter();

	panic("sleep resumed to originator?");
}

static void s3c2412_pm_prepare(void)
{
}

static int s3c2412_pm_add(struct device *dev, struct subsys_interface *sif)
{
	pm_cpu_prep = s3c2412_pm_prepare;
	pm_cpu_sleep = s3c2412_cpu_suspend;

	return 0;
}

static struct sleep_save s3c2412_sleep[] = {
	SAVE_ITEM(S3C2412_DSC0),
	SAVE_ITEM(S3C2412_DSC1),
	SAVE_ITEM(S3C2413_GPJDAT),
	SAVE_ITEM(S3C2413_GPJCON),
	SAVE_ITEM(S3C2413_GPJUP),

	/* save the PWRCFG to get back to original sleep method */

	SAVE_ITEM(S3C2412_PWRCFG),

	/* save the sleep configuration anyway, just in case these
	 * get damaged during wakeup */

	SAVE_ITEM(S3C2412_GPBSLPCON),
	SAVE_ITEM(S3C2412_GPCSLPCON),
	SAVE_ITEM(S3C2412_GPDSLPCON),
	SAVE_ITEM(S3C2412_GPFSLPCON),
	SAVE_ITEM(S3C2412_GPGSLPCON),
	SAVE_ITEM(S3C2412_GPHSLPCON),
	SAVE_ITEM(S3C2413_GPJSLPCON),
};

static struct subsys_interface s3c2412_pm_interface = {
	.name		= "s3c2412_pm",
	.subsys		= &s3c2412_subsys,
	.add_dev	= s3c2412_pm_add,
};

static __init int s3c2412_pm_init(void)
{
	return subsys_interface_register(&s3c2412_pm_interface);
}

arch_initcall(s3c2412_pm_init);

static int s3c2412_pm_suspend(void)
{
	s3c_pm_do_save(s3c2412_sleep, ARRAY_SIZE(s3c2412_sleep));
	return 0;
}

static void s3c2412_pm_resume(void)
{
	unsigned long tmp;

	tmp = __raw_readl(S3C2412_PWRCFG);
	tmp &= ~S3C2412_PWRCFG_STANDBYWFI_MASK;
	tmp |=  S3C2412_PWRCFG_STANDBYWFI_IDLE;
	__raw_writel(tmp, S3C2412_PWRCFG);

	s3c_pm_do_restore(s3c2412_sleep, ARRAY_SIZE(s3c2412_sleep));
}

struct syscore_ops s3c2412_pm_syscore_ops = {
	.suspend	= s3c2412_pm_suspend,
	.resume		= s3c2412_pm_resume,
};

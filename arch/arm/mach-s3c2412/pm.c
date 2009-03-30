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
#include <linux/sysdev.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/irq.h>

#include <mach/regs-power.h>
#include <mach/regs-gpioj.h>
#include <mach/regs-gpio.h>
#include <mach/regs-dsc.h>

#include <plat/cpu.h>
#include <plat/pm.h>

#include <plat/s3c2412.h>

extern void s3c2412_sleep_enter(void);

static void s3c2412_cpu_suspend(void)
{
	unsigned long tmp;

	/* set our standby method to sleep */

	tmp = __raw_readl(S3C2412_PWRCFG);
	tmp |= S3C2412_PWRCFG_STANDBYWFI_SLEEP;
	__raw_writel(tmp, S3C2412_PWRCFG);

	s3c2412_sleep_enter();
}

static void s3c2412_pm_prepare(void)
{
}

static int s3c2412_pm_add(struct sys_device *sysdev)
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

static int s3c2412_pm_suspend(struct sys_device *dev, pm_message_t state)
{
	s3c_pm_do_save(s3c2412_sleep, ARRAY_SIZE(s3c2412_sleep));
	return 0;
}

static int s3c2412_pm_resume(struct sys_device *dev)
{
	unsigned long tmp;

	tmp = __raw_readl(S3C2412_PWRCFG);
	tmp &= ~S3C2412_PWRCFG_STANDBYWFI_MASK;
	tmp |=  S3C2412_PWRCFG_STANDBYWFI_IDLE;
	__raw_writel(tmp, S3C2412_PWRCFG);

	s3c_pm_do_restore(s3c2412_sleep, ARRAY_SIZE(s3c2412_sleep));
	return 0;
}

static struct sysdev_driver s3c2412_pm_driver = {
	.add		= s3c2412_pm_add,
	.suspend	= s3c2412_pm_suspend,
	.resume		= s3c2412_pm_resume,
};

static __init int s3c2412_pm_init(void)
{
	return sysdev_driver_register(&s3c2412_sysclass, &s3c2412_pm_driver);
}

arch_initcall(s3c2412_pm_init);

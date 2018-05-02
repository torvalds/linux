// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (c) 2004-2005 Simtec Electronics
//	http://armlinux.simtec.co.uk/
//	Ben Dooks <ben@simtec.co.uk>
//
// S3C2442 core and lock support

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/syscore_ops.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/gpio-samsung.h>
#include <linux/atomic.h>
#include <asm/irq.h>

#include <mach/regs-clock.h>

#include <plat/cpu.h>
#include <plat/pm.h>

#include <plat/gpio-core.h>
#include <plat/gpio-cfg.h>
#include <plat/gpio-cfg-helpers.h>

#include "common.h"

static struct device s3c2442_dev = {
	.bus		= &s3c2442_subsys,
};

int __init s3c2442_init(void)
{
	printk("S3C2442: Initialising architecture\n");

#ifdef CONFIG_PM_SLEEP
	register_syscore_ops(&s3c2410_pm_syscore_ops);
	register_syscore_ops(&s3c24xx_irq_syscore_ops);
	register_syscore_ops(&s3c244x_pm_syscore_ops);
#endif

	return device_register(&s3c2442_dev);
}

void __init s3c2442_map_io(void)
{
	s3c244x_map_io();

	s3c24xx_gpiocfg_default.set_pull = s3c24xx_gpio_setpull_1down;
	s3c24xx_gpiocfg_default.get_pull = s3c24xx_gpio_getpull_1down;
}

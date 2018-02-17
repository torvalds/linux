// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2004-2006 Simtec Electronics
//   Ben Dooks <ben@simtec.co.uk>
//
// Samsung S3C2440 Mobile CPU support

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/device.h>
#include <linux/syscore_ops.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <mach/gpio-samsung.h>
#include <asm/irq.h>

#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/pm.h>

#include <plat/gpio-core.h>
#include <plat/gpio-cfg.h>
#include <plat/gpio-cfg-helpers.h>

#include "common.h"

static struct device s3c2440_dev = {
	.bus		= &s3c2440_subsys,
};

int __init s3c2440_init(void)
{
	printk("S3C2440: Initialising architecture\n");

	/* change irq for watchdog */

	s3c_device_wdt.resource[1].start = IRQ_S3C2440_WDT;
	s3c_device_wdt.resource[1].end   = IRQ_S3C2440_WDT;

	/* register suspend/resume handlers */

#ifdef CONFIG_PM_SLEEP
	register_syscore_ops(&s3c2410_pm_syscore_ops);
	register_syscore_ops(&s3c24xx_irq_syscore_ops);
	register_syscore_ops(&s3c244x_pm_syscore_ops);
#endif

	/* register our system device for everything else */

	return device_register(&s3c2440_dev);
}

void __init s3c2440_map_io(void)
{
	s3c244x_map_io();

	s3c24xx_gpiocfg_default.set_pull = s3c24xx_gpio_setpull_1up;
	s3c24xx_gpiocfg_default.get_pull = s3c24xx_gpio_getpull_1up;
}

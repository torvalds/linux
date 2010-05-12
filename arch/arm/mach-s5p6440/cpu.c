/* linux/arch/arm/mach-s5p6440/cpu.c
 *
 * Copyright (c) 2009 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
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
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/sysdev.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/proc-fns.h>

#include <mach/hardware.h>
#include <mach/map.h>
#include <asm/irq.h>

#include <plat/regs-serial.h>
#include <mach/regs-clock.h>

#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/clock.h>
#include <plat/s5p6440.h>

static void s5p6440_idle(void)
{
	unsigned long val;

	if (!need_resched()) {
		val = __raw_readl(S5P_PWR_CFG);
		val &= ~(0x3<<5);
		val |= (0x1<<5);
		__raw_writel(val, S5P_PWR_CFG);

		cpu_do_idle();
	}
	local_irq_enable();
}

/* s5p6440_map_io
 *
 * register the standard cpu IO areas
*/

void __init s5p6440_map_io(void)
{
	/* initialize any device information early */
}

void __init s5p6440_init_clocks(int xtal)
{
	printk(KERN_DEBUG "%s: initializing clocks\n", __func__);

	s3c24xx_register_baseclocks(xtal);
	s5p_register_clocks(xtal);
	s5p6440_register_clocks();
	s5p6440_setup_clocks();
}

void __init s5p6440_init_irq(void)
{
	/* S5P6440 supports only 2 VIC */
	u32 vic[2];

	/*
	 * VIC0 is missing IRQ_VIC0[3, 4, 8, 10, (12-22)]
	 * VIC1 is missing IRQ VIC1[1, 3, 4, 10, 11, 12, 14, 15, 22]
	 */
	vic[0] = 0xff800ae7;
	vic[1] = 0xffbf23e5;

	s5p_init_irq(vic, ARRAY_SIZE(vic));
}

static struct sysdev_class s5p6440_sysclass = {
	.name	= "s5p6440-core",
};

static struct sys_device s5p6440_sysdev = {
	.cls	= &s5p6440_sysclass,
};

static int __init s5p6440_core_init(void)
{
	return sysdev_class_register(&s5p6440_sysclass);
}

core_initcall(s5p6440_core_init);

int __init s5p6440_init(void)
{
	printk(KERN_INFO "S5P6440: Initializing architecture\n");

	/* set idle function */
	pm_idle = s5p6440_idle;

	return sysdev_register(&s5p6440_sysdev);
}

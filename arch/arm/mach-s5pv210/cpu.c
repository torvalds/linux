/* linux/arch/arm/mach-s5pv210/cpu.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
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
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/sysdev.h>
#include <linux/platform_device.h>
#include <linux/sched.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/proc-fns.h>
#include <mach/map.h>
#include <mach/regs-clock.h>

#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/clock.h>
#include <plat/fb-core.h>
#include <plat/s5pv210.h>
#include <plat/adc-core.h>
#include <plat/ata-core.h>
#include <plat/fimc-core.h>
#include <plat/iic-core.h>
#include <plat/keypad-core.h>
#include <plat/sdhci.h>
#include <plat/reset.h>

/* Initial IO mappings */

static struct map_desc s5pv210_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S5P_VA_SYSTIMER,
		.pfn		= __phys_to_pfn(S5PV210_PA_SYSTIMER),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_GPIO,
		.pfn		= __phys_to_pfn(S5PV210_PA_GPIO),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)VA_VIC0,
		.pfn		= __phys_to_pfn(S5PV210_PA_VIC0),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)VA_VIC1,
		.pfn		= __phys_to_pfn(S5PV210_PA_VIC1),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)VA_VIC2,
		.pfn		= __phys_to_pfn(S5PV210_PA_VIC2),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)VA_VIC3,
		.pfn		= __phys_to_pfn(S5PV210_PA_VIC3),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_UART,
		.pfn		= __phys_to_pfn(S3C_PA_UART),
		.length		= SZ_512K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_SROMC,
		.pfn		= __phys_to_pfn(S5PV210_PA_SROMC),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}
};

static void s5pv210_idle(void)
{
	if (!need_resched())
		cpu_do_idle();

	local_irq_enable();
}

static void s5pv210_sw_reset(void)
{
	__raw_writel(0x1, S5P_SWRESET);
}

/* s5pv210_map_io
 *
 * register the standard cpu IO areas
*/

void __init s5pv210_map_io(void)
{
	iotable_init(s5pv210_iodesc, ARRAY_SIZE(s5pv210_iodesc));

	/* initialise device information early */
	s5pv210_default_sdhci0();
	s5pv210_default_sdhci1();
	s5pv210_default_sdhci2();
	s5pv210_default_sdhci3();

	s3c_adc_setname("s3c64xx-adc");

	s3c_cfcon_setname("s5pv210-pata");

	s3c_fimc_setname(0, "s5pv210-fimc");
	s3c_fimc_setname(1, "s5pv210-fimc");
	s3c_fimc_setname(2, "s5pv210-fimc");

	/* the i2c devices are directly compatible with s3c2440 */
	s3c_i2c0_setname("s3c2440-i2c");
	s3c_i2c1_setname("s3c2440-i2c");
	s3c_i2c2_setname("s3c2440-i2c");

	s3c_fb_setname("s5pv210-fb");

	/* Use s5pv210-keypad instead of samsung-keypad */
	samsung_keypad_setname("s5pv210-keypad");
}

void __init s5pv210_init_clocks(int xtal)
{
	printk(KERN_DEBUG "%s: initializing clocks\n", __func__);

	s3c24xx_register_baseclocks(xtal);
	s5p_register_clocks(xtal);
	s5pv210_register_clocks();
	s5pv210_setup_clocks();
}

void __init s5pv210_init_irq(void)
{
	u32 vic[4];	/* S5PV210 supports 4 VIC */

	/* All the VICs are fully populated. */
	vic[0] = ~0;
	vic[1] = ~0;
	vic[2] = ~0;
	vic[3] = ~0;

	s5p_init_irq(vic, ARRAY_SIZE(vic));
}

struct sysdev_class s5pv210_sysclass = {
	.name	= "s5pv210-core",
};

static struct sys_device s5pv210_sysdev = {
	.cls	= &s5pv210_sysclass,
};

static int __init s5pv210_core_init(void)
{
	return sysdev_class_register(&s5pv210_sysclass);
}

core_initcall(s5pv210_core_init);

int __init s5pv210_init(void)
{
	printk(KERN_INFO "S5PV210: Initializing architecture\n");

	/* set idle function */
	pm_idle = s5pv210_idle;

	/* set sw_reset function */
	s5p_reset_hook = s5pv210_sw_reset;

	return sysdev_register(&s5pv210_sysdev);
}

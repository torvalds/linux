/* linux/arch/arm/mach-s5p64x0/cpu.c
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
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
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/sysdev.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/dma-mapping.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/proc-fns.h>
#include <asm/irq.h>

#include <mach/hardware.h>
#include <mach/map.h>
#include <mach/regs-clock.h>

#include <plat/regs-serial.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/clock.h>
#include <plat/s5p6440.h>
#include <plat/s5p6450.h>
#include <plat/adc-core.h>
#include <plat/fb-core.h>
#include <plat/sdhci.h>

/* Initial IO mappings */

static struct map_desc s5p64x0_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S5P_VA_GPIO,
		.pfn		= __phys_to_pfn(S5P64X0_PA_GPIO),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)VA_VIC0,
		.pfn		= __phys_to_pfn(S5P64X0_PA_VIC0),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)VA_VIC1,
		.pfn		= __phys_to_pfn(S5P64X0_PA_VIC1),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	},
};

static struct map_desc s5p6440_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S3C_VA_UART,
		.pfn		= __phys_to_pfn(S5P6440_PA_UART(0)),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static struct map_desc s5p6450_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S3C_VA_UART,
		.pfn		= __phys_to_pfn(S5P6450_PA_UART(0)),
		.length		= SZ_512K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_UART + SZ_512K,
		.pfn		= __phys_to_pfn(S5P6450_PA_UART(5)),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static void s5p64x0_idle(void)
{
	unsigned long val;

	if (!need_resched()) {
		val = __raw_readl(S5P64X0_PWR_CFG);
		val &= ~(0x3 << 5);
		val |= (0x1 << 5);
		__raw_writel(val, S5P64X0_PWR_CFG);

		cpu_do_idle();
	}
	local_irq_enable();
}

/*
 * s5p64x0_map_io
 *
 * register the standard CPU IO areas
 */

void __init s5p6440_map_io(void)
{
	/* initialize any device information early */
	s3c_adc_setname("s3c64xx-adc");
	s3c_fb_setname("s5p64x0-fb");

	s5p64x0_default_sdhci0();
	s5p64x0_default_sdhci1();
	s5p6440_default_sdhci2();

	iotable_init(s5p64x0_iodesc, ARRAY_SIZE(s5p64x0_iodesc));
	iotable_init(s5p6440_iodesc, ARRAY_SIZE(s5p6440_iodesc));
	init_consistent_dma_size(SZ_8M);
}

void __init s5p6450_map_io(void)
{
	/* initialize any device information early */
	s3c_adc_setname("s3c64xx-adc");
	s3c_fb_setname("s5p64x0-fb");

	s5p64x0_default_sdhci0();
	s5p64x0_default_sdhci1();
	s5p6450_default_sdhci2();

	iotable_init(s5p64x0_iodesc, ARRAY_SIZE(s5p64x0_iodesc));
	iotable_init(s5p6450_iodesc, ARRAY_SIZE(s5p6450_iodesc));
	init_consistent_dma_size(SZ_8M);
}

/*
 * s5p64x0_init_clocks
 *
 * register and setup the CPU clocks
 */

void __init s5p6440_init_clocks(int xtal)
{
	printk(KERN_DEBUG "%s: initializing clocks\n", __func__);

	s3c24xx_register_baseclocks(xtal);
	s5p_register_clocks(xtal);
	s5p6440_register_clocks();
	s5p6440_setup_clocks();
}

void __init s5p6450_init_clocks(int xtal)
{
	printk(KERN_DEBUG "%s: initializing clocks\n", __func__);

	s3c24xx_register_baseclocks(xtal);
	s5p_register_clocks(xtal);
	s5p6450_register_clocks();
	s5p6450_setup_clocks();
}

/*
 * s5p64x0_init_irq
 *
 * register the CPU interrupts
 */

void __init s5p6440_init_irq(void)
{
	/* S5P6440 supports 2 VIC */
	u32 vic[2];

	/*
	 * VIC0 is missing IRQ_VIC0[3, 4, 8, 10, (12-22)]
	 * VIC1 is missing IRQ VIC1[1, 3, 4, 10, 11, 12, 14, 15, 22]
	 */
	vic[0] = 0xff800ae7;
	vic[1] = 0xffbf23e5;

	s5p_init_irq(vic, ARRAY_SIZE(vic));
}

void __init s5p6450_init_irq(void)
{
	/* S5P6450 supports only 2 VIC */
	u32 vic[2];

	/*
	 * VIC0 is missing IRQ_VIC0[(13-15), (21-22)]
	 * VIC1 is missing IRQ VIC1[12, 14, 23]
	 */
	vic[0] = 0xff9f1fff;
	vic[1] = 0xff7fafff;

	s5p_init_irq(vic, ARRAY_SIZE(vic));
}

struct sysdev_class s5p64x0_sysclass = {
	.name	= "s5p64x0-core",
};

static struct sys_device s5p64x0_sysdev = {
	.cls	= &s5p64x0_sysclass,
};

static int __init s5p64x0_core_init(void)
{
	return sysdev_class_register(&s5p64x0_sysclass);
}
core_initcall(s5p64x0_core_init);

int __init s5p64x0_init(void)
{
	printk(KERN_INFO "S5P64X0(S5P6440/S5P6450): Initializing architecture\n");

	/* set idle function */
	pm_idle = s5p64x0_idle;

	return sysdev_register(&s5p64x0_sysdev);
}

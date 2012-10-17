/*
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Copyright 2009 Samsung Electronics Co.
 *	Byungho Min <bhmin@samsung.com>
 *
 * Common Codes for S5PC100
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
#include <linux/device.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/sched.h>

#include <asm/irq.h>
#include <asm/proc-fns.h>
#include <asm/system_misc.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/map.h>
#include <mach/hardware.h>
#include <mach/regs-clock.h>

#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/clock.h>
#include <plat/sdhci.h>
#include <plat/adc-core.h>
#include <plat/ata-core.h>
#include <plat/fb-core.h>
#include <plat/iic-core.h>
#include <plat/onenand-core.h>
#include <plat/regs-serial.h>
#include <plat/watchdog-reset.h>

#include "common.h"

static const char name_s5pc100[] = "S5PC100";

static struct cpu_table cpu_ids[] __initdata = {
	{
		.idcode		= S5PC100_CPU_ID,
		.idmask		= S5PC100_CPU_MASK,
		.map_io		= s5pc100_map_io,
		.init_clocks	= s5pc100_init_clocks,
		.init_uarts	= s5pc100_init_uarts,
		.init		= s5pc100_init,
		.name		= name_s5pc100,
	},
};

/* Initial IO mappings */

static struct map_desc s5pc100_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S5P_VA_CHIPID,
		.pfn		= __phys_to_pfn(S5PC100_PA_CHIPID),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_SYS,
		.pfn		= __phys_to_pfn(S5PC100_PA_SYSCON),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_TIMER,
		.pfn		= __phys_to_pfn(S5PC100_PA_TIMER),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_WATCHDOG,
		.pfn		= __phys_to_pfn(S5PC100_PA_WATCHDOG),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_SROMC,
		.pfn		= __phys_to_pfn(S5PC100_PA_SROMC),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_SYSTIMER,
		.pfn		= __phys_to_pfn(S5PC100_PA_SYSTIMER),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_GPIO,
		.pfn		= __phys_to_pfn(S5PC100_PA_GPIO),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)VA_VIC0,
		.pfn		= __phys_to_pfn(S5PC100_PA_VIC0),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)VA_VIC1,
		.pfn		= __phys_to_pfn(S5PC100_PA_VIC1),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)VA_VIC2,
		.pfn		= __phys_to_pfn(S5PC100_PA_VIC2),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_UART,
		.pfn		= __phys_to_pfn(S3C_PA_UART),
		.length		= SZ_512K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5PC100_VA_OTHERS,
		.pfn		= __phys_to_pfn(S5PC100_PA_OTHERS),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}
};

/*
 * s5pc100_map_io
 *
 * register the standard CPU IO areas
 */

void __init s5pc100_init_io(struct map_desc *mach_desc, int size)
{
	/* initialize the io descriptors we need for initialization */
	iotable_init(s5pc100_iodesc, ARRAY_SIZE(s5pc100_iodesc));
	if (mach_desc)
		iotable_init(mach_desc, size);

	/* detect cpu id and rev. */
	s5p_init_cpu(S5P_VA_CHIPID);

	s3c_init_cpu(samsung_cpu_id, cpu_ids, ARRAY_SIZE(cpu_ids));
}

void __init s5pc100_map_io(void)
{
	/* initialise device information early */
	s5pc100_default_sdhci0();
	s5pc100_default_sdhci1();
	s5pc100_default_sdhci2();

	s3c_adc_setname("s3c64xx-adc");

	/* the i2c devices are directly compatible with s3c2440 */
	s3c_i2c0_setname("s3c2440-i2c");
	s3c_i2c1_setname("s3c2440-i2c");

	s3c_onenand_setname("s5pc100-onenand");
	s3c_fb_setname("s5pc100-fb");
	s3c_cfcon_setname("s5pc100-pata");
}

void __init s5pc100_init_clocks(int xtal)
{
	printk(KERN_DEBUG "%s: initializing clocks\n", __func__);

	s3c24xx_register_baseclocks(xtal);
	s5p_register_clocks(xtal);
	s5pc100_register_clocks();
	s5pc100_setup_clocks();
}

void __init s5pc100_init_irq(void)
{
	u32 vic[] = {~0, ~0, ~0};

	/* VIC0, VIC1, and VIC2 are fully populated. */
	s5p_init_irq(vic, ARRAY_SIZE(vic));
}

static struct bus_type s5pc100_subsys = {
	.name		= "s5pc100-core",
	.dev_name	= "s5pc100-core",
};

static struct device s5pc100_dev = {
	.bus	= &s5pc100_subsys,
};

static int __init s5pc100_core_init(void)
{
	return subsys_system_register(&s5pc100_subsys, NULL);
}
core_initcall(s5pc100_core_init);

int __init s5pc100_init(void)
{
	printk(KERN_INFO "S5PC100: Initializing architecture\n");
	return device_register(&s5pc100_dev);
}

/* uart registration process */

void __init s5pc100_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	s3c24xx_init_uartdevs("s3c6400-uart", s5p_uart_resources, cfg, no);
}

void s5pc100_restart(char mode, const char *cmd)
{
	if (mode != 's')
		arch_wdt_reset();

	soft_restart(0);
}

/*
 * Copyright (c) 2009-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Common Codes for S5PV210
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
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/dma-mapping.h>
#include <linux/serial_core.h>

#include <asm/proc-fns.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/map.h>
#include <mach/regs-clock.h>

#include <plat/cpu.h>
#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/sdhci.h>
#include <plat/adc-core.h>
#include <plat/ata-core.h>
#include <plat/fb-core.h>
#include <plat/fimc-core.h>
#include <plat/iic-core.h>
#include <plat/keypad-core.h>
#include <plat/tv-core.h>
#include <plat/spi-core.h>
#include <plat/regs-serial.h>

#include "common.h"

static const char name_s5pv210[] = "S5PV210/S5PC110";

static struct cpu_table cpu_ids[] __initdata = {
	{
		.idcode		= S5PV210_CPU_ID,
		.idmask		= S5PV210_CPU_MASK,
		.map_io		= s5pv210_map_io,
		.init_clocks	= s5pv210_init_clocks,
		.init_uarts	= s5pv210_init_uarts,
		.init		= s5pv210_init,
		.name		= name_s5pv210,
	},
};

/* Initial IO mappings */

static struct map_desc s5pv210_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S5P_VA_CHIPID,
		.pfn		= __phys_to_pfn(S5PV210_PA_CHIPID),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_SYS,
		.pfn		= __phys_to_pfn(S5PV210_PA_SYSCON),
		.length		= SZ_64K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_TIMER,
		.pfn		= __phys_to_pfn(S5PV210_PA_TIMER),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_WATCHDOG,
		.pfn		= __phys_to_pfn(S5PV210_PA_WATCHDOG),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_SROMC,
		.pfn		= __phys_to_pfn(S5PV210_PA_SROMC),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
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
		.virtual	= (unsigned long)S5P_VA_DMC0,
		.pfn		= __phys_to_pfn(S5PV210_PA_DMC0),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S5P_VA_DMC1,
		.pfn		= __phys_to_pfn(S5PV210_PA_DMC1),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)S3C_VA_USB_HSPHY,
		.pfn		=__phys_to_pfn(S5PV210_PA_HSPHY),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	}
};

void s5pv210_restart(char mode, const char *cmd)
{
	__raw_writel(0x1, S5P_SWRESET);
}

/*
 * s5pv210_map_io
 *
 * register the standard cpu IO areas
 */

void __init s5pv210_init_io(struct map_desc *mach_desc, int size)
{
	/* initialize the io descriptors we need for initialization */
	iotable_init(s5pv210_iodesc, ARRAY_SIZE(s5pv210_iodesc));
	if (mach_desc)
		iotable_init(mach_desc, size);

	/* detect cpu id and rev. */
	s5p_init_cpu(S5P_VA_CHIPID);

	s3c_init_cpu(samsung_cpu_id, cpu_ids, ARRAY_SIZE(cpu_ids));
}

void __init s5pv210_map_io(void)
{
	init_consistent_dma_size(14 << 20);

	/* initialise device information early */
	s5pv210_default_sdhci0();
	s5pv210_default_sdhci1();
	s5pv210_default_sdhci2();
	s5pv210_default_sdhci3();

	s3c_adc_setname("samsung-adc-v3");

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

	/* setup TV devices */
	s5p_hdmi_setname("s5pv210-hdmi");

	s3c64xx_spi_setname("s5pv210-spi");
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

struct bus_type s5pv210_subsys = {
	.name		= "s5pv210-core",
	.dev_name	= "s5pv210-core",
};

static struct device s5pv210_dev = {
	.bus	= &s5pv210_subsys,
};

static int __init s5pv210_core_init(void)
{
	return subsys_system_register(&s5pv210_subsys, NULL);
}
core_initcall(s5pv210_core_init);

int __init s5pv210_init(void)
{
	printk(KERN_INFO "S5PV210: Initializing architecture\n");
	return device_register(&s5pv210_dev);
}

/* uart registration process */

void __init s5pv210_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	s3c24xx_init_uartdevs("s5pv210-uart", s5p_uart_resources, cfg, no);
}

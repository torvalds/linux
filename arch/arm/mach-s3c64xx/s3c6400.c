/* linux/arch/arm/mach-s3c64xx/cpu.c
 *
 * Copyright 2009 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/*
 * NOTE: Code in this file is not used when booting with Device Tree support.
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
#include <linux/serial_s3c.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <asm/irq.h>

#include <plat/cpu-freq.h>
#include <mach/regs-clock.h>

#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/sdhci.h>
#include <plat/iic-core.h>
#include <plat/onenand-core.h>

#include "common.h"

void __init s3c6400_map_io(void)
{
	/* setup SDHCI */

	s3c6400_default_sdhci0();
	s3c6400_default_sdhci1();
	s3c6400_default_sdhci2();

	/* the i2c devices are directly compatible with s3c2440 */
	s3c_i2c0_setname("s3c2440-i2c");

	s3c_device_nand.name = "s3c6400-nand";

	s3c_onenand_setname("s3c6400-onenand");
	s3c64xx_onenand1_setname("s3c6400-onenand");
}

void __init s3c6400_init_irq(void)
{
	/* VIC0 does not have IRQS 5..7,
	 * VIC1 is fully populated. */
	s3c64xx_init_irq(~0 & ~(0xf << 5), ~0);
}

static struct bus_type s3c6400_subsys = {
	.name		= "s3c6400-core",
	.dev_name	= "s3c6400-core",
};

static struct device s3c6400_dev = {
	.bus	= &s3c6400_subsys,
};

static int __init s3c6400_core_init(void)
{
	/* Not applicable when using DT. */
	if (of_have_populated_dt())
		return 0;

	return subsys_system_register(&s3c6400_subsys, NULL);
}

core_initcall(s3c6400_core_init);

int __init s3c6400_init(void)
{
	printk("S3C6400: Initialising architecture\n");

	return device_register(&s3c6400_dev);
}

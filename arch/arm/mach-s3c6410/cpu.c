/* linux/arch/arm/mach-s3c6410/cpu.c
 *
 * Copyright 2008 Simtec Electronics
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
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

#include <mach/hardware.h>
#include <asm/irq.h>

#include <plat/cpu-freq.h>
#include <plat/regs-serial.h>
#include <plat/regs-clock.h>

#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/clock.h>
#include <plat/sdhci.h>
#include <plat/iic-core.h>
#include <plat/s3c6400.h>
#include <plat/s3c6410.h>

/* Initial IO mappings */

static struct map_desc s3c6410_iodesc[] __initdata = {
};

/* s3c6410_map_io
 *
 * register the standard cpu IO areas
*/

void __init s3c6410_map_io(void)
{
	iotable_init(s3c6410_iodesc, ARRAY_SIZE(s3c6410_iodesc));

	/* initialise device information early */
	s3c6410_default_sdhci0();
	s3c6410_default_sdhci1();

	/* the i2c devices are directly compatible with s3c2440 */
	s3c_i2c0_setname("s3c2440-i2c");
	s3c_i2c1_setname("s3c2440-i2c");

	s3c_device_nand.name = "s3c6400-nand";
}

void __init s3c6410_init_clocks(int xtal)
{
	printk(KERN_DEBUG "%s: initialising clocks\n", __func__);
	s3c24xx_register_baseclocks(xtal);
	s3c64xx_register_clocks();
	s3c6400_register_clocks(S3C6410_CLKDIV0_ARM_MASK);
	s3c6400_setup_clocks();
}

void __init s3c6410_init_irq(void)
{
	/* VIC0 is missing IRQ7, VIC1 is fully populated. */
	s3c64xx_init_irq(~0 & ~(1 << 7), ~0);
}

struct sysdev_class s3c6410_sysclass = {
	.name	= "s3c6410-core",
};

static struct sys_device s3c6410_sysdev = {
	.cls	= &s3c6410_sysclass,
};

static int __init s3c6410_core_init(void)
{
	return sysdev_class_register(&s3c6410_sysclass);
}

core_initcall(s3c6410_core_init);

int __init s3c6410_init(void)
{
	printk("S3C6410: Initialising architecture\n");

	return sysdev_register(&s3c6410_sysdev);
}

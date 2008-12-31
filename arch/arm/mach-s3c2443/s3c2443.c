/* linux/arch/arm/mach-s3c2443/s3c2443.c
 *
 * Copyright (c) 2007 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * Samsung S3C2443 Mobile CPU support
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
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/sysdev.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <asm/irq.h>

#include <mach/regs-s3c2443-clock.h>
#include <mach/reset.h>

#include <plat/s3c2443.h>
#include <plat/devs.h>
#include <plat/cpu.h>

static struct map_desc s3c2443_iodesc[] __initdata = {
	IODESC_ENT(WATCHDOG),
	IODESC_ENT(CLKPWR),
	IODESC_ENT(TIMER),
};

struct sysdev_class s3c2443_sysclass = {
	.name = "s3c2443-core",
};

static struct sys_device s3c2443_sysdev = {
	.cls		= &s3c2443_sysclass,
};

static void s3c2443_hard_reset(void)
{
	__raw_writel(S3C2443_SWRST_RESET, S3C2443_SWRST);
}

int __init s3c2443_init(void)
{
	printk("S3C2443: Initialising architecture\n");

	s3c24xx_reset_hook = s3c2443_hard_reset;

	s3c_device_nand.name = "s3c2412-nand";

	/* change WDT IRQ number */
	s3c_device_wdt.resource[1].start = IRQ_S3C2443_WDT;
	s3c_device_wdt.resource[1].end   = IRQ_S3C2443_WDT;

	return sysdev_register(&s3c2443_sysdev);
}

void __init s3c2443_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	s3c24xx_init_uartdevs("s3c2440-uart", s3c2410_uart_resources, cfg, no);
}

/* s3c2443_map_io
 *
 * register the standard cpu IO areas, and any passed in from the
 * machine specific initialisation.
 */

void __init s3c2443_map_io(void)
{
	iotable_init(s3c2443_iodesc, ARRAY_SIZE(s3c2443_iodesc));
}

/* need to register class before we actually register the device, and
 * we also need to ensure that it has been initialised before any of the
 * drivers even try to use it (even if not on an s3c2443 based system)
 * as a driver which may support both 2443 and 2440 may try and use it.
*/

static int __init s3c2443_core_init(void)
{
	return sysdev_class_register(&s3c2443_sysclass);
}

core_initcall(s3c2443_core_init);

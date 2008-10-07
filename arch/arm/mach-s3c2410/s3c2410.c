/* linux/arch/arm/mach-s3c2410/s3c2410.c
 *
 * Copyright (c) 2003-2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * http://www.simtec.co.uk/products/EB2410ITX/
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
#include <linux/sysdev.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <mach/regs-clock.h>
#include <plat/regs-serial.h>

#include <plat/s3c2410.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <asm/plat-s3c24xx/clock.h>

/* Initial IO mappings */

static struct map_desc s3c2410_iodesc[] __initdata = {
	IODESC_ENT(CLKPWR),
	IODESC_ENT(TIMER),
	IODESC_ENT(WATCHDOG),
};

/* our uart devices */

/* uart registration process */

void __init s3c2410_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	s3c24xx_init_uartdevs("s3c2410-uart", s3c2410_uart_resources, cfg, no);
}

/* s3c2410_map_io
 *
 * register the standard cpu IO areas, and any passed in from the
 * machine specific initialisation.
*/

void __init s3c2410_map_io(struct map_desc *mach_desc, int mach_size)
{
	/* register our io-tables */

	iotable_init(s3c2410_iodesc, ARRAY_SIZE(s3c2410_iodesc));
	iotable_init(mach_desc, mach_size);
}

void __init s3c2410_init_clocks(int xtal)
{
	unsigned long tmp;
	unsigned long fclk;
	unsigned long hclk;
	unsigned long pclk;

	/* now we've got our machine bits initialised, work out what
	 * clocks we've got */

	fclk = s3c2410_get_pll(__raw_readl(S3C2410_MPLLCON), xtal);

	tmp = __raw_readl(S3C2410_CLKDIVN);

	/* work out clock scalings */

	hclk = fclk / ((tmp & S3C2410_CLKDIVN_HDIVN) ? 2 : 1);
	pclk = hclk / ((tmp & S3C2410_CLKDIVN_PDIVN) ? 2 : 1);

	/* print brieft summary of clocks, etc */

	printk("S3C2410: core %ld.%03ld MHz, memory %ld.%03ld MHz, peripheral %ld.%03ld MHz\n",
	       print_mhz(fclk), print_mhz(hclk), print_mhz(pclk));

	/* initialise the clocks here, to allow other things like the
	 * console to use them
	 */

	s3c24xx_setup_clocks(xtal, fclk, hclk, pclk);
	s3c2410_baseclk_add();
}

struct sysdev_class s3c2410_sysclass = {
	.name = "s3c2410-core",
};

static struct sys_device s3c2410_sysdev = {
	.cls		= &s3c2410_sysclass,
};

/* need to register class before we actually register the device, and
 * we also need to ensure that it has been initialised before any of the
 * drivers even try to use it (even if not on an s3c2410 based system)
 * as a driver which may support both 2410 and 2440 may try and use it.
*/

static int __init s3c2410_core_init(void)
{
	return sysdev_class_register(&s3c2410_sysclass);
}

core_initcall(s3c2410_core_init);

int __init s3c2410_init(void)
{
	printk("S3C2410: Initialising architecture\n");

	return sysdev_register(&s3c2410_sysdev);
}

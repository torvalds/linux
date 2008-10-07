/* linux/arch/arm/plat-s3c24xx/s3c244x.c
 *
 * Copyright (c) 2004-2006 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * Samsung S3C2440 and S3C2442 Mobile CPU support (not S3C2443)
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
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/sysdev.h>
#include <linux/clk.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <mach/regs-clock.h>
#include <plat/regs-serial.h>
#include <mach/regs-gpio.h>
#include <mach/regs-gpioj.h>
#include <mach/regs-dsc.h>

#include <plat/s3c2410.h>
#include <plat/s3c2440.h>
#include "s3c244x.h"
#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/pm.h>

static struct map_desc s3c244x_iodesc[] __initdata = {
	IODESC_ENT(CLKPWR),
	IODESC_ENT(TIMER),
	IODESC_ENT(WATCHDOG),
};

/* uart initialisation */

void __init s3c244x_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	s3c24xx_init_uartdevs("s3c2440-uart", s3c2410_uart_resources, cfg, no);
}

void __init s3c244x_map_io(struct map_desc *mach_desc, int size)
{
	/* register our io-tables */

	iotable_init(s3c244x_iodesc, ARRAY_SIZE(s3c244x_iodesc));
	iotable_init(mach_desc, size);

	/* rename any peripherals used differing from the s3c2410 */

	s3c_device_sdi.name  = "s3c2440-sdi";
	s3c_device_i2c.name  = "s3c2440-i2c";
	s3c_device_nand.name = "s3c2440-nand";
	s3c_device_usbgadget.name = "s3c2440-usbgadget";
}

void __init s3c244x_init_clocks(int xtal)
{
	unsigned long clkdiv;
	unsigned long camdiv;
	unsigned long hclk, fclk, pclk;
	int hdiv = 1;

	/* now we've got our machine bits initialised, work out what
	 * clocks we've got */

	fclk = s3c2410_get_pll(__raw_readl(S3C2410_MPLLCON), xtal) * 2;

	clkdiv = __raw_readl(S3C2410_CLKDIVN);
	camdiv = __raw_readl(S3C2440_CAMDIVN);

	/* work out clock scalings */

	switch (clkdiv & S3C2440_CLKDIVN_HDIVN_MASK) {
	case S3C2440_CLKDIVN_HDIVN_1:
		hdiv = 1;
		break;

	case S3C2440_CLKDIVN_HDIVN_2:
		hdiv = 2;
		break;

	case S3C2440_CLKDIVN_HDIVN_4_8:
		hdiv = (camdiv & S3C2440_CAMDIVN_HCLK4_HALF) ? 8 : 4;
		break;

	case S3C2440_CLKDIVN_HDIVN_3_6:
		hdiv = (camdiv & S3C2440_CAMDIVN_HCLK3_HALF) ? 6 : 3;
		break;
	}

	hclk = fclk / hdiv;
	pclk = hclk / ((clkdiv & S3C2440_CLKDIVN_PDIVN)? 2:1);

	/* print brief summary of clocks, etc */

	printk("S3C244X: core %ld.%03ld MHz, memory %ld.%03ld MHz, peripheral %ld.%03ld MHz\n",
	       print_mhz(fclk), print_mhz(hclk), print_mhz(pclk));

	/* initialise the clocks here, to allow other things like the
	 * console to use them, and to add new ones after the initialisation
	 */

	s3c24xx_setup_clocks(xtal, fclk, hclk, pclk);
	s3c2410_baseclk_add();
}

#ifdef CONFIG_PM

static struct sleep_save s3c244x_sleep[] = {
	SAVE_ITEM(S3C2440_DSC0),
	SAVE_ITEM(S3C2440_DSC1),
	SAVE_ITEM(S3C2440_GPJDAT),
	SAVE_ITEM(S3C2440_GPJCON),
	SAVE_ITEM(S3C2440_GPJUP)
};

static int s3c244x_suspend(struct sys_device *dev, pm_message_t state)
{
	s3c2410_pm_do_save(s3c244x_sleep, ARRAY_SIZE(s3c244x_sleep));
	return 0;
}

static int s3c244x_resume(struct sys_device *dev)
{
	s3c2410_pm_do_restore(s3c244x_sleep, ARRAY_SIZE(s3c244x_sleep));
	return 0;
}

#else
#define s3c244x_suspend NULL
#define s3c244x_resume  NULL
#endif

/* Since the S3C2442 and S3C2440 share  items, put both sysclasses here */

struct sysdev_class s3c2440_sysclass = {
	.name		= "s3c2440-core",
	.suspend	= s3c244x_suspend,
	.resume		= s3c244x_resume
};

struct sysdev_class s3c2442_sysclass = {
	.name		= "s3c2442-core",
	.suspend	= s3c244x_suspend,
	.resume		= s3c244x_resume
};

/* need to register class before we actually register the device, and
 * we also need to ensure that it has been initialised before any of the
 * drivers even try to use it (even if not on an s3c2440 based system)
 * as a driver which may support both 2410 and 2440 may try and use it.
*/

static int __init s3c2440_core_init(void)
{
	return sysdev_class_register(&s3c2440_sysclass);
}

core_initcall(s3c2440_core_init);

static int __init s3c2442_core_init(void)
{
	return sysdev_class_register(&s3c2442_sysclass);
}

core_initcall(s3c2442_core_init);

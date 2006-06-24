/* linux/arch/arm/mach-s3c2410/s3c2412.c
 *
 * Copyright (c) 2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * http://armlinux.simtec.co.uk/.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *     16-May-2003 BJD  Created initial version
 *     16-Aug-2003 BJD  Fixed header files and copyright, added URL
 *     05-Sep-2003 BJD  Moved to kernel v2.6
 *     18-Jan-2004 BJD  Added serial port configuration
 *     21-Aug-2004 BJD  Added new struct s3c2410_board handler
 *     28-Sep-2004 BJD  Updates for new serial port bits
 *     04-Nov-2004 BJD  Updated UART configuration process
 *     10-Jan-2005 BJD  Removed s3c2410_clock_tick_rate
 *     13-Aug-2005 DA   Removed UART from initial I/O mappings
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/sysdev.h>
#include <linux/platform_device.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <asm/arch/regs-clock.h>
#include <asm/arch/regs-serial.h>
#include <asm/arch/regs-gpio.h>
#include <asm/arch/regs-gpioj.h>
#include <asm/arch/regs-dsc.h>

#include "s3c2412.h"
#include "cpu.h"
#include "devs.h"
#include "clock.h"
#include "pm.h"

#ifndef CONFIG_CPU_S3C2412_ONLY
void __iomem *s3c24xx_va_gpio2 = S3C24XX_VA_GPIO;
#endif

/* Initial IO mappings */

static struct map_desc s3c2412_iodesc[] __initdata = {
	IODESC_ENT(CLKPWR),
	IODESC_ENT(LCD),
	IODESC_ENT(TIMER),
	IODESC_ENT(ADC),
	IODESC_ENT(WATCHDOG),
};

/* uart registration process */

void __init s3c2412_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	s3c24xx_init_uartdevs("s3c2412-uart", s3c2410_uart_resources, cfg, no);

	/* rename devices that are s3c2412/s3c2413 specific */
	s3c_device_sdi.name  = "s3c2412-sdi";
	s3c_device_nand.name = "s3c2412-nand";
}

/* s3c2412_map_io
 *
 * register the standard cpu IO areas, and any passed in from the
 * machine specific initialisation.
*/

void __init s3c2412_map_io(struct map_desc *mach_desc, int mach_size)
{
	/* move base of IO */

	s3c24xx_va_gpio2 = S3C24XX_VA_GPIO + 0x10;

	/* register our io-tables */

	iotable_init(s3c2412_iodesc, ARRAY_SIZE(s3c2412_iodesc));
	iotable_init(mach_desc, mach_size);
}

void __init s3c2412_init_clocks(int xtal)
{
	unsigned long tmp;
	unsigned long fclk;
	unsigned long hclk;
	unsigned long pclk;

	/* now we've got our machine bits initialised, work out what
	 * clocks we've got */

	fclk = s3c2410_get_pll(__raw_readl(S3C2410_MPLLCON), xtal*2);

	tmp = __raw_readl(S3C2410_CLKDIVN);

	/* work out clock scalings */

	hclk = fclk / ((tmp & S3C2412_CLKDIVN_HDIVN_MASK) + 1);
	hclk /= ((tmp & S3C2421_CLKDIVN_ARMDIVN) ? 2 : 1);
	pclk = hclk / ((tmp & S3C2412_CLKDIVN_PDIVN) ? 2 : 1);

	/* print brieft summary of clocks, etc */

	printk("S3C2412: core %ld.%03ld MHz, memory %ld.%03ld MHz, peripheral %ld.%03ld MHz\n",
	       print_mhz(fclk), print_mhz(hclk), print_mhz(pclk));

	/* initialise the clocks here, to allow other things like the
	 * console to use them
	 */

	s3c24xx_setup_clocks(xtal, fclk, hclk, pclk);
	s3c2412_baseclk_add();
}

/* need to register class before we actually register the device, and
 * we also need to ensure that it has been initialised before any of the
 * drivers even try to use it (even if not on an s3c2412 based system)
 * as a driver which may support both 2410 and 2440 may try and use it.
*/

#ifdef CONFIG_PM
static struct sleep_save s3c2412_sleep[] = {
	SAVE_ITEM(S3C2412_DSC0),
	SAVE_ITEM(S3C2412_DSC1),
	SAVE_ITEM(S3C2413_GPJDAT),
	SAVE_ITEM(S3C2413_GPJCON),
	SAVE_ITEM(S3C2413_GPJUP),

	/* save the sleep configuration anyway, just in case these
	 * get damaged during wakeup */

	SAVE_ITEM(S3C2412_GPBSLPCON),
	SAVE_ITEM(S3C2412_GPCSLPCON),
	SAVE_ITEM(S3C2412_GPDSLPCON),
	SAVE_ITEM(S3C2412_GPESLPCON),
	SAVE_ITEM(S3C2412_GPFSLPCON),
	SAVE_ITEM(S3C2412_GPGSLPCON),
	SAVE_ITEM(S3C2412_GPHSLPCON),
	SAVE_ITEM(S3C2413_GPJSLPCON),
};

static int s3c2412_suspend(struct sys_device *dev, pm_message_t state)
{
	s3c2410_pm_do_save(s3c2412_sleep, ARRAY_SIZE(s3c2412_sleep));
	return 0;
}

static int s3c2412_resume(struct sys_device *dev)
{
	s3c2410_pm_do_restore(s3c2412_sleep, ARRAY_SIZE(s3c2412_sleep));
	return 0;
}

#else
#define s3c2412_suspend NULL
#define s3c2412_resume  NULL
#endif

struct sysdev_class s3c2412_sysclass = {
	set_kset_name("s3c2412-core"),
	.suspend	= s3c2412_suspend,
	.resume		= s3c2412_resume
};

static int __init s3c2412_core_init(void)
{
	return sysdev_class_register(&s3c2412_sysclass);
}

core_initcall(s3c2412_core_init);

static struct sys_device s3c2412_sysdev = {
	.cls		= &s3c2412_sysclass,
};

int __init s3c2412_init(void)
{
	printk("S3C2412: Initialising architecture\n");

	return sysdev_register(&s3c2412_sysdev);
}

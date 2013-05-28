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
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/system_misc.h>

#include <mach/regs-s3c2443-clock.h>

#include <plat/gpio-core.h>
#include <plat/gpio-cfg.h>
#include <plat/gpio-cfg-helpers.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/fb-core.h>
#include <plat/nand-core.h>
#include <plat/adc-core.h>
#include <plat/rtc-core.h>
#include <plat/spi-core.h>

static struct map_desc s3c2443_iodesc[] __initdata = {
	IODESC_ENT(WATCHDOG),
	IODESC_ENT(CLKPWR),
	IODESC_ENT(TIMER),
};

struct bus_type s3c2443_subsys = {
	.name = "s3c2443-core",
	.dev_name = "s3c2443-core",
};

static struct device s3c2443_dev = {
	.bus		= &s3c2443_subsys,
};

void s3c2443_restart(char mode, const char *cmd)
{
	if (mode == 's')
		soft_restart(0);

	__raw_writel(S3C2443_SWRST_RESET, S3C2443_SWRST);
}

int __init s3c2443_init(void)
{
	printk("S3C2443: Initialising architecture\n");

	s3c_nand_setname("s3c2412-nand");
	s3c_fb_setname("s3c2443-fb");

	s3c_adc_setname("s3c2443-adc");
	s3c_rtc_setname("s3c2443-rtc");

	/* change WDT IRQ number */
	s3c_device_wdt.resource[1].start = IRQ_S3C2443_WDT;
	s3c_device_wdt.resource[1].end   = IRQ_S3C2443_WDT;

	return device_register(&s3c2443_dev);
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
	s3c24xx_gpiocfg_default.set_pull = s3c2443_gpio_setpull;
	s3c24xx_gpiocfg_default.get_pull = s3c2443_gpio_getpull;

	/* initialize device information early */
	s3c64xx_spi_setname("s3c2443-spi");

	iotable_init(s3c2443_iodesc, ARRAY_SIZE(s3c2443_iodesc));
}

/* need to register the subsystem before we actually register the device, and
 * we also need to ensure that it has been initialised before any of the
 * drivers even try to use it (even if not on an s3c2443 based system)
 * as a driver which may support both 2443 and 2440 may try and use it.
*/

static int __init s3c2443_core_init(void)
{
	return subsys_system_register(&s3c2443_subsys, NULL);
}

core_initcall(s3c2443_core_init);

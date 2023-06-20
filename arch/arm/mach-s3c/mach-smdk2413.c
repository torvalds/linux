// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2006 Simtec Electronics
//	Ben Dooks <ben@simtec.co.uk>
//
// Thanks to Dimity Andric (TomTom) and Steven Ryu (Samsung) for the
// loans of SMDK2413 to work with.

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/serial_core.h>
#include <linux/serial_s3c.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/memblock.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/hardware/iomd.h>
#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

//#include <asm/debug-ll.h>
#include "hardware-s3c24xx.h"
#include "regs-gpio.h"

#include <linux/platform_data/usb-s3c2410_udc.h>
#include <linux/platform_data/i2c-s3c2410.h>
#include <linux/platform_data/fb-s3c2410.h>
#include "gpio-samsung.h"
#include "gpio-cfg.h"

#include "devs.h"
#include "cpu.h"

#include "s3c24xx.h"
#include "common-smdk-s3c24xx.h"

static struct map_desc smdk2413_iodesc[] __initdata = {
};

static struct s3c2410_uartcfg smdk2413_uartcfgs[] __initdata = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x51,
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x51,
	},
	/* IR port */
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x43,
		.ufcon	     = 0x51,
	}
};


static struct s3c2410_udc_mach_info smdk2413_udc_cfg __initdata = {
	.pullup_pin = S3C2410_GPF(2),
};


static struct platform_device *smdk2413_devices[] __initdata = {
	&s3c_device_ohci,
	&s3c_device_wdt,
	&s3c_device_i2c0,
	&s3c_device_iis,
	&s3c_device_usbgadget,
	&s3c2412_device_dma,
};

static void __init smdk2413_fixup(struct tag *tags, char **cmdline)
{
	if (tags != phys_to_virt(S3C2410_SDRAM_PA + 0x100)) {
		memblock_add(0x30000000, SZ_64M);
	}
}

static void __init smdk2413_map_io(void)
{
	s3c24xx_init_io(smdk2413_iodesc, ARRAY_SIZE(smdk2413_iodesc));
	s3c24xx_init_uarts(smdk2413_uartcfgs, ARRAY_SIZE(smdk2413_uartcfgs));
	s3c24xx_set_timer_source(S3C24XX_PWM3, S3C24XX_PWM4);
}

static void __init smdk2413_init_time(void)
{
	s3c2412_init_clocks(12000000);
	s3c24xx_timer_init();
}

static void __init smdk2413_machine_init(void)
{	/* Turn off suspend on both USB ports, and switch the
	 * selectable USB port to USB device mode. */

	s3c2410_modify_misccr(S3C2410_MISCCR_USBHOST |
			      S3C2410_MISCCR_USBSUSPND0 |
			      S3C2410_MISCCR_USBSUSPND1, 0x0);


 	s3c24xx_udc_set_platdata(&smdk2413_udc_cfg);
	s3c_i2c0_set_platdata(NULL);
	/* Configure the I2S pins (GPE0...GPE4) in correct mode */
	s3c_gpio_cfgall_range(S3C2410_GPE(0), 5, S3C_GPIO_SFN(2),
			      S3C_GPIO_PULL_NONE);

	platform_add_devices(smdk2413_devices, ARRAY_SIZE(smdk2413_devices));
	smdk_machine_init();
}

MACHINE_START(S3C2413, "S3C2413")
	/* Maintainer: Ben Dooks <ben-linux@fluff.org> */
	.atag_offset	= 0x100,
	.nr_irqs	= NR_IRQS_S3C2412,

	.fixup		= smdk2413_fixup,
	.init_irq	= s3c2412_init_irq,
	.map_io		= smdk2413_map_io,
	.init_machine	= smdk2413_machine_init,
	.init_time	= s3c24xx_timer_init,
MACHINE_END

MACHINE_START(SMDK2412, "SMDK2412")
	/* Maintainer: Ben Dooks <ben-linux@fluff.org> */
	.atag_offset	= 0x100,
	.nr_irqs	= NR_IRQS_S3C2412,

	.fixup		= smdk2413_fixup,
	.init_irq	= s3c2412_init_irq,
	.map_io		= smdk2413_map_io,
	.init_machine	= smdk2413_machine_init,
	.init_time	= s3c24xx_timer_init,
MACHINE_END

MACHINE_START(SMDK2413, "SMDK2413")
	/* Maintainer: Ben Dooks <ben-linux@fluff.org> */
	.atag_offset	= 0x100,
	.nr_irqs	= NR_IRQS_S3C2412,

	.fixup		= smdk2413_fixup,
	.init_irq	= s3c2412_init_irq,
	.map_io		= smdk2413_map_io,
	.init_machine	= smdk2413_machine_init,
	.init_time	= smdk2413_init_time,
MACHINE_END

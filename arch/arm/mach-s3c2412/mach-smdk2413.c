/* linux/arch/arm/mach-s3c2412/mach-smdk2413.c
 *
 * Copyright (c) 2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Thanks to Dimity Andric (TomTom) and Steven Ryu (Samsung) for the
 * loans of SMDK2413 to work with.
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
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <asm/hardware/iomd.h>
#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

//#include <asm/debug-ll.h>
#include <plat/regs-serial.h>
#include <mach/regs-gpio.h>
#include <mach/regs-lcd.h>

#include <mach/idle.h>
#include <plat/udc.h>
#include <plat/iic.h>
#include <mach/fb.h>

#include <plat/s3c2410.h>
#include <plat/s3c2412.h>
#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/cpu.h>

#include <plat/common-smdk.h>

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

static void smdk2413_udc_pullup(enum s3c2410_udc_cmd_e cmd)
{
	printk(KERN_DEBUG "udc: pullup(%d)\n",cmd);

	switch (cmd)
	{
		case S3C2410_UDC_P_ENABLE :
			gpio_set_value(S3C2410_GPF(2), 1);
			break;
		case S3C2410_UDC_P_DISABLE :
			gpio_set_value(S3C2410_GPF(2), 0);
			break;
		case S3C2410_UDC_P_RESET :
			break;
		default:
			break;
	}
}


static struct s3c2410_udc_mach_info smdk2413_udc_cfg __initdata = {
	.udc_command		= smdk2413_udc_pullup,
};


static struct platform_device *smdk2413_devices[] __initdata = {
	&s3c_device_ohci,
	&s3c_device_wdt,
	&s3c_device_i2c0,
	&s3c_device_iis,
	&s3c_device_usbgadget,
};

static void __init smdk2413_fixup(struct machine_desc *desc,
				  struct tag *tags, char **cmdline,
				  struct meminfo *mi)
{
	if (tags != phys_to_virt(S3C2410_SDRAM_PA + 0x100)) {
		mi->nr_banks=1;
		mi->bank[0].start = 0x30000000;
		mi->bank[0].size = SZ_64M;
	}
}

static void __init smdk2413_map_io(void)
{
	s3c24xx_init_io(smdk2413_iodesc, ARRAY_SIZE(smdk2413_iodesc));
	s3c24xx_init_clocks(12000000);
	s3c24xx_init_uarts(smdk2413_uartcfgs, ARRAY_SIZE(smdk2413_uartcfgs));
}

static void __init smdk2413_machine_init(void)
{	/* Turn off suspend on both USB ports, and switch the
	 * selectable USB port to USB device mode. */

	WARN_ON(gpio_request(S3C2410_GPF(2), "udc pull"));
	gpio_direction_output(S3C2410_GPF(2), 0);

	s3c2410_modify_misccr(S3C2410_MISCCR_USBHOST |
			      S3C2410_MISCCR_USBSUSPND0 |
			      S3C2410_MISCCR_USBSUSPND1, 0x0);


 	s3c24xx_udc_set_platdata(&smdk2413_udc_cfg);
	s3c_i2c0_set_platdata(NULL);

	platform_add_devices(smdk2413_devices, ARRAY_SIZE(smdk2413_devices));
	smdk_machine_init();
}

MACHINE_START(S3C2413, "S3C2413")
	/* Maintainer: Ben Dooks <ben-linux@fluff.org> */
	.boot_params	= S3C2410_SDRAM_PA + 0x100,

	.fixup		= smdk2413_fixup,
	.init_irq	= s3c24xx_init_irq,
	.map_io		= smdk2413_map_io,
	.init_machine	= smdk2413_machine_init,
	.timer		= &s3c24xx_timer,
MACHINE_END

MACHINE_START(SMDK2412, "SMDK2412")
	/* Maintainer: Ben Dooks <ben-linux@fluff.org> */
	.boot_params	= S3C2410_SDRAM_PA + 0x100,

	.fixup		= smdk2413_fixup,
	.init_irq	= s3c24xx_init_irq,
	.map_io		= smdk2413_map_io,
	.init_machine	= smdk2413_machine_init,
	.timer		= &s3c24xx_timer,
MACHINE_END

MACHINE_START(SMDK2413, "SMDK2413")
	/* Maintainer: Ben Dooks <ben-linux@fluff.org> */
	.boot_params	= S3C2410_SDRAM_PA + 0x100,

	.fixup		= smdk2413_fixup,
	.init_irq	= s3c24xx_init_irq,
	.map_io		= smdk2413_map_io,
	.init_machine	= smdk2413_machine_init,
	.timer		= &s3c24xx_timer,
MACHINE_END

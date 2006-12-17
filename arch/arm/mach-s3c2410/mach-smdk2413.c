/* linux/arch/arm/mach-s3c2410/mach-smdk2413.c
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
#include <linux/serial_core.h>
#include <linux/platform_device.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/hardware.h>
#include <asm/hardware/iomd.h>
#include <asm/setup.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

//#include <asm/debug-ll.h>
#include <asm/arch/regs-serial.h>
#include <asm/arch/regs-gpio.h>
#include <asm/arch/regs-lcd.h>

#include <asm/arch/idle.h>
#include <asm/arch/fb.h>

#include "s3c2410.h"
#include "s3c2412.h"
#include "clock.h"
#include "devs.h"
#include "cpu.h"

#include "common-smdk.h"

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

static struct platform_device *smdk2413_devices[] __initdata = {
	&s3c_device_usb,
	//&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c,
	&s3c_device_iis,
};

static struct s3c24xx_board smdk2413_board __initdata = {
	.devices       = smdk2413_devices,
	.devices_count = ARRAY_SIZE(smdk2413_devices)
};

static void __init smdk2413_fixup(struct machine_desc *desc,
				  struct tag *tags, char **cmdline,
				  struct meminfo *mi)
{
	if (tags != phys_to_virt(S3C2410_SDRAM_PA + 0x100)) {
		mi->nr_banks=1;
		mi->bank[0].start = 0x30000000;
		mi->bank[0].size = SZ_64M;
		mi->bank[0].node = 0;
	}
}

static void __init smdk2413_map_io(void)
{
	s3c24xx_init_io(smdk2413_iodesc, ARRAY_SIZE(smdk2413_iodesc));
	s3c24xx_init_clocks(12000000);
	s3c24xx_init_uarts(smdk2413_uartcfgs, ARRAY_SIZE(smdk2413_uartcfgs));
	s3c24xx_set_board(&smdk2413_board);
}

static void __init smdk2413_machine_init(void)
{
	smdk_machine_init();
}

MACHINE_START(S3C2413, "S3C2413")
	/* Maintainer: Ben Dooks <ben@fluff.org> */
	.phys_io	= S3C2410_PA_UART,
	.io_pg_offst	= (((u32)S3C24XX_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S3C2410_SDRAM_PA + 0x100,

	.fixup		= smdk2413_fixup,
	.init_irq	= s3c24xx_init_irq,
	.map_io		= smdk2413_map_io,
	.init_machine	= smdk2413_machine_init,
	.timer		= &s3c24xx_timer,
MACHINE_END

MACHINE_START(SMDK2413, "SMDK2413")
	/* Maintainer: Ben Dooks <ben@fluff.org> */
	.phys_io	= S3C2410_PA_UART,
	.io_pg_offst	= (((u32)S3C24XX_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S3C2410_SDRAM_PA + 0x100,

	.fixup		= smdk2413_fixup,
	.init_irq	= s3c24xx_init_irq,
	.map_io		= smdk2413_map_io,
	.init_machine	= smdk2413_machine_init,
	.timer		= &s3c24xx_timer,
MACHINE_END

/* linux/arch/arm/mach-s3c64xx/mach-real6410.c
 *
 * Copyright 2010 Darius Augulis <augulis.darius@gmail.com>
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/dm9000.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/map.h>
#include <mach/s3c6410.h>
#include <mach/regs-srom.h>
#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/regs-serial.h>

#define UCON (S3C2410_UCON_DEFAULT | S3C2410_UCON_UCLK)
#define ULCON (S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB)
#define UFCON (S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE)

static struct s3c2410_uartcfg real6410_uartcfgs[] __initdata = {
	[0] = {
		.hwport	= 0,
		.flags	= 0,
		.ucon	= UCON,
		.ulcon	= ULCON,
		.ufcon	= UFCON,
	},
	[1] = {
		.hwport	= 1,
		.flags	= 0,
		.ucon	= UCON,
		.ulcon	= ULCON,
		.ufcon	= UFCON,
	},
	[2] = {
		.hwport	= 2,
		.flags	= 0,
		.ucon	= UCON,
		.ulcon	= ULCON,
		.ufcon	= UFCON,
	},
	[3] = {
		.hwport	= 3,
		.flags	= 0,
		.ucon	= UCON,
		.ulcon	= ULCON,
		.ufcon	= UFCON,
	},
};

/* DM9000AEP 10/100 ethernet controller */

static struct resource real6410_dm9k_resource[] = {
	[0] = {
		.start	= S3C64XX_PA_XM0CSN1,
		.end	= S3C64XX_PA_XM0CSN1 + 1,
		.flags	= IORESOURCE_MEM
	},
	[1] = {
		.start	= S3C64XX_PA_XM0CSN1 + 4,
		.end	= S3C64XX_PA_XM0CSN1 + 5,
		.flags	= IORESOURCE_MEM
	},
	[2] = {
		.start	= S3C_EINT(7),
		.end	= S3C_EINT(7),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL
	}
};

static struct dm9000_plat_data real6410_dm9k_pdata = {
	.flags		= (DM9000_PLATF_16BITONLY | DM9000_PLATF_NO_EEPROM),
};

static struct platform_device real6410_device_eth = {
	.name		= "dm9000",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(real6410_dm9k_resource),
	.resource	= real6410_dm9k_resource,
	.dev		= {
		.platform_data	= &real6410_dm9k_pdata,
	},
};

static struct platform_device *real6410_devices[] __initdata = {
	&real6410_device_eth,
	&s3c_device_hsmmc0,
	&s3c_device_hsmmc1,
};

static void __init real6410_map_io(void)
{
	s3c64xx_init_io(NULL, 0);
	s3c24xx_init_clocks(12000000);
	s3c24xx_init_uarts(real6410_uartcfgs, ARRAY_SIZE(real6410_uartcfgs));
}

static void __init real6410_machine_init(void)
{
	u32 cs1;

	/* configure nCS1 width to 16 bits */

	cs1 = __raw_readl(S3C64XX_SROM_BW) &
		~(S3C64XX_SROM_BW__CS_MASK << S3C64XX_SROM_BW__NCS1__SHIFT);
	cs1 |= ((1 << S3C64XX_SROM_BW__DATAWIDTH__SHIFT) |
		(1 << S3C64XX_SROM_BW__WAITENABLE__SHIFT) |
		(1 << S3C64XX_SROM_BW__BYTEENABLE__SHIFT)) <<
			S3C64XX_SROM_BW__NCS1__SHIFT;
	__raw_writel(cs1, S3C64XX_SROM_BW);

	/* set timing for nCS1 suitable for ethernet chip */

	__raw_writel((0 << S3C64XX_SROM_BCX__PMC__SHIFT) |
		(6 << S3C64XX_SROM_BCX__TACP__SHIFT) |
		(4 << S3C64XX_SROM_BCX__TCAH__SHIFT) |
		(1 << S3C64XX_SROM_BCX__TCOH__SHIFT) |
		(13 << S3C64XX_SROM_BCX__TACC__SHIFT) |
		(4 << S3C64XX_SROM_BCX__TCOS__SHIFT) |
		(0 << S3C64XX_SROM_BCX__TACS__SHIFT), S3C64XX_SROM_BC1);

	platform_add_devices(real6410_devices, ARRAY_SIZE(real6410_devices));
}

MACHINE_START(REAL6410, "REAL6410")
	/* Maintainer: Darius Augulis <augulis.darius@gmail.com> */
	.boot_params	= S3C64XX_PA_SDRAM + 0x100,

	.init_irq	= s3c6410_init_irq,
	.map_io		= real6410_map_io,
	.init_machine	= real6410_machine_init,
	.timer		= &s3c24xx_timer,
MACHINE_END

// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2007 Simtec Electronics
//	Ben Dooks <ben@simtec.co.uk>
//
// http://www.fluff.org/ben/smdk2443/
//
// Thanks to Samsung for the loan of an SMDK2443

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/serial_s3c.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/irq.h>
#include <asm/mach-types.h>

#include "regs-gpio.h"

#include <linux/platform_data/fb-s3c2410.h>
#include <linux/platform_data/i2c-s3c2410.h>

#include "devs.h"
#include "cpu.h"

#include "s3c24xx.h"
#include "common-smdk-s3c24xx.h"

static struct map_desc smdk2443_iodesc[] __initdata = {
	/* ISA IO Space map (memory space selected by A24) */

	{
		.virtual	= (u32)S3C24XX_VA_ISA_WORD,
		.pfn		= __phys_to_pfn(S3C2410_CS2),
		.length		= 0x10000,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (u32)S3C24XX_VA_ISA_WORD + 0x10000,
		.pfn		= __phys_to_pfn(S3C2410_CS2 + (1<<24)),
		.length		= SZ_4M,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (u32)S3C24XX_VA_ISA_BYTE,
		.pfn		= __phys_to_pfn(S3C2410_CS2),
		.length		= 0x10000,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (u32)S3C24XX_VA_ISA_BYTE + 0x10000,
		.pfn		= __phys_to_pfn(S3C2410_CS2 + (1<<24)),
		.length		= SZ_4M,
		.type		= MT_DEVICE,
	}
};

#define UCON S3C2410_UCON_DEFAULT | S3C2410_UCON_UCLK
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg smdk2443_uartcfgs[] __initdata = {
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
	},
	[3] = {
		.hwport	     = 3,
		.flags	     = 0,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x51,
	}
};

static struct platform_device *smdk2443_devices[] __initdata = {
	&s3c_device_wdt,
	&s3c_device_i2c0,
	&s3c_device_hsmmc1,
	&s3c2443_device_dma,
};

static void __init smdk2443_map_io(void)
{
	s3c24xx_init_io(smdk2443_iodesc, ARRAY_SIZE(smdk2443_iodesc));
	s3c24xx_init_uarts(smdk2443_uartcfgs, ARRAY_SIZE(smdk2443_uartcfgs));
	s3c24xx_set_timer_source(S3C24XX_PWM3, S3C24XX_PWM4);
}

static void __init smdk2443_init_time(void)
{
	s3c2443_init_clocks(12000000);
	s3c24xx_timer_init();
}

static void __init smdk2443_machine_init(void)
{
	s3c_i2c0_set_platdata(NULL);
	platform_add_devices(smdk2443_devices, ARRAY_SIZE(smdk2443_devices));
	smdk_machine_init();
}

MACHINE_START(SMDK2443, "SMDK2443")
	/* Maintainer: Ben Dooks <ben-linux@fluff.org> */
	.atag_offset	= 0x100,

	.init_irq	= s3c2443_init_irq,
	.map_io		= smdk2443_map_io,
	.init_machine	= smdk2443_machine_init,
	.init_time	= smdk2443_init_time,
MACHINE_END

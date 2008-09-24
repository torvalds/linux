/* linux/arch/arm/mach-s3c2440/mach-at2440evb.c
 *
 * Copyright (c) 2008 Ramax Lo <ramaxlo@gmail.com>
 *      Based on mach-anubis.c by Ben Dooks <ben@simtec.co.uk>
 *      and modifications by SBZ <sbz@spgui.org> and
 *      Weibing <http://weibing.blogbus.com>
 *
 * For product information, visit http://www.arm9e.com/
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
#include <linux/io.h>
#include <linux/serial_core.h>
#include <linux/dm9000.h>
#include <linux/platform_device.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <asm/plat-s3c/regs-serial.h>
#include <mach/regs-gpio.h>
#include <mach/regs-mem.h>
#include <mach/regs-lcd.h>
#include <asm/plat-s3c/nand.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <asm/plat-s3c24xx/clock.h>
#include <asm/plat-s3c24xx/devs.h>
#include <asm/plat-s3c24xx/cpu.h>

static struct map_desc at2440evb_iodesc[] __initdata = {
	/* Nothing here */
};

#define UCON S3C2410_UCON_DEFAULT
#define ULCON (S3C2410_LCON_CS8 | S3C2410_LCON_PNONE)
#define UFCON (S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE)

static struct s3c24xx_uart_clksrc at2440evb_serial_clocks[] = {
	[0] = {
		.name		= "uclk",
		.divisor	= 1,
		.min_baud	= 0,
		.max_baud	= 0,
	},
	[1] = {
		.name		= "pclk",
		.divisor	= 1,
		.min_baud	= 0,
		.max_baud	= 0,
	}
};


static struct s3c2410_uartcfg at2440evb_uartcfgs[] __initdata = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = at2440evb_serial_clocks,
		.clocks_size = ARRAY_SIZE(at2440evb_serial_clocks),
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = at2440evb_serial_clocks,
		.clocks_size = ARRAY_SIZE(at2440evb_serial_clocks),
	},
};

/* NAND Flash on AT2440EVB board */

static struct mtd_partition at2440evb_default_nand_part[] = {
	[0] = {
		.name	= "Boot Agent",
		.size	= SZ_256K,
		.offset	= 0,
	},
	[1] = {
		.name	= "Kernel",
		.size	= SZ_2M,
		.offset	= SZ_256K,
	},
	[2] = {
		.name	= "Root",
		.offset	= SZ_256K + SZ_2M,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct s3c2410_nand_set at2440evb_nand_sets[] = {
	[0] = {
		.name		= "nand",
		.nr_chips	= 1,
		.nr_partitions	= ARRAY_SIZE(at2440evb_default_nand_part),
		.partitions	= at2440evb_default_nand_part,
	},
};

static struct s3c2410_platform_nand at2440evb_nand_info = {
	.tacls		= 25,
	.twrph0		= 55,
	.twrph1		= 40,
	.nr_sets	= ARRAY_SIZE(at2440evb_nand_sets),
	.sets		= at2440evb_nand_sets,
};

/* DM9000AEP 10/100 ethernet controller */

static struct resource at2440evb_dm9k_resource[] = {
	[0] = {
		.start = S3C2410_CS3,
		.end   = S3C2410_CS3 + 3,
		.flags = IORESOURCE_MEM
	},
	[1] = {
		.start = S3C2410_CS3 + 4,
		.end   = S3C2410_CS3 + 7,
		.flags = IORESOURCE_MEM
	},
	[2] = {
		.start = IRQ_EINT7,
		.end   = IRQ_EINT7,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	}
};

static struct dm9000_plat_data at2440evb_dm9k_pdata = {
	.flags		= (DM9000_PLATF_16BITONLY | DM9000_PLATF_NO_EEPROM),
};

static struct platform_device at2440evb_device_eth = {
	.name		= "dm9000",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(at2440evb_dm9k_resource),
	.resource	= at2440evb_dm9k_resource,
	.dev		= {
		.platform_data	= &at2440evb_dm9k_pdata,
	},
};

static struct platform_device *at2440evb_devices[] __initdata = {
	&s3c_device_usb,
	&s3c_device_wdt,
	&s3c_device_adc,
	&s3c_device_i2c,
	&s3c_device_rtc,
	&s3c_device_nand,
	&at2440evb_device_eth,
};

static void __init at2440evb_map_io(void)
{
	s3c_device_nand.dev.platform_data = &at2440evb_nand_info;

	s3c24xx_init_io(at2440evb_iodesc, ARRAY_SIZE(at2440evb_iodesc));
	s3c24xx_init_clocks(16934400);
	s3c24xx_init_uarts(at2440evb_uartcfgs, ARRAY_SIZE(at2440evb_uartcfgs));
}

static void __init at2440evb_init(void)
{
	platform_add_devices(at2440evb_devices, ARRAY_SIZE(at2440evb_devices));
}


MACHINE_START(AT2440EVB, "AT2440EVB")
	.phys_io	= S3C2410_PA_UART,
	.io_pg_offst	= (((u32)S3C24XX_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S3C2410_SDRAM_PA + 0x100,
	.map_io		= at2440evb_map_io,
	.init_machine	= at2440evb_init,
	.init_irq	= s3c24xx_init_irq,
	.timer		= &s3c24xx_timer,
MACHINE_END

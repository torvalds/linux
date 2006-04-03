/* linux/arch/arm/mach-s3c2410/mach-anubis.c
 *
 * Copyright (c) 2003-2005 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *	02-May-2005 BJD  Copied from mach-bast.c
 *	20-Sep-2005 BJD  Added static to non-exported items
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/arch/anubis-map.h>
#include <asm/arch/anubis-irq.h>
#include <asm/arch/anubis-cpld.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <asm/arch/regs-serial.h>
#include <asm/arch/regs-gpio.h>
#include <asm/arch/regs-mem.h>
#include <asm/arch/regs-lcd.h>
#include <asm/arch/nand.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include "clock.h"
#include "devs.h"
#include "cpu.h"

#define COPYRIGHT ", (c) 2005 Simtec Electronics"

static struct map_desc anubis_iodesc[] __initdata = {
  /* ISA IO areas */

  {
	.virtual	= (u32)S3C24XX_VA_ISA_BYTE,
	.pfn		= __phys_to_pfn(0x0),
	.length		= SZ_4M,
	.type		= MT_DEVICE
  }, {
	.virtual	= (u32)S3C24XX_VA_ISA_WORD,
	.pfn		= __phys_to_pfn(0x0),
	.length 	= SZ_4M, MT_DEVICE
  },

  /* we could possibly compress the next set down into a set of smaller tables
   * pagetables, but that would mean using an L2 section, and it still means
   * we cannot actually feed the same register to an LDR due to 16K spacing
   */

  /* CPLD control registers */

  {
	.virtual	= (u32)ANUBIS_VA_CTRL1,
	.pfn		= __phys_to_pfn(ANUBIS_PA_CTRL1),
	.length		= SZ_4K,
	.type		= MT_DEVICE
  }, {
	.virtual	= (u32)ANUBIS_VA_CTRL2,
	.pfn		= __phys_to_pfn(ANUBIS_PA_CTRL2),
	.length		= SZ_4K,
	.type		=MT_DEVICE
  },

  /* IDE drives */

  {
	.virtual	= (u32)ANUBIS_IDEPRI,
	.pfn		= __phys_to_pfn(S3C2410_CS3),
	.length		= SZ_1M,
	.type		= MT_DEVICE
  }, {
	.virtual	= (u32)ANUBIS_IDEPRIAUX,
	.pfn		= __phys_to_pfn(S3C2410_CS3+(1<<26)),
	.length		= SZ_1M,
	.type		= MT_DEVICE
  }, {
	.virtual	= (u32)ANUBIS_IDESEC,
	.pfn		= __phys_to_pfn(S3C2410_CS4),
	.length		= SZ_1M,
	.type		= MT_DEVICE
  }, {
	.virtual	= (u32)ANUBIS_IDESECAUX,
	.pfn		= __phys_to_pfn(S3C2410_CS4+(1<<26)),
	.length		= SZ_1M,
	.type		= MT_DEVICE
  },
};

#define UCON S3C2410_UCON_DEFAULT | S3C2410_UCON_UCLK
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

static struct s3c24xx_uart_clksrc anubis_serial_clocks[] = {
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
		.max_baud	= 0.
	}
};


static struct s3c2410_uartcfg anubis_uartcfgs[] = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = anubis_serial_clocks,
		.clocks_size = ARRAY_SIZE(anubis_serial_clocks)
	},
	[1] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = anubis_serial_clocks,
		.clocks_size = ARRAY_SIZE(anubis_serial_clocks)
	},
};

/* NAND Flash on Anubis board */

static int external_map[]   = { 2 };
static int chip0_map[]      = { 0 };
static int chip1_map[]      = { 1 };

static struct mtd_partition anubis_default_nand_part[] = {
	[0] = {
		.name	= "Boot Agent",
		.size	= SZ_16K,
		.offset	= 0
	},
	[1] = {
		.name	= "/boot",
		.size	= SZ_4M - SZ_16K,
		.offset	= SZ_16K,
	},
	[2] = {
		.name	= "user1",
		.offset	= SZ_4M,
		.size	= SZ_32M - SZ_4M,
	},
	[3] = {
		.name	= "user2",
		.offset	= SZ_32M,
		.size	= MTDPART_SIZ_FULL,
	}
};

/* the Anubis has 3 selectable slots for nand-flash, the two
 * on-board chip areas, as well as the external slot.
 *
 * Note, there is no current hot-plug support for the External
 * socket.
*/

static struct s3c2410_nand_set anubis_nand_sets[] = {
	[1] = {
		.name		= "External",
		.nr_chips	= 1,
		.nr_map		= external_map,
		.nr_partitions	= ARRAY_SIZE(anubis_default_nand_part),
		.partitions	= anubis_default_nand_part
	},
	[0] = {
		.name		= "chip0",
		.nr_chips	= 1,
		.nr_map		= chip0_map,
		.nr_partitions	= ARRAY_SIZE(anubis_default_nand_part),
		.partitions	= anubis_default_nand_part
	},
	[2] = {
		.name		= "chip1",
		.nr_chips	= 1,
		.nr_map		= chip1_map,
		.nr_partitions	= ARRAY_SIZE(anubis_default_nand_part),
		.partitions	= anubis_default_nand_part
	},
};

static void anubis_nand_select(struct s3c2410_nand_set *set, int slot)
{
	unsigned int tmp;

	slot = set->nr_map[slot] & 3;

	pr_debug("anubis_nand: selecting slot %d (set %p,%p)\n",
		 slot, set, set->nr_map);

	tmp = __raw_readb(ANUBIS_VA_CTRL1);
	tmp &= ~ANUBIS_CTRL1_NANDSEL;
	tmp |= slot;

	pr_debug("anubis_nand: ctrl1 now %02x\n", tmp);

	__raw_writeb(tmp, ANUBIS_VA_CTRL1);
}

static struct s3c2410_platform_nand anubis_nand_info = {
	.tacls		= 25,
	.twrph0		= 55,
	.twrph1		= 40,
	.nr_sets	= ARRAY_SIZE(anubis_nand_sets),
	.sets		= anubis_nand_sets,
	.select_chip	= anubis_nand_select,
};


/* Standard Anubis devices */

static struct platform_device *anubis_devices[] __initdata = {
	&s3c_device_usb,
	&s3c_device_wdt,
	&s3c_device_adc,
	&s3c_device_i2c,
 	&s3c_device_rtc,
	&s3c_device_nand,
};

static struct clk *anubis_clocks[] = {
	&s3c24xx_dclk0,
	&s3c24xx_dclk1,
	&s3c24xx_clkout0,
	&s3c24xx_clkout1,
	&s3c24xx_uclk,
};

static struct s3c24xx_board anubis_board __initdata = {
	.devices       = anubis_devices,
	.devices_count = ARRAY_SIZE(anubis_devices),
	.clocks	       = anubis_clocks,
	.clocks_count  = ARRAY_SIZE(anubis_clocks)
};

static void __init anubis_map_io(void)
{
	/* initialise the clocks */

	s3c24xx_dclk0.parent = NULL;
	s3c24xx_dclk0.rate   = 12*1000*1000;

	s3c24xx_dclk1.parent = NULL;
	s3c24xx_dclk1.rate   = 24*1000*1000;

	s3c24xx_clkout0.parent  = &s3c24xx_dclk0;
	s3c24xx_clkout1.parent  = &s3c24xx_dclk1;

	s3c24xx_uclk.parent  = &s3c24xx_clkout1;

	s3c_device_nand.dev.platform_data = &anubis_nand_info;

	s3c24xx_init_io(anubis_iodesc, ARRAY_SIZE(anubis_iodesc));
	s3c24xx_init_clocks(0);
	s3c24xx_init_uarts(anubis_uartcfgs, ARRAY_SIZE(anubis_uartcfgs));
	s3c24xx_set_board(&anubis_board);

	/* ensure that the GPIO is setup */
	s3c2410_gpio_setpin(S3C2410_GPA0, 1);
}

MACHINE_START(ANUBIS, "Simtec-Anubis")
	/* Maintainer: Ben Dooks <ben@simtec.co.uk> */
	.phys_io	= S3C2410_PA_UART,
	.io_pg_offst	= (((u32)S3C24XX_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S3C2410_SDRAM_PA + 0x100,
	.map_io		= anubis_map_io,
	.init_irq	= s3c24xx_init_irq,
	.timer		= &s3c24xx_timer,
MACHINE_END

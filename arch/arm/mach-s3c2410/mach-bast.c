/* linux/arch/arm/mach-s3c2410/mach-bast.c
 *
 * Copyright (c) 2003-2005 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * http://www.simtec.co.uk/products/EB2410ITX/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *     14-Sep-2004 BJD  USB power control
 *     20-Aug-2004 BJD  Added s3c2410_board struct
 *     18-Aug-2004 BJD  Added platform devices from default set
 *     16-May-2003 BJD  Created initial version
 *     16-Aug-2003 BJD  Fixed header files and copyright, added URL
 *     05-Sep-2003 BJD  Moved to v2.6 kernel
 *     06-Jan-2003 BJD  Updates for <arch/map.h>
 *     18-Jan-2003 BJD  Added serial port configuration
 *     05-Oct-2004 BJD  Power management code
 *     04-Nov-2004 BJD  Updated serial port clocks
 *     04-Jan-2005 BJD  New uart init call
 *     10-Jan-2005 BJD  Removed include of s3c2410.h
 *     14-Jan-2005 BJD  Add support for muitlple NAND devices
 *     03-Mar-2005 BJD  Ensured that bast-cpld.h is included
 *     10-Mar-2005 LCVR Changed S3C2410_VA to S3C24XX_VA
 *     14-Mar-2005 BJD  Updated for __iomem changes
 *     22-Jun-2005 BJD  Added DM9000 platform information
 *     28-Jun-2005 BJD  Moved pm functionality out to common code
 *     17-Jul-2005 BJD  Changed to platform device for SuperIO 16550s
 *     25-Jul-2005 BJD  Removed ASIX static mappings
 *     27-Jul-2005 BJD  Ensure maximum frequency of i2c bus
 *     20-Sep-2005 BJD  Added static to non-exported items
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/dm9000.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/arch/bast-map.h>
#include <asm/arch/bast-irq.h>
#include <asm/arch/bast-cpld.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

//#include <asm/debug-ll.h>
#include <asm/arch/regs-serial.h>
#include <asm/arch/regs-gpio.h>
#include <asm/arch/regs-mem.h>
#include <asm/arch/regs-lcd.h>
#include <asm/arch/nand.h>
#include <asm/arch/iic.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <linux/serial_8250.h>

#include "clock.h"
#include "devs.h"
#include "cpu.h"
#include "usb-simtec.h"

#define COPYRIGHT ", (c) 2004-2005 Simtec Electronics"

/* macros for virtual address mods for the io space entries */
#define VA_C5(item) ((unsigned long)(item) + BAST_VAM_CS5)
#define VA_C4(item) ((unsigned long)(item) + BAST_VAM_CS4)
#define VA_C3(item) ((unsigned long)(item) + BAST_VAM_CS3)
#define VA_C2(item) ((unsigned long)(item) + BAST_VAM_CS2)

/* macros to modify the physical addresses for io space */

#define PA_CS2(item) ((item) + S3C2410_CS2)
#define PA_CS3(item) ((item) + S3C2410_CS3)
#define PA_CS4(item) ((item) + S3C2410_CS4)
#define PA_CS5(item) ((item) + S3C2410_CS5)

static struct map_desc bast_iodesc[] __initdata = {
  /* ISA IO areas */

  { (u32)S3C24XX_VA_ISA_BYTE, PA_CS2(BAST_PA_ISAIO),   SZ_16M, MT_DEVICE },
  { (u32)S3C24XX_VA_ISA_WORD, PA_CS3(BAST_PA_ISAIO),   SZ_16M, MT_DEVICE },

  /* we could possibly compress the next set down into a set of smaller tables
   * pagetables, but that would mean using an L2 section, and it still means
   * we cannot actually feed the same register to an LDR due to 16K spacing
   */

  /* bast CPLD control registers, and external interrupt controls */
  { (u32)BAST_VA_CTRL1, BAST_PA_CTRL1,		   SZ_1M, MT_DEVICE },
  { (u32)BAST_VA_CTRL2, BAST_PA_CTRL2,		   SZ_1M, MT_DEVICE },
  { (u32)BAST_VA_CTRL3, BAST_PA_CTRL3,		   SZ_1M, MT_DEVICE },
  { (u32)BAST_VA_CTRL4, BAST_PA_CTRL4,		   SZ_1M, MT_DEVICE },

  /* PC104 IRQ mux */
  { (u32)BAST_VA_PC104_IRQREQ,  BAST_PA_PC104_IRQREQ,   SZ_1M, MT_DEVICE },
  { (u32)BAST_VA_PC104_IRQRAW,  BAST_PA_PC104_IRQRAW,   SZ_1M, MT_DEVICE },
  { (u32)BAST_VA_PC104_IRQMASK, BAST_PA_PC104_IRQMASK,  SZ_1M, MT_DEVICE },

  /* peripheral space... one for each of fast/slow/byte/16bit */
  /* note, ide is only decoded in word space, even though some registers
   * are only 8bit */

  /* slow, byte */
  { VA_C2(BAST_VA_ISAIO),   PA_CS2(BAST_PA_ISAIO),    SZ_16M, MT_DEVICE },
  { VA_C2(BAST_VA_ISAMEM),  PA_CS2(BAST_PA_ISAMEM),   SZ_16M, MT_DEVICE },
  { VA_C2(BAST_VA_SUPERIO), PA_CS2(BAST_PA_SUPERIO),  SZ_1M,  MT_DEVICE },
  { VA_C2(BAST_VA_IDEPRI),  PA_CS3(BAST_PA_IDEPRI),   SZ_1M,  MT_DEVICE },
  { VA_C2(BAST_VA_IDESEC),  PA_CS3(BAST_PA_IDESEC),   SZ_1M,  MT_DEVICE },
  { VA_C2(BAST_VA_IDEPRIAUX), PA_CS3(BAST_PA_IDEPRIAUX), SZ_1M, MT_DEVICE },
  { VA_C2(BAST_VA_IDESECAUX), PA_CS3(BAST_PA_IDESECAUX), SZ_1M, MT_DEVICE },

  /* slow, word */
  { VA_C3(BAST_VA_ISAIO),   PA_CS3(BAST_PA_ISAIO),    SZ_16M, MT_DEVICE },
  { VA_C3(BAST_VA_ISAMEM),  PA_CS3(BAST_PA_ISAMEM),   SZ_16M, MT_DEVICE },
  { VA_C3(BAST_VA_SUPERIO), PA_CS3(BAST_PA_SUPERIO),  SZ_1M,  MT_DEVICE },
  { VA_C3(BAST_VA_IDEPRI),  PA_CS3(BAST_PA_IDEPRI),   SZ_1M,  MT_DEVICE },
  { VA_C3(BAST_VA_IDESEC),  PA_CS3(BAST_PA_IDESEC),   SZ_1M,  MT_DEVICE },
  { VA_C3(BAST_VA_IDEPRIAUX), PA_CS3(BAST_PA_IDEPRIAUX), SZ_1M, MT_DEVICE },
  { VA_C3(BAST_VA_IDESECAUX), PA_CS3(BAST_PA_IDESECAUX), SZ_1M, MT_DEVICE },

  /* fast, byte */
  { VA_C4(BAST_VA_ISAIO),   PA_CS4(BAST_PA_ISAIO),    SZ_16M, MT_DEVICE },
  { VA_C4(BAST_VA_ISAMEM),  PA_CS4(BAST_PA_ISAMEM),   SZ_16M, MT_DEVICE },
  { VA_C4(BAST_VA_SUPERIO), PA_CS4(BAST_PA_SUPERIO),  SZ_1M,  MT_DEVICE },
  { VA_C4(BAST_VA_IDEPRI),  PA_CS5(BAST_PA_IDEPRI),   SZ_1M,  MT_DEVICE },
  { VA_C4(BAST_VA_IDESEC),  PA_CS5(BAST_PA_IDESEC),   SZ_1M,  MT_DEVICE },
  { VA_C4(BAST_VA_IDEPRIAUX), PA_CS5(BAST_PA_IDEPRIAUX), SZ_1M, MT_DEVICE },
  { VA_C4(BAST_VA_IDESECAUX), PA_CS5(BAST_PA_IDESECAUX), SZ_1M, MT_DEVICE },

  /* fast, word */
  { VA_C5(BAST_VA_ISAIO),   PA_CS5(BAST_PA_ISAIO),    SZ_16M, MT_DEVICE },
  { VA_C5(BAST_VA_ISAMEM),  PA_CS5(BAST_PA_ISAMEM),   SZ_16M, MT_DEVICE },
  { VA_C5(BAST_VA_SUPERIO), PA_CS5(BAST_PA_SUPERIO),  SZ_1M,  MT_DEVICE },
  { VA_C5(BAST_VA_IDEPRI),  PA_CS5(BAST_PA_IDEPRI),   SZ_1M,  MT_DEVICE },
  { VA_C5(BAST_VA_IDESEC),  PA_CS5(BAST_PA_IDESEC),   SZ_1M,  MT_DEVICE },
  { VA_C5(BAST_VA_IDEPRIAUX), PA_CS5(BAST_PA_IDEPRIAUX), SZ_1M, MT_DEVICE },
  { VA_C5(BAST_VA_IDESECAUX), PA_CS5(BAST_PA_IDESECAUX), SZ_1M, MT_DEVICE },
};

#define UCON S3C2410_UCON_DEFAULT | S3C2410_UCON_UCLK
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

static struct s3c24xx_uart_clksrc bast_serial_clocks[] = {
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


static struct s3c2410_uartcfg bast_uartcfgs[] = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = bast_serial_clocks,
		.clocks_size = ARRAY_SIZE(bast_serial_clocks)
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = bast_serial_clocks,
		.clocks_size = ARRAY_SIZE(bast_serial_clocks)
	},
	/* port 2 is not actually used */
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = bast_serial_clocks,
		.clocks_size = ARRAY_SIZE(bast_serial_clocks)
	}
};

/* NOR Flash on BAST board */

static struct resource bast_nor_resource[] = {
	[0] = {
		.start = S3C2410_CS1 + 0x4000000,
		.end   = S3C2410_CS1 + 0x4000000 + (32*1024*1024) - 1,
		.flags = IORESOURCE_MEM,
	}
};

static struct platform_device bast_device_nor = {
	.name		= "bast-nor",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(bast_nor_resource),
	.resource	= bast_nor_resource,
};

/* NAND Flash on BAST board */


static int smartmedia_map[] = { 0 };
static int chip0_map[] = { 1 };
static int chip1_map[] = { 2 };
static int chip2_map[] = { 3 };

static struct mtd_partition bast_default_nand_part[] = {
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
		.name	= "user",
		.offset	= SZ_4M,
		.size	= MTDPART_SIZ_FULL,
	}
};

/* the bast has 4 selectable slots for nand-flash, the three
 * on-board chip areas, as well as the external SmartMedia
 * slot.
 *
 * Note, there is no current hot-plug support for the SmartMedia
 * socket.
*/

static struct s3c2410_nand_set bast_nand_sets[] = {
	[0] = {
		.name		= "SmartMedia",
		.nr_chips	= 1,
		.nr_map		= smartmedia_map,
		.nr_partitions	= ARRAY_SIZE(bast_default_nand_part),
		.partitions	= bast_default_nand_part
	},
	[1] = {
		.name		= "chip0",
		.nr_chips	= 1,
		.nr_map		= chip0_map,
		.nr_partitions	= ARRAY_SIZE(bast_default_nand_part),
		.partitions	= bast_default_nand_part
	},
	[2] = {
		.name		= "chip1",
		.nr_chips	= 1,
		.nr_map		= chip1_map,
		.nr_partitions	= ARRAY_SIZE(bast_default_nand_part),
		.partitions	= bast_default_nand_part
	},
	[3] = {
		.name		= "chip2",
		.nr_chips	= 1,
		.nr_map		= chip2_map,
		.nr_partitions	= ARRAY_SIZE(bast_default_nand_part),
		.partitions	= bast_default_nand_part
	}
};

static void bast_nand_select(struct s3c2410_nand_set *set, int slot)
{
	unsigned int tmp;

	slot = set->nr_map[slot] & 3;

	pr_debug("bast_nand: selecting slot %d (set %p,%p)\n",
		 slot, set, set->nr_map);

	tmp = __raw_readb(BAST_VA_CTRL2);
	tmp &= BAST_CPLD_CTLR2_IDERST;
	tmp |= slot;
	tmp |= BAST_CPLD_CTRL2_WNAND;

	pr_debug("bast_nand: ctrl2 now %02x\n", tmp);

	__raw_writeb(tmp, BAST_VA_CTRL2);
}

static struct s3c2410_platform_nand bast_nand_info = {
	.tacls		= 40,
	.twrph0		= 80,
	.twrph1		= 80,
	.nr_sets	= ARRAY_SIZE(bast_nand_sets),
	.sets		= bast_nand_sets,
	.select_chip	= bast_nand_select,
};

/* DM9000 */

static struct resource bast_dm9k_resource[] = {
	[0] = {
		.start = S3C2410_CS5 + BAST_PA_DM9000,
		.end   = S3C2410_CS5 + BAST_PA_DM9000 + 3,
		.flags = IORESOURCE_MEM
	},
	[1] = {
		.start = S3C2410_CS5 + BAST_PA_DM9000 + 0x40,
		.end   = S3C2410_CS5 + BAST_PA_DM9000 + 0x40 + 0x3f,
		.flags = IORESOURCE_MEM
	},
	[2] = {
		.start = IRQ_DM9000,
		.end   = IRQ_DM9000,
		.flags = IORESOURCE_IRQ
	}

};

/* for the moment we limit ourselves to 16bit IO until some
 * better IO routines can be written and tested
*/

static struct dm9000_plat_data bast_dm9k_platdata = {
	.flags		= DM9000_PLATF_16BITONLY
};

static struct platform_device bast_device_dm9k = {
	.name		= "dm9000",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(bast_dm9k_resource),
	.resource	= bast_dm9k_resource,
	.dev		= {
		.platform_data = &bast_dm9k_platdata,
	}
};

/* serial devices */

#define SERIAL_BASE  (S3C2410_CS2 + BAST_PA_SUPERIO)
#define SERIAL_FLAGS (UPF_BOOT_AUTOCONF | UPF_IOREMAP | UPF_SHARE_IRQ)
#define SERIAL_CLK   (1843200)

static struct plat_serial8250_port bast_sio_data[] = {
	[0] = {
		.mapbase	= SERIAL_BASE + 0x2f8,
		.irq		= IRQ_PCSERIAL1,
		.flags		= SERIAL_FLAGS,
		.iotype		= UPIO_MEM,
		.regshift	= 0,
		.uartclk	= SERIAL_CLK,
	},
	[1] = {
		.mapbase	= SERIAL_BASE + 0x3f8,
		.irq		= IRQ_PCSERIAL2,
		.flags		= SERIAL_FLAGS,
		.iotype		= UPIO_MEM,
		.regshift	= 0,
		.uartclk	= SERIAL_CLK,
	},
	{ }
};

static struct platform_device bast_sio = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= &bast_sio_data,
	},
};

/* we have devices on the bus which cannot work much over the
 * standard 100KHz i2c bus frequency
*/

static struct s3c2410_platform_i2c bast_i2c_info = {
	.flags		= 0,
	.slave_addr	= 0x10,
	.bus_freq	= 100*1000,
	.max_freq	= 130*1000,
};

/* Standard BAST devices */

static struct platform_device *bast_devices[] __initdata = {
	&s3c_device_usb,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c,
	&s3c_device_iis,
 	&s3c_device_rtc,
	&s3c_device_nand,
	&bast_device_nor,
	&bast_device_dm9k,
	&bast_sio,
};

static struct clk *bast_clocks[] = {
	&s3c24xx_dclk0,
	&s3c24xx_dclk1,
	&s3c24xx_clkout0,
	&s3c24xx_clkout1,
	&s3c24xx_uclk,
};

static struct s3c24xx_board bast_board __initdata = {
	.devices       = bast_devices,
	.devices_count = ARRAY_SIZE(bast_devices),
	.clocks	       = bast_clocks,
	.clocks_count  = ARRAY_SIZE(bast_clocks)
};

static void __init bast_map_io(void)
{
	/* initialise the clocks */

	s3c24xx_dclk0.parent = NULL;
	s3c24xx_dclk0.rate   = 12*1000*1000;

	s3c24xx_dclk1.parent = NULL;
	s3c24xx_dclk1.rate   = 24*1000*1000;

	s3c24xx_clkout0.parent  = &s3c24xx_dclk0;
	s3c24xx_clkout1.parent  = &s3c24xx_dclk1;

	s3c24xx_uclk.parent  = &s3c24xx_clkout1;

	s3c_device_nand.dev.platform_data = &bast_nand_info;
	s3c_device_i2c.dev.platform_data = &bast_i2c_info;

	s3c24xx_init_io(bast_iodesc, ARRAY_SIZE(bast_iodesc));
	s3c24xx_init_clocks(0);
	s3c24xx_init_uarts(bast_uartcfgs, ARRAY_SIZE(bast_uartcfgs));
	s3c24xx_set_board(&bast_board);
	usb_simtec_init();
}


MACHINE_START(BAST, "Simtec-BAST")
	/* Maintainer: Ben Dooks <ben@simtec.co.uk> */
	.phys_ram	= S3C2410_SDRAM_PA,
	.phys_io	= S3C2410_PA_UART,
	.io_pg_offst	= (((u32)S3C24XX_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S3C2410_SDRAM_PA + 0x100,
	.map_io		= bast_map_io,
	.init_irq	= s3c24xx_init_irq,
	.timer		= &s3c24xx_timer,
MACHINE_END

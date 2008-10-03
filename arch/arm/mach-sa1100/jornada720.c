/*
 * linux/arch/arm/mach-sa1100/jornada720.c
 *
 * HP Jornada720 init code
 *
 * Copyright (C) 2007 Kristoffer Ericson <Kristoffer.Ericson@gmail.com>
 * Copyright (C) 2006 Filip Zyzniewski <filip.zyzniewski@tefnet.pl>
 *  Copyright (C) 2005 Michael Gernoth <michael@gernoth.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <video/s1d13xxxfb.h>

#include <mach/hardware.h>
#include <asm/hardware/sa1111.h>
#include <asm/irq.h>
#include <asm/mach-types.h>
#include <asm/setup.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>

#include "generic.h"

/*
 * HP Documentation referred in this file:
 * http://www.jlime.com/downloads/development/docs/jornada7xx/jornada720.txt
 */

/* line 110 of HP's doc */
#define TUCR_VAL	0x20000400

/* memory space (line 52 of HP's doc) */
#define SA1111REGSTART	0x40000000
#define SA1111REGLEN	0x00001fff
#define EPSONREGSTART	0x48000000
#define EPSONREGLEN	0x00100000
#define EPSONFBSTART	0x48200000
/* 512kB framebuffer */
#define EPSONFBLEN	512*1024

static struct s1d13xxxfb_regval s1d13xxxfb_initregs[] = {
	/* line 344 of HP's doc */
	{0x0001,0x00},	// Miscellaneous Register
	{0x01FC,0x00},	// Display Mode Register
	{0x0004,0x00},	// General IO Pins Configuration Register 0
	{0x0005,0x00},	// General IO Pins Configuration Register 1
	{0x0008,0x00},	// General IO Pins Control Register 0
	{0x0009,0x00},	// General IO Pins Control Register 1
	{0x0010,0x01},	// Memory Clock Configuration Register
	{0x0014,0x11},	// LCD Pixel Clock Configuration Register
	{0x0018,0x01},	// CRT/TV Pixel Clock Configuration Register
	{0x001C,0x01},	// MediaPlug Clock Configuration Register
	{0x001E,0x01},	// CPU To Memory Wait State Select Register
	{0x0020,0x00},	// Memory Configuration Register
	{0x0021,0x45},	// DRAM Refresh Rate Register
	{0x002A,0x01},	// DRAM Timings Control Register 0
	{0x002B,0x03},	// DRAM Timings Control Register 1
	{0x0030,0x1c},	// Panel Type Register
	{0x0031,0x00},	// MOD Rate Register
	{0x0032,0x4F},	// LCD Horizontal Display Width Register
	{0x0034,0x07},	// LCD Horizontal Non-Display Period Register
	{0x0035,0x01},	// TFT FPLINE Start Position Register
	{0x0036,0x0B},	// TFT FPLINE Pulse Width Register
	{0x0038,0xEF},	// LCD Vertical Display Height Register 0
	{0x0039,0x00},	// LCD Vertical Display Height Register 1
	{0x003A,0x13},	// LCD Vertical Non-Display Period Register
	{0x003B,0x0B},	// TFT FPFRAME Start Position Register
	{0x003C,0x01},	// TFT FPFRAME Pulse Width Register
	{0x0040,0x05},	// LCD Display Mode Register (2:4bpp,3:8bpp,5:16bpp)
	{0x0041,0x00},	// LCD Miscellaneous Register
	{0x0042,0x00},	// LCD Display Start Address Register 0
	{0x0043,0x00},	// LCD Display Start Address Register 1
	{0x0044,0x00},	// LCD Display Start Address Register 2
	{0x0046,0x80},	// LCD Memory Address Offset Register 0
	{0x0047,0x02},	// LCD Memory Address Offset Register 1
	{0x0048,0x00},	// LCD Pixel Panning Register
	{0x004A,0x00},	// LCD Display FIFO High Threshold Control Register
	{0x004B,0x00},	// LCD Display FIFO Low Threshold Control Register
	{0x0050,0x4F},	// CRT/TV Horizontal Display Width Register
	{0x0052,0x13},	// CRT/TV Horizontal Non-Display Period Register
	{0x0053,0x01},	// CRT/TV HRTC Start Position Register
	{0x0054,0x0B},	// CRT/TV HRTC Pulse Width Register
	{0x0056,0xDF},	// CRT/TV Vertical Display Height Register 0
	{0x0057,0x01},	// CRT/TV Vertical Display Height Register 1
	{0x0058,0x2B},	// CRT/TV Vertical Non-Display Period Register
	{0x0059,0x09},	// CRT/TV VRTC Start Position Register
	{0x005A,0x01},	// CRT/TV VRTC Pulse Width Register
	{0x005B,0x10},	// TV Output Control Register
	{0x0060,0x03},	// CRT/TV Display Mode Register (2:4bpp,3:8bpp,5:16bpp)
	{0x0062,0x00},	// CRT/TV Display Start Address Register 0
	{0x0063,0x00},	// CRT/TV Display Start Address Register 1
	{0x0064,0x00},	// CRT/TV Display Start Address Register 2
	{0x0066,0x40},	// CRT/TV Memory Address Offset Register 0
	{0x0067,0x01},	// CRT/TV Memory Address Offset Register 1
	{0x0068,0x00},	// CRT/TV Pixel Panning Register
	{0x006A,0x00},	// CRT/TV Display FIFO High Threshold Control Register
	{0x006B,0x00},	// CRT/TV Display FIFO Low Threshold Control Register
	{0x0070,0x00},	// LCD Ink/Cursor Control Register
	{0x0071,0x01},	// LCD Ink/Cursor Start Address Register
	{0x0072,0x00},	// LCD Cursor X Position Register 0
	{0x0073,0x00},	// LCD Cursor X Position Register 1
	{0x0074,0x00},	// LCD Cursor Y Position Register 0
	{0x0075,0x00},	// LCD Cursor Y Position Register 1
	{0x0076,0x00},	// LCD Ink/Cursor Blue Color 0 Register
	{0x0077,0x00},	// LCD Ink/Cursor Green Color 0 Register
	{0x0078,0x00},	// LCD Ink/Cursor Red Color 0 Register
	{0x007A,0x1F},	// LCD Ink/Cursor Blue Color 1 Register
	{0x007B,0x3F},	// LCD Ink/Cursor Green Color 1 Register
	{0x007C,0x1F},	// LCD Ink/Cursor Red Color 1 Register
	{0x007E,0x00},	// LCD Ink/Cursor FIFO Threshold Register
	{0x0080,0x00},	// CRT/TV Ink/Cursor Control Register
	{0x0081,0x01},	// CRT/TV Ink/Cursor Start Address Register
	{0x0082,0x00},	// CRT/TV Cursor X Position Register 0
	{0x0083,0x00},	// CRT/TV Cursor X Position Register 1
	{0x0084,0x00},	// CRT/TV Cursor Y Position Register 0
	{0x0085,0x00},	// CRT/TV Cursor Y Position Register 1
	{0x0086,0x00},	// CRT/TV Ink/Cursor Blue Color 0 Register
	{0x0087,0x00},	// CRT/TV Ink/Cursor Green Color 0 Register
	{0x0088,0x00},	// CRT/TV Ink/Cursor Red Color 0 Register
	{0x008A,0x1F},	// CRT/TV Ink/Cursor Blue Color 1 Register
	{0x008B,0x3F},	// CRT/TV Ink/Cursor Green Color 1 Register
	{0x008C,0x1F},	// CRT/TV Ink/Cursor Red Color 1 Register
	{0x008E,0x00},	// CRT/TV Ink/Cursor FIFO Threshold Register
	{0x0100,0x00},	// BitBlt Control Register 0
	{0x0101,0x00},	// BitBlt Control Register 1
	{0x0102,0x00},	// BitBlt ROP Code/Color Expansion Register
	{0x0103,0x00},	// BitBlt Operation Register
	{0x0104,0x00},	// BitBlt Source Start Address Register 0
	{0x0105,0x00},	// BitBlt Source Start Address Register 1
	{0x0106,0x00},	// BitBlt Source Start Address Register 2
	{0x0108,0x00},	// BitBlt Destination Start Address Register 0
	{0x0109,0x00},	// BitBlt Destination Start Address Register 1
	{0x010A,0x00},	// BitBlt Destination Start Address Register 2
	{0x010C,0x00},	// BitBlt Memory Address Offset Register 0
	{0x010D,0x00},	// BitBlt Memory Address Offset Register 1
	{0x0110,0x00},	// BitBlt Width Register 0
	{0x0111,0x00},	// BitBlt Width Register 1
	{0x0112,0x00},	// BitBlt Height Register 0
	{0x0113,0x00},	// BitBlt Height Register 1
	{0x0114,0x00},	// BitBlt Background Color Register 0
	{0x0115,0x00},	// BitBlt Background Color Register 1
	{0x0118,0x00},	// BitBlt Foreground Color Register 0
	{0x0119,0x00},	// BitBlt Foreground Color Register 1
	{0x01E0,0x00},	// Look-Up Table Mode Register
	{0x01E2,0x00},	// Look-Up Table Address Register
	/* not sure, wouldn't like to mess with the driver */
	{0x01E4,0x00},	// Look-Up Table Data Register
	/* jornada doc says 0x00, but I trust the driver */
	{0x01F0,0x10},	// Power Save Configuration Register
	{0x01F1,0x00},	// Power Save Status Register
	{0x01F4,0x00},	// CPU-to-Memory Access Watchdog Timer Register
	{0x01FC,0x01},	// Display Mode Register(0x01:LCD, 0x02:CRT, 0x03:LCD&CRT)
};

static struct s1d13xxxfb_pdata s1d13xxxfb_data = {
	.initregs		= s1d13xxxfb_initregs,
	.initregssize		= ARRAY_SIZE(s1d13xxxfb_initregs),
	.platform_init_video	= NULL
};

static struct resource s1d13xxxfb_resources[] = {
	[0] = {
		.start	= EPSONFBSTART,
		.end	= EPSONFBSTART + EPSONFBLEN,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= EPSONREGSTART,
		.end	= EPSONREGSTART + EPSONREGLEN,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device s1d13xxxfb_device = {
	.name		= S1D_DEVICENAME,
	.id		= 0,
	.dev		= {
		.platform_data	= &s1d13xxxfb_data,
	},
	.num_resources	= ARRAY_SIZE(s1d13xxxfb_resources),
	.resource	= s1d13xxxfb_resources,
};

static struct resource sa1111_resources[] = {
	[0] = {
		.start		= SA1111REGSTART,
		.end		= SA1111REGSTART + SA1111REGLEN,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IRQ_GPIO1,
		.end		= IRQ_GPIO1,
		.flags		= IORESOURCE_IRQ,
	},
};

static u64 sa1111_dmamask = 0xffffffffUL;

static struct platform_device sa1111_device = {
	.name		= "sa1111",
	.id		= 0,
	.dev		= {
		.dma_mask = &sa1111_dmamask,
		.coherent_dma_mask = 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(sa1111_resources),
	.resource	= sa1111_resources,
};

static struct platform_device jornada_ssp_device = {
	.name           = "jornada_ssp",
	.id             = -1,
};

static struct platform_device *devices[] __initdata = {
	&sa1111_device,
#ifdef CONFIG_SA1100_JORNADA720_SSP
	&jornada_ssp_device,
#endif
	&s1d13xxxfb_device,
};

static int __init jornada720_init(void)
{
	int ret = -ENODEV;

	if (machine_is_jornada720()) {
		/* we want to use gpio20 as input to drive the clock of our uart 3 */
		GPDR |= GPIO_GPIO20;	/* Clear gpio20 pin as input */
		TUCR = TUCR_VAL;
		GPSR = GPIO_GPIO20;	/* start gpio20 pin */
		udelay(1);
		GPCR = GPIO_GPIO20;	/* stop gpio20 */
		udelay(1);
		GPSR = GPIO_GPIO20;	/* restart gpio20 */
		udelay(20);		/* give it some time to restart */

		ret = platform_add_devices(devices, ARRAY_SIZE(devices));
	}

	return ret;
}

arch_initcall(jornada720_init);

static struct map_desc jornada720_io_desc[] __initdata = {
	{	/* Epson registers */
		.virtual	= 0xf0000000,
		.pfn		= __phys_to_pfn(EPSONREGSTART),
		.length		= EPSONREGLEN,
		.type		= MT_DEVICE
	}, {	/* Epson frame buffer */
		.virtual	= 0xf1000000,
		.pfn		= __phys_to_pfn(EPSONFBSTART),
		.length		= EPSONFBLEN,
		.type		= MT_DEVICE
	}, {	/* SA-1111 */
		.virtual	= 0xf4000000,
		.pfn		= __phys_to_pfn(SA1111REGSTART),
		.length		= SA1111REGLEN,
		.type		= MT_DEVICE
	}
};

static void __init jornada720_map_io(void)
{
	sa1100_map_io();
	iotable_init(jornada720_io_desc, ARRAY_SIZE(jornada720_io_desc));

	sa1100_register_uart(0, 3);
	sa1100_register_uart(1, 1);
}

static struct mtd_partition jornada720_partitions[] = {
	{
		.name		= "JORNADA720 boot firmware",
		.size		= 0x00040000,
		.offset		= 0,
		.mask_flags	= MTD_WRITEABLE, /* force read-only */
	}, {
		.name		= "JORNADA720 kernel",
		.size		= 0x000c0000,
		.offset		= 0x00040000,
	}, {
		.name		= "JORNADA720 params",
		.size		= 0x00040000,
		.offset		= 0x00100000,
	}, {
		.name		= "JORNADA720 initrd",
		.size		= 0x00100000,
		.offset		= 0x00140000,
	}, {
		.name		= "JORNADA720 root cramfs",
		.size		= 0x00300000,
		.offset		= 0x00240000,
	}, {
		.name		= "JORNADA720 usr cramfs",
		.size		= 0x00800000,
		.offset		= 0x00540000,
	}, {
		.name		= "JORNADA720 usr local",
		.size		= 0, /* will expand to the end of the flash */
		.offset		= 0x00d00000,
	}
};

static void jornada720_set_vpp(int vpp)
{
	if (vpp)
		/* enabling flash write (line 470 of HP's doc) */
		PPSR |= PPC_LDD7;
	else
		/* disabling flash write (line 470 of HP's doc) */
		PPSR &= ~PPC_LDD7;
	PPDR |= PPC_LDD7;
}

static struct flash_platform_data jornada720_flash_data = {
	.map_name	= "cfi_probe",
	.set_vpp	= jornada720_set_vpp,
	.parts		= jornada720_partitions,
	.nr_parts	= ARRAY_SIZE(jornada720_partitions),
};

static struct resource jornada720_flash_resource = {
	.start		= SA1100_CS0_PHYS,
	.end		= SA1100_CS0_PHYS + SZ_32M - 1,
	.flags		= IORESOURCE_MEM,
};

static void __init jornada720_mach_init(void)
{
	sa11x0_set_flash_data(&jornada720_flash_data, &jornada720_flash_resource, 1);
}

MACHINE_START(JORNADA720, "HP Jornada 720")
	/* Maintainer: Kristoffer Ericson <Kristoffer.Ericson@gmail.com> */
	.phys_io	= 0x80000000,
	.io_pg_offst	= ((0xf8000000) >> 18) & 0xfffc,
	.boot_params	= 0xc0000100,
	.map_io		= jornada720_map_io,
	.init_irq	= sa1100_init_irq,
	.timer		= &sa1100_timer,
	.init_machine	= jornada720_mach_init,
MACHINE_END

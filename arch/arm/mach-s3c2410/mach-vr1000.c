/* linux/arch/arm/mach-s3c2410/mach-vr1000.c
 *
 * Copyright (c) 2003-2005 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * Machine support for Thorcom VR1000 board. Designed for Thorcom by
 * Simtec Electronics, http://www.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Modifications:
 *     14-Sep-2004 BJD  USB Power control
 *     04-Sep-2004 BJD  Added new uart init, and io init
 *     21-Aug-2004 BJD  Added struct s3c2410_board
 *     06-Aug-2004 BJD  Fixed call to time initialisation
 *     05-Apr-2004 BJD  Copied to make mach-vr1000.c
 *     18-Oct-2004 BJD  Updated board struct
 *     04-Nov-2004 BJD  Clock and serial configuration update
 *
 *     04-Jan-2005 BJD  Updated uart init call
 *     10-Jan-2005 BJD  Removed include of s3c2410.h
 *     14-Jan-2005 BJD  Added clock init
 *     15-Jan-2005 BJD  Add serial port device definition
 *     20-Jan-2005 BJD  Use UPF_IOREMAP for ports
 *     10-Feb-2005 BJD  Added power-off capability
 *     10-Mar-2005 LCVR Changed S3C2410_VA to S3C24XX_VA
 *     14-Mar-2006 BJD  void __iomem fixes
 *     22-Jun-2006 BJD  Added DM9000 platform information
 *     20-Sep-2005 BJD  Added static to non-exported items
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/dm9000.h>

#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/arch/bast-map.h>
#include <asm/arch/vr1000-map.h>
#include <asm/arch/vr1000-irq.h>
#include <asm/arch/vr1000-cpld.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <asm/arch/regs-serial.h>
#include <asm/arch/regs-gpio.h>

#include "clock.h"
#include "devs.h"
#include "cpu.h"
#include "usb-simtec.h"

/* macros for virtual address mods for the io space entries */
#define VA_C5(item) ((unsigned long)(item) + BAST_VAM_CS5)
#define VA_C4(item) ((unsigned long)(item) + BAST_VAM_CS4)
#define VA_C3(item) ((unsigned long)(item) + BAST_VAM_CS3)
#define VA_C2(item) ((unsigned long)(item) + BAST_VAM_CS2)

/* macros to modify the physical addresses for io space */

#define PA_CS2(item) (__phys_to_pfn((item) + S3C2410_CS2))
#define PA_CS3(item) (__phys_to_pfn((item) + S3C2410_CS3))
#define PA_CS4(item) (__phys_to_pfn((item) + S3C2410_CS4))
#define PA_CS5(item) (__phys_to_pfn((item) + S3C2410_CS5))

static struct map_desc vr1000_iodesc[] __initdata = {
  /* ISA IO areas */
  {
	  .virtual	= (u32)S3C24XX_VA_ISA_BYTE,
	  .pfn		= PA_CS2(BAST_PA_ISAIO),
	  .length	= SZ_16M,
	  .type		= MT_DEVICE,
  }, {
	  .virtual	= (u32)S3C24XX_VA_ISA_WORD,
	  .pfn		= PA_CS3(BAST_PA_ISAIO),
	  .length	= SZ_16M,
	  .type		= MT_DEVICE,
  },

  /*  CPLD control registers, and external interrupt controls */
  {
	  .virtual	= (u32)VR1000_VA_CTRL1,
	  .pfn		= __phys_to_pfn(VR1000_PA_CTRL1),
	  .length	= SZ_1M,
	  .type		= MT_DEVICE,
  }, {
	  .virtual	= (u32)VR1000_VA_CTRL2,
	  .pfn		= __phys_to_pfn(VR1000_PA_CTRL2),
	  .length	= SZ_1M,
	  .type		= MT_DEVICE,
  }, {
	  .virtual	= (u32)VR1000_VA_CTRL3,
	  .pfn		= __phys_to_pfn(VR1000_PA_CTRL3),
	  .length	= SZ_1M,
	  .type		= MT_DEVICE,
  }, {
	  .virtual	= (u32)VR1000_VA_CTRL4,
	  .pfn		= __phys_to_pfn(VR1000_PA_CTRL4),
	  .length	= SZ_1M,
	  .type		= MT_DEVICE,
  },

  /* peripheral space... one for each of fast/slow/byte/16bit */
  /* note, ide is only decoded in word space, even though some registers
   * are only 8bit */

  /* slow, byte */
  { VA_C2(VR1000_VA_IDEPRI),  PA_CS3(VR1000_PA_IDEPRI),	  SZ_1M,  MT_DEVICE },
  { VA_C2(VR1000_VA_IDESEC),  PA_CS3(VR1000_PA_IDESEC),	  SZ_1M,  MT_DEVICE },
  { VA_C2(VR1000_VA_IDEPRIAUX), PA_CS3(VR1000_PA_IDEPRIAUX), SZ_1M, MT_DEVICE },
  { VA_C2(VR1000_VA_IDESECAUX), PA_CS3(VR1000_PA_IDESECAUX), SZ_1M, MT_DEVICE },

  /* slow, word */
  { VA_C3(VR1000_VA_IDEPRI),  PA_CS3(VR1000_PA_IDEPRI),	  SZ_1M,  MT_DEVICE },
  { VA_C3(VR1000_VA_IDESEC),  PA_CS3(VR1000_PA_IDESEC),	  SZ_1M,  MT_DEVICE },
  { VA_C3(VR1000_VA_IDEPRIAUX), PA_CS3(VR1000_PA_IDEPRIAUX), SZ_1M, MT_DEVICE },
  { VA_C3(VR1000_VA_IDESECAUX), PA_CS3(VR1000_PA_IDESECAUX), SZ_1M, MT_DEVICE },

  /* fast, byte */
  { VA_C4(VR1000_VA_IDEPRI),  PA_CS5(VR1000_PA_IDEPRI),	  SZ_1M,  MT_DEVICE },
  { VA_C4(VR1000_VA_IDESEC),  PA_CS5(VR1000_PA_IDESEC),	  SZ_1M,  MT_DEVICE },
  { VA_C4(VR1000_VA_IDEPRIAUX), PA_CS5(VR1000_PA_IDEPRIAUX), SZ_1M, MT_DEVICE },
  { VA_C4(VR1000_VA_IDESECAUX), PA_CS5(VR1000_PA_IDESECAUX), SZ_1M, MT_DEVICE },

  /* fast, word */
  { VA_C5(VR1000_VA_IDEPRI),  PA_CS5(VR1000_PA_IDEPRI),	  SZ_1M,  MT_DEVICE },
  { VA_C5(VR1000_VA_IDESEC),  PA_CS5(VR1000_PA_IDESEC),	  SZ_1M,  MT_DEVICE },
  { VA_C5(VR1000_VA_IDEPRIAUX), PA_CS5(VR1000_PA_IDEPRIAUX), SZ_1M, MT_DEVICE },
  { VA_C5(VR1000_VA_IDESECAUX), PA_CS5(VR1000_PA_IDESECAUX), SZ_1M, MT_DEVICE },
};

#define UCON S3C2410_UCON_DEFAULT | S3C2410_UCON_UCLK
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

/* uart clock source(s) */

static struct s3c24xx_uart_clksrc vr1000_serial_clocks[] = {
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

static struct s3c2410_uartcfg vr1000_uartcfgs[] __initdata = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = vr1000_serial_clocks,
		.clocks_size = ARRAY_SIZE(vr1000_serial_clocks),
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = vr1000_serial_clocks,
		.clocks_size = ARRAY_SIZE(vr1000_serial_clocks),
	},
	/* port 2 is not actually used */
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = UCON,
		.ulcon	     = ULCON,
		.ufcon	     = UFCON,
		.clocks	     = vr1000_serial_clocks,
		.clocks_size = ARRAY_SIZE(vr1000_serial_clocks),

	}
};

/* definitions for the vr1000 extra 16550 serial ports */

#define VR1000_BAUDBASE (3692307)

#define VR1000_SERIAL_MAPBASE(x) (VR1000_PA_SERIAL + 0x80 + ((x) << 5))

static struct plat_serial8250_port serial_platform_data[] = {
	[0] = {
		.mapbase	= VR1000_SERIAL_MAPBASE(0),
		.irq		= IRQ_VR1000_SERIAL + 0,
		.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 0,
		.uartclk	= VR1000_BAUDBASE,
	},
	[1] = {
		.mapbase	= VR1000_SERIAL_MAPBASE(1),
		.irq		= IRQ_VR1000_SERIAL + 1,
		.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 0,
		.uartclk	= VR1000_BAUDBASE,
	},
	[2] = {
		.mapbase	= VR1000_SERIAL_MAPBASE(2),
		.irq		= IRQ_VR1000_SERIAL + 2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 0,
		.uartclk	= VR1000_BAUDBASE,
	},
	[3] = {
		.mapbase	= VR1000_SERIAL_MAPBASE(3),
		.irq		= IRQ_VR1000_SERIAL + 3,
		.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,
		.iotype		= UPIO_MEM,
		.regshift	= 0,
		.uartclk	= VR1000_BAUDBASE,
	},
	{ },
};

static struct platform_device serial_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= serial_platform_data,
	},
};

/* MTD NOR Flash */

static struct resource vr1000_nor_resource[] = {
	[0] = {
		.start	= S3C2410_CS1 + 0x4000000,
		.end	= S3C2410_CS1 + 0x4000000 + SZ_16M - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device vr1000_nor = {
	.name		= "bast-nor",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(vr1000_nor_resource),
	.resource	= vr1000_nor_resource,
};

/* DM9000 ethernet devices */

static struct resource vr1000_dm9k0_resource[] = {
	[0] = {
		.start = S3C2410_CS5 + VR1000_PA_DM9000,
		.end   = S3C2410_CS5 + VR1000_PA_DM9000 + 3,
		.flags = IORESOURCE_MEM
	},
	[1] = {
		.start = S3C2410_CS5 + VR1000_PA_DM9000 + 0x40,
		.end   = S3C2410_CS5 + VR1000_PA_DM9000 + 0x7f,
		.flags = IORESOURCE_MEM
	},
	[2] = {
		.start = IRQ_VR1000_DM9000A,
		.end   = IRQ_VR1000_DM9000A,
		.flags = IORESOURCE_IRQ
	}

};

static struct resource vr1000_dm9k1_resource[] = {
	[0] = {
		.start = S3C2410_CS5 + VR1000_PA_DM9000 + 0x80,
		.end   = S3C2410_CS5 + VR1000_PA_DM9000 + 0x83,
		.flags = IORESOURCE_MEM
	},
	[1] = {
		.start = S3C2410_CS5 + VR1000_PA_DM9000 + 0xC0,
		.end   = S3C2410_CS5 + VR1000_PA_DM9000 + 0xFF,
		.flags = IORESOURCE_MEM
	},
	[2] = {
		.start = IRQ_VR1000_DM9000N,
		.end   = IRQ_VR1000_DM9000N,
		.flags = IORESOURCE_IRQ
	}
};

/* for the moment we limit ourselves to 16bit IO until some
 * better IO routines can be written and tested
*/

static struct dm9000_plat_data vr1000_dm9k_platdata = {
	.flags		= DM9000_PLATF_16BITONLY,
};

static struct platform_device vr1000_dm9k0 = {
	.name		= "dm9000",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(vr1000_dm9k0_resource),
	.resource	= vr1000_dm9k0_resource,
	.dev		= {
		.platform_data = &vr1000_dm9k_platdata,
	}
};

static struct platform_device vr1000_dm9k1 = {
	.name		= "dm9000",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(vr1000_dm9k1_resource),
	.resource	= vr1000_dm9k1_resource,
	.dev		= {
		.platform_data = &vr1000_dm9k_platdata,
	}
};

/* devices for this board */

static struct platform_device *vr1000_devices[] __initdata = {
	&s3c_device_usb,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c,
	&s3c_device_iis,
	&s3c_device_adc,
	&serial_device,
	&vr1000_nor,
	&vr1000_dm9k0,
	&vr1000_dm9k1
};

static struct clk *vr1000_clocks[] = {
	&s3c24xx_dclk0,
	&s3c24xx_dclk1,
	&s3c24xx_clkout0,
	&s3c24xx_clkout1,
	&s3c24xx_uclk,
};

static struct s3c24xx_board vr1000_board __initdata = {
	.devices       = vr1000_devices,
	.devices_count = ARRAY_SIZE(vr1000_devices),
	.clocks	       = vr1000_clocks,
	.clocks_count  = ARRAY_SIZE(vr1000_clocks),
};

static void vr1000_power_off(void)
{
	s3c2410_gpio_cfgpin(S3C2410_GPB9, S3C2410_GPB9_OUTP);
	s3c2410_gpio_setpin(S3C2410_GPB9, 1);
}

static void __init vr1000_map_io(void)
{
	/* initialise clock sources */

	s3c24xx_dclk0.parent = NULL;
	s3c24xx_dclk0.rate   = 12*1000*1000;

	s3c24xx_dclk1.parent = NULL;
	s3c24xx_dclk1.rate   = 3692307;

	s3c24xx_clkout0.parent  = &s3c24xx_dclk0;
	s3c24xx_clkout1.parent  = &s3c24xx_dclk1;

	s3c24xx_uclk.parent  = &s3c24xx_clkout1;

	pm_power_off = vr1000_power_off;

	s3c24xx_init_io(vr1000_iodesc, ARRAY_SIZE(vr1000_iodesc));
	s3c24xx_init_clocks(0);
	s3c24xx_init_uarts(vr1000_uartcfgs, ARRAY_SIZE(vr1000_uartcfgs));
	s3c24xx_set_board(&vr1000_board);
	usb_simtec_init();
}


MACHINE_START(VR1000, "Thorcom-VR1000")
	/* Maintainer: Ben Dooks <ben@simtec.co.uk> */
	.phys_io	= S3C2410_PA_UART,
	.io_pg_offst	= (((u32)S3C24XX_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S3C2410_SDRAM_PA + 0x100,
	.map_io		= vr1000_map_io,
	.init_irq	= s3c24xx_init_irq,
	.timer		= &s3c24xx_timer,
MACHINE_END

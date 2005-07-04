/*
 * linux/arch/arm/mach-pxa/poodle.c
 *
 *  Support for the SHARP Poodle Board.
 *
 * Based on:
 *  linux/arch/arm/mach-pxa/lubbock.c Author:	Nicolas Pitre
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 * Change Log
 *  12-Dec-2002 Sharp Corporation for Poodle
 *  John Lenz <lenz@cs.wisc.edu> updates to 2.6
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/fb.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/irq.h>
#include <asm/arch/poodle.h>
#include <asm/arch/pxafb.h>

#include <asm/hardware/scoop.h>
#include <asm/hardware/locomo.h>
#include <asm/mach/sharpsl_param.h>

#include "generic.h"

static struct resource poodle_scoop_resources[] = {
	[0] = {
		.start		= 0x10800000,
		.end		= 0x10800fff,
		.flags		= IORESOURCE_MEM,
	},
};

static struct scoop_config poodle_scoop_setup = {
	.io_dir		= POODLE_SCOOP_IO_DIR,
	.io_out		= POODLE_SCOOP_IO_OUT,
};

struct platform_device poodle_scoop_device = {
	.name		= "sharp-scoop",
	.id		= -1,
	.dev		= {
		.platform_data	= &poodle_scoop_setup,
	},
	.num_resources	= ARRAY_SIZE(poodle_scoop_resources),
	.resource	= poodle_scoop_resources,
};


/* LoCoMo device */
static struct resource locomo_resources[] = {
	[0] = {
		.start		= 0x10000000,
		.end		= 0x10001fff,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= IRQ_GPIO(10),
		.end		= IRQ_GPIO(10),
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device locomo_device = {
	.name		= "locomo",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(locomo_resources),
	.resource	= locomo_resources,
};

/* PXAFB device */
static struct pxafb_mach_info poodle_fb_info __initdata = {
	.pixclock	= 144700,

	.xres		= 320,
	.yres		= 240,
	.bpp		= 16,

	.hsync_len	= 7,
	.left_margin	= 11,
	.right_margin	= 30,

	.vsync_len	= 2,
	.upper_margin	= 2,
	.lower_margin	= 0,
	.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,

	.lccr0		= LCCR0_Act | LCCR0_Sngl | LCCR0_Color,
	.lccr3		= 0,

	.pxafb_backlight_power	= NULL,
	.pxafb_lcd_power	= NULL,
};

static struct platform_device *devices[] __initdata = {
	&locomo_device,
	&poodle_scoop_device,
};

static void __init poodle_init(void)
{
	int ret = 0;

	/* cpu initialize */
	/* Pgsr Register */
  	PGSR0 = 0x0146dd80;
  	PGSR1 = 0x03bf0890;
  	PGSR2 = 0x0001c000;

	/* Alternate Register */
  	GAFR0_L = 0x01001000;
  	GAFR0_U = 0x591a8010;
  	GAFR1_L = 0x900a8451;
  	GAFR1_U = 0xaaa5aaaa;
  	GAFR2_L = 0x8aaaaaaa;
  	GAFR2_U = 0x00000002;

	/* Direction Register */
  	GPDR0 = 0xd3f0904c;
  	GPDR1 = 0xfcffb7d3;
  	GPDR2 = 0x0001ffff;

	/* Output Register */
  	GPCR0 = 0x00000000;
  	GPCR1 = 0x00000000;
  	GPCR2 = 0x00000000;

  	GPSR0 = 0x00400000;
  	GPSR1 = 0x00000000;
        GPSR2 = 0x00000000;

	set_pxa_fb_info(&poodle_fb_info);

	ret = platform_add_devices(devices, ARRAY_SIZE(devices));
	if (ret) {
		printk(KERN_WARNING "poodle: Unable to register LoCoMo device\n");
	}
}

static void __init fixup_poodle(struct machine_desc *desc,
		struct tag *tags, char **cmdline, struct meminfo *mi)
{
	sharpsl_save_param();
}

static struct map_desc poodle_io_desc[] __initdata = {
 /* virtual     physical    length                   */
  { 0xef800000, 0x00000000, 0x00800000, MT_DEVICE }, /* Boot Flash */
};

static void __init poodle_map_io(void)
{
	pxa_map_io();
	iotable_init(poodle_io_desc, ARRAY_SIZE(poodle_io_desc));

	/* setup sleep mode values */
	PWER  = 0x00000002;
	PFER  = 0x00000000;
	PRER  = 0x00000002;
	PGSR0 = 0x00008000;
	PGSR1 = 0x003F0202;
	PGSR2 = 0x0001C000;
	PCFR |= PCFR_OPDE;
}

MACHINE_START(POODLE, "SHARP Poodle")
	.phys_ram	= 0xa0000000,
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.fixup		= fixup_poodle,
	.map_io		= poodle_map_io,
	.init_irq	= pxa_init_irq,
	.timer		= &pxa_timer,
	.init_machine	= poodle_init,
MACHINE_END

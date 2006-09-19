/*
 * linux/arch/arm/mach-footbridge/cats-hw.c
 *
 * CATS machine fixup
 *
 * Copyright (C) 1998, 1999 Russell King, Phil Blundell
 */
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/screen_info.h>

#include <asm/hardware/dec21285.h>
#include <asm/io.h>
#include <asm/mach-types.h>
#include <asm/setup.h>

#include <asm/mach/arch.h>

#include "common.h"

#define CFG_PORT	0x370
#define INDEX_PORT	(CFG_PORT)
#define DATA_PORT	(CFG_PORT + 1)

static int __init cats_hw_init(void)
{
	if (machine_is_cats()) {
		/* Set Aladdin to CONFIGURE mode */
		outb(0x51, CFG_PORT);
		outb(0x23, CFG_PORT);

		/* Select logical device 3 */
		outb(0x07, INDEX_PORT);
		outb(0x03, DATA_PORT);

		/* Set parallel port to DMA channel 3, ECP+EPP1.9, 
		   enable EPP timeout */
		outb(0x74, INDEX_PORT);
		outb(0x03, DATA_PORT);
	
		outb(0xf0, INDEX_PORT);
		outb(0x0f, DATA_PORT);

		outb(0xf1, INDEX_PORT);
		outb(0x07, DATA_PORT);

		/* Select logical device 4 */
		outb(0x07, INDEX_PORT);
		outb(0x04, DATA_PORT);

		/* UART1 high speed mode */
		outb(0xf0, INDEX_PORT);
		outb(0x02, DATA_PORT);

		/* Select logical device 5 */
		outb(0x07, INDEX_PORT);
		outb(0x05, DATA_PORT);

		/* UART2 high speed mode */
		outb(0xf0, INDEX_PORT);
		outb(0x02, DATA_PORT);

		/* Set Aladdin to RUN mode */
		outb(0xbb, CFG_PORT);
	}

	return 0;
}

__initcall(cats_hw_init);

/*
 * CATS uses soft-reboot by default, since
 * hard reboots fail on early boards.
 */
static void __init
fixup_cats(struct machine_desc *desc, struct tag *tags,
	   char **cmdline, struct meminfo *mi)
{
	ORIG_VIDEO_LINES  = 25;
	ORIG_VIDEO_POINTS = 16;
	ORIG_Y = 24;
}

MACHINE_START(CATS, "Chalice-CATS")
	/* Maintainer: Philip Blundell */
	.phys_io	= DC21285_ARMCSR_BASE,
	.io_pg_offst	= ((0xfe000000) >> 18) & 0xfffc,
	.boot_params	= 0x00000100,
	.soft_reboot	= 1,
	.fixup		= fixup_cats,
	.map_io		= footbridge_map_io,
	.init_irq	= footbridge_init_irq,
	.timer		= &isa_timer,
MACHINE_END

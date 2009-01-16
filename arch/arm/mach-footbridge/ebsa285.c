/*
 * linux/arch/arm/mach-footbridge/ebsa285.c
 *
 * EBSA285 machine fixup
 */
#include <linux/init.h>
#include <linux/spinlock.h>

#include <asm/hardware/dec21285.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>

#include "common.h"

MACHINE_START(EBSA285, "EBSA285")
	/* Maintainer: Russell King */
	.phys_io	= DC21285_ARMCSR_BASE,
	.io_pg_offst	= ((0xfe000000) >> 18) & 0xfffc,
	.boot_params	= 0x00000100,
	.video_start	= 0x000a0000,
	.video_end	= 0x000bffff,
	.map_io		= footbridge_map_io,
	.init_irq	= footbridge_init_irq,
	.timer		= &footbridge_timer,
MACHINE_END


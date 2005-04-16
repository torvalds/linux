/*
 * linux/arch/arm/mach-footbridge/ebsa285.c
 *
 * EBSA285 machine fixup
 */
#include <linux/init.h>

#include <asm/hardware/dec21285.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>

#include "common.h"

MACHINE_START(EBSA285, "EBSA285")
	MAINTAINER("Russell King")
	BOOT_MEM(0x00000000, DC21285_ARMCSR_BASE, 0xfe000000)
	BOOT_PARAMS(0x00000100)
	VIDEO(0x000a0000, 0x000bffff)
	MAPIO(footbridge_map_io)
	INITIRQ(footbridge_init_irq)
	.timer		= &footbridge_timer,
MACHINE_END


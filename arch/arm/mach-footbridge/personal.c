/*
 * linux/arch/arm/mach-footbridge/personal.c
 *
 * Personal server (Skiff) machine fixup
 */
#include <linux/init.h>

#include <asm/hardware/dec21285.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>

#include "common.h"

MACHINE_START(PERSONAL_SERVER, "Compaq-PersonalServer")
	MAINTAINER("Jamey Hicks / George France")
	BOOT_MEM(0x00000000, DC21285_ARMCSR_BASE, 0xfe000000)
	BOOT_PARAMS(0x00000100)
	MAPIO(footbridge_map_io)
	INITIRQ(footbridge_init_irq)
	.timer		= &footbridge_timer,
MACHINE_END


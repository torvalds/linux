/*
 * linux/arch/arm/mach-footbridge/co285.c
 *
 * CO285 machine fixup
 */
#include <linux/init.h>

#include <asm/hardware/dec21285.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>

#include "common.h"

static void __init
fixup_coebsa285(struct machine_desc *desc, struct tag *tags,
		char **cmdline, struct meminfo *mi)
{
	extern unsigned long boot_memory_end;
	extern char boot_command_line[];

	mi->nr_banks      = 1;
	mi->bank[0].start = PHYS_OFFSET;
	mi->bank[0].size  = boot_memory_end;
	mi->bank[0].node  = 0;

	*cmdline = boot_command_line;
}

MACHINE_START(CO285, "co-EBSA285")
	/* Maintainer: Mark van Doesburg */
	.phys_io	= DC21285_ARMCSR_BASE,
	.io_pg_offst	= ((0x7cf00000) >> 18) & 0xfffc,
	.fixup		= fixup_coebsa285,
	.map_io		= footbridge_map_io,
	.init_irq	= footbridge_init_irq,
	.timer		= &footbridge_timer,
MACHINE_END


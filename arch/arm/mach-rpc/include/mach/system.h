/*
 *  arch/arm/mach-rpc/include/mach/system.h
 *
 *  Copyright (C) 1996-1999 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <mach/hardware.h>
#include <asm/hardware/iomd.h>
#include <asm/io.h>

static inline void arch_idle(void)
{
	cpu_do_idle();
}

static inline void arch_reset(char mode)
{
	iomd_writeb(0, IOMD_ROMCR0);

	/*
	 * Jump into the ROM
	 */
	cpu_reset(0);
}

/*
 * arch/arm/mach-clps7500/include/mach/system.h
 *
 * Copyright (c) 1999 Nexus Electronics Ltd.
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#include <asm/hardware/iomd.h>
#include <asm/io.h>

static inline void arch_idle(void)
{
	iomd_writeb(0, IOMD_SUSMODE);
}

#define arch_reset(mode)			\
	do {					\
		iomd_writeb(0, IOMD_ROMCR0);	\
		cpu_reset(0);			\
	} while (0)

#endif

/*
 * Copied from arch/arm/mach-sa1100/include/mach/system.h
 * Copyright (c) 1999 Nicolas Pitre <nico@fluxnic.net>
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#include <asm/proc-fns.h>

static inline void arch_idle(void)
{
	cpu_do_idle();
}

extern void (*arch_reset)(char, const char *);

#endif

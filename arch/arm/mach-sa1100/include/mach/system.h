/*
 * arch/arm/mach-sa1100/include/mach/system.h
 *
 * Copyright (c) 1999 Nicolas Pitre <nico@fluxnic.net>
 */
static inline void arch_idle(void)
{
	cpu_do_idle();
}

static inline void arch_reset(char mode, const char *cmd)
{
}

/*
 *  arch/arm/mach-rpc/include/mach/system.h
 *
 *  Copyright (C) 1996-1999 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
static inline void arch_idle(void)
{
	cpu_do_idle();
}

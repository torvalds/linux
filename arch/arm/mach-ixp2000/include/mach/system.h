/*
 * arch/arm/mach-ixp2000/include/mach/system.h
 *
 * Copyright (C) 2002 Intel Corp.
 * Copyricht (C) 2003-2005 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
static inline void arch_idle(void)
{
	cpu_do_idle();
}

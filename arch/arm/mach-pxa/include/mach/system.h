/*
 * arch/arm/mach-pxa/include/mach/system.h
 *
 * Author:	Nicolas Pitre
 * Created:	Jun 15, 2001
 * Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
static inline void arch_idle(void)
{
	cpu_do_idle();
}

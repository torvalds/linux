/*
 * arch/arm/mach-ixp4xx/include/mach/system.h
 *
 * Copyright (C) 2002 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
static inline void arch_idle(void)
{
	/* ixp4xx does not implement the XScale PWRMODE register,
	 * so it must not call cpu_do_idle() here.
	 */
#if 0
	cpu_do_idle();
#endif
}

/*
 * arch/arm/mach-h720x/include/mach/system.h
 *
 * Copyright (C) 2001-2002 Jungjun Kim, Hynix Semiconductor Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * arch/arm/mach-h720x/include/mach/system.h
 *
 */

#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H
#include <mach/hardware.h>

static void arch_idle(void)
{
	cpu_do_idle();
}

#endif

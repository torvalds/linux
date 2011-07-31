/*
 *  Copyright (C) 1999 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *  Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __ASM_ARCH_MXC_SYSTEM_H__
#define __ASM_ARCH_MXC_SYSTEM_H__

#include <mach/hardware.h>
#include <mach/common.h>

static inline void arch_idle(void)
{
#ifdef CONFIG_ARCH_MXC91231
	if (cpu_is_mxc91231()) {
		/* Need this to set DSM low-power mode */
		mxc91231_prepare_idle();
	}
#endif

	cpu_do_idle();
}

void arch_reset(char mode, const char *cmd);

#endif /* __ASM_ARCH_MXC_SYSTEM_H__ */

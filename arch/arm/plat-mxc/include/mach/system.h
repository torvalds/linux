/*
 *  Copyright (C) 1999 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *  Copyright 2004-2008 Freescale Semiconductor, Inc. All Rights Reserved.
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

extern void mx5_cpu_lp_set(enum mxc_cpu_pwr_mode mode);

static inline void arch_idle(void)
{
	/* fix i.MX31 errata TLSbo65953 and i.MX35 errata ENGcm09472 */
	if (cpu_is_mx31() || cpu_is_mx35()) {
		unsigned long reg = 0;
		__asm__ __volatile__(
			/* disable I and D cache */
			"mrc p15, 0, %0, c1, c0, 0\n"
			"bic %0, %0, #0x00001000\n"
			"bic %0, %0, #0x00000004\n"
			"mcr p15, 0, %0, c1, c0, 0\n"
			/* invalidate I cache */
			"mov %0, #0\n"
			"mcr p15, 0, %0, c7, c5, 0\n"
			/* clear and invalidate D cache */
			"mov %0, #0\n"
			"mcr p15, 0, %0, c7, c14, 0\n"
			/* WFI */
			"mov %0, #0\n"
			"mcr p15, 0, %0, c7, c0, 4\n"
			"nop\n" "nop\n" "nop\n" "nop\n"
			"nop\n" "nop\n" "nop\n"
			/* enable I and D cache */
			"mrc p15, 0, %0, c1, c0, 0\n"
			"orr %0, %0, #0x00001000\n"
			"orr %0, %0, #0x00000004\n"
			"mcr p15, 0, %0, c1, c0, 0\n"
			: "=r" (reg));
	} else if (cpu_is_mx51())
		mx5_cpu_lp_set(WAIT_UNCLOCKED_POWER_OFF);
	else
		cpu_do_idle();
}

void arch_reset(char mode, const char *cmd);

#endif /* __ASM_ARCH_MXC_SYSTEM_H__ */

/*
 * arch/arm64/include/asm/arm_generic.h
 *
 * Copyright (C) 2012 ARM Ltd.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_ARM_GENERIC_H
#define __ASM_ARM_GENERIC_H

#include <linux/clocksource.h>

#define ARCH_TIMER_CTRL_ENABLE		(1 << 0)
#define ARCH_TIMER_CTRL_IMASK		(1 << 1)
#define ARCH_TIMER_CTRL_ISTATUS		(1 << 2)

#define ARCH_TIMER_REG_CTRL		0
#define ARCH_TIMER_REG_FREQ		1
#define ARCH_TIMER_REG_TVAL		2

static inline void arch_timer_reg_write(int reg, u32 val)
{
	switch (reg) {
	case ARCH_TIMER_REG_CTRL:
		asm volatile("msr cntp_ctl_el0,  %0" : : "r" (val));
		break;
	case ARCH_TIMER_REG_TVAL:
		asm volatile("msr cntp_tval_el0, %0" : : "r" (val));
		break;
	default:
		BUILD_BUG();
	}

	isb();
}

static inline u32 arch_timer_reg_read(int reg)
{
	u32 val;

	switch (reg) {
	case ARCH_TIMER_REG_CTRL:
		asm volatile("mrs %0,  cntp_ctl_el0" : "=r" (val));
		break;
	case ARCH_TIMER_REG_FREQ:
		asm volatile("mrs %0,   cntfrq_el0" : "=r" (val));
		break;
	case ARCH_TIMER_REG_TVAL:
		asm volatile("mrs %0, cntp_tval_el0" : "=r" (val));
		break;
	default:
		BUILD_BUG();
	}

	return val;
}

static inline void __cpuinit arch_counter_enable_user_access(void)
{
	u32 cntkctl;

	/* Disable user access to the timers and the virtual counter. */
	asm volatile("mrs	%0, cntkctl_el1" : "=r" (cntkctl));
	cntkctl &= ~((3 << 8) | (1 << 1));

	/* Enable user access to the physical counter and frequency. */
	cntkctl |= 1;
	asm volatile("msr	cntkctl_el1, %0" : : "r" (cntkctl));
}

static inline cycle_t arch_counter_get_cntpct(void)
{
	cycle_t cval;

	asm volatile("mrs %0, cntpct_el0" : "=r" (cval));

	return cval;
}

static inline cycle_t arch_counter_get_cntvct(void)
{
	cycle_t cval;

	asm volatile("mrs %0, cntvct_el0" : "=r" (cval));

	return cval;
}

#endif

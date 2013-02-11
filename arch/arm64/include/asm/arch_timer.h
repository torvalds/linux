/*
 * arch/arm64/include/asm/arch_timer.h
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
#ifndef __ASM_ARCH_TIMER_H
#define __ASM_ARCH_TIMER_H

#include <asm/barrier.h>

#include <linux/init.h>
#include <linux/types.h>

#include <clocksource/arm_arch_timer.h>

static inline void arch_timer_reg_write(int access, int reg, u32 val)
{
	if (access == ARCH_TIMER_PHYS_ACCESS) {
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
	} else if (access == ARCH_TIMER_VIRT_ACCESS) {
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			asm volatile("msr cntv_ctl_el0,  %0" : : "r" (val));
			break;
		case ARCH_TIMER_REG_TVAL:
			asm volatile("msr cntv_tval_el0, %0" : : "r" (val));
			break;
		default:
			BUILD_BUG();
		}
	} else {
		BUILD_BUG();
	}

	isb();
}

static inline u32 arch_timer_reg_read(int access, int reg)
{
	u32 val;

	if (access == ARCH_TIMER_PHYS_ACCESS) {
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			asm volatile("mrs %0,  cntp_ctl_el0" : "=r" (val));
			break;
		case ARCH_TIMER_REG_TVAL:
			asm volatile("mrs %0, cntp_tval_el0" : "=r" (val));
			break;
		default:
			BUILD_BUG();
		}
	} else if (access == ARCH_TIMER_VIRT_ACCESS) {
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			asm volatile("mrs %0,  cntv_ctl_el0" : "=r" (val));
			break;
		case ARCH_TIMER_REG_TVAL:
			asm volatile("mrs %0, cntv_tval_el0" : "=r" (val));
			break;
		default:
			BUILD_BUG();
		}
	} else {
		BUILD_BUG();
	}

	return val;
}

static inline u32 arch_timer_get_cntfrq(void)
{
	u32 val;
	asm volatile("mrs %0,   cntfrq_el0" : "=r" (val));
	return val;
}

static inline void __cpuinit arch_counter_set_user_access(void)
{
	u32 cntkctl;

	/* Disable user access to the timers and the physical counter. */
	asm volatile("mrs	%0, cntkctl_el1" : "=r" (cntkctl));
	cntkctl &= ~((3 << 8) | (1 << 0));

	/* Enable user access to the virtual counter and frequency. */
	cntkctl |= (1 << 1);
	asm volatile("msr	cntkctl_el1, %0" : : "r" (cntkctl));
}

static inline u64 arch_counter_get_cntpct(void)
{
	u64 cval;

	isb();
	asm volatile("mrs %0, cntpct_el0" : "=r" (cval));

	return cval;
}

static inline u64 arch_counter_get_cntvct(void)
{
	u64 cval;

	isb();
	asm volatile("mrs %0, cntvct_el0" : "=r" (cval));

	return cval;
}

#endif

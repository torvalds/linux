/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASMARM_ARCH_TIMER_H
#define __ASMARM_ARCH_TIMER_H

#include <asm/barrier.h>
#include <asm/errno.h>
#include <asm/hwcap.h>
#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/types.h>

#include <clocksource/arm_arch_timer.h>

#ifdef CONFIG_ARM_ARCH_TIMER
/* 32bit ARM doesn't know anything about timer errata... */
#define has_erratum_handler(h)		(false)
#define erratum_handler(h)		(arch_timer_##h)

int arch_timer_arch_init(void);

/*
 * These register accessors are marked inline so the compiler can
 * nicely work out which register we want, and chuck away the rest of
 * the code. At least it does so with a recent GCC (4.6.3).
 */
static __always_inline
void arch_timer_reg_write_cp15(int access, enum arch_timer_reg reg, u64 val)
{
	if (access == ARCH_TIMER_PHYS_ACCESS) {
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			asm volatile("mcr p15, 0, %0, c14, c2, 1" : : "r" ((u32)val));
			isb();
			break;
		case ARCH_TIMER_REG_CVAL:
			asm volatile("mcrr p15, 2, %Q0, %R0, c14" : : "r" (val));
			break;
		default:
			BUILD_BUG();
		}
	} else if (access == ARCH_TIMER_VIRT_ACCESS) {
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			asm volatile("mcr p15, 0, %0, c14, c3, 1" : : "r" ((u32)val));
			isb();
			break;
		case ARCH_TIMER_REG_CVAL:
			asm volatile("mcrr p15, 3, %Q0, %R0, c14" : : "r" (val));
			break;
		default:
			BUILD_BUG();
		}
	} else {
		BUILD_BUG();
	}
}

static __always_inline
u32 arch_timer_reg_read_cp15(int access, enum arch_timer_reg reg)
{
	u32 val = 0;

	if (access == ARCH_TIMER_PHYS_ACCESS) {
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			asm volatile("mrc p15, 0, %0, c14, c2, 1" : "=r" (val));
			break;
		default:
			BUILD_BUG();
		}
	} else if (access == ARCH_TIMER_VIRT_ACCESS) {
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			asm volatile("mrc p15, 0, %0, c14, c3, 1" : "=r" (val));
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
	asm volatile("mrc p15, 0, %0, c14, c0, 0" : "=r" (val));
	return val;
}

static inline u64 __arch_counter_get_cntpct(void)
{
	u64 cval;

	isb();
	asm volatile("mrrc p15, 0, %Q0, %R0, c14" : "=r" (cval));
	return cval;
}

static inline u64 __arch_counter_get_cntpct_stable(void)
{
	return __arch_counter_get_cntpct();
}

static inline u64 __arch_counter_get_cntvct(void)
{
	u64 cval;

	isb();
	asm volatile("mrrc p15, 1, %Q0, %R0, c14" : "=r" (cval));
	return cval;
}

static inline u64 __arch_counter_get_cntvct_stable(void)
{
	return __arch_counter_get_cntvct();
}

static inline u32 arch_timer_get_cntkctl(void)
{
	u32 cntkctl;
	asm volatile("mrc p15, 0, %0, c14, c1, 0" : "=r" (cntkctl));
	return cntkctl;
}

static inline void arch_timer_set_cntkctl(u32 cntkctl)
{
	asm volatile("mcr p15, 0, %0, c14, c1, 0" : : "r" (cntkctl));
	isb();
}

static inline void arch_timer_set_evtstrm_feature(void)
{
	elf_hwcap |= HWCAP_EVTSTRM;
}

static inline bool arch_timer_have_evtstrm_feature(void)
{
	return elf_hwcap & HWCAP_EVTSTRM;
}
#endif

#endif

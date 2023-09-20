/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm64/include/asm/arch_timer.h
 *
 * Copyright (C) 2012 ARM Ltd.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */
#ifndef __ASM_ARCH_TIMER_H
#define __ASM_ARCH_TIMER_H

#include <asm/barrier.h>
#include <asm/hwcap.h>
#include <asm/sysreg.h>

#include <linux/bug.h>
#include <linux/init.h>
#include <linux/jump_label.h>
#include <linux/smp.h>
#include <linux/types.h>

#include <clocksource/arm_arch_timer.h>

#if IS_ENABLED(CONFIG_ARM_ARCH_TIMER_OOL_WORKAROUND)
#define has_erratum_handler(h)						\
	({								\
		const struct arch_timer_erratum_workaround *__wa;	\
		__wa = __this_cpu_read(timer_unstable_counter_workaround); \
		(__wa && __wa->h);					\
	})

#define erratum_handler(h)						\
	({								\
		const struct arch_timer_erratum_workaround *__wa;	\
		__wa = __this_cpu_read(timer_unstable_counter_workaround); \
		(__wa && __wa->h) ? ({ isb(); __wa->h;}) : arch_timer_##h; \
	})

#else
#define has_erratum_handler(h)			   false
#define erratum_handler(h)			   (arch_timer_##h)
#endif

enum arch_timer_erratum_match_type {
	ate_match_dt,
	ate_match_local_cap_id,
	ate_match_acpi_oem_info,
};

struct clock_event_device;

struct arch_timer_erratum_workaround {
	enum arch_timer_erratum_match_type match_type;
	const void *id;
	const char *desc;
	u64 (*read_cntpct_el0)(void);
	u64 (*read_cntvct_el0)(void);
	int (*set_next_event_phys)(unsigned long, struct clock_event_device *);
	int (*set_next_event_virt)(unsigned long, struct clock_event_device *);
	bool disable_compat_vdso;
};

DECLARE_PER_CPU(const struct arch_timer_erratum_workaround *,
		timer_unstable_counter_workaround);

static inline notrace u64 arch_timer_read_cntpct_el0(void)
{
	u64 cnt;

	asm volatile(ALTERNATIVE("isb\n mrs %0, cntpct_el0",
				 "nop\n" __mrs_s("%0", SYS_CNTPCTSS_EL0),
				 ARM64_HAS_ECV)
		     : "=r" (cnt));

	return cnt;
}

static inline notrace u64 arch_timer_read_cntvct_el0(void)
{
	u64 cnt;

	asm volatile(ALTERNATIVE("isb\n mrs %0, cntvct_el0",
				 "nop\n" __mrs_s("%0", SYS_CNTVCTSS_EL0),
				 ARM64_HAS_ECV)
		     : "=r" (cnt));

	return cnt;
}

#define arch_timer_reg_read_stable(reg)					\
	({								\
		erratum_handler(read_ ## reg)();			\
	})

/*
 * These register accessors are marked inline so the compiler can
 * nicely work out which register we want, and chuck away the rest of
 * the code.
 */
static __always_inline
void arch_timer_reg_write_cp15(int access, enum arch_timer_reg reg, u64 val)
{
	if (access == ARCH_TIMER_PHYS_ACCESS) {
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			write_sysreg(val, cntp_ctl_el0);
			isb();
			break;
		case ARCH_TIMER_REG_CVAL:
			write_sysreg(val, cntp_cval_el0);
			break;
		default:
			BUILD_BUG();
		}
	} else if (access == ARCH_TIMER_VIRT_ACCESS) {
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			write_sysreg(val, cntv_ctl_el0);
			isb();
			break;
		case ARCH_TIMER_REG_CVAL:
			write_sysreg(val, cntv_cval_el0);
			break;
		default:
			BUILD_BUG();
		}
	} else {
		BUILD_BUG();
	}
}

static __always_inline
u64 arch_timer_reg_read_cp15(int access, enum arch_timer_reg reg)
{
	if (access == ARCH_TIMER_PHYS_ACCESS) {
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			return read_sysreg(cntp_ctl_el0);
		default:
			BUILD_BUG();
		}
	} else if (access == ARCH_TIMER_VIRT_ACCESS) {
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			return read_sysreg(cntv_ctl_el0);
		default:
			BUILD_BUG();
		}
	}

	BUILD_BUG();
	unreachable();
}

static inline u32 arch_timer_get_cntfrq(void)
{
	return read_sysreg(cntfrq_el0);
}

static inline u32 arch_timer_get_cntkctl(void)
{
	return read_sysreg(cntkctl_el1);
}

static inline void arch_timer_set_cntkctl(u32 cntkctl)
{
	write_sysreg(cntkctl, cntkctl_el1);
	isb();
}

static __always_inline u64 __arch_counter_get_cntpct_stable(void)
{
	u64 cnt;

	cnt = arch_timer_reg_read_stable(cntpct_el0);
	arch_counter_enforce_ordering(cnt);
	return cnt;
}

static __always_inline u64 __arch_counter_get_cntpct(void)
{
	u64 cnt;

	asm volatile(ALTERNATIVE("isb\n mrs %0, cntpct_el0",
				 "nop\n" __mrs_s("%0", SYS_CNTPCTSS_EL0),
				 ARM64_HAS_ECV)
		     : "=r" (cnt));
	arch_counter_enforce_ordering(cnt);
	return cnt;
}

static __always_inline u64 __arch_counter_get_cntvct_stable(void)
{
	u64 cnt;

	cnt = arch_timer_reg_read_stable(cntvct_el0);
	arch_counter_enforce_ordering(cnt);
	return cnt;
}

static __always_inline u64 __arch_counter_get_cntvct(void)
{
	u64 cnt;

	asm volatile(ALTERNATIVE("isb\n mrs %0, cntvct_el0",
				 "nop\n" __mrs_s("%0", SYS_CNTVCTSS_EL0),
				 ARM64_HAS_ECV)
		     : "=r" (cnt));
	arch_counter_enforce_ordering(cnt);
	return cnt;
}

static inline int arch_timer_arch_init(void)
{
	return 0;
}

static inline void arch_timer_set_evtstrm_feature(void)
{
	cpu_set_named_feature(EVTSTRM);
#ifdef CONFIG_COMPAT
	compat_elf_hwcap |= COMPAT_HWCAP_EVTSTRM;
#endif
}

static inline bool arch_timer_have_evtstrm_feature(void)
{
	return cpu_have_named_feature(EVTSTRM);
}
#endif

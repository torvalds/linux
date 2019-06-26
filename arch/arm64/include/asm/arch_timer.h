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
		(__wa && __wa->h) ? __wa->h : arch_timer_##h;		\
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
	u32 (*read_cntp_tval_el0)(void);
	u32 (*read_cntv_tval_el0)(void);
	u64 (*read_cntpct_el0)(void);
	u64 (*read_cntvct_el0)(void);
	int (*set_next_event_phys)(unsigned long, struct clock_event_device *);
	int (*set_next_event_virt)(unsigned long, struct clock_event_device *);
};

DECLARE_PER_CPU(const struct arch_timer_erratum_workaround *,
		timer_unstable_counter_workaround);

/* inline sysreg accessors that make erratum_handler() work */
static inline notrace u32 arch_timer_read_cntp_tval_el0(void)
{
	return read_sysreg(cntp_tval_el0);
}

static inline notrace u32 arch_timer_read_cntv_tval_el0(void)
{
	return read_sysreg(cntv_tval_el0);
}

static inline notrace u64 arch_timer_read_cntpct_el0(void)
{
	return read_sysreg(cntpct_el0);
}

static inline notrace u64 arch_timer_read_cntvct_el0(void)
{
	return read_sysreg(cntvct_el0);
}

#define arch_timer_reg_read_stable(reg)					\
	({								\
		u64 _val;						\
									\
		preempt_disable_notrace();				\
		_val = erratum_handler(read_ ## reg)();			\
		preempt_enable_notrace();				\
									\
		_val;							\
	})

/*
 * These register accessors are marked inline so the compiler can
 * nicely work out which register we want, and chuck away the rest of
 * the code.
 */
static __always_inline
void arch_timer_reg_write_cp15(int access, enum arch_timer_reg reg, u32 val)
{
	if (access == ARCH_TIMER_PHYS_ACCESS) {
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			write_sysreg(val, cntp_ctl_el0);
			break;
		case ARCH_TIMER_REG_TVAL:
			write_sysreg(val, cntp_tval_el0);
			break;
		}
	} else if (access == ARCH_TIMER_VIRT_ACCESS) {
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			write_sysreg(val, cntv_ctl_el0);
			break;
		case ARCH_TIMER_REG_TVAL:
			write_sysreg(val, cntv_tval_el0);
			break;
		}
	}

	isb();
}

static __always_inline
u32 arch_timer_reg_read_cp15(int access, enum arch_timer_reg reg)
{
	if (access == ARCH_TIMER_PHYS_ACCESS) {
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			return read_sysreg(cntp_ctl_el0);
		case ARCH_TIMER_REG_TVAL:
			return arch_timer_reg_read_stable(cntp_tval_el0);
		}
	} else if (access == ARCH_TIMER_VIRT_ACCESS) {
		switch (reg) {
		case ARCH_TIMER_REG_CTRL:
			return read_sysreg(cntv_ctl_el0);
		case ARCH_TIMER_REG_TVAL:
			return arch_timer_reg_read_stable(cntv_tval_el0);
		}
	}

	BUG();
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

/*
 * Ensure that reads of the counter are treated the same as memory reads
 * for the purposes of ordering by subsequent memory barriers.
 *
 * This insanity brought to you by speculative system register reads,
 * out-of-order memory accesses, sequence locks and Thomas Gleixner.
 *
 * http://lists.infradead.org/pipermail/linux-arm-kernel/2019-February/631195.html
 */
#define arch_counter_enforce_ordering(val) do {				\
	u64 tmp, _val = (val);						\
									\
	asm volatile(							\
	"	eor	%0, %1, %1\n"					\
	"	add	%0, sp, %0\n"					\
	"	ldr	xzr, [%0]"					\
	: "=r" (tmp) : "r" (_val));					\
} while (0)

static __always_inline u64 __arch_counter_get_cntpct_stable(void)
{
	u64 cnt;

	isb();
	cnt = arch_timer_reg_read_stable(cntpct_el0);
	arch_counter_enforce_ordering(cnt);
	return cnt;
}

static __always_inline u64 __arch_counter_get_cntpct(void)
{
	u64 cnt;

	isb();
	cnt = read_sysreg(cntpct_el0);
	arch_counter_enforce_ordering(cnt);
	return cnt;
}

static __always_inline u64 __arch_counter_get_cntvct_stable(void)
{
	u64 cnt;

	isb();
	cnt = arch_timer_reg_read_stable(cntvct_el0);
	arch_counter_enforce_ordering(cnt);
	return cnt;
}

static __always_inline u64 __arch_counter_get_cntvct(void)
{
	u64 cnt;

	isb();
	cnt = read_sysreg(cntvct_el0);
	arch_counter_enforce_ordering(cnt);
	return cnt;
}

#undef arch_counter_enforce_ordering

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

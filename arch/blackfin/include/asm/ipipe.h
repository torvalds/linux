/*   -*- linux-c -*-
 *   include/asm-blackfin/ipipe.h
 *
 *   Copyright (C) 2002-2007 Philippe Gerum.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __ASM_BLACKFIN_IPIPE_H
#define __ASM_BLACKFIN_IPIPE_H

#ifdef CONFIG_IPIPE

#include <linux/cpumask.h>
#include <linux/list.h>
#include <linux/threads.h>
#include <linux/irq.h>
#include <linux/ipipe_percpu.h>
#include <asm/ptrace.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/atomic.h>
#include <asm/traps.h>

#define IPIPE_ARCH_STRING     "1.11-00"
#define IPIPE_MAJOR_NUMBER    1
#define IPIPE_MINOR_NUMBER    11
#define IPIPE_PATCH_NUMBER    0

#ifdef CONFIG_SMP
#error "I-pipe/blackfin: SMP not implemented"
#else /* !CONFIG_SMP */
#define ipipe_processor_id()	0
#endif	/* CONFIG_SMP */

#define prepare_arch_switch(next)		\
do {						\
	ipipe_schedule_notify(current, next);	\
	local_irq_disable_hw();			\
} while (0)

#define task_hijacked(p)						\
	({								\
		int __x__ = __ipipe_root_domain_p;			\
		__clear_bit(IPIPE_SYNC_FLAG, &ipipe_root_cpudom_var(status)); \
		if (__x__)						\
			local_irq_enable_hw();				\
		!__x__;							\
	})

struct ipipe_domain;

struct ipipe_sysinfo {

	int ncpus;		/* Number of CPUs on board */
	u64 cpufreq;		/* CPU frequency (in Hz) */

	/* Arch-dependent block */

	struct {
		unsigned tmirq;	/* Timer tick IRQ */
		u64 tmfreq;	/* Timer frequency */
	} archdep;
};

#define ipipe_read_tsc(t)					\
	({							\
	unsigned long __cy2;					\
	__asm__ __volatile__ ("1: %0 = CYCLES2\n"		\
				"%1 = CYCLES\n"			\
				"%2 = CYCLES2\n"		\
				"CC = %2 == %0\n"		\
				"if ! CC jump 1b\n"		\
				: "=d,a" (((unsigned long *)&t)[1]),	\
				  "=d,a" (((unsigned long *)&t)[0]),	\
				  "=d,a" (__cy2)				\
				: /*no input*/ : "CC");			\
	t;								\
	})

#define ipipe_cpu_freq()	__ipipe_core_clock
#define ipipe_tsc2ns(_t)	(((unsigned long)(_t)) * __ipipe_freq_scale)
#define ipipe_tsc2us(_t)	(ipipe_tsc2ns(_t) / 1000 + 1)

/* Private interface -- Internal use only */

#define __ipipe_check_platform()	do { } while (0)

#define __ipipe_init_platform()		do { } while (0)

extern atomic_t __ipipe_irq_lvdepth[IVG15 + 1];

extern unsigned long __ipipe_irq_lvmask;

extern struct ipipe_domain ipipe_root;

/* enable/disable_irqdesc _must_ be used in pairs. */

void __ipipe_enable_irqdesc(struct ipipe_domain *ipd,
			    unsigned irq);

void __ipipe_disable_irqdesc(struct ipipe_domain *ipd,
			     unsigned irq);

#define __ipipe_enable_irq(irq)		(irq_desc[irq].chip->unmask(irq))

#define __ipipe_disable_irq(irq)	(irq_desc[irq].chip->mask(irq))

static inline int __ipipe_check_tickdev(const char *devname)
{
	return 1;
}

static inline void __ipipe_lock_root(void)
{
	set_bit(IPIPE_SYNCDEFER_FLAG, &ipipe_root_cpudom_var(status));
}

static inline void __ipipe_unlock_root(void)
{
	clear_bit(IPIPE_SYNCDEFER_FLAG, &ipipe_root_cpudom_var(status));
}

void __ipipe_enable_pipeline(void);

#define __ipipe_hook_critical_ipi(ipd) do { } while (0)

#define __ipipe_sync_pipeline  ___ipipe_sync_pipeline
void ___ipipe_sync_pipeline(unsigned long syncmask);

void __ipipe_handle_irq(unsigned irq, struct pt_regs *regs);

int __ipipe_get_irq_priority(unsigned irq);

void __ipipe_stall_root_raw(void);

void __ipipe_unstall_root_raw(void);

void __ipipe_serial_debug(const char *fmt, ...);

asmlinkage void __ipipe_call_irqtail(unsigned long addr);

DECLARE_PER_CPU(struct pt_regs, __ipipe_tick_regs);

extern unsigned long __ipipe_core_clock;

extern unsigned long __ipipe_freq_scale;

extern unsigned long __ipipe_irq_tail_hook;

static inline unsigned long __ipipe_ffnz(unsigned long ul)
{
	return ffs(ul) - 1;
}

#define __ipipe_run_irqtail()  /* Must be a macro */			\
	do {								\
		unsigned long __pending;				\
		CSYNC();						\
		__pending = bfin_read_IPEND();				\
		if (__pending & 0x8000) {				\
			__pending &= ~0x8010;				\
			if (__pending && (__pending & (__pending - 1)) == 0) \
				__ipipe_call_irqtail(__ipipe_irq_tail_hook); \
		}							\
	} while (0)

#define __ipipe_run_isr(ipd, irq)					\
	do {								\
		if (!__ipipe_pipeline_head_p(ipd))			\
			local_irq_enable_hw();				\
		if (ipd == ipipe_root_domain) {				\
			if (unlikely(ipipe_virtual_irq_p(irq))) {	\
				irq_enter();				\
				ipd->irqs[irq].handler(irq, ipd->irqs[irq].cookie); \
				irq_exit();				\
			} else 						\
				ipd->irqs[irq].handler(irq, &__raw_get_cpu_var(__ipipe_tick_regs)); \
		} else {						\
			__clear_bit(IPIPE_SYNC_FLAG, &ipipe_cpudom_var(ipd, status)); \
			ipd->irqs[irq].handler(irq, ipd->irqs[irq].cookie); \
			/* Attempt to exit the outer interrupt level before \
			 * starting the deferred IRQ processing. */	\
			__ipipe_run_irqtail();				\
			__set_bit(IPIPE_SYNC_FLAG, &ipipe_cpudom_var(ipd, status)); \
		}							\
		local_irq_disable_hw();					\
	} while (0)

#define __ipipe_syscall_watched_p(p, sc)	\
	(((p)->flags & PF_EVNOTIFY) || (unsigned long)sc >= NR_syscalls)

void ipipe_init_irq_threads(void);

int ipipe_start_irq_thread(unsigned irq, struct irq_desc *desc);

#ifdef CONFIG_TICKSOURCE_CORETMR
#define IRQ_SYSTMR		IRQ_CORETMR
#define IRQ_PRIOTMR		IRQ_CORETMR
#else
#define IRQ_SYSTMR		IRQ_TIMER0
#define IRQ_PRIOTMR		CONFIG_IRQ_TIMER0
#endif

#ifdef CONFIG_BF561
#define bfin_write_TIMER_DISABLE(val)	bfin_write_TMRS8_DISABLE(val)
#define bfin_write_TIMER_ENABLE(val)	bfin_write_TMRS8_ENABLE(val)
#define bfin_write_TIMER_STATUS(val)	bfin_write_TMRS8_STATUS(val)
#define bfin_read_TIMER_STATUS()	bfin_read_TMRS8_STATUS()
#elif defined(CONFIG_BF54x)
#define bfin_write_TIMER_DISABLE(val)	bfin_write_TIMER_DISABLE0(val)
#define bfin_write_TIMER_ENABLE(val)	bfin_write_TIMER_ENABLE0(val)
#define bfin_write_TIMER_STATUS(val)	bfin_write_TIMER_STATUS0(val)
#define bfin_read_TIMER_STATUS(val)	bfin_read_TIMER_STATUS0(val)
#endif

#define __ipipe_root_tick_p(regs)	((regs->ipend & 0x10) != 0)

#else /* !CONFIG_IPIPE */

#define task_hijacked(p)		0
#define ipipe_trap_notify(t, r)  	0

#define __ipipe_stall_root_raw()	do { } while (0)
#define __ipipe_unstall_root_raw()	do { } while (0)

#define ipipe_init_irq_threads()		do { } while (0)
#define ipipe_start_irq_thread(irq, desc)	0

#ifndef CONFIG_TICKSOURCE_GPTMR0
#define IRQ_SYSTMR		IRQ_CORETMR
#define IRQ_PRIOTMR		IRQ_CORETMR
#else
#define IRQ_SYSTMR		IRQ_TIMER0
#define IRQ_PRIOTMR		CONFIG_IRQ_TIMER0
#endif

#define __ipipe_root_tick_p(regs)	1

#endif /* !CONFIG_IPIPE */

#define ipipe_update_tick_evtdev(evtdev)	do { } while (0)

#endif	/* !__ASM_BLACKFIN_IPIPE_H */

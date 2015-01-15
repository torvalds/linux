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
#include <linux/atomic.h>
#include <asm/traps.h>
#include <asm/bitsperlong.h>

#define IPIPE_ARCH_STRING     "1.16-01"
#define IPIPE_MAJOR_NUMBER    1
#define IPIPE_MINOR_NUMBER    16
#define IPIPE_PATCH_NUMBER    1

#ifdef CONFIG_SMP
#error "I-pipe/blackfin: SMP not implemented"
#else /* !CONFIG_SMP */
#define ipipe_processor_id()	0
#endif	/* CONFIG_SMP */

#define prepare_arch_switch(next)		\
do {						\
	ipipe_schedule_notify(current, next);	\
	hard_local_irq_disable();			\
} while (0)

#define task_hijacked(p)						\
	({								\
		int __x__ = __ipipe_root_domain_p;			\
		if (__x__)						\
			hard_local_irq_enable();			\
		!__x__;							\
	})

struct ipipe_domain;

struct ipipe_sysinfo {
	int sys_nr_cpus;	/* Number of CPUs on board */
	int sys_hrtimer_irq;	/* hrtimer device IRQ */
	u64 sys_hrtimer_freq;	/* hrtimer device frequency */
	u64 sys_hrclock_freq;	/* hrclock device frequency */
	u64 sys_cpu_freq;	/* CPU frequency (Hz) */
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

#define __ipipe_enable_irq(irq)						\
	do {								\
		struct irq_desc *desc = irq_to_desc(irq);		\
		struct irq_chip *chip = get_irq_desc_chip(desc);	\
		chip->irq_unmask(&desc->irq_data);			\
	} while (0)

#define __ipipe_disable_irq(irq)					\
	do {								\
		struct irq_desc *desc = irq_to_desc(irq);		\
		struct irq_chip *chip = get_irq_desc_chip(desc);	\
		chip->irq_mask(&desc->irq_data);			\
	} while (0)

static inline int __ipipe_check_tickdev(const char *devname)
{
	return 1;
}

void __ipipe_enable_pipeline(void);

#define __ipipe_hook_critical_ipi(ipd) do { } while (0)

void ___ipipe_sync_pipeline(void);

void __ipipe_handle_irq(unsigned irq, struct pt_regs *regs);

int __ipipe_get_irq_priority(unsigned int irq);

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

#define __ipipe_do_root_xirq(ipd, irq)					\
	((ipd)->irqs[irq].handler(irq, raw_cpu_ptr(&__ipipe_tick_regs)))

#define __ipipe_run_irqtail(irq)  /* Must be a macro */			\
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

#define __ipipe_syscall_watched_p(p, sc)	\
	(ipipe_notifier_enabled_p(p) || (unsigned long)sc >= NR_syscalls)

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
#define __ipipe_root_tick_p(regs)	1

#endif /* !CONFIG_IPIPE */

#ifdef CONFIG_TICKSOURCE_CORETMR
#define IRQ_SYSTMR		IRQ_CORETMR
#define IRQ_PRIOTMR		IRQ_CORETMR
#else
#define IRQ_SYSTMR		IRQ_TIMER0
#define IRQ_PRIOTMR		CONFIG_IRQ_TIMER0
#endif

#define ipipe_update_tick_evtdev(evtdev)	do { } while (0)

#endif	/* !__ASM_BLACKFIN_IPIPE_H */

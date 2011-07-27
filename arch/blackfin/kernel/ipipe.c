/* -*- linux-c -*-
 * linux/arch/blackfin/kernel/ipipe.c
 *
 * Copyright (C) 2005-2007 Philippe Gerum.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 * USA; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Architecture-dependent I-pipe support for the Blackfin.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/kthread.h>
#include <linux/unistd.h>
#include <linux/io.h>
#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/irq_handler.h>

DEFINE_PER_CPU(struct pt_regs, __ipipe_tick_regs);

asmlinkage void asm_do_IRQ(unsigned int irq, struct pt_regs *regs);

static void __ipipe_no_irqtail(void);

unsigned long __ipipe_irq_tail_hook = (unsigned long)&__ipipe_no_irqtail;
EXPORT_SYMBOL(__ipipe_irq_tail_hook);

unsigned long __ipipe_core_clock;
EXPORT_SYMBOL(__ipipe_core_clock);

unsigned long __ipipe_freq_scale;
EXPORT_SYMBOL(__ipipe_freq_scale);

atomic_t __ipipe_irq_lvdepth[IVG15 + 1];

unsigned long __ipipe_irq_lvmask = bfin_no_irqs;
EXPORT_SYMBOL(__ipipe_irq_lvmask);

static void __ipipe_ack_irq(unsigned irq, struct irq_desc *desc)
{
	desc->ipipe_ack(irq, desc);
}

/*
 * __ipipe_enable_pipeline() -- We are running on the boot CPU, hw
 * interrupts are off, and secondary CPUs are still lost in space.
 */
void __ipipe_enable_pipeline(void)
{
	unsigned irq;

	__ipipe_core_clock = get_cclk(); /* Fetch this once. */
	__ipipe_freq_scale = 1000000000UL / __ipipe_core_clock;

	for (irq = 0; irq < NR_IRQS; ++irq)
		ipipe_virtualize_irq(ipipe_root_domain,
				     irq,
				     (ipipe_irq_handler_t)&asm_do_IRQ,
				     NULL,
				     &__ipipe_ack_irq,
				     IPIPE_HANDLE_MASK | IPIPE_PASS_MASK);
}

/*
 * __ipipe_handle_irq() -- IPIPE's generic IRQ handler. An optimistic
 * interrupt protection log is maintained here for each domain. Hw
 * interrupts are masked on entry.
 */
void __ipipe_handle_irq(unsigned irq, struct pt_regs *regs)
{
	struct ipipe_percpu_domain_data *p = ipipe_root_cpudom_ptr();
	struct ipipe_domain *this_domain, *next_domain;
	struct list_head *head, *pos;
	struct ipipe_irqdesc *idesc;
	int m_ack, s = -1;

	/*
	 * Software-triggered IRQs do not need any ack.  The contents
	 * of the register frame should only be used when processing
	 * the timer interrupt, but not for handling any other
	 * interrupt.
	 */
	m_ack = (regs == NULL || irq == IRQ_SYSTMR || irq == IRQ_CORETMR);
	this_domain = __ipipe_current_domain;
	idesc = &this_domain->irqs[irq];

	if (unlikely(test_bit(IPIPE_STICKY_FLAG, &idesc->control)))
		head = &this_domain->p_link;
	else {
		head = __ipipe_pipeline.next;
		next_domain = list_entry(head, struct ipipe_domain, p_link);
		idesc = &next_domain->irqs[irq];
		if (likely(test_bit(IPIPE_WIRED_FLAG, &idesc->control))) {
			if (!m_ack && idesc->acknowledge != NULL)
				idesc->acknowledge(irq, irq_to_desc(irq));
			if (test_bit(IPIPE_SYNCDEFER_FLAG, &p->status))
				s = __test_and_set_bit(IPIPE_STALL_FLAG,
						       &p->status);
			__ipipe_dispatch_wired(next_domain, irq);
			goto out;
		}
	}

	/* Ack the interrupt. */

	pos = head;
	while (pos != &__ipipe_pipeline) {
		next_domain = list_entry(pos, struct ipipe_domain, p_link);
		idesc = &next_domain->irqs[irq];
		if (test_bit(IPIPE_HANDLE_FLAG, &idesc->control)) {
			__ipipe_set_irq_pending(next_domain, irq);
			if (!m_ack && idesc->acknowledge != NULL) {
				idesc->acknowledge(irq, irq_to_desc(irq));
				m_ack = 1;
			}
		}
		if (!test_bit(IPIPE_PASS_FLAG, &idesc->control))
			break;
		pos = next_domain->p_link.next;
	}

	/*
	 * Now walk the pipeline, yielding control to the highest
	 * priority domain that has pending interrupt(s) or
	 * immediately to the current domain if the interrupt has been
	 * marked as 'sticky'. This search does not go beyond the
	 * current domain in the pipeline. We also enforce the
	 * additional root stage lock (blackfin-specific).
	 */
	if (test_bit(IPIPE_SYNCDEFER_FLAG, &p->status))
		s = __test_and_set_bit(IPIPE_STALL_FLAG, &p->status);

	/*
	 * If the interrupt preempted the head domain, then do not
	 * even try to walk the pipeline, unless an interrupt is
	 * pending for it.
	 */
	if (test_bit(IPIPE_AHEAD_FLAG, &this_domain->flags) &&
	    !__ipipe_ipending_p(ipipe_head_cpudom_ptr()))
		goto out;

	__ipipe_walk_pipeline(head);
out:
	if (!s)
		__clear_bit(IPIPE_STALL_FLAG, &p->status);
}

void __ipipe_enable_irqdesc(struct ipipe_domain *ipd, unsigned irq)
{
	struct irq_desc *desc = irq_to_desc(irq);
	int prio = __ipipe_get_irq_priority(irq);

	desc->depth = 0;
	if (ipd != &ipipe_root &&
	    atomic_inc_return(&__ipipe_irq_lvdepth[prio]) == 1)
		__set_bit(prio, &__ipipe_irq_lvmask);
}
EXPORT_SYMBOL(__ipipe_enable_irqdesc);

void __ipipe_disable_irqdesc(struct ipipe_domain *ipd, unsigned irq)
{
	int prio = __ipipe_get_irq_priority(irq);

	if (ipd != &ipipe_root &&
	    atomic_dec_and_test(&__ipipe_irq_lvdepth[prio]))
		__clear_bit(prio, &__ipipe_irq_lvmask);
}
EXPORT_SYMBOL(__ipipe_disable_irqdesc);

asmlinkage int __ipipe_syscall_root(struct pt_regs *regs)
{
	struct ipipe_percpu_domain_data *p;
	void (*hook)(void);
	int ret;

	WARN_ON_ONCE(irqs_disabled_hw());

	/*
	 * We need to run the IRQ tail hook each time we intercept a
	 * syscall, because we know that important operations might be
	 * pending there (e.g. Xenomai deferred rescheduling).
	 */
	hook = (__typeof__(hook))__ipipe_irq_tail_hook;
	hook();

	/*
	 * This routine either returns:
	 * 0 -- if the syscall is to be passed to Linux;
	 * >0 -- if the syscall should not be passed to Linux, and no
	 * tail work should be performed;
	 * <0 -- if the syscall should not be passed to Linux but the
	 * tail work has to be performed (for handling signals etc).
	 */

	if (!__ipipe_syscall_watched_p(current, regs->orig_p0) ||
	    !__ipipe_event_monitored_p(IPIPE_EVENT_SYSCALL))
		return 0;

	ret = __ipipe_dispatch_event(IPIPE_EVENT_SYSCALL, regs);

	hard_local_irq_disable();

	/*
	 * This is the end of the syscall path, so we may
	 * safely assume a valid Linux task stack here.
	 */
	if (current->ipipe_flags & PF_EVTRET) {
		current->ipipe_flags &= ~PF_EVTRET;
		__ipipe_dispatch_event(IPIPE_EVENT_RETURN, regs);
	}

	if (!__ipipe_root_domain_p)
		ret = -1;
	else {
		p = ipipe_root_cpudom_ptr();
		if (__ipipe_ipending_p(p))
			__ipipe_sync_pipeline();
	}

	hard_local_irq_enable();

	return -ret;
}

static void __ipipe_no_irqtail(void)
{
}

int ipipe_get_sysinfo(struct ipipe_sysinfo *info)
{
	info->sys_nr_cpus = num_online_cpus();
	info->sys_cpu_freq = ipipe_cpu_freq();
	info->sys_hrtimer_irq = IPIPE_TIMER_IRQ;
	info->sys_hrtimer_freq = __ipipe_core_clock;
	info->sys_hrclock_freq = __ipipe_core_clock;

	return 0;
}

/*
 * ipipe_trigger_irq() -- Push the interrupt at front of the pipeline
 * just like if it has been actually received from a hw source. Also
 * works for virtual interrupts.
 */
int ipipe_trigger_irq(unsigned irq)
{
	unsigned long flags;

#ifdef CONFIG_IPIPE_DEBUG
	if (irq >= IPIPE_NR_IRQS ||
	    (ipipe_virtual_irq_p(irq)
	     && !test_bit(irq - IPIPE_VIRQ_BASE, &__ipipe_virtual_irq_map)))
		return -EINVAL;
#endif

	flags = hard_local_irq_save();
	__ipipe_handle_irq(irq, NULL);
	hard_local_irq_restore(flags);

	return 1;
}

asmlinkage void __ipipe_sync_root(void)
{
	void (*irq_tail_hook)(void) = (void (*)(void))__ipipe_irq_tail_hook;
	struct ipipe_percpu_domain_data *p;
	unsigned long flags;

	BUG_ON(irqs_disabled());

	flags = hard_local_irq_save();

	if (irq_tail_hook)
		irq_tail_hook();

	clear_thread_flag(TIF_IRQ_SYNC);

	p = ipipe_root_cpudom_ptr();
	if (__ipipe_ipending_p(p))
		__ipipe_sync_pipeline();

	hard_local_irq_restore(flags);
}

void ___ipipe_sync_pipeline(void)
{
	if (__ipipe_root_domain_p &&
	    test_bit(IPIPE_SYNCDEFER_FLAG, &ipipe_root_cpudom_var(status)))
		return;

	__ipipe_sync_stage();
}

void __ipipe_disable_root_irqs_hw(void)
{
	/*
	 * This code is called by the ins{bwl} routines (see
	 * arch/blackfin/lib/ins.S), which are heavily used by the
	 * network stack. It masks all interrupts but those handled by
	 * non-root domains, so that we keep decent network transfer
	 * rates for Linux without inducing pathological jitter for
	 * the real-time domain.
	 */
	bfin_sti(__ipipe_irq_lvmask);
	__set_bit(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status));
}

void __ipipe_enable_root_irqs_hw(void)
{
	__clear_bit(IPIPE_STALL_FLAG, &ipipe_root_cpudom_var(status));
	bfin_sti(bfin_irq_flags);
}

/*
 * We could use standard atomic bitops in the following root status
 * manipulation routines, but let's prepare for SMP support in the
 * same move, preventing CPU migration as required.
 */
void __ipipe_stall_root(void)
{
	unsigned long *p, flags;

	flags = hard_local_irq_save();
	p = &__ipipe_root_status;
	__set_bit(IPIPE_STALL_FLAG, p);
	hard_local_irq_restore(flags);
}
EXPORT_SYMBOL(__ipipe_stall_root);

unsigned long __ipipe_test_and_stall_root(void)
{
	unsigned long *p, flags;
	int x;

	flags = hard_local_irq_save();
	p = &__ipipe_root_status;
	x = __test_and_set_bit(IPIPE_STALL_FLAG, p);
	hard_local_irq_restore(flags);

	return x;
}
EXPORT_SYMBOL(__ipipe_test_and_stall_root);

unsigned long __ipipe_test_root(void)
{
	const unsigned long *p;
	unsigned long flags;
	int x;

	flags = hard_local_irq_save_smp();
	p = &__ipipe_root_status;
	x = test_bit(IPIPE_STALL_FLAG, p);
	hard_local_irq_restore_smp(flags);

	return x;
}
EXPORT_SYMBOL(__ipipe_test_root);

void __ipipe_lock_root(void)
{
	unsigned long *p, flags;

	flags = hard_local_irq_save();
	p = &__ipipe_root_status;
	__set_bit(IPIPE_SYNCDEFER_FLAG, p);
	hard_local_irq_restore(flags);
}
EXPORT_SYMBOL(__ipipe_lock_root);

void __ipipe_unlock_root(void)
{
	unsigned long *p, flags;

	flags = hard_local_irq_save();
	p = &__ipipe_root_status;
	__clear_bit(IPIPE_SYNCDEFER_FLAG, p);
	hard_local_irq_restore(flags);
}
EXPORT_SYMBOL(__ipipe_unlock_root);

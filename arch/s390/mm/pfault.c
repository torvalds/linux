// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 1999, 2023
 */

#include <linux/cpuhotplug.h>
#include <linux/sched/task.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/asm-extable.h>
#include <asm/pfault.h>
#include <asm/diag.h>

#define __SUBCODE_MASK 0x0600
#define __PF_RES_FIELD 0x8000000000000000UL

/*
 * 'pfault' pseudo page faults routines.
 */
static int pfault_disable;

static int __init nopfault(char *str)
{
	pfault_disable = 1;
	return 1;
}
early_param("nopfault", nopfault);

struct pfault_refbk {
	u16 refdiagc;
	u16 reffcode;
	u16 refdwlen;
	u16 refversn;
	u64 refgaddr;
	u64 refselmk;
	u64 refcmpmk;
	u64 reserved;
};

static struct pfault_refbk pfault_init_refbk = {
	.refdiagc = 0x258,
	.reffcode = 0,
	.refdwlen = 5,
	.refversn = 2,
	.refgaddr = __LC_LPP,
	.refselmk = 1UL << 48,
	.refcmpmk = 1UL << 48,
	.reserved = __PF_RES_FIELD
};

int __pfault_init(void)
{
	int rc = -EOPNOTSUPP;

	if (pfault_disable)
		return rc;
	diag_stat_inc(DIAG_STAT_X258);
	asm volatile(
		"	diag	%[refbk],%[rc],0x258\n"
		"0:	nopr	%%r7\n"
		EX_TABLE(0b, 0b)
		: [rc] "+d" (rc)
		: [refbk] "a" (&pfault_init_refbk), "m" (pfault_init_refbk)
		: "cc");
	return rc;
}

static struct pfault_refbk pfault_fini_refbk = {
	.refdiagc = 0x258,
	.reffcode = 1,
	.refdwlen = 5,
	.refversn = 2,
};

void __pfault_fini(void)
{
	if (pfault_disable)
		return;
	diag_stat_inc(DIAG_STAT_X258);
	asm volatile(
		"	diag	%[refbk],0,0x258\n"
		"0:	nopr	%%r7\n"
		EX_TABLE(0b, 0b)
		:
		: [refbk] "a" (&pfault_fini_refbk), "m" (pfault_fini_refbk)
		: "cc");
}

static DEFINE_SPINLOCK(pfault_lock);
static LIST_HEAD(pfault_list);

#define PF_COMPLETE	0x0080

/*
 * The mechanism of our pfault code: if Linux is running as guest, runs a user
 * space process and the user space process accesses a page that the host has
 * paged out we get a pfault interrupt.
 *
 * This allows us, within the guest, to schedule a different process. Without
 * this mechanism the host would have to suspend the whole virtual cpu until
 * the page has been paged in.
 *
 * So when we get such an interrupt then we set the state of the current task
 * to uninterruptible and also set the need_resched flag. Both happens within
 * interrupt context(!). If we later on want to return to user space we
 * recognize the need_resched flag and then call schedule().  It's not very
 * obvious how this works...
 *
 * Of course we have a lot of additional fun with the completion interrupt (->
 * host signals that a page of a process has been paged in and the process can
 * continue to run). This interrupt can arrive on any cpu and, since we have
 * virtual cpus, actually appear before the interrupt that signals that a page
 * is missing.
 */
static void pfault_interrupt(struct ext_code ext_code,
			     unsigned int param32, unsigned long param64)
{
	struct task_struct *tsk;
	__u16 subcode;
	pid_t pid;

	/*
	 * Get the external interruption subcode & pfault initial/completion
	 * signal bit. VM stores this in the 'cpu address' field associated
	 * with the external interrupt.
	 */
	subcode = ext_code.subcode;
	if ((subcode & 0xff00) != __SUBCODE_MASK)
		return;
	inc_irq_stat(IRQEXT_PFL);
	/* Get the token (= pid of the affected task). */
	pid = param64 & LPP_PID_MASK;
	rcu_read_lock();
	tsk = find_task_by_pid_ns(pid, &init_pid_ns);
	if (tsk)
		get_task_struct(tsk);
	rcu_read_unlock();
	if (!tsk)
		return;
	spin_lock(&pfault_lock);
	if (subcode & PF_COMPLETE) {
		/* signal bit is set -> a page has been swapped in by VM */
		if (tsk->thread.pfault_wait == 1) {
			/*
			 * Initial interrupt was faster than the completion
			 * interrupt. pfault_wait is valid. Set pfault_wait
			 * back to zero and wake up the process. This can
			 * safely be done because the task is still sleeping
			 * and can't produce new pfaults.
			 */
			tsk->thread.pfault_wait = 0;
			list_del(&tsk->thread.list);
			wake_up_process(tsk);
			put_task_struct(tsk);
		} else {
			/*
			 * Completion interrupt was faster than initial
			 * interrupt. Set pfault_wait to -1 so the initial
			 * interrupt doesn't put the task to sleep.
			 * If the task is not running, ignore the completion
			 * interrupt since it must be a leftover of a PFAULT
			 * CANCEL operation which didn't remove all pending
			 * completion interrupts.
			 */
			if (task_is_running(tsk))
				tsk->thread.pfault_wait = -1;
		}
	} else {
		/* signal bit not set -> a real page is missing. */
		if (WARN_ON_ONCE(tsk != current))
			goto out;
		if (tsk->thread.pfault_wait == 1) {
			/* Already on the list with a reference: put to sleep */
			goto block;
		} else if (tsk->thread.pfault_wait == -1) {
			/*
			 * Completion interrupt was faster than the initial
			 * interrupt (pfault_wait == -1). Set pfault_wait
			 * back to zero and exit.
			 */
			tsk->thread.pfault_wait = 0;
		} else {
			/*
			 * Initial interrupt arrived before completion
			 * interrupt. Let the task sleep.
			 * An extra task reference is needed since a different
			 * cpu may set the task state to TASK_RUNNING again
			 * before the scheduler is reached.
			 */
			get_task_struct(tsk);
			tsk->thread.pfault_wait = 1;
			list_add(&tsk->thread.list, &pfault_list);
block:
			/*
			 * Since this must be a userspace fault, there
			 * is no kernel task state to trample. Rely on the
			 * return to userspace schedule() to block.
			 */
			__set_current_state(TASK_UNINTERRUPTIBLE);
			set_tsk_need_resched(tsk);
			set_preempt_need_resched();
		}
	}
out:
	spin_unlock(&pfault_lock);
	put_task_struct(tsk);
}

static int pfault_cpu_dead(unsigned int cpu)
{
	struct thread_struct *thread, *next;
	struct task_struct *tsk;

	spin_lock_irq(&pfault_lock);
	list_for_each_entry_safe(thread, next, &pfault_list, list) {
		thread->pfault_wait = 0;
		list_del(&thread->list);
		tsk = container_of(thread, struct task_struct, thread);
		wake_up_process(tsk);
		put_task_struct(tsk);
	}
	spin_unlock_irq(&pfault_lock);
	return 0;
}

static int __init pfault_irq_init(void)
{
	int rc;

	rc = register_external_irq(EXT_IRQ_CP_SERVICE, pfault_interrupt);
	if (rc)
		goto out_extint;
	rc = pfault_init() == 0 ? 0 : -EOPNOTSUPP;
	if (rc)
		goto out_pfault;
	irq_subclass_register(IRQ_SUBCLASS_SERVICE_SIGNAL);
	cpuhp_setup_state_nocalls(CPUHP_S390_PFAULT_DEAD, "s390/pfault:dead",
				  NULL, pfault_cpu_dead);
	return 0;

out_pfault:
	unregister_external_irq(EXT_IRQ_CP_SERVICE, pfault_interrupt);
out_extint:
	pfault_disable = 1;
	return rc;
}
early_initcall(pfault_irq_init);

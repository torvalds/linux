/*
 *  arch/s390/kernel/vtime.c
 *    Virtual cpu timer based timer functions.
 *
 *  S390 version
 *    Copyright (C) 2004 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Jan Glauber <jan.glauber@de.ibm.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/types.h>
#include <linux/timex.h>
#include <linux/notifier.h>
#include <linux/kernel_stat.h>
#include <linux/rcupdate.h>
#include <linux/posix-timers.h>

#include <asm/s390_ext.h>
#include <asm/timer.h>
#include <asm/irq_regs.h>
#include <asm/cpu.h>

static ext_int_info_t ext_int_info_timer;

static DEFINE_PER_CPU(struct vtimer_queue, virt_cpu_timer);

DEFINE_PER_CPU(struct s390_idle_data, s390_idle) = {
	.lock = __SPIN_LOCK_UNLOCKED(s390_idle.lock)
};

static inline __u64 get_vtimer(void)
{
	__u64 timer;

	asm volatile("STPT %0" : "=m" (timer));
	return timer;
}

static inline void set_vtimer(__u64 expires)
{
	__u64 timer;

	asm volatile ("  STPT %0\n"  /* Store current cpu timer value */
		      "  SPT %1"     /* Set new value immediatly afterwards */
		      : "=m" (timer) : "m" (expires) );
	S390_lowcore.system_timer += S390_lowcore.last_update_timer - timer;
	S390_lowcore.last_update_timer = expires;
}

/*
 * Update process times based on virtual cpu times stored by entry.S
 * to the lowcore fields user_timer, system_timer & steal_clock.
 */
static void do_account_vtime(struct task_struct *tsk, int hardirq_offset)
{
	struct thread_info *ti = task_thread_info(tsk);
	__u64 timer, clock, user, system, steal;

	timer = S390_lowcore.last_update_timer;
	clock = S390_lowcore.last_update_clock;
	asm volatile ("  STPT %0\n"    /* Store current cpu timer value */
		      "  STCK %1"      /* Store current tod clock value */
		      : "=m" (S390_lowcore.last_update_timer),
		        "=m" (S390_lowcore.last_update_clock) );
	S390_lowcore.system_timer += timer - S390_lowcore.last_update_timer;
	S390_lowcore.steal_timer += S390_lowcore.last_update_clock - clock;

	user = S390_lowcore.user_timer - ti->user_timer;
	S390_lowcore.steal_timer -= user;
	ti->user_timer = S390_lowcore.user_timer;
	account_user_time(tsk, user, user);

	system = S390_lowcore.system_timer - ti->system_timer;
	S390_lowcore.steal_timer -= system;
	ti->system_timer = S390_lowcore.system_timer;
	account_system_time(tsk, hardirq_offset, system, system);

	steal = S390_lowcore.steal_timer;
	if ((s64) steal > 0) {
		S390_lowcore.steal_timer = 0;
		account_steal_time(steal);
	}
}

void account_vtime(struct task_struct *prev, struct task_struct *next)
{
	struct thread_info *ti;

	do_account_vtime(prev, 0);
	ti = task_thread_info(prev);
	ti->user_timer = S390_lowcore.user_timer;
	ti->system_timer = S390_lowcore.system_timer;
	ti = task_thread_info(next);
	S390_lowcore.user_timer = ti->user_timer;
	S390_lowcore.system_timer = ti->system_timer;
}

void account_process_tick(struct task_struct *tsk, int user_tick)
{
	do_account_vtime(tsk, HARDIRQ_OFFSET);
}

/*
 * Update process times based on virtual cpu times stored by entry.S
 * to the lowcore fields user_timer, system_timer & steal_clock.
 */
void account_system_vtime(struct task_struct *tsk)
{
	struct thread_info *ti = task_thread_info(tsk);
	__u64 timer, system;

	timer = S390_lowcore.last_update_timer;
	S390_lowcore.last_update_timer = get_vtimer();
	S390_lowcore.system_timer += timer - S390_lowcore.last_update_timer;

	system = S390_lowcore.system_timer - ti->system_timer;
	S390_lowcore.steal_timer -= system;
	ti->system_timer = S390_lowcore.system_timer;
	account_system_time(tsk, 0, system, system);
}
EXPORT_SYMBOL_GPL(account_system_vtime);

void vtime_start_cpu(void)
{
	struct s390_idle_data *idle = &__get_cpu_var(s390_idle);
	struct vtimer_queue *vq = &__get_cpu_var(virt_cpu_timer);
	__u64 idle_time, expires;

	/* Account time spent with enabled wait psw loaded as idle time. */
	idle_time = S390_lowcore.int_clock - idle->idle_enter;
	account_idle_time(idle_time);
	S390_lowcore.last_update_clock = S390_lowcore.int_clock;

	/* Account system time spent going idle. */
	S390_lowcore.system_timer += S390_lowcore.last_update_timer - vq->idle;
	S390_lowcore.last_update_timer = S390_lowcore.async_enter_timer;

	/* Restart vtime CPU timer */
	if (vq->do_spt) {
		/* Program old expire value but first save progress. */
		expires = vq->idle - S390_lowcore.async_enter_timer;
		expires += get_vtimer();
		set_vtimer(expires);
	} else {
		/* Don't account the CPU timer delta while the cpu was idle. */
		vq->elapsed -= vq->idle - S390_lowcore.async_enter_timer;
	}

	spin_lock(&idle->lock);
	idle->idle_time += idle_time;
	idle->idle_enter = 0ULL;
	idle->idle_count++;
	spin_unlock(&idle->lock);
}

void vtime_stop_cpu(void)
{
	struct s390_idle_data *idle = &__get_cpu_var(s390_idle);
	struct vtimer_queue *vq = &__get_cpu_var(virt_cpu_timer);
	psw_t psw;

	/* Wait for external, I/O or machine check interrupt. */
	psw.mask = psw_kernel_bits | PSW_MASK_WAIT | PSW_MASK_IO | PSW_MASK_EXT;

	/* Check if the CPU timer needs to be reprogrammed. */
	if (vq->do_spt) {
		__u64 vmax = VTIMER_MAX_SLICE;
		/*
		 * The inline assembly is equivalent to
		 *	vq->idle = get_cpu_timer();
		 *	set_cpu_timer(VTIMER_MAX_SLICE);
		 *	idle->idle_enter = get_clock();
		 *	__load_psw_mask(psw_kernel_bits | PSW_MASK_WAIT |
		 *			   PSW_MASK_IO | PSW_MASK_EXT);
		 * The difference is that the inline assembly makes sure that
		 * the last three instruction are stpt, stck and lpsw in that
		 * order. This is done to increase the precision.
		 */
		asm volatile(
#ifndef CONFIG_64BIT
			"	basr	1,0\n"
			"0:	ahi	1,1f-0b\n"
			"	st	1,4(%2)\n"
#else /* CONFIG_64BIT */
			"	larl	1,1f\n"
			"	stg	1,8(%2)\n"
#endif /* CONFIG_64BIT */
			"	stpt	0(%4)\n"
			"	spt	0(%5)\n"
			"	stck	0(%3)\n"
#ifndef CONFIG_64BIT
			"	lpsw	0(%2)\n"
#else /* CONFIG_64BIT */
			"	lpswe	0(%2)\n"
#endif /* CONFIG_64BIT */
			"1:"
			: "=m" (idle->idle_enter), "=m" (vq->idle)
			: "a" (&psw), "a" (&idle->idle_enter),
			  "a" (&vq->idle), "a" (&vmax), "m" (vmax), "m" (psw)
			: "memory", "cc", "1");
	} else {
		/*
		 * The inline assembly is equivalent to
		 *	vq->idle = get_cpu_timer();
		 *	idle->idle_enter = get_clock();
		 *	__load_psw_mask(psw_kernel_bits | PSW_MASK_WAIT |
		 *			   PSW_MASK_IO | PSW_MASK_EXT);
		 * The difference is that the inline assembly makes sure that
		 * the last three instruction are stpt, stck and lpsw in that
		 * order. This is done to increase the precision.
		 */
		asm volatile(
#ifndef CONFIG_64BIT
			"	basr	1,0\n"
			"0:	ahi	1,1f-0b\n"
			"	st	1,4(%2)\n"
#else /* CONFIG_64BIT */
			"	larl	1,1f\n"
			"	stg	1,8(%2)\n"
#endif /* CONFIG_64BIT */
			"	stpt	0(%4)\n"
			"	stck	0(%3)\n"
#ifndef CONFIG_64BIT
			"	lpsw	0(%2)\n"
#else /* CONFIG_64BIT */
			"	lpswe	0(%2)\n"
#endif /* CONFIG_64BIT */
			"1:"
			: "=m" (idle->idle_enter), "=m" (vq->idle)
			: "a" (&psw), "a" (&idle->idle_enter),
			  "a" (&vq->idle), "m" (psw)
			: "memory", "cc", "1");
	}
}

/*
 * Sorted add to a list. List is linear searched until first bigger
 * element is found.
 */
static void list_add_sorted(struct vtimer_list *timer, struct list_head *head)
{
	struct vtimer_list *event;

	list_for_each_entry(event, head, entry) {
		if (event->expires > timer->expires) {
			list_add_tail(&timer->entry, &event->entry);
			return;
		}
	}
	list_add_tail(&timer->entry, head);
}

/*
 * Do the callback functions of expired vtimer events.
 * Called from within the interrupt handler.
 */
static void do_callbacks(struct list_head *cb_list)
{
	struct vtimer_queue *vq;
	struct vtimer_list *event, *tmp;

	if (list_empty(cb_list))
		return;

	vq = &__get_cpu_var(virt_cpu_timer);

	list_for_each_entry_safe(event, tmp, cb_list, entry) {
		list_del_init(&event->entry);
		(event->function)(event->data);
		if (event->interval) {
			/* Recharge interval timer */
			event->expires = event->interval + vq->elapsed;
			spin_lock(&vq->lock);
			list_add_sorted(event, &vq->list);
			spin_unlock(&vq->lock);
		}
	}
}

/*
 * Handler for the virtual CPU timer.
 */
static void do_cpu_timer_interrupt(__u16 error_code)
{
	struct vtimer_queue *vq;
	struct vtimer_list *event, *tmp;
	struct list_head cb_list;	/* the callback queue */
	__u64 elapsed, next;

	INIT_LIST_HEAD(&cb_list);
	vq = &__get_cpu_var(virt_cpu_timer);

	/* walk timer list, fire all expired events */
	spin_lock(&vq->lock);

	elapsed = vq->elapsed + (vq->timer - S390_lowcore.async_enter_timer);
	BUG_ON((s64) elapsed < 0);
	vq->elapsed = 0;
	list_for_each_entry_safe(event, tmp, &vq->list, entry) {
		if (event->expires < elapsed)
			/* move expired timer to the callback queue */
			list_move_tail(&event->entry, &cb_list);
		else
			event->expires -= elapsed;
	}
	spin_unlock(&vq->lock);

	vq->do_spt = list_empty(&cb_list);
	do_callbacks(&cb_list);

	/* next event is first in list */
	next = VTIMER_MAX_SLICE;
	spin_lock(&vq->lock);
	if (!list_empty(&vq->list)) {
		event = list_first_entry(&vq->list, struct vtimer_list, entry);
		next = event->expires;
	} else
		vq->do_spt = 0;
	spin_unlock(&vq->lock);
	/*
	 * To improve precision add the time spent by the
	 * interrupt handler to the elapsed time.
	 * Note: CPU timer counts down and we got an interrupt,
	 *	 the current content is negative
	 */
	elapsed = S390_lowcore.async_enter_timer - get_vtimer();
	set_vtimer(next - elapsed);
	vq->timer = next - elapsed;
	vq->elapsed = elapsed;
}

void init_virt_timer(struct vtimer_list *timer)
{
	timer->function = NULL;
	INIT_LIST_HEAD(&timer->entry);
}
EXPORT_SYMBOL(init_virt_timer);

static inline int vtimer_pending(struct vtimer_list *timer)
{
	return (!list_empty(&timer->entry));
}

/*
 * this function should only run on the specified CPU
 */
static void internal_add_vtimer(struct vtimer_list *timer)
{
	struct vtimer_queue *vq;
	unsigned long flags;
	__u64 left, expires;

	vq = &per_cpu(virt_cpu_timer, timer->cpu);
	spin_lock_irqsave(&vq->lock, flags);

	BUG_ON(timer->cpu != smp_processor_id());

	if (list_empty(&vq->list)) {
		/* First timer on this cpu, just program it. */
		list_add(&timer->entry, &vq->list);
		set_vtimer(timer->expires);
		vq->timer = timer->expires;
		vq->elapsed = 0;
	} else {
		/* Check progress of old timers. */
		expires = timer->expires;
		left = get_vtimer();
		if (likely((s64) expires < (s64) left)) {
			/* The new timer expires before the current timer. */
			set_vtimer(expires);
			vq->elapsed += vq->timer - left;
			vq->timer = expires;
		} else {
			vq->elapsed += vq->timer - left;
			vq->timer = left;
		}
		/* Insert new timer into per cpu list. */
		timer->expires += vq->elapsed;
		list_add_sorted(timer, &vq->list);
	}

	spin_unlock_irqrestore(&vq->lock, flags);
	/* release CPU acquired in prepare_vtimer or mod_virt_timer() */
	put_cpu();
}

static inline void prepare_vtimer(struct vtimer_list *timer)
{
	BUG_ON(!timer->function);
	BUG_ON(!timer->expires || timer->expires > VTIMER_MAX_SLICE);
	BUG_ON(vtimer_pending(timer));
	timer->cpu = get_cpu();
}

/*
 * add_virt_timer - add an oneshot virtual CPU timer
 */
void add_virt_timer(void *new)
{
	struct vtimer_list *timer;

	timer = (struct vtimer_list *)new;
	prepare_vtimer(timer);
	timer->interval = 0;
	internal_add_vtimer(timer);
}
EXPORT_SYMBOL(add_virt_timer);

/*
 * add_virt_timer_int - add an interval virtual CPU timer
 */
void add_virt_timer_periodic(void *new)
{
	struct vtimer_list *timer;

	timer = (struct vtimer_list *)new;
	prepare_vtimer(timer);
	timer->interval = timer->expires;
	internal_add_vtimer(timer);
}
EXPORT_SYMBOL(add_virt_timer_periodic);

/*
 * If we change a pending timer the function must be called on the CPU
 * where the timer is running on, e.g. by smp_call_function_single()
 *
 * The original mod_timer adds the timer if it is not pending. For
 * compatibility we do the same. The timer will be added on the current
 * CPU as a oneshot timer.
 *
 * returns whether it has modified a pending timer (1) or not (0)
 */
int mod_virt_timer(struct vtimer_list *timer, __u64 expires)
{
	struct vtimer_queue *vq;
	unsigned long flags;
	int cpu;

	BUG_ON(!timer->function);
	BUG_ON(!expires || expires > VTIMER_MAX_SLICE);

	/*
	 * This is a common optimization triggered by the
	 * networking code - if the timer is re-modified
	 * to be the same thing then just return:
	 */
	if (timer->expires == expires && vtimer_pending(timer))
		return 1;

	cpu = get_cpu();
	vq = &per_cpu(virt_cpu_timer, cpu);

	/* check if we run on the right CPU */
	BUG_ON(timer->cpu != cpu);

	/* disable interrupts before test if timer is pending */
	spin_lock_irqsave(&vq->lock, flags);

	/* if timer isn't pending add it on the current CPU */
	if (!vtimer_pending(timer)) {
		spin_unlock_irqrestore(&vq->lock, flags);
		/* we do not activate an interval timer with mod_virt_timer */
		timer->interval = 0;
		timer->expires = expires;
		timer->cpu = cpu;
		internal_add_vtimer(timer);
		return 0;
	}

	list_del_init(&timer->entry);
	timer->expires = expires;

	/* also change the interval if we have an interval timer */
	if (timer->interval)
		timer->interval = expires;

	/* the timer can't expire anymore so we can release the lock */
	spin_unlock_irqrestore(&vq->lock, flags);
	internal_add_vtimer(timer);
	return 1;
}
EXPORT_SYMBOL(mod_virt_timer);

/*
 * delete a virtual timer
 *
 * returns whether the deleted timer was pending (1) or not (0)
 */
int del_virt_timer(struct vtimer_list *timer)
{
	unsigned long flags;
	struct vtimer_queue *vq;

	/* check if timer is pending */
	if (!vtimer_pending(timer))
		return 0;

	vq = &per_cpu(virt_cpu_timer, timer->cpu);
	spin_lock_irqsave(&vq->lock, flags);

	/* we don't interrupt a running timer, just let it expire! */
	list_del_init(&timer->entry);

	spin_unlock_irqrestore(&vq->lock, flags);
	return 1;
}
EXPORT_SYMBOL(del_virt_timer);

/*
 * Start the virtual CPU timer on the current CPU.
 */
void init_cpu_vtimer(void)
{
	struct thread_info *ti = current_thread_info();
	struct vtimer_queue *vq;

	S390_lowcore.user_timer = ti->user_timer;
	S390_lowcore.system_timer = ti->system_timer;

	/* kick the virtual timer */
	asm volatile ("STCK %0" : "=m" (S390_lowcore.last_update_clock));
	asm volatile ("STPT %0" : "=m" (S390_lowcore.last_update_timer));

	/* initialize per cpu vtimer structure */
	vq = &__get_cpu_var(virt_cpu_timer);
	INIT_LIST_HEAD(&vq->list);
	spin_lock_init(&vq->lock);

	/* enable cpu timer interrupts */
	__ctl_set_bit(0,10);
}

void __init vtime_init(void)
{
	/* request the cpu timer external interrupt */
	if (register_early_external_interrupt(0x1005, do_cpu_timer_interrupt,
					      &ext_int_info_timer) != 0)
		panic("Couldn't request external interrupt 0x1005");

	/* Enable cpu timer interrupts on the boot cpu. */
	init_cpu_vtimer();
}


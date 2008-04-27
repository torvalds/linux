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

static ext_int_info_t ext_int_info_timer;
static DEFINE_PER_CPU(struct vtimer_queue, virt_cpu_timer);

#ifdef CONFIG_VIRT_CPU_ACCOUNTING
/*
 * Update process times based on virtual cpu times stored by entry.S
 * to the lowcore fields user_timer, system_timer & steal_clock.
 */
void account_process_tick(struct task_struct *tsk, int user_tick)
{
	cputime_t cputime;
	__u64 timer, clock;
	int rcu_user_flag;

	timer = S390_lowcore.last_update_timer;
	clock = S390_lowcore.last_update_clock;
	asm volatile ("  STPT %0\n"    /* Store current cpu timer value */
		      "  STCK %1"      /* Store current tod clock value */
		      : "=m" (S390_lowcore.last_update_timer),
		        "=m" (S390_lowcore.last_update_clock) );
	S390_lowcore.system_timer += timer - S390_lowcore.last_update_timer;
	S390_lowcore.steal_clock += S390_lowcore.last_update_clock - clock;

	cputime = S390_lowcore.user_timer >> 12;
	rcu_user_flag = cputime != 0;
	S390_lowcore.user_timer -= cputime << 12;
	S390_lowcore.steal_clock -= cputime << 12;
	account_user_time(tsk, cputime);

	cputime =  S390_lowcore.system_timer >> 12;
	S390_lowcore.system_timer -= cputime << 12;
	S390_lowcore.steal_clock -= cputime << 12;
	account_system_time(tsk, HARDIRQ_OFFSET, cputime);

	cputime = S390_lowcore.steal_clock;
	if ((__s64) cputime > 0) {
		cputime >>= 12;
		S390_lowcore.steal_clock -= cputime << 12;
		account_steal_time(tsk, cputime);
	}
}

/*
 * Update process times based on virtual cpu times stored by entry.S
 * to the lowcore fields user_timer, system_timer & steal_clock.
 */
void account_vtime(struct task_struct *tsk)
{
	cputime_t cputime;
	__u64 timer;

	timer = S390_lowcore.last_update_timer;
	asm volatile ("  STPT %0"    /* Store current cpu timer value */
		      : "=m" (S390_lowcore.last_update_timer) );
	S390_lowcore.system_timer += timer - S390_lowcore.last_update_timer;

	cputime = S390_lowcore.user_timer >> 12;
	S390_lowcore.user_timer -= cputime << 12;
	S390_lowcore.steal_clock -= cputime << 12;
	account_user_time(tsk, cputime);

	cputime =  S390_lowcore.system_timer >> 12;
	S390_lowcore.system_timer -= cputime << 12;
	S390_lowcore.steal_clock -= cputime << 12;
	account_system_time(tsk, 0, cputime);
}

/*
 * Update process times based on virtual cpu times stored by entry.S
 * to the lowcore fields user_timer, system_timer & steal_clock.
 */
void account_system_vtime(struct task_struct *tsk)
{
	cputime_t cputime;
	__u64 timer;

	timer = S390_lowcore.last_update_timer;
	asm volatile ("  STPT %0"    /* Store current cpu timer value */
		      : "=m" (S390_lowcore.last_update_timer) );
	S390_lowcore.system_timer += timer - S390_lowcore.last_update_timer;

	cputime =  S390_lowcore.system_timer >> 12;
	S390_lowcore.system_timer -= cputime << 12;
	S390_lowcore.steal_clock -= cputime << 12;
	account_system_time(tsk, 0, cputime);
}
EXPORT_SYMBOL_GPL(account_system_vtime);

static inline void set_vtimer(__u64 expires)
{
	__u64 timer;

	asm volatile ("  STPT %0\n"  /* Store current cpu timer value */
		      "  SPT %1"     /* Set new value immediatly afterwards */
		      : "=m" (timer) : "m" (expires) );
	S390_lowcore.system_timer += S390_lowcore.last_update_timer - timer;
	S390_lowcore.last_update_timer = expires;

	/* store expire time for this CPU timer */
	__get_cpu_var(virt_cpu_timer).to_expire = expires;
}
#else
static inline void set_vtimer(__u64 expires)
{
	S390_lowcore.last_update_timer = expires;
	asm volatile ("SPT %0" : : "m" (S390_lowcore.last_update_timer));

	/* store expire time for this CPU timer */
	__get_cpu_var(virt_cpu_timer).to_expire = expires;
}
#endif

static void start_cpu_timer(void)
{
	struct vtimer_queue *vt_list;

	vt_list = &__get_cpu_var(virt_cpu_timer);

	/* CPU timer interrupt is pending, don't reprogramm it */
	if (vt_list->idle & 1LL<<63)
		return;

	if (!list_empty(&vt_list->list))
		set_vtimer(vt_list->idle);
}

static void stop_cpu_timer(void)
{
	struct vtimer_queue *vt_list;

	vt_list = &__get_cpu_var(virt_cpu_timer);

	/* nothing to do */
	if (list_empty(&vt_list->list)) {
		vt_list->idle = VTIMER_MAX_SLICE;
		goto fire;
	}

	/* store the actual expire value */
	asm volatile ("STPT %0" : "=m" (vt_list->idle));

	/*
	 * If the CPU timer is negative we don't reprogramm
	 * it because we will get instantly an interrupt.
	 */
	if (vt_list->idle & 1LL<<63)
		return;

	vt_list->offset += vt_list->to_expire - vt_list->idle;

	/*
	 * We cannot halt the CPU timer, we just write a value that
	 * nearly never expires (only after 71 years) and re-write
	 * the stored expire value if we continue the timer
	 */
 fire:
	set_vtimer(VTIMER_MAX_SLICE);
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
	struct vtimer_queue *vt_list;
	struct vtimer_list *event, *tmp;
	void (*fn)(unsigned long);
	unsigned long data;

	if (list_empty(cb_list))
		return;

	vt_list = &__get_cpu_var(virt_cpu_timer);

	list_for_each_entry_safe(event, tmp, cb_list, entry) {
		fn = event->function;
		data = event->data;
		fn(data);

		if (!event->interval)
			/* delete one shot timer */
			list_del_init(&event->entry);
		else {
			/* move interval timer back to list */
			spin_lock(&vt_list->lock);
			list_del_init(&event->entry);
			list_add_sorted(event, &vt_list->list);
			spin_unlock(&vt_list->lock);
		}
	}
}

/*
 * Handler for the virtual CPU timer.
 */
static void do_cpu_timer_interrupt(__u16 error_code)
{
	__u64 next, delta;
	struct vtimer_queue *vt_list;
	struct vtimer_list *event, *tmp;
	struct list_head *ptr;
	/* the callback queue */
	struct list_head cb_list;

	INIT_LIST_HEAD(&cb_list);
	vt_list = &__get_cpu_var(virt_cpu_timer);

	/* walk timer list, fire all expired events */
	spin_lock(&vt_list->lock);

	if (vt_list->to_expire < VTIMER_MAX_SLICE)
		vt_list->offset += vt_list->to_expire;

	list_for_each_entry_safe(event, tmp, &vt_list->list, entry) {
		if (event->expires > vt_list->offset)
			/* found first unexpired event, leave */
			break;

		/* re-charge interval timer, we have to add the offset */
		if (event->interval)
			event->expires = event->interval + vt_list->offset;

		/* move expired timer to the callback queue */
		list_move_tail(&event->entry, &cb_list);
	}
	spin_unlock(&vt_list->lock);
	do_callbacks(&cb_list);

	/* next event is first in list */
	spin_lock(&vt_list->lock);
	if (!list_empty(&vt_list->list)) {
		ptr = vt_list->list.next;
		event = list_entry(ptr, struct vtimer_list, entry);
		next = event->expires - vt_list->offset;

		/* add the expired time from this interrupt handler
		 * and the callback functions
		 */
		asm volatile ("STPT %0" : "=m" (delta));
		delta = 0xffffffffffffffffLL - delta + 1;
		vt_list->offset += delta;
		next -= delta;
	} else {
		vt_list->offset = 0;
		next = VTIMER_MAX_SLICE;
	}
	spin_unlock(&vt_list->lock);
	set_vtimer(next);
}

void init_virt_timer(struct vtimer_list *timer)
{
	timer->function = NULL;
	INIT_LIST_HEAD(&timer->entry);
	spin_lock_init(&timer->lock);
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
	unsigned long flags;
	__u64 done;
	struct vtimer_list *event;
	struct vtimer_queue *vt_list;

	vt_list = &per_cpu(virt_cpu_timer, timer->cpu);
	spin_lock_irqsave(&vt_list->lock, flags);

	if (timer->cpu != smp_processor_id())
		printk("internal_add_vtimer: BUG, running on wrong CPU");

	/* if list is empty we only have to set the timer */
	if (list_empty(&vt_list->list)) {
		/* reset the offset, this may happen if the last timer was
		 * just deleted by mod_virt_timer and the interrupt
		 * didn't happen until here
		 */
		vt_list->offset = 0;
		goto fire;
	}

	/* save progress */
	asm volatile ("STPT %0" : "=m" (done));

	/* calculate completed work */
	done = vt_list->to_expire - done + vt_list->offset;
	vt_list->offset = 0;

	list_for_each_entry(event, &vt_list->list, entry)
		event->expires -= done;

 fire:
	list_add_sorted(timer, &vt_list->list);

	/* get first element, which is the next vtimer slice */
	event = list_entry(vt_list->list.next, struct vtimer_list, entry);

	set_vtimer(event->expires);
	spin_unlock_irqrestore(&vt_list->lock, flags);
	/* release CPU acquired in prepare_vtimer or mod_virt_timer() */
	put_cpu();
}

static inline int prepare_vtimer(struct vtimer_list *timer)
{
	if (!timer->function) {
		printk("add_virt_timer: uninitialized timer\n");
		return -EINVAL;
	}

	if (!timer->expires || timer->expires > VTIMER_MAX_SLICE) {
		printk("add_virt_timer: invalid timer expire value!\n");
		return -EINVAL;
	}

	if (vtimer_pending(timer)) {
		printk("add_virt_timer: timer pending\n");
		return -EBUSY;
	}

	timer->cpu = get_cpu();
	return 0;
}

/*
 * add_virt_timer - add an oneshot virtual CPU timer
 */
void add_virt_timer(void *new)
{
	struct vtimer_list *timer;

	timer = (struct vtimer_list *)new;

	if (prepare_vtimer(timer) < 0)
		return;

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

	if (prepare_vtimer(timer) < 0)
		return;

	timer->interval = timer->expires;
	internal_add_vtimer(timer);
}
EXPORT_SYMBOL(add_virt_timer_periodic);

/*
 * If we change a pending timer the function must be called on the CPU
 * where the timer is running on, e.g. by smp_call_function_single()
 *
 * The original mod_timer adds the timer if it is not pending. For compatibility
 * we do the same. The timer will be added on the current CPU as a oneshot timer.
 *
 * returns whether it has modified a pending timer (1) or not (0)
 */
int mod_virt_timer(struct vtimer_list *timer, __u64 expires)
{
	struct vtimer_queue *vt_list;
	unsigned long flags;
	int cpu;

	if (!timer->function) {
		printk("mod_virt_timer: uninitialized timer\n");
		return	-EINVAL;
	}

	if (!expires || expires > VTIMER_MAX_SLICE) {
		printk("mod_virt_timer: invalid expire range\n");
		return -EINVAL;
	}

	/*
	 * This is a common optimization triggered by the
	 * networking code - if the timer is re-modified
	 * to be the same thing then just return:
	 */
	if (timer->expires == expires && vtimer_pending(timer))
		return 1;

	cpu = get_cpu();
	vt_list = &per_cpu(virt_cpu_timer, cpu);

	/* disable interrupts before test if timer is pending */
	spin_lock_irqsave(&vt_list->lock, flags);

	/* if timer isn't pending add it on the current CPU */
	if (!vtimer_pending(timer)) {
		spin_unlock_irqrestore(&vt_list->lock, flags);
		/* we do not activate an interval timer with mod_virt_timer */
		timer->interval = 0;
		timer->expires = expires;
		timer->cpu = cpu;
		internal_add_vtimer(timer);
		return 0;
	}

	/* check if we run on the right CPU */
	if (timer->cpu != cpu) {
		printk("mod_virt_timer: running on wrong CPU, check your code\n");
		spin_unlock_irqrestore(&vt_list->lock, flags);
		put_cpu();
		return -EINVAL;
	}

	list_del_init(&timer->entry);
	timer->expires = expires;

	/* also change the interval if we have an interval timer */
	if (timer->interval)
		timer->interval = expires;

	/* the timer can't expire anymore so we can release the lock */
	spin_unlock_irqrestore(&vt_list->lock, flags);
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
	struct vtimer_queue *vt_list;

	/* check if timer is pending */
	if (!vtimer_pending(timer))
		return 0;

	vt_list = &per_cpu(virt_cpu_timer, timer->cpu);
	spin_lock_irqsave(&vt_list->lock, flags);

	/* we don't interrupt a running timer, just let it expire! */
	list_del_init(&timer->entry);

	/* last timer removed */
	if (list_empty(&vt_list->list)) {
		vt_list->to_expire = 0;
		vt_list->offset = 0;
	}

	spin_unlock_irqrestore(&vt_list->lock, flags);
	return 1;
}
EXPORT_SYMBOL(del_virt_timer);

/*
 * Start the virtual CPU timer on the current CPU.
 */
void init_cpu_vtimer(void)
{
	struct vtimer_queue *vt_list;

	/* kick the virtual timer */
	S390_lowcore.exit_timer = VTIMER_MAX_SLICE;
	S390_lowcore.last_update_timer = VTIMER_MAX_SLICE;
	asm volatile ("SPT %0" : : "m" (S390_lowcore.last_update_timer));
	asm volatile ("STCK %0" : "=m" (S390_lowcore.last_update_clock));

	/* enable cpu timer interrupts */
	__ctl_set_bit(0,10);

	vt_list = &__get_cpu_var(virt_cpu_timer);
	INIT_LIST_HEAD(&vt_list->list);
	spin_lock_init(&vt_list->lock);
	vt_list->to_expire = 0;
	vt_list->offset = 0;
	vt_list->idle = 0;

}

static int vtimer_idle_notify(struct notifier_block *self,
			      unsigned long action, void *hcpu)
{
	switch (action) {
	case S390_CPU_IDLE:
		stop_cpu_timer();
		break;
	case S390_CPU_NOT_IDLE:
		start_cpu_timer();
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block vtimer_idle_nb = {
	.notifier_call = vtimer_idle_notify,
};

void __init vtime_init(void)
{
	/* request the cpu timer external interrupt */
	if (register_early_external_interrupt(0x1005, do_cpu_timer_interrupt,
					      &ext_int_info_timer) != 0)
		panic("Couldn't request external interrupt 0x1005");

	if (register_idle_notifier(&vtimer_idle_nb))
		panic("Couldn't register idle notifier");

	/* Enable cpu timer interrupts on the boot cpu. */
	init_cpu_vtimer();
}


// SPDX-License-Identifier: GPL-2.0
/*
 *    Virtual CPU time accounting
 *
 *    Copyright IBM Corp. 2004, 2012
 *    Author(s): Jan Glauber <jan.glauber@de.ibm.com>
 */

#include <linux/kernel_stat.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/timex.h>
#include <linux/types.h>
#include <linux/time.h>
#include <asm/alternative.h>
#include <asm/cputime.h>
#include <asm/vtime.h>
#include <asm/cpu_mf.h>
#include <asm/idle.h>
#include <asm/smp.h>

#include "entry.h"

#define CPU_TIMER_MAX	0x7fffffffffffffffUL

DEFINE_PER_CPU(u64, mt_cycles[8]);
static DEFINE_PER_CPU(u64, mt_scaling_mult) = { 1 };
static DEFINE_PER_CPU(u64, mt_scaling_div) = { 1 };
static DEFINE_PER_CPU(unsigned long, mt_scaling_jiffies);

static inline void cpu_timer_init(u64 value)
{
	struct lowcore *lc = get_lowcore();
	u64 timer;

	asm volatile(
		"	stpt	%0\n"	/* Store current cpu timer value */
		"	spt	%1"	/* Set new value imm. afterwards */
		: "=Q" (timer) : "Q" (value));
	lc->system_timer += lc->last_update_timer - timer;
	lc->last_update_timer = value;
}

static void update_mt_scaling(void)
{
	u64 cycles_new[8], *cycles_old;
	u64 delta, fac, mult, div;
	int i;

	stcctm(MT_DIAG, smp_cpu_mtid + 1, cycles_new);
	cycles_old = this_cpu_ptr(mt_cycles);
	fac = 1;
	mult = div = 0;
	for (i = 0; i <= smp_cpu_mtid; i++) {
		delta = cycles_new[i] - cycles_old[i];
		div += delta;
		mult *= i + 1;
		mult += delta * fac;
		fac *= i + 1;
	}
	div *= fac;
	if (div > 0) {
		/* Update scaling factor */
		__this_cpu_write(mt_scaling_mult, mult);
		__this_cpu_write(mt_scaling_div, div);
		memcpy(cycles_old, cycles_new,
		       sizeof(u64) * (smp_cpu_mtid + 1));
	}
	__this_cpu_write(mt_scaling_jiffies, jiffies);
}

static inline u64 update_tsk_timer(unsigned long *tsk_vtime, u64 new)
{
	u64 delta;

	delta = new - *tsk_vtime;
	*tsk_vtime = new;
	return delta;
}


static inline u64 scale_vtime(u64 vtime)
{
	u64 mult = __this_cpu_read(mt_scaling_mult);
	u64 div = __this_cpu_read(mt_scaling_div);

	if (smp_cpu_mtid)
		return vtime * mult / div;
	return vtime;
}

static void account_system_index_scaled(struct task_struct *p, u64 cputime,
					enum cpu_usage_stat index)
{
	p->stimescaled += cputime_to_nsecs(scale_vtime(cputime));
	account_system_index_time(p, cputime_to_nsecs(cputime), index);
}

static inline void vtime_reset_last_update(struct lowcore *lc)
{
	asm volatile(
		"	stpt	%0\n"	/* Store current cpu timer value */
		"	stckf	%1"	/* Store current tod clock value */
		: "=Q" (lc->last_update_timer),
		  "=Q" (lc->last_update_clock)
		: : "cc");
}

/*
 * Update process times based on virtual cpu times stored by entry.S
 * to the lowcore fields user_timer, system_timer & steal_clock.
 */
static void do_account_vtime(struct task_struct *tsk)
{
	u64 timer, clock, user, guest, system, hardirq, softirq;
	struct lowcore *lc = get_lowcore();

	timer = lc->last_update_timer;
	clock = lc->last_update_clock;

	vtime_reset_last_update(lc);

	clock = lc->last_update_clock - clock;
	timer -= lc->last_update_timer;

	if (hardirq_count())
		lc->hardirq_timer += timer;
	else
		lc->system_timer += timer;

	/* Update MT utilization calculation */
	if (smp_cpu_mtid && time_after(jiffies, __this_cpu_read(mt_scaling_jiffies)))
		update_mt_scaling();

	/* Calculate cputime delta */
	user = update_tsk_timer(&tsk->thread.user_timer, lc->user_timer);
	guest = update_tsk_timer(&tsk->thread.guest_timer, lc->guest_timer);
	system = update_tsk_timer(&tsk->thread.system_timer, lc->system_timer);
	hardirq = update_tsk_timer(&tsk->thread.hardirq_timer, lc->hardirq_timer);
	softirq = update_tsk_timer(&tsk->thread.softirq_timer, lc->softirq_timer);
	lc->steal_timer += clock - user - guest - system - hardirq - softirq;

	/* Push account value */
	if (user) {
		account_user_time(tsk, cputime_to_nsecs(user));
		tsk->utimescaled += cputime_to_nsecs(scale_vtime(user));
	}

	if (guest) {
		account_guest_time(tsk, cputime_to_nsecs(guest));
		tsk->utimescaled += cputime_to_nsecs(scale_vtime(guest));
	}

	if (system)
		account_system_index_scaled(tsk, system, CPUTIME_SYSTEM);
	if (hardirq)
		account_system_index_scaled(tsk, hardirq, CPUTIME_IRQ);
	if (softirq)
		account_system_index_scaled(tsk, softirq, CPUTIME_SOFTIRQ);
}

void vtime_task_switch(struct task_struct *prev)
{
	struct lowcore *lc = get_lowcore();

	do_account_vtime(prev);
	prev->thread.user_timer = lc->user_timer;
	prev->thread.guest_timer = lc->guest_timer;
	prev->thread.system_timer = lc->system_timer;
	prev->thread.hardirq_timer = lc->hardirq_timer;
	prev->thread.softirq_timer = lc->softirq_timer;
	lc->user_timer = current->thread.user_timer;
	lc->guest_timer = current->thread.guest_timer;
	lc->system_timer = current->thread.system_timer;
	lc->hardirq_timer = current->thread.hardirq_timer;
	lc->softirq_timer = current->thread.softirq_timer;
}

/*
 * In s390, accounting pending user time also implies
 * accounting system time in order to correctly compute
 * the stolen time accounting.
 */
void vtime_flush(struct task_struct *tsk)
{
	struct lowcore *lc = get_lowcore();
	u64 steal, avg_steal;

	do_account_vtime(tsk);

	steal = lc->steal_timer;
	avg_steal = lc->avg_steal_timer;
	if ((s64) steal > 0) {
		lc->steal_timer = 0;
		account_steal_time(cputime_to_nsecs(steal));
		avg_steal += steal;
	}
	lc->avg_steal_timer = avg_steal / 2;
}

static u64 vtime_delta(void)
{
	struct lowcore *lc = get_lowcore();
	u64 timer = lc->last_update_timer;

	lc->last_update_timer = get_cpu_timer();
	return timer - lc->last_update_timer;
}

void vtime_account_kernel(struct task_struct *tsk)
{
	struct lowcore *lc = get_lowcore();
	u64 delta = vtime_delta();

	if (tsk->flags & PF_VCPU)
		lc->guest_timer += delta;
	else
		lc->system_timer += delta;
}
EXPORT_SYMBOL_GPL(vtime_account_kernel);

void vtime_account_softirq(struct task_struct *tsk)
{
	get_lowcore()->softirq_timer += vtime_delta();
}

void vtime_account_hardirq(struct task_struct *tsk)
{
	get_lowcore()->hardirq_timer += vtime_delta();
}

void vtime_init(void)
{
	cpu_timer_init(CPU_TIMER_MAX);
	/* Setup initial MT scaling values */
	if (smp_cpu_mtid) {
		__this_cpu_write(mt_scaling_jiffies, jiffies);
		__this_cpu_write(mt_scaling_mult, 1);
		__this_cpu_write(mt_scaling_div, 1);
		stcctm(MT_DIAG, smp_cpu_mtid + 1, this_cpu_ptr(mt_cycles));
	}
}

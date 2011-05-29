/******************************************************************************
 * arch/ia64/xen/time.c
 *
 * Copyright (c) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/delay.h>
#include <linux/kernel_stat.h>
#include <linux/posix-timers.h>
#include <linux/irq.h>
#include <linux/clocksource.h>

#include <asm/timex.h>

#include <asm/xen/hypervisor.h>

#include <xen/interface/vcpu.h>

#include "../kernel/fsyscall_gtod_data.h"

static DEFINE_PER_CPU(struct vcpu_runstate_info, xen_runstate);
static DEFINE_PER_CPU(unsigned long, xen_stolen_time);
static DEFINE_PER_CPU(unsigned long, xen_blocked_time);

/* taken from i386/kernel/time-xen.c */
static void xen_init_missing_ticks_accounting(int cpu)
{
	struct vcpu_register_runstate_memory_area area;
	struct vcpu_runstate_info *runstate = &per_cpu(xen_runstate, cpu);
	int rc;

	memset(runstate, 0, sizeof(*runstate));

	area.addr.v = runstate;
	rc = HYPERVISOR_vcpu_op(VCPUOP_register_runstate_memory_area, cpu,
				&area);
	WARN_ON(rc && rc != -ENOSYS);

	per_cpu(xen_blocked_time, cpu) = runstate->time[RUNSTATE_blocked];
	per_cpu(xen_stolen_time, cpu) = runstate->time[RUNSTATE_runnable]
					    + runstate->time[RUNSTATE_offline];
}

/*
 * Runstate accounting
 */
/* stolen from arch/x86/xen/time.c */
static void get_runstate_snapshot(struct vcpu_runstate_info *res)
{
	u64 state_time;
	struct vcpu_runstate_info *state;

	BUG_ON(preemptible());

	state = &__get_cpu_var(xen_runstate);

	/*
	 * The runstate info is always updated by the hypervisor on
	 * the current CPU, so there's no need to use anything
	 * stronger than a compiler barrier when fetching it.
	 */
	do {
		state_time = state->state_entry_time;
		rmb();
		*res = *state;
		rmb();
	} while (state->state_entry_time != state_time);
}

#define NS_PER_TICK (1000000000LL/HZ)

static unsigned long
consider_steal_time(unsigned long new_itm)
{
	unsigned long stolen, blocked;
	unsigned long delta_itm = 0, stolentick = 0;
	int cpu = smp_processor_id();
	struct vcpu_runstate_info runstate;
	struct task_struct *p = current;

	get_runstate_snapshot(&runstate);

	/*
	 * Check for vcpu migration effect
	 * In this case, itc value is reversed.
	 * This causes huge stolen value.
	 * This function just checks and reject this effect.
	 */
	if (!time_after_eq(runstate.time[RUNSTATE_blocked],
			   per_cpu(xen_blocked_time, cpu)))
		blocked = 0;

	if (!time_after_eq(runstate.time[RUNSTATE_runnable] +
			   runstate.time[RUNSTATE_offline],
			   per_cpu(xen_stolen_time, cpu)))
		stolen = 0;

	if (!time_after(delta_itm + new_itm, ia64_get_itc()))
		stolentick = ia64_get_itc() - new_itm;

	do_div(stolentick, NS_PER_TICK);
	stolentick++;

	do_div(stolen, NS_PER_TICK);

	if (stolen > stolentick)
		stolen = stolentick;

	stolentick -= stolen;
	do_div(blocked, NS_PER_TICK);

	if (blocked > stolentick)
		blocked = stolentick;

	if (stolen > 0 || blocked > 0) {
		account_steal_ticks(stolen);
		account_idle_ticks(blocked);
		run_local_timers();

		rcu_check_callbacks(cpu, user_mode(get_irq_regs()));

		scheduler_tick();
		run_posix_cpu_timers(p);
		delta_itm += local_cpu_data->itm_delta * (stolen + blocked);

		if (cpu == time_keeper_id)
			xtime_update(stolen + blocked);

		local_cpu_data->itm_next = delta_itm + new_itm;

		per_cpu(xen_stolen_time, cpu) += NS_PER_TICK * stolen;
		per_cpu(xen_blocked_time, cpu) += NS_PER_TICK * blocked;
	}
	return delta_itm;
}

static int xen_do_steal_accounting(unsigned long *new_itm)
{
	unsigned long delta_itm;
	delta_itm = consider_steal_time(*new_itm);
	*new_itm += delta_itm;
	if (time_after(*new_itm, ia64_get_itc()) && delta_itm)
		return 1;

	return 0;
}

static void xen_itc_jitter_data_reset(void)
{
	u64 lcycle, ret;

	do {
		lcycle = itc_jitter_data.itc_lastcycle;
		ret = cmpxchg(&itc_jitter_data.itc_lastcycle, lcycle, 0);
	} while (unlikely(ret != lcycle));
}

/* based on xen_sched_clock() in arch/x86/xen/time.c. */
/*
 * This relies on HAVE_UNSTABLE_SCHED_CLOCK. If it can't be defined,
 * something similar logic should be implemented here.
 */
/*
 * Xen sched_clock implementation.  Returns the number of unstolen
 * nanoseconds, which is nanoseconds the VCPU spent in RUNNING+BLOCKED
 * states.
 */
static unsigned long long xen_sched_clock(void)
{
	struct vcpu_runstate_info runstate;

	unsigned long long now;
	unsigned long long offset;
	unsigned long long ret;

	/*
	 * Ideally sched_clock should be called on a per-cpu basis
	 * anyway, so preempt should already be disabled, but that's
	 * not current practice at the moment.
	 */
	preempt_disable();

	/*
	 * both ia64_native_sched_clock() and xen's runstate are
	 * based on mAR.ITC. So difference of them makes sense.
	 */
	now = ia64_native_sched_clock();

	get_runstate_snapshot(&runstate);

	WARN_ON(runstate.state != RUNSTATE_running);

	offset = 0;
	if (now > runstate.state_entry_time)
		offset = now - runstate.state_entry_time;
	ret = runstate.time[RUNSTATE_blocked] +
		runstate.time[RUNSTATE_running] +
		offset;

	preempt_enable();

	return ret;
}

struct pv_time_ops xen_time_ops __initdata = {
	.init_missing_ticks_accounting	= xen_init_missing_ticks_accounting,
	.do_steal_accounting		= xen_do_steal_accounting,
	.clocksource_resume		= xen_itc_jitter_data_reset,
	.sched_clock			= xen_sched_clock,
};

/* Called after suspend, to resume time.  */
static void xen_local_tick_resume(void)
{
	/* Just trigger a tick.  */
	ia64_cpu_local_tick();
	touch_softlockup_watchdog();
}

void
xen_timer_resume(void)
{
	unsigned int cpu;

	xen_local_tick_resume();

	for_each_online_cpu(cpu)
		xen_init_missing_ticks_accounting(cpu);
}

static void ia64_cpu_local_tick_fn(void *unused)
{
	xen_local_tick_resume();
	xen_init_missing_ticks_accounting(smp_processor_id());
}

void
xen_timer_resume_on_aps(void)
{
	smp_call_function(&ia64_cpu_local_tick_fn, NULL, 1);
}

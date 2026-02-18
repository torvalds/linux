/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _S390_VTIME_H
#define _S390_VTIME_H

#include <asm/lowcore.h>
#include <asm/cpu_mf.h>
#include <asm/idle.h>

DECLARE_PER_CPU(u64, mt_cycles[8]);

static inline void update_timer_sys(void)
{
	struct lowcore *lc = get_lowcore();

	lc->system_timer += lc->last_update_timer - lc->exit_timer;
	lc->user_timer += lc->exit_timer - lc->sys_enter_timer;
	lc->last_update_timer = lc->sys_enter_timer;
}

static inline void update_timer_mcck(void)
{
	struct lowcore *lc = get_lowcore();

	lc->system_timer += lc->last_update_timer - lc->exit_timer;
	lc->user_timer += lc->exit_timer - lc->mcck_enter_timer;
	lc->last_update_timer = lc->mcck_enter_timer;
}

static inline void update_timer_idle(void)
{
	struct s390_idle_data *idle = this_cpu_ptr(&s390_idle);
	struct lowcore *lc = get_lowcore();
	u64 cycles_new[8];
	int i, mtid;

	mtid = smp_cpu_mtid;
	if (mtid) {
		stcctm(MT_DIAG, mtid, cycles_new);
		for (i = 0; i < mtid; i++)
			__this_cpu_add(mt_cycles[i], cycles_new[i] - idle->mt_cycles_enter[i]);
	}
	/*
	 * This is a bit subtle: Forward last_update_clock so it excludes idle
	 * time. For correct steal time calculation in do_account_vtime() add
	 * passed wall time before idle_enter to steal_timer:
	 * During the passed wall time before idle_enter CPU time may have
	 * been accounted to system, hardirq, softirq, etc. lowcore fields.
	 * The accounted CPU times will be subtracted again from steal_timer
	 * when accumulated steal time is calculated in do_account_vtime().
	 */
	lc->steal_timer += idle->clock_idle_enter - lc->last_update_clock;
	lc->last_update_clock = lc->int_clock;
	lc->system_timer += lc->last_update_timer - idle->timer_idle_enter;
	lc->last_update_timer = lc->sys_enter_timer;
}

#endif /* _S390_VTIME_H */

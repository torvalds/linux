/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_TIMER_H
#define _ASM_X86_TIMER_H
#include <linux/pm.h>
#include <linux/percpu.h>
#include <linux/interrupt.h>
#include <linux/math64.h>

#define TICK_SIZE (tick_nsec / 1000)

unsigned long long native_sched_clock(void);
extern void recalibrate_cpu_khz(void);

extern int no_timer_check;

extern bool using_native_sched_clock(void);

/*
 * We use the full linear equation: f(x) = a + b*x, in order to allow
 * a continuous function in the face of dynamic freq changes.
 *
 * Continuity means that when our frequency changes our slope (b); we want to
 * ensure that: f(t) == f'(t), which gives: a + b*t == a' + b'*t.
 *
 * Without an offset (a) the above would not be possible.
 *
 * See the comment near cycles_2_ns() for details on how we compute (b).
 */
struct cyc2ns_data {
	u32 cyc2ns_mul;
	u32 cyc2ns_shift;
	u64 cyc2ns_offset;
}; /* 16 bytes */

extern void cyc2ns_read_begin(struct cyc2ns_data *);
extern void cyc2ns_read_end(void);

#endif /* _ASM_X86_TIMER_H */

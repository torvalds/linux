#ifndef _ASM_X86_TIMER_H
#define _ASM_X86_TIMER_H
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/percpu.h>
#include <linux/interrupt.h>
#include <linux/math64.h>

#define TICK_SIZE (tick_nsec / 1000)

unsigned long long native_sched_clock(void);
extern int recalibrate_cpu_khz(void);

extern int no_timer_check;

DECLARE_PER_CPU(unsigned long, cyc2ns);
DECLARE_PER_CPU(unsigned long long, cyc2ns_offset);

#endif /* _ASM_X86_TIMER_H */

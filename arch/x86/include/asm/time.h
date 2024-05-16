/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_TIME_H
#define _ASM_X86_TIME_H

#include <linux/clocksource.h>
#include <asm/mc146818rtc.h>

extern void hpet_time_init(void);
extern bool pit_timer_init(void);
extern bool tsc_clocksource_watchdog_disabled(void);

extern struct clock_event_device *global_clock_event;

#endif /* _ASM_X86_TIME_H */

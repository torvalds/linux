/* Internal definitions for the arch part of the core kernel
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

struct clocksource;
struct clock_event_device;

/*
 * kthread.S
 */
extern int kernel_thread_helper(int);

/*
 * entry.S
 */
extern void ret_from_fork(struct task_struct *) __attribute__((noreturn));

/*
 * smp-low.S
 */
#ifdef CONFIG_SMP
extern void mn10300_low_ipi_handler(void);
#endif

/*
 * time.c
 */
extern irqreturn_t local_timer_interrupt(void);

/*
 * time.c
 */
#ifdef CONFIG_CEVT_MN10300
extern void clockevent_set_clock(struct clock_event_device *, unsigned int);
#endif
#ifdef CONFIG_CSRC_MN10300
extern void clocksource_set_clock(struct clocksource *, unsigned int);
#endif

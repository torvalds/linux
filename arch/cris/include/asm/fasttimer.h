/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/include/asm-cris/fasttimer.h
 *
 * Fast timers for ETRAX100LX
 * Copyright (C) 2000-2007 Axis Communications AB
 */
#include <linux/time.h> /* struct timeval */
#include <linux/timex.h>

#ifdef CONFIG_ETRAX_FAST_TIMER

typedef void fast_timer_function_type(unsigned long);

struct fasttime_t {
	unsigned long tv_jiff;  /* jiffies */
	unsigned long tv_usec;  /* microseconds */
};

struct fast_timer{ /* Close to timer_list */
  struct fast_timer *next;
  struct fast_timer *prev;
	struct fasttime_t tv_set;
	struct fasttime_t tv_expires;
  unsigned long delay_us;
  fast_timer_function_type *function;
  unsigned long data;
  const char *name;
};

extern struct fast_timer *fast_timer_list;

void start_one_shot_timer(struct fast_timer *t,
                          fast_timer_function_type *function,
                          unsigned long data,
                          unsigned long delay_us,
                          const char *name);

int del_fast_timer(struct fast_timer * t);
/* return 1 if deleted */


void schedule_usleep(unsigned long us);


int fast_timer_init(void);

#endif

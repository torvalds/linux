/*
 * Definitions for measuring cputime on powerpc machines.
 *
 * Copyright (C) 2006 Paul Mackerras, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * If we have CONFIG_VIRT_CPU_ACCOUNTING_NATIVE, we measure cpu time in
 * the same units as the timebase.  Otherwise we measure cpu time
 * in jiffies using the generic definitions.
 */

#ifndef __POWERPC_CPUTIME_H
#define __POWERPC_CPUTIME_H

#ifndef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
#include <asm-generic/cputime.h>
#ifdef __KERNEL__
static inline void setup_cputime_one_jiffy(void) { }
#endif
#else

#include <linux/types.h>
#include <linux/time.h>
#include <asm/div64.h>
#include <asm/time.h>
#include <asm/param.h>

typedef u64 __nocast cputime_t;
typedef u64 __nocast cputime64_t;

#ifdef __KERNEL__

/*
 * One jiffy in timebase units computed during initialization
 */
extern cputime_t cputime_one_jiffy;

/*
 * Convert cputime <-> jiffies
 */
extern u64 __cputime_jiffies_factor;
DECLARE_PER_CPU(unsigned long, cputime_last_delta);
DECLARE_PER_CPU(unsigned long, cputime_scaled_last_delta);

static inline unsigned long cputime_to_jiffies(const cputime_t ct)
{
	return mulhdu((__force u64) ct, __cputime_jiffies_factor);
}

/* Estimate the scaled cputime by scaling the real cputime based on
 * the last scaled to real ratio */
static inline cputime_t cputime_to_scaled(const cputime_t ct)
{
	if (cpu_has_feature(CPU_FTR_SPURR) &&
	    __get_cpu_var(cputime_last_delta))
		return (__force u64) ct *
			__get_cpu_var(cputime_scaled_last_delta) /
			__get_cpu_var(cputime_last_delta);
	return ct;
}

static inline cputime_t jiffies_to_cputime(const unsigned long jif)
{
	u64 ct;
	unsigned long sec;

	/* have to be a little careful about overflow */
	ct = jif % HZ;
	sec = jif / HZ;
	if (ct) {
		ct *= tb_ticks_per_sec;
		do_div(ct, HZ);
	}
	if (sec)
		ct += (cputime_t) sec * tb_ticks_per_sec;
	return (__force cputime_t) ct;
}

static inline void setup_cputime_one_jiffy(void)
{
	cputime_one_jiffy = jiffies_to_cputime(1);
}

static inline cputime64_t jiffies64_to_cputime64(const u64 jif)
{
	u64 ct;
	u64 sec;

	/* have to be a little careful about overflow */
	ct = jif % HZ;
	sec = jif / HZ;
	if (ct) {
		ct *= tb_ticks_per_sec;
		do_div(ct, HZ);
	}
	if (sec)
		ct += (u64) sec * tb_ticks_per_sec;
	return (__force cputime64_t) ct;
}

static inline u64 cputime64_to_jiffies64(const cputime_t ct)
{
	return mulhdu((__force u64) ct, __cputime_jiffies_factor);
}

/*
 * Convert cputime <-> microseconds
 */
extern u64 __cputime_usec_factor;

static inline unsigned long cputime_to_usecs(const cputime_t ct)
{
	return mulhdu((__force u64) ct, __cputime_usec_factor);
}

static inline cputime_t usecs_to_cputime(const unsigned long us)
{
	u64 ct;
	unsigned long sec;

	/* have to be a little careful about overflow */
	ct = us % 1000000;
	sec = us / 1000000;
	if (ct) {
		ct *= tb_ticks_per_sec;
		do_div(ct, 1000000);
	}
	if (sec)
		ct += (cputime_t) sec * tb_ticks_per_sec;
	return (__force cputime_t) ct;
}

#define usecs_to_cputime64(us)		usecs_to_cputime(us)

/*
 * Convert cputime <-> seconds
 */
extern u64 __cputime_sec_factor;

static inline unsigned long cputime_to_secs(const cputime_t ct)
{
	return mulhdu((__force u64) ct, __cputime_sec_factor);
}

static inline cputime_t secs_to_cputime(const unsigned long sec)
{
	return (__force cputime_t)((u64) sec * tb_ticks_per_sec);
}

/*
 * Convert cputime <-> timespec
 */
static inline void cputime_to_timespec(const cputime_t ct, struct timespec *p)
{
	u64 x = (__force u64) ct;
	unsigned int frac;

	frac = do_div(x, tb_ticks_per_sec);
	p->tv_sec = x;
	x = (u64) frac * 1000000000;
	do_div(x, tb_ticks_per_sec);
	p->tv_nsec = x;
}

static inline cputime_t timespec_to_cputime(const struct timespec *p)
{
	u64 ct;

	ct = (u64) p->tv_nsec * tb_ticks_per_sec;
	do_div(ct, 1000000000);
	return (__force cputime_t)(ct + (u64) p->tv_sec * tb_ticks_per_sec);
}

/*
 * Convert cputime <-> timeval
 */
static inline void cputime_to_timeval(const cputime_t ct, struct timeval *p)
{
	u64 x = (__force u64) ct;
	unsigned int frac;

	frac = do_div(x, tb_ticks_per_sec);
	p->tv_sec = x;
	x = (u64) frac * 1000000;
	do_div(x, tb_ticks_per_sec);
	p->tv_usec = x;
}

static inline cputime_t timeval_to_cputime(const struct timeval *p)
{
	u64 ct;

	ct = (u64) p->tv_usec * tb_ticks_per_sec;
	do_div(ct, 1000000);
	return (__force cputime_t)(ct + (u64) p->tv_sec * tb_ticks_per_sec);
}

/*
 * Convert cputime <-> clock_t (units of 1/USER_HZ seconds)
 */
extern u64 __cputime_clockt_factor;

static inline unsigned long cputime_to_clock_t(const cputime_t ct)
{
	return mulhdu((__force u64) ct, __cputime_clockt_factor);
}

static inline cputime_t clock_t_to_cputime(const unsigned long clk)
{
	u64 ct;
	unsigned long sec;

	/* have to be a little careful about overflow */
	ct = clk % USER_HZ;
	sec = clk / USER_HZ;
	if (ct) {
		ct *= tb_ticks_per_sec;
		do_div(ct, USER_HZ);
	}
	if (sec)
		ct += (u64) sec * tb_ticks_per_sec;
	return (__force cputime_t) ct;
}

#define cputime64_to_clock_t(ct)	cputime_to_clock_t((cputime_t)(ct))

static inline void arch_vtime_task_switch(struct task_struct *tsk) { }

#endif /* __KERNEL__ */
#endif /* CONFIG_VIRT_CPU_ACCOUNTING_NATIVE */
#endif /* __POWERPC_CPUTIME_H */

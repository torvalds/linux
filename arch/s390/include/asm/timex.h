/*
 *  S390 version
 *    Copyright IBM Corp. 1999
 *
 *  Derived from "include/asm-i386/timex.h"
 *    Copyright (C) 1992, Linus Torvalds
 */

#ifndef _ASM_S390_TIMEX_H
#define _ASM_S390_TIMEX_H

#include <asm/lowcore.h>

/* The value of the TOD clock for 1.1.1970. */
#define TOD_UNIX_EPOCH 0x7d91048bca000000ULL

/* Inline functions for clock register access. */
static inline int set_clock(__u64 time)
{
	int cc;

	asm volatile(
		"   sck   %1\n"
		"   ipm   %0\n"
		"   srl   %0,28\n"
		: "=d" (cc) : "Q" (time) : "cc");
	return cc;
}

static inline int store_clock(__u64 *time)
{
	int cc;

	asm volatile(
		"   stck  %1\n"
		"   ipm   %0\n"
		"   srl   %0,28\n"
		: "=d" (cc), "=Q" (*time) : : "cc");
	return cc;
}

static inline void set_clock_comparator(__u64 time)
{
	asm volatile("sckc %0" : : "Q" (time));
}

static inline void store_clock_comparator(__u64 *time)
{
	asm volatile("stckc %0" : "=Q" (*time));
}

void clock_comparator_work(void);

static inline unsigned long long local_tick_disable(void)
{
	unsigned long long old;

	old = S390_lowcore.clock_comparator;
	S390_lowcore.clock_comparator = -1ULL;
	set_clock_comparator(S390_lowcore.clock_comparator);
	return old;
}

static inline void local_tick_enable(unsigned long long comp)
{
	S390_lowcore.clock_comparator = comp;
	set_clock_comparator(S390_lowcore.clock_comparator);
}

#define CLOCK_TICK_RATE	1193180 /* Underlying HZ */

typedef unsigned long long cycles_t;

static inline unsigned long long get_clock(void)
{
	unsigned long long clk;

#ifdef CONFIG_HAVE_MARCH_Z9_109_FEATURES
	asm volatile(".insn s,0xb27c0000,%0" : "=Q" (clk) : : "cc");
#else
	asm volatile("stck %0" : "=Q" (clk) : : "cc");
#endif
	return clk;
}

static inline void get_clock_ext(char *clk)
{
	asm volatile("stcke %0" : "=Q" (*clk) : : "cc");
}

static inline unsigned long long get_clock_xt(void)
{
	unsigned char clk[16];
	get_clock_ext(clk);
	return *((unsigned long long *)&clk[1]);
}

static inline cycles_t get_cycles(void)
{
	return (cycles_t) get_clock() >> 2;
}

int get_sync_clock(unsigned long long *clock);
void init_cpu_timer(void);
unsigned long long monotonic_clock(void);

void tod_to_timeval(__u64, struct timespec *);

static inline
void stck_to_timespec(unsigned long long stck, struct timespec *ts)
{
	tod_to_timeval(stck - TOD_UNIX_EPOCH, ts);
}

extern u64 sched_clock_base_cc;

/**
 * get_clock_monotonic - returns current time in clock rate units
 *
 * The caller must ensure that preemption is disabled.
 * The clock and sched_clock_base get changed via stop_machine.
 * Therefore preemption must be disabled when calling this
 * function, otherwise the returned value is not guaranteed to
 * be monotonic.
 */
static inline unsigned long long get_clock_monotonic(void)
{
	return get_clock_xt() - sched_clock_base_cc;
}

/**
 * tod_to_ns - convert a TOD format value to nanoseconds
 * @todval: to be converted TOD format value
 * Returns: number of nanoseconds that correspond to the TOD format value
 *
 * Converting a 64 Bit TOD format value to nanoseconds means that the value
 * must be divided by 4.096. In order to achieve that we multiply with 125
 * and divide by 512:
 *
 *    ns = (todval * 125) >> 9;
 *
 * In order to avoid an overflow with the multiplication we can rewrite this.
 * With a split todval == 2^32 * th + tl (th upper 32 bits, tl lower 32 bits)
 * we end up with
 *
 *    ns = ((2^32 * th + tl) * 125 ) >> 9;
 * -> ns = (2^23 * th * 125) + ((tl * 125) >> 9);
 *
 */
static inline unsigned long long tod_to_ns(unsigned long long todval)
{
	unsigned long long ns;

	ns = ((todval >> 32) << 23) * 125;
	ns += ((todval & 0xffffffff) * 125) >> 9;
	return ns;
}

#endif

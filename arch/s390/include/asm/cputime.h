/*
 *  include/asm-s390/cputime.h
 *
 *  (C) Copyright IBM Corp. 2004
 *
 *  Author: Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef _S390_CPUTIME_H
#define _S390_CPUTIME_H

#include <linux/types.h>
#include <linux/percpu.h>
#include <linux/spinlock.h>
#include <asm/div64.h>

/* We want to use full resolution of the CPU timer: 2**-12 micro-seconds. */

typedef unsigned long long cputime_t;
typedef unsigned long long cputime64_t;

#ifndef __s390x__

static inline unsigned int
__div(unsigned long long n, unsigned int base)
{
	register_pair rp;

	rp.pair = n >> 1;
	asm ("dr %0,%1" : "+d" (rp) : "d" (base >> 1));
	return rp.subreg.odd;
}

#else /* __s390x__ */

static inline unsigned int
__div(unsigned long long n, unsigned int base)
{
	return n / base;
}

#endif /* __s390x__ */

#define cputime_zero			(0ULL)
#define cputime_one_jiffy		jiffies_to_cputime(1)
#define cputime_max			((~0UL >> 1) - 1)
#define cputime_add(__a, __b)		((__a) +  (__b))
#define cputime_sub(__a, __b)		((__a) -  (__b))
#define cputime_div(__a, __n) ({		\
	unsigned long long __div = (__a);	\
	do_div(__div,__n);			\
	__div;					\
})
#define cputime_halve(__a)		((__a) >> 1)
#define cputime_eq(__a, __b)		((__a) == (__b))
#define cputime_gt(__a, __b)		((__a) >  (__b))
#define cputime_ge(__a, __b)		((__a) >= (__b))
#define cputime_lt(__a, __b)		((__a) <  (__b))
#define cputime_le(__a, __b)		((__a) <= (__b))
#define cputime_to_jiffies(__ct)	(__div((__ct), 4096000000ULL / HZ))
#define cputime_to_scaled(__ct)		(__ct)
#define jiffies_to_cputime(__hz)	((cputime_t)(__hz) * (4096000000ULL / HZ))

#define cputime64_zero			(0ULL)
#define cputime64_add(__a, __b)		((__a) + (__b))
#define cputime_to_cputime64(__ct)	(__ct)

static inline u64
cputime64_to_jiffies64(cputime64_t cputime)
{
	do_div(cputime, 4096000000ULL / HZ);
	return cputime;
}

/*
 * Convert cputime to microseconds and back.
 */
static inline unsigned int
cputime_to_usecs(const cputime_t cputime)
{
	return cputime_div(cputime, 4096);
}

static inline cputime_t
usecs_to_cputime(const unsigned int m)
{
	return (cputime_t) m * 4096;
}

/*
 * Convert cputime to milliseconds and back.
 */
static inline unsigned int
cputime_to_secs(const cputime_t cputime)
{
	return __div(cputime, 2048000000) >> 1;
}

static inline cputime_t
secs_to_cputime(const unsigned int s)
{
	return (cputime_t) s * 4096000000ULL;
}

/*
 * Convert cputime to timespec and back.
 */
static inline cputime_t
timespec_to_cputime(const struct timespec *value)
{
	return value->tv_nsec * 4096 / 1000 + (u64) value->tv_sec * 4096000000ULL;
}

static inline void
cputime_to_timespec(const cputime_t cputime, struct timespec *value)
{
#ifndef __s390x__
	register_pair rp;

	rp.pair = cputime >> 1;
	asm ("dr %0,%1" : "+d" (rp) : "d" (2048000000UL));
	value->tv_nsec = rp.subreg.even * 1000 / 4096;
	value->tv_sec = rp.subreg.odd;
#else
	value->tv_nsec = (cputime % 4096000000ULL) * 1000 / 4096;
	value->tv_sec = cputime / 4096000000ULL;
#endif
}

/*
 * Convert cputime to timeval and back.
 * Since cputime and timeval have the same resolution (microseconds)
 * this is easy.
 */
static inline cputime_t
timeval_to_cputime(const struct timeval *value)
{
	return value->tv_usec * 4096 + (u64) value->tv_sec * 4096000000ULL;
}

static inline void
cputime_to_timeval(const cputime_t cputime, struct timeval *value)
{
#ifndef __s390x__
	register_pair rp;

	rp.pair = cputime >> 1;
	asm ("dr %0,%1" : "+d" (rp) : "d" (2048000000UL));
	value->tv_usec = rp.subreg.even / 4096;
	value->tv_sec = rp.subreg.odd;
#else
	value->tv_usec = (cputime % 4096000000ULL) / 4096;
	value->tv_sec = cputime / 4096000000ULL;
#endif
}

/*
 * Convert cputime to clock and back.
 */
static inline clock_t
cputime_to_clock_t(cputime_t cputime)
{
	return cputime_div(cputime, 4096000000ULL / USER_HZ);
}

static inline cputime_t
clock_t_to_cputime(unsigned long x)
{
	return (cputime_t) x * (4096000000ULL / USER_HZ);
}

/*
 * Convert cputime64 to clock.
 */
static inline clock_t
cputime64_to_clock_t(cputime64_t cputime)
{
       return cputime_div(cputime, 4096000000ULL / USER_HZ);
}

struct s390_idle_data {
	unsigned int sequence;
	unsigned long long idle_count;
	unsigned long long idle_enter;
	unsigned long long idle_time;
	int nohz_delay;
};

DECLARE_PER_CPU(struct s390_idle_data, s390_idle);

void vtime_start_cpu(__u64 int_clock, __u64 enter_timer);
cputime64_t s390_get_idle_time(int cpu);

#define arch_idle_time(cpu) s390_get_idle_time(cpu)

static inline void s390_idle_check(struct pt_regs *regs, __u64 int_clock,
				   __u64 enter_timer)
{
	if (regs->psw.mask & PSW_MASK_WAIT)
		vtime_start_cpu(int_clock, enter_timer);
}

static inline int s390_nohz_delay(int cpu)
{
	return per_cpu(s390_idle, cpu).nohz_delay != 0;
}

#define arch_needs_cpu(cpu) s390_nohz_delay(cpu)

#endif /* _S390_CPUTIME_H */

/*
 *  Copyright IBM Corp. 2004
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

typedef unsigned long long __nocast cputime_t;
typedef unsigned long long __nocast cputime64_t;

static inline unsigned long __div(unsigned long long n, unsigned long base)
{
#ifndef CONFIG_64BIT
	register_pair rp;

	rp.pair = n >> 1;
	asm ("dr %0,%1" : "+d" (rp) : "d" (base >> 1));
	return rp.subreg.odd;
#else /* CONFIG_64BIT */
	return n / base;
#endif /* CONFIG_64BIT */
}

#define cputime_one_jiffy		jiffies_to_cputime(1)

/*
 * Convert cputime to jiffies and back.
 */
static inline unsigned long cputime_to_jiffies(const cputime_t cputime)
{
	return __div((__force unsigned long long) cputime, 4096000000ULL / HZ);
}

static inline cputime_t jiffies_to_cputime(const unsigned int jif)
{
	return (__force cputime_t)(jif * (4096000000ULL / HZ));
}

static inline u64 cputime64_to_jiffies64(cputime64_t cputime)
{
	unsigned long long jif = (__force unsigned long long) cputime;
	do_div(jif, 4096000000ULL / HZ);
	return jif;
}

static inline cputime64_t jiffies64_to_cputime64(const u64 jif)
{
	return (__force cputime64_t)(jif * (4096000000ULL / HZ));
}

/*
 * Convert cputime to microseconds and back.
 */
static inline unsigned int cputime_to_usecs(const cputime_t cputime)
{
	return (__force unsigned long long) cputime >> 12;
}

static inline cputime_t usecs_to_cputime(const unsigned int m)
{
	return (__force cputime_t)(m * 4096ULL);
}

#define usecs_to_cputime64(m)		usecs_to_cputime(m)

/*
 * Convert cputime to milliseconds and back.
 */
static inline unsigned int cputime_to_secs(const cputime_t cputime)
{
	return __div((__force unsigned long long) cputime, 2048000000) >> 1;
}

static inline cputime_t secs_to_cputime(const unsigned int s)
{
	return (__force cputime_t)(s * 4096000000ULL);
}

/*
 * Convert cputime to timespec and back.
 */
static inline cputime_t timespec_to_cputime(const struct timespec *value)
{
	unsigned long long ret = value->tv_sec * 4096000000ULL;
	return (__force cputime_t)(ret + value->tv_nsec * 4096 / 1000);
}

static inline void cputime_to_timespec(const cputime_t cputime,
				       struct timespec *value)
{
	unsigned long long __cputime = (__force unsigned long long) cputime;
#ifndef CONFIG_64BIT
	register_pair rp;

	rp.pair = __cputime >> 1;
	asm ("dr %0,%1" : "+d" (rp) : "d" (2048000000UL));
	value->tv_nsec = rp.subreg.even * 1000 / 4096;
	value->tv_sec = rp.subreg.odd;
#else
	value->tv_nsec = (__cputime % 4096000000ULL) * 1000 / 4096;
	value->tv_sec = __cputime / 4096000000ULL;
#endif
}

/*
 * Convert cputime to timeval and back.
 * Since cputime and timeval have the same resolution (microseconds)
 * this is easy.
 */
static inline cputime_t timeval_to_cputime(const struct timeval *value)
{
	unsigned long long ret = value->tv_sec * 4096000000ULL;
	return (__force cputime_t)(ret + value->tv_usec * 4096ULL);
}

static inline void cputime_to_timeval(const cputime_t cputime,
				      struct timeval *value)
{
	unsigned long long __cputime = (__force unsigned long long) cputime;
#ifndef CONFIG_64BIT
	register_pair rp;

	rp.pair = __cputime >> 1;
	asm ("dr %0,%1" : "+d" (rp) : "d" (2048000000UL));
	value->tv_usec = rp.subreg.even / 4096;
	value->tv_sec = rp.subreg.odd;
#else
	value->tv_usec = (__cputime % 4096000000ULL) / 4096;
	value->tv_sec = __cputime / 4096000000ULL;
#endif
}

/*
 * Convert cputime to clock and back.
 */
static inline clock_t cputime_to_clock_t(cputime_t cputime)
{
	unsigned long long clock = (__force unsigned long long) cputime;
	do_div(clock, 4096000000ULL / USER_HZ);
	return clock;
}

static inline cputime_t clock_t_to_cputime(unsigned long x)
{
	return (__force cputime_t)(x * (4096000000ULL / USER_HZ));
}

/*
 * Convert cputime64 to clock.
 */
static inline clock_t cputime64_to_clock_t(cputime64_t cputime)
{
	unsigned long long clock = (__force unsigned long long) cputime;
	do_div(clock, 4096000000ULL / USER_HZ);
	return clock;
}

struct s390_idle_data {
	unsigned int sequence;
	unsigned long long idle_count;
	unsigned long long idle_time;
	unsigned long long clock_idle_enter;
	unsigned long long clock_idle_exit;
	unsigned long long timer_idle_enter;
	unsigned long long timer_idle_exit;
};

DECLARE_PER_CPU(struct s390_idle_data, s390_idle);

cputime64_t s390_get_idle_time(int cpu);

#define arch_idle_time(cpu) s390_get_idle_time(cpu)

#endif /* _S390_CPUTIME_H */

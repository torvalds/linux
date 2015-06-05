/*
 *  Copyright IBM Corp. 2004
 *
 *  Author: Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef _S390_CPUTIME_H
#define _S390_CPUTIME_H

#include <linux/types.h>
#include <asm/div64.h>

#define CPUTIME_PER_USEC 4096ULL
#define CPUTIME_PER_SEC (CPUTIME_PER_USEC * USEC_PER_SEC)

/* We want to use full resolution of the CPU timer: 2**-12 micro-seconds. */

typedef unsigned long long __nocast cputime_t;
typedef unsigned long long __nocast cputime64_t;

#define cmpxchg_cputime(ptr, old, new) cmpxchg64(ptr, old, new)

static inline unsigned long __div(unsigned long long n, unsigned long base)
{
	return n / base;
}

#define cputime_one_jiffy		jiffies_to_cputime(1)

/*
 * Convert cputime to jiffies and back.
 */
static inline unsigned long cputime_to_jiffies(const cputime_t cputime)
{
	return __div((__force unsigned long long) cputime, CPUTIME_PER_SEC / HZ);
}

static inline cputime_t jiffies_to_cputime(const unsigned int jif)
{
	return (__force cputime_t)(jif * (CPUTIME_PER_SEC / HZ));
}

static inline u64 cputime64_to_jiffies64(cputime64_t cputime)
{
	unsigned long long jif = (__force unsigned long long) cputime;
	do_div(jif, CPUTIME_PER_SEC / HZ);
	return jif;
}

static inline cputime64_t jiffies64_to_cputime64(const u64 jif)
{
	return (__force cputime64_t)(jif * (CPUTIME_PER_SEC / HZ));
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
	return (__force cputime_t)(m * CPUTIME_PER_USEC);
}

#define usecs_to_cputime64(m)		usecs_to_cputime(m)

/*
 * Convert cputime to milliseconds and back.
 */
static inline unsigned int cputime_to_secs(const cputime_t cputime)
{
	return __div((__force unsigned long long) cputime, CPUTIME_PER_SEC / 2) >> 1;
}

static inline cputime_t secs_to_cputime(const unsigned int s)
{
	return (__force cputime_t)(s * CPUTIME_PER_SEC);
}

/*
 * Convert cputime to timespec and back.
 */
static inline cputime_t timespec_to_cputime(const struct timespec *value)
{
	unsigned long long ret = value->tv_sec * CPUTIME_PER_SEC;
	return (__force cputime_t)(ret + __div(value->tv_nsec * CPUTIME_PER_USEC, NSEC_PER_USEC));
}

static inline void cputime_to_timespec(const cputime_t cputime,
				       struct timespec *value)
{
	unsigned long long __cputime = (__force unsigned long long) cputime;
	value->tv_nsec = (__cputime % CPUTIME_PER_SEC) * NSEC_PER_USEC / CPUTIME_PER_USEC;
	value->tv_sec = __cputime / CPUTIME_PER_SEC;
}

/*
 * Convert cputime to timeval and back.
 * Since cputime and timeval have the same resolution (microseconds)
 * this is easy.
 */
static inline cputime_t timeval_to_cputime(const struct timeval *value)
{
	unsigned long long ret = value->tv_sec * CPUTIME_PER_SEC;
	return (__force cputime_t)(ret + value->tv_usec * CPUTIME_PER_USEC);
}

static inline void cputime_to_timeval(const cputime_t cputime,
				      struct timeval *value)
{
	unsigned long long __cputime = (__force unsigned long long) cputime;
	value->tv_usec = (__cputime % CPUTIME_PER_SEC) / CPUTIME_PER_USEC;
	value->tv_sec = __cputime / CPUTIME_PER_SEC;
}

/*
 * Convert cputime to clock and back.
 */
static inline clock_t cputime_to_clock_t(cputime_t cputime)
{
	unsigned long long clock = (__force unsigned long long) cputime;
	do_div(clock, CPUTIME_PER_SEC / USER_HZ);
	return clock;
}

static inline cputime_t clock_t_to_cputime(unsigned long x)
{
	return (__force cputime_t)(x * (CPUTIME_PER_SEC / USER_HZ));
}

/*
 * Convert cputime64 to clock.
 */
static inline clock_t cputime64_to_clock_t(cputime64_t cputime)
{
	unsigned long long clock = (__force unsigned long long) cputime;
	do_div(clock, CPUTIME_PER_SEC / USER_HZ);
	return clock;
}

cputime64_t arch_cpu_idle_time(int cpu);

#define arch_idle_time(cpu) arch_cpu_idle_time(cpu)

#endif /* _S390_CPUTIME_H */

/*
 *  include/asm-s390/cputime.h
 *
 *  (C) Copyright IBM Corp. 2004
 *
 *  Author: Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef _S390_CPUTIME_H
#define _S390_CPUTIME_H

#include <asm/div64.h>

/* We want to use micro-second resolution. */

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
#define cputime_to_jiffies(__ct)	(__div((__ct), 1000000 / HZ))
#define cputime_to_scaled(__ct)		(__ct)
#define jiffies_to_cputime(__hz)	((cputime_t)(__hz) * (1000000 / HZ))

#define cputime64_zero			(0ULL)
#define cputime64_add(__a, __b)		((__a) + (__b))
#define cputime_to_cputime64(__ct)	(__ct)

static inline u64
cputime64_to_jiffies64(cputime64_t cputime)
{
	do_div(cputime, 1000000 / HZ);
	return cputime;
}

/*
 * Convert cputime to milliseconds and back.
 */
static inline unsigned int
cputime_to_msecs(const cputime_t cputime)
{
	return __div(cputime, 1000);
}

static inline cputime_t
msecs_to_cputime(const unsigned int m)
{
	return (cputime_t) m * 1000;
}

/*
 * Convert cputime to milliseconds and back.
 */
static inline unsigned int
cputime_to_secs(const cputime_t cputime)
{
	return __div(cputime, 1000000);
}

static inline cputime_t
secs_to_cputime(const unsigned int s)
{
	return (cputime_t) s * 1000000;
}

/*
 * Convert cputime to timespec and back.
 */
static inline cputime_t
timespec_to_cputime(const struct timespec *value)
{
        return value->tv_nsec / 1000 + (u64) value->tv_sec * 1000000;
}

static inline void
cputime_to_timespec(const cputime_t cputime, struct timespec *value)
{
#ifndef __s390x__
	register_pair rp;

	rp.pair = cputime >> 1;
	asm ("dr %0,%1" : "+d" (rp) : "d" (1000000 >> 1));
	value->tv_nsec = rp.subreg.even * 1000;
	value->tv_sec = rp.subreg.odd;
#else
	value->tv_nsec = (cputime % 1000000) * 1000;
	value->tv_sec = cputime / 1000000;
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
        return value->tv_usec + (u64) value->tv_sec * 1000000;
}

static inline void
cputime_to_timeval(const cputime_t cputime, struct timeval *value)
{
#ifndef __s390x__
	register_pair rp;

	rp.pair = cputime >> 1;
	asm ("dr %0,%1" : "+d" (rp) : "d" (1000000 >> 1));
	value->tv_usec = rp.subreg.even;
	value->tv_sec = rp.subreg.odd;
#else
	value->tv_usec = cputime % 1000000;
	value->tv_sec = cputime / 1000000;
#endif
}

/*
 * Convert cputime to clock and back.
 */
static inline clock_t
cputime_to_clock_t(cputime_t cputime)
{
	return __div(cputime, 1000000 / USER_HZ);
}

static inline cputime_t
clock_t_to_cputime(unsigned long x)
{
	return (cputime_t) x * (1000000 / USER_HZ);
}

/*
 * Convert cputime64 to clock.
 */
static inline clock_t
cputime64_to_clock_t(cputime64_t cputime)
{
       return __div(cputime, 1000000 / USER_HZ);
}

#endif /* _S390_CPUTIME_H */

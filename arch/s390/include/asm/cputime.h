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

/*
 * Convert cputime to microseconds and back.
 */
static inline unsigned int cputime_to_usecs(const cputime_t cputime)
{
	return (__force unsigned long long) cputime >> 12;
}


u64 arch_cpu_idle_time(int cpu);

#define arch_idle_time(cpu) arch_cpu_idle_time(cpu)

#endif /* _S390_CPUTIME_H */

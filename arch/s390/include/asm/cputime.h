/*
 *  Copyright IBM Corp. 2004
 *
 *  Author: Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef _S390_CPUTIME_H
#define _S390_CPUTIME_H

#include <linux/types.h>
#include <asm/timex.h>

#define CPUTIME_PER_USEC 4096ULL
#define CPUTIME_PER_SEC (CPUTIME_PER_USEC * USEC_PER_SEC)

/* We want to use full resolution of the CPU timer: 2**-12 micro-seconds. */

#define cmpxchg_cputime(ptr, old, new) cmpxchg64(ptr, old, new)

/*
 * Convert cputime to microseconds.
 */
static inline u64 cputime_to_usecs(const u64 cputime)
{
	return cputime >> 12;
}

/*
 * Convert cputime to nanoseconds.
 */
#define cputime_to_nsecs(cputime) tod_to_ns(cputime)

u64 arch_cpu_idle_time(int cpu);

#define arch_idle_time(cpu) arch_cpu_idle_time(cpu)

#endif /* _S390_CPUTIME_H */

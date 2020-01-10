/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of the Linux kernel.
 *
 * Copyright (c) 2011-2014, Intel Corporation
 * Authors: Fenghua Yu <fenghua.yu@intel.com>,
 *          H. Peter Anvin <hpa@linux.intel.com>
 */

#ifndef ASM_X86_ARCHRANDOM_H
#define ASM_X86_ARCHRANDOM_H

#include <asm/processor.h>
#include <asm/cpufeature.h>

#define RDRAND_RETRY_LOOPS	10

#define RDRAND_INT	".byte 0x0f,0xc7,0xf0"
#define RDSEED_INT	".byte 0x0f,0xc7,0xf8"
#ifdef CONFIG_X86_64
# define RDRAND_LONG	".byte 0x48,0x0f,0xc7,0xf0"
# define RDSEED_LONG	".byte 0x48,0x0f,0xc7,0xf8"
#else
# define RDRAND_LONG	RDRAND_INT
# define RDSEED_LONG	RDSEED_INT
#endif

/* Unconditional execution of RDRAND and RDSEED */

static inline bool rdrand_long(unsigned long *v)
{
	bool ok;
	unsigned int retry = RDRAND_RETRY_LOOPS;
	do {
		asm volatile(RDRAND_LONG
			     CC_SET(c)
			     : CC_OUT(c) (ok), "=a" (*v));
		if (ok)
			return true;
	} while (--retry);
	return false;
}

static inline bool rdrand_int(unsigned int *v)
{
	bool ok;
	unsigned int retry = RDRAND_RETRY_LOOPS;
	do {
		asm volatile(RDRAND_INT
			     CC_SET(c)
			     : CC_OUT(c) (ok), "=a" (*v));
		if (ok)
			return true;
	} while (--retry);
	return false;
}

static inline bool rdseed_long(unsigned long *v)
{
	bool ok;
	asm volatile(RDSEED_LONG
		     CC_SET(c)
		     : CC_OUT(c) (ok), "=a" (*v));
	return ok;
}

static inline bool rdseed_int(unsigned int *v)
{
	bool ok;
	asm volatile(RDSEED_INT
		     CC_SET(c)
		     : CC_OUT(c) (ok), "=a" (*v));
	return ok;
}

/*
 * These are the generic interfaces; they must not be declared if the
 * stubs in <linux/random.h> are to be invoked,
 * i.e. CONFIG_ARCH_RANDOM is not defined.
 */
#ifdef CONFIG_ARCH_RANDOM

static inline bool arch_get_random_long(unsigned long *v)
{
	return static_cpu_has(X86_FEATURE_RDRAND) ? rdrand_long(v) : false;
}

static inline bool arch_get_random_int(unsigned int *v)
{
	return static_cpu_has(X86_FEATURE_RDRAND) ? rdrand_int(v) : false;
}

static inline bool arch_get_random_seed_long(unsigned long *v)
{
	return static_cpu_has(X86_FEATURE_RDSEED) ? rdseed_long(v) : false;
}

static inline bool arch_get_random_seed_int(unsigned int *v)
{
	return static_cpu_has(X86_FEATURE_RDSEED) ? rdseed_int(v) : false;
}

extern void x86_init_rdrand(struct cpuinfo_x86 *c);

#else  /* !CONFIG_ARCH_RANDOM */

static inline void x86_init_rdrand(struct cpuinfo_x86 *c) { }

#endif  /* !CONFIG_ARCH_RANDOM */

#endif /* ASM_X86_ARCHRANDOM_H */

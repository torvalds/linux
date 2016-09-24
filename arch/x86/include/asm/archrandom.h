/*
 * This file is part of the Linux kernel.
 *
 * Copyright (c) 2011-2014, Intel Corporation
 * Authors: Fenghua Yu <fenghua.yu@intel.com>,
 *          H. Peter Anvin <hpa@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
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
		asm volatile(RDRAND_LONG "\n\t"
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
		asm volatile(RDRAND_INT "\n\t"
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
	asm volatile(RDSEED_LONG "\n\t"
		     CC_SET(c)
		     : CC_OUT(c) (ok), "=a" (*v));
	return ok;
}

static inline bool rdseed_int(unsigned int *v)
{
	bool ok;
	asm volatile(RDSEED_INT "\n\t"
		     CC_SET(c)
		     : CC_OUT(c) (ok), "=a" (*v));
	return ok;
}

/* Conditional execution based on CPU type */
#define arch_has_random()	static_cpu_has(X86_FEATURE_RDRAND)
#define arch_has_random_seed()	static_cpu_has(X86_FEATURE_RDSEED)

/*
 * These are the generic interfaces; they must not be declared if the
 * stubs in <linux/random.h> are to be invoked,
 * i.e. CONFIG_ARCH_RANDOM is not defined.
 */
#ifdef CONFIG_ARCH_RANDOM

static inline bool arch_get_random_long(unsigned long *v)
{
	return arch_has_random() ? rdrand_long(v) : false;
}

static inline bool arch_get_random_int(unsigned int *v)
{
	return arch_has_random() ? rdrand_int(v) : false;
}

static inline bool arch_get_random_seed_long(unsigned long *v)
{
	return arch_has_random_seed() ? rdseed_long(v) : false;
}

static inline bool arch_get_random_seed_int(unsigned int *v)
{
	return arch_has_random_seed() ? rdseed_int(v) : false;
}

extern void x86_init_rdrand(struct cpuinfo_x86 *c);

#else  /* !CONFIG_ARCH_RANDOM */

static inline void x86_init_rdrand(struct cpuinfo_x86 *c) { }

#endif  /* !CONFIG_ARCH_RANDOM */

#endif /* ASM_X86_ARCHRANDOM_H */

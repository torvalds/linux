/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kernel interface for the s390 arch_random_* functions
 *
 * Copyright IBM Corp. 2017, 2022
 *
 * Author: Harald Freudenberger <freude@de.ibm.com>
 *
 */

#ifndef _ASM_S390_ARCHRANDOM_H
#define _ASM_S390_ARCHRANDOM_H

#include <linux/static_key.h>
#include <linux/preempt.h>
#include <linux/atomic.h>
#include <asm/cpacf.h>

DECLARE_STATIC_KEY_FALSE(s390_arch_random_available);
extern atomic64_t s390_arch_random_counter;

static inline size_t __must_check arch_get_random_longs(unsigned long *v, size_t max_longs)
{
	return 0;
}

static inline size_t __must_check arch_get_random_seed_longs(unsigned long *v, size_t max_longs)
{
	if (static_branch_likely(&s390_arch_random_available) &&
	    in_task()) {
		cpacf_trng(NULL, 0, (u8 *)v, max_longs * sizeof(*v));
		atomic64_add(max_longs * sizeof(*v), &s390_arch_random_counter);
		return max_longs;
	}
	return 0;
}

#endif /* _ASM_S390_ARCHRANDOM_H */

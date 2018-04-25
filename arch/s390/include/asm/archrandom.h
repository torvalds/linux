/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kernel interface for the s390 arch_random_* functions
 *
 * Copyright IBM Corp. 2017
 *
 * Author: Harald Freudenberger <freude@de.ibm.com>
 *
 */

#ifndef _ASM_S390_ARCHRANDOM_H
#define _ASM_S390_ARCHRANDOM_H

#ifdef CONFIG_ARCH_RANDOM

#include <linux/static_key.h>
#include <linux/atomic.h>

DECLARE_STATIC_KEY_FALSE(s390_arch_random_available);
extern atomic64_t s390_arch_random_counter;

bool s390_arch_random_generate(u8 *buf, unsigned int nbytes);

static inline bool arch_has_random(void)
{
	return false;
}

static inline bool arch_has_random_seed(void)
{
	if (static_branch_likely(&s390_arch_random_available))
		return true;
	return false;
}

static inline bool arch_get_random_long(unsigned long *v)
{
	return false;
}

static inline bool arch_get_random_int(unsigned int *v)
{
	return false;
}

static inline bool arch_get_random_seed_long(unsigned long *v)
{
	if (static_branch_likely(&s390_arch_random_available)) {
		return s390_arch_random_generate((u8 *)v, sizeof(*v));
	}
	return false;
}

static inline bool arch_get_random_seed_int(unsigned int *v)
{
	if (static_branch_likely(&s390_arch_random_available)) {
		return s390_arch_random_generate((u8 *)v, sizeof(*v));
	}
	return false;
}

#endif /* CONFIG_ARCH_RANDOM */
#endif /* _ASM_S390_ARCHRANDOM_H */

// SPDX-License-Identifier: GPL-2.0
/*
 * s390 arch random implementation.
 *
 * Copyright IBM Corp. 2017
 * Author(s): Harald Freudenberger <freude@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/atomic.h>
#include <linux/random.h>
#include <linux/static_key.h>
#include <asm/cpacf.h>

DEFINE_STATIC_KEY_FALSE(s390_arch_random_available);

atomic64_t s390_arch_random_counter = ATOMIC64_INIT(0);
EXPORT_SYMBOL(s390_arch_random_counter);

static int __init s390_arch_random_init(void)
{
	/* check if subfunction CPACF_PRNO_TRNG is available */
	if (cpacf_query_func(CPACF_PRNO, CPACF_PRNO_TRNG))
		static_branch_enable(&s390_arch_random_available);

	return 0;
}
arch_initcall(s390_arch_random_init);

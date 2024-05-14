// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Derived from arch/powerpc/platforms/powernv/rng.c, which is:
 * Copyright 2013, Michael Ellerman, IBM Corporation.
 */

#define pr_fmt(fmt)	"microwatt-rng: " fmt

#include <linux/kernel.h>
#include <linux/smp.h>
#include <asm/archrandom.h>
#include <asm/cputable.h>
#include <asm/machdep.h>
#include "microwatt.h"

#define DARN_ERR 0xFFFFFFFFFFFFFFFFul

static int microwatt_get_random_darn(unsigned long *v)
{
	unsigned long val;

	/* Using DARN with L=1 - 64-bit conditioned random number */
	asm volatile(PPC_DARN(%0, 1) : "=r"(val));

	if (val == DARN_ERR)
		return 0;

	*v = val;

	return 1;
}

void __init microwatt_rng_init(void)
{
	unsigned long val;
	int i;

	for (i = 0; i < 10; i++) {
		if (microwatt_get_random_darn(&val)) {
			ppc_md.get_random_seed = microwatt_get_random_darn;
			return;
		}
	}
}

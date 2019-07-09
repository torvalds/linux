// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2013, Michael Ellerman, IBM Corporation.
 */

#define pr_fmt(fmt)	"pseries-rng: " fmt

#include <linux/kernel.h>
#include <linux/of.h>
#include <asm/archrandom.h>
#include <asm/machdep.h>
#include <asm/plpar_wrappers.h>


static int pseries_get_random_long(unsigned long *v)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];

	if (plpar_hcall(H_RANDOM, retbuf) == H_SUCCESS) {
		*v = retbuf[0];
		return 1;
	}

	return 0;
}

static __init int rng_init(void)
{
	struct device_node *dn;

	dn = of_find_compatible_node(NULL, NULL, "ibm,random");
	if (!dn)
		return -ENODEV;

	pr_info("Registering arch random hook.\n");

	ppc_md.get_random_seed = pseries_get_random_long;

	return 0;
}
machine_subsys_initcall(pseries, rng_init);

/*
 * Copyright 2013, Michael Ellerman, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt)	"powernv-rng: " fmt

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <asm/archrandom.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/machdep.h>


struct powernv_rng {
	void __iomem *regs;
	unsigned long mask;
};

static DEFINE_PER_CPU(struct powernv_rng *, powernv_rng);


static unsigned long rng_whiten(struct powernv_rng *rng, unsigned long val)
{
	unsigned long parity;

	/* Calculate the parity of the value */
	asm ("popcntd %0,%1" : "=r" (parity) : "r" (val));

	/* xor our value with the previous mask */
	val ^= rng->mask;

	/* update the mask based on the parity of this value */
	rng->mask = (rng->mask << 1) | (parity & 1);

	return val;
}

int powernv_get_random_long(unsigned long *v)
{
	struct powernv_rng *rng;

	rng = get_cpu_var(powernv_rng);

	*v = rng_whiten(rng, in_be64(rng->regs));

	put_cpu_var(rng);

	return 1;
}
EXPORT_SYMBOL_GPL(powernv_get_random_long);

static __init void rng_init_per_cpu(struct powernv_rng *rng,
				    struct device_node *dn)
{
	int chip_id, cpu;

	chip_id = of_get_ibm_chip_id(dn);
	if (chip_id == -1)
		pr_warn("No ibm,chip-id found for %s.\n", dn->full_name);

	for_each_possible_cpu(cpu) {
		if (per_cpu(powernv_rng, cpu) == NULL ||
		    cpu_to_chip_id(cpu) == chip_id) {
			per_cpu(powernv_rng, cpu) = rng;
		}
	}
}

static __init int rng_create(struct device_node *dn)
{
	struct powernv_rng *rng;
	unsigned long val;

	rng = kzalloc(sizeof(*rng), GFP_KERNEL);
	if (!rng)
		return -ENOMEM;

	rng->regs = of_iomap(dn, 0);
	if (!rng->regs) {
		kfree(rng);
		return -ENXIO;
	}

	val = in_be64(rng->regs);
	rng->mask = val;

	rng_init_per_cpu(rng, dn);

	pr_info_once("Registering arch random hook.\n");

	ppc_md.get_random_long = powernv_get_random_long;

	return 0;
}

static __init int rng_init(void)
{
	struct device_node *dn;
	int rc;

	for_each_compatible_node(dn, NULL, "ibm,power-rng") {
		rc = rng_create(dn);
		if (rc) {
			pr_err("Failed creating rng for %s (%d).\n",
				dn->full_name, rc);
			continue;
		}

		/* Create devices for hwrng driver */
		of_platform_device_create(dn, NULL, NULL);
	}

	return 0;
}
subsys_initcall(rng_init);

/*
 * Nest Performance Monitor counter support for POWER8 processors.
 *
 * Copyright (C) 2015 Madhavan Srinivasan, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include "nest-pmu.h"

static struct perchip_nest_info p8_nest_perchip_info[P8_NEST_MAX_CHIPS];

static int nest_ima_dt_parser(void)
{
	const __be32 *gcid;
	const __be64 *chip_ima_reg;
	const __be64 *chip_ima_size;
	struct device_node *dev;
	struct perchip_nest_info *p8ni;
	int idx;

	/*
	 * "nest-ima" folder contains two things,
	 * a) per-chip reserved memory region for Nest PMU Counter data
	 * b) Support Nest PMU units and their event files
	 */
	for_each_node_with_property(dev, "ibm,ima-chip") {
		gcid = of_get_property(dev, "ibm,chip-id", NULL);
		chip_ima_reg = of_get_property(dev, "reg", NULL);
		chip_ima_size = of_get_property(dev, "size", NULL);

		if ((!gcid) || (!chip_ima_reg) || (!chip_ima_size)) {
			pr_err("Nest_PMU: device %s missing property\n",
							dev->full_name);
			return -ENODEV;
		}

		/* chip id to save reserve memory region */
		idx = (uint32_t)be32_to_cpup(gcid);

		/*
		 * Using a local variable to make it compact and
		 * easier to read
		 */
		p8ni = &p8_nest_perchip_info[idx];
		p8ni->pbase = be64_to_cpup(chip_ima_reg);
		p8ni->size = be64_to_cpup(chip_ima_size);
		p8ni->vbase = (uint64_t) phys_to_virt(p8ni->pbase);
	}

	return 0;
}

static int __init nest_pmu_init(void)
{
	int ret = -ENODEV;

	/*
	 * Lets do this only if we are hypervisor
	 */
	if (!cur_cpu_spec->oprofile_cpu_type ||
	    !(strcmp(cur_cpu_spec->oprofile_cpu_type, "ppc64/power8") == 0) ||
	    !cpu_has_feature(CPU_FTR_HVMODE))
		return ret;

	/*
	 * Nest PMU information is grouped under "nest-ima" node
	 * of the top-level device-tree directory. Detect Nest PMU
	 * by the "ibm,ima-chip" property.
	 */
	if (!of_find_node_with_property(NULL, "ibm,ima-chip"))
		return ret;

	/*
	 * Parse device-tree for Nest PMU information
	 */
	ret = nest_ima_dt_parser();
	if (ret)
		return ret;

	return 0;
}
device_initcall(nest_pmu_init);

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
static struct nest_pmu *per_nest_pmu_arr[P8_NEST_MAX_PMUS];

static int nest_event_info(struct property *pp, char *start,
			struct nest_ima_events *p8_events, int flg, u32 val)
{
	char *buf;

	/* memory for event name */
	buf = kzalloc(P8_NEST_MAX_PMU_NAME_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	strncpy(buf, start, strlen(start));
	p8_events->ev_name = buf;

	/* memory for content */
	buf = kzalloc(P8_NEST_MAX_PMU_NAME_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (flg) {
		/* string content*/
		if (!pp->value ||
		   (strnlen(pp->value, pp->length) == pp->length))
			return -EINVAL;

		strncpy(buf, (const char *)pp->value, pp->length);
	} else
		sprintf(buf, "event=0x%x", val);

	p8_events->ev_value = buf;
	return 0;
}

static int nest_pmu_create(struct device_node *dev, int pmu_index)
{
	struct nest_ima_events **p8_events_arr, *p8_events;
	struct nest_pmu *pmu_ptr;
	struct property *pp;
	char *buf, *start;
	const __be32 *lval;
	u32 val;
	int idx = 0, ret;

	if (!dev)
		return -EINVAL;

	/* memory for nest pmus */
	pmu_ptr = kzalloc(sizeof(struct nest_pmu), GFP_KERNEL);
	if (!pmu_ptr)
		return -ENOMEM;

	/* Needed for hotplug/migration */
	per_nest_pmu_arr[pmu_index] = pmu_ptr;

	/* memory for nest pmu events */
	p8_events_arr = kzalloc((sizeof(struct nest_ima_events) * 64),
								GFP_KERNEL);
	if (!p8_events_arr)
		return -ENOMEM;
	p8_events = (struct nest_ima_events *)p8_events_arr;

	/*
	 * Loop through each property
	 */
	for_each_property_of_node(dev, pp) {
		start = pp->name;

		if (!strcmp(pp->name, "name")) {
			if (!pp->value ||
			   (strnlen(pp->value, pp->length) == pp->length))
				return -EINVAL;

			buf = kzalloc(P8_NEST_MAX_PMU_NAME_LEN, GFP_KERNEL);
			if (!buf)
				return -ENOMEM;

			/* Save the name to register it later */
			sprintf(buf, "Nest_%s", (char *)pp->value);
			pmu_ptr->pmu.name = (char *)buf;
			continue;
		}

		/* Skip these, we dont need it */
		if (!strcmp(pp->name, "phandle") ||
		    !strcmp(pp->name, "device_type") ||
		    !strcmp(pp->name, "linux,phandle"))
			continue;

		if (strncmp(pp->name, "unit.", 5) == 0) {
			/* Skip first few chars in the name */
			start += 5;
			ret = nest_event_info(pp, start, p8_events++, 1, 0);
		} else if (strncmp(pp->name, "scale.", 6) == 0) {
			/* Skip first few chars in the name */
			start += 6;
			ret = nest_event_info(pp, start, p8_events++, 1, 0);
		} else {
			lval = of_get_property(dev, pp->name, NULL);
			val = (uint32_t)be32_to_cpup(lval);

			ret = nest_event_info(pp, start, p8_events++, 0, val);
		}

		if (ret)
			return ret;

		/* book keeping */
		idx++;
	}

	return 0;
}

static int nest_ima_dt_parser(void)
{
	const __be32 *gcid;
	const __be64 *chip_ima_reg;
	const __be64 *chip_ima_size;
	struct device_node *dev;
	struct perchip_nest_info *p8ni;
	int idx, ret;

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

	/* Look for supported Nest PMU units */
	idx = 0;
	for_each_node_by_type(dev, "nest-ima-unit") {
		ret = nest_pmu_create(dev, idx);
		if (ret)
			return ret;
		idx++;
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

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

PMU_FORMAT_ATTR(event, "config:0-20");
struct attribute *p8_nest_format_attrs[] = {
	&format_attr_event.attr,
	NULL,
};

struct attribute_group p8_nest_format_group = {
	.name = "format",
	.attrs = p8_nest_format_attrs,
};

static int p8_nest_event_init(struct perf_event *event)
{
	int chip_id;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* Sampling not supported yet */
	if (event->hw.sample_period)
		return -EINVAL;

	/* unsupported modes and filters */
	if (event->attr.exclude_user   ||
	    event->attr.exclude_kernel ||
	    event->attr.exclude_hv     ||
	    event->attr.exclude_idle   ||
	    event->attr.exclude_host   ||
	    event->attr.exclude_guest)
		return -EINVAL;

	if (event->cpu < 0)
		return -EINVAL;

	chip_id = topology_physical_package_id(event->cpu);
	event->hw.event_base = event->attr.config +
					p8_nest_perchip_info[chip_id].vbase;

	return 0;
}

static void p8_nest_read_counter(struct perf_event *event)
{
	uint64_t *addr;
	u64 data = 0;

	addr = (u64 *)event->hw.event_base;
	data = __be64_to_cpu(*addr);
	local64_set(&event->hw.prev_count, data);
}

static void p8_nest_perf_event_update(struct perf_event *event)
{
	u64 counter_prev, counter_new, final_count;
	uint64_t *addr;

	addr = (uint64_t *)event->hw.event_base;
	counter_prev = local64_read(&event->hw.prev_count);
	counter_new = __be64_to_cpu(*addr);
	final_count = counter_new - counter_prev;

	local64_set(&event->hw.prev_count, counter_new);
	local64_add(final_count, &event->count);
}

static void p8_nest_event_start(struct perf_event *event, int flags)
{
	event->hw.state = 0;
	p8_nest_read_counter(event);
}

static void p8_nest_event_stop(struct perf_event *event, int flags)
{
	if (flags & PERF_EF_UPDATE)
		p8_nest_perf_event_update(event);
}

static int p8_nest_event_add(struct perf_event *event, int flags)
{
	if (flags & PERF_EF_START)
		p8_nest_event_start(event, flags);

	return 0;
}

/*
 * Populate pmu ops in the structure
 */
static int update_pmu_ops(struct nest_pmu *pmu)
{
	if (!pmu)
		return -EINVAL;

	pmu->pmu.task_ctx_nr = perf_invalid_context;
	pmu->pmu.event_init = p8_nest_event_init;
	pmu->pmu.add = p8_nest_event_add;
	pmu->pmu.del = p8_nest_event_stop;
	pmu->pmu.start = p8_nest_event_start;
	pmu->pmu.stop = p8_nest_event_stop;
	pmu->pmu.read = p8_nest_perf_event_update;
	pmu->pmu.attr_groups = pmu->attr_groups;

	return 0;
}

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

/*
 * Populate event name and string in attribute
 */
struct attribute *dev_str_attr(const char *name, const char *str)
{
	struct perf_pmu_events_attr *attr;

	attr = kzalloc(sizeof(*attr), GFP_KERNEL);

	attr->event_str = str;
	attr->attr.attr.name = name;
	attr->attr.attr.mode = 0444;
	attr->attr.show = perf_event_sysfs_show;

	return &attr->attr.attr;
}

int update_events_in_group(
	struct nest_ima_events *p8_events, int idx, struct nest_pmu *pmu)
{
	struct attribute_group *attr_group;
	struct attribute **attrs;
	int i;

	/* Allocate memory for event attribute group */
	attr_group = kzalloc(((sizeof(struct attribute *) * (idx + 1)) +
				sizeof(*attr_group)), GFP_KERNEL);
	if (!attr_group)
		return -ENOMEM;

	attrs = (struct attribute **)(attr_group + 1);
	attr_group->name = "events";
	attr_group->attrs = attrs;

	for (i = 0; i < idx; i++, p8_events++)
		attrs[i] = dev_str_attr((char *)p8_events->ev_name,
					(char *)p8_events->ev_value);

	pmu->attr_groups[0] = attr_group;
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
			pmu_ptr->attr_groups[1] = &p8_nest_format_group;
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

	update_events_in_group(
		(struct nest_ima_events *)p8_events_arr, idx, pmu_ptr);

	update_pmu_ops(pmu_ptr);
	/* Register the pmu */
	ret = perf_pmu_register(&pmu_ptr->pmu, pmu_ptr->pmu.name, -1);
	if (ret) {
		pr_err("Nest PMU %s Register failed\n", pmu_ptr->pmu.name);
		return ret;
	}

	pr_info("%s performance monitor hardware support registered\n",
			pmu_ptr->pmu.name);
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

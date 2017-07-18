/*
 * In-Memory Collection (IMC) Performance Monitor counter support.
 *
 * Copyright (C) 2017 Madhavan Srinivasan, IBM Corporation.
 *           (C) 2017 Anju T Sudhakar, IBM Corporation.
 *           (C) 2017 Hemant K Shaw, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or later version.
 */
#include <linux/perf_event.h>
#include <linux/slab.h>
#include <asm/opal.h>
#include <asm/imc-pmu.h>
#include <asm/cputhreads.h>
#include <asm/smp.h>
#include <linux/string.h>

/* Nest IMC data structures and variables */

/*
 * Used to avoid races in counting the nest-pmu units during hotplug
 * register and unregister
 */
static DEFINE_MUTEX(nest_init_lock);
static DEFINE_PER_CPU(struct imc_pmu_ref *, local_nest_imc_refc);
static struct imc_pmu *per_nest_pmu_arr[IMC_MAX_PMUS];
static cpumask_t nest_imc_cpumask;
struct imc_pmu_ref *nest_imc_refc;
static int nest_pmus;

struct imc_pmu *imc_event_to_pmu(struct perf_event *event)
{
	return container_of(event->pmu, struct imc_pmu, pmu);
}

PMU_FORMAT_ATTR(event, "config:0-40");
PMU_FORMAT_ATTR(offset, "config:0-31");
PMU_FORMAT_ATTR(rvalue, "config:32");
PMU_FORMAT_ATTR(mode, "config:33-40");
static struct attribute *imc_format_attrs[] = {
	&format_attr_event.attr,
	&format_attr_offset.attr,
	&format_attr_rvalue.attr,
	&format_attr_mode.attr,
	NULL,
};

static struct attribute_group imc_format_group = {
	.name = "format",
	.attrs = imc_format_attrs,
};

/* Get the cpumask printed to a buffer "buf" */
static ssize_t imc_pmu_cpumask_get_attr(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct pmu *pmu = dev_get_drvdata(dev);
	struct imc_pmu *imc_pmu = container_of(pmu, struct imc_pmu, pmu);
	cpumask_t *active_mask;

	/* Subsequenct patch will add more pmu types here */
	switch(imc_pmu->domain){
	case IMC_DOMAIN_NEST:
		active_mask = &nest_imc_cpumask;
		break;
	default:
		return 0;
	}

	return cpumap_print_to_pagebuf(true, buf, active_mask);
}

static DEVICE_ATTR(cpumask, S_IRUGO, imc_pmu_cpumask_get_attr, NULL);

static struct attribute *imc_pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static struct attribute_group imc_pmu_cpumask_attr_group = {
	.attrs = imc_pmu_cpumask_attrs,
};

/* device_str_attr_create : Populate event "name" and string "str" in attribute */
static struct attribute *device_str_attr_create(const char *name, const char *str)
{
	struct perf_pmu_events_attr *attr;

	attr = kzalloc(sizeof(*attr), GFP_KERNEL);
	if (!attr)
		return NULL;
	sysfs_attr_init(&attr->attr.attr);

	attr->event_str = str;
	attr->attr.attr.name = name;
	attr->attr.attr.mode = 0444;
	attr->attr.show = perf_event_sysfs_show;

	return &attr->attr.attr;
}

struct imc_events *imc_parse_event(struct device_node *np, const char *scale,
				  const char *unit, const char *prefix, u32 base)
{
	struct imc_events *event;
	const char *s;
	u32 reg;

	event = kzalloc(sizeof(struct imc_events), GFP_KERNEL);
	if (!event)
		return NULL;

	if (of_property_read_u32(np, "reg", &reg))
		goto error;
	/* Add the base_reg value to the "reg" */
	event->value = base + reg;

	if (of_property_read_string(np, "event-name", &s))
		goto error;

	event->name = kasprintf(GFP_KERNEL, "%s%s", prefix, s);
	if (!event->name)
		goto error;

	if (of_property_read_string(np, "scale", &s))
		s = scale;

	if (s) {
		event->scale = kstrdup(s, GFP_KERNEL);
		if (!event->scale)
			goto error;
	}

	if (of_property_read_string(np, "unit", &s))
		s = unit;

	if (s) {
		event->unit = kstrdup(s, GFP_KERNEL);
		if (!event->unit)
			goto error;
	}

	return event;
error:
	kfree(event->unit);
	kfree(event->scale);
	kfree(event->name);
	kfree(event);

	return NULL;
}

/*
 * update_events_in_group: Update the "events" information in an attr_group
 *                         and assign the attr_group to the pmu "pmu".
 */
static int update_events_in_group(struct device_node *node, struct imc_pmu *pmu)
{
	struct attribute_group *attr_group;
	struct attribute **attrs, *dev_str;
	struct device_node *np, *pmu_events;
	struct imc_events *ev;
	u32 handle, base_reg;
	int i=0, j=0, ct;
	const char *prefix, *g_scale, *g_unit;
	const char *ev_val_str, *ev_scale_str, *ev_unit_str;

	if (!of_property_read_u32(node, "events", &handle))
		pmu_events = of_find_node_by_phandle(handle);
	else
		return 0;

	/* Did not find any node with a given phandle */
	if (!pmu_events)
		return 0;

	/* Get a count of number of child nodes */
	ct = of_get_child_count(pmu_events);

	/* Get the event prefix */
	if (of_property_read_string(node, "events-prefix", &prefix))
		return 0;

	/* Get a global unit and scale data if available */
	if (of_property_read_string(node, "scale", &g_scale))
		g_scale = NULL;

	if (of_property_read_string(node, "unit", &g_unit))
		g_unit = NULL;

	/* "reg" property gives out the base offset of the counters data */
	of_property_read_u32(node, "reg", &base_reg);

	/* Allocate memory for the events */
	pmu->events = kcalloc(ct, sizeof(struct imc_events), GFP_KERNEL);
	if (!pmu->events)
		return -ENOMEM;

	ct = 0;
	/* Parse the events and update the struct */
	for_each_child_of_node(pmu_events, np) {
		ev = imc_parse_event(np, g_scale, g_unit, prefix, base_reg);
		if (ev)
			pmu->events[ct++] = ev;
	}

	/* Allocate memory for attribute group */
	attr_group = kzalloc(sizeof(*attr_group), GFP_KERNEL);
	if (!attr_group)
		return -ENOMEM;

	/*
	 * Allocate memory for attributes.
	 * Since we have count of events for this pmu, we also allocate
	 * memory for the scale and unit attribute for now.
	 * "ct" has the total event structs added from the events-parent node.
	 * So allocate three times the "ct" (this includes event, event_scale and
	 * event_unit).
	 */
	attrs = kcalloc(((ct * 3) + 1), sizeof(struct attribute *), GFP_KERNEL);
	if (!attrs) {
		kfree(attr_group);
		kfree(pmu->events);
		return -ENOMEM;
	}

	attr_group->name = "events";
	attr_group->attrs = attrs;
	do {
		ev_val_str = kasprintf(GFP_KERNEL, "event=0x%x", pmu->events[i]->value);
		dev_str = device_str_attr_create(pmu->events[i]->name, ev_val_str);
		if (!dev_str)
			continue;

		attrs[j++] = dev_str;
		if (pmu->events[i]->scale) {
			ev_scale_str = kasprintf(GFP_KERNEL, "%s.scale",pmu->events[i]->name);
			dev_str = device_str_attr_create(ev_scale_str, pmu->events[i]->scale);
			if (!dev_str)
				continue;

			attrs[j++] = dev_str;
		}

		if (pmu->events[i]->unit) {
			ev_unit_str = kasprintf(GFP_KERNEL, "%s.unit",pmu->events[i]->name);
			dev_str = device_str_attr_create(ev_unit_str, pmu->events[i]->unit);
			if (!dev_str)
				continue;

			attrs[j++] = dev_str;
		}
	} while (++i < ct);

	/* Save the event attribute */
	pmu->attr_groups[IMC_EVENT_ATTR] = attr_group;

	kfree(pmu->events);
	return 0;
}

/* get_nest_pmu_ref: Return the imc_pmu_ref struct for the given node */
static struct imc_pmu_ref *get_nest_pmu_ref(int cpu)
{
	return per_cpu(local_nest_imc_refc, cpu);
}

static void nest_change_cpu_context(int old_cpu, int new_cpu)
{
	struct imc_pmu **pn = per_nest_pmu_arr;
	int i;

	if (old_cpu < 0 || new_cpu < 0)
		return;

	for (i = 0; *pn && i < IMC_MAX_PMUS; i++, pn++)
		perf_pmu_migrate_context(&(*pn)->pmu, old_cpu, new_cpu);
}

static int ppc_nest_imc_cpu_offline(unsigned int cpu)
{
	int nid, target = -1;
	const struct cpumask *l_cpumask;
	struct imc_pmu_ref *ref;

	/*
	 * Check in the designated list for this cpu. Dont bother
	 * if not one of them.
	 */
	if (!cpumask_test_and_clear_cpu(cpu, &nest_imc_cpumask))
		return 0;

	/*
	 * Now that this cpu is one of the designated,
	 * find a next cpu a) which is online and b) in same chip.
	 */
	nid = cpu_to_node(cpu);
	l_cpumask = cpumask_of_node(nid);
	target = cpumask_any_but(l_cpumask, cpu);

	/*
	 * Update the cpumask with the target cpu and
	 * migrate the context if needed
	 */
	if (target >= 0 && target < nr_cpu_ids) {
		cpumask_set_cpu(target, &nest_imc_cpumask);
		nest_change_cpu_context(cpu, target);
	} else {
		opal_imc_counters_stop(OPAL_IMC_COUNTERS_NEST,
				       get_hard_smp_processor_id(cpu));
		/*
		 * If this is the last cpu in this chip then, skip the reference
		 * count mutex lock and make the reference count on this chip zero.
		 */
		ref = get_nest_pmu_ref(cpu);
		if (!ref)
			return -EINVAL;

		ref->refc = 0;
	}
	return 0;
}

static int ppc_nest_imc_cpu_online(unsigned int cpu)
{
	const struct cpumask *l_cpumask;
	static struct cpumask tmp_mask;
	int res;

	/* Get the cpumask of this node */
	l_cpumask = cpumask_of_node(cpu_to_node(cpu));

	/*
	 * If this is not the first online CPU on this node, then
	 * just return.
	 */
	if (cpumask_and(&tmp_mask, l_cpumask, &nest_imc_cpumask))
		return 0;

	/*
	 * If this is the first online cpu on this node
	 * disable the nest counters by making an OPAL call.
	 */
	res = opal_imc_counters_stop(OPAL_IMC_COUNTERS_NEST,
				     get_hard_smp_processor_id(cpu));
	if (res)
		return res;

	/* Make this CPU the designated target for counter collection */
	cpumask_set_cpu(cpu, &nest_imc_cpumask);
	return 0;
}

static int nest_pmu_cpumask_init(void)
{
	return cpuhp_setup_state(CPUHP_AP_PERF_POWERPC_NEST_IMC_ONLINE,
				 "perf/powerpc/imc:online",
				 ppc_nest_imc_cpu_online,
				 ppc_nest_imc_cpu_offline);
}

static void nest_imc_counters_release(struct perf_event *event)
{
	int rc, node_id;
	struct imc_pmu_ref *ref;

	if (event->cpu < 0)
		return;

	node_id = cpu_to_node(event->cpu);

	/*
	 * See if we need to disable the nest PMU.
	 * If no events are currently in use, then we have to take a
	 * mutex to ensure that we don't race with another task doing
	 * enable or disable the nest counters.
	 */
	ref = get_nest_pmu_ref(event->cpu);
	if (!ref)
		return;

	/* Take the mutex lock for this node and then decrement the reference count */
	mutex_lock(&ref->lock);
	ref->refc--;
	if (ref->refc == 0) {
		rc = opal_imc_counters_stop(OPAL_IMC_COUNTERS_NEST,
					    get_hard_smp_processor_id(event->cpu));
		if (rc) {
			mutex_unlock(&nest_imc_refc[node_id].lock);
			pr_err("nest-imc: Unable to stop the counters for core %d\n", node_id);
			return;
		}
	} else if (ref->refc < 0) {
		WARN(1, "nest-imc: Invalid event reference count\n");
		ref->refc = 0;
	}
	mutex_unlock(&ref->lock);
}

static int nest_imc_event_init(struct perf_event *event)
{
	int chip_id, rc, node_id;
	u32 l_config, config = event->attr.config;
	struct imc_mem_info *pcni;
	struct imc_pmu *pmu;
	struct imc_pmu_ref *ref;
	bool flag = false;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* Sampling not supported */
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

	pmu = imc_event_to_pmu(event);

	/* Sanity check for config (event offset) */
	if ((config & IMC_EVENT_OFFSET_MASK) > pmu->counter_mem_size)
		return -EINVAL;

	/*
	 * Nest HW counter memory resides in a per-chip reserve-memory (HOMER).
	 * Get the base memory addresss for this cpu.
	 */
	chip_id = topology_physical_package_id(event->cpu);
	pcni = pmu->mem_info;
	do {
		if (pcni->id == chip_id) {
			flag = true;
			break;
		}
		pcni++;
	} while (pcni);

	if (!flag)
		return -ENODEV;

	/*
	 * Add the event offset to the base address.
	 */
	l_config = config & IMC_EVENT_OFFSET_MASK;
	event->hw.event_base = (u64)pcni->vbase + l_config;
	node_id = cpu_to_node(event->cpu);

	/*
	 * Get the imc_pmu_ref struct for this node.
	 * Take the mutex lock and then increment the count of nest pmu events
	 * inited.
	 */
	ref = get_nest_pmu_ref(event->cpu);
	if (!ref)
		return -EINVAL;

	mutex_lock(&ref->lock);
	if (ref->refc == 0) {
		rc = opal_imc_counters_start(OPAL_IMC_COUNTERS_NEST,
					     get_hard_smp_processor_id(event->cpu));
		if (rc) {
			mutex_unlock(&nest_imc_refc[node_id].lock);
			pr_err("nest-imc: Unable to start the counters for node %d\n",
									node_id);
			return rc;
		}
	}
	++ref->refc;
	mutex_unlock(&ref->lock);

	event->destroy = nest_imc_counters_release;
	return 0;
}

static u64 * get_event_base_addr(struct perf_event *event)
{
	/*
	 * Subsequent patch will add code to detect caller imc pmu
	 * and return accordingly.
	 */
	return (u64 *)event->hw.event_base;
}

static u64 imc_read_counter(struct perf_event *event)
{
	u64 *addr, data;

	/*
	 * In-Memory Collection (IMC) counters are free flowing counters.
	 * So we take a snapshot of the counter value on enable and save it
	 * to calculate the delta at later stage to present the event counter
	 * value.
	 */
	addr = get_event_base_addr(event);
	data = be64_to_cpu(READ_ONCE(*addr));
	local64_set(&event->hw.prev_count, data);

	return data;
}

static void imc_event_update(struct perf_event *event)
{
	u64 counter_prev, counter_new, final_count;

	counter_prev = local64_read(&event->hw.prev_count);
	counter_new = imc_read_counter(event);
	final_count = counter_new - counter_prev;

	/* Update the delta to the event count */
	local64_add(final_count, &event->count);
}

static void imc_event_start(struct perf_event *event, int flags)
{
	/*
	 * In Memory Counters are free flowing counters. HW or the microcode
	 * keeps adding to the counter offset in memory. To get event
	 * counter value, we snapshot the value here and we calculate
	 * delta at later point.
	 */
	imc_read_counter(event);
}

static void imc_event_stop(struct perf_event *event, int flags)
{
	/*
	 * Take a snapshot and calculate the delta and update
	 * the event counter values.
	 */
	imc_event_update(event);
}

static int imc_event_add(struct perf_event *event, int flags)
{
	if (flags & PERF_EF_START)
		imc_event_start(event, flags);

	return 0;
}

/* update_pmu_ops : Populate the appropriate operations for "pmu" */
static int update_pmu_ops(struct imc_pmu *pmu)
{
	pmu->pmu.task_ctx_nr = perf_invalid_context;
	pmu->pmu.add = imc_event_add;
	pmu->pmu.del = imc_event_stop;
	pmu->pmu.start = imc_event_start;
	pmu->pmu.stop = imc_event_stop;
	pmu->pmu.read = imc_event_update;
	pmu->pmu.attr_groups = pmu->attr_groups;
	pmu->attr_groups[IMC_FORMAT_ATTR] = &imc_format_group;

	/* Subsequenct patch will add more pmu types here */
	switch (pmu->domain) {
	case IMC_DOMAIN_NEST:
		pmu->pmu.event_init = nest_imc_event_init;
		pmu->attr_groups[IMC_CPUMASK_ATTR] = &imc_pmu_cpumask_attr_group;
		break;
	default:
		break;
	}

	return 0;
}

/* init_nest_pmu_ref: Initialize the imc_pmu_ref struct for all the nodes */
static int init_nest_pmu_ref(void)
{
	int nid, i, cpu;

	nest_imc_refc = kcalloc(num_possible_nodes(), sizeof(*nest_imc_refc),
								GFP_KERNEL);

	if (!nest_imc_refc)
		return -ENOMEM;

	i = 0;
	for_each_node(nid) {
		/*
		 * Mutex lock to avoid races while tracking the number of
		 * sessions using the chip's nest pmu units.
		 */
		mutex_init(&nest_imc_refc[i].lock);

		/*
		 * Loop to init the "id" with the node_id. Variable "i" initialized to
		 * 0 and will be used as index to the array. "i" will not go off the
		 * end of the array since the "for_each_node" loops for "N_POSSIBLE"
		 * nodes only.
		 */
		nest_imc_refc[i++].id = nid;
	}

	/*
	 * Loop to init the per_cpu "local_nest_imc_refc" with the proper
	 * "nest_imc_refc" index. This makes get_nest_pmu_ref() alot simple.
	 */
	for_each_possible_cpu(cpu) {
		nid = cpu_to_node(cpu);
		for_each_online_node(i) {
			if (nest_imc_refc[i].id == nid) {
				per_cpu(local_nest_imc_refc, cpu) = &nest_imc_refc[i];
				break;
			}
		}
	}
	return 0;
}

/*
 * Common function to unregister cpu hotplug callback and
 * free the memory.
 * TODO: Need to handle pmu unregistering, which will be
 * done in followup series.
 */
static void imc_common_cpuhp_mem_free(struct imc_pmu *pmu_ptr)
{
	if (pmu_ptr->domain == IMC_DOMAIN_NEST) {
		mutex_unlock(&nest_init_lock);
		if (nest_pmus == 1) {
			cpuhp_remove_state(CPUHP_AP_PERF_POWERPC_NEST_IMC_ONLINE);
			kfree(nest_imc_refc);
		}

		if (nest_pmus > 0)
			nest_pmus--;
		mutex_unlock(&nest_init_lock);
	}

	/* Only free the attr_groups which are dynamically allocated  */
	kfree(pmu_ptr->attr_groups[IMC_EVENT_ATTR]->attrs);
	kfree(pmu_ptr->attr_groups[IMC_EVENT_ATTR]);
	kfree(pmu_ptr);
	return;
}


/*
 * imc_mem_init : Function to support memory allocation for core imc.
 */
static int imc_mem_init(struct imc_pmu *pmu_ptr, struct device_node *parent,
								int pmu_index)
{
	const char *s;

	if (of_property_read_string(parent, "name", &s))
		return -ENODEV;

	/* Subsequenct patch will add more pmu types here */
	switch (pmu_ptr->domain) {
	case IMC_DOMAIN_NEST:
		/* Update the pmu name */
		pmu_ptr->pmu.name = kasprintf(GFP_KERNEL, "%s%s_imc", "nest_", s);
		if (!pmu_ptr->pmu.name)
			return -ENOMEM;

		/* Needed for hotplug/migration */
		per_nest_pmu_arr[pmu_index] = pmu_ptr;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * init_imc_pmu : Setup and register the IMC pmu device.
 *
 * @parent:	Device tree unit node
 * @pmu_ptr:	memory allocated for this pmu
 * @pmu_idx:	Count of nest pmc registered
 *
 * init_imc_pmu() setup pmu cpumask and registers for a cpu hotplug callback.
 * Handles failure cases and accordingly frees memory.
 */
int init_imc_pmu(struct device_node *parent, struct imc_pmu *pmu_ptr, int pmu_idx)
{
	int ret;

	ret = imc_mem_init(pmu_ptr, parent, pmu_idx);
	if (ret)
		goto err_free;

	/* Subsequenct patch will add more pmu types here */
	switch (pmu_ptr->domain) {
	case IMC_DOMAIN_NEST:
		/*
		* Nest imc pmu need only one cpu per chip, we initialize the
		* cpumask for the first nest imc pmu and use the same for the
		* rest. To handle the cpuhotplug callback unregister, we track
		* the number of nest pmus in "nest_pmus".
		*/
		mutex_lock(&nest_init_lock);
		if (nest_pmus == 0) {
			ret = init_nest_pmu_ref();
			if (ret) {
				mutex_unlock(&nest_init_lock);
				goto err_free;
			}
			/* Register for cpu hotplug notification. */
			ret = nest_pmu_cpumask_init();
			if (ret) {
				mutex_unlock(&nest_init_lock);
				goto err_free;
			}
		}
		nest_pmus++;
		mutex_unlock(&nest_init_lock);
		break;
	default:
		return  -1;	/* Unknown domain */
	}

	ret = update_events_in_group(parent, pmu_ptr);
	if (ret)
		goto err_free;

	ret = update_pmu_ops(pmu_ptr);
	if (ret)
		goto err_free;

	ret = perf_pmu_register(&pmu_ptr->pmu, pmu_ptr->pmu.name, -1);
	if (ret)
		goto err_free;

	pr_info("%s performance monitor hardware support registered\n",
							pmu_ptr->pmu.name);

	return 0;

err_free:
	imc_common_cpuhp_mem_free(pmu_ptr);
	return ret;
}

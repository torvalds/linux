// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * In-Memory Collection (IMC) Performance Monitor counter support.
 *
 * Copyright (C) 2017 Madhavan Srinivasan, IBM Corporation.
 *           (C) 2017 Anju T Sudhakar, IBM Corporation.
 *           (C) 2017 Hemant K Shaw, IBM Corporation.
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
static struct imc_pmu **per_nest_pmu_arr;
static cpumask_t nest_imc_cpumask;
static struct imc_pmu_ref *nest_imc_refc;
static int nest_pmus;

/* Core IMC data structures and variables */

static cpumask_t core_imc_cpumask;
static struct imc_pmu_ref *core_imc_refc;
static struct imc_pmu *core_imc_pmu;

/* Thread IMC data structures and variables */

static DEFINE_PER_CPU(u64 *, thread_imc_mem);
static struct imc_pmu *thread_imc_pmu;
static int thread_imc_mem_size;

/* Trace IMC data structures */
static DEFINE_PER_CPU(u64 *, trace_imc_mem);
static struct imc_pmu_ref *trace_imc_refc;
static int trace_imc_mem_size;

/*
 * Global data structure used to avoid races between thread,
 * core and trace-imc
 */
static struct imc_pmu_ref imc_global_refc = {
	.lock = __MUTEX_INITIALIZER(imc_global_refc.lock),
	.id = 0,
	.refc = 0,
};

static struct imc_pmu *imc_event_to_pmu(struct perf_event *event)
{
	return container_of(event->pmu, struct imc_pmu, pmu);
}

PMU_FORMAT_ATTR(event, "config:0-61");
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

/* Format attribute for imc trace-mode */
PMU_FORMAT_ATTR(cpmc_reserved, "config:0-19");
PMU_FORMAT_ATTR(cpmc_event, "config:20-27");
PMU_FORMAT_ATTR(cpmc_samplesel, "config:28-29");
PMU_FORMAT_ATTR(cpmc_load, "config:30-61");
static struct attribute *trace_imc_format_attrs[] = {
	&format_attr_event.attr,
	&format_attr_cpmc_reserved.attr,
	&format_attr_cpmc_event.attr,
	&format_attr_cpmc_samplesel.attr,
	&format_attr_cpmc_load.attr,
	NULL,
};

static struct attribute_group trace_imc_format_group = {
.name = "format",
.attrs = trace_imc_format_attrs,
};

/* Get the cpumask printed to a buffer "buf" */
static ssize_t imc_pmu_cpumask_get_attr(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct pmu *pmu = dev_get_drvdata(dev);
	struct imc_pmu *imc_pmu = container_of(pmu, struct imc_pmu, pmu);
	cpumask_t *active_mask;

	switch(imc_pmu->domain){
	case IMC_DOMAIN_NEST:
		active_mask = &nest_imc_cpumask;
		break;
	case IMC_DOMAIN_CORE:
		active_mask = &core_imc_cpumask;
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

static int imc_parse_event(struct device_node *np, const char *scale,
				  const char *unit, const char *prefix,
				  u32 base, struct imc_events *event)
{
	const char *s;
	u32 reg;

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

	return 0;
error:
	kfree(event->unit);
	kfree(event->scale);
	kfree(event->name);
	return -EINVAL;
}

/*
 * imc_free_events: Function to cleanup the events list, having
 * 		    "nr_entries".
 */
static void imc_free_events(struct imc_events *events, int nr_entries)
{
	int i;

	/* Nothing to clean, return */
	if (!events)
		return;
	for (i = 0; i < nr_entries; i++) {
		kfree(events[i].unit);
		kfree(events[i].scale);
		kfree(events[i].name);
	}

	kfree(events);
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
	u32 handle, base_reg;
	int i = 0, j = 0, ct, ret;
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
		ret = imc_parse_event(np, g_scale, g_unit, prefix, base_reg, &pmu->events[ct]);
		if (!ret)
			ct++;
	}

	/* Allocate memory for attribute group */
	attr_group = kzalloc(sizeof(*attr_group), GFP_KERNEL);
	if (!attr_group) {
		imc_free_events(pmu->events, ct);
		return -ENOMEM;
	}

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
		imc_free_events(pmu->events, ct);
		return -ENOMEM;
	}

	attr_group->name = "events";
	attr_group->attrs = attrs;
	do {
		ev_val_str = kasprintf(GFP_KERNEL, "event=0x%x", pmu->events[i].value);
		dev_str = device_str_attr_create(pmu->events[i].name, ev_val_str);
		if (!dev_str)
			continue;

		attrs[j++] = dev_str;
		if (pmu->events[i].scale) {
			ev_scale_str = kasprintf(GFP_KERNEL, "%s.scale", pmu->events[i].name);
			dev_str = device_str_attr_create(ev_scale_str, pmu->events[i].scale);
			if (!dev_str)
				continue;

			attrs[j++] = dev_str;
		}

		if (pmu->events[i].unit) {
			ev_unit_str = kasprintf(GFP_KERNEL, "%s.unit", pmu->events[i].name);
			dev_str = device_str_attr_create(ev_unit_str, pmu->events[i].unit);
			if (!dev_str)
				continue;

			attrs[j++] = dev_str;
		}
	} while (++i < ct);

	/* Save the event attribute */
	pmu->attr_groups[IMC_EVENT_ATTR] = attr_group;

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

	if (old_cpu < 0 || new_cpu < 0)
		return;

	while (*pn) {
		perf_pmu_migrate_context(&(*pn)->pmu, old_cpu, new_cpu);
		pn++;
	}
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
	 * Check whether nest_imc is registered. We could end up here if the
	 * cpuhotplug callback registration fails. i.e, callback invokes the
	 * offline path for all successfully registered nodes. At this stage,
	 * nest_imc pmu will not be registered and we should return here.
	 *
	 * We return with a zero since this is not an offline failure. And
	 * cpuhp_setup_state() returns the actual failure reason to the caller,
	 * which in turn will call the cleanup routine.
	 */
	if (!nest_pmus)
		return 0;

	/*
	 * Now that this cpu is one of the designated,
	 * find a next cpu a) which is online and b) in same chip.
	 */
	nid = cpu_to_node(cpu);
	l_cpumask = cpumask_of_node(nid);
	target = cpumask_last(l_cpumask);

	/*
	 * If this(target) is the last cpu in the cpumask for this chip,
	 * check for any possible online cpu in the chip.
	 */
	if (unlikely(target == cpu))
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
	if (ref->refc == 0) {
		/*
		 * The scenario where this is true is, when perf session is
		 * started, followed by offlining of all cpus in a given node.
		 *
		 * In the cpuhotplug offline path, ppc_nest_imc_cpu_offline()
		 * function set the ref->count to zero, if the cpu which is
		 * about to offline is the last cpu in a given node and make
		 * an OPAL call to disable the engine in that node.
		 *
		 */
		mutex_unlock(&ref->lock);
		return;
	}
	ref->refc--;
	if (ref->refc == 0) {
		rc = opal_imc_counters_stop(OPAL_IMC_COUNTERS_NEST,
					    get_hard_smp_processor_id(event->cpu));
		if (rc) {
			mutex_unlock(&ref->lock);
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
	chip_id = cpu_to_chip_id(event->cpu);

	/* Return, if chip_id is not valid */
	if (chip_id < 0)
		return -ENODEV;

	pcni = pmu->mem_info;
	do {
		if (pcni->id == chip_id) {
			flag = true;
			break;
		}
		pcni++;
	} while (pcni->vbase != 0);

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
			mutex_unlock(&ref->lock);
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

/*
 * core_imc_mem_init : Initializes memory for the current core.
 *
 * Uses alloc_pages_node() and uses the returned address as an argument to
 * an opal call to configure the pdbar. The address sent as an argument is
 * converted to physical address before the opal call is made. This is the
 * base address at which the core imc counters are populated.
 */
static int core_imc_mem_init(int cpu, int size)
{
	int nid, rc = 0, core_id = (cpu / threads_per_core);
	struct imc_mem_info *mem_info;
	struct page *page;

	/*
	 * alloc_pages_node() will allocate memory for core in the
	 * local node only.
	 */
	nid = cpu_to_node(cpu);
	mem_info = &core_imc_pmu->mem_info[core_id];
	mem_info->id = core_id;

	/* We need only vbase for core counters */
	page = alloc_pages_node(nid,
				GFP_KERNEL | __GFP_ZERO | __GFP_THISNODE |
				__GFP_NOWARN, get_order(size));
	if (!page)
		return -ENOMEM;
	mem_info->vbase = page_address(page);

	/* Init the mutex */
	core_imc_refc[core_id].id = core_id;
	mutex_init(&core_imc_refc[core_id].lock);

	rc = opal_imc_counters_init(OPAL_IMC_COUNTERS_CORE,
				__pa((void *)mem_info->vbase),
				get_hard_smp_processor_id(cpu));
	if (rc) {
		free_pages((u64)mem_info->vbase, get_order(size));
		mem_info->vbase = NULL;
	}

	return rc;
}

static bool is_core_imc_mem_inited(int cpu)
{
	struct imc_mem_info *mem_info;
	int core_id = (cpu / threads_per_core);

	mem_info = &core_imc_pmu->mem_info[core_id];
	if (!mem_info->vbase)
		return false;

	return true;
}

static int ppc_core_imc_cpu_online(unsigned int cpu)
{
	const struct cpumask *l_cpumask;
	static struct cpumask tmp_mask;
	int ret = 0;

	/* Get the cpumask for this core */
	l_cpumask = cpu_sibling_mask(cpu);

	/* If a cpu for this core is already set, then, don't do anything */
	if (cpumask_and(&tmp_mask, l_cpumask, &core_imc_cpumask))
		return 0;

	if (!is_core_imc_mem_inited(cpu)) {
		ret = core_imc_mem_init(cpu, core_imc_pmu->counter_mem_size);
		if (ret) {
			pr_info("core_imc memory allocation for cpu %d failed\n", cpu);
			return ret;
		}
	}

	/* set the cpu in the mask */
	cpumask_set_cpu(cpu, &core_imc_cpumask);
	return 0;
}

static int ppc_core_imc_cpu_offline(unsigned int cpu)
{
	unsigned int core_id;
	int ncpu;
	struct imc_pmu_ref *ref;

	/*
	 * clear this cpu out of the mask, if not present in the mask,
	 * don't bother doing anything.
	 */
	if (!cpumask_test_and_clear_cpu(cpu, &core_imc_cpumask))
		return 0;

	/*
	 * Check whether core_imc is registered. We could end up here
	 * if the cpuhotplug callback registration fails. i.e, callback
	 * invokes the offline path for all sucessfully registered cpus.
	 * At this stage, core_imc pmu will not be registered and we
	 * should return here.
	 *
	 * We return with a zero since this is not an offline failure.
	 * And cpuhp_setup_state() returns the actual failure reason
	 * to the caller, which inturn will call the cleanup routine.
	 */
	if (!core_imc_pmu->pmu.event_init)
		return 0;

	/* Find any online cpu in that core except the current "cpu" */
	ncpu = cpumask_last(cpu_sibling_mask(cpu));

	if (unlikely(ncpu == cpu))
		ncpu = cpumask_any_but(cpu_sibling_mask(cpu), cpu);

	if (ncpu >= 0 && ncpu < nr_cpu_ids) {
		cpumask_set_cpu(ncpu, &core_imc_cpumask);
		perf_pmu_migrate_context(&core_imc_pmu->pmu, cpu, ncpu);
	} else {
		/*
		 * If this is the last cpu in this core then, skip taking refernce
		 * count mutex lock for this core and directly zero "refc" for
		 * this core.
		 */
		opal_imc_counters_stop(OPAL_IMC_COUNTERS_CORE,
				       get_hard_smp_processor_id(cpu));
		core_id = cpu / threads_per_core;
		ref = &core_imc_refc[core_id];
		if (!ref)
			return -EINVAL;

		ref->refc = 0;
		/*
		 * Reduce the global reference count, if this is the
		 * last cpu in this core and core-imc event running
		 * in this cpu.
		 */
		mutex_lock(&imc_global_refc.lock);
		if (imc_global_refc.id == IMC_DOMAIN_CORE)
			imc_global_refc.refc--;

		mutex_unlock(&imc_global_refc.lock);
	}
	return 0;
}

static int core_imc_pmu_cpumask_init(void)
{
	return cpuhp_setup_state(CPUHP_AP_PERF_POWERPC_CORE_IMC_ONLINE,
				 "perf/powerpc/imc_core:online",
				 ppc_core_imc_cpu_online,
				 ppc_core_imc_cpu_offline);
}

static void reset_global_refc(struct perf_event *event)
{
		mutex_lock(&imc_global_refc.lock);
		imc_global_refc.refc--;

		/*
		 * If no other thread is running any
		 * event for this domain(thread/core/trace),
		 * set the global id to zero.
		 */
		if (imc_global_refc.refc <= 0) {
			imc_global_refc.refc = 0;
			imc_global_refc.id = 0;
		}
		mutex_unlock(&imc_global_refc.lock);
}

static void core_imc_counters_release(struct perf_event *event)
{
	int rc, core_id;
	struct imc_pmu_ref *ref;

	if (event->cpu < 0)
		return;
	/*
	 * See if we need to disable the IMC PMU.
	 * If no events are currently in use, then we have to take a
	 * mutex to ensure that we don't race with another task doing
	 * enable or disable the core counters.
	 */
	core_id = event->cpu / threads_per_core;

	/* Take the mutex lock and decrement the refernce count for this core */
	ref = &core_imc_refc[core_id];
	if (!ref)
		return;

	mutex_lock(&ref->lock);
	if (ref->refc == 0) {
		/*
		 * The scenario where this is true is, when perf session is
		 * started, followed by offlining of all cpus in a given core.
		 *
		 * In the cpuhotplug offline path, ppc_core_imc_cpu_offline()
		 * function set the ref->count to zero, if the cpu which is
		 * about to offline is the last cpu in a given core and make
		 * an OPAL call to disable the engine in that core.
		 *
		 */
		mutex_unlock(&ref->lock);
		return;
	}
	ref->refc--;
	if (ref->refc == 0) {
		rc = opal_imc_counters_stop(OPAL_IMC_COUNTERS_CORE,
					    get_hard_smp_processor_id(event->cpu));
		if (rc) {
			mutex_unlock(&ref->lock);
			pr_err("IMC: Unable to stop the counters for core %d\n", core_id);
			return;
		}
	} else if (ref->refc < 0) {
		WARN(1, "core-imc: Invalid event reference count\n");
		ref->refc = 0;
	}
	mutex_unlock(&ref->lock);

	reset_global_refc(event);
}

static int core_imc_event_init(struct perf_event *event)
{
	int core_id, rc;
	u64 config = event->attr.config;
	struct imc_mem_info *pcmi;
	struct imc_pmu *pmu;
	struct imc_pmu_ref *ref;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* Sampling not supported */
	if (event->hw.sample_period)
		return -EINVAL;

	if (event->cpu < 0)
		return -EINVAL;

	event->hw.idx = -1;
	pmu = imc_event_to_pmu(event);

	/* Sanity check for config (event offset) */
	if (((config & IMC_EVENT_OFFSET_MASK) > pmu->counter_mem_size))
		return -EINVAL;

	if (!is_core_imc_mem_inited(event->cpu))
		return -ENODEV;

	core_id = event->cpu / threads_per_core;
	pcmi = &core_imc_pmu->mem_info[core_id];
	if ((!pcmi->vbase))
		return -ENODEV;

	/* Get the core_imc mutex for this core */
	ref = &core_imc_refc[core_id];
	if (!ref)
		return -EINVAL;

	/*
	 * Core pmu units are enabled only when it is used.
	 * See if this is triggered for the first time.
	 * If yes, take the mutex lock and enable the core counters.
	 * If not, just increment the count in core_imc_refc struct.
	 */
	mutex_lock(&ref->lock);
	if (ref->refc == 0) {
		rc = opal_imc_counters_start(OPAL_IMC_COUNTERS_CORE,
					     get_hard_smp_processor_id(event->cpu));
		if (rc) {
			mutex_unlock(&ref->lock);
			pr_err("core-imc: Unable to start the counters for core %d\n",
									core_id);
			return rc;
		}
	}
	++ref->refc;
	mutex_unlock(&ref->lock);

	/*
	 * Since the system can run either in accumulation or trace-mode
	 * of IMC at a time, core-imc events are allowed only if no other
	 * trace/thread imc events are enabled/monitored.
	 *
	 * Take the global lock, and check the refc.id
	 * to know whether any other trace/thread imc
	 * events are running.
	 */
	mutex_lock(&imc_global_refc.lock);
	if (imc_global_refc.id == 0 || imc_global_refc.id == IMC_DOMAIN_CORE) {
		/*
		 * No other trace/thread imc events are running in
		 * the system, so set the refc.id to core-imc.
		 */
		imc_global_refc.id = IMC_DOMAIN_CORE;
		imc_global_refc.refc++;
	} else {
		mutex_unlock(&imc_global_refc.lock);
		return -EBUSY;
	}
	mutex_unlock(&imc_global_refc.lock);

	event->hw.event_base = (u64)pcmi->vbase + (config & IMC_EVENT_OFFSET_MASK);
	event->destroy = core_imc_counters_release;
	return 0;
}

/*
 * Allocates a page of memory for each of the online cpus, and load
 * LDBAR with 0.
 * The physical base address of the page allocated for a cpu will be
 * written to the LDBAR for that cpu, when the thread-imc event
 * is added.
 *
 * LDBAR Register Layout:
 *
 *  0          4         8         12        16        20        24        28
 * | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - |
 *   | |       [   ]    [                   Counter Address [8:50]
 *   | * Mode    |
 *   |           * PB Scope
 *   * Enable/Disable
 *
 *  32        36        40        44        48        52        56        60
 * | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - | - - - - |
 *           Counter Address [8:50]              ]
 *
 */
static int thread_imc_mem_alloc(int cpu_id, int size)
{
	u64 *local_mem = per_cpu(thread_imc_mem, cpu_id);
	int nid = cpu_to_node(cpu_id);

	if (!local_mem) {
		struct page *page;
		/*
		 * This case could happen only once at start, since we dont
		 * free the memory in cpu offline path.
		 */
		page = alloc_pages_node(nid,
				  GFP_KERNEL | __GFP_ZERO | __GFP_THISNODE |
				  __GFP_NOWARN, get_order(size));
		if (!page)
			return -ENOMEM;
		local_mem = page_address(page);

		per_cpu(thread_imc_mem, cpu_id) = local_mem;
	}

	mtspr(SPRN_LDBAR, 0);
	return 0;
}

static int ppc_thread_imc_cpu_online(unsigned int cpu)
{
	return thread_imc_mem_alloc(cpu, thread_imc_mem_size);
}

static int ppc_thread_imc_cpu_offline(unsigned int cpu)
{
	/*
	 * Set the bit 0 of LDBAR to zero.
	 *
	 * If bit 0 of LDBAR is unset, it will stop posting
	 * the counter data to memory.
	 * For thread-imc, bit 0 of LDBAR will be set to 1 in the
	 * event_add function. So reset this bit here, to stop the updates
	 * to memory in the cpu_offline path.
	 */
	mtspr(SPRN_LDBAR, (mfspr(SPRN_LDBAR) & (~(1UL << 63))));

	/* Reduce the refc if thread-imc event running on this cpu */
	mutex_lock(&imc_global_refc.lock);
	if (imc_global_refc.id == IMC_DOMAIN_THREAD)
		imc_global_refc.refc--;
	mutex_unlock(&imc_global_refc.lock);

	return 0;
}

static int thread_imc_cpu_init(void)
{
	return cpuhp_setup_state(CPUHP_AP_PERF_POWERPC_THREAD_IMC_ONLINE,
			  "perf/powerpc/imc_thread:online",
			  ppc_thread_imc_cpu_online,
			  ppc_thread_imc_cpu_offline);
}

static int thread_imc_event_init(struct perf_event *event)
{
	u32 config = event->attr.config;
	struct task_struct *target;
	struct imc_pmu *pmu;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (!perfmon_capable())
		return -EACCES;

	/* Sampling not supported */
	if (event->hw.sample_period)
		return -EINVAL;

	event->hw.idx = -1;
	pmu = imc_event_to_pmu(event);

	/* Sanity check for config offset */
	if (((config & IMC_EVENT_OFFSET_MASK) > pmu->counter_mem_size))
		return -EINVAL;

	target = event->hw.target;
	if (!target)
		return -EINVAL;

	mutex_lock(&imc_global_refc.lock);
	/*
	 * Check if any other trace/core imc events are running in the
	 * system, if not set the global id to thread-imc.
	 */
	if (imc_global_refc.id == 0 || imc_global_refc.id == IMC_DOMAIN_THREAD) {
		imc_global_refc.id = IMC_DOMAIN_THREAD;
		imc_global_refc.refc++;
	} else {
		mutex_unlock(&imc_global_refc.lock);
		return -EBUSY;
	}
	mutex_unlock(&imc_global_refc.lock);

	event->pmu->task_ctx_nr = perf_sw_context;
	event->destroy = reset_global_refc;
	return 0;
}

static bool is_thread_imc_pmu(struct perf_event *event)
{
	if (!strncmp(event->pmu->name, "thread_imc", strlen("thread_imc")))
		return true;

	return false;
}

static u64 * get_event_base_addr(struct perf_event *event)
{
	u64 addr;

	if (is_thread_imc_pmu(event)) {
		addr = (u64)per_cpu(thread_imc_mem, smp_processor_id());
		return (u64 *)(addr + (event->attr.config & IMC_EVENT_OFFSET_MASK));
	}

	return (u64 *)event->hw.event_base;
}

static void thread_imc_pmu_start_txn(struct pmu *pmu,
				     unsigned int txn_flags)
{
	if (txn_flags & ~PERF_PMU_TXN_ADD)
		return;
	perf_pmu_disable(pmu);
}

static void thread_imc_pmu_cancel_txn(struct pmu *pmu)
{
	perf_pmu_enable(pmu);
}

static int thread_imc_pmu_commit_txn(struct pmu *pmu)
{
	perf_pmu_enable(pmu);
	return 0;
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

static int thread_imc_event_add(struct perf_event *event, int flags)
{
	int core_id;
	struct imc_pmu_ref *ref;
	u64 ldbar_value, *local_mem = per_cpu(thread_imc_mem, smp_processor_id());

	if (flags & PERF_EF_START)
		imc_event_start(event, flags);

	if (!is_core_imc_mem_inited(smp_processor_id()))
		return -EINVAL;

	core_id = smp_processor_id() / threads_per_core;
	ldbar_value = ((u64)local_mem & THREAD_IMC_LDBAR_MASK) | THREAD_IMC_ENABLE;
	mtspr(SPRN_LDBAR, ldbar_value);

	/*
	 * imc pmus are enabled only when it is used.
	 * See if this is triggered for the first time.
	 * If yes, take the mutex lock and enable the counters.
	 * If not, just increment the count in ref count struct.
	 */
	ref = &core_imc_refc[core_id];
	if (!ref)
		return -EINVAL;

	mutex_lock(&ref->lock);
	if (ref->refc == 0) {
		if (opal_imc_counters_start(OPAL_IMC_COUNTERS_CORE,
		    get_hard_smp_processor_id(smp_processor_id()))) {
			mutex_unlock(&ref->lock);
			pr_err("thread-imc: Unable to start the counter\
				for core %d\n", core_id);
			return -EINVAL;
		}
	}
	++ref->refc;
	mutex_unlock(&ref->lock);
	return 0;
}

static void thread_imc_event_del(struct perf_event *event, int flags)
{

	int core_id;
	struct imc_pmu_ref *ref;

	core_id = smp_processor_id() / threads_per_core;
	ref = &core_imc_refc[core_id];
	if (!ref) {
		pr_debug("imc: Failed to get event reference count\n");
		return;
	}

	mutex_lock(&ref->lock);
	ref->refc--;
	if (ref->refc == 0) {
		if (opal_imc_counters_stop(OPAL_IMC_COUNTERS_CORE,
		    get_hard_smp_processor_id(smp_processor_id()))) {
			mutex_unlock(&ref->lock);
			pr_err("thread-imc: Unable to stop the counters\
				for core %d\n", core_id);
			return;
		}
	} else if (ref->refc < 0) {
		ref->refc = 0;
	}
	mutex_unlock(&ref->lock);

	/* Set bit 0 of LDBAR to zero, to stop posting updates to memory */
	mtspr(SPRN_LDBAR, (mfspr(SPRN_LDBAR) & (~(1UL << 63))));

	/*
	 * Take a snapshot and calculate the delta and update
	 * the event counter values.
	 */
	imc_event_update(event);
}

/*
 * Allocate a page of memory for each cpu, and load LDBAR with 0.
 */
static int trace_imc_mem_alloc(int cpu_id, int size)
{
	u64 *local_mem = per_cpu(trace_imc_mem, cpu_id);
	int phys_id = cpu_to_node(cpu_id), rc = 0;
	int core_id = (cpu_id / threads_per_core);

	if (!local_mem) {
		struct page *page;

		page = alloc_pages_node(phys_id,
				GFP_KERNEL | __GFP_ZERO | __GFP_THISNODE |
				__GFP_NOWARN, get_order(size));
		if (!page)
			return -ENOMEM;
		local_mem = page_address(page);
		per_cpu(trace_imc_mem, cpu_id) = local_mem;

		/* Initialise the counters for trace mode */
		rc = opal_imc_counters_init(OPAL_IMC_COUNTERS_TRACE, __pa((void *)local_mem),
					    get_hard_smp_processor_id(cpu_id));
		if (rc) {
			pr_info("IMC:opal init failed for trace imc\n");
			return rc;
		}
	}

	/* Init the mutex, if not already */
	trace_imc_refc[core_id].id = core_id;
	mutex_init(&trace_imc_refc[core_id].lock);

	mtspr(SPRN_LDBAR, 0);
	return 0;
}

static int ppc_trace_imc_cpu_online(unsigned int cpu)
{
	return trace_imc_mem_alloc(cpu, trace_imc_mem_size);
}

static int ppc_trace_imc_cpu_offline(unsigned int cpu)
{
	/*
	 * No need to set bit 0 of LDBAR to zero, as
	 * it is set to zero for imc trace-mode
	 *
	 * Reduce the refc if any trace-imc event running
	 * on this cpu.
	 */
	mutex_lock(&imc_global_refc.lock);
	if (imc_global_refc.id == IMC_DOMAIN_TRACE)
		imc_global_refc.refc--;
	mutex_unlock(&imc_global_refc.lock);

	return 0;
}

static int trace_imc_cpu_init(void)
{
	return cpuhp_setup_state(CPUHP_AP_PERF_POWERPC_TRACE_IMC_ONLINE,
			  "perf/powerpc/imc_trace:online",
			  ppc_trace_imc_cpu_online,
			  ppc_trace_imc_cpu_offline);
}

static u64 get_trace_imc_event_base_addr(void)
{
	return (u64)per_cpu(trace_imc_mem, smp_processor_id());
}

/*
 * Function to parse trace-imc data obtained
 * and to prepare the perf sample.
 */
static int trace_imc_prepare_sample(struct trace_imc_data *mem,
				    struct perf_sample_data *data,
				    u64 *prev_tb,
				    struct perf_event_header *header,
				    struct perf_event *event)
{
	/* Sanity checks for a valid record */
	if (be64_to_cpu(READ_ONCE(mem->tb1)) > *prev_tb)
		*prev_tb = be64_to_cpu(READ_ONCE(mem->tb1));
	else
		return -EINVAL;

	if ((be64_to_cpu(READ_ONCE(mem->tb1)) & IMC_TRACE_RECORD_TB1_MASK) !=
			 be64_to_cpu(READ_ONCE(mem->tb2)))
		return -EINVAL;

	/* Prepare perf sample */
	data->ip =  be64_to_cpu(READ_ONCE(mem->ip));
	data->period = event->hw.last_period;

	header->type = PERF_RECORD_SAMPLE;
	header->size = sizeof(*header) + event->header_size;
	header->misc = 0;

	if (cpu_has_feature(CPU_FTR_ARCH_31)) {
		switch (IMC_TRACE_RECORD_VAL_HVPR(be64_to_cpu(READ_ONCE(mem->val)))) {
		case 0:/* when MSR HV and PR not set in the trace-record */
			header->misc |= PERF_RECORD_MISC_GUEST_KERNEL;
			break;
		case 1: /* MSR HV is 0 and PR is 1 */
			header->misc |= PERF_RECORD_MISC_GUEST_USER;
			break;
		case 2: /* MSR HV is 1 and PR is 0 */
			header->misc |= PERF_RECORD_MISC_KERNEL;
			break;
		case 3: /* MSR HV is 1 and PR is 1 */
			header->misc |= PERF_RECORD_MISC_USER;
			break;
		default:
			pr_info("IMC: Unable to set the flag based on MSR bits\n");
			break;
		}
	} else {
		if (is_kernel_addr(data->ip))
			header->misc |= PERF_RECORD_MISC_KERNEL;
		else
			header->misc |= PERF_RECORD_MISC_USER;
	}
	perf_event_header__init_id(header, data, event);

	return 0;
}

static void dump_trace_imc_data(struct perf_event *event)
{
	struct trace_imc_data *mem;
	int i, ret;
	u64 prev_tb = 0;

	mem = (struct trace_imc_data *)get_trace_imc_event_base_addr();
	for (i = 0; i < (trace_imc_mem_size / sizeof(struct trace_imc_data));
		i++, mem++) {
		struct perf_sample_data data;
		struct perf_event_header header;

		ret = trace_imc_prepare_sample(mem, &data, &prev_tb, &header, event);
		if (ret) /* Exit, if not a valid record */
			break;
		else {
			/* If this is a valid record, create the sample */
			struct perf_output_handle handle;

			if (perf_output_begin(&handle, event, header.size))
				return;

			perf_output_sample(&handle, &header, &data, event);
			perf_output_end(&handle);
		}
	}
}

static int trace_imc_event_add(struct perf_event *event, int flags)
{
	int core_id = smp_processor_id() / threads_per_core;
	struct imc_pmu_ref *ref = NULL;
	u64 local_mem, ldbar_value;

	/* Set trace-imc bit in ldbar and load ldbar with per-thread memory address */
	local_mem = get_trace_imc_event_base_addr();
	ldbar_value = ((u64)local_mem & THREAD_IMC_LDBAR_MASK) | TRACE_IMC_ENABLE;

	/* trace-imc reference count */
	if (trace_imc_refc)
		ref = &trace_imc_refc[core_id];
	if (!ref) {
		pr_debug("imc: Failed to get the event reference count\n");
		return -EINVAL;
	}

	mtspr(SPRN_LDBAR, ldbar_value);
	mutex_lock(&ref->lock);
	if (ref->refc == 0) {
		if (opal_imc_counters_start(OPAL_IMC_COUNTERS_TRACE,
				get_hard_smp_processor_id(smp_processor_id()))) {
			mutex_unlock(&ref->lock);
			pr_err("trace-imc: Unable to start the counters for core %d\n", core_id);
			return -EINVAL;
		}
	}
	++ref->refc;
	mutex_unlock(&ref->lock);
	return 0;
}

static void trace_imc_event_read(struct perf_event *event)
{
	return;
}

static void trace_imc_event_stop(struct perf_event *event, int flags)
{
	u64 local_mem = get_trace_imc_event_base_addr();
	dump_trace_imc_data(event);
	memset((void *)local_mem, 0, sizeof(u64));
}

static void trace_imc_event_start(struct perf_event *event, int flags)
{
	return;
}

static void trace_imc_event_del(struct perf_event *event, int flags)
{
	int core_id = smp_processor_id() / threads_per_core;
	struct imc_pmu_ref *ref = NULL;

	if (trace_imc_refc)
		ref = &trace_imc_refc[core_id];
	if (!ref) {
		pr_debug("imc: Failed to get event reference count\n");
		return;
	}

	mutex_lock(&ref->lock);
	ref->refc--;
	if (ref->refc == 0) {
		if (opal_imc_counters_stop(OPAL_IMC_COUNTERS_TRACE,
				get_hard_smp_processor_id(smp_processor_id()))) {
			mutex_unlock(&ref->lock);
			pr_err("trace-imc: Unable to stop the counters for core %d\n", core_id);
			return;
		}
	} else if (ref->refc < 0) {
		ref->refc = 0;
	}
	mutex_unlock(&ref->lock);

	trace_imc_event_stop(event, flags);
}

static int trace_imc_event_init(struct perf_event *event)
{
	struct task_struct *target;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (!perfmon_capable())
		return -EACCES;

	/* Return if this is a couting event */
	if (event->attr.sample_period == 0)
		return -ENOENT;

	/*
	 * Take the global lock, and make sure
	 * no other thread is running any core/thread imc
	 * events
	 */
	mutex_lock(&imc_global_refc.lock);
	if (imc_global_refc.id == 0 || imc_global_refc.id == IMC_DOMAIN_TRACE) {
		/*
		 * No core/thread imc events are running in the
		 * system, so set the refc.id to trace-imc.
		 */
		imc_global_refc.id = IMC_DOMAIN_TRACE;
		imc_global_refc.refc++;
	} else {
		mutex_unlock(&imc_global_refc.lock);
		return -EBUSY;
	}
	mutex_unlock(&imc_global_refc.lock);

	event->hw.idx = -1;
	target = event->hw.target;

	event->pmu->task_ctx_nr = perf_hw_context;
	event->destroy = reset_global_refc;
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
	pmu->pmu.capabilities = PERF_PMU_CAP_NO_EXCLUDE;
	pmu->attr_groups[IMC_FORMAT_ATTR] = &imc_format_group;

	switch (pmu->domain) {
	case IMC_DOMAIN_NEST:
		pmu->pmu.event_init = nest_imc_event_init;
		pmu->attr_groups[IMC_CPUMASK_ATTR] = &imc_pmu_cpumask_attr_group;
		break;
	case IMC_DOMAIN_CORE:
		pmu->pmu.event_init = core_imc_event_init;
		pmu->attr_groups[IMC_CPUMASK_ATTR] = &imc_pmu_cpumask_attr_group;
		break;
	case IMC_DOMAIN_THREAD:
		pmu->pmu.event_init = thread_imc_event_init;
		pmu->pmu.add = thread_imc_event_add;
		pmu->pmu.del = thread_imc_event_del;
		pmu->pmu.start_txn = thread_imc_pmu_start_txn;
		pmu->pmu.cancel_txn = thread_imc_pmu_cancel_txn;
		pmu->pmu.commit_txn = thread_imc_pmu_commit_txn;
		break;
	case IMC_DOMAIN_TRACE:
		pmu->pmu.event_init = trace_imc_event_init;
		pmu->pmu.add = trace_imc_event_add;
		pmu->pmu.del = trace_imc_event_del;
		pmu->pmu.start = trace_imc_event_start;
		pmu->pmu.stop = trace_imc_event_stop;
		pmu->pmu.read = trace_imc_event_read;
		pmu->attr_groups[IMC_FORMAT_ATTR] = &trace_imc_format_group;
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
		for (i = 0; i < num_possible_nodes(); i++) {
			if (nest_imc_refc[i].id == nid) {
				per_cpu(local_nest_imc_refc, cpu) = &nest_imc_refc[i];
				break;
			}
		}
	}
	return 0;
}

static void cleanup_all_core_imc_memory(void)
{
	int i, nr_cores = DIV_ROUND_UP(num_possible_cpus(), threads_per_core);
	struct imc_mem_info *ptr = core_imc_pmu->mem_info;
	int size = core_imc_pmu->counter_mem_size;

	/* mem_info will never be NULL */
	for (i = 0; i < nr_cores; i++) {
		if (ptr[i].vbase)
			free_pages((u64)ptr[i].vbase, get_order(size));
	}

	kfree(ptr);
	kfree(core_imc_refc);
}

static void thread_imc_ldbar_disable(void *dummy)
{
	/*
	 * By setting 0th bit of LDBAR to zero, we disable thread-imc
	 * updates to memory.
	 */
	mtspr(SPRN_LDBAR, (mfspr(SPRN_LDBAR) & (~(1UL << 63))));
}

void thread_imc_disable(void)
{
	on_each_cpu(thread_imc_ldbar_disable, NULL, 1);
}

static void cleanup_all_thread_imc_memory(void)
{
	int i, order = get_order(thread_imc_mem_size);

	for_each_online_cpu(i) {
		if (per_cpu(thread_imc_mem, i))
			free_pages((u64)per_cpu(thread_imc_mem, i), order);

	}
}

static void cleanup_all_trace_imc_memory(void)
{
	int i, order = get_order(trace_imc_mem_size);

	for_each_online_cpu(i) {
		if (per_cpu(trace_imc_mem, i))
			free_pages((u64)per_cpu(trace_imc_mem, i), order);

	}
	kfree(trace_imc_refc);
}

/* Function to free the attr_groups which are dynamically allocated */
static void imc_common_mem_free(struct imc_pmu *pmu_ptr)
{
	if (pmu_ptr->attr_groups[IMC_EVENT_ATTR])
		kfree(pmu_ptr->attr_groups[IMC_EVENT_ATTR]->attrs);
	kfree(pmu_ptr->attr_groups[IMC_EVENT_ATTR]);
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
		mutex_lock(&nest_init_lock);
		if (nest_pmus == 1) {
			cpuhp_remove_state(CPUHP_AP_PERF_POWERPC_NEST_IMC_ONLINE);
			kfree(nest_imc_refc);
			kfree(per_nest_pmu_arr);
			per_nest_pmu_arr = NULL;
		}

		if (nest_pmus > 0)
			nest_pmus--;
		mutex_unlock(&nest_init_lock);
	}

	/* Free core_imc memory */
	if (pmu_ptr->domain == IMC_DOMAIN_CORE) {
		cpuhp_remove_state(CPUHP_AP_PERF_POWERPC_CORE_IMC_ONLINE);
		cleanup_all_core_imc_memory();
	}

	/* Free thread_imc memory */
	if (pmu_ptr->domain == IMC_DOMAIN_THREAD) {
		cpuhp_remove_state(CPUHP_AP_PERF_POWERPC_THREAD_IMC_ONLINE);
		cleanup_all_thread_imc_memory();
	}

	if (pmu_ptr->domain == IMC_DOMAIN_TRACE) {
		cpuhp_remove_state(CPUHP_AP_PERF_POWERPC_TRACE_IMC_ONLINE);
		cleanup_all_trace_imc_memory();
	}
}

/*
 * Function to unregister thread-imc if core-imc
 * is not registered.
 */
void unregister_thread_imc(void)
{
	imc_common_cpuhp_mem_free(thread_imc_pmu);
	imc_common_mem_free(thread_imc_pmu);
	perf_pmu_unregister(&thread_imc_pmu->pmu);
}

/*
 * imc_mem_init : Function to support memory allocation for core imc.
 */
static int imc_mem_init(struct imc_pmu *pmu_ptr, struct device_node *parent,
								int pmu_index)
{
	const char *s;
	int nr_cores, cpu, res = -ENOMEM;

	if (of_property_read_string(parent, "name", &s))
		return -ENODEV;

	switch (pmu_ptr->domain) {
	case IMC_DOMAIN_NEST:
		/* Update the pmu name */
		pmu_ptr->pmu.name = kasprintf(GFP_KERNEL, "%s%s_imc", "nest_", s);
		if (!pmu_ptr->pmu.name)
			goto err;

		/* Needed for hotplug/migration */
		if (!per_nest_pmu_arr) {
			per_nest_pmu_arr = kcalloc(get_max_nest_dev() + 1,
						sizeof(struct imc_pmu *),
						GFP_KERNEL);
			if (!per_nest_pmu_arr)
				goto err;
		}
		per_nest_pmu_arr[pmu_index] = pmu_ptr;
		break;
	case IMC_DOMAIN_CORE:
		/* Update the pmu name */
		pmu_ptr->pmu.name = kasprintf(GFP_KERNEL, "%s%s", s, "_imc");
		if (!pmu_ptr->pmu.name)
			goto err;

		nr_cores = DIV_ROUND_UP(num_possible_cpus(), threads_per_core);
		pmu_ptr->mem_info = kcalloc(nr_cores, sizeof(struct imc_mem_info),
								GFP_KERNEL);

		if (!pmu_ptr->mem_info)
			goto err;

		core_imc_refc = kcalloc(nr_cores, sizeof(struct imc_pmu_ref),
								GFP_KERNEL);

		if (!core_imc_refc) {
			kfree(pmu_ptr->mem_info);
			goto err;
		}

		core_imc_pmu = pmu_ptr;
		break;
	case IMC_DOMAIN_THREAD:
		/* Update the pmu name */
		pmu_ptr->pmu.name = kasprintf(GFP_KERNEL, "%s%s", s, "_imc");
		if (!pmu_ptr->pmu.name)
			goto err;

		thread_imc_mem_size = pmu_ptr->counter_mem_size;
		for_each_online_cpu(cpu) {
			res = thread_imc_mem_alloc(cpu, pmu_ptr->counter_mem_size);
			if (res) {
				cleanup_all_thread_imc_memory();
				goto err;
			}
		}

		thread_imc_pmu = pmu_ptr;
		break;
	case IMC_DOMAIN_TRACE:
		/* Update the pmu name */
		pmu_ptr->pmu.name = kasprintf(GFP_KERNEL, "%s%s", s, "_imc");
		if (!pmu_ptr->pmu.name)
			return -ENOMEM;

		nr_cores = DIV_ROUND_UP(num_possible_cpus(), threads_per_core);
		trace_imc_refc = kcalloc(nr_cores, sizeof(struct imc_pmu_ref),
								GFP_KERNEL);
		if (!trace_imc_refc)
			return -ENOMEM;

		trace_imc_mem_size = pmu_ptr->counter_mem_size;
		for_each_online_cpu(cpu) {
			res = trace_imc_mem_alloc(cpu, trace_imc_mem_size);
			if (res) {
				cleanup_all_trace_imc_memory();
				goto err;
			}
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
err:
	return res;
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
		goto err_free_mem;

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
				kfree(per_nest_pmu_arr);
				per_nest_pmu_arr = NULL;
				goto err_free_mem;
			}
			/* Register for cpu hotplug notification. */
			ret = nest_pmu_cpumask_init();
			if (ret) {
				mutex_unlock(&nest_init_lock);
				kfree(nest_imc_refc);
				kfree(per_nest_pmu_arr);
				per_nest_pmu_arr = NULL;
				goto err_free_mem;
			}
		}
		nest_pmus++;
		mutex_unlock(&nest_init_lock);
		break;
	case IMC_DOMAIN_CORE:
		ret = core_imc_pmu_cpumask_init();
		if (ret) {
			cleanup_all_core_imc_memory();
			goto err_free_mem;
		}

		break;
	case IMC_DOMAIN_THREAD:
		ret = thread_imc_cpu_init();
		if (ret) {
			cleanup_all_thread_imc_memory();
			goto err_free_mem;
		}

		break;
	case IMC_DOMAIN_TRACE:
		ret = trace_imc_cpu_init();
		if (ret) {
			cleanup_all_trace_imc_memory();
			goto err_free_mem;
		}

		break;
	default:
		return  -EINVAL;	/* Unknown domain */
	}

	ret = update_events_in_group(parent, pmu_ptr);
	if (ret)
		goto err_free_cpuhp_mem;

	ret = update_pmu_ops(pmu_ptr);
	if (ret)
		goto err_free_cpuhp_mem;

	ret = perf_pmu_register(&pmu_ptr->pmu, pmu_ptr->pmu.name, -1);
	if (ret)
		goto err_free_cpuhp_mem;

	pr_debug("%s performance monitor hardware support registered\n",
							pmu_ptr->pmu.name);

	return 0;

err_free_cpuhp_mem:
	imc_common_cpuhp_mem_free(pmu_ptr);
err_free_mem:
	imc_common_mem_free(pmu_ptr);
	return ret;
}

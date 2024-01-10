// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * nd_perf.c: NVDIMM Device Performance Monitoring Unit support
 *
 * Perf interface to expose nvdimm performance stats.
 *
 * Copyright (C) 2021 IBM Corporation
 */

#define pr_fmt(fmt) "nvdimm_pmu: " fmt

#include <linux/nd.h>
#include <linux/platform_device.h>

#define EVENT(_name, _code)     enum{_name = _code}

/*
 * NVDIMM Events codes.
 */

/* Controller Reset Count */
EVENT(CTL_RES_CNT,		0x1);
/* Controller Reset Elapsed Time */
EVENT(CTL_RES_TM,		0x2);
/* Power-on Seconds */
EVENT(POWERON_SECS,		0x3);
/* Life Remaining */
EVENT(MEM_LIFE,		0x4);
/* Critical Resource Utilization */
EVENT(CRI_RES_UTIL,		0x5);
/* Host Load Count */
EVENT(HOST_L_CNT,		0x6);
/* Host Store Count */
EVENT(HOST_S_CNT,		0x7);
/* Host Store Duration */
EVENT(HOST_S_DUR,		0x8);
/* Host Load Duration */
EVENT(HOST_L_DUR,		0x9);
/* Media Read Count */
EVENT(MED_R_CNT,		0xa);
/* Media Write Count */
EVENT(MED_W_CNT,		0xb);
/* Media Read Duration */
EVENT(MED_R_DUR,		0xc);
/* Media Write Duration */
EVENT(MED_W_DUR,		0xd);
/* Cache Read Hit Count */
EVENT(CACHE_RH_CNT,		0xe);
/* Cache Write Hit Count */
EVENT(CACHE_WH_CNT,		0xf);
/* Fast Write Count */
EVENT(FAST_W_CNT,		0x10);

NVDIMM_EVENT_ATTR(ctl_res_cnt,		CTL_RES_CNT);
NVDIMM_EVENT_ATTR(ctl_res_tm,		CTL_RES_TM);
NVDIMM_EVENT_ATTR(poweron_secs,		POWERON_SECS);
NVDIMM_EVENT_ATTR(mem_life,		MEM_LIFE);
NVDIMM_EVENT_ATTR(cri_res_util,		CRI_RES_UTIL);
NVDIMM_EVENT_ATTR(host_l_cnt,		HOST_L_CNT);
NVDIMM_EVENT_ATTR(host_s_cnt,		HOST_S_CNT);
NVDIMM_EVENT_ATTR(host_s_dur,		HOST_S_DUR);
NVDIMM_EVENT_ATTR(host_l_dur,		HOST_L_DUR);
NVDIMM_EVENT_ATTR(med_r_cnt,		MED_R_CNT);
NVDIMM_EVENT_ATTR(med_w_cnt,		MED_W_CNT);
NVDIMM_EVENT_ATTR(med_r_dur,		MED_R_DUR);
NVDIMM_EVENT_ATTR(med_w_dur,		MED_W_DUR);
NVDIMM_EVENT_ATTR(cache_rh_cnt,		CACHE_RH_CNT);
NVDIMM_EVENT_ATTR(cache_wh_cnt,		CACHE_WH_CNT);
NVDIMM_EVENT_ATTR(fast_w_cnt,		FAST_W_CNT);

static struct attribute *nvdimm_events_attr[] = {
	NVDIMM_EVENT_PTR(CTL_RES_CNT),
	NVDIMM_EVENT_PTR(CTL_RES_TM),
	NVDIMM_EVENT_PTR(POWERON_SECS),
	NVDIMM_EVENT_PTR(MEM_LIFE),
	NVDIMM_EVENT_PTR(CRI_RES_UTIL),
	NVDIMM_EVENT_PTR(HOST_L_CNT),
	NVDIMM_EVENT_PTR(HOST_S_CNT),
	NVDIMM_EVENT_PTR(HOST_S_DUR),
	NVDIMM_EVENT_PTR(HOST_L_DUR),
	NVDIMM_EVENT_PTR(MED_R_CNT),
	NVDIMM_EVENT_PTR(MED_W_CNT),
	NVDIMM_EVENT_PTR(MED_R_DUR),
	NVDIMM_EVENT_PTR(MED_W_DUR),
	NVDIMM_EVENT_PTR(CACHE_RH_CNT),
	NVDIMM_EVENT_PTR(CACHE_WH_CNT),
	NVDIMM_EVENT_PTR(FAST_W_CNT),
	NULL
};

static struct attribute_group nvdimm_pmu_events_group = {
	.name = "events",
	.attrs = nvdimm_events_attr,
};

PMU_FORMAT_ATTR(event, "config:0-4");

static struct attribute *nvdimm_pmu_format_attr[] = {
	&format_attr_event.attr,
	NULL,
};

static struct attribute_group nvdimm_pmu_format_group = {
	.name = "format",
	.attrs = nvdimm_pmu_format_attr,
};

ssize_t nvdimm_events_sysfs_show(struct device *dev,
				 struct device_attribute *attr, char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);

	return sprintf(page, "event=0x%02llx\n", pmu_attr->id);
}

static ssize_t nvdimm_pmu_cpumask_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct pmu *pmu = dev_get_drvdata(dev);
	struct nvdimm_pmu *nd_pmu;

	nd_pmu = container_of(pmu, struct nvdimm_pmu, pmu);

	return cpumap_print_to_pagebuf(true, buf, cpumask_of(nd_pmu->cpu));
}

static int nvdimm_pmu_cpu_offline(unsigned int cpu, struct hlist_node *node)
{
	struct nvdimm_pmu *nd_pmu;
	u32 target;
	int nodeid;
	const struct cpumask *cpumask;

	nd_pmu = hlist_entry_safe(node, struct nvdimm_pmu, node);

	/* Clear it, incase given cpu is set in nd_pmu->arch_cpumask */
	cpumask_test_and_clear_cpu(cpu, &nd_pmu->arch_cpumask);

	/*
	 * If given cpu is not same as current designated cpu for
	 * counter access, just return.
	 */
	if (cpu != nd_pmu->cpu)
		return 0;

	/* Check for any active cpu in nd_pmu->arch_cpumask */
	target = cpumask_any(&nd_pmu->arch_cpumask);

	/*
	 * Incase we don't have any active cpu in nd_pmu->arch_cpumask,
	 * check in given cpu's numa node list.
	 */
	if (target >= nr_cpu_ids) {
		nodeid = cpu_to_node(cpu);
		cpumask = cpumask_of_node(nodeid);
		target = cpumask_any_but(cpumask, cpu);
	}
	nd_pmu->cpu = target;

	/* Migrate nvdimm pmu events to the new target cpu if valid */
	if (target >= 0 && target < nr_cpu_ids)
		perf_pmu_migrate_context(&nd_pmu->pmu, cpu, target);

	return 0;
}

static int nvdimm_pmu_cpu_online(unsigned int cpu, struct hlist_node *node)
{
	struct nvdimm_pmu *nd_pmu;

	nd_pmu = hlist_entry_safe(node, struct nvdimm_pmu, node);

	if (nd_pmu->cpu >= nr_cpu_ids)
		nd_pmu->cpu = cpu;

	return 0;
}

static int create_cpumask_attr_group(struct nvdimm_pmu *nd_pmu)
{
	struct perf_pmu_events_attr *pmu_events_attr;
	struct attribute **attrs_group;
	struct attribute_group *nvdimm_pmu_cpumask_group;

	pmu_events_attr = kzalloc(sizeof(*pmu_events_attr), GFP_KERNEL);
	if (!pmu_events_attr)
		return -ENOMEM;

	attrs_group = kzalloc(2 * sizeof(struct attribute *), GFP_KERNEL);
	if (!attrs_group) {
		kfree(pmu_events_attr);
		return -ENOMEM;
	}

	/* Allocate memory for cpumask attribute group */
	nvdimm_pmu_cpumask_group = kzalloc(sizeof(*nvdimm_pmu_cpumask_group), GFP_KERNEL);
	if (!nvdimm_pmu_cpumask_group) {
		kfree(pmu_events_attr);
		kfree(attrs_group);
		return -ENOMEM;
	}

	sysfs_attr_init(&pmu_events_attr->attr.attr);
	pmu_events_attr->attr.attr.name = "cpumask";
	pmu_events_attr->attr.attr.mode = 0444;
	pmu_events_attr->attr.show = nvdimm_pmu_cpumask_show;
	attrs_group[0] = &pmu_events_attr->attr.attr;
	attrs_group[1] = NULL;

	nvdimm_pmu_cpumask_group->attrs = attrs_group;
	nd_pmu->pmu.attr_groups[NVDIMM_PMU_CPUMASK_ATTR] = nvdimm_pmu_cpumask_group;
	return 0;
}

static int nvdimm_pmu_cpu_hotplug_init(struct nvdimm_pmu *nd_pmu)
{
	int nodeid, rc;
	const struct cpumask *cpumask;

	/*
	 * Incase of cpu hotplug feature, arch specific code
	 * can provide required cpumask which can be used
	 * to get designatd cpu for counter access.
	 * Check for any active cpu in nd_pmu->arch_cpumask.
	 */
	if (!cpumask_empty(&nd_pmu->arch_cpumask)) {
		nd_pmu->cpu = cpumask_any(&nd_pmu->arch_cpumask);
	} else {
		/* pick active cpu from the cpumask of device numa node. */
		nodeid = dev_to_node(nd_pmu->dev);
		cpumask = cpumask_of_node(nodeid);
		nd_pmu->cpu = cpumask_any(cpumask);
	}

	rc = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN, "perf/nvdimm:online",
				     nvdimm_pmu_cpu_online, nvdimm_pmu_cpu_offline);

	if (rc < 0)
		return rc;

	nd_pmu->cpuhp_state = rc;

	/* Register the pmu instance for cpu hotplug */
	rc = cpuhp_state_add_instance_nocalls(nd_pmu->cpuhp_state, &nd_pmu->node);
	if (rc) {
		cpuhp_remove_multi_state(nd_pmu->cpuhp_state);
		return rc;
	}

	/* Create cpumask attribute group */
	rc = create_cpumask_attr_group(nd_pmu);
	if (rc) {
		cpuhp_state_remove_instance_nocalls(nd_pmu->cpuhp_state, &nd_pmu->node);
		cpuhp_remove_multi_state(nd_pmu->cpuhp_state);
		return rc;
	}

	return 0;
}

static void nvdimm_pmu_free_hotplug_memory(struct nvdimm_pmu *nd_pmu)
{
	cpuhp_state_remove_instance_nocalls(nd_pmu->cpuhp_state, &nd_pmu->node);
	cpuhp_remove_multi_state(nd_pmu->cpuhp_state);

	if (nd_pmu->pmu.attr_groups[NVDIMM_PMU_CPUMASK_ATTR])
		kfree(nd_pmu->pmu.attr_groups[NVDIMM_PMU_CPUMASK_ATTR]->attrs);
	kfree(nd_pmu->pmu.attr_groups[NVDIMM_PMU_CPUMASK_ATTR]);
}

int register_nvdimm_pmu(struct nvdimm_pmu *nd_pmu, struct platform_device *pdev)
{
	int rc;

	if (!nd_pmu || !pdev)
		return -EINVAL;

	/* event functions like add/del/read/event_init and pmu name should not be NULL */
	if (WARN_ON_ONCE(!(nd_pmu->pmu.event_init && nd_pmu->pmu.add &&
			   nd_pmu->pmu.del && nd_pmu->pmu.read && nd_pmu->pmu.name)))
		return -EINVAL;

	nd_pmu->pmu.attr_groups = kzalloc((NVDIMM_PMU_NULL_ATTR + 1) *
					  sizeof(struct attribute_group *), GFP_KERNEL);
	if (!nd_pmu->pmu.attr_groups)
		return -ENOMEM;

	/*
	 * Add platform_device->dev pointer to nvdimm_pmu to access
	 * device data in events functions.
	 */
	nd_pmu->dev = &pdev->dev;

	/* Fill attribute groups for the nvdimm pmu device */
	nd_pmu->pmu.attr_groups[NVDIMM_PMU_FORMAT_ATTR] = &nvdimm_pmu_format_group;
	nd_pmu->pmu.attr_groups[NVDIMM_PMU_EVENT_ATTR] = &nvdimm_pmu_events_group;
	nd_pmu->pmu.attr_groups[NVDIMM_PMU_NULL_ATTR] = NULL;

	/* Fill attribute group for cpumask */
	rc = nvdimm_pmu_cpu_hotplug_init(nd_pmu);
	if (rc) {
		pr_info("cpu hotplug feature failed for device: %s\n", nd_pmu->pmu.name);
		kfree(nd_pmu->pmu.attr_groups);
		return rc;
	}

	rc = perf_pmu_register(&nd_pmu->pmu, nd_pmu->pmu.name, -1);
	if (rc) {
		nvdimm_pmu_free_hotplug_memory(nd_pmu);
		kfree(nd_pmu->pmu.attr_groups);
		return rc;
	}

	pr_info("%s NVDIMM performance monitor support registered\n",
		nd_pmu->pmu.name);

	return 0;
}
EXPORT_SYMBOL_GPL(register_nvdimm_pmu);

void unregister_nvdimm_pmu(struct nvdimm_pmu *nd_pmu)
{
	perf_pmu_unregister(&nd_pmu->pmu);
	nvdimm_pmu_free_hotplug_memory(nd_pmu);
	kfree(nd_pmu->pmu.attr_groups);
	kfree(nd_pmu);
}
EXPORT_SYMBOL_GPL(unregister_nvdimm_pmu);

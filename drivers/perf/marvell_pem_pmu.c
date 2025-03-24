// SPDX-License-Identifier: GPL-2.0
/*
 * Marvell PEM(PCIe RC) Performance Monitor Driver
 *
 * Copyright (C) 2024 Marvell.
 */

#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>

/*
 * Each of these events maps to a free running 64 bit counter
 * with no event control, but can be reset.
 */
enum pem_events {
	IB_TLP_NPR,
	IB_TLP_PR,
	IB_TLP_CPL,
	IB_TLP_DWORDS_NPR,
	IB_TLP_DWORDS_PR,
	IB_TLP_DWORDS_CPL,
	IB_INFLIGHT,
	IB_READS,
	IB_REQ_NO_RO_NCB,
	IB_REQ_NO_RO_EBUS,
	OB_TLP_NPR,
	OB_TLP_PR,
	OB_TLP_CPL,
	OB_TLP_DWORDS_NPR,
	OB_TLP_DWORDS_PR,
	OB_TLP_DWORDS_CPL,
	OB_INFLIGHT,
	OB_READS,
	OB_MERGES_NPR,
	OB_MERGES_PR,
	OB_MERGES_CPL,
	ATS_TRANS,
	ATS_TRANS_LATENCY,
	ATS_PRI,
	ATS_PRI_LATENCY,
	ATS_INV,
	ATS_INV_LATENCY,
	PEM_EVENTIDS_MAX
};

static u64 eventid_to_offset_table[] = {
	[IB_TLP_NPR]	     = 0x0,
	[IB_TLP_PR]	     = 0x8,
	[IB_TLP_CPL]	     = 0x10,
	[IB_TLP_DWORDS_NPR]  = 0x100,
	[IB_TLP_DWORDS_PR]   = 0x108,
	[IB_TLP_DWORDS_CPL]  = 0x110,
	[IB_INFLIGHT]	     = 0x200,
	[IB_READS]	     = 0x300,
	[IB_REQ_NO_RO_NCB]   = 0x400,
	[IB_REQ_NO_RO_EBUS]  = 0x408,
	[OB_TLP_NPR]         = 0x500,
	[OB_TLP_PR]          = 0x508,
	[OB_TLP_CPL]         = 0x510,
	[OB_TLP_DWORDS_NPR]  = 0x600,
	[OB_TLP_DWORDS_PR]   = 0x608,
	[OB_TLP_DWORDS_CPL]  = 0x610,
	[OB_INFLIGHT]        = 0x700,
	[OB_READS]	     = 0x800,
	[OB_MERGES_NPR]      = 0x900,
	[OB_MERGES_PR]       = 0x908,
	[OB_MERGES_CPL]      = 0x910,
	[ATS_TRANS]          = 0x2D18,
	[ATS_TRANS_LATENCY]  = 0x2D20,
	[ATS_PRI]            = 0x2D28,
	[ATS_PRI_LATENCY]    = 0x2D30,
	[ATS_INV]            = 0x2D38,
	[ATS_INV_LATENCY]    = 0x2D40,
};

struct pem_pmu {
	struct pmu pmu;
	void __iomem *base;
	unsigned int cpu;
	struct	device *dev;
	struct hlist_node node;
};

#define to_pem_pmu(p)	container_of(p, struct pem_pmu, pmu)

static int eventid_to_offset(int eventid)
{
	return eventid_to_offset_table[eventid];
}

/* Events */
static ssize_t pem_pmu_event_show(struct device *dev,
				  struct device_attribute *attr,
				  char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);
	return sysfs_emit(page, "event=0x%02llx\n", pmu_attr->id);
}

#define PEM_EVENT_ATTR(_name, _id)					\
	(&((struct perf_pmu_events_attr[]) {				\
	{ .attr = __ATTR(_name, 0444, pem_pmu_event_show, NULL),	\
		.id = _id, }						\
	})[0].attr.attr)

static struct attribute *pem_perf_events_attrs[] = {
	PEM_EVENT_ATTR(ib_tlp_npr, IB_TLP_NPR),
	PEM_EVENT_ATTR(ib_tlp_pr, IB_TLP_PR),
	PEM_EVENT_ATTR(ib_tlp_cpl_partid, IB_TLP_CPL),
	PEM_EVENT_ATTR(ib_tlp_dwords_npr, IB_TLP_DWORDS_NPR),
	PEM_EVENT_ATTR(ib_tlp_dwords_pr, IB_TLP_DWORDS_PR),
	PEM_EVENT_ATTR(ib_tlp_dwords_cpl_partid, IB_TLP_DWORDS_CPL),
	PEM_EVENT_ATTR(ib_inflight, IB_INFLIGHT),
	PEM_EVENT_ATTR(ib_reads, IB_READS),
	PEM_EVENT_ATTR(ib_req_no_ro_ncb, IB_REQ_NO_RO_NCB),
	PEM_EVENT_ATTR(ib_req_no_ro_ebus, IB_REQ_NO_RO_EBUS),
	PEM_EVENT_ATTR(ob_tlp_npr_partid, OB_TLP_NPR),
	PEM_EVENT_ATTR(ob_tlp_pr_partid, OB_TLP_PR),
	PEM_EVENT_ATTR(ob_tlp_cpl_partid, OB_TLP_CPL),
	PEM_EVENT_ATTR(ob_tlp_dwords_npr_partid, OB_TLP_DWORDS_NPR),
	PEM_EVENT_ATTR(ob_tlp_dwords_pr_partid, OB_TLP_DWORDS_PR),
	PEM_EVENT_ATTR(ob_tlp_dwords_cpl_partid, OB_TLP_DWORDS_CPL),
	PEM_EVENT_ATTR(ob_inflight_partid, OB_INFLIGHT),
	PEM_EVENT_ATTR(ob_reads_partid, OB_READS),
	PEM_EVENT_ATTR(ob_merges_npr_partid, OB_MERGES_NPR),
	PEM_EVENT_ATTR(ob_merges_pr_partid, OB_MERGES_PR),
	PEM_EVENT_ATTR(ob_merges_cpl_partid, OB_MERGES_CPL),
	PEM_EVENT_ATTR(ats_trans, ATS_TRANS),
	PEM_EVENT_ATTR(ats_trans_latency, ATS_TRANS_LATENCY),
	PEM_EVENT_ATTR(ats_pri, ATS_PRI),
	PEM_EVENT_ATTR(ats_pri_latency, ATS_PRI_LATENCY),
	PEM_EVENT_ATTR(ats_inv, ATS_INV),
	PEM_EVENT_ATTR(ats_inv_latency, ATS_INV_LATENCY),
	NULL
};

static struct attribute_group pem_perf_events_attr_group = {
	.name = "events",
	.attrs = pem_perf_events_attrs,
};

PMU_FORMAT_ATTR(event, "config:0-5");

static struct attribute *pem_perf_format_attrs[] = {
	&format_attr_event.attr,
	NULL
};

static struct attribute_group pem_perf_format_attr_group = {
	.name = "format",
	.attrs = pem_perf_format_attrs,
};

/* cpumask */
static ssize_t pem_perf_cpumask_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct pem_pmu *pmu = dev_get_drvdata(dev);

	return cpumap_print_to_pagebuf(true, buf, cpumask_of(pmu->cpu));
}

static struct device_attribute pem_perf_cpumask_attr =
	__ATTR(cpumask, 0444, pem_perf_cpumask_show, NULL);

static struct attribute *pem_perf_cpumask_attrs[] = {
	&pem_perf_cpumask_attr.attr,
	NULL
};

static struct attribute_group pem_perf_cpumask_attr_group = {
	.attrs = pem_perf_cpumask_attrs,
};

static const struct attribute_group *pem_perf_attr_groups[] = {
	&pem_perf_events_attr_group,
	&pem_perf_cpumask_attr_group,
	&pem_perf_format_attr_group,
	NULL
};

static int pem_perf_event_init(struct perf_event *event)
{
	struct pem_pmu *pmu = to_pem_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	struct perf_event *sibling;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (event->attr.config >= PEM_EVENTIDS_MAX)
		return -EINVAL;

	if (is_sampling_event(event) ||
	    event->attach_state & PERF_ATTACH_TASK) {
		return -EOPNOTSUPP;
	}

	if (event->cpu < 0)
		return -EOPNOTSUPP;

	/*  We must NOT create groups containing mixed PMUs */
	if (event->group_leader->pmu != event->pmu &&
	    !is_software_event(event->group_leader))
		return -EINVAL;

	for_each_sibling_event(sibling, event->group_leader) {
		if (sibling->pmu != event->pmu &&
		    !is_software_event(sibling))
			return -EINVAL;
	}
	/*
	 * Set ownership of event to one CPU, same event can not be observed
	 * on multiple cpus at same time.
	 */
	event->cpu = pmu->cpu;
	hwc->idx = -1;
	return 0;
}

static u64 pem_perf_read_counter(struct pem_pmu *pmu,
				 struct perf_event *event, int eventid)
{
	return readq_relaxed(pmu->base + eventid_to_offset(eventid));
}

static void pem_perf_event_update(struct perf_event *event)
{
	struct pem_pmu *pmu = to_pem_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	u64 prev_count, new_count;

	do {
		prev_count = local64_read(&hwc->prev_count);
		new_count = pem_perf_read_counter(pmu, event, hwc->idx);
	} while (local64_xchg(&hwc->prev_count, new_count) != prev_count);

	local64_add((new_count - prev_count), &event->count);
}

static void pem_perf_event_start(struct perf_event *event, int flags)
{
	struct pem_pmu *pmu = to_pem_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int eventid = hwc->idx;

	/*
	 * All counters are free-running and associated with
	 * a fixed event to track in Hardware
	 */
	local64_set(&hwc->prev_count,
		    pem_perf_read_counter(pmu, event, eventid));

	hwc->state = 0;
}

static int pem_perf_event_add(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;

	hwc->idx = event->attr.config;
	if (WARN_ON_ONCE(hwc->idx >= PEM_EVENTIDS_MAX))
		return -EINVAL;
	hwc->state |= PERF_HES_STOPPED;

	if (flags & PERF_EF_START)
		pem_perf_event_start(event, flags);

	return 0;
}

static void pem_perf_event_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;

	if (flags & PERF_EF_UPDATE)
		pem_perf_event_update(event);

	hwc->state |= PERF_HES_STOPPED;
}

static void pem_perf_event_del(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;

	pem_perf_event_stop(event, PERF_EF_UPDATE);
	hwc->idx = -1;
}

static int pem_pmu_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct pem_pmu *pmu = hlist_entry_safe(node, struct pem_pmu, node);
	unsigned int target;

	if (cpu != pmu->cpu)
		return 0;

	target = cpumask_any_but(cpu_online_mask, cpu);
	if (target >= nr_cpu_ids)
		return 0;

	perf_pmu_migrate_context(&pmu->pmu, cpu, target);
	pmu->cpu = target;
	return 0;
}

static int pem_perf_probe(struct platform_device *pdev)
{
	struct pem_pmu *pem_pmu;
	struct resource *res;
	void __iomem *base;
	char *name;
	int ret;

	pem_pmu = devm_kzalloc(&pdev->dev, sizeof(*pem_pmu), GFP_KERNEL);
	if (!pem_pmu)
		return -ENOMEM;

	pem_pmu->dev = &pdev->dev;
	platform_set_drvdata(pdev, pem_pmu);

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	pem_pmu->base = base;

	pem_pmu->pmu = (struct pmu) {
		.module	      = THIS_MODULE,
		.capabilities = PERF_PMU_CAP_NO_EXCLUDE,
		.task_ctx_nr = perf_invalid_context,
		.attr_groups = pem_perf_attr_groups,
		.event_init  = pem_perf_event_init,
		.add	     = pem_perf_event_add,
		.del	     = pem_perf_event_del,
		.start	     = pem_perf_event_start,
		.stop	     = pem_perf_event_stop,
		.read	     = pem_perf_event_update,
	};

	/* Choose this cpu to collect perf data */
	pem_pmu->cpu = raw_smp_processor_id();

	name = devm_kasprintf(pem_pmu->dev, GFP_KERNEL, "mrvl_pcie_rc_pmu_%llx",
			      res->start);
	if (!name)
		return -ENOMEM;

	cpuhp_state_add_instance_nocalls(CPUHP_AP_PERF_ARM_MRVL_PEM_ONLINE,
					 &pem_pmu->node);

	ret = perf_pmu_register(&pem_pmu->pmu, name, -1);
	if (ret)
		goto error;

	return 0;
error:
	cpuhp_state_remove_instance_nocalls(CPUHP_AP_PERF_ARM_MRVL_PEM_ONLINE,
					    &pem_pmu->node);
	return ret;
}

static void pem_perf_remove(struct platform_device *pdev)
{
	struct pem_pmu *pem_pmu = platform_get_drvdata(pdev);

	cpuhp_state_remove_instance_nocalls(CPUHP_AP_PERF_ARM_MRVL_PEM_ONLINE,
					    &pem_pmu->node);

	perf_pmu_unregister(&pem_pmu->pmu);
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id pem_pmu_acpi_match[] = {
	{"MRVL000E", 0},
	{}
};
MODULE_DEVICE_TABLE(acpi, pem_pmu_acpi_match);
#endif

static struct platform_driver pem_pmu_driver = {
	.driver	= {
		.name   = "pem-pmu",
		.acpi_match_table = ACPI_PTR(pem_pmu_acpi_match),
		.suppress_bind_attrs = true,
	},
	.probe		= pem_perf_probe,
	.remove		= pem_perf_remove,
};

static int __init pem_pmu_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_PERF_ARM_MRVL_PEM_ONLINE,
				      "perf/marvell/pem:online", NULL,
				       pem_pmu_offline_cpu);
	if (ret)
		return ret;

	ret = platform_driver_register(&pem_pmu_driver);
	if (ret)
		cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_MRVL_PEM_ONLINE);
	return ret;
}

static void __exit pem_pmu_exit(void)
{
	platform_driver_unregister(&pem_pmu_driver);
	cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_MRVL_PEM_ONLINE);
}

module_init(pem_pmu_init);
module_exit(pem_pmu_exit);

MODULE_DESCRIPTION("Marvell PEM Perf driver");
MODULE_AUTHOR("Gowthami Thiagarajan <gthiagarajan@marvell.com>");
MODULE_LICENSE("GPL");

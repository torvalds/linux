// SPDX-License-Identifier: GPL-2.0
/* Marvell CN10K LLC-TAD perf driver
 *
 * Copyright (C) 2021 Marvell
 */

#define pr_fmt(fmt) "tad_pmu: " fmt

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/cpuhotplug.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>

#define TAD_PFC_OFFSET		0x800
#define TAD_PFC(counter)	(TAD_PFC_OFFSET | (counter << 3))
#define TAD_PRF_OFFSET		0x900
#define TAD_PRF(counter)	(TAD_PRF_OFFSET | (counter << 3))
#define TAD_PRF_CNTSEL_MASK	0xFF
#define TAD_MAX_COUNTERS	8

#define to_tad_pmu(p) (container_of(p, struct tad_pmu, pmu))

struct tad_region {
	void __iomem	*base;
};

struct tad_pmu {
	struct pmu pmu;
	struct tad_region *regions;
	u32 region_cnt;
	unsigned int cpu;
	struct hlist_node node;
	struct perf_event *events[TAD_MAX_COUNTERS];
	DECLARE_BITMAP(counters_map, TAD_MAX_COUNTERS);
};

static int tad_pmu_cpuhp_state;

static void tad_pmu_event_counter_read(struct perf_event *event)
{
	struct tad_pmu *tad_pmu = to_tad_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	u32 counter_idx = hwc->idx;
	u64 prev, new;
	int i;

	do {
		prev = local64_read(&hwc->prev_count);
		for (i = 0, new = 0; i < tad_pmu->region_cnt; i++)
			new += readq(tad_pmu->regions[i].base +
				     TAD_PFC(counter_idx));
	} while (local64_cmpxchg(&hwc->prev_count, prev, new) != prev);

	local64_add(new - prev, &event->count);
}

static void tad_pmu_event_counter_stop(struct perf_event *event, int flags)
{
	struct tad_pmu *tad_pmu = to_tad_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	u32 counter_idx = hwc->idx;
	int i;

	/* TAD()_PFC() stop counting on the write
	 * which sets TAD()_PRF()[CNTSEL] == 0
	 */
	for (i = 0; i < tad_pmu->region_cnt; i++) {
		writeq_relaxed(0, tad_pmu->regions[i].base +
			       TAD_PRF(counter_idx));
	}

	tad_pmu_event_counter_read(event);
	hwc->state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
}

static void tad_pmu_event_counter_start(struct perf_event *event, int flags)
{
	struct tad_pmu *tad_pmu = to_tad_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	u32 event_idx = event->attr.config;
	u32 counter_idx = hwc->idx;
	u64 reg_val;
	int i;

	hwc->state = 0;

	/* Typically TAD_PFC() are zeroed to start counting */
	for (i = 0; i < tad_pmu->region_cnt; i++)
		writeq_relaxed(0, tad_pmu->regions[i].base +
			       TAD_PFC(counter_idx));

	/* TAD()_PFC() start counting on the write
	 * which sets TAD()_PRF()[CNTSEL] != 0
	 */
	for (i = 0; i < tad_pmu->region_cnt; i++) {
		reg_val = event_idx & 0xFF;
		writeq_relaxed(reg_val,	tad_pmu->regions[i].base +
			       TAD_PRF(counter_idx));
	}
}

static void tad_pmu_event_counter_del(struct perf_event *event, int flags)
{
	struct tad_pmu *tad_pmu = to_tad_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	tad_pmu_event_counter_stop(event, flags | PERF_EF_UPDATE);
	tad_pmu->events[idx] = NULL;
	clear_bit(idx, tad_pmu->counters_map);
}

static int tad_pmu_event_counter_add(struct perf_event *event, int flags)
{
	struct tad_pmu *tad_pmu = to_tad_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx;

	/* Get a free counter for this event */
	idx = find_first_zero_bit(tad_pmu->counters_map, TAD_MAX_COUNTERS);
	if (idx == TAD_MAX_COUNTERS)
		return -EAGAIN;

	set_bit(idx, tad_pmu->counters_map);

	hwc->idx = idx;
	hwc->state = PERF_HES_STOPPED;
	tad_pmu->events[idx] = event;

	if (flags & PERF_EF_START)
		tad_pmu_event_counter_start(event, flags);

	return 0;
}

static int tad_pmu_event_init(struct perf_event *event)
{
	struct tad_pmu *tad_pmu = to_tad_pmu(event->pmu);

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (!event->attr.disabled)
		return -EINVAL;

	if (event->state != PERF_EVENT_STATE_OFF)
		return -EINVAL;

	event->cpu = tad_pmu->cpu;
	event->hw.idx = -1;
	event->hw.config_base = event->attr.config;

	return 0;
}

static ssize_t tad_pmu_event_show(struct device *dev,
				struct device_attribute *attr, char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);
	return sysfs_emit(page, "event=0x%02llx\n", pmu_attr->id);
}

#define TAD_PMU_EVENT_ATTR(name, config)			\
	PMU_EVENT_ATTR_ID(name, tad_pmu_event_show, config)

static struct attribute *tad_pmu_event_attrs[] = {
	TAD_PMU_EVENT_ATTR(tad_none, 0x0),
	TAD_PMU_EVENT_ATTR(tad_req_msh_in_any, 0x1),
	TAD_PMU_EVENT_ATTR(tad_req_msh_in_mn, 0x2),
	TAD_PMU_EVENT_ATTR(tad_req_msh_in_exlmn, 0x3),
	TAD_PMU_EVENT_ATTR(tad_rsp_msh_in_any, 0x4),
	TAD_PMU_EVENT_ATTR(tad_rsp_msh_in_mn, 0x5),
	TAD_PMU_EVENT_ATTR(tad_rsp_msh_in_exlmn, 0x6),
	TAD_PMU_EVENT_ATTR(tad_rsp_msh_in_dss, 0x7),
	TAD_PMU_EVENT_ATTR(tad_rsp_msh_in_retry_dss, 0x8),
	TAD_PMU_EVENT_ATTR(tad_dat_msh_in_any, 0x9),
	TAD_PMU_EVENT_ATTR(tad_dat_msh_in_dss, 0xa),
	TAD_PMU_EVENT_ATTR(tad_req_msh_out_any, 0xb),
	TAD_PMU_EVENT_ATTR(tad_req_msh_out_dss_rd, 0xc),
	TAD_PMU_EVENT_ATTR(tad_req_msh_out_dss_wr, 0xd),
	TAD_PMU_EVENT_ATTR(tad_req_msh_out_evict, 0xe),
	TAD_PMU_EVENT_ATTR(tad_rsp_msh_out_any, 0xf),
	TAD_PMU_EVENT_ATTR(tad_rsp_msh_out_retry_exlmn, 0x10),
	TAD_PMU_EVENT_ATTR(tad_rsp_msh_out_retry_mn, 0x11),
	TAD_PMU_EVENT_ATTR(tad_rsp_msh_out_exlmn, 0x12),
	TAD_PMU_EVENT_ATTR(tad_rsp_msh_out_mn, 0x13),
	TAD_PMU_EVENT_ATTR(tad_snp_msh_out_any, 0x14),
	TAD_PMU_EVENT_ATTR(tad_snp_msh_out_mn, 0x15),
	TAD_PMU_EVENT_ATTR(tad_snp_msh_out_exlmn, 0x16),
	TAD_PMU_EVENT_ATTR(tad_dat_msh_out_any, 0x17),
	TAD_PMU_EVENT_ATTR(tad_dat_msh_out_fill, 0x18),
	TAD_PMU_EVENT_ATTR(tad_dat_msh_out_dss, 0x19),
	TAD_PMU_EVENT_ATTR(tad_alloc_dtg, 0x1a),
	TAD_PMU_EVENT_ATTR(tad_alloc_ltg, 0x1b),
	TAD_PMU_EVENT_ATTR(tad_alloc_any, 0x1c),
	TAD_PMU_EVENT_ATTR(tad_hit_dtg, 0x1d),
	TAD_PMU_EVENT_ATTR(tad_hit_ltg, 0x1e),
	TAD_PMU_EVENT_ATTR(tad_hit_any, 0x1f),
	TAD_PMU_EVENT_ATTR(tad_tag_rd, 0x20),
	TAD_PMU_EVENT_ATTR(tad_dat_rd, 0x21),
	TAD_PMU_EVENT_ATTR(tad_dat_rd_byp, 0x22),
	TAD_PMU_EVENT_ATTR(tad_ifb_occ, 0x23),
	TAD_PMU_EVENT_ATTR(tad_req_occ, 0x24),
	NULL
};

static const struct attribute_group tad_pmu_events_attr_group = {
	.name = "events",
	.attrs = tad_pmu_event_attrs,
};

PMU_FORMAT_ATTR(event, "config:0-7");

static struct attribute *tad_pmu_format_attrs[] = {
	&format_attr_event.attr,
	NULL
};

static struct attribute_group tad_pmu_format_attr_group = {
	.name = "format",
	.attrs = tad_pmu_format_attrs,
};

static ssize_t tad_pmu_cpumask_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct tad_pmu *tad_pmu = to_tad_pmu(dev_get_drvdata(dev));

	return cpumap_print_to_pagebuf(true, buf, cpumask_of(tad_pmu->cpu));
}

static DEVICE_ATTR(cpumask, 0444, tad_pmu_cpumask_show, NULL);

static struct attribute *tad_pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL
};

static struct attribute_group tad_pmu_cpumask_attr_group = {
	.attrs = tad_pmu_cpumask_attrs,
};

static const struct attribute_group *tad_pmu_attr_groups[] = {
	&tad_pmu_events_attr_group,
	&tad_pmu_format_attr_group,
	&tad_pmu_cpumask_attr_group,
	NULL
};

static int tad_pmu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tad_region *regions;
	struct tad_pmu *tad_pmu;
	struct resource *res;
	u32 tad_pmu_page_size;
	u32 tad_page_size;
	u32 tad_cnt;
	int i, ret;
	char *name;

	tad_pmu = devm_kzalloc(&pdev->dev, sizeof(*tad_pmu), GFP_KERNEL);
	if (!tad_pmu)
		return -ENOMEM;

	platform_set_drvdata(pdev, tad_pmu);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Mem resource not found\n");
		return -ENODEV;
	}

	ret = device_property_read_u32(dev, "marvell,tad-page-size",
				       &tad_page_size);
	if (ret) {
		dev_err(&pdev->dev, "Can't find tad-page-size property\n");
		return ret;
	}

	ret = device_property_read_u32(dev, "marvell,tad-pmu-page-size",
				       &tad_pmu_page_size);
	if (ret) {
		dev_err(&pdev->dev, "Can't find tad-pmu-page-size property\n");
		return ret;
	}

	ret = device_property_read_u32(dev, "marvell,tad-cnt", &tad_cnt);
	if (ret) {
		dev_err(&pdev->dev, "Can't find tad-cnt property\n");
		return ret;
	}

	regions = devm_kcalloc(&pdev->dev, tad_cnt,
			       sizeof(*regions), GFP_KERNEL);
	if (!regions)
		return -ENOMEM;

	/* ioremap the distributed TAD pmu regions */
	for (i = 0; i < tad_cnt && res->start < res->end; i++) {
		regions[i].base = devm_ioremap(&pdev->dev,
					       res->start,
					       tad_pmu_page_size);
		if (!regions[i].base) {
			dev_err(&pdev->dev, "TAD%d ioremap fail\n", i);
			return -ENOMEM;
		}
		res->start += tad_page_size;
	}

	tad_pmu->regions = regions;
	tad_pmu->region_cnt = tad_cnt;

	tad_pmu->pmu = (struct pmu) {

		.module		= THIS_MODULE,
		.attr_groups	= tad_pmu_attr_groups,
		.capabilities	= PERF_PMU_CAP_NO_EXCLUDE |
				  PERF_PMU_CAP_NO_INTERRUPT,
		.task_ctx_nr	= perf_invalid_context,

		.event_init	= tad_pmu_event_init,
		.add		= tad_pmu_event_counter_add,
		.del		= tad_pmu_event_counter_del,
		.start		= tad_pmu_event_counter_start,
		.stop		= tad_pmu_event_counter_stop,
		.read		= tad_pmu_event_counter_read,
	};

	tad_pmu->cpu = raw_smp_processor_id();

	/* Register pmu instance for cpu hotplug */
	ret = cpuhp_state_add_instance_nocalls(tad_pmu_cpuhp_state,
					       &tad_pmu->node);
	if (ret) {
		dev_err(&pdev->dev, "Error %d registering hotplug\n", ret);
		return ret;
	}

	name = "tad";
	ret = perf_pmu_register(&tad_pmu->pmu, name, -1);
	if (ret)
		cpuhp_state_remove_instance_nocalls(tad_pmu_cpuhp_state,
						    &tad_pmu->node);

	return ret;
}

static void tad_pmu_remove(struct platform_device *pdev)
{
	struct tad_pmu *pmu = platform_get_drvdata(pdev);

	cpuhp_state_remove_instance_nocalls(tad_pmu_cpuhp_state,
						&pmu->node);
	perf_pmu_unregister(&pmu->pmu);
}

#ifdef CONFIG_OF
static const struct of_device_id tad_pmu_of_match[] = {
	{ .compatible = "marvell,cn10k-tad-pmu", },
	{},
};
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id tad_pmu_acpi_match[] = {
	{"MRVL000B", 0},
	{},
};
MODULE_DEVICE_TABLE(acpi, tad_pmu_acpi_match);
#endif

static struct platform_driver tad_pmu_driver = {
	.driver         = {
		.name   = "cn10k_tad_pmu",
		.of_match_table = of_match_ptr(tad_pmu_of_match),
		.acpi_match_table = ACPI_PTR(tad_pmu_acpi_match),
		.suppress_bind_attrs = true,
	},
	.probe          = tad_pmu_probe,
	.remove         = tad_pmu_remove,
};

static int tad_pmu_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct tad_pmu *pmu = hlist_entry_safe(node, struct tad_pmu, node);
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

static int __init tad_pmu_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
				      "perf/cn10k/tadpmu:online",
				      NULL,
				      tad_pmu_offline_cpu);
	if (ret < 0)
		return ret;
	tad_pmu_cpuhp_state = ret;
	ret = platform_driver_register(&tad_pmu_driver);
	if (ret)
		cpuhp_remove_multi_state(tad_pmu_cpuhp_state);

	return ret;
}

static void __exit tad_pmu_exit(void)
{
	platform_driver_unregister(&tad_pmu_driver);
	cpuhp_remove_multi_state(tad_pmu_cpuhp_state);
}

module_init(tad_pmu_init);
module_exit(tad_pmu_exit);

MODULE_DESCRIPTION("Marvell CN10K LLC-TAD Perf driver");
MODULE_AUTHOR("Bhaskara Budiredla <bbudiredla@marvell.com>");
MODULE_LICENSE("GPL v2");

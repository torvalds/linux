// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Amlogic, Inc. All rights reserved.
 */

#include <linux/bitfield.h>
#include <linux/init.h>
#include <linux/irqreturn.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include <soc/amlogic/meson_ddr_pmu.h>

struct ddr_pmu {
	struct pmu pmu;
	struct dmc_info info;
	struct dmc_counter counters;	/* save counters from hw */
	bool pmu_enabled;
	struct device *dev;
	char *name;
	struct hlist_node node;
	enum cpuhp_state cpuhp_state;
	int cpu;			/* for cpu hotplug */
};

#define DDR_PERF_DEV_NAME "meson_ddr_bw"
#define MAX_AXI_PORTS_OF_CHANNEL	4	/* A DMC channel can monitor max 4 axi ports */

#define to_ddr_pmu(p)		container_of(p, struct ddr_pmu, pmu)
#define dmc_info_to_pmu(p)	container_of(p, struct ddr_pmu, info)

static void dmc_pmu_enable(struct ddr_pmu *pmu)
{
	if (!pmu->pmu_enabled)
		pmu->info.hw_info->enable(&pmu->info);

	pmu->pmu_enabled = true;
}

static void dmc_pmu_disable(struct ddr_pmu *pmu)
{
	if (pmu->pmu_enabled)
		pmu->info.hw_info->disable(&pmu->info);

	pmu->pmu_enabled = false;
}

static void meson_ddr_set_axi_filter(struct perf_event *event, u8 axi_id)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	int chann;

	if (event->attr.config > ALL_CHAN_COUNTER_ID &&
	    event->attr.config < COUNTER_MAX_ID) {
		chann = event->attr.config - CHAN1_COUNTER_ID;

		pmu->info.hw_info->set_axi_filter(&pmu->info, axi_id, chann);
	}
}

static void ddr_cnt_addition(struct dmc_counter *sum,
			     struct dmc_counter *add1,
			     struct dmc_counter *add2,
			     int chann_nr)
{
	int i;
	u64 cnt1, cnt2;

	sum->all_cnt = add1->all_cnt + add2->all_cnt;
	sum->all_req = add1->all_req + add2->all_req;
	for (i = 0; i < chann_nr; i++) {
		cnt1 = add1->channel_cnt[i];
		cnt2 = add2->channel_cnt[i];

		sum->channel_cnt[i] = cnt1 + cnt2;
	}
}

static void meson_ddr_perf_event_update(struct perf_event *event)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	u64 new_raw_count = 0;
	struct dmc_counter dc = {0}, sum_dc = {0};
	int idx;
	int chann_nr = pmu->info.hw_info->chann_nr;

	/* get the remain counters in register. */
	pmu->info.hw_info->get_counters(&pmu->info, &dc);

	ddr_cnt_addition(&sum_dc, &pmu->counters, &dc, chann_nr);

	switch (event->attr.config) {
	case ALL_CHAN_COUNTER_ID:
		new_raw_count = sum_dc.all_cnt;
		break;
	case CHAN1_COUNTER_ID:
	case CHAN2_COUNTER_ID:
	case CHAN3_COUNTER_ID:
	case CHAN4_COUNTER_ID:
	case CHAN5_COUNTER_ID:
	case CHAN6_COUNTER_ID:
	case CHAN7_COUNTER_ID:
	case CHAN8_COUNTER_ID:
		idx = event->attr.config - CHAN1_COUNTER_ID;
		new_raw_count = sum_dc.channel_cnt[idx];
		break;
	}

	local64_set(&event->count, new_raw_count);
}

static int meson_ddr_perf_event_init(struct perf_event *event)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);
	u64 config1 = event->attr.config1;
	u64 config2 = event->attr.config2;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (is_sampling_event(event) || event->attach_state & PERF_ATTACH_TASK)
		return -EOPNOTSUPP;

	if (event->cpu < 0)
		return -EOPNOTSUPP;

	/* check if the number of parameters is too much */
	if (event->attr.config != ALL_CHAN_COUNTER_ID &&
	    hweight64(config1) + hweight64(config2) > MAX_AXI_PORTS_OF_CHANNEL)
		return -EOPNOTSUPP;

	event->cpu = pmu->cpu;

	return 0;
}

static void meson_ddr_perf_event_start(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);

	memset(&pmu->counters, 0, sizeof(pmu->counters));
	dmc_pmu_enable(pmu);
}

static int meson_ddr_perf_event_add(struct perf_event *event, int flags)
{
	u64 config1 = event->attr.config1;
	u64 config2 = event->attr.config2;
	int i;

	for_each_set_bit(i,
			 (const unsigned long *)&config1,
			 BITS_PER_TYPE(config1))
		meson_ddr_set_axi_filter(event, i);

	for_each_set_bit(i,
			 (const unsigned long *)&config2,
			 BITS_PER_TYPE(config2))
		meson_ddr_set_axi_filter(event, i + 64);

	if (flags & PERF_EF_START)
		meson_ddr_perf_event_start(event, flags);

	return 0;
}

static void meson_ddr_perf_event_stop(struct perf_event *event, int flags)
{
	struct ddr_pmu *pmu = to_ddr_pmu(event->pmu);

	if (flags & PERF_EF_UPDATE)
		meson_ddr_perf_event_update(event);

	dmc_pmu_disable(pmu);
}

static void meson_ddr_perf_event_del(struct perf_event *event, int flags)
{
	meson_ddr_perf_event_stop(event, PERF_EF_UPDATE);
}

static ssize_t meson_ddr_perf_cpumask_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct ddr_pmu *pmu = dev_get_drvdata(dev);

	return cpumap_print_to_pagebuf(true, buf, cpumask_of(pmu->cpu));
}

static struct device_attribute meson_ddr_perf_cpumask_attr =
__ATTR(cpumask, 0444, meson_ddr_perf_cpumask_show, NULL);

static struct attribute *meson_ddr_perf_cpumask_attrs[] = {
	&meson_ddr_perf_cpumask_attr.attr,
	NULL,
};

static const struct attribute_group ddr_perf_cpumask_attr_group = {
	.attrs = meson_ddr_perf_cpumask_attrs,
};

static ssize_t
pmu_event_show(struct device *dev, struct device_attribute *attr,
	       char *page)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, struct perf_pmu_events_attr, attr);
	return sysfs_emit(page, "event=0x%02llx\n", pmu_attr->id);
}

static ssize_t
event_show_unit(struct device *dev, struct device_attribute *attr,
		char *page)
{
	return sysfs_emit(page, "MB\n");
}

static ssize_t
event_show_scale(struct device *dev, struct device_attribute *attr,
		 char *page)
{
	/* one count = 16byte = 1.52587890625e-05 MB */
	return sysfs_emit(page, "1.52587890625e-05\n");
}

#define AML_DDR_PMU_EVENT_ATTR(_name, _id)				\
{									\
	.attr = __ATTR(_name, 0444, pmu_event_show, NULL),		\
	.id = _id,							\
}

#define AML_DDR_PMU_EVENT_UNIT_ATTR(_name)				\
	__ATTR(_name.unit, 0444, event_show_unit, NULL)

#define AML_DDR_PMU_EVENT_SCALE_ATTR(_name)				\
	__ATTR(_name.scale, 0444, event_show_scale, NULL)

static struct device_attribute event_unit_attrs[] = {
	AML_DDR_PMU_EVENT_UNIT_ATTR(total_rw_bytes),
	AML_DDR_PMU_EVENT_UNIT_ATTR(chan_1_rw_bytes),
	AML_DDR_PMU_EVENT_UNIT_ATTR(chan_2_rw_bytes),
	AML_DDR_PMU_EVENT_UNIT_ATTR(chan_3_rw_bytes),
	AML_DDR_PMU_EVENT_UNIT_ATTR(chan_4_rw_bytes),
	AML_DDR_PMU_EVENT_UNIT_ATTR(chan_5_rw_bytes),
	AML_DDR_PMU_EVENT_UNIT_ATTR(chan_6_rw_bytes),
	AML_DDR_PMU_EVENT_UNIT_ATTR(chan_7_rw_bytes),
	AML_DDR_PMU_EVENT_UNIT_ATTR(chan_8_rw_bytes),
};

static struct device_attribute event_scale_attrs[] = {
	AML_DDR_PMU_EVENT_SCALE_ATTR(total_rw_bytes),
	AML_DDR_PMU_EVENT_SCALE_ATTR(chan_1_rw_bytes),
	AML_DDR_PMU_EVENT_SCALE_ATTR(chan_2_rw_bytes),
	AML_DDR_PMU_EVENT_SCALE_ATTR(chan_3_rw_bytes),
	AML_DDR_PMU_EVENT_SCALE_ATTR(chan_4_rw_bytes),
	AML_DDR_PMU_EVENT_SCALE_ATTR(chan_5_rw_bytes),
	AML_DDR_PMU_EVENT_SCALE_ATTR(chan_6_rw_bytes),
	AML_DDR_PMU_EVENT_SCALE_ATTR(chan_7_rw_bytes),
	AML_DDR_PMU_EVENT_SCALE_ATTR(chan_8_rw_bytes),
};

static struct perf_pmu_events_attr event_attrs[] = {
	AML_DDR_PMU_EVENT_ATTR(total_rw_bytes, ALL_CHAN_COUNTER_ID),
	AML_DDR_PMU_EVENT_ATTR(chan_1_rw_bytes, CHAN1_COUNTER_ID),
	AML_DDR_PMU_EVENT_ATTR(chan_2_rw_bytes, CHAN2_COUNTER_ID),
	AML_DDR_PMU_EVENT_ATTR(chan_3_rw_bytes, CHAN3_COUNTER_ID),
	AML_DDR_PMU_EVENT_ATTR(chan_4_rw_bytes, CHAN4_COUNTER_ID),
	AML_DDR_PMU_EVENT_ATTR(chan_5_rw_bytes, CHAN5_COUNTER_ID),
	AML_DDR_PMU_EVENT_ATTR(chan_6_rw_bytes, CHAN6_COUNTER_ID),
	AML_DDR_PMU_EVENT_ATTR(chan_7_rw_bytes, CHAN7_COUNTER_ID),
	AML_DDR_PMU_EVENT_ATTR(chan_8_rw_bytes, CHAN8_COUNTER_ID),
};

/* three attrs are combined an event */
static struct attribute *ddr_perf_events_attrs[COUNTER_MAX_ID * 3];

static struct attribute_group ddr_perf_events_attr_group = {
	.name = "events",
	.attrs = ddr_perf_events_attrs,
};

static umode_t meson_ddr_perf_format_attr_visible(struct kobject *kobj,
						  struct attribute *attr,
						  int n)
{
	struct pmu *pmu = dev_get_drvdata(kobj_to_dev(kobj));
	struct ddr_pmu *ddr_pmu = to_ddr_pmu(pmu);
	const u64 *capability = ddr_pmu->info.hw_info->capability;
	struct device_attribute *dev_attr;
	int id;
	char value[20]; // config1:xxx, 20 is enough

	dev_attr = container_of(attr, struct device_attribute, attr);
	dev_attr->show(NULL, NULL, value);

	if (sscanf(value, "config1:%d", &id) == 1)
		return capability[0] & (1ULL << id) ? attr->mode : 0;

	if (sscanf(value, "config2:%d", &id) == 1)
		return capability[1] & (1ULL << id) ? attr->mode : 0;

	return attr->mode;
}

static struct attribute_group ddr_perf_format_attr_group = {
	.name = "format",
	.is_visible = meson_ddr_perf_format_attr_visible,
};

static ssize_t meson_ddr_perf_identifier_show(struct device *dev,
					      struct device_attribute *attr,
					      char *page)
{
	struct ddr_pmu *pmu = dev_get_drvdata(dev);

	return sysfs_emit(page, "%s\n", pmu->name);
}

static struct device_attribute meson_ddr_perf_identifier_attr =
__ATTR(identifier, 0444, meson_ddr_perf_identifier_show, NULL);

static struct attribute *meson_ddr_perf_identifier_attrs[] = {
	&meson_ddr_perf_identifier_attr.attr,
	NULL,
};

static const struct attribute_group ddr_perf_identifier_attr_group = {
	.attrs = meson_ddr_perf_identifier_attrs,
};

static const struct attribute_group *attr_groups[] = {
	&ddr_perf_events_attr_group,
	&ddr_perf_format_attr_group,
	&ddr_perf_cpumask_attr_group,
	&ddr_perf_identifier_attr_group,
	NULL,
};

static irqreturn_t dmc_irq_handler(int irq, void *dev_id)
{
	struct dmc_info *info = dev_id;
	struct ddr_pmu *pmu;
	struct dmc_counter counters, *sum_cnter;
	int i;

	pmu = dmc_info_to_pmu(info);

	if (info->hw_info->irq_handler(info, &counters) != 0)
		goto out;

	sum_cnter = &pmu->counters;
	sum_cnter->all_cnt += counters.all_cnt;
	sum_cnter->all_req += counters.all_req;

	for (i = 0; i < pmu->info.hw_info->chann_nr; i++)
		sum_cnter->channel_cnt[i] += counters.channel_cnt[i];

	if (pmu->pmu_enabled)
		/*
		 * the timer interrupt only supprt
		 * one shot mode, we have to re-enable
		 * it in ISR to support continue mode.
		 */
		info->hw_info->enable(info);

	dev_dbg(pmu->dev, "counts: %llu %llu %llu, %llu, %llu, %llu\t\t"
			"sum: %llu %llu %llu, %llu, %llu, %llu\n",
			counters.all_req,
			counters.all_cnt,
			counters.channel_cnt[0],
			counters.channel_cnt[1],
			counters.channel_cnt[2],
			counters.channel_cnt[3],

			pmu->counters.all_req,
			pmu->counters.all_cnt,
			pmu->counters.channel_cnt[0],
			pmu->counters.channel_cnt[1],
			pmu->counters.channel_cnt[2],
			pmu->counters.channel_cnt[3]);
out:
	return IRQ_HANDLED;
}

static int ddr_perf_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct ddr_pmu *pmu = hlist_entry_safe(node, struct ddr_pmu, node);
	int target;

	if (cpu != pmu->cpu)
		return 0;

	target = cpumask_any_but(cpu_online_mask, cpu);
	if (target >= nr_cpu_ids)
		return 0;

	perf_pmu_migrate_context(&pmu->pmu, cpu, target);
	pmu->cpu = target;

	WARN_ON(irq_set_affinity(pmu->info.irq_num, cpumask_of(pmu->cpu)));

	return 0;
}

static void fill_event_attr(struct ddr_pmu *pmu)
{
	int i, j, k;
	struct attribute **dst = ddr_perf_events_attrs;

	j = 0;
	k = 0;

	/* fill ALL_CHAN_COUNTER_ID event */
	dst[j++] = &event_attrs[k].attr.attr;
	dst[j++] = &event_unit_attrs[k].attr;
	dst[j++] = &event_scale_attrs[k].attr;

	k++;

	/* fill each channel event */
	for (i = 0; i < pmu->info.hw_info->chann_nr; i++, k++) {
		dst[j++] = &event_attrs[k].attr.attr;
		dst[j++] = &event_unit_attrs[k].attr;
		dst[j++] = &event_scale_attrs[k].attr;
	}

	dst[j] = NULL; /* mark end */
}

static void fmt_attr_fill(struct attribute **fmt_attr)
{
	ddr_perf_format_attr_group.attrs = fmt_attr;
}

static int ddr_pmu_parse_dt(struct platform_device *pdev,
			    struct dmc_info *info)
{
	void __iomem *base;
	int i, ret;

	info->hw_info = of_device_get_match_data(&pdev->dev);

	for (i = 0; i < info->hw_info->dmc_nr; i++) {
		/* resource 0 for ddr register base */
		base = devm_platform_ioremap_resource(pdev, i);
		if (IS_ERR(base))
			return PTR_ERR(base);

		info->ddr_reg[i] = base;
	}

	/* resource i for pll register base */
	base = devm_platform_ioremap_resource(pdev, i);
	if (IS_ERR(base))
		return PTR_ERR(base);

	info->pll_reg = base;

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;

	info->irq_num = ret;

	ret = devm_request_irq(&pdev->dev, info->irq_num, dmc_irq_handler,
			       IRQF_NOBALANCING, dev_name(&pdev->dev),
			       (void *)info);
	if (ret < 0)
		return ret;

	return 0;
}

int meson_ddr_pmu_create(struct platform_device *pdev)
{
	int ret;
	char *name;
	struct ddr_pmu *pmu;

	pmu = devm_kzalloc(&pdev->dev, sizeof(struct ddr_pmu), GFP_KERNEL);
	if (!pmu)
		return -ENOMEM;

	*pmu = (struct ddr_pmu) {
		.pmu = {
			.module		= THIS_MODULE,
			.parent		= &pdev->dev,
			.capabilities	= PERF_PMU_CAP_NO_EXCLUDE,
			.task_ctx_nr	= perf_invalid_context,
			.attr_groups	= attr_groups,
			.event_init	= meson_ddr_perf_event_init,
			.add		= meson_ddr_perf_event_add,
			.del		= meson_ddr_perf_event_del,
			.start		= meson_ddr_perf_event_start,
			.stop		= meson_ddr_perf_event_stop,
			.read		= meson_ddr_perf_event_update,
		},
	};

	ret = ddr_pmu_parse_dt(pdev, &pmu->info);
	if (ret < 0)
		return ret;

	fmt_attr_fill(pmu->info.hw_info->fmt_attr);

	pmu->cpu = smp_processor_id();

	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, DDR_PERF_DEV_NAME);
	if (!name)
		return -ENOMEM;

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN, name, NULL,
				      ddr_perf_offline_cpu);
	if (ret < 0)
		return ret;

	pmu->cpuhp_state = ret;

	/* Register the pmu instance for cpu hotplug */
	ret = cpuhp_state_add_instance_nocalls(pmu->cpuhp_state, &pmu->node);
	if (ret)
		goto cpuhp_instance_err;

	fill_event_attr(pmu);

	ret = perf_pmu_register(&pmu->pmu, name, -1);
	if (ret)
		goto pmu_register_err;

	pmu->name = name;
	pmu->dev = &pdev->dev;
	pmu->pmu_enabled = false;

	platform_set_drvdata(pdev, pmu);

	return 0;

pmu_register_err:
	cpuhp_state_remove_instance_nocalls(pmu->cpuhp_state, &pmu->node);

cpuhp_instance_err:
	cpuhp_remove_state(pmu->cpuhp_state);

	return ret;
}

int meson_ddr_pmu_remove(struct platform_device *pdev)
{
	struct ddr_pmu *pmu = platform_get_drvdata(pdev);

	perf_pmu_unregister(&pmu->pmu);
	cpuhp_state_remove_instance_nocalls(pmu->cpuhp_state, &pmu->node);
	cpuhp_remove_state(pmu->cpuhp_state);

	return 0;
}

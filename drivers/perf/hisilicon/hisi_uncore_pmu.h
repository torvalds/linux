/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * HiSilicon SoC Hardware event counters support
 *
 * Copyright (C) 2017 HiSilicon Limited
 * Author: Anurup M <anurup.m@huawei.com>
 *         Shaokun Zhang <zhangshaokun@hisilicon.com>
 *
 * This code is based on the uncore PMUs like arm-cci and arm-ccn.
 */
#ifndef __HISI_UNCORE_PMU_H__
#define __HISI_UNCORE_PMU_H__

#include <linux/bitfield.h>
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#undef pr_fmt
#define pr_fmt(fmt)     "hisi_pmu: " fmt

#define HISI_PMU_V2		0x30
#define HISI_MAX_COUNTERS 0x10
#define to_hisi_pmu(p)	(container_of(p, struct hisi_pmu, pmu))

#define HISI_PMU_ATTR(_name, _func, _config)				\
	(&((struct dev_ext_attribute[]) {				\
		{ __ATTR(_name, 0444, _func, NULL), (void *)_config }   \
	})[0].attr.attr)

#define HISI_PMU_FORMAT_ATTR(_name, _config)		\
	HISI_PMU_ATTR(_name, hisi_format_sysfs_show, (void *)_config)
#define HISI_PMU_EVENT_ATTR(_name, _config)		\
	HISI_PMU_ATTR(_name, hisi_event_sysfs_show, (unsigned long)_config)

#define HISI_PMU_EVENT_ATTR_EXTRACTOR(name, config, hi, lo)        \
	static inline u32 hisi_get_##name(struct perf_event *event)            \
	{                                                                  \
		return FIELD_GET(GENMASK_ULL(hi, lo), event->attr.config);  \
	}

#define HISI_GET_EVENTID(ev) (ev->hw.config_base & 0xff)

#define HISI_PMU_EVTYPE_BITS		8
#define HISI_PMU_EVTYPE_SHIFT(idx)	((idx) % 4 * HISI_PMU_EVTYPE_BITS)

struct hisi_pmu;

struct hisi_uncore_ops {
	int (*check_filter)(struct perf_event *event);
	void (*write_evtype)(struct hisi_pmu *, int, u32);
	int (*get_event_idx)(struct perf_event *);
	u64 (*read_counter)(struct hisi_pmu *, struct hw_perf_event *);
	void (*write_counter)(struct hisi_pmu *, struct hw_perf_event *, u64);
	void (*enable_counter)(struct hisi_pmu *, struct hw_perf_event *);
	void (*disable_counter)(struct hisi_pmu *, struct hw_perf_event *);
	void (*enable_counter_int)(struct hisi_pmu *, struct hw_perf_event *);
	void (*disable_counter_int)(struct hisi_pmu *, struct hw_perf_event *);
	void (*start_counters)(struct hisi_pmu *);
	void (*stop_counters)(struct hisi_pmu *);
	u32 (*get_int_status)(struct hisi_pmu *hisi_pmu);
	void (*clear_int_status)(struct hisi_pmu *hisi_pmu, int idx);
	void (*enable_filter)(struct perf_event *event);
	void (*disable_filter)(struct perf_event *event);
};

/* Describes the HISI PMU chip features information */
struct hisi_pmu_dev_info {
	const char *name;
	const struct attribute_group **attr_groups;
	void *private;
};

struct hisi_pmu_hwevents {
	struct perf_event *hw_events[HISI_MAX_COUNTERS];
	DECLARE_BITMAP(used_mask, HISI_MAX_COUNTERS);
	const struct attribute_group **attr_groups;
};

/* Generic pmu struct for different pmu types */
struct hisi_pmu {
	struct pmu pmu;
	const struct hisi_uncore_ops *ops;
	const struct hisi_pmu_dev_info *dev_info;
	struct hisi_pmu_hwevents pmu_events;
	/* associated_cpus: All CPUs associated with the PMU */
	cpumask_t associated_cpus;
	/* CPU used for counting */
	int on_cpu;
	int irq;
	struct device *dev;
	struct hlist_node node;
	int sccl_id;
	int sicl_id;
	int ccl_id;
	void __iomem *base;
	/* the ID of the PMU modules */
	u32 index_id;
	/* For DDRC PMU v2: each DDRC has more than one DMC */
	u32 sub_id;
	int num_counters;
	int counter_bits;
	/* check event code range */
	int check_event;
	u32 identifier;
};

int hisi_uncore_pmu_get_event_idx(struct perf_event *event);
void hisi_uncore_pmu_read(struct perf_event *event);
int hisi_uncore_pmu_add(struct perf_event *event, int flags);
void hisi_uncore_pmu_del(struct perf_event *event, int flags);
void hisi_uncore_pmu_start(struct perf_event *event, int flags);
void hisi_uncore_pmu_stop(struct perf_event *event, int flags);
void hisi_uncore_pmu_set_event_period(struct perf_event *event);
void hisi_uncore_pmu_event_update(struct perf_event *event);
int hisi_uncore_pmu_event_init(struct perf_event *event);
void hisi_uncore_pmu_enable(struct pmu *pmu);
void hisi_uncore_pmu_disable(struct pmu *pmu);
ssize_t hisi_event_sysfs_show(struct device *dev,
			      struct device_attribute *attr, char *buf);
ssize_t hisi_format_sysfs_show(struct device *dev,
			       struct device_attribute *attr, char *buf);
ssize_t hisi_cpumask_sysfs_show(struct device *dev,
				struct device_attribute *attr, char *buf);
int hisi_uncore_pmu_online_cpu(unsigned int cpu, struct hlist_node *node);
int hisi_uncore_pmu_offline_cpu(unsigned int cpu, struct hlist_node *node);

ssize_t hisi_uncore_pmu_identifier_attr_show(struct device *dev,
					     struct device_attribute *attr,
					     char *page);
int hisi_uncore_pmu_init_irq(struct hisi_pmu *hisi_pmu,
			     struct platform_device *pdev);

void hisi_pmu_init(struct hisi_pmu *hisi_pmu, struct module *module);
#endif /* __HISI_UNCORE_PMU_H__ */

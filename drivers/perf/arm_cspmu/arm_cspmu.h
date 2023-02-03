/* SPDX-License-Identifier: GPL-2.0
 *
 * ARM CoreSight Architecture PMU driver.
 * Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 */

#ifndef __ARM_CSPMU_H__
#define __ARM_CSPMU_H__

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#define to_arm_cspmu(p) (container_of(p, struct arm_cspmu, pmu))

#define ARM_CSPMU_EXT_ATTR(_name, _func, _config)			\
	(&((struct dev_ext_attribute[]){				\
		{							\
			.attr = __ATTR(_name, 0444, _func, NULL),	\
			.var = (void *)_config				\
		}							\
	})[0].attr.attr)

#define ARM_CSPMU_FORMAT_ATTR(_name, _config)				\
	ARM_CSPMU_EXT_ATTR(_name, arm_cspmu_sysfs_format_show, (char *)_config)

#define ARM_CSPMU_EVENT_ATTR(_name, _config)				\
	PMU_EVENT_ATTR_ID(_name, arm_cspmu_sysfs_event_show, _config)


/* Default event id mask */
#define ARM_CSPMU_EVENT_MASK	GENMASK_ULL(63, 0)

/* Default filter value mask */
#define ARM_CSPMU_FILTER_MASK	GENMASK_ULL(63, 0)

/* Default event format */
#define ARM_CSPMU_FORMAT_EVENT_ATTR	\
	ARM_CSPMU_FORMAT_ATTR(event, "config:0-32")

/* Default filter format */
#define ARM_CSPMU_FORMAT_FILTER_ATTR	\
	ARM_CSPMU_FORMAT_ATTR(filter, "config1:0-31")

/*
 * This is the default event number for cycle count, if supported, since the
 * ARM Coresight PMU specification does not define a standard event code
 * for cycle count.
 */
#define ARM_CSPMU_EVT_CYCLES_DEFAULT	(0x1ULL << 32)

/*
 * The ARM Coresight PMU supports up to 256 event counters.
 * If the counters are larger-than 32-bits, then the PMU includes at
 * most 128 counters.
 */
#define ARM_CSPMU_MAX_HW_CNTRS		256

/* The cycle counter, if implemented, is located at counter[31]. */
#define ARM_CSPMU_CYCLE_CNTR_IDX	31

/* PMIIDR register field */
#define ARM_CSPMU_PMIIDR_IMPLEMENTER	GENMASK(11, 0)
#define ARM_CSPMU_PMIIDR_PRODUCTID	GENMASK(31, 20)

struct arm_cspmu;

/* This tracks the events assigned to each counter in the PMU. */
struct arm_cspmu_hw_events {
	/* The events that are active on the PMU for a given logical index. */
	struct perf_event **events;

	/*
	 * Each bit indicates a logical counter is being used (or not) for an
	 * event. If cycle counter is supported and there is a gap between
	 * regular and cycle counter, the last logical counter is mapped to
	 * cycle counter. Otherwise, logical and physical have 1-to-1 mapping.
	 */
	DECLARE_BITMAP(used_ctrs, ARM_CSPMU_MAX_HW_CNTRS);
};

/* Contains ops to query vendor/implementer specific attribute. */
struct arm_cspmu_impl_ops {
	/* Get event attributes */
	struct attribute **(*get_event_attrs)(const struct arm_cspmu *cspmu);
	/* Get format attributes */
	struct attribute **(*get_format_attrs)(const struct arm_cspmu *cspmu);
	/* Get string identifier */
	const char *(*get_identifier)(const struct arm_cspmu *cspmu);
	/* Get PMU name to register to core perf */
	const char *(*get_name)(const struct arm_cspmu *cspmu);
	/* Check if the event corresponds to cycle count event */
	bool (*is_cycle_counter_event)(const struct perf_event *event);
	/* Decode event type/id from configs */
	u32 (*event_type)(const struct perf_event *event);
	/* Decode filter value from configs */
	u32 (*event_filter)(const struct perf_event *event);
	/* Hide/show unsupported events */
	umode_t (*event_attr_is_visible)(struct kobject *kobj,
					 struct attribute *attr, int unused);
};

/* Vendor/implementer descriptor. */
struct arm_cspmu_impl {
	u32 pmiidr;
	struct arm_cspmu_impl_ops ops;
	void *ctx;
};

/* Coresight PMU descriptor. */
struct arm_cspmu {
	struct pmu pmu;
	struct device *dev;
	struct acpi_apmt_node *apmt_node;
	const char *name;
	const char *identifier;
	void __iomem *base0;
	void __iomem *base1;
	int irq;
	cpumask_t associated_cpus;
	cpumask_t active_cpu;
	struct hlist_node cpuhp_node;

	u32 pmcfgr;
	u32 num_logical_ctrs;
	u32 num_set_clr_reg;
	int cycle_counter_logical_idx;

	struct arm_cspmu_hw_events hw_events;

	struct arm_cspmu_impl impl;
};

/* Default function to show event attribute in sysfs. */
ssize_t arm_cspmu_sysfs_event_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf);

/* Default function to show format attribute in sysfs. */
ssize_t arm_cspmu_sysfs_format_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf);

#endif /* __ARM_CSPMU_H__ */

/* SPDX-License-Identifier: GPL-2.0
 *
 * ARM CoreSight Architecture PMU driver.
 * Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 */

#ifndef __ARM_CSPMU_H__
#define __ARM_CSPMU_H__

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
	ARM_CSPMU_EXT_ATTR(_name, device_show_string, _config)

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
#define ARM_CSPMU_FORMAT_FILTER2_ATTR	\
	ARM_CSPMU_FORMAT_ATTR(filter2, "config2:0-31")

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

/*
 * CoreSight PMU Arch register offsets.
 */
#define PMEVCNTR_LO			0x0
#define PMEVCNTR_HI			0x4
#define PMEVTYPER			0x400
#define PMCCFILTR			0x47C
#define PMEVFILT2R			0x800
#define PMEVFILTR			0xA00
#define PMCNTENSET			0xC00
#define PMCNTENCLR			0xC20
#define PMINTENSET			0xC40
#define PMINTENCLR			0xC60
#define PMOVSCLR			0xC80
#define PMOVSSET			0xCC0
#define PMIMPDEF			0xD80
#define PMCFGR				0xE00
#define PMCR				0xE04
#define PMIIDR				0xE08

/* PMCFGR register field */
#define PMCFGR_NCG			GENMASK(31, 28)
#define PMCFGR_HDBG			BIT(24)
#define PMCFGR_TRO			BIT(23)
#define PMCFGR_SS			BIT(22)
#define PMCFGR_FZO			BIT(21)
#define PMCFGR_MSI			BIT(20)
#define PMCFGR_UEN			BIT(19)
#define PMCFGR_NA			BIT(17)
#define PMCFGR_EX			BIT(16)
#define PMCFGR_CCD			BIT(15)
#define PMCFGR_CC			BIT(14)
#define PMCFGR_SIZE			GENMASK(13, 8)
#define PMCFGR_N			GENMASK(7, 0)

/* PMCR register field */
#define PMCR_TRO			BIT(11)
#define PMCR_HDBG			BIT(10)
#define PMCR_FZO			BIT(9)
#define PMCR_NA				BIT(8)
#define PMCR_DP				BIT(5)
#define PMCR_X				BIT(4)
#define PMCR_D				BIT(3)
#define PMCR_C				BIT(2)
#define PMCR_P				BIT(1)
#define PMCR_E				BIT(0)

/* PMIIDR register field */
#define ARM_CSPMU_PMIIDR_IMPLEMENTER	GENMASK(11, 0)
#define ARM_CSPMU_PMIIDR_PRODUCTID	GENMASK(31, 20)

/* JEDEC-assigned JEP106 identification code */
#define ARM_CSPMU_IMPL_ID_NVIDIA	0x36B
#define ARM_CSPMU_IMPL_ID_AMPERE	0xA16

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
	/* Set event filters */
	void (*set_cc_filter)(struct arm_cspmu *cspmu,
			      const struct perf_event *event);
	void (*set_ev_filter)(struct arm_cspmu *cspmu,
			      const struct perf_event *event);
	/* Implementation specific event validation */
	int (*validate_event)(struct arm_cspmu *cspmu,
			      struct perf_event *event);
	/* Hide/show unsupported events */
	umode_t (*event_attr_is_visible)(struct kobject *kobj,
					 struct attribute *attr, int unused);
};

/* Vendor/implementer registration parameter. */
struct arm_cspmu_impl_match {
	/* Backend module. */
	struct module *module;
	const char *module_name;
	/* PMIIDR value/mask. */
	u32 pmiidr_val;
	u32 pmiidr_mask;
	/* Callback to vendor backend to init arm_cspmu_impl::ops. */
	int (*impl_init_ops)(struct arm_cspmu *cspmu);
};

/* Vendor/implementer descriptor. */
struct arm_cspmu_impl {
	u32 pmiidr;
	struct module *module;
	struct arm_cspmu_impl_match *match;
	struct arm_cspmu_impl_ops ops;
	void *ctx;
};

/* Coresight PMU descriptor. */
struct arm_cspmu {
	struct pmu pmu;
	struct device *dev;
	const char *name;
	const char *identifier;
	void __iomem *base0;
	void __iomem *base1;
	cpumask_t associated_cpus;
	cpumask_t active_cpu;
	struct hlist_node cpuhp_node;
	int irq;

	bool has_atomic_dword;
	u32 pmcfgr;
	u32 num_logical_ctrs;
	u32 num_set_clr_reg;
	int cycle_counter_logical_idx;

	struct arm_cspmu_hw_events hw_events;
	const struct attribute_group *attr_groups[5];

	struct arm_cspmu_impl impl;
};

/* Default function to show event attribute in sysfs. */
ssize_t arm_cspmu_sysfs_event_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf);

/* Register vendor backend. */
int arm_cspmu_impl_register(const struct arm_cspmu_impl_match *impl_match);

/* Unregister vendor backend. */
void arm_cspmu_impl_unregister(const struct arm_cspmu_impl_match *impl_match);

#endif /* __ARM_CSPMU_H__ */

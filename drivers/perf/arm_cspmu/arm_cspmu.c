// SPDX-License-Identifier: GPL-2.0
/*
 * ARM CoreSight Architecture PMU driver.
 *
 * This driver adds support for uncore PMU based on ARM CoreSight Performance
 * Monitoring Unit Architecture. The PMU is accessible via MMIO registers and
 * like other uncore PMUs, it does not support process specific events and
 * cannot be used in sampling mode.
 *
 * This code is based on other uncore PMUs like ARM DSU PMU. It provides a
 * generic implementation to operate the PMU according to CoreSight PMU
 * architecture and ACPI ARM PMU table (APMT) documents below:
 *   - ARM CoreSight PMU architecture document number: ARM IHI 0091 A.a-00bet0.
 *   - APMT document number: ARM DEN0117.
 *
 * The user should refer to the vendor technical documentation to get details
 * about the supported events.
 *
 * Copyright (c) 2022-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 */

#include <linux/acpi.h>
#include <linux/cacheinfo.h>
#include <linux/ctype.h>
#include <linux/interrupt.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>

#include "arm_cspmu.h"

#define PMUNAME "arm_cspmu"
#define DRVNAME "arm-cs-arch-pmu"

#define ARM_CSPMU_CPUMASK_ATTR(_name, _config)			\
	ARM_CSPMU_EXT_ATTR(_name, arm_cspmu_cpumask_show,	\
				(unsigned long)_config)

/*
 * CoreSight PMU Arch register offsets.
 */
#define PMEVCNTR_LO					0x0
#define PMEVCNTR_HI					0x4
#define PMEVTYPER					0x400
#define PMCCFILTR					0x47C
#define PMEVFILTR					0xA00
#define PMCNTENSET					0xC00
#define PMCNTENCLR					0xC20
#define PMINTENSET					0xC40
#define PMINTENCLR					0xC60
#define PMOVSCLR					0xC80
#define PMOVSSET					0xCC0
#define PMCFGR						0xE00
#define PMCR						0xE04
#define PMIIDR						0xE08

/* PMCFGR register field */
#define PMCFGR_NCG					GENMASK(31, 28)
#define PMCFGR_HDBG					BIT(24)
#define PMCFGR_TRO					BIT(23)
#define PMCFGR_SS					BIT(22)
#define PMCFGR_FZO					BIT(21)
#define PMCFGR_MSI					BIT(20)
#define PMCFGR_UEN					BIT(19)
#define PMCFGR_NA					BIT(17)
#define PMCFGR_EX					BIT(16)
#define PMCFGR_CCD					BIT(15)
#define PMCFGR_CC					BIT(14)
#define PMCFGR_SIZE					GENMASK(13, 8)
#define PMCFGR_N					GENMASK(7, 0)

/* PMCR register field */
#define PMCR_TRO					BIT(11)
#define PMCR_HDBG					BIT(10)
#define PMCR_FZO					BIT(9)
#define PMCR_NA						BIT(8)
#define PMCR_DP						BIT(5)
#define PMCR_X						BIT(4)
#define PMCR_D						BIT(3)
#define PMCR_C						BIT(2)
#define PMCR_P						BIT(1)
#define PMCR_E						BIT(0)

/* Each SET/CLR register supports up to 32 counters. */
#define ARM_CSPMU_SET_CLR_COUNTER_SHIFT		5
#define ARM_CSPMU_SET_CLR_COUNTER_NUM		\
	(1 << ARM_CSPMU_SET_CLR_COUNTER_SHIFT)

/* Convert counter idx into SET/CLR register number. */
#define COUNTER_TO_SET_CLR_ID(idx)			\
	(idx >> ARM_CSPMU_SET_CLR_COUNTER_SHIFT)

/* Convert counter idx into SET/CLR register bit. */
#define COUNTER_TO_SET_CLR_BIT(idx)			\
	(idx & (ARM_CSPMU_SET_CLR_COUNTER_NUM - 1))

#define ARM_CSPMU_ACTIVE_CPU_MASK		0x0
#define ARM_CSPMU_ASSOCIATED_CPU_MASK		0x1

/*
 * Maximum poll count for reading counter value using high-low-high sequence.
 */
#define HILOHI_MAX_POLL	1000

static unsigned long arm_cspmu_cpuhp_state;

static DEFINE_MUTEX(arm_cspmu_lock);

static void arm_cspmu_set_ev_filter(struct arm_cspmu *cspmu,
				    struct hw_perf_event *hwc, u32 filter);

static struct acpi_apmt_node *arm_cspmu_apmt_node(struct device *dev)
{
	struct acpi_apmt_node **ptr = dev_get_platdata(dev);

	return ptr ? *ptr : NULL;
}

/*
 * In CoreSight PMU architecture, all of the MMIO registers are 32-bit except
 * counter register. The counter register can be implemented as 32-bit or 64-bit
 * register depending on the value of PMCFGR.SIZE field. For 64-bit access,
 * single-copy 64-bit atomic support is implementation defined. APMT node flag
 * is used to identify if the PMU supports 64-bit single copy atomic. If 64-bit
 * single copy atomic is not supported, the driver treats the register as a pair
 * of 32-bit register.
 */

/*
 * Read 64-bit register as a pair of 32-bit registers using hi-lo-hi sequence.
 */
static u64 read_reg64_hilohi(const void __iomem *addr, u32 max_poll_count)
{
	u32 val_lo, val_hi;
	u64 val;

	/* Use high-low-high sequence to avoid tearing */
	do {
		if (max_poll_count-- == 0) {
			pr_err("ARM CSPMU: timeout hi-low-high sequence\n");
			return 0;
		}

		val_hi = readl(addr + 4);
		val_lo = readl(addr);
	} while (val_hi != readl(addr + 4));

	val = (((u64)val_hi << 32) | val_lo);

	return val;
}

/* Check if cycle counter is supported. */
static inline bool supports_cycle_counter(const struct arm_cspmu *cspmu)
{
	return (cspmu->pmcfgr & PMCFGR_CC);
}

/* Get counter size, which is (PMCFGR_SIZE + 1). */
static inline u32 counter_size(const struct arm_cspmu *cspmu)
{
	return FIELD_GET(PMCFGR_SIZE, cspmu->pmcfgr) + 1;
}

/* Get counter mask. */
static inline u64 counter_mask(const struct arm_cspmu *cspmu)
{
	return GENMASK_ULL(counter_size(cspmu) - 1, 0);
}

/* Check if counter is implemented as 64-bit register. */
static inline bool use_64b_counter_reg(const struct arm_cspmu *cspmu)
{
	return (counter_size(cspmu) > 32);
}

ssize_t arm_cspmu_sysfs_event_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct perf_pmu_events_attr *pmu_attr;

	pmu_attr = container_of(attr, typeof(*pmu_attr), attr);
	return sysfs_emit(buf, "event=0x%llx\n", pmu_attr->id);
}
EXPORT_SYMBOL_GPL(arm_cspmu_sysfs_event_show);

/* Default event list. */
static struct attribute *arm_cspmu_event_attrs[] = {
	ARM_CSPMU_EVENT_ATTR(cycles, ARM_CSPMU_EVT_CYCLES_DEFAULT),
	NULL,
};

static struct attribute **
arm_cspmu_get_event_attrs(const struct arm_cspmu *cspmu)
{
	struct attribute **attrs;

	attrs = devm_kmemdup(cspmu->dev, arm_cspmu_event_attrs,
		sizeof(arm_cspmu_event_attrs), GFP_KERNEL);

	return attrs;
}

static umode_t
arm_cspmu_event_attr_is_visible(struct kobject *kobj,
				struct attribute *attr, int unused)
{
	struct device *dev = kobj_to_dev(kobj);
	struct arm_cspmu *cspmu = to_arm_cspmu(dev_get_drvdata(dev));
	struct perf_pmu_events_attr *eattr;

	eattr = container_of(attr, typeof(*eattr), attr.attr);

	/* Hide cycle event if not supported */
	if (!supports_cycle_counter(cspmu) &&
	    eattr->id == ARM_CSPMU_EVT_CYCLES_DEFAULT)
		return 0;

	return attr->mode;
}

static struct attribute *arm_cspmu_format_attrs[] = {
	ARM_CSPMU_FORMAT_EVENT_ATTR,
	ARM_CSPMU_FORMAT_FILTER_ATTR,
	NULL,
};

static struct attribute **
arm_cspmu_get_format_attrs(const struct arm_cspmu *cspmu)
{
	struct attribute **attrs;

	attrs = devm_kmemdup(cspmu->dev, arm_cspmu_format_attrs,
		sizeof(arm_cspmu_format_attrs), GFP_KERNEL);

	return attrs;
}

static u32 arm_cspmu_event_type(const struct perf_event *event)
{
	return event->attr.config & ARM_CSPMU_EVENT_MASK;
}

static bool arm_cspmu_is_cycle_counter_event(const struct perf_event *event)
{
	return (event->attr.config == ARM_CSPMU_EVT_CYCLES_DEFAULT);
}

static u32 arm_cspmu_event_filter(const struct perf_event *event)
{
	return event->attr.config1 & ARM_CSPMU_FILTER_MASK;
}

static ssize_t arm_cspmu_identifier_show(struct device *dev,
					 struct device_attribute *attr,
					 char *page)
{
	struct arm_cspmu *cspmu = to_arm_cspmu(dev_get_drvdata(dev));

	return sysfs_emit(page, "%s\n", cspmu->identifier);
}

static struct device_attribute arm_cspmu_identifier_attr =
	__ATTR(identifier, 0444, arm_cspmu_identifier_show, NULL);

static struct attribute *arm_cspmu_identifier_attrs[] = {
	&arm_cspmu_identifier_attr.attr,
	NULL,
};

static struct attribute_group arm_cspmu_identifier_attr_group = {
	.attrs = arm_cspmu_identifier_attrs,
};

static const char *arm_cspmu_get_identifier(const struct arm_cspmu *cspmu)
{
	const char *identifier =
		devm_kasprintf(cspmu->dev, GFP_KERNEL, "%x",
			       cspmu->impl.pmiidr);
	return identifier;
}

static const char *arm_cspmu_type_str[ACPI_APMT_NODE_TYPE_COUNT] = {
	"mc",
	"smmu",
	"pcie",
	"acpi",
	"cache",
};

static const char *arm_cspmu_get_name(const struct arm_cspmu *cspmu)
{
	struct device *dev;
	struct acpi_apmt_node *apmt_node;
	u8 pmu_type;
	char *name;
	char acpi_hid_string[ACPI_ID_LEN] = { 0 };
	static atomic_t pmu_idx[ACPI_APMT_NODE_TYPE_COUNT] = { 0 };

	dev = cspmu->dev;
	apmt_node = arm_cspmu_apmt_node(dev);
	if (!apmt_node)
		return devm_kasprintf(dev, GFP_KERNEL, PMUNAME "_%u",
				      atomic_fetch_inc(&pmu_idx[0]));

	pmu_type = apmt_node->type;

	if (pmu_type >= ACPI_APMT_NODE_TYPE_COUNT) {
		dev_err(dev, "unsupported PMU type-%u\n", pmu_type);
		return NULL;
	}

	if (pmu_type == ACPI_APMT_NODE_TYPE_ACPI) {
		memcpy(acpi_hid_string,
			&apmt_node->inst_primary,
			sizeof(apmt_node->inst_primary));
		name = devm_kasprintf(dev, GFP_KERNEL, "%s_%s_%s_%u", PMUNAME,
				      arm_cspmu_type_str[pmu_type],
				      acpi_hid_string,
				      apmt_node->inst_secondary);
	} else {
		name = devm_kasprintf(dev, GFP_KERNEL, "%s_%s_%d", PMUNAME,
				      arm_cspmu_type_str[pmu_type],
				      atomic_fetch_inc(&pmu_idx[pmu_type]));
	}

	return name;
}

static ssize_t arm_cspmu_cpumask_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct pmu *pmu = dev_get_drvdata(dev);
	struct arm_cspmu *cspmu = to_arm_cspmu(pmu);
	struct dev_ext_attribute *eattr =
		container_of(attr, struct dev_ext_attribute, attr);
	unsigned long mask_id = (unsigned long)eattr->var;
	const cpumask_t *cpumask;

	switch (mask_id) {
	case ARM_CSPMU_ACTIVE_CPU_MASK:
		cpumask = &cspmu->active_cpu;
		break;
	case ARM_CSPMU_ASSOCIATED_CPU_MASK:
		cpumask = &cspmu->associated_cpus;
		break;
	default:
		return 0;
	}
	return cpumap_print_to_pagebuf(true, buf, cpumask);
}

static struct attribute *arm_cspmu_cpumask_attrs[] = {
	ARM_CSPMU_CPUMASK_ATTR(cpumask, ARM_CSPMU_ACTIVE_CPU_MASK),
	ARM_CSPMU_CPUMASK_ATTR(associated_cpus, ARM_CSPMU_ASSOCIATED_CPU_MASK),
	NULL,
};

static struct attribute_group arm_cspmu_cpumask_attr_group = {
	.attrs = arm_cspmu_cpumask_attrs,
};

static struct arm_cspmu_impl_match impl_match[] = {
	{
		.module_name	= "nvidia_cspmu",
		.pmiidr_val	= ARM_CSPMU_IMPL_ID_NVIDIA,
		.pmiidr_mask	= ARM_CSPMU_PMIIDR_IMPLEMENTER,
		.module		= NULL,
		.impl_init_ops	= NULL,
	},
	{
		.module_name	= "ampere_cspmu",
		.pmiidr_val	= ARM_CSPMU_IMPL_ID_AMPERE,
		.pmiidr_mask	= ARM_CSPMU_PMIIDR_IMPLEMENTER,
		.module		= NULL,
		.impl_init_ops	= NULL,
	},

	{0}
};

static struct arm_cspmu_impl_match *arm_cspmu_impl_match_get(u32 pmiidr)
{
	struct arm_cspmu_impl_match *match = impl_match;

	for (; match->pmiidr_val; match++) {
		u32 mask = match->pmiidr_mask;

		if ((match->pmiidr_val & mask) == (pmiidr & mask))
			return match;
	}

	return NULL;
}

#define DEFAULT_IMPL_OP(name)	.name = arm_cspmu_##name

static int arm_cspmu_init_impl_ops(struct arm_cspmu *cspmu)
{
	int ret = 0;
	struct acpi_apmt_node *apmt_node = arm_cspmu_apmt_node(cspmu->dev);
	struct arm_cspmu_impl_match *match;

	/* Start with a default PMU implementation */
	cspmu->impl.module = THIS_MODULE;
	cspmu->impl.pmiidr = readl(cspmu->base0 + PMIIDR);
	cspmu->impl.ops = (struct arm_cspmu_impl_ops) {
		DEFAULT_IMPL_OP(get_event_attrs),
		DEFAULT_IMPL_OP(get_format_attrs),
		DEFAULT_IMPL_OP(get_identifier),
		DEFAULT_IMPL_OP(get_name),
		DEFAULT_IMPL_OP(is_cycle_counter_event),
		DEFAULT_IMPL_OP(event_type),
		DEFAULT_IMPL_OP(event_filter),
		DEFAULT_IMPL_OP(set_ev_filter),
		DEFAULT_IMPL_OP(event_attr_is_visible),
	};

	/* Firmware may override implementer/product ID from PMIIDR */
	if (apmt_node && apmt_node->impl_id)
		cspmu->impl.pmiidr = apmt_node->impl_id;

	/* Find implementer specific attribute ops. */
	match = arm_cspmu_impl_match_get(cspmu->impl.pmiidr);

	/* Load implementer module and initialize the callbacks. */
	if (match) {
		mutex_lock(&arm_cspmu_lock);

		if (match->impl_init_ops) {
			/* Prevent unload until PMU registration is done. */
			if (try_module_get(match->module)) {
				cspmu->impl.module = match->module;
				cspmu->impl.match = match;
				ret = match->impl_init_ops(cspmu);
				if (ret)
					module_put(match->module);
			} else {
				WARN(1, "arm_cspmu failed to get module: %s\n",
					match->module_name);
				ret = -EINVAL;
			}
		} else {
			request_module_nowait(match->module_name);
			ret = -EPROBE_DEFER;
		}

		mutex_unlock(&arm_cspmu_lock);
	}

	return ret;
}

static struct attribute_group *
arm_cspmu_alloc_event_attr_group(struct arm_cspmu *cspmu)
{
	struct attribute_group *event_group;
	struct device *dev = cspmu->dev;
	const struct arm_cspmu_impl_ops *impl_ops = &cspmu->impl.ops;

	event_group =
		devm_kzalloc(dev, sizeof(struct attribute_group), GFP_KERNEL);
	if (!event_group)
		return NULL;

	event_group->name = "events";
	event_group->is_visible = impl_ops->event_attr_is_visible;
	event_group->attrs = impl_ops->get_event_attrs(cspmu);

	if (!event_group->attrs)
		return NULL;

	return event_group;
}

static struct attribute_group *
arm_cspmu_alloc_format_attr_group(struct arm_cspmu *cspmu)
{
	struct attribute_group *format_group;
	struct device *dev = cspmu->dev;

	format_group =
		devm_kzalloc(dev, sizeof(struct attribute_group), GFP_KERNEL);
	if (!format_group)
		return NULL;

	format_group->name = "format";
	format_group->attrs = cspmu->impl.ops.get_format_attrs(cspmu);

	if (!format_group->attrs)
		return NULL;

	return format_group;
}

static int arm_cspmu_alloc_attr_groups(struct arm_cspmu *cspmu)
{
	const struct attribute_group **attr_groups = cspmu->attr_groups;
	const struct arm_cspmu_impl_ops *impl_ops = &cspmu->impl.ops;

	cspmu->identifier = impl_ops->get_identifier(cspmu);
	cspmu->name = impl_ops->get_name(cspmu);

	if (!cspmu->identifier || !cspmu->name)
		return -ENOMEM;

	attr_groups[0] = arm_cspmu_alloc_event_attr_group(cspmu);
	attr_groups[1] = arm_cspmu_alloc_format_attr_group(cspmu);
	attr_groups[2] = &arm_cspmu_identifier_attr_group;
	attr_groups[3] = &arm_cspmu_cpumask_attr_group;

	if (!attr_groups[0] || !attr_groups[1])
		return -ENOMEM;

	return 0;
}

static inline void arm_cspmu_reset_counters(struct arm_cspmu *cspmu)
{
	writel(PMCR_C | PMCR_P, cspmu->base0 + PMCR);
}

static inline void arm_cspmu_start_counters(struct arm_cspmu *cspmu)
{
	writel(PMCR_E, cspmu->base0 + PMCR);
}

static inline void arm_cspmu_stop_counters(struct arm_cspmu *cspmu)
{
	writel(0, cspmu->base0 + PMCR);
}

static void arm_cspmu_enable(struct pmu *pmu)
{
	bool disabled;
	struct arm_cspmu *cspmu = to_arm_cspmu(pmu);

	disabled = bitmap_empty(cspmu->hw_events.used_ctrs,
				cspmu->num_logical_ctrs);

	if (disabled)
		return;

	arm_cspmu_start_counters(cspmu);
}

static void arm_cspmu_disable(struct pmu *pmu)
{
	struct arm_cspmu *cspmu = to_arm_cspmu(pmu);

	arm_cspmu_stop_counters(cspmu);
}

static int arm_cspmu_get_event_idx(struct arm_cspmu_hw_events *hw_events,
				struct perf_event *event)
{
	int idx, ret;
	struct arm_cspmu *cspmu = to_arm_cspmu(event->pmu);

	if (supports_cycle_counter(cspmu)) {
		if (cspmu->impl.ops.is_cycle_counter_event(event)) {
			/* Search for available cycle counter. */
			if (test_and_set_bit(cspmu->cycle_counter_logical_idx,
					     hw_events->used_ctrs))
				return -EAGAIN;

			return cspmu->cycle_counter_logical_idx;
		}

		/*
		 * Search a regular counter from the used counter bitmap.
		 * The cycle counter divides the bitmap into two parts. Search
		 * the first then second half to exclude the cycle counter bit.
		 */
		idx = find_first_zero_bit(hw_events->used_ctrs,
					  cspmu->cycle_counter_logical_idx);
		if (idx >= cspmu->cycle_counter_logical_idx) {
			idx = find_next_zero_bit(
				hw_events->used_ctrs,
				cspmu->num_logical_ctrs,
				cspmu->cycle_counter_logical_idx + 1);
		}
	} else {
		idx = find_first_zero_bit(hw_events->used_ctrs,
					  cspmu->num_logical_ctrs);
	}

	if (idx >= cspmu->num_logical_ctrs)
		return -EAGAIN;

	if (cspmu->impl.ops.validate_event) {
		ret = cspmu->impl.ops.validate_event(cspmu, event);
		if (ret)
			return ret;
	}

	set_bit(idx, hw_events->used_ctrs);

	return idx;
}

static bool arm_cspmu_validate_event(struct pmu *pmu,
				 struct arm_cspmu_hw_events *hw_events,
				 struct perf_event *event)
{
	if (is_software_event(event))
		return true;

	/* Reject groups spanning multiple HW PMUs. */
	if (event->pmu != pmu)
		return false;

	return (arm_cspmu_get_event_idx(hw_events, event) >= 0);
}

/*
 * Make sure the group of events can be scheduled at once
 * on the PMU.
 */
static bool arm_cspmu_validate_group(struct perf_event *event)
{
	struct perf_event *sibling, *leader = event->group_leader;
	struct arm_cspmu_hw_events fake_hw_events;

	if (event->group_leader == event)
		return true;

	memset(&fake_hw_events, 0, sizeof(fake_hw_events));

	if (!arm_cspmu_validate_event(event->pmu, &fake_hw_events, leader))
		return false;

	for_each_sibling_event(sibling, leader) {
		if (!arm_cspmu_validate_event(event->pmu, &fake_hw_events,
						  sibling))
			return false;
	}

	return arm_cspmu_validate_event(event->pmu, &fake_hw_events, event);
}

static int arm_cspmu_event_init(struct perf_event *event)
{
	struct arm_cspmu *cspmu;
	struct hw_perf_event *hwc = &event->hw;

	cspmu = to_arm_cspmu(event->pmu);

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/*
	 * Following other "uncore" PMUs, we do not support sampling mode or
	 * attach to a task (per-process mode).
	 */
	if (is_sampling_event(event)) {
		dev_dbg(cspmu->pmu.dev,
			"Can't support sampling events\n");
		return -EOPNOTSUPP;
	}

	if (event->cpu < 0 || event->attach_state & PERF_ATTACH_TASK) {
		dev_dbg(cspmu->pmu.dev,
			"Can't support per-task counters\n");
		return -EINVAL;
	}

	/*
	 * Make sure the CPU assignment is on one of the CPUs associated with
	 * this PMU.
	 */
	if (!cpumask_test_cpu(event->cpu, &cspmu->associated_cpus)) {
		dev_dbg(cspmu->pmu.dev,
			"Requested cpu is not associated with the PMU\n");
		return -EINVAL;
	}

	/* Enforce the current active CPU to handle the events in this PMU. */
	event->cpu = cpumask_first(&cspmu->active_cpu);
	if (event->cpu >= nr_cpu_ids)
		return -EINVAL;

	if (!arm_cspmu_validate_group(event))
		return -EINVAL;

	/*
	 * The logical counter id is tracked with hw_perf_event.extra_reg.idx.
	 * The physical counter id is tracked with hw_perf_event.idx.
	 * We don't assign an index until we actually place the event onto
	 * hardware. Use -1 to signify that we haven't decided where to put it
	 * yet.
	 */
	hwc->idx = -1;
	hwc->extra_reg.idx = -1;
	hwc->config = cspmu->impl.ops.event_type(event);

	return 0;
}

static inline u32 counter_offset(u32 reg_sz, u32 ctr_idx)
{
	return (PMEVCNTR_LO + (reg_sz * ctr_idx));
}

static void arm_cspmu_write_counter(struct perf_event *event, u64 val)
{
	u32 offset;
	struct arm_cspmu *cspmu = to_arm_cspmu(event->pmu);

	if (use_64b_counter_reg(cspmu)) {
		offset = counter_offset(sizeof(u64), event->hw.idx);

		if (cspmu->has_atomic_dword)
			writeq(val, cspmu->base1 + offset);
		else
			lo_hi_writeq(val, cspmu->base1 + offset);
	} else {
		offset = counter_offset(sizeof(u32), event->hw.idx);

		writel(lower_32_bits(val), cspmu->base1 + offset);
	}
}

static u64 arm_cspmu_read_counter(struct perf_event *event)
{
	u32 offset;
	const void __iomem *counter_addr;
	struct arm_cspmu *cspmu = to_arm_cspmu(event->pmu);

	if (use_64b_counter_reg(cspmu)) {
		offset = counter_offset(sizeof(u64), event->hw.idx);
		counter_addr = cspmu->base1 + offset;

		return cspmu->has_atomic_dword ?
			       readq(counter_addr) :
			       read_reg64_hilohi(counter_addr, HILOHI_MAX_POLL);
	}

	offset = counter_offset(sizeof(u32), event->hw.idx);
	return readl(cspmu->base1 + offset);
}

/*
 * arm_cspmu_set_event_period: Set the period for the counter.
 *
 * To handle cases of extreme interrupt latency, we program
 * the counter with half of the max count for the counters.
 */
static void arm_cspmu_set_event_period(struct perf_event *event)
{
	struct arm_cspmu *cspmu = to_arm_cspmu(event->pmu);
	u64 val = counter_mask(cspmu) >> 1ULL;

	local64_set(&event->hw.prev_count, val);
	arm_cspmu_write_counter(event, val);
}

static void arm_cspmu_enable_counter(struct arm_cspmu *cspmu, int idx)
{
	u32 reg_id, reg_bit, inten_off, cnten_off;

	reg_id = COUNTER_TO_SET_CLR_ID(idx);
	reg_bit = COUNTER_TO_SET_CLR_BIT(idx);

	inten_off = PMINTENSET + (4 * reg_id);
	cnten_off = PMCNTENSET + (4 * reg_id);

	writel(BIT(reg_bit), cspmu->base0 + inten_off);
	writel(BIT(reg_bit), cspmu->base0 + cnten_off);
}

static void arm_cspmu_disable_counter(struct arm_cspmu *cspmu, int idx)
{
	u32 reg_id, reg_bit, inten_off, cnten_off;

	reg_id = COUNTER_TO_SET_CLR_ID(idx);
	reg_bit = COUNTER_TO_SET_CLR_BIT(idx);

	inten_off = PMINTENCLR + (4 * reg_id);
	cnten_off = PMCNTENCLR + (4 * reg_id);

	writel(BIT(reg_bit), cspmu->base0 + cnten_off);
	writel(BIT(reg_bit), cspmu->base0 + inten_off);
}

static void arm_cspmu_event_update(struct perf_event *event)
{
	struct arm_cspmu *cspmu = to_arm_cspmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	u64 delta, prev, now;

	do {
		prev = local64_read(&hwc->prev_count);
		now = arm_cspmu_read_counter(event);
	} while (local64_cmpxchg(&hwc->prev_count, prev, now) != prev);

	delta = (now - prev) & counter_mask(cspmu);
	local64_add(delta, &event->count);
}

static inline void arm_cspmu_set_event(struct arm_cspmu *cspmu,
					struct hw_perf_event *hwc)
{
	u32 offset = PMEVTYPER + (4 * hwc->idx);

	writel(hwc->config, cspmu->base0 + offset);
}

static void arm_cspmu_set_ev_filter(struct arm_cspmu *cspmu,
					struct hw_perf_event *hwc,
					u32 filter)
{
	u32 offset = PMEVFILTR + (4 * hwc->idx);

	writel(filter, cspmu->base0 + offset);
}

static inline void arm_cspmu_set_cc_filter(struct arm_cspmu *cspmu, u32 filter)
{
	u32 offset = PMCCFILTR;

	writel(filter, cspmu->base0 + offset);
}

static void arm_cspmu_start(struct perf_event *event, int pmu_flags)
{
	struct arm_cspmu *cspmu = to_arm_cspmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	u32 filter;

	/* We always reprogram the counter */
	if (pmu_flags & PERF_EF_RELOAD)
		WARN_ON(!(hwc->state & PERF_HES_UPTODATE));

	arm_cspmu_set_event_period(event);

	filter = cspmu->impl.ops.event_filter(event);

	if (event->hw.extra_reg.idx == cspmu->cycle_counter_logical_idx) {
		arm_cspmu_set_cc_filter(cspmu, filter);
	} else {
		arm_cspmu_set_event(cspmu, hwc);
		cspmu->impl.ops.set_ev_filter(cspmu, hwc, filter);
	}

	hwc->state = 0;

	arm_cspmu_enable_counter(cspmu, hwc->idx);
}

static void arm_cspmu_stop(struct perf_event *event, int pmu_flags)
{
	struct arm_cspmu *cspmu = to_arm_cspmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	if (hwc->state & PERF_HES_STOPPED)
		return;

	arm_cspmu_disable_counter(cspmu, hwc->idx);
	arm_cspmu_event_update(event);

	hwc->state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
}

static inline u32 to_phys_idx(struct arm_cspmu *cspmu, u32 idx)
{
	return (idx == cspmu->cycle_counter_logical_idx) ?
		ARM_CSPMU_CYCLE_CNTR_IDX : idx;
}

static int arm_cspmu_add(struct perf_event *event, int flags)
{
	struct arm_cspmu *cspmu = to_arm_cspmu(event->pmu);
	struct arm_cspmu_hw_events *hw_events = &cspmu->hw_events;
	struct hw_perf_event *hwc = &event->hw;
	int idx;

	if (WARN_ON_ONCE(!cpumask_test_cpu(smp_processor_id(),
					   &cspmu->associated_cpus)))
		return -ENOENT;

	idx = arm_cspmu_get_event_idx(hw_events, event);
	if (idx < 0)
		return idx;

	hw_events->events[idx] = event;
	hwc->idx = to_phys_idx(cspmu, idx);
	hwc->extra_reg.idx = idx;
	hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;

	if (flags & PERF_EF_START)
		arm_cspmu_start(event, PERF_EF_RELOAD);

	/* Propagate changes to the userspace mapping. */
	perf_event_update_userpage(event);

	return 0;
}

static void arm_cspmu_del(struct perf_event *event, int flags)
{
	struct arm_cspmu *cspmu = to_arm_cspmu(event->pmu);
	struct arm_cspmu_hw_events *hw_events = &cspmu->hw_events;
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->extra_reg.idx;

	arm_cspmu_stop(event, PERF_EF_UPDATE);

	hw_events->events[idx] = NULL;

	clear_bit(idx, hw_events->used_ctrs);

	perf_event_update_userpage(event);
}

static void arm_cspmu_read(struct perf_event *event)
{
	arm_cspmu_event_update(event);
}

static struct arm_cspmu *arm_cspmu_alloc(struct platform_device *pdev)
{
	struct acpi_apmt_node *apmt_node;
	struct arm_cspmu *cspmu;
	struct device *dev = &pdev->dev;

	cspmu = devm_kzalloc(dev, sizeof(*cspmu), GFP_KERNEL);
	if (!cspmu)
		return NULL;

	cspmu->dev = dev;
	platform_set_drvdata(pdev, cspmu);

	apmt_node = arm_cspmu_apmt_node(dev);
	if (apmt_node) {
		cspmu->has_atomic_dword = apmt_node->flags & ACPI_APMT_FLAGS_ATOMIC;
	} else {
		u32 width = 0;

		device_property_read_u32(dev, "reg-io-width", &width);
		cspmu->has_atomic_dword = (width == 8);
	}

	return cspmu;
}

static int arm_cspmu_init_mmio(struct arm_cspmu *cspmu)
{
	struct device *dev;
	struct platform_device *pdev;

	dev = cspmu->dev;
	pdev = to_platform_device(dev);

	/* Base address for page 0. */
	cspmu->base0 = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(cspmu->base0)) {
		dev_err(dev, "ioremap failed for page-0 resource\n");
		return PTR_ERR(cspmu->base0);
	}

	/* Base address for page 1 if supported. Otherwise point to page 0. */
	cspmu->base1 = cspmu->base0;
	if (platform_get_resource(pdev, IORESOURCE_MEM, 1)) {
		cspmu->base1 = devm_platform_ioremap_resource(pdev, 1);
		if (IS_ERR(cspmu->base1)) {
			dev_err(dev, "ioremap failed for page-1 resource\n");
			return PTR_ERR(cspmu->base1);
		}
	}

	cspmu->pmcfgr = readl(cspmu->base0 + PMCFGR);

	cspmu->num_logical_ctrs = FIELD_GET(PMCFGR_N, cspmu->pmcfgr) + 1;

	cspmu->cycle_counter_logical_idx = ARM_CSPMU_MAX_HW_CNTRS;

	if (supports_cycle_counter(cspmu)) {
		/*
		 * The last logical counter is mapped to cycle counter if
		 * there is a gap between regular and cycle counter. Otherwise,
		 * logical and physical have 1-to-1 mapping.
		 */
		cspmu->cycle_counter_logical_idx =
			(cspmu->num_logical_ctrs <= ARM_CSPMU_CYCLE_CNTR_IDX) ?
				cspmu->num_logical_ctrs - 1 :
				ARM_CSPMU_CYCLE_CNTR_IDX;
	}

	cspmu->num_set_clr_reg =
		DIV_ROUND_UP(cspmu->num_logical_ctrs,
				ARM_CSPMU_SET_CLR_COUNTER_NUM);

	cspmu->hw_events.events =
		devm_kcalloc(dev, cspmu->num_logical_ctrs,
			     sizeof(*cspmu->hw_events.events), GFP_KERNEL);

	if (!cspmu->hw_events.events)
		return -ENOMEM;

	return 0;
}

static inline int arm_cspmu_get_reset_overflow(struct arm_cspmu *cspmu,
					       u32 *pmovs)
{
	int i;
	u32 pmovclr_offset = PMOVSCLR;
	u32 has_overflowed = 0;

	for (i = 0; i < cspmu->num_set_clr_reg; ++i) {
		pmovs[i] = readl(cspmu->base1 + pmovclr_offset);
		has_overflowed |= pmovs[i];
		writel(pmovs[i], cspmu->base1 + pmovclr_offset);
		pmovclr_offset += sizeof(u32);
	}

	return has_overflowed != 0;
}

static irqreturn_t arm_cspmu_handle_irq(int irq_num, void *dev)
{
	int idx, has_overflowed;
	struct perf_event *event;
	struct arm_cspmu *cspmu = dev;
	DECLARE_BITMAP(pmovs, ARM_CSPMU_MAX_HW_CNTRS);
	bool handled = false;

	arm_cspmu_stop_counters(cspmu);

	has_overflowed = arm_cspmu_get_reset_overflow(cspmu, (u32 *)pmovs);
	if (!has_overflowed)
		goto done;

	for_each_set_bit(idx, cspmu->hw_events.used_ctrs,
			cspmu->num_logical_ctrs) {
		event = cspmu->hw_events.events[idx];

		if (!event)
			continue;

		if (!test_bit(event->hw.idx, pmovs))
			continue;

		arm_cspmu_event_update(event);
		arm_cspmu_set_event_period(event);

		handled = true;
	}

done:
	arm_cspmu_start_counters(cspmu);
	return IRQ_RETVAL(handled);
}

static int arm_cspmu_request_irq(struct arm_cspmu *cspmu)
{
	int irq, ret;
	struct device *dev;
	struct platform_device *pdev;

	dev = cspmu->dev;
	pdev = to_platform_device(dev);

	/* Skip IRQ request if the PMU does not support overflow interrupt. */
	irq = platform_get_irq_optional(pdev, 0);
	if (irq < 0)
		return irq == -ENXIO ? 0 : irq;

	ret = devm_request_irq(dev, irq, arm_cspmu_handle_irq,
			       IRQF_NOBALANCING | IRQF_NO_THREAD, dev_name(dev),
			       cspmu);
	if (ret) {
		dev_err(dev, "Could not request IRQ %d\n", irq);
		return ret;
	}

	cspmu->irq = irq;

	return 0;
}

#if defined(CONFIG_ACPI) && defined(CONFIG_ARM64)
#include <acpi/processor.h>

static inline int arm_cspmu_find_cpu_container(int cpu, u32 container_uid)
{
	struct device *cpu_dev;
	struct acpi_device *acpi_dev;

	cpu_dev = get_cpu_device(cpu);
	if (!cpu_dev)
		return -ENODEV;

	acpi_dev = ACPI_COMPANION(cpu_dev);
	while (acpi_dev) {
		if (acpi_dev_hid_uid_match(acpi_dev, ACPI_PROCESSOR_CONTAINER_HID, container_uid))
			return 0;

		acpi_dev = acpi_dev_parent(acpi_dev);
	}

	return -ENODEV;
}

static int arm_cspmu_acpi_get_cpus(struct arm_cspmu *cspmu)
{
	struct acpi_apmt_node *apmt_node;
	int affinity_flag;
	int cpu;

	apmt_node = arm_cspmu_apmt_node(cspmu->dev);
	affinity_flag = apmt_node->flags & ACPI_APMT_FLAGS_AFFINITY;

	if (affinity_flag == ACPI_APMT_FLAGS_AFFINITY_PROC) {
		for_each_possible_cpu(cpu) {
			if (apmt_node->proc_affinity ==
			    get_acpi_id_for_cpu(cpu)) {
				cpumask_set_cpu(cpu, &cspmu->associated_cpus);
				break;
			}
		}
	} else {
		for_each_possible_cpu(cpu) {
			if (arm_cspmu_find_cpu_container(
				    cpu, apmt_node->proc_affinity))
				continue;

			cpumask_set_cpu(cpu, &cspmu->associated_cpus);
		}
	}

	return 0;
}
#else
static int arm_cspmu_acpi_get_cpus(struct arm_cspmu *cspmu)
{
	return -ENODEV;
}
#endif

static int arm_cspmu_of_get_cpus(struct arm_cspmu *cspmu)
{
	struct of_phandle_iterator it;
	int ret, cpu;

	of_for_each_phandle(&it, ret, dev_of_node(cspmu->dev), "cpus", NULL, 0) {
		cpu = of_cpu_node_to_id(it.node);
		if (cpu < 0)
			continue;
		cpumask_set_cpu(cpu, &cspmu->associated_cpus);
	}
	return ret == -ENOENT ? 0 : ret;
}

static int arm_cspmu_get_cpus(struct arm_cspmu *cspmu)
{
	int ret = 0;

	if (arm_cspmu_apmt_node(cspmu->dev))
		ret = arm_cspmu_acpi_get_cpus(cspmu);
	else if (device_property_present(cspmu->dev, "cpus"))
		ret = arm_cspmu_of_get_cpus(cspmu);
	else
		cpumask_copy(&cspmu->associated_cpus, cpu_possible_mask);

	if (!ret && cpumask_empty(&cspmu->associated_cpus)) {
		dev_dbg(cspmu->dev, "No cpu associated with the PMU\n");
		ret = -ENODEV;
	}
	return ret;
}

static int arm_cspmu_register_pmu(struct arm_cspmu *cspmu)
{
	int ret, capabilities;

	ret = arm_cspmu_alloc_attr_groups(cspmu);
	if (ret)
		return ret;

	ret = cpuhp_state_add_instance(arm_cspmu_cpuhp_state,
				       &cspmu->cpuhp_node);
	if (ret)
		return ret;

	capabilities = PERF_PMU_CAP_NO_EXCLUDE;
	if (cspmu->irq == 0)
		capabilities |= PERF_PMU_CAP_NO_INTERRUPT;

	cspmu->pmu = (struct pmu){
		.task_ctx_nr	= perf_invalid_context,
		.module		= cspmu->impl.module,
		.parent		= cspmu->dev,
		.pmu_enable	= arm_cspmu_enable,
		.pmu_disable	= arm_cspmu_disable,
		.event_init	= arm_cspmu_event_init,
		.add		= arm_cspmu_add,
		.del		= arm_cspmu_del,
		.start		= arm_cspmu_start,
		.stop		= arm_cspmu_stop,
		.read		= arm_cspmu_read,
		.attr_groups	= cspmu->attr_groups,
		.capabilities	= capabilities,
	};

	/* Hardware counter init */
	arm_cspmu_reset_counters(cspmu);

	ret = perf_pmu_register(&cspmu->pmu, cspmu->name, -1);
	if (ret) {
		cpuhp_state_remove_instance(arm_cspmu_cpuhp_state,
					    &cspmu->cpuhp_node);
	}

	return ret;
}

static int arm_cspmu_device_probe(struct platform_device *pdev)
{
	int ret;
	struct arm_cspmu *cspmu;

	cspmu = arm_cspmu_alloc(pdev);
	if (!cspmu)
		return -ENOMEM;

	ret = arm_cspmu_init_mmio(cspmu);
	if (ret)
		return ret;

	ret = arm_cspmu_request_irq(cspmu);
	if (ret)
		return ret;

	ret = arm_cspmu_get_cpus(cspmu);
	if (ret)
		return ret;

	ret = arm_cspmu_init_impl_ops(cspmu);
	if (ret)
		return ret;

	ret = arm_cspmu_register_pmu(cspmu);

	/* Matches arm_cspmu_init_impl_ops() above. */
	if (cspmu->impl.module != THIS_MODULE)
		module_put(cspmu->impl.module);

	return ret;
}

static void arm_cspmu_device_remove(struct platform_device *pdev)
{
	struct arm_cspmu *cspmu = platform_get_drvdata(pdev);

	perf_pmu_unregister(&cspmu->pmu);
	cpuhp_state_remove_instance(arm_cspmu_cpuhp_state, &cspmu->cpuhp_node);
}

static const struct platform_device_id arm_cspmu_id[] = {
	{DRVNAME, 0},
	{ },
};
MODULE_DEVICE_TABLE(platform, arm_cspmu_id);

static const struct of_device_id arm_cspmu_of_match[] = {
	{ .compatible = "arm,coresight-pmu" },
	{}
};
MODULE_DEVICE_TABLE(of, arm_cspmu_of_match);

static struct platform_driver arm_cspmu_driver = {
	.driver = {
		.name = DRVNAME,
		.of_match_table = arm_cspmu_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = arm_cspmu_device_probe,
	.remove_new = arm_cspmu_device_remove,
	.id_table = arm_cspmu_id,
};

static void arm_cspmu_set_active_cpu(int cpu, struct arm_cspmu *cspmu)
{
	cpumask_set_cpu(cpu, &cspmu->active_cpu);
	if (cspmu->irq)
		WARN_ON(irq_set_affinity(cspmu->irq, &cspmu->active_cpu));
}

static int arm_cspmu_cpu_online(unsigned int cpu, struct hlist_node *node)
{
	struct arm_cspmu *cspmu =
		hlist_entry_safe(node, struct arm_cspmu, cpuhp_node);

	if (!cpumask_test_cpu(cpu, &cspmu->associated_cpus))
		return 0;

	/* If the PMU is already managed, there is nothing to do */
	if (!cpumask_empty(&cspmu->active_cpu))
		return 0;

	/* Use this CPU for event counting */
	arm_cspmu_set_active_cpu(cpu, cspmu);

	return 0;
}

static int arm_cspmu_cpu_teardown(unsigned int cpu, struct hlist_node *node)
{
	unsigned int dst;

	struct arm_cspmu *cspmu =
		hlist_entry_safe(node, struct arm_cspmu, cpuhp_node);

	/* Nothing to do if this CPU doesn't own the PMU */
	if (!cpumask_test_and_clear_cpu(cpu, &cspmu->active_cpu))
		return 0;

	/* Choose a new CPU to migrate ownership of the PMU to */
	dst = cpumask_any_and_but(&cspmu->associated_cpus,
				  cpu_online_mask, cpu);
	if (dst >= nr_cpu_ids)
		return 0;

	/* Use this CPU for event counting */
	perf_pmu_migrate_context(&cspmu->pmu, cpu, dst);
	arm_cspmu_set_active_cpu(dst, cspmu);

	return 0;
}

static int __init arm_cspmu_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN,
					"perf/arm/cspmu:online",
					arm_cspmu_cpu_online,
					arm_cspmu_cpu_teardown);
	if (ret < 0)
		return ret;
	arm_cspmu_cpuhp_state = ret;
	return platform_driver_register(&arm_cspmu_driver);
}

static void __exit arm_cspmu_exit(void)
{
	platform_driver_unregister(&arm_cspmu_driver);
	cpuhp_remove_multi_state(arm_cspmu_cpuhp_state);
}

int arm_cspmu_impl_register(const struct arm_cspmu_impl_match *impl_match)
{
	struct arm_cspmu_impl_match *match;
	int ret = 0;

	match = arm_cspmu_impl_match_get(impl_match->pmiidr_val);

	if (match) {
		mutex_lock(&arm_cspmu_lock);

		if (!match->impl_init_ops) {
			match->module = impl_match->module;
			match->impl_init_ops = impl_match->impl_init_ops;
		} else {
			/* Broken match table may contain non-unique entries */
			WARN(1, "arm_cspmu backend already registered for module: %s, pmiidr: 0x%x, mask: 0x%x\n",
				match->module_name,
				match->pmiidr_val,
				match->pmiidr_mask);

			ret = -EINVAL;
		}

		mutex_unlock(&arm_cspmu_lock);

		if (!ret)
			ret = driver_attach(&arm_cspmu_driver.driver);
	} else {
		pr_err("arm_cspmu reg failed, unable to find a match for pmiidr: 0x%x\n",
			impl_match->pmiidr_val);

		ret = -EINVAL;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(arm_cspmu_impl_register);

static int arm_cspmu_match_device(struct device *dev, const void *match)
{
	struct arm_cspmu *cspmu = platform_get_drvdata(to_platform_device(dev));

	return (cspmu && cspmu->impl.match == match) ? 1 : 0;
}

void arm_cspmu_impl_unregister(const struct arm_cspmu_impl_match *impl_match)
{
	struct device *dev;
	struct arm_cspmu_impl_match *match;

	match = arm_cspmu_impl_match_get(impl_match->pmiidr_val);

	if (WARN_ON(!match))
		return;

	/* Unbind the driver from all matching backend devices. */
	while ((dev = driver_find_device(&arm_cspmu_driver.driver, NULL,
			match, arm_cspmu_match_device)))
		device_release_driver(dev);

	mutex_lock(&arm_cspmu_lock);

	match->module = NULL;
	match->impl_init_ops = NULL;

	mutex_unlock(&arm_cspmu_lock);
}
EXPORT_SYMBOL_GPL(arm_cspmu_impl_unregister);

module_init(arm_cspmu_init);
module_exit(arm_cspmu_exit);

MODULE_LICENSE("GPL v2");

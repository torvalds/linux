// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Advanced Micro Devices, Inc.
 *
 * Author: Steven Kinney <Steven.Kinney@amd.com>
 * Author: Suravee Suthikulpanit <Suraveee.Suthikulpanit@amd.com>
 *
 * Perf: amd_iommu - AMD IOMMU Performance Counter PMU implementation
 */

#define pr_fmt(fmt)	"perf/amd_iommu: " fmt

#include <linux/perf_event.h>
#include <linux/init.h>
#include <linux/cpumask.h>
#include <linux/slab.h>
#include <linux/amd-iommu.h>

#include "../perf_event.h"
#include "iommu.h"

/* iommu pmu conf masks */
#define GET_CSOURCE(x)     ((x)->conf & 0xFFULL)
#define GET_DEVID(x)       (((x)->conf >> 8)  & 0xFFFFULL)
#define GET_DOMID(x)       (((x)->conf >> 24) & 0xFFFFULL)
#define GET_PASID(x)       (((x)->conf >> 40) & 0xFFFFFULL)

/* iommu pmu conf1 masks */
#define GET_DEVID_MASK(x)  ((x)->conf1  & 0xFFFFULL)
#define GET_DOMID_MASK(x)  (((x)->conf1 >> 16) & 0xFFFFULL)
#define GET_PASID_MASK(x)  (((x)->conf1 >> 32) & 0xFFFFFULL)

#define IOMMU_NAME_SIZE 16

struct perf_amd_iommu {
	struct list_head list;
	struct pmu pmu;
	struct amd_iommu *iommu;
	char name[IOMMU_NAME_SIZE];
	u8 max_banks;
	u8 max_counters;
	u64 cntr_assign_mask;
	raw_spinlock_t lock;
};

static LIST_HEAD(perf_amd_iommu_list);

/*---------------------------------------------
 * sysfs format attributes
 *---------------------------------------------*/
PMU_FORMAT_ATTR(csource,    "config:0-7");
PMU_FORMAT_ATTR(devid,      "config:8-23");
PMU_FORMAT_ATTR(domid,      "config:24-39");
PMU_FORMAT_ATTR(pasid,      "config:40-59");
PMU_FORMAT_ATTR(devid_mask, "config1:0-15");
PMU_FORMAT_ATTR(domid_mask, "config1:16-31");
PMU_FORMAT_ATTR(pasid_mask, "config1:32-51");

static struct attribute *iommu_format_attrs[] = {
	&format_attr_csource.attr,
	&format_attr_devid.attr,
	&format_attr_pasid.attr,
	&format_attr_domid.attr,
	&format_attr_devid_mask.attr,
	&format_attr_pasid_mask.attr,
	&format_attr_domid_mask.attr,
	NULL,
};

static struct attribute_group amd_iommu_format_group = {
	.name = "format",
	.attrs = iommu_format_attrs,
};

/*---------------------------------------------
 * sysfs events attributes
 *---------------------------------------------*/
static struct attribute_group amd_iommu_events_group = {
	.name = "events",
};

struct amd_iommu_event_desc {
	struct device_attribute attr;
	const char *event;
};

static ssize_t _iommu_event_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct amd_iommu_event_desc *event =
		container_of(attr, struct amd_iommu_event_desc, attr);
	return sprintf(buf, "%s\n", event->event);
}

#define AMD_IOMMU_EVENT_DESC(_name, _event)			\
{								\
	.attr  = __ATTR(_name, 0444, _iommu_event_show, NULL),	\
	.event = _event,					\
}

static struct amd_iommu_event_desc amd_iommu_v2_event_descs[] = {
	AMD_IOMMU_EVENT_DESC(mem_pass_untrans,        "csource=0x01"),
	AMD_IOMMU_EVENT_DESC(mem_pass_pretrans,       "csource=0x02"),
	AMD_IOMMU_EVENT_DESC(mem_pass_excl,           "csource=0x03"),
	AMD_IOMMU_EVENT_DESC(mem_target_abort,        "csource=0x04"),
	AMD_IOMMU_EVENT_DESC(mem_trans_total,         "csource=0x05"),
	AMD_IOMMU_EVENT_DESC(mem_iommu_tlb_pte_hit,   "csource=0x06"),
	AMD_IOMMU_EVENT_DESC(mem_iommu_tlb_pte_mis,   "csource=0x07"),
	AMD_IOMMU_EVENT_DESC(mem_iommu_tlb_pde_hit,   "csource=0x08"),
	AMD_IOMMU_EVENT_DESC(mem_iommu_tlb_pde_mis,   "csource=0x09"),
	AMD_IOMMU_EVENT_DESC(mem_dte_hit,             "csource=0x0a"),
	AMD_IOMMU_EVENT_DESC(mem_dte_mis,             "csource=0x0b"),
	AMD_IOMMU_EVENT_DESC(page_tbl_read_tot,       "csource=0x0c"),
	AMD_IOMMU_EVENT_DESC(page_tbl_read_nst,       "csource=0x0d"),
	AMD_IOMMU_EVENT_DESC(page_tbl_read_gst,       "csource=0x0e"),
	AMD_IOMMU_EVENT_DESC(int_dte_hit,             "csource=0x0f"),
	AMD_IOMMU_EVENT_DESC(int_dte_mis,             "csource=0x10"),
	AMD_IOMMU_EVENT_DESC(cmd_processed,           "csource=0x11"),
	AMD_IOMMU_EVENT_DESC(cmd_processed_inv,       "csource=0x12"),
	AMD_IOMMU_EVENT_DESC(tlb_inv,                 "csource=0x13"),
	AMD_IOMMU_EVENT_DESC(ign_rd_wr_mmio_1ff8h,    "csource=0x14"),
	AMD_IOMMU_EVENT_DESC(vapic_int_non_guest,     "csource=0x15"),
	AMD_IOMMU_EVENT_DESC(vapic_int_guest,         "csource=0x16"),
	AMD_IOMMU_EVENT_DESC(smi_recv,                "csource=0x17"),
	AMD_IOMMU_EVENT_DESC(smi_blk,                 "csource=0x18"),
	{ /* end: all zeroes */ },
};

/*---------------------------------------------
 * sysfs cpumask attributes
 *---------------------------------------------*/
static cpumask_t iommu_cpumask;

static ssize_t _iommu_cpumask_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	return cpumap_print_to_pagebuf(true, buf, &iommu_cpumask);
}
static DEVICE_ATTR(cpumask, S_IRUGO, _iommu_cpumask_show, NULL);

static struct attribute *iommu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static struct attribute_group amd_iommu_cpumask_group = {
	.attrs = iommu_cpumask_attrs,
};

/*---------------------------------------------*/

static int get_next_avail_iommu_bnk_cntr(struct perf_event *event)
{
	struct perf_amd_iommu *piommu = container_of(event->pmu, struct perf_amd_iommu, pmu);
	int max_cntrs = piommu->max_counters;
	int max_banks = piommu->max_banks;
	u32 shift, bank, cntr;
	unsigned long flags;
	int retval;

	raw_spin_lock_irqsave(&piommu->lock, flags);

	for (bank = 0; bank < max_banks; bank++) {
		for (cntr = 0; cntr < max_cntrs; cntr++) {
			shift = bank + (bank*3) + cntr;
			if (piommu->cntr_assign_mask & BIT_ULL(shift)) {
				continue;
			} else {
				piommu->cntr_assign_mask |= BIT_ULL(shift);
				event->hw.iommu_bank = bank;
				event->hw.iommu_cntr = cntr;
				retval = 0;
				goto out;
			}
		}
	}
	retval = -ENOSPC;
out:
	raw_spin_unlock_irqrestore(&piommu->lock, flags);
	return retval;
}

static int clear_avail_iommu_bnk_cntr(struct perf_amd_iommu *perf_iommu,
					u8 bank, u8 cntr)
{
	unsigned long flags;
	int max_banks, max_cntrs;
	int shift = 0;

	max_banks = perf_iommu->max_banks;
	max_cntrs = perf_iommu->max_counters;

	if ((bank > max_banks) || (cntr > max_cntrs))
		return -EINVAL;

	shift = bank + cntr + (bank*3);

	raw_spin_lock_irqsave(&perf_iommu->lock, flags);
	perf_iommu->cntr_assign_mask &= ~(1ULL<<shift);
	raw_spin_unlock_irqrestore(&perf_iommu->lock, flags);

	return 0;
}

static int perf_iommu_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	/* test the event attr type check for PMU enumeration */
	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/*
	 * IOMMU counters are shared across all cores.
	 * Therefore, it does not support per-process mode.
	 * Also, it does not support event sampling mode.
	 */
	if (is_sampling_event(event) || event->attach_state & PERF_ATTACH_TASK)
		return -EINVAL;

	if (event->cpu < 0)
		return -EINVAL;

	/* update the hw_perf_event struct with the iommu config data */
	hwc->conf  = event->attr.config;
	hwc->conf1 = event->attr.config1;

	return 0;
}

static inline struct amd_iommu *perf_event_2_iommu(struct perf_event *ev)
{
	return (container_of(ev->pmu, struct perf_amd_iommu, pmu))->iommu;
}

static void perf_iommu_enable_event(struct perf_event *ev)
{
	struct amd_iommu *iommu = perf_event_2_iommu(ev);
	struct hw_perf_event *hwc = &ev->hw;
	u8 bank = hwc->iommu_bank;
	u8 cntr = hwc->iommu_cntr;
	u64 reg = 0ULL;

	reg = GET_CSOURCE(hwc);
	amd_iommu_pc_set_reg(iommu, bank, cntr, IOMMU_PC_COUNTER_SRC_REG, &reg);

	reg = GET_DEVID_MASK(hwc);
	reg = GET_DEVID(hwc) | (reg << 32);
	if (reg)
		reg |= BIT(31);
	amd_iommu_pc_set_reg(iommu, bank, cntr, IOMMU_PC_DEVID_MATCH_REG, &reg);

	reg = GET_PASID_MASK(hwc);
	reg = GET_PASID(hwc) | (reg << 32);
	if (reg)
		reg |= BIT(31);
	amd_iommu_pc_set_reg(iommu, bank, cntr, IOMMU_PC_PASID_MATCH_REG, &reg);

	reg = GET_DOMID_MASK(hwc);
	reg = GET_DOMID(hwc) | (reg << 32);
	if (reg)
		reg |= BIT(31);
	amd_iommu_pc_set_reg(iommu, bank, cntr, IOMMU_PC_DOMID_MATCH_REG, &reg);
}

static void perf_iommu_disable_event(struct perf_event *event)
{
	struct amd_iommu *iommu = perf_event_2_iommu(event);
	struct hw_perf_event *hwc = &event->hw;
	u64 reg = 0ULL;

	amd_iommu_pc_set_reg(iommu, hwc->iommu_bank, hwc->iommu_cntr,
			     IOMMU_PC_COUNTER_SRC_REG, &reg);
}

static void perf_iommu_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;

	if (WARN_ON_ONCE(!(hwc->state & PERF_HES_STOPPED)))
		return;

	WARN_ON_ONCE(!(hwc->state & PERF_HES_UPTODATE));
	hwc->state = 0;

	/*
	 * To account for power-gating, which prevents write to
	 * the counter, we need to enable the counter
	 * before setting up counter register.
	 */
	perf_iommu_enable_event(event);

	if (flags & PERF_EF_RELOAD) {
		u64 count = 0;
		struct amd_iommu *iommu = perf_event_2_iommu(event);

		/*
		 * Since the IOMMU PMU only support counting mode,
		 * the counter always start with value zero.
		 */
		amd_iommu_pc_set_reg(iommu, hwc->iommu_bank, hwc->iommu_cntr,
				     IOMMU_PC_COUNTER_REG, &count);
	}

	perf_event_update_userpage(event);
}

static void perf_iommu_read(struct perf_event *event)
{
	u64 count;
	struct hw_perf_event *hwc = &event->hw;
	struct amd_iommu *iommu = perf_event_2_iommu(event);

	if (amd_iommu_pc_get_reg(iommu, hwc->iommu_bank, hwc->iommu_cntr,
				 IOMMU_PC_COUNTER_REG, &count))
		return;

	/* IOMMU pc counter register is only 48 bits */
	count &= GENMASK_ULL(47, 0);

	/*
	 * Since the counter always start with value zero,
	 * simply just accumulate the count for the event.
	 */
	local64_add(count, &event->count);
}

static void perf_iommu_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;

	if (hwc->state & PERF_HES_UPTODATE)
		return;

	/*
	 * To account for power-gating, in which reading the counter would
	 * return zero, we need to read the register before disabling.
	 */
	perf_iommu_read(event);
	hwc->state |= PERF_HES_UPTODATE;

	perf_iommu_disable_event(event);
	WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
	hwc->state |= PERF_HES_STOPPED;
}

static int perf_iommu_add(struct perf_event *event, int flags)
{
	int retval;

	event->hw.state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	/* request an iommu bank/counter */
	retval = get_next_avail_iommu_bnk_cntr(event);
	if (retval)
		return retval;

	if (flags & PERF_EF_START)
		perf_iommu_start(event, PERF_EF_RELOAD);

	return 0;
}

static void perf_iommu_del(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct perf_amd_iommu *perf_iommu =
			container_of(event->pmu, struct perf_amd_iommu, pmu);

	perf_iommu_stop(event, PERF_EF_UPDATE);

	/* clear the assigned iommu bank/counter */
	clear_avail_iommu_bnk_cntr(perf_iommu,
				   hwc->iommu_bank, hwc->iommu_cntr);

	perf_event_update_userpage(event);
}

static __init int _init_events_attrs(void)
{
	int i = 0, j;
	struct attribute **attrs;

	while (amd_iommu_v2_event_descs[i].attr.attr.name)
		i++;

	attrs = kcalloc(i + 1, sizeof(*attrs), GFP_KERNEL);
	if (!attrs)
		return -ENOMEM;

	for (j = 0; j < i; j++)
		attrs[j] = &amd_iommu_v2_event_descs[j].attr.attr;

	amd_iommu_events_group.attrs = attrs;
	return 0;
}

static const struct attribute_group *amd_iommu_attr_groups[] = {
	&amd_iommu_format_group,
	&amd_iommu_cpumask_group,
	&amd_iommu_events_group,
	NULL,
};

static const struct pmu iommu_pmu __initconst = {
	.event_init	= perf_iommu_event_init,
	.add		= perf_iommu_add,
	.del		= perf_iommu_del,
	.start		= perf_iommu_start,
	.stop		= perf_iommu_stop,
	.read		= perf_iommu_read,
	.task_ctx_nr	= perf_invalid_context,
	.attr_groups	= amd_iommu_attr_groups,
	.capabilities	= PERF_PMU_CAP_NO_EXCLUDE,
};

static __init int init_one_iommu(unsigned int idx)
{
	struct perf_amd_iommu *perf_iommu;
	int ret;

	perf_iommu = kzalloc(sizeof(struct perf_amd_iommu), GFP_KERNEL);
	if (!perf_iommu)
		return -ENOMEM;

	raw_spin_lock_init(&perf_iommu->lock);

	perf_iommu->pmu          = iommu_pmu;
	perf_iommu->iommu        = get_amd_iommu(idx);
	perf_iommu->max_banks    = amd_iommu_pc_get_max_banks(idx);
	perf_iommu->max_counters = amd_iommu_pc_get_max_counters(idx);

	if (!perf_iommu->iommu ||
	    !perf_iommu->max_banks ||
	    !perf_iommu->max_counters) {
		kfree(perf_iommu);
		return -EINVAL;
	}

	snprintf(perf_iommu->name, IOMMU_NAME_SIZE, "amd_iommu_%u", idx);

	ret = perf_pmu_register(&perf_iommu->pmu, perf_iommu->name, -1);
	if (!ret) {
		pr_info("Detected AMD IOMMU #%d (%d banks, %d counters/bank).\n",
			idx, perf_iommu->max_banks, perf_iommu->max_counters);
		list_add_tail(&perf_iommu->list, &perf_amd_iommu_list);
	} else {
		pr_warn("Error initializing IOMMU %d.\n", idx);
		kfree(perf_iommu);
	}
	return ret;
}

static __init int amd_iommu_pc_init(void)
{
	unsigned int i, cnt = 0;
	int ret;

	/* Make sure the IOMMU PC resource is available */
	if (!amd_iommu_pc_supported())
		return -ENODEV;

	ret = _init_events_attrs();
	if (ret)
		return ret;

	/*
	 * An IOMMU PMU is specific to an IOMMU, and can function independently.
	 * So we go through all IOMMUs and ignore the one that fails init
	 * unless all IOMMU are failing.
	 */
	for (i = 0; i < amd_iommu_get_num_iommus(); i++) {
		ret = init_one_iommu(i);
		if (!ret)
			cnt++;
	}

	if (!cnt) {
		kfree(amd_iommu_events_group.attrs);
		return -ENODEV;
	}

	/* Init cpumask attributes to only core 0 */
	cpumask_set_cpu(0, &iommu_cpumask);
	return 0;
}

device_initcall(amd_iommu_pc_init);

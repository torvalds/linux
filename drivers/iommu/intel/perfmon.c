// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support Intel IOMMU PerfMon
 * Copyright(c) 2023 Intel Corporation.
 */
#define pr_fmt(fmt)	"DMAR: " fmt
#define dev_fmt(fmt)	pr_fmt(fmt)

#include <linux/dmar.h>
#include "iommu.h"
#include "perfmon.h"

PMU_FORMAT_ATTR(event,		"config:0-27");		/* ES: Events Select */
PMU_FORMAT_ATTR(event_group,	"config:28-31");	/* EGI: Event Group Index */

static struct attribute *iommu_pmu_format_attrs[] = {
	&format_attr_event_group.attr,
	&format_attr_event.attr,
	NULL
};

static struct attribute_group iommu_pmu_format_attr_group = {
	.name = "format",
	.attrs = iommu_pmu_format_attrs,
};

/* The available events are added in attr_update later */
static struct attribute *attrs_empty[] = {
	NULL
};

static struct attribute_group iommu_pmu_events_attr_group = {
	.name = "events",
	.attrs = attrs_empty,
};

static cpumask_t iommu_pmu_cpu_mask;

static ssize_t
cpumask_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return cpumap_print_to_pagebuf(true, buf, &iommu_pmu_cpu_mask);
}
static DEVICE_ATTR_RO(cpumask);

static struct attribute *iommu_pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL
};

static struct attribute_group iommu_pmu_cpumask_attr_group = {
	.attrs = iommu_pmu_cpumask_attrs,
};

static const struct attribute_group *iommu_pmu_attr_groups[] = {
	&iommu_pmu_format_attr_group,
	&iommu_pmu_events_attr_group,
	&iommu_pmu_cpumask_attr_group,
	NULL
};

static inline struct iommu_pmu *dev_to_iommu_pmu(struct device *dev)
{
	/*
	 * The perf_event creates its own dev for each PMU.
	 * See pmu_dev_alloc()
	 */
	return container_of(dev_get_drvdata(dev), struct iommu_pmu, pmu);
}

#define IOMMU_PMU_ATTR(_name, _format, _filter)				\
	PMU_FORMAT_ATTR(_name, _format);				\
									\
static struct attribute *_name##_attr[] = {				\
	&format_attr_##_name.attr,					\
	NULL								\
};									\
									\
static umode_t								\
_name##_is_visible(struct kobject *kobj, struct attribute *attr, int i)	\
{									\
	struct device *dev = kobj_to_dev(kobj);				\
	struct iommu_pmu *iommu_pmu = dev_to_iommu_pmu(dev);		\
									\
	if (!iommu_pmu)							\
		return 0;						\
	return (iommu_pmu->filter & _filter) ? attr->mode : 0;		\
}									\
									\
static struct attribute_group _name = {					\
	.name		= "format",					\
	.attrs		= _name##_attr,					\
	.is_visible	= _name##_is_visible,				\
};

IOMMU_PMU_ATTR(filter_requester_id_en,	"config1:0",		IOMMU_PMU_FILTER_REQUESTER_ID);
IOMMU_PMU_ATTR(filter_domain_en,	"config1:1",		IOMMU_PMU_FILTER_DOMAIN);
IOMMU_PMU_ATTR(filter_pasid_en,		"config1:2",		IOMMU_PMU_FILTER_PASID);
IOMMU_PMU_ATTR(filter_ats_en,		"config1:3",		IOMMU_PMU_FILTER_ATS);
IOMMU_PMU_ATTR(filter_page_table_en,	"config1:4",		IOMMU_PMU_FILTER_PAGE_TABLE);
IOMMU_PMU_ATTR(filter_requester_id,	"config1:16-31",	IOMMU_PMU_FILTER_REQUESTER_ID);
IOMMU_PMU_ATTR(filter_domain,		"config1:32-47",	IOMMU_PMU_FILTER_DOMAIN);
IOMMU_PMU_ATTR(filter_pasid,		"config2:0-21",		IOMMU_PMU_FILTER_PASID);
IOMMU_PMU_ATTR(filter_ats,		"config2:24-28",	IOMMU_PMU_FILTER_ATS);
IOMMU_PMU_ATTR(filter_page_table,	"config2:32-36",	IOMMU_PMU_FILTER_PAGE_TABLE);

#define iommu_pmu_en_requester_id(e)		((e) & 0x1)
#define iommu_pmu_en_domain(e)			(((e) >> 1) & 0x1)
#define iommu_pmu_en_pasid(e)			(((e) >> 2) & 0x1)
#define iommu_pmu_en_ats(e)			(((e) >> 3) & 0x1)
#define iommu_pmu_en_page_table(e)		(((e) >> 4) & 0x1)
#define iommu_pmu_get_requester_id(filter)	(((filter) >> 16) & 0xffff)
#define iommu_pmu_get_domain(filter)		(((filter) >> 32) & 0xffff)
#define iommu_pmu_get_pasid(filter)		((filter) & 0x3fffff)
#define iommu_pmu_get_ats(filter)		(((filter) >> 24) & 0x1f)
#define iommu_pmu_get_page_table(filter)	(((filter) >> 32) & 0x1f)

#define iommu_pmu_set_filter(_name, _config, _filter, _idx, _econfig)		\
{										\
	if ((iommu_pmu->filter & _filter) && iommu_pmu_en_##_name(_econfig)) {	\
		dmar_writel(iommu_pmu->cfg_reg + _idx * IOMMU_PMU_CFG_OFFSET +	\
			    IOMMU_PMU_CFG_SIZE +				\
			    (ffs(_filter) - 1) * IOMMU_PMU_CFG_FILTERS_OFFSET,	\
			    iommu_pmu_get_##_name(_config) | IOMMU_PMU_FILTER_EN);\
	}									\
}

#define iommu_pmu_clear_filter(_filter, _idx)					\
{										\
	if (iommu_pmu->filter & _filter) {					\
		dmar_writel(iommu_pmu->cfg_reg + _idx * IOMMU_PMU_CFG_OFFSET +	\
			    IOMMU_PMU_CFG_SIZE +				\
			    (ffs(_filter) - 1) * IOMMU_PMU_CFG_FILTERS_OFFSET,	\
			    0);							\
	}									\
}

/*
 * Define the event attr related functions
 * Input: _name: event attr name
 *        _string: string of the event in sysfs
 *        _g_idx: event group encoding
 *        _event: event encoding
 */
#define IOMMU_PMU_EVENT_ATTR(_name, _string, _g_idx, _event)			\
	PMU_EVENT_ATTR_STRING(_name, event_attr_##_name, _string)		\
										\
static struct attribute *_name##_attr[] = {					\
	&event_attr_##_name.attr.attr,						\
	NULL									\
};										\
										\
static umode_t									\
_name##_is_visible(struct kobject *kobj, struct attribute *attr, int i)		\
{										\
	struct device *dev = kobj_to_dev(kobj);					\
	struct iommu_pmu *iommu_pmu = dev_to_iommu_pmu(dev);			\
										\
	if (!iommu_pmu)								\
		return 0;							\
	return (iommu_pmu->evcap[_g_idx] & _event) ? attr->mode : 0;		\
}										\
										\
static struct attribute_group _name = {						\
	.name		= "events",						\
	.attrs		= _name##_attr,						\
	.is_visible	= _name##_is_visible,					\
};

IOMMU_PMU_EVENT_ATTR(iommu_clocks,		"event_group=0x0,event=0x001", 0x0, 0x001)
IOMMU_PMU_EVENT_ATTR(iommu_requests,		"event_group=0x0,event=0x002", 0x0, 0x002)
IOMMU_PMU_EVENT_ATTR(pw_occupancy,		"event_group=0x0,event=0x004", 0x0, 0x004)
IOMMU_PMU_EVENT_ATTR(ats_blocked,		"event_group=0x0,event=0x008", 0x0, 0x008)
IOMMU_PMU_EVENT_ATTR(iommu_mrds,		"event_group=0x1,event=0x001", 0x1, 0x001)
IOMMU_PMU_EVENT_ATTR(iommu_mem_blocked,		"event_group=0x1,event=0x020", 0x1, 0x020)
IOMMU_PMU_EVENT_ATTR(pg_req_posted,		"event_group=0x1,event=0x040", 0x1, 0x040)
IOMMU_PMU_EVENT_ATTR(ctxt_cache_lookup,		"event_group=0x2,event=0x001", 0x2, 0x001)
IOMMU_PMU_EVENT_ATTR(ctxt_cache_hit,		"event_group=0x2,event=0x002", 0x2, 0x002)
IOMMU_PMU_EVENT_ATTR(pasid_cache_lookup,	"event_group=0x2,event=0x004", 0x2, 0x004)
IOMMU_PMU_EVENT_ATTR(pasid_cache_hit,		"event_group=0x2,event=0x008", 0x2, 0x008)
IOMMU_PMU_EVENT_ATTR(ss_nonleaf_lookup,		"event_group=0x2,event=0x010", 0x2, 0x010)
IOMMU_PMU_EVENT_ATTR(ss_nonleaf_hit,		"event_group=0x2,event=0x020", 0x2, 0x020)
IOMMU_PMU_EVENT_ATTR(fs_nonleaf_lookup,		"event_group=0x2,event=0x040", 0x2, 0x040)
IOMMU_PMU_EVENT_ATTR(fs_nonleaf_hit,		"event_group=0x2,event=0x080", 0x2, 0x080)
IOMMU_PMU_EVENT_ATTR(hpt_nonleaf_lookup,	"event_group=0x2,event=0x100", 0x2, 0x100)
IOMMU_PMU_EVENT_ATTR(hpt_nonleaf_hit,		"event_group=0x2,event=0x200", 0x2, 0x200)
IOMMU_PMU_EVENT_ATTR(iotlb_lookup,		"event_group=0x3,event=0x001", 0x3, 0x001)
IOMMU_PMU_EVENT_ATTR(iotlb_hit,			"event_group=0x3,event=0x002", 0x3, 0x002)
IOMMU_PMU_EVENT_ATTR(hpt_leaf_lookup,		"event_group=0x3,event=0x004", 0x3, 0x004)
IOMMU_PMU_EVENT_ATTR(hpt_leaf_hit,		"event_group=0x3,event=0x008", 0x3, 0x008)
IOMMU_PMU_EVENT_ATTR(int_cache_lookup,		"event_group=0x4,event=0x001", 0x4, 0x001)
IOMMU_PMU_EVENT_ATTR(int_cache_hit_nonposted,	"event_group=0x4,event=0x002", 0x4, 0x002)
IOMMU_PMU_EVENT_ATTR(int_cache_hit_posted,	"event_group=0x4,event=0x004", 0x4, 0x004)

static const struct attribute_group *iommu_pmu_attr_update[] = {
	&filter_requester_id_en,
	&filter_domain_en,
	&filter_pasid_en,
	&filter_ats_en,
	&filter_page_table_en,
	&filter_requester_id,
	&filter_domain,
	&filter_pasid,
	&filter_ats,
	&filter_page_table,
	&iommu_clocks,
	&iommu_requests,
	&pw_occupancy,
	&ats_blocked,
	&iommu_mrds,
	&iommu_mem_blocked,
	&pg_req_posted,
	&ctxt_cache_lookup,
	&ctxt_cache_hit,
	&pasid_cache_lookup,
	&pasid_cache_hit,
	&ss_nonleaf_lookup,
	&ss_nonleaf_hit,
	&fs_nonleaf_lookup,
	&fs_nonleaf_hit,
	&hpt_nonleaf_lookup,
	&hpt_nonleaf_hit,
	&iotlb_lookup,
	&iotlb_hit,
	&hpt_leaf_lookup,
	&hpt_leaf_hit,
	&int_cache_lookup,
	&int_cache_hit_nonposted,
	&int_cache_hit_posted,
	NULL
};

static inline void __iomem *
iommu_event_base(struct iommu_pmu *iommu_pmu, int idx)
{
	return iommu_pmu->cntr_reg + idx * iommu_pmu->cntr_stride;
}

static inline void __iomem *
iommu_config_base(struct iommu_pmu *iommu_pmu, int idx)
{
	return iommu_pmu->cfg_reg + idx * IOMMU_PMU_CFG_OFFSET;
}

static inline struct iommu_pmu *iommu_event_to_pmu(struct perf_event *event)
{
	return container_of(event->pmu, struct iommu_pmu, pmu);
}

static inline u64 iommu_event_config(struct perf_event *event)
{
	u64 config = event->attr.config;

	return (iommu_event_select(config) << IOMMU_EVENT_CFG_ES_SHIFT) |
	       (iommu_event_group(config) << IOMMU_EVENT_CFG_EGI_SHIFT) |
	       IOMMU_EVENT_CFG_INT;
}

static inline bool is_iommu_pmu_event(struct iommu_pmu *iommu_pmu,
				      struct perf_event *event)
{
	return event->pmu == &iommu_pmu->pmu;
}

static int iommu_pmu_validate_event(struct perf_event *event)
{
	struct iommu_pmu *iommu_pmu = iommu_event_to_pmu(event);
	u32 event_group = iommu_event_group(event->attr.config);

	if (event_group >= iommu_pmu->num_eg)
		return -EINVAL;

	return 0;
}

static int iommu_pmu_validate_group(struct perf_event *event)
{
	struct iommu_pmu *iommu_pmu = iommu_event_to_pmu(event);
	struct perf_event *sibling;
	int nr = 0;

	/*
	 * All events in a group must be scheduled simultaneously.
	 * Check whether there is enough counters for all the events.
	 */
	for_each_sibling_event(sibling, event->group_leader) {
		if (!is_iommu_pmu_event(iommu_pmu, sibling) ||
		    sibling->state <= PERF_EVENT_STATE_OFF)
			continue;

		if (++nr > iommu_pmu->num_cntr)
			return -EINVAL;
	}

	return 0;
}

static int iommu_pmu_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* sampling not supported */
	if (event->attr.sample_period)
		return -EINVAL;

	if (event->cpu < 0)
		return -EINVAL;

	if (iommu_pmu_validate_event(event))
		return -EINVAL;

	hwc->config = iommu_event_config(event);

	return iommu_pmu_validate_group(event);
}

static void iommu_pmu_event_update(struct perf_event *event)
{
	struct iommu_pmu *iommu_pmu = iommu_event_to_pmu(event);
	struct hw_perf_event *hwc = &event->hw;
	u64 prev_count, new_count, delta;
	int shift = 64 - iommu_pmu->cntr_width;

again:
	prev_count = local64_read(&hwc->prev_count);
	new_count = dmar_readq(iommu_event_base(iommu_pmu, hwc->idx));
	if (local64_xchg(&hwc->prev_count, new_count) != prev_count)
		goto again;

	/*
	 * The counter width is enumerated. Always shift the counter
	 * before using it.
	 */
	delta = (new_count << shift) - (prev_count << shift);
	delta >>= shift;

	local64_add(delta, &event->count);
}

static void iommu_pmu_start(struct perf_event *event, int flags)
{
	struct iommu_pmu *iommu_pmu = iommu_event_to_pmu(event);
	struct intel_iommu *iommu = iommu_pmu->iommu;
	struct hw_perf_event *hwc = &event->hw;
	u64 count;

	if (WARN_ON_ONCE(!(hwc->state & PERF_HES_STOPPED)))
		return;

	if (WARN_ON_ONCE(hwc->idx < 0 || hwc->idx >= IOMMU_PMU_IDX_MAX))
		return;

	if (flags & PERF_EF_RELOAD)
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));

	hwc->state = 0;

	/* Always reprogram the period */
	count = dmar_readq(iommu_event_base(iommu_pmu, hwc->idx));
	local64_set((&hwc->prev_count), count);

	/*
	 * The error of ecmd will be ignored.
	 * - The existing perf_event subsystem doesn't handle the error.
	 *   Only IOMMU PMU returns runtime HW error. We don't want to
	 *   change the existing generic interfaces for the specific case.
	 * - It's a corner case caused by HW, which is very unlikely to
	 *   happen. There is nothing SW can do.
	 * - The worst case is that the user will get <not count> with
	 *   perf command, which can give the user some hints.
	 */
	ecmd_submit_sync(iommu, DMA_ECMD_ENABLE, hwc->idx, 0);

	perf_event_update_userpage(event);
}

static void iommu_pmu_stop(struct perf_event *event, int flags)
{
	struct iommu_pmu *iommu_pmu = iommu_event_to_pmu(event);
	struct intel_iommu *iommu = iommu_pmu->iommu;
	struct hw_perf_event *hwc = &event->hw;

	if (!(hwc->state & PERF_HES_STOPPED)) {
		ecmd_submit_sync(iommu, DMA_ECMD_DISABLE, hwc->idx, 0);

		iommu_pmu_event_update(event);

		hwc->state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
	}
}

static inline int
iommu_pmu_validate_per_cntr_event(struct iommu_pmu *iommu_pmu,
				  int idx, struct perf_event *event)
{
	u32 event_group = iommu_event_group(event->attr.config);
	u32 select = iommu_event_select(event->attr.config);

	if (!(iommu_pmu->cntr_evcap[idx][event_group] & select))
		return -EINVAL;

	return 0;
}

static int iommu_pmu_assign_event(struct iommu_pmu *iommu_pmu,
				  struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int idx;

	/*
	 * The counters which support limited events are usually at the end.
	 * Schedule them first to accommodate more events.
	 */
	for (idx = iommu_pmu->num_cntr - 1; idx >= 0; idx--) {
		if (test_and_set_bit(idx, iommu_pmu->used_mask))
			continue;
		/* Check per-counter event capabilities */
		if (!iommu_pmu_validate_per_cntr_event(iommu_pmu, idx, event))
			break;
		clear_bit(idx, iommu_pmu->used_mask);
	}
	if (idx < 0)
		return -EINVAL;

	iommu_pmu->event_list[idx] = event;
	hwc->idx = idx;

	/* config events */
	dmar_writeq(iommu_config_base(iommu_pmu, idx), hwc->config);

	iommu_pmu_set_filter(requester_id, event->attr.config1,
			     IOMMU_PMU_FILTER_REQUESTER_ID, idx,
			     event->attr.config1);
	iommu_pmu_set_filter(domain, event->attr.config1,
			     IOMMU_PMU_FILTER_DOMAIN, idx,
			     event->attr.config1);
	iommu_pmu_set_filter(pasid, event->attr.config1,
			     IOMMU_PMU_FILTER_PASID, idx,
			     event->attr.config1);
	iommu_pmu_set_filter(ats, event->attr.config2,
			     IOMMU_PMU_FILTER_ATS, idx,
			     event->attr.config1);
	iommu_pmu_set_filter(page_table, event->attr.config2,
			     IOMMU_PMU_FILTER_PAGE_TABLE, idx,
			     event->attr.config1);

	return 0;
}

static int iommu_pmu_add(struct perf_event *event, int flags)
{
	struct iommu_pmu *iommu_pmu = iommu_event_to_pmu(event);
	struct hw_perf_event *hwc = &event->hw;
	int ret;

	ret = iommu_pmu_assign_event(iommu_pmu, event);
	if (ret < 0)
		return ret;

	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	if (flags & PERF_EF_START)
		iommu_pmu_start(event, 0);

	return 0;
}

static void iommu_pmu_del(struct perf_event *event, int flags)
{
	struct iommu_pmu *iommu_pmu = iommu_event_to_pmu(event);
	int idx = event->hw.idx;

	iommu_pmu_stop(event, PERF_EF_UPDATE);

	iommu_pmu_clear_filter(IOMMU_PMU_FILTER_REQUESTER_ID, idx);
	iommu_pmu_clear_filter(IOMMU_PMU_FILTER_DOMAIN, idx);
	iommu_pmu_clear_filter(IOMMU_PMU_FILTER_PASID, idx);
	iommu_pmu_clear_filter(IOMMU_PMU_FILTER_ATS, idx);
	iommu_pmu_clear_filter(IOMMU_PMU_FILTER_PAGE_TABLE, idx);

	iommu_pmu->event_list[idx] = NULL;
	event->hw.idx = -1;
	clear_bit(idx, iommu_pmu->used_mask);

	perf_event_update_userpage(event);
}

static void iommu_pmu_enable(struct pmu *pmu)
{
	struct iommu_pmu *iommu_pmu = container_of(pmu, struct iommu_pmu, pmu);
	struct intel_iommu *iommu = iommu_pmu->iommu;

	ecmd_submit_sync(iommu, DMA_ECMD_UNFREEZE, 0, 0);
}

static void iommu_pmu_disable(struct pmu *pmu)
{
	struct iommu_pmu *iommu_pmu = container_of(pmu, struct iommu_pmu, pmu);
	struct intel_iommu *iommu = iommu_pmu->iommu;

	ecmd_submit_sync(iommu, DMA_ECMD_FREEZE, 0, 0);
}

static int __iommu_pmu_register(struct intel_iommu *iommu)
{
	struct iommu_pmu *iommu_pmu = iommu->pmu;

	iommu_pmu->pmu.name		= iommu->name;
	iommu_pmu->pmu.task_ctx_nr	= perf_invalid_context;
	iommu_pmu->pmu.event_init	= iommu_pmu_event_init;
	iommu_pmu->pmu.pmu_enable	= iommu_pmu_enable;
	iommu_pmu->pmu.pmu_disable	= iommu_pmu_disable;
	iommu_pmu->pmu.add		= iommu_pmu_add;
	iommu_pmu->pmu.del		= iommu_pmu_del;
	iommu_pmu->pmu.start		= iommu_pmu_start;
	iommu_pmu->pmu.stop		= iommu_pmu_stop;
	iommu_pmu->pmu.read		= iommu_pmu_event_update;
	iommu_pmu->pmu.attr_groups	= iommu_pmu_attr_groups;
	iommu_pmu->pmu.attr_update	= iommu_pmu_attr_update;
	iommu_pmu->pmu.capabilities	= PERF_PMU_CAP_NO_EXCLUDE;
	iommu_pmu->pmu.module		= THIS_MODULE;

	return perf_pmu_register(&iommu_pmu->pmu, iommu_pmu->pmu.name, -1);
}

static inline void __iomem *
get_perf_reg_address(struct intel_iommu *iommu, u32 offset)
{
	u32 off = dmar_readl(iommu->reg + offset);

	return iommu->reg + off;
}

int alloc_iommu_pmu(struct intel_iommu *iommu)
{
	struct iommu_pmu *iommu_pmu;
	int i, j, ret;
	u64 perfcap;
	u32 cap;

	if (!ecap_pms(iommu->ecap))
		return 0;

	/* The IOMMU PMU requires the ECMD support as well */
	if (!cap_ecmds(iommu->cap))
		return -ENODEV;

	perfcap = dmar_readq(iommu->reg + DMAR_PERFCAP_REG);
	/* The performance monitoring is not supported. */
	if (!perfcap)
		return -ENODEV;

	/* Sanity check for the number of the counters and event groups */
	if (!pcap_num_cntr(perfcap) || !pcap_num_event_group(perfcap))
		return -ENODEV;

	/* The interrupt on overflow is required */
	if (!pcap_interrupt(perfcap))
		return -ENODEV;

	/* Check required Enhanced Command Capability */
	if (!ecmd_has_pmu_essential(iommu))
		return -ENODEV;

	iommu_pmu = kzalloc(sizeof(*iommu_pmu), GFP_KERNEL);
	if (!iommu_pmu)
		return -ENOMEM;

	iommu_pmu->num_cntr = pcap_num_cntr(perfcap);
	if (iommu_pmu->num_cntr > IOMMU_PMU_IDX_MAX) {
		pr_warn_once("The number of IOMMU counters %d > max(%d), clipping!",
			     iommu_pmu->num_cntr, IOMMU_PMU_IDX_MAX);
		iommu_pmu->num_cntr = IOMMU_PMU_IDX_MAX;
	}

	iommu_pmu->cntr_width = pcap_cntr_width(perfcap);
	iommu_pmu->filter = pcap_filters_mask(perfcap);
	iommu_pmu->cntr_stride = pcap_cntr_stride(perfcap);
	iommu_pmu->num_eg = pcap_num_event_group(perfcap);

	iommu_pmu->evcap = kcalloc(iommu_pmu->num_eg, sizeof(u64), GFP_KERNEL);
	if (!iommu_pmu->evcap) {
		ret = -ENOMEM;
		goto free_pmu;
	}

	/* Parse event group capabilities */
	for (i = 0; i < iommu_pmu->num_eg; i++) {
		u64 pcap;

		pcap = dmar_readq(iommu->reg + DMAR_PERFEVNTCAP_REG +
				  i * IOMMU_PMU_CAP_REGS_STEP);
		iommu_pmu->evcap[i] = pecap_es(pcap);
	}

	iommu_pmu->cntr_evcap = kcalloc(iommu_pmu->num_cntr, sizeof(u32 *), GFP_KERNEL);
	if (!iommu_pmu->cntr_evcap) {
		ret = -ENOMEM;
		goto free_pmu_evcap;
	}
	for (i = 0; i < iommu_pmu->num_cntr; i++) {
		iommu_pmu->cntr_evcap[i] = kcalloc(iommu_pmu->num_eg, sizeof(u32), GFP_KERNEL);
		if (!iommu_pmu->cntr_evcap[i]) {
			ret = -ENOMEM;
			goto free_pmu_cntr_evcap;
		}
		/*
		 * Set to the global capabilities, will adjust according
		 * to per-counter capabilities later.
		 */
		for (j = 0; j < iommu_pmu->num_eg; j++)
			iommu_pmu->cntr_evcap[i][j] = (u32)iommu_pmu->evcap[j];
	}

	iommu_pmu->cfg_reg = get_perf_reg_address(iommu, DMAR_PERFCFGOFF_REG);
	iommu_pmu->cntr_reg = get_perf_reg_address(iommu, DMAR_PERFCNTROFF_REG);
	iommu_pmu->overflow = get_perf_reg_address(iommu, DMAR_PERFOVFOFF_REG);

	/*
	 * Check per-counter capabilities. All counters should have the
	 * same capabilities on Interrupt on Overflow Support and Counter
	 * Width.
	 */
	for (i = 0; i < iommu_pmu->num_cntr; i++) {
		cap = dmar_readl(iommu_pmu->cfg_reg +
				 i * IOMMU_PMU_CFG_OFFSET +
				 IOMMU_PMU_CFG_CNTRCAP_OFFSET);
		if (!iommu_cntrcap_pcc(cap))
			continue;

		/*
		 * It's possible that some counters have a different
		 * capability because of e.g., HW bug. Check the corner
		 * case here and simply drop those counters.
		 */
		if ((iommu_cntrcap_cw(cap) != iommu_pmu->cntr_width) ||
		    !iommu_cntrcap_ios(cap)) {
			iommu_pmu->num_cntr = i;
			pr_warn("PMU counter capability inconsistent, counter number reduced to %d\n",
				iommu_pmu->num_cntr);
		}

		/* Clear the pre-defined events group */
		for (j = 0; j < iommu_pmu->num_eg; j++)
			iommu_pmu->cntr_evcap[i][j] = 0;

		/* Override with per-counter event capabilities */
		for (j = 0; j < iommu_cntrcap_egcnt(cap); j++) {
			cap = dmar_readl(iommu_pmu->cfg_reg + i * IOMMU_PMU_CFG_OFFSET +
					 IOMMU_PMU_CFG_CNTREVCAP_OFFSET +
					 (j * IOMMU_PMU_OFF_REGS_STEP));
			iommu_pmu->cntr_evcap[i][iommu_event_group(cap)] = iommu_event_select(cap);
			/*
			 * Some events may only be supported by a specific counter.
			 * Track them in the evcap as well.
			 */
			iommu_pmu->evcap[iommu_event_group(cap)] |= iommu_event_select(cap);
		}
	}

	iommu_pmu->iommu = iommu;
	iommu->pmu = iommu_pmu;

	return 0;

free_pmu_cntr_evcap:
	for (i = 0; i < iommu_pmu->num_cntr; i++)
		kfree(iommu_pmu->cntr_evcap[i]);
	kfree(iommu_pmu->cntr_evcap);
free_pmu_evcap:
	kfree(iommu_pmu->evcap);
free_pmu:
	kfree(iommu_pmu);

	return ret;
}

void free_iommu_pmu(struct intel_iommu *iommu)
{
	struct iommu_pmu *iommu_pmu = iommu->pmu;

	if (!iommu_pmu)
		return;

	if (iommu_pmu->evcap) {
		int i;

		for (i = 0; i < iommu_pmu->num_cntr; i++)
			kfree(iommu_pmu->cntr_evcap[i]);
		kfree(iommu_pmu->cntr_evcap);
	}
	kfree(iommu_pmu->evcap);
	kfree(iommu_pmu);
	iommu->pmu = NULL;
}

static int iommu_pmu_cpu_online(unsigned int cpu)
{
	if (cpumask_empty(&iommu_pmu_cpu_mask))
		cpumask_set_cpu(cpu, &iommu_pmu_cpu_mask);

	return 0;
}

static int iommu_pmu_cpu_offline(unsigned int cpu)
{
	struct dmar_drhd_unit *drhd;
	struct intel_iommu *iommu;
	int target;

	if (!cpumask_test_and_clear_cpu(cpu, &iommu_pmu_cpu_mask))
		return 0;

	target = cpumask_any_but(cpu_online_mask, cpu);

	if (target < nr_cpu_ids)
		cpumask_set_cpu(target, &iommu_pmu_cpu_mask);
	else
		target = -1;

	rcu_read_lock();

	for_each_iommu(iommu, drhd) {
		if (!iommu->pmu)
			continue;
		perf_pmu_migrate_context(&iommu->pmu->pmu, cpu, target);
	}
	rcu_read_unlock();

	return 0;
}

static int nr_iommu_pmu;

static int iommu_pmu_cpuhp_setup(struct iommu_pmu *iommu_pmu)
{
	int ret;

	if (nr_iommu_pmu++)
		return 0;

	ret = cpuhp_setup_state(CPUHP_AP_PERF_X86_IOMMU_PERF_ONLINE,
				"driver/iommu/intel/perfmon:online",
				iommu_pmu_cpu_online,
				iommu_pmu_cpu_offline);
	if (ret)
		nr_iommu_pmu = 0;

	return ret;
}

static void iommu_pmu_cpuhp_free(struct iommu_pmu *iommu_pmu)
{
	if (--nr_iommu_pmu)
		return;

	cpuhp_remove_state(CPUHP_AP_PERF_X86_IOMMU_PERF_ONLINE);
}

void iommu_pmu_register(struct intel_iommu *iommu)
{
	struct iommu_pmu *iommu_pmu = iommu->pmu;

	if (!iommu_pmu)
		return;

	if (__iommu_pmu_register(iommu))
		goto err;

	if (iommu_pmu_cpuhp_setup(iommu_pmu))
		goto unregister;

	return;

unregister:
	perf_pmu_unregister(&iommu_pmu->pmu);
err:
	pr_err("Failed to register PMU for iommu (seq_id = %d)\n", iommu->seq_id);
	free_iommu_pmu(iommu);
}

void iommu_pmu_unregister(struct intel_iommu *iommu)
{
	struct iommu_pmu *iommu_pmu = iommu->pmu;

	if (!iommu_pmu)
		return;

	iommu_pmu_cpuhp_free(iommu_pmu);
	perf_pmu_unregister(&iommu_pmu->pmu);
}

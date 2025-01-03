// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Advanced Micro Devices, Inc.
 *
 * Author: Jacob Shin <jacob.shin@amd.com>
 */

#include <linux/perf_event.h>
#include <linux/percpu.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/cpufeature.h>
#include <linux/smp.h>

#include <asm/perf_event.h>
#include <asm/msr.h>

#define NUM_COUNTERS_NB		4
#define NUM_COUNTERS_L2		4
#define NUM_COUNTERS_L3		6

#define RDPMC_BASE_NB		6
#define RDPMC_BASE_LLC		10

#define COUNTER_SHIFT		16
#define UNCORE_NAME_LEN		16
#define UNCORE_GROUP_MAX	256

#undef pr_fmt
#define pr_fmt(fmt)	"amd_uncore: " fmt

static int pmu_version;

struct amd_uncore_ctx {
	int refcnt;
	int cpu;
	struct perf_event **events;
	struct hlist_node node;
};

struct amd_uncore_pmu {
	char name[UNCORE_NAME_LEN];
	int num_counters;
	int rdpmc_base;
	u32 msr_base;
	int group;
	cpumask_t active_mask;
	struct pmu pmu;
	struct amd_uncore_ctx * __percpu *ctx;
};

enum {
	UNCORE_TYPE_DF,
	UNCORE_TYPE_L3,
	UNCORE_TYPE_UMC,

	UNCORE_TYPE_MAX
};

union amd_uncore_info {
	struct {
		u64	aux_data:32;	/* auxiliary data */
		u64	num_pmcs:8;	/* number of counters */
		u64	gid:8;		/* group id */
		u64	cid:8;		/* context id */
	} split;
	u64		full;
};

struct amd_uncore {
	union amd_uncore_info  __percpu *info;
	struct amd_uncore_pmu *pmus;
	unsigned int num_pmus;
	bool init_done;
	void (*scan)(struct amd_uncore *uncore, unsigned int cpu);
	int  (*init)(struct amd_uncore *uncore, unsigned int cpu);
	void (*move)(struct amd_uncore *uncore, unsigned int cpu);
	void (*free)(struct amd_uncore *uncore, unsigned int cpu);
};

static struct amd_uncore uncores[UNCORE_TYPE_MAX];

static struct amd_uncore_pmu *event_to_amd_uncore_pmu(struct perf_event *event)
{
	return container_of(event->pmu, struct amd_uncore_pmu, pmu);
}

static void amd_uncore_read(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 prev, new;
	s64 delta;

	/*
	 * since we do not enable counter overflow interrupts,
	 * we do not have to worry about prev_count changing on us
	 */

	prev = local64_read(&hwc->prev_count);

	/*
	 * Some uncore PMUs do not have RDPMC assignments. In such cases,
	 * read counts directly from the corresponding PERF_CTR.
	 */
	if (hwc->event_base_rdpmc < 0)
		rdmsrl(hwc->event_base, new);
	else
		rdpmcl(hwc->event_base_rdpmc, new);

	local64_set(&hwc->prev_count, new);
	delta = (new << COUNTER_SHIFT) - (prev << COUNTER_SHIFT);
	delta >>= COUNTER_SHIFT;
	local64_add(delta, &event->count);
}

static void amd_uncore_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;

	if (flags & PERF_EF_RELOAD)
		wrmsrl(hwc->event_base, (u64)local64_read(&hwc->prev_count));

	hwc->state = 0;
	wrmsrl(hwc->config_base, (hwc->config | ARCH_PERFMON_EVENTSEL_ENABLE));
	perf_event_update_userpage(event);
}

static void amd_uncore_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;

	wrmsrl(hwc->config_base, hwc->config);
	hwc->state |= PERF_HES_STOPPED;

	if ((flags & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		event->pmu->read(event);
		hwc->state |= PERF_HES_UPTODATE;
	}
}

static int amd_uncore_add(struct perf_event *event, int flags)
{
	int i;
	struct amd_uncore_pmu *pmu = event_to_amd_uncore_pmu(event);
	struct amd_uncore_ctx *ctx = *per_cpu_ptr(pmu->ctx, event->cpu);
	struct hw_perf_event *hwc = &event->hw;

	/* are we already assigned? */
	if (hwc->idx != -1 && ctx->events[hwc->idx] == event)
		goto out;

	for (i = 0; i < pmu->num_counters; i++) {
		if (ctx->events[i] == event) {
			hwc->idx = i;
			goto out;
		}
	}

	/* if not, take the first available counter */
	hwc->idx = -1;
	for (i = 0; i < pmu->num_counters; i++) {
		struct perf_event *tmp = NULL;

		if (try_cmpxchg(&ctx->events[i], &tmp, event)) {
			hwc->idx = i;
			break;
		}
	}

out:
	if (hwc->idx == -1)
		return -EBUSY;

	hwc->config_base = pmu->msr_base + (2 * hwc->idx);
	hwc->event_base = pmu->msr_base + 1 + (2 * hwc->idx);
	hwc->event_base_rdpmc = pmu->rdpmc_base + hwc->idx;
	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	if (pmu->rdpmc_base < 0)
		hwc->event_base_rdpmc = -1;

	if (flags & PERF_EF_START)
		event->pmu->start(event, PERF_EF_RELOAD);

	return 0;
}

static void amd_uncore_del(struct perf_event *event, int flags)
{
	int i;
	struct amd_uncore_pmu *pmu = event_to_amd_uncore_pmu(event);
	struct amd_uncore_ctx *ctx = *per_cpu_ptr(pmu->ctx, event->cpu);
	struct hw_perf_event *hwc = &event->hw;

	event->pmu->stop(event, PERF_EF_UPDATE);

	for (i = 0; i < pmu->num_counters; i++) {
		struct perf_event *tmp = event;

		if (try_cmpxchg(&ctx->events[i], &tmp, NULL))
			break;
	}

	hwc->idx = -1;
}

static int amd_uncore_event_init(struct perf_event *event)
{
	struct amd_uncore_pmu *pmu;
	struct amd_uncore_ctx *ctx;
	struct hw_perf_event *hwc = &event->hw;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (event->cpu < 0)
		return -EINVAL;

	pmu = event_to_amd_uncore_pmu(event);
	ctx = *per_cpu_ptr(pmu->ctx, event->cpu);
	if (!ctx)
		return -ENODEV;

	/*
	 * NB and Last level cache counters (MSRs) are shared across all cores
	 * that share the same NB / Last level cache.  On family 16h and below,
	 * Interrupts can be directed to a single target core, however, event
	 * counts generated by processes running on other cores cannot be masked
	 * out. So we do not support sampling and per-thread events via
	 * CAP_NO_INTERRUPT, and we do not enable counter overflow interrupts:
	 */
	hwc->config = event->attr.config;
	hwc->idx = -1;

	/*
	 * since request can come in to any of the shared cores, we will remap
	 * to a single common cpu.
	 */
	event->cpu = ctx->cpu;

	return 0;
}

static umode_t
amd_f17h_uncore_is_visible(struct kobject *kobj, struct attribute *attr, int i)
{
	return boot_cpu_data.x86 >= 0x17 && boot_cpu_data.x86 < 0x19 ?
	       attr->mode : 0;
}

static umode_t
amd_f19h_uncore_is_visible(struct kobject *kobj, struct attribute *attr, int i)
{
	return boot_cpu_data.x86 >= 0x19 ? attr->mode : 0;
}

static ssize_t amd_uncore_attr_show_cpumask(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct pmu *ptr = dev_get_drvdata(dev);
	struct amd_uncore_pmu *pmu = container_of(ptr, struct amd_uncore_pmu, pmu);

	return cpumap_print_to_pagebuf(true, buf, &pmu->active_mask);
}
static DEVICE_ATTR(cpumask, S_IRUGO, amd_uncore_attr_show_cpumask, NULL);

static struct attribute *amd_uncore_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static struct attribute_group amd_uncore_attr_group = {
	.attrs = amd_uncore_attrs,
};

#define DEFINE_UNCORE_FORMAT_ATTR(_var, _name, _format)			\
static ssize_t __uncore_##_var##_show(struct device *dev,		\
				struct device_attribute *attr,		\
				char *page)				\
{									\
	BUILD_BUG_ON(sizeof(_format) >= PAGE_SIZE);			\
	return sprintf(page, _format "\n");				\
}									\
static struct device_attribute format_attr_##_var =			\
	__ATTR(_name, 0444, __uncore_##_var##_show, NULL)

DEFINE_UNCORE_FORMAT_ATTR(event12,	event,		"config:0-7,32-35");
DEFINE_UNCORE_FORMAT_ATTR(event14,	event,		"config:0-7,32-35,59-60"); /* F17h+ DF */
DEFINE_UNCORE_FORMAT_ATTR(event14v2,	event,		"config:0-7,32-37");	   /* PerfMonV2 DF */
DEFINE_UNCORE_FORMAT_ATTR(event8,	event,		"config:0-7");		   /* F17h+ L3, PerfMonV2 UMC */
DEFINE_UNCORE_FORMAT_ATTR(umask8,	umask,		"config:8-15");
DEFINE_UNCORE_FORMAT_ATTR(umask12,	umask,		"config:8-15,24-27");	   /* PerfMonV2 DF */
DEFINE_UNCORE_FORMAT_ATTR(coreid,	coreid,		"config:42-44");	   /* F19h L3 */
DEFINE_UNCORE_FORMAT_ATTR(slicemask,	slicemask,	"config:48-51");	   /* F17h L3 */
DEFINE_UNCORE_FORMAT_ATTR(threadmask8,	threadmask,	"config:56-63");	   /* F17h L3 */
DEFINE_UNCORE_FORMAT_ATTR(threadmask2,	threadmask,	"config:56-57");	   /* F19h L3 */
DEFINE_UNCORE_FORMAT_ATTR(enallslices,	enallslices,	"config:46");		   /* F19h L3 */
DEFINE_UNCORE_FORMAT_ATTR(enallcores,	enallcores,	"config:47");		   /* F19h L3 */
DEFINE_UNCORE_FORMAT_ATTR(sliceid,	sliceid,	"config:48-50");	   /* F19h L3 */
DEFINE_UNCORE_FORMAT_ATTR(rdwrmask,	rdwrmask,	"config:8-9");		   /* PerfMonV2 UMC */

/* Common DF and NB attributes */
static struct attribute *amd_uncore_df_format_attr[] = {
	&format_attr_event12.attr,	/* event */
	&format_attr_umask8.attr,	/* umask */
	NULL,
};

/* Common L2 and L3 attributes */
static struct attribute *amd_uncore_l3_format_attr[] = {
	&format_attr_event12.attr,	/* event */
	&format_attr_umask8.attr,	/* umask */
	NULL,				/* threadmask */
	NULL,
};

/* Common UMC attributes */
static struct attribute *amd_uncore_umc_format_attr[] = {
	&format_attr_event8.attr,       /* event */
	&format_attr_rdwrmask.attr,     /* rdwrmask */
	NULL,
};

/* F17h unique L3 attributes */
static struct attribute *amd_f17h_uncore_l3_format_attr[] = {
	&format_attr_slicemask.attr,	/* slicemask */
	NULL,
};

/* F19h unique L3 attributes */
static struct attribute *amd_f19h_uncore_l3_format_attr[] = {
	&format_attr_coreid.attr,	/* coreid */
	&format_attr_enallslices.attr,	/* enallslices */
	&format_attr_enallcores.attr,	/* enallcores */
	&format_attr_sliceid.attr,	/* sliceid */
	NULL,
};

static struct attribute_group amd_uncore_df_format_group = {
	.name = "format",
	.attrs = amd_uncore_df_format_attr,
};

static struct attribute_group amd_uncore_l3_format_group = {
	.name = "format",
	.attrs = amd_uncore_l3_format_attr,
};

static struct attribute_group amd_f17h_uncore_l3_format_group = {
	.name = "format",
	.attrs = amd_f17h_uncore_l3_format_attr,
	.is_visible = amd_f17h_uncore_is_visible,
};

static struct attribute_group amd_f19h_uncore_l3_format_group = {
	.name = "format",
	.attrs = amd_f19h_uncore_l3_format_attr,
	.is_visible = amd_f19h_uncore_is_visible,
};

static struct attribute_group amd_uncore_umc_format_group = {
	.name = "format",
	.attrs = amd_uncore_umc_format_attr,
};

static const struct attribute_group *amd_uncore_df_attr_groups[] = {
	&amd_uncore_attr_group,
	&amd_uncore_df_format_group,
	NULL,
};

static const struct attribute_group *amd_uncore_l3_attr_groups[] = {
	&amd_uncore_attr_group,
	&amd_uncore_l3_format_group,
	NULL,
};

static const struct attribute_group *amd_uncore_l3_attr_update[] = {
	&amd_f17h_uncore_l3_format_group,
	&amd_f19h_uncore_l3_format_group,
	NULL,
};

static const struct attribute_group *amd_uncore_umc_attr_groups[] = {
	&amd_uncore_attr_group,
	&amd_uncore_umc_format_group,
	NULL,
};

static __always_inline
int amd_uncore_ctx_cid(struct amd_uncore *uncore, unsigned int cpu)
{
	union amd_uncore_info *info = per_cpu_ptr(uncore->info, cpu);
	return info->split.cid;
}

static __always_inline
int amd_uncore_ctx_gid(struct amd_uncore *uncore, unsigned int cpu)
{
	union amd_uncore_info *info = per_cpu_ptr(uncore->info, cpu);
	return info->split.gid;
}

static __always_inline
int amd_uncore_ctx_num_pmcs(struct amd_uncore *uncore, unsigned int cpu)
{
	union amd_uncore_info *info = per_cpu_ptr(uncore->info, cpu);
	return info->split.num_pmcs;
}

static void amd_uncore_ctx_free(struct amd_uncore *uncore, unsigned int cpu)
{
	struct amd_uncore_pmu *pmu;
	struct amd_uncore_ctx *ctx;
	int i;

	if (!uncore->init_done)
		return;

	for (i = 0; i < uncore->num_pmus; i++) {
		pmu = &uncore->pmus[i];
		ctx = *per_cpu_ptr(pmu->ctx, cpu);
		if (!ctx)
			continue;

		if (cpu == ctx->cpu)
			cpumask_clear_cpu(cpu, &pmu->active_mask);

		if (!--ctx->refcnt) {
			kfree(ctx->events);
			kfree(ctx);
		}

		*per_cpu_ptr(pmu->ctx, cpu) = NULL;
	}
}

static int amd_uncore_ctx_init(struct amd_uncore *uncore, unsigned int cpu)
{
	struct amd_uncore_ctx *curr, *prev;
	struct amd_uncore_pmu *pmu;
	int node, cid, gid, i, j;

	if (!uncore->init_done || !uncore->num_pmus)
		return 0;

	cid = amd_uncore_ctx_cid(uncore, cpu);
	gid = amd_uncore_ctx_gid(uncore, cpu);

	for (i = 0; i < uncore->num_pmus; i++) {
		pmu = &uncore->pmus[i];
		*per_cpu_ptr(pmu->ctx, cpu) = NULL;
		curr = NULL;

		/* Check for group exclusivity */
		if (gid != pmu->group)
			continue;

		/* Find a sibling context */
		for_each_online_cpu(j) {
			if (cpu == j)
				continue;

			prev = *per_cpu_ptr(pmu->ctx, j);
			if (!prev)
				continue;

			if (cid == amd_uncore_ctx_cid(uncore, j)) {
				curr = prev;
				break;
			}
		}

		/* Allocate context if sibling does not exist */
		if (!curr) {
			node = cpu_to_node(cpu);
			curr = kzalloc_node(sizeof(*curr), GFP_KERNEL, node);
			if (!curr)
				goto fail;

			curr->cpu = cpu;
			curr->events = kzalloc_node(sizeof(*curr->events) *
						    pmu->num_counters,
						    GFP_KERNEL, node);
			if (!curr->events) {
				kfree(curr);
				goto fail;
			}

			cpumask_set_cpu(cpu, &pmu->active_mask);
		}

		curr->refcnt++;
		*per_cpu_ptr(pmu->ctx, cpu) = curr;
	}

	return 0;

fail:
	amd_uncore_ctx_free(uncore, cpu);

	return -ENOMEM;
}

static void amd_uncore_ctx_move(struct amd_uncore *uncore, unsigned int cpu)
{
	struct amd_uncore_ctx *curr, *next;
	struct amd_uncore_pmu *pmu;
	int i, j;

	if (!uncore->init_done)
		return;

	for (i = 0; i < uncore->num_pmus; i++) {
		pmu = &uncore->pmus[i];
		curr = *per_cpu_ptr(pmu->ctx, cpu);
		if (!curr)
			continue;

		/* Migrate to a shared sibling if possible */
		for_each_online_cpu(j) {
			next = *per_cpu_ptr(pmu->ctx, j);
			if (!next || cpu == j)
				continue;

			if (curr == next) {
				perf_pmu_migrate_context(&pmu->pmu, cpu, j);
				cpumask_clear_cpu(cpu, &pmu->active_mask);
				cpumask_set_cpu(j, &pmu->active_mask);
				next->cpu = j;
				break;
			}
		}
	}
}

static int amd_uncore_cpu_starting(unsigned int cpu)
{
	struct amd_uncore *uncore;
	int i;

	for (i = 0; i < UNCORE_TYPE_MAX; i++) {
		uncore = &uncores[i];
		uncore->scan(uncore, cpu);
	}

	return 0;
}

static int amd_uncore_cpu_online(unsigned int cpu)
{
	struct amd_uncore *uncore;
	int i;

	for (i = 0; i < UNCORE_TYPE_MAX; i++) {
		uncore = &uncores[i];
		if (uncore->init(uncore, cpu))
			break;
	}

	return 0;
}

static int amd_uncore_cpu_down_prepare(unsigned int cpu)
{
	struct amd_uncore *uncore;
	int i;

	for (i = 0; i < UNCORE_TYPE_MAX; i++) {
		uncore = &uncores[i];
		uncore->move(uncore, cpu);
	}

	return 0;
}

static int amd_uncore_cpu_dead(unsigned int cpu)
{
	struct amd_uncore *uncore;
	int i;

	for (i = 0; i < UNCORE_TYPE_MAX; i++) {
		uncore = &uncores[i];
		uncore->free(uncore, cpu);
	}

	return 0;
}

static int amd_uncore_df_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int ret = amd_uncore_event_init(event);

	if (ret || pmu_version < 2)
		return ret;

	hwc->config = event->attr.config &
		      (pmu_version >= 2 ? AMD64_PERFMON_V2_RAW_EVENT_MASK_NB :
					  AMD64_RAW_EVENT_MASK_NB);

	return 0;
}

static int amd_uncore_df_add(struct perf_event *event, int flags)
{
	int ret = amd_uncore_add(event, flags & ~PERF_EF_START);
	struct hw_perf_event *hwc = &event->hw;

	if (ret)
		return ret;

	/*
	 * The first four DF counters are accessible via RDPMC index 6 to 9
	 * followed by the L3 counters from index 10 to 15. For processors
	 * with more than four DF counters, the DF RDPMC assignments become
	 * discontiguous as the additional counters are accessible starting
	 * from index 16.
	 */
	if (hwc->idx >= NUM_COUNTERS_NB)
		hwc->event_base_rdpmc += NUM_COUNTERS_L3;

	/* Delayed start after rdpmc base update */
	if (flags & PERF_EF_START)
		amd_uncore_start(event, PERF_EF_RELOAD);

	return 0;
}

static
void amd_uncore_df_ctx_scan(struct amd_uncore *uncore, unsigned int cpu)
{
	union cpuid_0x80000022_ebx ebx;
	union amd_uncore_info info;

	if (!boot_cpu_has(X86_FEATURE_PERFCTR_NB))
		return;

	info.split.aux_data = 0;
	info.split.num_pmcs = NUM_COUNTERS_NB;
	info.split.gid = 0;
	info.split.cid = topology_logical_package_id(cpu);

	if (pmu_version >= 2) {
		ebx.full = cpuid_ebx(EXT_PERFMON_DEBUG_FEATURES);
		info.split.num_pmcs = ebx.split.num_df_pmc;
	}

	*per_cpu_ptr(uncore->info, cpu) = info;
}

static
int amd_uncore_df_ctx_init(struct amd_uncore *uncore, unsigned int cpu)
{
	struct attribute **df_attr = amd_uncore_df_format_attr;
	struct amd_uncore_pmu *pmu;
	int num_counters;

	/* Run just once */
	if (uncore->init_done)
		return amd_uncore_ctx_init(uncore, cpu);

	num_counters = amd_uncore_ctx_num_pmcs(uncore, cpu);
	if (!num_counters)
		goto done;

	/* No grouping, single instance for a system */
	uncore->pmus = kzalloc(sizeof(*uncore->pmus), GFP_KERNEL);
	if (!uncore->pmus)
		goto done;

	/*
	 * For Family 17h and above, the Northbridge counters are repurposed
	 * as Data Fabric counters. The PMUs are exported based on family as
	 * either NB or DF.
	 */
	pmu = &uncore->pmus[0];
	strscpy(pmu->name, boot_cpu_data.x86 >= 0x17 ? "amd_df" : "amd_nb",
		sizeof(pmu->name));
	pmu->num_counters = num_counters;
	pmu->msr_base = MSR_F15H_NB_PERF_CTL;
	pmu->rdpmc_base = RDPMC_BASE_NB;
	pmu->group = amd_uncore_ctx_gid(uncore, cpu);

	if (pmu_version >= 2) {
		*df_attr++ = &format_attr_event14v2.attr;
		*df_attr++ = &format_attr_umask12.attr;
	} else if (boot_cpu_data.x86 >= 0x17) {
		*df_attr = &format_attr_event14.attr;
	}

	pmu->ctx = alloc_percpu(struct amd_uncore_ctx *);
	if (!pmu->ctx)
		goto done;

	pmu->pmu = (struct pmu) {
		.task_ctx_nr	= perf_invalid_context,
		.attr_groups	= amd_uncore_df_attr_groups,
		.name		= pmu->name,
		.event_init	= amd_uncore_df_event_init,
		.add		= amd_uncore_df_add,
		.del		= amd_uncore_del,
		.start		= amd_uncore_start,
		.stop		= amd_uncore_stop,
		.read		= amd_uncore_read,
		.capabilities	= PERF_PMU_CAP_NO_EXCLUDE | PERF_PMU_CAP_NO_INTERRUPT,
		.module		= THIS_MODULE,
	};

	if (perf_pmu_register(&pmu->pmu, pmu->pmu.name, -1)) {
		free_percpu(pmu->ctx);
		pmu->ctx = NULL;
		goto done;
	}

	pr_info("%d %s%s counters detected\n", pmu->num_counters,
		boot_cpu_data.x86_vendor == X86_VENDOR_HYGON ?  "HYGON " : "",
		pmu->pmu.name);

	uncore->num_pmus = 1;

done:
	uncore->init_done = true;

	return amd_uncore_ctx_init(uncore, cpu);
}

static int amd_uncore_l3_event_init(struct perf_event *event)
{
	int ret = amd_uncore_event_init(event);
	struct hw_perf_event *hwc = &event->hw;
	u64 config = event->attr.config;
	u64 mask;

	hwc->config = config & AMD64_RAW_EVENT_MASK_NB;

	/*
	 * SliceMask and ThreadMask need to be set for certain L3 events.
	 * For other events, the two fields do not affect the count.
	 */
	if (ret || boot_cpu_data.x86 < 0x17)
		return ret;

	mask = config & (AMD64_L3_F19H_THREAD_MASK | AMD64_L3_SLICEID_MASK |
			 AMD64_L3_EN_ALL_CORES | AMD64_L3_EN_ALL_SLICES |
			 AMD64_L3_COREID_MASK);

	if (boot_cpu_data.x86 <= 0x18)
		mask = ((config & AMD64_L3_SLICE_MASK) ? : AMD64_L3_SLICE_MASK) |
		       ((config & AMD64_L3_THREAD_MASK) ? : AMD64_L3_THREAD_MASK);

	/*
	 * If the user doesn't specify a ThreadMask, they're not trying to
	 * count core 0, so we enable all cores & threads.
	 * We'll also assume that they want to count slice 0 if they specify
	 * a ThreadMask and leave SliceId and EnAllSlices unpopulated.
	 */
	else if (!(config & AMD64_L3_F19H_THREAD_MASK))
		mask = AMD64_L3_F19H_THREAD_MASK | AMD64_L3_EN_ALL_SLICES |
		       AMD64_L3_EN_ALL_CORES;

	hwc->config |= mask;

	return 0;
}

static
void amd_uncore_l3_ctx_scan(struct amd_uncore *uncore, unsigned int cpu)
{
	union amd_uncore_info info;

	if (!boot_cpu_has(X86_FEATURE_PERFCTR_LLC))
		return;

	info.split.aux_data = 0;
	info.split.num_pmcs = NUM_COUNTERS_L2;
	info.split.gid = 0;
	info.split.cid = per_cpu_llc_id(cpu);

	if (boot_cpu_data.x86 >= 0x17)
		info.split.num_pmcs = NUM_COUNTERS_L3;

	*per_cpu_ptr(uncore->info, cpu) = info;
}

static
int amd_uncore_l3_ctx_init(struct amd_uncore *uncore, unsigned int cpu)
{
	struct attribute **l3_attr = amd_uncore_l3_format_attr;
	struct amd_uncore_pmu *pmu;
	int num_counters;

	/* Run just once */
	if (uncore->init_done)
		return amd_uncore_ctx_init(uncore, cpu);

	num_counters = amd_uncore_ctx_num_pmcs(uncore, cpu);
	if (!num_counters)
		goto done;

	/* No grouping, single instance for a system */
	uncore->pmus = kzalloc(sizeof(*uncore->pmus), GFP_KERNEL);
	if (!uncore->pmus)
		goto done;

	/*
	 * For Family 17h and above, L3 cache counters are available instead
	 * of L2 cache counters. The PMUs are exported based on family as
	 * either L2 or L3.
	 */
	pmu = &uncore->pmus[0];
	strscpy(pmu->name, boot_cpu_data.x86 >= 0x17 ? "amd_l3" : "amd_l2",
		sizeof(pmu->name));
	pmu->num_counters = num_counters;
	pmu->msr_base = MSR_F16H_L2I_PERF_CTL;
	pmu->rdpmc_base = RDPMC_BASE_LLC;
	pmu->group = amd_uncore_ctx_gid(uncore, cpu);

	if (boot_cpu_data.x86 >= 0x17) {
		*l3_attr++ = &format_attr_event8.attr;
		*l3_attr++ = &format_attr_umask8.attr;
		*l3_attr++ = boot_cpu_data.x86 >= 0x19 ?
			     &format_attr_threadmask2.attr :
			     &format_attr_threadmask8.attr;
	}

	pmu->ctx = alloc_percpu(struct amd_uncore_ctx *);
	if (!pmu->ctx)
		goto done;

	pmu->pmu = (struct pmu) {
		.task_ctx_nr	= perf_invalid_context,
		.attr_groups	= amd_uncore_l3_attr_groups,
		.attr_update	= amd_uncore_l3_attr_update,
		.name		= pmu->name,
		.event_init	= amd_uncore_l3_event_init,
		.add		= amd_uncore_add,
		.del		= amd_uncore_del,
		.start		= amd_uncore_start,
		.stop		= amd_uncore_stop,
		.read		= amd_uncore_read,
		.capabilities	= PERF_PMU_CAP_NO_EXCLUDE | PERF_PMU_CAP_NO_INTERRUPT,
		.module		= THIS_MODULE,
	};

	if (perf_pmu_register(&pmu->pmu, pmu->pmu.name, -1)) {
		free_percpu(pmu->ctx);
		pmu->ctx = NULL;
		goto done;
	}

	pr_info("%d %s%s counters detected\n", pmu->num_counters,
		boot_cpu_data.x86_vendor == X86_VENDOR_HYGON ?  "HYGON " : "",
		pmu->pmu.name);

	uncore->num_pmus = 1;

done:
	uncore->init_done = true;

	return amd_uncore_ctx_init(uncore, cpu);
}

static int amd_uncore_umc_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int ret = amd_uncore_event_init(event);

	if (ret)
		return ret;

	hwc->config = event->attr.config & AMD64_PERFMON_V2_RAW_EVENT_MASK_UMC;

	return 0;
}

static void amd_uncore_umc_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;

	if (flags & PERF_EF_RELOAD)
		wrmsrl(hwc->event_base, (u64)local64_read(&hwc->prev_count));

	hwc->state = 0;
	wrmsrl(hwc->config_base, (hwc->config | AMD64_PERFMON_V2_ENABLE_UMC));
	perf_event_update_userpage(event);
}

static
void amd_uncore_umc_ctx_scan(struct amd_uncore *uncore, unsigned int cpu)
{
	union cpuid_0x80000022_ebx ebx;
	union amd_uncore_info info;
	unsigned int eax, ecx, edx;

	if (pmu_version < 2)
		return;

	cpuid(EXT_PERFMON_DEBUG_FEATURES, &eax, &ebx.full, &ecx, &edx);
	info.split.aux_data = ecx;	/* stash active mask */
	info.split.num_pmcs = ebx.split.num_umc_pmc;
	info.split.gid = topology_logical_package_id(cpu);
	info.split.cid = topology_logical_package_id(cpu);
	*per_cpu_ptr(uncore->info, cpu) = info;
}

static
int amd_uncore_umc_ctx_init(struct amd_uncore *uncore, unsigned int cpu)
{
	DECLARE_BITMAP(gmask, UNCORE_GROUP_MAX) = { 0 };
	u8 group_num_pmus[UNCORE_GROUP_MAX] = { 0 };
	u8 group_num_pmcs[UNCORE_GROUP_MAX] = { 0 };
	union amd_uncore_info info;
	struct amd_uncore_pmu *pmu;
	int gid, i;
	u16 index = 0;

	if (pmu_version < 2)
		return 0;

	/* Run just once */
	if (uncore->init_done)
		return amd_uncore_ctx_init(uncore, cpu);

	/* Find unique groups */
	for_each_online_cpu(i) {
		info = *per_cpu_ptr(uncore->info, i);
		gid = info.split.gid;
		if (test_bit(gid, gmask))
			continue;

		__set_bit(gid, gmask);
		group_num_pmus[gid] = hweight32(info.split.aux_data);
		group_num_pmcs[gid] = info.split.num_pmcs;
		uncore->num_pmus += group_num_pmus[gid];
	}

	uncore->pmus = kzalloc(sizeof(*uncore->pmus) * uncore->num_pmus,
			       GFP_KERNEL);
	if (!uncore->pmus) {
		uncore->num_pmus = 0;
		goto done;
	}

	for_each_set_bit(gid, gmask, UNCORE_GROUP_MAX) {
		for (i = 0; i < group_num_pmus[gid]; i++) {
			pmu = &uncore->pmus[index];
			snprintf(pmu->name, sizeof(pmu->name), "amd_umc_%hu", index);
			pmu->num_counters = group_num_pmcs[gid] / group_num_pmus[gid];
			pmu->msr_base = MSR_F19H_UMC_PERF_CTL + i * pmu->num_counters * 2;
			pmu->rdpmc_base = -1;
			pmu->group = gid;

			pmu->ctx = alloc_percpu(struct amd_uncore_ctx *);
			if (!pmu->ctx)
				goto done;

			pmu->pmu = (struct pmu) {
				.task_ctx_nr	= perf_invalid_context,
				.attr_groups	= amd_uncore_umc_attr_groups,
				.name		= pmu->name,
				.event_init	= amd_uncore_umc_event_init,
				.add		= amd_uncore_add,
				.del		= amd_uncore_del,
				.start		= amd_uncore_umc_start,
				.stop		= amd_uncore_stop,
				.read		= amd_uncore_read,
				.capabilities	= PERF_PMU_CAP_NO_EXCLUDE | PERF_PMU_CAP_NO_INTERRUPT,
				.module		= THIS_MODULE,
			};

			if (perf_pmu_register(&pmu->pmu, pmu->pmu.name, -1)) {
				free_percpu(pmu->ctx);
				pmu->ctx = NULL;
				goto done;
			}

			pr_info("%d %s counters detected\n", pmu->num_counters,
				pmu->pmu.name);

			index++;
		}
	}

done:
	uncore->num_pmus = index;
	uncore->init_done = true;

	return amd_uncore_ctx_init(uncore, cpu);
}

static struct amd_uncore uncores[UNCORE_TYPE_MAX] = {
	/* UNCORE_TYPE_DF */
	{
		.scan = amd_uncore_df_ctx_scan,
		.init = amd_uncore_df_ctx_init,
		.move = amd_uncore_ctx_move,
		.free = amd_uncore_ctx_free,
	},
	/* UNCORE_TYPE_L3 */
	{
		.scan = amd_uncore_l3_ctx_scan,
		.init = amd_uncore_l3_ctx_init,
		.move = amd_uncore_ctx_move,
		.free = amd_uncore_ctx_free,
	},
	/* UNCORE_TYPE_UMC */
	{
		.scan = amd_uncore_umc_ctx_scan,
		.init = amd_uncore_umc_ctx_init,
		.move = amd_uncore_ctx_move,
		.free = amd_uncore_ctx_free,
	},
};

static int __init amd_uncore_init(void)
{
	struct amd_uncore *uncore;
	int ret = -ENODEV;
	int i;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD &&
	    boot_cpu_data.x86_vendor != X86_VENDOR_HYGON)
		return -ENODEV;

	if (!boot_cpu_has(X86_FEATURE_TOPOEXT))
		return -ENODEV;

	if (boot_cpu_has(X86_FEATURE_PERFMON_V2))
		pmu_version = 2;

	for (i = 0; i < UNCORE_TYPE_MAX; i++) {
		uncore = &uncores[i];

		BUG_ON(!uncore->scan);
		BUG_ON(!uncore->init);
		BUG_ON(!uncore->move);
		BUG_ON(!uncore->free);

		uncore->info = alloc_percpu(union amd_uncore_info);
		if (!uncore->info) {
			ret = -ENOMEM;
			goto fail;
		}
	};

	/*
	 * Install callbacks. Core will call them for each online cpu.
	 */
	ret = cpuhp_setup_state(CPUHP_PERF_X86_AMD_UNCORE_PREP,
				"perf/x86/amd/uncore:prepare",
				NULL, amd_uncore_cpu_dead);
	if (ret)
		goto fail;

	ret = cpuhp_setup_state(CPUHP_AP_PERF_X86_AMD_UNCORE_STARTING,
				"perf/x86/amd/uncore:starting",
				amd_uncore_cpu_starting, NULL);
	if (ret)
		goto fail_prep;

	ret = cpuhp_setup_state(CPUHP_AP_PERF_X86_AMD_UNCORE_ONLINE,
				"perf/x86/amd/uncore:online",
				amd_uncore_cpu_online,
				amd_uncore_cpu_down_prepare);
	if (ret)
		goto fail_start;

	return 0;

fail_start:
	cpuhp_remove_state(CPUHP_AP_PERF_X86_AMD_UNCORE_STARTING);
fail_prep:
	cpuhp_remove_state(CPUHP_PERF_X86_AMD_UNCORE_PREP);
fail:
	for (i = 0; i < UNCORE_TYPE_MAX; i++) {
		uncore = &uncores[i];
		if (uncore->info) {
			free_percpu(uncore->info);
			uncore->info = NULL;
		}
	}

	return ret;
}

static void __exit amd_uncore_exit(void)
{
	struct amd_uncore *uncore;
	struct amd_uncore_pmu *pmu;
	int i, j;

	cpuhp_remove_state(CPUHP_AP_PERF_X86_AMD_UNCORE_ONLINE);
	cpuhp_remove_state(CPUHP_AP_PERF_X86_AMD_UNCORE_STARTING);
	cpuhp_remove_state(CPUHP_PERF_X86_AMD_UNCORE_PREP);

	for (i = 0; i < UNCORE_TYPE_MAX; i++) {
		uncore = &uncores[i];
		if (!uncore->info)
			continue;

		free_percpu(uncore->info);
		uncore->info = NULL;

		for (j = 0; j < uncore->num_pmus; j++) {
			pmu = &uncore->pmus[j];
			if (!pmu->ctx)
				continue;

			perf_pmu_unregister(&pmu->pmu);
			free_percpu(pmu->ctx);
			pmu->ctx = NULL;
		}

		kfree(uncore->pmus);
		uncore->pmus = NULL;
	}
}

module_init(amd_uncore_init);
module_exit(amd_uncore_exit);

MODULE_DESCRIPTION("AMD Uncore Driver");
MODULE_LICENSE("GPL v2");

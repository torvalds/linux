// SPDX-License-Identifier: GPL-2.0-only
/*
 * Perf support for the Statistical Profiling Extension, introduced as
 * part of ARMv8.2.
 *
 * Copyright (C) 2016 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#define PMUNAME					"arm_spe"
#define DRVNAME					PMUNAME "_pmu"
#define pr_fmt(fmt)				DRVNAME ": " fmt

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/capability.h>
#include <linux/cpuhotplug.h>
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/perf_event.h>
#include <linux/perf/arm_pmu.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/vmalloc.h>

#include <asm/barrier.h>
#include <asm/cpufeature.h>
#include <asm/mmu.h>
#include <asm/sysreg.h>

/*
 * Cache if the event is allowed to trace Context information.
 * This allows us to perform the check, i.e, perfmon_capable(),
 * in the context of the event owner, once, during the event_init().
 */
#define SPE_PMU_HW_FLAGS_CX			0x00001

static_assert((PERF_EVENT_FLAG_ARCH & SPE_PMU_HW_FLAGS_CX) == SPE_PMU_HW_FLAGS_CX);

static void set_spe_event_has_cx(struct perf_event *event)
{
	if (IS_ENABLED(CONFIG_PID_IN_CONTEXTIDR) && perfmon_capable())
		event->hw.flags |= SPE_PMU_HW_FLAGS_CX;
}

static bool get_spe_event_has_cx(struct perf_event *event)
{
	return !!(event->hw.flags & SPE_PMU_HW_FLAGS_CX);
}

#define ARM_SPE_BUF_PAD_BYTE			0

struct arm_spe_pmu_buf {
	int					nr_pages;
	bool					snapshot;
	void					*base;
};

struct arm_spe_pmu {
	struct pmu				pmu;
	struct platform_device			*pdev;
	cpumask_t				supported_cpus;
	struct hlist_node			hotplug_node;

	int					irq; /* PPI */
	u16					pmsver;
	u16					min_period;
	u16					counter_sz;

#define SPE_PMU_FEAT_FILT_EVT			(1UL << 0)
#define SPE_PMU_FEAT_FILT_TYP			(1UL << 1)
#define SPE_PMU_FEAT_FILT_LAT			(1UL << 2)
#define SPE_PMU_FEAT_ARCH_INST			(1UL << 3)
#define SPE_PMU_FEAT_LDS			(1UL << 4)
#define SPE_PMU_FEAT_ERND			(1UL << 5)
#define SPE_PMU_FEAT_INV_FILT_EVT		(1UL << 6)
#define SPE_PMU_FEAT_DEV_PROBED			(1UL << 63)
	u64					features;

	u16					max_record_sz;
	u16					align;
	struct perf_output_handle __percpu	*handle;
};

#define to_spe_pmu(p) (container_of(p, struct arm_spe_pmu, pmu))

/* Convert a free-running index from perf into an SPE buffer offset */
#define PERF_IDX2OFF(idx, buf)	((idx) % ((buf)->nr_pages << PAGE_SHIFT))

/* Keep track of our dynamic hotplug state */
static enum cpuhp_state arm_spe_pmu_online;

enum arm_spe_pmu_buf_fault_action {
	SPE_PMU_BUF_FAULT_ACT_SPURIOUS,
	SPE_PMU_BUF_FAULT_ACT_FATAL,
	SPE_PMU_BUF_FAULT_ACT_OK,
};

/* This sysfs gunk was really good fun to write. */
enum arm_spe_pmu_capabilities {
	SPE_PMU_CAP_ARCH_INST = 0,
	SPE_PMU_CAP_ERND,
	SPE_PMU_CAP_FEAT_MAX,
	SPE_PMU_CAP_CNT_SZ = SPE_PMU_CAP_FEAT_MAX,
	SPE_PMU_CAP_MIN_IVAL,
};

static int arm_spe_pmu_feat_caps[SPE_PMU_CAP_FEAT_MAX] = {
	[SPE_PMU_CAP_ARCH_INST]	= SPE_PMU_FEAT_ARCH_INST,
	[SPE_PMU_CAP_ERND]	= SPE_PMU_FEAT_ERND,
};

static u32 arm_spe_pmu_cap_get(struct arm_spe_pmu *spe_pmu, int cap)
{
	if (cap < SPE_PMU_CAP_FEAT_MAX)
		return !!(spe_pmu->features & arm_spe_pmu_feat_caps[cap]);

	switch (cap) {
	case SPE_PMU_CAP_CNT_SZ:
		return spe_pmu->counter_sz;
	case SPE_PMU_CAP_MIN_IVAL:
		return spe_pmu->min_period;
	default:
		WARN(1, "unknown cap %d\n", cap);
	}

	return 0;
}

static ssize_t arm_spe_pmu_cap_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct arm_spe_pmu *spe_pmu = dev_get_drvdata(dev);
	struct dev_ext_attribute *ea =
		container_of(attr, struct dev_ext_attribute, attr);
	int cap = (long)ea->var;

	return sysfs_emit(buf, "%u\n", arm_spe_pmu_cap_get(spe_pmu, cap));
}

#define SPE_EXT_ATTR_ENTRY(_name, _func, _var)				\
	&((struct dev_ext_attribute[]) {				\
		{ __ATTR(_name, S_IRUGO, _func, NULL), (void *)_var }	\
	})[0].attr.attr

#define SPE_CAP_EXT_ATTR_ENTRY(_name, _var)				\
	SPE_EXT_ATTR_ENTRY(_name, arm_spe_pmu_cap_show, _var)

static struct attribute *arm_spe_pmu_cap_attr[] = {
	SPE_CAP_EXT_ATTR_ENTRY(arch_inst, SPE_PMU_CAP_ARCH_INST),
	SPE_CAP_EXT_ATTR_ENTRY(ernd, SPE_PMU_CAP_ERND),
	SPE_CAP_EXT_ATTR_ENTRY(count_size, SPE_PMU_CAP_CNT_SZ),
	SPE_CAP_EXT_ATTR_ENTRY(min_interval, SPE_PMU_CAP_MIN_IVAL),
	NULL,
};

static const struct attribute_group arm_spe_pmu_cap_group = {
	.name	= "caps",
	.attrs	= arm_spe_pmu_cap_attr,
};

/* User ABI */
#define ATTR_CFG_FLD_ts_enable_CFG		config	/* PMSCR_EL1.TS */
#define ATTR_CFG_FLD_ts_enable_LO		0
#define ATTR_CFG_FLD_ts_enable_HI		0
#define ATTR_CFG_FLD_pa_enable_CFG		config	/* PMSCR_EL1.PA */
#define ATTR_CFG_FLD_pa_enable_LO		1
#define ATTR_CFG_FLD_pa_enable_HI		1
#define ATTR_CFG_FLD_pct_enable_CFG		config	/* PMSCR_EL1.PCT */
#define ATTR_CFG_FLD_pct_enable_LO		2
#define ATTR_CFG_FLD_pct_enable_HI		2
#define ATTR_CFG_FLD_jitter_CFG			config	/* PMSIRR_EL1.RND */
#define ATTR_CFG_FLD_jitter_LO			16
#define ATTR_CFG_FLD_jitter_HI			16
#define ATTR_CFG_FLD_branch_filter_CFG		config	/* PMSFCR_EL1.B */
#define ATTR_CFG_FLD_branch_filter_LO		32
#define ATTR_CFG_FLD_branch_filter_HI		32
#define ATTR_CFG_FLD_load_filter_CFG		config	/* PMSFCR_EL1.LD */
#define ATTR_CFG_FLD_load_filter_LO		33
#define ATTR_CFG_FLD_load_filter_HI		33
#define ATTR_CFG_FLD_store_filter_CFG		config	/* PMSFCR_EL1.ST */
#define ATTR_CFG_FLD_store_filter_LO		34
#define ATTR_CFG_FLD_store_filter_HI		34

#define ATTR_CFG_FLD_event_filter_CFG		config1	/* PMSEVFR_EL1 */
#define ATTR_CFG_FLD_event_filter_LO		0
#define ATTR_CFG_FLD_event_filter_HI		63

#define ATTR_CFG_FLD_min_latency_CFG		config2	/* PMSLATFR_EL1.MINLAT */
#define ATTR_CFG_FLD_min_latency_LO		0
#define ATTR_CFG_FLD_min_latency_HI		11

#define ATTR_CFG_FLD_inv_event_filter_CFG	config3	/* PMSNEVFR_EL1 */
#define ATTR_CFG_FLD_inv_event_filter_LO	0
#define ATTR_CFG_FLD_inv_event_filter_HI	63

GEN_PMU_FORMAT_ATTR(ts_enable);
GEN_PMU_FORMAT_ATTR(pa_enable);
GEN_PMU_FORMAT_ATTR(pct_enable);
GEN_PMU_FORMAT_ATTR(jitter);
GEN_PMU_FORMAT_ATTR(branch_filter);
GEN_PMU_FORMAT_ATTR(load_filter);
GEN_PMU_FORMAT_ATTR(store_filter);
GEN_PMU_FORMAT_ATTR(event_filter);
GEN_PMU_FORMAT_ATTR(inv_event_filter);
GEN_PMU_FORMAT_ATTR(min_latency);

static struct attribute *arm_spe_pmu_formats_attr[] = {
	&format_attr_ts_enable.attr,
	&format_attr_pa_enable.attr,
	&format_attr_pct_enable.attr,
	&format_attr_jitter.attr,
	&format_attr_branch_filter.attr,
	&format_attr_load_filter.attr,
	&format_attr_store_filter.attr,
	&format_attr_event_filter.attr,
	&format_attr_inv_event_filter.attr,
	&format_attr_min_latency.attr,
	NULL,
};

static umode_t arm_spe_pmu_format_attr_is_visible(struct kobject *kobj,
						  struct attribute *attr,
						  int unused)
	{
	struct device *dev = kobj_to_dev(kobj);
	struct arm_spe_pmu *spe_pmu = dev_get_drvdata(dev);

	if (attr == &format_attr_inv_event_filter.attr && !(spe_pmu->features & SPE_PMU_FEAT_INV_FILT_EVT))
		return 0;

	return attr->mode;
}

static const struct attribute_group arm_spe_pmu_format_group = {
	.name	= "format",
	.is_visible = arm_spe_pmu_format_attr_is_visible,
	.attrs	= arm_spe_pmu_formats_attr,
};

static ssize_t cpumask_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct arm_spe_pmu *spe_pmu = dev_get_drvdata(dev);

	return cpumap_print_to_pagebuf(true, buf, &spe_pmu->supported_cpus);
}
static DEVICE_ATTR_RO(cpumask);

static struct attribute *arm_spe_pmu_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static const struct attribute_group arm_spe_pmu_group = {
	.attrs	= arm_spe_pmu_attrs,
};

static const struct attribute_group *arm_spe_pmu_attr_groups[] = {
	&arm_spe_pmu_group,
	&arm_spe_pmu_cap_group,
	&arm_spe_pmu_format_group,
	NULL,
};

/* Convert between user ABI and register values */
static u64 arm_spe_event_to_pmscr(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	u64 reg = 0;

	reg |= FIELD_PREP(PMSCR_EL1_TS, ATTR_CFG_GET_FLD(attr, ts_enable));
	reg |= FIELD_PREP(PMSCR_EL1_PA, ATTR_CFG_GET_FLD(attr, pa_enable));
	reg |= FIELD_PREP(PMSCR_EL1_PCT, ATTR_CFG_GET_FLD(attr, pct_enable));

	if (!attr->exclude_user)
		reg |= PMSCR_EL1_E0SPE;

	if (!attr->exclude_kernel)
		reg |= PMSCR_EL1_E1SPE;

	if (get_spe_event_has_cx(event))
		reg |= PMSCR_EL1_CX;

	return reg;
}

static void arm_spe_event_sanitise_period(struct perf_event *event)
{
	struct arm_spe_pmu *spe_pmu = to_spe_pmu(event->pmu);
	u64 period = event->hw.sample_period;
	u64 max_period = PMSIRR_EL1_INTERVAL_MASK;

	if (period < spe_pmu->min_period)
		period = spe_pmu->min_period;
	else if (period > max_period)
		period = max_period;
	else
		period &= max_period;

	event->hw.sample_period = period;
}

static u64 arm_spe_event_to_pmsirr(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	u64 reg = 0;

	arm_spe_event_sanitise_period(event);

	reg |= FIELD_PREP(PMSIRR_EL1_RND, ATTR_CFG_GET_FLD(attr, jitter));
	reg |= event->hw.sample_period;

	return reg;
}

static u64 arm_spe_event_to_pmsfcr(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	u64 reg = 0;

	reg |= FIELD_PREP(PMSFCR_EL1_LD, ATTR_CFG_GET_FLD(attr, load_filter));
	reg |= FIELD_PREP(PMSFCR_EL1_ST, ATTR_CFG_GET_FLD(attr, store_filter));
	reg |= FIELD_PREP(PMSFCR_EL1_B, ATTR_CFG_GET_FLD(attr, branch_filter));

	if (reg)
		reg |= PMSFCR_EL1_FT;

	if (ATTR_CFG_GET_FLD(attr, event_filter))
		reg |= PMSFCR_EL1_FE;

	if (ATTR_CFG_GET_FLD(attr, inv_event_filter))
		reg |= PMSFCR_EL1_FnE;

	if (ATTR_CFG_GET_FLD(attr, min_latency))
		reg |= PMSFCR_EL1_FL;

	return reg;
}

static u64 arm_spe_event_to_pmsevfr(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	return ATTR_CFG_GET_FLD(attr, event_filter);
}

static u64 arm_spe_event_to_pmsnevfr(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	return ATTR_CFG_GET_FLD(attr, inv_event_filter);
}

static u64 arm_spe_event_to_pmslatfr(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	return FIELD_PREP(PMSLATFR_EL1_MINLAT, ATTR_CFG_GET_FLD(attr, min_latency));
}

static void arm_spe_pmu_pad_buf(struct perf_output_handle *handle, int len)
{
	struct arm_spe_pmu_buf *buf = perf_get_aux(handle);
	u64 head = PERF_IDX2OFF(handle->head, buf);

	memset(buf->base + head, ARM_SPE_BUF_PAD_BYTE, len);
	if (!buf->snapshot)
		perf_aux_output_skip(handle, len);
}

static u64 arm_spe_pmu_next_snapshot_off(struct perf_output_handle *handle)
{
	struct arm_spe_pmu_buf *buf = perf_get_aux(handle);
	struct arm_spe_pmu *spe_pmu = to_spe_pmu(handle->event->pmu);
	u64 head = PERF_IDX2OFF(handle->head, buf);
	u64 limit = buf->nr_pages * PAGE_SIZE;

	/*
	 * The trace format isn't parseable in reverse, so clamp
	 * the limit to half of the buffer size in snapshot mode
	 * so that the worst case is half a buffer of records, as
	 * opposed to a single record.
	 */
	if (head < limit >> 1)
		limit >>= 1;

	/*
	 * If we're within max_record_sz of the limit, we must
	 * pad, move the head index and recompute the limit.
	 */
	if (limit - head < spe_pmu->max_record_sz) {
		arm_spe_pmu_pad_buf(handle, limit - head);
		handle->head = PERF_IDX2OFF(limit, buf);
		limit = ((buf->nr_pages * PAGE_SIZE) >> 1) + handle->head;
	}

	return limit;
}

static u64 __arm_spe_pmu_next_off(struct perf_output_handle *handle)
{
	struct arm_spe_pmu *spe_pmu = to_spe_pmu(handle->event->pmu);
	struct arm_spe_pmu_buf *buf = perf_get_aux(handle);
	const u64 bufsize = buf->nr_pages * PAGE_SIZE;
	u64 limit = bufsize;
	u64 head, tail, wakeup;

	/*
	 * The head can be misaligned for two reasons:
	 *
	 * 1. The hardware left PMBPTR pointing to the first byte after
	 *    a record when generating a buffer management event.
	 *
	 * 2. We used perf_aux_output_skip to consume handle->size bytes
	 *    and CIRC_SPACE was used to compute the size, which always
	 *    leaves one entry free.
	 *
	 * Deal with this by padding to the next alignment boundary and
	 * moving the head index. If we run out of buffer space, we'll
	 * reduce handle->size to zero and end up reporting truncation.
	 */
	head = PERF_IDX2OFF(handle->head, buf);
	if (!IS_ALIGNED(head, spe_pmu->align)) {
		unsigned long delta = roundup(head, spe_pmu->align) - head;

		delta = min(delta, handle->size);
		arm_spe_pmu_pad_buf(handle, delta);
		head = PERF_IDX2OFF(handle->head, buf);
	}

	/* If we've run out of free space, then nothing more to do */
	if (!handle->size)
		goto no_space;

	/* Compute the tail and wakeup indices now that we've aligned head */
	tail = PERF_IDX2OFF(handle->head + handle->size, buf);
	wakeup = PERF_IDX2OFF(handle->wakeup, buf);

	/*
	 * Avoid clobbering unconsumed data. We know we have space, so
	 * if we see head == tail we know that the buffer is empty. If
	 * head > tail, then there's nothing to clobber prior to
	 * wrapping.
	 */
	if (head < tail)
		limit = round_down(tail, PAGE_SIZE);

	/*
	 * Wakeup may be arbitrarily far into the future. If it's not in
	 * the current generation, either we'll wrap before hitting it,
	 * or it's in the past and has been handled already.
	 *
	 * If there's a wakeup before we wrap, arrange to be woken up by
	 * the page boundary following it. Keep the tail boundary if
	 * that's lower.
	 */
	if (handle->wakeup < (handle->head + handle->size) && head <= wakeup)
		limit = min(limit, round_up(wakeup, PAGE_SIZE));

	if (limit > head)
		return limit;

	arm_spe_pmu_pad_buf(handle, handle->size);
no_space:
	perf_aux_output_flag(handle, PERF_AUX_FLAG_TRUNCATED);
	perf_aux_output_end(handle, 0);
	return 0;
}

static u64 arm_spe_pmu_next_off(struct perf_output_handle *handle)
{
	struct arm_spe_pmu_buf *buf = perf_get_aux(handle);
	struct arm_spe_pmu *spe_pmu = to_spe_pmu(handle->event->pmu);
	u64 limit = __arm_spe_pmu_next_off(handle);
	u64 head = PERF_IDX2OFF(handle->head, buf);

	/*
	 * If the head has come too close to the end of the buffer,
	 * then pad to the end and recompute the limit.
	 */
	if (limit && (limit - head < spe_pmu->max_record_sz)) {
		arm_spe_pmu_pad_buf(handle, limit - head);
		limit = __arm_spe_pmu_next_off(handle);
	}

	return limit;
}

static void arm_spe_perf_aux_output_begin(struct perf_output_handle *handle,
					  struct perf_event *event)
{
	u64 base, limit;
	struct arm_spe_pmu_buf *buf;

	/* Start a new aux session */
	buf = perf_aux_output_begin(handle, event);
	if (!buf) {
		event->hw.state |= PERF_HES_STOPPED;
		/*
		 * We still need to clear the limit pointer, since the
		 * profiler might only be disabled by virtue of a fault.
		 */
		limit = 0;
		goto out_write_limit;
	}

	limit = buf->snapshot ? arm_spe_pmu_next_snapshot_off(handle)
			      : arm_spe_pmu_next_off(handle);
	if (limit)
		limit |= PMBLIMITR_EL1_E;

	limit += (u64)buf->base;
	base = (u64)buf->base + PERF_IDX2OFF(handle->head, buf);
	write_sysreg_s(base, SYS_PMBPTR_EL1);

out_write_limit:
	write_sysreg_s(limit, SYS_PMBLIMITR_EL1);
}

static void arm_spe_perf_aux_output_end(struct perf_output_handle *handle)
{
	struct arm_spe_pmu_buf *buf = perf_get_aux(handle);
	u64 offset, size;

	offset = read_sysreg_s(SYS_PMBPTR_EL1) - (u64)buf->base;
	size = offset - PERF_IDX2OFF(handle->head, buf);

	if (buf->snapshot)
		handle->head = offset;

	perf_aux_output_end(handle, size);
}

static void arm_spe_pmu_disable_and_drain_local(void)
{
	/* Disable profiling at EL0 and EL1 */
	write_sysreg_s(0, SYS_PMSCR_EL1);
	isb();

	/* Drain any buffered data */
	psb_csync();
	dsb(nsh);

	/* Disable the profiling buffer */
	write_sysreg_s(0, SYS_PMBLIMITR_EL1);
	isb();
}

/* IRQ handling */
static enum arm_spe_pmu_buf_fault_action
arm_spe_pmu_buf_get_fault_act(struct perf_output_handle *handle)
{
	const char *err_str;
	u64 pmbsr;
	enum arm_spe_pmu_buf_fault_action ret;

	/*
	 * Ensure new profiling data is visible to the CPU and any external
	 * aborts have been resolved.
	 */
	psb_csync();
	dsb(nsh);

	/* Ensure hardware updates to PMBPTR_EL1 are visible */
	isb();

	/* Service required? */
	pmbsr = read_sysreg_s(SYS_PMBSR_EL1);
	if (!FIELD_GET(PMBSR_EL1_S, pmbsr))
		return SPE_PMU_BUF_FAULT_ACT_SPURIOUS;

	/*
	 * If we've lost data, disable profiling and also set the PARTIAL
	 * flag to indicate that the last record is corrupted.
	 */
	if (FIELD_GET(PMBSR_EL1_DL, pmbsr))
		perf_aux_output_flag(handle, PERF_AUX_FLAG_TRUNCATED |
					     PERF_AUX_FLAG_PARTIAL);

	/* Report collisions to userspace so that it can up the period */
	if (FIELD_GET(PMBSR_EL1_COLL, pmbsr))
		perf_aux_output_flag(handle, PERF_AUX_FLAG_COLLISION);

	/* We only expect buffer management events */
	switch (FIELD_GET(PMBSR_EL1_EC, pmbsr)) {
	case PMBSR_EL1_EC_BUF:
		/* Handled below */
		break;
	case PMBSR_EL1_EC_FAULT_S1:
	case PMBSR_EL1_EC_FAULT_S2:
		err_str = "Unexpected buffer fault";
		goto out_err;
	default:
		err_str = "Unknown error code";
		goto out_err;
	}

	/* Buffer management event */
	switch (FIELD_GET(PMBSR_EL1_BUF_BSC_MASK, pmbsr)) {
	case PMBSR_EL1_BUF_BSC_FULL:
		ret = SPE_PMU_BUF_FAULT_ACT_OK;
		goto out_stop;
	default:
		err_str = "Unknown buffer status code";
	}

out_err:
	pr_err_ratelimited("%s on CPU %d [PMBSR=0x%016llx, PMBPTR=0x%016llx, PMBLIMITR=0x%016llx]\n",
			   err_str, smp_processor_id(), pmbsr,
			   read_sysreg_s(SYS_PMBPTR_EL1),
			   read_sysreg_s(SYS_PMBLIMITR_EL1));
	ret = SPE_PMU_BUF_FAULT_ACT_FATAL;

out_stop:
	arm_spe_perf_aux_output_end(handle);
	return ret;
}

static irqreturn_t arm_spe_pmu_irq_handler(int irq, void *dev)
{
	struct perf_output_handle *handle = dev;
	struct perf_event *event = handle->event;
	enum arm_spe_pmu_buf_fault_action act;

	if (!perf_get_aux(handle))
		return IRQ_NONE;

	act = arm_spe_pmu_buf_get_fault_act(handle);
	if (act == SPE_PMU_BUF_FAULT_ACT_SPURIOUS)
		return IRQ_NONE;

	/*
	 * Ensure perf callbacks have completed, which may disable the
	 * profiling buffer in response to a TRUNCATION flag.
	 */
	irq_work_run();

	switch (act) {
	case SPE_PMU_BUF_FAULT_ACT_FATAL:
		/*
		 * If a fatal exception occurred then leaving the profiling
		 * buffer enabled is a recipe waiting to happen. Since
		 * fatal faults don't always imply truncation, make sure
		 * that the profiling buffer is disabled explicitly before
		 * clearing the syndrome register.
		 */
		arm_spe_pmu_disable_and_drain_local();
		break;
	case SPE_PMU_BUF_FAULT_ACT_OK:
		/*
		 * We handled the fault (the buffer was full), so resume
		 * profiling as long as we didn't detect truncation.
		 * PMBPTR might be misaligned, but we'll burn that bridge
		 * when we get to it.
		 */
		if (!(handle->aux_flags & PERF_AUX_FLAG_TRUNCATED)) {
			arm_spe_perf_aux_output_begin(handle, event);
			isb();
		}
		break;
	case SPE_PMU_BUF_FAULT_ACT_SPURIOUS:
		/* We've seen you before, but GCC has the memory of a sieve. */
		break;
	}

	/* The buffer pointers are now sane, so resume profiling. */
	write_sysreg_s(0, SYS_PMBSR_EL1);
	return IRQ_HANDLED;
}

static u64 arm_spe_pmsevfr_res0(u16 pmsver)
{
	switch (pmsver) {
	case ID_AA64DFR0_EL1_PMSVer_IMP:
		return PMSEVFR_EL1_RES0_IMP;
	case ID_AA64DFR0_EL1_PMSVer_V1P1:
		return PMSEVFR_EL1_RES0_V1P1;
	case ID_AA64DFR0_EL1_PMSVer_V1P2:
	/* Return the highest version we support in default */
	default:
		return PMSEVFR_EL1_RES0_V1P2;
	}
}

/* Perf callbacks */
static int arm_spe_pmu_event_init(struct perf_event *event)
{
	u64 reg;
	struct perf_event_attr *attr = &event->attr;
	struct arm_spe_pmu *spe_pmu = to_spe_pmu(event->pmu);

	/* This is, of course, deeply driver-specific */
	if (attr->type != event->pmu->type)
		return -ENOENT;

	if (event->cpu >= 0 &&
	    !cpumask_test_cpu(event->cpu, &spe_pmu->supported_cpus))
		return -ENOENT;

	if (arm_spe_event_to_pmsevfr(event) & arm_spe_pmsevfr_res0(spe_pmu->pmsver))
		return -EOPNOTSUPP;

	if (arm_spe_event_to_pmsnevfr(event) & arm_spe_pmsevfr_res0(spe_pmu->pmsver))
		return -EOPNOTSUPP;

	if (attr->exclude_idle)
		return -EOPNOTSUPP;

	/*
	 * Feedback-directed frequency throttling doesn't work when we
	 * have a buffer of samples. We'd need to manually count the
	 * samples in the buffer when it fills up and adjust the event
	 * count to reflect that. Instead, just force the user to specify
	 * a sample period.
	 */
	if (attr->freq)
		return -EINVAL;

	reg = arm_spe_event_to_pmsfcr(event);
	if ((FIELD_GET(PMSFCR_EL1_FE, reg)) &&
	    !(spe_pmu->features & SPE_PMU_FEAT_FILT_EVT))
		return -EOPNOTSUPP;

	if ((FIELD_GET(PMSFCR_EL1_FnE, reg)) &&
	    !(spe_pmu->features & SPE_PMU_FEAT_INV_FILT_EVT))
		return -EOPNOTSUPP;

	if ((FIELD_GET(PMSFCR_EL1_FT, reg)) &&
	    !(spe_pmu->features & SPE_PMU_FEAT_FILT_TYP))
		return -EOPNOTSUPP;

	if ((FIELD_GET(PMSFCR_EL1_FL, reg)) &&
	    !(spe_pmu->features & SPE_PMU_FEAT_FILT_LAT))
		return -EOPNOTSUPP;

	set_spe_event_has_cx(event);
	reg = arm_spe_event_to_pmscr(event);
	if (!perfmon_capable() &&
	    (reg & (PMSCR_EL1_PA | PMSCR_EL1_PCT)))
		return -EACCES;

	return 0;
}

static void arm_spe_pmu_start(struct perf_event *event, int flags)
{
	u64 reg;
	struct arm_spe_pmu *spe_pmu = to_spe_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	struct perf_output_handle *handle = this_cpu_ptr(spe_pmu->handle);

	hwc->state = 0;
	arm_spe_perf_aux_output_begin(handle, event);
	if (hwc->state)
		return;

	reg = arm_spe_event_to_pmsfcr(event);
	write_sysreg_s(reg, SYS_PMSFCR_EL1);

	reg = arm_spe_event_to_pmsevfr(event);
	write_sysreg_s(reg, SYS_PMSEVFR_EL1);

	if (spe_pmu->features & SPE_PMU_FEAT_INV_FILT_EVT) {
		reg = arm_spe_event_to_pmsnevfr(event);
		write_sysreg_s(reg, SYS_PMSNEVFR_EL1);
	}

	reg = arm_spe_event_to_pmslatfr(event);
	write_sysreg_s(reg, SYS_PMSLATFR_EL1);

	if (flags & PERF_EF_RELOAD) {
		reg = arm_spe_event_to_pmsirr(event);
		write_sysreg_s(reg, SYS_PMSIRR_EL1);
		isb();
		reg = local64_read(&hwc->period_left);
		write_sysreg_s(reg, SYS_PMSICR_EL1);
	}

	reg = arm_spe_event_to_pmscr(event);
	isb();
	write_sysreg_s(reg, SYS_PMSCR_EL1);
}

static void arm_spe_pmu_stop(struct perf_event *event, int flags)
{
	struct arm_spe_pmu *spe_pmu = to_spe_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	struct perf_output_handle *handle = this_cpu_ptr(spe_pmu->handle);

	/* If we're already stopped, then nothing to do */
	if (hwc->state & PERF_HES_STOPPED)
		return;

	/* Stop all trace generation */
	arm_spe_pmu_disable_and_drain_local();

	if (flags & PERF_EF_UPDATE) {
		/*
		 * If there's a fault pending then ensure we contain it
		 * to this buffer, since we might be on the context-switch
		 * path.
		 */
		if (perf_get_aux(handle)) {
			enum arm_spe_pmu_buf_fault_action act;

			act = arm_spe_pmu_buf_get_fault_act(handle);
			if (act == SPE_PMU_BUF_FAULT_ACT_SPURIOUS)
				arm_spe_perf_aux_output_end(handle);
			else
				write_sysreg_s(0, SYS_PMBSR_EL1);
		}

		/*
		 * This may also contain ECOUNT, but nobody else should
		 * be looking at period_left, since we forbid frequency
		 * based sampling.
		 */
		local64_set(&hwc->period_left, read_sysreg_s(SYS_PMSICR_EL1));
		hwc->state |= PERF_HES_UPTODATE;
	}

	hwc->state |= PERF_HES_STOPPED;
}

static int arm_spe_pmu_add(struct perf_event *event, int flags)
{
	int ret = 0;
	struct arm_spe_pmu *spe_pmu = to_spe_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int cpu = event->cpu == -1 ? smp_processor_id() : event->cpu;

	if (!cpumask_test_cpu(cpu, &spe_pmu->supported_cpus))
		return -ENOENT;

	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;

	if (flags & PERF_EF_START) {
		arm_spe_pmu_start(event, PERF_EF_RELOAD);
		if (hwc->state & PERF_HES_STOPPED)
			ret = -EINVAL;
	}

	return ret;
}

static void arm_spe_pmu_del(struct perf_event *event, int flags)
{
	arm_spe_pmu_stop(event, PERF_EF_UPDATE);
}

static void arm_spe_pmu_read(struct perf_event *event)
{
}

static void *arm_spe_pmu_setup_aux(struct perf_event *event, void **pages,
				   int nr_pages, bool snapshot)
{
	int i, cpu = event->cpu;
	struct page **pglist;
	struct arm_spe_pmu_buf *buf;

	/* We need at least two pages for this to work. */
	if (nr_pages < 2)
		return NULL;

	/*
	 * We require an even number of pages for snapshot mode, so that
	 * we can effectively treat the buffer as consisting of two equal
	 * parts and give userspace a fighting chance of getting some
	 * useful data out of it.
	 */
	if (snapshot && (nr_pages & 1))
		return NULL;

	if (cpu == -1)
		cpu = raw_smp_processor_id();

	buf = kzalloc_node(sizeof(*buf), GFP_KERNEL, cpu_to_node(cpu));
	if (!buf)
		return NULL;

	pglist = kcalloc(nr_pages, sizeof(*pglist), GFP_KERNEL);
	if (!pglist)
		goto out_free_buf;

	for (i = 0; i < nr_pages; ++i)
		pglist[i] = virt_to_page(pages[i]);

	buf->base = vmap(pglist, nr_pages, VM_MAP, PAGE_KERNEL);
	if (!buf->base)
		goto out_free_pglist;

	buf->nr_pages	= nr_pages;
	buf->snapshot	= snapshot;

	kfree(pglist);
	return buf;

out_free_pglist:
	kfree(pglist);
out_free_buf:
	kfree(buf);
	return NULL;
}

static void arm_spe_pmu_free_aux(void *aux)
{
	struct arm_spe_pmu_buf *buf = aux;

	vunmap(buf->base);
	kfree(buf);
}

/* Initialisation and teardown functions */
static int arm_spe_pmu_perf_init(struct arm_spe_pmu *spe_pmu)
{
	static atomic_t pmu_idx = ATOMIC_INIT(-1);

	int idx;
	char *name;
	struct device *dev = &spe_pmu->pdev->dev;

	spe_pmu->pmu = (struct pmu) {
		.module = THIS_MODULE,
		.capabilities	= PERF_PMU_CAP_EXCLUSIVE | PERF_PMU_CAP_ITRACE,
		.attr_groups	= arm_spe_pmu_attr_groups,
		/*
		 * We hitch a ride on the software context here, so that
		 * we can support per-task profiling (which is not possible
		 * with the invalid context as it doesn't get sched callbacks).
		 * This requires that userspace either uses a dummy event for
		 * perf_event_open, since the aux buffer is not setup until
		 * a subsequent mmap, or creates the profiling event in a
		 * disabled state and explicitly PERF_EVENT_IOC_ENABLEs it
		 * once the buffer has been created.
		 */
		.task_ctx_nr	= perf_sw_context,
		.event_init	= arm_spe_pmu_event_init,
		.add		= arm_spe_pmu_add,
		.del		= arm_spe_pmu_del,
		.start		= arm_spe_pmu_start,
		.stop		= arm_spe_pmu_stop,
		.read		= arm_spe_pmu_read,
		.setup_aux	= arm_spe_pmu_setup_aux,
		.free_aux	= arm_spe_pmu_free_aux,
	};

	idx = atomic_inc_return(&pmu_idx);
	name = devm_kasprintf(dev, GFP_KERNEL, "%s_%d", PMUNAME, idx);
	if (!name) {
		dev_err(dev, "failed to allocate name for pmu %d\n", idx);
		return -ENOMEM;
	}

	return perf_pmu_register(&spe_pmu->pmu, name, -1);
}

static void arm_spe_pmu_perf_destroy(struct arm_spe_pmu *spe_pmu)
{
	perf_pmu_unregister(&spe_pmu->pmu);
}

static void __arm_spe_pmu_dev_probe(void *info)
{
	int fld;
	u64 reg;
	struct arm_spe_pmu *spe_pmu = info;
	struct device *dev = &spe_pmu->pdev->dev;

	fld = cpuid_feature_extract_unsigned_field(read_cpuid(ID_AA64DFR0_EL1),
						   ID_AA64DFR0_EL1_PMSVer_SHIFT);
	if (!fld) {
		dev_err(dev,
			"unsupported ID_AA64DFR0_EL1.PMSVer [%d] on CPU %d\n",
			fld, smp_processor_id());
		return;
	}
	spe_pmu->pmsver = (u16)fld;

	/* Read PMBIDR first to determine whether or not we have access */
	reg = read_sysreg_s(SYS_PMBIDR_EL1);
	if (FIELD_GET(PMBIDR_EL1_P, reg)) {
		dev_err(dev,
			"profiling buffer owned by higher exception level\n");
		return;
	}

	/* Minimum alignment. If it's out-of-range, then fail the probe */
	fld = FIELD_GET(PMBIDR_EL1_ALIGN, reg);
	spe_pmu->align = 1 << fld;
	if (spe_pmu->align > SZ_2K) {
		dev_err(dev, "unsupported PMBIDR.Align [%d] on CPU %d\n",
			fld, smp_processor_id());
		return;
	}

	/* It's now safe to read PMSIDR and figure out what we've got */
	reg = read_sysreg_s(SYS_PMSIDR_EL1);
	if (FIELD_GET(PMSIDR_EL1_FE, reg))
		spe_pmu->features |= SPE_PMU_FEAT_FILT_EVT;

	if (FIELD_GET(PMSIDR_EL1_FnE, reg))
		spe_pmu->features |= SPE_PMU_FEAT_INV_FILT_EVT;

	if (FIELD_GET(PMSIDR_EL1_FT, reg))
		spe_pmu->features |= SPE_PMU_FEAT_FILT_TYP;

	if (FIELD_GET(PMSIDR_EL1_FL, reg))
		spe_pmu->features |= SPE_PMU_FEAT_FILT_LAT;

	if (FIELD_GET(PMSIDR_EL1_ARCHINST, reg))
		spe_pmu->features |= SPE_PMU_FEAT_ARCH_INST;

	if (FIELD_GET(PMSIDR_EL1_LDS, reg))
		spe_pmu->features |= SPE_PMU_FEAT_LDS;

	if (FIELD_GET(PMSIDR_EL1_ERND, reg))
		spe_pmu->features |= SPE_PMU_FEAT_ERND;

	/* This field has a spaced out encoding, so just use a look-up */
	fld = FIELD_GET(PMSIDR_EL1_INTERVAL, reg);
	switch (fld) {
	case PMSIDR_EL1_INTERVAL_256:
		spe_pmu->min_period = 256;
		break;
	case PMSIDR_EL1_INTERVAL_512:
		spe_pmu->min_period = 512;
		break;
	case PMSIDR_EL1_INTERVAL_768:
		spe_pmu->min_period = 768;
		break;
	case PMSIDR_EL1_INTERVAL_1024:
		spe_pmu->min_period = 1024;
		break;
	case PMSIDR_EL1_INTERVAL_1536:
		spe_pmu->min_period = 1536;
		break;
	case PMSIDR_EL1_INTERVAL_2048:
		spe_pmu->min_period = 2048;
		break;
	case PMSIDR_EL1_INTERVAL_3072:
		spe_pmu->min_period = 3072;
		break;
	default:
		dev_warn(dev, "unknown PMSIDR_EL1.Interval [%d]; assuming 8\n",
			 fld);
		fallthrough;
	case PMSIDR_EL1_INTERVAL_4096:
		spe_pmu->min_period = 4096;
	}

	/* Maximum record size. If it's out-of-range, then fail the probe */
	fld = FIELD_GET(PMSIDR_EL1_MAXSIZE, reg);
	spe_pmu->max_record_sz = 1 << fld;
	if (spe_pmu->max_record_sz > SZ_2K || spe_pmu->max_record_sz < 16) {
		dev_err(dev, "unsupported PMSIDR_EL1.MaxSize [%d] on CPU %d\n",
			fld, smp_processor_id());
		return;
	}

	fld = FIELD_GET(PMSIDR_EL1_COUNTSIZE, reg);
	switch (fld) {
	default:
		dev_warn(dev, "unknown PMSIDR_EL1.CountSize [%d]; assuming 2\n",
			 fld);
		fallthrough;
	case PMSIDR_EL1_COUNTSIZE_12_BIT_SAT:
		spe_pmu->counter_sz = 12;
		break;
	case PMSIDR_EL1_COUNTSIZE_16_BIT_SAT:
		spe_pmu->counter_sz = 16;
	}

	dev_info(dev,
		 "probed SPEv1.%d for CPUs %*pbl [max_record_sz %u, align %u, features 0x%llx]\n",
		 spe_pmu->pmsver - 1, cpumask_pr_args(&spe_pmu->supported_cpus),
		 spe_pmu->max_record_sz, spe_pmu->align, spe_pmu->features);

	spe_pmu->features |= SPE_PMU_FEAT_DEV_PROBED;
}

static void __arm_spe_pmu_reset_local(void)
{
	/*
	 * This is probably overkill, as we have no idea where we're
	 * draining any buffered data to...
	 */
	arm_spe_pmu_disable_and_drain_local();

	/* Reset the buffer base pointer */
	write_sysreg_s(0, SYS_PMBPTR_EL1);
	isb();

	/* Clear any pending management interrupts */
	write_sysreg_s(0, SYS_PMBSR_EL1);
	isb();
}

static void __arm_spe_pmu_setup_one(void *info)
{
	struct arm_spe_pmu *spe_pmu = info;

	__arm_spe_pmu_reset_local();
	enable_percpu_irq(spe_pmu->irq, IRQ_TYPE_NONE);
}

static void __arm_spe_pmu_stop_one(void *info)
{
	struct arm_spe_pmu *spe_pmu = info;

	disable_percpu_irq(spe_pmu->irq);
	__arm_spe_pmu_reset_local();
}

static int arm_spe_pmu_cpu_startup(unsigned int cpu, struct hlist_node *node)
{
	struct arm_spe_pmu *spe_pmu;

	spe_pmu = hlist_entry_safe(node, struct arm_spe_pmu, hotplug_node);
	if (!cpumask_test_cpu(cpu, &spe_pmu->supported_cpus))
		return 0;

	__arm_spe_pmu_setup_one(spe_pmu);
	return 0;
}

static int arm_spe_pmu_cpu_teardown(unsigned int cpu, struct hlist_node *node)
{
	struct arm_spe_pmu *spe_pmu;

	spe_pmu = hlist_entry_safe(node, struct arm_spe_pmu, hotplug_node);
	if (!cpumask_test_cpu(cpu, &spe_pmu->supported_cpus))
		return 0;

	__arm_spe_pmu_stop_one(spe_pmu);
	return 0;
}

static int arm_spe_pmu_dev_init(struct arm_spe_pmu *spe_pmu)
{
	int ret;
	cpumask_t *mask = &spe_pmu->supported_cpus;

	/* Make sure we probe the hardware on a relevant CPU */
	ret = smp_call_function_any(mask,  __arm_spe_pmu_dev_probe, spe_pmu, 1);
	if (ret || !(spe_pmu->features & SPE_PMU_FEAT_DEV_PROBED))
		return -ENXIO;

	/* Request our PPIs (note that the IRQ is still disabled) */
	ret = request_percpu_irq(spe_pmu->irq, arm_spe_pmu_irq_handler, DRVNAME,
				 spe_pmu->handle);
	if (ret)
		return ret;

	/*
	 * Register our hotplug notifier now so we don't miss any events.
	 * This will enable the IRQ for any supported CPUs that are already
	 * up.
	 */
	ret = cpuhp_state_add_instance(arm_spe_pmu_online,
				       &spe_pmu->hotplug_node);
	if (ret)
		free_percpu_irq(spe_pmu->irq, spe_pmu->handle);

	return ret;
}

static void arm_spe_pmu_dev_teardown(struct arm_spe_pmu *spe_pmu)
{
	cpuhp_state_remove_instance(arm_spe_pmu_online, &spe_pmu->hotplug_node);
	free_percpu_irq(spe_pmu->irq, spe_pmu->handle);
}

/* Driver and device probing */
static int arm_spe_pmu_irq_probe(struct arm_spe_pmu *spe_pmu)
{
	struct platform_device *pdev = spe_pmu->pdev;
	int irq = platform_get_irq(pdev, 0);

	if (irq < 0)
		return -ENXIO;

	if (!irq_is_percpu(irq)) {
		dev_err(&pdev->dev, "expected PPI but got SPI (%d)\n", irq);
		return -EINVAL;
	}

	if (irq_get_percpu_devid_partition(irq, &spe_pmu->supported_cpus)) {
		dev_err(&pdev->dev, "failed to get PPI partition (%d)\n", irq);
		return -EINVAL;
	}

	spe_pmu->irq = irq;
	return 0;
}

static const struct of_device_id arm_spe_pmu_of_match[] = {
	{ .compatible = "arm,statistical-profiling-extension-v1", .data = (void *)1 },
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, arm_spe_pmu_of_match);

static const struct platform_device_id arm_spe_match[] = {
	{ ARMV8_SPE_PDEV_NAME, 0},
	{ }
};
MODULE_DEVICE_TABLE(platform, arm_spe_match);

static int arm_spe_pmu_device_probe(struct platform_device *pdev)
{
	int ret;
	struct arm_spe_pmu *spe_pmu;
	struct device *dev = &pdev->dev;

	/*
	 * If kernelspace is unmapped when running at EL0, then the SPE
	 * buffer will fault and prematurely terminate the AUX session.
	 */
	if (arm64_kernel_unmapped_at_el0()) {
		dev_warn_once(dev, "profiling buffer inaccessible. Try passing \"kpti=off\" on the kernel command line\n");
		return -EPERM;
	}

	spe_pmu = devm_kzalloc(dev, sizeof(*spe_pmu), GFP_KERNEL);
	if (!spe_pmu)
		return -ENOMEM;

	spe_pmu->handle = alloc_percpu(typeof(*spe_pmu->handle));
	if (!spe_pmu->handle)
		return -ENOMEM;

	spe_pmu->pdev = pdev;
	platform_set_drvdata(pdev, spe_pmu);

	ret = arm_spe_pmu_irq_probe(spe_pmu);
	if (ret)
		goto out_free_handle;

	ret = arm_spe_pmu_dev_init(spe_pmu);
	if (ret)
		goto out_free_handle;

	ret = arm_spe_pmu_perf_init(spe_pmu);
	if (ret)
		goto out_teardown_dev;

	return 0;

out_teardown_dev:
	arm_spe_pmu_dev_teardown(spe_pmu);
out_free_handle:
	free_percpu(spe_pmu->handle);
	return ret;
}

static int arm_spe_pmu_device_remove(struct platform_device *pdev)
{
	struct arm_spe_pmu *spe_pmu = platform_get_drvdata(pdev);

	arm_spe_pmu_perf_destroy(spe_pmu);
	arm_spe_pmu_dev_teardown(spe_pmu);
	free_percpu(spe_pmu->handle);
	return 0;
}

static struct platform_driver arm_spe_pmu_driver = {
	.id_table = arm_spe_match,
	.driver	= {
		.name		= DRVNAME,
		.of_match_table	= of_match_ptr(arm_spe_pmu_of_match),
		.suppress_bind_attrs = true,
	},
	.probe	= arm_spe_pmu_device_probe,
	.remove	= arm_spe_pmu_device_remove,
};

static int __init arm_spe_pmu_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN, DRVNAME,
				      arm_spe_pmu_cpu_startup,
				      arm_spe_pmu_cpu_teardown);
	if (ret < 0)
		return ret;
	arm_spe_pmu_online = ret;

	ret = platform_driver_register(&arm_spe_pmu_driver);
	if (ret)
		cpuhp_remove_multi_state(arm_spe_pmu_online);

	return ret;
}

static void __exit arm_spe_pmu_exit(void)
{
	platform_driver_unregister(&arm_spe_pmu_driver);
	cpuhp_remove_multi_state(arm_spe_pmu_online);
}

module_init(arm_spe_pmu_init);
module_exit(arm_spe_pmu_exit);

MODULE_DESCRIPTION("Perf driver for the ARMv8.2 Statistical Profiling Extension");
MODULE_AUTHOR("Will Deacon <will.deacon@arm.com>");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Perf interface to expose Dispatch Trace Log counters.
 *
 * Copyright (C) 2024 Kajol Jain, IBM Corporation
 */

#ifdef CONFIG_PPC_SPLPAR
#define pr_fmt(fmt) "vpa_dtl: " fmt

#include <asm/dtl.h>
#include <linux/perf_event.h>
#include <asm/plpar_wrappers.h>
#include <linux/vmalloc.h>

#define EVENT(_name, _code)     enum{_name = _code}

/*
 * Based on Power Architecture Platform Reference(PAPR) documentation,
 * Table 14.14. Per Virtual Processor Area, below Dispatch Trace Log(DTL)
 * Enable Mask used to get corresponding virtual processor dispatch
 * to preempt traces:
 *   DTL_CEDE(0x1): Trace voluntary (OS initiated) virtual
 *   processor waits
 *   DTL_PREEMPT(0x2): Trace time slice preempts
 *   DTL_FAULT(0x4): Trace virtual partition memory page
 faults.
 *   DTL_ALL(0x7): Trace all (DTL_CEDE | DTL_PREEMPT | DTL_FAULT)
 *
 * Event codes based on Dispatch Trace Log Enable Mask.
 */
EVENT(DTL_CEDE,         0x1);
EVENT(DTL_PREEMPT,      0x2);
EVENT(DTL_FAULT,        0x4);
EVENT(DTL_ALL,          0x7);

GENERIC_EVENT_ATTR(dtl_cede, DTL_CEDE);
GENERIC_EVENT_ATTR(dtl_preempt, DTL_PREEMPT);
GENERIC_EVENT_ATTR(dtl_fault, DTL_FAULT);
GENERIC_EVENT_ATTR(dtl_all, DTL_ALL);

PMU_FORMAT_ATTR(event, "config:0-7");

static struct attribute *events_attr[] = {
	GENERIC_EVENT_PTR(DTL_CEDE),
	GENERIC_EVENT_PTR(DTL_PREEMPT),
	GENERIC_EVENT_PTR(DTL_FAULT),
	GENERIC_EVENT_PTR(DTL_ALL),
	NULL
};

static struct attribute_group event_group = {
	.name = "events",
	.attrs = events_attr,
};

static struct attribute *format_attrs[] = {
	&format_attr_event.attr,
	NULL,
};

static const struct attribute_group format_group = {
	.name = "format",
	.attrs = format_attrs,
};

static const struct attribute_group *attr_groups[] = {
	&format_group,
	&event_group,
	NULL,
};

struct vpa_dtl {
	struct dtl_entry	*buf;
	u64			last_idx;
};

struct vpa_pmu_ctx {
	struct perf_output_handle handle;
};

struct vpa_pmu_buf {
	int     nr_pages;
	bool    snapshot;
	u64     *base;
	u64     size;
	u64     head;
};

static DEFINE_PER_CPU(struct vpa_pmu_ctx, vpa_pmu_ctx);
static DEFINE_PER_CPU(struct vpa_dtl, vpa_dtl_cpu);

/* variable to capture reference count for the active dtl threads */
static int dtl_global_refc;
static spinlock_t dtl_global_lock = __SPIN_LOCK_UNLOCKED(dtl_global_lock);

/*
 * Function to dump the dispatch trace log buffer data to the
 * perf data.
 */
static void vpa_dtl_dump_sample_data(struct perf_event *event)
{
	return;
}

/*
 * The VPA Dispatch Trace log counters do not interrupt on overflow.
 * Therefore, the kernel needs to poll the counters to avoid missing
 * an overflow using hrtimer. The timer interval is based on sample_period
 * count provided by user, and minimum interval is 1 millisecond.
 */
static enum hrtimer_restart vpa_dtl_hrtimer_handle(struct hrtimer *hrtimer)
{
	struct perf_event *event;
	u64 period;

	event = container_of(hrtimer, struct perf_event, hw.hrtimer);

	if (event->state != PERF_EVENT_STATE_ACTIVE)
		return HRTIMER_NORESTART;

	vpa_dtl_dump_sample_data(event);
	period = max_t(u64, NSEC_PER_MSEC, event->hw.sample_period);
	hrtimer_forward_now(hrtimer, ns_to_ktime(period));

	return HRTIMER_RESTART;
}

static void vpa_dtl_start_hrtimer(struct perf_event *event)
{
	u64 period;
	struct hw_perf_event *hwc = &event->hw;

	period = max_t(u64, NSEC_PER_MSEC, hwc->sample_period);
	hrtimer_start(&hwc->hrtimer, ns_to_ktime(period), HRTIMER_MODE_REL_PINNED);
}

static void vpa_dtl_stop_hrtimer(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	hrtimer_cancel(&hwc->hrtimer);
}

static void vpa_dtl_reset_global_refc(struct perf_event *event)
{
	spin_lock(&dtl_global_lock);
	dtl_global_refc--;
	if (dtl_global_refc <= 0) {
		dtl_global_refc = 0;
		up_write(&dtl_access_lock);
	}
	spin_unlock(&dtl_global_lock);
}

static int vpa_dtl_mem_alloc(int cpu)
{
	struct vpa_dtl *dtl = &per_cpu(vpa_dtl_cpu, cpu);
	struct dtl_entry *buf = NULL;

	/* Check for dispatch trace log buffer cache */
	if (!dtl_cache)
		return -ENOMEM;

	buf = kmem_cache_alloc_node(dtl_cache, GFP_KERNEL | GFP_ATOMIC, cpu_to_node(cpu));
	if (!buf) {
		pr_warn("buffer allocation failed for cpu %d\n", cpu);
		return -ENOMEM;
	}
	dtl->buf = buf;
	return 0;
}

static int vpa_dtl_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	/* test the event attr type for PMU enumeration */
	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	if (!perfmon_capable())
		return -EACCES;

	/* Return if this is a counting event */
	if (!is_sampling_event(event))
		return -EOPNOTSUPP;

	/* no branch sampling */
	if (has_branch_stack(event))
		return -EOPNOTSUPP;

	/* Invalid eventcode */
	switch (event->attr.config) {
	case DTL_LOG_CEDE:
	case DTL_LOG_PREEMPT:
	case DTL_LOG_FAULT:
	case DTL_LOG_ALL:
		break;
	default:
		return -EINVAL;
	}

	spin_lock(&dtl_global_lock);

	/*
	 * To ensure there are no other conflicting dtl users
	 * (example: /proc/powerpc/vcpudispatch_stats or debugfs dtl),
	 * below code try to take the dtl_access_lock.
	 * The dtl_access_lock is a rwlock defined in dtl.h, which is used
	 * to unsure there is no conflicting dtl users.
	 * Based on below code, vpa_dtl pmu tries to take write access lock
	 * and also checks for dtl_global_refc, to make sure that the
	 * dtl_access_lock is taken by vpa_dtl pmu interface.
	 */
	if (dtl_global_refc == 0 && !down_write_trylock(&dtl_access_lock)) {
		spin_unlock(&dtl_global_lock);
		return -EBUSY;
	}

	/* Allocate dtl buffer memory */
	if (vpa_dtl_mem_alloc(event->cpu)) {
		spin_unlock(&dtl_global_lock);
		return -ENOMEM;
	}

	/*
	 * Increment the number of active vpa_dtl pmu threads. The
	 * dtl_global_refc is used to keep count of cpu threads that
	 * currently capturing dtl data using vpa_dtl pmu interface.
	 */
	dtl_global_refc++;

	spin_unlock(&dtl_global_lock);

	hrtimer_setup(&hwc->hrtimer, vpa_dtl_hrtimer_handle, CLOCK_MONOTONIC, HRTIMER_MODE_REL);

	/*
	 * Since hrtimers have a fixed rate, we can do a static freq->period
	 * mapping and avoid the whole period adjust feedback stuff.
	 */
	if (event->attr.freq) {
		long freq = event->attr.sample_freq;

		event->attr.sample_period = NSEC_PER_SEC / freq;
		hwc->sample_period = event->attr.sample_period;
		local64_set(&hwc->period_left, hwc->sample_period);
		hwc->last_period = hwc->sample_period;
		event->attr.freq = 0;
	}

	event->destroy = vpa_dtl_reset_global_refc;
	return 0;
}

static int vpa_dtl_event_add(struct perf_event *event, int flags)
{
	int ret, hwcpu;
	unsigned long addr;
	struct vpa_dtl *dtl = &per_cpu(vpa_dtl_cpu, event->cpu);

	/*
	 * Register our dtl buffer with the hypervisor. The
	 * HV expects the buffer size to be passed in the second
	 * word of the buffer. Refer section '14.11.3.2. H_REGISTER_VPA'
	 * from PAPR for more information.
	 */
	((u32 *)dtl->buf)[1] = cpu_to_be32(DISPATCH_LOG_BYTES);
	dtl->last_idx = 0;

	hwcpu = get_hard_smp_processor_id(event->cpu);
	addr = __pa(dtl->buf);

	ret = register_dtl(hwcpu, addr);
	if (ret) {
		pr_warn("DTL registration for cpu %d (hw %d) failed with %d\n",
			event->cpu, hwcpu, ret);
		return ret;
	}

	/* set our initial buffer indices */
	lppaca_of(event->cpu).dtl_idx = 0;

	/*
	 * Ensure that our updates to the lppaca fields have
	 * occurred before we actually enable the logging
	 */
	smp_wmb();

	/* enable event logging */
	lppaca_of(event->cpu).dtl_enable_mask = event->attr.config;

	vpa_dtl_start_hrtimer(event);

	return 0;
}

static void vpa_dtl_event_del(struct perf_event *event, int flags)
{
	int hwcpu = get_hard_smp_processor_id(event->cpu);
	struct vpa_dtl *dtl = &per_cpu(vpa_dtl_cpu, event->cpu);

	vpa_dtl_stop_hrtimer(event);
	unregister_dtl(hwcpu);
	kmem_cache_free(dtl_cache, dtl->buf);
	dtl->buf = NULL;
	lppaca_of(event->cpu).dtl_enable_mask = 0x0;
}

/*
 * This function definition is empty as vpa_dtl_dump_sample_data
 * is used to parse and dump the dispatch trace log data,
 * to perf data.
 */
static void vpa_dtl_event_read(struct perf_event *event)
{
}

/*
 * Set up pmu-private data structures for an AUX area
 * **pages contains the aux buffer allocated for this event
 * for the corresponding cpu. rb_alloc_aux uses "alloc_pages_node"
 * and returns pointer to each page address. Map these pages to
 * contiguous space using vmap and use that as base address.
 *
 * The aux private data structure ie, "struct vpa_pmu_buf" mainly
 * saves
 * - buf->base: aux buffer base address
 * - buf->head: offset from base address where data will be written to.
 * - buf->size: Size of allocated memory
 */
static void *vpa_dtl_setup_aux(struct perf_event *event, void **pages,
		int nr_pages, bool snapshot)
{
	int i, cpu = event->cpu;
	struct vpa_pmu_buf *buf __free(kfree) = NULL;
	struct page **pglist __free(kfree) = NULL;

	/* We need at least one page for this to work. */
	if (!nr_pages)
		return NULL;

	if (cpu == -1)
		cpu = raw_smp_processor_id();

	buf = kzalloc_node(sizeof(*buf), GFP_KERNEL, cpu_to_node(cpu));
	if (!buf)
		return NULL;

	pglist = kcalloc(nr_pages, sizeof(*pglist), GFP_KERNEL);
	if (!pglist)
		return NULL;

	for (i = 0; i < nr_pages; ++i)
		pglist[i] = virt_to_page(pages[i]);

	buf->base = vmap(pglist, nr_pages, VM_MAP, PAGE_KERNEL);
	if (!buf->base)
		return NULL;

	buf->nr_pages = nr_pages;
	buf->snapshot = false;

	buf->size = nr_pages << PAGE_SHIFT;
	buf->head = 0;
	return no_free_ptr(buf);
}

/*
 * free pmu-private AUX data structures
 */
static void vpa_dtl_free_aux(void *aux)
{
	struct vpa_pmu_buf *buf = aux;

	vunmap(buf->base);
	kfree(buf);
}

static struct pmu vpa_dtl_pmu = {
	.task_ctx_nr = perf_invalid_context,

	.name = "vpa_dtl",
	.attr_groups = attr_groups,
	.event_init  = vpa_dtl_event_init,
	.add         = vpa_dtl_event_add,
	.del         = vpa_dtl_event_del,
	.read        = vpa_dtl_event_read,
	.setup_aux   = vpa_dtl_setup_aux,
	.free_aux    = vpa_dtl_free_aux,
	.capabilities = PERF_PMU_CAP_NO_EXCLUDE | PERF_PMU_CAP_EXCLUSIVE,
};

static int vpa_dtl_init(void)
{
	int r;

	if (!firmware_has_feature(FW_FEATURE_SPLPAR)) {
		pr_debug("not a shared virtualized system, not enabling\n");
		return -ENODEV;
	}

	/* This driver is intended only for L1 host. */
	if (is_kvm_guest()) {
		pr_debug("Only supported for L1 host system\n");
		return -ENODEV;
	}

	r = perf_pmu_register(&vpa_dtl_pmu, vpa_dtl_pmu.name, -1);
	if (r)
		return r;

	return 0;
}

device_initcall(vpa_dtl_init);
#endif //CONFIG_PPC_SPLPAR

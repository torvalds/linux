/*
 * Performance event support framework for SuperH hardware counters.
 *
 *  Copyright (C) 2009  Paul Mundt
 *
 * Heavily based on the x86 and PowerPC implementations.
 *
 * x86:
 *  Copyright (C) 2008 Thomas Gleixner <tglx@linutronix.de>
 *  Copyright (C) 2008-2009 Red Hat, Inc., Ingo Molnar
 *  Copyright (C) 2009 Jaswinder Singh Rajput
 *  Copyright (C) 2009 Advanced Micro Devices, Inc., Robert Richter
 *  Copyright (C) 2008-2009 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 *  Copyright (C) 2009 Intel Corporation, <markus.t.metzger@intel.com>
 *
 * ppc:
 *  Copyright 2008-2009 Paul Mackerras, IBM Corporation.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/perf_event.h>
#include <linux/export.h>
#include <asm/processor.h>

struct cpu_hw_events {
	struct perf_event	*events[MAX_HWEVENTS];
	unsigned long		used_mask[BITS_TO_LONGS(MAX_HWEVENTS)];
	unsigned long		active_mask[BITS_TO_LONGS(MAX_HWEVENTS)];
};

DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events);

static struct sh_pmu *sh_pmu __read_mostly;

/* Number of perf_events counting hardware events */
static atomic_t num_events;
/* Used to avoid races in calling reserve/release_pmc_hardware */
static DEFINE_MUTEX(pmc_reserve_mutex);

/*
 * Stub these out for now, do something more profound later.
 */
int reserve_pmc_hardware(void)
{
	return 0;
}

void release_pmc_hardware(void)
{
}

static inline int sh_pmu_initialized(void)
{
	return !!sh_pmu;
}

const char *perf_pmu_name(void)
{
	if (!sh_pmu)
		return NULL;

	return sh_pmu->name;
}
EXPORT_SYMBOL_GPL(perf_pmu_name);

int perf_num_counters(void)
{
	if (!sh_pmu)
		return 0;

	return sh_pmu->num_events;
}
EXPORT_SYMBOL_GPL(perf_num_counters);

/*
 * Release the PMU if this is the last perf_event.
 */
static void hw_perf_event_destroy(struct perf_event *event)
{
	if (!atomic_add_unless(&num_events, -1, 1)) {
		mutex_lock(&pmc_reserve_mutex);
		if (atomic_dec_return(&num_events) == 0)
			release_pmc_hardware();
		mutex_unlock(&pmc_reserve_mutex);
	}
}

static int hw_perf_cache_event(int config, int *evp)
{
	unsigned long type, op, result;
	int ev;

	if (!sh_pmu->cache_events)
		return -EINVAL;

	/* unpack config */
	type = config & 0xff;
	op = (config >> 8) & 0xff;
	result = (config >> 16) & 0xff;

	if (type >= PERF_COUNT_HW_CACHE_MAX ||
	    op >= PERF_COUNT_HW_CACHE_OP_MAX ||
	    result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return -EINVAL;

	ev = (*sh_pmu->cache_events)[type][op][result];
	if (ev == 0)
		return -EOPNOTSUPP;
	if (ev == -1)
		return -EINVAL;
	*evp = ev;
	return 0;
}

static int __hw_perf_event_init(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	struct hw_perf_event *hwc = &event->hw;
	int config = -1;
	int err;

	if (!sh_pmu_initialized())
		return -ENODEV;

	/*
	 * All of the on-chip counters are "limited", in that they have
	 * no interrupts, and are therefore unable to do sampling without
	 * further work and timer assistance.
	 */
	if (hwc->sample_period)
		return -EINVAL;

	/*
	 * See if we need to reserve the counter.
	 *
	 * If no events are currently in use, then we have to take a
	 * mutex to ensure that we don't race with another task doing
	 * reserve_pmc_hardware or release_pmc_hardware.
	 */
	err = 0;
	if (!atomic_inc_not_zero(&num_events)) {
		mutex_lock(&pmc_reserve_mutex);
		if (atomic_read(&num_events) == 0 &&
		    reserve_pmc_hardware())
			err = -EBUSY;
		else
			atomic_inc(&num_events);
		mutex_unlock(&pmc_reserve_mutex);
	}

	if (err)
		return err;

	event->destroy = hw_perf_event_destroy;

	switch (attr->type) {
	case PERF_TYPE_RAW:
		config = attr->config & sh_pmu->raw_event_mask;
		break;
	case PERF_TYPE_HW_CACHE:
		err = hw_perf_cache_event(attr->config, &config);
		if (err)
			return err;
		break;
	case PERF_TYPE_HARDWARE:
		if (attr->config >= sh_pmu->max_events)
			return -EINVAL;

		config = sh_pmu->event_map(attr->config);
		break;
	}

	if (config == -1)
		return -EINVAL;

	hwc->config |= config;

	return 0;
}

static void sh_perf_event_update(struct perf_event *event,
				   struct hw_perf_event *hwc, int idx)
{
	u64 prev_raw_count, new_raw_count;
	s64 delta;
	int shift = 0;

	/*
	 * Depending on the counter configuration, they may or may not
	 * be chained, in which case the previous counter value can be
	 * updated underneath us if the lower-half overflows.
	 *
	 * Our tactic to handle this is to first atomically read and
	 * exchange a new raw count - then add that new-prev delta
	 * count to the generic counter atomically.
	 *
	 * As there is no interrupt associated with the overflow events,
	 * this is the simplest approach for maintaining consistency.
	 */
again:
	prev_raw_count = local64_read(&hwc->prev_count);
	new_raw_count = sh_pmu->read(idx);

	if (local64_cmpxchg(&hwc->prev_count, prev_raw_count,
			     new_raw_count) != prev_raw_count)
		goto again;

	/*
	 * Now we have the new raw value and have updated the prev
	 * timestamp already. We can now calculate the elapsed delta
	 * (counter-)time and add that to the generic counter.
	 *
	 * Careful, not all hw sign-extends above the physical width
	 * of the count.
	 */
	delta = (new_raw_count << shift) - (prev_raw_count << shift);
	delta >>= shift;

	local64_add(delta, &event->count);
}

static void sh_pmu_stop(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (!(event->hw.state & PERF_HES_STOPPED)) {
		sh_pmu->disable(hwc, idx);
		cpuc->events[idx] = NULL;
		event->hw.state |= PERF_HES_STOPPED;
	}

	if ((flags & PERF_EF_UPDATE) && !(event->hw.state & PERF_HES_UPTODATE)) {
		sh_perf_event_update(event, &event->hw, idx);
		event->hw.state |= PERF_HES_UPTODATE;
	}
}

static void sh_pmu_start(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (WARN_ON_ONCE(idx == -1))
		return;

	if (flags & PERF_EF_RELOAD)
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));

	cpuc->events[idx] = event;
	event->hw.state = 0;
	sh_pmu->enable(hwc, idx);
}

static void sh_pmu_del(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	sh_pmu_stop(event, PERF_EF_UPDATE);
	__clear_bit(event->hw.idx, cpuc->used_mask);

	perf_event_update_userpage(event);
}

static int sh_pmu_add(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;
	int ret = -EAGAIN;

	perf_pmu_disable(event->pmu);

	if (__test_and_set_bit(idx, cpuc->used_mask)) {
		idx = find_first_zero_bit(cpuc->used_mask, sh_pmu->num_events);
		if (idx == sh_pmu->num_events)
			goto out;

		__set_bit(idx, cpuc->used_mask);
		hwc->idx = idx;
	}

	sh_pmu->disable(hwc, idx);

	event->hw.state = PERF_HES_UPTODATE | PERF_HES_STOPPED;
	if (flags & PERF_EF_START)
		sh_pmu_start(event, PERF_EF_RELOAD);

	perf_event_update_userpage(event);
	ret = 0;
out:
	perf_pmu_enable(event->pmu);
	return ret;
}

static void sh_pmu_read(struct perf_event *event)
{
	sh_perf_event_update(event, &event->hw, event->hw.idx);
}

static int sh_pmu_event_init(struct perf_event *event)
{
	int err;

	/* does not support taken branch sampling */
	if (has_branch_stack(event))
		return -EOPNOTSUPP;

	switch (event->attr.type) {
	case PERF_TYPE_RAW:
	case PERF_TYPE_HW_CACHE:
	case PERF_TYPE_HARDWARE:
		err = __hw_perf_event_init(event);
		break;

	default:
		return -ENOENT;
	}

	if (unlikely(err)) {
		if (event->destroy)
			event->destroy(event);
	}

	return err;
}

static void sh_pmu_enable(struct pmu *pmu)
{
	if (!sh_pmu_initialized())
		return;

	sh_pmu->enable_all();
}

static void sh_pmu_disable(struct pmu *pmu)
{
	if (!sh_pmu_initialized())
		return;

	sh_pmu->disable_all();
}

static struct pmu pmu = {
	.pmu_enable	= sh_pmu_enable,
	.pmu_disable	= sh_pmu_disable,
	.event_init	= sh_pmu_event_init,
	.add		= sh_pmu_add,
	.del		= sh_pmu_del,
	.start		= sh_pmu_start,
	.stop		= sh_pmu_stop,
	.read		= sh_pmu_read,
};

static void sh_pmu_setup(int cpu)
{
	struct cpu_hw_events *cpuhw = &per_cpu(cpu_hw_events, cpu);

	memset(cpuhw, 0, sizeof(struct cpu_hw_events));
}

static int
sh_pmu_notifier(struct notifier_block *self, unsigned long action, void *hcpu)
{
	unsigned int cpu = (long)hcpu;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_UP_PREPARE:
		sh_pmu_setup(cpu);
		break;

	default:
		break;
	}

	return NOTIFY_OK;
}

int register_sh_pmu(struct sh_pmu *_pmu)
{
	if (sh_pmu)
		return -EBUSY;
	sh_pmu = _pmu;

	pr_info("Performance Events: %s support registered\n", _pmu->name);

	WARN_ON(_pmu->num_events > MAX_HWEVENTS);

	perf_pmu_register(&pmu, "cpu", PERF_TYPE_RAW);
	perf_cpu_notifier(sh_pmu_notifier);
	return 0;
}

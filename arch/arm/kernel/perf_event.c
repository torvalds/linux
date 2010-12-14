#undef DEBUG

/*
 * ARM performance counter support.
 *
 * Copyright (C) 2009 picoChip Designs, Ltd., Jamie Iles
 *
 * ARMv7 support: Jean Pihet <jpihet@mvista.com>
 * 2010 (c) MontaVista Software, LLC.
 *
 * This code is based on the sparc64 perf event code, which is in turn based
 * on the x86 code. Callchain code is based on the ARM OProfile backtrace
 * code.
 */
#define pr_fmt(fmt) "hw perfevents: " fmt

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>

#include <asm/cputype.h>
#include <asm/irq.h>
#include <asm/irq_regs.h>
#include <asm/pmu.h>
#include <asm/stacktrace.h>

static struct platform_device *pmu_device;

/*
 * Hardware lock to serialize accesses to PMU registers. Needed for the
 * read/modify/write sequences.
 */
DEFINE_SPINLOCK(pmu_lock);

/*
 * ARMv6 supports a maximum of 3 events, starting from index 1. If we add
 * another platform that supports more, we need to increase this to be the
 * largest of all platforms.
 *
 * ARMv7 supports up to 32 events:
 *  cycle counter CCNT + 31 events counters CNT0..30.
 *  Cortex-A8 has 1+4 counters, Cortex-A9 has 1+6 counters.
 */
#define ARMPMU_MAX_HWEVENTS		33

/* The events for a given CPU. */
struct cpu_hw_events {
	/*
	 * The events that are active on the CPU for the given index. Index 0
	 * is reserved.
	 */
	struct perf_event	*events[ARMPMU_MAX_HWEVENTS];

	/*
	 * A 1 bit for an index indicates that the counter is being used for
	 * an event. A 0 means that the counter can be used.
	 */
	unsigned long		used_mask[BITS_TO_LONGS(ARMPMU_MAX_HWEVENTS)];

	/*
	 * A 1 bit for an index indicates that the counter is actively being
	 * used.
	 */
	unsigned long		active_mask[BITS_TO_LONGS(ARMPMU_MAX_HWEVENTS)];
};
DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events);

/* PMU names. */
static const char *arm_pmu_names[] = {
	[ARM_PERF_PMU_ID_XSCALE1] = "xscale1",
	[ARM_PERF_PMU_ID_XSCALE2] = "xscale2",
	[ARM_PERF_PMU_ID_V6]	  = "v6",
	[ARM_PERF_PMU_ID_V6MP]	  = "v6mpcore",
	[ARM_PERF_PMU_ID_CA8]	  = "ARMv7 Cortex-A8",
	[ARM_PERF_PMU_ID_CA9]	  = "ARMv7 Cortex-A9",
};

struct arm_pmu {
	enum arm_perf_pmu_ids id;
	irqreturn_t	(*handle_irq)(int irq_num, void *dev);
	void		(*enable)(struct hw_perf_event *evt, int idx);
	void		(*disable)(struct hw_perf_event *evt, int idx);
	int		(*event_map)(int evt);
	u64		(*raw_event)(u64);
	int		(*get_event_idx)(struct cpu_hw_events *cpuc,
					 struct hw_perf_event *hwc);
	u32		(*read_counter)(int idx);
	void		(*write_counter)(int idx, u32 val);
	void		(*start)(void);
	void		(*stop)(void);
	int		num_events;
	u64		max_period;
};

/* Set at runtime when we know what CPU type we are. */
static const struct arm_pmu *armpmu;

enum arm_perf_pmu_ids
armpmu_get_pmu_id(void)
{
	int id = -ENODEV;

	if (armpmu != NULL)
		id = armpmu->id;

	return id;
}
EXPORT_SYMBOL_GPL(armpmu_get_pmu_id);

int
armpmu_get_max_events(void)
{
	int max_events = 0;

	if (armpmu != NULL)
		max_events = armpmu->num_events;

	return max_events;
}
EXPORT_SYMBOL_GPL(armpmu_get_max_events);

int perf_num_counters(void)
{
	return armpmu_get_max_events();
}
EXPORT_SYMBOL_GPL(perf_num_counters);

#define HW_OP_UNSUPPORTED		0xFFFF

#define C(_x) \
	PERF_COUNT_HW_CACHE_##_x

#define CACHE_OP_UNSUPPORTED		0xFFFF

static unsigned armpmu_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
				     [PERF_COUNT_HW_CACHE_OP_MAX]
				     [PERF_COUNT_HW_CACHE_RESULT_MAX];

static int
armpmu_map_cache_event(u64 config)
{
	unsigned int cache_type, cache_op, cache_result, ret;

	cache_type = (config >>  0) & 0xff;
	if (cache_type >= PERF_COUNT_HW_CACHE_MAX)
		return -EINVAL;

	cache_op = (config >>  8) & 0xff;
	if (cache_op >= PERF_COUNT_HW_CACHE_OP_MAX)
		return -EINVAL;

	cache_result = (config >> 16) & 0xff;
	if (cache_result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return -EINVAL;

	ret = (int)armpmu_perf_cache_map[cache_type][cache_op][cache_result];

	if (ret == CACHE_OP_UNSUPPORTED)
		return -ENOENT;

	return ret;
}

static int
armpmu_event_set_period(struct perf_event *event,
			struct hw_perf_event *hwc,
			int idx)
{
	s64 left = local64_read(&hwc->period_left);
	s64 period = hwc->sample_period;
	int ret = 0;

	if (unlikely(left <= -period)) {
		left = period;
		local64_set(&hwc->period_left, left);
		hwc->last_period = period;
		ret = 1;
	}

	if (unlikely(left <= 0)) {
		left += period;
		local64_set(&hwc->period_left, left);
		hwc->last_period = period;
		ret = 1;
	}

	if (left > (s64)armpmu->max_period)
		left = armpmu->max_period;

	local64_set(&hwc->prev_count, (u64)-left);

	armpmu->write_counter(idx, (u64)(-left) & 0xffffffff);

	perf_event_update_userpage(event);

	return ret;
}

static u64
armpmu_event_update(struct perf_event *event,
		    struct hw_perf_event *hwc,
		    int idx)
{
	int shift = 64 - 32;
	s64 prev_raw_count, new_raw_count;
	u64 delta;

again:
	prev_raw_count = local64_read(&hwc->prev_count);
	new_raw_count = armpmu->read_counter(idx);

	if (local64_cmpxchg(&hwc->prev_count, prev_raw_count,
			     new_raw_count) != prev_raw_count)
		goto again;

	delta = (new_raw_count << shift) - (prev_raw_count << shift);
	delta >>= shift;

	local64_add(delta, &event->count);
	local64_sub(delta, &hwc->period_left);

	return new_raw_count;
}

static void
armpmu_read(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	/* Don't read disabled counters! */
	if (hwc->idx < 0)
		return;

	armpmu_event_update(event, hwc, hwc->idx);
}

static void
armpmu_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;

	if (!armpmu)
		return;

	/*
	 * ARM pmu always has to update the counter, so ignore
	 * PERF_EF_UPDATE, see comments in armpmu_start().
	 */
	if (!(hwc->state & PERF_HES_STOPPED)) {
		armpmu->disable(hwc, hwc->idx);
		barrier(); /* why? */
		armpmu_event_update(event, hwc, hwc->idx);
		hwc->state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
	}
}

static void
armpmu_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;

	if (!armpmu)
		return;

	/*
	 * ARM pmu always has to reprogram the period, so ignore
	 * PERF_EF_RELOAD, see the comment below.
	 */
	if (flags & PERF_EF_RELOAD)
		WARN_ON_ONCE(!(hwc->state & PERF_HES_UPTODATE));

	hwc->state = 0;
	/*
	 * Set the period again. Some counters can't be stopped, so when we
	 * were stopped we simply disabled the IRQ source and the counter
	 * may have been left counting. If we don't do this step then we may
	 * get an interrupt too soon or *way* too late if the overflow has
	 * happened since disabling.
	 */
	armpmu_event_set_period(event, hwc, hwc->idx);
	armpmu->enable(hwc, hwc->idx);
}

static void
armpmu_del(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	WARN_ON(idx < 0);

	clear_bit(idx, cpuc->active_mask);
	armpmu_stop(event, PERF_EF_UPDATE);
	cpuc->events[idx] = NULL;
	clear_bit(idx, cpuc->used_mask);

	perf_event_update_userpage(event);
}

static int
armpmu_add(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	int idx;
	int err = 0;

	perf_pmu_disable(event->pmu);

	/* If we don't have a space for the counter then finish early. */
	idx = armpmu->get_event_idx(cpuc, hwc);
	if (idx < 0) {
		err = idx;
		goto out;
	}

	/*
	 * If there is an event in the counter we are going to use then make
	 * sure it is disabled.
	 */
	event->hw.idx = idx;
	armpmu->disable(hwc, idx);
	cpuc->events[idx] = event;
	set_bit(idx, cpuc->active_mask);

	hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;
	if (flags & PERF_EF_START)
		armpmu_start(event, PERF_EF_RELOAD);

	/* Propagate our changes to the userspace mapping. */
	perf_event_update_userpage(event);

out:
	perf_pmu_enable(event->pmu);
	return err;
}

static struct pmu pmu;

static int
validate_event(struct cpu_hw_events *cpuc,
	       struct perf_event *event)
{
	struct hw_perf_event fake_event = event->hw;

	if (event->pmu != &pmu || event->state <= PERF_EVENT_STATE_OFF)
		return 1;

	return armpmu->get_event_idx(cpuc, &fake_event) >= 0;
}

static int
validate_group(struct perf_event *event)
{
	struct perf_event *sibling, *leader = event->group_leader;
	struct cpu_hw_events fake_pmu;

	memset(&fake_pmu, 0, sizeof(fake_pmu));

	if (!validate_event(&fake_pmu, leader))
		return -ENOSPC;

	list_for_each_entry(sibling, &leader->sibling_list, group_entry) {
		if (!validate_event(&fake_pmu, sibling))
			return -ENOSPC;
	}

	if (!validate_event(&fake_pmu, event))
		return -ENOSPC;

	return 0;
}

static int
armpmu_reserve_hardware(void)
{
	int i, err = -ENODEV, irq;

	pmu_device = reserve_pmu(ARM_PMU_DEVICE_CPU);
	if (IS_ERR(pmu_device)) {
		pr_warning("unable to reserve pmu\n");
		return PTR_ERR(pmu_device);
	}

	init_pmu(ARM_PMU_DEVICE_CPU);

	if (pmu_device->num_resources < 1) {
		pr_err("no irqs for PMUs defined\n");
		return -ENODEV;
	}

	for (i = 0; i < pmu_device->num_resources; ++i) {
		irq = platform_get_irq(pmu_device, i);
		if (irq < 0)
			continue;

		err = request_irq(irq, armpmu->handle_irq,
				  IRQF_DISABLED | IRQF_NOBALANCING,
				  "armpmu", NULL);
		if (err) {
			pr_warning("unable to request IRQ%d for ARM perf "
				"counters\n", irq);
			break;
		}
	}

	if (err) {
		for (i = i - 1; i >= 0; --i) {
			irq = platform_get_irq(pmu_device, i);
			if (irq >= 0)
				free_irq(irq, NULL);
		}
		release_pmu(pmu_device);
		pmu_device = NULL;
	}

	return err;
}

static void
armpmu_release_hardware(void)
{
	int i, irq;

	for (i = pmu_device->num_resources - 1; i >= 0; --i) {
		irq = platform_get_irq(pmu_device, i);
		if (irq >= 0)
			free_irq(irq, NULL);
	}
	armpmu->stop();

	release_pmu(pmu_device);
	pmu_device = NULL;
}

static atomic_t active_events = ATOMIC_INIT(0);
static DEFINE_MUTEX(pmu_reserve_mutex);

static void
hw_perf_event_destroy(struct perf_event *event)
{
	if (atomic_dec_and_mutex_lock(&active_events, &pmu_reserve_mutex)) {
		armpmu_release_hardware();
		mutex_unlock(&pmu_reserve_mutex);
	}
}

static int
__hw_perf_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int mapping, err;

	/* Decode the generic type into an ARM event identifier. */
	if (PERF_TYPE_HARDWARE == event->attr.type) {
		mapping = armpmu->event_map(event->attr.config);
	} else if (PERF_TYPE_HW_CACHE == event->attr.type) {
		mapping = armpmu_map_cache_event(event->attr.config);
	} else if (PERF_TYPE_RAW == event->attr.type) {
		mapping = armpmu->raw_event(event->attr.config);
	} else {
		pr_debug("event type %x not supported\n", event->attr.type);
		return -EOPNOTSUPP;
	}

	if (mapping < 0) {
		pr_debug("event %x:%llx not supported\n", event->attr.type,
			 event->attr.config);
		return mapping;
	}

	/*
	 * Check whether we need to exclude the counter from certain modes.
	 * The ARM performance counters are on all of the time so if someone
	 * has asked us for some excludes then we have to fail.
	 */
	if (event->attr.exclude_kernel || event->attr.exclude_user ||
	    event->attr.exclude_hv || event->attr.exclude_idle) {
		pr_debug("ARM performance counters do not support "
			 "mode exclusion\n");
		return -EPERM;
	}

	/*
	 * We don't assign an index until we actually place the event onto
	 * hardware. Use -1 to signify that we haven't decided where to put it
	 * yet. For SMP systems, each core has it's own PMU so we can't do any
	 * clever allocation or constraints checking at this point.
	 */
	hwc->idx = -1;

	/*
	 * Store the event encoding into the config_base field. config and
	 * event_base are unused as the only 2 things we need to know are
	 * the event mapping and the counter to use. The counter to use is
	 * also the indx and the config_base is the event type.
	 */
	hwc->config_base	    = (unsigned long)mapping;
	hwc->config		    = 0;
	hwc->event_base		    = 0;

	if (!hwc->sample_period) {
		hwc->sample_period  = armpmu->max_period;
		hwc->last_period    = hwc->sample_period;
		local64_set(&hwc->period_left, hwc->sample_period);
	}

	err = 0;
	if (event->group_leader != event) {
		err = validate_group(event);
		if (err)
			return -EINVAL;
	}

	return err;
}

static int armpmu_event_init(struct perf_event *event)
{
	int err = 0;

	switch (event->attr.type) {
	case PERF_TYPE_RAW:
	case PERF_TYPE_HARDWARE:
	case PERF_TYPE_HW_CACHE:
		break;

	default:
		return -ENOENT;
	}

	if (!armpmu)
		return -ENODEV;

	event->destroy = hw_perf_event_destroy;

	if (!atomic_inc_not_zero(&active_events)) {
		if (atomic_read(&active_events) > armpmu->num_events) {
			atomic_dec(&active_events);
			return -ENOSPC;
		}

		mutex_lock(&pmu_reserve_mutex);
		if (atomic_read(&active_events) == 0) {
			err = armpmu_reserve_hardware();
		}

		if (!err)
			atomic_inc(&active_events);
		mutex_unlock(&pmu_reserve_mutex);
	}

	if (err)
		return err;

	err = __hw_perf_event_init(event);
	if (err)
		hw_perf_event_destroy(event);

	return err;
}

static void armpmu_enable(struct pmu *pmu)
{
	/* Enable all of the perf events on hardware. */
	int idx;
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (!armpmu)
		return;

	for (idx = 0; idx <= armpmu->num_events; ++idx) {
		struct perf_event *event = cpuc->events[idx];

		if (!event)
			continue;

		armpmu->enable(&event->hw, idx);
	}

	armpmu->start();
}

static void armpmu_disable(struct pmu *pmu)
{
	if (armpmu)
		armpmu->stop();
}

static struct pmu pmu = {
	.pmu_enable	= armpmu_enable,
	.pmu_disable	= armpmu_disable,
	.event_init	= armpmu_event_init,
	.add		= armpmu_add,
	.del		= armpmu_del,
	.start		= armpmu_start,
	.stop		= armpmu_stop,
	.read		= armpmu_read,
};

/*
 * ARMv6 Performance counter handling code.
 *
 * ARMv6 has 2 configurable performance counters and a single cycle counter.
 * They all share a single reset bit but can be written to zero so we can use
 * that for a reset.
 *
 * The counters can't be individually enabled or disabled so when we remove
 * one event and replace it with another we could get spurious counts from the
 * wrong event. However, we can take advantage of the fact that the
 * performance counters can export events to the event bus, and the event bus
 * itself can be monitored. This requires that we *don't* export the events to
 * the event bus. The procedure for disabling a configurable counter is:
 *	- change the counter to count the ETMEXTOUT[0] signal (0x20). This
 *	  effectively stops the counter from counting.
 *	- disable the counter's interrupt generation (each counter has it's
 *	  own interrupt enable bit).
 * Once stopped, the counter value can be written as 0 to reset.
 *
 * To enable a counter:
 *	- enable the counter's interrupt generation.
 *	- set the new event type.
 *
 * Note: the dedicated cycle counter only counts cycles and can't be
 * enabled/disabled independently of the others. When we want to disable the
 * cycle counter, we have to just disable the interrupt reporting and start
 * ignoring that counter. When re-enabling, we have to reset the value and
 * enable the interrupt.
 */

enum armv6_perf_types {
	ARMV6_PERFCTR_ICACHE_MISS	    = 0x0,
	ARMV6_PERFCTR_IBUF_STALL	    = 0x1,
	ARMV6_PERFCTR_DDEP_STALL	    = 0x2,
	ARMV6_PERFCTR_ITLB_MISS		    = 0x3,
	ARMV6_PERFCTR_DTLB_MISS		    = 0x4,
	ARMV6_PERFCTR_BR_EXEC		    = 0x5,
	ARMV6_PERFCTR_BR_MISPREDICT	    = 0x6,
	ARMV6_PERFCTR_INSTR_EXEC	    = 0x7,
	ARMV6_PERFCTR_DCACHE_HIT	    = 0x9,
	ARMV6_PERFCTR_DCACHE_ACCESS	    = 0xA,
	ARMV6_PERFCTR_DCACHE_MISS	    = 0xB,
	ARMV6_PERFCTR_DCACHE_WBACK	    = 0xC,
	ARMV6_PERFCTR_SW_PC_CHANGE	    = 0xD,
	ARMV6_PERFCTR_MAIN_TLB_MISS	    = 0xF,
	ARMV6_PERFCTR_EXPL_D_ACCESS	    = 0x10,
	ARMV6_PERFCTR_LSU_FULL_STALL	    = 0x11,
	ARMV6_PERFCTR_WBUF_DRAINED	    = 0x12,
	ARMV6_PERFCTR_CPU_CYCLES	    = 0xFF,
	ARMV6_PERFCTR_NOP		    = 0x20,
};

enum armv6_counters {
	ARMV6_CYCLE_COUNTER = 1,
	ARMV6_COUNTER0,
	ARMV6_COUNTER1,
};

/*
 * The hardware events that we support. We do support cache operations but
 * we have harvard caches and no way to combine instruction and data
 * accesses/misses in hardware.
 */
static const unsigned armv6_perf_map[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]	    = ARMV6_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]	    = ARMV6_PERFCTR_INSTR_EXEC,
	[PERF_COUNT_HW_CACHE_REFERENCES]    = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_CACHE_MISSES]	    = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = ARMV6_PERFCTR_BR_EXEC,
	[PERF_COUNT_HW_BRANCH_MISSES]	    = ARMV6_PERFCTR_BR_MISPREDICT,
	[PERF_COUNT_HW_BUS_CYCLES]	    = HW_OP_UNSUPPORTED,
};

static const unsigned armv6_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					  [PERF_COUNT_HW_CACHE_OP_MAX]
					  [PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		/*
		 * The performance counters don't differentiate between read
		 * and write accesses/misses so this isn't strictly correct,
		 * but it's the best we can do. Writes and reads get
		 * combined.
		 */
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV6_PERFCTR_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV6_PERFCTR_DCACHE_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV6_PERFCTR_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV6_PERFCTR_DCACHE_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV6_PERFCTR_ICACHE_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV6_PERFCTR_ICACHE_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(DTLB)] = {
		/*
		 * The ARM performance counters can count micro DTLB misses,
		 * micro ITLB misses and main TLB misses. There isn't an event
		 * for TLB misses, so use the micro misses here and if users
		 * want the main TLB misses they can use a raw counter.
		 */
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV6_PERFCTR_DTLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV6_PERFCTR_DTLB_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV6_PERFCTR_ITLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV6_PERFCTR_ITLB_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
};

enum armv6mpcore_perf_types {
	ARMV6MPCORE_PERFCTR_ICACHE_MISS	    = 0x0,
	ARMV6MPCORE_PERFCTR_IBUF_STALL	    = 0x1,
	ARMV6MPCORE_PERFCTR_DDEP_STALL	    = 0x2,
	ARMV6MPCORE_PERFCTR_ITLB_MISS	    = 0x3,
	ARMV6MPCORE_PERFCTR_DTLB_MISS	    = 0x4,
	ARMV6MPCORE_PERFCTR_BR_EXEC	    = 0x5,
	ARMV6MPCORE_PERFCTR_BR_NOTPREDICT   = 0x6,
	ARMV6MPCORE_PERFCTR_BR_MISPREDICT   = 0x7,
	ARMV6MPCORE_PERFCTR_INSTR_EXEC	    = 0x8,
	ARMV6MPCORE_PERFCTR_DCACHE_RDACCESS = 0xA,
	ARMV6MPCORE_PERFCTR_DCACHE_RDMISS   = 0xB,
	ARMV6MPCORE_PERFCTR_DCACHE_WRACCESS = 0xC,
	ARMV6MPCORE_PERFCTR_DCACHE_WRMISS   = 0xD,
	ARMV6MPCORE_PERFCTR_DCACHE_EVICTION = 0xE,
	ARMV6MPCORE_PERFCTR_SW_PC_CHANGE    = 0xF,
	ARMV6MPCORE_PERFCTR_MAIN_TLB_MISS   = 0x10,
	ARMV6MPCORE_PERFCTR_EXPL_MEM_ACCESS = 0x11,
	ARMV6MPCORE_PERFCTR_LSU_FULL_STALL  = 0x12,
	ARMV6MPCORE_PERFCTR_WBUF_DRAINED    = 0x13,
	ARMV6MPCORE_PERFCTR_CPU_CYCLES	    = 0xFF,
};

/*
 * The hardware events that we support. We do support cache operations but
 * we have harvard caches and no way to combine instruction and data
 * accesses/misses in hardware.
 */
static const unsigned armv6mpcore_perf_map[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]	    = ARMV6MPCORE_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]	    = ARMV6MPCORE_PERFCTR_INSTR_EXEC,
	[PERF_COUNT_HW_CACHE_REFERENCES]    = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_CACHE_MISSES]	    = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = ARMV6MPCORE_PERFCTR_BR_EXEC,
	[PERF_COUNT_HW_BRANCH_MISSES]	    = ARMV6MPCORE_PERFCTR_BR_MISPREDICT,
	[PERF_COUNT_HW_BUS_CYCLES]	    = HW_OP_UNSUPPORTED,
};

static const unsigned armv6mpcore_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					[PERF_COUNT_HW_CACHE_OP_MAX]
					[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]  =
				ARMV6MPCORE_PERFCTR_DCACHE_RDACCESS,
			[C(RESULT_MISS)]    =
				ARMV6MPCORE_PERFCTR_DCACHE_RDMISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]  =
				ARMV6MPCORE_PERFCTR_DCACHE_WRACCESS,
			[C(RESULT_MISS)]    =
				ARMV6MPCORE_PERFCTR_DCACHE_WRMISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = CACHE_OP_UNSUPPORTED,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = ARMV6MPCORE_PERFCTR_ICACHE_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = ARMV6MPCORE_PERFCTR_ICACHE_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = CACHE_OP_UNSUPPORTED,
		},
	},
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = CACHE_OP_UNSUPPORTED,
		},
	},
	[C(DTLB)] = {
		/*
		 * The ARM performance counters can count micro DTLB misses,
		 * micro ITLB misses and main TLB misses. There isn't an event
		 * for TLB misses, so use the micro misses here and if users
		 * want the main TLB misses they can use a raw counter.
		 */
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = ARMV6MPCORE_PERFCTR_DTLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = ARMV6MPCORE_PERFCTR_DTLB_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = CACHE_OP_UNSUPPORTED,
		},
	},
	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = ARMV6MPCORE_PERFCTR_ITLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = ARMV6MPCORE_PERFCTR_ITLB_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = CACHE_OP_UNSUPPORTED,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]  = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]    = CACHE_OP_UNSUPPORTED,
		},
	},
};

static inline unsigned long
armv6_pmcr_read(void)
{
	u32 val;
	asm volatile("mrc   p15, 0, %0, c15, c12, 0" : "=r"(val));
	return val;
}

static inline void
armv6_pmcr_write(unsigned long val)
{
	asm volatile("mcr   p15, 0, %0, c15, c12, 0" : : "r"(val));
}

#define ARMV6_PMCR_ENABLE		(1 << 0)
#define ARMV6_PMCR_CTR01_RESET		(1 << 1)
#define ARMV6_PMCR_CCOUNT_RESET		(1 << 2)
#define ARMV6_PMCR_CCOUNT_DIV		(1 << 3)
#define ARMV6_PMCR_COUNT0_IEN		(1 << 4)
#define ARMV6_PMCR_COUNT1_IEN		(1 << 5)
#define ARMV6_PMCR_CCOUNT_IEN		(1 << 6)
#define ARMV6_PMCR_COUNT0_OVERFLOW	(1 << 8)
#define ARMV6_PMCR_COUNT1_OVERFLOW	(1 << 9)
#define ARMV6_PMCR_CCOUNT_OVERFLOW	(1 << 10)
#define ARMV6_PMCR_EVT_COUNT0_SHIFT	20
#define ARMV6_PMCR_EVT_COUNT0_MASK	(0xFF << ARMV6_PMCR_EVT_COUNT0_SHIFT)
#define ARMV6_PMCR_EVT_COUNT1_SHIFT	12
#define ARMV6_PMCR_EVT_COUNT1_MASK	(0xFF << ARMV6_PMCR_EVT_COUNT1_SHIFT)

#define ARMV6_PMCR_OVERFLOWED_MASK \
	(ARMV6_PMCR_COUNT0_OVERFLOW | ARMV6_PMCR_COUNT1_OVERFLOW | \
	 ARMV6_PMCR_CCOUNT_OVERFLOW)

static inline int
armv6_pmcr_has_overflowed(unsigned long pmcr)
{
	return (pmcr & ARMV6_PMCR_OVERFLOWED_MASK);
}

static inline int
armv6_pmcr_counter_has_overflowed(unsigned long pmcr,
				  enum armv6_counters counter)
{
	int ret = 0;

	if (ARMV6_CYCLE_COUNTER == counter)
		ret = pmcr & ARMV6_PMCR_CCOUNT_OVERFLOW;
	else if (ARMV6_COUNTER0 == counter)
		ret = pmcr & ARMV6_PMCR_COUNT0_OVERFLOW;
	else if (ARMV6_COUNTER1 == counter)
		ret = pmcr & ARMV6_PMCR_COUNT1_OVERFLOW;
	else
		WARN_ONCE(1, "invalid counter number (%d)\n", counter);

	return ret;
}

static inline u32
armv6pmu_read_counter(int counter)
{
	unsigned long value = 0;

	if (ARMV6_CYCLE_COUNTER == counter)
		asm volatile("mrc   p15, 0, %0, c15, c12, 1" : "=r"(value));
	else if (ARMV6_COUNTER0 == counter)
		asm volatile("mrc   p15, 0, %0, c15, c12, 2" : "=r"(value));
	else if (ARMV6_COUNTER1 == counter)
		asm volatile("mrc   p15, 0, %0, c15, c12, 3" : "=r"(value));
	else
		WARN_ONCE(1, "invalid counter number (%d)\n", counter);

	return value;
}

static inline void
armv6pmu_write_counter(int counter,
		       u32 value)
{
	if (ARMV6_CYCLE_COUNTER == counter)
		asm volatile("mcr   p15, 0, %0, c15, c12, 1" : : "r"(value));
	else if (ARMV6_COUNTER0 == counter)
		asm volatile("mcr   p15, 0, %0, c15, c12, 2" : : "r"(value));
	else if (ARMV6_COUNTER1 == counter)
		asm volatile("mcr   p15, 0, %0, c15, c12, 3" : : "r"(value));
	else
		WARN_ONCE(1, "invalid counter number (%d)\n", counter);
}

void
armv6pmu_enable_event(struct hw_perf_event *hwc,
		      int idx)
{
	unsigned long val, mask, evt, flags;

	if (ARMV6_CYCLE_COUNTER == idx) {
		mask	= 0;
		evt	= ARMV6_PMCR_CCOUNT_IEN;
	} else if (ARMV6_COUNTER0 == idx) {
		mask	= ARMV6_PMCR_EVT_COUNT0_MASK;
		evt	= (hwc->config_base << ARMV6_PMCR_EVT_COUNT0_SHIFT) |
			  ARMV6_PMCR_COUNT0_IEN;
	} else if (ARMV6_COUNTER1 == idx) {
		mask	= ARMV6_PMCR_EVT_COUNT1_MASK;
		evt	= (hwc->config_base << ARMV6_PMCR_EVT_COUNT1_SHIFT) |
			  ARMV6_PMCR_COUNT1_IEN;
	} else {
		WARN_ONCE(1, "invalid counter number (%d)\n", idx);
		return;
	}

	/*
	 * Mask out the current event and set the counter to count the event
	 * that we're interested in.
	 */
	spin_lock_irqsave(&pmu_lock, flags);
	val = armv6_pmcr_read();
	val &= ~mask;
	val |= evt;
	armv6_pmcr_write(val);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static irqreturn_t
armv6pmu_handle_irq(int irq_num,
		    void *dev)
{
	unsigned long pmcr = armv6_pmcr_read();
	struct perf_sample_data data;
	struct cpu_hw_events *cpuc;
	struct pt_regs *regs;
	int idx;

	if (!armv6_pmcr_has_overflowed(pmcr))
		return IRQ_NONE;

	regs = get_irq_regs();

	/*
	 * The interrupts are cleared by writing the overflow flags back to
	 * the control register. All of the other bits don't have any effect
	 * if they are rewritten, so write the whole value back.
	 */
	armv6_pmcr_write(pmcr);

	perf_sample_data_init(&data, 0);

	cpuc = &__get_cpu_var(cpu_hw_events);
	for (idx = 0; idx <= armpmu->num_events; ++idx) {
		struct perf_event *event = cpuc->events[idx];
		struct hw_perf_event *hwc;

		if (!test_bit(idx, cpuc->active_mask))
			continue;

		/*
		 * We have a single interrupt for all counters. Check that
		 * each counter has overflowed before we process it.
		 */
		if (!armv6_pmcr_counter_has_overflowed(pmcr, idx))
			continue;

		hwc = &event->hw;
		armpmu_event_update(event, hwc, idx);
		data.period = event->hw.last_period;
		if (!armpmu_event_set_period(event, hwc, idx))
			continue;

		if (perf_event_overflow(event, 0, &data, regs))
			armpmu->disable(hwc, idx);
	}

	/*
	 * Handle the pending perf events.
	 *
	 * Note: this call *must* be run with interrupts disabled. For
	 * platforms that can have the PMU interrupts raised as an NMI, this
	 * will not work.
	 */
	irq_work_run();

	return IRQ_HANDLED;
}

static void
armv6pmu_start(void)
{
	unsigned long flags, val;

	spin_lock_irqsave(&pmu_lock, flags);
	val = armv6_pmcr_read();
	val |= ARMV6_PMCR_ENABLE;
	armv6_pmcr_write(val);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

void
armv6pmu_stop(void)
{
	unsigned long flags, val;

	spin_lock_irqsave(&pmu_lock, flags);
	val = armv6_pmcr_read();
	val &= ~ARMV6_PMCR_ENABLE;
	armv6_pmcr_write(val);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static inline int
armv6pmu_event_map(int config)
{
	int mapping = armv6_perf_map[config];
	if (HW_OP_UNSUPPORTED == mapping)
		mapping = -EOPNOTSUPP;
	return mapping;
}

static inline int
armv6mpcore_pmu_event_map(int config)
{
	int mapping = armv6mpcore_perf_map[config];
	if (HW_OP_UNSUPPORTED == mapping)
		mapping = -EOPNOTSUPP;
	return mapping;
}

static u64
armv6pmu_raw_event(u64 config)
{
	return config & 0xff;
}

static int
armv6pmu_get_event_idx(struct cpu_hw_events *cpuc,
		       struct hw_perf_event *event)
{
	/* Always place a cycle counter into the cycle counter. */
	if (ARMV6_PERFCTR_CPU_CYCLES == event->config_base) {
		if (test_and_set_bit(ARMV6_CYCLE_COUNTER, cpuc->used_mask))
			return -EAGAIN;

		return ARMV6_CYCLE_COUNTER;
	} else {
		/*
		 * For anything other than a cycle counter, try and use
		 * counter0 and counter1.
		 */
		if (!test_and_set_bit(ARMV6_COUNTER1, cpuc->used_mask)) {
			return ARMV6_COUNTER1;
		}

		if (!test_and_set_bit(ARMV6_COUNTER0, cpuc->used_mask)) {
			return ARMV6_COUNTER0;
		}

		/* The counters are all in use. */
		return -EAGAIN;
	}
}

static void
armv6pmu_disable_event(struct hw_perf_event *hwc,
		       int idx)
{
	unsigned long val, mask, evt, flags;

	if (ARMV6_CYCLE_COUNTER == idx) {
		mask	= ARMV6_PMCR_CCOUNT_IEN;
		evt	= 0;
	} else if (ARMV6_COUNTER0 == idx) {
		mask	= ARMV6_PMCR_COUNT0_IEN | ARMV6_PMCR_EVT_COUNT0_MASK;
		evt	= ARMV6_PERFCTR_NOP << ARMV6_PMCR_EVT_COUNT0_SHIFT;
	} else if (ARMV6_COUNTER1 == idx) {
		mask	= ARMV6_PMCR_COUNT1_IEN | ARMV6_PMCR_EVT_COUNT1_MASK;
		evt	= ARMV6_PERFCTR_NOP << ARMV6_PMCR_EVT_COUNT1_SHIFT;
	} else {
		WARN_ONCE(1, "invalid counter number (%d)\n", idx);
		return;
	}

	/*
	 * Mask out the current event and set the counter to count the number
	 * of ETM bus signal assertion cycles. The external reporting should
	 * be disabled and so this should never increment.
	 */
	spin_lock_irqsave(&pmu_lock, flags);
	val = armv6_pmcr_read();
	val &= ~mask;
	val |= evt;
	armv6_pmcr_write(val);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static void
armv6mpcore_pmu_disable_event(struct hw_perf_event *hwc,
			      int idx)
{
	unsigned long val, mask, flags, evt = 0;

	if (ARMV6_CYCLE_COUNTER == idx) {
		mask	= ARMV6_PMCR_CCOUNT_IEN;
	} else if (ARMV6_COUNTER0 == idx) {
		mask	= ARMV6_PMCR_COUNT0_IEN;
	} else if (ARMV6_COUNTER1 == idx) {
		mask	= ARMV6_PMCR_COUNT1_IEN;
	} else {
		WARN_ONCE(1, "invalid counter number (%d)\n", idx);
		return;
	}

	/*
	 * Unlike UP ARMv6, we don't have a way of stopping the counters. We
	 * simply disable the interrupt reporting.
	 */
	spin_lock_irqsave(&pmu_lock, flags);
	val = armv6_pmcr_read();
	val &= ~mask;
	val |= evt;
	armv6_pmcr_write(val);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static const struct arm_pmu armv6pmu = {
	.id			= ARM_PERF_PMU_ID_V6,
	.handle_irq		= armv6pmu_handle_irq,
	.enable			= armv6pmu_enable_event,
	.disable		= armv6pmu_disable_event,
	.event_map		= armv6pmu_event_map,
	.raw_event		= armv6pmu_raw_event,
	.read_counter		= armv6pmu_read_counter,
	.write_counter		= armv6pmu_write_counter,
	.get_event_idx		= armv6pmu_get_event_idx,
	.start			= armv6pmu_start,
	.stop			= armv6pmu_stop,
	.num_events		= 3,
	.max_period		= (1LLU << 32) - 1,
};

/*
 * ARMv6mpcore is almost identical to single core ARMv6 with the exception
 * that some of the events have different enumerations and that there is no
 * *hack* to stop the programmable counters. To stop the counters we simply
 * disable the interrupt reporting and update the event. When unthrottling we
 * reset the period and enable the interrupt reporting.
 */
static const struct arm_pmu armv6mpcore_pmu = {
	.id			= ARM_PERF_PMU_ID_V6MP,
	.handle_irq		= armv6pmu_handle_irq,
	.enable			= armv6pmu_enable_event,
	.disable		= armv6mpcore_pmu_disable_event,
	.event_map		= armv6mpcore_pmu_event_map,
	.raw_event		= armv6pmu_raw_event,
	.read_counter		= armv6pmu_read_counter,
	.write_counter		= armv6pmu_write_counter,
	.get_event_idx		= armv6pmu_get_event_idx,
	.start			= armv6pmu_start,
	.stop			= armv6pmu_stop,
	.num_events		= 3,
	.max_period		= (1LLU << 32) - 1,
};

/*
 * ARMv7 Cortex-A8 and Cortex-A9 Performance Events handling code.
 *
 * Copied from ARMv6 code, with the low level code inspired
 *  by the ARMv7 Oprofile code.
 *
 * Cortex-A8 has up to 4 configurable performance counters and
 *  a single cycle counter.
 * Cortex-A9 has up to 31 configurable performance counters and
 *  a single cycle counter.
 *
 * All counters can be enabled/disabled and IRQ masked separately. The cycle
 *  counter and all 4 performance counters together can be reset separately.
 */

/* Common ARMv7 event types */
enum armv7_perf_types {
	ARMV7_PERFCTR_PMNC_SW_INCR		= 0x00,
	ARMV7_PERFCTR_IFETCH_MISS		= 0x01,
	ARMV7_PERFCTR_ITLB_MISS			= 0x02,
	ARMV7_PERFCTR_DCACHE_REFILL		= 0x03,
	ARMV7_PERFCTR_DCACHE_ACCESS		= 0x04,
	ARMV7_PERFCTR_DTLB_REFILL		= 0x05,
	ARMV7_PERFCTR_DREAD			= 0x06,
	ARMV7_PERFCTR_DWRITE			= 0x07,

	ARMV7_PERFCTR_EXC_TAKEN			= 0x09,
	ARMV7_PERFCTR_EXC_EXECUTED		= 0x0A,
	ARMV7_PERFCTR_CID_WRITE			= 0x0B,
	/* ARMV7_PERFCTR_PC_WRITE is equivalent to HW_BRANCH_INSTRUCTIONS.
	 * It counts:
	 *  - all branch instructions,
	 *  - instructions that explicitly write the PC,
	 *  - exception generating instructions.
	 */
	ARMV7_PERFCTR_PC_WRITE			= 0x0C,
	ARMV7_PERFCTR_PC_IMM_BRANCH		= 0x0D,
	ARMV7_PERFCTR_UNALIGNED_ACCESS		= 0x0F,
	ARMV7_PERFCTR_PC_BRANCH_MIS_PRED	= 0x10,
	ARMV7_PERFCTR_CLOCK_CYCLES		= 0x11,

	ARMV7_PERFCTR_PC_BRANCH_MIS_USED	= 0x12,

	ARMV7_PERFCTR_CPU_CYCLES		= 0xFF
};

/* ARMv7 Cortex-A8 specific event types */
enum armv7_a8_perf_types {
	ARMV7_PERFCTR_INSTR_EXECUTED		= 0x08,

	ARMV7_PERFCTR_PC_PROC_RETURN		= 0x0E,

	ARMV7_PERFCTR_WRITE_BUFFER_FULL		= 0x40,
	ARMV7_PERFCTR_L2_STORE_MERGED		= 0x41,
	ARMV7_PERFCTR_L2_STORE_BUFF		= 0x42,
	ARMV7_PERFCTR_L2_ACCESS			= 0x43,
	ARMV7_PERFCTR_L2_CACH_MISS		= 0x44,
	ARMV7_PERFCTR_AXI_READ_CYCLES		= 0x45,
	ARMV7_PERFCTR_AXI_WRITE_CYCLES		= 0x46,
	ARMV7_PERFCTR_MEMORY_REPLAY		= 0x47,
	ARMV7_PERFCTR_UNALIGNED_ACCESS_REPLAY	= 0x48,
	ARMV7_PERFCTR_L1_DATA_MISS		= 0x49,
	ARMV7_PERFCTR_L1_INST_MISS		= 0x4A,
	ARMV7_PERFCTR_L1_DATA_COLORING		= 0x4B,
	ARMV7_PERFCTR_L1_NEON_DATA		= 0x4C,
	ARMV7_PERFCTR_L1_NEON_CACH_DATA		= 0x4D,
	ARMV7_PERFCTR_L2_NEON			= 0x4E,
	ARMV7_PERFCTR_L2_NEON_HIT		= 0x4F,
	ARMV7_PERFCTR_L1_INST			= 0x50,
	ARMV7_PERFCTR_PC_RETURN_MIS_PRED	= 0x51,
	ARMV7_PERFCTR_PC_BRANCH_FAILED		= 0x52,
	ARMV7_PERFCTR_PC_BRANCH_TAKEN		= 0x53,
	ARMV7_PERFCTR_PC_BRANCH_EXECUTED	= 0x54,
	ARMV7_PERFCTR_OP_EXECUTED		= 0x55,
	ARMV7_PERFCTR_CYCLES_INST_STALL		= 0x56,
	ARMV7_PERFCTR_CYCLES_INST		= 0x57,
	ARMV7_PERFCTR_CYCLES_NEON_DATA_STALL	= 0x58,
	ARMV7_PERFCTR_CYCLES_NEON_INST_STALL	= 0x59,
	ARMV7_PERFCTR_NEON_CYCLES		= 0x5A,

	ARMV7_PERFCTR_PMU0_EVENTS		= 0x70,
	ARMV7_PERFCTR_PMU1_EVENTS		= 0x71,
	ARMV7_PERFCTR_PMU_EVENTS		= 0x72,
};

/* ARMv7 Cortex-A9 specific event types */
enum armv7_a9_perf_types {
	ARMV7_PERFCTR_JAVA_HW_BYTECODE_EXEC	= 0x40,
	ARMV7_PERFCTR_JAVA_SW_BYTECODE_EXEC	= 0x41,
	ARMV7_PERFCTR_JAZELLE_BRANCH_EXEC	= 0x42,

	ARMV7_PERFCTR_COHERENT_LINE_MISS	= 0x50,
	ARMV7_PERFCTR_COHERENT_LINE_HIT		= 0x51,

	ARMV7_PERFCTR_ICACHE_DEP_STALL_CYCLES	= 0x60,
	ARMV7_PERFCTR_DCACHE_DEP_STALL_CYCLES	= 0x61,
	ARMV7_PERFCTR_TLB_MISS_DEP_STALL_CYCLES	= 0x62,
	ARMV7_PERFCTR_STREX_EXECUTED_PASSED	= 0x63,
	ARMV7_PERFCTR_STREX_EXECUTED_FAILED	= 0x64,
	ARMV7_PERFCTR_DATA_EVICTION		= 0x65,
	ARMV7_PERFCTR_ISSUE_STAGE_NO_INST	= 0x66,
	ARMV7_PERFCTR_ISSUE_STAGE_EMPTY		= 0x67,
	ARMV7_PERFCTR_INST_OUT_OF_RENAME_STAGE	= 0x68,

	ARMV7_PERFCTR_PREDICTABLE_FUNCT_RETURNS	= 0x6E,

	ARMV7_PERFCTR_MAIN_UNIT_EXECUTED_INST	= 0x70,
	ARMV7_PERFCTR_SECOND_UNIT_EXECUTED_INST	= 0x71,
	ARMV7_PERFCTR_LD_ST_UNIT_EXECUTED_INST	= 0x72,
	ARMV7_PERFCTR_FP_EXECUTED_INST		= 0x73,
	ARMV7_PERFCTR_NEON_EXECUTED_INST	= 0x74,

	ARMV7_PERFCTR_PLD_FULL_DEP_STALL_CYCLES	= 0x80,
	ARMV7_PERFCTR_DATA_WR_DEP_STALL_CYCLES	= 0x81,
	ARMV7_PERFCTR_ITLB_MISS_DEP_STALL_CYCLES	= 0x82,
	ARMV7_PERFCTR_DTLB_MISS_DEP_STALL_CYCLES	= 0x83,
	ARMV7_PERFCTR_MICRO_ITLB_MISS_DEP_STALL_CYCLES	= 0x84,
	ARMV7_PERFCTR_MICRO_DTLB_MISS_DEP_STALL_CYCLES 	= 0x85,
	ARMV7_PERFCTR_DMB_DEP_STALL_CYCLES	= 0x86,

	ARMV7_PERFCTR_INTGR_CLK_ENABLED_CYCLES	= 0x8A,
	ARMV7_PERFCTR_DATA_ENGINE_CLK_EN_CYCLES	= 0x8B,

	ARMV7_PERFCTR_ISB_INST			= 0x90,
	ARMV7_PERFCTR_DSB_INST			= 0x91,
	ARMV7_PERFCTR_DMB_INST			= 0x92,
	ARMV7_PERFCTR_EXT_INTERRUPTS		= 0x93,

	ARMV7_PERFCTR_PLE_CACHE_LINE_RQST_COMPLETED	= 0xA0,
	ARMV7_PERFCTR_PLE_CACHE_LINE_RQST_SKIPPED	= 0xA1,
	ARMV7_PERFCTR_PLE_FIFO_FLUSH		= 0xA2,
	ARMV7_PERFCTR_PLE_RQST_COMPLETED	= 0xA3,
	ARMV7_PERFCTR_PLE_FIFO_OVERFLOW		= 0xA4,
	ARMV7_PERFCTR_PLE_RQST_PROG		= 0xA5
};

/*
 * Cortex-A8 HW events mapping
 *
 * The hardware events that we support. We do support cache operations but
 * we have harvard caches and no way to combine instruction and data
 * accesses/misses in hardware.
 */
static const unsigned armv7_a8_perf_map[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]	    = ARMV7_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]	    = ARMV7_PERFCTR_INSTR_EXECUTED,
	[PERF_COUNT_HW_CACHE_REFERENCES]    = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_CACHE_MISSES]	    = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = ARMV7_PERFCTR_PC_WRITE,
	[PERF_COUNT_HW_BRANCH_MISSES]	    = ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_BUS_CYCLES]	    = ARMV7_PERFCTR_CLOCK_CYCLES,
};

static const unsigned armv7_a8_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					  [PERF_COUNT_HW_CACHE_OP_MAX]
					  [PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		/*
		 * The performance counters don't differentiate between read
		 * and write accesses/misses so this isn't strictly correct,
		 * but it's the best we can do. Writes and reads get
		 * combined.
		 */
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_DCACHE_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_DCACHE_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_INST,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_INST_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L1_INST,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L1_INST_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L2_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L2_CACH_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_L2_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_L2_CACH_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(DTLB)] = {
		/*
		 * Only ITLB misses and DTLB refills are supported.
		 * If users want the DTLB refills misses a raw counter
		 * must be used.
		 */
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_DTLB_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_DTLB_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_WRITE,
			[C(RESULT_MISS)]
					= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_WRITE,
			[C(RESULT_MISS)]
					= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
};

/*
 * Cortex-A9 HW events mapping
 */
static const unsigned armv7_a9_perf_map[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]	    = ARMV7_PERFCTR_CPU_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]	    =
					ARMV7_PERFCTR_INST_OUT_OF_RENAME_STAGE,
	[PERF_COUNT_HW_CACHE_REFERENCES]    = ARMV7_PERFCTR_COHERENT_LINE_HIT,
	[PERF_COUNT_HW_CACHE_MISSES]	    = ARMV7_PERFCTR_COHERENT_LINE_MISS,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = ARMV7_PERFCTR_PC_WRITE,
	[PERF_COUNT_HW_BRANCH_MISSES]	    = ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_BUS_CYCLES]	    = ARMV7_PERFCTR_CLOCK_CYCLES,
};

static const unsigned armv7_a9_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					  [PERF_COUNT_HW_CACHE_OP_MAX]
					  [PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		/*
		 * The performance counters don't differentiate between read
		 * and write accesses/misses so this isn't strictly correct,
		 * but it's the best we can do. Writes and reads get
		 * combined.
		 */
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_DCACHE_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_DCACHE_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_IFETCH_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_IFETCH_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(DTLB)] = {
		/*
		 * Only ITLB misses and DTLB refills are supported.
		 * If users want the DTLB refills misses a raw counter
		 * must be used.
		 */
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_DTLB_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_DTLB_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= ARMV7_PERFCTR_ITLB_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_WRITE,
			[C(RESULT_MISS)]
					= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV7_PERFCTR_PC_WRITE,
			[C(RESULT_MISS)]
					= ARMV7_PERFCTR_PC_BRANCH_MIS_PRED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
};

/*
 * Perf Events counters
 */
enum armv7_counters {
	ARMV7_CYCLE_COUNTER 		= 1,	/* Cycle counter */
	ARMV7_COUNTER0			= 2,	/* First event counter */
};

/*
 * The cycle counter is ARMV7_CYCLE_COUNTER.
 * The first event counter is ARMV7_COUNTER0.
 * The last event counter is (ARMV7_COUNTER0 + armpmu->num_events - 1).
 */
#define	ARMV7_COUNTER_LAST	(ARMV7_COUNTER0 + armpmu->num_events - 1)

/*
 * ARMv7 low level PMNC access
 */

/*
 * Per-CPU PMNC: config reg
 */
#define ARMV7_PMNC_E		(1 << 0) /* Enable all counters */
#define ARMV7_PMNC_P		(1 << 1) /* Reset all counters */
#define ARMV7_PMNC_C		(1 << 2) /* Cycle counter reset */
#define ARMV7_PMNC_D		(1 << 3) /* CCNT counts every 64th cpu cycle */
#define ARMV7_PMNC_X		(1 << 4) /* Export to ETM */
#define ARMV7_PMNC_DP		(1 << 5) /* Disable CCNT if non-invasive debug*/
#define	ARMV7_PMNC_N_SHIFT	11	 /* Number of counters supported */
#define	ARMV7_PMNC_N_MASK	0x1f
#define	ARMV7_PMNC_MASK		0x3f	 /* Mask for writable bits */

/*
 * Available counters
 */
#define ARMV7_CNT0 		0	/* First event counter */
#define ARMV7_CCNT 		31	/* Cycle counter */

/* Perf Event to low level counters mapping */
#define ARMV7_EVENT_CNT_TO_CNTx	(ARMV7_COUNTER0 - ARMV7_CNT0)

/*
 * CNTENS: counters enable reg
 */
#define ARMV7_CNTENS_P(idx)	(1 << (idx - ARMV7_EVENT_CNT_TO_CNTx))
#define ARMV7_CNTENS_C		(1 << ARMV7_CCNT)

/*
 * CNTENC: counters disable reg
 */
#define ARMV7_CNTENC_P(idx)	(1 << (idx - ARMV7_EVENT_CNT_TO_CNTx))
#define ARMV7_CNTENC_C		(1 << ARMV7_CCNT)

/*
 * INTENS: counters overflow interrupt enable reg
 */
#define ARMV7_INTENS_P(idx)	(1 << (idx - ARMV7_EVENT_CNT_TO_CNTx))
#define ARMV7_INTENS_C		(1 << ARMV7_CCNT)

/*
 * INTENC: counters overflow interrupt disable reg
 */
#define ARMV7_INTENC_P(idx)	(1 << (idx - ARMV7_EVENT_CNT_TO_CNTx))
#define ARMV7_INTENC_C		(1 << ARMV7_CCNT)

/*
 * EVTSEL: Event selection reg
 */
#define	ARMV7_EVTSEL_MASK	0xff		/* Mask for writable bits */

/*
 * SELECT: Counter selection reg
 */
#define	ARMV7_SELECT_MASK	0x1f		/* Mask for writable bits */

/*
 * FLAG: counters overflow flag status reg
 */
#define ARMV7_FLAG_P(idx)	(1 << (idx - ARMV7_EVENT_CNT_TO_CNTx))
#define ARMV7_FLAG_C		(1 << ARMV7_CCNT)
#define	ARMV7_FLAG_MASK		0xffffffff	/* Mask for writable bits */
#define	ARMV7_OVERFLOWED_MASK	ARMV7_FLAG_MASK

static inline unsigned long armv7_pmnc_read(void)
{
	u32 val;
	asm volatile("mrc p15, 0, %0, c9, c12, 0" : "=r"(val));
	return val;
}

static inline void armv7_pmnc_write(unsigned long val)
{
	val &= ARMV7_PMNC_MASK;
	asm volatile("mcr p15, 0, %0, c9, c12, 0" : : "r"(val));
}

static inline int armv7_pmnc_has_overflowed(unsigned long pmnc)
{
	return pmnc & ARMV7_OVERFLOWED_MASK;
}

static inline int armv7_pmnc_counter_has_overflowed(unsigned long pmnc,
					enum armv7_counters counter)
{
	int ret;

	if (counter == ARMV7_CYCLE_COUNTER)
		ret = pmnc & ARMV7_FLAG_C;
	else if ((counter >= ARMV7_COUNTER0) && (counter <= ARMV7_COUNTER_LAST))
		ret = pmnc & ARMV7_FLAG_P(counter);
	else
		pr_err("CPU%u checking wrong counter %d overflow status\n",
			smp_processor_id(), counter);

	return ret;
}

static inline int armv7_pmnc_select_counter(unsigned int idx)
{
	u32 val;

	if ((idx < ARMV7_COUNTER0) || (idx > ARMV7_COUNTER_LAST)) {
		pr_err("CPU%u selecting wrong PMNC counter"
			" %d\n", smp_processor_id(), idx);
		return -1;
	}

	val = (idx - ARMV7_EVENT_CNT_TO_CNTx) & ARMV7_SELECT_MASK;
	asm volatile("mcr p15, 0, %0, c9, c12, 5" : : "r" (val));

	return idx;
}

static inline u32 armv7pmu_read_counter(int idx)
{
	unsigned long value = 0;

	if (idx == ARMV7_CYCLE_COUNTER)
		asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r" (value));
	else if ((idx >= ARMV7_COUNTER0) && (idx <= ARMV7_COUNTER_LAST)) {
		if (armv7_pmnc_select_counter(idx) == idx)
			asm volatile("mrc p15, 0, %0, c9, c13, 2"
				     : "=r" (value));
	} else
		pr_err("CPU%u reading wrong counter %d\n",
			smp_processor_id(), idx);

	return value;
}

static inline void armv7pmu_write_counter(int idx, u32 value)
{
	if (idx == ARMV7_CYCLE_COUNTER)
		asm volatile("mcr p15, 0, %0, c9, c13, 0" : : "r" (value));
	else if ((idx >= ARMV7_COUNTER0) && (idx <= ARMV7_COUNTER_LAST)) {
		if (armv7_pmnc_select_counter(idx) == idx)
			asm volatile("mcr p15, 0, %0, c9, c13, 2"
				     : : "r" (value));
	} else
		pr_err("CPU%u writing wrong counter %d\n",
			smp_processor_id(), idx);
}

static inline void armv7_pmnc_write_evtsel(unsigned int idx, u32 val)
{
	if (armv7_pmnc_select_counter(idx) == idx) {
		val &= ARMV7_EVTSEL_MASK;
		asm volatile("mcr p15, 0, %0, c9, c13, 1" : : "r" (val));
	}
}

static inline u32 armv7_pmnc_enable_counter(unsigned int idx)
{
	u32 val;

	if ((idx != ARMV7_CYCLE_COUNTER) &&
	    ((idx < ARMV7_COUNTER0) || (idx > ARMV7_COUNTER_LAST))) {
		pr_err("CPU%u enabling wrong PMNC counter"
			" %d\n", smp_processor_id(), idx);
		return -1;
	}

	if (idx == ARMV7_CYCLE_COUNTER)
		val = ARMV7_CNTENS_C;
	else
		val = ARMV7_CNTENS_P(idx);

	asm volatile("mcr p15, 0, %0, c9, c12, 1" : : "r" (val));

	return idx;
}

static inline u32 armv7_pmnc_disable_counter(unsigned int idx)
{
	u32 val;


	if ((idx != ARMV7_CYCLE_COUNTER) &&
	    ((idx < ARMV7_COUNTER0) || (idx > ARMV7_COUNTER_LAST))) {
		pr_err("CPU%u disabling wrong PMNC counter"
			" %d\n", smp_processor_id(), idx);
		return -1;
	}

	if (idx == ARMV7_CYCLE_COUNTER)
		val = ARMV7_CNTENC_C;
	else
		val = ARMV7_CNTENC_P(idx);

	asm volatile("mcr p15, 0, %0, c9, c12, 2" : : "r" (val));

	return idx;
}

static inline u32 armv7_pmnc_enable_intens(unsigned int idx)
{
	u32 val;

	if ((idx != ARMV7_CYCLE_COUNTER) &&
	    ((idx < ARMV7_COUNTER0) || (idx > ARMV7_COUNTER_LAST))) {
		pr_err("CPU%u enabling wrong PMNC counter"
			" interrupt enable %d\n", smp_processor_id(), idx);
		return -1;
	}

	if (idx == ARMV7_CYCLE_COUNTER)
		val = ARMV7_INTENS_C;
	else
		val = ARMV7_INTENS_P(idx);

	asm volatile("mcr p15, 0, %0, c9, c14, 1" : : "r" (val));

	return idx;
}

static inline u32 armv7_pmnc_disable_intens(unsigned int idx)
{
	u32 val;

	if ((idx != ARMV7_CYCLE_COUNTER) &&
	    ((idx < ARMV7_COUNTER0) || (idx > ARMV7_COUNTER_LAST))) {
		pr_err("CPU%u disabling wrong PMNC counter"
			" interrupt enable %d\n", smp_processor_id(), idx);
		return -1;
	}

	if (idx == ARMV7_CYCLE_COUNTER)
		val = ARMV7_INTENC_C;
	else
		val = ARMV7_INTENC_P(idx);

	asm volatile("mcr p15, 0, %0, c9, c14, 2" : : "r" (val));

	return idx;
}

static inline u32 armv7_pmnc_getreset_flags(void)
{
	u32 val;

	/* Read */
	asm volatile("mrc p15, 0, %0, c9, c12, 3" : "=r" (val));

	/* Write to clear flags */
	val &= ARMV7_FLAG_MASK;
	asm volatile("mcr p15, 0, %0, c9, c12, 3" : : "r" (val));

	return val;
}

#ifdef DEBUG
static void armv7_pmnc_dump_regs(void)
{
	u32 val;
	unsigned int cnt;

	printk(KERN_INFO "PMNC registers dump:\n");

	asm volatile("mrc p15, 0, %0, c9, c12, 0" : "=r" (val));
	printk(KERN_INFO "PMNC  =0x%08x\n", val);

	asm volatile("mrc p15, 0, %0, c9, c12, 1" : "=r" (val));
	printk(KERN_INFO "CNTENS=0x%08x\n", val);

	asm volatile("mrc p15, 0, %0, c9, c14, 1" : "=r" (val));
	printk(KERN_INFO "INTENS=0x%08x\n", val);

	asm volatile("mrc p15, 0, %0, c9, c12, 3" : "=r" (val));
	printk(KERN_INFO "FLAGS =0x%08x\n", val);

	asm volatile("mrc p15, 0, %0, c9, c12, 5" : "=r" (val));
	printk(KERN_INFO "SELECT=0x%08x\n", val);

	asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r" (val));
	printk(KERN_INFO "CCNT  =0x%08x\n", val);

	for (cnt = ARMV7_COUNTER0; cnt < ARMV7_COUNTER_LAST; cnt++) {
		armv7_pmnc_select_counter(cnt);
		asm volatile("mrc p15, 0, %0, c9, c13, 2" : "=r" (val));
		printk(KERN_INFO "CNT[%d] count =0x%08x\n",
			cnt-ARMV7_EVENT_CNT_TO_CNTx, val);
		asm volatile("mrc p15, 0, %0, c9, c13, 1" : "=r" (val));
		printk(KERN_INFO "CNT[%d] evtsel=0x%08x\n",
			cnt-ARMV7_EVENT_CNT_TO_CNTx, val);
	}
}
#endif

void armv7pmu_enable_event(struct hw_perf_event *hwc, int idx)
{
	unsigned long flags;

	/*
	 * Enable counter and interrupt, and set the counter to count
	 * the event that we're interested in.
	 */
	spin_lock_irqsave(&pmu_lock, flags);

	/*
	 * Disable counter
	 */
	armv7_pmnc_disable_counter(idx);

	/*
	 * Set event (if destined for PMNx counters)
	 * We don't need to set the event if it's a cycle count
	 */
	if (idx != ARMV7_CYCLE_COUNTER)
		armv7_pmnc_write_evtsel(idx, hwc->config_base);

	/*
	 * Enable interrupt for this counter
	 */
	armv7_pmnc_enable_intens(idx);

	/*
	 * Enable counter
	 */
	armv7_pmnc_enable_counter(idx);

	spin_unlock_irqrestore(&pmu_lock, flags);
}

static void armv7pmu_disable_event(struct hw_perf_event *hwc, int idx)
{
	unsigned long flags;

	/*
	 * Disable counter and interrupt
	 */
	spin_lock_irqsave(&pmu_lock, flags);

	/*
	 * Disable counter
	 */
	armv7_pmnc_disable_counter(idx);

	/*
	 * Disable interrupt for this counter
	 */
	armv7_pmnc_disable_intens(idx);

	spin_unlock_irqrestore(&pmu_lock, flags);
}

static irqreturn_t armv7pmu_handle_irq(int irq_num, void *dev)
{
	unsigned long pmnc;
	struct perf_sample_data data;
	struct cpu_hw_events *cpuc;
	struct pt_regs *regs;
	int idx;

	/*
	 * Get and reset the IRQ flags
	 */
	pmnc = armv7_pmnc_getreset_flags();

	/*
	 * Did an overflow occur?
	 */
	if (!armv7_pmnc_has_overflowed(pmnc))
		return IRQ_NONE;

	/*
	 * Handle the counter(s) overflow(s)
	 */
	regs = get_irq_regs();

	perf_sample_data_init(&data, 0);

	cpuc = &__get_cpu_var(cpu_hw_events);
	for (idx = 0; idx <= armpmu->num_events; ++idx) {
		struct perf_event *event = cpuc->events[idx];
		struct hw_perf_event *hwc;

		if (!test_bit(idx, cpuc->active_mask))
			continue;

		/*
		 * We have a single interrupt for all counters. Check that
		 * each counter has overflowed before we process it.
		 */
		if (!armv7_pmnc_counter_has_overflowed(pmnc, idx))
			continue;

		hwc = &event->hw;
		armpmu_event_update(event, hwc, idx);
		data.period = event->hw.last_period;
		if (!armpmu_event_set_period(event, hwc, idx))
			continue;

		if (perf_event_overflow(event, 0, &data, regs))
			armpmu->disable(hwc, idx);
	}

	/*
	 * Handle the pending perf events.
	 *
	 * Note: this call *must* be run with interrupts disabled. For
	 * platforms that can have the PMU interrupts raised as an NMI, this
	 * will not work.
	 */
	irq_work_run();

	return IRQ_HANDLED;
}

static void armv7pmu_start(void)
{
	unsigned long flags;

	spin_lock_irqsave(&pmu_lock, flags);
	/* Enable all counters */
	armv7_pmnc_write(armv7_pmnc_read() | ARMV7_PMNC_E);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static void armv7pmu_stop(void)
{
	unsigned long flags;

	spin_lock_irqsave(&pmu_lock, flags);
	/* Disable all counters */
	armv7_pmnc_write(armv7_pmnc_read() & ~ARMV7_PMNC_E);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static inline int armv7_a8_pmu_event_map(int config)
{
	int mapping = armv7_a8_perf_map[config];
	if (HW_OP_UNSUPPORTED == mapping)
		mapping = -EOPNOTSUPP;
	return mapping;
}

static inline int armv7_a9_pmu_event_map(int config)
{
	int mapping = armv7_a9_perf_map[config];
	if (HW_OP_UNSUPPORTED == mapping)
		mapping = -EOPNOTSUPP;
	return mapping;
}

static u64 armv7pmu_raw_event(u64 config)
{
	return config & 0xff;
}

static int armv7pmu_get_event_idx(struct cpu_hw_events *cpuc,
				  struct hw_perf_event *event)
{
	int idx;

	/* Always place a cycle counter into the cycle counter. */
	if (event->config_base == ARMV7_PERFCTR_CPU_CYCLES) {
		if (test_and_set_bit(ARMV7_CYCLE_COUNTER, cpuc->used_mask))
			return -EAGAIN;

		return ARMV7_CYCLE_COUNTER;
	} else {
		/*
		 * For anything other than a cycle counter, try and use
		 * the events counters
		 */
		for (idx = ARMV7_COUNTER0; idx <= armpmu->num_events; ++idx) {
			if (!test_and_set_bit(idx, cpuc->used_mask))
				return idx;
		}

		/* The counters are all in use. */
		return -EAGAIN;
	}
}

static struct arm_pmu armv7pmu = {
	.handle_irq		= armv7pmu_handle_irq,
	.enable			= armv7pmu_enable_event,
	.disable		= armv7pmu_disable_event,
	.raw_event		= armv7pmu_raw_event,
	.read_counter		= armv7pmu_read_counter,
	.write_counter		= armv7pmu_write_counter,
	.get_event_idx		= armv7pmu_get_event_idx,
	.start			= armv7pmu_start,
	.stop			= armv7pmu_stop,
	.max_period		= (1LLU << 32) - 1,
};

static u32 __init armv7_reset_read_pmnc(void)
{
	u32 nb_cnt;

	/* Initialize & Reset PMNC: C and P bits */
	armv7_pmnc_write(ARMV7_PMNC_P | ARMV7_PMNC_C);

	/* Read the nb of CNTx counters supported from PMNC */
	nb_cnt = (armv7_pmnc_read() >> ARMV7_PMNC_N_SHIFT) & ARMV7_PMNC_N_MASK;

	/* Add the CPU cycles counter and return */
	return nb_cnt + 1;
}

/*
 * ARMv5 [xscale] Performance counter handling code.
 *
 * Based on xscale OProfile code.
 *
 * There are two variants of the xscale PMU that we support:
 * 	- xscale1pmu: 2 event counters and a cycle counter
 * 	- xscale2pmu: 4 event counters and a cycle counter
 * The two variants share event definitions, but have different
 * PMU structures.
 */

enum xscale_perf_types {
	XSCALE_PERFCTR_ICACHE_MISS		= 0x00,
	XSCALE_PERFCTR_ICACHE_NO_DELIVER	= 0x01,
	XSCALE_PERFCTR_DATA_STALL		= 0x02,
	XSCALE_PERFCTR_ITLB_MISS		= 0x03,
	XSCALE_PERFCTR_DTLB_MISS		= 0x04,
	XSCALE_PERFCTR_BRANCH			= 0x05,
	XSCALE_PERFCTR_BRANCH_MISS		= 0x06,
	XSCALE_PERFCTR_INSTRUCTION		= 0x07,
	XSCALE_PERFCTR_DCACHE_FULL_STALL	= 0x08,
	XSCALE_PERFCTR_DCACHE_FULL_STALL_CONTIG	= 0x09,
	XSCALE_PERFCTR_DCACHE_ACCESS		= 0x0A,
	XSCALE_PERFCTR_DCACHE_MISS		= 0x0B,
	XSCALE_PERFCTR_DCACHE_WRITE_BACK	= 0x0C,
	XSCALE_PERFCTR_PC_CHANGED		= 0x0D,
	XSCALE_PERFCTR_BCU_REQUEST		= 0x10,
	XSCALE_PERFCTR_BCU_FULL			= 0x11,
	XSCALE_PERFCTR_BCU_DRAIN		= 0x12,
	XSCALE_PERFCTR_BCU_ECC_NO_ELOG		= 0x14,
	XSCALE_PERFCTR_BCU_1_BIT_ERR		= 0x15,
	XSCALE_PERFCTR_RMW			= 0x16,
	/* XSCALE_PERFCTR_CCNT is not hardware defined */
	XSCALE_PERFCTR_CCNT			= 0xFE,
	XSCALE_PERFCTR_UNUSED			= 0xFF,
};

enum xscale_counters {
	XSCALE_CYCLE_COUNTER	= 1,
	XSCALE_COUNTER0,
	XSCALE_COUNTER1,
	XSCALE_COUNTER2,
	XSCALE_COUNTER3,
};

static const unsigned xscale_perf_map[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]	    = XSCALE_PERFCTR_CCNT,
	[PERF_COUNT_HW_INSTRUCTIONS]	    = XSCALE_PERFCTR_INSTRUCTION,
	[PERF_COUNT_HW_CACHE_REFERENCES]    = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_CACHE_MISSES]	    = HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = XSCALE_PERFCTR_BRANCH,
	[PERF_COUNT_HW_BRANCH_MISSES]	    = XSCALE_PERFCTR_BRANCH_MISS,
	[PERF_COUNT_HW_BUS_CYCLES]	    = HW_OP_UNSUPPORTED,
};

static const unsigned xscale_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
					   [PERF_COUNT_HW_CACHE_OP_MAX]
					   [PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= XSCALE_PERFCTR_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= XSCALE_PERFCTR_DCACHE_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= XSCALE_PERFCTR_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= XSCALE_PERFCTR_DCACHE_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= XSCALE_PERFCTR_ICACHE_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= XSCALE_PERFCTR_ICACHE_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(DTLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= XSCALE_PERFCTR_DTLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= XSCALE_PERFCTR_DTLB_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= XSCALE_PERFCTR_ITLB_MISS,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= XSCALE_PERFCTR_ITLB_MISS,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
};

#define	XSCALE_PMU_ENABLE	0x001
#define XSCALE_PMN_RESET	0x002
#define	XSCALE_CCNT_RESET	0x004
#define	XSCALE_PMU_RESET	(CCNT_RESET | PMN_RESET)
#define XSCALE_PMU_CNT64	0x008

static inline int
xscalepmu_event_map(int config)
{
	int mapping = xscale_perf_map[config];
	if (HW_OP_UNSUPPORTED == mapping)
		mapping = -EOPNOTSUPP;
	return mapping;
}

static u64
xscalepmu_raw_event(u64 config)
{
	return config & 0xff;
}

#define XSCALE1_OVERFLOWED_MASK	0x700
#define XSCALE1_CCOUNT_OVERFLOW	0x400
#define XSCALE1_COUNT0_OVERFLOW	0x100
#define XSCALE1_COUNT1_OVERFLOW	0x200
#define XSCALE1_CCOUNT_INT_EN	0x040
#define XSCALE1_COUNT0_INT_EN	0x010
#define XSCALE1_COUNT1_INT_EN	0x020
#define XSCALE1_COUNT0_EVT_SHFT	12
#define XSCALE1_COUNT0_EVT_MASK	(0xff << XSCALE1_COUNT0_EVT_SHFT)
#define XSCALE1_COUNT1_EVT_SHFT	20
#define XSCALE1_COUNT1_EVT_MASK	(0xff << XSCALE1_COUNT1_EVT_SHFT)

static inline u32
xscale1pmu_read_pmnc(void)
{
	u32 val;
	asm volatile("mrc p14, 0, %0, c0, c0, 0" : "=r" (val));
	return val;
}

static inline void
xscale1pmu_write_pmnc(u32 val)
{
	/* upper 4bits and 7, 11 are write-as-0 */
	val &= 0xffff77f;
	asm volatile("mcr p14, 0, %0, c0, c0, 0" : : "r" (val));
}

static inline int
xscale1_pmnc_counter_has_overflowed(unsigned long pmnc,
					enum xscale_counters counter)
{
	int ret = 0;

	switch (counter) {
	case XSCALE_CYCLE_COUNTER:
		ret = pmnc & XSCALE1_CCOUNT_OVERFLOW;
		break;
	case XSCALE_COUNTER0:
		ret = pmnc & XSCALE1_COUNT0_OVERFLOW;
		break;
	case XSCALE_COUNTER1:
		ret = pmnc & XSCALE1_COUNT1_OVERFLOW;
		break;
	default:
		WARN_ONCE(1, "invalid counter number (%d)\n", counter);
	}

	return ret;
}

static irqreturn_t
xscale1pmu_handle_irq(int irq_num, void *dev)
{
	unsigned long pmnc;
	struct perf_sample_data data;
	struct cpu_hw_events *cpuc;
	struct pt_regs *regs;
	int idx;

	/*
	 * NOTE: there's an A stepping erratum that states if an overflow
	 *       bit already exists and another occurs, the previous
	 *       Overflow bit gets cleared. There's no workaround.
	 *	 Fixed in B stepping or later.
	 */
	pmnc = xscale1pmu_read_pmnc();

	/*
	 * Write the value back to clear the overflow flags. Overflow
	 * flags remain in pmnc for use below. We also disable the PMU
	 * while we process the interrupt.
	 */
	xscale1pmu_write_pmnc(pmnc & ~XSCALE_PMU_ENABLE);

	if (!(pmnc & XSCALE1_OVERFLOWED_MASK))
		return IRQ_NONE;

	regs = get_irq_regs();

	perf_sample_data_init(&data, 0);

	cpuc = &__get_cpu_var(cpu_hw_events);
	for (idx = 0; idx <= armpmu->num_events; ++idx) {
		struct perf_event *event = cpuc->events[idx];
		struct hw_perf_event *hwc;

		if (!test_bit(idx, cpuc->active_mask))
			continue;

		if (!xscale1_pmnc_counter_has_overflowed(pmnc, idx))
			continue;

		hwc = &event->hw;
		armpmu_event_update(event, hwc, idx);
		data.period = event->hw.last_period;
		if (!armpmu_event_set_period(event, hwc, idx))
			continue;

		if (perf_event_overflow(event, 0, &data, regs))
			armpmu->disable(hwc, idx);
	}

	irq_work_run();

	/*
	 * Re-enable the PMU.
	 */
	pmnc = xscale1pmu_read_pmnc() | XSCALE_PMU_ENABLE;
	xscale1pmu_write_pmnc(pmnc);

	return IRQ_HANDLED;
}

static void
xscale1pmu_enable_event(struct hw_perf_event *hwc, int idx)
{
	unsigned long val, mask, evt, flags;

	switch (idx) {
	case XSCALE_CYCLE_COUNTER:
		mask = 0;
		evt = XSCALE1_CCOUNT_INT_EN;
		break;
	case XSCALE_COUNTER0:
		mask = XSCALE1_COUNT0_EVT_MASK;
		evt = (hwc->config_base << XSCALE1_COUNT0_EVT_SHFT) |
			XSCALE1_COUNT0_INT_EN;
		break;
	case XSCALE_COUNTER1:
		mask = XSCALE1_COUNT1_EVT_MASK;
		evt = (hwc->config_base << XSCALE1_COUNT1_EVT_SHFT) |
			XSCALE1_COUNT1_INT_EN;
		break;
	default:
		WARN_ONCE(1, "invalid counter number (%d)\n", idx);
		return;
	}

	spin_lock_irqsave(&pmu_lock, flags);
	val = xscale1pmu_read_pmnc();
	val &= ~mask;
	val |= evt;
	xscale1pmu_write_pmnc(val);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static void
xscale1pmu_disable_event(struct hw_perf_event *hwc, int idx)
{
	unsigned long val, mask, evt, flags;

	switch (idx) {
	case XSCALE_CYCLE_COUNTER:
		mask = XSCALE1_CCOUNT_INT_EN;
		evt = 0;
		break;
	case XSCALE_COUNTER0:
		mask = XSCALE1_COUNT0_INT_EN | XSCALE1_COUNT0_EVT_MASK;
		evt = XSCALE_PERFCTR_UNUSED << XSCALE1_COUNT0_EVT_SHFT;
		break;
	case XSCALE_COUNTER1:
		mask = XSCALE1_COUNT1_INT_EN | XSCALE1_COUNT1_EVT_MASK;
		evt = XSCALE_PERFCTR_UNUSED << XSCALE1_COUNT1_EVT_SHFT;
		break;
	default:
		WARN_ONCE(1, "invalid counter number (%d)\n", idx);
		return;
	}

	spin_lock_irqsave(&pmu_lock, flags);
	val = xscale1pmu_read_pmnc();
	val &= ~mask;
	val |= evt;
	xscale1pmu_write_pmnc(val);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static int
xscale1pmu_get_event_idx(struct cpu_hw_events *cpuc,
			struct hw_perf_event *event)
{
	if (XSCALE_PERFCTR_CCNT == event->config_base) {
		if (test_and_set_bit(XSCALE_CYCLE_COUNTER, cpuc->used_mask))
			return -EAGAIN;

		return XSCALE_CYCLE_COUNTER;
	} else {
		if (!test_and_set_bit(XSCALE_COUNTER1, cpuc->used_mask)) {
			return XSCALE_COUNTER1;
		}

		if (!test_and_set_bit(XSCALE_COUNTER0, cpuc->used_mask)) {
			return XSCALE_COUNTER0;
		}

		return -EAGAIN;
	}
}

static void
xscale1pmu_start(void)
{
	unsigned long flags, val;

	spin_lock_irqsave(&pmu_lock, flags);
	val = xscale1pmu_read_pmnc();
	val |= XSCALE_PMU_ENABLE;
	xscale1pmu_write_pmnc(val);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static void
xscale1pmu_stop(void)
{
	unsigned long flags, val;

	spin_lock_irqsave(&pmu_lock, flags);
	val = xscale1pmu_read_pmnc();
	val &= ~XSCALE_PMU_ENABLE;
	xscale1pmu_write_pmnc(val);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static inline u32
xscale1pmu_read_counter(int counter)
{
	u32 val = 0;

	switch (counter) {
	case XSCALE_CYCLE_COUNTER:
		asm volatile("mrc p14, 0, %0, c1, c0, 0" : "=r" (val));
		break;
	case XSCALE_COUNTER0:
		asm volatile("mrc p14, 0, %0, c2, c0, 0" : "=r" (val));
		break;
	case XSCALE_COUNTER1:
		asm volatile("mrc p14, 0, %0, c3, c0, 0" : "=r" (val));
		break;
	}

	return val;
}

static inline void
xscale1pmu_write_counter(int counter, u32 val)
{
	switch (counter) {
	case XSCALE_CYCLE_COUNTER:
		asm volatile("mcr p14, 0, %0, c1, c0, 0" : : "r" (val));
		break;
	case XSCALE_COUNTER0:
		asm volatile("mcr p14, 0, %0, c2, c0, 0" : : "r" (val));
		break;
	case XSCALE_COUNTER1:
		asm volatile("mcr p14, 0, %0, c3, c0, 0" : : "r" (val));
		break;
	}
}

static const struct arm_pmu xscale1pmu = {
	.id		= ARM_PERF_PMU_ID_XSCALE1,
	.handle_irq	= xscale1pmu_handle_irq,
	.enable		= xscale1pmu_enable_event,
	.disable	= xscale1pmu_disable_event,
	.event_map	= xscalepmu_event_map,
	.raw_event	= xscalepmu_raw_event,
	.read_counter	= xscale1pmu_read_counter,
	.write_counter	= xscale1pmu_write_counter,
	.get_event_idx	= xscale1pmu_get_event_idx,
	.start		= xscale1pmu_start,
	.stop		= xscale1pmu_stop,
	.num_events	= 3,
	.max_period	= (1LLU << 32) - 1,
};

#define XSCALE2_OVERFLOWED_MASK	0x01f
#define XSCALE2_CCOUNT_OVERFLOW	0x001
#define XSCALE2_COUNT0_OVERFLOW	0x002
#define XSCALE2_COUNT1_OVERFLOW	0x004
#define XSCALE2_COUNT2_OVERFLOW	0x008
#define XSCALE2_COUNT3_OVERFLOW	0x010
#define XSCALE2_CCOUNT_INT_EN	0x001
#define XSCALE2_COUNT0_INT_EN	0x002
#define XSCALE2_COUNT1_INT_EN	0x004
#define XSCALE2_COUNT2_INT_EN	0x008
#define XSCALE2_COUNT3_INT_EN	0x010
#define XSCALE2_COUNT0_EVT_SHFT	0
#define XSCALE2_COUNT0_EVT_MASK	(0xff << XSCALE2_COUNT0_EVT_SHFT)
#define XSCALE2_COUNT1_EVT_SHFT	8
#define XSCALE2_COUNT1_EVT_MASK	(0xff << XSCALE2_COUNT1_EVT_SHFT)
#define XSCALE2_COUNT2_EVT_SHFT	16
#define XSCALE2_COUNT2_EVT_MASK	(0xff << XSCALE2_COUNT2_EVT_SHFT)
#define XSCALE2_COUNT3_EVT_SHFT	24
#define XSCALE2_COUNT3_EVT_MASK	(0xff << XSCALE2_COUNT3_EVT_SHFT)

static inline u32
xscale2pmu_read_pmnc(void)
{
	u32 val;
	asm volatile("mrc p14, 0, %0, c0, c1, 0" : "=r" (val));
	/* bits 1-2 and 4-23 are read-unpredictable */
	return val & 0xff000009;
}

static inline void
xscale2pmu_write_pmnc(u32 val)
{
	/* bits 4-23 are write-as-0, 24-31 are write ignored */
	val &= 0xf;
	asm volatile("mcr p14, 0, %0, c0, c1, 0" : : "r" (val));
}

static inline u32
xscale2pmu_read_overflow_flags(void)
{
	u32 val;
	asm volatile("mrc p14, 0, %0, c5, c1, 0" : "=r" (val));
	return val;
}

static inline void
xscale2pmu_write_overflow_flags(u32 val)
{
	asm volatile("mcr p14, 0, %0, c5, c1, 0" : : "r" (val));
}

static inline u32
xscale2pmu_read_event_select(void)
{
	u32 val;
	asm volatile("mrc p14, 0, %0, c8, c1, 0" : "=r" (val));
	return val;
}

static inline void
xscale2pmu_write_event_select(u32 val)
{
	asm volatile("mcr p14, 0, %0, c8, c1, 0" : : "r"(val));
}

static inline u32
xscale2pmu_read_int_enable(void)
{
	u32 val;
	asm volatile("mrc p14, 0, %0, c4, c1, 0" : "=r" (val));
	return val;
}

static void
xscale2pmu_write_int_enable(u32 val)
{
	asm volatile("mcr p14, 0, %0, c4, c1, 0" : : "r" (val));
}

static inline int
xscale2_pmnc_counter_has_overflowed(unsigned long of_flags,
					enum xscale_counters counter)
{
	int ret = 0;

	switch (counter) {
	case XSCALE_CYCLE_COUNTER:
		ret = of_flags & XSCALE2_CCOUNT_OVERFLOW;
		break;
	case XSCALE_COUNTER0:
		ret = of_flags & XSCALE2_COUNT0_OVERFLOW;
		break;
	case XSCALE_COUNTER1:
		ret = of_flags & XSCALE2_COUNT1_OVERFLOW;
		break;
	case XSCALE_COUNTER2:
		ret = of_flags & XSCALE2_COUNT2_OVERFLOW;
		break;
	case XSCALE_COUNTER3:
		ret = of_flags & XSCALE2_COUNT3_OVERFLOW;
		break;
	default:
		WARN_ONCE(1, "invalid counter number (%d)\n", counter);
	}

	return ret;
}

static irqreturn_t
xscale2pmu_handle_irq(int irq_num, void *dev)
{
	unsigned long pmnc, of_flags;
	struct perf_sample_data data;
	struct cpu_hw_events *cpuc;
	struct pt_regs *regs;
	int idx;

	/* Disable the PMU. */
	pmnc = xscale2pmu_read_pmnc();
	xscale2pmu_write_pmnc(pmnc & ~XSCALE_PMU_ENABLE);

	/* Check the overflow flag register. */
	of_flags = xscale2pmu_read_overflow_flags();
	if (!(of_flags & XSCALE2_OVERFLOWED_MASK))
		return IRQ_NONE;

	/* Clear the overflow bits. */
	xscale2pmu_write_overflow_flags(of_flags);

	regs = get_irq_regs();

	perf_sample_data_init(&data, 0);

	cpuc = &__get_cpu_var(cpu_hw_events);
	for (idx = 0; idx <= armpmu->num_events; ++idx) {
		struct perf_event *event = cpuc->events[idx];
		struct hw_perf_event *hwc;

		if (!test_bit(idx, cpuc->active_mask))
			continue;

		if (!xscale2_pmnc_counter_has_overflowed(pmnc, idx))
			continue;

		hwc = &event->hw;
		armpmu_event_update(event, hwc, idx);
		data.period = event->hw.last_period;
		if (!armpmu_event_set_period(event, hwc, idx))
			continue;

		if (perf_event_overflow(event, 0, &data, regs))
			armpmu->disable(hwc, idx);
	}

	irq_work_run();

	/*
	 * Re-enable the PMU.
	 */
	pmnc = xscale2pmu_read_pmnc() | XSCALE_PMU_ENABLE;
	xscale2pmu_write_pmnc(pmnc);

	return IRQ_HANDLED;
}

static void
xscale2pmu_enable_event(struct hw_perf_event *hwc, int idx)
{
	unsigned long flags, ien, evtsel;

	ien = xscale2pmu_read_int_enable();
	evtsel = xscale2pmu_read_event_select();

	switch (idx) {
	case XSCALE_CYCLE_COUNTER:
		ien |= XSCALE2_CCOUNT_INT_EN;
		break;
	case XSCALE_COUNTER0:
		ien |= XSCALE2_COUNT0_INT_EN;
		evtsel &= ~XSCALE2_COUNT0_EVT_MASK;
		evtsel |= hwc->config_base << XSCALE2_COUNT0_EVT_SHFT;
		break;
	case XSCALE_COUNTER1:
		ien |= XSCALE2_COUNT1_INT_EN;
		evtsel &= ~XSCALE2_COUNT1_EVT_MASK;
		evtsel |= hwc->config_base << XSCALE2_COUNT1_EVT_SHFT;
		break;
	case XSCALE_COUNTER2:
		ien |= XSCALE2_COUNT2_INT_EN;
		evtsel &= ~XSCALE2_COUNT2_EVT_MASK;
		evtsel |= hwc->config_base << XSCALE2_COUNT2_EVT_SHFT;
		break;
	case XSCALE_COUNTER3:
		ien |= XSCALE2_COUNT3_INT_EN;
		evtsel &= ~XSCALE2_COUNT3_EVT_MASK;
		evtsel |= hwc->config_base << XSCALE2_COUNT3_EVT_SHFT;
		break;
	default:
		WARN_ONCE(1, "invalid counter number (%d)\n", idx);
		return;
	}

	spin_lock_irqsave(&pmu_lock, flags);
	xscale2pmu_write_event_select(evtsel);
	xscale2pmu_write_int_enable(ien);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static void
xscale2pmu_disable_event(struct hw_perf_event *hwc, int idx)
{
	unsigned long flags, ien, evtsel;

	ien = xscale2pmu_read_int_enable();
	evtsel = xscale2pmu_read_event_select();

	switch (idx) {
	case XSCALE_CYCLE_COUNTER:
		ien &= ~XSCALE2_CCOUNT_INT_EN;
		break;
	case XSCALE_COUNTER0:
		ien &= ~XSCALE2_COUNT0_INT_EN;
		evtsel &= ~XSCALE2_COUNT0_EVT_MASK;
		evtsel |= XSCALE_PERFCTR_UNUSED << XSCALE2_COUNT0_EVT_SHFT;
		break;
	case XSCALE_COUNTER1:
		ien &= ~XSCALE2_COUNT1_INT_EN;
		evtsel &= ~XSCALE2_COUNT1_EVT_MASK;
		evtsel |= XSCALE_PERFCTR_UNUSED << XSCALE2_COUNT1_EVT_SHFT;
		break;
	case XSCALE_COUNTER2:
		ien &= ~XSCALE2_COUNT2_INT_EN;
		evtsel &= ~XSCALE2_COUNT2_EVT_MASK;
		evtsel |= XSCALE_PERFCTR_UNUSED << XSCALE2_COUNT2_EVT_SHFT;
		break;
	case XSCALE_COUNTER3:
		ien &= ~XSCALE2_COUNT3_INT_EN;
		evtsel &= ~XSCALE2_COUNT3_EVT_MASK;
		evtsel |= XSCALE_PERFCTR_UNUSED << XSCALE2_COUNT3_EVT_SHFT;
		break;
	default:
		WARN_ONCE(1, "invalid counter number (%d)\n", idx);
		return;
	}

	spin_lock_irqsave(&pmu_lock, flags);
	xscale2pmu_write_event_select(evtsel);
	xscale2pmu_write_int_enable(ien);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static int
xscale2pmu_get_event_idx(struct cpu_hw_events *cpuc,
			struct hw_perf_event *event)
{
	int idx = xscale1pmu_get_event_idx(cpuc, event);
	if (idx >= 0)
		goto out;

	if (!test_and_set_bit(XSCALE_COUNTER3, cpuc->used_mask))
		idx = XSCALE_COUNTER3;
	else if (!test_and_set_bit(XSCALE_COUNTER2, cpuc->used_mask))
		idx = XSCALE_COUNTER2;
out:
	return idx;
}

static void
xscale2pmu_start(void)
{
	unsigned long flags, val;

	spin_lock_irqsave(&pmu_lock, flags);
	val = xscale2pmu_read_pmnc() & ~XSCALE_PMU_CNT64;
	val |= XSCALE_PMU_ENABLE;
	xscale2pmu_write_pmnc(val);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static void
xscale2pmu_stop(void)
{
	unsigned long flags, val;

	spin_lock_irqsave(&pmu_lock, flags);
	val = xscale2pmu_read_pmnc();
	val &= ~XSCALE_PMU_ENABLE;
	xscale2pmu_write_pmnc(val);
	spin_unlock_irqrestore(&pmu_lock, flags);
}

static inline u32
xscale2pmu_read_counter(int counter)
{
	u32 val = 0;

	switch (counter) {
	case XSCALE_CYCLE_COUNTER:
		asm volatile("mrc p14, 0, %0, c1, c1, 0" : "=r" (val));
		break;
	case XSCALE_COUNTER0:
		asm volatile("mrc p14, 0, %0, c0, c2, 0" : "=r" (val));
		break;
	case XSCALE_COUNTER1:
		asm volatile("mrc p14, 0, %0, c1, c2, 0" : "=r" (val));
		break;
	case XSCALE_COUNTER2:
		asm volatile("mrc p14, 0, %0, c2, c2, 0" : "=r" (val));
		break;
	case XSCALE_COUNTER3:
		asm volatile("mrc p14, 0, %0, c3, c2, 0" : "=r" (val));
		break;
	}

	return val;
}

static inline void
xscale2pmu_write_counter(int counter, u32 val)
{
	switch (counter) {
	case XSCALE_CYCLE_COUNTER:
		asm volatile("mcr p14, 0, %0, c1, c1, 0" : : "r" (val));
		break;
	case XSCALE_COUNTER0:
		asm volatile("mcr p14, 0, %0, c0, c2, 0" : : "r" (val));
		break;
	case XSCALE_COUNTER1:
		asm volatile("mcr p14, 0, %0, c1, c2, 0" : : "r" (val));
		break;
	case XSCALE_COUNTER2:
		asm volatile("mcr p14, 0, %0, c2, c2, 0" : : "r" (val));
		break;
	case XSCALE_COUNTER3:
		asm volatile("mcr p14, 0, %0, c3, c2, 0" : : "r" (val));
		break;
	}
}

static const struct arm_pmu xscale2pmu = {
	.id		= ARM_PERF_PMU_ID_XSCALE2,
	.handle_irq	= xscale2pmu_handle_irq,
	.enable		= xscale2pmu_enable_event,
	.disable	= xscale2pmu_disable_event,
	.event_map	= xscalepmu_event_map,
	.raw_event	= xscalepmu_raw_event,
	.read_counter	= xscale2pmu_read_counter,
	.write_counter	= xscale2pmu_write_counter,
	.get_event_idx	= xscale2pmu_get_event_idx,
	.start		= xscale2pmu_start,
	.stop		= xscale2pmu_stop,
	.num_events	= 5,
	.max_period	= (1LLU << 32) - 1,
};

static int __init
init_hw_perf_events(void)
{
	unsigned long cpuid = read_cpuid_id();
	unsigned long implementor = (cpuid & 0xFF000000) >> 24;
	unsigned long part_number = (cpuid & 0xFFF0);

	/* ARM Ltd CPUs. */
	if (0x41 == implementor) {
		switch (part_number) {
		case 0xB360:	/* ARM1136 */
		case 0xB560:	/* ARM1156 */
		case 0xB760:	/* ARM1176 */
			armpmu = &armv6pmu;
			memcpy(armpmu_perf_cache_map, armv6_perf_cache_map,
					sizeof(armv6_perf_cache_map));
			break;
		case 0xB020:	/* ARM11mpcore */
			armpmu = &armv6mpcore_pmu;
			memcpy(armpmu_perf_cache_map,
			       armv6mpcore_perf_cache_map,
			       sizeof(armv6mpcore_perf_cache_map));
			break;
		case 0xC080:	/* Cortex-A8 */
			armv7pmu.id = ARM_PERF_PMU_ID_CA8;
			memcpy(armpmu_perf_cache_map, armv7_a8_perf_cache_map,
				sizeof(armv7_a8_perf_cache_map));
			armv7pmu.event_map = armv7_a8_pmu_event_map;
			armpmu = &armv7pmu;

			/* Reset PMNC and read the nb of CNTx counters
			    supported */
			armv7pmu.num_events = armv7_reset_read_pmnc();
			break;
		case 0xC090:	/* Cortex-A9 */
			armv7pmu.id = ARM_PERF_PMU_ID_CA9;
			memcpy(armpmu_perf_cache_map, armv7_a9_perf_cache_map,
				sizeof(armv7_a9_perf_cache_map));
			armv7pmu.event_map = armv7_a9_pmu_event_map;
			armpmu = &armv7pmu;

			/* Reset PMNC and read the nb of CNTx counters
			    supported */
			armv7pmu.num_events = armv7_reset_read_pmnc();
			break;
		}
	/* Intel CPUs [xscale]. */
	} else if (0x69 == implementor) {
		part_number = (cpuid >> 13) & 0x7;
		switch (part_number) {
		case 1:
			armpmu = &xscale1pmu;
			memcpy(armpmu_perf_cache_map, xscale_perf_cache_map,
					sizeof(xscale_perf_cache_map));
			break;
		case 2:
			armpmu = &xscale2pmu;
			memcpy(armpmu_perf_cache_map, xscale_perf_cache_map,
					sizeof(xscale_perf_cache_map));
			break;
		}
	}

	if (armpmu) {
		pr_info("enabled with %s PMU driver, %d counters available\n",
				arm_pmu_names[armpmu->id], armpmu->num_events);
	} else {
		pr_info("no hardware support available\n");
	}

	perf_pmu_register(&pmu);

	return 0;
}
arch_initcall(init_hw_perf_events);

/*
 * Callchain handling code.
 */

/*
 * The registers we're interested in are at the end of the variable
 * length saved register structure. The fp points at the end of this
 * structure so the address of this struct is:
 * (struct frame_tail *)(xxx->fp)-1
 *
 * This code has been adapted from the ARM OProfile support.
 */
struct frame_tail {
	struct frame_tail   *fp;
	unsigned long	    sp;
	unsigned long	    lr;
} __attribute__((packed));

/*
 * Get the return address for a single stackframe and return a pointer to the
 * next frame tail.
 */
static struct frame_tail *
user_backtrace(struct frame_tail *tail,
	       struct perf_callchain_entry *entry)
{
	struct frame_tail buftail;

	/* Also check accessibility of one struct frame_tail beyond */
	if (!access_ok(VERIFY_READ, tail, sizeof(buftail)))
		return NULL;
	if (__copy_from_user_inatomic(&buftail, tail, sizeof(buftail)))
		return NULL;

	perf_callchain_store(entry, buftail.lr);

	/*
	 * Frame pointers should strictly progress back up the stack
	 * (towards higher addresses).
	 */
	if (tail >= buftail.fp)
		return NULL;

	return buftail.fp - 1;
}

void
perf_callchain_user(struct perf_callchain_entry *entry, struct pt_regs *regs)
{
	struct frame_tail *tail;


	tail = (struct frame_tail *)regs->ARM_fp - 1;

	while (tail && !((unsigned long)tail & 0x3))
		tail = user_backtrace(tail, entry);
}

/*
 * Gets called by walk_stackframe() for every stackframe. This will be called
 * whist unwinding the stackframe and is like a subroutine return so we use
 * the PC.
 */
static int
callchain_trace(struct stackframe *fr,
		void *data)
{
	struct perf_callchain_entry *entry = data;
	perf_callchain_store(entry, fr->pc);
	return 0;
}

void
perf_callchain_kernel(struct perf_callchain_entry *entry, struct pt_regs *regs)
{
	struct stackframe fr;

	fr.fp = regs->ARM_fp;
	fr.sp = regs->ARM_sp;
	fr.lr = regs->ARM_lr;
	fr.pc = regs->ARM_pc;
	walk_stackframe(&fr, callchain_trace, entry);
}

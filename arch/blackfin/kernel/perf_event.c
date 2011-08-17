/*
 * Blackfin performance counters
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Ripped from SuperH version:
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
 * Licensed under the GPL-2 or later.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/perf_event.h>
#include <asm/bfin_pfmon.h>

/*
 * We have two counters, and each counter can support an event type.
 * The 'o' is PFCNTx=1 and 's' is PFCNTx=0
 *
 * 0x04 o pc invariant branches
 * 0x06 o mispredicted branches
 * 0x09 o predicted branches taken
 * 0x0B o EXCPT insn
 * 0x0C o CSYNC/SSYNC insn
 * 0x0D o Insns committed
 * 0x0E o Interrupts taken
 * 0x0F o Misaligned address exceptions
 * 0x80 o Code memory fetches stalled due to DMA
 * 0x83 o 64bit insn fetches delivered
 * 0x9A o data cache fills (bank a)
 * 0x9B o data cache fills (bank b)
 * 0x9C o data cache lines evicted (bank a)
 * 0x9D o data cache lines evicted (bank b)
 * 0x9E o data cache high priority fills
 * 0x9F o data cache low priority fills
 * 0x00 s loop 0 iterations
 * 0x01 s loop 1 iterations
 * 0x0A s CSYNC/SSYNC stalls
 * 0x10 s DAG read/after write hazards
 * 0x13 s RAW data hazards
 * 0x81 s code TAG stalls
 * 0x82 s code fill stalls
 * 0x90 s processor to memory stalls
 * 0x91 s data memory stalls not hidden by 0x90
 * 0x92 s data store buffer full stalls
 * 0x93 s data memory write buffer full stalls due to high->low priority
 * 0x95 s data memory fill buffer stalls
 * 0x96 s data TAG collision stalls
 * 0x97 s data collision stalls
 * 0x98 s data stalls
 * 0x99 s data stalls sent to processor
 */

static const int event_map[] = {
	/* use CYCLES cpu register */
	[PERF_COUNT_HW_CPU_CYCLES]          = -1,
	[PERF_COUNT_HW_INSTRUCTIONS]        = 0x0D,
	[PERF_COUNT_HW_CACHE_REFERENCES]    = -1,
	[PERF_COUNT_HW_CACHE_MISSES]        = 0x83,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = 0x09,
	[PERF_COUNT_HW_BRANCH_MISSES]       = 0x06,
	[PERF_COUNT_HW_BUS_CYCLES]          = -1,
};

#define C(x)	PERF_COUNT_HW_CACHE_##x

static const int cache_events[PERF_COUNT_HW_CACHE_MAX]
                             [PERF_COUNT_HW_CACHE_OP_MAX]
                             [PERF_COUNT_HW_CACHE_RESULT_MAX] =
{
	[C(L1D)] = {	/* Data bank A */
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = 0,
			[C(RESULT_MISS)  ] = 0x9A,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = 0,
			[C(RESULT_MISS)  ] = 0,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = 0,
			[C(RESULT_MISS)  ] = 0,
		},
	},

	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = 0,
			[C(RESULT_MISS)  ] = 0x83,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)  ] = -1,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = 0,
			[C(RESULT_MISS)  ] = 0,
		},
	},

	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)  ] = -1,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)  ] = -1,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)  ] = -1,
		},
	},

	[C(DTLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)  ] = -1,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)  ] = -1,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)  ] = -1,
		},
	},

	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)  ] = -1,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)  ] = -1,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)  ] = -1,
		},
	},

	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)  ] = -1,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)  ] = -1,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = -1,
			[C(RESULT_MISS)  ] = -1,
		},
	},
};

const char *perf_pmu_name(void)
{
	return "bfin";
}
EXPORT_SYMBOL(perf_pmu_name);

int perf_num_counters(void)
{
	return ARRAY_SIZE(event_map);
}
EXPORT_SYMBOL(perf_num_counters);

static u64 bfin_pfmon_read(int idx)
{
	return bfin_read32(PFCNTR0 + (idx * 4));
}

static void bfin_pfmon_disable(struct hw_perf_event *hwc, int idx)
{
	bfin_write_PFCTL(bfin_read_PFCTL() & ~PFCEN(idx, PFCEN_MASK));
}

static void bfin_pfmon_enable(struct hw_perf_event *hwc, int idx)
{
	u32 val, mask;

	val = PFPWR;
	if (idx) {
		mask = ~(PFCNT1 | PFMON1 | PFCEN1 | PEMUSW1);
		/* The packed config is for event0, so shift it to event1 slots */
		val |= (hwc->config << (PFMON1_P - PFMON0_P));
		val |= (hwc->config & PFCNT0) << (PFCNT1_P - PFCNT0_P);
		bfin_write_PFCNTR1(0);
	} else {
		mask = ~(PFCNT0 | PFMON0 | PFCEN0 | PEMUSW0);
		val |= hwc->config;
		bfin_write_PFCNTR0(0);
	}

	bfin_write_PFCTL((bfin_read_PFCTL() & mask) | val);
}

static void bfin_pfmon_disable_all(void)
{
	bfin_write_PFCTL(bfin_read_PFCTL() & ~PFPWR);
}

static void bfin_pfmon_enable_all(void)
{
	bfin_write_PFCTL(bfin_read_PFCTL() | PFPWR);
}

struct cpu_hw_events {
	struct perf_event *events[MAX_HWEVENTS];
	unsigned long used_mask[BITS_TO_LONGS(MAX_HWEVENTS)];
};
DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events);

static int hw_perf_cache_event(int config, int *evp)
{
	unsigned long type, op, result;
	int ev;

	/* unpack config */
	type = config & 0xff;
	op = (config >> 8) & 0xff;
	result = (config >> 16) & 0xff;

	if (type >= PERF_COUNT_HW_CACHE_MAX ||
	    op >= PERF_COUNT_HW_CACHE_OP_MAX ||
	    result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return -EINVAL;

	ev = cache_events[type][op][result];
	if (ev == 0)
		return -EOPNOTSUPP;
	if (ev == -1)
		return -EINVAL;
	*evp = ev;
	return 0;
}

static void bfin_perf_event_update(struct perf_event *event,
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
	new_raw_count = bfin_pfmon_read(idx);

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

static void bfin_pmu_stop(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (!(event->hw.state & PERF_HES_STOPPED)) {
		bfin_pfmon_disable(hwc, idx);
		cpuc->events[idx] = NULL;
		event->hw.state |= PERF_HES_STOPPED;
	}

	if ((flags & PERF_EF_UPDATE) && !(event->hw.state & PERF_HES_UPTODATE)) {
		bfin_perf_event_update(event, &event->hw, idx);
		event->hw.state |= PERF_HES_UPTODATE;
	}
}

static void bfin_pmu_start(struct perf_event *event, int flags)
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
	bfin_pfmon_enable(hwc, idx);
}

static void bfin_pmu_del(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	bfin_pmu_stop(event, PERF_EF_UPDATE);
	__clear_bit(event->hw.idx, cpuc->used_mask);

	perf_event_update_userpage(event);
}

static int bfin_pmu_add(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;
	int ret = -EAGAIN;

	perf_pmu_disable(event->pmu);

	if (__test_and_set_bit(idx, cpuc->used_mask)) {
		idx = find_first_zero_bit(cpuc->used_mask, MAX_HWEVENTS);
		if (idx == MAX_HWEVENTS)
			goto out;

		__set_bit(idx, cpuc->used_mask);
		hwc->idx = idx;
	}

	bfin_pfmon_disable(hwc, idx);

	event->hw.state = PERF_HES_UPTODATE | PERF_HES_STOPPED;
	if (flags & PERF_EF_START)
		bfin_pmu_start(event, PERF_EF_RELOAD);

	perf_event_update_userpage(event);
	ret = 0;
out:
	perf_pmu_enable(event->pmu);
	return ret;
}

static void bfin_pmu_read(struct perf_event *event)
{
	bfin_perf_event_update(event, &event->hw, event->hw.idx);
}

static int bfin_pmu_event_init(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	struct hw_perf_event *hwc = &event->hw;
	int config = -1;
	int ret;

	if (attr->exclude_hv || attr->exclude_idle)
		return -EPERM;

	/*
	 * All of the on-chip counters are "limited", in that they have
	 * no interrupts, and are therefore unable to do sampling without
	 * further work and timer assistance.
	 */
	if (hwc->sample_period)
		return -EINVAL;

	ret = 0;
	switch (attr->type) {
	case PERF_TYPE_RAW:
		config = PFMON(0, attr->config & PFMON_MASK) |
			PFCNT(0, !(attr->config & 0x100));
		break;
	case PERF_TYPE_HW_CACHE:
		ret = hw_perf_cache_event(attr->config, &config);
		break;
	case PERF_TYPE_HARDWARE:
		if (attr->config >= ARRAY_SIZE(event_map))
			return -EINVAL;

		config = event_map[attr->config];
		break;
	}

	if (config == -1)
		return -EINVAL;

	if (!attr->exclude_kernel)
		config |= PFCEN(0, PFCEN_ENABLE_SUPV);
	if (!attr->exclude_user)
		config |= PFCEN(0, PFCEN_ENABLE_USER);

	hwc->config |= config;

	return ret;
}

static void bfin_pmu_enable(struct pmu *pmu)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct perf_event *event;
	struct hw_perf_event *hwc;
	int i;

	for (i = 0; i < MAX_HWEVENTS; ++i) {
		event = cpuc->events[i];
		if (!event)
			continue;
		hwc = &event->hw;
		bfin_pfmon_enable(hwc, hwc->idx);
	}

	bfin_pfmon_enable_all();
}

static void bfin_pmu_disable(struct pmu *pmu)
{
	bfin_pfmon_disable_all();
}

static struct pmu pmu = {
	.pmu_enable  = bfin_pmu_enable,
	.pmu_disable = bfin_pmu_disable,
	.event_init  = bfin_pmu_event_init,
	.add         = bfin_pmu_add,
	.del         = bfin_pmu_del,
	.start       = bfin_pmu_start,
	.stop        = bfin_pmu_stop,
	.read        = bfin_pmu_read,
};

static void bfin_pmu_setup(int cpu)
{
	struct cpu_hw_events *cpuhw = &per_cpu(cpu_hw_events, cpu);

	memset(cpuhw, 0, sizeof(struct cpu_hw_events));
}

static int __cpuinit
bfin_pmu_notifier(struct notifier_block *self, unsigned long action, void *hcpu)
{
	unsigned int cpu = (long)hcpu;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_UP_PREPARE:
		bfin_write_PFCTL(0);
		bfin_pmu_setup(cpu);
		break;

	default:
		break;
	}

	return NOTIFY_OK;
}

static int __init bfin_pmu_init(void)
{
	int ret;

	ret = perf_pmu_register(&pmu, "cpu", PERF_TYPE_RAW);
	if (!ret)
		perf_cpu_notifier(bfin_pmu_notifier);

	return ret;
}
early_initcall(bfin_pmu_init);

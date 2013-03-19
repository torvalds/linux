/*
 * Meta performance counter support.
 *  Copyright (C) 2012 Imagination Technologies Ltd
 *
 * This code is based on the sh pmu code:
 *  Copyright (C) 2009 Paul Mundt
 *
 * and on the arm pmu code:
 *  Copyright (C) 2009 picoChip Designs, Ltd., James Iles
 *  Copyright (C) 2010 ARM Ltd., Will Deacon <will.deacon@arm.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/atomic.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/irqchip/metag.h>
#include <linux/perf_event.h>
#include <linux/slab.h>

#include <asm/core_reg.h>
#include <asm/hwthread.h>
#include <asm/io.h>
#include <asm/irq.h>

#include "perf_event.h"

static int _hw_perf_event_init(struct perf_event *);
static void _hw_perf_event_destroy(struct perf_event *);

/* Determines which core type we are */
static struct metag_pmu *metag_pmu __read_mostly;

/* Processor specific data */
static DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events);

/* PMU admin */
const char *perf_pmu_name(void)
{
	if (metag_pmu)
		return metag_pmu->pmu.name;

	return NULL;
}
EXPORT_SYMBOL_GPL(perf_pmu_name);

int perf_num_counters(void)
{
	if (metag_pmu)
		return metag_pmu->max_events;

	return 0;
}
EXPORT_SYMBOL_GPL(perf_num_counters);

static inline int metag_pmu_initialised(void)
{
	return !!metag_pmu;
}

static void release_pmu_hardware(void)
{
	int irq;
	unsigned int version = (metag_pmu->version &
			(METAC_ID_MINOR_BITS | METAC_ID_REV_BITS)) >>
			METAC_ID_REV_S;

	/* Early cores don't have overflow interrupts */
	if (version < 0x0104)
		return;

	irq = internal_irq_map(17);
	if (irq >= 0)
		free_irq(irq, (void *)1);

	irq = internal_irq_map(16);
	if (irq >= 0)
		free_irq(irq, (void *)0);
}

static int reserve_pmu_hardware(void)
{
	int err = 0, irq[2];
	unsigned int version = (metag_pmu->version &
			(METAC_ID_MINOR_BITS | METAC_ID_REV_BITS)) >>
			METAC_ID_REV_S;

	/* Early cores don't have overflow interrupts */
	if (version < 0x0104)
		goto out;

	/*
	 * Bit 16 on HWSTATMETA is the interrupt for performance counter 0;
	 * similarly, 17 is the interrupt for performance counter 1.
	 * We can't (yet) interrupt on the cycle counter, because it's a
	 * register, however it holds a 32-bit value as opposed to 24-bit.
	 */
	irq[0] = internal_irq_map(16);
	if (irq[0] < 0) {
		pr_err("unable to map internal IRQ %d\n", 16);
		goto out;
	}
	err = request_irq(irq[0], metag_pmu->handle_irq, IRQF_NOBALANCING,
			"metagpmu0", (void *)0);
	if (err) {
		pr_err("unable to request IRQ%d for metag PMU counters\n",
				irq[0]);
		goto out;
	}

	irq[1] = internal_irq_map(17);
	if (irq[1] < 0) {
		pr_err("unable to map internal IRQ %d\n", 17);
		goto out_irq1;
	}
	err = request_irq(irq[1], metag_pmu->handle_irq, IRQF_NOBALANCING,
			"metagpmu1", (void *)1);
	if (err) {
		pr_err("unable to request IRQ%d for metag PMU counters\n",
				irq[1]);
		goto out_irq1;
	}

	return 0;

out_irq1:
	free_irq(irq[0], (void *)0);
out:
	return err;
}

/* PMU operations */
static void metag_pmu_enable(struct pmu *pmu)
{
}

static void metag_pmu_disable(struct pmu *pmu)
{
}

static int metag_pmu_event_init(struct perf_event *event)
{
	int err = 0;
	atomic_t *active_events = &metag_pmu->active_events;

	if (!metag_pmu_initialised()) {
		err = -ENODEV;
		goto out;
	}

	if (has_branch_stack(event))
		return -EOPNOTSUPP;

	event->destroy = _hw_perf_event_destroy;

	if (!atomic_inc_not_zero(active_events)) {
		mutex_lock(&metag_pmu->reserve_mutex);
		if (atomic_read(active_events) == 0)
			err = reserve_pmu_hardware();

		if (!err)
			atomic_inc(active_events);

		mutex_unlock(&metag_pmu->reserve_mutex);
	}

	/* Hardware and caches counters */
	switch (event->attr.type) {
	case PERF_TYPE_HARDWARE:
	case PERF_TYPE_HW_CACHE:
		err = _hw_perf_event_init(event);
		break;

	default:
		return -ENOENT;
	}

	if (err)
		event->destroy(event);

out:
	return err;
}

void metag_pmu_event_update(struct perf_event *event,
		struct hw_perf_event *hwc, int idx)
{
	u64 prev_raw_count, new_raw_count;
	s64 delta;

	/*
	 * If this counter is chained, it may be that the previous counter
	 * value has been changed beneath us.
	 *
	 * To get around this, we read and exchange the new raw count, then
	 * add the delta (new - prev) to the generic counter atomically.
	 *
	 * Without interrupts, this is the simplest approach.
	 */
again:
	prev_raw_count = local64_read(&hwc->prev_count);
	new_raw_count = metag_pmu->read(idx);

	if (local64_cmpxchg(&hwc->prev_count, prev_raw_count,
			new_raw_count) != prev_raw_count)
		goto again;

	/*
	 * Calculate the delta and add it to the counter.
	 */
	delta = new_raw_count - prev_raw_count;

	local64_add(delta, &event->count);
}

int metag_pmu_event_set_period(struct perf_event *event,
		struct hw_perf_event *hwc, int idx)
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

	if (left > (s64)metag_pmu->max_period)
		left = metag_pmu->max_period;

	if (metag_pmu->write)
		metag_pmu->write(idx, (u64)(-left) & MAX_PERIOD);

	perf_event_update_userpage(event);

	return ret;
}

static void metag_pmu_start(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (WARN_ON_ONCE(idx == -1))
		return;

	/*
	 * We always have to reprogram the period, so ignore PERF_EF_RELOAD.
	 */
	if (flags & PERF_EF_RELOAD)
		WARN_ON_ONCE(!(hwc->state & PERF_HES_UPTODATE));

	hwc->state = 0;

	/*
	 * Reset the period.
	 * Some counters can't be stopped (i.e. are core global), so when the
	 * counter was 'stopped' we merely disabled the IRQ. If we don't reset
	 * the period, then we'll either: a) get an overflow too soon;
	 * or b) too late if the overflow happened since disabling.
	 * Obviously, this has little bearing on cores without the overflow
	 * interrupt, as the performance counter resets to zero on write
	 * anyway.
	 */
	if (metag_pmu->max_period)
		metag_pmu_event_set_period(event, hwc, hwc->idx);
	cpuc->events[idx] = event;
	metag_pmu->enable(hwc, idx);
}

static void metag_pmu_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;

	/*
	 * We should always update the counter on stop; see comment above
	 * why.
	 */
	if (!(hwc->state & PERF_HES_STOPPED)) {
		metag_pmu_event_update(event, hwc, hwc->idx);
		metag_pmu->disable(hwc, hwc->idx);
		hwc->state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
	}
}

static int metag_pmu_add(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	int idx = 0, ret = 0;

	perf_pmu_disable(event->pmu);

	/* check whether we're counting instructions */
	if (hwc->config == 0x100) {
		if (__test_and_set_bit(METAG_INST_COUNTER,
				cpuc->used_mask)) {
			ret = -EAGAIN;
			goto out;
		}
		idx = METAG_INST_COUNTER;
	} else {
		/* Check whether we have a spare counter */
		idx = find_first_zero_bit(cpuc->used_mask,
				atomic_read(&metag_pmu->active_events));
		if (idx >= METAG_INST_COUNTER) {
			ret = -EAGAIN;
			goto out;
		}

		__set_bit(idx, cpuc->used_mask);
	}
	hwc->idx = idx;

	/* Make sure the counter is disabled */
	metag_pmu->disable(hwc, idx);

	hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;
	if (flags & PERF_EF_START)
		metag_pmu_start(event, PERF_EF_RELOAD);

	perf_event_update_userpage(event);
out:
	perf_pmu_enable(event->pmu);
	return ret;
}

static void metag_pmu_del(struct perf_event *event, int flags)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	WARN_ON(idx < 0);
	metag_pmu_stop(event, PERF_EF_UPDATE);
	cpuc->events[idx] = NULL;
	__clear_bit(idx, cpuc->used_mask);

	perf_event_update_userpage(event);
}

static void metag_pmu_read(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	/* Don't read disabled counters! */
	if (hwc->idx < 0)
		return;

	metag_pmu_event_update(event, hwc, hwc->idx);
}

static struct pmu pmu = {
	.pmu_enable	= metag_pmu_enable,
	.pmu_disable	= metag_pmu_disable,

	.event_init	= metag_pmu_event_init,

	.add		= metag_pmu_add,
	.del		= metag_pmu_del,
	.start		= metag_pmu_start,
	.stop		= metag_pmu_stop,
	.read		= metag_pmu_read,
};

/* Core counter specific functions */
static const int metag_general_events[] = {
	[PERF_COUNT_HW_CPU_CYCLES] = 0x03,
	[PERF_COUNT_HW_INSTRUCTIONS] = 0x100,
	[PERF_COUNT_HW_CACHE_REFERENCES] = -1,
	[PERF_COUNT_HW_CACHE_MISSES] = -1,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = -1,
	[PERF_COUNT_HW_BRANCH_MISSES] = -1,
	[PERF_COUNT_HW_BUS_CYCLES] = -1,
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND] = -1,
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND] = -1,
	[PERF_COUNT_HW_REF_CPU_CYCLES] = -1,
};

static const int metag_pmu_cache_events[C(MAX)][C(OP_MAX)][C(RESULT_MAX)] = {
	[C(L1D)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = 0x08,
			[C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
		},
	},
	[C(L1I)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = 0x09,
			[C(RESULT_MISS)] = 0x0a,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
		},
	},
	[C(LL)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
		},
	},
	[C(DTLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = 0xd0,
			[C(RESULT_MISS)] = 0xd2,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = 0xd4,
			[C(RESULT_MISS)] = 0xd5,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
		},
	},
	[C(ITLB)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = 0xd1,
			[C(RESULT_MISS)] = 0xd3,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
		},
	},
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
		},
	},
	[C(NODE)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)] = CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)] = CACHE_OP_UNSUPPORTED,
		},
	},
};


static void _hw_perf_event_destroy(struct perf_event *event)
{
	atomic_t *active_events = &metag_pmu->active_events;
	struct mutex *pmu_mutex = &metag_pmu->reserve_mutex;

	if (atomic_dec_and_mutex_lock(active_events, pmu_mutex)) {
		release_pmu_hardware();
		mutex_unlock(pmu_mutex);
	}
}

static int _hw_perf_cache_event(int config, int *evp)
{
	unsigned long type, op, result;
	int ev;

	if (!metag_pmu->cache_events)
		return -EINVAL;

	/* Unpack config */
	type = config & 0xff;
	op = (config >> 8) & 0xff;
	result = (config >> 16) & 0xff;

	if (type >= PERF_COUNT_HW_CACHE_MAX ||
			op >= PERF_COUNT_HW_CACHE_OP_MAX ||
			result >= PERF_COUNT_HW_CACHE_RESULT_MAX)
		return -EINVAL;

	ev = (*metag_pmu->cache_events)[type][op][result];
	if (ev == 0)
		return -EOPNOTSUPP;
	if (ev == -1)
		return -EINVAL;
	*evp = ev;
	return 0;
}

static int _hw_perf_event_init(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	struct hw_perf_event *hwc = &event->hw;
	int mapping = 0, err;

	switch (attr->type) {
	case PERF_TYPE_HARDWARE:
		if (attr->config >= PERF_COUNT_HW_MAX)
			return -EINVAL;

		mapping = metag_pmu->event_map(attr->config);
		break;

	case PERF_TYPE_HW_CACHE:
		err = _hw_perf_cache_event(attr->config, &mapping);
		if (err)
			return err;
		break;
	}

	/* Return early if the event is unsupported */
	if (mapping == -1)
		return -EINVAL;

	/*
	 * Early cores have "limited" counters - they have no overflow
	 * interrupts - and so are unable to do sampling without extra work
	 * and timer assistance.
	 */
	if (metag_pmu->max_period == 0) {
		if (hwc->sample_period)
			return -EINVAL;
	}

	/*
	 * Don't assign an index until the event is placed into the hardware.
	 * -1 signifies that we're still deciding where to put it. On SMP
	 * systems each core has its own set of counters, so we can't do any
	 * constraint checking yet.
	 */
	hwc->idx = -1;

	/* Store the event encoding */
	hwc->config |= (unsigned long)mapping;

	/*
	 * For non-sampling runs, limit the sample_period to half of the
	 * counter width. This way, the new counter value should be less
	 * likely to overtake the previous one (unless there are IRQ latency
	 * issues...)
	 */
	if (metag_pmu->max_period) {
		if (!hwc->sample_period) {
			hwc->sample_period = metag_pmu->max_period >> 1;
			hwc->last_period = hwc->sample_period;
			local64_set(&hwc->period_left, hwc->sample_period);
		}
	}

	return 0;
}

static void metag_pmu_enable_counter(struct hw_perf_event *event, int idx)
{
	struct cpu_hw_events *events = &__get_cpu_var(cpu_hw_events);
	unsigned int config = event->config;
	unsigned int tmp = config & 0xf0;
	unsigned long flags;

	raw_spin_lock_irqsave(&events->pmu_lock, flags);

	/*
	 * Check if we're enabling the instruction counter (index of
	 * MAX_HWEVENTS - 1)
	 */
	if (METAG_INST_COUNTER == idx) {
		WARN_ONCE((config != 0x100),
			"invalid configuration (%d) for counter (%d)\n",
			config, idx);

		/* Reset the cycle count */
		__core_reg_set(TXTACTCYC, 0);
		goto unlock;
	}

	/* Check for a core internal or performance channel event. */
	if (tmp) {
		void *perf_addr = (void *)PERF_COUNT(idx);

		/*
		 * Anything other than a cycle count will write the low-
		 * nibble to the correct counter register.
		 */
		switch (tmp) {
		case 0xd0:
			perf_addr = (void *)PERF_ICORE(idx);
			break;

		case 0xf0:
			perf_addr = (void *)PERF_CHAN(idx);
			break;
		}

		metag_out32((tmp & 0x0f), perf_addr);

		/*
		 * Now we use the high nibble as the performance event to
		 * to count.
		 */
		config = tmp >> 4;
	}

	/*
	 * Enabled counters start from 0. Early cores clear the count on
	 * write but newer cores don't, so we make sure that the count is
	 * set to 0.
	 */
	tmp = ((config & 0xf) << 28) |
			((1 << 24) << cpu_2_hwthread_id[get_cpu()]);
	metag_out32(tmp, PERF_COUNT(idx));
unlock:
	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static void metag_pmu_disable_counter(struct hw_perf_event *event, int idx)
{
	struct cpu_hw_events *events = &__get_cpu_var(cpu_hw_events);
	unsigned int tmp = 0;
	unsigned long flags;

	/*
	 * The cycle counter can't be disabled per se, as it's a hardware
	 * thread register which is always counting. We merely return if this
	 * is the counter we're attempting to disable.
	 */
	if (METAG_INST_COUNTER == idx)
		return;

	/*
	 * The counter value _should_ have been read prior to disabling,
	 * as if we're running on an early core then the value gets reset to
	 * 0, and any read after that would be useless. On the newer cores,
	 * however, it's better to read-modify-update this for purposes of
	 * the overflow interrupt.
	 * Here we remove the thread id AND the event nibble (there are at
	 * least two events that count events that are core global and ignore
	 * the thread id mask). This only works because we don't mix thread
	 * performance counts, and event 0x00 requires a thread id mask!
	 */
	raw_spin_lock_irqsave(&events->pmu_lock, flags);

	tmp = metag_in32(PERF_COUNT(idx));
	tmp &= 0x00ffffff;
	metag_out32(tmp, PERF_COUNT(idx));

	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static u64 metag_pmu_read_counter(int idx)
{
	u32 tmp = 0;

	/* The act of reading the cycle counter also clears it */
	if (METAG_INST_COUNTER == idx) {
		__core_reg_swap(TXTACTCYC, tmp);
		goto out;
	}

	tmp = metag_in32(PERF_COUNT(idx)) & 0x00ffffff;
out:
	return tmp;
}

static void metag_pmu_write_counter(int idx, u32 val)
{
	struct cpu_hw_events *events = &__get_cpu_var(cpu_hw_events);
	u32 tmp = 0;
	unsigned long flags;

	/*
	 * This _shouldn't_ happen, but if it does, then we can just
	 * ignore the write, as the register is read-only and clear-on-write.
	 */
	if (METAG_INST_COUNTER == idx)
		return;

	/*
	 * We'll keep the thread mask and event id, and just update the
	 * counter itself. Also , we should bound the value to 24-bits.
	 */
	raw_spin_lock_irqsave(&events->pmu_lock, flags);

	val &= 0x00ffffff;
	tmp = metag_in32(PERF_COUNT(idx)) & 0xff000000;
	val |= tmp;
	metag_out32(val, PERF_COUNT(idx));

	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static int metag_pmu_event_map(int idx)
{
	return metag_general_events[idx];
}

static irqreturn_t metag_pmu_counter_overflow(int irq, void *dev)
{
	int idx = (int)dev;
	struct cpu_hw_events *cpuhw = &__get_cpu_var(cpu_hw_events);
	struct perf_event *event = cpuhw->events[idx];
	struct hw_perf_event *hwc = &event->hw;
	struct pt_regs *regs = get_irq_regs();
	struct perf_sample_data sampledata;
	unsigned long flags;
	u32 counter = 0;

	/*
	 * We need to stop the core temporarily from generating another
	 * interrupt while we disable this counter. However, we don't want
	 * to flag the counter as free
	 */
	__global_lock2(flags);
	counter = metag_in32(PERF_COUNT(idx));
	metag_out32((counter & 0x00ffffff), PERF_COUNT(idx));
	__global_unlock2(flags);

	/* Update the counts and reset the sample period */
	metag_pmu_event_update(event, hwc, idx);
	perf_sample_data_init(&sampledata, 0, hwc->last_period);
	metag_pmu_event_set_period(event, hwc, idx);

	/*
	 * Enable the counter again once core overflow processing has
	 * completed.
	 */
	if (!perf_event_overflow(event, &sampledata, regs))
		metag_out32(counter, PERF_COUNT(idx));

	return IRQ_HANDLED;
}

static struct metag_pmu _metag_pmu = {
	.handle_irq	= metag_pmu_counter_overflow,
	.enable		= metag_pmu_enable_counter,
	.disable	= metag_pmu_disable_counter,
	.read		= metag_pmu_read_counter,
	.write		= metag_pmu_write_counter,
	.event_map	= metag_pmu_event_map,
	.cache_events	= &metag_pmu_cache_events,
	.max_period	= MAX_PERIOD,
	.max_events	= MAX_HWEVENTS,
};

/* PMU CPU hotplug notifier */
static int __cpuinit metag_pmu_cpu_notify(struct notifier_block *b,
		unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned int)hcpu;
	struct cpu_hw_events *cpuc = &per_cpu(cpu_hw_events, cpu);

	if ((action & ~CPU_TASKS_FROZEN) != CPU_STARTING)
		return NOTIFY_DONE;

	memset(cpuc, 0, sizeof(struct cpu_hw_events));
	raw_spin_lock_init(&cpuc->pmu_lock);

	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata metag_pmu_notifier = {
	.notifier_call = metag_pmu_cpu_notify,
};

/* PMU Initialisation */
static int __init init_hw_perf_events(void)
{
	int ret = 0, cpu;
	u32 version = *(u32 *)METAC_ID;
	int major = (version & METAC_ID_MAJOR_BITS) >> METAC_ID_MAJOR_S;
	int min_rev = (version & (METAC_ID_MINOR_BITS | METAC_ID_REV_BITS))
			>> METAC_ID_REV_S;

	/* Not a Meta 2 core, then not supported */
	if (0x02 > major) {
		pr_info("no hardware counter support available\n");
		goto out;
	} else if (0x02 == major) {
		metag_pmu = &_metag_pmu;

		if (min_rev < 0x0104) {
			/*
			 * A core without overflow interrupts, and clear-on-
			 * write counters.
			 */
			metag_pmu->handle_irq = NULL;
			metag_pmu->write = NULL;
			metag_pmu->max_period = 0;
		}

		metag_pmu->name = "Meta 2";
		metag_pmu->version = version;
		metag_pmu->pmu = pmu;
	}

	pr_info("enabled with %s PMU driver, %d counters available\n",
			metag_pmu->name, metag_pmu->max_events);

	/* Initialise the active events and reservation mutex */
	atomic_set(&metag_pmu->active_events, 0);
	mutex_init(&metag_pmu->reserve_mutex);

	/* Clear the counters */
	metag_out32(0, PERF_COUNT(0));
	metag_out32(0, PERF_COUNT(1));

	for_each_possible_cpu(cpu) {
		struct cpu_hw_events *cpuc = &per_cpu(cpu_hw_events, cpu);

		memset(cpuc, 0, sizeof(struct cpu_hw_events));
		raw_spin_lock_init(&cpuc->pmu_lock);
	}

	register_cpu_notifier(&metag_pmu_notifier);
	ret = perf_pmu_register(&pmu, (char *)metag_pmu->name, PERF_TYPE_RAW);
out:
	return ret;
}
early_initcall(init_hw_perf_events);

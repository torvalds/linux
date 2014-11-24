/*
 * PMU support
 *
 * Copyright (C) 2012 ARM Limited
 * Author: Will Deacon <will.deacon@arm.com>
 *
 * This code is based heavily on the ARMv7 perf event code.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#define pr_fmt(fmt) "hw perfevents: " fmt

#include <linux/bitmap.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/perf_event.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>

#include <asm/cputype.h>
#include <asm/irq.h>
#include <asm/irq_regs.h>
#include <asm/pmu.h>
#include <asm/stacktrace.h>

/*
 * ARMv8 supports a maximum of 32 events.
 * The cycle counter is included in this total.
 */
#define ARMPMU_MAX_HWEVENTS		32

static DEFINE_PER_CPU(struct perf_event * [ARMPMU_MAX_HWEVENTS], hw_events);
static DEFINE_PER_CPU(unsigned long [BITS_TO_LONGS(ARMPMU_MAX_HWEVENTS)], used_mask);
static DEFINE_PER_CPU(struct pmu_hw_events, cpu_hw_events);

#define to_arm_pmu(p) (container_of(p, struct arm_pmu, pmu))

/* Set at runtime when we know what CPU type we are. */
static struct arm_pmu *cpu_pmu;

int
armpmu_get_max_events(void)
{
	int max_events = 0;

	if (cpu_pmu != NULL)
		max_events = cpu_pmu->num_events;

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

static int
armpmu_map_cache_event(const unsigned (*cache_map)
				      [PERF_COUNT_HW_CACHE_MAX]
				      [PERF_COUNT_HW_CACHE_OP_MAX]
				      [PERF_COUNT_HW_CACHE_RESULT_MAX],
		       u64 config)
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

	ret = (int)(*cache_map)[cache_type][cache_op][cache_result];

	if (ret == CACHE_OP_UNSUPPORTED)
		return -ENOENT;

	return ret;
}

static int
armpmu_map_event(const unsigned (*event_map)[PERF_COUNT_HW_MAX], u64 config)
{
	int mapping;

	if (config >= PERF_COUNT_HW_MAX)
		return -EINVAL;

	mapping = (*event_map)[config];
	return mapping == HW_OP_UNSUPPORTED ? -ENOENT : mapping;
}

static int
armpmu_map_raw_event(u32 raw_event_mask, u64 config)
{
	return (int)(config & raw_event_mask);
}

static int map_cpu_event(struct perf_event *event,
			 const unsigned (*event_map)[PERF_COUNT_HW_MAX],
			 const unsigned (*cache_map)
					[PERF_COUNT_HW_CACHE_MAX]
					[PERF_COUNT_HW_CACHE_OP_MAX]
					[PERF_COUNT_HW_CACHE_RESULT_MAX],
			 u32 raw_event_mask)
{
	u64 config = event->attr.config;

	switch (event->attr.type) {
	case PERF_TYPE_HARDWARE:
		return armpmu_map_event(event_map, config);
	case PERF_TYPE_HW_CACHE:
		return armpmu_map_cache_event(cache_map, config);
	case PERF_TYPE_RAW:
		return armpmu_map_raw_event(raw_event_mask, config);
	}

	return -ENOENT;
}

int
armpmu_event_set_period(struct perf_event *event,
			struct hw_perf_event *hwc,
			int idx)
{
	struct arm_pmu *armpmu = to_arm_pmu(event->pmu);
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

u64
armpmu_event_update(struct perf_event *event,
		    struct hw_perf_event *hwc,
		    int idx)
{
	struct arm_pmu *armpmu = to_arm_pmu(event->pmu);
	u64 delta, prev_raw_count, new_raw_count;

again:
	prev_raw_count = local64_read(&hwc->prev_count);
	new_raw_count = armpmu->read_counter(idx);

	if (local64_cmpxchg(&hwc->prev_count, prev_raw_count,
			     new_raw_count) != prev_raw_count)
		goto again;

	delta = (new_raw_count - prev_raw_count) & armpmu->max_period;

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
	struct arm_pmu *armpmu = to_arm_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

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
	struct arm_pmu *armpmu = to_arm_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

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
	struct arm_pmu *armpmu = to_arm_pmu(event->pmu);
	struct pmu_hw_events *hw_events = armpmu->get_hw_events();
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	WARN_ON(idx < 0);

	armpmu_stop(event, PERF_EF_UPDATE);
	hw_events->events[idx] = NULL;
	clear_bit(idx, hw_events->used_mask);

	perf_event_update_userpage(event);
}

static int
armpmu_add(struct perf_event *event, int flags)
{
	struct arm_pmu *armpmu = to_arm_pmu(event->pmu);
	struct pmu_hw_events *hw_events = armpmu->get_hw_events();
	struct hw_perf_event *hwc = &event->hw;
	int idx;
	int err = 0;

	perf_pmu_disable(event->pmu);

	/* If we don't have a space for the counter then finish early. */
	idx = armpmu->get_event_idx(hw_events, hwc);
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
	hw_events->events[idx] = event;

	hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;
	if (flags & PERF_EF_START)
		armpmu_start(event, PERF_EF_RELOAD);

	/* Propagate our changes to the userspace mapping. */
	perf_event_update_userpage(event);

out:
	perf_pmu_enable(event->pmu);
	return err;
}

static int
validate_event(struct pmu_hw_events *hw_events,
	       struct perf_event *event)
{
	struct arm_pmu *armpmu = to_arm_pmu(event->pmu);
	struct hw_perf_event fake_event = event->hw;
	struct pmu *leader_pmu = event->group_leader->pmu;

	if (is_software_event(event))
		return 1;

	if (event->pmu != leader_pmu || event->state < PERF_EVENT_STATE_OFF)
		return 1;

	if (event->state == PERF_EVENT_STATE_OFF && !event->attr.enable_on_exec)
		return 1;

	return armpmu->get_event_idx(hw_events, &fake_event) >= 0;
}

static int
validate_group(struct perf_event *event)
{
	struct perf_event *sibling, *leader = event->group_leader;
	struct pmu_hw_events fake_pmu;
	DECLARE_BITMAP(fake_used_mask, ARMPMU_MAX_HWEVENTS);

	/*
	 * Initialise the fake PMU. We only need to populate the
	 * used_mask for the purposes of validation.
	 */
	memset(fake_used_mask, 0, sizeof(fake_used_mask));
	fake_pmu.used_mask = fake_used_mask;

	if (!validate_event(&fake_pmu, leader))
		return -EINVAL;

	list_for_each_entry(sibling, &leader->sibling_list, group_entry) {
		if (!validate_event(&fake_pmu, sibling))
			return -EINVAL;
	}

	if (!validate_event(&fake_pmu, event))
		return -EINVAL;

	return 0;
}

static void
armpmu_disable_percpu_irq(void *data)
{
	unsigned int irq = *(unsigned int *)data;
	disable_percpu_irq(irq);
}

static void
armpmu_release_hardware(struct arm_pmu *armpmu)
{
	int irq;
	unsigned int i, irqs;
	struct platform_device *pmu_device = armpmu->plat_device;

	irqs = min(pmu_device->num_resources, num_possible_cpus());
	if (!irqs)
		return;

	irq = platform_get_irq(pmu_device, 0);
	if (irq <= 0)
		return;

	if (irq_is_percpu(irq)) {
		on_each_cpu(armpmu_disable_percpu_irq, &irq, 1);
		free_percpu_irq(irq, &cpu_hw_events);
	} else {
		for (i = 0; i < irqs; ++i) {
			if (!cpumask_test_and_clear_cpu(i, &armpmu->active_irqs))
				continue;
			irq = platform_get_irq(pmu_device, i);
			if (irq > 0)
				free_irq(irq, armpmu);
		}
	}
}

static void
armpmu_enable_percpu_irq(void *data)
{
	unsigned int irq = *(unsigned int *)data;
	enable_percpu_irq(irq, IRQ_TYPE_NONE);
}

static int
armpmu_reserve_hardware(struct arm_pmu *armpmu)
{
	int err, irq;
	unsigned int i, irqs;
	struct platform_device *pmu_device = armpmu->plat_device;

	if (!pmu_device) {
		pr_err("no PMU device registered\n");
		return -ENODEV;
	}

	irqs = min(pmu_device->num_resources, num_possible_cpus());
	if (!irqs) {
		pr_err("no irqs for PMUs defined\n");
		return -ENODEV;
	}

	irq = platform_get_irq(pmu_device, 0);
	if (irq <= 0) {
		pr_err("failed to get valid irq for PMU device\n");
		return -ENODEV;
	}

	if (irq_is_percpu(irq)) {
		err = request_percpu_irq(irq, armpmu->handle_irq,
				"arm-pmu", &cpu_hw_events);

		if (err) {
			pr_err("unable to request percpu IRQ%d for ARM PMU counters\n",
					irq);
			armpmu_release_hardware(armpmu);
			return err;
		}

		on_each_cpu(armpmu_enable_percpu_irq, &irq, 1);
	} else {
		for (i = 0; i < irqs; ++i) {
			err = 0;
			irq = platform_get_irq(pmu_device, i);
			if (irq <= 0)
				continue;

			/*
			 * If we have a single PMU interrupt that we can't shift,
			 * assume that we're running on a uniprocessor machine and
			 * continue. Otherwise, continue without this interrupt.
			 */
			if (irq_set_affinity(irq, cpumask_of(i)) && irqs > 1) {
				pr_warning("unable to set irq affinity (irq=%d, cpu=%u)\n",
						irq, i);
				continue;
			}

			err = request_irq(irq, armpmu->handle_irq,
					IRQF_NOBALANCING,
					"arm-pmu", armpmu);
			if (err) {
				pr_err("unable to request IRQ%d for ARM PMU counters\n",
						irq);
				armpmu_release_hardware(armpmu);
				return err;
			}

			cpumask_set_cpu(i, &armpmu->active_irqs);
		}
	}

	return 0;
}

static void
hw_perf_event_destroy(struct perf_event *event)
{
	struct arm_pmu *armpmu = to_arm_pmu(event->pmu);
	atomic_t *active_events	 = &armpmu->active_events;
	struct mutex *pmu_reserve_mutex = &armpmu->reserve_mutex;

	if (atomic_dec_and_mutex_lock(active_events, pmu_reserve_mutex)) {
		armpmu_release_hardware(armpmu);
		mutex_unlock(pmu_reserve_mutex);
	}
}

static int
event_requires_mode_exclusion(struct perf_event_attr *attr)
{
	return attr->exclude_idle || attr->exclude_user ||
	       attr->exclude_kernel || attr->exclude_hv;
}

static int
__hw_perf_event_init(struct perf_event *event)
{
	struct arm_pmu *armpmu = to_arm_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int mapping, err;

	mapping = armpmu->map_event(event);

	if (mapping < 0) {
		pr_debug("event %x:%llx not supported\n", event->attr.type,
			 event->attr.config);
		return mapping;
	}

	/*
	 * We don't assign an index until we actually place the event onto
	 * hardware. Use -1 to signify that we haven't decided where to put it
	 * yet. For SMP systems, each core has it's own PMU so we can't do any
	 * clever allocation or constraints checking at this point.
	 */
	hwc->idx		= -1;
	hwc->config_base	= 0;
	hwc->config		= 0;
	hwc->event_base		= 0;

	/*
	 * Check whether we need to exclude the counter from certain modes.
	 */
	if ((!armpmu->set_event_filter ||
	     armpmu->set_event_filter(hwc, &event->attr)) &&
	     event_requires_mode_exclusion(&event->attr)) {
		pr_debug("ARM performance counters do not support mode exclusion\n");
		return -EPERM;
	}

	/*
	 * Store the event encoding into the config_base field.
	 */
	hwc->config_base	    |= (unsigned long)mapping;

	if (!hwc->sample_period) {
		/*
		 * For non-sampling runs, limit the sample_period to half
		 * of the counter width. That way, the new counter value
		 * is far less likely to overtake the previous one unless
		 * you have some serious IRQ latency issues.
		 */
		hwc->sample_period  = armpmu->max_period >> 1;
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
	struct arm_pmu *armpmu = to_arm_pmu(event->pmu);
	int err = 0;
	atomic_t *active_events = &armpmu->active_events;

	if (armpmu->map_event(event) == -ENOENT)
		return -ENOENT;

	event->destroy = hw_perf_event_destroy;

	if (!atomic_inc_not_zero(active_events)) {
		mutex_lock(&armpmu->reserve_mutex);
		if (atomic_read(active_events) == 0)
			err = armpmu_reserve_hardware(armpmu);

		if (!err)
			atomic_inc(active_events);
		mutex_unlock(&armpmu->reserve_mutex);
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
	struct arm_pmu *armpmu = to_arm_pmu(pmu);
	struct pmu_hw_events *hw_events = armpmu->get_hw_events();
	int enabled = bitmap_weight(hw_events->used_mask, armpmu->num_events);

	if (enabled)
		armpmu->start();
}

static void armpmu_disable(struct pmu *pmu)
{
	struct arm_pmu *armpmu = to_arm_pmu(pmu);
	armpmu->stop();
}

static void __init armpmu_init(struct arm_pmu *armpmu)
{
	atomic_set(&armpmu->active_events, 0);
	mutex_init(&armpmu->reserve_mutex);

	armpmu->pmu = (struct pmu) {
		.pmu_enable	= armpmu_enable,
		.pmu_disable	= armpmu_disable,
		.event_init	= armpmu_event_init,
		.add		= armpmu_add,
		.del		= armpmu_del,
		.start		= armpmu_start,
		.stop		= armpmu_stop,
		.read		= armpmu_read,
	};
}

int __init armpmu_register(struct arm_pmu *armpmu, char *name, int type)
{
	armpmu_init(armpmu);
	return perf_pmu_register(&armpmu->pmu, name, type);
}

/*
 * ARMv8 PMUv3 Performance Events handling code.
 * Common event types.
 */
enum armv8_pmuv3_perf_types {
	/* Required events. */
	ARMV8_PMUV3_PERFCTR_PMNC_SW_INCR			= 0x00,
	ARMV8_PMUV3_PERFCTR_L1_DCACHE_REFILL			= 0x03,
	ARMV8_PMUV3_PERFCTR_L1_DCACHE_ACCESS			= 0x04,
	ARMV8_PMUV3_PERFCTR_PC_BRANCH_MIS_PRED			= 0x10,
	ARMV8_PMUV3_PERFCTR_CLOCK_CYCLES			= 0x11,
	ARMV8_PMUV3_PERFCTR_PC_BRANCH_PRED			= 0x12,

	/* At least one of the following is required. */
	ARMV8_PMUV3_PERFCTR_INSTR_EXECUTED			= 0x08,
	ARMV8_PMUV3_PERFCTR_OP_SPEC				= 0x1B,

	/* Common architectural events. */
	ARMV8_PMUV3_PERFCTR_MEM_READ				= 0x06,
	ARMV8_PMUV3_PERFCTR_MEM_WRITE				= 0x07,
	ARMV8_PMUV3_PERFCTR_EXC_TAKEN				= 0x09,
	ARMV8_PMUV3_PERFCTR_EXC_EXECUTED			= 0x0A,
	ARMV8_PMUV3_PERFCTR_CID_WRITE				= 0x0B,
	ARMV8_PMUV3_PERFCTR_PC_WRITE				= 0x0C,
	ARMV8_PMUV3_PERFCTR_PC_IMM_BRANCH			= 0x0D,
	ARMV8_PMUV3_PERFCTR_PC_PROC_RETURN			= 0x0E,
	ARMV8_PMUV3_PERFCTR_MEM_UNALIGNED_ACCESS		= 0x0F,
	ARMV8_PMUV3_PERFCTR_TTBR_WRITE				= 0x1C,

	/* Common microarchitectural events. */
	ARMV8_PMUV3_PERFCTR_L1_ICACHE_REFILL			= 0x01,
	ARMV8_PMUV3_PERFCTR_ITLB_REFILL				= 0x02,
	ARMV8_PMUV3_PERFCTR_DTLB_REFILL				= 0x05,
	ARMV8_PMUV3_PERFCTR_MEM_ACCESS				= 0x13,
	ARMV8_PMUV3_PERFCTR_L1_ICACHE_ACCESS			= 0x14,
	ARMV8_PMUV3_PERFCTR_L1_DCACHE_WB			= 0x15,
	ARMV8_PMUV3_PERFCTR_L2_CACHE_ACCESS			= 0x16,
	ARMV8_PMUV3_PERFCTR_L2_CACHE_REFILL			= 0x17,
	ARMV8_PMUV3_PERFCTR_L2_CACHE_WB				= 0x18,
	ARMV8_PMUV3_PERFCTR_BUS_ACCESS				= 0x19,
	ARMV8_PMUV3_PERFCTR_MEM_ERROR				= 0x1A,
	ARMV8_PMUV3_PERFCTR_BUS_CYCLES				= 0x1D,
};

/* PMUv3 HW events mapping. */
static const unsigned armv8_pmuv3_perf_map[PERF_COUNT_HW_MAX] = {
	[PERF_COUNT_HW_CPU_CYCLES]		= ARMV8_PMUV3_PERFCTR_CLOCK_CYCLES,
	[PERF_COUNT_HW_INSTRUCTIONS]		= ARMV8_PMUV3_PERFCTR_INSTR_EXECUTED,
	[PERF_COUNT_HW_CACHE_REFERENCES]	= ARMV8_PMUV3_PERFCTR_L1_DCACHE_ACCESS,
	[PERF_COUNT_HW_CACHE_MISSES]		= ARMV8_PMUV3_PERFCTR_L1_DCACHE_REFILL,
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS]	= HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_BRANCH_MISSES]		= ARMV8_PMUV3_PERFCTR_PC_BRANCH_MIS_PRED,
	[PERF_COUNT_HW_BUS_CYCLES]		= HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_STALLED_CYCLES_FRONTEND]	= HW_OP_UNSUPPORTED,
	[PERF_COUNT_HW_STALLED_CYCLES_BACKEND]	= HW_OP_UNSUPPORTED,
};

static const unsigned armv8_pmuv3_perf_cache_map[PERF_COUNT_HW_CACHE_MAX]
						[PERF_COUNT_HW_CACHE_OP_MAX]
						[PERF_COUNT_HW_CACHE_RESULT_MAX] = {
	[C(L1D)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV8_PMUV3_PERFCTR_L1_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_L1_DCACHE_REFILL,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV8_PMUV3_PERFCTR_L1_DCACHE_ACCESS,
			[C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_L1_DCACHE_REFILL,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(L1I)] = {
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
	[C(ITLB)] = {
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
	[C(BPU)] = {
		[C(OP_READ)] = {
			[C(RESULT_ACCESS)]	= ARMV8_PMUV3_PERFCTR_PC_BRANCH_PRED,
			[C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_PC_BRANCH_MIS_PRED,
		},
		[C(OP_WRITE)] = {
			[C(RESULT_ACCESS)]	= ARMV8_PMUV3_PERFCTR_PC_BRANCH_PRED,
			[C(RESULT_MISS)]	= ARMV8_PMUV3_PERFCTR_PC_BRANCH_MIS_PRED,
		},
		[C(OP_PREFETCH)] = {
			[C(RESULT_ACCESS)]	= CACHE_OP_UNSUPPORTED,
			[C(RESULT_MISS)]	= CACHE_OP_UNSUPPORTED,
		},
	},
	[C(NODE)] = {
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

/*
 * Perf Events' indices
 */
#define	ARMV8_IDX_CYCLE_COUNTER	0
#define	ARMV8_IDX_COUNTER0	1
#define	ARMV8_IDX_COUNTER_LAST	(ARMV8_IDX_CYCLE_COUNTER + cpu_pmu->num_events - 1)

#define	ARMV8_MAX_COUNTERS	32
#define	ARMV8_COUNTER_MASK	(ARMV8_MAX_COUNTERS - 1)

/*
 * ARMv8 low level PMU access
 */

/*
 * Perf Event to low level counters mapping
 */
#define	ARMV8_IDX_TO_COUNTER(x)	\
	(((x) - ARMV8_IDX_COUNTER0) & ARMV8_COUNTER_MASK)

/*
 * Per-CPU PMCR: config reg
 */
#define ARMV8_PMCR_E		(1 << 0) /* Enable all counters */
#define ARMV8_PMCR_P		(1 << 1) /* Reset all counters */
#define ARMV8_PMCR_C		(1 << 2) /* Cycle counter reset */
#define ARMV8_PMCR_D		(1 << 3) /* CCNT counts every 64th cpu cycle */
#define ARMV8_PMCR_X		(1 << 4) /* Export to ETM */
#define ARMV8_PMCR_DP		(1 << 5) /* Disable CCNT if non-invasive debug*/
#define	ARMV8_PMCR_N_SHIFT	11	 /* Number of counters supported */
#define	ARMV8_PMCR_N_MASK	0x1f
#define	ARMV8_PMCR_MASK		0x3f	 /* Mask for writable bits */

/*
 * PMOVSR: counters overflow flag status reg
 */
#define	ARMV8_OVSR_MASK		0xffffffff	/* Mask for writable bits */
#define	ARMV8_OVERFLOWED_MASK	ARMV8_OVSR_MASK

/*
 * PMXEVTYPER: Event selection reg
 */
#define	ARMV8_EVTYPE_MASK	0xc80003ff	/* Mask for writable bits */
#define	ARMV8_EVTYPE_EVENT	0x3ff		/* Mask for EVENT bits */

/*
 * Event filters for PMUv3
 */
#define	ARMV8_EXCLUDE_EL1	(1 << 31)
#define	ARMV8_EXCLUDE_EL0	(1 << 30)
#define	ARMV8_INCLUDE_EL2	(1 << 27)

static inline u32 armv8pmu_pmcr_read(void)
{
	u32 val;
	asm volatile("mrs %0, pmcr_el0" : "=r" (val));
	return val;
}

static inline void armv8pmu_pmcr_write(u32 val)
{
	val &= ARMV8_PMCR_MASK;
	isb();
	asm volatile("msr pmcr_el0, %0" :: "r" (val));
}

static inline int armv8pmu_has_overflowed(u32 pmovsr)
{
	return pmovsr & ARMV8_OVERFLOWED_MASK;
}

static inline int armv8pmu_counter_valid(int idx)
{
	return idx >= ARMV8_IDX_CYCLE_COUNTER && idx <= ARMV8_IDX_COUNTER_LAST;
}

static inline int armv8pmu_counter_has_overflowed(u32 pmnc, int idx)
{
	int ret = 0;
	u32 counter;

	if (!armv8pmu_counter_valid(idx)) {
		pr_err("CPU%u checking wrong counter %d overflow status\n",
			smp_processor_id(), idx);
	} else {
		counter = ARMV8_IDX_TO_COUNTER(idx);
		ret = pmnc & BIT(counter);
	}

	return ret;
}

static inline int armv8pmu_select_counter(int idx)
{
	u32 counter;

	if (!armv8pmu_counter_valid(idx)) {
		pr_err("CPU%u selecting wrong PMNC counter %d\n",
			smp_processor_id(), idx);
		return -EINVAL;
	}

	counter = ARMV8_IDX_TO_COUNTER(idx);
	asm volatile("msr pmselr_el0, %0" :: "r" (counter));
	isb();

	return idx;
}

static inline u32 armv8pmu_read_counter(int idx)
{
	u32 value = 0;

	if (!armv8pmu_counter_valid(idx))
		pr_err("CPU%u reading wrong counter %d\n",
			smp_processor_id(), idx);
	else if (idx == ARMV8_IDX_CYCLE_COUNTER)
		asm volatile("mrs %0, pmccntr_el0" : "=r" (value));
	else if (armv8pmu_select_counter(idx) == idx)
		asm volatile("mrs %0, pmxevcntr_el0" : "=r" (value));

	return value;
}

static inline void armv8pmu_write_counter(int idx, u32 value)
{
	if (!armv8pmu_counter_valid(idx))
		pr_err("CPU%u writing wrong counter %d\n",
			smp_processor_id(), idx);
	else if (idx == ARMV8_IDX_CYCLE_COUNTER)
		asm volatile("msr pmccntr_el0, %0" :: "r" (value));
	else if (armv8pmu_select_counter(idx) == idx)
		asm volatile("msr pmxevcntr_el0, %0" :: "r" (value));
}

static inline void armv8pmu_write_evtype(int idx, u32 val)
{
	if (armv8pmu_select_counter(idx) == idx) {
		val &= ARMV8_EVTYPE_MASK;
		asm volatile("msr pmxevtyper_el0, %0" :: "r" (val));
	}
}

static inline int armv8pmu_enable_counter(int idx)
{
	u32 counter;

	if (!armv8pmu_counter_valid(idx)) {
		pr_err("CPU%u enabling wrong PMNC counter %d\n",
			smp_processor_id(), idx);
		return -EINVAL;
	}

	counter = ARMV8_IDX_TO_COUNTER(idx);
	asm volatile("msr pmcntenset_el0, %0" :: "r" (BIT(counter)));
	return idx;
}

static inline int armv8pmu_disable_counter(int idx)
{
	u32 counter;

	if (!armv8pmu_counter_valid(idx)) {
		pr_err("CPU%u disabling wrong PMNC counter %d\n",
			smp_processor_id(), idx);
		return -EINVAL;
	}

	counter = ARMV8_IDX_TO_COUNTER(idx);
	asm volatile("msr pmcntenclr_el0, %0" :: "r" (BIT(counter)));
	return idx;
}

static inline int armv8pmu_enable_intens(int idx)
{
	u32 counter;

	if (!armv8pmu_counter_valid(idx)) {
		pr_err("CPU%u enabling wrong PMNC counter IRQ enable %d\n",
			smp_processor_id(), idx);
		return -EINVAL;
	}

	counter = ARMV8_IDX_TO_COUNTER(idx);
	asm volatile("msr pmintenset_el1, %0" :: "r" (BIT(counter)));
	return idx;
}

static inline int armv8pmu_disable_intens(int idx)
{
	u32 counter;

	if (!armv8pmu_counter_valid(idx)) {
		pr_err("CPU%u disabling wrong PMNC counter IRQ enable %d\n",
			smp_processor_id(), idx);
		return -EINVAL;
	}

	counter = ARMV8_IDX_TO_COUNTER(idx);
	asm volatile("msr pmintenclr_el1, %0" :: "r" (BIT(counter)));
	isb();
	/* Clear the overflow flag in case an interrupt is pending. */
	asm volatile("msr pmovsclr_el0, %0" :: "r" (BIT(counter)));
	isb();
	return idx;
}

static inline u32 armv8pmu_getreset_flags(void)
{
	u32 value;

	/* Read */
	asm volatile("mrs %0, pmovsclr_el0" : "=r" (value));

	/* Write to clear flags */
	value &= ARMV8_OVSR_MASK;
	asm volatile("msr pmovsclr_el0, %0" :: "r" (value));

	return value;
}

static void armv8pmu_enable_event(struct hw_perf_event *hwc, int idx)
{
	unsigned long flags;
	struct pmu_hw_events *events = cpu_pmu->get_hw_events();

	/*
	 * Enable counter and interrupt, and set the counter to count
	 * the event that we're interested in.
	 */
	raw_spin_lock_irqsave(&events->pmu_lock, flags);

	/*
	 * Disable counter
	 */
	armv8pmu_disable_counter(idx);

	/*
	 * Set event (if destined for PMNx counters).
	 */
	armv8pmu_write_evtype(idx, hwc->config_base);

	/*
	 * Enable interrupt for this counter
	 */
	armv8pmu_enable_intens(idx);

	/*
	 * Enable counter
	 */
	armv8pmu_enable_counter(idx);

	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static void armv8pmu_disable_event(struct hw_perf_event *hwc, int idx)
{
	unsigned long flags;
	struct pmu_hw_events *events = cpu_pmu->get_hw_events();

	/*
	 * Disable counter and interrupt
	 */
	raw_spin_lock_irqsave(&events->pmu_lock, flags);

	/*
	 * Disable counter
	 */
	armv8pmu_disable_counter(idx);

	/*
	 * Disable interrupt for this counter
	 */
	armv8pmu_disable_intens(idx);

	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static irqreturn_t armv8pmu_handle_irq(int irq_num, void *dev)
{
	u32 pmovsr;
	struct perf_sample_data data;
	struct pmu_hw_events *cpuc;
	struct pt_regs *regs;
	int idx;

	/*
	 * Get and reset the IRQ flags
	 */
	pmovsr = armv8pmu_getreset_flags();

	/*
	 * Did an overflow occur?
	 */
	if (!armv8pmu_has_overflowed(pmovsr))
		return IRQ_NONE;

	/*
	 * Handle the counter(s) overflow(s)
	 */
	regs = get_irq_regs();

	cpuc = this_cpu_ptr(&cpu_hw_events);
	for (idx = 0; idx < cpu_pmu->num_events; ++idx) {
		struct perf_event *event = cpuc->events[idx];
		struct hw_perf_event *hwc;

		/* Ignore if we don't have an event. */
		if (!event)
			continue;

		/*
		 * We have a single interrupt for all counters. Check that
		 * each counter has overflowed before we process it.
		 */
		if (!armv8pmu_counter_has_overflowed(pmovsr, idx))
			continue;

		hwc = &event->hw;
		armpmu_event_update(event, hwc, idx);
		perf_sample_data_init(&data, 0, hwc->last_period);
		if (!armpmu_event_set_period(event, hwc, idx))
			continue;

		if (perf_event_overflow(event, &data, regs))
			cpu_pmu->disable(hwc, idx);
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

static void armv8pmu_start(void)
{
	unsigned long flags;
	struct pmu_hw_events *events = cpu_pmu->get_hw_events();

	raw_spin_lock_irqsave(&events->pmu_lock, flags);
	/* Enable all counters */
	armv8pmu_pmcr_write(armv8pmu_pmcr_read() | ARMV8_PMCR_E);
	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static void armv8pmu_stop(void)
{
	unsigned long flags;
	struct pmu_hw_events *events = cpu_pmu->get_hw_events();

	raw_spin_lock_irqsave(&events->pmu_lock, flags);
	/* Disable all counters */
	armv8pmu_pmcr_write(armv8pmu_pmcr_read() & ~ARMV8_PMCR_E);
	raw_spin_unlock_irqrestore(&events->pmu_lock, flags);
}

static int armv8pmu_get_event_idx(struct pmu_hw_events *cpuc,
				  struct hw_perf_event *event)
{
	int idx;
	unsigned long evtype = event->config_base & ARMV8_EVTYPE_EVENT;

	/* Always place a cycle counter into the cycle counter. */
	if (evtype == ARMV8_PMUV3_PERFCTR_CLOCK_CYCLES) {
		if (test_and_set_bit(ARMV8_IDX_CYCLE_COUNTER, cpuc->used_mask))
			return -EAGAIN;

		return ARMV8_IDX_CYCLE_COUNTER;
	}

	/*
	 * For anything other than a cycle counter, try and use
	 * the events counters
	 */
	for (idx = ARMV8_IDX_COUNTER0; idx < cpu_pmu->num_events; ++idx) {
		if (!test_and_set_bit(idx, cpuc->used_mask))
			return idx;
	}

	/* The counters are all in use. */
	return -EAGAIN;
}

/*
 * Add an event filter to a given event. This will only work for PMUv2 PMUs.
 */
static int armv8pmu_set_event_filter(struct hw_perf_event *event,
				     struct perf_event_attr *attr)
{
	unsigned long config_base = 0;

	if (attr->exclude_idle)
		return -EPERM;
	if (attr->exclude_user)
		config_base |= ARMV8_EXCLUDE_EL0;
	if (attr->exclude_kernel)
		config_base |= ARMV8_EXCLUDE_EL1;
	if (!attr->exclude_hv)
		config_base |= ARMV8_INCLUDE_EL2;

	/*
	 * Install the filter into config_base as this is used to
	 * construct the event type.
	 */
	event->config_base = config_base;

	return 0;
}

static void armv8pmu_reset(void *info)
{
	u32 idx, nb_cnt = cpu_pmu->num_events;

	/* The counter and interrupt enable registers are unknown at reset. */
	for (idx = ARMV8_IDX_CYCLE_COUNTER; idx < nb_cnt; ++idx)
		armv8pmu_disable_event(NULL, idx);

	/* Initialize & Reset PMNC: C and P bits. */
	armv8pmu_pmcr_write(ARMV8_PMCR_P | ARMV8_PMCR_C);

	/* Disable access from userspace. */
	asm volatile("msr pmuserenr_el0, %0" :: "r" (0));
}

static int armv8_pmuv3_map_event(struct perf_event *event)
{
	return map_cpu_event(event, &armv8_pmuv3_perf_map,
				&armv8_pmuv3_perf_cache_map,
				ARMV8_EVTYPE_EVENT);
}

static struct arm_pmu armv8pmu = {
	.handle_irq		= armv8pmu_handle_irq,
	.enable			= armv8pmu_enable_event,
	.disable		= armv8pmu_disable_event,
	.read_counter		= armv8pmu_read_counter,
	.write_counter		= armv8pmu_write_counter,
	.get_event_idx		= armv8pmu_get_event_idx,
	.start			= armv8pmu_start,
	.stop			= armv8pmu_stop,
	.reset			= armv8pmu_reset,
	.max_period		= (1LLU << 32) - 1,
};

static u32 __init armv8pmu_read_num_pmnc_events(void)
{
	u32 nb_cnt;

	/* Read the nb of CNTx counters supported from PMNC */
	nb_cnt = (armv8pmu_pmcr_read() >> ARMV8_PMCR_N_SHIFT) & ARMV8_PMCR_N_MASK;

	/* Add the CPU cycles counter and return */
	return nb_cnt + 1;
}

static struct arm_pmu *__init armv8_pmuv3_pmu_init(void)
{
	armv8pmu.name			= "arm/armv8-pmuv3";
	armv8pmu.map_event		= armv8_pmuv3_map_event;
	armv8pmu.num_events		= armv8pmu_read_num_pmnc_events();
	armv8pmu.set_event_filter	= armv8pmu_set_event_filter;
	return &armv8pmu;
}

/*
 * Ensure the PMU has sane values out of reset.
 * This requires SMP to be available, so exists as a separate initcall.
 */
static int __init
cpu_pmu_reset(void)
{
	if (cpu_pmu && cpu_pmu->reset)
		return on_each_cpu(cpu_pmu->reset, NULL, 1);
	return 0;
}
arch_initcall(cpu_pmu_reset);

/*
 * PMU platform driver and devicetree bindings.
 */
static const struct of_device_id armpmu_of_device_ids[] = {
	{.compatible = "arm,armv8-pmuv3"},
	{},
};

static int armpmu_device_probe(struct platform_device *pdev)
{
	if (!cpu_pmu)
		return -ENODEV;

	cpu_pmu->plat_device = pdev;
	return 0;
}

static struct platform_driver armpmu_driver = {
	.driver		= {
		.name	= "arm-pmu",
		.of_match_table = armpmu_of_device_ids,
	},
	.probe		= armpmu_device_probe,
};

static int __init register_pmu_driver(void)
{
	return platform_driver_register(&armpmu_driver);
}
device_initcall(register_pmu_driver);

static struct pmu_hw_events *armpmu_get_cpu_events(void)
{
	return this_cpu_ptr(&cpu_hw_events);
}

static void __init cpu_pmu_init(struct arm_pmu *armpmu)
{
	int cpu;
	for_each_possible_cpu(cpu) {
		struct pmu_hw_events *events = &per_cpu(cpu_hw_events, cpu);
		events->events = per_cpu(hw_events, cpu);
		events->used_mask = per_cpu(used_mask, cpu);
		raw_spin_lock_init(&events->pmu_lock);
	}
	armpmu->get_hw_events = armpmu_get_cpu_events;
}

static int __init init_hw_perf_events(void)
{
	u64 dfr = read_cpuid(ID_AA64DFR0_EL1);

	switch ((dfr >> 8) & 0xf) {
	case 0x1:	/* PMUv3 */
		cpu_pmu = armv8_pmuv3_pmu_init();
		break;
	}

	if (cpu_pmu) {
		pr_info("enabled with %s PMU driver, %d counters available\n",
			cpu_pmu->name, cpu_pmu->num_events);
		cpu_pmu_init(cpu_pmu);
		armpmu_register(cpu_pmu, "cpu", PERF_TYPE_RAW);
	} else {
		pr_info("no hardware support available\n");
	}

	return 0;
}
early_initcall(init_hw_perf_events);

/*
 * Callchain handling code.
 */
struct frame_tail {
	struct frame_tail	__user *fp;
	unsigned long		lr;
} __attribute__((packed));

/*
 * Get the return address for a single stackframe and return a pointer to the
 * next frame tail.
 */
static struct frame_tail __user *
user_backtrace(struct frame_tail __user *tail,
	       struct perf_callchain_entry *entry)
{
	struct frame_tail buftail;
	unsigned long err;

	/* Also check accessibility of one struct frame_tail beyond */
	if (!access_ok(VERIFY_READ, tail, sizeof(buftail)))
		return NULL;

	pagefault_disable();
	err = __copy_from_user_inatomic(&buftail, tail, sizeof(buftail));
	pagefault_enable();

	if (err)
		return NULL;

	perf_callchain_store(entry, buftail.lr);

	/*
	 * Frame pointers should strictly progress back up the stack
	 * (towards higher addresses).
	 */
	if (tail >= buftail.fp)
		return NULL;

	return buftail.fp;
}

#ifdef CONFIG_COMPAT
/*
 * The registers we're interested in are at the end of the variable
 * length saved register structure. The fp points at the end of this
 * structure so the address of this struct is:
 * (struct compat_frame_tail *)(xxx->fp)-1
 *
 * This code has been adapted from the ARM OProfile support.
 */
struct compat_frame_tail {
	compat_uptr_t	fp; /* a (struct compat_frame_tail *) in compat mode */
	u32		sp;
	u32		lr;
} __attribute__((packed));

static struct compat_frame_tail __user *
compat_user_backtrace(struct compat_frame_tail __user *tail,
		      struct perf_callchain_entry *entry)
{
	struct compat_frame_tail buftail;
	unsigned long err;

	/* Also check accessibility of one struct frame_tail beyond */
	if (!access_ok(VERIFY_READ, tail, sizeof(buftail)))
		return NULL;

	pagefault_disable();
	err = __copy_from_user_inatomic(&buftail, tail, sizeof(buftail));
	pagefault_enable();

	if (err)
		return NULL;

	perf_callchain_store(entry, buftail.lr);

	/*
	 * Frame pointers should strictly progress back up the stack
	 * (towards higher addresses).
	 */
	if (tail + 1 >= (struct compat_frame_tail __user *)
			compat_ptr(buftail.fp))
		return NULL;

	return (struct compat_frame_tail __user *)compat_ptr(buftail.fp) - 1;
}
#endif /* CONFIG_COMPAT */

void perf_callchain_user(struct perf_callchain_entry *entry,
			 struct pt_regs *regs)
{
	if (perf_guest_cbs && perf_guest_cbs->is_in_guest()) {
		/* We don't support guest os callchain now */
		return;
	}

	perf_callchain_store(entry, regs->pc);

	if (!compat_user_mode(regs)) {
		/* AARCH64 mode */
		struct frame_tail __user *tail;

		tail = (struct frame_tail __user *)regs->regs[29];

		while (entry->nr < PERF_MAX_STACK_DEPTH &&
		       tail && !((unsigned long)tail & 0xf))
			tail = user_backtrace(tail, entry);
	} else {
#ifdef CONFIG_COMPAT
		/* AARCH32 compat mode */
		struct compat_frame_tail __user *tail;

		tail = (struct compat_frame_tail __user *)regs->compat_fp - 1;

		while ((entry->nr < PERF_MAX_STACK_DEPTH) &&
			tail && !((unsigned long)tail & 0x3))
			tail = compat_user_backtrace(tail, entry);
#endif
	}
}

/*
 * Gets called by walk_stackframe() for every stackframe. This will be called
 * whist unwinding the stackframe and is like a subroutine return so we use
 * the PC.
 */
static int callchain_trace(struct stackframe *frame, void *data)
{
	struct perf_callchain_entry *entry = data;
	perf_callchain_store(entry, frame->pc);
	return 0;
}

void perf_callchain_kernel(struct perf_callchain_entry *entry,
			   struct pt_regs *regs)
{
	struct stackframe frame;

	if (perf_guest_cbs && perf_guest_cbs->is_in_guest()) {
		/* We don't support guest os callchain now */
		return;
	}

	frame.fp = regs->regs[29];
	frame.sp = regs->sp;
	frame.pc = regs->pc;

	walk_stackframe(&frame, callchain_trace, entry);
}

unsigned long perf_instruction_pointer(struct pt_regs *regs)
{
	if (perf_guest_cbs && perf_guest_cbs->is_in_guest())
		return perf_guest_cbs->get_guest_ip();

	return instruction_pointer(regs);
}

unsigned long perf_misc_flags(struct pt_regs *regs)
{
	int misc = 0;

	if (perf_guest_cbs && perf_guest_cbs->is_in_guest()) {
		if (perf_guest_cbs->is_user_mode())
			misc |= PERF_RECORD_MISC_GUEST_USER;
		else
			misc |= PERF_RECORD_MISC_GUEST_KERNEL;
	} else {
		if (user_mode(regs))
			misc |= PERF_RECORD_MISC_USER;
		else
			misc |= PERF_RECORD_MISC_KERNEL;
	}

	return misc;
}

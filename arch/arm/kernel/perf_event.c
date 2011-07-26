#undef DEBUG

/*
 * ARM performance counter support.
 *
 * Copyright (C) 2009 picoChip Designs, Ltd., Jamie Iles
 * Copyright (C) 2010 ARM Ltd., Will Deacon <will.deacon@arm.com>
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
static DEFINE_RAW_SPINLOCK(pmu_lock);

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
static DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events);

struct arm_pmu {
	enum arm_perf_pmu_ids id;
	const char	*name;
	irqreturn_t	(*handle_irq)(int irq_num, void *dev);
	void		(*enable)(struct hw_perf_event *evt, int idx);
	void		(*disable)(struct hw_perf_event *evt, int idx);
	int		(*get_event_idx)(struct cpu_hw_events *cpuc,
					 struct hw_perf_event *hwc);
	u32		(*read_counter)(int idx);
	void		(*write_counter)(int idx, u32 val);
	void		(*start)(void);
	void		(*stop)(void);
	void		(*reset)(void *);
	const unsigned	(*cache_map)[PERF_COUNT_HW_CACHE_MAX]
				    [PERF_COUNT_HW_CACHE_OP_MAX]
				    [PERF_COUNT_HW_CACHE_RESULT_MAX];
	const unsigned	(*event_map)[PERF_COUNT_HW_MAX];
	u32		raw_event_mask;
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

	ret = (int)(*armpmu->cache_map)[cache_type][cache_op][cache_result];

	if (ret == CACHE_OP_UNSUPPORTED)
		return -ENOENT;

	return ret;
}

static int
armpmu_map_event(u64 config)
{
	int mapping = (*armpmu->event_map)[config];
	return mapping == HW_OP_UNSUPPORTED ? -EOPNOTSUPP : mapping;
}

static int
armpmu_map_raw_event(u64 config)
{
	return (int)(config & armpmu->raw_event_mask);
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
		    int idx, int overflow)
{
	u64 delta, prev_raw_count, new_raw_count;

again:
	prev_raw_count = local64_read(&hwc->prev_count);
	new_raw_count = armpmu->read_counter(idx);

	if (local64_cmpxchg(&hwc->prev_count, prev_raw_count,
			     new_raw_count) != prev_raw_count)
		goto again;

	new_raw_count &= armpmu->max_period;
	prev_raw_count &= armpmu->max_period;

	if (overflow)
		delta = armpmu->max_period - prev_raw_count + new_raw_count + 1;
	else
		delta = new_raw_count - prev_raw_count;

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

	armpmu_event_update(event, hwc, hwc->idx, 0);
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
		armpmu_event_update(event, hwc, hwc->idx, 0);
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

static irqreturn_t armpmu_platform_irq(int irq, void *dev)
{
	struct arm_pmu_platdata *plat = dev_get_platdata(&pmu_device->dev);

	return plat->handle_irq(irq, dev, armpmu->handle_irq);
}

static int
armpmu_reserve_hardware(void)
{
	struct arm_pmu_platdata *plat;
	irq_handler_t handle_irq;
	int i, err = -ENODEV, irq;

	pmu_device = reserve_pmu(ARM_PMU_DEVICE_CPU);
	if (IS_ERR(pmu_device)) {
		pr_warning("unable to reserve pmu\n");
		return PTR_ERR(pmu_device);
	}

	init_pmu(ARM_PMU_DEVICE_CPU);

	plat = dev_get_platdata(&pmu_device->dev);
	if (plat && plat->handle_irq)
		handle_irq = armpmu_platform_irq;
	else
		handle_irq = armpmu->handle_irq;

	if (pmu_device->num_resources < 1) {
		pr_err("no irqs for PMUs defined\n");
		return -ENODEV;
	}

	for (i = 0; i < pmu_device->num_resources; ++i) {
		irq = platform_get_irq(pmu_device, i);
		if (irq < 0)
			continue;

		err = request_irq(irq, handle_irq,
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
		release_pmu(ARM_PMU_DEVICE_CPU);
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

	release_pmu(ARM_PMU_DEVICE_CPU);
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
		mapping = armpmu_map_event(event->attr.config);
	} else if (PERF_TYPE_HW_CACHE == event->attr.type) {
		mapping = armpmu_map_cache_event(event->attr.config);
	} else if (PERF_TYPE_RAW == event->attr.type) {
		mapping = armpmu_map_raw_event(event->attr.config);
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
	int idx, enabled = 0;
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);

	if (!armpmu)
		return;

	for (idx = 0; idx <= armpmu->num_events; ++idx) {
		struct perf_event *event = cpuc->events[idx];

		if (!event)
			continue;

		armpmu->enable(&event->hw, idx);
		enabled = 1;
	}

	if (enabled)
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

/* Include the PMU-specific implementations. */
#include "perf_event_xscale.c"
#include "perf_event_v6.c"
#include "perf_event_v7.c"

/*
 * Ensure the PMU has sane values out of reset.
 * This requires SMP to be available, so exists as a separate initcall.
 */
static int __init
armpmu_reset(void)
{
	if (armpmu && armpmu->reset)
		return on_each_cpu(armpmu->reset, NULL, 1);
	return 0;
}
arch_initcall(armpmu_reset);

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
			armpmu = armv6pmu_init();
			break;
		case 0xB020:	/* ARM11mpcore */
			armpmu = armv6mpcore_pmu_init();
			break;
		case 0xC080:	/* Cortex-A8 */
			armpmu = armv7_a8_pmu_init();
			break;
		case 0xC090:	/* Cortex-A9 */
			armpmu = armv7_a9_pmu_init();
			break;
		case 0xC050:	/* Cortex-A5 */
			armpmu = armv7_a5_pmu_init();
			break;
		case 0xC0F0:	/* Cortex-A15 */
			armpmu = armv7_a15_pmu_init();
			break;
		}
	/* Intel CPUs [xscale]. */
	} else if (0x69 == implementor) {
		part_number = (cpuid >> 13) & 0x7;
		switch (part_number) {
		case 1:
			armpmu = xscale1pmu_init();
			break;
		case 2:
			armpmu = xscale2pmu_init();
			break;
		}
	}

	if (armpmu) {
		pr_info("enabled with %s PMU driver, %d counters available\n",
			armpmu->name, armpmu->num_events);
	} else {
		pr_info("no hardware support available\n");
	}

	perf_pmu_register(&pmu, "cpu", PERF_TYPE_RAW);

	return 0;
}
early_initcall(init_hw_perf_events);

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
	struct frame_tail __user *fp;
	unsigned long sp;
	unsigned long lr;
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
	if (tail + 1 >= buftail.fp)
		return NULL;

	return buftail.fp - 1;
}

void
perf_callchain_user(struct perf_callchain_entry *entry, struct pt_regs *regs)
{
	struct frame_tail __user *tail;


	tail = (struct frame_tail __user *)regs->ARM_fp - 1;

	while ((entry->nr < PERF_MAX_STACK_DEPTH) &&
	       tail && !((unsigned long)tail & 0x3))
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

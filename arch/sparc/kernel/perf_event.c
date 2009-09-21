/* Performance event support for sparc64.
 *
 * Copyright (C) 2009 David S. Miller <davem@davemloft.net>
 *
 * This code is based almost entirely upon the x86 perf event
 * code, which is:
 *
 *  Copyright (C) 2008 Thomas Gleixner <tglx@linutronix.de>
 *  Copyright (C) 2008-2009 Red Hat, Inc., Ingo Molnar
 *  Copyright (C) 2009 Jaswinder Singh Rajput
 *  Copyright (C) 2009 Advanced Micro Devices, Inc., Robert Richter
 *  Copyright (C) 2008-2009 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 */

#include <linux/perf_event.h>
#include <linux/kprobes.h>
#include <linux/kernel.h>
#include <linux/kdebug.h>
#include <linux/mutex.h>

#include <asm/cpudata.h>
#include <asm/atomic.h>
#include <asm/nmi.h>
#include <asm/pcr.h>

/* Sparc64 chips have two performance counters, 32-bits each, with
 * overflow interrupts generated on transition from 0xffffffff to 0.
 * The counters are accessed in one go using a 64-bit register.
 *
 * Both counters are controlled using a single control register.  The
 * only way to stop all sampling is to clear all of the context (user,
 * supervisor, hypervisor) sampling enable bits.  But these bits apply
 * to both counters, thus the two counters can't be enabled/disabled
 * individually.
 *
 * The control register has two event fields, one for each of the two
 * counters.  It's thus nearly impossible to have one counter going
 * while keeping the other one stopped.  Therefore it is possible to
 * get overflow interrupts for counters not currently "in use" and
 * that condition must be checked in the overflow interrupt handler.
 *
 * So we use a hack, in that we program inactive counters with the
 * "sw_count0" and "sw_count1" events.  These count how many times
 * the instruction "sethi %hi(0xfc000), %g0" is executed.  It's an
 * unusual way to encode a NOP and therefore will not trigger in
 * normal code.
 */

#define MAX_HWEVENTS			2
#define MAX_PERIOD			((1UL << 32) - 1)

#define PIC_UPPER_INDEX			0
#define PIC_LOWER_INDEX			1

struct cpu_hw_events {
	struct perf_event	*events[MAX_HWEVENTS];
	unsigned long		used_mask[BITS_TO_LONGS(MAX_HWEVENTS)];
	unsigned long		active_mask[BITS_TO_LONGS(MAX_HWEVENTS)];
	int enabled;
};
DEFINE_PER_CPU(struct cpu_hw_events, cpu_hw_events) = { .enabled = 1, };

struct perf_event_map {
	u16	encoding;
	u8	pic_mask;
#define PIC_NONE	0x00
#define PIC_UPPER	0x01
#define PIC_LOWER	0x02
};

struct sparc_pmu {
	const struct perf_event_map	*(*event_map)(int);
	int				max_events;
	int				upper_shift;
	int				lower_shift;
	int				event_mask;
	int				hv_bit;
	int				irq_bit;
	int				upper_nop;
	int				lower_nop;
};

static const struct perf_event_map ultra3i_perfmon_event_map[] = {
	[PERF_COUNT_HW_CPU_CYCLES] = { 0x0000, PIC_UPPER | PIC_LOWER },
	[PERF_COUNT_HW_INSTRUCTIONS] = { 0x0001, PIC_UPPER | PIC_LOWER },
	[PERF_COUNT_HW_CACHE_REFERENCES] = { 0x0009, PIC_LOWER },
	[PERF_COUNT_HW_CACHE_MISSES] = { 0x0009, PIC_UPPER },
};

static const struct perf_event_map *ultra3i_event_map(int event_id)
{
	return &ultra3i_perfmon_event_map[event_id];
}

static const struct sparc_pmu ultra3i_pmu = {
	.event_map	= ultra3i_event_map,
	.max_events	= ARRAY_SIZE(ultra3i_perfmon_event_map),
	.upper_shift	= 11,
	.lower_shift	= 4,
	.event_mask	= 0x3f,
	.upper_nop	= 0x1c,
	.lower_nop	= 0x14,
};

static const struct perf_event_map niagara2_perfmon_event_map[] = {
	[PERF_COUNT_HW_CPU_CYCLES] = { 0x02ff, PIC_UPPER | PIC_LOWER },
	[PERF_COUNT_HW_INSTRUCTIONS] = { 0x02ff, PIC_UPPER | PIC_LOWER },
	[PERF_COUNT_HW_CACHE_REFERENCES] = { 0x0208, PIC_UPPER | PIC_LOWER },
	[PERF_COUNT_HW_CACHE_MISSES] = { 0x0302, PIC_UPPER | PIC_LOWER },
	[PERF_COUNT_HW_BRANCH_INSTRUCTIONS] = { 0x0201, PIC_UPPER | PIC_LOWER },
	[PERF_COUNT_HW_BRANCH_MISSES] = { 0x0202, PIC_UPPER | PIC_LOWER },
};

static const struct perf_event_map *niagara2_event_map(int event_id)
{
	return &niagara2_perfmon_event_map[event_id];
}

static const struct sparc_pmu niagara2_pmu = {
	.event_map	= niagara2_event_map,
	.max_events	= ARRAY_SIZE(niagara2_perfmon_event_map),
	.upper_shift	= 19,
	.lower_shift	= 6,
	.event_mask	= 0xfff,
	.hv_bit		= 0x8,
	.irq_bit	= 0x03,
	.upper_nop	= 0x220,
	.lower_nop	= 0x220,
};

static const struct sparc_pmu *sparc_pmu __read_mostly;

static u64 event_encoding(u64 event_id, int idx)
{
	if (idx == PIC_UPPER_INDEX)
		event_id <<= sparc_pmu->upper_shift;
	else
		event_id <<= sparc_pmu->lower_shift;
	return event_id;
}

static u64 mask_for_index(int idx)
{
	return event_encoding(sparc_pmu->event_mask, idx);
}

static u64 nop_for_index(int idx)
{
	return event_encoding(idx == PIC_UPPER_INDEX ?
			      sparc_pmu->upper_nop :
			      sparc_pmu->lower_nop, idx);
}

static inline void sparc_pmu_enable_event(struct hw_perf_event *hwc,
					    int idx)
{
	u64 val, mask = mask_for_index(idx);

	val = pcr_ops->read();
	pcr_ops->write((val & ~mask) | hwc->config);
}

static inline void sparc_pmu_disable_event(struct hw_perf_event *hwc,
					     int idx)
{
	u64 mask = mask_for_index(idx);
	u64 nop = nop_for_index(idx);
	u64 val = pcr_ops->read();

	pcr_ops->write((val & ~mask) | nop);
}

void hw_perf_enable(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	u64 val;
	int i;

	if (cpuc->enabled)
		return;

	cpuc->enabled = 1;
	barrier();

	val = pcr_ops->read();

	for (i = 0; i < MAX_HWEVENTS; i++) {
		struct perf_event *cp = cpuc->events[i];
		struct hw_perf_event *hwc;

		if (!cp)
			continue;
		hwc = &cp->hw;
		val |= hwc->config_base;
	}

	pcr_ops->write(val);
}

void hw_perf_disable(void)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	u64 val;

	if (!cpuc->enabled)
		return;

	cpuc->enabled = 0;

	val = pcr_ops->read();
	val &= ~(PCR_UTRACE | PCR_STRACE |
		 sparc_pmu->hv_bit | sparc_pmu->irq_bit);
	pcr_ops->write(val);
}

static u32 read_pmc(int idx)
{
	u64 val;

	read_pic(val);
	if (idx == PIC_UPPER_INDEX)
		val >>= 32;

	return val & 0xffffffff;
}

static void write_pmc(int idx, u64 val)
{
	u64 shift, mask, pic;

	shift = 0;
	if (idx == PIC_UPPER_INDEX)
		shift = 32;

	mask = ((u64) 0xffffffff) << shift;
	val <<= shift;

	read_pic(pic);
	pic &= ~mask;
	pic |= val;
	write_pic(pic);
}

static int sparc_perf_event_set_period(struct perf_event *event,
					 struct hw_perf_event *hwc, int idx)
{
	s64 left = atomic64_read(&hwc->period_left);
	s64 period = hwc->sample_period;
	int ret = 0;

	if (unlikely(left <= -period)) {
		left = period;
		atomic64_set(&hwc->period_left, left);
		hwc->last_period = period;
		ret = 1;
	}

	if (unlikely(left <= 0)) {
		left += period;
		atomic64_set(&hwc->period_left, left);
		hwc->last_period = period;
		ret = 1;
	}
	if (left > MAX_PERIOD)
		left = MAX_PERIOD;

	atomic64_set(&hwc->prev_count, (u64)-left);

	write_pmc(idx, (u64)(-left) & 0xffffffff);

	perf_event_update_userpage(event);

	return ret;
}

static int sparc_pmu_enable(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	if (test_and_set_bit(idx, cpuc->used_mask))
		return -EAGAIN;

	sparc_pmu_disable_event(hwc, idx);

	cpuc->events[idx] = event;
	set_bit(idx, cpuc->active_mask);

	sparc_perf_event_set_period(event, hwc, idx);
	sparc_pmu_enable_event(hwc, idx);
	perf_event_update_userpage(event);
	return 0;
}

static u64 sparc_perf_event_update(struct perf_event *event,
				     struct hw_perf_event *hwc, int idx)
{
	int shift = 64 - 32;
	u64 prev_raw_count, new_raw_count;
	s64 delta;

again:
	prev_raw_count = atomic64_read(&hwc->prev_count);
	new_raw_count = read_pmc(idx);

	if (atomic64_cmpxchg(&hwc->prev_count, prev_raw_count,
			     new_raw_count) != prev_raw_count)
		goto again;

	delta = (new_raw_count << shift) - (prev_raw_count << shift);
	delta >>= shift;

	atomic64_add(delta, &event->count);
	atomic64_sub(delta, &hwc->period_left);

	return new_raw_count;
}

static void sparc_pmu_disable(struct perf_event *event)
{
	struct cpu_hw_events *cpuc = &__get_cpu_var(cpu_hw_events);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	clear_bit(idx, cpuc->active_mask);
	sparc_pmu_disable_event(hwc, idx);

	barrier();

	sparc_perf_event_update(event, hwc, idx);
	cpuc->events[idx] = NULL;
	clear_bit(idx, cpuc->used_mask);

	perf_event_update_userpage(event);
}

static void sparc_pmu_read(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	sparc_perf_event_update(event, hwc, hwc->idx);
}

static void sparc_pmu_unthrottle(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	sparc_pmu_enable_event(hwc, hwc->idx);
}

static atomic_t active_events = ATOMIC_INIT(0);
static DEFINE_MUTEX(pmc_grab_mutex);

void perf_event_grab_pmc(void)
{
	if (atomic_inc_not_zero(&active_events))
		return;

	mutex_lock(&pmc_grab_mutex);
	if (atomic_read(&active_events) == 0) {
		if (atomic_read(&nmi_active) > 0) {
			on_each_cpu(stop_nmi_watchdog, NULL, 1);
			BUG_ON(atomic_read(&nmi_active) != 0);
		}
		atomic_inc(&active_events);
	}
	mutex_unlock(&pmc_grab_mutex);
}

void perf_event_release_pmc(void)
{
	if (atomic_dec_and_mutex_lock(&active_events, &pmc_grab_mutex)) {
		if (atomic_read(&nmi_active) == 0)
			on_each_cpu(start_nmi_watchdog, NULL, 1);
		mutex_unlock(&pmc_grab_mutex);
	}
}

static void hw_perf_event_destroy(struct perf_event *event)
{
	perf_event_release_pmc();
}

static int __hw_perf_event_init(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;
	struct hw_perf_event *hwc = &event->hw;
	const struct perf_event_map *pmap;
	u64 enc;

	if (atomic_read(&nmi_active) < 0)
		return -ENODEV;

	if (attr->type != PERF_TYPE_HARDWARE)
		return -EOPNOTSUPP;

	if (attr->config >= sparc_pmu->max_events)
		return -EINVAL;

	perf_event_grab_pmc();
	event->destroy = hw_perf_event_destroy;

	/* We save the enable bits in the config_base.  So to
	 * turn off sampling just write 'config', and to enable
	 * things write 'config | config_base'.
	 */
	hwc->config_base = sparc_pmu->irq_bit;
	if (!attr->exclude_user)
		hwc->config_base |= PCR_UTRACE;
	if (!attr->exclude_kernel)
		hwc->config_base |= PCR_STRACE;
	if (!attr->exclude_hv)
		hwc->config_base |= sparc_pmu->hv_bit;

	if (!hwc->sample_period) {
		hwc->sample_period = MAX_PERIOD;
		hwc->last_period = hwc->sample_period;
		atomic64_set(&hwc->period_left, hwc->sample_period);
	}

	pmap = sparc_pmu->event_map(attr->config);

	enc = pmap->encoding;
	if (pmap->pic_mask & PIC_UPPER) {
		hwc->idx = PIC_UPPER_INDEX;
		enc <<= sparc_pmu->upper_shift;
	} else {
		hwc->idx = PIC_LOWER_INDEX;
		enc <<= sparc_pmu->lower_shift;
	}

	hwc->config |= enc;
	return 0;
}

static const struct pmu pmu = {
	.enable		= sparc_pmu_enable,
	.disable	= sparc_pmu_disable,
	.read		= sparc_pmu_read,
	.unthrottle	= sparc_pmu_unthrottle,
};

const struct pmu *hw_perf_event_init(struct perf_event *event)
{
	int err = __hw_perf_event_init(event);

	if (err)
		return ERR_PTR(err);
	return &pmu;
}

void perf_event_print_debug(void)
{
	unsigned long flags;
	u64 pcr, pic;
	int cpu;

	if (!sparc_pmu)
		return;

	local_irq_save(flags);

	cpu = smp_processor_id();

	pcr = pcr_ops->read();
	read_pic(pic);

	pr_info("\n");
	pr_info("CPU#%d: PCR[%016llx] PIC[%016llx]\n",
		cpu, pcr, pic);

	local_irq_restore(flags);
}

static int __kprobes perf_event_nmi_handler(struct notifier_block *self,
					      unsigned long cmd, void *__args)
{
	struct die_args *args = __args;
	struct perf_sample_data data;
	struct cpu_hw_events *cpuc;
	struct pt_regs *regs;
	int idx;

	if (!atomic_read(&active_events))
		return NOTIFY_DONE;

	switch (cmd) {
	case DIE_NMI:
		break;

	default:
		return NOTIFY_DONE;
	}

	regs = args->regs;

	data.addr = 0;

	cpuc = &__get_cpu_var(cpu_hw_events);
	for (idx = 0; idx < MAX_HWEVENTS; idx++) {
		struct perf_event *event = cpuc->events[idx];
		struct hw_perf_event *hwc;
		u64 val;

		if (!test_bit(idx, cpuc->active_mask))
			continue;
		hwc = &event->hw;
		val = sparc_perf_event_update(event, hwc, idx);
		if (val & (1ULL << 31))
			continue;

		data.period = event->hw.last_period;
		if (!sparc_perf_event_set_period(event, hwc, idx))
			continue;

		if (perf_event_overflow(event, 1, &data, regs))
			sparc_pmu_disable_event(hwc, idx);
	}

	return NOTIFY_STOP;
}

static __read_mostly struct notifier_block perf_event_nmi_notifier = {
	.notifier_call		= perf_event_nmi_handler,
};

static bool __init supported_pmu(void)
{
	if (!strcmp(sparc_pmu_type, "ultra3i")) {
		sparc_pmu = &ultra3i_pmu;
		return true;
	}
	if (!strcmp(sparc_pmu_type, "niagara2")) {
		sparc_pmu = &niagara2_pmu;
		return true;
	}
	return false;
}

void __init init_hw_perf_events(void)
{
	pr_info("Performance events: ");

	if (!supported_pmu()) {
		pr_cont("No support for PMU type '%s'\n", sparc_pmu_type);
		return;
	}

	pr_cont("Supported PMU type is '%s'\n", sparc_pmu_type);

	/* All sparc64 PMUs currently have 2 events.  But this simple
	 * driver only supports one active event at a time.
	 */
	perf_max_events = 1;

	register_die_notifier(&perf_event_nmi_notifier);
}

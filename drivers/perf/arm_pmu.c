#undef DEBUG

/*
 * ARM performance counter support.
 *
 * Copyright (C) 2009 picoChip Designs, Ltd., Jamie Iles
 * Copyright (C) 2010 ARM Ltd., Will Deacon <will.deacon@arm.com>
 *
 * This code is based on the sparc64 perf event code, which is in turn based
 * on the x86 code.
 */
#define pr_fmt(fmt) "hw perfevents: " fmt

#include <linux/bitmap.h>
#include <linux/cpumask.h>
#include <linux/cpu_pm.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/of_device.h>
#include <linux/perf/arm_pmu.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>

#include <asm/cputype.h>
#include <asm/irq_regs.h>

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
armpmu_map_hw_event(const unsigned (*event_map)[PERF_COUNT_HW_MAX], u64 config)
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

int
armpmu_map_event(struct perf_event *event,
		 const unsigned (*event_map)[PERF_COUNT_HW_MAX],
		 const unsigned (*cache_map)
				[PERF_COUNT_HW_CACHE_MAX]
				[PERF_COUNT_HW_CACHE_OP_MAX]
				[PERF_COUNT_HW_CACHE_RESULT_MAX],
		 u32 raw_event_mask)
{
	u64 config = event->attr.config;
	int type = event->attr.type;

	if (type == event->pmu->type)
		return armpmu_map_raw_event(raw_event_mask, config);

	switch (type) {
	case PERF_TYPE_HARDWARE:
		return armpmu_map_hw_event(event_map, config);
	case PERF_TYPE_HW_CACHE:
		return armpmu_map_cache_event(cache_map, config);
	case PERF_TYPE_RAW:
		return armpmu_map_raw_event(raw_event_mask, config);
	}

	return -ENOENT;
}

int armpmu_event_set_period(struct perf_event *event)
{
	struct arm_pmu *armpmu = to_arm_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
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

	/*
	 * Limit the maximum period to prevent the counter value
	 * from overtaking the one we are about to program. In
	 * effect we are reducing max_period to account for
	 * interrupt latency (and we are being very conservative).
	 */
	if (left > (armpmu->max_period >> 1))
		left = armpmu->max_period >> 1;

	local64_set(&hwc->prev_count, (u64)-left);

	armpmu->write_counter(event, (u64)(-left) & 0xffffffff);

	perf_event_update_userpage(event);

	return ret;
}

u64 armpmu_event_update(struct perf_event *event)
{
	struct arm_pmu *armpmu = to_arm_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	u64 delta, prev_raw_count, new_raw_count;

again:
	prev_raw_count = local64_read(&hwc->prev_count);
	new_raw_count = armpmu->read_counter(event);

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
	armpmu_event_update(event);
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
		armpmu->disable(event);
		armpmu_event_update(event);
		hwc->state |= PERF_HES_STOPPED | PERF_HES_UPTODATE;
	}
}

static void armpmu_start(struct perf_event *event, int flags)
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
	armpmu_event_set_period(event);
	armpmu->enable(event);
}

static void
armpmu_del(struct perf_event *event, int flags)
{
	struct arm_pmu *armpmu = to_arm_pmu(event->pmu);
	struct pmu_hw_events *hw_events = this_cpu_ptr(armpmu->hw_events);
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;

	armpmu_stop(event, PERF_EF_UPDATE);
	hw_events->events[idx] = NULL;
	clear_bit(idx, hw_events->used_mask);
	if (armpmu->clear_event_idx)
		armpmu->clear_event_idx(hw_events, event);

	perf_event_update_userpage(event);
}

static int
armpmu_add(struct perf_event *event, int flags)
{
	struct arm_pmu *armpmu = to_arm_pmu(event->pmu);
	struct pmu_hw_events *hw_events = this_cpu_ptr(armpmu->hw_events);
	struct hw_perf_event *hwc = &event->hw;
	int idx;
	int err = 0;

	/* An event following a process won't be stopped earlier */
	if (!cpumask_test_cpu(smp_processor_id(), &armpmu->supported_cpus))
		return -ENOENT;

	perf_pmu_disable(event->pmu);

	/* If we don't have a space for the counter then finish early. */
	idx = armpmu->get_event_idx(hw_events, event);
	if (idx < 0) {
		err = idx;
		goto out;
	}

	/*
	 * If there is an event in the counter we are going to use then make
	 * sure it is disabled.
	 */
	event->hw.idx = idx;
	armpmu->disable(event);
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
validate_event(struct pmu *pmu, struct pmu_hw_events *hw_events,
			       struct perf_event *event)
{
	struct arm_pmu *armpmu;

	if (is_software_event(event))
		return 1;

	/*
	 * Reject groups spanning multiple HW PMUs (e.g. CPU + CCI). The
	 * core perf code won't check that the pmu->ctx == leader->ctx
	 * until after pmu->event_init(event).
	 */
	if (event->pmu != pmu)
		return 0;

	if (event->state < PERF_EVENT_STATE_OFF)
		return 1;

	if (event->state == PERF_EVENT_STATE_OFF && !event->attr.enable_on_exec)
		return 1;

	armpmu = to_arm_pmu(event->pmu);
	return armpmu->get_event_idx(hw_events, event) >= 0;
}

static int
validate_group(struct perf_event *event)
{
	struct perf_event *sibling, *leader = event->group_leader;
	struct pmu_hw_events fake_pmu;

	/*
	 * Initialise the fake PMU. We only need to populate the
	 * used_mask for the purposes of validation.
	 */
	memset(&fake_pmu.used_mask, 0, sizeof(fake_pmu.used_mask));

	if (!validate_event(event->pmu, &fake_pmu, leader))
		return -EINVAL;

	list_for_each_entry(sibling, &leader->sibling_list, group_entry) {
		if (!validate_event(event->pmu, &fake_pmu, sibling))
			return -EINVAL;
	}

	if (!validate_event(event->pmu, &fake_pmu, event))
		return -EINVAL;

	return 0;
}

static struct arm_pmu_platdata *armpmu_get_platdata(struct arm_pmu *armpmu)
{
	struct platform_device *pdev = armpmu->plat_device;

	return pdev ? dev_get_platdata(&pdev->dev) : NULL;
}

static irqreturn_t armpmu_dispatch_irq(int irq, void *dev)
{
	struct arm_pmu *armpmu;
	struct arm_pmu_platdata *plat;
	int ret;
	u64 start_clock, finish_clock;

	/*
	 * we request the IRQ with a (possibly percpu) struct arm_pmu**, but
	 * the handlers expect a struct arm_pmu*. The percpu_irq framework will
	 * do any necessary shifting, we just need to perform the first
	 * dereference.
	 */
	armpmu = *(void **)dev;

	plat = armpmu_get_platdata(armpmu);

	start_clock = sched_clock();
	if (plat && plat->handle_irq)
		ret = plat->handle_irq(irq, armpmu, armpmu->handle_irq);
	else
		ret = armpmu->handle_irq(irq, armpmu);
	finish_clock = sched_clock();

	perf_sample_event_took(finish_clock - start_clock);
	return ret;
}

static void
armpmu_release_hardware(struct arm_pmu *armpmu)
{
	armpmu->free_irq(armpmu);
}

static int
armpmu_reserve_hardware(struct arm_pmu *armpmu)
{
	int err = armpmu->request_irq(armpmu, armpmu_dispatch_irq);
	if (err) {
		armpmu_release_hardware(armpmu);
		return err;
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
	int mapping;

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
		pr_debug("ARM performance counters do not support "
			 "mode exclusion\n");
		return -EOPNOTSUPP;
	}

	/*
	 * Store the event encoding into the config_base field.
	 */
	hwc->config_base	    |= (unsigned long)mapping;

	if (!is_sampling_event(event)) {
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

	if (event->group_leader != event) {
		if (validate_group(event) != 0)
			return -EINVAL;
	}

	return 0;
}

static int armpmu_event_init(struct perf_event *event)
{
	struct arm_pmu *armpmu = to_arm_pmu(event->pmu);
	int err = 0;
	atomic_t *active_events = &armpmu->active_events;

	/*
	 * Reject CPU-affine events for CPUs that are of a different class to
	 * that which this PMU handles. Process-following events (where
	 * event->cpu == -1) can be migrated between CPUs, and thus we have to
	 * reject them later (in armpmu_add) if they're scheduled on a
	 * different class of CPU.
	 */
	if (event->cpu != -1 &&
		!cpumask_test_cpu(event->cpu, &armpmu->supported_cpus))
		return -ENOENT;

	/* does not support taken branch sampling */
	if (has_branch_stack(event))
		return -EOPNOTSUPP;

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
	struct pmu_hw_events *hw_events = this_cpu_ptr(armpmu->hw_events);
	int enabled = bitmap_weight(hw_events->used_mask, armpmu->num_events);

	/* For task-bound events we may be called on other CPUs */
	if (!cpumask_test_cpu(smp_processor_id(), &armpmu->supported_cpus))
		return;

	if (enabled)
		armpmu->start(armpmu);
}

static void armpmu_disable(struct pmu *pmu)
{
	struct arm_pmu *armpmu = to_arm_pmu(pmu);

	/* For task-bound events we may be called on other CPUs */
	if (!cpumask_test_cpu(smp_processor_id(), &armpmu->supported_cpus))
		return;

	armpmu->stop(armpmu);
}

/*
 * In heterogeneous systems, events are specific to a particular
 * microarchitecture, and aren't suitable for another. Thus, only match CPUs of
 * the same microarchitecture.
 */
static int armpmu_filter_match(struct perf_event *event)
{
	struct arm_pmu *armpmu = to_arm_pmu(event->pmu);
	unsigned int cpu = smp_processor_id();
	return cpumask_test_cpu(cpu, &armpmu->supported_cpus);
}

static void armpmu_init(struct arm_pmu *armpmu)
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
		.filter_match	= armpmu_filter_match,
	};
}

int armpmu_register(struct arm_pmu *armpmu, int type)
{
	armpmu_init(armpmu);
	pr_info("enabled with %s PMU driver, %d counters available\n",
			armpmu->name, armpmu->num_events);
	return perf_pmu_register(&armpmu->pmu, armpmu->name, type);
}

/* Set at runtime when we know what CPU type we are. */
static struct arm_pmu *__oprofile_cpu_pmu;

/*
 * Despite the names, these two functions are CPU-specific and are used
 * by the OProfile/perf code.
 */
const char *perf_pmu_name(void)
{
	if (!__oprofile_cpu_pmu)
		return NULL;

	return __oprofile_cpu_pmu->name;
}
EXPORT_SYMBOL_GPL(perf_pmu_name);

int perf_num_counters(void)
{
	int max_events = 0;

	if (__oprofile_cpu_pmu != NULL)
		max_events = __oprofile_cpu_pmu->num_events;

	return max_events;
}
EXPORT_SYMBOL_GPL(perf_num_counters);

static void cpu_pmu_enable_percpu_irq(void *data)
{
	int irq = *(int *)data;

	enable_percpu_irq(irq, IRQ_TYPE_NONE);
}

static void cpu_pmu_disable_percpu_irq(void *data)
{
	int irq = *(int *)data;

	disable_percpu_irq(irq);
}

static void cpu_pmu_free_irq(struct arm_pmu *cpu_pmu)
{
	int i, irq, irqs;
	struct platform_device *pmu_device = cpu_pmu->plat_device;
	struct pmu_hw_events __percpu *hw_events = cpu_pmu->hw_events;

	irqs = min(pmu_device->num_resources, num_possible_cpus());

	irq = platform_get_irq(pmu_device, 0);
	if (irq >= 0 && irq_is_percpu(irq)) {
		on_each_cpu(cpu_pmu_disable_percpu_irq, &irq, 1);
		free_percpu_irq(irq, &hw_events->percpu_pmu);
	} else {
		for (i = 0; i < irqs; ++i) {
			int cpu = i;

			if (cpu_pmu->irq_affinity)
				cpu = cpu_pmu->irq_affinity[i];

			if (!cpumask_test_and_clear_cpu(cpu, &cpu_pmu->active_irqs))
				continue;
			irq = platform_get_irq(pmu_device, i);
			if (irq >= 0)
				free_irq(irq, per_cpu_ptr(&hw_events->percpu_pmu, cpu));
		}
	}
}

static int cpu_pmu_request_irq(struct arm_pmu *cpu_pmu, irq_handler_t handler)
{
	int i, err, irq, irqs;
	struct platform_device *pmu_device = cpu_pmu->plat_device;
	struct pmu_hw_events __percpu *hw_events = cpu_pmu->hw_events;

	if (!pmu_device)
		return -ENODEV;

	irqs = min(pmu_device->num_resources, num_possible_cpus());
	if (irqs < 1) {
		pr_warn_once("perf/ARM: No irqs for PMU defined, sampling events not supported\n");
		return 0;
	}

	irq = platform_get_irq(pmu_device, 0);
	if (irq >= 0 && irq_is_percpu(irq)) {
		err = request_percpu_irq(irq, handler, "arm-pmu",
					 &hw_events->percpu_pmu);
		if (err) {
			pr_err("unable to request IRQ%d for ARM PMU counters\n",
				irq);
			return err;
		}
		on_each_cpu(cpu_pmu_enable_percpu_irq, &irq, 1);
	} else {
		for (i = 0; i < irqs; ++i) {
			int cpu = i;

			err = 0;
			irq = platform_get_irq(pmu_device, i);
			if (irq < 0)
				continue;

			if (cpu_pmu->irq_affinity)
				cpu = cpu_pmu->irq_affinity[i];

			/*
			 * If we have a single PMU interrupt that we can't shift,
			 * assume that we're running on a uniprocessor machine and
			 * continue. Otherwise, continue without this interrupt.
			 */
			if (irq_set_affinity(irq, cpumask_of(cpu)) && irqs > 1) {
				pr_warn("unable to set irq affinity (irq=%d, cpu=%u)\n",
					irq, cpu);
				continue;
			}

			err = request_irq(irq, handler,
					  IRQF_NOBALANCING | IRQF_NO_THREAD, "arm-pmu",
					  per_cpu_ptr(&hw_events->percpu_pmu, cpu));
			if (err) {
				pr_err("unable to request IRQ%d for ARM PMU counters\n",
					irq);
				return err;
			}

			cpumask_set_cpu(cpu, &cpu_pmu->active_irqs);
		}
	}

	return 0;
}

/*
 * PMU hardware loses all context when a CPU goes offline.
 * When a CPU is hotplugged back in, since some hardware registers are
 * UNKNOWN at reset, the PMU must be explicitly reset to avoid reading
 * junk values out of them.
 */
static int cpu_pmu_notify(struct notifier_block *b, unsigned long action,
			  void *hcpu)
{
	int cpu = (unsigned long)hcpu;
	struct arm_pmu *pmu = container_of(b, struct arm_pmu, hotplug_nb);

	if ((action & ~CPU_TASKS_FROZEN) != CPU_STARTING)
		return NOTIFY_DONE;

	if (!cpumask_test_cpu(cpu, &pmu->supported_cpus))
		return NOTIFY_DONE;

	if (pmu->reset)
		pmu->reset(pmu);
	else
		return NOTIFY_DONE;

	return NOTIFY_OK;
}

#ifdef CONFIG_CPU_PM
static void cpu_pm_pmu_setup(struct arm_pmu *armpmu, unsigned long cmd)
{
	struct pmu_hw_events *hw_events = this_cpu_ptr(armpmu->hw_events);
	struct perf_event *event;
	int idx;

	for (idx = 0; idx < armpmu->num_events; idx++) {
		/*
		 * If the counter is not used skip it, there is no
		 * need of stopping/restarting it.
		 */
		if (!test_bit(idx, hw_events->used_mask))
			continue;

		event = hw_events->events[idx];

		switch (cmd) {
		case CPU_PM_ENTER:
			/*
			 * Stop and update the counter
			 */
			armpmu_stop(event, PERF_EF_UPDATE);
			break;
		case CPU_PM_EXIT:
		case CPU_PM_ENTER_FAILED:
			 /*
			  * Restore and enable the counter.
			  * armpmu_start() indirectly calls
			  *
			  * perf_event_update_userpage()
			  *
			  * that requires RCU read locking to be functional,
			  * wrap the call within RCU_NONIDLE to make the
			  * RCU subsystem aware this cpu is not idle from
			  * an RCU perspective for the armpmu_start() call
			  * duration.
			  */
			RCU_NONIDLE(armpmu_start(event, PERF_EF_RELOAD));
			break;
		default:
			break;
		}
	}
}

static int cpu_pm_pmu_notify(struct notifier_block *b, unsigned long cmd,
			     void *v)
{
	struct arm_pmu *armpmu = container_of(b, struct arm_pmu, cpu_pm_nb);
	struct pmu_hw_events *hw_events = this_cpu_ptr(armpmu->hw_events);
	int enabled = bitmap_weight(hw_events->used_mask, armpmu->num_events);

	if (!cpumask_test_cpu(smp_processor_id(), &armpmu->supported_cpus))
		return NOTIFY_DONE;

	/*
	 * Always reset the PMU registers on power-up even if
	 * there are no events running.
	 */
	if (cmd == CPU_PM_EXIT && armpmu->reset)
		armpmu->reset(armpmu);

	if (!enabled)
		return NOTIFY_OK;

	switch (cmd) {
	case CPU_PM_ENTER:
		armpmu->stop(armpmu);
		cpu_pm_pmu_setup(armpmu, cmd);
		break;
	case CPU_PM_EXIT:
		cpu_pm_pmu_setup(armpmu, cmd);
	case CPU_PM_ENTER_FAILED:
		armpmu->start(armpmu);
		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static int cpu_pm_pmu_register(struct arm_pmu *cpu_pmu)
{
	cpu_pmu->cpu_pm_nb.notifier_call = cpu_pm_pmu_notify;
	return cpu_pm_register_notifier(&cpu_pmu->cpu_pm_nb);
}

static void cpu_pm_pmu_unregister(struct arm_pmu *cpu_pmu)
{
	cpu_pm_unregister_notifier(&cpu_pmu->cpu_pm_nb);
}
#else
static inline int cpu_pm_pmu_register(struct arm_pmu *cpu_pmu) { return 0; }
static inline void cpu_pm_pmu_unregister(struct arm_pmu *cpu_pmu) { }
#endif

static int cpu_pmu_init(struct arm_pmu *cpu_pmu)
{
	int err;
	int cpu;
	struct pmu_hw_events __percpu *cpu_hw_events;

	cpu_hw_events = alloc_percpu(struct pmu_hw_events);
	if (!cpu_hw_events)
		return -ENOMEM;

	cpu_pmu->hotplug_nb.notifier_call = cpu_pmu_notify;
	err = register_cpu_notifier(&cpu_pmu->hotplug_nb);
	if (err)
		goto out_hw_events;

	err = cpu_pm_pmu_register(cpu_pmu);
	if (err)
		goto out_unregister;

	for_each_possible_cpu(cpu) {
		struct pmu_hw_events *events = per_cpu_ptr(cpu_hw_events, cpu);
		raw_spin_lock_init(&events->pmu_lock);
		events->percpu_pmu = cpu_pmu;
	}

	cpu_pmu->hw_events	= cpu_hw_events;
	cpu_pmu->request_irq	= cpu_pmu_request_irq;
	cpu_pmu->free_irq	= cpu_pmu_free_irq;

	/* Ensure the PMU has sane values out of reset. */
	if (cpu_pmu->reset)
		on_each_cpu_mask(&cpu_pmu->supported_cpus, cpu_pmu->reset,
			 cpu_pmu, 1);

	/* If no interrupts available, set the corresponding capability flag */
	if (!platform_get_irq(cpu_pmu->plat_device, 0))
		cpu_pmu->pmu.capabilities |= PERF_PMU_CAP_NO_INTERRUPT;

	return 0;

out_unregister:
	unregister_cpu_notifier(&cpu_pmu->hotplug_nb);
out_hw_events:
	free_percpu(cpu_hw_events);
	return err;
}

static void cpu_pmu_destroy(struct arm_pmu *cpu_pmu)
{
	cpu_pm_pmu_unregister(cpu_pmu);
	unregister_cpu_notifier(&cpu_pmu->hotplug_nb);
	free_percpu(cpu_pmu->hw_events);
}

/*
 * CPU PMU identification and probing.
 */
static int probe_current_pmu(struct arm_pmu *pmu,
			     const struct pmu_probe_info *info)
{
	int cpu = get_cpu();
	unsigned int cpuid = read_cpuid_id();
	int ret = -ENODEV;

	pr_info("probing PMU on CPU %d\n", cpu);

	for (; info->init != NULL; info++) {
		if ((cpuid & info->mask) != info->cpuid)
			continue;
		ret = info->init(pmu);
		break;
	}

	put_cpu();
	return ret;
}

static int of_pmu_irq_cfg(struct arm_pmu *pmu)
{
	int *irqs, i = 0;
	bool using_spi = false;
	struct platform_device *pdev = pmu->plat_device;

	irqs = kcalloc(pdev->num_resources, sizeof(*irqs), GFP_KERNEL);
	if (!irqs)
		return -ENOMEM;

	do {
		struct device_node *dn;
		int cpu, irq;

		/* See if we have an affinity entry */
		dn = of_parse_phandle(pdev->dev.of_node, "interrupt-affinity", i);
		if (!dn)
			break;

		/* Check the IRQ type and prohibit a mix of PPIs and SPIs */
		irq = platform_get_irq(pdev, i);
		if (irq >= 0) {
			bool spi = !irq_is_percpu(irq);

			if (i > 0 && spi != using_spi) {
				pr_err("PPI/SPI IRQ type mismatch for %s!\n",
					dn->name);
				of_node_put(dn);
				kfree(irqs);
				return -EINVAL;
			}

			using_spi = spi;
		}

		/* Now look up the logical CPU number */
		for_each_possible_cpu(cpu) {
			struct device_node *cpu_dn;

			cpu_dn = of_cpu_device_node_get(cpu);
			of_node_put(cpu_dn);

			if (dn == cpu_dn)
				break;
		}

		if (cpu >= nr_cpu_ids) {
			pr_warn("Failed to find logical CPU for %s\n",
				dn->name);
			of_node_put(dn);
			cpumask_setall(&pmu->supported_cpus);
			break;
		}
		of_node_put(dn);

		/* For SPIs, we need to track the affinity per IRQ */
		if (using_spi) {
			if (i >= pdev->num_resources) {
				of_node_put(dn);
				break;
			}

			irqs[i] = cpu;
		}

		/* Keep track of the CPUs containing this PMU type */
		cpumask_set_cpu(cpu, &pmu->supported_cpus);
		of_node_put(dn);
		i++;
	} while (1);

	/* If we didn't manage to parse anything, claim to support all CPUs */
	if (cpumask_weight(&pmu->supported_cpus) == 0)
		cpumask_setall(&pmu->supported_cpus);

	/* If we matched up the IRQ affinities, use them to route the SPIs */
	if (using_spi && i == pdev->num_resources)
		pmu->irq_affinity = irqs;
	else
		kfree(irqs);

	return 0;
}

int arm_pmu_device_probe(struct platform_device *pdev,
			 const struct of_device_id *of_table,
			 const struct pmu_probe_info *probe_table)
{
	const struct of_device_id *of_id;
	const int (*init_fn)(struct arm_pmu *);
	struct device_node *node = pdev->dev.of_node;
	struct arm_pmu *pmu;
	int ret = -ENODEV;

	pmu = kzalloc(sizeof(struct arm_pmu), GFP_KERNEL);
	if (!pmu) {
		pr_info("failed to allocate PMU device!\n");
		return -ENOMEM;
	}

	if (!__oprofile_cpu_pmu)
		__oprofile_cpu_pmu = pmu;

	pmu->plat_device = pdev;

	if (node && (of_id = of_match_node(of_table, pdev->dev.of_node))) {
		init_fn = of_id->data;

		ret = of_pmu_irq_cfg(pmu);
		if (!ret)
			ret = init_fn(pmu);
	} else {
		ret = probe_current_pmu(pmu, probe_table);
		cpumask_setall(&pmu->supported_cpus);
	}

	if (ret) {
		pr_info("failed to probe PMU!\n");
		goto out_free;
	}

	ret = cpu_pmu_init(pmu);
	if (ret)
		goto out_free;

	ret = armpmu_register(pmu, -1);
	if (ret)
		goto out_destroy;

	return 0;

out_destroy:
	cpu_pmu_destroy(pmu);
out_free:
	pr_info("failed to register PMU devices!\n");
	kfree(pmu);
	return ret;
}

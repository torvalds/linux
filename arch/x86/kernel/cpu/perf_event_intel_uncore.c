#include "perf_event_intel_uncore.h"

static struct intel_uncore_type *empty_uncore[] = { NULL, };
static struct intel_uncore_type **msr_uncores = empty_uncore;

/* mask of cpus that collect uncore events */
static cpumask_t uncore_cpu_mask;

/* constraint for the fixed counter */
static struct event_constraint constraint_fixed =
	EVENT_CONSTRAINT(~0ULL, 1 << UNCORE_PMC_IDX_FIXED, ~0ULL);

static void uncore_assign_hw_event(struct intel_uncore_box *box,
				struct perf_event *event, int idx)
{
	struct hw_perf_event *hwc = &event->hw;

	hwc->idx = idx;
	hwc->last_tag = ++box->tags[idx];

	if (hwc->idx == UNCORE_PMC_IDX_FIXED) {
		hwc->event_base = uncore_msr_fixed_ctr(box);
		hwc->config_base = uncore_msr_fixed_ctl(box);
		return;
	}

	hwc->config_base = uncore_msr_event_ctl(box, hwc->idx);
	hwc->event_base =  uncore_msr_perf_ctr(box, hwc->idx);
}

static void uncore_perf_event_update(struct intel_uncore_box *box,
					struct perf_event *event)
{
	u64 prev_count, new_count, delta;
	int shift;

	if (event->hw.idx >= UNCORE_PMC_IDX_FIXED)
		shift = 64 - uncore_fixed_ctr_bits(box);
	else
		shift = 64 - uncore_perf_ctr_bits(box);

	/* the hrtimer might modify the previous event value */
again:
	prev_count = local64_read(&event->hw.prev_count);
	new_count = uncore_read_counter(box, event);
	if (local64_xchg(&event->hw.prev_count, new_count) != prev_count)
		goto again;

	delta = (new_count << shift) - (prev_count << shift);
	delta >>= shift;

	local64_add(delta, &event->count);
}

/*
 * The overflow interrupt is unavailable for SandyBridge-EP, is broken
 * for SandyBridge. So we use hrtimer to periodically poll the counter
 * to avoid overflow.
 */
static enum hrtimer_restart uncore_pmu_hrtimer(struct hrtimer *hrtimer)
{
	struct intel_uncore_box *box;
	unsigned long flags;
	int bit;

	box = container_of(hrtimer, struct intel_uncore_box, hrtimer);
	if (!box->n_active || box->cpu != smp_processor_id())
		return HRTIMER_NORESTART;
	/*
	 * disable local interrupt to prevent uncore_pmu_event_start/stop
	 * to interrupt the update process
	 */
	local_irq_save(flags);

	for_each_set_bit(bit, box->active_mask, UNCORE_PMC_IDX_MAX)
		uncore_perf_event_update(box, box->events[bit]);

	local_irq_restore(flags);

	hrtimer_forward_now(hrtimer, ns_to_ktime(UNCORE_PMU_HRTIMER_INTERVAL));
	return HRTIMER_RESTART;
}

static void uncore_pmu_start_hrtimer(struct intel_uncore_box *box)
{
	__hrtimer_start_range_ns(&box->hrtimer,
			ns_to_ktime(UNCORE_PMU_HRTIMER_INTERVAL), 0,
			HRTIMER_MODE_REL_PINNED, 0);
}

static void uncore_pmu_cancel_hrtimer(struct intel_uncore_box *box)
{
	hrtimer_cancel(&box->hrtimer);
}

static void uncore_pmu_init_hrtimer(struct intel_uncore_box *box)
{
	hrtimer_init(&box->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	box->hrtimer.function = uncore_pmu_hrtimer;
}

struct intel_uncore_box *uncore_alloc_box(int cpu)
{
	struct intel_uncore_box *box;

	box = kmalloc_node(sizeof(*box), GFP_KERNEL | __GFP_ZERO,
			   cpu_to_node(cpu));
	if (!box)
		return NULL;

	uncore_pmu_init_hrtimer(box);
	atomic_set(&box->refcnt, 1);
	box->cpu = -1;
	box->phys_id = -1;

	return box;
}

static struct intel_uncore_box *
uncore_pmu_to_box(struct intel_uncore_pmu *pmu, int cpu)
{
	return *per_cpu_ptr(pmu->box, cpu);
}

static struct intel_uncore_pmu *uncore_event_to_pmu(struct perf_event *event)
{
	return container_of(event->pmu, struct intel_uncore_pmu, pmu);
}

static struct intel_uncore_box *uncore_event_to_box(struct perf_event *event)
{
	/*
	 * perf core schedules event on the basis of cpu, uncore events are
	 * collected by one of the cpus inside a physical package.
	 */
	return uncore_pmu_to_box(uncore_event_to_pmu(event),
				 smp_processor_id());
}

static int uncore_collect_events(struct intel_uncore_box *box,
				struct perf_event *leader, bool dogrp)
{
	struct perf_event *event;
	int n, max_count;

	max_count = box->pmu->type->num_counters;
	if (box->pmu->type->fixed_ctl)
		max_count++;

	if (box->n_events >= max_count)
		return -EINVAL;

	n = box->n_events;
	box->event_list[n] = leader;
	n++;
	if (!dogrp)
		return n;

	list_for_each_entry(event, &leader->sibling_list, group_entry) {
		if (event->state <= PERF_EVENT_STATE_OFF)
			continue;

		if (n >= max_count)
			return -EINVAL;

		box->event_list[n] = event;
		n++;
	}
	return n;
}

static struct event_constraint *
uncore_event_constraint(struct intel_uncore_type *type,
			struct perf_event *event)
{
	struct event_constraint *c;

	if (event->hw.config == ~0ULL)
		return &constraint_fixed;

	if (type->constraints) {
		for_each_event_constraint(c, type->constraints) {
			if ((event->hw.config & c->cmask) == c->code)
				return c;
		}
	}

	return &type->unconstrainted;
}

static int uncore_assign_events(struct intel_uncore_box *box,
				int assign[], int n)
{
	unsigned long used_mask[BITS_TO_LONGS(UNCORE_PMC_IDX_MAX)];
	struct event_constraint *c, *constraints[UNCORE_PMC_IDX_MAX];
	int i, ret, wmin, wmax;
	struct hw_perf_event *hwc;

	bitmap_zero(used_mask, UNCORE_PMC_IDX_MAX);

	for (i = 0, wmin = UNCORE_PMC_IDX_MAX, wmax = 0; i < n; i++) {
		c = uncore_event_constraint(box->pmu->type,
				box->event_list[i]);
		constraints[i] = c;
		wmin = min(wmin, c->weight);
		wmax = max(wmax, c->weight);
	}

	/* fastpath, try to reuse previous register */
	for (i = 0; i < n; i++) {
		hwc = &box->event_list[i]->hw;
		c = constraints[i];

		/* never assigned */
		if (hwc->idx == -1)
			break;

		/* constraint still honored */
		if (!test_bit(hwc->idx, c->idxmsk))
			break;

		/* not already used */
		if (test_bit(hwc->idx, used_mask))
			break;

		__set_bit(hwc->idx, used_mask);
		assign[i] = hwc->idx;
	}
	if (i == n)
		return 0;

	/* slow path */
	ret = perf_assign_events(constraints, n, wmin, wmax, assign);
	return ret ? -EINVAL : 0;
}

static void uncore_pmu_event_start(struct perf_event *event, int flags)
{
	struct intel_uncore_box *box = uncore_event_to_box(event);
	int idx = event->hw.idx;

	if (WARN_ON_ONCE(!(event->hw.state & PERF_HES_STOPPED)))
		return;

	if (WARN_ON_ONCE(idx == -1 || idx >= UNCORE_PMC_IDX_MAX))
		return;

	event->hw.state = 0;
	box->events[idx] = event;
	box->n_active++;
	__set_bit(idx, box->active_mask);

	local64_set(&event->hw.prev_count, uncore_read_counter(box, event));
	uncore_enable_event(box, event);

	if (box->n_active == 1) {
		uncore_enable_box(box);
		uncore_pmu_start_hrtimer(box);
	}
}

static void uncore_pmu_event_stop(struct perf_event *event, int flags)
{
	struct intel_uncore_box *box = uncore_event_to_box(event);
	struct hw_perf_event *hwc = &event->hw;

	if (__test_and_clear_bit(hwc->idx, box->active_mask)) {
		uncore_disable_event(box, event);
		box->n_active--;
		box->events[hwc->idx] = NULL;
		WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
		hwc->state |= PERF_HES_STOPPED;

		if (box->n_active == 0) {
			uncore_disable_box(box);
			uncore_pmu_cancel_hrtimer(box);
		}
	}

	if ((flags & PERF_EF_UPDATE) && !(hwc->state & PERF_HES_UPTODATE)) {
		/*
		 * Drain the remaining delta count out of a event
		 * that we are disabling:
		 */
		uncore_perf_event_update(box, event);
		hwc->state |= PERF_HES_UPTODATE;
	}
}

static int uncore_pmu_event_add(struct perf_event *event, int flags)
{
	struct intel_uncore_box *box = uncore_event_to_box(event);
	struct hw_perf_event *hwc = &event->hw;
	int assign[UNCORE_PMC_IDX_MAX];
	int i, n, ret;

	if (!box)
		return -ENODEV;

	ret = n = uncore_collect_events(box, event, false);
	if (ret < 0)
		return ret;

	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;
	if (!(flags & PERF_EF_START))
		hwc->state |= PERF_HES_ARCH;

	ret = uncore_assign_events(box, assign, n);
	if (ret)
		return ret;

	/* save events moving to new counters */
	for (i = 0; i < box->n_events; i++) {
		event = box->event_list[i];
		hwc = &event->hw;

		if (hwc->idx == assign[i] &&
			hwc->last_tag == box->tags[assign[i]])
			continue;
		/*
		 * Ensure we don't accidentally enable a stopped
		 * counter simply because we rescheduled.
		 */
		if (hwc->state & PERF_HES_STOPPED)
			hwc->state |= PERF_HES_ARCH;

		uncore_pmu_event_stop(event, PERF_EF_UPDATE);
	}

	/* reprogram moved events into new counters */
	for (i = 0; i < n; i++) {
		event = box->event_list[i];
		hwc = &event->hw;

		if (hwc->idx != assign[i] ||
			hwc->last_tag != box->tags[assign[i]])
			uncore_assign_hw_event(box, event, assign[i]);
		else if (i < box->n_events)
			continue;

		if (hwc->state & PERF_HES_ARCH)
			continue;

		uncore_pmu_event_start(event, 0);
	}
	box->n_events = n;

	return 0;
}

static void uncore_pmu_event_del(struct perf_event *event, int flags)
{
	struct intel_uncore_box *box = uncore_event_to_box(event);
	int i;

	uncore_pmu_event_stop(event, PERF_EF_UPDATE);

	for (i = 0; i < box->n_events; i++) {
		if (event == box->event_list[i]) {
			while (++i < box->n_events)
				box->event_list[i - 1] = box->event_list[i];

			--box->n_events;
			break;
		}
	}

	event->hw.idx = -1;
	event->hw.last_tag = ~0ULL;
}

static void uncore_pmu_event_read(struct perf_event *event)
{
	struct intel_uncore_box *box = uncore_event_to_box(event);
	uncore_perf_event_update(box, event);
}

/*
 * validation ensures the group can be loaded onto the
 * PMU if it was the only group available.
 */
static int uncore_validate_group(struct intel_uncore_pmu *pmu,
				struct perf_event *event)
{
	struct perf_event *leader = event->group_leader;
	struct intel_uncore_box *fake_box;
	int assign[UNCORE_PMC_IDX_MAX];
	int ret = -EINVAL, n;

	fake_box = uncore_alloc_box(smp_processor_id());
	if (!fake_box)
		return -ENOMEM;

	fake_box->pmu = pmu;
	/*
	 * the event is not yet connected with its
	 * siblings therefore we must first collect
	 * existing siblings, then add the new event
	 * before we can simulate the scheduling
	 */
	n = uncore_collect_events(fake_box, leader, true);
	if (n < 0)
		goto out;

	fake_box->n_events = n;
	n = uncore_collect_events(fake_box, event, false);
	if (n < 0)
		goto out;

	fake_box->n_events = n;

	ret = uncore_assign_events(fake_box, assign, n);
out:
	kfree(fake_box);
	return ret;
}

int uncore_pmu_event_init(struct perf_event *event)
{
	struct intel_uncore_pmu *pmu;
	struct intel_uncore_box *box;
	struct hw_perf_event *hwc = &event->hw;
	int ret;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	pmu = uncore_event_to_pmu(event);
	/* no device found for this pmu */
	if (pmu->func_id < 0)
		return -ENOENT;

	/*
	 * Uncore PMU does measure at all privilege level all the time.
	 * So it doesn't make sense to specify any exclude bits.
	 */
	if (event->attr.exclude_user || event->attr.exclude_kernel ||
			event->attr.exclude_hv || event->attr.exclude_idle)
		return -EINVAL;

	/* Sampling not supported yet */
	if (hwc->sample_period)
		return -EINVAL;

	/*
	 * Place all uncore events for a particular physical package
	 * onto a single cpu
	 */
	if (event->cpu < 0)
		return -EINVAL;
	box = uncore_pmu_to_box(pmu, event->cpu);
	if (!box || box->cpu < 0)
		return -EINVAL;
	event->cpu = box->cpu;

	if (event->attr.config == UNCORE_FIXED_EVENT) {
		/* no fixed counter */
		if (!pmu->type->fixed_ctl)
			return -EINVAL;
		/*
		 * if there is only one fixed counter, only the first pmu
		 * can access the fixed counter
		 */
		if (pmu->type->single_fixed && pmu->pmu_idx > 0)
			return -EINVAL;
		hwc->config = ~0ULL;
	} else {
		hwc->config = event->attr.config & pmu->type->event_mask;
	}

	event->hw.idx = -1;
	event->hw.last_tag = ~0ULL;

	if (event->group_leader != event)
		ret = uncore_validate_group(pmu, event);
	else
		ret = 0;

	return ret;
}

static int __init uncore_pmu_register(struct intel_uncore_pmu *pmu)
{
	int ret;

	pmu->pmu = (struct pmu) {
		.attr_groups	= pmu->type->attr_groups,
		.task_ctx_nr	= perf_invalid_context,
		.event_init	= uncore_pmu_event_init,
		.add		= uncore_pmu_event_add,
		.del		= uncore_pmu_event_del,
		.start		= uncore_pmu_event_start,
		.stop		= uncore_pmu_event_stop,
		.read		= uncore_pmu_event_read,
	};

	if (pmu->type->num_boxes == 1) {
		if (strlen(pmu->type->name) > 0)
			sprintf(pmu->name, "uncore_%s", pmu->type->name);
		else
			sprintf(pmu->name, "uncore");
	} else {
		sprintf(pmu->name, "uncore_%s_%d", pmu->type->name,
			pmu->pmu_idx);
	}

	ret = perf_pmu_register(&pmu->pmu, pmu->name, -1);
	return ret;
}

static void __init uncore_type_exit(struct intel_uncore_type *type)
{
	int i;

	for (i = 0; i < type->num_boxes; i++)
		free_percpu(type->pmus[i].box);
	kfree(type->pmus);
	type->pmus = NULL;
	kfree(type->attr_groups[1]);
	type->attr_groups[1] = NULL;
}

static int __init uncore_type_init(struct intel_uncore_type *type)
{
	struct intel_uncore_pmu *pmus;
	struct attribute_group *events_group;
	struct attribute **attrs;
	int i, j;

	pmus = kzalloc(sizeof(*pmus) * type->num_boxes, GFP_KERNEL);
	if (!pmus)
		return -ENOMEM;

	type->unconstrainted = (struct event_constraint)
		__EVENT_CONSTRAINT(0, (1ULL << type->num_counters) - 1,
				0, type->num_counters, 0);

	for (i = 0; i < type->num_boxes; i++) {
		pmus[i].func_id = -1;
		pmus[i].pmu_idx = i;
		pmus[i].type = type;
		pmus[i].box = alloc_percpu(struct intel_uncore_box *);
		if (!pmus[i].box)
			goto fail;
	}

	if (type->event_descs) {
		i = 0;
		while (type->event_descs[i].attr.attr.name)
			i++;

		events_group = kzalloc(sizeof(struct attribute *) * (i + 1) +
					sizeof(*events_group), GFP_KERNEL);
		if (!events_group)
			goto fail;

		attrs = (struct attribute **)(events_group + 1);
		events_group->name = "events";
		events_group->attrs = attrs;

		for (j = 0; j < i; j++)
			attrs[j] = &type->event_descs[j].attr.attr;

		type->attr_groups[1] = events_group;
	}

	type->pmus = pmus;
	return 0;
fail:
	uncore_type_exit(type);
	return -ENOMEM;
}

static int __init uncore_types_init(struct intel_uncore_type **types)
{
	int i, ret;

	for (i = 0; types[i]; i++) {
		ret = uncore_type_init(types[i]);
		if (ret)
			goto fail;
	}
	return 0;
fail:
	while (--i >= 0)
		uncore_type_exit(types[i]);
	return ret;
}

static void __cpuinit uncore_cpu_dying(int cpu)
{
	struct intel_uncore_type *type;
	struct intel_uncore_pmu *pmu;
	struct intel_uncore_box *box;
	int i, j;

	for (i = 0; msr_uncores[i]; i++) {
		type = msr_uncores[i];
		for (j = 0; j < type->num_boxes; j++) {
			pmu = &type->pmus[j];
			box = *per_cpu_ptr(pmu->box, cpu);
			*per_cpu_ptr(pmu->box, cpu) = NULL;
			if (box && atomic_dec_and_test(&box->refcnt))
				kfree(box);
		}
	}
}

static int __cpuinit uncore_cpu_starting(int cpu)
{
	struct intel_uncore_type *type;
	struct intel_uncore_pmu *pmu;
	struct intel_uncore_box *box, *exist;
	int i, j, k, phys_id;

	phys_id = topology_physical_package_id(cpu);

	for (i = 0; msr_uncores[i]; i++) {
		type = msr_uncores[i];
		for (j = 0; j < type->num_boxes; j++) {
			pmu = &type->pmus[j];
			box = *per_cpu_ptr(pmu->box, cpu);
			/* called by uncore_cpu_init? */
			if (box && box->phys_id >= 0) {
				uncore_box_init(box);
				continue;
			}

			for_each_online_cpu(k) {
				exist = *per_cpu_ptr(pmu->box, k);
				if (exist && exist->phys_id == phys_id) {
					atomic_inc(&exist->refcnt);
					*per_cpu_ptr(pmu->box, cpu) = exist;
					kfree(box);
					box = NULL;
					break;
				}
			}

			if (box) {
				box->phys_id = phys_id;
				uncore_box_init(box);
			}
		}
	}
	return 0;
}

static int __cpuinit uncore_cpu_prepare(int cpu, int phys_id)
{
	struct intel_uncore_type *type;
	struct intel_uncore_pmu *pmu;
	struct intel_uncore_box *box;
	int i, j;

	for (i = 0; msr_uncores[i]; i++) {
		type = msr_uncores[i];
		for (j = 0; j < type->num_boxes; j++) {
			pmu = &type->pmus[j];
			if (pmu->func_id < 0)
				pmu->func_id = j;

			box = uncore_alloc_box(cpu);
			if (!box)
				return -ENOMEM;

			box->pmu = pmu;
			box->phys_id = phys_id;
			*per_cpu_ptr(pmu->box, cpu) = box;
		}
	}
	return 0;
}

static void __cpuinit uncore_change_context(struct intel_uncore_type **uncores,
					    int old_cpu, int new_cpu)
{
	struct intel_uncore_type *type;
	struct intel_uncore_pmu *pmu;
	struct intel_uncore_box *box;
	int i, j;

	for (i = 0; uncores[i]; i++) {
		type = uncores[i];
		for (j = 0; j < type->num_boxes; j++) {
			pmu = &type->pmus[j];
			if (old_cpu < 0)
				box = uncore_pmu_to_box(pmu, new_cpu);
			else
				box = uncore_pmu_to_box(pmu, old_cpu);
			if (!box)
				continue;

			if (old_cpu < 0) {
				WARN_ON_ONCE(box->cpu != -1);
				box->cpu = new_cpu;
				continue;
			}

			WARN_ON_ONCE(box->cpu != old_cpu);
			if (new_cpu >= 0) {
				uncore_pmu_cancel_hrtimer(box);
				perf_pmu_migrate_context(&pmu->pmu,
						old_cpu, new_cpu);
				box->cpu = new_cpu;
			} else {
				box->cpu = -1;
			}
		}
	}
}

static void __cpuinit uncore_event_exit_cpu(int cpu)
{
	int i, phys_id, target;

	/* if exiting cpu is used for collecting uncore events */
	if (!cpumask_test_and_clear_cpu(cpu, &uncore_cpu_mask))
		return;

	/* find a new cpu to collect uncore events */
	phys_id = topology_physical_package_id(cpu);
	target = -1;
	for_each_online_cpu(i) {
		if (i == cpu)
			continue;
		if (phys_id == topology_physical_package_id(i)) {
			target = i;
			break;
		}
	}

	/* migrate uncore events to the new cpu */
	if (target >= 0)
		cpumask_set_cpu(target, &uncore_cpu_mask);

	uncore_change_context(msr_uncores, cpu, target);
}

static void __cpuinit uncore_event_init_cpu(int cpu)
{
	int i, phys_id;

	phys_id = topology_physical_package_id(cpu);
	for_each_cpu(i, &uncore_cpu_mask) {
		if (phys_id == topology_physical_package_id(i))
			return;
	}

	cpumask_set_cpu(cpu, &uncore_cpu_mask);

	uncore_change_context(msr_uncores, -1, cpu);
}

static int __cpuinit uncore_cpu_notifier(struct notifier_block *self,
					 unsigned long action, void *hcpu)
{
	unsigned int cpu = (long)hcpu;

	/* allocate/free data structure for uncore box */
	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_UP_PREPARE:
		uncore_cpu_prepare(cpu, -1);
		break;
	case CPU_STARTING:
		uncore_cpu_starting(cpu);
		break;
	case CPU_UP_CANCELED:
	case CPU_DYING:
		uncore_cpu_dying(cpu);
		break;
	default:
		break;
	}

	/* select the cpu that collects uncore events */
	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_DOWN_FAILED:
	case CPU_STARTING:
		uncore_event_init_cpu(cpu);
		break;
	case CPU_DOWN_PREPARE:
		uncore_event_exit_cpu(cpu);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block uncore_cpu_nb __cpuinitdata = {
	.notifier_call = uncore_cpu_notifier,
	/*
	 * to migrate uncore events, our notifier should be executed
	 * before perf core's notifier.
	 */
	.priority = CPU_PRI_PERF + 1,
};

static void __init uncore_cpu_setup(void *dummy)
{
	uncore_cpu_starting(smp_processor_id());
}

static int __init uncore_cpu_init(void)
{
	int ret, cpu;

	switch (boot_cpu_data.x86_model) {
	default:
		return 0;
	}

	ret = uncore_types_init(msr_uncores);
	if (ret)
		return ret;

	get_online_cpus();

	for_each_online_cpu(cpu) {
		int i, phys_id = topology_physical_package_id(cpu);

		for_each_cpu(i, &uncore_cpu_mask) {
			if (phys_id == topology_physical_package_id(i)) {
				phys_id = -1;
				break;
			}
		}
		if (phys_id < 0)
			continue;

		uncore_cpu_prepare(cpu, phys_id);
		uncore_event_init_cpu(cpu);
	}
	on_each_cpu(uncore_cpu_setup, NULL, 1);

	register_cpu_notifier(&uncore_cpu_nb);

	put_online_cpus();

	return 0;
}

static int __init uncore_pmus_register(void)
{
	struct intel_uncore_pmu *pmu;
	struct intel_uncore_type *type;
	int i, j;

	for (i = 0; msr_uncores[i]; i++) {
		type = msr_uncores[i];
		for (j = 0; j < type->num_boxes; j++) {
			pmu = &type->pmus[j];
			uncore_pmu_register(pmu);
		}
	}

	return 0;
}

static int __init intel_uncore_init(void)
{
	int ret;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL)
		return -ENODEV;

	ret = uncore_cpu_init();
	if (ret)
		goto fail;

	uncore_pmus_register();
	return 0;
fail:
	return ret;
}
device_initcall(intel_uncore_init);

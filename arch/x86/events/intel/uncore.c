// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include "uncore.h"
#include "uncore_discovery.h"

static bool uncore_no_discover;
module_param(uncore_no_discover, bool, 0);
MODULE_PARM_DESC(uncore_no_discover, "Don't enable the Intel uncore PerfMon discovery mechanism "
				     "(default: enable the discovery mechanism).");
struct intel_uncore_type *empty_uncore[] = { NULL, };
struct intel_uncore_type **uncore_msr_uncores = empty_uncore;
struct intel_uncore_type **uncore_pci_uncores = empty_uncore;
struct intel_uncore_type **uncore_mmio_uncores = empty_uncore;

static bool pcidrv_registered;
struct pci_driver *uncore_pci_driver;
/* The PCI driver for the device which the uncore doesn't own. */
struct pci_driver *uncore_pci_sub_driver;
/* pci bus to socket mapping */
DEFINE_RAW_SPINLOCK(pci2phy_map_lock);
struct list_head pci2phy_map_head = LIST_HEAD_INIT(pci2phy_map_head);
struct pci_extra_dev *uncore_extra_pci_dev;
int __uncore_max_dies;

/* mask of cpus that collect uncore events */
static cpumask_t uncore_cpu_mask;

/* constraint for the fixed counter */
static struct event_constraint uncore_constraint_fixed =
	EVENT_CONSTRAINT(~0ULL, 1 << UNCORE_PMC_IDX_FIXED, ~0ULL);
struct event_constraint uncore_constraint_empty =
	EVENT_CONSTRAINT(0, 0, 0);

MODULE_LICENSE("GPL");

int uncore_pcibus_to_dieid(struct pci_bus *bus)
{
	struct pci2phy_map *map;
	int die_id = -1;

	raw_spin_lock(&pci2phy_map_lock);
	list_for_each_entry(map, &pci2phy_map_head, list) {
		if (map->segment == pci_domain_nr(bus)) {
			die_id = map->pbus_to_dieid[bus->number];
			break;
		}
	}
	raw_spin_unlock(&pci2phy_map_lock);

	return die_id;
}

int uncore_die_to_segment(int die)
{
	struct pci_bus *bus = NULL;

	/* Find first pci bus which attributes to specified die. */
	while ((bus = pci_find_next_bus(bus)) &&
	       (die != uncore_pcibus_to_dieid(bus)))
		;

	return bus ? pci_domain_nr(bus) : -EINVAL;
}

static void uncore_free_pcibus_map(void)
{
	struct pci2phy_map *map, *tmp;

	list_for_each_entry_safe(map, tmp, &pci2phy_map_head, list) {
		list_del(&map->list);
		kfree(map);
	}
}

struct pci2phy_map *__find_pci2phy_map(int segment)
{
	struct pci2phy_map *map, *alloc = NULL;
	int i;

	lockdep_assert_held(&pci2phy_map_lock);

lookup:
	list_for_each_entry(map, &pci2phy_map_head, list) {
		if (map->segment == segment)
			goto end;
	}

	if (!alloc) {
		raw_spin_unlock(&pci2phy_map_lock);
		alloc = kmalloc(sizeof(struct pci2phy_map), GFP_KERNEL);
		raw_spin_lock(&pci2phy_map_lock);

		if (!alloc)
			return NULL;

		goto lookup;
	}

	map = alloc;
	alloc = NULL;
	map->segment = segment;
	for (i = 0; i < 256; i++)
		map->pbus_to_dieid[i] = -1;
	list_add_tail(&map->list, &pci2phy_map_head);

end:
	kfree(alloc);
	return map;
}

ssize_t uncore_event_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct uncore_event_desc *event =
		container_of(attr, struct uncore_event_desc, attr);
	return sprintf(buf, "%s", event->config);
}

struct intel_uncore_box *uncore_pmu_to_box(struct intel_uncore_pmu *pmu, int cpu)
{
	unsigned int dieid = topology_logical_die_id(cpu);

	/*
	 * The unsigned check also catches the '-1' return value for non
	 * existent mappings in the topology map.
	 */
	return dieid < uncore_max_dies() ? pmu->boxes[dieid] : NULL;
}

u64 uncore_msr_read_counter(struct intel_uncore_box *box, struct perf_event *event)
{
	u64 count;

	rdmsrl(event->hw.event_base, count);

	return count;
}

void uncore_mmio_exit_box(struct intel_uncore_box *box)
{
	if (box->io_addr)
		iounmap(box->io_addr);
}

u64 uncore_mmio_read_counter(struct intel_uncore_box *box,
			     struct perf_event *event)
{
	if (!box->io_addr)
		return 0;

	if (!uncore_mmio_is_valid_offset(box, event->hw.event_base))
		return 0;

	return readq(box->io_addr + event->hw.event_base);
}

/*
 * generic get constraint function for shared match/mask registers.
 */
struct event_constraint *
uncore_get_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct intel_uncore_extra_reg *er;
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;
	struct hw_perf_event_extra *reg2 = &event->hw.branch_reg;
	unsigned long flags;
	bool ok = false;

	/*
	 * reg->alloc can be set due to existing state, so for fake box we
	 * need to ignore this, otherwise we might fail to allocate proper
	 * fake state for this extra reg constraint.
	 */
	if (reg1->idx == EXTRA_REG_NONE ||
	    (!uncore_box_is_fake(box) && reg1->alloc))
		return NULL;

	er = &box->shared_regs[reg1->idx];
	raw_spin_lock_irqsave(&er->lock, flags);
	if (!atomic_read(&er->ref) ||
	    (er->config1 == reg1->config && er->config2 == reg2->config)) {
		atomic_inc(&er->ref);
		er->config1 = reg1->config;
		er->config2 = reg2->config;
		ok = true;
	}
	raw_spin_unlock_irqrestore(&er->lock, flags);

	if (ok) {
		if (!uncore_box_is_fake(box))
			reg1->alloc = 1;
		return NULL;
	}

	return &uncore_constraint_empty;
}

void uncore_put_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct intel_uncore_extra_reg *er;
	struct hw_perf_event_extra *reg1 = &event->hw.extra_reg;

	/*
	 * Only put constraint if extra reg was actually allocated. Also
	 * takes care of event which do not use an extra shared reg.
	 *
	 * Also, if this is a fake box we shouldn't touch any event state
	 * (reg->alloc) and we don't care about leaving inconsistent box
	 * state either since it will be thrown out.
	 */
	if (uncore_box_is_fake(box) || !reg1->alloc)
		return;

	er = &box->shared_regs[reg1->idx];
	atomic_dec(&er->ref);
	reg1->alloc = 0;
}

u64 uncore_shared_reg_config(struct intel_uncore_box *box, int idx)
{
	struct intel_uncore_extra_reg *er;
	unsigned long flags;
	u64 config;

	er = &box->shared_regs[idx];

	raw_spin_lock_irqsave(&er->lock, flags);
	config = er->config;
	raw_spin_unlock_irqrestore(&er->lock, flags);

	return config;
}

static void uncore_assign_hw_event(struct intel_uncore_box *box,
				   struct perf_event *event, int idx)
{
	struct hw_perf_event *hwc = &event->hw;

	hwc->idx = idx;
	hwc->last_tag = ++box->tags[idx];

	if (uncore_pmc_fixed(hwc->idx)) {
		hwc->event_base = uncore_fixed_ctr(box);
		hwc->config_base = uncore_fixed_ctl(box);
		return;
	}

	hwc->config_base = uncore_event_ctl(box, hwc->idx);
	hwc->event_base  = uncore_perf_ctr(box, hwc->idx);
}

void uncore_perf_event_update(struct intel_uncore_box *box, struct perf_event *event)
{
	u64 prev_count, new_count, delta;
	int shift;

	if (uncore_pmc_freerunning(event->hw.idx))
		shift = 64 - uncore_freerunning_bits(box, event);
	else if (uncore_pmc_fixed(event->hw.idx))
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
	struct perf_event *event;
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

	/*
	 * handle boxes with an active event list as opposed to active
	 * counters
	 */
	list_for_each_entry(event, &box->active_list, active_entry) {
		uncore_perf_event_update(box, event);
	}

	for_each_set_bit(bit, box->active_mask, UNCORE_PMC_IDX_MAX)
		uncore_perf_event_update(box, box->events[bit]);

	local_irq_restore(flags);

	hrtimer_forward_now(hrtimer, ns_to_ktime(box->hrtimer_duration));
	return HRTIMER_RESTART;
}

void uncore_pmu_start_hrtimer(struct intel_uncore_box *box)
{
	hrtimer_start(&box->hrtimer, ns_to_ktime(box->hrtimer_duration),
		      HRTIMER_MODE_REL_PINNED);
}

void uncore_pmu_cancel_hrtimer(struct intel_uncore_box *box)
{
	hrtimer_cancel(&box->hrtimer);
}

static void uncore_pmu_init_hrtimer(struct intel_uncore_box *box)
{
	hrtimer_init(&box->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	box->hrtimer.function = uncore_pmu_hrtimer;
}

static struct intel_uncore_box *uncore_alloc_box(struct intel_uncore_type *type,
						 int node)
{
	int i, size, numshared = type->num_shared_regs ;
	struct intel_uncore_box *box;

	size = sizeof(*box) + numshared * sizeof(struct intel_uncore_extra_reg);

	box = kzalloc_node(size, GFP_KERNEL, node);
	if (!box)
		return NULL;

	for (i = 0; i < numshared; i++)
		raw_spin_lock_init(&box->shared_regs[i].lock);

	uncore_pmu_init_hrtimer(box);
	box->cpu = -1;
	box->dieid = -1;

	/* set default hrtimer timeout */
	box->hrtimer_duration = UNCORE_PMU_HRTIMER_INTERVAL;

	INIT_LIST_HEAD(&box->active_list);

	return box;
}

/*
 * Using uncore_pmu_event_init pmu event_init callback
 * as a detection point for uncore events.
 */
static int uncore_pmu_event_init(struct perf_event *event);

static bool is_box_event(struct intel_uncore_box *box, struct perf_event *event)
{
	return &box->pmu->pmu == event->pmu;
}

static int
uncore_collect_events(struct intel_uncore_box *box, struct perf_event *leader,
		      bool dogrp)
{
	struct perf_event *event;
	int n, max_count;

	max_count = box->pmu->type->num_counters;
	if (box->pmu->type->fixed_ctl)
		max_count++;

	if (box->n_events >= max_count)
		return -EINVAL;

	n = box->n_events;

	if (is_box_event(box, leader)) {
		box->event_list[n] = leader;
		n++;
	}

	if (!dogrp)
		return n;

	for_each_sibling_event(event, leader) {
		if (!is_box_event(box, event) ||
		    event->state <= PERF_EVENT_STATE_OFF)
			continue;

		if (n >= max_count)
			return -EINVAL;

		box->event_list[n] = event;
		n++;
	}
	return n;
}

static struct event_constraint *
uncore_get_event_constraint(struct intel_uncore_box *box, struct perf_event *event)
{
	struct intel_uncore_type *type = box->pmu->type;
	struct event_constraint *c;

	if (type->ops->get_constraint) {
		c = type->ops->get_constraint(box, event);
		if (c)
			return c;
	}

	if (event->attr.config == UNCORE_FIXED_EVENT)
		return &uncore_constraint_fixed;

	if (type->constraints) {
		for_each_event_constraint(c, type->constraints) {
			if ((event->hw.config & c->cmask) == c->code)
				return c;
		}
	}

	return &type->unconstrainted;
}

static void uncore_put_event_constraint(struct intel_uncore_box *box,
					struct perf_event *event)
{
	if (box->pmu->type->ops->put_constraint)
		box->pmu->type->ops->put_constraint(box, event);
}

static int uncore_assign_events(struct intel_uncore_box *box, int assign[], int n)
{
	unsigned long used_mask[BITS_TO_LONGS(UNCORE_PMC_IDX_MAX)];
	struct event_constraint *c;
	int i, wmin, wmax, ret = 0;
	struct hw_perf_event *hwc;

	bitmap_zero(used_mask, UNCORE_PMC_IDX_MAX);

	for (i = 0, wmin = UNCORE_PMC_IDX_MAX, wmax = 0; i < n; i++) {
		c = uncore_get_event_constraint(box, box->event_list[i]);
		box->event_constraint[i] = c;
		wmin = min(wmin, c->weight);
		wmax = max(wmax, c->weight);
	}

	/* fastpath, try to reuse previous register */
	for (i = 0; i < n; i++) {
		hwc = &box->event_list[i]->hw;
		c = box->event_constraint[i];

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
		if (assign)
			assign[i] = hwc->idx;
	}
	/* slow path */
	if (i != n)
		ret = perf_assign_events(box->event_constraint, n,
					 wmin, wmax, n, assign);

	if (!assign || ret) {
		for (i = 0; i < n; i++)
			uncore_put_event_constraint(box, box->event_list[i]);
	}
	return ret ? -EINVAL : 0;
}

void uncore_pmu_event_start(struct perf_event *event, int flags)
{
	struct intel_uncore_box *box = uncore_event_to_box(event);
	int idx = event->hw.idx;

	if (WARN_ON_ONCE(idx == -1 || idx >= UNCORE_PMC_IDX_MAX))
		return;

	/*
	 * Free running counter is read-only and always active.
	 * Use the current counter value as start point.
	 * There is no overflow interrupt for free running counter.
	 * Use hrtimer to periodically poll the counter to avoid overflow.
	 */
	if (uncore_pmc_freerunning(event->hw.idx)) {
		list_add_tail(&event->active_entry, &box->active_list);
		local64_set(&event->hw.prev_count,
			    uncore_read_counter(box, event));
		if (box->n_active++ == 0)
			uncore_pmu_start_hrtimer(box);
		return;
	}

	if (WARN_ON_ONCE(!(event->hw.state & PERF_HES_STOPPED)))
		return;

	event->hw.state = 0;
	box->events[idx] = event;
	box->n_active++;
	__set_bit(idx, box->active_mask);

	local64_set(&event->hw.prev_count, uncore_read_counter(box, event));
	uncore_enable_event(box, event);

	if (box->n_active == 1)
		uncore_pmu_start_hrtimer(box);
}

void uncore_pmu_event_stop(struct perf_event *event, int flags)
{
	struct intel_uncore_box *box = uncore_event_to_box(event);
	struct hw_perf_event *hwc = &event->hw;

	/* Cannot disable free running counter which is read-only */
	if (uncore_pmc_freerunning(hwc->idx)) {
		list_del(&event->active_entry);
		if (--box->n_active == 0)
			uncore_pmu_cancel_hrtimer(box);
		uncore_perf_event_update(box, event);
		return;
	}

	if (__test_and_clear_bit(hwc->idx, box->active_mask)) {
		uncore_disable_event(box, event);
		box->n_active--;
		box->events[hwc->idx] = NULL;
		WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
		hwc->state |= PERF_HES_STOPPED;

		if (box->n_active == 0)
			uncore_pmu_cancel_hrtimer(box);
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

int uncore_pmu_event_add(struct perf_event *event, int flags)
{
	struct intel_uncore_box *box = uncore_event_to_box(event);
	struct hw_perf_event *hwc = &event->hw;
	int assign[UNCORE_PMC_IDX_MAX];
	int i, n, ret;

	if (!box)
		return -ENODEV;

	/*
	 * The free funning counter is assigned in event_init().
	 * The free running counter event and free running counter
	 * are 1:1 mapped. It doesn't need to be tracked in event_list.
	 */
	if (uncore_pmc_freerunning(hwc->idx)) {
		if (flags & PERF_EF_START)
			uncore_pmu_event_start(event, 0);
		return 0;
	}

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

void uncore_pmu_event_del(struct perf_event *event, int flags)
{
	struct intel_uncore_box *box = uncore_event_to_box(event);
	int i;

	uncore_pmu_event_stop(event, PERF_EF_UPDATE);

	/*
	 * The event for free running counter is not tracked by event_list.
	 * It doesn't need to force event->hw.idx = -1 to reassign the counter.
	 * Because the event and the free running counter are 1:1 mapped.
	 */
	if (uncore_pmc_freerunning(event->hw.idx))
		return;

	for (i = 0; i < box->n_events; i++) {
		if (event == box->event_list[i]) {
			uncore_put_event_constraint(box, event);

			for (++i; i < box->n_events; i++)
				box->event_list[i - 1] = box->event_list[i];

			--box->n_events;
			break;
		}
	}

	event->hw.idx = -1;
	event->hw.last_tag = ~0ULL;
}

void uncore_pmu_event_read(struct perf_event *event)
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
	int ret = -EINVAL, n;

	/* The free running counter is always active. */
	if (uncore_pmc_freerunning(event->hw.idx))
		return 0;

	fake_box = uncore_alloc_box(pmu->type, NUMA_NO_NODE);
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

	ret = uncore_assign_events(fake_box, NULL, n);
out:
	kfree(fake_box);
	return ret;
}

static int uncore_pmu_event_init(struct perf_event *event)
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
	event->pmu_private = box;

	event->event_caps |= PERF_EV_CAP_READ_ACTIVE_PKG;

	event->hw.idx = -1;
	event->hw.last_tag = ~0ULL;
	event->hw.extra_reg.idx = EXTRA_REG_NONE;
	event->hw.branch_reg.idx = EXTRA_REG_NONE;

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

		/* fixed counters have event field hardcoded to zero */
		hwc->config = 0ULL;
	} else if (is_freerunning_event(event)) {
		hwc->config = event->attr.config;
		if (!check_valid_freerunning_event(box, event))
			return -EINVAL;
		event->hw.idx = UNCORE_PMC_IDX_FREERUNNING;
		/*
		 * The free running counter event and free running counter
		 * are always 1:1 mapped.
		 * The free running counter is always active.
		 * Assign the free running counter here.
		 */
		event->hw.event_base = uncore_freerunning_counter(box, event);
	} else {
		hwc->config = event->attr.config &
			      (pmu->type->event_mask | ((u64)pmu->type->event_mask_ext << 32));
		if (pmu->type->ops->hw_config) {
			ret = pmu->type->ops->hw_config(box, event);
			if (ret)
				return ret;
		}
	}

	if (event->group_leader != event)
		ret = uncore_validate_group(pmu, event);
	else
		ret = 0;

	return ret;
}

static void uncore_pmu_enable(struct pmu *pmu)
{
	struct intel_uncore_pmu *uncore_pmu;
	struct intel_uncore_box *box;

	uncore_pmu = container_of(pmu, struct intel_uncore_pmu, pmu);

	box = uncore_pmu_to_box(uncore_pmu, smp_processor_id());
	if (!box)
		return;

	if (uncore_pmu->type->ops->enable_box)
		uncore_pmu->type->ops->enable_box(box);
}

static void uncore_pmu_disable(struct pmu *pmu)
{
	struct intel_uncore_pmu *uncore_pmu;
	struct intel_uncore_box *box;

	uncore_pmu = container_of(pmu, struct intel_uncore_pmu, pmu);

	box = uncore_pmu_to_box(uncore_pmu, smp_processor_id());
	if (!box)
		return;

	if (uncore_pmu->type->ops->disable_box)
		uncore_pmu->type->ops->disable_box(box);
}

static ssize_t uncore_get_attr_cpumask(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return cpumap_print_to_pagebuf(true, buf, &uncore_cpu_mask);
}

static DEVICE_ATTR(cpumask, S_IRUGO, uncore_get_attr_cpumask, NULL);

static struct attribute *uncore_pmu_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static const struct attribute_group uncore_pmu_attr_group = {
	.attrs = uncore_pmu_attrs,
};

void uncore_get_alias_name(char *pmu_name, struct intel_uncore_pmu *pmu)
{
	struct intel_uncore_type *type = pmu->type;

	if (type->num_boxes == 1)
		sprintf(pmu_name, "uncore_type_%u", type->type_id);
	else {
		sprintf(pmu_name, "uncore_type_%u_%d",
			type->type_id, type->box_ids[pmu->pmu_idx]);
	}
}

static void uncore_get_pmu_name(struct intel_uncore_pmu *pmu)
{
	struct intel_uncore_type *type = pmu->type;

	/*
	 * No uncore block name in discovery table.
	 * Use uncore_type_&typeid_&boxid as name.
	 */
	if (!type->name) {
		uncore_get_alias_name(pmu->name, pmu);
		return;
	}

	if (type->num_boxes == 1) {
		if (strlen(type->name) > 0)
			sprintf(pmu->name, "uncore_%s", type->name);
		else
			sprintf(pmu->name, "uncore");
	} else {
		/*
		 * Use the box ID from the discovery table if applicable.
		 */
		sprintf(pmu->name, "uncore_%s_%d", type->name,
			type->box_ids ? type->box_ids[pmu->pmu_idx] : pmu->pmu_idx);
	}
}

static int uncore_pmu_register(struct intel_uncore_pmu *pmu)
{
	int ret;

	if (!pmu->type->pmu) {
		pmu->pmu = (struct pmu) {
			.attr_groups	= pmu->type->attr_groups,
			.task_ctx_nr	= perf_invalid_context,
			.pmu_enable	= uncore_pmu_enable,
			.pmu_disable	= uncore_pmu_disable,
			.event_init	= uncore_pmu_event_init,
			.add		= uncore_pmu_event_add,
			.del		= uncore_pmu_event_del,
			.start		= uncore_pmu_event_start,
			.stop		= uncore_pmu_event_stop,
			.read		= uncore_pmu_event_read,
			.module		= THIS_MODULE,
			.capabilities	= PERF_PMU_CAP_NO_EXCLUDE,
			.attr_update	= pmu->type->attr_update,
		};
	} else {
		pmu->pmu = *pmu->type->pmu;
		pmu->pmu.attr_groups = pmu->type->attr_groups;
		pmu->pmu.attr_update = pmu->type->attr_update;
	}

	uncore_get_pmu_name(pmu);

	ret = perf_pmu_register(&pmu->pmu, pmu->name, -1);
	if (!ret)
		pmu->registered = true;
	return ret;
}

static void uncore_pmu_unregister(struct intel_uncore_pmu *pmu)
{
	if (!pmu->registered)
		return;
	perf_pmu_unregister(&pmu->pmu);
	pmu->registered = false;
}

static void uncore_free_boxes(struct intel_uncore_pmu *pmu)
{
	int die;

	for (die = 0; die < uncore_max_dies(); die++)
		kfree(pmu->boxes[die]);
	kfree(pmu->boxes);
}

static void uncore_type_exit(struct intel_uncore_type *type)
{
	struct intel_uncore_pmu *pmu = type->pmus;
	int i;

	if (type->cleanup_mapping)
		type->cleanup_mapping(type);

	if (pmu) {
		for (i = 0; i < type->num_boxes; i++, pmu++) {
			uncore_pmu_unregister(pmu);
			uncore_free_boxes(pmu);
		}
		kfree(type->pmus);
		type->pmus = NULL;
	}
	if (type->box_ids) {
		kfree(type->box_ids);
		type->box_ids = NULL;
	}
	kfree(type->events_group);
	type->events_group = NULL;
}

static void uncore_types_exit(struct intel_uncore_type **types)
{
	for (; *types; types++)
		uncore_type_exit(*types);
}

static int __init uncore_type_init(struct intel_uncore_type *type, bool setid)
{
	struct intel_uncore_pmu *pmus;
	size_t size;
	int i, j;

	pmus = kcalloc(type->num_boxes, sizeof(*pmus), GFP_KERNEL);
	if (!pmus)
		return -ENOMEM;

	size = uncore_max_dies() * sizeof(struct intel_uncore_box *);

	for (i = 0; i < type->num_boxes; i++) {
		pmus[i].func_id	= setid ? i : -1;
		pmus[i].pmu_idx	= i;
		pmus[i].type	= type;
		pmus[i].boxes	= kzalloc(size, GFP_KERNEL);
		if (!pmus[i].boxes)
			goto err;
	}

	type->pmus = pmus;
	type->unconstrainted = (struct event_constraint)
		__EVENT_CONSTRAINT(0, (1ULL << type->num_counters) - 1,
				0, type->num_counters, 0, 0);

	if (type->event_descs) {
		struct {
			struct attribute_group group;
			struct attribute *attrs[];
		} *attr_group;
		for (i = 0; type->event_descs[i].attr.attr.name; i++);

		attr_group = kzalloc(struct_size(attr_group, attrs, i + 1),
								GFP_KERNEL);
		if (!attr_group)
			goto err;

		attr_group->group.name = "events";
		attr_group->group.attrs = attr_group->attrs;

		for (j = 0; j < i; j++)
			attr_group->attrs[j] = &type->event_descs[j].attr.attr;

		type->events_group = &attr_group->group;
	}

	type->pmu_group = &uncore_pmu_attr_group;

	if (type->set_mapping)
		type->set_mapping(type);

	return 0;

err:
	for (i = 0; i < type->num_boxes; i++)
		kfree(pmus[i].boxes);
	kfree(pmus);

	return -ENOMEM;
}

static int __init
uncore_types_init(struct intel_uncore_type **types, bool setid)
{
	int ret;

	for (; *types; types++) {
		ret = uncore_type_init(*types, setid);
		if (ret)
			return ret;
	}
	return 0;
}

/*
 * Get the die information of a PCI device.
 * @pdev: The PCI device.
 * @die: The die id which the device maps to.
 */
static int uncore_pci_get_dev_die_info(struct pci_dev *pdev, int *die)
{
	*die = uncore_pcibus_to_dieid(pdev->bus);
	if (*die < 0)
		return -EINVAL;

	return 0;
}

static struct intel_uncore_pmu *
uncore_pci_find_dev_pmu_from_types(struct pci_dev *pdev)
{
	struct intel_uncore_type **types = uncore_pci_uncores;
	struct intel_uncore_type *type;
	u64 box_ctl;
	int i, die;

	for (; *types; types++) {
		type = *types;
		for (die = 0; die < __uncore_max_dies; die++) {
			for (i = 0; i < type->num_boxes; i++) {
				if (!type->box_ctls[die])
					continue;
				box_ctl = type->box_ctls[die] + type->pci_offsets[i];
				if (pdev->devfn == UNCORE_DISCOVERY_PCI_DEVFN(box_ctl) &&
				    pdev->bus->number == UNCORE_DISCOVERY_PCI_BUS(box_ctl) &&
				    pci_domain_nr(pdev->bus) == UNCORE_DISCOVERY_PCI_DOMAIN(box_ctl))
					return &type->pmus[i];
			}
		}
	}

	return NULL;
}

/*
 * Find the PMU of a PCI device.
 * @pdev: The PCI device.
 * @ids: The ID table of the available PCI devices with a PMU.
 *       If NULL, search the whole uncore_pci_uncores.
 */
static struct intel_uncore_pmu *
uncore_pci_find_dev_pmu(struct pci_dev *pdev, const struct pci_device_id *ids)
{
	struct intel_uncore_pmu *pmu = NULL;
	struct intel_uncore_type *type;
	kernel_ulong_t data;
	unsigned int devfn;

	if (!ids)
		return uncore_pci_find_dev_pmu_from_types(pdev);

	while (ids && ids->vendor) {
		if ((ids->vendor == pdev->vendor) &&
		    (ids->device == pdev->device)) {
			data = ids->driver_data;
			devfn = PCI_DEVFN(UNCORE_PCI_DEV_DEV(data),
					  UNCORE_PCI_DEV_FUNC(data));
			if (devfn == pdev->devfn) {
				type = uncore_pci_uncores[UNCORE_PCI_DEV_TYPE(data)];
				pmu = &type->pmus[UNCORE_PCI_DEV_IDX(data)];
				break;
			}
		}
		ids++;
	}
	return pmu;
}

/*
 * Register the PMU for a PCI device
 * @pdev: The PCI device.
 * @type: The corresponding PMU type of the device.
 * @pmu: The corresponding PMU of the device.
 * @die: The die id which the device maps to.
 */
static int uncore_pci_pmu_register(struct pci_dev *pdev,
				   struct intel_uncore_type *type,
				   struct intel_uncore_pmu *pmu,
				   int die)
{
	struct intel_uncore_box *box;
	int ret;

	if (WARN_ON_ONCE(pmu->boxes[die] != NULL))
		return -EINVAL;

	box = uncore_alloc_box(type, NUMA_NO_NODE);
	if (!box)
		return -ENOMEM;

	if (pmu->func_id < 0)
		pmu->func_id = pdev->devfn;
	else
		WARN_ON_ONCE(pmu->func_id != pdev->devfn);

	atomic_inc(&box->refcnt);
	box->dieid = die;
	box->pci_dev = pdev;
	box->pmu = pmu;
	uncore_box_init(box);

	pmu->boxes[die] = box;
	if (atomic_inc_return(&pmu->activeboxes) > 1)
		return 0;

	/* First active box registers the pmu */
	ret = uncore_pmu_register(pmu);
	if (ret) {
		pmu->boxes[die] = NULL;
		uncore_box_exit(box);
		kfree(box);
	}
	return ret;
}

/*
 * add a pci uncore device
 */
static int uncore_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct intel_uncore_type *type;
	struct intel_uncore_pmu *pmu = NULL;
	int die, ret;

	ret = uncore_pci_get_dev_die_info(pdev, &die);
	if (ret)
		return ret;

	if (UNCORE_PCI_DEV_TYPE(id->driver_data) == UNCORE_EXTRA_PCI_DEV) {
		int idx = UNCORE_PCI_DEV_IDX(id->driver_data);

		uncore_extra_pci_dev[die].dev[idx] = pdev;
		pci_set_drvdata(pdev, NULL);
		return 0;
	}

	type = uncore_pci_uncores[UNCORE_PCI_DEV_TYPE(id->driver_data)];

	/*
	 * Some platforms, e.g.  Knights Landing, use a common PCI device ID
	 * for multiple instances of an uncore PMU device type. We should check
	 * PCI slot and func to indicate the uncore box.
	 */
	if (id->driver_data & ~0xffff) {
		struct pci_driver *pci_drv = to_pci_driver(pdev->dev.driver);

		pmu = uncore_pci_find_dev_pmu(pdev, pci_drv->id_table);
		if (pmu == NULL)
			return -ENODEV;
	} else {
		/*
		 * for performance monitoring unit with multiple boxes,
		 * each box has a different function id.
		 */
		pmu = &type->pmus[UNCORE_PCI_DEV_IDX(id->driver_data)];
	}

	ret = uncore_pci_pmu_register(pdev, type, pmu, die);

	pci_set_drvdata(pdev, pmu->boxes[die]);

	return ret;
}

/*
 * Unregister the PMU of a PCI device
 * @pmu: The corresponding PMU is unregistered.
 * @die: The die id which the device maps to.
 */
static void uncore_pci_pmu_unregister(struct intel_uncore_pmu *pmu, int die)
{
	struct intel_uncore_box *box = pmu->boxes[die];

	pmu->boxes[die] = NULL;
	if (atomic_dec_return(&pmu->activeboxes) == 0)
		uncore_pmu_unregister(pmu);
	uncore_box_exit(box);
	kfree(box);
}

static void uncore_pci_remove(struct pci_dev *pdev)
{
	struct intel_uncore_box *box;
	struct intel_uncore_pmu *pmu;
	int i, die;

	if (uncore_pci_get_dev_die_info(pdev, &die))
		return;

	box = pci_get_drvdata(pdev);
	if (!box) {
		for (i = 0; i < UNCORE_EXTRA_PCI_DEV_MAX; i++) {
			if (uncore_extra_pci_dev[die].dev[i] == pdev) {
				uncore_extra_pci_dev[die].dev[i] = NULL;
				break;
			}
		}
		WARN_ON_ONCE(i >= UNCORE_EXTRA_PCI_DEV_MAX);
		return;
	}

	pmu = box->pmu;

	pci_set_drvdata(pdev, NULL);

	uncore_pci_pmu_unregister(pmu, die);
}

static int uncore_bus_notify(struct notifier_block *nb,
			     unsigned long action, void *data,
			     const struct pci_device_id *ids)
{
	struct device *dev = data;
	struct pci_dev *pdev = to_pci_dev(dev);
	struct intel_uncore_pmu *pmu;
	int die;

	/* Unregister the PMU when the device is going to be deleted. */
	if (action != BUS_NOTIFY_DEL_DEVICE)
		return NOTIFY_DONE;

	pmu = uncore_pci_find_dev_pmu(pdev, ids);
	if (!pmu)
		return NOTIFY_DONE;

	if (uncore_pci_get_dev_die_info(pdev, &die))
		return NOTIFY_DONE;

	uncore_pci_pmu_unregister(pmu, die);

	return NOTIFY_OK;
}

static int uncore_pci_sub_bus_notify(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	return uncore_bus_notify(nb, action, data,
				 uncore_pci_sub_driver->id_table);
}

static struct notifier_block uncore_pci_sub_notifier = {
	.notifier_call = uncore_pci_sub_bus_notify,
};

static void uncore_pci_sub_driver_init(void)
{
	const struct pci_device_id *ids = uncore_pci_sub_driver->id_table;
	struct intel_uncore_type *type;
	struct intel_uncore_pmu *pmu;
	struct pci_dev *pci_sub_dev;
	bool notify = false;
	unsigned int devfn;
	int die;

	while (ids && ids->vendor) {
		pci_sub_dev = NULL;
		type = uncore_pci_uncores[UNCORE_PCI_DEV_TYPE(ids->driver_data)];
		/*
		 * Search the available device, and register the
		 * corresponding PMU.
		 */
		while ((pci_sub_dev = pci_get_device(PCI_VENDOR_ID_INTEL,
						     ids->device, pci_sub_dev))) {
			devfn = PCI_DEVFN(UNCORE_PCI_DEV_DEV(ids->driver_data),
					  UNCORE_PCI_DEV_FUNC(ids->driver_data));
			if (devfn != pci_sub_dev->devfn)
				continue;

			pmu = &type->pmus[UNCORE_PCI_DEV_IDX(ids->driver_data)];
			if (!pmu)
				continue;

			if (uncore_pci_get_dev_die_info(pci_sub_dev, &die))
				continue;

			if (!uncore_pci_pmu_register(pci_sub_dev, type, pmu,
						     die))
				notify = true;
		}
		ids++;
	}

	if (notify && bus_register_notifier(&pci_bus_type, &uncore_pci_sub_notifier))
		notify = false;

	if (!notify)
		uncore_pci_sub_driver = NULL;
}

static int uncore_pci_bus_notify(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	return uncore_bus_notify(nb, action, data, NULL);
}

static struct notifier_block uncore_pci_notifier = {
	.notifier_call = uncore_pci_bus_notify,
};


static void uncore_pci_pmus_register(void)
{
	struct intel_uncore_type **types = uncore_pci_uncores;
	struct intel_uncore_type *type;
	struct intel_uncore_pmu *pmu;
	struct pci_dev *pdev;
	u64 box_ctl;
	int i, die;

	for (; *types; types++) {
		type = *types;
		for (die = 0; die < __uncore_max_dies; die++) {
			for (i = 0; i < type->num_boxes; i++) {
				if (!type->box_ctls[die])
					continue;
				box_ctl = type->box_ctls[die] + type->pci_offsets[i];
				pdev = pci_get_domain_bus_and_slot(UNCORE_DISCOVERY_PCI_DOMAIN(box_ctl),
								   UNCORE_DISCOVERY_PCI_BUS(box_ctl),
								   UNCORE_DISCOVERY_PCI_DEVFN(box_ctl));
				if (!pdev)
					continue;
				pmu = &type->pmus[i];

				uncore_pci_pmu_register(pdev, type, pmu, die);
			}
		}
	}

	bus_register_notifier(&pci_bus_type, &uncore_pci_notifier);
}

static int __init uncore_pci_init(void)
{
	size_t size;
	int ret;

	size = uncore_max_dies() * sizeof(struct pci_extra_dev);
	uncore_extra_pci_dev = kzalloc(size, GFP_KERNEL);
	if (!uncore_extra_pci_dev) {
		ret = -ENOMEM;
		goto err;
	}

	ret = uncore_types_init(uncore_pci_uncores, false);
	if (ret)
		goto errtype;

	if (uncore_pci_driver) {
		uncore_pci_driver->probe = uncore_pci_probe;
		uncore_pci_driver->remove = uncore_pci_remove;

		ret = pci_register_driver(uncore_pci_driver);
		if (ret)
			goto errtype;
	} else
		uncore_pci_pmus_register();

	if (uncore_pci_sub_driver)
		uncore_pci_sub_driver_init();

	pcidrv_registered = true;
	return 0;

errtype:
	uncore_types_exit(uncore_pci_uncores);
	kfree(uncore_extra_pci_dev);
	uncore_extra_pci_dev = NULL;
	uncore_free_pcibus_map();
err:
	uncore_pci_uncores = empty_uncore;
	return ret;
}

static void uncore_pci_exit(void)
{
	if (pcidrv_registered) {
		pcidrv_registered = false;
		if (uncore_pci_sub_driver)
			bus_unregister_notifier(&pci_bus_type, &uncore_pci_sub_notifier);
		if (uncore_pci_driver)
			pci_unregister_driver(uncore_pci_driver);
		else
			bus_unregister_notifier(&pci_bus_type, &uncore_pci_notifier);
		uncore_types_exit(uncore_pci_uncores);
		kfree(uncore_extra_pci_dev);
		uncore_free_pcibus_map();
	}
}

static void uncore_change_type_ctx(struct intel_uncore_type *type, int old_cpu,
				   int new_cpu)
{
	struct intel_uncore_pmu *pmu = type->pmus;
	struct intel_uncore_box *box;
	int i, die;

	die = topology_logical_die_id(old_cpu < 0 ? new_cpu : old_cpu);
	for (i = 0; i < type->num_boxes; i++, pmu++) {
		box = pmu->boxes[die];
		if (!box)
			continue;

		if (old_cpu < 0) {
			WARN_ON_ONCE(box->cpu != -1);
			box->cpu = new_cpu;
			continue;
		}

		WARN_ON_ONCE(box->cpu != old_cpu);
		box->cpu = -1;
		if (new_cpu < 0)
			continue;

		uncore_pmu_cancel_hrtimer(box);
		perf_pmu_migrate_context(&pmu->pmu, old_cpu, new_cpu);
		box->cpu = new_cpu;
	}
}

static void uncore_change_context(struct intel_uncore_type **uncores,
				  int old_cpu, int new_cpu)
{
	for (; *uncores; uncores++)
		uncore_change_type_ctx(*uncores, old_cpu, new_cpu);
}

static void uncore_box_unref(struct intel_uncore_type **types, int id)
{
	struct intel_uncore_type *type;
	struct intel_uncore_pmu *pmu;
	struct intel_uncore_box *box;
	int i;

	for (; *types; types++) {
		type = *types;
		pmu = type->pmus;
		for (i = 0; i < type->num_boxes; i++, pmu++) {
			box = pmu->boxes[id];
			if (box && atomic_dec_return(&box->refcnt) == 0)
				uncore_box_exit(box);
		}
	}
}

static int uncore_event_cpu_offline(unsigned int cpu)
{
	int die, target;

	/* Check if exiting cpu is used for collecting uncore events */
	if (!cpumask_test_and_clear_cpu(cpu, &uncore_cpu_mask))
		goto unref;
	/* Find a new cpu to collect uncore events */
	target = cpumask_any_but(topology_die_cpumask(cpu), cpu);

	/* Migrate uncore events to the new target */
	if (target < nr_cpu_ids)
		cpumask_set_cpu(target, &uncore_cpu_mask);
	else
		target = -1;

	uncore_change_context(uncore_msr_uncores, cpu, target);
	uncore_change_context(uncore_mmio_uncores, cpu, target);
	uncore_change_context(uncore_pci_uncores, cpu, target);

unref:
	/* Clear the references */
	die = topology_logical_die_id(cpu);
	uncore_box_unref(uncore_msr_uncores, die);
	uncore_box_unref(uncore_mmio_uncores, die);
	return 0;
}

static int allocate_boxes(struct intel_uncore_type **types,
			 unsigned int die, unsigned int cpu)
{
	struct intel_uncore_box *box, *tmp;
	struct intel_uncore_type *type;
	struct intel_uncore_pmu *pmu;
	LIST_HEAD(allocated);
	int i;

	/* Try to allocate all required boxes */
	for (; *types; types++) {
		type = *types;
		pmu = type->pmus;
		for (i = 0; i < type->num_boxes; i++, pmu++) {
			if (pmu->boxes[die])
				continue;
			box = uncore_alloc_box(type, cpu_to_node(cpu));
			if (!box)
				goto cleanup;
			box->pmu = pmu;
			box->dieid = die;
			list_add(&box->active_list, &allocated);
		}
	}
	/* Install them in the pmus */
	list_for_each_entry_safe(box, tmp, &allocated, active_list) {
		list_del_init(&box->active_list);
		box->pmu->boxes[die] = box;
	}
	return 0;

cleanup:
	list_for_each_entry_safe(box, tmp, &allocated, active_list) {
		list_del_init(&box->active_list);
		kfree(box);
	}
	return -ENOMEM;
}

static int uncore_box_ref(struct intel_uncore_type **types,
			  int id, unsigned int cpu)
{
	struct intel_uncore_type *type;
	struct intel_uncore_pmu *pmu;
	struct intel_uncore_box *box;
	int i, ret;

	ret = allocate_boxes(types, id, cpu);
	if (ret)
		return ret;

	for (; *types; types++) {
		type = *types;
		pmu = type->pmus;
		for (i = 0; i < type->num_boxes; i++, pmu++) {
			box = pmu->boxes[id];
			if (box && atomic_inc_return(&box->refcnt) == 1)
				uncore_box_init(box);
		}
	}
	return 0;
}

static int uncore_event_cpu_online(unsigned int cpu)
{
	int die, target, msr_ret, mmio_ret;

	die = topology_logical_die_id(cpu);
	msr_ret = uncore_box_ref(uncore_msr_uncores, die, cpu);
	mmio_ret = uncore_box_ref(uncore_mmio_uncores, die, cpu);
	if (msr_ret && mmio_ret)
		return -ENOMEM;

	/*
	 * Check if there is an online cpu in the package
	 * which collects uncore events already.
	 */
	target = cpumask_any_and(&uncore_cpu_mask, topology_die_cpumask(cpu));
	if (target < nr_cpu_ids)
		return 0;

	cpumask_set_cpu(cpu, &uncore_cpu_mask);

	if (!msr_ret)
		uncore_change_context(uncore_msr_uncores, -1, cpu);
	if (!mmio_ret)
		uncore_change_context(uncore_mmio_uncores, -1, cpu);
	uncore_change_context(uncore_pci_uncores, -1, cpu);
	return 0;
}

static int __init type_pmu_register(struct intel_uncore_type *type)
{
	int i, ret;

	for (i = 0; i < type->num_boxes; i++) {
		ret = uncore_pmu_register(&type->pmus[i]);
		if (ret)
			return ret;
	}
	return 0;
}

static int __init uncore_msr_pmus_register(void)
{
	struct intel_uncore_type **types = uncore_msr_uncores;
	int ret;

	for (; *types; types++) {
		ret = type_pmu_register(*types);
		if (ret)
			return ret;
	}
	return 0;
}

static int __init uncore_cpu_init(void)
{
	int ret;

	ret = uncore_types_init(uncore_msr_uncores, true);
	if (ret)
		goto err;

	ret = uncore_msr_pmus_register();
	if (ret)
		goto err;
	return 0;
err:
	uncore_types_exit(uncore_msr_uncores);
	uncore_msr_uncores = empty_uncore;
	return ret;
}

static int __init uncore_mmio_init(void)
{
	struct intel_uncore_type **types = uncore_mmio_uncores;
	int ret;

	ret = uncore_types_init(types, true);
	if (ret)
		goto err;

	for (; *types; types++) {
		ret = type_pmu_register(*types);
		if (ret)
			goto err;
	}
	return 0;
err:
	uncore_types_exit(uncore_mmio_uncores);
	uncore_mmio_uncores = empty_uncore;
	return ret;
}

struct intel_uncore_init_fun {
	void	(*cpu_init)(void);
	int	(*pci_init)(void);
	void	(*mmio_init)(void);
	bool	use_discovery;
};

static const struct intel_uncore_init_fun nhm_uncore_init __initconst = {
	.cpu_init = nhm_uncore_cpu_init,
};

static const struct intel_uncore_init_fun snb_uncore_init __initconst = {
	.cpu_init = snb_uncore_cpu_init,
	.pci_init = snb_uncore_pci_init,
};

static const struct intel_uncore_init_fun ivb_uncore_init __initconst = {
	.cpu_init = snb_uncore_cpu_init,
	.pci_init = ivb_uncore_pci_init,
};

static const struct intel_uncore_init_fun hsw_uncore_init __initconst = {
	.cpu_init = snb_uncore_cpu_init,
	.pci_init = hsw_uncore_pci_init,
};

static const struct intel_uncore_init_fun bdw_uncore_init __initconst = {
	.cpu_init = snb_uncore_cpu_init,
	.pci_init = bdw_uncore_pci_init,
};

static const struct intel_uncore_init_fun snbep_uncore_init __initconst = {
	.cpu_init = snbep_uncore_cpu_init,
	.pci_init = snbep_uncore_pci_init,
};

static const struct intel_uncore_init_fun nhmex_uncore_init __initconst = {
	.cpu_init = nhmex_uncore_cpu_init,
};

static const struct intel_uncore_init_fun ivbep_uncore_init __initconst = {
	.cpu_init = ivbep_uncore_cpu_init,
	.pci_init = ivbep_uncore_pci_init,
};

static const struct intel_uncore_init_fun hswep_uncore_init __initconst = {
	.cpu_init = hswep_uncore_cpu_init,
	.pci_init = hswep_uncore_pci_init,
};

static const struct intel_uncore_init_fun bdx_uncore_init __initconst = {
	.cpu_init = bdx_uncore_cpu_init,
	.pci_init = bdx_uncore_pci_init,
};

static const struct intel_uncore_init_fun knl_uncore_init __initconst = {
	.cpu_init = knl_uncore_cpu_init,
	.pci_init = knl_uncore_pci_init,
};

static const struct intel_uncore_init_fun skl_uncore_init __initconst = {
	.cpu_init = skl_uncore_cpu_init,
	.pci_init = skl_uncore_pci_init,
};

static const struct intel_uncore_init_fun skx_uncore_init __initconst = {
	.cpu_init = skx_uncore_cpu_init,
	.pci_init = skx_uncore_pci_init,
};

static const struct intel_uncore_init_fun icl_uncore_init __initconst = {
	.cpu_init = icl_uncore_cpu_init,
	.pci_init = skl_uncore_pci_init,
};

static const struct intel_uncore_init_fun tgl_uncore_init __initconst = {
	.cpu_init = tgl_uncore_cpu_init,
	.mmio_init = tgl_uncore_mmio_init,
};

static const struct intel_uncore_init_fun tgl_l_uncore_init __initconst = {
	.cpu_init = tgl_uncore_cpu_init,
	.mmio_init = tgl_l_uncore_mmio_init,
};

static const struct intel_uncore_init_fun rkl_uncore_init __initconst = {
	.cpu_init = tgl_uncore_cpu_init,
	.pci_init = skl_uncore_pci_init,
};

static const struct intel_uncore_init_fun adl_uncore_init __initconst = {
	.cpu_init = adl_uncore_cpu_init,
	.mmio_init = adl_uncore_mmio_init,
};

static const struct intel_uncore_init_fun icx_uncore_init __initconst = {
	.cpu_init = icx_uncore_cpu_init,
	.pci_init = icx_uncore_pci_init,
	.mmio_init = icx_uncore_mmio_init,
};

static const struct intel_uncore_init_fun snr_uncore_init __initconst = {
	.cpu_init = snr_uncore_cpu_init,
	.pci_init = snr_uncore_pci_init,
	.mmio_init = snr_uncore_mmio_init,
};

static const struct intel_uncore_init_fun spr_uncore_init __initconst = {
	.cpu_init = spr_uncore_cpu_init,
	.pci_init = spr_uncore_pci_init,
	.mmio_init = spr_uncore_mmio_init,
	.use_discovery = true,
};

static const struct intel_uncore_init_fun generic_uncore_init __initconst = {
	.cpu_init = intel_uncore_generic_uncore_cpu_init,
	.pci_init = intel_uncore_generic_uncore_pci_init,
	.mmio_init = intel_uncore_generic_uncore_mmio_init,
};

static const struct x86_cpu_id intel_uncore_match[] __initconst = {
	X86_MATCH_INTEL_FAM6_MODEL(NEHALEM_EP,		&nhm_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(NEHALEM,		&nhm_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(WESTMERE,		&nhm_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(WESTMERE_EP,		&nhm_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(SANDYBRIDGE,		&snb_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(IVYBRIDGE,		&ivb_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(HASWELL,		&hsw_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(HASWELL_L,		&hsw_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(HASWELL_G,		&hsw_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(BROADWELL,		&bdw_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(BROADWELL_G,		&bdw_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(SANDYBRIDGE_X,	&snbep_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(NEHALEM_EX,		&nhmex_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(WESTMERE_EX,		&nhmex_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(IVYBRIDGE_X,		&ivbep_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(HASWELL_X,		&hswep_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(BROADWELL_X,		&bdx_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(BROADWELL_D,		&bdx_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(XEON_PHI_KNL,	&knl_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(XEON_PHI_KNM,	&knl_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(SKYLAKE,		&skl_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(SKYLAKE_L,		&skl_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(SKYLAKE_X,		&skx_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(KABYLAKE_L,		&skl_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(KABYLAKE,		&skl_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(COMETLAKE_L,		&skl_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(COMETLAKE,		&skl_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE_L,		&icl_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE_NNPI,	&icl_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE,		&icl_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE_D,		&icx_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE_X,		&icx_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(TIGERLAKE_L,		&tgl_l_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(TIGERLAKE,		&tgl_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(ROCKETLAKE,		&rkl_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(ALDERLAKE,		&adl_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(ALDERLAKE_L,		&adl_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(SAPPHIRERAPIDS_X,	&spr_uncore_init),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_TREMONT_D,	&snr_uncore_init),
	{},
};
MODULE_DEVICE_TABLE(x86cpu, intel_uncore_match);

static int __init intel_uncore_init(void)
{
	const struct x86_cpu_id *id;
	struct intel_uncore_init_fun *uncore_init;
	int pret = 0, cret = 0, mret = 0, ret;

	if (boot_cpu_has(X86_FEATURE_HYPERVISOR))
		return -ENODEV;

	__uncore_max_dies =
		topology_max_packages() * topology_max_die_per_package();

	id = x86_match_cpu(intel_uncore_match);
	if (!id) {
		if (!uncore_no_discover && intel_uncore_has_discovery_tables())
			uncore_init = (struct intel_uncore_init_fun *)&generic_uncore_init;
		else
			return -ENODEV;
	} else {
		uncore_init = (struct intel_uncore_init_fun *)id->driver_data;
		if (uncore_no_discover && uncore_init->use_discovery)
			return -ENODEV;
		if (uncore_init->use_discovery && !intel_uncore_has_discovery_tables())
			return -ENODEV;
	}

	if (uncore_init->pci_init) {
		pret = uncore_init->pci_init();
		if (!pret)
			pret = uncore_pci_init();
	}

	if (uncore_init->cpu_init) {
		uncore_init->cpu_init();
		cret = uncore_cpu_init();
	}

	if (uncore_init->mmio_init) {
		uncore_init->mmio_init();
		mret = uncore_mmio_init();
	}

	if (cret && pret && mret) {
		ret = -ENODEV;
		goto free_discovery;
	}

	/* Install hotplug callbacks to setup the targets for each package */
	ret = cpuhp_setup_state(CPUHP_AP_PERF_X86_UNCORE_ONLINE,
				"perf/x86/intel/uncore:online",
				uncore_event_cpu_online,
				uncore_event_cpu_offline);
	if (ret)
		goto err;
	return 0;

err:
	uncore_types_exit(uncore_msr_uncores);
	uncore_types_exit(uncore_mmio_uncores);
	uncore_pci_exit();
free_discovery:
	intel_uncore_clear_discovery_tables();
	return ret;
}
module_init(intel_uncore_init);

static void __exit intel_uncore_exit(void)
{
	cpuhp_remove_state(CPUHP_AP_PERF_X86_UNCORE_ONLINE);
	uncore_types_exit(uncore_msr_uncores);
	uncore_types_exit(uncore_mmio_uncores);
	uncore_pci_exit();
	intel_uncore_clear_discovery_tables();
}
module_exit(intel_uncore_exit);

/*
 * L220/L310 cache controller support
 *
 * Copyright (C) 2016 ARM Limited
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/errno.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/perf_event.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <asm/hardware/cache-l2x0.h>

#define PMU_NR_COUNTERS 2

static void __iomem *l2x0_base;
static struct pmu *l2x0_pmu;
static cpumask_t pmu_cpu;

static const char *l2x0_name;

static ktime_t l2x0_pmu_poll_period;
static struct hrtimer l2x0_pmu_hrtimer;

/*
 * The L220/PL310 PMU has two equivalent counters, Counter1 and Counter0.
 * Registers controlling these are laid out in pairs, in descending order, i.e.
 * the register for Counter1 comes first, followed by the register for
 * Counter0.
 * We ensure that idx 0 -> Counter0, and idx1 -> Counter1.
 */
static struct perf_event *events[PMU_NR_COUNTERS];

/* Find an unused counter */
static int l2x0_pmu_find_idx(void)
{
	int i;

	for (i = 0; i < PMU_NR_COUNTERS; i++) {
		if (!events[i])
			return i;
	}

	return -1;
}

/* How many counters are allocated? */
static int l2x0_pmu_num_active_counters(void)
{
	int i, cnt = 0;

	for (i = 0; i < PMU_NR_COUNTERS; i++) {
		if (events[i])
			cnt++;
	}

	return cnt;
}

static void l2x0_pmu_counter_config_write(int idx, u32 val)
{
	writel_relaxed(val, l2x0_base + L2X0_EVENT_CNT0_CFG - 4 * idx);
}

static u32 l2x0_pmu_counter_read(int idx)
{
	return readl_relaxed(l2x0_base + L2X0_EVENT_CNT0_VAL - 4 * idx);
}

static void l2x0_pmu_counter_write(int idx, u32 val)
{
	writel_relaxed(val, l2x0_base + L2X0_EVENT_CNT0_VAL - 4 * idx);
}

static void __l2x0_pmu_enable(void)
{
	u32 val = readl_relaxed(l2x0_base + L2X0_EVENT_CNT_CTRL);
	val |= L2X0_EVENT_CNT_CTRL_ENABLE;
	writel_relaxed(val, l2x0_base + L2X0_EVENT_CNT_CTRL);
}

static void __l2x0_pmu_disable(void)
{
	u32 val = readl_relaxed(l2x0_base + L2X0_EVENT_CNT_CTRL);
	val &= ~L2X0_EVENT_CNT_CTRL_ENABLE;
	writel_relaxed(val, l2x0_base + L2X0_EVENT_CNT_CTRL);
}

static void l2x0_pmu_enable(struct pmu *pmu)
{
	if (l2x0_pmu_num_active_counters() == 0)
		return;

	__l2x0_pmu_enable();
}

static void l2x0_pmu_disable(struct pmu *pmu)
{
	if (l2x0_pmu_num_active_counters() == 0)
		return;

	__l2x0_pmu_disable();
}

static void warn_if_saturated(u32 count)
{
	if (count != 0xffffffff)
		return;

	pr_warn_ratelimited("L2X0 counter saturated. Poll period too long\n");
}

static void l2x0_pmu_event_read(struct perf_event *event)
{
	struct hw_perf_event *hw = &event->hw;
	u64 prev_count, new_count, mask;

	do {
		 prev_count = local64_read(&hw->prev_count);
		 new_count = l2x0_pmu_counter_read(hw->idx);
	} while (local64_xchg(&hw->prev_count, new_count) != prev_count);

	mask = GENMASK_ULL(31, 0);
	local64_add((new_count - prev_count) & mask, &event->count);

	warn_if_saturated(new_count);
}

static void l2x0_pmu_event_configure(struct perf_event *event)
{
	struct hw_perf_event *hw = &event->hw;

	/*
	 * The L2X0 counters saturate at 0xffffffff rather than wrapping, so we
	 * will *always* lose some number of events when a counter saturates,
	 * and have no way of detecting how many were lost.
	 *
	 * To minimize the impact of this, we try to maximize the period by
	 * always starting counters at zero. To ensure that group ratios are
	 * representative, we poll periodically to avoid counters saturating.
	 * See l2x0_pmu_poll().
	 */
	local64_set(&hw->prev_count, 0);
	l2x0_pmu_counter_write(hw->idx, 0);
}

static enum hrtimer_restart l2x0_pmu_poll(struct hrtimer *hrtimer)
{
	unsigned long flags;
	int i;

	local_irq_save(flags);
	__l2x0_pmu_disable();

	for (i = 0; i < PMU_NR_COUNTERS; i++) {
		struct perf_event *event = events[i];

		if (!event)
			continue;

		l2x0_pmu_event_read(event);
		l2x0_pmu_event_configure(event);
	}

	__l2x0_pmu_enable();
	local_irq_restore(flags);

	hrtimer_forward_now(hrtimer, l2x0_pmu_poll_period);
	return HRTIMER_RESTART;
}


static void __l2x0_pmu_event_enable(int idx, u32 event)
{
	u32 val;

	val = event << L2X0_EVENT_CNT_CFG_SRC_SHIFT;
	val |= L2X0_EVENT_CNT_CFG_INT_DISABLED;
	l2x0_pmu_counter_config_write(idx, val);
}

static void l2x0_pmu_event_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hw = &event->hw;

	if (WARN_ON_ONCE(!(event->hw.state & PERF_HES_STOPPED)))
		return;

	if (flags & PERF_EF_RELOAD) {
		WARN_ON_ONCE(!(hw->state & PERF_HES_UPTODATE));
		l2x0_pmu_event_configure(event);
	}

	hw->state = 0;

	__l2x0_pmu_event_enable(hw->idx, hw->config_base);
}

static void __l2x0_pmu_event_disable(int idx)
{
	u32 val;

	val = L2X0_EVENT_CNT_CFG_SRC_DISABLED << L2X0_EVENT_CNT_CFG_SRC_SHIFT;
	val |= L2X0_EVENT_CNT_CFG_INT_DISABLED;
	l2x0_pmu_counter_config_write(idx, val);
}

static void l2x0_pmu_event_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hw = &event->hw;

	if (WARN_ON_ONCE(event->hw.state & PERF_HES_STOPPED))
		return;

	__l2x0_pmu_event_disable(hw->idx);

	hw->state |= PERF_HES_STOPPED;

	if (flags & PERF_EF_UPDATE) {
		l2x0_pmu_event_read(event);
		hw->state |= PERF_HES_UPTODATE;
	}
}

static int l2x0_pmu_event_add(struct perf_event *event, int flags)
{
	struct hw_perf_event *hw = &event->hw;
	int idx = l2x0_pmu_find_idx();

	if (idx == -1)
		return -EAGAIN;

	/*
	 * Pin the timer, so that the overflows are handled by the chosen
	 * event->cpu (this is the same one as presented in "cpumask"
	 * attribute).
	 */
	if (l2x0_pmu_num_active_counters() == 0)
		hrtimer_start(&l2x0_pmu_hrtimer, l2x0_pmu_poll_period,
			      HRTIMER_MODE_REL_PINNED);

	events[idx] = event;
	hw->idx = idx;

	l2x0_pmu_event_configure(event);

	hw->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;

	if (flags & PERF_EF_START)
		l2x0_pmu_event_start(event, 0);

	return 0;
}

static void l2x0_pmu_event_del(struct perf_event *event, int flags)
{
	struct hw_perf_event *hw = &event->hw;

	l2x0_pmu_event_stop(event, PERF_EF_UPDATE);

	events[hw->idx] = NULL;
	hw->idx = -1;

	if (l2x0_pmu_num_active_counters() == 0)
		hrtimer_cancel(&l2x0_pmu_hrtimer);
}

static bool l2x0_pmu_group_is_valid(struct perf_event *event)
{
	struct pmu *pmu = event->pmu;
	struct perf_event *leader = event->group_leader;
	struct perf_event *sibling;
	int num_hw = 0;

	if (leader->pmu == pmu)
		num_hw++;
	else if (!is_software_event(leader))
		return false;

	list_for_each_entry(sibling, &leader->sibling_list, group_entry) {
		if (sibling->pmu == pmu)
			num_hw++;
		else if (!is_software_event(sibling))
			return false;
	}

	return num_hw <= PMU_NR_COUNTERS;
}

static int l2x0_pmu_event_init(struct perf_event *event)
{
	struct hw_perf_event *hw = &event->hw;

	if (event->attr.type != l2x0_pmu->type)
		return -ENOENT;

	if (is_sampling_event(event) ||
	    event->attach_state & PERF_ATTACH_TASK)
		return -EINVAL;

	if (event->attr.exclude_user   ||
	    event->attr.exclude_kernel ||
	    event->attr.exclude_hv     ||
	    event->attr.exclude_idle   ||
	    event->attr.exclude_host   ||
	    event->attr.exclude_guest)
		return -EINVAL;

	if (event->cpu < 0)
		return -EINVAL;

	if (event->attr.config & ~L2X0_EVENT_CNT_CFG_SRC_MASK)
		return -EINVAL;

	hw->config_base = event->attr.config;

	if (!l2x0_pmu_group_is_valid(event))
		return -EINVAL;

	event->cpu = cpumask_first(&pmu_cpu);

	return 0;
}

struct l2x0_event_attribute {
	struct device_attribute attr;
	unsigned int config;
	bool pl310_only;
};

#define L2X0_EVENT_ATTR(_name, _config, _pl310_only)				\
	(&((struct l2x0_event_attribute[]) {{					\
		.attr = __ATTR(_name, S_IRUGO, l2x0_pmu_event_show, NULL),	\
		.config = _config,						\
		.pl310_only = _pl310_only,					\
	}})[0].attr.attr)

#define L220_PLUS_EVENT_ATTR(_name, _config)					\
	L2X0_EVENT_ATTR(_name, _config, false)

#define PL310_EVENT_ATTR(_name, _config)					\
	L2X0_EVENT_ATTR(_name, _config, true)

static ssize_t l2x0_pmu_event_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct l2x0_event_attribute *lattr;

	lattr = container_of(attr, typeof(*lattr), attr);
	return snprintf(buf, PAGE_SIZE, "config=0x%x\n", lattr->config);
}

static umode_t l2x0_pmu_event_attr_is_visible(struct kobject *kobj,
					      struct attribute *attr,
					      int unused)
{
	struct device *dev = kobj_to_dev(kobj);
	struct pmu *pmu = dev_get_drvdata(dev);
	struct l2x0_event_attribute *lattr;

	lattr = container_of(attr, typeof(*lattr), attr.attr);

	if (!lattr->pl310_only || strcmp("l2c_310", pmu->name) == 0)
		return attr->mode;

	return 0;
}

static struct attribute *l2x0_pmu_event_attrs[] = {
	L220_PLUS_EVENT_ATTR(co,	0x1),
	L220_PLUS_EVENT_ATTR(drhit,	0x2),
	L220_PLUS_EVENT_ATTR(drreq,	0x3),
	L220_PLUS_EVENT_ATTR(dwhit,	0x4),
	L220_PLUS_EVENT_ATTR(dwreq,	0x5),
	L220_PLUS_EVENT_ATTR(dwtreq,	0x6),
	L220_PLUS_EVENT_ATTR(irhit,	0x7),
	L220_PLUS_EVENT_ATTR(irreq,	0x8),
	L220_PLUS_EVENT_ATTR(wa,	0x9),
	PL310_EVENT_ATTR(ipfalloc,	0xa),
	PL310_EVENT_ATTR(epfhit,	0xb),
	PL310_EVENT_ATTR(epfalloc,	0xc),
	PL310_EVENT_ATTR(srrcvd,	0xd),
	PL310_EVENT_ATTR(srconf,	0xe),
	PL310_EVENT_ATTR(epfrcvd,	0xf),
	NULL
};

static struct attribute_group l2x0_pmu_event_attrs_group = {
	.name = "events",
	.attrs = l2x0_pmu_event_attrs,
	.is_visible = l2x0_pmu_event_attr_is_visible,
};

static ssize_t l2x0_pmu_cpumask_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return cpumap_print_to_pagebuf(true, buf, &pmu_cpu);
}

static struct device_attribute l2x0_pmu_cpumask_attr =
		__ATTR(cpumask, S_IRUGO, l2x0_pmu_cpumask_show, NULL);

static struct attribute *l2x0_pmu_cpumask_attrs[] = {
	&l2x0_pmu_cpumask_attr.attr,
	NULL,
};

static struct attribute_group l2x0_pmu_cpumask_attr_group = {
	.attrs = l2x0_pmu_cpumask_attrs,
};

static const struct attribute_group *l2x0_pmu_attr_groups[] = {
	&l2x0_pmu_event_attrs_group,
	&l2x0_pmu_cpumask_attr_group,
	NULL,
};

static void l2x0_pmu_reset(void)
{
	int i;

	__l2x0_pmu_disable();

	for (i = 0; i < PMU_NR_COUNTERS; i++)
		__l2x0_pmu_event_disable(i);
}

static int l2x0_pmu_offline_cpu(unsigned int cpu)
{
	unsigned int target;

	if (!cpumask_test_and_clear_cpu(cpu, &pmu_cpu))
		return 0;

	target = cpumask_any_but(cpu_online_mask, cpu);
	if (target >= nr_cpu_ids)
		return 0;

	perf_pmu_migrate_context(l2x0_pmu, cpu, target);
	cpumask_set_cpu(target, &pmu_cpu);

	return 0;
}

void l2x0_pmu_suspend(void)
{
	int i;

	if (!l2x0_pmu)
		return;

	l2x0_pmu_disable(l2x0_pmu);

	for (i = 0; i < PMU_NR_COUNTERS; i++) {
		if (events[i])
			l2x0_pmu_event_stop(events[i], PERF_EF_UPDATE);
	}

}

void l2x0_pmu_resume(void)
{
	int i;

	if (!l2x0_pmu)
		return;

	l2x0_pmu_reset();

	for (i = 0; i < PMU_NR_COUNTERS; i++) {
		if (events[i])
			l2x0_pmu_event_start(events[i], PERF_EF_RELOAD);
	}

	l2x0_pmu_enable(l2x0_pmu);
}

void __init l2x0_pmu_register(void __iomem *base, u32 part)
{
	/*
	 * Determine whether we support the PMU, and choose the name for sysfs.
	 * This is also used by l2x0_pmu_event_attr_is_visible to determine
	 * which events to display, as the PL310 PMU supports a superset of
	 * L220 events.
	 *
	 * The L210 PMU has a different programmer's interface, and is not
	 * supported by this driver.
	 *
	 * We must defer registering the PMU until the perf subsystem is up and
	 * running, so just stash the name and base, and leave that to another
	 * initcall.
	 */
	switch (part & L2X0_CACHE_ID_PART_MASK) {
	case L2X0_CACHE_ID_PART_L220:
		l2x0_name = "l2c_220";
		break;
	case L2X0_CACHE_ID_PART_L310:
		l2x0_name = "l2c_310";
		break;
	default:
		return;
	}

	l2x0_base = base;
}

static __init int l2x0_pmu_init(void)
{
	int ret;

	if (!l2x0_base)
		return 0;

	l2x0_pmu = kzalloc(sizeof(*l2x0_pmu), GFP_KERNEL);
	if (!l2x0_pmu) {
		pr_warn("Unable to allocate L2x0 PMU\n");
		return -ENOMEM;
	}

	*l2x0_pmu = (struct pmu) {
		.task_ctx_nr = perf_invalid_context,
		.pmu_enable = l2x0_pmu_enable,
		.pmu_disable = l2x0_pmu_disable,
		.read = l2x0_pmu_event_read,
		.start = l2x0_pmu_event_start,
		.stop = l2x0_pmu_event_stop,
		.add = l2x0_pmu_event_add,
		.del = l2x0_pmu_event_del,
		.event_init = l2x0_pmu_event_init,
		.attr_groups = l2x0_pmu_attr_groups,
	};

	l2x0_pmu_reset();

	/*
	 * We always use a hrtimer rather than an interrupt.
	 * See comments in l2x0_pmu_event_configure and l2x0_pmu_poll.
	 *
	 * Polling once a second allows the counters to fill up to 1/128th on a
	 * quad-core test chip with cores clocked at 400MHz. Hopefully this
	 * leaves sufficient headroom to avoid overflow on production silicon
	 * at higher frequencies.
	 */
	l2x0_pmu_poll_period = ms_to_ktime(1000);
	hrtimer_init(&l2x0_pmu_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	l2x0_pmu_hrtimer.function = l2x0_pmu_poll;

	cpumask_set_cpu(0, &pmu_cpu);
	ret = cpuhp_setup_state_nocalls(CPUHP_AP_PERF_ARM_L2X0_ONLINE,
					"perf/arm/l2x0:online", NULL,
					l2x0_pmu_offline_cpu);
	if (ret)
		goto out_pmu;

	ret = perf_pmu_register(l2x0_pmu, l2x0_name, -1);
	if (ret)
		goto out_cpuhp;

	return 0;

out_cpuhp:
	cpuhp_remove_state_nocalls(CPUHP_AP_PERF_ARM_L2X0_ONLINE);
out_pmu:
	kfree(l2x0_pmu);
	l2x0_pmu = NULL;
	return ret;
}
device_initcall(l2x0_pmu_init);

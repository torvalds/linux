/*
 * Intel Cache Quality-of-Service Monitoring (CQM) support.
 *
 * Based very, very heavily on work by Peter Zijlstra.
 */

#include <linux/perf_event.h>
#include <linux/slab.h>
#include <asm/cpu_device_id.h>
#include "perf_event.h"

#define MSR_IA32_PQR_ASSOC	0x0c8f
#define MSR_IA32_QM_CTR		0x0c8e
#define MSR_IA32_QM_EVTSEL	0x0c8d

static unsigned int cqm_max_rmid = -1;
static unsigned int cqm_l3_scale; /* supposedly cacheline size */

struct intel_cqm_state {
	raw_spinlock_t		lock;
	int			rmid;
	int			cnt;
};

static DEFINE_PER_CPU(struct intel_cqm_state, cqm_state);

/*
 * Protects cache_cgroups and cqm_rmid_lru.
 */
static DEFINE_MUTEX(cache_mutex);

/*
 * Groups of events that have the same target(s), one RMID per group.
 */
static LIST_HEAD(cache_groups);

/*
 * Mask of CPUs for reading CQM values. We only need one per-socket.
 */
static cpumask_t cqm_cpumask;

#define RMID_VAL_ERROR		(1ULL << 63)
#define RMID_VAL_UNAVAIL	(1ULL << 62)

#define QOS_L3_OCCUP_EVENT_ID	(1 << 0)

#define QOS_EVENT_MASK	QOS_L3_OCCUP_EVENT_ID

static u64 __rmid_read(unsigned long rmid)
{
	u64 val;

	/*
	 * Ignore the SDM, this thing is _NOTHING_ like a regular perfcnt,
	 * it just says that to increase confusion.
	 */
	wrmsr(MSR_IA32_QM_EVTSEL, QOS_L3_OCCUP_EVENT_ID, rmid);
	rdmsrl(MSR_IA32_QM_CTR, val);

	/*
	 * Aside from the ERROR and UNAVAIL bits, assume this thing returns
	 * the number of cachelines tagged with @rmid.
	 */
	return val;
}

struct cqm_rmid_entry {
	u64 rmid;
	struct list_head list;
};

/*
 * A least recently used list of RMIDs.
 *
 * Oldest entry at the head, newest (most recently used) entry at the
 * tail. This list is never traversed, it's only used to keep track of
 * the lru order. That is, we only pick entries of the head or insert
 * them on the tail.
 *
 * All entries on the list are 'free', and their RMIDs are not currently
 * in use. To mark an RMID as in use, remove its entry from the lru
 * list.
 *
 * This list is protected by cache_mutex.
 */
static LIST_HEAD(cqm_rmid_lru);

/*
 * We use a simple array of pointers so that we can lookup a struct
 * cqm_rmid_entry in O(1). This alleviates the callers of __get_rmid()
 * and __put_rmid() from having to worry about dealing with struct
 * cqm_rmid_entry - they just deal with rmids, i.e. integers.
 *
 * Once this array is initialized it is read-only. No locks are required
 * to access it.
 *
 * All entries for all RMIDs can be looked up in the this array at all
 * times.
 */
static struct cqm_rmid_entry **cqm_rmid_ptrs;

static inline struct cqm_rmid_entry *__rmid_entry(int rmid)
{
	struct cqm_rmid_entry *entry;

	entry = cqm_rmid_ptrs[rmid];
	WARN_ON(entry->rmid != rmid);

	return entry;
}

/*
 * Returns < 0 on fail.
 *
 * We expect to be called with cache_mutex held.
 */
static int __get_rmid(void)
{
	struct cqm_rmid_entry *entry;

	lockdep_assert_held(&cache_mutex);

	if (list_empty(&cqm_rmid_lru))
		return -EAGAIN;

	entry = list_first_entry(&cqm_rmid_lru, struct cqm_rmid_entry, list);
	list_del(&entry->list);

	return entry->rmid;
}

static void __put_rmid(int rmid)
{
	struct cqm_rmid_entry *entry;

	lockdep_assert_held(&cache_mutex);

	entry = __rmid_entry(rmid);

	list_add_tail(&entry->list, &cqm_rmid_lru);
}

static int intel_cqm_setup_rmid_cache(void)
{
	struct cqm_rmid_entry *entry;
	int r;

	cqm_rmid_ptrs = kmalloc(sizeof(struct cqm_rmid_entry *) *
				(cqm_max_rmid + 1), GFP_KERNEL);
	if (!cqm_rmid_ptrs)
		return -ENOMEM;

	for (r = 0; r <= cqm_max_rmid; r++) {
		struct cqm_rmid_entry *entry;

		entry = kmalloc(sizeof(*entry), GFP_KERNEL);
		if (!entry)
			goto fail;

		INIT_LIST_HEAD(&entry->list);
		entry->rmid = r;
		cqm_rmid_ptrs[r] = entry;

		list_add_tail(&entry->list, &cqm_rmid_lru);
	}

	/*
	 * RMID 0 is special and is always allocated. It's used for all
	 * tasks that are not monitored.
	 */
	entry = __rmid_entry(0);
	list_del(&entry->list);

	return 0;
fail:
	while (r--)
		kfree(cqm_rmid_ptrs[r]);

	kfree(cqm_rmid_ptrs);
	return -ENOMEM;
}

/*
 * Determine if @a and @b measure the same set of tasks.
 */
static bool __match_event(struct perf_event *a, struct perf_event *b)
{
	if ((a->attach_state & PERF_ATTACH_TASK) !=
	    (b->attach_state & PERF_ATTACH_TASK))
		return false;

	/* not task */

	return true; /* if not task, we're machine wide */
}

/*
 * Determine if @a's tasks intersect with @b's tasks
 */
static bool __conflict_event(struct perf_event *a, struct perf_event *b)
{
	/*
	 * If one of them is not a task, same story as above with cgroups.
	 */
	if (!(a->attach_state & PERF_ATTACH_TASK) ||
	    !(b->attach_state & PERF_ATTACH_TASK))
		return true;

	/*
	 * Must be non-overlapping.
	 */
	return false;
}

/*
 * Find a group and setup RMID.
 *
 * If we're part of a group, we use the group's RMID.
 */
static int intel_cqm_setup_event(struct perf_event *event,
				 struct perf_event **group)
{
	struct perf_event *iter;
	int rmid;

	list_for_each_entry(iter, &cache_groups, hw.cqm_groups_entry) {
		if (__match_event(iter, event)) {
			/* All tasks in a group share an RMID */
			event->hw.cqm_rmid = iter->hw.cqm_rmid;
			*group = iter;
			return 0;
		}

		if (__conflict_event(iter, event))
			return -EBUSY;
	}

	rmid = __get_rmid();
	if (rmid < 0)
		return rmid;

	event->hw.cqm_rmid = rmid;
	return 0;
}

static void intel_cqm_event_read(struct perf_event *event)
{
	unsigned long rmid = event->hw.cqm_rmid;
	u64 val;

	val = __rmid_read(rmid);

	/*
	 * Ignore this reading on error states and do not update the value.
	 */
	if (val & (RMID_VAL_ERROR | RMID_VAL_UNAVAIL))
		return;

	local64_set(&event->count, val);
}

static void intel_cqm_event_start(struct perf_event *event, int mode)
{
	struct intel_cqm_state *state = this_cpu_ptr(&cqm_state);
	unsigned long rmid = event->hw.cqm_rmid;
	unsigned long flags;

	if (!(event->hw.cqm_state & PERF_HES_STOPPED))
		return;

	event->hw.cqm_state &= ~PERF_HES_STOPPED;

	raw_spin_lock_irqsave(&state->lock, flags);

	if (state->cnt++)
		WARN_ON_ONCE(state->rmid != rmid);
	else
		WARN_ON_ONCE(state->rmid);

	state->rmid = rmid;
	wrmsrl(MSR_IA32_PQR_ASSOC, state->rmid);

	raw_spin_unlock_irqrestore(&state->lock, flags);
}

static void intel_cqm_event_stop(struct perf_event *event, int mode)
{
	struct intel_cqm_state *state = this_cpu_ptr(&cqm_state);
	unsigned long flags;

	if (event->hw.cqm_state & PERF_HES_STOPPED)
		return;

	event->hw.cqm_state |= PERF_HES_STOPPED;

	raw_spin_lock_irqsave(&state->lock, flags);
	intel_cqm_event_read(event);

	if (!--state->cnt) {
		state->rmid = 0;
		wrmsrl(MSR_IA32_PQR_ASSOC, 0);
	} else {
		WARN_ON_ONCE(!state->rmid);
	}

	raw_spin_unlock_irqrestore(&state->lock, flags);
}

static int intel_cqm_event_add(struct perf_event *event, int mode)
{
	int rmid;

	event->hw.cqm_state = PERF_HES_STOPPED;
	rmid = event->hw.cqm_rmid;
	WARN_ON_ONCE(!rmid);

	if (mode & PERF_EF_START)
		intel_cqm_event_start(event, mode);

	return 0;
}

static void intel_cqm_event_del(struct perf_event *event, int mode)
{
	intel_cqm_event_stop(event, mode);
}

static void intel_cqm_event_destroy(struct perf_event *event)
{
	struct perf_event *group_other = NULL;

	mutex_lock(&cache_mutex);

	/*
	 * If there's another event in this group...
	 */
	if (!list_empty(&event->hw.cqm_group_entry)) {
		group_other = list_first_entry(&event->hw.cqm_group_entry,
					       struct perf_event,
					       hw.cqm_group_entry);
		list_del(&event->hw.cqm_group_entry);
	}

	/*
	 * And we're the group leader..
	 */
	if (!list_empty(&event->hw.cqm_groups_entry)) {
		/*
		 * If there was a group_other, make that leader, otherwise
		 * destroy the group and return the RMID.
		 */
		if (group_other) {
			list_replace(&event->hw.cqm_groups_entry,
				     &group_other->hw.cqm_groups_entry);
		} else {
			int rmid = event->hw.cqm_rmid;

			__put_rmid(rmid);
			list_del(&event->hw.cqm_groups_entry);
		}
	}

	mutex_unlock(&cache_mutex);
}

static struct pmu intel_cqm_pmu;

/*
 * XXX there's a bit of a problem in that we cannot simply do the one
 * event per node as one would want, since that one event would one get
 * scheduled on the one cpu. But we want to 'schedule' the RMID on all
 * CPUs.
 *
 * This means we want events for each CPU, however, that generates a lot
 * of duplicate values out to userspace -- this is not to be helped
 * unless we want to change the core code in some way. Fore more info,
 * see intel_cqm_event_read().
 */
static int intel_cqm_event_init(struct perf_event *event)
{
	struct perf_event *group = NULL;
	int err;

	if (event->attr.type != intel_cqm_pmu.type)
		return -ENOENT;

	if (event->attr.config & ~QOS_EVENT_MASK)
		return -EINVAL;

	if (event->cpu == -1)
		return -EINVAL;

	/* unsupported modes and filters */
	if (event->attr.exclude_user   ||
	    event->attr.exclude_kernel ||
	    event->attr.exclude_hv     ||
	    event->attr.exclude_idle   ||
	    event->attr.exclude_host   ||
	    event->attr.exclude_guest  ||
	    event->attr.sample_period) /* no sampling */
		return -EINVAL;

	INIT_LIST_HEAD(&event->hw.cqm_group_entry);
	INIT_LIST_HEAD(&event->hw.cqm_groups_entry);

	event->destroy = intel_cqm_event_destroy;

	mutex_lock(&cache_mutex);

	err = intel_cqm_setup_event(event, &group); /* will also set rmid */
	if (err)
		goto out;

	if (group) {
		list_add_tail(&event->hw.cqm_group_entry,
			      &group->hw.cqm_group_entry);
	} else {
		list_add_tail(&event->hw.cqm_groups_entry,
			      &cache_groups);
	}

out:
	mutex_unlock(&cache_mutex);
	return err;
}

EVENT_ATTR_STR(llc_occupancy, intel_cqm_llc, "event=0x01");
EVENT_ATTR_STR(llc_occupancy.per-pkg, intel_cqm_llc_pkg, "1");
EVENT_ATTR_STR(llc_occupancy.unit, intel_cqm_llc_unit, "Bytes");
EVENT_ATTR_STR(llc_occupancy.scale, intel_cqm_llc_scale, NULL);
EVENT_ATTR_STR(llc_occupancy.snapshot, intel_cqm_llc_snapshot, "1");

static struct attribute *intel_cqm_events_attr[] = {
	EVENT_PTR(intel_cqm_llc),
	EVENT_PTR(intel_cqm_llc_pkg),
	EVENT_PTR(intel_cqm_llc_unit),
	EVENT_PTR(intel_cqm_llc_scale),
	EVENT_PTR(intel_cqm_llc_snapshot),
	NULL,
};

static struct attribute_group intel_cqm_events_group = {
	.name = "events",
	.attrs = intel_cqm_events_attr,
};

PMU_FORMAT_ATTR(event, "config:0-7");
static struct attribute *intel_cqm_formats_attr[] = {
	&format_attr_event.attr,
	NULL,
};

static struct attribute_group intel_cqm_format_group = {
	.name = "format",
	.attrs = intel_cqm_formats_attr,
};

static const struct attribute_group *intel_cqm_attr_groups[] = {
	&intel_cqm_events_group,
	&intel_cqm_format_group,
	NULL,
};

static struct pmu intel_cqm_pmu = {
	.attr_groups	= intel_cqm_attr_groups,
	.task_ctx_nr	= perf_sw_context,
	.event_init	= intel_cqm_event_init,
	.add		= intel_cqm_event_add,
	.del		= intel_cqm_event_del,
	.start		= intel_cqm_event_start,
	.stop		= intel_cqm_event_stop,
	.read		= intel_cqm_event_read,
};

static inline void cqm_pick_event_reader(int cpu)
{
	int phys_id = topology_physical_package_id(cpu);
	int i;

	for_each_cpu(i, &cqm_cpumask) {
		if (phys_id == topology_physical_package_id(i))
			return;	/* already got reader for this socket */
	}

	cpumask_set_cpu(cpu, &cqm_cpumask);
}

static void intel_cqm_cpu_prepare(unsigned int cpu)
{
	struct intel_cqm_state *state = &per_cpu(cqm_state, cpu);
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	raw_spin_lock_init(&state->lock);
	state->rmid = 0;
	state->cnt  = 0;

	WARN_ON(c->x86_cache_max_rmid != cqm_max_rmid);
	WARN_ON(c->x86_cache_occ_scale != cqm_l3_scale);
}

static void intel_cqm_cpu_exit(unsigned int cpu)
{
	int phys_id = topology_physical_package_id(cpu);
	int i;

	/*
	 * Is @cpu a designated cqm reader?
	 */
	if (!cpumask_test_and_clear_cpu(cpu, &cqm_cpumask))
		return;

	for_each_online_cpu(i) {
		if (i == cpu)
			continue;

		if (phys_id == topology_physical_package_id(i)) {
			cpumask_set_cpu(i, &cqm_cpumask);
			break;
		}
	}
}

static int intel_cqm_cpu_notifier(struct notifier_block *nb,
				  unsigned long action, void *hcpu)
{
	unsigned int cpu  = (unsigned long)hcpu;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_UP_PREPARE:
		intel_cqm_cpu_prepare(cpu);
		break;
	case CPU_DOWN_PREPARE:
		intel_cqm_cpu_exit(cpu);
		break;
	case CPU_STARTING:
		cqm_pick_event_reader(cpu);
		break;
	}

	return NOTIFY_OK;
}

static const struct x86_cpu_id intel_cqm_match[] = {
	{ .vendor = X86_VENDOR_INTEL, .feature = X86_FEATURE_CQM_OCCUP_LLC },
	{}
};

static int __init intel_cqm_init(void)
{
	char *str, scale[20];
	int i, cpu, ret;

	if (!x86_match_cpu(intel_cqm_match))
		return -ENODEV;

	cqm_l3_scale = boot_cpu_data.x86_cache_occ_scale;

	/*
	 * It's possible that not all resources support the same number
	 * of RMIDs. Instead of making scheduling much more complicated
	 * (where we have to match a task's RMID to a cpu that supports
	 * that many RMIDs) just find the minimum RMIDs supported across
	 * all cpus.
	 *
	 * Also, check that the scales match on all cpus.
	 */
	cpu_notifier_register_begin();

	for_each_online_cpu(cpu) {
		struct cpuinfo_x86 *c = &cpu_data(cpu);

		if (c->x86_cache_max_rmid < cqm_max_rmid)
			cqm_max_rmid = c->x86_cache_max_rmid;

		if (c->x86_cache_occ_scale != cqm_l3_scale) {
			pr_err("Multiple LLC scale values, disabling\n");
			ret = -EINVAL;
			goto out;
		}
	}

	snprintf(scale, sizeof(scale), "%u", cqm_l3_scale);
	str = kstrdup(scale, GFP_KERNEL);
	if (!str) {
		ret = -ENOMEM;
		goto out;
	}

	event_attr_intel_cqm_llc_scale.event_str = str;

	ret = intel_cqm_setup_rmid_cache();
	if (ret)
		goto out;

	for_each_online_cpu(i) {
		intel_cqm_cpu_prepare(i);
		cqm_pick_event_reader(i);
	}

	__perf_cpu_notifier(intel_cqm_cpu_notifier);

	ret = perf_pmu_register(&intel_cqm_pmu, "intel_cqm", -1);

	if (ret)
		pr_err("Intel CQM perf registration failed: %d\n", ret);
	else
		pr_info("Intel CQM monitoring enabled\n");

out:
	cpu_notifier_register_done();

	return ret;
}
device_initcall(intel_cqm_init);

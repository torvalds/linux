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

static u32 cqm_max_rmid = -1;
static unsigned int cqm_l3_scale; /* supposedly cacheline size */

/**
 * struct intel_pqr_state - State cache for the PQR MSR
 * @rmid:		The cached Resource Monitoring ID
 * @closid:		The cached Class Of Service ID
 * @rmid_usecnt:	The usage counter for rmid
 *
 * The upper 32 bits of MSR_IA32_PQR_ASSOC contain closid and the
 * lower 10 bits rmid. The update to MSR_IA32_PQR_ASSOC always
 * contains both parts, so we need to cache them.
 *
 * The cache also helps to avoid pointless updates if the value does
 * not change.
 */
struct intel_pqr_state {
	u32			rmid;
	u32			closid;
	int			rmid_usecnt;
};

/*
 * The cached intel_pqr_state is strictly per CPU and can never be
 * updated from a remote CPU. Both functions which modify the state
 * (intel_cqm_event_start and intel_cqm_event_stop) are called with
 * interrupts disabled, which is sufficient for the protection.
 */
static DEFINE_PER_CPU(struct intel_pqr_state, pqr_state);

/*
 * Protects cache_cgroups and cqm_rmid_free_lru and cqm_rmid_limbo_lru.
 * Also protects event->hw.cqm_rmid
 *
 * Hold either for stability, both for modification of ->hw.cqm_rmid.
 */
static DEFINE_MUTEX(cache_mutex);
static DEFINE_RAW_SPINLOCK(cache_lock);

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

/*
 * This is central to the rotation algorithm in __intel_cqm_rmid_rotate().
 *
 * This rmid is always free and is guaranteed to have an associated
 * near-zero occupancy value, i.e. no cachelines are tagged with this
 * RMID, once __intel_cqm_rmid_rotate() returns.
 */
static u32 intel_cqm_rotation_rmid;

#define INVALID_RMID		(-1)

/*
 * Is @rmid valid for programming the hardware?
 *
 * rmid 0 is reserved by the hardware for all non-monitored tasks, which
 * means that we should never come across an rmid with that value.
 * Likewise, an rmid value of -1 is used to indicate "no rmid currently
 * assigned" and is used as part of the rotation code.
 */
static inline bool __rmid_valid(u32 rmid)
{
	if (!rmid || rmid == INVALID_RMID)
		return false;

	return true;
}

static u64 __rmid_read(u32 rmid)
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

enum rmid_recycle_state {
	RMID_YOUNG = 0,
	RMID_AVAILABLE,
	RMID_DIRTY,
};

struct cqm_rmid_entry {
	u32 rmid;
	enum rmid_recycle_state state;
	struct list_head list;
	unsigned long queue_time;
};

/*
 * cqm_rmid_free_lru - A least recently used list of RMIDs.
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
 *
 * cqm_rmid_limbo_lru - list of currently unused but (potentially) dirty RMIDs.
 *
 * This list is contains RMIDs that no one is currently using but that
 * may have a non-zero occupancy value associated with them. The
 * rotation worker moves RMIDs from the limbo list to the free list once
 * the occupancy value drops below __intel_cqm_threshold.
 *
 * Both lists are protected by cache_mutex.
 */
static LIST_HEAD(cqm_rmid_free_lru);
static LIST_HEAD(cqm_rmid_limbo_lru);

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

static inline struct cqm_rmid_entry *__rmid_entry(u32 rmid)
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
static u32 __get_rmid(void)
{
	struct cqm_rmid_entry *entry;

	lockdep_assert_held(&cache_mutex);

	if (list_empty(&cqm_rmid_free_lru))
		return INVALID_RMID;

	entry = list_first_entry(&cqm_rmid_free_lru, struct cqm_rmid_entry, list);
	list_del(&entry->list);

	return entry->rmid;
}

static void __put_rmid(u32 rmid)
{
	struct cqm_rmid_entry *entry;

	lockdep_assert_held(&cache_mutex);

	WARN_ON(!__rmid_valid(rmid));
	entry = __rmid_entry(rmid);

	entry->queue_time = jiffies;
	entry->state = RMID_YOUNG;

	list_add_tail(&entry->list, &cqm_rmid_limbo_lru);
}

static int intel_cqm_setup_rmid_cache(void)
{
	struct cqm_rmid_entry *entry;
	unsigned int nr_rmids;
	int r = 0;

	nr_rmids = cqm_max_rmid + 1;
	cqm_rmid_ptrs = kmalloc(sizeof(struct cqm_rmid_entry *) *
				nr_rmids, GFP_KERNEL);
	if (!cqm_rmid_ptrs)
		return -ENOMEM;

	for (; r <= cqm_max_rmid; r++) {
		struct cqm_rmid_entry *entry;

		entry = kmalloc(sizeof(*entry), GFP_KERNEL);
		if (!entry)
			goto fail;

		INIT_LIST_HEAD(&entry->list);
		entry->rmid = r;
		cqm_rmid_ptrs[r] = entry;

		list_add_tail(&entry->list, &cqm_rmid_free_lru);
	}

	/*
	 * RMID 0 is special and is always allocated. It's used for all
	 * tasks that are not monitored.
	 */
	entry = __rmid_entry(0);
	list_del(&entry->list);

	mutex_lock(&cache_mutex);
	intel_cqm_rotation_rmid = __get_rmid();
	mutex_unlock(&cache_mutex);

	return 0;
fail:
	while (r--)
		kfree(cqm_rmid_ptrs[r]);

	kfree(cqm_rmid_ptrs);
	return -ENOMEM;
}

/*
 * Determine if @a and @b measure the same set of tasks.
 *
 * If @a and @b measure the same set of tasks then we want to share a
 * single RMID.
 */
static bool __match_event(struct perf_event *a, struct perf_event *b)
{
	/* Per-cpu and task events don't mix */
	if ((a->attach_state & PERF_ATTACH_TASK) !=
	    (b->attach_state & PERF_ATTACH_TASK))
		return false;

#ifdef CONFIG_CGROUP_PERF
	if (a->cgrp != b->cgrp)
		return false;
#endif

	/* If not task event, we're machine wide */
	if (!(b->attach_state & PERF_ATTACH_TASK))
		return true;

	/*
	 * Events that target same task are placed into the same cache group.
	 */
	if (a->hw.target == b->hw.target)
		return true;

	/*
	 * Are we an inherited event?
	 */
	if (b->parent == a)
		return true;

	return false;
}

#ifdef CONFIG_CGROUP_PERF
static inline struct perf_cgroup *event_to_cgroup(struct perf_event *event)
{
	if (event->attach_state & PERF_ATTACH_TASK)
		return perf_cgroup_from_task(event->hw.target);

	return event->cgrp;
}
#endif

/*
 * Determine if @a's tasks intersect with @b's tasks
 *
 * There are combinations of events that we explicitly prohibit,
 *
 *		   PROHIBITS
 *     system-wide    -> 	cgroup and task
 *     cgroup 	      ->	system-wide
 *     		      ->	task in cgroup
 *     task 	      -> 	system-wide
 *     		      ->	task in cgroup
 *
 * Call this function before allocating an RMID.
 */
static bool __conflict_event(struct perf_event *a, struct perf_event *b)
{
#ifdef CONFIG_CGROUP_PERF
	/*
	 * We can have any number of cgroups but only one system-wide
	 * event at a time.
	 */
	if (a->cgrp && b->cgrp) {
		struct perf_cgroup *ac = a->cgrp;
		struct perf_cgroup *bc = b->cgrp;

		/*
		 * This condition should have been caught in
		 * __match_event() and we should be sharing an RMID.
		 */
		WARN_ON_ONCE(ac == bc);

		if (cgroup_is_descendant(ac->css.cgroup, bc->css.cgroup) ||
		    cgroup_is_descendant(bc->css.cgroup, ac->css.cgroup))
			return true;

		return false;
	}

	if (a->cgrp || b->cgrp) {
		struct perf_cgroup *ac, *bc;

		/*
		 * cgroup and system-wide events are mutually exclusive
		 */
		if ((a->cgrp && !(b->attach_state & PERF_ATTACH_TASK)) ||
		    (b->cgrp && !(a->attach_state & PERF_ATTACH_TASK)))
			return true;

		/*
		 * Ensure neither event is part of the other's cgroup
		 */
		ac = event_to_cgroup(a);
		bc = event_to_cgroup(b);
		if (ac == bc)
			return true;

		/*
		 * Must have cgroup and non-intersecting task events.
		 */
		if (!ac || !bc)
			return false;

		/*
		 * We have cgroup and task events, and the task belongs
		 * to a cgroup. Check for for overlap.
		 */
		if (cgroup_is_descendant(ac->css.cgroup, bc->css.cgroup) ||
		    cgroup_is_descendant(bc->css.cgroup, ac->css.cgroup))
			return true;

		return false;
	}
#endif
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

struct rmid_read {
	u32 rmid;
	atomic64_t value;
};

static void __intel_cqm_event_count(void *info);

/*
 * Exchange the RMID of a group of events.
 */
static u32 intel_cqm_xchg_rmid(struct perf_event *group, u32 rmid)
{
	struct perf_event *event;
	struct list_head *head = &group->hw.cqm_group_entry;
	u32 old_rmid = group->hw.cqm_rmid;

	lockdep_assert_held(&cache_mutex);

	/*
	 * If our RMID is being deallocated, perform a read now.
	 */
	if (__rmid_valid(old_rmid) && !__rmid_valid(rmid)) {
		struct rmid_read rr = {
			.value = ATOMIC64_INIT(0),
			.rmid = old_rmid,
		};

		on_each_cpu_mask(&cqm_cpumask, __intel_cqm_event_count,
				 &rr, 1);
		local64_set(&group->count, atomic64_read(&rr.value));
	}

	raw_spin_lock_irq(&cache_lock);

	group->hw.cqm_rmid = rmid;
	list_for_each_entry(event, head, hw.cqm_group_entry)
		event->hw.cqm_rmid = rmid;

	raw_spin_unlock_irq(&cache_lock);

	return old_rmid;
}

/*
 * If we fail to assign a new RMID for intel_cqm_rotation_rmid because
 * cachelines are still tagged with RMIDs in limbo, we progressively
 * increment the threshold until we find an RMID in limbo with <=
 * __intel_cqm_threshold lines tagged. This is designed to mitigate the
 * problem where cachelines tagged with an RMID are not steadily being
 * evicted.
 *
 * On successful rotations we decrease the threshold back towards zero.
 *
 * __intel_cqm_max_threshold provides an upper bound on the threshold,
 * and is measured in bytes because it's exposed to userland.
 */
static unsigned int __intel_cqm_threshold;
static unsigned int __intel_cqm_max_threshold;

/*
 * Test whether an RMID has a zero occupancy value on this cpu.
 */
static void intel_cqm_stable(void *arg)
{
	struct cqm_rmid_entry *entry;

	list_for_each_entry(entry, &cqm_rmid_limbo_lru, list) {
		if (entry->state != RMID_AVAILABLE)
			break;

		if (__rmid_read(entry->rmid) > __intel_cqm_threshold)
			entry->state = RMID_DIRTY;
	}
}

/*
 * If we have group events waiting for an RMID that don't conflict with
 * events already running, assign @rmid.
 */
static bool intel_cqm_sched_in_event(u32 rmid)
{
	struct perf_event *leader, *event;

	lockdep_assert_held(&cache_mutex);

	leader = list_first_entry(&cache_groups, struct perf_event,
				  hw.cqm_groups_entry);
	event = leader;

	list_for_each_entry_continue(event, &cache_groups,
				     hw.cqm_groups_entry) {
		if (__rmid_valid(event->hw.cqm_rmid))
			continue;

		if (__conflict_event(event, leader))
			continue;

		intel_cqm_xchg_rmid(event, rmid);
		return true;
	}

	return false;
}

/*
 * Initially use this constant for both the limbo queue time and the
 * rotation timer interval, pmu::hrtimer_interval_ms.
 *
 * They don't need to be the same, but the two are related since if you
 * rotate faster than you recycle RMIDs, you may run out of available
 * RMIDs.
 */
#define RMID_DEFAULT_QUEUE_TIME 250	/* ms */

static unsigned int __rmid_queue_time_ms = RMID_DEFAULT_QUEUE_TIME;

/*
 * intel_cqm_rmid_stabilize - move RMIDs from limbo to free list
 * @nr_available: number of freeable RMIDs on the limbo list
 *
 * Quiescent state; wait for all 'freed' RMIDs to become unused, i.e. no
 * cachelines are tagged with those RMIDs. After this we can reuse them
 * and know that the current set of active RMIDs is stable.
 *
 * Return %true or %false depending on whether stabilization needs to be
 * reattempted.
 *
 * If we return %true then @nr_available is updated to indicate the
 * number of RMIDs on the limbo list that have been queued for the
 * minimum queue time (RMID_AVAILABLE), but whose data occupancy values
 * are above __intel_cqm_threshold.
 */
static bool intel_cqm_rmid_stabilize(unsigned int *available)
{
	struct cqm_rmid_entry *entry, *tmp;

	lockdep_assert_held(&cache_mutex);

	*available = 0;
	list_for_each_entry(entry, &cqm_rmid_limbo_lru, list) {
		unsigned long min_queue_time;
		unsigned long now = jiffies;

		/*
		 * We hold RMIDs placed into limbo for a minimum queue
		 * time. Before the minimum queue time has elapsed we do
		 * not recycle RMIDs.
		 *
		 * The reasoning is that until a sufficient time has
		 * passed since we stopped using an RMID, any RMID
		 * placed onto the limbo list will likely still have
		 * data tagged in the cache, which means we'll probably
		 * fail to recycle it anyway.
		 *
		 * We can save ourselves an expensive IPI by skipping
		 * any RMIDs that have not been queued for the minimum
		 * time.
		 */
		min_queue_time = entry->queue_time +
			msecs_to_jiffies(__rmid_queue_time_ms);

		if (time_after(min_queue_time, now))
			break;

		entry->state = RMID_AVAILABLE;
		(*available)++;
	}

	/*
	 * Fast return if none of the RMIDs on the limbo list have been
	 * sitting on the queue for the minimum queue time.
	 */
	if (!*available)
		return false;

	/*
	 * Test whether an RMID is free for each package.
	 */
	on_each_cpu_mask(&cqm_cpumask, intel_cqm_stable, NULL, true);

	list_for_each_entry_safe(entry, tmp, &cqm_rmid_limbo_lru, list) {
		/*
		 * Exhausted all RMIDs that have waited min queue time.
		 */
		if (entry->state == RMID_YOUNG)
			break;

		if (entry->state == RMID_DIRTY)
			continue;

		list_del(&entry->list);	/* remove from limbo */

		/*
		 * The rotation RMID gets priority if it's
		 * currently invalid. In which case, skip adding
		 * the RMID to the the free lru.
		 */
		if (!__rmid_valid(intel_cqm_rotation_rmid)) {
			intel_cqm_rotation_rmid = entry->rmid;
			continue;
		}

		/*
		 * If we have groups waiting for RMIDs, hand
		 * them one now provided they don't conflict.
		 */
		if (intel_cqm_sched_in_event(entry->rmid))
			continue;

		/*
		 * Otherwise place it onto the free list.
		 */
		list_add_tail(&entry->list, &cqm_rmid_free_lru);
	}


	return __rmid_valid(intel_cqm_rotation_rmid);
}

/*
 * Pick a victim group and move it to the tail of the group list.
 * @next: The first group without an RMID
 */
static void __intel_cqm_pick_and_rotate(struct perf_event *next)
{
	struct perf_event *rotor;
	u32 rmid;

	lockdep_assert_held(&cache_mutex);

	rotor = list_first_entry(&cache_groups, struct perf_event,
				 hw.cqm_groups_entry);

	/*
	 * The group at the front of the list should always have a valid
	 * RMID. If it doesn't then no groups have RMIDs assigned and we
	 * don't need to rotate the list.
	 */
	if (next == rotor)
		return;

	rmid = intel_cqm_xchg_rmid(rotor, INVALID_RMID);
	__put_rmid(rmid);

	list_rotate_left(&cache_groups);
}

/*
 * Deallocate the RMIDs from any events that conflict with @event, and
 * place them on the back of the group list.
 */
static void intel_cqm_sched_out_conflicting_events(struct perf_event *event)
{
	struct perf_event *group, *g;
	u32 rmid;

	lockdep_assert_held(&cache_mutex);

	list_for_each_entry_safe(group, g, &cache_groups, hw.cqm_groups_entry) {
		if (group == event)
			continue;

		rmid = group->hw.cqm_rmid;

		/*
		 * Skip events that don't have a valid RMID.
		 */
		if (!__rmid_valid(rmid))
			continue;

		/*
		 * No conflict? No problem! Leave the event alone.
		 */
		if (!__conflict_event(group, event))
			continue;

		intel_cqm_xchg_rmid(group, INVALID_RMID);
		__put_rmid(rmid);
	}
}

/*
 * Attempt to rotate the groups and assign new RMIDs.
 *
 * We rotate for two reasons,
 *   1. To handle the scheduling of conflicting events
 *   2. To recycle RMIDs
 *
 * Rotating RMIDs is complicated because the hardware doesn't give us
 * any clues.
 *
 * There's problems with the hardware interface; when you change the
 * task:RMID map cachelines retain their 'old' tags, giving a skewed
 * picture. In order to work around this, we must always keep one free
 * RMID - intel_cqm_rotation_rmid.
 *
 * Rotation works by taking away an RMID from a group (the old RMID),
 * and assigning the free RMID to another group (the new RMID). We must
 * then wait for the old RMID to not be used (no cachelines tagged).
 * This ensure that all cachelines are tagged with 'active' RMIDs. At
 * this point we can start reading values for the new RMID and treat the
 * old RMID as the free RMID for the next rotation.
 *
 * Return %true or %false depending on whether we did any rotating.
 */
static bool __intel_cqm_rmid_rotate(void)
{
	struct perf_event *group, *start = NULL;
	unsigned int threshold_limit;
	unsigned int nr_needed = 0;
	unsigned int nr_available;
	bool rotated = false;

	mutex_lock(&cache_mutex);

again:
	/*
	 * Fast path through this function if there are no groups and no
	 * RMIDs that need cleaning.
	 */
	if (list_empty(&cache_groups) && list_empty(&cqm_rmid_limbo_lru))
		goto out;

	list_for_each_entry(group, &cache_groups, hw.cqm_groups_entry) {
		if (!__rmid_valid(group->hw.cqm_rmid)) {
			if (!start)
				start = group;
			nr_needed++;
		}
	}

	/*
	 * We have some event groups, but they all have RMIDs assigned
	 * and no RMIDs need cleaning.
	 */
	if (!nr_needed && list_empty(&cqm_rmid_limbo_lru))
		goto out;

	if (!nr_needed)
		goto stabilize;

	/*
	 * We have more event groups without RMIDs than available RMIDs,
	 * or we have event groups that conflict with the ones currently
	 * scheduled.
	 *
	 * We force deallocate the rmid of the group at the head of
	 * cache_groups. The first event group without an RMID then gets
	 * assigned intel_cqm_rotation_rmid. This ensures we always make
	 * forward progress.
	 *
	 * Rotate the cache_groups list so the previous head is now the
	 * tail.
	 */
	__intel_cqm_pick_and_rotate(start);

	/*
	 * If the rotation is going to succeed, reduce the threshold so
	 * that we don't needlessly reuse dirty RMIDs.
	 */
	if (__rmid_valid(intel_cqm_rotation_rmid)) {
		intel_cqm_xchg_rmid(start, intel_cqm_rotation_rmid);
		intel_cqm_rotation_rmid = __get_rmid();

		intel_cqm_sched_out_conflicting_events(start);

		if (__intel_cqm_threshold)
			__intel_cqm_threshold--;
	}

	rotated = true;

stabilize:
	/*
	 * We now need to stablize the RMID we freed above (if any) to
	 * ensure that the next time we rotate we have an RMID with zero
	 * occupancy value.
	 *
	 * Alternatively, if we didn't need to perform any rotation,
	 * we'll have a bunch of RMIDs in limbo that need stabilizing.
	 */
	threshold_limit = __intel_cqm_max_threshold / cqm_l3_scale;

	while (intel_cqm_rmid_stabilize(&nr_available) &&
	       __intel_cqm_threshold < threshold_limit) {
		unsigned int steal_limit;

		/*
		 * Don't spin if nobody is actively waiting for an RMID,
		 * the rotation worker will be kicked as soon as an
		 * event needs an RMID anyway.
		 */
		if (!nr_needed)
			break;

		/* Allow max 25% of RMIDs to be in limbo. */
		steal_limit = (cqm_max_rmid + 1) / 4;

		/*
		 * We failed to stabilize any RMIDs so our rotation
		 * logic is now stuck. In order to make forward progress
		 * we have a few options:
		 *
		 *   1. rotate ("steal") another RMID
		 *   2. increase the threshold
		 *   3. do nothing
		 *
		 * We do both of 1. and 2. until we hit the steal limit.
		 *
		 * The steal limit prevents all RMIDs ending up on the
		 * limbo list. This can happen if every RMID has a
		 * non-zero occupancy above threshold_limit, and the
		 * occupancy values aren't dropping fast enough.
		 *
		 * Note that there is prioritisation at work here - we'd
		 * rather increase the number of RMIDs on the limbo list
		 * than increase the threshold, because increasing the
		 * threshold skews the event data (because we reuse
		 * dirty RMIDs) - threshold bumps are a last resort.
		 */
		if (nr_available < steal_limit)
			goto again;

		__intel_cqm_threshold++;
	}

out:
	mutex_unlock(&cache_mutex);
	return rotated;
}

static void intel_cqm_rmid_rotate(struct work_struct *work);

static DECLARE_DELAYED_WORK(intel_cqm_rmid_work, intel_cqm_rmid_rotate);

static struct pmu intel_cqm_pmu;

static void intel_cqm_rmid_rotate(struct work_struct *work)
{
	unsigned long delay;

	__intel_cqm_rmid_rotate();

	delay = msecs_to_jiffies(intel_cqm_pmu.hrtimer_interval_ms);
	schedule_delayed_work(&intel_cqm_rmid_work, delay);
}

/*
 * Find a group and setup RMID.
 *
 * If we're part of a group, we use the group's RMID.
 */
static void intel_cqm_setup_event(struct perf_event *event,
				  struct perf_event **group)
{
	struct perf_event *iter;
	bool conflict = false;
	u32 rmid;

	list_for_each_entry(iter, &cache_groups, hw.cqm_groups_entry) {
		rmid = iter->hw.cqm_rmid;

		if (__match_event(iter, event)) {
			/* All tasks in a group share an RMID */
			event->hw.cqm_rmid = rmid;
			*group = iter;
			return;
		}

		/*
		 * We only care about conflicts for events that are
		 * actually scheduled in (and hence have a valid RMID).
		 */
		if (__conflict_event(iter, event) && __rmid_valid(rmid))
			conflict = true;
	}

	if (conflict)
		rmid = INVALID_RMID;
	else
		rmid = __get_rmid();

	event->hw.cqm_rmid = rmid;
}

static void intel_cqm_event_read(struct perf_event *event)
{
	unsigned long flags;
	u32 rmid;
	u64 val;

	/*
	 * Task events are handled by intel_cqm_event_count().
	 */
	if (event->cpu == -1)
		return;

	raw_spin_lock_irqsave(&cache_lock, flags);
	rmid = event->hw.cqm_rmid;

	if (!__rmid_valid(rmid))
		goto out;

	val = __rmid_read(rmid);

	/*
	 * Ignore this reading on error states and do not update the value.
	 */
	if (val & (RMID_VAL_ERROR | RMID_VAL_UNAVAIL))
		goto out;

	local64_set(&event->count, val);
out:
	raw_spin_unlock_irqrestore(&cache_lock, flags);
}

static void __intel_cqm_event_count(void *info)
{
	struct rmid_read *rr = info;
	u64 val;

	val = __rmid_read(rr->rmid);

	if (val & (RMID_VAL_ERROR | RMID_VAL_UNAVAIL))
		return;

	atomic64_add(val, &rr->value);
}

static inline bool cqm_group_leader(struct perf_event *event)
{
	return !list_empty(&event->hw.cqm_groups_entry);
}

static u64 intel_cqm_event_count(struct perf_event *event)
{
	unsigned long flags;
	struct rmid_read rr = {
		.value = ATOMIC64_INIT(0),
	};

	/*
	 * We only need to worry about task events. System-wide events
	 * are handled like usual, i.e. entirely with
	 * intel_cqm_event_read().
	 */
	if (event->cpu != -1)
		return __perf_event_count(event);

	/*
	 * Only the group leader gets to report values. This stops us
	 * reporting duplicate values to userspace, and gives us a clear
	 * rule for which task gets to report the values.
	 *
	 * Note that it is impossible to attribute these values to
	 * specific packages - we forfeit that ability when we create
	 * task events.
	 */
	if (!cqm_group_leader(event))
		return 0;

	/*
	 * Getting up-to-date values requires an SMP IPI which is not
	 * possible if we're being called in interrupt context. Return
	 * the cached values instead.
	 */
	if (unlikely(in_interrupt()))
		goto out;

	/*
	 * Notice that we don't perform the reading of an RMID
	 * atomically, because we can't hold a spin lock across the
	 * IPIs.
	 *
	 * Speculatively perform the read, since @event might be
	 * assigned a different (possibly invalid) RMID while we're
	 * busying performing the IPI calls. It's therefore necessary to
	 * check @event's RMID afterwards, and if it has changed,
	 * discard the result of the read.
	 */
	rr.rmid = ACCESS_ONCE(event->hw.cqm_rmid);

	if (!__rmid_valid(rr.rmid))
		goto out;

	on_each_cpu_mask(&cqm_cpumask, __intel_cqm_event_count, &rr, 1);

	raw_spin_lock_irqsave(&cache_lock, flags);
	if (event->hw.cqm_rmid == rr.rmid)
		local64_set(&event->count, atomic64_read(&rr.value));
	raw_spin_unlock_irqrestore(&cache_lock, flags);
out:
	return __perf_event_count(event);
}

static void intel_cqm_event_start(struct perf_event *event, int mode)
{
	struct intel_pqr_state *state = this_cpu_ptr(&pqr_state);
	u32 rmid = event->hw.cqm_rmid;

	if (!(event->hw.cqm_state & PERF_HES_STOPPED))
		return;

	event->hw.cqm_state &= ~PERF_HES_STOPPED;

	if (state->rmid_usecnt++) {
		if (!WARN_ON_ONCE(state->rmid != rmid))
			return;
	} else {
		WARN_ON_ONCE(state->rmid);
	}

	state->rmid = rmid;
	wrmsr(MSR_IA32_PQR_ASSOC, rmid, state->closid);
}

static void intel_cqm_event_stop(struct perf_event *event, int mode)
{
	struct intel_pqr_state *state = this_cpu_ptr(&pqr_state);

	if (event->hw.cqm_state & PERF_HES_STOPPED)
		return;

	event->hw.cqm_state |= PERF_HES_STOPPED;

	intel_cqm_event_read(event);

	if (!--state->rmid_usecnt) {
		state->rmid = 0;
		wrmsr(MSR_IA32_PQR_ASSOC, 0, state->closid);
	} else {
		WARN_ON_ONCE(!state->rmid);
	}
}

static int intel_cqm_event_add(struct perf_event *event, int mode)
{
	unsigned long flags;
	u32 rmid;

	raw_spin_lock_irqsave(&cache_lock, flags);

	event->hw.cqm_state = PERF_HES_STOPPED;
	rmid = event->hw.cqm_rmid;

	if (__rmid_valid(rmid) && (mode & PERF_EF_START))
		intel_cqm_event_start(event, mode);

	raw_spin_unlock_irqrestore(&cache_lock, flags);

	return 0;
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
	if (cqm_group_leader(event)) {
		/*
		 * If there was a group_other, make that leader, otherwise
		 * destroy the group and return the RMID.
		 */
		if (group_other) {
			list_replace(&event->hw.cqm_groups_entry,
				     &group_other->hw.cqm_groups_entry);
		} else {
			u32 rmid = event->hw.cqm_rmid;

			if (__rmid_valid(rmid))
				__put_rmid(rmid);
			list_del(&event->hw.cqm_groups_entry);
		}
	}

	mutex_unlock(&cache_mutex);
}

static int intel_cqm_event_init(struct perf_event *event)
{
	struct perf_event *group = NULL;
	bool rotate = false;

	if (event->attr.type != intel_cqm_pmu.type)
		return -ENOENT;

	if (event->attr.config & ~QOS_EVENT_MASK)
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

	/* Will also set rmid */
	intel_cqm_setup_event(event, &group);

	if (group) {
		list_add_tail(&event->hw.cqm_group_entry,
			      &group->hw.cqm_group_entry);
	} else {
		list_add_tail(&event->hw.cqm_groups_entry,
			      &cache_groups);

		/*
		 * All RMIDs are either in use or have recently been
		 * used. Kick the rotation worker to clean/free some.
		 *
		 * We only do this for the group leader, rather than for
		 * every event in a group to save on needless work.
		 */
		if (!__rmid_valid(event->hw.cqm_rmid))
			rotate = true;
	}

	mutex_unlock(&cache_mutex);

	if (rotate)
		schedule_delayed_work(&intel_cqm_rmid_work, 0);

	return 0;
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

static ssize_t
max_recycle_threshold_show(struct device *dev, struct device_attribute *attr,
			   char *page)
{
	ssize_t rv;

	mutex_lock(&cache_mutex);
	rv = snprintf(page, PAGE_SIZE-1, "%u\n", __intel_cqm_max_threshold);
	mutex_unlock(&cache_mutex);

	return rv;
}

static ssize_t
max_recycle_threshold_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	unsigned int bytes, cachelines;
	int ret;

	ret = kstrtouint(buf, 0, &bytes);
	if (ret)
		return ret;

	mutex_lock(&cache_mutex);

	__intel_cqm_max_threshold = bytes;
	cachelines = bytes / cqm_l3_scale;

	/*
	 * The new maximum takes effect immediately.
	 */
	if (__intel_cqm_threshold > cachelines)
		__intel_cqm_threshold = cachelines;

	mutex_unlock(&cache_mutex);

	return count;
}

static DEVICE_ATTR_RW(max_recycle_threshold);

static struct attribute *intel_cqm_attrs[] = {
	&dev_attr_max_recycle_threshold.attr,
	NULL,
};

static const struct attribute_group intel_cqm_group = {
	.attrs = intel_cqm_attrs,
};

static const struct attribute_group *intel_cqm_attr_groups[] = {
	&intel_cqm_events_group,
	&intel_cqm_format_group,
	&intel_cqm_group,
	NULL,
};

static struct pmu intel_cqm_pmu = {
	.hrtimer_interval_ms = RMID_DEFAULT_QUEUE_TIME,
	.attr_groups	     = intel_cqm_attr_groups,
	.task_ctx_nr	     = perf_sw_context,
	.event_init	     = intel_cqm_event_init,
	.add		     = intel_cqm_event_add,
	.del		     = intel_cqm_event_stop,
	.start		     = intel_cqm_event_start,
	.stop		     = intel_cqm_event_stop,
	.read		     = intel_cqm_event_read,
	.count		     = intel_cqm_event_count,
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
	struct intel_pqr_state *state = &per_cpu(pqr_state, cpu);
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	state->rmid = 0;
	state->closid = 0;
	state->rmid_usecnt = 0;

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

	/*
	 * A reasonable upper limit on the max threshold is the number
	 * of lines tagged per RMID if all RMIDs have the same number of
	 * lines tagged in the LLC.
	 *
	 * For a 35MB LLC and 56 RMIDs, this is ~1.8% of the LLC.
	 */
	__intel_cqm_max_threshold =
		boot_cpu_data.x86_cache_size * 1024 / (cqm_max_rmid + 1);

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

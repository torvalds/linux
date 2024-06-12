// SPDX-License-Identifier: GPL-2.0-only
/*
 * Resource Director Technology(RDT)
 * - Monitoring code
 *
 * Copyright (C) 2017 Intel Corporation
 *
 * Author:
 *    Vikas Shivappa <vikas.shivappa@intel.com>
 *
 * This replaces the cqm.c based on perf but we reuse a lot of
 * code and datastructures originally from Peter Zijlstra and Matt Fleming.
 *
 * More information about RDT be found in the Intel (R) x86 Architecture
 * Software Developer Manual June 2016, volume 3, section 17.17.
 */

#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/sizes.h>
#include <linux/slab.h>

#include <asm/cpu_device_id.h>
#include <asm/resctrl.h>

#include "internal.h"
#include "trace.h"

/**
 * struct rmid_entry - dirty tracking for all RMID.
 * @closid:	The CLOSID for this entry.
 * @rmid:	The RMID for this entry.
 * @busy:	The number of domains with cached data using this RMID.
 * @list:	Member of the rmid_free_lru list when busy == 0.
 *
 * Depending on the architecture the correct monitor is accessed using
 * both @closid and @rmid, or @rmid only.
 *
 * Take the rdtgroup_mutex when accessing.
 */
struct rmid_entry {
	u32				closid;
	u32				rmid;
	int				busy;
	struct list_head		list;
};

/*
 * @rmid_free_lru - A least recently used list of free RMIDs
 *     These RMIDs are guaranteed to have an occupancy less than the
 *     threshold occupancy
 */
static LIST_HEAD(rmid_free_lru);

/*
 * @closid_num_dirty_rmid    The number of dirty RMID each CLOSID has.
 *     Only allocated when CONFIG_RESCTRL_RMID_DEPENDS_ON_CLOSID is defined.
 *     Indexed by CLOSID. Protected by rdtgroup_mutex.
 */
static u32 *closid_num_dirty_rmid;

/*
 * @rmid_limbo_count - count of currently unused but (potentially)
 *     dirty RMIDs.
 *     This counts RMIDs that no one is currently using but that
 *     may have a occupancy value > resctrl_rmid_realloc_threshold. User can
 *     change the threshold occupancy value.
 */
static unsigned int rmid_limbo_count;

/*
 * @rmid_entry - The entry in the limbo and free lists.
 */
static struct rmid_entry	*rmid_ptrs;

/*
 * Global boolean for rdt_monitor which is true if any
 * resource monitoring is enabled.
 */
bool rdt_mon_capable;

/*
 * Global to indicate which monitoring events are enabled.
 */
unsigned int rdt_mon_features;

/*
 * This is the threshold cache occupancy in bytes at which we will consider an
 * RMID available for re-allocation.
 */
unsigned int resctrl_rmid_realloc_threshold;

/*
 * This is the maximum value for the reallocation threshold, in bytes.
 */
unsigned int resctrl_rmid_realloc_limit;

#define CF(cf)	((unsigned long)(1048576 * (cf) + 0.5))

/*
 * The correction factor table is documented in Documentation/arch/x86/resctrl.rst.
 * If rmid > rmid threshold, MBM total and local values should be multiplied
 * by the correction factor.
 *
 * The original table is modified for better code:
 *
 * 1. The threshold 0 is changed to rmid count - 1 so don't do correction
 *    for the case.
 * 2. MBM total and local correction table indexed by core counter which is
 *    equal to (x86_cache_max_rmid + 1) / 8 - 1 and is from 0 up to 27.
 * 3. The correction factor is normalized to 2^20 (1048576) so it's faster
 *    to calculate corrected value by shifting:
 *    corrected_value = (original_value * correction_factor) >> 20
 */
static const struct mbm_correction_factor_table {
	u32 rmidthreshold;
	u64 cf;
} mbm_cf_table[] __initconst = {
	{7,	CF(1.000000)},
	{15,	CF(1.000000)},
	{15,	CF(0.969650)},
	{31,	CF(1.000000)},
	{31,	CF(1.066667)},
	{31,	CF(0.969650)},
	{47,	CF(1.142857)},
	{63,	CF(1.000000)},
	{63,	CF(1.185115)},
	{63,	CF(1.066553)},
	{79,	CF(1.454545)},
	{95,	CF(1.000000)},
	{95,	CF(1.230769)},
	{95,	CF(1.142857)},
	{95,	CF(1.066667)},
	{127,	CF(1.000000)},
	{127,	CF(1.254863)},
	{127,	CF(1.185255)},
	{151,	CF(1.000000)},
	{127,	CF(1.066667)},
	{167,	CF(1.000000)},
	{159,	CF(1.454334)},
	{183,	CF(1.000000)},
	{127,	CF(0.969744)},
	{191,	CF(1.280246)},
	{191,	CF(1.230921)},
	{215,	CF(1.000000)},
	{191,	CF(1.143118)},
};

static u32 mbm_cf_rmidthreshold __read_mostly = UINT_MAX;
static u64 mbm_cf __read_mostly;

static inline u64 get_corrected_mbm_count(u32 rmid, unsigned long val)
{
	/* Correct MBM value. */
	if (rmid > mbm_cf_rmidthreshold)
		val = (val * mbm_cf) >> 20;

	return val;
}

/*
 * x86 and arm64 differ in their handling of monitoring.
 * x86's RMID are independent numbers, there is only one source of traffic
 * with an RMID value of '1'.
 * arm64's PMG extends the PARTID/CLOSID space, there are multiple sources of
 * traffic with a PMG value of '1', one for each CLOSID, meaning the RMID
 * value is no longer unique.
 * To account for this, resctrl uses an index. On x86 this is just the RMID,
 * on arm64 it encodes the CLOSID and RMID. This gives a unique number.
 *
 * The domain's rmid_busy_llc and rmid_ptrs[] are sized by index. The arch code
 * must accept an attempt to read every index.
 */
static inline struct rmid_entry *__rmid_entry(u32 idx)
{
	struct rmid_entry *entry;
	u32 closid, rmid;

	entry = &rmid_ptrs[idx];
	resctrl_arch_rmid_idx_decode(idx, &closid, &rmid);

	WARN_ON_ONCE(entry->closid != closid);
	WARN_ON_ONCE(entry->rmid != rmid);

	return entry;
}

static int __rmid_read(u32 rmid, enum resctrl_event_id eventid, u64 *val)
{
	u64 msr_val;

	/*
	 * As per the SDM, when IA32_QM_EVTSEL.EvtID (bits 7:0) is configured
	 * with a valid event code for supported resource type and the bits
	 * IA32_QM_EVTSEL.RMID (bits 41:32) are configured with valid RMID,
	 * IA32_QM_CTR.data (bits 61:0) reports the monitored data.
	 * IA32_QM_CTR.Error (bit 63) and IA32_QM_CTR.Unavailable (bit 62)
	 * are error bits.
	 */
	wrmsr(MSR_IA32_QM_EVTSEL, eventid, rmid);
	rdmsrl(MSR_IA32_QM_CTR, msr_val);

	if (msr_val & RMID_VAL_ERROR)
		return -EIO;
	if (msr_val & RMID_VAL_UNAVAIL)
		return -EINVAL;

	*val = msr_val;
	return 0;
}

static struct arch_mbm_state *get_arch_mbm_state(struct rdt_hw_domain *hw_dom,
						 u32 rmid,
						 enum resctrl_event_id eventid)
{
	switch (eventid) {
	case QOS_L3_OCCUP_EVENT_ID:
		return NULL;
	case QOS_L3_MBM_TOTAL_EVENT_ID:
		return &hw_dom->arch_mbm_total[rmid];
	case QOS_L3_MBM_LOCAL_EVENT_ID:
		return &hw_dom->arch_mbm_local[rmid];
	}

	/* Never expect to get here */
	WARN_ON_ONCE(1);

	return NULL;
}

void resctrl_arch_reset_rmid(struct rdt_resource *r, struct rdt_domain *d,
			     u32 unused, u32 rmid,
			     enum resctrl_event_id eventid)
{
	struct rdt_hw_domain *hw_dom = resctrl_to_arch_dom(d);
	struct arch_mbm_state *am;

	am = get_arch_mbm_state(hw_dom, rmid, eventid);
	if (am) {
		memset(am, 0, sizeof(*am));

		/* Record any initial, non-zero count value. */
		__rmid_read(rmid, eventid, &am->prev_msr);
	}
}

/*
 * Assumes that hardware counters are also reset and thus that there is
 * no need to record initial non-zero counts.
 */
void resctrl_arch_reset_rmid_all(struct rdt_resource *r, struct rdt_domain *d)
{
	struct rdt_hw_domain *hw_dom = resctrl_to_arch_dom(d);

	if (is_mbm_total_enabled())
		memset(hw_dom->arch_mbm_total, 0,
		       sizeof(*hw_dom->arch_mbm_total) * r->num_rmid);

	if (is_mbm_local_enabled())
		memset(hw_dom->arch_mbm_local, 0,
		       sizeof(*hw_dom->arch_mbm_local) * r->num_rmid);
}

static u64 mbm_overflow_count(u64 prev_msr, u64 cur_msr, unsigned int width)
{
	u64 shift = 64 - width, chunks;

	chunks = (cur_msr << shift) - (prev_msr << shift);
	return chunks >> shift;
}

int resctrl_arch_rmid_read(struct rdt_resource *r, struct rdt_domain *d,
			   u32 unused, u32 rmid, enum resctrl_event_id eventid,
			   u64 *val, void *ignored)
{
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);
	struct rdt_hw_domain *hw_dom = resctrl_to_arch_dom(d);
	struct arch_mbm_state *am;
	u64 msr_val, chunks;
	int ret;

	resctrl_arch_rmid_read_context_check();

	if (!cpumask_test_cpu(smp_processor_id(), &d->cpu_mask))
		return -EINVAL;

	ret = __rmid_read(rmid, eventid, &msr_val);
	if (ret)
		return ret;

	am = get_arch_mbm_state(hw_dom, rmid, eventid);
	if (am) {
		am->chunks += mbm_overflow_count(am->prev_msr, msr_val,
						 hw_res->mbm_width);
		chunks = get_corrected_mbm_count(rmid, am->chunks);
		am->prev_msr = msr_val;
	} else {
		chunks = msr_val;
	}

	*val = chunks * hw_res->mon_scale;

	return 0;
}

static void limbo_release_entry(struct rmid_entry *entry)
{
	lockdep_assert_held(&rdtgroup_mutex);

	rmid_limbo_count--;
	list_add_tail(&entry->list, &rmid_free_lru);

	if (IS_ENABLED(CONFIG_RESCTRL_RMID_DEPENDS_ON_CLOSID))
		closid_num_dirty_rmid[entry->closid]--;
}

/*
 * Check the RMIDs that are marked as busy for this domain. If the
 * reported LLC occupancy is below the threshold clear the busy bit and
 * decrement the count. If the busy count gets to zero on an RMID, we
 * free the RMID
 */
void __check_limbo(struct rdt_domain *d, bool force_free)
{
	struct rdt_resource *r = &rdt_resources_all[RDT_RESOURCE_L3].r_resctrl;
	u32 idx_limit = resctrl_arch_system_num_rmid_idx();
	struct rmid_entry *entry;
	u32 idx, cur_idx = 1;
	void *arch_mon_ctx;
	bool rmid_dirty;
	u64 val = 0;

	arch_mon_ctx = resctrl_arch_mon_ctx_alloc(r, QOS_L3_OCCUP_EVENT_ID);
	if (IS_ERR(arch_mon_ctx)) {
		pr_warn_ratelimited("Failed to allocate monitor context: %ld",
				    PTR_ERR(arch_mon_ctx));
		return;
	}

	/*
	 * Skip RMID 0 and start from RMID 1 and check all the RMIDs that
	 * are marked as busy for occupancy < threshold. If the occupancy
	 * is less than the threshold decrement the busy counter of the
	 * RMID and move it to the free list when the counter reaches 0.
	 */
	for (;;) {
		idx = find_next_bit(d->rmid_busy_llc, idx_limit, cur_idx);
		if (idx >= idx_limit)
			break;

		entry = __rmid_entry(idx);
		if (resctrl_arch_rmid_read(r, d, entry->closid, entry->rmid,
					   QOS_L3_OCCUP_EVENT_ID, &val,
					   arch_mon_ctx)) {
			rmid_dirty = true;
		} else {
			rmid_dirty = (val >= resctrl_rmid_realloc_threshold);

			/*
			 * x86's CLOSID and RMID are independent numbers, so the entry's
			 * CLOSID is an empty CLOSID (X86_RESCTRL_EMPTY_CLOSID). On Arm the
			 * RMID (PMG) extends the CLOSID (PARTID) space with bits that aren't
			 * used to select the configuration. It is thus necessary to track both
			 * CLOSID and RMID because there may be dependencies between them
			 * on some architectures.
			 */
			trace_mon_llc_occupancy_limbo(entry->closid, entry->rmid, d->id, val);
		}

		if (force_free || !rmid_dirty) {
			clear_bit(idx, d->rmid_busy_llc);
			if (!--entry->busy)
				limbo_release_entry(entry);
		}
		cur_idx = idx + 1;
	}

	resctrl_arch_mon_ctx_free(r, QOS_L3_OCCUP_EVENT_ID, arch_mon_ctx);
}

bool has_busy_rmid(struct rdt_domain *d)
{
	u32 idx_limit = resctrl_arch_system_num_rmid_idx();

	return find_first_bit(d->rmid_busy_llc, idx_limit) != idx_limit;
}

static struct rmid_entry *resctrl_find_free_rmid(u32 closid)
{
	struct rmid_entry *itr;
	u32 itr_idx, cmp_idx;

	if (list_empty(&rmid_free_lru))
		return rmid_limbo_count ? ERR_PTR(-EBUSY) : ERR_PTR(-ENOSPC);

	list_for_each_entry(itr, &rmid_free_lru, list) {
		/*
		 * Get the index of this free RMID, and the index it would need
		 * to be if it were used with this CLOSID.
		 * If the CLOSID is irrelevant on this architecture, the two
		 * index values are always the same on every entry and thus the
		 * very first entry will be returned.
		 */
		itr_idx = resctrl_arch_rmid_idx_encode(itr->closid, itr->rmid);
		cmp_idx = resctrl_arch_rmid_idx_encode(closid, itr->rmid);

		if (itr_idx == cmp_idx)
			return itr;
	}

	return ERR_PTR(-ENOSPC);
}

/**
 * resctrl_find_cleanest_closid() - Find a CLOSID where all the associated
 *                                  RMID are clean, or the CLOSID that has
 *                                  the most clean RMID.
 *
 * MPAM's equivalent of RMID are per-CLOSID, meaning a freshly allocated CLOSID
 * may not be able to allocate clean RMID. To avoid this the allocator will
 * choose the CLOSID with the most clean RMID.
 *
 * When the CLOSID and RMID are independent numbers, the first free CLOSID will
 * be returned.
 */
int resctrl_find_cleanest_closid(void)
{
	u32 cleanest_closid = ~0;
	int i = 0;

	lockdep_assert_held(&rdtgroup_mutex);

	if (!IS_ENABLED(CONFIG_RESCTRL_RMID_DEPENDS_ON_CLOSID))
		return -EIO;

	for (i = 0; i < closids_supported(); i++) {
		int num_dirty;

		if (closid_allocated(i))
			continue;

		num_dirty = closid_num_dirty_rmid[i];
		if (num_dirty == 0)
			return i;

		if (cleanest_closid == ~0)
			cleanest_closid = i;

		if (num_dirty < closid_num_dirty_rmid[cleanest_closid])
			cleanest_closid = i;
	}

	if (cleanest_closid == ~0)
		return -ENOSPC;

	return cleanest_closid;
}

/*
 * For MPAM the RMID value is not unique, and has to be considered with
 * the CLOSID. The (CLOSID, RMID) pair is allocated on all domains, which
 * allows all domains to be managed by a single free list.
 * Each domain also has a rmid_busy_llc to reduce the work of the limbo handler.
 */
int alloc_rmid(u32 closid)
{
	struct rmid_entry *entry;

	lockdep_assert_held(&rdtgroup_mutex);

	entry = resctrl_find_free_rmid(closid);
	if (IS_ERR(entry))
		return PTR_ERR(entry);

	list_del(&entry->list);
	return entry->rmid;
}

static void add_rmid_to_limbo(struct rmid_entry *entry)
{
	struct rdt_resource *r = &rdt_resources_all[RDT_RESOURCE_L3].r_resctrl;
	struct rdt_domain *d;
	u32 idx;

	lockdep_assert_held(&rdtgroup_mutex);

	/* Walking r->domains, ensure it can't race with cpuhp */
	lockdep_assert_cpus_held();

	idx = resctrl_arch_rmid_idx_encode(entry->closid, entry->rmid);

	entry->busy = 0;
	list_for_each_entry(d, &r->domains, list) {
		/*
		 * For the first limbo RMID in the domain,
		 * setup up the limbo worker.
		 */
		if (!has_busy_rmid(d))
			cqm_setup_limbo_handler(d, CQM_LIMBOCHECK_INTERVAL,
						RESCTRL_PICK_ANY_CPU);
		set_bit(idx, d->rmid_busy_llc);
		entry->busy++;
	}

	rmid_limbo_count++;
	if (IS_ENABLED(CONFIG_RESCTRL_RMID_DEPENDS_ON_CLOSID))
		closid_num_dirty_rmid[entry->closid]++;
}

void free_rmid(u32 closid, u32 rmid)
{
	u32 idx = resctrl_arch_rmid_idx_encode(closid, rmid);
	struct rmid_entry *entry;

	lockdep_assert_held(&rdtgroup_mutex);

	/*
	 * Do not allow the default rmid to be free'd. Comparing by index
	 * allows architectures that ignore the closid parameter to avoid an
	 * unnecessary check.
	 */
	if (idx == resctrl_arch_rmid_idx_encode(RESCTRL_RESERVED_CLOSID,
						RESCTRL_RESERVED_RMID))
		return;

	entry = __rmid_entry(idx);

	if (is_llc_occupancy_enabled())
		add_rmid_to_limbo(entry);
	else
		list_add_tail(&entry->list, &rmid_free_lru);
}

static struct mbm_state *get_mbm_state(struct rdt_domain *d, u32 closid,
				       u32 rmid, enum resctrl_event_id evtid)
{
	u32 idx = resctrl_arch_rmid_idx_encode(closid, rmid);

	switch (evtid) {
	case QOS_L3_MBM_TOTAL_EVENT_ID:
		return &d->mbm_total[idx];
	case QOS_L3_MBM_LOCAL_EVENT_ID:
		return &d->mbm_local[idx];
	default:
		return NULL;
	}
}

static int __mon_event_count(u32 closid, u32 rmid, struct rmid_read *rr)
{
	struct mbm_state *m;
	u64 tval = 0;

	if (rr->first) {
		resctrl_arch_reset_rmid(rr->r, rr->d, closid, rmid, rr->evtid);
		m = get_mbm_state(rr->d, closid, rmid, rr->evtid);
		if (m)
			memset(m, 0, sizeof(struct mbm_state));
		return 0;
	}

	rr->err = resctrl_arch_rmid_read(rr->r, rr->d, closid, rmid, rr->evtid,
					 &tval, rr->arch_mon_ctx);
	if (rr->err)
		return rr->err;

	rr->val += tval;

	return 0;
}

/*
 * mbm_bw_count() - Update bw count from values previously read by
 *		    __mon_event_count().
 * @closid:	The closid used to identify the cached mbm_state.
 * @rmid:	The rmid used to identify the cached mbm_state.
 * @rr:		The struct rmid_read populated by __mon_event_count().
 *
 * Supporting function to calculate the memory bandwidth
 * and delta bandwidth in MBps. The chunks value previously read by
 * __mon_event_count() is compared with the chunks value from the previous
 * invocation. This must be called once per second to maintain values in MBps.
 */
static void mbm_bw_count(u32 closid, u32 rmid, struct rmid_read *rr)
{
	u32 idx = resctrl_arch_rmid_idx_encode(closid, rmid);
	struct mbm_state *m = &rr->d->mbm_local[idx];
	u64 cur_bw, bytes, cur_bytes;

	cur_bytes = rr->val;
	bytes = cur_bytes - m->prev_bw_bytes;
	m->prev_bw_bytes = cur_bytes;

	cur_bw = bytes / SZ_1M;

	m->prev_bw = cur_bw;
}

/*
 * This is scheduled by mon_event_read() to read the CQM/MBM counters
 * on a domain.
 */
void mon_event_count(void *info)
{
	struct rdtgroup *rdtgrp, *entry;
	struct rmid_read *rr = info;
	struct list_head *head;
	int ret;

	rdtgrp = rr->rgrp;

	ret = __mon_event_count(rdtgrp->closid, rdtgrp->mon.rmid, rr);

	/*
	 * For Ctrl groups read data from child monitor groups and
	 * add them together. Count events which are read successfully.
	 * Discard the rmid_read's reporting errors.
	 */
	head = &rdtgrp->mon.crdtgrp_list;

	if (rdtgrp->type == RDTCTRL_GROUP) {
		list_for_each_entry(entry, head, mon.crdtgrp_list) {
			if (__mon_event_count(entry->closid, entry->mon.rmid,
					      rr) == 0)
				ret = 0;
		}
	}

	/*
	 * __mon_event_count() calls for newly created monitor groups may
	 * report -EINVAL/Unavailable if the monitor hasn't seen any traffic.
	 * Discard error if any of the monitor event reads succeeded.
	 */
	if (ret == 0)
		rr->err = 0;
}

/*
 * Feedback loop for MBA software controller (mba_sc)
 *
 * mba_sc is a feedback loop where we periodically read MBM counters and
 * adjust the bandwidth percentage values via the IA32_MBA_THRTL_MSRs so
 * that:
 *
 *   current bandwidth(cur_bw) < user specified bandwidth(user_bw)
 *
 * This uses the MBM counters to measure the bandwidth and MBA throttle
 * MSRs to control the bandwidth for a particular rdtgrp. It builds on the
 * fact that resctrl rdtgroups have both monitoring and control.
 *
 * The frequency of the checks is 1s and we just tag along the MBM overflow
 * timer. Having 1s interval makes the calculation of bandwidth simpler.
 *
 * Although MBA's goal is to restrict the bandwidth to a maximum, there may
 * be a need to increase the bandwidth to avoid unnecessarily restricting
 * the L2 <-> L3 traffic.
 *
 * Since MBA controls the L2 external bandwidth where as MBM measures the
 * L3 external bandwidth the following sequence could lead to such a
 * situation.
 *
 * Consider an rdtgroup which had high L3 <-> memory traffic in initial
 * phases -> mba_sc kicks in and reduced bandwidth percentage values -> but
 * after some time rdtgroup has mostly L2 <-> L3 traffic.
 *
 * In this case we may restrict the rdtgroup's L2 <-> L3 traffic as its
 * throttle MSRs already have low percentage values.  To avoid
 * unnecessarily restricting such rdtgroups, we also increase the bandwidth.
 */
static void update_mba_bw(struct rdtgroup *rgrp, struct rdt_domain *dom_mbm)
{
	u32 closid, rmid, cur_msr_val, new_msr_val;
	struct mbm_state *pmbm_data, *cmbm_data;
	struct rdt_resource *r_mba;
	struct rdt_domain *dom_mba;
	u32 cur_bw, user_bw, idx;
	struct list_head *head;
	struct rdtgroup *entry;

	if (!is_mbm_local_enabled())
		return;

	r_mba = &rdt_resources_all[RDT_RESOURCE_MBA].r_resctrl;

	closid = rgrp->closid;
	rmid = rgrp->mon.rmid;
	idx = resctrl_arch_rmid_idx_encode(closid, rmid);
	pmbm_data = &dom_mbm->mbm_local[idx];

	dom_mba = get_domain_from_cpu(smp_processor_id(), r_mba);
	if (!dom_mba) {
		pr_warn_once("Failure to get domain for MBA update\n");
		return;
	}

	cur_bw = pmbm_data->prev_bw;
	user_bw = dom_mba->mbps_val[closid];

	/* MBA resource doesn't support CDP */
	cur_msr_val = resctrl_arch_get_config(r_mba, dom_mba, closid, CDP_NONE);

	/*
	 * For Ctrl groups read data from child monitor groups.
	 */
	head = &rgrp->mon.crdtgrp_list;
	list_for_each_entry(entry, head, mon.crdtgrp_list) {
		cmbm_data = &dom_mbm->mbm_local[entry->mon.rmid];
		cur_bw += cmbm_data->prev_bw;
	}

	/*
	 * Scale up/down the bandwidth linearly for the ctrl group.  The
	 * bandwidth step is the bandwidth granularity specified by the
	 * hardware.
	 * Always increase throttling if current bandwidth is above the
	 * target set by user.
	 * But avoid thrashing up and down on every poll by checking
	 * whether a decrease in throttling is likely to push the group
	 * back over target. E.g. if currently throttling to 30% of bandwidth
	 * on a system with 10% granularity steps, check whether moving to
	 * 40% would go past the limit by multiplying current bandwidth by
	 * "(30 + 10) / 30".
	 */
	if (cur_msr_val > r_mba->membw.min_bw && user_bw < cur_bw) {
		new_msr_val = cur_msr_val - r_mba->membw.bw_gran;
	} else if (cur_msr_val < MAX_MBA_BW &&
		   (user_bw > (cur_bw * (cur_msr_val + r_mba->membw.min_bw) / cur_msr_val))) {
		new_msr_val = cur_msr_val + r_mba->membw.bw_gran;
	} else {
		return;
	}

	resctrl_arch_update_one(r_mba, dom_mba, closid, CDP_NONE, new_msr_val);
}

static void mbm_update(struct rdt_resource *r, struct rdt_domain *d,
		       u32 closid, u32 rmid)
{
	struct rmid_read rr;

	rr.first = false;
	rr.r = r;
	rr.d = d;

	/*
	 * This is protected from concurrent reads from user
	 * as both the user and we hold the global mutex.
	 */
	if (is_mbm_total_enabled()) {
		rr.evtid = QOS_L3_MBM_TOTAL_EVENT_ID;
		rr.val = 0;
		rr.arch_mon_ctx = resctrl_arch_mon_ctx_alloc(rr.r, rr.evtid);
		if (IS_ERR(rr.arch_mon_ctx)) {
			pr_warn_ratelimited("Failed to allocate monitor context: %ld",
					    PTR_ERR(rr.arch_mon_ctx));
			return;
		}

		__mon_event_count(closid, rmid, &rr);

		resctrl_arch_mon_ctx_free(rr.r, rr.evtid, rr.arch_mon_ctx);
	}
	if (is_mbm_local_enabled()) {
		rr.evtid = QOS_L3_MBM_LOCAL_EVENT_ID;
		rr.val = 0;
		rr.arch_mon_ctx = resctrl_arch_mon_ctx_alloc(rr.r, rr.evtid);
		if (IS_ERR(rr.arch_mon_ctx)) {
			pr_warn_ratelimited("Failed to allocate monitor context: %ld",
					    PTR_ERR(rr.arch_mon_ctx));
			return;
		}

		__mon_event_count(closid, rmid, &rr);

		/*
		 * Call the MBA software controller only for the
		 * control groups and when user has enabled
		 * the software controller explicitly.
		 */
		if (is_mba_sc(NULL))
			mbm_bw_count(closid, rmid, &rr);

		resctrl_arch_mon_ctx_free(rr.r, rr.evtid, rr.arch_mon_ctx);
	}
}

/*
 * Handler to scan the limbo list and move the RMIDs
 * to free list whose occupancy < threshold_occupancy.
 */
void cqm_handle_limbo(struct work_struct *work)
{
	unsigned long delay = msecs_to_jiffies(CQM_LIMBOCHECK_INTERVAL);
	struct rdt_domain *d;

	cpus_read_lock();
	mutex_lock(&rdtgroup_mutex);

	d = container_of(work, struct rdt_domain, cqm_limbo.work);

	__check_limbo(d, false);

	if (has_busy_rmid(d)) {
		d->cqm_work_cpu = cpumask_any_housekeeping(&d->cpu_mask,
							   RESCTRL_PICK_ANY_CPU);
		schedule_delayed_work_on(d->cqm_work_cpu, &d->cqm_limbo,
					 delay);
	}

	mutex_unlock(&rdtgroup_mutex);
	cpus_read_unlock();
}

/**
 * cqm_setup_limbo_handler() - Schedule the limbo handler to run for this
 *                             domain.
 * @dom:           The domain the limbo handler should run for.
 * @delay_ms:      How far in the future the handler should run.
 * @exclude_cpu:   Which CPU the handler should not run on,
 *		   RESCTRL_PICK_ANY_CPU to pick any CPU.
 */
void cqm_setup_limbo_handler(struct rdt_domain *dom, unsigned long delay_ms,
			     int exclude_cpu)
{
	unsigned long delay = msecs_to_jiffies(delay_ms);
	int cpu;

	cpu = cpumask_any_housekeeping(&dom->cpu_mask, exclude_cpu);
	dom->cqm_work_cpu = cpu;

	if (cpu < nr_cpu_ids)
		schedule_delayed_work_on(cpu, &dom->cqm_limbo, delay);
}

void mbm_handle_overflow(struct work_struct *work)
{
	unsigned long delay = msecs_to_jiffies(MBM_OVERFLOW_INTERVAL);
	struct rdtgroup *prgrp, *crgrp;
	struct list_head *head;
	struct rdt_resource *r;
	struct rdt_domain *d;

	cpus_read_lock();
	mutex_lock(&rdtgroup_mutex);

	/*
	 * If the filesystem has been unmounted this work no longer needs to
	 * run.
	 */
	if (!resctrl_mounted || !resctrl_arch_mon_capable())
		goto out_unlock;

	r = &rdt_resources_all[RDT_RESOURCE_L3].r_resctrl;
	d = container_of(work, struct rdt_domain, mbm_over.work);

	list_for_each_entry(prgrp, &rdt_all_groups, rdtgroup_list) {
		mbm_update(r, d, prgrp->closid, prgrp->mon.rmid);

		head = &prgrp->mon.crdtgrp_list;
		list_for_each_entry(crgrp, head, mon.crdtgrp_list)
			mbm_update(r, d, crgrp->closid, crgrp->mon.rmid);

		if (is_mba_sc(NULL))
			update_mba_bw(prgrp, d);
	}

	/*
	 * Re-check for housekeeping CPUs. This allows the overflow handler to
	 * move off a nohz_full CPU quickly.
	 */
	d->mbm_work_cpu = cpumask_any_housekeeping(&d->cpu_mask,
						   RESCTRL_PICK_ANY_CPU);
	schedule_delayed_work_on(d->mbm_work_cpu, &d->mbm_over, delay);

out_unlock:
	mutex_unlock(&rdtgroup_mutex);
	cpus_read_unlock();
}

/**
 * mbm_setup_overflow_handler() - Schedule the overflow handler to run for this
 *                                domain.
 * @dom:           The domain the overflow handler should run for.
 * @delay_ms:      How far in the future the handler should run.
 * @exclude_cpu:   Which CPU the handler should not run on,
 *		   RESCTRL_PICK_ANY_CPU to pick any CPU.
 */
void mbm_setup_overflow_handler(struct rdt_domain *dom, unsigned long delay_ms,
				int exclude_cpu)
{
	unsigned long delay = msecs_to_jiffies(delay_ms);
	int cpu;

	/*
	 * When a domain comes online there is no guarantee the filesystem is
	 * mounted. If not, there is no need to catch counter overflow.
	 */
	if (!resctrl_mounted || !resctrl_arch_mon_capable())
		return;
	cpu = cpumask_any_housekeeping(&dom->cpu_mask, exclude_cpu);
	dom->mbm_work_cpu = cpu;

	if (cpu < nr_cpu_ids)
		schedule_delayed_work_on(cpu, &dom->mbm_over, delay);
}

static int dom_data_init(struct rdt_resource *r)
{
	u32 idx_limit = resctrl_arch_system_num_rmid_idx();
	u32 num_closid = resctrl_arch_get_num_closid(r);
	struct rmid_entry *entry = NULL;
	int err = 0, i;
	u32 idx;

	mutex_lock(&rdtgroup_mutex);
	if (IS_ENABLED(CONFIG_RESCTRL_RMID_DEPENDS_ON_CLOSID)) {
		u32 *tmp;

		/*
		 * If the architecture hasn't provided a sanitised value here,
		 * this may result in larger arrays than necessary. Resctrl will
		 * use a smaller system wide value based on the resources in
		 * use.
		 */
		tmp = kcalloc(num_closid, sizeof(*tmp), GFP_KERNEL);
		if (!tmp) {
			err = -ENOMEM;
			goto out_unlock;
		}

		closid_num_dirty_rmid = tmp;
	}

	rmid_ptrs = kcalloc(idx_limit, sizeof(struct rmid_entry), GFP_KERNEL);
	if (!rmid_ptrs) {
		if (IS_ENABLED(CONFIG_RESCTRL_RMID_DEPENDS_ON_CLOSID)) {
			kfree(closid_num_dirty_rmid);
			closid_num_dirty_rmid = NULL;
		}
		err = -ENOMEM;
		goto out_unlock;
	}

	for (i = 0; i < idx_limit; i++) {
		entry = &rmid_ptrs[i];
		INIT_LIST_HEAD(&entry->list);

		resctrl_arch_rmid_idx_decode(i, &entry->closid, &entry->rmid);
		list_add_tail(&entry->list, &rmid_free_lru);
	}

	/*
	 * RESCTRL_RESERVED_CLOSID and RESCTRL_RESERVED_RMID are special and
	 * are always allocated. These are used for the rdtgroup_default
	 * control group, which will be setup later in rdtgroup_init().
	 */
	idx = resctrl_arch_rmid_idx_encode(RESCTRL_RESERVED_CLOSID,
					   RESCTRL_RESERVED_RMID);
	entry = __rmid_entry(idx);
	list_del(&entry->list);

out_unlock:
	mutex_unlock(&rdtgroup_mutex);

	return err;
}

static void __exit dom_data_exit(void)
{
	mutex_lock(&rdtgroup_mutex);

	if (IS_ENABLED(CONFIG_RESCTRL_RMID_DEPENDS_ON_CLOSID)) {
		kfree(closid_num_dirty_rmid);
		closid_num_dirty_rmid = NULL;
	}

	kfree(rmid_ptrs);
	rmid_ptrs = NULL;

	mutex_unlock(&rdtgroup_mutex);
}

static struct mon_evt llc_occupancy_event = {
	.name		= "llc_occupancy",
	.evtid		= QOS_L3_OCCUP_EVENT_ID,
};

static struct mon_evt mbm_total_event = {
	.name		= "mbm_total_bytes",
	.evtid		= QOS_L3_MBM_TOTAL_EVENT_ID,
};

static struct mon_evt mbm_local_event = {
	.name		= "mbm_local_bytes",
	.evtid		= QOS_L3_MBM_LOCAL_EVENT_ID,
};

/*
 * Initialize the event list for the resource.
 *
 * Note that MBM events are also part of RDT_RESOURCE_L3 resource
 * because as per the SDM the total and local memory bandwidth
 * are enumerated as part of L3 monitoring.
 */
static void l3_mon_evt_init(struct rdt_resource *r)
{
	INIT_LIST_HEAD(&r->evt_list);

	if (is_llc_occupancy_enabled())
		list_add_tail(&llc_occupancy_event.list, &r->evt_list);
	if (is_mbm_total_enabled())
		list_add_tail(&mbm_total_event.list, &r->evt_list);
	if (is_mbm_local_enabled())
		list_add_tail(&mbm_local_event.list, &r->evt_list);
}

int __init rdt_get_mon_l3_config(struct rdt_resource *r)
{
	unsigned int mbm_offset = boot_cpu_data.x86_cache_mbm_width_offset;
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);
	unsigned int threshold;
	int ret;

	resctrl_rmid_realloc_limit = boot_cpu_data.x86_cache_size * 1024;
	hw_res->mon_scale = boot_cpu_data.x86_cache_occ_scale;
	r->num_rmid = boot_cpu_data.x86_cache_max_rmid + 1;
	hw_res->mbm_width = MBM_CNTR_WIDTH_BASE;

	if (mbm_offset > 0 && mbm_offset <= MBM_CNTR_WIDTH_OFFSET_MAX)
		hw_res->mbm_width += mbm_offset;
	else if (mbm_offset > MBM_CNTR_WIDTH_OFFSET_MAX)
		pr_warn("Ignoring impossible MBM counter offset\n");

	/*
	 * A reasonable upper limit on the max threshold is the number
	 * of lines tagged per RMID if all RMIDs have the same number of
	 * lines tagged in the LLC.
	 *
	 * For a 35MB LLC and 56 RMIDs, this is ~1.8% of the LLC.
	 */
	threshold = resctrl_rmid_realloc_limit / r->num_rmid;

	/*
	 * Because num_rmid may not be a power of two, round the value
	 * to the nearest multiple of hw_res->mon_scale so it matches a
	 * value the hardware will measure. mon_scale may not be a power of 2.
	 */
	resctrl_rmid_realloc_threshold = resctrl_arch_round_mon_val(threshold);

	ret = dom_data_init(r);
	if (ret)
		return ret;

	if (rdt_cpu_has(X86_FEATURE_BMEC)) {
		u32 eax, ebx, ecx, edx;

		/* Detect list of bandwidth sources that can be tracked */
		cpuid_count(0x80000020, 3, &eax, &ebx, &ecx, &edx);
		hw_res->mbm_cfg_mask = ecx & MAX_EVT_CONFIG_BITS;

		if (rdt_cpu_has(X86_FEATURE_CQM_MBM_TOTAL)) {
			mbm_total_event.configurable = true;
			mbm_config_rftype_init("mbm_total_bytes_config");
		}
		if (rdt_cpu_has(X86_FEATURE_CQM_MBM_LOCAL)) {
			mbm_local_event.configurable = true;
			mbm_config_rftype_init("mbm_local_bytes_config");
		}
	}

	l3_mon_evt_init(r);

	r->mon_capable = true;

	return 0;
}

void __exit rdt_put_mon_l3_config(void)
{
	dom_data_exit();
}

void __init intel_rdt_mbm_apply_quirk(void)
{
	int cf_index;

	cf_index = (boot_cpu_data.x86_cache_max_rmid + 1) / 8 - 1;
	if (cf_index >= ARRAY_SIZE(mbm_cf_table)) {
		pr_info("No MBM correction factor available\n");
		return;
	}

	mbm_cf_rmidthreshold = mbm_cf_table[cf_index].rmidthreshold;
	mbm_cf = mbm_cf_table[cf_index].cf;
}

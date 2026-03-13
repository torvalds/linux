// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Arm Ltd.

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/arm_mpam.h>
#include <linux/cacheinfo.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/errno.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/math.h>
#include <linux/printk.h>
#include <linux/rculist.h>
#include <linux/resctrl.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/wait.h>

#include <asm/mpam.h>

#include "mpam_internal.h"

DECLARE_WAIT_QUEUE_HEAD(resctrl_mon_ctx_waiters);

/*
 * The classes we've picked to map to resctrl resources, wrapped
 * in with their resctrl structure.
 * Class pointer may be NULL.
 */
static struct mpam_resctrl_res mpam_resctrl_controls[RDT_NUM_RESOURCES];

#define for_each_mpam_resctrl_control(res, rid)					\
	for (rid = 0, res = &mpam_resctrl_controls[rid];			\
	     rid < RDT_NUM_RESOURCES;						\
	     rid++, res = &mpam_resctrl_controls[rid])

/*
 * The classes we've picked to map to resctrl events.
 * Resctrl believes all the worlds a Xeon, and these are all on the L3. This
 * array lets us find the actual class backing the event counters. e.g.
 * the only memory bandwidth counters may be on the memory controller, but to
 * make use of them, we pretend they are on L3. Restrict the events considered
 * to those supported by MPAM.
 * Class pointer may be NULL.
 */
#define MPAM_MAX_EVENT QOS_L3_MBM_TOTAL_EVENT_ID
static struct mpam_resctrl_mon mpam_resctrl_counters[MPAM_MAX_EVENT + 1];

#define for_each_mpam_resctrl_mon(mon, eventid)					\
	for (eventid = QOS_FIRST_EVENT, mon = &mpam_resctrl_counters[eventid];	\
	     eventid <= MPAM_MAX_EVENT;						\
	     eventid++, mon = &mpam_resctrl_counters[eventid])

/* The lock for modifying resctrl's domain lists from cpuhp callbacks. */
static DEFINE_MUTEX(domain_list_lock);

/*
 * MPAM emulates CDP by setting different PARTID in the I/D fields of MPAM0_EL1.
 * This applies globally to all traffic the CPU generates.
 */
static bool cdp_enabled;

/*
 * We use cacheinfo to discover the size of the caches and their id. cacheinfo
 * populates this from a device_initcall(). mpam_resctrl_setup() must wait.
 */
static bool cacheinfo_ready;
static DECLARE_WAIT_QUEUE_HEAD(wait_cacheinfo_ready);

/*
 * If resctrl_init() succeeded, resctrl_exit() can be used to remove support
 * for the filesystem in the event of an error.
 */
static bool resctrl_enabled;

bool resctrl_arch_alloc_capable(void)
{
	struct mpam_resctrl_res *res;
	enum resctrl_res_level rid;

	for_each_mpam_resctrl_control(res, rid) {
		if (res->resctrl_res.alloc_capable)
			return true;
	}

	return false;
}

bool resctrl_arch_mon_capable(void)
{
	struct mpam_resctrl_res *res = &mpam_resctrl_controls[RDT_RESOURCE_L3];
	struct rdt_resource *l3 = &res->resctrl_res;

	/* All monitors are presented as being on the L3 cache */
	return l3->mon_capable;
}

bool resctrl_arch_is_evt_configurable(enum resctrl_event_id evt)
{
	return false;
}

void resctrl_arch_mon_event_config_read(void *info)
{
}

void resctrl_arch_mon_event_config_write(void *info)
{
}

void resctrl_arch_reset_rmid_all(struct rdt_resource *r, struct rdt_l3_mon_domain *d)
{
}

void resctrl_arch_reset_rmid(struct rdt_resource *r, struct rdt_l3_mon_domain *d,
			     u32 closid, u32 rmid, enum resctrl_event_id eventid)
{
}

void resctrl_arch_reset_cntr(struct rdt_resource *r, struct rdt_l3_mon_domain *d,
			     u32 closid, u32 rmid, int cntr_id,
			     enum resctrl_event_id eventid)
{
}

void resctrl_arch_config_cntr(struct rdt_resource *r, struct rdt_l3_mon_domain *d,
			      enum resctrl_event_id evtid, u32 rmid, u32 closid,
			      u32 cntr_id, bool assign)
{
}

int resctrl_arch_cntr_read(struct rdt_resource *r, struct rdt_l3_mon_domain *d,
			   u32 unused, u32 rmid, int cntr_id,
			   enum resctrl_event_id eventid, u64 *val)
{
	return -EOPNOTSUPP;
}

bool resctrl_arch_mbm_cntr_assign_enabled(struct rdt_resource *r)
{
	return false;
}

int resctrl_arch_mbm_cntr_assign_set(struct rdt_resource *r, bool enable)
{
	return -EINVAL;
}

int resctrl_arch_io_alloc_enable(struct rdt_resource *r, bool enable)
{
	return -EOPNOTSUPP;
}

bool resctrl_arch_get_io_alloc_enabled(struct rdt_resource *r)
{
	return false;
}

void resctrl_arch_pre_mount(void)
{
}

bool resctrl_arch_get_cdp_enabled(enum resctrl_res_level rid)
{
	return mpam_resctrl_controls[rid].cdp_enabled;
}

/**
 * resctrl_reset_task_closids() - Reset the PARTID/PMG values for all tasks.
 *
 * At boot, all existing tasks use partid zero for D and I.
 * To enable/disable CDP emulation, all these tasks need relabelling.
 */
static void resctrl_reset_task_closids(void)
{
	struct task_struct *p, *t;

	read_lock(&tasklist_lock);
	for_each_process_thread(p, t) {
		resctrl_arch_set_closid_rmid(t, RESCTRL_RESERVED_CLOSID,
					     RESCTRL_RESERVED_RMID);
	}
	read_unlock(&tasklist_lock);
}

int resctrl_arch_set_cdp_enabled(enum resctrl_res_level rid, bool enable)
{
	u32 partid_i = RESCTRL_RESERVED_CLOSID, partid_d = RESCTRL_RESERVED_CLOSID;
	struct mpam_resctrl_res *res = &mpam_resctrl_controls[RDT_RESOURCE_L3];
	struct rdt_resource *l3 = &res->resctrl_res;
	int cpu;

	if (!IS_ENABLED(CONFIG_EXPERT) && enable) {
		/*
		 * If the resctrl fs is mounted more than once, sequentially,
		 * then CDP can lead to the use of out of range PARTIDs.
		 */
		pr_warn("CDP not supported\n");
		return -EOPNOTSUPP;
	}

	if (enable)
		pr_warn("CDP is an expert feature and may cause MPAM to malfunction.\n");

	/*
	 * resctrl_arch_set_cdp_enabled() is only called with enable set to
	 * false on error and unmount.
	 */
	cdp_enabled = enable;
	mpam_resctrl_controls[rid].cdp_enabled = enable;

	if (enable)
		l3->mon.num_rmid = resctrl_arch_system_num_rmid_idx() / 2;
	else
		l3->mon.num_rmid = resctrl_arch_system_num_rmid_idx();

	/* The mbw_max feature can't hide cdp as it's a per-partid maximum. */
	if (cdp_enabled && !mpam_resctrl_controls[RDT_RESOURCE_MBA].cdp_enabled)
		mpam_resctrl_controls[RDT_RESOURCE_MBA].resctrl_res.alloc_capable = false;

	if (mpam_resctrl_controls[RDT_RESOURCE_MBA].cdp_enabled &&
	    mpam_resctrl_controls[RDT_RESOURCE_MBA].class)
		mpam_resctrl_controls[RDT_RESOURCE_MBA].resctrl_res.alloc_capable = true;

	if (enable) {
		if (mpam_partid_max < 1)
			return -EINVAL;

		partid_d = resctrl_get_config_index(RESCTRL_RESERVED_CLOSID, CDP_DATA);
		partid_i = resctrl_get_config_index(RESCTRL_RESERVED_CLOSID, CDP_CODE);
	}

	mpam_set_task_partid_pmg(current, partid_d, partid_i, 0, 0);
	WRITE_ONCE(arm64_mpam_global_default, mpam_get_regval(current));

	resctrl_reset_task_closids();

	for_each_possible_cpu(cpu)
		mpam_set_cpu_defaults(cpu, partid_d, partid_i, 0, 0);
	on_each_cpu(resctrl_arch_sync_cpu_closid_rmid, NULL, 1);

	return 0;
}

static bool mpam_resctrl_hide_cdp(enum resctrl_res_level rid)
{
	return cdp_enabled && !resctrl_arch_get_cdp_enabled(rid);
}

/*
 * MSC may raise an error interrupt if it sees an out or range partid/pmg,
 * and go on to truncate the value. Regardless of what the hardware supports,
 * only the system wide safe value is safe to use.
 */
u32 resctrl_arch_get_num_closid(struct rdt_resource *ignored)
{
	return mpam_partid_max + 1;
}

u32 resctrl_arch_system_num_rmid_idx(void)
{
	return (mpam_pmg_max + 1) * (mpam_partid_max + 1);
}

u32 resctrl_arch_rmid_idx_encode(u32 closid, u32 rmid)
{
	return closid * (mpam_pmg_max + 1) + rmid;
}

void resctrl_arch_rmid_idx_decode(u32 idx, u32 *closid, u32 *rmid)
{
	*closid = idx / (mpam_pmg_max + 1);
	*rmid = idx % (mpam_pmg_max + 1);
}

void resctrl_arch_sched_in(struct task_struct *tsk)
{
	lockdep_assert_preemption_disabled();

	mpam_thread_switch(tsk);
}

void resctrl_arch_set_cpu_default_closid_rmid(int cpu, u32 closid, u32 rmid)
{
	WARN_ON_ONCE(closid > U16_MAX);
	WARN_ON_ONCE(rmid > U8_MAX);

	if (!cdp_enabled) {
		mpam_set_cpu_defaults(cpu, closid, closid, rmid, rmid);
	} else {
		/*
		 * When CDP is enabled, resctrl halves the closid range and we
		 * use odd/even partid for one closid.
		 */
		u32 partid_d = resctrl_get_config_index(closid, CDP_DATA);
		u32 partid_i = resctrl_get_config_index(closid, CDP_CODE);

		mpam_set_cpu_defaults(cpu, partid_d, partid_i, rmid, rmid);
	}
}

void resctrl_arch_sync_cpu_closid_rmid(void *info)
{
	struct resctrl_cpu_defaults *r = info;

	lockdep_assert_preemption_disabled();

	if (r) {
		resctrl_arch_set_cpu_default_closid_rmid(smp_processor_id(),
							 r->closid, r->rmid);
	}

	resctrl_arch_sched_in(current);
}

void resctrl_arch_set_closid_rmid(struct task_struct *tsk, u32 closid, u32 rmid)
{
	WARN_ON_ONCE(closid > U16_MAX);
	WARN_ON_ONCE(rmid > U8_MAX);

	if (!cdp_enabled) {
		mpam_set_task_partid_pmg(tsk, closid, closid, rmid, rmid);
	} else {
		u32 partid_d = resctrl_get_config_index(closid, CDP_DATA);
		u32 partid_i = resctrl_get_config_index(closid, CDP_CODE);

		mpam_set_task_partid_pmg(tsk, partid_d, partid_i, rmid, rmid);
	}
}

bool resctrl_arch_match_closid(struct task_struct *tsk, u32 closid)
{
	u64 regval = mpam_get_regval(tsk);
	u32 tsk_closid = FIELD_GET(MPAM0_EL1_PARTID_D, regval);

	if (cdp_enabled)
		tsk_closid >>= 1;

	return tsk_closid == closid;
}

/* The task's pmg is not unique, the partid must be considered too */
bool resctrl_arch_match_rmid(struct task_struct *tsk, u32 closid, u32 rmid)
{
	u64 regval = mpam_get_regval(tsk);
	u32 tsk_closid = FIELD_GET(MPAM0_EL1_PARTID_D, regval);
	u32 tsk_rmid = FIELD_GET(MPAM0_EL1_PMG_D, regval);

	if (cdp_enabled)
		tsk_closid >>= 1;

	return (tsk_closid == closid) && (tsk_rmid == rmid);
}

struct rdt_resource *resctrl_arch_get_resource(enum resctrl_res_level l)
{
	if (l >= RDT_NUM_RESOURCES)
		return NULL;

	return &mpam_resctrl_controls[l].resctrl_res;
}

static int resctrl_arch_mon_ctx_alloc_no_wait(enum resctrl_event_id evtid)
{
	struct mpam_resctrl_mon *mon = &mpam_resctrl_counters[evtid];

	if (!mpam_is_enabled())
		return -EINVAL;

	if (!mon->class)
		return -EINVAL;

	switch (evtid) {
	case QOS_L3_OCCUP_EVENT_ID:
		/* With CDP, one monitor gets used for both code/data reads */
		return mpam_alloc_csu_mon(mon->class);
	case QOS_L3_MBM_LOCAL_EVENT_ID:
	case QOS_L3_MBM_TOTAL_EVENT_ID:
		return USE_PRE_ALLOCATED;
	default:
		return -EOPNOTSUPP;
	}
}

void *resctrl_arch_mon_ctx_alloc(struct rdt_resource *r,
				 enum resctrl_event_id evtid)
{
	DEFINE_WAIT(wait);
	int *ret;

	ret = kmalloc_obj(*ret);
	if (!ret)
		return ERR_PTR(-ENOMEM);

	do {
		prepare_to_wait(&resctrl_mon_ctx_waiters, &wait,
				TASK_INTERRUPTIBLE);
		*ret = resctrl_arch_mon_ctx_alloc_no_wait(evtid);
		if (*ret == -ENOSPC)
			schedule();
	} while (*ret == -ENOSPC && !signal_pending(current));
	finish_wait(&resctrl_mon_ctx_waiters, &wait);

	return ret;
}

static void resctrl_arch_mon_ctx_free_no_wait(enum resctrl_event_id evtid,
					      u32 mon_idx)
{
	struct mpam_resctrl_mon *mon = &mpam_resctrl_counters[evtid];

	if (!mpam_is_enabled())
		return;

	if (!mon->class)
		return;

	if (evtid == QOS_L3_OCCUP_EVENT_ID)
		mpam_free_csu_mon(mon->class, mon_idx);

	wake_up(&resctrl_mon_ctx_waiters);
}

void resctrl_arch_mon_ctx_free(struct rdt_resource *r,
			       enum resctrl_event_id evtid, void *arch_mon_ctx)
{
	u32 mon_idx = *(u32 *)arch_mon_ctx;

	kfree(arch_mon_ctx);

	resctrl_arch_mon_ctx_free_no_wait(evtid, mon_idx);
}

static int __read_mon(struct mpam_resctrl_mon *mon, struct mpam_component *mon_comp,
		      enum mpam_device_features mon_type,
		      int mon_idx,
		      enum resctrl_conf_type cdp_type, u32 closid, u32 rmid, u64 *val)
{
	struct mon_cfg cfg;

	if (!mpam_is_enabled())
		return -EINVAL;

	/* Shift closid to account for CDP */
	closid = resctrl_get_config_index(closid, cdp_type);

	if (irqs_disabled()) {
		/* Check if we can access this domain without an IPI */
		return -EIO;
	}

	cfg = (struct mon_cfg) {
		.mon = mon_idx,
		.match_pmg = true,
		.partid = closid,
		.pmg = rmid,
	};

	return mpam_msmon_read(mon_comp, &cfg, mon_type, val);
}

static int read_mon_cdp_safe(struct mpam_resctrl_mon *mon, struct mpam_component *mon_comp,
			     enum mpam_device_features mon_type,
			     int mon_idx, u32 closid, u32 rmid, u64 *val)
{
	if (cdp_enabled) {
		u64 code_val = 0, data_val = 0;
		int err;

		err = __read_mon(mon, mon_comp, mon_type, mon_idx,
				 CDP_CODE, closid, rmid, &code_val);
		if (err)
			return err;

		err = __read_mon(mon, mon_comp, mon_type, mon_idx,
				 CDP_DATA, closid, rmid, &data_val);
		if (err)
			return err;

		*val += code_val + data_val;
		return 0;
	}

	return __read_mon(mon, mon_comp, mon_type, mon_idx,
			  CDP_NONE, closid, rmid, val);
}

/* MBWU when not in ABMC mode (not supported), and CSU counters. */
int resctrl_arch_rmid_read(struct rdt_resource *r, struct rdt_domain_hdr *hdr,
			   u32 closid, u32 rmid, enum resctrl_event_id eventid,
			   void *arch_priv, u64 *val, void *arch_mon_ctx)
{
	struct mpam_resctrl_dom *l3_dom;
	struct mpam_component *mon_comp;
	u32 mon_idx = *(u32 *)arch_mon_ctx;
	enum mpam_device_features mon_type;
	struct mpam_resctrl_mon *mon = &mpam_resctrl_counters[eventid];

	resctrl_arch_rmid_read_context_check();

	if (!mpam_is_enabled())
		return -EINVAL;

	if (eventid >= QOS_NUM_EVENTS || !mon->class)
		return -EINVAL;

	l3_dom = container_of(hdr, struct mpam_resctrl_dom, resctrl_mon_dom.hdr);
	mon_comp = l3_dom->mon_comp[eventid];

	if (eventid != QOS_L3_OCCUP_EVENT_ID)
		return -EINVAL;

	mon_type = mpam_feat_msmon_csu;

	return read_mon_cdp_safe(mon, mon_comp, mon_type, mon_idx,
				 closid, rmid, val);
}

/*
 * The rmid realloc threshold should be for the smallest cache exposed to
 * resctrl.
 */
static int update_rmid_limits(struct mpam_class *class)
{
	u32 num_unique_pmg = resctrl_arch_system_num_rmid_idx();
	struct mpam_props *cprops = &class->props;
	struct cacheinfo *ci;

	lockdep_assert_cpus_held();

	if (!mpam_has_feature(mpam_feat_msmon_csu, cprops))
		return 0;

	/*
	 * Assume cache levels are the same size for all CPUs...
	 * The check just requires any online CPU and it can't go offline as we
	 * hold the cpu lock.
	 */
	ci = get_cpu_cacheinfo_level(raw_smp_processor_id(), class->level);
	if (!ci || ci->size == 0) {
		pr_debug("Could not read cache size for class %u\n",
			 class->level);
		return -EINVAL;
	}

	if (!resctrl_rmid_realloc_limit ||
	    ci->size < resctrl_rmid_realloc_limit) {
		resctrl_rmid_realloc_limit = ci->size;
		resctrl_rmid_realloc_threshold = ci->size / num_unique_pmg;
	}

	return 0;
}

static bool cache_has_usable_cpor(struct mpam_class *class)
{
	struct mpam_props *cprops = &class->props;

	if (!mpam_has_feature(mpam_feat_cpor_part, cprops))
		return false;

	/* resctrl uses u32 for all bitmap configurations */
	return class->props.cpbm_wd <= 32;
}

static bool mba_class_use_mbw_max(struct mpam_props *cprops)
{
	return (mpam_has_feature(mpam_feat_mbw_max, cprops) &&
		cprops->bwa_wd);
}

static bool class_has_usable_mba(struct mpam_props *cprops)
{
	return mba_class_use_mbw_max(cprops);
}

static bool cache_has_usable_csu(struct mpam_class *class)
{
	struct mpam_props *cprops;

	if (!class)
		return false;

	cprops = &class->props;

	if (!mpam_has_feature(mpam_feat_msmon_csu, cprops))
		return false;

	/*
	 * CSU counters settle on the value, so we can get away with
	 * having only one.
	 */
	if (!cprops->num_csu_mon)
		return false;

	return true;
}

/*
 * Calculate the worst-case percentage change from each implemented step
 * in the control.
 */
static u32 get_mba_granularity(struct mpam_props *cprops)
{
	if (!mba_class_use_mbw_max(cprops))
		return 0;

	/*
	 * bwa_wd is the number of bits implemented in the 0.xxx
	 * fixed point fraction. 1 bit is 50%, 2 is 25% etc.
	 */
	return DIV_ROUND_UP(MAX_MBA_BW, 1 << cprops->bwa_wd);
}

/*
 * Each fixed-point hardware value architecturally represents a range
 * of values: the full range 0% - 100% is split contiguously into
 * (1 << cprops->bwa_wd) equal bands.
 *
 * Although the bwa_bwd fields have 6 bits the maximum valid value is 16
 * as it reports the width of fields that are at most 16 bits. When
 * fewer than 16 bits are valid the least significant bits are
 * ignored. The implied binary point is kept between bits 15 and 16 and
 * so the valid bits are leftmost.
 *
 * See ARM IHI0099B.a "MPAM system component specification", Section 9.3,
 * "The fixed-point fractional format" for more information.
 *
 * Find the nearest percentage value to the upper bound of the selected band:
 */
static u32 mbw_max_to_percent(u16 mbw_max, struct mpam_props *cprops)
{
	u32 val = mbw_max;

	val >>= 16 - cprops->bwa_wd;
	val += 1;
	val *= MAX_MBA_BW;
	val = DIV_ROUND_CLOSEST(val, 1 << cprops->bwa_wd);

	return val;
}

/*
 * Find the band whose upper bound is closest to the specified percentage.
 *
 * A round-to-nearest policy is followed here as a balanced compromise
 * between unexpected under-commit of the resource (where the total of
 * a set of resource allocations after conversion is less than the
 * expected total, due to rounding of the individual converted
 * percentages) and over-commit (where the total of the converted
 * allocations is greater than expected).
 */
static u16 percent_to_mbw_max(u8 pc, struct mpam_props *cprops)
{
	u32 val = pc;

	val <<= cprops->bwa_wd;
	val = DIV_ROUND_CLOSEST(val, MAX_MBA_BW);
	val = max(val, 1) - 1;
	val <<= 16 - cprops->bwa_wd;

	return val;
}

static u32 get_mba_min(struct mpam_props *cprops)
{
	if (!mba_class_use_mbw_max(cprops)) {
		WARN_ON_ONCE(1);
		return 0;
	}

	return mbw_max_to_percent(0, cprops);
}

/* Find the L3 cache that has affinity with this CPU */
static int find_l3_equivalent_bitmask(int cpu, cpumask_var_t tmp_cpumask)
{
	u32 cache_id = get_cpu_cacheinfo_id(cpu, 3);

	lockdep_assert_cpus_held();

	return mpam_get_cpumask_from_cache_id(cache_id, 3, tmp_cpumask);
}

/*
 * topology_matches_l3() - Is the provided class the same shape as L3
 * @victim:		The class we'd like to pretend is L3.
 *
 * resctrl expects all the world's a Xeon, and all counters are on the
 * L3. We allow some mapping counters on other classes. This requires
 * that the CPU->domain mapping is the same kind of shape.
 *
 * Using cacheinfo directly would make this work even if resctrl can't
 * use the L3 - but cacheinfo can't tell us anything about offline CPUs.
 * Using the L3 resctrl domain list also depends on CPUs being online.
 * Using the mpam_class we picked for L3 so we can use its domain list
 * assumes that there are MPAM controls on the L3.
 * Instead, this path eventually uses the mpam_get_cpumask_from_cache_id()
 * helper which can tell us about offline CPUs ... but getting the cache_id
 * to start with relies on at least one CPU per L3 cache being online at
 * boot.
 *
 * Walk the victim component list and compare the affinity mask with the
 * corresponding L3. The topology matches if each victim:component's affinity
 * mask is the same as the CPU's corresponding L3's. These lists/masks are
 * computed from firmware tables so don't change at runtime.
 */
static bool topology_matches_l3(struct mpam_class *victim)
{
	int cpu, err;
	struct mpam_component *victim_iter;

	lockdep_assert_cpus_held();

	cpumask_var_t __free(free_cpumask_var) tmp_cpumask = CPUMASK_VAR_NULL;
	if (!alloc_cpumask_var(&tmp_cpumask, GFP_KERNEL))
		return false;

	guard(srcu)(&mpam_srcu);
	list_for_each_entry_srcu(victim_iter, &victim->components, class_list,
				 srcu_read_lock_held(&mpam_srcu)) {
		if (cpumask_empty(&victim_iter->affinity)) {
			pr_debug("class %u has CPU-less component %u - can't match L3!\n",
				 victim->level, victim_iter->comp_id);
			return false;
		}

		cpu = cpumask_any_and(&victim_iter->affinity, cpu_online_mask);
		if (WARN_ON_ONCE(cpu >= nr_cpu_ids))
			return false;

		cpumask_clear(tmp_cpumask);
		err = find_l3_equivalent_bitmask(cpu, tmp_cpumask);
		if (err) {
			pr_debug("Failed to find L3's equivalent component to class %u component %u\n",
				 victim->level, victim_iter->comp_id);
			return false;
		}

		/* Any differing bits in the affinity mask? */
		if (!cpumask_equal(tmp_cpumask, &victim_iter->affinity)) {
			pr_debug("class %u component %u has Mismatched CPU mask with L3 equivalent\n"
				 "L3:%*pbl != victim:%*pbl\n",
				 victim->level, victim_iter->comp_id,
				 cpumask_pr_args(tmp_cpumask),
				 cpumask_pr_args(&victim_iter->affinity));

			return false;
		}
	}

	return true;
}

/*
 * Test if the traffic for a class matches that at egress from the L3. For
 * MSC at memory controllers this is only possible if there is a single L3
 * as otherwise the counters at the memory can include bandwidth from the
 * non-local L3.
 */
static bool traffic_matches_l3(struct mpam_class *class)
{
	int err, cpu;

	lockdep_assert_cpus_held();

	if (class->type == MPAM_CLASS_CACHE && class->level == 3)
		return true;

	if (class->type == MPAM_CLASS_CACHE && class->level != 3) {
		pr_debug("class %u is a different cache from L3\n", class->level);
		return false;
	}

	if (class->type != MPAM_CLASS_MEMORY) {
		pr_debug("class %u is neither of type cache or memory\n", class->level);
		return false;
	}

	cpumask_var_t __free(free_cpumask_var) tmp_cpumask = CPUMASK_VAR_NULL;
	if (!alloc_cpumask_var(&tmp_cpumask, GFP_KERNEL)) {
		pr_debug("cpumask allocation failed\n");
		return false;
	}

	cpu = cpumask_any_and(&class->affinity, cpu_online_mask);
	err = find_l3_equivalent_bitmask(cpu, tmp_cpumask);
	if (err) {
		pr_debug("Failed to find L3 downstream to cpu %d\n", cpu);
		return false;
	}

	if (!cpumask_equal(tmp_cpumask, cpu_possible_mask)) {
		pr_debug("There is more than one L3\n");
		return false;
	}

	/* Be strict; the traffic might stop in the intermediate cache. */
	if (get_cpu_cacheinfo_id(cpu, 4) != -1) {
		pr_debug("L3 isn't the last level of cache\n");
		return false;
	}

	if (num_possible_nodes() > 1) {
		pr_debug("There is more than one numa node\n");
		return false;
	}

#ifdef CONFIG_HMEM_REPORTING
	if (node_devices[cpu_to_node(cpu)]->cache_dev) {
		pr_debug("There is a memory side cache\n");
		return false;
	}
#endif

	return true;
}

/* Test whether we can export MPAM_CLASS_CACHE:{2,3}? */
static void mpam_resctrl_pick_caches(void)
{
	struct mpam_class *class;
	struct mpam_resctrl_res *res;

	lockdep_assert_cpus_held();

	guard(srcu)(&mpam_srcu);
	list_for_each_entry_srcu(class, &mpam_classes, classes_list,
				 srcu_read_lock_held(&mpam_srcu)) {
		if (class->type != MPAM_CLASS_CACHE) {
			pr_debug("class %u is not a cache\n", class->level);
			continue;
		}

		if (class->level != 2 && class->level != 3) {
			pr_debug("class %u is not L2 or L3\n", class->level);
			continue;
		}

		if (!cache_has_usable_cpor(class)) {
			pr_debug("class %u cache misses CPOR\n", class->level);
			continue;
		}

		if (!cpumask_equal(&class->affinity, cpu_possible_mask)) {
			pr_debug("class %u has missing CPUs, mask %*pb != %*pb\n", class->level,
				 cpumask_pr_args(&class->affinity),
				 cpumask_pr_args(cpu_possible_mask));
			continue;
		}

		if (class->level == 2)
			res = &mpam_resctrl_controls[RDT_RESOURCE_L2];
		else
			res = &mpam_resctrl_controls[RDT_RESOURCE_L3];
		res->class = class;
	}
}

static void mpam_resctrl_pick_mba(void)
{
	struct mpam_class *class, *candidate_class = NULL;
	struct mpam_resctrl_res *res;

	lockdep_assert_cpus_held();

	guard(srcu)(&mpam_srcu);
	list_for_each_entry_srcu(class, &mpam_classes, classes_list,
				 srcu_read_lock_held(&mpam_srcu)) {
		struct mpam_props *cprops = &class->props;

		if (class->level != 3 && class->type == MPAM_CLASS_CACHE) {
			pr_debug("class %u is a cache but not the L3\n", class->level);
			continue;
		}

		if (!class_has_usable_mba(cprops)) {
			pr_debug("class %u has no bandwidth control\n",
				 class->level);
			continue;
		}

		if (!cpumask_equal(&class->affinity, cpu_possible_mask)) {
			pr_debug("class %u has missing CPUs\n", class->level);
			continue;
		}

		if (!topology_matches_l3(class)) {
			pr_debug("class %u topology doesn't match L3\n",
				 class->level);
			continue;
		}

		if (!traffic_matches_l3(class)) {
			pr_debug("class %u traffic doesn't match L3 egress\n",
				 class->level);
			continue;
		}

		/*
		 * Pick a resource to be MBA that as close as possible to
		 * the L3. mbm_total counts the bandwidth leaving the L3
		 * cache and MBA should correspond as closely as possible
		 * for proper operation of mba_sc.
		 */
		if (!candidate_class || class->level < candidate_class->level)
			candidate_class = class;
	}

	if (candidate_class) {
		pr_debug("selected class %u to back MBA\n",
			 candidate_class->level);
		res = &mpam_resctrl_controls[RDT_RESOURCE_MBA];
		res->class = candidate_class;
	}
}

static void counter_update_class(enum resctrl_event_id evt_id,
				 struct mpam_class *class)
{
	struct mpam_class *existing_class = mpam_resctrl_counters[evt_id].class;

	if (existing_class) {
		if (class->level == 3) {
			pr_debug("Existing class is L3 - L3 wins\n");
			return;
		}

		if (existing_class->level < class->level) {
			pr_debug("Existing class is closer to L3, %u versus %u - closer is better\n",
				 existing_class->level, class->level);
			return;
		}
	}

	mpam_resctrl_counters[evt_id].class = class;
}

static void mpam_resctrl_pick_counters(void)
{
	struct mpam_class *class;

	lockdep_assert_cpus_held();

	guard(srcu)(&mpam_srcu);
	list_for_each_entry_srcu(class, &mpam_classes, classes_list,
				 srcu_read_lock_held(&mpam_srcu)) {
		/* The name of the resource is L3... */
		if (class->type == MPAM_CLASS_CACHE && class->level != 3) {
			pr_debug("class %u is a cache but not the L3", class->level);
			continue;
		}

		if (!cpumask_equal(&class->affinity, cpu_possible_mask)) {
			pr_debug("class %u does not cover all CPUs",
				 class->level);
			continue;
		}

		if (cache_has_usable_csu(class)) {
			pr_debug("class %u has usable CSU",
				 class->level);

			/* CSU counters only make sense on a cache. */
			switch (class->type) {
			case MPAM_CLASS_CACHE:
				if (update_rmid_limits(class))
					break;

				counter_update_class(QOS_L3_OCCUP_EVENT_ID, class);
				break;
			default:
				break;
			}
		}
	}
}

static int mpam_resctrl_control_init(struct mpam_resctrl_res *res)
{
	struct mpam_class *class = res->class;
	struct mpam_props *cprops = &class->props;
	struct rdt_resource *r = &res->resctrl_res;

	switch (r->rid) {
	case RDT_RESOURCE_L2:
	case RDT_RESOURCE_L3:
		r->schema_fmt = RESCTRL_SCHEMA_BITMAP;
		r->cache.arch_has_sparse_bitmasks = true;

		r->cache.cbm_len = class->props.cpbm_wd;
		/* mpam_devices will reject empty bitmaps */
		r->cache.min_cbm_bits = 1;

		if (r->rid == RDT_RESOURCE_L2) {
			r->name = "L2";
			r->ctrl_scope = RESCTRL_L2_CACHE;
			r->cdp_capable = true;
		} else {
			r->name = "L3";
			r->ctrl_scope = RESCTRL_L3_CACHE;
			r->cdp_capable = true;
		}

		/*
		 * Which bits are shared with other ...things...  Unknown
		 * devices use partid-0 which uses all the bitmap fields. Until
		 * we have configured the SMMU and GIC not to do this 'all the
		 * bits' is the correct answer here.
		 */
		r->cache.shareable_bits = resctrl_get_default_ctrl(r);
		r->alloc_capable = true;
		break;
	case RDT_RESOURCE_MBA:
		r->schema_fmt = RESCTRL_SCHEMA_RANGE;
		r->ctrl_scope = RESCTRL_L3_CACHE;

		r->membw.delay_linear = true;
		r->membw.throttle_mode = THREAD_THROTTLE_UNDEFINED;
		r->membw.min_bw = get_mba_min(cprops);
		r->membw.max_bw = MAX_MBA_BW;
		r->membw.bw_gran = get_mba_granularity(cprops);

		r->name = "MB";
		r->alloc_capable = true;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mpam_resctrl_pick_domain_id(int cpu, struct mpam_component *comp)
{
	struct mpam_class *class = comp->class;

	if (class->type == MPAM_CLASS_CACHE)
		return comp->comp_id;

	if (topology_matches_l3(class)) {
		/* Use the corresponding L3 component ID as the domain ID */
		int id = get_cpu_cacheinfo_id(cpu, 3);

		/* Implies topology_matches_l3() made a mistake */
		if (WARN_ON_ONCE(id == -1))
			return comp->comp_id;

		return id;
	}

	/* Otherwise, expose the ID used by the firmware table code. */
	return comp->comp_id;
}

static int mpam_resctrl_monitor_init(struct mpam_resctrl_mon *mon,
				     enum resctrl_event_id type)
{
	struct mpam_resctrl_res *res = &mpam_resctrl_controls[RDT_RESOURCE_L3];
	struct rdt_resource *l3 = &res->resctrl_res;

	lockdep_assert_cpus_held();

	/*
	 * There also needs to be an L3 cache present.
	 * The check just requires any online CPU and it can't go offline as we
	 * hold the cpu lock.
	 */
	if (get_cpu_cacheinfo_id(raw_smp_processor_id(), 3) == -1)
		return 0;

	/*
	 * If there are no MPAM resources on L3, force it into existence.
	 * topology_matches_l3() already ensures this looks like the L3.
	 * The domain-ids will be fixed up by mpam_resctrl_domain_hdr_init().
	 */
	if (!res->class) {
		pr_warn_once("Faking L3 MSC to enable counters.\n");
		res->class = mpam_resctrl_counters[type].class;
	}

	/*
	 * Called multiple times!, once per event type that has a
	 * monitoring class.
	 * Setting name is necessary on monitor only platforms.
	 */
	l3->name = "L3";
	l3->mon_scope = RESCTRL_L3_CACHE;

	/*
	 * num-rmid is the upper bound for the number of monitoring groups that
	 * can exist simultaneously, including the default monitoring group for
	 * each control group. Hence, advertise the whole rmid_idx space even
	 * though each control group has its own pmg/rmid space. Unfortunately,
	 * this does mean userspace needs to know the architecture to correctly
	 * interpret this value.
	 */
	l3->mon.num_rmid = resctrl_arch_system_num_rmid_idx();

	if (resctrl_enable_mon_event(type, false, 0, NULL))
		l3->mon_capable = true;

	return 0;
}

u32 resctrl_arch_get_config(struct rdt_resource *r, struct rdt_ctrl_domain *d,
			    u32 closid, enum resctrl_conf_type type)
{
	u32 partid;
	struct mpam_config *cfg;
	struct mpam_props *cprops;
	struct mpam_resctrl_res *res;
	struct mpam_resctrl_dom *dom;
	enum mpam_device_features configured_by;

	lockdep_assert_cpus_held();

	if (!mpam_is_enabled())
		return resctrl_get_default_ctrl(r);

	res = container_of(r, struct mpam_resctrl_res, resctrl_res);
	dom = container_of(d, struct mpam_resctrl_dom, resctrl_ctrl_dom);
	cprops = &res->class->props;

	/*
	 * When CDP is enabled, but the resource doesn't support it,
	 * the control is cloned across both partids.
	 * Pick one at random to read:
	 */
	if (mpam_resctrl_hide_cdp(r->rid))
		type = CDP_DATA;

	partid = resctrl_get_config_index(closid, type);
	cfg = &dom->ctrl_comp->cfg[partid];

	switch (r->rid) {
	case RDT_RESOURCE_L2:
	case RDT_RESOURCE_L3:
		configured_by = mpam_feat_cpor_part;
		break;
	case RDT_RESOURCE_MBA:
		if (mpam_has_feature(mpam_feat_mbw_max, cprops)) {
			configured_by = mpam_feat_mbw_max;
			break;
		}
		fallthrough;
	default:
		return resctrl_get_default_ctrl(r);
	}

	if (!r->alloc_capable || partid >= resctrl_arch_get_num_closid(r) ||
	    !mpam_has_feature(configured_by, cfg))
		return resctrl_get_default_ctrl(r);

	switch (configured_by) {
	case mpam_feat_cpor_part:
		return cfg->cpbm;
	case mpam_feat_mbw_max:
		return mbw_max_to_percent(cfg->mbw_max, cprops);
	default:
		return resctrl_get_default_ctrl(r);
	}
}

int resctrl_arch_update_one(struct rdt_resource *r, struct rdt_ctrl_domain *d,
			    u32 closid, enum resctrl_conf_type t, u32 cfg_val)
{
	int err;
	u32 partid;
	struct mpam_config cfg;
	struct mpam_props *cprops;
	struct mpam_resctrl_res *res;
	struct mpam_resctrl_dom *dom;

	lockdep_assert_cpus_held();
	lockdep_assert_irqs_enabled();

	if (!mpam_is_enabled())
		return -EINVAL;

	/*
	 * No need to check the CPU as mpam_apply_config() doesn't care, and
	 * resctrl_arch_update_domains() relies on this.
	 */
	res = container_of(r, struct mpam_resctrl_res, resctrl_res);
	dom = container_of(d, struct mpam_resctrl_dom, resctrl_ctrl_dom);
	cprops = &res->class->props;

	if (mpam_resctrl_hide_cdp(r->rid))
		t = CDP_DATA;

	partid = resctrl_get_config_index(closid, t);
	if (!r->alloc_capable || partid >= resctrl_arch_get_num_closid(r)) {
		pr_debug("Not alloc capable or computed PARTID out of range\n");
		return -EINVAL;
	}

	/*
	 * Copy the current config to avoid clearing other resources when the
	 * same component is exposed multiple times through resctrl.
	 */
	cfg = dom->ctrl_comp->cfg[partid];

	switch (r->rid) {
	case RDT_RESOURCE_L2:
	case RDT_RESOURCE_L3:
		cfg.cpbm = cfg_val;
		mpam_set_feature(mpam_feat_cpor_part, &cfg);
		break;
	case RDT_RESOURCE_MBA:
		if (mpam_has_feature(mpam_feat_mbw_max, cprops)) {
			cfg.mbw_max = percent_to_mbw_max(cfg_val, cprops);
			mpam_set_feature(mpam_feat_mbw_max, &cfg);
			break;
		}
		fallthrough;
	default:
		return -EINVAL;
	}

	/*
	 * When CDP is enabled, but the resource doesn't support it, we need to
	 * apply the same configuration to the other partid.
	 */
	if (mpam_resctrl_hide_cdp(r->rid)) {
		partid = resctrl_get_config_index(closid, CDP_CODE);
		err = mpam_apply_config(dom->ctrl_comp, partid, &cfg);
		if (err)
			return err;

		partid = resctrl_get_config_index(closid, CDP_DATA);
		return mpam_apply_config(dom->ctrl_comp, partid, &cfg);
	}

	return mpam_apply_config(dom->ctrl_comp, partid, &cfg);
}

int resctrl_arch_update_domains(struct rdt_resource *r, u32 closid)
{
	int err;
	struct rdt_ctrl_domain *d;

	lockdep_assert_cpus_held();
	lockdep_assert_irqs_enabled();

	if (!mpam_is_enabled())
		return -EINVAL;

	list_for_each_entry_rcu(d, &r->ctrl_domains, hdr.list) {
		for (enum resctrl_conf_type t = 0; t < CDP_NUM_TYPES; t++) {
			struct resctrl_staged_config *cfg = &d->staged_config[t];

			if (!cfg->have_new_ctrl)
				continue;

			err = resctrl_arch_update_one(r, d, closid, t,
						      cfg->new_ctrl);
			if (err)
				return err;
		}
	}

	return 0;
}

void resctrl_arch_reset_all_ctrls(struct rdt_resource *r)
{
	struct mpam_resctrl_res *res;

	lockdep_assert_cpus_held();

	if (!mpam_is_enabled())
		return;

	res = container_of(r, struct mpam_resctrl_res, resctrl_res);
	mpam_reset_class_locked(res->class);
}

static void mpam_resctrl_domain_hdr_init(int cpu, struct mpam_component *comp,
					 enum resctrl_res_level rid,
					 struct rdt_domain_hdr *hdr)
{
	lockdep_assert_cpus_held();

	INIT_LIST_HEAD(&hdr->list);
	hdr->id = mpam_resctrl_pick_domain_id(cpu, comp);
	hdr->rid = rid;
	cpumask_set_cpu(cpu, &hdr->cpu_mask);
}

static void mpam_resctrl_online_domain_hdr(unsigned int cpu,
					   struct rdt_domain_hdr *hdr)
{
	lockdep_assert_cpus_held();

	cpumask_set_cpu(cpu, &hdr->cpu_mask);
}

/**
 * mpam_resctrl_offline_domain_hdr() - Update the domain header to remove a CPU.
 * @cpu:	The CPU to remove from the domain.
 * @hdr:	The domain's header.
 *
 * Removes @cpu from the header mask. If this was the last CPU in the domain,
 * the domain header is removed from its parent list and true is returned,
 * indicating the parent structure can be freed.
 * If there are other CPUs in the domain, returns false.
 */
static bool mpam_resctrl_offline_domain_hdr(unsigned int cpu,
					    struct rdt_domain_hdr *hdr)
{
	lockdep_assert_held(&domain_list_lock);

	cpumask_clear_cpu(cpu, &hdr->cpu_mask);
	if (cpumask_empty(&hdr->cpu_mask)) {
		list_del_rcu(&hdr->list);
		synchronize_rcu();
		return true;
	}

	return false;
}

static void mpam_resctrl_domain_insert(struct list_head *list,
				       struct rdt_domain_hdr *new)
{
	struct rdt_domain_hdr *err;
	struct list_head *pos = NULL;

	lockdep_assert_held(&domain_list_lock);

	err = resctrl_find_domain(list, new->id, &pos);
	if (WARN_ON_ONCE(err))
		return;

	list_add_tail_rcu(&new->list, pos);
}

static struct mpam_component *find_component(struct mpam_class *class, int cpu)
{
	struct mpam_component *comp;

	guard(srcu)(&mpam_srcu);
	list_for_each_entry_srcu(comp, &class->components, class_list,
				 srcu_read_lock_held(&mpam_srcu)) {
		if (cpumask_test_cpu(cpu, &comp->affinity))
			return comp;
	}

	return NULL;
}

static struct mpam_resctrl_dom *
mpam_resctrl_alloc_domain(unsigned int cpu, struct mpam_resctrl_res *res)
{
	int err;
	struct mpam_resctrl_dom *dom;
	struct rdt_l3_mon_domain *mon_d;
	struct rdt_ctrl_domain *ctrl_d;
	struct mpam_class *class = res->class;
	struct mpam_component *comp_iter, *ctrl_comp;
	struct rdt_resource *r = &res->resctrl_res;

	lockdep_assert_held(&domain_list_lock);

	ctrl_comp = NULL;
	guard(srcu)(&mpam_srcu);
	list_for_each_entry_srcu(comp_iter, &class->components, class_list,
				 srcu_read_lock_held(&mpam_srcu)) {
		if (cpumask_test_cpu(cpu, &comp_iter->affinity)) {
			ctrl_comp = comp_iter;
			break;
		}
	}

	/* class has no component for this CPU */
	if (WARN_ON_ONCE(!ctrl_comp))
		return ERR_PTR(-EINVAL);

	dom = kzalloc_node(sizeof(*dom), GFP_KERNEL, cpu_to_node(cpu));
	if (!dom)
		return ERR_PTR(-ENOMEM);

	if (r->alloc_capable) {
		dom->ctrl_comp = ctrl_comp;

		ctrl_d = &dom->resctrl_ctrl_dom;
		mpam_resctrl_domain_hdr_init(cpu, ctrl_comp, r->rid, &ctrl_d->hdr);
		ctrl_d->hdr.type = RESCTRL_CTRL_DOMAIN;
		err = resctrl_online_ctrl_domain(r, ctrl_d);
		if (err)
			goto free_domain;

		mpam_resctrl_domain_insert(&r->ctrl_domains, &ctrl_d->hdr);
	} else {
		pr_debug("Skipped control domain online - no controls\n");
	}

	if (r->mon_capable) {
		struct mpam_component *any_mon_comp;
		struct mpam_resctrl_mon *mon;
		enum resctrl_event_id eventid;

		/*
		 * Even if the monitor domain is backed by a different
		 * component, the L3 component IDs need to be used... only
		 * there may be no ctrl_comp for the L3.
		 * Search each event's class list for a component with
		 * overlapping CPUs and set up the dom->mon_comp array.
		 */

		for_each_mpam_resctrl_mon(mon, eventid) {
			struct mpam_component *mon_comp;

			if (!mon->class)
				continue;       // dummy resource

			mon_comp = find_component(mon->class, cpu);
			dom->mon_comp[eventid] = mon_comp;
			if (mon_comp)
				any_mon_comp = mon_comp;
		}
		if (!any_mon_comp) {
			WARN_ON_ONCE(0);
			err = -EFAULT;
			goto offline_ctrl_domain;
		}

		mon_d = &dom->resctrl_mon_dom;
		mpam_resctrl_domain_hdr_init(cpu, any_mon_comp, r->rid, &mon_d->hdr);
		mon_d->hdr.type = RESCTRL_MON_DOMAIN;
		err = resctrl_online_mon_domain(r, &mon_d->hdr);
		if (err)
			goto offline_ctrl_domain;

		mpam_resctrl_domain_insert(&r->mon_domains, &mon_d->hdr);
	} else {
		pr_debug("Skipped monitor domain online - no monitors\n");
	}

	return dom;

offline_ctrl_domain:
	if (r->alloc_capable) {
		mpam_resctrl_offline_domain_hdr(cpu, &ctrl_d->hdr);
		resctrl_offline_ctrl_domain(r, ctrl_d);
	}
free_domain:
	kfree(dom);
	dom = ERR_PTR(err);

	return dom;
}

/*
 * We know all the monitors are associated with the L3, even if there are no
 * controls and therefore no control component. Find the cache-id for the CPU
 * and use that to search for existing resctrl domains.
 * This relies on mpam_resctrl_pick_domain_id() using the L3 cache-id
 * for anything that is not a cache.
 */
static struct mpam_resctrl_dom *mpam_resctrl_get_mon_domain_from_cpu(int cpu)
{
	int cache_id;
	struct mpam_resctrl_dom *dom;
	struct mpam_resctrl_res *l3 = &mpam_resctrl_controls[RDT_RESOURCE_L3];

	lockdep_assert_cpus_held();

	if (!l3->class)
		return NULL;
	cache_id = get_cpu_cacheinfo_id(cpu, 3);
	if (cache_id < 0)
		return NULL;

	list_for_each_entry_rcu(dom, &l3->resctrl_res.mon_domains, resctrl_mon_dom.hdr.list) {
		if (dom->resctrl_mon_dom.hdr.id == cache_id)
			return dom;
	}

	return NULL;
}

static struct mpam_resctrl_dom *
mpam_resctrl_get_domain_from_cpu(int cpu, struct mpam_resctrl_res *res)
{
	struct mpam_resctrl_dom *dom;
	struct rdt_resource *r = &res->resctrl_res;

	lockdep_assert_cpus_held();

	list_for_each_entry_rcu(dom, &r->ctrl_domains, resctrl_ctrl_dom.hdr.list) {
		if (cpumask_test_cpu(cpu, &dom->ctrl_comp->affinity))
			return dom;
	}

	if (r->rid != RDT_RESOURCE_L3)
		return NULL;

	/* Search the mon domain list too - needed on monitor only platforms. */
	return mpam_resctrl_get_mon_domain_from_cpu(cpu);
}

int mpam_resctrl_online_cpu(unsigned int cpu)
{
	struct mpam_resctrl_res *res;
	enum resctrl_res_level rid;

	guard(mutex)(&domain_list_lock);
	for_each_mpam_resctrl_control(res, rid) {
		struct mpam_resctrl_dom *dom;
		struct rdt_resource *r = &res->resctrl_res;

		if (!res->class)
			continue;	// dummy_resource;

		dom = mpam_resctrl_get_domain_from_cpu(cpu, res);
		if (!dom) {
			dom = mpam_resctrl_alloc_domain(cpu, res);
			if (IS_ERR(dom))
				return PTR_ERR(dom);
		} else {
			if (r->alloc_capable) {
				struct rdt_ctrl_domain *ctrl_d = &dom->resctrl_ctrl_dom;

				mpam_resctrl_online_domain_hdr(cpu, &ctrl_d->hdr);
			}
			if (r->mon_capable) {
				struct rdt_l3_mon_domain *mon_d = &dom->resctrl_mon_dom;

				mpam_resctrl_online_domain_hdr(cpu, &mon_d->hdr);
			}
		}
	}

	resctrl_online_cpu(cpu);

	return 0;
}

void mpam_resctrl_offline_cpu(unsigned int cpu)
{
	struct mpam_resctrl_res *res;
	enum resctrl_res_level rid;

	resctrl_offline_cpu(cpu);

	guard(mutex)(&domain_list_lock);
	for_each_mpam_resctrl_control(res, rid) {
		struct mpam_resctrl_dom *dom;
		struct rdt_l3_mon_domain *mon_d;
		struct rdt_ctrl_domain *ctrl_d;
		bool ctrl_dom_empty, mon_dom_empty;
		struct rdt_resource *r = &res->resctrl_res;

		if (!res->class)
			continue;	// dummy resource

		dom = mpam_resctrl_get_domain_from_cpu(cpu, res);
		if (WARN_ON_ONCE(!dom))
			continue;

		if (r->alloc_capable) {
			ctrl_d = &dom->resctrl_ctrl_dom;
			ctrl_dom_empty = mpam_resctrl_offline_domain_hdr(cpu, &ctrl_d->hdr);
			if (ctrl_dom_empty)
				resctrl_offline_ctrl_domain(&res->resctrl_res, ctrl_d);
		} else {
			ctrl_dom_empty = true;
		}

		if (r->mon_capable) {
			mon_d = &dom->resctrl_mon_dom;
			mon_dom_empty = mpam_resctrl_offline_domain_hdr(cpu, &mon_d->hdr);
			if (mon_dom_empty)
				resctrl_offline_mon_domain(&res->resctrl_res, &mon_d->hdr);
		} else {
			mon_dom_empty = true;
		}

		if (ctrl_dom_empty && mon_dom_empty)
			kfree(dom);
	}
}

int mpam_resctrl_setup(void)
{
	int err = 0;
	struct mpam_resctrl_res *res;
	enum resctrl_res_level rid;
	struct mpam_resctrl_mon *mon;
	enum resctrl_event_id eventid;

	wait_event(wait_cacheinfo_ready, cacheinfo_ready);

	cpus_read_lock();
	for_each_mpam_resctrl_control(res, rid) {
		INIT_LIST_HEAD_RCU(&res->resctrl_res.ctrl_domains);
		INIT_LIST_HEAD_RCU(&res->resctrl_res.mon_domains);
		res->resctrl_res.rid = rid;
	}

	/* Find some classes to use for controls */
	mpam_resctrl_pick_caches();
	mpam_resctrl_pick_mba();

	/* Initialise the resctrl structures from the classes */
	for_each_mpam_resctrl_control(res, rid) {
		if (!res->class)
			continue;	// dummy resource

		err = mpam_resctrl_control_init(res);
		if (err) {
			pr_debug("Failed to initialise rid %u\n", rid);
			goto internal_error;
		}
	}

	/* Find some classes to use for monitors */
	mpam_resctrl_pick_counters();

	for_each_mpam_resctrl_mon(mon, eventid) {
		if (!mon->class)
			continue;	// dummy resource

		err = mpam_resctrl_monitor_init(mon, eventid);
		if (err) {
			pr_debug("Failed to initialise event %u\n", eventid);
			goto internal_error;
		}
	}

	cpus_read_unlock();

	if (!resctrl_arch_alloc_capable() && !resctrl_arch_mon_capable()) {
		pr_debug("No alloc(%u) or monitor(%u) found - resctrl not supported\n",
			 resctrl_arch_alloc_capable(), resctrl_arch_mon_capable());
		return -EOPNOTSUPP;
	}

	err = resctrl_init();
	if (err)
		return err;

	WRITE_ONCE(resctrl_enabled, true);

	return 0;

internal_error:
	cpus_read_unlock();
	pr_debug("Internal error %d - resctrl not supported\n", err);
	return err;
}

void mpam_resctrl_exit(void)
{
	if (!READ_ONCE(resctrl_enabled))
		return;

	WRITE_ONCE(resctrl_enabled, false);
	resctrl_exit();
}

/*
 * The driver is detaching an MSC from this class, if resctrl was using it,
 * pull on resctrl_exit().
 */
void mpam_resctrl_teardown_class(struct mpam_class *class)
{
	struct mpam_resctrl_res *res;
	enum resctrl_res_level rid;
	struct mpam_resctrl_mon *mon;
	enum resctrl_event_id eventid;

	might_sleep();

	for_each_mpam_resctrl_control(res, rid) {
		if (res->class == class) {
			res->class = NULL;
			break;
		}
	}
	for_each_mpam_resctrl_mon(mon, eventid) {
		if (mon->class == class) {
			mon->class = NULL;
			break;
		}
	}
}

static int __init __cacheinfo_ready(void)
{
	cacheinfo_ready = true;
	wake_up(&wait_cacheinfo_ready);

	return 0;
}
device_initcall_sync(__cacheinfo_ready);

#ifdef CONFIG_MPAM_KUNIT_TEST
#include "test_mpam_resctrl.c"
#endif

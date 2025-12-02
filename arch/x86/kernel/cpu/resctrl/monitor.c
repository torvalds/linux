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

#define pr_fmt(fmt)	"resctrl: " fmt

#include <linux/cpu.h>
#include <linux/resctrl.h>

#include <asm/cpu_device_id.h>
#include <asm/msr.h>

#include "internal.h"

/*
 * Global boolean for rdt_monitor which is true if any
 * resource monitoring is enabled.
 */
bool rdt_mon_capable;

#define CF(cf)	((unsigned long)(1048576 * (cf) + 0.5))

static int snc_nodes_per_l3_cache = 1;

/*
 * The correction factor table is documented in Documentation/filesystems/resctrl.rst.
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
 * When Sub-NUMA Cluster (SNC) mode is not enabled (as indicated by
 * "snc_nodes_per_l3_cache == 1") no translation of the RMID value is
 * needed. The physical RMID is the same as the logical RMID.
 *
 * On a platform with SNC mode enabled, Linux enables RMID sharing mode
 * via MSR 0xCA0 (see the "RMID Sharing Mode" section in the "Intel
 * Resource Director Technology Architecture Specification" for a full
 * description of RMID sharing mode).
 *
 * In RMID sharing mode there are fewer "logical RMID" values available
 * to accumulate data ("physical RMIDs" are divided evenly between SNC
 * nodes that share an L3 cache). Linux creates an rdt_mon_domain for
 * each SNC node.
 *
 * The value loaded into IA32_PQR_ASSOC is the "logical RMID".
 *
 * Data is collected independently on each SNC node and can be retrieved
 * using the "physical RMID" value computed by this function and loaded
 * into IA32_QM_EVTSEL. @cpu can be any CPU in the SNC node.
 *
 * The scope of the IA32_QM_EVTSEL and IA32_QM_CTR MSRs is at the L3
 * cache.  So a "physical RMID" may be read from any CPU that shares
 * the L3 cache with the desired SNC node, not just from a CPU in
 * the specific SNC node.
 */
static int logical_rmid_to_physical_rmid(int cpu, int lrmid)
{
	struct rdt_resource *r = &rdt_resources_all[RDT_RESOURCE_L3].r_resctrl;

	if (snc_nodes_per_l3_cache == 1)
		return lrmid;

	return lrmid + (cpu_to_node(cpu) % snc_nodes_per_l3_cache) * r->mon.num_rmid;
}

static int __rmid_read_phys(u32 prmid, enum resctrl_event_id eventid, u64 *val)
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
	wrmsr(MSR_IA32_QM_EVTSEL, eventid, prmid);
	rdmsrq(MSR_IA32_QM_CTR, msr_val);

	if (msr_val & RMID_VAL_ERROR)
		return -EIO;
	if (msr_val & RMID_VAL_UNAVAIL)
		return -EINVAL;

	*val = msr_val;
	return 0;
}

static struct arch_mbm_state *get_arch_mbm_state(struct rdt_hw_mon_domain *hw_dom,
						 u32 rmid,
						 enum resctrl_event_id eventid)
{
	struct arch_mbm_state *state;

	if (!resctrl_is_mbm_event(eventid))
		return NULL;

	state = hw_dom->arch_mbm_states[MBM_STATE_IDX(eventid)];

	return state ? &state[rmid] : NULL;
}

void resctrl_arch_reset_rmid(struct rdt_resource *r, struct rdt_mon_domain *d,
			     u32 unused, u32 rmid,
			     enum resctrl_event_id eventid)
{
	struct rdt_hw_mon_domain *hw_dom = resctrl_to_arch_mon_dom(d);
	int cpu = cpumask_any(&d->hdr.cpu_mask);
	struct arch_mbm_state *am;
	u32 prmid;

	am = get_arch_mbm_state(hw_dom, rmid, eventid);
	if (am) {
		memset(am, 0, sizeof(*am));

		prmid = logical_rmid_to_physical_rmid(cpu, rmid);
		/* Record any initial, non-zero count value. */
		__rmid_read_phys(prmid, eventid, &am->prev_msr);
	}
}

/*
 * Assumes that hardware counters are also reset and thus that there is
 * no need to record initial non-zero counts.
 */
void resctrl_arch_reset_rmid_all(struct rdt_resource *r, struct rdt_mon_domain *d)
{
	struct rdt_hw_mon_domain *hw_dom = resctrl_to_arch_mon_dom(d);
	enum resctrl_event_id eventid;
	int idx;

	for_each_mbm_event_id(eventid) {
		if (!resctrl_is_mon_event_enabled(eventid))
			continue;
		idx = MBM_STATE_IDX(eventid);
		memset(hw_dom->arch_mbm_states[idx], 0,
		       sizeof(*hw_dom->arch_mbm_states[0]) * r->mon.num_rmid);
	}
}

static u64 mbm_overflow_count(u64 prev_msr, u64 cur_msr, unsigned int width)
{
	u64 shift = 64 - width, chunks;

	chunks = (cur_msr << shift) - (prev_msr << shift);
	return chunks >> shift;
}

static u64 get_corrected_val(struct rdt_resource *r, struct rdt_mon_domain *d,
			     u32 rmid, enum resctrl_event_id eventid, u64 msr_val)
{
	struct rdt_hw_mon_domain *hw_dom = resctrl_to_arch_mon_dom(d);
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);
	struct arch_mbm_state *am;
	u64 chunks;

	am = get_arch_mbm_state(hw_dom, rmid, eventid);
	if (am) {
		am->chunks += mbm_overflow_count(am->prev_msr, msr_val,
						 hw_res->mbm_width);
		chunks = get_corrected_mbm_count(rmid, am->chunks);
		am->prev_msr = msr_val;
	} else {
		chunks = msr_val;
	}

	return chunks * hw_res->mon_scale;
}

int resctrl_arch_rmid_read(struct rdt_resource *r, struct rdt_mon_domain *d,
			   u32 unused, u32 rmid, enum resctrl_event_id eventid,
			   u64 *val, void *ignored)
{
	struct rdt_hw_mon_domain *hw_dom = resctrl_to_arch_mon_dom(d);
	int cpu = cpumask_any(&d->hdr.cpu_mask);
	struct arch_mbm_state *am;
	u64 msr_val;
	u32 prmid;
	int ret;

	resctrl_arch_rmid_read_context_check();

	prmid = logical_rmid_to_physical_rmid(cpu, rmid);
	ret = __rmid_read_phys(prmid, eventid, &msr_val);

	if (!ret) {
		*val = get_corrected_val(r, d, rmid, eventid, msr_val);
	} else if (ret == -EINVAL) {
		am = get_arch_mbm_state(hw_dom, rmid, eventid);
		if (am)
			am->prev_msr = 0;
	}

	return ret;
}

static int __cntr_id_read(u32 cntr_id, u64 *val)
{
	u64 msr_val;

	/*
	 * QM_EVTSEL Register definition:
	 * =======================================================
	 * Bits    Mnemonic        Description
	 * =======================================================
	 * 63:44   --              Reserved
	 * 43:32   RMID            RMID or counter ID in ABMC mode
	 *                         when reading an MBM event
	 * 31      ExtendedEvtID   Extended Event Identifier
	 * 30:8    --              Reserved
	 * 7:0     EvtID           Event Identifier
	 * =======================================================
	 * The contents of a specific counter can be read by setting the
	 * following fields in QM_EVTSEL.ExtendedEvtID(=1) and
	 * QM_EVTSEL.EvtID = L3CacheABMC (=1) and setting QM_EVTSEL.RMID
	 * to the desired counter ID. Reading the QM_CTR then returns the
	 * contents of the specified counter. The RMID_VAL_ERROR bit is set
	 * if the counter configuration is invalid, or if an invalid counter
	 * ID is set in the QM_EVTSEL.RMID field.  The RMID_VAL_UNAVAIL bit
	 * is set if the counter data is unavailable.
	 */
	wrmsr(MSR_IA32_QM_EVTSEL, ABMC_EXTENDED_EVT_ID | ABMC_EVT_ID, cntr_id);
	rdmsrl(MSR_IA32_QM_CTR, msr_val);

	if (msr_val & RMID_VAL_ERROR)
		return -EIO;
	if (msr_val & RMID_VAL_UNAVAIL)
		return -EINVAL;

	*val = msr_val;
	return 0;
}

void resctrl_arch_reset_cntr(struct rdt_resource *r, struct rdt_mon_domain *d,
			     u32 unused, u32 rmid, int cntr_id,
			     enum resctrl_event_id eventid)
{
	struct rdt_hw_mon_domain *hw_dom = resctrl_to_arch_mon_dom(d);
	struct arch_mbm_state *am;

	am = get_arch_mbm_state(hw_dom, rmid, eventid);
	if (am) {
		memset(am, 0, sizeof(*am));

		/* Record any initial, non-zero count value. */
		__cntr_id_read(cntr_id, &am->prev_msr);
	}
}

int resctrl_arch_cntr_read(struct rdt_resource *r, struct rdt_mon_domain *d,
			   u32 unused, u32 rmid, int cntr_id,
			   enum resctrl_event_id eventid, u64 *val)
{
	u64 msr_val;
	int ret;

	ret = __cntr_id_read(cntr_id, &msr_val);
	if (ret)
		return ret;

	*val = get_corrected_val(r, d, rmid, eventid, msr_val);

	return 0;
}

/*
 * The power-on reset value of MSR_RMID_SNC_CONFIG is 0x1
 * which indicates that RMIDs are configured in legacy mode.
 * This mode is incompatible with Linux resctrl semantics
 * as RMIDs are partitioned between SNC nodes, which requires
 * a user to know which RMID is allocated to a task.
 * Clearing bit 0 reconfigures the RMID counters for use
 * in RMID sharing mode. This mode is better for Linux.
 * The RMID space is divided between all SNC nodes with the
 * RMIDs renumbered to start from zero in each node when
 * counting operations from tasks. Code to read the counters
 * must adjust RMID counter numbers based on SNC node. See
 * logical_rmid_to_physical_rmid() for code that does this.
 */
void arch_mon_domain_online(struct rdt_resource *r, struct rdt_mon_domain *d)
{
	if (snc_nodes_per_l3_cache > 1)
		msr_clear_bit(MSR_RMID_SNC_CONFIG, 0);
}

/* CPU models that support MSR_RMID_SNC_CONFIG */
static const struct x86_cpu_id snc_cpu_ids[] __initconst = {
	X86_MATCH_VFM(INTEL_ICELAKE_X, 0),
	X86_MATCH_VFM(INTEL_SAPPHIRERAPIDS_X, 0),
	X86_MATCH_VFM(INTEL_EMERALDRAPIDS_X, 0),
	X86_MATCH_VFM(INTEL_GRANITERAPIDS_X, 0),
	X86_MATCH_VFM(INTEL_ATOM_CRESTMONT_X, 0),
	{}
};

/*
 * There isn't a simple hardware bit that indicates whether a CPU is running
 * in Sub-NUMA Cluster (SNC) mode. Infer the state by comparing the
 * number of CPUs sharing the L3 cache with CPU0 to the number of CPUs in
 * the same NUMA node as CPU0.
 * It is not possible to accurately determine SNC state if the system is
 * booted with a maxcpus=N parameter. That distorts the ratio of SNC nodes
 * to L3 caches. It will be OK if system is booted with hyperthreading
 * disabled (since this doesn't affect the ratio).
 */
static __init int snc_get_config(void)
{
	struct cacheinfo *ci = get_cpu_cacheinfo_level(0, RESCTRL_L3_CACHE);
	const cpumask_t *node0_cpumask;
	int cpus_per_node, cpus_per_l3;
	int ret;

	if (!x86_match_cpu(snc_cpu_ids) || !ci)
		return 1;

	cpus_read_lock();
	if (num_online_cpus() != num_present_cpus())
		pr_warn("Some CPUs offline, SNC detection may be incorrect\n");
	cpus_read_unlock();

	node0_cpumask = cpumask_of_node(cpu_to_node(0));

	cpus_per_node = cpumask_weight(node0_cpumask);
	cpus_per_l3 = cpumask_weight(&ci->shared_cpu_map);

	if (!cpus_per_node || !cpus_per_l3)
		return 1;

	ret = cpus_per_l3 / cpus_per_node;

	/* sanity check: Only valid results are 1, 2, 3, 4, 6 */
	switch (ret) {
	case 1:
		break;
	case 2 ... 4:
	case 6:
		pr_info("Sub-NUMA Cluster mode detected with %d nodes per L3 cache\n", ret);
		rdt_resources_all[RDT_RESOURCE_L3].r_resctrl.mon_scope = RESCTRL_L3_NODE;
		break;
	default:
		pr_warn("Ignore improbable SNC node count %d\n", ret);
		ret = 1;
		break;
	}

	return ret;
}

int __init rdt_get_mon_l3_config(struct rdt_resource *r)
{
	unsigned int mbm_offset = boot_cpu_data.x86_cache_mbm_width_offset;
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);
	unsigned int threshold;
	u32 eax, ebx, ecx, edx;

	snc_nodes_per_l3_cache = snc_get_config();

	resctrl_rmid_realloc_limit = boot_cpu_data.x86_cache_size * 1024;
	hw_res->mon_scale = boot_cpu_data.x86_cache_occ_scale / snc_nodes_per_l3_cache;
	r->mon.num_rmid = (boot_cpu_data.x86_cache_max_rmid + 1) / snc_nodes_per_l3_cache;
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
	threshold = resctrl_rmid_realloc_limit / r->mon.num_rmid;

	/*
	 * Because num_rmid may not be a power of two, round the value
	 * to the nearest multiple of hw_res->mon_scale so it matches a
	 * value the hardware will measure. mon_scale may not be a power of 2.
	 */
	resctrl_rmid_realloc_threshold = resctrl_arch_round_mon_val(threshold);

	if (rdt_cpu_has(X86_FEATURE_BMEC) || rdt_cpu_has(X86_FEATURE_ABMC)) {
		/* Detect list of bandwidth sources that can be tracked */
		cpuid_count(0x80000020, 3, &eax, &ebx, &ecx, &edx);
		r->mon.mbm_cfg_mask = ecx & MAX_EVT_CONFIG_BITS;
	}

	/*
	 * resctrl assumes a system that supports assignable counters can
	 * switch to "default" mode. Ensure that there is a "default" mode
	 * to switch to. This enforces a dependency between the independent
	 * X86_FEATURE_ABMC and X86_FEATURE_CQM_MBM_TOTAL/X86_FEATURE_CQM_MBM_LOCAL
	 * hardware features.
	 */
	if (rdt_cpu_has(X86_FEATURE_ABMC) &&
	    (rdt_cpu_has(X86_FEATURE_CQM_MBM_TOTAL) ||
	     rdt_cpu_has(X86_FEATURE_CQM_MBM_LOCAL))) {
		r->mon.mbm_cntr_assignable = true;
		cpuid_count(0x80000020, 5, &eax, &ebx, &ecx, &edx);
		r->mon.num_mbm_cntrs = (ebx & GENMASK(15, 0)) + 1;
		hw_res->mbm_cntr_assign_enabled = true;
	}

	r->mon_capable = true;

	return 0;
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

static void resctrl_abmc_set_one_amd(void *arg)
{
	bool *enable = arg;

	if (*enable)
		msr_set_bit(MSR_IA32_L3_QOS_EXT_CFG, ABMC_ENABLE_BIT);
	else
		msr_clear_bit(MSR_IA32_L3_QOS_EXT_CFG, ABMC_ENABLE_BIT);
}

/*
 * ABMC enable/disable requires update of L3_QOS_EXT_CFG MSR on all the CPUs
 * associated with all monitor domains.
 */
static void _resctrl_abmc_enable(struct rdt_resource *r, bool enable)
{
	struct rdt_mon_domain *d;

	lockdep_assert_cpus_held();

	list_for_each_entry(d, &r->mon_domains, hdr.list) {
		on_each_cpu_mask(&d->hdr.cpu_mask, resctrl_abmc_set_one_amd,
				 &enable, 1);
		resctrl_arch_reset_rmid_all(r, d);
	}
}

int resctrl_arch_mbm_cntr_assign_set(struct rdt_resource *r, bool enable)
{
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);

	if (r->mon.mbm_cntr_assignable &&
	    hw_res->mbm_cntr_assign_enabled != enable) {
		_resctrl_abmc_enable(r, enable);
		hw_res->mbm_cntr_assign_enabled = enable;
	}

	return 0;
}

bool resctrl_arch_mbm_cntr_assign_enabled(struct rdt_resource *r)
{
	return resctrl_to_arch_res(r)->mbm_cntr_assign_enabled;
}

static void resctrl_abmc_config_one_amd(void *info)
{
	union l3_qos_abmc_cfg *abmc_cfg = info;

	wrmsrl(MSR_IA32_L3_QOS_ABMC_CFG, abmc_cfg->full);
}

/*
 * Send an IPI to the domain to assign the counter to RMID, event pair.
 */
void resctrl_arch_config_cntr(struct rdt_resource *r, struct rdt_mon_domain *d,
			      enum resctrl_event_id evtid, u32 rmid, u32 closid,
			      u32 cntr_id, bool assign)
{
	struct rdt_hw_mon_domain *hw_dom = resctrl_to_arch_mon_dom(d);
	union l3_qos_abmc_cfg abmc_cfg = { 0 };
	struct arch_mbm_state *am;

	abmc_cfg.split.cfg_en = 1;
	abmc_cfg.split.cntr_en = assign ? 1 : 0;
	abmc_cfg.split.cntr_id = cntr_id;
	abmc_cfg.split.bw_src = rmid;
	if (assign)
		abmc_cfg.split.bw_type = resctrl_get_mon_evt_cfg(evtid);

	smp_call_function_any(&d->hdr.cpu_mask, resctrl_abmc_config_one_amd, &abmc_cfg, 1);

	/*
	 * The hardware counter is reset (because cfg_en == 1) so there is no
	 * need to record initial non-zero counts.
	 */
	am = get_arch_mbm_state(hw_dom, rmid, evtid);
	if (am)
		memset(am, 0, sizeof(*am));
}

void resctrl_arch_mbm_cntr_assign_set_one(struct rdt_resource *r)
{
	struct rdt_hw_resource *hw_res = resctrl_to_arch_res(r);

	resctrl_abmc_set_one_amd(&hw_res->mbm_cntr_assign_enabled);
}

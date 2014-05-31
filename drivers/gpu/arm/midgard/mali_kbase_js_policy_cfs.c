/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





/*
 * Job Scheduler: Completely Fair Policy Implementation
 */

#include <mali_kbase.h>
#include <mali_kbase_jm.h>
#include <mali_kbase_js.h>
#include <mali_kbase_js_policy_cfs.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
#include <linux/sched/rt.h>
#endif

/**
 * Define for when dumping is enabled.
 * This should not be based on the instrumentation level as whether dumping is enabled for a particular level is down to the integrator.
 * However this is being used for now as otherwise the cinstr headers would be needed.
 */
#define CINSTR_DUMPING_ENABLED (2 == MALI_INSTRUMENTATION_LEVEL)

/** Fixed point constants used for runtime weight calculations */
#define WEIGHT_FIXEDPOINT_SHIFT 10
#define WEIGHT_TABLE_SIZE       40
#define WEIGHT_0_NICE           (WEIGHT_TABLE_SIZE/2)
#define WEIGHT_0_VAL            (1 << WEIGHT_FIXEDPOINT_SHIFT)

#define LOOKUP_VARIANT_MASK ((1u<<KBASEP_JS_MAX_NR_CORE_REQ_VARIANTS) - 1u)

#define PROCESS_PRIORITY_MIN (-20)
#define PROCESS_PRIORITY_MAX  (19)

/** Core requirements that all the variants support */
#define JS_CORE_REQ_ALL_OTHERS \
	(BASE_JD_REQ_CF | BASE_JD_REQ_V | BASE_JD_REQ_PERMON | BASE_JD_REQ_EXTERNAL_RESOURCES | BASEP_JD_REQ_EVENT_NEVER)

/** Context requirements the all the variants support */

/* In HW issue 8987 workaround, restrict Compute-only contexts and Compute jobs onto job slot[2],
 * which will ensure their affinity does not intersect GLES jobs */
#define JS_CTX_REQ_ALL_OTHERS_8987 \
	(KBASE_CTX_FLAG_PRIVILEGED)
#define JS_CORE_REQ_COMPUTE_SLOT_8987 \
	(BASE_JD_REQ_CS)
#define JS_CORE_REQ_ONLY_COMPUTE_SLOT_8987 \
	(BASE_JD_REQ_ONLY_COMPUTE)

/* Otherwise, compute-only contexts/compute jobs can use any job slot */
#define JS_CTX_REQ_ALL_OTHERS \
	(KBASE_CTX_FLAG_PRIVILEGED | KBASE_CTX_FLAG_HINT_ONLY_COMPUTE)
#define JS_CORE_REQ_COMPUTE_SLOT \
	(BASE_JD_REQ_CS | BASE_JD_REQ_ONLY_COMPUTE)

/* core_req variants are ordered by least restrictive first, so that our
 * algorithm in cached_variant_idx_init picks the least restrictive variant for
 * each job . Note that coherent_group requirement is added to all CS variants as the
 * selection of job-slot does not depend on the coherency requirement. */
static const kbasep_atom_req core_req_variants[] = {
	{
	 /* 0: Fragment variant */
	 (JS_CORE_REQ_ALL_OTHERS | BASE_JD_REQ_FS | BASE_JD_REQ_FS_AFBC |
						BASE_JD_REQ_COHERENT_GROUP),
	 (JS_CTX_REQ_ALL_OTHERS),
	 0},
	{
	 /* 1: Compute variant, can use all coregroups */
	 (JS_CORE_REQ_ALL_OTHERS | JS_CORE_REQ_COMPUTE_SLOT),
	 (JS_CTX_REQ_ALL_OTHERS),
	 0},
	{
	 /* 2: Compute variant, uses only coherent coregroups */
	 (JS_CORE_REQ_ALL_OTHERS | JS_CORE_REQ_COMPUTE_SLOT | BASE_JD_REQ_COHERENT_GROUP),
	 (JS_CTX_REQ_ALL_OTHERS),
	 0},
	{
	 /* 3: Compute variant, might only use coherent coregroup, and must use tiling */
	 (JS_CORE_REQ_ALL_OTHERS | JS_CORE_REQ_COMPUTE_SLOT | BASE_JD_REQ_COHERENT_GROUP | BASE_JD_REQ_T),
	 (JS_CTX_REQ_ALL_OTHERS),
	 0},

	{
	 /* 4: Unused */
	 0,
	 0,
	 0},

	{
	 /* 5: Compute variant for specific-coherent-group targetting CoreGroup 0 */
	 (JS_CORE_REQ_ALL_OTHERS | JS_CORE_REQ_COMPUTE_SLOT | BASE_JD_REQ_COHERENT_GROUP | BASE_JD_REQ_SPECIFIC_COHERENT_GROUP),
	 (JS_CTX_REQ_ALL_OTHERS),
	 0			/* device_nr */
	 },
	{
	 /* 6: Compute variant for specific-coherent-group targetting CoreGroup 1 */
	 (JS_CORE_REQ_ALL_OTHERS | JS_CORE_REQ_COMPUTE_SLOT | BASE_JD_REQ_COHERENT_GROUP | BASE_JD_REQ_SPECIFIC_COHERENT_GROUP),
	 (JS_CTX_REQ_ALL_OTHERS),
	 1			/* device_nr */
	 },

	/* Unused core_req variants, to bring the total up to a power of 2 */
	{
	 /* 7 */
	 0,
	 0,
	 0},
};

static const kbasep_atom_req core_req_variants_8987[] = {
	{
	 /* 0: Fragment variant */
	 (JS_CORE_REQ_ALL_OTHERS | BASE_JD_REQ_FS | BASE_JD_REQ_COHERENT_GROUP),
	 (JS_CTX_REQ_ALL_OTHERS_8987),
	 0},
	{
	 /* 1: Compute variant, can use all coregroups */
	 (JS_CORE_REQ_ALL_OTHERS | JS_CORE_REQ_COMPUTE_SLOT_8987),
	 (JS_CTX_REQ_ALL_OTHERS_8987),
	 0},
	{
	 /* 2: Compute variant, uses only coherent coregroups */
	 (JS_CORE_REQ_ALL_OTHERS | JS_CORE_REQ_COMPUTE_SLOT_8987 | BASE_JD_REQ_COHERENT_GROUP),
	 (JS_CTX_REQ_ALL_OTHERS_8987),
	 0},
	{
	 /* 3: Compute variant, might only use coherent coregroup, and must use tiling */
	 (JS_CORE_REQ_ALL_OTHERS | JS_CORE_REQ_COMPUTE_SLOT_8987 | BASE_JD_REQ_COHERENT_GROUP | BASE_JD_REQ_T),
	 (JS_CTX_REQ_ALL_OTHERS_8987),
	 0},

	{
	 /* 4: Variant guarenteed to support Compute contexts/atoms
	  *
	  * In the case of a context that's specified as 'Only Compute', it'll
	  * not allow Tiler or Fragment atoms, and so those get rejected */
	 (JS_CORE_REQ_ALL_OTHERS | JS_CORE_REQ_ONLY_COMPUTE_SLOT_8987 | BASE_JD_REQ_COHERENT_GROUP),
	 (JS_CTX_REQ_ALL_OTHERS_8987 | KBASE_CTX_FLAG_HINT_ONLY_COMPUTE),
	 0},

	{
	 /* 5: Compute variant for specific-coherent-group targetting CoreGroup 0
	  * Specifically, this only allows 'Only Compute' contexts/atoms */
	 (JS_CORE_REQ_ALL_OTHERS | JS_CORE_REQ_ONLY_COMPUTE_SLOT_8987 | BASE_JD_REQ_COHERENT_GROUP | BASE_JD_REQ_SPECIFIC_COHERENT_GROUP),
	 (JS_CTX_REQ_ALL_OTHERS_8987 | KBASE_CTX_FLAG_HINT_ONLY_COMPUTE),
	 0			/* device_nr */
	 },
	{
	 /* 6: Compute variant for specific-coherent-group targetting CoreGroup 1
	  * Specifically, this only allows 'Only Compute' contexts/atoms */
	 (JS_CORE_REQ_ALL_OTHERS | JS_CORE_REQ_ONLY_COMPUTE_SLOT_8987 | BASE_JD_REQ_COHERENT_GROUP | BASE_JD_REQ_SPECIFIC_COHERENT_GROUP),
	 (JS_CTX_REQ_ALL_OTHERS_8987 | KBASE_CTX_FLAG_HINT_ONLY_COMPUTE),
	 1			/* device_nr */
	 },
	/* Unused core_req variants, to bring the total up to a power of 2 */
	{
	 /* 7 */
	 0,
	 0,
	 0},
};

#define CORE_REQ_VARIANT_FRAGMENT                    0
#define CORE_REQ_VARIANT_COMPUTE_ALL_CORES           1
#define CORE_REQ_VARIANT_COMPUTE_ONLY_COHERENT_GROUP 2
#define CORE_REQ_VARIANT_COMPUTE_OR_TILING           3
#define CORE_REQ_VARIANT_COMPUTE_SPECIFIC_COHERENT_0 5
#define CORE_REQ_VARIANT_COMPUTE_SPECIFIC_COHERENT_1 6

#define CORE_REQ_VARIANT_ONLY_COMPUTE_8987                     4
#define CORE_REQ_VARIANT_ONLY_COMPUTE_8987_SPECIFIC_COHERENT_0 5
#define CORE_REQ_VARIANT_ONLY_COMPUTE_8987_SPECIFIC_COHERENT_1 6

#define NUM_CORE_REQ_VARIANTS NELEMS(core_req_variants)
#define NUM_CORE_REQ_VARIANTS_8987 NELEMS(core_req_variants_8987)

/** Mappings between job slot and variant lists for Soft-Stoppable State */
static const u32 variants_supported_ss_state[] = {
	/* js[0] uses Fragment only */
	(1u << CORE_REQ_VARIANT_FRAGMENT),

	/* js[1] uses: Compute-all-cores, Compute-only-coherent, Compute-or-Tiling,
	 * compute-specific-coregroup-0 */
	(1u << CORE_REQ_VARIANT_COMPUTE_ALL_CORES)
	    | (1u << CORE_REQ_VARIANT_COMPUTE_ONLY_COHERENT_GROUP)
	    | (1u << CORE_REQ_VARIANT_COMPUTE_OR_TILING)
	    | (1u << CORE_REQ_VARIANT_COMPUTE_SPECIFIC_COHERENT_0),

	/* js[2] uses: Compute-only-coherent, compute-specific-coregroup-1 */
	(1u << CORE_REQ_VARIANT_COMPUTE_ONLY_COHERENT_GROUP)
	    | (1u << CORE_REQ_VARIANT_COMPUTE_SPECIFIC_COHERENT_1)
};

/** Mappings between job slot and variant lists for Soft-Stoppable State, when
 * we have atoms that can use all the cores (KBASEP_JS_CTX_ATTR_COMPUTE_ALL_CORES)
 * and there's more than one coregroup */
static const u32 variants_supported_ss_allcore_state[] = {
	/* js[0] uses Fragment only */
	(1u << CORE_REQ_VARIANT_FRAGMENT),

	/* js[1] uses: Compute-all-cores, Compute-only-coherent, Compute-or-Tiling,
	 * compute-specific-coregroup-0, compute-specific-coregroup-1 */
	(1u << CORE_REQ_VARIANT_COMPUTE_ALL_CORES)
	    | (1u << CORE_REQ_VARIANT_COMPUTE_ONLY_COHERENT_GROUP)
	    | (1u << CORE_REQ_VARIANT_COMPUTE_OR_TILING)
	    | (1u << CORE_REQ_VARIANT_COMPUTE_SPECIFIC_COHERENT_0)
	    | (1u << CORE_REQ_VARIANT_COMPUTE_SPECIFIC_COHERENT_1),

	/* js[2] not used */
	0
};

/** Mappings between job slot and variant lists for Soft-Stoppable State for
 * BASE_HW_ISSUE_8987
 *
 * @note There is no 'allcores' variant of this, because this HW issue forces all
 * atoms with BASE_JD_CORE_REQ_SPECIFIC_COHERENT_GROUP to use slot 2 anyway -
 * hence regardless of whether a specific coregroup is targetted, those atoms
 * still make progress. */
static const u32 variants_supported_ss_state_8987[] = {
	/* js[0] uses Fragment only */
	(1u << CORE_REQ_VARIANT_FRAGMENT),

	/* js[1] uses: Compute-all-cores, Compute-only-coherent, Compute-or-Tiling */
	(1u << CORE_REQ_VARIANT_COMPUTE_ALL_CORES)
	    | (1u << CORE_REQ_VARIANT_COMPUTE_ONLY_COHERENT_GROUP)
	    | (1u << CORE_REQ_VARIANT_COMPUTE_OR_TILING),

	/* js[2] uses: All Only-compute atoms (including those targetting a
	 * specific coregroup), and nothing else. This is because their affinity
	 * must not intersect with non-only-compute atoms.
	 *
	 * As a side effect, this causes the 'device_nr' for atoms targetting a
	 * specific coregroup to be ignored */
	(1u << CORE_REQ_VARIANT_ONLY_COMPUTE_8987)
	    | (1u << CORE_REQ_VARIANT_ONLY_COMPUTE_8987_SPECIFIC_COHERENT_0)
	    | (1u << CORE_REQ_VARIANT_ONLY_COMPUTE_8987_SPECIFIC_COHERENT_1)
};

/* Defines for easy asserts 'is scheduled'/'is queued'/'is neither queued norscheduled' */
#define KBASEP_JS_CHECKFLAG_QUEUED       (1u << 0) /**< Check the queued state */
#define KBASEP_JS_CHECKFLAG_SCHEDULED    (1u << 1) /**< Check the scheduled state */
#define KBASEP_JS_CHECKFLAG_IS_QUEUED    (1u << 2) /**< Expect queued state to be set */
#define KBASEP_JS_CHECKFLAG_IS_SCHEDULED (1u << 3) /**< Expect scheduled state to be set */

enum {
	KBASEP_JS_CHECK_NOTQUEUED = KBASEP_JS_CHECKFLAG_QUEUED,
	KBASEP_JS_CHECK_NOTSCHEDULED = KBASEP_JS_CHECKFLAG_SCHEDULED,
	KBASEP_JS_CHECK_QUEUED = KBASEP_JS_CHECKFLAG_QUEUED | KBASEP_JS_CHECKFLAG_IS_QUEUED,
	KBASEP_JS_CHECK_SCHEDULED = KBASEP_JS_CHECKFLAG_SCHEDULED | KBASEP_JS_CHECKFLAG_IS_SCHEDULED
};

typedef u32 kbasep_js_check;

/*
 * Private Functions
 */

/* Table autogenerated using util built from: midgard/scripts/gen_cfs_weight_of_prio.c */

/* weight = 1.25 */
static const int weight_of_priority[] = {
	/*  -20 */ 11, 14, 18, 23,
	/*  -16 */ 29, 36, 45, 56,
	/*  -12 */ 70, 88, 110, 137,
	/*   -8 */ 171, 214, 268, 335,
	/*   -4 */ 419, 524, 655, 819,
	/*    0 */ 1024, 1280, 1600, 2000,
	/*    4 */ 2500, 3125, 3906, 4883,
	/*    8 */ 6104, 7630, 9538, 11923,
	/*   12 */ 14904, 18630, 23288, 29110,
	/*   16 */ 36388, 45485, 56856, 71070
};

/**
 * @note There is nothing to stop the priority of the ctx containing \a
 * ctx_info changing during or immediately after this function is called
 * (because its jsctx_mutex cannot be held during IRQ). Therefore, this
 * function should only be seen as a heuristic guide as to the priority weight
 * of the context.
 */
STATIC u64 priority_weight(kbasep_js_policy_cfs_ctx *ctx_info, u64 time_us)
{
	u64 time_delta_us;
	int priority;
	priority = ctx_info->process_priority + ctx_info->bag_priority;

	/* Adjust runtime_us using priority weight if required */
	if (priority != 0 && time_us != 0) {
		int clamped_priority;

		/* Clamp values to min..max weights */
		if (priority > PROCESS_PRIORITY_MAX)
			clamped_priority = PROCESS_PRIORITY_MAX;
		else if (priority < PROCESS_PRIORITY_MIN)
			clamped_priority = PROCESS_PRIORITY_MIN;
		else
			clamped_priority = priority;

		/* Fixed point multiplication */
		time_delta_us = (time_us * weight_of_priority[WEIGHT_0_NICE + clamped_priority]);
		/* Remove fraction */
		time_delta_us = time_delta_us >> WEIGHT_FIXEDPOINT_SHIFT;
		/* Make sure the time always increases */
		if (0 == time_delta_us)
			time_delta_us++;
	} else {
		time_delta_us = time_us;
	}

	return time_delta_us;
}

#if KBASE_TRACE_ENABLE != 0
STATIC int kbasep_js_policy_trace_get_refcnt_nolock(kbase_device *kbdev, kbase_context *kctx)
{
	kbasep_js_device_data *js_devdata;
	int as_nr;
	int refcnt = 0;

	js_devdata = &kbdev->js_data;

	as_nr = kctx->as_nr;
	if (as_nr != KBASEP_AS_NR_INVALID) {
		kbasep_js_per_as_data *js_per_as_data;
		js_per_as_data = &js_devdata->runpool_irq.per_as_data[as_nr];

		refcnt = js_per_as_data->as_busy_refcount;
	}

	return refcnt;
}

STATIC INLINE int kbasep_js_policy_trace_get_refcnt(kbase_device *kbdev, kbase_context *kctx)
{
	unsigned long flags;
	kbasep_js_device_data *js_devdata;
	int refcnt = 0;

	js_devdata = &kbdev->js_data;

	spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);
	refcnt = kbasep_js_policy_trace_get_refcnt_nolock(kbdev, kctx);
	spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);

	return refcnt;
}
#else				/* KBASE_TRACE_ENABLE != 0 */
STATIC int kbasep_js_policy_trace_get_refcnt_nolock(kbase_device *kbdev, kbase_context *kctx)
{
	CSTD_UNUSED(kbdev);
	CSTD_UNUSED(kctx);
	return 0;
}

STATIC INLINE int kbasep_js_policy_trace_get_refcnt(kbase_device *kbdev, kbase_context *kctx)
{
	CSTD_UNUSED(kbdev);
	CSTD_UNUSED(kctx);
	return 0;
}
#endif				/* KBASE_TRACE_ENABLE != 0 */

#ifdef CONFIG_MALI_DEBUG
STATIC void kbasep_js_debug_check(kbasep_js_policy_cfs *policy_info, kbase_context *kctx, kbasep_js_check check_flag)
{
	/* This function uses the ternary operator and non-explicit comparisons,
	 * because it makes for much shorter, easier to read code */

	if (check_flag & KBASEP_JS_CHECKFLAG_QUEUED) {
		mali_bool is_queued;
		mali_bool expect_queued;
		is_queued = (kbasep_list_member_of(&policy_info->ctx_queue_head, &kctx->jctx.sched_info.runpool.policy_ctx.cfs.list)) ? MALI_TRUE : MALI_FALSE;

		if (!is_queued)
			is_queued = (kbasep_list_member_of(&policy_info->ctx_rt_queue_head, &kctx->jctx.sched_info.runpool.policy_ctx.cfs.list)) ? MALI_TRUE : MALI_FALSE;

		expect_queued = (check_flag & KBASEP_JS_CHECKFLAG_IS_QUEUED) ? MALI_TRUE : MALI_FALSE;

		KBASE_DEBUG_ASSERT_MSG(expect_queued == is_queued, "Expected context %p to be %s but it was %s\n", kctx, (expect_queued) ? "queued" : "not queued", (is_queued) ? "queued" : "not queued");

	}

	if (check_flag & KBASEP_JS_CHECKFLAG_SCHEDULED) {
		mali_bool is_scheduled;
		mali_bool expect_scheduled;
		is_scheduled = (kbasep_list_member_of(&policy_info->scheduled_ctxs_head, &kctx->jctx.sched_info.runpool.policy_ctx.cfs.list)) ? MALI_TRUE : MALI_FALSE;

		expect_scheduled = (check_flag & KBASEP_JS_CHECKFLAG_IS_SCHEDULED) ? MALI_TRUE : MALI_FALSE;
		KBASE_DEBUG_ASSERT_MSG(expect_scheduled == is_scheduled, "Expected context %p to be %s but it was %s\n", kctx, (expect_scheduled) ? "scheduled" : "not scheduled", (is_scheduled) ? "scheduled" : "not scheduled");

	}

}
#else				/* CONFIG_MALI_DEBUG */
STATIC void kbasep_js_debug_check(kbasep_js_policy_cfs *policy_info, kbase_context *kctx, kbasep_js_check check_flag)
{
	CSTD_UNUSED(policy_info);
	CSTD_UNUSED(kctx);
	CSTD_UNUSED(check_flag);
	return;
}
#endif				/* CONFIG_MALI_DEBUG */

STATIC INLINE void set_slot_to_variant_lookup(u32 *bit_array, u32 slot_idx, u32 variants_supported)
{
	u32 overall_bit_idx = slot_idx * KBASEP_JS_MAX_NR_CORE_REQ_VARIANTS;
	u32 word_idx = overall_bit_idx / 32;
	u32 bit_idx = overall_bit_idx % 32;

	KBASE_DEBUG_ASSERT(slot_idx < BASE_JM_MAX_NR_SLOTS);
	KBASE_DEBUG_ASSERT((variants_supported & ~LOOKUP_VARIANT_MASK) == 0);

	bit_array[word_idx] |= variants_supported << bit_idx;
}

STATIC INLINE u32 get_slot_to_variant_lookup(u32 *bit_array, u32 slot_idx)
{
	u32 overall_bit_idx = slot_idx * KBASEP_JS_MAX_NR_CORE_REQ_VARIANTS;
	u32 word_idx = overall_bit_idx / 32;
	u32 bit_idx = overall_bit_idx % 32;

	u32 res;

	KBASE_DEBUG_ASSERT(slot_idx < BASE_JM_MAX_NR_SLOTS);

	res = bit_array[word_idx] >> bit_idx;
	res &= LOOKUP_VARIANT_MASK;

	return res;
}

/* Check the core_req_variants: make sure that every job slot is satisifed by
 * one of the variants. This checks that cached_variant_idx_init will produce a
 * valid result for jobs that make maximum use of the job slots.
 *
 * @note The checks are limited to the job slots - this does not check that
 * every context requirement is covered (because some are intentionally not
 * supported, such as KBASE_CTX_FLAG_SUBMIT_DISABLED) */
#ifdef CONFIG_MALI_DEBUG
STATIC void debug_check_core_req_variants(kbase_device *kbdev, kbasep_js_policy_cfs *policy_info)
{
	kbasep_js_device_data *js_devdata;
	u32 i;
	int j;

	js_devdata = &kbdev->js_data;

	for (j = 0; j < kbdev->gpu_props.num_job_slots; ++j) {
		base_jd_core_req job_core_req;
		mali_bool found = MALI_FALSE;

		job_core_req = js_devdata->js_reqs[j];
		for (i = 0; i < policy_info->num_core_req_variants; ++i) {
			base_jd_core_req var_core_req;
			var_core_req = policy_info->core_req_variants[i].core_req;

			if ((var_core_req & job_core_req) == job_core_req) {
				found = MALI_TRUE;
				break;
			}
		}

		/* Early-out on any failure */
		KBASE_DEBUG_ASSERT_MSG(found != MALI_FALSE, "Job slot %d features 0x%x not matched by core_req_variants. " "Rework core_req_variants and vairants_supported_<...>_state[] to match\n", j, job_core_req);
	}
}
#endif

STATIC void build_core_req_variants(kbase_device *kbdev, kbasep_js_policy_cfs *policy_info)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(policy_info != NULL);
	CSTD_UNUSED(kbdev);

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8987)) {
		KBASE_DEBUG_ASSERT(NUM_CORE_REQ_VARIANTS_8987 <= KBASEP_JS_MAX_NR_CORE_REQ_VARIANTS);

		/* Assume a static set of variants */
		memcpy(policy_info->core_req_variants, core_req_variants_8987, sizeof(core_req_variants_8987));

		policy_info->num_core_req_variants = NUM_CORE_REQ_VARIANTS_8987;
	} else {
		KBASE_DEBUG_ASSERT(NUM_CORE_REQ_VARIANTS <= KBASEP_JS_MAX_NR_CORE_REQ_VARIANTS);

		/* Assume a static set of variants */
		memcpy(policy_info->core_req_variants, core_req_variants, sizeof(core_req_variants));

		policy_info->num_core_req_variants = NUM_CORE_REQ_VARIANTS;
	}

	KBASE_DEBUG_CODE(debug_check_core_req_variants(kbdev, policy_info));
}

STATIC void build_slot_lookups(kbase_device *kbdev, kbasep_js_policy_cfs *policy_info)
{
	u8 i;
	const u32 *variants_supported_ss_for_this_hw = variants_supported_ss_state;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(policy_info != NULL);

	KBASE_DEBUG_ASSERT(kbdev->gpu_props.num_job_slots <= NELEMS(variants_supported_ss_state));
	KBASE_DEBUG_ASSERT(kbdev->gpu_props.num_job_slots <= NELEMS(variants_supported_ss_allcore_state));
	KBASE_DEBUG_ASSERT(kbdev->gpu_props.num_job_slots <= NELEMS(variants_supported_ss_state_8987));

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8987))
		variants_supported_ss_for_this_hw = variants_supported_ss_state_8987;

	/* Given the static set of variants, provide a static set of lookups */
	for (i = 0; i < kbdev->gpu_props.num_job_slots; ++i) {
		set_slot_to_variant_lookup(policy_info->slot_to_variant_lookup_ss_state, i, variants_supported_ss_for_this_hw[i]);

		set_slot_to_variant_lookup(policy_info->slot_to_variant_lookup_ss_allcore_state, i, variants_supported_ss_allcore_state[i]);
	}

}

STATIC mali_error cached_variant_idx_init(const kbasep_js_policy_cfs *policy_info, const kbase_context *kctx, kbase_jd_atom *atom)
{
	kbasep_js_policy_cfs_job *job_info;
	u32 i;
	base_jd_core_req job_core_req;
	u32 job_device_nr;
	kbase_context_flags ctx_flags;
	const kbasep_js_kctx_info *js_kctx_info;
	const kbase_device *kbdev;

	KBASE_DEBUG_ASSERT(policy_info != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);
	KBASE_DEBUG_ASSERT(atom != NULL);

	kbdev = container_of(policy_info, const kbase_device, js_data.policy.cfs);
	job_info = &atom->sched_info.cfs;
	job_core_req = atom->core_req;
	job_device_nr = atom->device_nr;
	js_kctx_info = &kctx->jctx.sched_info;
	ctx_flags = js_kctx_info->ctx.flags;

	/* Initial check for atoms targetting a specific coregroup */
	if ((job_core_req & BASE_JD_REQ_SPECIFIC_COHERENT_GROUP) != MALI_FALSE && job_device_nr >= kbdev->gpu_props.num_core_groups) {
		/* device_nr exceeds the number of coregroups - not allowed by
		 * @ref base_jd_atom API contract */
		return MALI_ERROR_FUNCTION_FAILED;
	}

	/* Pick a core_req variant that matches us. Since they're ordered by least
	 * restrictive first, it picks the least restrictive variant */
	for (i = 0; i < policy_info->num_core_req_variants; ++i) {
		base_jd_core_req var_core_req;
		kbase_context_flags var_ctx_req;
		u32 var_device_nr;
		var_core_req = policy_info->core_req_variants[i].core_req;
		var_ctx_req = policy_info->core_req_variants[i].ctx_req;
		var_device_nr = policy_info->core_req_variants[i].device_nr;

		if ((var_core_req & job_core_req) == job_core_req && (var_ctx_req & ctx_flags) == ctx_flags && ((var_core_req & BASE_JD_REQ_SPECIFIC_COHERENT_GROUP) == MALI_FALSE || var_device_nr == job_device_nr)) {
			job_info->cached_variant_idx = i;
			return MALI_ERROR_NONE;
		}
	}

	/* Could not find a matching requirement, this should only be caused by an
	 * attempt to attack the driver. */
	return MALI_ERROR_FUNCTION_FAILED;
}

STATIC mali_bool dequeue_job(kbase_device *kbdev,
			     kbase_context *kctx,
			     u32 variants_supported,
			     kbase_jd_atom ** const katom_ptr,
			     int job_slot_idx)
{
	kbasep_js_device_data *js_devdata;
	kbasep_js_policy_cfs *policy_info;
	kbasep_js_policy_cfs_ctx *ctx_info;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(katom_ptr != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	js_devdata = &kbdev->js_data;
	policy_info = &js_devdata->policy.cfs;
	ctx_info = &kctx->jctx.sched_info.runpool.policy_ctx.cfs;

	/* Only submit jobs from contexts that are allowed */
	if (kbasep_js_is_submit_allowed(js_devdata, kctx) != MALI_FALSE) {
		/* Check each variant in turn */
		while (variants_supported != 0) {
			long variant_idx;
			struct list_head *job_list;
			variant_idx = ffs(variants_supported) - 1;
			job_list = &ctx_info->job_list_head[variant_idx];

			if (!list_empty(job_list)) {
				/* Found a context with a matching job */
				{
					kbase_jd_atom *front_atom = list_entry(job_list->next, kbase_jd_atom, sched_info.cfs.list);
					KBASE_TRACE_ADD_SLOT(kbdev, JS_POLICY_DEQUEUE_JOB, front_atom->kctx, front_atom, front_atom->jc, job_slot_idx);
				}
				*katom_ptr = list_entry(job_list->next, kbase_jd_atom, sched_info.cfs.list);
				list_del(job_list->next);

				(*katom_ptr)->sched_info.cfs.ticks = 0;

				/* Put this context at the back of the Run Pool */
				list_del(&kctx->jctx.sched_info.runpool.policy_ctx.cfs.list);
				list_add_tail(&kctx->jctx.sched_info.runpool.policy_ctx.cfs.list, &policy_info->scheduled_ctxs_head);

				return MALI_TRUE;
			}

			variants_supported &= ~(1u << variant_idx);
		}
		/* All variants checked by here */
	}

	/* The context does not have a  matching job */

	return MALI_FALSE;
}

/**
 * Hold the runpool_irq spinlock for this
 */
STATIC INLINE mali_bool timer_callback_should_run(kbase_device *kbdev)
{
	kbasep_js_device_data *js_devdata;
	s8 nr_running_ctxs;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	js_devdata = &kbdev->js_data;

	/* nr_user_contexts_running is updated with the runpool_mutex. However, the
	 * locking in the caller gives us a barrier that ensures nr_user_contexts is
	 * up-to-date for reading */
	nr_running_ctxs = js_devdata->nr_user_contexts_running;

#ifdef CONFIG_MALI_DEBUG
	if (js_devdata->softstop_always && nr_running_ctxs > 0) {
		/* Debug support for allowing soft-stop on a single context */
		return MALI_TRUE;
	}
#endif				/* CONFIG_MALI_DEBUG */

	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_9435)) {
		/* Timeouts would have to be 4x longer (due to micro-architectural design)
		 * to support OpenCL conformance tests, so only run the timer when there's:
		 * - 2 or more CL contexts
		 * - 1 or more GLES contexts
		 *
		 * NOTE: We will treat a context that has both Compute and Non-Compute jobs
		 * will be treated as an OpenCL context (hence, we don't check
		 * KBASEP_JS_CTX_ATTR_NON_COMPUTE).
		 */
		{
			s8 nr_compute_ctxs = kbasep_js_ctx_attr_count_on_runpool(kbdev, KBASEP_JS_CTX_ATTR_COMPUTE);
			s8 nr_noncompute_ctxs = nr_running_ctxs - nr_compute_ctxs;

			return (mali_bool) (nr_compute_ctxs >= 2 || nr_noncompute_ctxs > 0);
		}
	} else {
		/* Run the timer callback whenever you have at least 1 context */
		return (mali_bool) (nr_running_ctxs > 0);
	}
}

static enum hrtimer_restart timer_callback(struct hrtimer *timer)
{
	unsigned long flags;
	kbase_device *kbdev;
	kbasep_js_device_data *js_devdata;
	kbasep_js_policy_cfs *policy_info;
	int s;
	mali_bool reset_needed = MALI_FALSE;

	KBASE_DEBUG_ASSERT(timer != NULL);

	policy_info = container_of(timer, kbasep_js_policy_cfs, scheduling_timer);
	kbdev = container_of(policy_info, kbase_device, js_data.policy.cfs);
	js_devdata = &kbdev->js_data;

	/* Loop through the slots */
	spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);
	for (s = 0; s < kbdev->gpu_props.num_job_slots; s++) {
		kbase_jm_slot *slot = &kbdev->jm_slots[s];
		kbase_jd_atom *atom = NULL;

		if (kbasep_jm_nr_jobs_submitted(slot) > 0) {
			atom = kbasep_jm_peek_idx_submit_slot(slot, 0);
			KBASE_DEBUG_ASSERT(atom != NULL);

			if (kbasep_jm_is_dummy_workaround_job(kbdev, atom) != MALI_FALSE) {
				/* Prevent further use of the atom - never cause a soft-stop, hard-stop, or a GPU reset due to it. */
				atom = NULL;
			}
		}

		if (atom != NULL) {
			/* The current version of the model doesn't support Soft-Stop */
			if (!kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_5736)) {
				u32 ticks = atom->sched_info.cfs.ticks++;

#if !CINSTR_DUMPING_ENABLED
				u32 soft_stop_ticks, hard_stop_ticks, gpu_reset_ticks;
				if (atom->core_req & BASE_JD_REQ_ONLY_COMPUTE) {
					soft_stop_ticks = js_devdata->soft_stop_ticks_cl;
					hard_stop_ticks = js_devdata->hard_stop_ticks_cl;
					gpu_reset_ticks = js_devdata->gpu_reset_ticks_cl;
				} else {
					soft_stop_ticks = js_devdata->soft_stop_ticks;
					hard_stop_ticks = js_devdata->hard_stop_ticks_ss;
					gpu_reset_ticks = js_devdata->gpu_reset_ticks_ss;
				}

				/* Job is Soft-Stoppable */
				if (ticks == soft_stop_ticks) {
					/* Job has been scheduled for at least js_devdata->soft_stop_ticks ticks.
					 * Soft stop the slot so we can run other jobs.
					 */
					dev_dbg(kbdev->dev, "Soft-stop");

#if KBASE_DISABLE_SCHEDULING_SOFT_STOPS == 0
					kbase_job_slot_softstop(kbdev, s, atom);
#endif
				} else if (ticks == hard_stop_ticks) {
					/* Job has been scheduled for at least js_devdata->hard_stop_ticks_ss ticks.
					 * It should have been soft-stopped by now. Hard stop the slot.
					 */
#if KBASE_DISABLE_SCHEDULING_HARD_STOPS == 0
					dev_warn(kbdev->dev, "JS: Job Hard-Stopped (took more than %lu ticks at %lu ms/tick)", (unsigned long)ticks, (unsigned long)(js_devdata->scheduling_tick_ns / 1000000u));
					kbase_job_slot_hardstop(atom->kctx, s, atom);
#endif
				} else if (ticks == gpu_reset_ticks) {
					/* Job has been scheduled for at least js_devdata->gpu_reset_ticks_ss ticks.
					 * It should have left the GPU by now. Signal that the GPU needs to be reset.
					 */
					reset_needed = MALI_TRUE;
				}
#else 				/* !CINSTR_DUMPING_ENABLED */
				/* NOTE: During CINSTR_DUMPING_ENABLED, we use the alternate timeouts, which
				 * makes the hard-stop and GPU reset timeout much longer. We also ensure that
				 * we don't soft-stop at all. */
				if (ticks == js_devdata->soft_stop_ticks) {
					/* Job has been scheduled for at least js_devdata->soft_stop_ticks.
					 * We do not soft-stop during CINSTR_DUMPING_ENABLED, however.
					 */
					dev_dbg(kbdev->dev, "Soft-stop");
				} else if (ticks == js_devdata->hard_stop_ticks_nss) {
					/* Job has been scheduled for at least js_devdata->hard_stop_ticks_nss ticks.
					 * Hard stop the slot.
					 */
#if KBASE_DISABLE_SCHEDULING_HARD_STOPS == 0
					dev_warn(kbdev->dev, "JS: Job Hard-Stopped (took more than %lu ticks at %lu ms/tick)", (unsigned long)ticks, (unsigned long)(js_devdata->scheduling_tick_ns / 1000000u));
					kbase_job_slot_hardstop(atom->kctx, s, atom);
#endif
				} else if (ticks == js_devdata->gpu_reset_ticks_nss) {
					/* Job has been scheduled for at least js_devdata->gpu_reset_ticks_nss ticks.
					 * It should have left the GPU by now. Signal that the GPU needs to be reset.
					 */
					reset_needed = MALI_TRUE;
				}
#endif				/* !CINSTR_DUMPING_ENABLED */
			}
		}
	}

	if (reset_needed) {
		dev_err(kbdev->dev, "JS: Job has been on the GPU for too long (KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS/NSS timeout hit). Issueing GPU soft-reset to resolve.");

		if (kbase_prepare_to_reset_gpu_locked(kbdev))
			kbase_reset_gpu_locked(kbdev);
	}

	/* the timer is re-issued if there is contexts in the run-pool */

	if (timer_callback_should_run(kbdev) != MALI_FALSE) {
		hrtimer_start(&policy_info->scheduling_timer, HR_TIMER_DELAY_NSEC(js_devdata->scheduling_tick_ns), HRTIMER_MODE_REL);
	} else {
		KBASE_TRACE_ADD(kbdev, JS_POLICY_TIMER_END, NULL, NULL, 0u, 0u);
		/* timer_running state is updated by kbasep_js_policy_runpool_timers_sync() */
	}

	spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);

	return HRTIMER_NORESTART;
}

/*
 * Non-private functions
 */

mali_error kbasep_js_policy_init(kbase_device *kbdev)
{
	kbasep_js_device_data *js_devdata;
	kbasep_js_policy_cfs *policy_info;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	js_devdata = &kbdev->js_data;
	policy_info = &js_devdata->policy.cfs;

	INIT_LIST_HEAD(&policy_info->ctx_queue_head);
	INIT_LIST_HEAD(&policy_info->scheduled_ctxs_head);
	INIT_LIST_HEAD(&policy_info->ctx_rt_queue_head);

	atomic64_set(&policy_info->least_runtime_us, KBASEP_JS_RUNTIME_EMPTY);
	atomic64_set(&policy_info->rt_least_runtime_us, KBASEP_JS_RUNTIME_EMPTY);

	hrtimer_init(&policy_info->scheduling_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	policy_info->scheduling_timer.function = timer_callback;

	policy_info->timer_running = MALI_FALSE;
	policy_info->head_runtime_us = 0;

	/* Build up the core_req variants */
	build_core_req_variants(kbdev, policy_info);
	/* Build the slot to variant lookups */
	build_slot_lookups(kbdev, policy_info);

	return MALI_ERROR_NONE;
}

void kbasep_js_policy_term(kbasep_js_policy *js_policy)
{
	kbasep_js_policy_cfs *policy_info;
	KBASE_DEBUG_ASSERT(js_policy != NULL);
	policy_info = &js_policy->cfs;

	/* ASSERT that there are no contexts queued */
	KBASE_DEBUG_ASSERT(list_empty(&policy_info->ctx_queue_head));
	KBASE_DEBUG_ASSERT(KBASEP_JS_RUNTIME_EMPTY == atomic64_read(&policy_info->least_runtime_us));

	/* ASSERT that there are no contexts scheduled */
	KBASE_DEBUG_ASSERT(list_empty(&policy_info->scheduled_ctxs_head));

	/* ASSERT that there are no contexts queued */
	KBASE_DEBUG_ASSERT(list_empty(&policy_info->ctx_rt_queue_head));
	KBASE_DEBUG_ASSERT(KBASEP_JS_RUNTIME_EMPTY == atomic64_read(&policy_info->rt_least_runtime_us));

	hrtimer_cancel(&policy_info->scheduling_timer);
}

mali_error kbasep_js_policy_init_ctx(kbase_device *kbdev, kbase_context *kctx)
{
	kbasep_js_device_data *js_devdata;
	kbasep_js_policy_cfs_ctx *ctx_info;
	kbasep_js_policy_cfs *policy_info;
	u32 i;
	int policy;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	js_devdata = &kbdev->js_data;
	policy_info = &kbdev->js_data.policy.cfs;
	ctx_info = &kctx->jctx.sched_info.runpool.policy_ctx.cfs;

	KBASE_TRACE_ADD_REFCOUNT(kbdev, JS_POLICY_INIT_CTX, kctx, NULL, 0u, kbasep_js_policy_trace_get_refcnt(kbdev, kctx));

	for (i = 0; i < policy_info->num_core_req_variants; ++i)
		INIT_LIST_HEAD(&ctx_info->job_list_head[i]);

	policy = current->policy;
	if (policy == SCHED_FIFO || policy == SCHED_RR) {
		ctx_info->process_rt_policy = MALI_TRUE;
		ctx_info->process_priority = (((MAX_RT_PRIO - 1) - current->rt_priority) / 5) - 20;
	} else {
		ctx_info->process_rt_policy = MALI_FALSE;
		ctx_info->process_priority = (current->static_prio - MAX_RT_PRIO) - 20;
	}

	ctx_info->bag_total_priority = 0;
	ctx_info->bag_total_nr_atoms = 0;

	/* Initial runtime (relative to least-run context runtime)
	 *
	 * This uses the Policy Queue's most up-to-date head_runtime_us by using the
	 * queue mutex to issue memory barriers - also ensure future updates to
	 * head_runtime_us occur strictly after this context is initialized */
	mutex_lock(&js_devdata->queue_mutex);

	/* No need to hold the the runpool_irq.lock here, because we're initializing
	 * the value, and the context is definitely not being updated in the
	 * runpool at this point. The queue_mutex ensures the memory barrier. */
	ctx_info->runtime_us = policy_info->head_runtime_us + priority_weight(ctx_info, (u64) js_devdata->cfs_ctx_runtime_init_slices * (u64) (js_devdata->ctx_timeslice_ns / 1000u));

	mutex_unlock(&js_devdata->queue_mutex);

	return MALI_ERROR_NONE;
}

void kbasep_js_policy_term_ctx(kbasep_js_policy *js_policy, kbase_context *kctx)
{
	kbasep_js_policy_cfs_ctx *ctx_info;
	kbasep_js_policy_cfs *policy_info;
	u32 i;

	KBASE_DEBUG_ASSERT(js_policy != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	policy_info = &js_policy->cfs;
	ctx_info = &kctx->jctx.sched_info.runpool.policy_ctx.cfs;

	{
		kbase_device *kbdev = container_of(js_policy, kbase_device, js_data.policy);
		KBASE_TRACE_ADD_REFCOUNT(kbdev, JS_POLICY_TERM_CTX, kctx, NULL, 0u, kbasep_js_policy_trace_get_refcnt(kbdev, kctx));
	}

	/* ASSERT that no jobs are present */
	for (i = 0; i < policy_info->num_core_req_variants; ++i)
		KBASE_DEBUG_ASSERT(list_empty(&ctx_info->job_list_head[i]));

	/* No work to do */
}

/*
 * Context Management
 */

void kbasep_js_policy_enqueue_ctx(kbasep_js_policy *js_policy, kbase_context *kctx)
{
	kbasep_js_policy_cfs *policy_info;
	kbasep_js_policy_cfs_ctx *ctx_info;
	kbase_context *head_ctx;
	kbase_context *list_kctx = NULL;
	kbasep_js_device_data *js_devdata;
	struct list_head *queue_head;
	struct list_head *pos;
	kbase_device *kbdev;
	atomic64_t *least_runtime_us;
	u64 head_runtime;

	KBASE_DEBUG_ASSERT(js_policy != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	policy_info = &js_policy->cfs;
	ctx_info = &kctx->jctx.sched_info.runpool.policy_ctx.cfs;
	kbdev = container_of(js_policy, kbase_device, js_data.policy);
	js_devdata = &kbdev->js_data;

	KBASE_TRACE_ADD_REFCOUNT(kbdev, JS_POLICY_ENQUEUE_CTX, kctx, NULL, 0u, kbasep_js_policy_trace_get_refcnt(kbdev, kctx));

	/* ASSERT about scheduled-ness/queued-ness */
	kbasep_js_debug_check(policy_info, kctx, KBASEP_JS_CHECK_NOTQUEUED);

	/* Clamp the runtime to prevent DoS attacks through "stored-up" runtime */
	if (policy_info->head_runtime_us > ctx_info->runtime_us + (u64) js_devdata->cfs_ctx_runtime_min_slices * (u64) (js_devdata->ctx_timeslice_ns / 1000u)) {
		/* No need to hold the the runpool_irq.lock here, because we're essentially
		 * initializing the value, and the context is definitely not being updated in the
		 * runpool at this point. The queue_mutex held by the caller ensures the memory
		 * barrier. */
		ctx_info->runtime_us = policy_info->head_runtime_us - (u64) js_devdata->cfs_ctx_runtime_min_slices * (u64) (js_devdata->ctx_timeslice_ns / 1000u);
	}

	/* Find the position where the context should be enqueued */
	if (ctx_info->process_rt_policy) {
		queue_head = &policy_info->ctx_rt_queue_head;
		least_runtime_us = &policy_info->rt_least_runtime_us;
	} else {
		queue_head = &policy_info->ctx_queue_head;
		least_runtime_us = &policy_info->least_runtime_us;
	}

	if (list_empty(queue_head)) {
		list_add_tail(&kctx->jctx.sched_info.runpool.policy_ctx.cfs.list, queue_head);
	} else {
		list_for_each(pos, queue_head) {
			kbasep_js_policy_cfs_ctx *list_ctx_info;

			list_kctx = list_entry(pos, kbase_context, jctx.sched_info.runpool.policy_ctx.cfs.list);
			list_ctx_info = &list_kctx->jctx.sched_info.runpool.policy_ctx.cfs;

			if ((kctx->jctx.sched_info.ctx.flags & KBASE_CTX_FLAG_PRIVILEGED) != 0)
				break;

			if ((list_ctx_info->runtime_us > ctx_info->runtime_us) && ((list_kctx->jctx.sched_info.ctx.flags & KBASE_CTX_FLAG_PRIVILEGED) == 0))
				break;
		}

		/* Add the context to the queue */
		list_add_tail(&kctx->jctx.sched_info.runpool.policy_ctx.cfs.list, &list_kctx->jctx.sched_info.runpool.policy_ctx.cfs.list);
	}

	/* Ensure least_runtime_us is up to date*/
	head_ctx = list_entry(queue_head->next, kbase_context, jctx.sched_info.runpool.policy_ctx.cfs.list);
	head_runtime = head_ctx->jctx.sched_info.runpool.policy_ctx.cfs.runtime_us;
	atomic64_set(least_runtime_us, head_runtime);
}

mali_bool kbasep_js_policy_dequeue_head_ctx(kbasep_js_policy *js_policy, kbase_context ** const kctx_ptr)
{
	kbasep_js_policy_cfs *policy_info;
	kbase_context *head_ctx;
	struct list_head *queue_head;
	atomic64_t *least_runtime_us;
	kbase_device *kbdev;

	KBASE_DEBUG_ASSERT(js_policy != NULL);
	KBASE_DEBUG_ASSERT(kctx_ptr != NULL);

	policy_info = &js_policy->cfs;
	kbdev = container_of(js_policy, kbase_device, js_data.policy);

	/* attempt to dequeue from the 'realttime' queue first */
	if (list_empty(&policy_info->ctx_rt_queue_head)) {
		if (list_empty(&policy_info->ctx_queue_head)) {
			/* Nothing to dequeue */
			return MALI_FALSE;
		} else {
			queue_head = &policy_info->ctx_queue_head;
			least_runtime_us = &policy_info->least_runtime_us;
		}
	} else {
		queue_head = &policy_info->ctx_rt_queue_head;
		least_runtime_us = &policy_info->rt_least_runtime_us;
	}

	/* Contexts are dequeued from the front of the queue */
	*kctx_ptr = list_entry(queue_head->next, kbase_context, jctx.sched_info.runpool.policy_ctx.cfs.list);
	/* If dequeuing will empty the list, then set least_runtime_us prior to deletion */
	if (queue_head->next->next == queue_head)
		atomic64_set(least_runtime_us, KBASEP_JS_RUNTIME_EMPTY);
	list_del(queue_head->next);

	KBASE_TRACE_ADD_REFCOUNT(kbdev, JS_POLICY_DEQUEUE_HEAD_CTX, *kctx_ptr, NULL, 0u, kbasep_js_policy_trace_get_refcnt(kbdev, *kctx_ptr));

	/* Update the head runtime */
	if (!list_empty(queue_head)) {
		u64 head_runtime;

		head_ctx = list_entry(queue_head->next, kbase_context, jctx.sched_info.runpool.policy_ctx.cfs.list);

		/* No need to hold the the runpool_irq.lock here for reading - the
		 * context is definitely not being updated in the runpool at this
		 * point. The queue_mutex held by the caller ensures the memory barrier. */
		head_runtime = head_ctx->jctx.sched_info.runpool.policy_ctx.cfs.runtime_us;

		if (head_runtime > policy_info->head_runtime_us)
			policy_info->head_runtime_us = head_runtime;

		atomic64_set(least_runtime_us, head_runtime);
	}

	return MALI_TRUE;
}

mali_bool kbasep_js_policy_try_evict_ctx(kbasep_js_policy *js_policy, kbase_context *kctx)
{
	kbasep_js_policy_cfs_ctx *ctx_info;
	kbasep_js_policy_cfs *policy_info;
	mali_bool is_present;
	struct list_head *queue_head;
	atomic64_t *least_runtime_us;
	struct list_head *qhead;
	kbase_device *kbdev;

	KBASE_DEBUG_ASSERT(js_policy != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	policy_info = &js_policy->cfs;
	ctx_info = &kctx->jctx.sched_info.runpool.policy_ctx.cfs;
	kbdev = container_of(js_policy, kbase_device, js_data.policy);

	if (ctx_info->process_rt_policy) {
		queue_head = &policy_info->ctx_rt_queue_head;
		least_runtime_us = &policy_info->rt_least_runtime_us;
	} else {
		queue_head = &policy_info->ctx_queue_head;
		least_runtime_us = &policy_info->least_runtime_us;
	}

	qhead = queue_head;

	is_present = kbasep_list_member_of(qhead, &kctx->jctx.sched_info.runpool.policy_ctx.cfs.list);

	KBASE_TRACE_ADD_REFCOUNT_INFO(kbdev, JS_POLICY_TRY_EVICT_CTX, kctx, NULL, 0u, kbasep_js_policy_trace_get_refcnt(kbdev, kctx), is_present);

	if (is_present != MALI_FALSE) {
		kbase_context *head_ctx;
		qhead = queue_head;

		/* If dequeuing will empty the list, then set least_runtime_us prior to deletion */
		if (queue_head->next->next == queue_head)
			atomic64_set(least_runtime_us, KBASEP_JS_RUNTIME_EMPTY);

		/* Remove the context */
		list_del(&kctx->jctx.sched_info.runpool.policy_ctx.cfs.list);

		qhead = queue_head;
		/* Update the head runtime */
		if (!list_empty(qhead)) {
			u64 head_runtime;

			head_ctx = list_entry(qhead->next, kbase_context, jctx.sched_info.runpool.policy_ctx.cfs.list);

			/* No need to hold the the runpool_irq.lock here for reading - the
			 * context is definitely not being updated in the runpool at this
			 * point. The queue_mutex held by the caller ensures the memory barrier. */
			head_runtime = head_ctx->jctx.sched_info.runpool.policy_ctx.cfs.runtime_us;

			if (head_runtime > policy_info->head_runtime_us)
				policy_info->head_runtime_us = head_runtime;

			atomic64_set(least_runtime_us, head_runtime);
		}
	}

	return is_present;
}

void kbasep_js_policy_foreach_ctx_job(kbasep_js_policy *js_policy, kbase_context *kctx,
	kbasep_js_policy_ctx_job_cb callback, mali_bool detach_jobs)
{
	kbasep_js_policy_cfs *policy_info;
	kbasep_js_policy_cfs_ctx *ctx_info;
	kbase_device *kbdev;
	u32 i;

	KBASE_DEBUG_ASSERT(js_policy != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	kbdev = container_of(js_policy, kbase_device, js_data.policy);
	policy_info = &js_policy->cfs;
	ctx_info = &kctx->jctx.sched_info.runpool.policy_ctx.cfs;

	KBASE_TRACE_ADD_REFCOUNT(kbdev, JS_POLICY_FOREACH_CTX_JOBS, kctx, NULL, 0u, kbasep_js_policy_trace_get_refcnt(kbdev, kctx));

	/* Invoke callback on jobs on each variant in turn */
	for (i = 0; i < policy_info->num_core_req_variants; ++i) {
		struct list_head *job_list;
		struct kbase_jd_atom *atom;
		struct kbase_jd_atom *tmp_iter;
		job_list = &ctx_info->job_list_head[i];
		/* Invoke callback on all kbase_jd_atoms in this list, optionally
		 * removing them from the list */
		list_for_each_entry_safe(atom, tmp_iter, job_list, sched_info.cfs.list) {
			if (detach_jobs)
				list_del(&atom->sched_info.cfs.list);
			callback(kbdev, atom);
		}
	}

}

void kbasep_js_policy_runpool_add_ctx(kbasep_js_policy *js_policy, kbase_context *kctx)
{
	kbasep_js_policy_cfs *policy_info;
	kbasep_js_device_data *js_devdata;
	kbase_device *kbdev;

	KBASE_DEBUG_ASSERT(js_policy != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	policy_info = &js_policy->cfs;
	js_devdata = container_of(js_policy, kbasep_js_device_data, policy);

	kbdev = kctx->kbdev;

	{
		KBASE_TRACE_ADD_REFCOUNT(kbdev, JS_POLICY_RUNPOOL_ADD_CTX, kctx, NULL, 0u, kbasep_js_policy_trace_get_refcnt_nolock(kbdev, kctx));
	}

	/* ASSERT about scheduled-ness/queued-ness */
	kbasep_js_debug_check(policy_info, kctx, KBASEP_JS_CHECK_NOTSCHEDULED);

	/* All enqueued contexts go to the back of the runpool */
	list_add_tail(&kctx->jctx.sched_info.runpool.policy_ctx.cfs.list, &policy_info->scheduled_ctxs_head);

	if (timer_callback_should_run(kbdev) != MALI_FALSE && policy_info->timer_running == MALI_FALSE) {
		hrtimer_start(&policy_info->scheduling_timer, HR_TIMER_DELAY_NSEC(js_devdata->scheduling_tick_ns), HRTIMER_MODE_REL);

		KBASE_TRACE_ADD(kbdev, JS_POLICY_TIMER_START, NULL, NULL, 0u, 0u);
		policy_info->timer_running = MALI_TRUE;
	}
}

void kbasep_js_policy_runpool_remove_ctx(kbasep_js_policy *js_policy, kbase_context *kctx)
{
	kbasep_js_policy_cfs *policy_info;

	KBASE_DEBUG_ASSERT(js_policy != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	policy_info = &js_policy->cfs;

	{
		kbase_device *kbdev = container_of(js_policy, kbase_device, js_data.policy);
		KBASE_TRACE_ADD_REFCOUNT(kbdev, JS_POLICY_RUNPOOL_REMOVE_CTX, kctx, NULL, 0u, kbasep_js_policy_trace_get_refcnt_nolock(kbdev, kctx));
	}

	/* ASSERT about scheduled-ness/queued-ness */
	kbasep_js_debug_check(policy_info, kctx, KBASEP_JS_CHECK_SCHEDULED);

	/* No searching or significant list maintenance required to remove this context */
	list_del(&kctx->jctx.sched_info.runpool.policy_ctx.cfs.list);

}

mali_bool kbasep_js_policy_should_remove_ctx(kbasep_js_policy *js_policy, kbase_context *kctx)
{
	kbasep_js_policy_cfs_ctx *ctx_info;
	kbasep_js_policy_cfs *policy_info;
	kbasep_js_device_data *js_devdata;
	u64 least_runtime_us;

	KBASE_DEBUG_ASSERT(js_policy != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	policy_info = &js_policy->cfs;
	ctx_info = &kctx->jctx.sched_info.runpool.policy_ctx.cfs;
	js_devdata = container_of(js_policy, kbasep_js_device_data, policy);

	if (ctx_info->process_rt_policy)
		least_runtime_us = atomic64_read(&policy_info->rt_least_runtime_us);
	else
		least_runtime_us = atomic64_read(&policy_info->least_runtime_us);

	if (KBASEP_JS_RUNTIME_EMPTY == least_runtime_us) {
		/* Queue is empty */
		return MALI_FALSE;
	}

	if ((least_runtime_us + priority_weight(ctx_info, (u64) (js_devdata->ctx_timeslice_ns / 1000u)))
	    < ctx_info->runtime_us) {
		/* The context is scheduled out if it's not the least-run context anymore.
		 * The "real" head runtime is used instead of the cached runtime so the current
		 * context is not scheduled out when there is less contexts than address spaces.
		 */
		return MALI_TRUE;
	}

	return MALI_FALSE;
}

void kbasep_js_policy_runpool_timers_sync(kbasep_js_policy *js_policy)
{
	kbasep_js_policy_cfs *policy_info;
	kbase_device *kbdev;
	kbasep_js_device_data *js_devdata;

	KBASE_DEBUG_ASSERT(js_policy != NULL);

	policy_info = &js_policy->cfs;
	kbdev = container_of(js_policy, kbase_device, js_data.policy);
	js_devdata = &kbdev->js_data;

	if (!timer_callback_should_run(kbdev)) {
		unsigned long flags;

		/* If the timer is running now, synchronize with it by
		 * locking/unlocking its spinlock, to ensure it's not using an old value
		 * from timer_callback_should_run() */
		spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);
		spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);

		/* From now on, return value of timer_callback_should_run() will also
		 * cause the timer to not requeue itself. Its return value cannot
		 * change, because it depends on variables updated with the
		 * runpool_mutex held, which the caller of this must also hold */
		hrtimer_cancel(&policy_info->scheduling_timer);

		policy_info->timer_running = MALI_FALSE;
	}
}

/*
 * Job Chain Management
 */

mali_error kbasep_js_policy_init_job(const kbasep_js_policy *js_policy, const kbase_context *kctx, kbase_jd_atom *katom)
{
	const kbasep_js_policy_cfs *policy_info;

	KBASE_DEBUG_ASSERT(js_policy != NULL);
	KBASE_DEBUG_ASSERT(katom != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	policy_info = &js_policy->cfs;

	/* Determine the job's index into the job list head, will return error if the
	 * atom is malformed and so is reported. */
	return cached_variant_idx_init(policy_info, kctx, katom);
}

void kbasep_js_policy_register_job(kbasep_js_policy *js_policy, kbase_context *kctx, kbase_jd_atom *katom)
{
	kbasep_js_policy_cfs_ctx *ctx_info;

	KBASE_DEBUG_ASSERT(js_policy != NULL);
	KBASE_DEBUG_ASSERT(katom != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	ctx_info = &kctx->jctx.sched_info.runpool.policy_ctx.cfs;

	/* Adjust context priority to include the new job */
	ctx_info->bag_total_nr_atoms++;
	ctx_info->bag_total_priority += katom->nice_prio;

	/* Get average priority and convert to NICE range -20..19 */
	if (ctx_info->bag_total_nr_atoms)
		ctx_info->bag_priority = (ctx_info->bag_total_priority / ctx_info->bag_total_nr_atoms) - 20;
}

void kbasep_js_policy_deregister_job(kbasep_js_policy *js_policy, kbase_context *kctx, kbase_jd_atom *katom)
{
	kbasep_js_policy_cfs_ctx *ctx_info;

	KBASE_DEBUG_ASSERT(js_policy != NULL);
	CSTD_UNUSED(js_policy);
	KBASE_DEBUG_ASSERT(katom != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	ctx_info = &kctx->jctx.sched_info.runpool.policy_ctx.cfs;

	/* Adjust context priority to no longer include removed job */
	KBASE_DEBUG_ASSERT(ctx_info->bag_total_nr_atoms > 0);
	ctx_info->bag_total_nr_atoms--;
	ctx_info->bag_total_priority -= katom->nice_prio;
	KBASE_DEBUG_ASSERT(ctx_info->bag_total_priority >= 0);

	/* Get average priority and convert to NICE range -20..19 */
	if (ctx_info->bag_total_nr_atoms)
		ctx_info->bag_priority = (ctx_info->bag_total_priority / ctx_info->bag_total_nr_atoms) - 20;
}
KBASE_EXPORT_TEST_API(kbasep_js_policy_deregister_job)

mali_bool kbasep_js_policy_dequeue_job(kbase_device *kbdev,
				       int job_slot_idx,
				       kbase_jd_atom ** const katom_ptr)
{
	kbasep_js_device_data *js_devdata;
	kbasep_js_policy_cfs *policy_info;
	kbase_context *kctx;
	u32 variants_supported;
	struct list_head *pos;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(katom_ptr != NULL);
	KBASE_DEBUG_ASSERT(job_slot_idx < BASE_JM_MAX_NR_SLOTS);

	js_devdata = &kbdev->js_data;
	policy_info = &js_devdata->policy.cfs;

	/* Get the variants for this slot */
	if (kbdev->gpu_props.num_core_groups > 1 && kbasep_js_ctx_attr_is_attr_on_runpool(kbdev, KBASEP_JS_CTX_ATTR_COMPUTE_ALL_CORES) != MALI_FALSE) {
		/* SS-allcore state, and there's more than one coregroup */
		variants_supported = get_slot_to_variant_lookup(policy_info->slot_to_variant_lookup_ss_allcore_state, job_slot_idx);
	} else {
		/* SS-state */
		variants_supported = get_slot_to_variant_lookup(policy_info->slot_to_variant_lookup_ss_state, job_slot_idx);
	}

	/* First pass through the runpool we consider the realtime priority jobs */
	list_for_each(pos, &policy_info->scheduled_ctxs_head) {
		kctx = list_entry(pos, kbase_context, jctx.sched_info.runpool.policy_ctx.cfs.list);
		if (kctx->jctx.sched_info.runpool.policy_ctx.cfs.process_rt_policy) {
			if (dequeue_job(kbdev, kctx, variants_supported, katom_ptr, job_slot_idx)) {
				/* Realtime policy job matched */
				return MALI_TRUE;
			}
		}
	}

	/* Second pass through the runpool we consider the non-realtime priority jobs */
	list_for_each(pos, &policy_info->scheduled_ctxs_head) {
		kctx = list_entry(pos, kbase_context, jctx.sched_info.runpool.policy_ctx.cfs.list);
		if (kctx->jctx.sched_info.runpool.policy_ctx.cfs.process_rt_policy == MALI_FALSE) {
			if (dequeue_job(kbdev, kctx, variants_supported, katom_ptr, job_slot_idx)) {
				/* Non-realtime policy job matched */
				return MALI_TRUE;
			}
		}
	}

	/* By this point, no contexts had a matching job */
	return MALI_FALSE;
}

void kbasep_js_policy_enqueue_job(kbasep_js_policy *js_policy, kbase_jd_atom *katom)
{
	kbasep_js_policy_cfs_job *job_info;
	kbasep_js_policy_cfs_ctx *ctx_info;
	kbase_context *parent_ctx;

	KBASE_DEBUG_ASSERT(js_policy != NULL);
	KBASE_DEBUG_ASSERT(katom != NULL);
	parent_ctx = katom->kctx;
	KBASE_DEBUG_ASSERT(parent_ctx != NULL);

	job_info = &katom->sched_info.cfs;
	ctx_info = &parent_ctx->jctx.sched_info.runpool.policy_ctx.cfs;

	{
		kbase_device *kbdev = container_of(js_policy, kbase_device, js_data.policy);
		KBASE_TRACE_ADD(kbdev, JS_POLICY_ENQUEUE_JOB, katom->kctx, katom, katom->jc, 0);
	}
	list_add_tail(&katom->sched_info.cfs.list, &ctx_info->job_list_head[job_info->cached_variant_idx]);
}

void kbasep_js_policy_log_job_result(kbasep_js_policy *js_policy, kbase_jd_atom *katom, u64 time_spent_us)
{
	kbasep_js_policy_cfs_ctx *ctx_info;
	kbase_context *parent_ctx;
	KBASE_DEBUG_ASSERT(js_policy != NULL);
	KBASE_DEBUG_ASSERT(katom != NULL);
	CSTD_UNUSED(js_policy);

	parent_ctx = katom->kctx;
	KBASE_DEBUG_ASSERT(parent_ctx != NULL);

	ctx_info = &parent_ctx->jctx.sched_info.runpool.policy_ctx.cfs;

	ctx_info->runtime_us += priority_weight(ctx_info, time_spent_us);
}

mali_bool kbasep_js_policy_ctx_has_priority(kbasep_js_policy *js_policy, kbase_context *current_ctx, kbase_context *new_ctx)
{
	kbasep_js_policy_cfs_ctx *current_ctx_info;
	kbasep_js_policy_cfs_ctx *new_ctx_info;

	KBASE_DEBUG_ASSERT(current_ctx != NULL);
	KBASE_DEBUG_ASSERT(new_ctx != NULL);
	CSTD_UNUSED(js_policy);

	current_ctx_info = &current_ctx->jctx.sched_info.runpool.policy_ctx.cfs;
	new_ctx_info = &new_ctx->jctx.sched_info.runpool.policy_ctx.cfs;

	if ((current_ctx_info->process_rt_policy == MALI_FALSE) && (new_ctx_info->process_rt_policy == MALI_TRUE))
		return MALI_TRUE;

	if ((current_ctx_info->process_rt_policy == new_ctx_info->process_rt_policy) && (current_ctx_info->bag_priority > new_ctx_info->bag_priority))
		return MALI_TRUE;

	return MALI_FALSE;
}

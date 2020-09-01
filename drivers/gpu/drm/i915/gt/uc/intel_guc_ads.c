// SPDX-License-Identifier: MIT
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#include "gt/intel_gt.h"
#include "intel_guc_ads.h"
#include "intel_uc.h"
#include "i915_drv.h"

/*
 * The Additional Data Struct (ADS) has pointers for different buffers used by
 * the GuC. One single gem object contains the ADS struct itself (guc_ads), the
 * scheduling policies (guc_policies), a structure describing a collection of
 * register sets (guc_mmio_reg_state) and some extra pages for the GuC to save
 * its internal state for sleep.
 */

static void guc_policy_init(struct guc_policy *policy)
{
	policy->execution_quantum = POLICY_DEFAULT_EXECUTION_QUANTUM_US;
	policy->preemption_time = POLICY_DEFAULT_PREEMPTION_TIME_US;
	policy->fault_time = POLICY_DEFAULT_FAULT_TIME_US;
	policy->policy_flags = 0;
}

static void guc_policies_init(struct guc_policies *policies)
{
	struct guc_policy *policy;
	u32 p, i;

	policies->dpc_promote_time = POLICY_DEFAULT_DPC_PROMOTE_TIME_US;
	policies->max_num_work_items = POLICY_MAX_NUM_WI;

	for (p = 0; p < GUC_CLIENT_PRIORITY_NUM; p++) {
		for (i = 0; i < GUC_MAX_ENGINE_CLASSES; i++) {
			policy = &policies->policy[p][i];

			guc_policy_init(policy);
		}
	}

	policies->is_valid = 1;
}

static void guc_ct_pool_entries_init(struct guc_ct_pool_entry *pool, u32 num)
{
	memset(pool, 0, num * sizeof(*pool));
}

/*
 * The first 80 dwords of the register state context, containing the
 * execlists and ppgtt registers.
 */
#define LR_HW_CONTEXT_SIZE	(80 * sizeof(u32))

/* The ads obj includes the struct itself and buffers passed to GuC */
struct __guc_ads_blob {
	struct guc_ads ads;
	struct guc_policies policies;
	struct guc_mmio_reg_state reg_state;
	struct guc_gt_system_info system_info;
	struct guc_clients_info clients_info;
	struct guc_ct_pool_entry ct_pool[GUC_CT_POOL_SIZE];
	u8 reg_state_buffer[GUC_S3_SAVE_SPACE_PAGES * PAGE_SIZE];
} __packed;

static void __guc_ads_init(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct __guc_ads_blob *blob = guc->ads_blob;
	const u32 skipped_size = LRC_PPHWSP_SZ * PAGE_SIZE + LR_HW_CONTEXT_SIZE;
	u32 base;
	u8 engine_class;

	/* GuC scheduling policies */
	guc_policies_init(&blob->policies);

	/*
	 * GuC expects a per-engine-class context image and size
	 * (minus hwsp and ring context). The context image will be
	 * used to reinitialize engines after a reset. It must exist
	 * and be pinned in the GGTT, so that the address won't change after
	 * we have told GuC where to find it. The context size will be used
	 * to validate that the LRC base + size fall within allowed GGTT.
	 */
	for (engine_class = 0; engine_class <= MAX_ENGINE_CLASS; ++engine_class) {
		if (engine_class == OTHER_CLASS)
			continue;
		/*
		 * TODO: Set context pointer to default state to allow
		 * GuC to re-init guilty contexts after internal reset.
		 */
		blob->ads.golden_context_lrca[engine_class] = 0;
		blob->ads.eng_state_size[engine_class] =
			intel_engine_context_size(guc_to_gt(guc),
						  engine_class) -
			skipped_size;
	}

	/* System info */
	blob->system_info.slice_enabled = hweight8(gt->info.sseu.slice_mask);
	blob->system_info.rcs_enabled = 1;
	blob->system_info.bcs_enabled = 1;

	blob->system_info.vdbox_enable_mask = VDBOX_MASK(gt);
	blob->system_info.vebox_enable_mask = VEBOX_MASK(gt);
	blob->system_info.vdbox_sfc_support_mask = gt->info.vdbox_sfc_access;

	base = intel_guc_ggtt_offset(guc, guc->ads_vma);

	/* Clients info  */
	guc_ct_pool_entries_init(blob->ct_pool, ARRAY_SIZE(blob->ct_pool));

	blob->clients_info.clients_num = 1;
	blob->clients_info.ct_pool_addr = base + ptr_offset(blob, ct_pool);
	blob->clients_info.ct_pool_count = ARRAY_SIZE(blob->ct_pool);

	/* ADS */
	blob->ads.scheduler_policies = base + ptr_offset(blob, policies);
	blob->ads.reg_state_buffer = base + ptr_offset(blob, reg_state_buffer);
	blob->ads.reg_state_addr = base + ptr_offset(blob, reg_state);
	blob->ads.gt_system_info = base + ptr_offset(blob, system_info);
	blob->ads.clients_info = base + ptr_offset(blob, clients_info);

	i915_gem_object_flush_map(guc->ads_vma->obj);
}

/**
 * intel_guc_ads_create() - allocates and initializes GuC ADS.
 * @guc: intel_guc struct
 *
 * GuC needs memory block (Additional Data Struct), where it will store
 * some data. Allocate and initialize such memory block for GuC use.
 */
int intel_guc_ads_create(struct intel_guc *guc)
{
	const u32 size = PAGE_ALIGN(sizeof(struct __guc_ads_blob));
	int ret;

	GEM_BUG_ON(guc->ads_vma);

	ret = intel_guc_allocate_and_map_vma(guc, size, &guc->ads_vma,
					     (void **)&guc->ads_blob);

	if (ret)
		return ret;

	__guc_ads_init(guc);

	return 0;
}

void intel_guc_ads_destroy(struct intel_guc *guc)
{
	i915_vma_unpin_and_release(&guc->ads_vma, I915_VMA_RELEASE_MAP);
}

/**
 * intel_guc_ads_reset() - prepares GuC Additional Data Struct for reuse
 * @guc: intel_guc struct
 *
 * GuC stores some data in ADS, which might be stale after a reset.
 * Reinitialize whole ADS in case any part of it was corrupted during
 * previous GuC run.
 */
void intel_guc_ads_reset(struct intel_guc *guc)
{
	if (!guc->ads_vma)
		return;
	__guc_ads_init(guc);
}

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
 * the GuC. One single gem object contains the ADS struct itself (guc_ads) and
 * all the extra buffers indirectly linked via the ADS struct's entries.
 *
 * Layout of the ADS blob allocated for the GuC:
 *
 *      +---------------------------------------+ <== base
 *      | guc_ads                               |
 *      +---------------------------------------+
 *      | guc_policies                          |
 *      +---------------------------------------+
 *      | guc_gt_system_info                    |
 *      +---------------------------------------+
 *      | guc_clients_info                      |
 *      +---------------------------------------+
 *      | guc_ct_pool_entry[size]               |
 *      +---------------------------------------+
 *      | padding                               |
 *      +---------------------------------------+ <== 4K aligned
 *      | private data                          |
 *      +---------------------------------------+
 *      | padding                               |
 *      +---------------------------------------+ <== 4K aligned
 */
struct __guc_ads_blob {
	struct guc_ads ads;
	struct guc_policies policies;
	struct guc_gt_system_info system_info;
	struct guc_clients_info clients_info;
	struct guc_ct_pool_entry ct_pool[GUC_CT_POOL_SIZE];
} __packed;

static u32 guc_ads_private_data_size(struct intel_guc *guc)
{
	return PAGE_ALIGN(guc->fw.private_data_size);
}

static u32 guc_ads_private_data_offset(struct intel_guc *guc)
{
	return PAGE_ALIGN(sizeof(struct __guc_ads_blob));
}

static u32 guc_ads_blob_size(struct intel_guc *guc)
{
	return guc_ads_private_data_offset(guc) +
	       guc_ads_private_data_size(guc);
}

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

static void guc_mapping_table_init(struct intel_gt *gt,
				   struct guc_gt_system_info *system_info)
{
	unsigned int i, j;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/* Table must be set to invalid values for entries not used */
	for (i = 0; i < GUC_MAX_ENGINE_CLASSES; ++i)
		for (j = 0; j < GUC_MAX_INSTANCES_PER_CLASS; ++j)
			system_info->mapping_table[i][j] =
				GUC_MAX_INSTANCES_PER_CLASS;

	for_each_engine(engine, gt, id) {
		u8 guc_class = engine->class;

		system_info->mapping_table[guc_class][engine->instance] =
			engine->instance;
	}
}

/*
 * The first 80 dwords of the register state context, containing the
 * execlists and ppgtt registers.
 */
#define LR_HW_CONTEXT_SIZE	(80 * sizeof(u32))

static void __guc_ads_init(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct drm_i915_private *i915 = gt->i915;
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
	blob->system_info.engine_enabled_masks[RENDER_CLASS] = 1;
	blob->system_info.engine_enabled_masks[COPY_ENGINE_CLASS] = 1;
	blob->system_info.engine_enabled_masks[VIDEO_DECODE_CLASS] = VDBOX_MASK(gt);
	blob->system_info.engine_enabled_masks[VIDEO_ENHANCEMENT_CLASS] = VEBOX_MASK(gt);

	blob->system_info.generic_gt_sysinfo[GUC_GENERIC_GT_SYSINFO_SLICE_ENABLED] =
		hweight8(gt->info.sseu.slice_mask);
	blob->system_info.generic_gt_sysinfo[GUC_GENERIC_GT_SYSINFO_VDBOX_SFC_SUPPORT_MASK] =
		gt->info.vdbox_sfc_access;

	if (INTEL_GEN(i915) >= 12 && !IS_DGFX(i915)) {
		u32 distdbreg = intel_uncore_read(gt->uncore,
						  GEN12_DIST_DBS_POPULATED);
		blob->system_info.generic_gt_sysinfo[GUC_GENERIC_GT_SYSINFO_DOORBELL_COUNT_PER_SQIDI] =
			((distdbreg >> GEN12_DOORBELLS_PER_SQIDI_SHIFT) &
			 GEN12_DOORBELLS_PER_SQIDI) + 1;
	}

	guc_mapping_table_init(guc_to_gt(guc), &blob->system_info);

	base = intel_guc_ggtt_offset(guc, guc->ads_vma);

	/* Clients info  */
	guc_ct_pool_entries_init(blob->ct_pool, ARRAY_SIZE(blob->ct_pool));

	blob->clients_info.clients_num = 1;
	blob->clients_info.ct_pool_addr = base + ptr_offset(blob, ct_pool);
	blob->clients_info.ct_pool_count = ARRAY_SIZE(blob->ct_pool);

	/* ADS */
	blob->ads.scheduler_policies = base + ptr_offset(blob, policies);
	blob->ads.gt_system_info = base + ptr_offset(blob, system_info);
	blob->ads.clients_info = base + ptr_offset(blob, clients_info);

	/* Private Data */
	blob->ads.private_data = base + guc_ads_private_data_offset(guc);

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
	u32 size;
	int ret;

	GEM_BUG_ON(guc->ads_vma);

	size = guc_ads_blob_size(guc);

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
	guc->ads_blob = NULL;
}

static void guc_ads_private_data_reset(struct intel_guc *guc)
{
	u32 size;

	size = guc_ads_private_data_size(guc);
	if (!size)
		return;

	memset((void *)guc->ads_blob + guc_ads_private_data_offset(guc), 0,
	       size);
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

	guc_ads_private_data_reset(guc);
}

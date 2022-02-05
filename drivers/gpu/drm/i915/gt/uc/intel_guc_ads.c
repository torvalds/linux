// SPDX-License-Identifier: MIT
/*
 * Copyright © 2014-2019 Intel Corporation
 */

#include <linux/bsearch.h>

#include "gt/intel_gt.h"
#include "gt/intel_lrc.h"
#include "gt/shmem_utils.h"
#include "intel_guc_ads.h"
#include "intel_guc_fwif.h"
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
 *      | guc_engine_usage                      |
 *      +---------------------------------------+ <== static
 *      | guc_mmio_reg[countA] (engine 0.0)     |
 *      | guc_mmio_reg[countB] (engine 0.1)     |
 *      | guc_mmio_reg[countC] (engine 1.0)     |
 *      |   ...                                 |
 *      +---------------------------------------+ <== dynamic
 *      | padding                               |
 *      +---------------------------------------+ <== 4K aligned
 *      | golden contexts                       |
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
	struct guc_engine_usage engine_usage;
	/* From here on, location is dynamic! Refer to above diagram. */
	struct guc_mmio_reg regset[0];
} __packed;

static u32 guc_ads_regset_size(struct intel_guc *guc)
{
	GEM_BUG_ON(!guc->ads_regset_size);
	return guc->ads_regset_size;
}

static u32 guc_ads_golden_ctxt_size(struct intel_guc *guc)
{
	return PAGE_ALIGN(guc->ads_golden_ctxt_size);
}

static u32 guc_ads_private_data_size(struct intel_guc *guc)
{
	return PAGE_ALIGN(guc->fw.private_data_size);
}

static u32 guc_ads_regset_offset(struct intel_guc *guc)
{
	return offsetof(struct __guc_ads_blob, regset);
}

static u32 guc_ads_golden_ctxt_offset(struct intel_guc *guc)
{
	u32 offset;

	offset = guc_ads_regset_offset(guc) +
		 guc_ads_regset_size(guc);

	return PAGE_ALIGN(offset);
}

static u32 guc_ads_private_data_offset(struct intel_guc *guc)
{
	u32 offset;

	offset = guc_ads_golden_ctxt_offset(guc) +
		 guc_ads_golden_ctxt_size(guc);

	return PAGE_ALIGN(offset);
}

static u32 guc_ads_blob_size(struct intel_guc *guc)
{
	return guc_ads_private_data_offset(guc) +
	       guc_ads_private_data_size(guc);
}

static void guc_policies_init(struct intel_guc *guc, struct guc_policies *policies)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct drm_i915_private *i915 = gt->i915;

	policies->dpc_promote_time = GLOBAL_POLICY_DEFAULT_DPC_PROMOTE_TIME_US;
	policies->max_num_work_items = GLOBAL_POLICY_MAX_NUM_WI;

	policies->global_flags = 0;
	if (i915->params.reset < 2)
		policies->global_flags |= GLOBAL_POLICY_DISABLE_ENGINE_RESET;

	policies->is_valid = 1;
}

void intel_guc_ads_print_policy_info(struct intel_guc *guc,
				     struct drm_printer *dp)
{
	struct __guc_ads_blob *blob = guc->ads_blob;

	if (unlikely(!blob))
		return;

	drm_printf(dp, "Global scheduling policies:\n");
	drm_printf(dp, "  DPC promote time   = %u\n", blob->policies.dpc_promote_time);
	drm_printf(dp, "  Max num work items = %u\n", blob->policies.max_num_work_items);
	drm_printf(dp, "  Flags              = %u\n", blob->policies.global_flags);
}

static int guc_action_policies_update(struct intel_guc *guc, u32 policy_offset)
{
	u32 action[] = {
		INTEL_GUC_ACTION_GLOBAL_SCHED_POLICY_CHANGE,
		policy_offset
	};

	return intel_guc_send_busy_loop(guc, action, ARRAY_SIZE(action), 0, true);
}

int intel_guc_global_policies_update(struct intel_guc *guc)
{
	struct __guc_ads_blob *blob = guc->ads_blob;
	struct intel_gt *gt = guc_to_gt(guc);
	intel_wakeref_t wakeref;
	int ret;

	if (!blob)
		return -EOPNOTSUPP;

	GEM_BUG_ON(!blob->ads.scheduler_policies);

	guc_policies_init(guc, &blob->policies);

	if (!intel_guc_is_ready(guc))
		return 0;

	with_intel_runtime_pm(&gt->i915->runtime_pm, wakeref)
		ret = guc_action_policies_update(guc, blob->ads.scheduler_policies);

	return ret;
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
		u8 guc_class = engine_class_to_guc_class(engine->class);

		system_info->mapping_table[guc_class][ilog2(engine->logical_mask)] =
			engine->instance;
	}
}

/*
 * The save/restore register list must be pre-calculated to a temporary
 * buffer of driver defined size before it can be generated in place
 * inside the ADS.
 */
#define MAX_MMIO_REGS	128	/* Arbitrary size, increase as needed */
struct temp_regset {
	struct guc_mmio_reg *registers;
	u32 used;
	u32 size;
};

static int guc_mmio_reg_cmp(const void *a, const void *b)
{
	const struct guc_mmio_reg *ra = a;
	const struct guc_mmio_reg *rb = b;

	return (int)ra->offset - (int)rb->offset;
}

static void guc_mmio_reg_add(struct temp_regset *regset,
			     u32 offset, u32 flags)
{
	u32 count = regset->used;
	struct guc_mmio_reg reg = {
		.offset = offset,
		.flags = flags,
	};
	struct guc_mmio_reg *slot;

	GEM_BUG_ON(count >= regset->size);

	/*
	 * The mmio list is built using separate lists within the driver.
	 * It's possible that at some point we may attempt to add the same
	 * register more than once. Do not consider this an error; silently
	 * move on if the register is already in the list.
	 */
	if (bsearch(&reg, regset->registers, count,
		    sizeof(reg), guc_mmio_reg_cmp))
		return;

	slot = &regset->registers[count];
	regset->used++;
	*slot = reg;

	while (slot-- > regset->registers) {
		GEM_BUG_ON(slot[0].offset == slot[1].offset);
		if (slot[1].offset > slot[0].offset)
			break;

		swap(slot[1], slot[0]);
	}
}

#define GUC_MMIO_REG_ADD(regset, reg, masked) \
	guc_mmio_reg_add(regset, \
			 i915_mmio_reg_offset((reg)), \
			 (masked) ? GUC_REGSET_MASKED : 0)

static void guc_mmio_regset_init(struct temp_regset *regset,
				 struct intel_engine_cs *engine)
{
	const u32 base = engine->mmio_base;
	struct i915_wa_list *wal = &engine->wa_list;
	struct i915_wa *wa;
	unsigned int i;

	regset->used = 0;

	GUC_MMIO_REG_ADD(regset, RING_MODE_GEN7(base), true);
	GUC_MMIO_REG_ADD(regset, RING_HWS_PGA(base), false);
	GUC_MMIO_REG_ADD(regset, RING_IMR(base), false);

	for (i = 0, wa = wal->list; i < wal->count; i++, wa++)
		GUC_MMIO_REG_ADD(regset, wa->reg, wa->masked_reg);

	/* Be extra paranoid and include all whitelist registers. */
	for (i = 0; i < RING_MAX_NONPRIV_SLOTS; i++)
		GUC_MMIO_REG_ADD(regset,
				 RING_FORCE_TO_NONPRIV(base, i),
				 false);

	/* add in local MOCS registers */
	for (i = 0; i < GEN9_LNCFCMOCS_REG_COUNT; i++)
		GUC_MMIO_REG_ADD(regset, GEN9_LNCFCMOCS(i), false);
}

static int guc_mmio_reg_state_query(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct temp_regset temp_set;
	u32 total;

	/*
	 * Need to actually build the list in order to filter out
	 * duplicates and other such data dependent constructions.
	 */
	temp_set.size = MAX_MMIO_REGS;
	temp_set.registers = kmalloc_array(temp_set.size,
					   sizeof(*temp_set.registers),
					   GFP_KERNEL);
	if (!temp_set.registers)
		return -ENOMEM;

	total = 0;
	for_each_engine(engine, gt, id) {
		guc_mmio_regset_init(&temp_set, engine);
		total += temp_set.used;
	}

	kfree(temp_set.registers);

	return total * sizeof(struct guc_mmio_reg);
}

static void guc_mmio_reg_state_init(struct intel_guc *guc,
				    struct __guc_ads_blob *blob)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct temp_regset temp_set;
	struct guc_mmio_reg_set *ads_reg_set;
	u32 addr_ggtt, offset;
	u8 guc_class;

	offset = guc_ads_regset_offset(guc);
	addr_ggtt = intel_guc_ggtt_offset(guc, guc->ads_vma) + offset;
	temp_set.registers = (struct guc_mmio_reg *)(((u8 *)blob) + offset);
	temp_set.size = guc->ads_regset_size / sizeof(temp_set.registers[0]);

	for_each_engine(engine, gt, id) {
		/* Class index is checked in class converter */
		GEM_BUG_ON(engine->instance >= GUC_MAX_INSTANCES_PER_CLASS);

		guc_class = engine_class_to_guc_class(engine->class);
		ads_reg_set = &blob->ads.reg_state_list[guc_class][engine->instance];

		guc_mmio_regset_init(&temp_set, engine);
		if (!temp_set.used) {
			ads_reg_set->address = 0;
			ads_reg_set->count = 0;
			continue;
		}

		ads_reg_set->address = addr_ggtt;
		ads_reg_set->count = temp_set.used;

		temp_set.size -= temp_set.used;
		temp_set.registers += temp_set.used;
		addr_ggtt += temp_set.used * sizeof(struct guc_mmio_reg);
	}

	GEM_BUG_ON(temp_set.size);
}

static void fill_engine_enable_masks(struct intel_gt *gt,
				     struct guc_gt_system_info *info)
{
	info->engine_enabled_masks[GUC_RENDER_CLASS] = 1;
	info->engine_enabled_masks[GUC_BLITTER_CLASS] = 1;
	info->engine_enabled_masks[GUC_VIDEO_CLASS] = VDBOX_MASK(gt);
	info->engine_enabled_masks[GUC_VIDEOENHANCE_CLASS] = VEBOX_MASK(gt);
}

#define LR_HW_CONTEXT_SIZE (80 * sizeof(u32))
#define LRC_SKIP_SIZE (LRC_PPHWSP_SZ * PAGE_SIZE + LR_HW_CONTEXT_SIZE)
static int guc_prep_golden_context(struct intel_guc *guc,
				   struct __guc_ads_blob *blob)
{
	struct intel_gt *gt = guc_to_gt(guc);
	u32 addr_ggtt, offset;
	u32 total_size = 0, alloc_size, real_size;
	u8 engine_class, guc_class;
	struct guc_gt_system_info *info, local_info;

	/*
	 * Reserve the memory for the golden contexts and point GuC at it but
	 * leave it empty for now. The context data will be filled in later
	 * once there is something available to put there.
	 *
	 * Note that the HWSP and ring context are not included.
	 *
	 * Note also that the storage must be pinned in the GGTT, so that the
	 * address won't change after GuC has been told where to find it. The
	 * GuC will also validate that the LRC base + size fall within the
	 * allowed GGTT range.
	 */
	if (blob) {
		offset = guc_ads_golden_ctxt_offset(guc);
		addr_ggtt = intel_guc_ggtt_offset(guc, guc->ads_vma) + offset;
		info = &blob->system_info;
	} else {
		memset(&local_info, 0, sizeof(local_info));
		info = &local_info;
		fill_engine_enable_masks(gt, info);
	}

	for (engine_class = 0; engine_class <= MAX_ENGINE_CLASS; ++engine_class) {
		if (engine_class == OTHER_CLASS)
			continue;

		guc_class = engine_class_to_guc_class(engine_class);

		if (!info->engine_enabled_masks[guc_class])
			continue;

		real_size = intel_engine_context_size(gt, engine_class);
		alloc_size = PAGE_ALIGN(real_size);
		total_size += alloc_size;

		if (!blob)
			continue;

		/*
		 * This interface is slightly confusing. We need to pass the
		 * base address of the full golden context and the size of just
		 * the engine state, which is the section of the context image
		 * that starts after the execlists context. This is required to
		 * allow the GuC to restore just the engine state when a
		 * watchdog reset occurs.
		 * We calculate the engine state size by removing the size of
		 * what comes before it in the context image (which is identical
		 * on all engines).
		 */
		blob->ads.eng_state_size[guc_class] = real_size - LRC_SKIP_SIZE;
		blob->ads.golden_context_lrca[guc_class] = addr_ggtt;
		addr_ggtt += alloc_size;
	}

	if (!blob)
		return total_size;

	GEM_BUG_ON(guc->ads_golden_ctxt_size != total_size);
	return total_size;
}

static struct intel_engine_cs *find_engine_state(struct intel_gt *gt, u8 engine_class)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, gt, id) {
		if (engine->class != engine_class)
			continue;

		if (!engine->default_state)
			continue;

		return engine;
	}

	return NULL;
}

static void guc_init_golden_context(struct intel_guc *guc)
{
	struct __guc_ads_blob *blob = guc->ads_blob;
	struct intel_engine_cs *engine;
	struct intel_gt *gt = guc_to_gt(guc);
	u32 addr_ggtt, offset;
	u32 total_size = 0, alloc_size, real_size;
	u8 engine_class, guc_class;
	u8 *ptr;

	if (!intel_uc_uses_guc_submission(&gt->uc))
		return;

	GEM_BUG_ON(!blob);

	/*
	 * Go back and fill in the golden context data now that it is
	 * available.
	 */
	offset = guc_ads_golden_ctxt_offset(guc);
	addr_ggtt = intel_guc_ggtt_offset(guc, guc->ads_vma) + offset;
	ptr = ((u8 *)blob) + offset;

	for (engine_class = 0; engine_class <= MAX_ENGINE_CLASS; ++engine_class) {
		if (engine_class == OTHER_CLASS)
			continue;

		guc_class = engine_class_to_guc_class(engine_class);

		if (!blob->system_info.engine_enabled_masks[guc_class])
			continue;

		real_size = intel_engine_context_size(gt, engine_class);
		alloc_size = PAGE_ALIGN(real_size);
		total_size += alloc_size;

		engine = find_engine_state(gt, engine_class);
		if (!engine) {
			drm_err(&gt->i915->drm, "No engine state recorded for class %d!\n",
				engine_class);
			blob->ads.eng_state_size[guc_class] = 0;
			blob->ads.golden_context_lrca[guc_class] = 0;
			continue;
		}

		GEM_BUG_ON(blob->ads.eng_state_size[guc_class] !=
			   real_size - LRC_SKIP_SIZE);
		GEM_BUG_ON(blob->ads.golden_context_lrca[guc_class] != addr_ggtt);
		addr_ggtt += alloc_size;

		shmem_read(engine->default_state, 0, ptr, real_size);
		ptr += alloc_size;
	}

	GEM_BUG_ON(guc->ads_golden_ctxt_size != total_size);
}

static void __guc_ads_init(struct intel_guc *guc)
{
	struct intel_gt *gt = guc_to_gt(guc);
	struct drm_i915_private *i915 = gt->i915;
	struct __guc_ads_blob *blob = guc->ads_blob;
	u32 base;

	/* GuC scheduling policies */
	guc_policies_init(guc, &blob->policies);

	/* System info */
	fill_engine_enable_masks(gt, &blob->system_info);

	blob->system_info.generic_gt_sysinfo[GUC_GENERIC_GT_SYSINFO_SLICE_ENABLED] =
		hweight8(gt->info.sseu.slice_mask);
	blob->system_info.generic_gt_sysinfo[GUC_GENERIC_GT_SYSINFO_VDBOX_SFC_SUPPORT_MASK] =
		gt->info.vdbox_sfc_access;

	if (GRAPHICS_VER(i915) >= 12 && !IS_DGFX(i915)) {
		u32 distdbreg = intel_uncore_read(gt->uncore,
						  GEN12_DIST_DBS_POPULATED);
		blob->system_info.generic_gt_sysinfo[GUC_GENERIC_GT_SYSINFO_DOORBELL_COUNT_PER_SQIDI] =
			((distdbreg >> GEN12_DOORBELLS_PER_SQIDI_SHIFT) &
			 GEN12_DOORBELLS_PER_SQIDI) + 1;
	}

	/* Golden contexts for re-initialising after a watchdog reset */
	guc_prep_golden_context(guc, blob);

	guc_mapping_table_init(guc_to_gt(guc), &blob->system_info);

	base = intel_guc_ggtt_offset(guc, guc->ads_vma);

	/* ADS */
	blob->ads.scheduler_policies = base + ptr_offset(blob, policies);
	blob->ads.gt_system_info = base + ptr_offset(blob, system_info);

	/* MMIO save/restore list */
	guc_mmio_reg_state_init(guc, blob);

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

	/* Need to calculate the reg state size dynamically: */
	ret = guc_mmio_reg_state_query(guc);
	if (ret < 0)
		return ret;
	guc->ads_regset_size = ret;

	/* Likewise the golden contexts: */
	ret = guc_prep_golden_context(guc, NULL);
	if (ret < 0)
		return ret;
	guc->ads_golden_ctxt_size = ret;

	/* Now the total size can be determined: */
	size = guc_ads_blob_size(guc);

	ret = intel_guc_allocate_and_map_vma(guc, size, &guc->ads_vma,
					     (void **)&guc->ads_blob);
	if (ret)
		return ret;

	__guc_ads_init(guc);

	return 0;
}

void intel_guc_ads_init_late(struct intel_guc *guc)
{
	/*
	 * The golden context setup requires the saved engine state from
	 * __engines_record_defaults(). However, that requires engines to be
	 * operational which means the ADS must already have been configured.
	 * Fortunately, the golden context state is not needed until a hang
	 * occurs, so it can be filled in during this late init phase.
	 */
	guc_init_golden_context(guc);
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

u32 intel_guc_engine_usage_offset(struct intel_guc *guc)
{
	struct __guc_ads_blob *blob = guc->ads_blob;
	u32 base = intel_guc_ggtt_offset(guc, guc->ads_vma);
	u32 offset = base + ptr_offset(blob, engine_usage);

	return offset;
}

struct guc_engine_usage_record *intel_guc_engine_usage(struct intel_engine_cs *engine)
{
	struct intel_guc *guc = &engine->gt->uc.guc;
	struct __guc_ads_blob *blob = guc->ads_blob;
	u8 guc_class = engine_class_to_guc_class(engine->class);

	return &blob->engine_usage.engines[guc_class][ilog2(engine->logical_mask)];
}

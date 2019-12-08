// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_gt.h"
#include "intel_gt_pm.h"
#include "intel_gt_requests.h"
#include "intel_mocs.h"
#include "intel_rc6.h"
#include "intel_rps.h"
#include "intel_uncore.h"
#include "intel_pm.h"

void intel_gt_init_early(struct intel_gt *gt, struct drm_i915_private *i915)
{
	gt->i915 = i915;
	gt->uncore = &i915->uncore;

	spin_lock_init(&gt->irq_lock);

	INIT_LIST_HEAD(&gt->closed_vma);
	spin_lock_init(&gt->closed_lock);

	intel_gt_init_reset(gt);
	intel_gt_init_requests(gt);
	intel_gt_pm_init_early(gt);

	intel_rps_init_early(&gt->rps);
	intel_uc_init_early(&gt->uc);
}

void intel_gt_init_hw_early(struct intel_gt *gt, struct i915_ggtt *ggtt)
{
	gt->ggtt = ggtt;

	intel_gt_sanitize(gt, false);
}

static void init_unused_ring(struct intel_gt *gt, u32 base)
{
	struct intel_uncore *uncore = gt->uncore;

	intel_uncore_write(uncore, RING_CTL(base), 0);
	intel_uncore_write(uncore, RING_HEAD(base), 0);
	intel_uncore_write(uncore, RING_TAIL(base), 0);
	intel_uncore_write(uncore, RING_START(base), 0);
}

static void init_unused_rings(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;

	if (IS_I830(i915)) {
		init_unused_ring(gt, PRB1_BASE);
		init_unused_ring(gt, SRB0_BASE);
		init_unused_ring(gt, SRB1_BASE);
		init_unused_ring(gt, SRB2_BASE);
		init_unused_ring(gt, SRB3_BASE);
	} else if (IS_GEN(i915, 2)) {
		init_unused_ring(gt, SRB0_BASE);
		init_unused_ring(gt, SRB1_BASE);
	} else if (IS_GEN(i915, 3)) {
		init_unused_ring(gt, PRB1_BASE);
		init_unused_ring(gt, PRB2_BASE);
	}
}

int intel_gt_init_hw(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	struct intel_uncore *uncore = gt->uncore;
	int ret;

	BUG_ON(!i915->kernel_context);
	ret = intel_gt_terminally_wedged(gt);
	if (ret)
		return ret;

	gt->last_init_time = ktime_get();

	/* Double layer security blanket, see i915_gem_init() */
	intel_uncore_forcewake_get(uncore, FORCEWAKE_ALL);

	if (HAS_EDRAM(i915) && INTEL_GEN(i915) < 9)
		intel_uncore_rmw(uncore, HSW_IDICR, 0, IDIHASHMSK(0xf));

	if (IS_HASWELL(i915))
		intel_uncore_write(uncore,
				   MI_PREDICATE_RESULT_2,
				   IS_HSW_GT3(i915) ?
				   LOWER_SLICE_ENABLED : LOWER_SLICE_DISABLED);

	/* Apply the GT workarounds... */
	intel_gt_apply_workarounds(gt);
	/* ...and determine whether they are sticking. */
	intel_gt_verify_workarounds(gt, "init");

	intel_gt_init_swizzling(gt);

	/*
	 * At least 830 can leave some of the unused rings
	 * "active" (ie. head != tail) after resume which
	 * will prevent c3 entry. Makes sure all unused rings
	 * are totally idle.
	 */
	init_unused_rings(gt);

	ret = i915_ppgtt_init_hw(gt);
	if (ret) {
		DRM_ERROR("Enabling PPGTT failed (%d)\n", ret);
		goto out;
	}

	/* We can't enable contexts until all firmware is loaded */
	ret = intel_uc_init_hw(&gt->uc);
	if (ret) {
		i915_probe_error(i915, "Enabling uc failed (%d)\n", ret);
		goto out;
	}

	intel_mocs_init(gt);

out:
	intel_uncore_forcewake_put(uncore, FORCEWAKE_ALL);
	return ret;
}

static void rmw_set(struct intel_uncore *uncore, i915_reg_t reg, u32 set)
{
	intel_uncore_rmw(uncore, reg, 0, set);
}

static void rmw_clear(struct intel_uncore *uncore, i915_reg_t reg, u32 clr)
{
	intel_uncore_rmw(uncore, reg, clr, 0);
}

static void clear_register(struct intel_uncore *uncore, i915_reg_t reg)
{
	intel_uncore_rmw(uncore, reg, 0, 0);
}

static void gen8_clear_engine_error_register(struct intel_engine_cs *engine)
{
	GEN6_RING_FAULT_REG_RMW(engine, RING_FAULT_VALID, 0);
	GEN6_RING_FAULT_REG_POSTING_READ(engine);
}

void
intel_gt_clear_error_registers(struct intel_gt *gt,
			       intel_engine_mask_t engine_mask)
{
	struct drm_i915_private *i915 = gt->i915;
	struct intel_uncore *uncore = gt->uncore;
	u32 eir;

	if (!IS_GEN(i915, 2))
		clear_register(uncore, PGTBL_ER);

	if (INTEL_GEN(i915) < 4)
		clear_register(uncore, IPEIR(RENDER_RING_BASE));
	else
		clear_register(uncore, IPEIR_I965);

	clear_register(uncore, EIR);
	eir = intel_uncore_read(uncore, EIR);
	if (eir) {
		/*
		 * some errors might have become stuck,
		 * mask them.
		 */
		DRM_DEBUG_DRIVER("EIR stuck: 0x%08x, masking\n", eir);
		rmw_set(uncore, EMR, eir);
		intel_uncore_write(uncore, GEN2_IIR,
				   I915_MASTER_ERROR_INTERRUPT);
	}

	if (INTEL_GEN(i915) >= 12) {
		rmw_clear(uncore, GEN12_RING_FAULT_REG, RING_FAULT_VALID);
		intel_uncore_posting_read(uncore, GEN12_RING_FAULT_REG);
	} else if (INTEL_GEN(i915) >= 8) {
		rmw_clear(uncore, GEN8_RING_FAULT_REG, RING_FAULT_VALID);
		intel_uncore_posting_read(uncore, GEN8_RING_FAULT_REG);
	} else if (INTEL_GEN(i915) >= 6) {
		struct intel_engine_cs *engine;
		enum intel_engine_id id;

		for_each_engine_masked(engine, gt, engine_mask, id)
			gen8_clear_engine_error_register(engine);
	}
}

static void gen6_check_faults(struct intel_gt *gt)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	u32 fault;

	for_each_engine(engine, gt, id) {
		fault = GEN6_RING_FAULT_REG_READ(engine);
		if (fault & RING_FAULT_VALID) {
			DRM_DEBUG_DRIVER("Unexpected fault\n"
					 "\tAddr: 0x%08lx\n"
					 "\tAddress space: %s\n"
					 "\tSource ID: %d\n"
					 "\tType: %d\n",
					 fault & PAGE_MASK,
					 fault & RING_FAULT_GTTSEL_MASK ?
					 "GGTT" : "PPGTT",
					 RING_FAULT_SRCID(fault),
					 RING_FAULT_FAULT_TYPE(fault));
		}
	}
}

static void gen8_check_faults(struct intel_gt *gt)
{
	struct intel_uncore *uncore = gt->uncore;
	i915_reg_t fault_reg, fault_data0_reg, fault_data1_reg;
	u32 fault;

	if (INTEL_GEN(gt->i915) >= 12) {
		fault_reg = GEN12_RING_FAULT_REG;
		fault_data0_reg = GEN12_FAULT_TLB_DATA0;
		fault_data1_reg = GEN12_FAULT_TLB_DATA1;
	} else {
		fault_reg = GEN8_RING_FAULT_REG;
		fault_data0_reg = GEN8_FAULT_TLB_DATA0;
		fault_data1_reg = GEN8_FAULT_TLB_DATA1;
	}

	fault = intel_uncore_read(uncore, fault_reg);
	if (fault & RING_FAULT_VALID) {
		u32 fault_data0, fault_data1;
		u64 fault_addr;

		fault_data0 = intel_uncore_read(uncore, fault_data0_reg);
		fault_data1 = intel_uncore_read(uncore, fault_data1_reg);

		fault_addr = ((u64)(fault_data1 & FAULT_VA_HIGH_BITS) << 44) |
			     ((u64)fault_data0 << 12);

		DRM_DEBUG_DRIVER("Unexpected fault\n"
				 "\tAddr: 0x%08x_%08x\n"
				 "\tAddress space: %s\n"
				 "\tEngine ID: %d\n"
				 "\tSource ID: %d\n"
				 "\tType: %d\n",
				 upper_32_bits(fault_addr),
				 lower_32_bits(fault_addr),
				 fault_data1 & FAULT_GTT_SEL ? "GGTT" : "PPGTT",
				 GEN8_RING_FAULT_ENGINE_ID(fault),
				 RING_FAULT_SRCID(fault),
				 RING_FAULT_FAULT_TYPE(fault));
	}
}

void intel_gt_check_and_clear_faults(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;

	/* From GEN8 onwards we only have one 'All Engine Fault Register' */
	if (INTEL_GEN(i915) >= 8)
		gen8_check_faults(gt);
	else if (INTEL_GEN(i915) >= 6)
		gen6_check_faults(gt);
	else
		return;

	intel_gt_clear_error_registers(gt, ALL_ENGINES);
}

void intel_gt_flush_ggtt_writes(struct intel_gt *gt)
{
	struct intel_uncore *uncore = gt->uncore;
	intel_wakeref_t wakeref;

	/*
	 * No actual flushing is required for the GTT write domain for reads
	 * from the GTT domain. Writes to it "immediately" go to main memory
	 * as far as we know, so there's no chipset flush. It also doesn't
	 * land in the GPU render cache.
	 *
	 * However, we do have to enforce the order so that all writes through
	 * the GTT land before any writes to the device, such as updates to
	 * the GATT itself.
	 *
	 * We also have to wait a bit for the writes to land from the GTT.
	 * An uncached read (i.e. mmio) seems to be ideal for the round-trip
	 * timing. This issue has only been observed when switching quickly
	 * between GTT writes and CPU reads from inside the kernel on recent hw,
	 * and it appears to only affect discrete GTT blocks (i.e. on LLC
	 * system agents we cannot reproduce this behaviour, until Cannonlake
	 * that was!).
	 */

	wmb();

	if (INTEL_INFO(gt->i915)->has_coherent_ggtt)
		return;

	intel_gt_chipset_flush(gt);

	with_intel_runtime_pm(uncore->rpm, wakeref) {
		unsigned long flags;

		spin_lock_irqsave(&uncore->lock, flags);
		intel_uncore_posting_read_fw(uncore,
					     RING_HEAD(RENDER_RING_BASE));
		spin_unlock_irqrestore(&uncore->lock, flags);
	}
}

void intel_gt_chipset_flush(struct intel_gt *gt)
{
	wmb();
	if (INTEL_GEN(gt->i915) < 6)
		intel_gtt_chipset_flush();
}

void intel_gt_driver_register(struct intel_gt *gt)
{
	intel_rps_driver_register(&gt->rps);
}

static int intel_gt_init_scratch(struct intel_gt *gt, unsigned int size)
{
	struct drm_i915_private *i915 = gt->i915;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int ret;

	obj = i915_gem_object_create_stolen(i915, size);
	if (IS_ERR(obj))
		obj = i915_gem_object_create_internal(i915, size);
	if (IS_ERR(obj)) {
		DRM_ERROR("Failed to allocate scratch page\n");
		return PTR_ERR(obj);
	}

	vma = i915_vma_instance(obj, &gt->ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto err_unref;
	}

	ret = i915_vma_pin(vma, 0, 0, PIN_GLOBAL | PIN_HIGH);
	if (ret)
		goto err_unref;

	gt->scratch = i915_vma_make_unshrinkable(vma);

	return 0;

err_unref:
	i915_gem_object_put(obj);
	return ret;
}

static void intel_gt_fini_scratch(struct intel_gt *gt)
{
	i915_vma_unpin_and_release(&gt->scratch, 0);
}

int intel_gt_init(struct intel_gt *gt)
{
	int err;

	err = intel_gt_init_scratch(gt, IS_GEN(gt->i915, 2) ? SZ_256K : SZ_4K);
	if (err)
		return err;

	intel_gt_pm_init(gt);

	return 0;
}

void intel_gt_driver_remove(struct intel_gt *gt)
{
	GEM_BUG_ON(gt->awake);
}

void intel_gt_driver_unregister(struct intel_gt *gt)
{
	intel_rps_driver_unregister(&gt->rps);
}

void intel_gt_driver_release(struct intel_gt *gt)
{
	intel_gt_pm_fini(gt);
	intel_gt_fini_scratch(gt);
}

void intel_gt_driver_late_release(struct intel_gt *gt)
{
	intel_uc_driver_late_release(&gt->uc);
	intel_gt_fini_reset(gt);
}

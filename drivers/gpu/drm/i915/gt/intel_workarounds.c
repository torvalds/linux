/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2018 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_context.h"
#include "intel_engine_pm.h"
#include "intel_gpu_commands.h"
#include "intel_gt.h"
#include "intel_ring.h"
#include "intel_workarounds.h"

/**
 * DOC: Hardware workarounds
 *
 * This file is intended as a central place to implement most [1]_ of the
 * required workarounds for hardware to work as originally intended. They fall
 * in five basic categories depending on how/when they are applied:
 *
 * - Workarounds that touch registers that are saved/restored to/from the HW
 *   context image. The list is emitted (via Load Register Immediate commands)
 *   everytime a new context is created.
 * - GT workarounds. The list of these WAs is applied whenever these registers
 *   revert to default values (on GPU reset, suspend/resume [2]_, etc..).
 * - Display workarounds. The list is applied during display clock-gating
 *   initialization.
 * - Workarounds that whitelist a privileged register, so that UMDs can manage
 *   them directly. This is just a special case of a MMMIO workaround (as we
 *   write the list of these to/be-whitelisted registers to some special HW
 *   registers).
 * - Workaround batchbuffers, that get executed automatically by the hardware
 *   on every HW context restore.
 *
 * .. [1] Please notice that there are other WAs that, due to their nature,
 *    cannot be applied from a central place. Those are peppered around the rest
 *    of the code, as needed.
 *
 * .. [2] Technically, some registers are powercontext saved & restored, so they
 *    survive a suspend/resume. In practice, writing them again is not too
 *    costly and simplifies things. We can revisit this in the future.
 *
 * Layout
 * ~~~~~~
 *
 * Keep things in this file ordered by WA type, as per the above (context, GT,
 * display, register whitelist, batchbuffer). Then, inside each type, keep the
 * following order:
 *
 * - Infrastructure functions and macros
 * - WAs per platform in standard gen/chrono order
 * - Public functions to init or apply the given workaround type.
 */

/*
 * KBL revision ID ordering is bizarre; higher revision ID's map to lower
 * steppings in some cases.  So rather than test against the revision ID
 * directly, let's map that into our own range of increasing ID's that we
 * can test against in a regular manner.
 */

const struct i915_rev_steppings kbl_revids[] = {
	[0] = { .gt_stepping = KBL_REVID_A0, .disp_stepping = KBL_REVID_A0 },
	[1] = { .gt_stepping = KBL_REVID_B0, .disp_stepping = KBL_REVID_B0 },
	[2] = { .gt_stepping = KBL_REVID_C0, .disp_stepping = KBL_REVID_B0 },
	[3] = { .gt_stepping = KBL_REVID_D0, .disp_stepping = KBL_REVID_B0 },
	[4] = { .gt_stepping = KBL_REVID_F0, .disp_stepping = KBL_REVID_C0 },
	[5] = { .gt_stepping = KBL_REVID_C0, .disp_stepping = KBL_REVID_B1 },
	[6] = { .gt_stepping = KBL_REVID_D1, .disp_stepping = KBL_REVID_B1 },
	[7] = { .gt_stepping = KBL_REVID_G0, .disp_stepping = KBL_REVID_C0 },
};

const struct i915_rev_steppings tgl_uy_revids[] = {
	[0] = { .gt_stepping = TGL_REVID_A0, .disp_stepping = TGL_REVID_A0 },
	[1] = { .gt_stepping = TGL_REVID_B0, .disp_stepping = TGL_REVID_C0 },
	[2] = { .gt_stepping = TGL_REVID_B1, .disp_stepping = TGL_REVID_C0 },
	[3] = { .gt_stepping = TGL_REVID_C0, .disp_stepping = TGL_REVID_D0 },
};

/* Same GT stepping between tgl_uy_revids and tgl_revids don't mean the same HW */
const struct i915_rev_steppings tgl_revids[] = {
	[0] = { .gt_stepping = TGL_REVID_A0, .disp_stepping = TGL_REVID_B0 },
	[1] = { .gt_stepping = TGL_REVID_B0, .disp_stepping = TGL_REVID_D0 },
};

static void wa_init_start(struct i915_wa_list *wal, const char *name, const char *engine_name)
{
	wal->name = name;
	wal->engine_name = engine_name;
}

#define WA_LIST_CHUNK (1 << 4)

static void wa_init_finish(struct i915_wa_list *wal)
{
	/* Trim unused entries. */
	if (!IS_ALIGNED(wal->count, WA_LIST_CHUNK)) {
		struct i915_wa *list = kmemdup(wal->list,
					       wal->count * sizeof(*list),
					       GFP_KERNEL);

		if (list) {
			kfree(wal->list);
			wal->list = list;
		}
	}

	if (!wal->count)
		return;

	DRM_DEBUG_DRIVER("Initialized %u %s workarounds on %s\n",
			 wal->wa_count, wal->name, wal->engine_name);
}

static void _wa_add(struct i915_wa_list *wal, const struct i915_wa *wa)
{
	unsigned int addr = i915_mmio_reg_offset(wa->reg);
	unsigned int start = 0, end = wal->count;
	const unsigned int grow = WA_LIST_CHUNK;
	struct i915_wa *wa_;

	GEM_BUG_ON(!is_power_of_2(grow));

	if (IS_ALIGNED(wal->count, grow)) { /* Either uninitialized or full. */
		struct i915_wa *list;

		list = kmalloc_array(ALIGN(wal->count + 1, grow), sizeof(*wa),
				     GFP_KERNEL);
		if (!list) {
			DRM_ERROR("No space for workaround init!\n");
			return;
		}

		if (wal->list) {
			memcpy(list, wal->list, sizeof(*wa) * wal->count);
			kfree(wal->list);
		}

		wal->list = list;
	}

	while (start < end) {
		unsigned int mid = start + (end - start) / 2;

		if (i915_mmio_reg_offset(wal->list[mid].reg) < addr) {
			start = mid + 1;
		} else if (i915_mmio_reg_offset(wal->list[mid].reg) > addr) {
			end = mid;
		} else {
			wa_ = &wal->list[mid];

			if ((wa->clr | wa_->clr) && !(wa->clr & ~wa_->clr)) {
				DRM_ERROR("Discarding overwritten w/a for reg %04x (clear: %08x, set: %08x)\n",
					  i915_mmio_reg_offset(wa_->reg),
					  wa_->clr, wa_->set);

				wa_->set &= ~wa->clr;
			}

			wal->wa_count++;
			wa_->set |= wa->set;
			wa_->clr |= wa->clr;
			wa_->read |= wa->read;
			return;
		}
	}

	wal->wa_count++;
	wa_ = &wal->list[wal->count++];
	*wa_ = *wa;

	while (wa_-- > wal->list) {
		GEM_BUG_ON(i915_mmio_reg_offset(wa_[0].reg) ==
			   i915_mmio_reg_offset(wa_[1].reg));
		if (i915_mmio_reg_offset(wa_[1].reg) >
		    i915_mmio_reg_offset(wa_[0].reg))
			break;

		swap(wa_[1], wa_[0]);
	}
}

static void wa_add(struct i915_wa_list *wal, i915_reg_t reg,
		   u32 clear, u32 set, u32 read_mask)
{
	struct i915_wa wa = {
		.reg  = reg,
		.clr  = clear,
		.set  = set,
		.read = read_mask,
	};

	_wa_add(wal, &wa);
}

static void
wa_write_clr_set(struct i915_wa_list *wal, i915_reg_t reg, u32 clear, u32 set)
{
	wa_add(wal, reg, clear, set, clear);
}

static void
wa_write(struct i915_wa_list *wal, i915_reg_t reg, u32 set)
{
	wa_write_clr_set(wal, reg, ~0, set);
}

static void
wa_write_or(struct i915_wa_list *wal, i915_reg_t reg, u32 set)
{
	wa_write_clr_set(wal, reg, set, set);
}

static void
wa_write_clr(struct i915_wa_list *wal, i915_reg_t reg, u32 clr)
{
	wa_write_clr_set(wal, reg, clr, 0);
}

/*
 * WA operations on "masked register". A masked register has the upper 16 bits
 * documented as "masked" in b-spec. Its purpose is to allow writing to just a
 * portion of the register without a rmw: you simply write in the upper 16 bits
 * the mask of bits you are going to modify.
 *
 * The wa_masked_* family of functions already does the necessary operations to
 * calculate the mask based on the parameters passed, so user only has to
 * provide the lower 16 bits of that register.
 */

static void
wa_masked_en(struct i915_wa_list *wal, i915_reg_t reg, u32 val)
{
	wa_add(wal, reg, 0, _MASKED_BIT_ENABLE(val), val);
}

static void
wa_masked_dis(struct i915_wa_list *wal, i915_reg_t reg, u32 val)
{
	wa_add(wal, reg, 0, _MASKED_BIT_DISABLE(val), val);
}

static void
wa_masked_field_set(struct i915_wa_list *wal, i915_reg_t reg,
		    u32 mask, u32 val)
{
	wa_add(wal, reg, 0, _MASKED_FIELD(mask, val), mask);
}

static void gen6_ctx_workarounds_init(struct intel_engine_cs *engine,
				      struct i915_wa_list *wal)
{
	wa_masked_en(wal, INSTPM, INSTPM_FORCE_ORDERING);
}

static void gen7_ctx_workarounds_init(struct intel_engine_cs *engine,
				      struct i915_wa_list *wal)
{
	wa_masked_en(wal, INSTPM, INSTPM_FORCE_ORDERING);
}

static void gen8_ctx_workarounds_init(struct intel_engine_cs *engine,
				      struct i915_wa_list *wal)
{
	wa_masked_en(wal, INSTPM, INSTPM_FORCE_ORDERING);

	/* WaDisableAsyncFlipPerfMode:bdw,chv */
	wa_masked_en(wal, MI_MODE, ASYNC_FLIP_PERF_DISABLE);

	/* WaDisablePartialInstShootdown:bdw,chv */
	wa_masked_en(wal, GEN8_ROW_CHICKEN,
		     PARTIAL_INSTRUCTION_SHOOTDOWN_DISABLE);

	/* Use Force Non-Coherent whenever executing a 3D context. This is a
	 * workaround for for a possible hang in the unlikely event a TLB
	 * invalidation occurs during a PSD flush.
	 */
	/* WaForceEnableNonCoherent:bdw,chv */
	/* WaHdcDisableFetchWhenMasked:bdw,chv */
	wa_masked_en(wal, HDC_CHICKEN0,
		     HDC_DONOT_FETCH_MEM_WHEN_MASKED |
		     HDC_FORCE_NON_COHERENT);

	/* From the Haswell PRM, Command Reference: Registers, CACHE_MODE_0:
	 * "The Hierarchical Z RAW Stall Optimization allows non-overlapping
	 *  polygons in the same 8x4 pixel/sample area to be processed without
	 *  stalling waiting for the earlier ones to write to Hierarchical Z
	 *  buffer."
	 *
	 * This optimization is off by default for BDW and CHV; turn it on.
	 */
	wa_masked_dis(wal, CACHE_MODE_0_GEN7, HIZ_RAW_STALL_OPT_DISABLE);

	/* Wa4x4STCOptimizationDisable:bdw,chv */
	wa_masked_en(wal, CACHE_MODE_1, GEN8_4x4_STC_OPTIMIZATION_DISABLE);

	/*
	 * BSpec recommends 8x4 when MSAA is used,
	 * however in practice 16x4 seems fastest.
	 *
	 * Note that PS/WM thread counts depend on the WIZ hashing
	 * disable bit, which we don't touch here, but it's good
	 * to keep in mind (see 3DSTATE_PS and 3DSTATE_WM).
	 */
	wa_masked_field_set(wal, GEN7_GT_MODE,
			    GEN6_WIZ_HASHING_MASK,
			    GEN6_WIZ_HASHING_16x4);
}

static void bdw_ctx_workarounds_init(struct intel_engine_cs *engine,
				     struct i915_wa_list *wal)
{
	struct drm_i915_private *i915 = engine->i915;

	gen8_ctx_workarounds_init(engine, wal);

	/* WaDisableThreadStallDopClockGating:bdw (pre-production) */
	wa_masked_en(wal, GEN8_ROW_CHICKEN, STALL_DOP_GATING_DISABLE);

	/* WaDisableDopClockGating:bdw
	 *
	 * Also see the related UCGTCL1 write in bdw_init_clock_gating()
	 * to disable EUTC clock gating.
	 */
	wa_masked_en(wal, GEN7_ROW_CHICKEN2,
		     DOP_CLOCK_GATING_DISABLE);

	wa_masked_en(wal, HALF_SLICE_CHICKEN3,
		     GEN8_SAMPLER_POWER_BYPASS_DIS);

	wa_masked_en(wal, HDC_CHICKEN0,
		     /* WaForceContextSaveRestoreNonCoherent:bdw */
		     HDC_FORCE_CONTEXT_SAVE_RESTORE_NON_COHERENT |
		     /* WaDisableFenceDestinationToSLM:bdw (pre-prod) */
		     (IS_BDW_GT3(i915) ? HDC_FENCE_DEST_SLM_DISABLE : 0));
}

static void chv_ctx_workarounds_init(struct intel_engine_cs *engine,
				     struct i915_wa_list *wal)
{
	gen8_ctx_workarounds_init(engine, wal);

	/* WaDisableThreadStallDopClockGating:chv */
	wa_masked_en(wal, GEN8_ROW_CHICKEN, STALL_DOP_GATING_DISABLE);

	/* Improve HiZ throughput on CHV. */
	wa_masked_en(wal, HIZ_CHICKEN, CHV_HZ_8X8_MODE_IN_1X);
}

static void gen9_ctx_workarounds_init(struct intel_engine_cs *engine,
				      struct i915_wa_list *wal)
{
	struct drm_i915_private *i915 = engine->i915;

	if (HAS_LLC(i915)) {
		/* WaCompressedResourceSamplerPbeMediaNewHashMode:skl,kbl
		 *
		 * Must match Display Engine. See
		 * WaCompressedResourceDisplayNewHashMode.
		 */
		wa_masked_en(wal, COMMON_SLICE_CHICKEN2,
			     GEN9_PBE_COMPRESSED_HASH_SELECTION);
		wa_masked_en(wal, GEN9_HALF_SLICE_CHICKEN7,
			     GEN9_SAMPLER_HASH_COMPRESSED_READ_ADDR);
	}

	/* WaClearFlowControlGpgpuContextSave:skl,bxt,kbl,glk,cfl */
	/* WaDisablePartialInstShootdown:skl,bxt,kbl,glk,cfl */
	wa_masked_en(wal, GEN8_ROW_CHICKEN,
		     FLOW_CONTROL_ENABLE |
		     PARTIAL_INSTRUCTION_SHOOTDOWN_DISABLE);

	/* WaEnableYV12BugFixInHalfSliceChicken7:skl,bxt,kbl,glk,cfl */
	/* WaEnableSamplerGPGPUPreemptionSupport:skl,bxt,kbl,cfl */
	wa_masked_en(wal, GEN9_HALF_SLICE_CHICKEN7,
		     GEN9_ENABLE_YV12_BUGFIX |
		     GEN9_ENABLE_GPGPU_PREEMPTION);

	/* Wa4x4STCOptimizationDisable:skl,bxt,kbl,glk,cfl */
	/* WaDisablePartialResolveInVc:skl,bxt,kbl,cfl */
	wa_masked_en(wal, CACHE_MODE_1,
		     GEN8_4x4_STC_OPTIMIZATION_DISABLE |
		     GEN9_PARTIAL_RESOLVE_IN_VC_DISABLE);

	/* WaCcsTlbPrefetchDisable:skl,bxt,kbl,glk,cfl */
	wa_masked_dis(wal, GEN9_HALF_SLICE_CHICKEN5,
		      GEN9_CCS_TLB_PREFETCH_ENABLE);

	/* WaForceContextSaveRestoreNonCoherent:skl,bxt,kbl,cfl */
	wa_masked_en(wal, HDC_CHICKEN0,
		     HDC_FORCE_CONTEXT_SAVE_RESTORE_NON_COHERENT |
		     HDC_FORCE_CSR_NON_COHERENT_OVR_DISABLE);

	/* WaForceEnableNonCoherent and WaDisableHDCInvalidation are
	 * both tied to WaForceContextSaveRestoreNonCoherent
	 * in some hsds for skl. We keep the tie for all gen9. The
	 * documentation is a bit hazy and so we want to get common behaviour,
	 * even though there is no clear evidence we would need both on kbl/bxt.
	 * This area has been source of system hangs so we play it safe
	 * and mimic the skl regardless of what bspec says.
	 *
	 * Use Force Non-Coherent whenever executing a 3D context. This
	 * is a workaround for a possible hang in the unlikely event
	 * a TLB invalidation occurs during a PSD flush.
	 */

	/* WaForceEnableNonCoherent:skl,bxt,kbl,cfl */
	wa_masked_en(wal, HDC_CHICKEN0,
		     HDC_FORCE_NON_COHERENT);

	/* WaDisableSamplerPowerBypassForSOPingPong:skl,bxt,kbl,cfl */
	if (IS_SKYLAKE(i915) ||
	    IS_KABYLAKE(i915) ||
	    IS_COFFEELAKE(i915) ||
	    IS_COMETLAKE(i915))
		wa_masked_en(wal, HALF_SLICE_CHICKEN3,
			     GEN8_SAMPLER_POWER_BYPASS_DIS);

	/* WaDisableSTUnitPowerOptimization:skl,bxt,kbl,glk,cfl */
	wa_masked_en(wal, HALF_SLICE_CHICKEN2, GEN8_ST_PO_DISABLE);

	/*
	 * Supporting preemption with fine-granularity requires changes in the
	 * batch buffer programming. Since we can't break old userspace, we
	 * need to set our default preemption level to safe value. Userspace is
	 * still able to use more fine-grained preemption levels, since in
	 * WaEnablePreemptionGranularityControlByUMD we're whitelisting the
	 * per-ctx register. As such, WaDisable{3D,GPGPU}MidCmdPreemption are
	 * not real HW workarounds, but merely a way to start using preemption
	 * while maintaining old contract with userspace.
	 */

	/* WaDisable3DMidCmdPreemption:skl,bxt,glk,cfl,[cnl] */
	wa_masked_dis(wal, GEN8_CS_CHICKEN1, GEN9_PREEMPT_3D_OBJECT_LEVEL);

	/* WaDisableGPGPUMidCmdPreemption:skl,bxt,blk,cfl,[cnl] */
	wa_masked_field_set(wal, GEN8_CS_CHICKEN1,
			    GEN9_PREEMPT_GPGPU_LEVEL_MASK,
			    GEN9_PREEMPT_GPGPU_COMMAND_LEVEL);

	/* WaClearHIZ_WM_CHICKEN3:bxt,glk */
	if (IS_GEN9_LP(i915))
		wa_masked_en(wal, GEN9_WM_CHICKEN3, GEN9_FACTOR_IN_CLR_VAL_HIZ);
}

static void skl_tune_iz_hashing(struct intel_engine_cs *engine,
				struct i915_wa_list *wal)
{
	struct intel_gt *gt = engine->gt;
	u8 vals[3] = { 0, 0, 0 };
	unsigned int i;

	for (i = 0; i < 3; i++) {
		u8 ss;

		/*
		 * Only consider slices where one, and only one, subslice has 7
		 * EUs
		 */
		if (!is_power_of_2(gt->info.sseu.subslice_7eu[i]))
			continue;

		/*
		 * subslice_7eu[i] != 0 (because of the check above) and
		 * ss_max == 4 (maximum number of subslices possible per slice)
		 *
		 * ->    0 <= ss <= 3;
		 */
		ss = ffs(gt->info.sseu.subslice_7eu[i]) - 1;
		vals[i] = 3 - ss;
	}

	if (vals[0] == 0 && vals[1] == 0 && vals[2] == 0)
		return;

	/* Tune IZ hashing. See intel_device_info_runtime_init() */
	wa_masked_field_set(wal, GEN7_GT_MODE,
			    GEN9_IZ_HASHING_MASK(2) |
			    GEN9_IZ_HASHING_MASK(1) |
			    GEN9_IZ_HASHING_MASK(0),
			    GEN9_IZ_HASHING(2, vals[2]) |
			    GEN9_IZ_HASHING(1, vals[1]) |
			    GEN9_IZ_HASHING(0, vals[0]));
}

static void skl_ctx_workarounds_init(struct intel_engine_cs *engine,
				     struct i915_wa_list *wal)
{
	gen9_ctx_workarounds_init(engine, wal);
	skl_tune_iz_hashing(engine, wal);
}

static void bxt_ctx_workarounds_init(struct intel_engine_cs *engine,
				     struct i915_wa_list *wal)
{
	gen9_ctx_workarounds_init(engine, wal);

	/* WaDisableThreadStallDopClockGating:bxt */
	wa_masked_en(wal, GEN8_ROW_CHICKEN,
		     STALL_DOP_GATING_DISABLE);

	/* WaToEnableHwFixForPushConstHWBug:bxt */
	wa_masked_en(wal, COMMON_SLICE_CHICKEN2,
		     GEN8_SBE_DISABLE_REPLAY_BUF_OPTIMIZATION);
}

static void kbl_ctx_workarounds_init(struct intel_engine_cs *engine,
				     struct i915_wa_list *wal)
{
	struct drm_i915_private *i915 = engine->i915;

	gen9_ctx_workarounds_init(engine, wal);

	/* WaToEnableHwFixForPushConstHWBug:kbl */
	if (IS_KBL_GT_REVID(i915, KBL_REVID_C0, REVID_FOREVER))
		wa_masked_en(wal, COMMON_SLICE_CHICKEN2,
			     GEN8_SBE_DISABLE_REPLAY_BUF_OPTIMIZATION);

	/* WaDisableSbeCacheDispatchPortSharing:kbl */
	wa_masked_en(wal, GEN7_HALF_SLICE_CHICKEN1,
		     GEN7_SBE_SS_CACHE_DISPATCH_PORT_SHARING_DISABLE);
}

static void glk_ctx_workarounds_init(struct intel_engine_cs *engine,
				     struct i915_wa_list *wal)
{
	gen9_ctx_workarounds_init(engine, wal);

	/* WaToEnableHwFixForPushConstHWBug:glk */
	wa_masked_en(wal, COMMON_SLICE_CHICKEN2,
		     GEN8_SBE_DISABLE_REPLAY_BUF_OPTIMIZATION);
}

static void cfl_ctx_workarounds_init(struct intel_engine_cs *engine,
				     struct i915_wa_list *wal)
{
	gen9_ctx_workarounds_init(engine, wal);

	/* WaToEnableHwFixForPushConstHWBug:cfl */
	wa_masked_en(wal, COMMON_SLICE_CHICKEN2,
		     GEN8_SBE_DISABLE_REPLAY_BUF_OPTIMIZATION);

	/* WaDisableSbeCacheDispatchPortSharing:cfl */
	wa_masked_en(wal, GEN7_HALF_SLICE_CHICKEN1,
		     GEN7_SBE_SS_CACHE_DISPATCH_PORT_SHARING_DISABLE);
}

static void cnl_ctx_workarounds_init(struct intel_engine_cs *engine,
				     struct i915_wa_list *wal)
{
	/* WaForceContextSaveRestoreNonCoherent:cnl */
	wa_masked_en(wal, CNL_HDC_CHICKEN0,
		     HDC_FORCE_CONTEXT_SAVE_RESTORE_NON_COHERENT);

	/* WaDisableReplayBufferBankArbitrationOptimization:cnl */
	wa_masked_en(wal, COMMON_SLICE_CHICKEN2,
		     GEN8_SBE_DISABLE_REPLAY_BUF_OPTIMIZATION);

	/* WaPushConstantDereferenceHoldDisable:cnl */
	wa_masked_en(wal, GEN7_ROW_CHICKEN2, PUSH_CONSTANT_DEREF_DISABLE);

	/* FtrEnableFastAnisoL1BankingFix:cnl */
	wa_masked_en(wal, HALF_SLICE_CHICKEN3, CNL_FAST_ANISO_L1_BANKING_FIX);

	/* WaDisable3DMidCmdPreemption:cnl */
	wa_masked_dis(wal, GEN8_CS_CHICKEN1, GEN9_PREEMPT_3D_OBJECT_LEVEL);

	/* WaDisableGPGPUMidCmdPreemption:cnl */
	wa_masked_field_set(wal, GEN8_CS_CHICKEN1,
			    GEN9_PREEMPT_GPGPU_LEVEL_MASK,
			    GEN9_PREEMPT_GPGPU_COMMAND_LEVEL);

	/* WaDisableEarlyEOT:cnl */
	wa_masked_en(wal, GEN8_ROW_CHICKEN, DISABLE_EARLY_EOT);
}

static void icl_ctx_workarounds_init(struct intel_engine_cs *engine,
				     struct i915_wa_list *wal)
{
	struct drm_i915_private *i915 = engine->i915;

	/* WaDisableBankHangMode:icl */
	wa_write(wal,
		 GEN8_L3CNTLREG,
		 intel_uncore_read(engine->uncore, GEN8_L3CNTLREG) |
		 GEN8_ERRDETBCTRL);

	/* Wa_1604370585:icl (pre-prod)
	 * Formerly known as WaPushConstantDereferenceHoldDisable
	 */
	if (IS_ICL_REVID(i915, ICL_REVID_A0, ICL_REVID_B0))
		wa_masked_en(wal, GEN7_ROW_CHICKEN2,
			     PUSH_CONSTANT_DEREF_DISABLE);

	/* WaForceEnableNonCoherent:icl
	 * This is not the same workaround as in early Gen9 platforms, where
	 * lacking this could cause system hangs, but coherency performance
	 * overhead is high and only a few compute workloads really need it
	 * (the register is whitelisted in hardware now, so UMDs can opt in
	 * for coherency if they have a good reason).
	 */
	wa_masked_en(wal, ICL_HDC_MODE, HDC_FORCE_NON_COHERENT);

	/* Wa_2006611047:icl (pre-prod)
	 * Formerly known as WaDisableImprovedTdlClkGating
	 */
	if (IS_ICL_REVID(i915, ICL_REVID_A0, ICL_REVID_A0))
		wa_masked_en(wal, GEN7_ROW_CHICKEN2,
			     GEN11_TDL_CLOCK_GATING_FIX_DISABLE);

	/* Wa_2006665173:icl (pre-prod) */
	if (IS_ICL_REVID(i915, ICL_REVID_A0, ICL_REVID_A0))
		wa_masked_en(wal, GEN11_COMMON_SLICE_CHICKEN3,
			     GEN11_BLEND_EMB_FIX_DISABLE_IN_RCC);

	/* WaEnableFloatBlendOptimization:icl */
	wa_write_clr_set(wal,
			 GEN10_CACHE_MODE_SS,
			 0, /* write-only, so skip validation */
			 _MASKED_BIT_ENABLE(FLOAT_BLEND_OPTIMIZATION_ENABLE));

	/* WaDisableGPGPUMidThreadPreemption:icl */
	wa_masked_field_set(wal, GEN8_CS_CHICKEN1,
			    GEN9_PREEMPT_GPGPU_LEVEL_MASK,
			    GEN9_PREEMPT_GPGPU_THREAD_GROUP_LEVEL);

	/* allow headerless messages for preemptible GPGPU context */
	wa_masked_en(wal, GEN10_SAMPLER_MODE,
		     GEN11_SAMPLER_ENABLE_HEADLESS_MSG);

	/* Wa_1604278689:icl,ehl */
	wa_write(wal, IVB_FBC_RT_BASE, 0xFFFFFFFF & ~ILK_FBC_RT_VALID);
	wa_write_clr_set(wal, IVB_FBC_RT_BASE_UPPER,
			 0, /* write-only register; skip validation */
			 0xFFFFFFFF);

	/* Wa_1406306137:icl,ehl */
	wa_masked_en(wal, GEN9_ROW_CHICKEN4, GEN11_DIS_PICK_2ND_EU);
}

static void gen12_ctx_workarounds_init(struct intel_engine_cs *engine,
				       struct i915_wa_list *wal)
{
	/*
	 * Wa_1409142259:tgl
	 * Wa_1409347922:tgl
	 * Wa_1409252684:tgl
	 * Wa_1409217633:tgl
	 * Wa_1409207793:tgl
	 * Wa_1409178076:tgl
	 * Wa_1408979724:tgl
	 * Wa_14010443199:rkl
	 * Wa_14010698770:rkl
	 */
	wa_masked_en(wal, GEN11_COMMON_SLICE_CHICKEN3,
		     GEN12_DISABLE_CPS_AWARE_COLOR_PIPE);

	/* WaDisableGPGPUMidThreadPreemption:gen12 */
	wa_masked_field_set(wal, GEN8_CS_CHICKEN1,
			    GEN9_PREEMPT_GPGPU_LEVEL_MASK,
			    GEN9_PREEMPT_GPGPU_THREAD_GROUP_LEVEL);
}

static void tgl_ctx_workarounds_init(struct intel_engine_cs *engine,
				     struct i915_wa_list *wal)
{
	gen12_ctx_workarounds_init(engine, wal);

	/*
	 * Wa_1604555607:tgl,rkl
	 *
	 * Note that the implementation of this workaround is further modified
	 * according to the FF_MODE2 guidance given by Wa_1608008084:gen12.
	 * FF_MODE2 register will return the wrong value when read. The default
	 * value for this register is zero for all fields and there are no bit
	 * masks. So instead of doing a RMW we should just write the GS Timer
	 * and TDS timer values for Wa_1604555607 and Wa_16011163337.
	 */
	wa_add(wal,
	       FF_MODE2,
	       FF_MODE2_GS_TIMER_MASK | FF_MODE2_TDS_TIMER_MASK,
	       FF_MODE2_GS_TIMER_224  | FF_MODE2_TDS_TIMER_128,
	       0);
}

static void dg1_ctx_workarounds_init(struct intel_engine_cs *engine,
				     struct i915_wa_list *wal)
{
	gen12_ctx_workarounds_init(engine, wal);

	/* Wa_1409044764 */
	wa_masked_dis(wal, GEN11_COMMON_SLICE_CHICKEN3,
		      DG1_FLOAT_POINT_BLEND_OPT_STRICT_MODE_EN);

	/* Wa_22010493298 */
	wa_masked_en(wal, HIZ_CHICKEN,
		     DG1_HZ_READ_SUPPRESSION_OPTIMIZATION_DISABLE);

	/*
	 * Wa_16011163337
	 *
	 * Like in tgl_ctx_workarounds_init(), read verification is ignored due
	 * to Wa_1608008084.
	 */
	wa_add(wal,
	       FF_MODE2,
	       FF_MODE2_GS_TIMER_MASK, FF_MODE2_GS_TIMER_224, 0);
}

static void
__intel_engine_init_ctx_wa(struct intel_engine_cs *engine,
			   struct i915_wa_list *wal,
			   const char *name)
{
	struct drm_i915_private *i915 = engine->i915;

	if (engine->class != RENDER_CLASS)
		return;

	wa_init_start(wal, name, engine->name);

	if (IS_DG1(i915))
		dg1_ctx_workarounds_init(engine, wal);
	else if (IS_ROCKETLAKE(i915) || IS_TIGERLAKE(i915))
		tgl_ctx_workarounds_init(engine, wal);
	else if (IS_GEN(i915, 12))
		gen12_ctx_workarounds_init(engine, wal);
	else if (IS_GEN(i915, 11))
		icl_ctx_workarounds_init(engine, wal);
	else if (IS_CANNONLAKE(i915))
		cnl_ctx_workarounds_init(engine, wal);
	else if (IS_COFFEELAKE(i915) || IS_COMETLAKE(i915))
		cfl_ctx_workarounds_init(engine, wal);
	else if (IS_GEMINILAKE(i915))
		glk_ctx_workarounds_init(engine, wal);
	else if (IS_KABYLAKE(i915))
		kbl_ctx_workarounds_init(engine, wal);
	else if (IS_BROXTON(i915))
		bxt_ctx_workarounds_init(engine, wal);
	else if (IS_SKYLAKE(i915))
		skl_ctx_workarounds_init(engine, wal);
	else if (IS_CHERRYVIEW(i915))
		chv_ctx_workarounds_init(engine, wal);
	else if (IS_BROADWELL(i915))
		bdw_ctx_workarounds_init(engine, wal);
	else if (IS_GEN(i915, 7))
		gen7_ctx_workarounds_init(engine, wal);
	else if (IS_GEN(i915, 6))
		gen6_ctx_workarounds_init(engine, wal);
	else if (INTEL_GEN(i915) < 8)
		return;
	else
		MISSING_CASE(INTEL_GEN(i915));

	wa_init_finish(wal);
}

void intel_engine_init_ctx_wa(struct intel_engine_cs *engine)
{
	__intel_engine_init_ctx_wa(engine, &engine->ctx_wa_list, "context");
}

int intel_engine_emit_ctx_wa(struct i915_request *rq)
{
	struct i915_wa_list *wal = &rq->engine->ctx_wa_list;
	struct i915_wa *wa;
	unsigned int i;
	u32 *cs;
	int ret;

	if (wal->count == 0)
		return 0;

	ret = rq->engine->emit_flush(rq, EMIT_BARRIER);
	if (ret)
		return ret;

	cs = intel_ring_begin(rq, (wal->count * 2 + 2));
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_LOAD_REGISTER_IMM(wal->count);
	for (i = 0, wa = wal->list; i < wal->count; i++, wa++) {
		*cs++ = i915_mmio_reg_offset(wa->reg);
		*cs++ = wa->set;
	}
	*cs++ = MI_NOOP;

	intel_ring_advance(rq, cs);

	ret = rq->engine->emit_flush(rq, EMIT_BARRIER);
	if (ret)
		return ret;

	return 0;
}

static void
gen4_gt_workarounds_init(struct drm_i915_private *i915,
			 struct i915_wa_list *wal)
{
	/* WaDisable_RenderCache_OperationalFlush:gen4,ilk */
	wa_masked_dis(wal, CACHE_MODE_0, RC_OP_FLUSH_ENABLE);
}

static void
g4x_gt_workarounds_init(struct drm_i915_private *i915, struct i915_wa_list *wal)
{
	gen4_gt_workarounds_init(i915, wal);

	/* WaDisableRenderCachePipelinedFlush:g4x,ilk */
	wa_masked_en(wal, CACHE_MODE_0, CM0_PIPELINED_RENDER_FLUSH_DISABLE);
}

static void
ilk_gt_workarounds_init(struct drm_i915_private *i915, struct i915_wa_list *wal)
{
	g4x_gt_workarounds_init(i915, wal);

	wa_masked_en(wal, _3D_CHICKEN2, _3D_CHICKEN2_WM_READ_PIPELINED);
}

static void
snb_gt_workarounds_init(struct drm_i915_private *i915, struct i915_wa_list *wal)
{
	/* WaDisableHiZPlanesWhenMSAAEnabled:snb */
	wa_masked_en(wal,
		     _3D_CHICKEN,
		     _3D_CHICKEN_HIZ_PLANE_DISABLE_MSAA_4X_SNB);

	/* WaDisable_RenderCache_OperationalFlush:snb */
	wa_masked_dis(wal, CACHE_MODE_0, RC_OP_FLUSH_ENABLE);

	/*
	 * BSpec recommends 8x4 when MSAA is used,
	 * however in practice 16x4 seems fastest.
	 *
	 * Note that PS/WM thread counts depend on the WIZ hashing
	 * disable bit, which we don't touch here, but it's good
	 * to keep in mind (see 3DSTATE_PS and 3DSTATE_WM).
	 */
	wa_add(wal,
	       GEN6_GT_MODE, 0,
	       _MASKED_FIELD(GEN6_WIZ_HASHING_MASK, GEN6_WIZ_HASHING_16x4),
	       GEN6_WIZ_HASHING_16x4);

	wa_masked_dis(wal, CACHE_MODE_0, CM0_STC_EVICT_DISABLE_LRA_SNB);

	wa_masked_en(wal,
		     _3D_CHICKEN3,
		     /* WaStripsFansDisableFastClipPerformanceFix:snb */
		     _3D_CHICKEN3_SF_DISABLE_FASTCLIP_CULL |
		     /*
		      * Bspec says:
		      * "This bit must be set if 3DSTATE_CLIP clip mode is set
		      * to normal and 3DSTATE_SF number of SF output attributes
		      * is more than 16."
		      */
		   _3D_CHICKEN3_SF_DISABLE_PIPELINED_ATTR_FETCH);
}

static void
ivb_gt_workarounds_init(struct drm_i915_private *i915, struct i915_wa_list *wal)
{
	/* WaDisableEarlyCull:ivb */
	wa_masked_en(wal, _3D_CHICKEN3, _3D_CHICKEN_SF_DISABLE_OBJEND_CULL);

	/* WaDisablePSDDualDispatchEnable:ivb */
	if (IS_IVB_GT1(i915))
		wa_masked_en(wal,
			     GEN7_HALF_SLICE_CHICKEN1,
			     GEN7_PSD_SINGLE_PORT_DISPATCH_ENABLE);

	/* WaDisable_RenderCache_OperationalFlush:ivb */
	wa_masked_dis(wal, CACHE_MODE_0_GEN7, RC_OP_FLUSH_ENABLE);

	/* Apply the WaDisableRHWOOptimizationForRenderHang:ivb workaround. */
	wa_masked_dis(wal,
		      GEN7_COMMON_SLICE_CHICKEN1,
		      GEN7_CSC1_RHWO_OPT_DISABLE_IN_RCC);

	/* WaApplyL3ControlAndL3ChickenMode:ivb */
	wa_write(wal, GEN7_L3CNTLREG1, GEN7_WA_FOR_GEN7_L3_CONTROL);
	wa_write(wal, GEN7_L3_CHICKEN_MODE_REGISTER, GEN7_WA_L3_CHICKEN_MODE);

	/* WaForceL3Serialization:ivb */
	wa_write_clr(wal, GEN7_L3SQCREG4, L3SQ_URB_READ_CAM_MATCH_DISABLE);

	/*
	 * WaVSThreadDispatchOverride:ivb,vlv
	 *
	 * This actually overrides the dispatch
	 * mode for all thread types.
	 */
	wa_write_clr_set(wal, GEN7_FF_THREAD_MODE,
			 GEN7_FF_SCHED_MASK,
			 GEN7_FF_TS_SCHED_HW |
			 GEN7_FF_VS_SCHED_HW |
			 GEN7_FF_DS_SCHED_HW);

	if (0) { /* causes HiZ corruption on ivb:gt1 */
		/* enable HiZ Raw Stall Optimization */
		wa_masked_dis(wal, CACHE_MODE_0_GEN7, HIZ_RAW_STALL_OPT_DISABLE);
	}

	/* WaDisable4x2SubspanOptimization:ivb */
	wa_masked_en(wal, CACHE_MODE_1, PIXEL_SUBSPAN_COLLECT_OPT_DISABLE);

	/*
	 * BSpec recommends 8x4 when MSAA is used,
	 * however in practice 16x4 seems fastest.
	 *
	 * Note that PS/WM thread counts depend on the WIZ hashing
	 * disable bit, which we don't touch here, but it's good
	 * to keep in mind (see 3DSTATE_PS and 3DSTATE_WM).
	 */
	wa_add(wal, GEN7_GT_MODE, 0,
	       _MASKED_FIELD(GEN6_WIZ_HASHING_MASK, GEN6_WIZ_HASHING_16x4),
	       GEN6_WIZ_HASHING_16x4);
}

static void
vlv_gt_workarounds_init(struct drm_i915_private *i915, struct i915_wa_list *wal)
{
	/* WaDisableEarlyCull:vlv */
	wa_masked_en(wal, _3D_CHICKEN3, _3D_CHICKEN_SF_DISABLE_OBJEND_CULL);

	/* WaPsdDispatchEnable:vlv */
	/* WaDisablePSDDualDispatchEnable:vlv */
	wa_masked_en(wal,
		     GEN7_HALF_SLICE_CHICKEN1,
		     GEN7_MAX_PS_THREAD_DEP |
		     GEN7_PSD_SINGLE_PORT_DISPATCH_ENABLE);

	/* WaDisable_RenderCache_OperationalFlush:vlv */
	wa_masked_dis(wal, CACHE_MODE_0_GEN7, RC_OP_FLUSH_ENABLE);

	/* WaForceL3Serialization:vlv */
	wa_write_clr(wal, GEN7_L3SQCREG4, L3SQ_URB_READ_CAM_MATCH_DISABLE);

	/*
	 * WaVSThreadDispatchOverride:ivb,vlv
	 *
	 * This actually overrides the dispatch
	 * mode for all thread types.
	 */
	wa_write_clr_set(wal,
			 GEN7_FF_THREAD_MODE,
			 GEN7_FF_SCHED_MASK,
			 GEN7_FF_TS_SCHED_HW |
			 GEN7_FF_VS_SCHED_HW |
			 GEN7_FF_DS_SCHED_HW);

	/*
	 * BSpec says this must be set, even though
	 * WaDisable4x2SubspanOptimization isn't listed for VLV.
	 */
	wa_masked_en(wal, CACHE_MODE_1, PIXEL_SUBSPAN_COLLECT_OPT_DISABLE);

	/*
	 * BSpec recommends 8x4 when MSAA is used,
	 * however in practice 16x4 seems fastest.
	 *
	 * Note that PS/WM thread counts depend on the WIZ hashing
	 * disable bit, which we don't touch here, but it's good
	 * to keep in mind (see 3DSTATE_PS and 3DSTATE_WM).
	 */
	wa_add(wal, GEN7_GT_MODE, 0,
	       _MASKED_FIELD(GEN6_WIZ_HASHING_MASK, GEN6_WIZ_HASHING_16x4),
	       GEN6_WIZ_HASHING_16x4);

	/*
	 * WaIncreaseL3CreditsForVLVB0:vlv
	 * This is the hardware default actually.
	 */
	wa_write(wal, GEN7_L3SQCREG1, VLV_B0_WA_L3SQCREG1_VALUE);
}

static void
hsw_gt_workarounds_init(struct drm_i915_private *i915, struct i915_wa_list *wal)
{
	/* L3 caching of data atomics doesn't work -- disable it. */
	wa_write(wal, HSW_SCRATCH1, HSW_SCRATCH1_L3_DATA_ATOMICS_DISABLE);

	wa_add(wal,
	       HSW_ROW_CHICKEN3, 0,
	       _MASKED_BIT_ENABLE(HSW_ROW_CHICKEN3_L3_GLOBAL_ATOMICS_DISABLE),
		0 /* XXX does this reg exist? */);

	/* WaVSRefCountFullforceMissDisable:hsw */
	wa_write_clr(wal, GEN7_FF_THREAD_MODE, GEN7_FF_VS_REF_CNT_FFME);

	wa_masked_dis(wal,
		      CACHE_MODE_0_GEN7,
		      /* WaDisable_RenderCache_OperationalFlush:hsw */
		      RC_OP_FLUSH_ENABLE |
		      /* enable HiZ Raw Stall Optimization */
		      HIZ_RAW_STALL_OPT_DISABLE);

	/* WaDisable4x2SubspanOptimization:hsw */
	wa_masked_en(wal, CACHE_MODE_1, PIXEL_SUBSPAN_COLLECT_OPT_DISABLE);

	/*
	 * BSpec recommends 8x4 when MSAA is used,
	 * however in practice 16x4 seems fastest.
	 *
	 * Note that PS/WM thread counts depend on the WIZ hashing
	 * disable bit, which we don't touch here, but it's good
	 * to keep in mind (see 3DSTATE_PS and 3DSTATE_WM).
	 */
	wa_add(wal, GEN7_GT_MODE, 0,
	       _MASKED_FIELD(GEN6_WIZ_HASHING_MASK, GEN6_WIZ_HASHING_16x4),
	       GEN6_WIZ_HASHING_16x4);

	/* WaSampleCChickenBitEnable:hsw */
	wa_masked_en(wal, HALF_SLICE_CHICKEN3, HSW_SAMPLE_C_PERFORMANCE);
}

static void
gen9_gt_workarounds_init(struct drm_i915_private *i915, struct i915_wa_list *wal)
{
	/* WaDisableKillLogic:bxt,skl,kbl */
	if (!IS_COFFEELAKE(i915) && !IS_COMETLAKE(i915))
		wa_write_or(wal,
			    GAM_ECOCHK,
			    ECOCHK_DIS_TLB);

	if (HAS_LLC(i915)) {
		/* WaCompressedResourceSamplerPbeMediaNewHashMode:skl,kbl
		 *
		 * Must match Display Engine. See
		 * WaCompressedResourceDisplayNewHashMode.
		 */
		wa_write_or(wal,
			    MMCD_MISC_CTRL,
			    MMCD_PCLA | MMCD_HOTSPOT_EN);
	}

	/* WaDisableHDCInvalidation:skl,bxt,kbl,cfl */
	wa_write_or(wal,
		    GAM_ECOCHK,
		    BDW_DISABLE_HDC_INVALIDATION);
}

static void
skl_gt_workarounds_init(struct drm_i915_private *i915, struct i915_wa_list *wal)
{
	gen9_gt_workarounds_init(i915, wal);

	/* WaDisableGafsUnitClkGating:skl */
	wa_write_or(wal,
		    GEN7_UCGCTL4,
		    GEN8_EU_GAUNIT_CLOCK_GATE_DISABLE);

	/* WaInPlaceDecompressionHang:skl */
	if (IS_SKL_REVID(i915, SKL_REVID_H0, REVID_FOREVER))
		wa_write_or(wal,
			    GEN9_GAMT_ECO_REG_RW_IA,
			    GAMT_ECO_ENABLE_IN_PLACE_DECOMPRESS);
}

static void
bxt_gt_workarounds_init(struct drm_i915_private *i915, struct i915_wa_list *wal)
{
	gen9_gt_workarounds_init(i915, wal);

	/* WaInPlaceDecompressionHang:bxt */
	wa_write_or(wal,
		    GEN9_GAMT_ECO_REG_RW_IA,
		    GAMT_ECO_ENABLE_IN_PLACE_DECOMPRESS);
}

static void
kbl_gt_workarounds_init(struct drm_i915_private *i915, struct i915_wa_list *wal)
{
	gen9_gt_workarounds_init(i915, wal);

	/* WaDisableDynamicCreditSharing:kbl */
	if (IS_KBL_GT_REVID(i915, 0, KBL_REVID_B0))
		wa_write_or(wal,
			    GAMT_CHKN_BIT_REG,
			    GAMT_CHKN_DISABLE_DYNAMIC_CREDIT_SHARING);

	/* WaDisableGafsUnitClkGating:kbl */
	wa_write_or(wal,
		    GEN7_UCGCTL4,
		    GEN8_EU_GAUNIT_CLOCK_GATE_DISABLE);

	/* WaInPlaceDecompressionHang:kbl */
	wa_write_or(wal,
		    GEN9_GAMT_ECO_REG_RW_IA,
		    GAMT_ECO_ENABLE_IN_PLACE_DECOMPRESS);
}

static void
glk_gt_workarounds_init(struct drm_i915_private *i915, struct i915_wa_list *wal)
{
	gen9_gt_workarounds_init(i915, wal);
}

static void
cfl_gt_workarounds_init(struct drm_i915_private *i915, struct i915_wa_list *wal)
{
	gen9_gt_workarounds_init(i915, wal);

	/* WaDisableGafsUnitClkGating:cfl */
	wa_write_or(wal,
		    GEN7_UCGCTL4,
		    GEN8_EU_GAUNIT_CLOCK_GATE_DISABLE);

	/* WaInPlaceDecompressionHang:cfl */
	wa_write_or(wal,
		    GEN9_GAMT_ECO_REG_RW_IA,
		    GAMT_ECO_ENABLE_IN_PLACE_DECOMPRESS);
}

static void
wa_init_mcr(struct drm_i915_private *i915, struct i915_wa_list *wal)
{
	const struct sseu_dev_info *sseu = &i915->gt.info.sseu;
	unsigned int slice, subslice;
	u32 l3_en, mcr, mcr_mask;

	GEM_BUG_ON(INTEL_GEN(i915) < 10);

	/*
	 * WaProgramMgsrForL3BankSpecificMmioReads: cnl,icl
	 * L3Banks could be fused off in single slice scenario. If that is
	 * the case, we might need to program MCR select to a valid L3Bank
	 * by default, to make sure we correctly read certain registers
	 * later on (in the range 0xB100 - 0xB3FF).
	 *
	 * WaProgramMgsrForCorrectSliceSpecificMmioReads:cnl,icl
	 * Before any MMIO read into slice/subslice specific registers, MCR
	 * packet control register needs to be programmed to point to any
	 * enabled s/ss pair. Otherwise, incorrect values will be returned.
	 * This means each subsequent MMIO read will be forwarded to an
	 * specific s/ss combination, but this is OK since these registers
	 * are consistent across s/ss in almost all cases. In the rare
	 * occasions, such as INSTDONE, where this value is dependent
	 * on s/ss combo, the read should be done with read_subslice_reg.
	 *
	 * Since GEN8_MCR_SELECTOR contains dual-purpose bits which select both
	 * to which subslice, or to which L3 bank, the respective mmio reads
	 * will go, we have to find a common index which works for both
	 * accesses.
	 *
	 * Case where we cannot find a common index fortunately should not
	 * happen in production hardware, so we only emit a warning instead of
	 * implementing something more complex that requires checking the range
	 * of every MMIO read.
	 */

	if (INTEL_GEN(i915) >= 10 && is_power_of_2(sseu->slice_mask)) {
		u32 l3_fuse =
			intel_uncore_read(&i915->uncore, GEN10_MIRROR_FUSE3) &
			GEN10_L3BANK_MASK;

		drm_dbg(&i915->drm, "L3 fuse = %x\n", l3_fuse);
		l3_en = ~(l3_fuse << GEN10_L3BANK_PAIR_COUNT | l3_fuse);
	} else {
		l3_en = ~0;
	}

	slice = fls(sseu->slice_mask) - 1;
	subslice = fls(l3_en & intel_sseu_get_subslices(sseu, slice));
	if (!subslice) {
		drm_warn(&i915->drm,
			 "No common index found between subslice mask %x and L3 bank mask %x!\n",
			 intel_sseu_get_subslices(sseu, slice), l3_en);
		subslice = fls(l3_en);
		drm_WARN_ON(&i915->drm, !subslice);
	}
	subslice--;

	if (INTEL_GEN(i915) >= 11) {
		mcr = GEN11_MCR_SLICE(slice) | GEN11_MCR_SUBSLICE(subslice);
		mcr_mask = GEN11_MCR_SLICE_MASK | GEN11_MCR_SUBSLICE_MASK;
	} else {
		mcr = GEN8_MCR_SLICE(slice) | GEN8_MCR_SUBSLICE(subslice);
		mcr_mask = GEN8_MCR_SLICE_MASK | GEN8_MCR_SUBSLICE_MASK;
	}

	drm_dbg(&i915->drm, "MCR slice/subslice = %x\n", mcr);

	wa_write_clr_set(wal, GEN8_MCR_SELECTOR, mcr_mask, mcr);
}

static void
cnl_gt_workarounds_init(struct drm_i915_private *i915, struct i915_wa_list *wal)
{
	wa_init_mcr(i915, wal);

	/* WaInPlaceDecompressionHang:cnl */
	wa_write_or(wal,
		    GEN9_GAMT_ECO_REG_RW_IA,
		    GAMT_ECO_ENABLE_IN_PLACE_DECOMPRESS);
}

static void
icl_gt_workarounds_init(struct drm_i915_private *i915, struct i915_wa_list *wal)
{
	wa_init_mcr(i915, wal);

	/* WaInPlaceDecompressionHang:icl */
	wa_write_or(wal,
		    GEN9_GAMT_ECO_REG_RW_IA,
		    GAMT_ECO_ENABLE_IN_PLACE_DECOMPRESS);

	/* WaModifyGamTlbPartitioning:icl */
	wa_write_clr_set(wal,
			 GEN11_GACB_PERF_CTRL,
			 GEN11_HASH_CTRL_MASK,
			 GEN11_HASH_CTRL_BIT0 | GEN11_HASH_CTRL_BIT4);

	/* Wa_1405766107:icl
	 * Formerly known as WaCL2SFHalfMaxAlloc
	 */
	wa_write_or(wal,
		    GEN11_LSN_UNSLCVC,
		    GEN11_LSN_UNSLCVC_GAFS_HALF_SF_MAXALLOC |
		    GEN11_LSN_UNSLCVC_GAFS_HALF_CL2_MAXALLOC);

	/* Wa_220166154:icl
	 * Formerly known as WaDisCtxReload
	 */
	wa_write_or(wal,
		    GEN8_GAMW_ECO_DEV_RW_IA,
		    GAMW_ECO_DEV_CTX_RELOAD_DISABLE);

	/* Wa_1405779004:icl (pre-prod) */
	if (IS_ICL_REVID(i915, ICL_REVID_A0, ICL_REVID_A0))
		wa_write_or(wal,
			    SLICE_UNIT_LEVEL_CLKGATE,
			    MSCUNIT_CLKGATE_DIS);

	/* Wa_1406838659:icl (pre-prod) */
	if (IS_ICL_REVID(i915, ICL_REVID_A0, ICL_REVID_B0))
		wa_write_or(wal,
			    INF_UNIT_LEVEL_CLKGATE,
			    CGPSF_CLKGATE_DIS);

	/* Wa_1406463099:icl
	 * Formerly known as WaGamTlbPendError
	 */
	wa_write_or(wal,
		    GAMT_CHKN_BIT_REG,
		    GAMT_CHKN_DISABLE_L3_COH_PIPE);

	/* Wa_1607087056:icl,ehl,jsl */
	if (IS_ICELAKE(i915) ||
		IS_JSL_EHL_REVID(i915, EHL_REVID_A0, EHL_REVID_A0)) {
		wa_write_or(wal,
			    SLICE_UNIT_LEVEL_CLKGATE,
			    L3_CLKGATE_DIS | L3_CR2X_CLKGATE_DIS);
	}
}

static void
gen12_gt_workarounds_init(struct drm_i915_private *i915,
			  struct i915_wa_list *wal)
{
	wa_init_mcr(i915, wal);
}

static void
tgl_gt_workarounds_init(struct drm_i915_private *i915, struct i915_wa_list *wal)
{
	gen12_gt_workarounds_init(i915, wal);

	/* Wa_1409420604:tgl */
	if (IS_TGL_UY_GT_REVID(i915, TGL_REVID_A0, TGL_REVID_A0))
		wa_write_or(wal,
			    SUBSLICE_UNIT_LEVEL_CLKGATE2,
			    CPSSUNIT_CLKGATE_DIS);

	/* Wa_1607087056:tgl also know as BUG:1409180338 */
	if (IS_TGL_UY_GT_REVID(i915, TGL_REVID_A0, TGL_REVID_A0))
		wa_write_or(wal,
			    SLICE_UNIT_LEVEL_CLKGATE,
			    L3_CLKGATE_DIS | L3_CR2X_CLKGATE_DIS);

	/* Wa_1408615072:tgl[a0] */
	if (IS_TGL_UY_GT_REVID(i915, TGL_REVID_A0, TGL_REVID_A0))
		wa_write_or(wal, UNSLICE_UNIT_LEVEL_CLKGATE2,
			    VSUNIT_CLKGATE_DIS_TGL);
}

static void
dg1_gt_workarounds_init(struct drm_i915_private *i915, struct i915_wa_list *wal)
{
	gen12_gt_workarounds_init(i915, wal);

	/* Wa_1607087056:dg1 */
	if (IS_DG1_REVID(i915, DG1_REVID_A0, DG1_REVID_A0))
		wa_write_or(wal,
			    SLICE_UNIT_LEVEL_CLKGATE,
			    L3_CLKGATE_DIS | L3_CR2X_CLKGATE_DIS);

	/* Wa_1409420604:dg1 */
	if (IS_DG1(i915))
		wa_write_or(wal,
			    SUBSLICE_UNIT_LEVEL_CLKGATE2,
			    CPSSUNIT_CLKGATE_DIS);

	/* Wa_1408615072:dg1 */
	/* Empirical testing shows this register is unaffected by engine reset. */
	if (IS_DG1(i915))
		wa_write_or(wal, UNSLICE_UNIT_LEVEL_CLKGATE2,
			    VSUNIT_CLKGATE_DIS_TGL);
}

static void
gt_init_workarounds(struct drm_i915_private *i915, struct i915_wa_list *wal)
{
	if (IS_DG1(i915))
		dg1_gt_workarounds_init(i915, wal);
	else if (IS_TIGERLAKE(i915))
		tgl_gt_workarounds_init(i915, wal);
	else if (IS_GEN(i915, 12))
		gen12_gt_workarounds_init(i915, wal);
	else if (IS_GEN(i915, 11))
		icl_gt_workarounds_init(i915, wal);
	else if (IS_CANNONLAKE(i915))
		cnl_gt_workarounds_init(i915, wal);
	else if (IS_COFFEELAKE(i915) || IS_COMETLAKE(i915))
		cfl_gt_workarounds_init(i915, wal);
	else if (IS_GEMINILAKE(i915))
		glk_gt_workarounds_init(i915, wal);
	else if (IS_KABYLAKE(i915))
		kbl_gt_workarounds_init(i915, wal);
	else if (IS_BROXTON(i915))
		bxt_gt_workarounds_init(i915, wal);
	else if (IS_SKYLAKE(i915))
		skl_gt_workarounds_init(i915, wal);
	else if (IS_HASWELL(i915))
		hsw_gt_workarounds_init(i915, wal);
	else if (IS_VALLEYVIEW(i915))
		vlv_gt_workarounds_init(i915, wal);
	else if (IS_IVYBRIDGE(i915))
		ivb_gt_workarounds_init(i915, wal);
	else if (IS_GEN(i915, 6))
		snb_gt_workarounds_init(i915, wal);
	else if (IS_GEN(i915, 5))
		ilk_gt_workarounds_init(i915, wal);
	else if (IS_G4X(i915))
		g4x_gt_workarounds_init(i915, wal);
	else if (IS_GEN(i915, 4))
		gen4_gt_workarounds_init(i915, wal);
	else if (INTEL_GEN(i915) <= 8)
		return;
	else
		MISSING_CASE(INTEL_GEN(i915));
}

void intel_gt_init_workarounds(struct drm_i915_private *i915)
{
	struct i915_wa_list *wal = &i915->gt_wa_list;

	wa_init_start(wal, "GT", "global");
	gt_init_workarounds(i915, wal);
	wa_init_finish(wal);
}

static enum forcewake_domains
wal_get_fw_for_rmw(struct intel_uncore *uncore, const struct i915_wa_list *wal)
{
	enum forcewake_domains fw = 0;
	struct i915_wa *wa;
	unsigned int i;

	for (i = 0, wa = wal->list; i < wal->count; i++, wa++)
		fw |= intel_uncore_forcewake_for_reg(uncore,
						     wa->reg,
						     FW_REG_READ |
						     FW_REG_WRITE);

	return fw;
}

static bool
wa_verify(const struct i915_wa *wa, u32 cur, const char *name, const char *from)
{
	if ((cur ^ wa->set) & wa->read) {
		DRM_ERROR("%s workaround lost on %s! (%x=%x/%x, expected %x)\n",
			  name, from, i915_mmio_reg_offset(wa->reg),
			  cur, cur & wa->read, wa->set);

		return false;
	}

	return true;
}

static void
wa_list_apply(struct intel_uncore *uncore, const struct i915_wa_list *wal)
{
	enum forcewake_domains fw;
	unsigned long flags;
	struct i915_wa *wa;
	unsigned int i;

	if (!wal->count)
		return;

	fw = wal_get_fw_for_rmw(uncore, wal);

	spin_lock_irqsave(&uncore->lock, flags);
	intel_uncore_forcewake_get__locked(uncore, fw);

	for (i = 0, wa = wal->list; i < wal->count; i++, wa++) {
		if (wa->clr)
			intel_uncore_rmw_fw(uncore, wa->reg, wa->clr, wa->set);
		else
			intel_uncore_write_fw(uncore, wa->reg, wa->set);
		if (IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM))
			wa_verify(wa,
				  intel_uncore_read_fw(uncore, wa->reg),
				  wal->name, "application");
	}

	intel_uncore_forcewake_put__locked(uncore, fw);
	spin_unlock_irqrestore(&uncore->lock, flags);
}

void intel_gt_apply_workarounds(struct intel_gt *gt)
{
	wa_list_apply(gt->uncore, &gt->i915->gt_wa_list);
}

static bool wa_list_verify(struct intel_uncore *uncore,
			   const struct i915_wa_list *wal,
			   const char *from)
{
	struct i915_wa *wa;
	unsigned int i;
	bool ok = true;

	for (i = 0, wa = wal->list; i < wal->count; i++, wa++)
		ok &= wa_verify(wa,
				intel_uncore_read(uncore, wa->reg),
				wal->name, from);

	return ok;
}

bool intel_gt_verify_workarounds(struct intel_gt *gt, const char *from)
{
	return wa_list_verify(gt->uncore, &gt->i915->gt_wa_list, from);
}

static inline bool is_nonpriv_flags_valid(u32 flags)
{
	/* Check only valid flag bits are set */
	if (flags & ~RING_FORCE_TO_NONPRIV_MASK_VALID)
		return false;

	/* NB: Only 3 out of 4 enum values are valid for access field */
	if ((flags & RING_FORCE_TO_NONPRIV_ACCESS_MASK) ==
	    RING_FORCE_TO_NONPRIV_ACCESS_INVALID)
		return false;

	return true;
}

static void
whitelist_reg_ext(struct i915_wa_list *wal, i915_reg_t reg, u32 flags)
{
	struct i915_wa wa = {
		.reg = reg
	};

	if (GEM_DEBUG_WARN_ON(wal->count >= RING_MAX_NONPRIV_SLOTS))
		return;

	if (GEM_DEBUG_WARN_ON(!is_nonpriv_flags_valid(flags)))
		return;

	wa.reg.reg |= flags;
	_wa_add(wal, &wa);
}

static void
whitelist_reg(struct i915_wa_list *wal, i915_reg_t reg)
{
	whitelist_reg_ext(wal, reg, RING_FORCE_TO_NONPRIV_ACCESS_RW);
}

static void gen9_whitelist_build(struct i915_wa_list *w)
{
	/* WaVFEStateAfterPipeControlwithMediaStateClear:skl,bxt,glk,cfl */
	whitelist_reg(w, GEN9_CTX_PREEMPT_REG);

	/* WaEnablePreemptionGranularityControlByUMD:skl,bxt,kbl,cfl,[cnl] */
	whitelist_reg(w, GEN8_CS_CHICKEN1);

	/* WaAllowUMDToModifyHDCChicken1:skl,bxt,kbl,glk,cfl */
	whitelist_reg(w, GEN8_HDC_CHICKEN1);

	/* WaSendPushConstantsFromMMIO:skl,bxt */
	whitelist_reg(w, COMMON_SLICE_CHICKEN2);
}

static void skl_whitelist_build(struct intel_engine_cs *engine)
{
	struct i915_wa_list *w = &engine->whitelist;

	if (engine->class != RENDER_CLASS)
		return;

	gen9_whitelist_build(w);

	/* WaDisableLSQCROPERFforOCL:skl */
	whitelist_reg(w, GEN8_L3SQCREG4);
}

static void bxt_whitelist_build(struct intel_engine_cs *engine)
{
	if (engine->class != RENDER_CLASS)
		return;

	gen9_whitelist_build(&engine->whitelist);
}

static void kbl_whitelist_build(struct intel_engine_cs *engine)
{
	struct i915_wa_list *w = &engine->whitelist;

	if (engine->class != RENDER_CLASS)
		return;

	gen9_whitelist_build(w);

	/* WaDisableLSQCROPERFforOCL:kbl */
	whitelist_reg(w, GEN8_L3SQCREG4);
}

static void glk_whitelist_build(struct intel_engine_cs *engine)
{
	struct i915_wa_list *w = &engine->whitelist;

	if (engine->class != RENDER_CLASS)
		return;

	gen9_whitelist_build(w);

	/* WA #0862: Userspace has to set "Barrier Mode" to avoid hangs. */
	whitelist_reg(w, GEN9_SLICE_COMMON_ECO_CHICKEN1);
}

static void cfl_whitelist_build(struct intel_engine_cs *engine)
{
	struct i915_wa_list *w = &engine->whitelist;

	if (engine->class != RENDER_CLASS)
		return;

	gen9_whitelist_build(w);

	/*
	 * WaAllowPMDepthAndInvocationCountAccessFromUMD:cfl,whl,cml,aml
	 *
	 * This covers 4 register which are next to one another :
	 *   - PS_INVOCATION_COUNT
	 *   - PS_INVOCATION_COUNT_UDW
	 *   - PS_DEPTH_COUNT
	 *   - PS_DEPTH_COUNT_UDW
	 */
	whitelist_reg_ext(w, PS_INVOCATION_COUNT,
			  RING_FORCE_TO_NONPRIV_ACCESS_RD |
			  RING_FORCE_TO_NONPRIV_RANGE_4);
}

static void cml_whitelist_build(struct intel_engine_cs *engine)
{
	struct i915_wa_list *w = &engine->whitelist;

	if (engine->class != RENDER_CLASS)
		whitelist_reg_ext(w,
				  RING_CTX_TIMESTAMP(engine->mmio_base),
				  RING_FORCE_TO_NONPRIV_ACCESS_RD);

	cfl_whitelist_build(engine);
}

static void cnl_whitelist_build(struct intel_engine_cs *engine)
{
	struct i915_wa_list *w = &engine->whitelist;

	if (engine->class != RENDER_CLASS)
		return;

	/* WaEnablePreemptionGranularityControlByUMD:cnl */
	whitelist_reg(w, GEN8_CS_CHICKEN1);
}

static void icl_whitelist_build(struct intel_engine_cs *engine)
{
	struct i915_wa_list *w = &engine->whitelist;

	switch (engine->class) {
	case RENDER_CLASS:
		/* WaAllowUMDToModifyHalfSliceChicken7:icl */
		whitelist_reg(w, GEN9_HALF_SLICE_CHICKEN7);

		/* WaAllowUMDToModifySamplerMode:icl */
		whitelist_reg(w, GEN10_SAMPLER_MODE);

		/* WaEnableStateCacheRedirectToCS:icl */
		whitelist_reg(w, GEN9_SLICE_COMMON_ECO_CHICKEN1);

		/*
		 * WaAllowPMDepthAndInvocationCountAccessFromUMD:icl
		 *
		 * This covers 4 register which are next to one another :
		 *   - PS_INVOCATION_COUNT
		 *   - PS_INVOCATION_COUNT_UDW
		 *   - PS_DEPTH_COUNT
		 *   - PS_DEPTH_COUNT_UDW
		 */
		whitelist_reg_ext(w, PS_INVOCATION_COUNT,
				  RING_FORCE_TO_NONPRIV_ACCESS_RD |
				  RING_FORCE_TO_NONPRIV_RANGE_4);
		break;

	case VIDEO_DECODE_CLASS:
		/* hucStatusRegOffset */
		whitelist_reg_ext(w, _MMIO(0x2000 + engine->mmio_base),
				  RING_FORCE_TO_NONPRIV_ACCESS_RD);
		/* hucUKernelHdrInfoRegOffset */
		whitelist_reg_ext(w, _MMIO(0x2014 + engine->mmio_base),
				  RING_FORCE_TO_NONPRIV_ACCESS_RD);
		/* hucStatus2RegOffset */
		whitelist_reg_ext(w, _MMIO(0x23B0 + engine->mmio_base),
				  RING_FORCE_TO_NONPRIV_ACCESS_RD);
		whitelist_reg_ext(w,
				  RING_CTX_TIMESTAMP(engine->mmio_base),
				  RING_FORCE_TO_NONPRIV_ACCESS_RD);
		break;

	default:
		whitelist_reg_ext(w,
				  RING_CTX_TIMESTAMP(engine->mmio_base),
				  RING_FORCE_TO_NONPRIV_ACCESS_RD);
		break;
	}
}

static void tgl_whitelist_build(struct intel_engine_cs *engine)
{
	struct i915_wa_list *w = &engine->whitelist;

	switch (engine->class) {
	case RENDER_CLASS:
		/*
		 * WaAllowPMDepthAndInvocationCountAccessFromUMD:tgl
		 * Wa_1408556865:tgl
		 *
		 * This covers 4 registers which are next to one another :
		 *   - PS_INVOCATION_COUNT
		 *   - PS_INVOCATION_COUNT_UDW
		 *   - PS_DEPTH_COUNT
		 *   - PS_DEPTH_COUNT_UDW
		 */
		whitelist_reg_ext(w, PS_INVOCATION_COUNT,
				  RING_FORCE_TO_NONPRIV_ACCESS_RD |
				  RING_FORCE_TO_NONPRIV_RANGE_4);

		/* Wa_1808121037:tgl */
		whitelist_reg(w, GEN7_COMMON_SLICE_CHICKEN1);

		/* Wa_1806527549:tgl */
		whitelist_reg(w, HIZ_CHICKEN);
		break;
	default:
		whitelist_reg_ext(w,
				  RING_CTX_TIMESTAMP(engine->mmio_base),
				  RING_FORCE_TO_NONPRIV_ACCESS_RD);
		break;
	}
}

static void dg1_whitelist_build(struct intel_engine_cs *engine)
{
	struct i915_wa_list *w = &engine->whitelist;

	tgl_whitelist_build(engine);

	/* GEN:BUG:1409280441:dg1 */
	if (IS_DG1_REVID(engine->i915, DG1_REVID_A0, DG1_REVID_A0) &&
	    (engine->class == RENDER_CLASS ||
	     engine->class == COPY_ENGINE_CLASS))
		whitelist_reg_ext(w, RING_ID(engine->mmio_base),
				  RING_FORCE_TO_NONPRIV_ACCESS_RD);
}

void intel_engine_init_whitelist(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;
	struct i915_wa_list *w = &engine->whitelist;

	wa_init_start(w, "whitelist", engine->name);

	if (IS_DG1(i915))
		dg1_whitelist_build(engine);
	else if (IS_GEN(i915, 12))
		tgl_whitelist_build(engine);
	else if (IS_GEN(i915, 11))
		icl_whitelist_build(engine);
	else if (IS_CANNONLAKE(i915))
		cnl_whitelist_build(engine);
	else if (IS_COMETLAKE(i915))
		cml_whitelist_build(engine);
	else if (IS_COFFEELAKE(i915))
		cfl_whitelist_build(engine);
	else if (IS_GEMINILAKE(i915))
		glk_whitelist_build(engine);
	else if (IS_KABYLAKE(i915))
		kbl_whitelist_build(engine);
	else if (IS_BROXTON(i915))
		bxt_whitelist_build(engine);
	else if (IS_SKYLAKE(i915))
		skl_whitelist_build(engine);
	else if (INTEL_GEN(i915) <= 8)
		return;
	else
		MISSING_CASE(INTEL_GEN(i915));

	wa_init_finish(w);
}

void intel_engine_apply_whitelist(struct intel_engine_cs *engine)
{
	const struct i915_wa_list *wal = &engine->whitelist;
	struct intel_uncore *uncore = engine->uncore;
	const u32 base = engine->mmio_base;
	struct i915_wa *wa;
	unsigned int i;

	if (!wal->count)
		return;

	for (i = 0, wa = wal->list; i < wal->count; i++, wa++)
		intel_uncore_write(uncore,
				   RING_FORCE_TO_NONPRIV(base, i),
				   i915_mmio_reg_offset(wa->reg));

	/* And clear the rest just in case of garbage */
	for (; i < RING_MAX_NONPRIV_SLOTS; i++)
		intel_uncore_write(uncore,
				   RING_FORCE_TO_NONPRIV(base, i),
				   i915_mmio_reg_offset(RING_NOPID(base)));
}

static void
rcs_engine_wa_init(struct intel_engine_cs *engine, struct i915_wa_list *wal)
{
	struct drm_i915_private *i915 = engine->i915;

	if (IS_DG1_REVID(i915, DG1_REVID_A0, DG1_REVID_A0) ||
	    IS_TGL_UY_GT_REVID(i915, TGL_REVID_A0, TGL_REVID_A0)) {
		/*
		 * Wa_1607138336:tgl[a0],dg1[a0]
		 * Wa_1607063988:tgl[a0],dg1[a0]
		 */
		wa_write_or(wal,
			    GEN9_CTX_PREEMPT_REG,
			    GEN12_DISABLE_POSH_BUSY_FF_DOP_CG);
	}

	if (IS_TGL_UY_GT_REVID(i915, TGL_REVID_A0, TGL_REVID_A0)) {
		/*
		 * Wa_1606679103:tgl
		 * (see also Wa_1606682166:icl)
		 */
		wa_write_or(wal,
			    GEN7_SARCHKMD,
			    GEN7_DISABLE_SAMPLER_PREFETCH);
	}

	if (IS_DG1(i915) || IS_ROCKETLAKE(i915) || IS_TIGERLAKE(i915)) {
		/* Wa_1606931601:tgl,rkl,dg1 */
		wa_masked_en(wal, GEN7_ROW_CHICKEN2, GEN12_DISABLE_EARLY_READ);

		/*
		 * Wa_1407928979:tgl A*
		 * Wa_18011464164:tgl[B0+],dg1[B0+]
		 * Wa_22010931296:tgl[B0+],dg1[B0+]
		 * Wa_14010919138:rkl, dg1
		 */
		wa_write_or(wal, GEN7_FF_THREAD_MODE,
			    GEN12_FF_TESSELATION_DOP_GATE_DISABLE);

		/*
		 * Wa_1606700617:tgl,dg1
		 * Wa_22010271021:tgl,rkl,dg1
		 */
		wa_masked_en(wal,
			     GEN9_CS_DEBUG_MODE1,
			     FF_DOP_CLOCK_GATE_DISABLE);

		/* Wa_1406941453:tgl,rkl,dg1 */
		wa_masked_en(wal,
			     GEN10_SAMPLER_MODE,
			     ENABLE_SMALLPL);
	}

	if (IS_DG1_REVID(i915, DG1_REVID_A0, DG1_REVID_A0) ||
	    IS_ROCKETLAKE(i915) || IS_TIGERLAKE(i915)) {
		/* Wa_1409804808:tgl,rkl,dg1[a0] */
		wa_masked_en(wal, GEN7_ROW_CHICKEN2,
			     GEN12_PUSH_CONST_DEREF_HOLD_DIS);

		/*
		 * Wa_1409085225:tgl
		 * Wa_14010229206:tgl,rkl,dg1[a0]
		 */
		wa_masked_en(wal, GEN9_ROW_CHICKEN4, GEN12_DISABLE_TDL_PUSH);

		/*
		 * Wa_1607030317:tgl
		 * Wa_1607186500:tgl
		 * Wa_1607297627:tgl,rkl,dg1[a0]
		 *
		 * On TGL and RKL there are multiple entries for this WA in the
		 * BSpec; some indicate this is an A0-only WA, others indicate
		 * it applies to all steppings so we trust the "all steppings."
		 * For DG1 this only applies to A0.
		 */
		wa_masked_en(wal,
			     GEN6_RC_SLEEP_PSMI_CONTROL,
			     GEN12_WAIT_FOR_EVENT_POWER_DOWN_DISABLE |
			     GEN8_RC_SEMA_IDLE_MSG_DISABLE);
	}

	if (IS_GEN(i915, 11)) {
		/* This is not an Wa. Enable for better image quality */
		wa_masked_en(wal,
			     _3D_CHICKEN3,
			     _3D_CHICKEN3_AA_LINE_QUALITY_FIX_ENABLE);

		/* WaPipelineFlushCoherentLines:icl */
		wa_write_or(wal,
			    GEN8_L3SQCREG4,
			    GEN8_LQSC_FLUSH_COHERENT_LINES);

		/*
		 * Wa_1405543622:icl
		 * Formerly known as WaGAPZPriorityScheme
		 */
		wa_write_or(wal,
			    GEN8_GARBCNTL,
			    GEN11_ARBITRATION_PRIO_ORDER_MASK);

		/*
		 * Wa_1604223664:icl
		 * Formerly known as WaL3BankAddressHashing
		 */
		wa_write_clr_set(wal,
				 GEN8_GARBCNTL,
				 GEN11_HASH_CTRL_EXCL_MASK,
				 GEN11_HASH_CTRL_EXCL_BIT0);
		wa_write_clr_set(wal,
				 GEN11_GLBLINVL,
				 GEN11_BANK_HASH_ADDR_EXCL_MASK,
				 GEN11_BANK_HASH_ADDR_EXCL_BIT0);

		/*
		 * Wa_1405733216:icl
		 * Formerly known as WaDisableCleanEvicts
		 */
		wa_write_or(wal,
			    GEN8_L3SQCREG4,
			    GEN11_LQSC_CLEAN_EVICT_DISABLE);

		/* WaForwardProgressSoftReset:icl */
		wa_write_or(wal,
			    GEN10_SCRATCH_LNCF2,
			    PMFLUSHDONE_LNICRSDROP |
			    PMFLUSH_GAPL3UNBLOCK |
			    PMFLUSHDONE_LNEBLK);

		/* Wa_1406609255:icl (pre-prod) */
		if (IS_ICL_REVID(i915, ICL_REVID_A0, ICL_REVID_B0))
			wa_write_or(wal,
				    GEN7_SARCHKMD,
				    GEN7_DISABLE_DEMAND_PREFETCH);

		/* Wa_1606682166:icl */
		wa_write_or(wal,
			    GEN7_SARCHKMD,
			    GEN7_DISABLE_SAMPLER_PREFETCH);

		/* Wa_1409178092:icl */
		wa_write_clr_set(wal,
				 GEN11_SCRATCH2,
				 GEN11_COHERENT_PARTIAL_WRITE_MERGE_ENABLE,
				 0);

		/* WaEnable32PlaneMode:icl */
		wa_masked_en(wal, GEN9_CSFE_CHICKEN1_RCS,
			     GEN11_ENABLE_32_PLANE_MODE);

		/*
		 * Wa_1408615072:icl,ehl  (vsunit)
		 * Wa_1407596294:icl,ehl  (hsunit)
		 */
		wa_write_or(wal, UNSLICE_UNIT_LEVEL_CLKGATE,
			    VSUNIT_CLKGATE_DIS | HSUNIT_CLKGATE_DIS);

		/* Wa_1407352427:icl,ehl */
		wa_write_or(wal, UNSLICE_UNIT_LEVEL_CLKGATE2,
			    PSDUNIT_CLKGATE_DIS);

		/* Wa_1406680159:icl,ehl */
		wa_write_or(wal,
			    SUBSLICE_UNIT_LEVEL_CLKGATE,
			    GWUNIT_CLKGATE_DIS);

		/*
		 * Wa_1408767742:icl[a2..forever],ehl[all]
		 * Wa_1605460711:icl[a0..c0]
		 */
		wa_write_or(wal,
			    GEN7_FF_THREAD_MODE,
			    GEN12_FF_TESSELATION_DOP_GATE_DISABLE);

		/* Wa_22010271021:ehl */
		if (IS_JSL_EHL(i915))
			wa_masked_en(wal,
				     GEN9_CS_DEBUG_MODE1,
				     FF_DOP_CLOCK_GATE_DISABLE);
	}

	if (IS_GEN_RANGE(i915, 9, 12)) {
		/* FtrPerCtxtPreemptionGranularityControl:skl,bxt,kbl,cfl,cnl,icl,tgl */
		wa_masked_en(wal,
			     GEN7_FF_SLICE_CS_CHICKEN1,
			     GEN9_FFSC_PERCTX_PREEMPT_CTRL);
	}

	if (IS_SKYLAKE(i915) ||
	    IS_KABYLAKE(i915) ||
	    IS_COFFEELAKE(i915) ||
	    IS_COMETLAKE(i915)) {
		/* WaEnableGapsTsvCreditFix:skl,kbl,cfl */
		wa_write_or(wal,
			    GEN8_GARBCNTL,
			    GEN9_GAPS_TSV_CREDIT_DISABLE);
	}

	if (IS_BROXTON(i915)) {
		/* WaDisablePooledEuLoadBalancingFix:bxt */
		wa_masked_en(wal,
			     FF_SLICE_CS_CHICKEN2,
			     GEN9_POOLED_EU_LOAD_BALANCING_FIX_DISABLE);
	}

	if (IS_GEN(i915, 9)) {
		/* WaContextSwitchWithConcurrentTLBInvalidate:skl,bxt,kbl,glk,cfl */
		wa_masked_en(wal,
			     GEN9_CSFE_CHICKEN1_RCS,
			     GEN9_PREEMPT_GPGPU_SYNC_SWITCH_DISABLE);

		/* WaEnableLbsSlaRetryTimerDecrement:skl,bxt,kbl,glk,cfl */
		wa_write_or(wal,
			    BDW_SCRATCH1,
			    GEN9_LBS_SLA_RETRY_TIMER_DECREMENT_ENABLE);

		/* WaProgramL3SqcReg1DefaultForPerf:bxt,glk */
		if (IS_GEN9_LP(i915))
			wa_write_clr_set(wal,
					 GEN8_L3SQCREG1,
					 L3_PRIO_CREDITS_MASK,
					 L3_GENERAL_PRIO_CREDITS(62) |
					 L3_HIGH_PRIO_CREDITS(2));

		/* WaOCLCoherentLineFlush:skl,bxt,kbl,cfl */
		wa_write_or(wal,
			    GEN8_L3SQCREG4,
			    GEN8_LQSC_FLUSH_COHERENT_LINES);
	}

	if (IS_GEN(i915, 7))
		/* WaBCSVCSTlbInvalidationMode:ivb,vlv,hsw */
		wa_masked_en(wal,
			     GFX_MODE_GEN7,
			     GFX_TLB_INVALIDATE_EXPLICIT | GFX_REPLAY_MODE);

	if (IS_GEN_RANGE(i915, 6, 7))
		/*
		 * We need to disable the AsyncFlip performance optimisations in
		 * order to use MI_WAIT_FOR_EVENT within the CS. It should
		 * already be programmed to '1' on all products.
		 *
		 * WaDisableAsyncFlipPerfMode:snb,ivb,hsw,vlv
		 */
		wa_masked_en(wal,
			     MI_MODE,
			     ASYNC_FLIP_PERF_DISABLE);

	if (IS_GEN(i915, 6)) {
		/*
		 * Required for the hardware to program scanline values for
		 * waiting
		 * WaEnableFlushTlbInvalidationMode:snb
		 */
		wa_masked_en(wal,
			     GFX_MODE,
			     GFX_TLB_INVALIDATE_EXPLICIT);

		/*
		 * From the Sandybridge PRM, volume 1 part 3, page 24:
		 * "If this bit is set, STCunit will have LRA as replacement
		 *  policy. [...] This bit must be reset. LRA replacement
		 *  policy is not supported."
		 */
		wa_masked_dis(wal,
			      CACHE_MODE_0,
			      CM0_STC_EVICT_DISABLE_LRA_SNB);
	}

	if (IS_GEN_RANGE(i915, 4, 6))
		/* WaTimedSingleVertexDispatch:cl,bw,ctg,elk,ilk,snb */
		wa_add(wal, MI_MODE,
		       0, _MASKED_BIT_ENABLE(VS_TIMER_DISPATCH),
		       /* XXX bit doesn't stick on Broadwater */
		       IS_I965G(i915) ? 0 : VS_TIMER_DISPATCH);

	if (IS_GEN(i915, 4))
		/*
		 * Disable CONSTANT_BUFFER before it is loaded from the context
		 * image. For as it is loaded, it is executed and the stored
		 * address may no longer be valid, leading to a GPU hang.
		 *
		 * This imposes the requirement that userspace reload their
		 * CONSTANT_BUFFER on every batch, fortunately a requirement
		 * they are already accustomed to from before contexts were
		 * enabled.
		 */
		wa_add(wal, ECOSKPD,
		       0, _MASKED_BIT_ENABLE(ECO_CONSTANT_BUFFER_SR_DISABLE),
		       0 /* XXX bit doesn't stick on Broadwater */);
}

static void
xcs_engine_wa_init(struct intel_engine_cs *engine, struct i915_wa_list *wal)
{
	struct drm_i915_private *i915 = engine->i915;

	/* WaKBLVECSSemaphoreWaitPoll:kbl */
	if (IS_KBL_GT_REVID(i915, KBL_REVID_A0, KBL_REVID_E0)) {
		wa_write(wal,
			 RING_SEMA_WAIT_POLL(engine->mmio_base),
			 1);
	}
}

static void
engine_init_workarounds(struct intel_engine_cs *engine, struct i915_wa_list *wal)
{
	if (I915_SELFTEST_ONLY(INTEL_GEN(engine->i915) < 4))
		return;

	if (engine->class == RENDER_CLASS)
		rcs_engine_wa_init(engine, wal);
	else
		xcs_engine_wa_init(engine, wal);
}

void intel_engine_init_workarounds(struct intel_engine_cs *engine)
{
	struct i915_wa_list *wal = &engine->wa_list;

	if (INTEL_GEN(engine->i915) < 4)
		return;

	wa_init_start(wal, "engine", engine->name);
	engine_init_workarounds(engine, wal);
	wa_init_finish(wal);
}

void intel_engine_apply_workarounds(struct intel_engine_cs *engine)
{
	wa_list_apply(engine->uncore, &engine->wa_list);
}

static struct i915_vma *
create_scratch(struct i915_address_space *vm, int count)
{
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	unsigned int size;
	int err;

	size = round_up(count * sizeof(u32), PAGE_SIZE);
	obj = i915_gem_object_create_internal(vm->i915, size);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	i915_gem_object_set_cache_coherency(obj, I915_CACHE_LLC);

	vma = i915_vma_instance(obj, vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err_obj;
	}

	err = i915_vma_pin(vma, 0, 0,
			   i915_vma_is_ggtt(vma) ? PIN_GLOBAL : PIN_USER);
	if (err)
		goto err_obj;

	return vma;

err_obj:
	i915_gem_object_put(obj);
	return ERR_PTR(err);
}

struct mcr_range {
	u32 start;
	u32 end;
};

static const struct mcr_range mcr_ranges_gen8[] = {
	{ .start = 0x5500, .end = 0x55ff },
	{ .start = 0x7000, .end = 0x7fff },
	{ .start = 0x9400, .end = 0x97ff },
	{ .start = 0xb000, .end = 0xb3ff },
	{ .start = 0xe000, .end = 0xe7ff },
	{},
};

static const struct mcr_range mcr_ranges_gen12[] = {
	{ .start =  0x8150, .end =  0x815f },
	{ .start =  0x9520, .end =  0x955f },
	{ .start =  0xb100, .end =  0xb3ff },
	{ .start =  0xde80, .end =  0xe8ff },
	{ .start = 0x24a00, .end = 0x24a7f },
	{},
};

static bool mcr_range(struct drm_i915_private *i915, u32 offset)
{
	const struct mcr_range *mcr_ranges;
	int i;

	if (INTEL_GEN(i915) >= 12)
		mcr_ranges = mcr_ranges_gen12;
	else if (INTEL_GEN(i915) >= 8)
		mcr_ranges = mcr_ranges_gen8;
	else
		return false;

	/*
	 * Registers in these ranges are affected by the MCR selector
	 * which only controls CPU initiated MMIO. Routing does not
	 * work for CS access so we cannot verify them on this path.
	 */
	for (i = 0; mcr_ranges[i].start; i++)
		if (offset >= mcr_ranges[i].start &&
		    offset <= mcr_ranges[i].end)
			return true;

	return false;
}

static int
wa_list_srm(struct i915_request *rq,
	    const struct i915_wa_list *wal,
	    struct i915_vma *vma)
{
	struct drm_i915_private *i915 = rq->engine->i915;
	unsigned int i, count = 0;
	const struct i915_wa *wa;
	u32 srm, *cs;

	srm = MI_STORE_REGISTER_MEM | MI_SRM_LRM_GLOBAL_GTT;
	if (INTEL_GEN(i915) >= 8)
		srm++;

	for (i = 0, wa = wal->list; i < wal->count; i++, wa++) {
		if (!mcr_range(i915, i915_mmio_reg_offset(wa->reg)))
			count++;
	}

	cs = intel_ring_begin(rq, 4 * count);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	for (i = 0, wa = wal->list; i < wal->count; i++, wa++) {
		u32 offset = i915_mmio_reg_offset(wa->reg);

		if (mcr_range(i915, offset))
			continue;

		*cs++ = srm;
		*cs++ = offset;
		*cs++ = i915_ggtt_offset(vma) + sizeof(u32) * i;
		*cs++ = 0;
	}
	intel_ring_advance(rq, cs);

	return 0;
}

static int engine_wa_list_verify(struct intel_context *ce,
				 const struct i915_wa_list * const wal,
				 const char *from)
{
	const struct i915_wa *wa;
	struct i915_request *rq;
	struct i915_vma *vma;
	struct i915_gem_ww_ctx ww;
	unsigned int i;
	u32 *results;
	int err;

	if (!wal->count)
		return 0;

	vma = create_scratch(&ce->engine->gt->ggtt->vm, wal->count);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	intel_engine_pm_get(ce->engine);
	i915_gem_ww_ctx_init(&ww, false);
retry:
	err = i915_gem_object_lock(vma->obj, &ww);
	if (err == 0)
		err = intel_context_pin_ww(ce, &ww);
	if (err)
		goto err_pm;

	rq = i915_request_create(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_unpin;
	}

	err = i915_request_await_object(rq, vma->obj, true);
	if (err == 0)
		err = i915_vma_move_to_active(vma, rq, EXEC_OBJECT_WRITE);
	if (err == 0)
		err = wa_list_srm(rq, wal, vma);

	i915_request_get(rq);
	if (err)
		i915_request_set_error_once(rq, err);
	i915_request_add(rq);

	if (err)
		goto err_rq;

	if (i915_request_wait(rq, 0, HZ / 5) < 0) {
		err = -ETIME;
		goto err_rq;
	}

	results = i915_gem_object_pin_map(vma->obj, I915_MAP_WB);
	if (IS_ERR(results)) {
		err = PTR_ERR(results);
		goto err_rq;
	}

	err = 0;
	for (i = 0, wa = wal->list; i < wal->count; i++, wa++) {
		if (mcr_range(rq->engine->i915, i915_mmio_reg_offset(wa->reg)))
			continue;

		if (!wa_verify(wa, results[i], wal->name, from))
			err = -ENXIO;
	}

	i915_gem_object_unpin_map(vma->obj);

err_rq:
	i915_request_put(rq);
err_unpin:
	intel_context_unpin(ce);
err_pm:
	if (err == -EDEADLK) {
		err = i915_gem_ww_ctx_backoff(&ww);
		if (!err)
			goto retry;
	}
	i915_gem_ww_ctx_fini(&ww);
	intel_engine_pm_put(ce->engine);
	i915_vma_unpin(vma);
	i915_vma_put(vma);
	return err;
}

int intel_engine_verify_workarounds(struct intel_engine_cs *engine,
				    const char *from)
{
	return engine_wa_list_verify(engine->kernel_context,
				     &engine->wa_list,
				     from);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftest_workarounds.c"
#endif

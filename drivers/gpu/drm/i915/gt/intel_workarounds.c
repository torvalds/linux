// SPDX-License-Identifier: MIT
/*
 * Copyright © 2014-2018 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_context.h"
#include "intel_engine_pm.h"
#include "intel_engine_regs.h"
#include "intel_gpu_commands.h"
#include "intel_gt.h"
#include "intel_gt_regs.h"
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
		   u32 clear, u32 set, u32 read_mask, bool masked_reg)
{
	struct i915_wa wa = {
		.reg  = reg,
		.clr  = clear,
		.set  = set,
		.read = read_mask,
		.masked_reg = masked_reg,
	};

	_wa_add(wal, &wa);
}

static void
wa_write_clr_set(struct i915_wa_list *wal, i915_reg_t reg, u32 clear, u32 set)
{
	wa_add(wal, reg, clear, set, clear, false);
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
	wa_add(wal, reg, 0, _MASKED_BIT_ENABLE(val), val, true);
}

static void
wa_masked_dis(struct i915_wa_list *wal, i915_reg_t reg, u32 val)
{
	wa_add(wal, reg, 0, _MASKED_BIT_DISABLE(val), val, true);
}

static void
wa_masked_field_set(struct i915_wa_list *wal, i915_reg_t reg,
		    u32 mask, u32 val)
{
	wa_add(wal, reg, 0, _MASKED_FIELD(mask, val), mask, true);
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
	 * workaround for a possible hang in the unlikely event a TLB
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
	if (IS_KBL_GRAPHICS_STEP(i915, STEP_C0, STEP_FOREVER))
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

static void icl_ctx_workarounds_init(struct intel_engine_cs *engine,
				     struct i915_wa_list *wal)
{
	/* Wa_1406697149 (WaDisableBankHangMode:icl) */
	wa_write(wal,
		 GEN8_L3CNTLREG,
		 intel_uncore_read(engine->uncore, GEN8_L3CNTLREG) |
		 GEN8_ERRDETBCTRL);

	/* WaForceEnableNonCoherent:icl
	 * This is not the same workaround as in early Gen9 platforms, where
	 * lacking this could cause system hangs, but coherency performance
	 * overhead is high and only a few compute workloads really need it
	 * (the register is whitelisted in hardware now, so UMDs can opt in
	 * for coherency if they have a good reason).
	 */
	wa_masked_en(wal, ICL_HDC_MODE, HDC_FORCE_NON_COHERENT);

	/* WaEnableFloatBlendOptimization:icl */
	wa_add(wal, GEN10_CACHE_MODE_SS, 0,
	       _MASKED_BIT_ENABLE(FLOAT_BLEND_OPTIMIZATION_ENABLE),
	       0 /* write-only, so skip validation */,
	       true);

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

/*
 * These settings aren't actually workarounds, but general tuning settings that
 * need to be programmed on dg2 platform.
 */
static void dg2_ctx_gt_tuning_init(struct intel_engine_cs *engine,
				   struct i915_wa_list *wal)
{
	wa_write_clr_set(wal, GEN11_L3SQCREG5, L3_PWM_TIMER_INIT_VAL_MASK,
			 REG_FIELD_PREP(L3_PWM_TIMER_INIT_VAL_MASK, 0x7f));
	wa_add(wal,
	       FF_MODE2,
	       FF_MODE2_TDS_TIMER_MASK,
	       FF_MODE2_TDS_TIMER_128,
	       0, false);
}

/*
 * These settings aren't actually workarounds, but general tuning settings that
 * need to be programmed on several platforms.
 */
static void gen12_ctx_gt_tuning_init(struct intel_engine_cs *engine,
				     struct i915_wa_list *wal)
{
	/*
	 * Although some platforms refer to it as Wa_1604555607, we need to
	 * program it even on those that don't explicitly list that
	 * workaround.
	 *
	 * Note that the programming of this register is further modified
	 * according to the FF_MODE2 guidance given by Wa_1608008084:gen12.
	 * Wa_1608008084 tells us the FF_MODE2 register will return the wrong
	 * value when read. The default value for this register is zero for all
	 * fields and there are no bit masks. So instead of doing a RMW we
	 * should just write TDS timer value. For the same reason read
	 * verification is ignored.
	 */
	wa_add(wal,
	       FF_MODE2,
	       FF_MODE2_TDS_TIMER_MASK,
	       FF_MODE2_TDS_TIMER_128,
	       0, false);
}

static void gen12_ctx_workarounds_init(struct intel_engine_cs *engine,
				       struct i915_wa_list *wal)
{
	gen12_ctx_gt_tuning_init(engine, wal);

	/*
	 * Wa_1409142259:tgl,dg1,adl-p
	 * Wa_1409347922:tgl,dg1,adl-p
	 * Wa_1409252684:tgl,dg1,adl-p
	 * Wa_1409217633:tgl,dg1,adl-p
	 * Wa_1409207793:tgl,dg1,adl-p
	 * Wa_1409178076:tgl,dg1,adl-p
	 * Wa_1408979724:tgl,dg1,adl-p
	 * Wa_14010443199:tgl,rkl,dg1,adl-p
	 * Wa_14010698770:tgl,rkl,dg1,adl-s,adl-p
	 * Wa_1409342910:tgl,rkl,dg1,adl-s,adl-p
	 */
	wa_masked_en(wal, GEN11_COMMON_SLICE_CHICKEN3,
		     GEN12_DISABLE_CPS_AWARE_COLOR_PIPE);

	/* WaDisableGPGPUMidThreadPreemption:gen12 */
	wa_masked_field_set(wal, GEN8_CS_CHICKEN1,
			    GEN9_PREEMPT_GPGPU_LEVEL_MASK,
			    GEN9_PREEMPT_GPGPU_THREAD_GROUP_LEVEL);

	/*
	 * Wa_16011163337
	 *
	 * Like in gen12_ctx_gt_tuning_init(), read verification is ignored due
	 * to Wa_1608008084.
	 */
	wa_add(wal,
	       FF_MODE2,
	       FF_MODE2_GS_TIMER_MASK,
	       FF_MODE2_GS_TIMER_224,
	       0, false);
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
}

static void dg2_ctx_workarounds_init(struct intel_engine_cs *engine,
				     struct i915_wa_list *wal)
{
	dg2_ctx_gt_tuning_init(engine, wal);

	/* Wa_16011186671:dg2_g11 */
	if (IS_DG2_GRAPHICS_STEP(engine->i915, G11, STEP_A0, STEP_B0)) {
		wa_masked_dis(wal, VFLSKPD, DIS_MULT_MISS_RD_SQUASH);
		wa_masked_en(wal, VFLSKPD, DIS_OVER_FETCH_CACHE);
	}

	if (IS_DG2_GRAPHICS_STEP(engine->i915, G10, STEP_A0, STEP_B0)) {
		/* Wa_14010469329:dg2_g10 */
		wa_masked_en(wal, GEN11_COMMON_SLICE_CHICKEN3,
			     XEHP_DUAL_SIMD8_SEQ_MERGE_DISABLE);

		/*
		 * Wa_22010465075:dg2_g10
		 * Wa_22010613112:dg2_g10
		 * Wa_14010698770:dg2_g10
		 */
		wa_masked_en(wal, GEN11_COMMON_SLICE_CHICKEN3,
			     GEN12_DISABLE_CPS_AWARE_COLOR_PIPE);
	}

	/* Wa_16013271637:dg2 */
	wa_masked_en(wal, SLICE_COMMON_ECO_CHICKEN1,
		     MSC_MSAA_REODER_BUF_BYPASS_DISABLE);

	/* Wa_22012532006:dg2 */
	if (IS_DG2_GRAPHICS_STEP(engine->i915, G10, STEP_A0, STEP_C0) ||
	    IS_DG2_GRAPHICS_STEP(engine->i915, G11, STEP_A0, STEP_B0))
		wa_masked_en(wal, GEN9_HALF_SLICE_CHICKEN7,
			     DG2_DISABLE_ROUND_ENABLE_ALLOW_FOR_SSLA);
}

static void fakewa_disable_nestedbb_mode(struct intel_engine_cs *engine,
					 struct i915_wa_list *wal)
{
	/*
	 * This is a "fake" workaround defined by software to ensure we
	 * maintain reliable, backward-compatible behavior for userspace with
	 * regards to how nested MI_BATCH_BUFFER_START commands are handled.
	 *
	 * The per-context setting of MI_MODE[12] determines whether the bits
	 * of a nested MI_BATCH_BUFFER_START instruction should be interpreted
	 * in the traditional manner or whether they should instead use a new
	 * tgl+ meaning that breaks backward compatibility, but allows nesting
	 * into 3rd-level batchbuffers.  When this new capability was first
	 * added in TGL, it remained off by default unless a context
	 * intentionally opted in to the new behavior.  However Xe_HPG now
	 * flips this on by default and requires that we explicitly opt out if
	 * we don't want the new behavior.
	 *
	 * From a SW perspective, we want to maintain the backward-compatible
	 * behavior for userspace, so we'll apply a fake workaround to set it
	 * back to the legacy behavior on platforms where the hardware default
	 * is to break compatibility.  At the moment there is no Linux
	 * userspace that utilizes third-level batchbuffers, so this will avoid
	 * userspace from needing to make any changes.  using the legacy
	 * meaning is the correct thing to do.  If/when we have userspace
	 * consumers that want to utilize third-level batch nesting, we can
	 * provide a context parameter to allow them to opt-in.
	 */
	wa_masked_dis(wal, RING_MI_MODE(engine->mmio_base), TGL_NESTED_BB_EN);
}

static void gen12_ctx_gt_mocs_init(struct intel_engine_cs *engine,
				   struct i915_wa_list *wal)
{
	u8 mocs;

	/*
	 * Some blitter commands do not have a field for MOCS, those
	 * commands will use MOCS index pointed by BLIT_CCTL.
	 * BLIT_CCTL registers are needed to be programmed to un-cached.
	 */
	if (engine->class == COPY_ENGINE_CLASS) {
		mocs = engine->gt->mocs.uc_index;
		wa_write_clr_set(wal,
				 BLIT_CCTL(engine->mmio_base),
				 BLIT_CCTL_MASK,
				 BLIT_CCTL_MOCS(mocs, mocs));
	}
}

/*
 * gen12_ctx_gt_fake_wa_init() aren't programmingan official workaround
 * defined by the hardware team, but it programming general context registers.
 * Adding those context register programming in context workaround
 * allow us to use the wa framework for proper application and validation.
 */
static void
gen12_ctx_gt_fake_wa_init(struct intel_engine_cs *engine,
			  struct i915_wa_list *wal)
{
	if (GRAPHICS_VER_FULL(engine->i915) >= IP_VER(12, 55))
		fakewa_disable_nestedbb_mode(engine, wal);

	gen12_ctx_gt_mocs_init(engine, wal);
}

static void
__intel_engine_init_ctx_wa(struct intel_engine_cs *engine,
			   struct i915_wa_list *wal,
			   const char *name)
{
	struct drm_i915_private *i915 = engine->i915;

	wa_init_start(wal, name, engine->name);

	/* Applies to all engines */
	/*
	 * Fake workarounds are not the actual workaround but
	 * programming of context registers using workaround framework.
	 */
	if (GRAPHICS_VER(i915) >= 12)
		gen12_ctx_gt_fake_wa_init(engine, wal);

	if (engine->class != RENDER_CLASS)
		goto done;

	if (IS_DG2(i915))
		dg2_ctx_workarounds_init(engine, wal);
	else if (IS_XEHPSDV(i915))
		; /* noop; none at this time */
	else if (IS_DG1(i915))
		dg1_ctx_workarounds_init(engine, wal);
	else if (GRAPHICS_VER(i915) == 12)
		gen12_ctx_workarounds_init(engine, wal);
	else if (GRAPHICS_VER(i915) == 11)
		icl_ctx_workarounds_init(engine, wal);
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
	else if (GRAPHICS_VER(i915) == 7)
		gen7_ctx_workarounds_init(engine, wal);
	else if (GRAPHICS_VER(i915) == 6)
		gen6_ctx_workarounds_init(engine, wal);
	else if (GRAPHICS_VER(i915) < 8)
		;
	else
		MISSING_CASE(GRAPHICS_VER(i915));

done:
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
gen4_gt_workarounds_init(struct intel_gt *gt,
			 struct i915_wa_list *wal)
{
	/* WaDisable_RenderCache_OperationalFlush:gen4,ilk */
	wa_masked_dis(wal, CACHE_MODE_0, RC_OP_FLUSH_ENABLE);
}

static void
g4x_gt_workarounds_init(struct intel_gt *gt, struct i915_wa_list *wal)
{
	gen4_gt_workarounds_init(gt, wal);

	/* WaDisableRenderCachePipelinedFlush:g4x,ilk */
	wa_masked_en(wal, CACHE_MODE_0, CM0_PIPELINED_RENDER_FLUSH_DISABLE);
}

static void
ilk_gt_workarounds_init(struct intel_gt *gt, struct i915_wa_list *wal)
{
	g4x_gt_workarounds_init(gt, wal);

	wa_masked_en(wal, _3D_CHICKEN2, _3D_CHICKEN2_WM_READ_PIPELINED);
}

static void
snb_gt_workarounds_init(struct intel_gt *gt, struct i915_wa_list *wal)
{
}

static void
ivb_gt_workarounds_init(struct intel_gt *gt, struct i915_wa_list *wal)
{
	/* Apply the WaDisableRHWOOptimizationForRenderHang:ivb workaround. */
	wa_masked_dis(wal,
		      GEN7_COMMON_SLICE_CHICKEN1,
		      GEN7_CSC1_RHWO_OPT_DISABLE_IN_RCC);

	/* WaApplyL3ControlAndL3ChickenMode:ivb */
	wa_write(wal, GEN7_L3CNTLREG1, GEN7_WA_FOR_GEN7_L3_CONTROL);
	wa_write(wal, GEN7_L3_CHICKEN_MODE_REGISTER, GEN7_WA_L3_CHICKEN_MODE);

	/* WaForceL3Serialization:ivb */
	wa_write_clr(wal, GEN7_L3SQCREG4, L3SQ_URB_READ_CAM_MATCH_DISABLE);
}

static void
vlv_gt_workarounds_init(struct intel_gt *gt, struct i915_wa_list *wal)
{
	/* WaForceL3Serialization:vlv */
	wa_write_clr(wal, GEN7_L3SQCREG4, L3SQ_URB_READ_CAM_MATCH_DISABLE);

	/*
	 * WaIncreaseL3CreditsForVLVB0:vlv
	 * This is the hardware default actually.
	 */
	wa_write(wal, GEN7_L3SQCREG1, VLV_B0_WA_L3SQCREG1_VALUE);
}

static void
hsw_gt_workarounds_init(struct intel_gt *gt, struct i915_wa_list *wal)
{
	/* L3 caching of data atomics doesn't work -- disable it. */
	wa_write(wal, HSW_SCRATCH1, HSW_SCRATCH1_L3_DATA_ATOMICS_DISABLE);

	wa_add(wal,
	       HSW_ROW_CHICKEN3, 0,
	       _MASKED_BIT_ENABLE(HSW_ROW_CHICKEN3_L3_GLOBAL_ATOMICS_DISABLE),
	       0 /* XXX does this reg exist? */, true);

	/* WaVSRefCountFullforceMissDisable:hsw */
	wa_write_clr(wal, GEN7_FF_THREAD_MODE, GEN7_FF_VS_REF_CNT_FFME);
}

static void
gen9_wa_init_mcr(struct drm_i915_private *i915, struct i915_wa_list *wal)
{
	const struct sseu_dev_info *sseu = &to_gt(i915)->info.sseu;
	unsigned int slice, subslice;
	u32 mcr, mcr_mask;

	GEM_BUG_ON(GRAPHICS_VER(i915) != 9);

	/*
	 * WaProgramMgsrForCorrectSliceSpecificMmioReads:gen9,glk,kbl,cml
	 * Before any MMIO read into slice/subslice specific registers, MCR
	 * packet control register needs to be programmed to point to any
	 * enabled s/ss pair. Otherwise, incorrect values will be returned.
	 * This means each subsequent MMIO read will be forwarded to an
	 * specific s/ss combination, but this is OK since these registers
	 * are consistent across s/ss in almost all cases. In the rare
	 * occasions, such as INSTDONE, where this value is dependent
	 * on s/ss combo, the read should be done with read_subslice_reg.
	 */
	slice = ffs(sseu->slice_mask) - 1;
	GEM_BUG_ON(slice >= ARRAY_SIZE(sseu->subslice_mask));
	subslice = ffs(intel_sseu_get_subslices(sseu, slice));
	GEM_BUG_ON(!subslice);
	subslice--;

	/*
	 * We use GEN8_MCR..() macros to calculate the |mcr| value for
	 * Gen9 to address WaProgramMgsrForCorrectSliceSpecificMmioReads
	 */
	mcr = GEN8_MCR_SLICE(slice) | GEN8_MCR_SUBSLICE(subslice);
	mcr_mask = GEN8_MCR_SLICE_MASK | GEN8_MCR_SUBSLICE_MASK;

	drm_dbg(&i915->drm, "MCR slice:%d/subslice:%d = %x\n", slice, subslice, mcr);

	wa_write_clr_set(wal, GEN8_MCR_SELECTOR, mcr_mask, mcr);
}

static void
gen9_gt_workarounds_init(struct intel_gt *gt, struct i915_wa_list *wal)
{
	struct drm_i915_private *i915 = gt->i915;

	/* WaProgramMgsrForCorrectSliceSpecificMmioReads:glk,kbl,cml,gen9 */
	gen9_wa_init_mcr(i915, wal);

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
skl_gt_workarounds_init(struct intel_gt *gt, struct i915_wa_list *wal)
{
	gen9_gt_workarounds_init(gt, wal);

	/* WaDisableGafsUnitClkGating:skl */
	wa_write_or(wal,
		    GEN7_UCGCTL4,
		    GEN8_EU_GAUNIT_CLOCK_GATE_DISABLE);

	/* WaInPlaceDecompressionHang:skl */
	if (IS_SKL_GRAPHICS_STEP(gt->i915, STEP_A0, STEP_H0))
		wa_write_or(wal,
			    GEN9_GAMT_ECO_REG_RW_IA,
			    GAMT_ECO_ENABLE_IN_PLACE_DECOMPRESS);
}

static void
kbl_gt_workarounds_init(struct intel_gt *gt, struct i915_wa_list *wal)
{
	gen9_gt_workarounds_init(gt, wal);

	/* WaDisableDynamicCreditSharing:kbl */
	if (IS_KBL_GRAPHICS_STEP(gt->i915, 0, STEP_C0))
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
glk_gt_workarounds_init(struct intel_gt *gt, struct i915_wa_list *wal)
{
	gen9_gt_workarounds_init(gt, wal);
}

static void
cfl_gt_workarounds_init(struct intel_gt *gt, struct i915_wa_list *wal)
{
	gen9_gt_workarounds_init(gt, wal);

	/* WaDisableGafsUnitClkGating:cfl */
	wa_write_or(wal,
		    GEN7_UCGCTL4,
		    GEN8_EU_GAUNIT_CLOCK_GATE_DISABLE);

	/* WaInPlaceDecompressionHang:cfl */
	wa_write_or(wal,
		    GEN9_GAMT_ECO_REG_RW_IA,
		    GAMT_ECO_ENABLE_IN_PLACE_DECOMPRESS);
}

static void __set_mcr_steering(struct i915_wa_list *wal,
			       i915_reg_t steering_reg,
			       unsigned int slice, unsigned int subslice)
{
	u32 mcr, mcr_mask;

	mcr = GEN11_MCR_SLICE(slice) | GEN11_MCR_SUBSLICE(subslice);
	mcr_mask = GEN11_MCR_SLICE_MASK | GEN11_MCR_SUBSLICE_MASK;

	wa_write_clr_set(wal, steering_reg, mcr_mask, mcr);
}

static void __add_mcr_wa(struct intel_gt *gt, struct i915_wa_list *wal,
			 unsigned int slice, unsigned int subslice)
{
	drm_dbg(&gt->i915->drm, "MCR slice=0x%x, subslice=0x%x\n", slice, subslice);

	__set_mcr_steering(wal, GEN8_MCR_SELECTOR, slice, subslice);
}

static void
icl_wa_init_mcr(struct intel_gt *gt, struct i915_wa_list *wal)
{
	const struct sseu_dev_info *sseu = &gt->info.sseu;
	unsigned int slice, subslice;

	GEM_BUG_ON(GRAPHICS_VER(gt->i915) < 11);
	GEM_BUG_ON(hweight8(sseu->slice_mask) > 1);
	slice = 0;

	/*
	 * Although a platform may have subslices, we need to always steer
	 * reads to the lowest instance that isn't fused off.  When Render
	 * Power Gating is enabled, grabbing forcewake will only power up a
	 * single subslice (the "minconfig") if there isn't a real workload
	 * that needs to be run; this means that if we steer register reads to
	 * one of the higher subslices, we run the risk of reading back 0's or
	 * random garbage.
	 */
	subslice = __ffs(intel_sseu_get_subslices(sseu, slice));

	/*
	 * If the subslice we picked above also steers us to a valid L3 bank,
	 * then we can just rely on the default steering and won't need to
	 * worry about explicitly re-steering L3BANK reads later.
	 */
	if (gt->info.l3bank_mask & BIT(subslice))
		gt->steering_table[L3BANK] = NULL;

	__add_mcr_wa(gt, wal, slice, subslice);
}

static void
xehp_init_mcr(struct intel_gt *gt, struct i915_wa_list *wal)
{
	const struct sseu_dev_info *sseu = &gt->info.sseu;
	unsigned long slice, subslice = 0, slice_mask = 0;
	u64 dss_mask = 0;
	u32 lncf_mask = 0;
	int i;

	/*
	 * On Xe_HP the steering increases in complexity. There are now several
	 * more units that require steering and we're not guaranteed to be able
	 * to find a common setting for all of them. These are:
	 * - GSLICE (fusable)
	 * - DSS (sub-unit within gslice; fusable)
	 * - L3 Bank (fusable)
	 * - MSLICE (fusable)
	 * - LNCF (sub-unit within mslice; always present if mslice is present)
	 *
	 * We'll do our default/implicit steering based on GSLICE (in the
	 * sliceid field) and DSS (in the subsliceid field).  If we can
	 * find overlap between the valid MSLICE and/or LNCF values with
	 * a suitable GSLICE, then we can just re-use the default value and
	 * skip and explicit steering at runtime.
	 *
	 * We only need to look for overlap between GSLICE/MSLICE/LNCF to find
	 * a valid sliceid value.  DSS steering is the only type of steering
	 * that utilizes the 'subsliceid' bits.
	 *
	 * Also note that, even though the steering domain is called "GSlice"
	 * and it is encoded in the register using the gslice format, the spec
	 * says that the combined (geometry | compute) fuse should be used to
	 * select the steering.
	 */

	/* Find the potential gslice candidates */
	dss_mask = intel_sseu_get_subslices(sseu, 0);
	slice_mask = intel_slicemask_from_dssmask(dss_mask, GEN_DSS_PER_GSLICE);

	/*
	 * Find the potential LNCF candidates.  Either LNCF within a valid
	 * mslice is fine.
	 */
	for_each_set_bit(i, &gt->info.mslice_mask, GEN12_MAX_MSLICES)
		lncf_mask |= (0x3 << (i * 2));

	/*
	 * Are there any sliceid values that work for both GSLICE and LNCF
	 * steering?
	 */
	if (slice_mask & lncf_mask) {
		slice_mask &= lncf_mask;
		gt->steering_table[LNCF] = NULL;
	}

	/* How about sliceid values that also work for MSLICE steering? */
	if (slice_mask & gt->info.mslice_mask) {
		slice_mask &= gt->info.mslice_mask;
		gt->steering_table[MSLICE] = NULL;
	}

	slice = __ffs(slice_mask);
	subslice = __ffs(dss_mask >> (slice * GEN_DSS_PER_GSLICE));
	WARN_ON(subslice > GEN_DSS_PER_GSLICE);
	WARN_ON(dss_mask >> (slice * GEN_DSS_PER_GSLICE) == 0);

	__add_mcr_wa(gt, wal, slice, subslice);

	/*
	 * SQIDI ranges are special because they use different steering
	 * registers than everything else we work with.  On XeHP SDV and
	 * DG2-G10, any value in the steering registers will work fine since
	 * all instances are present, but DG2-G11 only has SQIDI instances at
	 * ID's 2 and 3, so we need to steer to one of those.  For simplicity
	 * we'll just steer to a hardcoded "2" since that value will work
	 * everywhere.
	 */
	__set_mcr_steering(wal, MCFG_MCR_SELECTOR, 0, 2);
	__set_mcr_steering(wal, SF_MCR_SELECTOR, 0, 2);
}

static void
icl_gt_workarounds_init(struct intel_gt *gt, struct i915_wa_list *wal)
{
	struct drm_i915_private *i915 = gt->i915;

	icl_wa_init_mcr(gt, wal);

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

	/* Wa_1406463099:icl
	 * Formerly known as WaGamTlbPendError
	 */
	wa_write_or(wal,
		    GAMT_CHKN_BIT_REG,
		    GAMT_CHKN_DISABLE_L3_COH_PIPE);

	/* Wa_1407352427:icl,ehl */
	wa_write_or(wal, UNSLICE_UNIT_LEVEL_CLKGATE2,
		    PSDUNIT_CLKGATE_DIS);

	/* Wa_1406680159:icl,ehl */
	wa_write_or(wal,
		    SUBSLICE_UNIT_LEVEL_CLKGATE,
		    GWUNIT_CLKGATE_DIS);

	/* Wa_1607087056:icl,ehl,jsl */
	if (IS_ICELAKE(i915) ||
	    IS_JSL_EHL_GRAPHICS_STEP(i915, STEP_A0, STEP_B0))
		wa_write_or(wal,
			    SLICE_UNIT_LEVEL_CLKGATE,
			    L3_CLKGATE_DIS | L3_CR2X_CLKGATE_DIS);

	/*
	 * This is not a documented workaround, but rather an optimization
	 * to reduce sampler power.
	 */
	wa_write_clr(wal, GEN10_DFR_RATIO_EN_AND_CHICKEN, DFR_DISABLE);
}

/*
 * Though there are per-engine instances of these registers,
 * they retain their value through engine resets and should
 * only be provided on the GT workaround list rather than
 * the engine-specific workaround list.
 */
static void
wa_14011060649(struct intel_gt *gt, struct i915_wa_list *wal)
{
	struct intel_engine_cs *engine;
	int id;

	for_each_engine(engine, gt, id) {
		if (engine->class != VIDEO_DECODE_CLASS ||
		    (engine->instance % 2))
			continue;

		wa_write_or(wal, VDBOX_CGCTL3F10(engine->mmio_base),
			    IECPUNIT_CLKGATE_DIS);
	}
}

static void
gen12_gt_workarounds_init(struct intel_gt *gt, struct i915_wa_list *wal)
{
	icl_wa_init_mcr(gt, wal);

	/* Wa_14011060649:tgl,rkl,dg1,adl-s,adl-p */
	wa_14011060649(gt, wal);

	/* Wa_14011059788:tgl,rkl,adl-s,dg1,adl-p */
	wa_write_or(wal, GEN10_DFR_RATIO_EN_AND_CHICKEN, DFR_DISABLE);
}

static void
tgl_gt_workarounds_init(struct intel_gt *gt, struct i915_wa_list *wal)
{
	struct drm_i915_private *i915 = gt->i915;

	gen12_gt_workarounds_init(gt, wal);

	/* Wa_1409420604:tgl */
	if (IS_TGL_UY_GRAPHICS_STEP(i915, STEP_A0, STEP_B0))
		wa_write_or(wal,
			    SUBSLICE_UNIT_LEVEL_CLKGATE2,
			    CPSSUNIT_CLKGATE_DIS);

	/* Wa_1607087056:tgl also know as BUG:1409180338 */
	if (IS_TGL_UY_GRAPHICS_STEP(i915, STEP_A0, STEP_B0))
		wa_write_or(wal,
			    SLICE_UNIT_LEVEL_CLKGATE,
			    L3_CLKGATE_DIS | L3_CR2X_CLKGATE_DIS);

	/* Wa_1408615072:tgl[a0] */
	if (IS_TGL_UY_GRAPHICS_STEP(i915, STEP_A0, STEP_B0))
		wa_write_or(wal, UNSLICE_UNIT_LEVEL_CLKGATE2,
			    VSUNIT_CLKGATE_DIS_TGL);
}

static void
dg1_gt_workarounds_init(struct intel_gt *gt, struct i915_wa_list *wal)
{
	struct drm_i915_private *i915 = gt->i915;

	gen12_gt_workarounds_init(gt, wal);

	/* Wa_1607087056:dg1 */
	if (IS_DG1_GRAPHICS_STEP(i915, STEP_A0, STEP_B0))
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
xehpsdv_gt_workarounds_init(struct intel_gt *gt, struct i915_wa_list *wal)
{
	struct drm_i915_private *i915 = gt->i915;

	xehp_init_mcr(gt, wal);

	/* Wa_1409757795:xehpsdv */
	wa_write_or(wal, SCCGCTL94DC, CG3DDISURB);

	/* Wa_18011725039:xehpsdv */
	if (IS_XEHPSDV_GRAPHICS_STEP(i915, STEP_A1, STEP_B0)) {
		wa_masked_dis(wal, MLTICTXCTL, TDONRENDER);
		wa_write_or(wal, L3SQCREG1_CCS0, FLUSHALLNONCOH);
	}

	/* Wa_16011155590:xehpsdv */
	if (IS_XEHPSDV_GRAPHICS_STEP(i915, STEP_A0, STEP_B0))
		wa_write_or(wal, UNSLICE_UNIT_LEVEL_CLKGATE,
			    TSGUNIT_CLKGATE_DIS);

	/* Wa_14011780169:xehpsdv */
	if (IS_XEHPSDV_GRAPHICS_STEP(i915, STEP_B0, STEP_FOREVER)) {
		wa_write_or(wal, UNSLCGCTL9440, GAMTLBOACS_CLKGATE_DIS |
			    GAMTLBVDBOX7_CLKGATE_DIS |
			    GAMTLBVDBOX6_CLKGATE_DIS |
			    GAMTLBVDBOX5_CLKGATE_DIS |
			    GAMTLBVDBOX4_CLKGATE_DIS |
			    GAMTLBVDBOX3_CLKGATE_DIS |
			    GAMTLBVDBOX2_CLKGATE_DIS |
			    GAMTLBVDBOX1_CLKGATE_DIS |
			    GAMTLBVDBOX0_CLKGATE_DIS |
			    GAMTLBKCR_CLKGATE_DIS |
			    GAMTLBGUC_CLKGATE_DIS |
			    GAMTLBBLT_CLKGATE_DIS);
		wa_write_or(wal, UNSLCGCTL9444, GAMTLBGFXA0_CLKGATE_DIS |
			    GAMTLBGFXA1_CLKGATE_DIS |
			    GAMTLBCOMPA0_CLKGATE_DIS |
			    GAMTLBCOMPA1_CLKGATE_DIS |
			    GAMTLBCOMPB0_CLKGATE_DIS |
			    GAMTLBCOMPB1_CLKGATE_DIS |
			    GAMTLBCOMPC0_CLKGATE_DIS |
			    GAMTLBCOMPC1_CLKGATE_DIS |
			    GAMTLBCOMPD0_CLKGATE_DIS |
			    GAMTLBCOMPD1_CLKGATE_DIS |
			    GAMTLBMERT_CLKGATE_DIS   |
			    GAMTLBVEBOX3_CLKGATE_DIS |
			    GAMTLBVEBOX2_CLKGATE_DIS |
			    GAMTLBVEBOX1_CLKGATE_DIS |
			    GAMTLBVEBOX0_CLKGATE_DIS);
	}

	/* Wa_14012362059:xehpsdv */
	wa_write_or(wal, GEN12_MERT_MOD_CTRL, FORCE_MISS_FTLB);

	/* Wa_16012725990:xehpsdv */
	if (IS_XEHPSDV_GRAPHICS_STEP(i915, STEP_A1, STEP_FOREVER))
		wa_write_or(wal, UNSLICE_UNIT_LEVEL_CLKGATE, VFUNIT_CLKGATE_DIS);

	/* Wa_14011060649:xehpsdv */
	wa_14011060649(gt, wal);

	/* Wa_14014368820:xehpsdv */
	wa_write_or(wal, GEN12_GAMCNTRL_CTRL, INVALIDATION_BROADCAST_MODE_DIS |
		    GLOBAL_INVALIDATION_MODE);
}

static void
dg2_gt_workarounds_init(struct intel_gt *gt, struct i915_wa_list *wal)
{
	struct intel_engine_cs *engine;
	int id;

	xehp_init_mcr(gt, wal);

	/* Wa_14011060649:dg2 */
	wa_14011060649(gt, wal);

	/*
	 * Although there are per-engine instances of these registers,
	 * they technically exist outside the engine itself and are not
	 * impacted by engine resets.  Furthermore, they're part of the
	 * GuC blacklist so trying to treat them as engine workarounds
	 * will result in GuC initialization failure and a wedged GPU.
	 */
	for_each_engine(engine, gt, id) {
		if (engine->class != VIDEO_DECODE_CLASS)
			continue;

		/* Wa_16010515920:dg2_g10 */
		if (IS_DG2_GRAPHICS_STEP(gt->i915, G10, STEP_A0, STEP_B0))
			wa_write_or(wal, VDBOX_CGCTL3F18(engine->mmio_base),
				    ALNUNIT_CLKGATE_DIS);
	}

	if (IS_DG2_G10(gt->i915)) {
		/* Wa_22010523718:dg2 */
		wa_write_or(wal, UNSLICE_UNIT_LEVEL_CLKGATE,
			    CG3DDISCFEG_CLKGATE_DIS);

		/* Wa_14011006942:dg2 */
		wa_write_or(wal, SUBSLICE_UNIT_LEVEL_CLKGATE,
			    DSS_ROUTER_CLKGATE_DIS);
	}

	if (IS_DG2_GRAPHICS_STEP(gt->i915, G10, STEP_A0, STEP_B0)) {
		/* Wa_14010680813:dg2_g10 */
		wa_write_or(wal, GEN12_GAMSTLB_CTRL, CONTROL_BLOCK_CLKGATE_DIS |
			    EGRESS_BLOCK_CLKGATE_DIS | TAG_BLOCK_CLKGATE_DIS);

		/* Wa_14010948348:dg2_g10 */
		wa_write_or(wal, UNSLCGCTL9430, MSQDUNIT_CLKGATE_DIS);

		/* Wa_14011037102:dg2_g10 */
		wa_write_or(wal, UNSLCGCTL9444, LTCDD_CLKGATE_DIS);

		/* Wa_14011371254:dg2_g10 */
		wa_write_or(wal, SLICE_UNIT_LEVEL_CLKGATE, NODEDSS_CLKGATE_DIS);

		/* Wa_14011431319:dg2_g10 */
		wa_write_or(wal, UNSLCGCTL9440, GAMTLBOACS_CLKGATE_DIS |
			    GAMTLBVDBOX7_CLKGATE_DIS |
			    GAMTLBVDBOX6_CLKGATE_DIS |
			    GAMTLBVDBOX5_CLKGATE_DIS |
			    GAMTLBVDBOX4_CLKGATE_DIS |
			    GAMTLBVDBOX3_CLKGATE_DIS |
			    GAMTLBVDBOX2_CLKGATE_DIS |
			    GAMTLBVDBOX1_CLKGATE_DIS |
			    GAMTLBVDBOX0_CLKGATE_DIS |
			    GAMTLBKCR_CLKGATE_DIS |
			    GAMTLBGUC_CLKGATE_DIS |
			    GAMTLBBLT_CLKGATE_DIS);
		wa_write_or(wal, UNSLCGCTL9444, GAMTLBGFXA0_CLKGATE_DIS |
			    GAMTLBGFXA1_CLKGATE_DIS |
			    GAMTLBCOMPA0_CLKGATE_DIS |
			    GAMTLBCOMPA1_CLKGATE_DIS |
			    GAMTLBCOMPB0_CLKGATE_DIS |
			    GAMTLBCOMPB1_CLKGATE_DIS |
			    GAMTLBCOMPC0_CLKGATE_DIS |
			    GAMTLBCOMPC1_CLKGATE_DIS |
			    GAMTLBCOMPD0_CLKGATE_DIS |
			    GAMTLBCOMPD1_CLKGATE_DIS |
			    GAMTLBMERT_CLKGATE_DIS   |
			    GAMTLBVEBOX3_CLKGATE_DIS |
			    GAMTLBVEBOX2_CLKGATE_DIS |
			    GAMTLBVEBOX1_CLKGATE_DIS |
			    GAMTLBVEBOX0_CLKGATE_DIS);

		/* Wa_14010569222:dg2_g10 */
		wa_write_or(wal, UNSLICE_UNIT_LEVEL_CLKGATE,
			    GAMEDIA_CLKGATE_DIS);

		/* Wa_14011028019:dg2_g10 */
		wa_write_or(wal, SSMCGCTL9530, RTFUNIT_CLKGATE_DIS);
	}

	if (IS_DG2_GRAPHICS_STEP(gt->i915, G10, STEP_A0, STEP_B0) ||
	    IS_DG2_GRAPHICS_STEP(gt->i915, G11, STEP_A0, STEP_B0)) {
		/* Wa_14012362059:dg2 */
		wa_write_or(wal, GEN12_MERT_MOD_CTRL, FORCE_MISS_FTLB);
	}

	/* Wa_1509235366:dg2 */
	wa_write_or(wal, GEN12_GAMCNTRL_CTRL, INVALIDATION_BROADCAST_MODE_DIS |
		    GLOBAL_INVALIDATION_MODE);

	/* Wa_14014830051:dg2 */
	wa_write_clr(wal, SARB_CHICKEN1, COMP_CKN_IN);

	/*
	 * The following are not actually "workarounds" but rather
	 * recommended tuning settings documented in the bspec's
	 * performance guide section.
	 */
	wa_write_or(wal, XEHP_L3SCQREG7, BLEND_FILL_CACHING_OPT_DIS);
	wa_write_or(wal, GEN12_SQCM, EN_32B_ACCESS);
}

static void
gt_init_workarounds(struct intel_gt *gt, struct i915_wa_list *wal)
{
	struct drm_i915_private *i915 = gt->i915;

	if (IS_DG2(i915))
		dg2_gt_workarounds_init(gt, wal);
	else if (IS_XEHPSDV(i915))
		xehpsdv_gt_workarounds_init(gt, wal);
	else if (IS_DG1(i915))
		dg1_gt_workarounds_init(gt, wal);
	else if (IS_TIGERLAKE(i915))
		tgl_gt_workarounds_init(gt, wal);
	else if (GRAPHICS_VER(i915) == 12)
		gen12_gt_workarounds_init(gt, wal);
	else if (GRAPHICS_VER(i915) == 11)
		icl_gt_workarounds_init(gt, wal);
	else if (IS_COFFEELAKE(i915) || IS_COMETLAKE(i915))
		cfl_gt_workarounds_init(gt, wal);
	else if (IS_GEMINILAKE(i915))
		glk_gt_workarounds_init(gt, wal);
	else if (IS_KABYLAKE(i915))
		kbl_gt_workarounds_init(gt, wal);
	else if (IS_BROXTON(i915))
		gen9_gt_workarounds_init(gt, wal);
	else if (IS_SKYLAKE(i915))
		skl_gt_workarounds_init(gt, wal);
	else if (IS_HASWELL(i915))
		hsw_gt_workarounds_init(gt, wal);
	else if (IS_VALLEYVIEW(i915))
		vlv_gt_workarounds_init(gt, wal);
	else if (IS_IVYBRIDGE(i915))
		ivb_gt_workarounds_init(gt, wal);
	else if (GRAPHICS_VER(i915) == 6)
		snb_gt_workarounds_init(gt, wal);
	else if (GRAPHICS_VER(i915) == 5)
		ilk_gt_workarounds_init(gt, wal);
	else if (IS_G4X(i915))
		g4x_gt_workarounds_init(gt, wal);
	else if (GRAPHICS_VER(i915) == 4)
		gen4_gt_workarounds_init(gt, wal);
	else if (GRAPHICS_VER(i915) <= 8)
		;
	else
		MISSING_CASE(GRAPHICS_VER(i915));
}

void intel_gt_init_workarounds(struct intel_gt *gt)
{
	struct i915_wa_list *wal = &gt->wa_list;

	wa_init_start(wal, "GT", "global");
	gt_init_workarounds(gt, wal);
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
		DRM_ERROR("%s workaround lost on %s! (reg[%x]=0x%x, relevant bits were 0x%x vs expected 0x%x)\n",
			  name, from, i915_mmio_reg_offset(wa->reg),
			  cur, cur & wa->read, wa->set & wa->read);

		return false;
	}

	return true;
}

static void
wa_list_apply(struct intel_gt *gt, const struct i915_wa_list *wal)
{
	struct intel_uncore *uncore = gt->uncore;
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
		u32 val, old = 0;

		/* open-coded rmw due to steering */
		old = wa->clr ? intel_gt_read_register_fw(gt, wa->reg) : 0;
		val = (old & ~wa->clr) | wa->set;
		if (val != old || !wa->clr)
			intel_uncore_write_fw(uncore, wa->reg, val);

		if (IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM))
			wa_verify(wa, intel_gt_read_register_fw(gt, wa->reg),
				  wal->name, "application");
	}

	intel_uncore_forcewake_put__locked(uncore, fw);
	spin_unlock_irqrestore(&uncore->lock, flags);
}

void intel_gt_apply_workarounds(struct intel_gt *gt)
{
	wa_list_apply(gt, &gt->wa_list);
}

static bool wa_list_verify(struct intel_gt *gt,
			   const struct i915_wa_list *wal,
			   const char *from)
{
	struct intel_uncore *uncore = gt->uncore;
	struct i915_wa *wa;
	enum forcewake_domains fw;
	unsigned long flags;
	unsigned int i;
	bool ok = true;

	fw = wal_get_fw_for_rmw(uncore, wal);

	spin_lock_irqsave(&uncore->lock, flags);
	intel_uncore_forcewake_get__locked(uncore, fw);

	for (i = 0, wa = wal->list; i < wal->count; i++, wa++)
		ok &= wa_verify(wa,
				intel_gt_read_register_fw(gt, wa->reg),
				wal->name, from);

	intel_uncore_forcewake_put__locked(uncore, fw);
	spin_unlock_irqrestore(&uncore->lock, flags);

	return ok;
}

bool intel_gt_verify_workarounds(struct intel_gt *gt, const char *from)
{
	return wa_list_verify(gt, &gt->wa_list, from);
}

__maybe_unused
static bool is_nonpriv_flags_valid(u32 flags)
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

static void allow_read_ctx_timestamp(struct intel_engine_cs *engine)
{
	struct i915_wa_list *w = &engine->whitelist;

	if (engine->class != RENDER_CLASS)
		whitelist_reg_ext(w,
				  RING_CTX_TIMESTAMP(engine->mmio_base),
				  RING_FORCE_TO_NONPRIV_ACCESS_RD);
}

static void cml_whitelist_build(struct intel_engine_cs *engine)
{
	allow_read_ctx_timestamp(engine);

	cfl_whitelist_build(engine);
}

static void icl_whitelist_build(struct intel_engine_cs *engine)
{
	struct i915_wa_list *w = &engine->whitelist;

	allow_read_ctx_timestamp(engine);

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
		break;

	default:
		break;
	}
}

static void tgl_whitelist_build(struct intel_engine_cs *engine)
{
	struct i915_wa_list *w = &engine->whitelist;

	allow_read_ctx_timestamp(engine);

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

		/*
		 * Wa_1808121037:tgl
		 * Wa_14012131227:dg1
		 * Wa_1508744258:tgl,rkl,dg1,adl-s,adl-p
		 */
		whitelist_reg(w, GEN7_COMMON_SLICE_CHICKEN1);

		/* Wa_1806527549:tgl */
		whitelist_reg(w, HIZ_CHICKEN);
		break;
	default:
		break;
	}
}

static void dg1_whitelist_build(struct intel_engine_cs *engine)
{
	struct i915_wa_list *w = &engine->whitelist;

	tgl_whitelist_build(engine);

	/* GEN:BUG:1409280441:dg1 */
	if (IS_DG1_GRAPHICS_STEP(engine->i915, STEP_A0, STEP_B0) &&
	    (engine->class == RENDER_CLASS ||
	     engine->class == COPY_ENGINE_CLASS))
		whitelist_reg_ext(w, RING_ID(engine->mmio_base),
				  RING_FORCE_TO_NONPRIV_ACCESS_RD);
}

static void xehpsdv_whitelist_build(struct intel_engine_cs *engine)
{
	allow_read_ctx_timestamp(engine);
}

static void dg2_whitelist_build(struct intel_engine_cs *engine)
{
	struct i915_wa_list *w = &engine->whitelist;

	allow_read_ctx_timestamp(engine);

	switch (engine->class) {
	case RENDER_CLASS:
		/*
		 * Wa_1507100340:dg2_g10
		 *
		 * This covers 4 registers which are next to one another :
		 *   - PS_INVOCATION_COUNT
		 *   - PS_INVOCATION_COUNT_UDW
		 *   - PS_DEPTH_COUNT
		 *   - PS_DEPTH_COUNT_UDW
		 */
		if (IS_DG2_GRAPHICS_STEP(engine->i915, G10, STEP_A0, STEP_B0))
			whitelist_reg_ext(w, PS_INVOCATION_COUNT,
					  RING_FORCE_TO_NONPRIV_ACCESS_RD |
					  RING_FORCE_TO_NONPRIV_RANGE_4);

		break;
	default:
		break;
	}
}

void intel_engine_init_whitelist(struct intel_engine_cs *engine)
{
	struct drm_i915_private *i915 = engine->i915;
	struct i915_wa_list *w = &engine->whitelist;

	wa_init_start(w, "whitelist", engine->name);

	if (IS_DG2(i915))
		dg2_whitelist_build(engine);
	else if (IS_XEHPSDV(i915))
		xehpsdv_whitelist_build(engine);
	else if (IS_DG1(i915))
		dg1_whitelist_build(engine);
	else if (GRAPHICS_VER(i915) == 12)
		tgl_whitelist_build(engine);
	else if (GRAPHICS_VER(i915) == 11)
		icl_whitelist_build(engine);
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
	else if (GRAPHICS_VER(i915) <= 8)
		;
	else
		MISSING_CASE(GRAPHICS_VER(i915));

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

/*
 * engine_fake_wa_init(), a place holder to program the registers
 * which are not part of an official workaround defined by the
 * hardware team.
 * Adding programming of those register inside workaround will
 * allow utilizing wa framework to proper application and verification.
 */
static void
engine_fake_wa_init(struct intel_engine_cs *engine, struct i915_wa_list *wal)
{
	u8 mocs;

	/*
	 * RING_CMD_CCTL are need to be programed to un-cached
	 * for memory writes and reads outputted by Command
	 * Streamers on Gen12 onward platforms.
	 */
	if (GRAPHICS_VER(engine->i915) >= 12) {
		mocs = engine->gt->mocs.uc_index;
		wa_masked_field_set(wal,
				    RING_CMD_CCTL(engine->mmio_base),
				    CMD_CCTL_MOCS_MASK,
				    CMD_CCTL_MOCS_OVERRIDE(mocs, mocs));
	}
}

static bool needs_wa_1308578152(struct intel_engine_cs *engine)
{
	u64 dss_mask = intel_sseu_get_subslices(&engine->gt->info.sseu, 0);

	return (dss_mask & GENMASK(GEN_DSS_PER_GSLICE - 1, 0)) == 0;
}

static void
rcs_engine_wa_init(struct intel_engine_cs *engine, struct i915_wa_list *wal)
{
	struct drm_i915_private *i915 = engine->i915;

	if (IS_DG2_GRAPHICS_STEP(engine->i915, G11, STEP_A0, STEP_B0)) {
		/* Wa_14013392000:dg2_g11 */
		wa_masked_en(wal, GEN7_ROW_CHICKEN2, GEN12_ENABLE_LARGE_GRF_MODE);

		/* Wa_16011620976:dg2_g11 */
		wa_write_or(wal, LSC_CHICKEN_BIT_0_UDW, DIS_CHAIN_2XSIMD8);
	}

	if (IS_DG2_GRAPHICS_STEP(engine->i915, G10, STEP_A0, STEP_B0) ||
	    IS_DG2_GRAPHICS_STEP(engine->i915, G11, STEP_A0, STEP_B0)) {
		/* Wa_14012419201:dg2 */
		wa_masked_en(wal, GEN9_ROW_CHICKEN4,
			     GEN12_DISABLE_HDR_PAST_PAYLOAD_HOLD_FIX);
	}

	if (IS_DG2_GRAPHICS_STEP(engine->i915, G10, STEP_B0, STEP_C0) ||
	    IS_DG2_G11(engine->i915)) {
		/*
		 * Wa_22012826095:dg2
		 * Wa_22013059131:dg2
		 */
		wa_write_clr_set(wal, LSC_CHICKEN_BIT_0_UDW,
				 MAXREQS_PER_BANK,
				 REG_FIELD_PREP(MAXREQS_PER_BANK, 2));

		/* Wa_22013059131:dg2 */
		wa_write_or(wal, LSC_CHICKEN_BIT_0,
			    FORCE_1_SUB_MESSAGE_PER_FRAGMENT);
	}

	/* Wa_1308578152:dg2_g10 when first gslice is fused off */
	if (IS_DG2_GRAPHICS_STEP(engine->i915, G10, STEP_B0, STEP_C0) &&
	    needs_wa_1308578152(engine)) {
		wa_masked_dis(wal, GEN12_CS_DEBUG_MODE1_CCCSUNIT_BE_COMMON,
			      GEN12_REPLAY_MODE_GRANULARITY);
	}

	if (IS_DG2_GRAPHICS_STEP(engine->i915, G10, STEP_B0, STEP_FOREVER) ||
	    IS_DG2_G11(engine->i915)) {
		/* Wa_22013037850:dg2 */
		wa_write_or(wal, LSC_CHICKEN_BIT_0_UDW,
			    DISABLE_128B_EVICTION_COMMAND_UDW);

		/* Wa_22012856258:dg2 */
		wa_masked_en(wal, GEN7_ROW_CHICKEN2,
			     GEN12_DISABLE_READ_SUPPRESSION);

		/*
		 * Wa_22010960976:dg2
		 * Wa_14013347512:dg2
		 */
		wa_masked_dis(wal, GEN12_HDC_CHICKEN0,
			      LSC_L1_FLUSH_CTL_3D_DATAPORT_FLUSH_EVENTS_MASK);
	}

	if (IS_DG2_GRAPHICS_STEP(engine->i915, G10, STEP_A0, STEP_B0)) {
		/*
		 * Wa_1608949956:dg2_g10
		 * Wa_14010198302:dg2_g10
		 */
		wa_masked_en(wal, GEN8_ROW_CHICKEN,
			     MDQ_ARBITRATION_MODE | UGM_BACKUP_MODE);

		/*
		 * Wa_14010918519:dg2_g10
		 *
		 * LSC_CHICKEN_BIT_0 always reads back as 0 is this stepping,
		 * so ignoring verification.
		 */
		wa_add(wal, LSC_CHICKEN_BIT_0_UDW, 0,
		       FORCE_SLM_FENCE_SCOPE_TO_TILE | FORCE_UGM_FENCE_SCOPE_TO_TILE,
		       0, false);
	}

	if (IS_DG2_GRAPHICS_STEP(engine->i915, G10, STEP_A0, STEP_B0)) {
		/* Wa_22010430635:dg2 */
		wa_masked_en(wal,
			     GEN9_ROW_CHICKEN4,
			     GEN12_DISABLE_GRF_CLEAR);

		/* Wa_14010648519:dg2 */
		wa_write_or(wal, XEHP_L3NODEARBCFG, XEHP_LNESPARE);
	}

	if (IS_DG2_GRAPHICS_STEP(engine->i915, G10, STEP_A0, STEP_C0) ||
	    IS_DG2_G11(engine->i915)) {
		/* Wa_22012654132:dg2 */
		wa_add(wal, GEN10_CACHE_MODE_SS, 0,
		       _MASKED_BIT_ENABLE(ENABLE_PREFETCH_INTO_IC),
		       0 /* write-only, so skip validation */,
		       true);
	}

	/* Wa_14013202645:dg2 */
	if (IS_DG2_GRAPHICS_STEP(engine->i915, G10, STEP_B0, STEP_C0) ||
	    IS_DG2_GRAPHICS_STEP(engine->i915, G11, STEP_A0, STEP_B0))
		wa_write_or(wal, RT_CTRL, DIS_NULL_QUERY);

	if (IS_DG1_GRAPHICS_STEP(i915, STEP_A0, STEP_B0) ||
	    IS_TGL_UY_GRAPHICS_STEP(i915, STEP_A0, STEP_B0)) {
		/*
		 * Wa_1607138336:tgl[a0],dg1[a0]
		 * Wa_1607063988:tgl[a0],dg1[a0]
		 */
		wa_write_or(wal,
			    GEN9_CTX_PREEMPT_REG,
			    GEN12_DISABLE_POSH_BUSY_FF_DOP_CG);
	}

	if (IS_TGL_UY_GRAPHICS_STEP(i915, STEP_A0, STEP_B0)) {
		/*
		 * Wa_1606679103:tgl
		 * (see also Wa_1606682166:icl)
		 */
		wa_write_or(wal,
			    GEN7_SARCHKMD,
			    GEN7_DISABLE_SAMPLER_PREFETCH);
	}

	if (IS_ALDERLAKE_P(i915) || IS_ALDERLAKE_S(i915) || IS_DG1(i915) ||
	    IS_ROCKETLAKE(i915) || IS_TIGERLAKE(i915)) {
		/* Wa_1606931601:tgl,rkl,dg1,adl-s,adl-p */
		wa_masked_en(wal, GEN7_ROW_CHICKEN2, GEN12_DISABLE_EARLY_READ);

		/*
		 * Wa_1407928979:tgl A*
		 * Wa_18011464164:tgl[B0+],dg1[B0+]
		 * Wa_22010931296:tgl[B0+],dg1[B0+]
		 * Wa_14010919138:rkl,dg1,adl-s,adl-p
		 */
		wa_write_or(wal, GEN7_FF_THREAD_MODE,
			    GEN12_FF_TESSELATION_DOP_GATE_DISABLE);

		/*
		 * Wa_1606700617:tgl,dg1,adl-p
		 * Wa_22010271021:tgl,rkl,dg1,adl-s,adl-p
		 * Wa_14010826681:tgl,dg1,rkl,adl-p
		 */
		wa_masked_en(wal,
			     GEN9_CS_DEBUG_MODE1,
			     FF_DOP_CLOCK_GATE_DISABLE);
	}

	if (IS_ALDERLAKE_P(i915) || IS_ALDERLAKE_S(i915) ||
	    IS_DG1_GRAPHICS_STEP(i915, STEP_A0, STEP_B0) ||
	    IS_ROCKETLAKE(i915) || IS_TIGERLAKE(i915)) {
		/* Wa_1409804808:tgl,rkl,dg1[a0],adl-s,adl-p */
		wa_masked_en(wal, GEN7_ROW_CHICKEN2,
			     GEN12_PUSH_CONST_DEREF_HOLD_DIS);

		/*
		 * Wa_1409085225:tgl
		 * Wa_14010229206:tgl,rkl,dg1[a0],adl-s,adl-p
		 */
		wa_masked_en(wal, GEN9_ROW_CHICKEN4, GEN12_DISABLE_TDL_PUSH);
	}

	if (IS_DG1_GRAPHICS_STEP(i915, STEP_A0, STEP_B0) ||
	    IS_ROCKETLAKE(i915) || IS_TIGERLAKE(i915)) {
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
			     RING_PSMI_CTL(RENDER_RING_BASE),
			     GEN12_WAIT_FOR_EVENT_POWER_DOWN_DISABLE |
			     GEN8_RC_SEMA_IDLE_MSG_DISABLE);
	}

	if (IS_DG1(i915) || IS_ROCKETLAKE(i915) || IS_TIGERLAKE(i915) ||
	    IS_ALDERLAKE_S(i915) || IS_ALDERLAKE_P(i915)) {
		/* Wa_1406941453:tgl,rkl,dg1,adl-s,adl-p */
		wa_masked_en(wal,
			     GEN10_SAMPLER_MODE,
			     ENABLE_SMALLPL);
	}

	if (GRAPHICS_VER(i915) == 11) {
		/* This is not an Wa. Enable for better image quality */
		wa_masked_en(wal,
			     _3D_CHICKEN3,
			     _3D_CHICKEN3_AA_LINE_QUALITY_FIX_ENABLE);

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

		/*
		 * Wa_1408767742:icl[a2..forever],ehl[all]
		 * Wa_1605460711:icl[a0..c0]
		 */
		wa_write_or(wal,
			    GEN7_FF_THREAD_MODE,
			    GEN12_FF_TESSELATION_DOP_GATE_DISABLE);

		/* Wa_22010271021 */
		wa_masked_en(wal,
			     GEN9_CS_DEBUG_MODE1,
			     FF_DOP_CLOCK_GATE_DISABLE);
	}

	if (IS_GRAPHICS_VER(i915, 9, 12)) {
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

	if (GRAPHICS_VER(i915) == 9) {
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

		/* Disable atomics in L3 to prevent unrecoverable hangs */
		wa_write_clr_set(wal, GEN9_SCRATCH_LNCF1,
				 GEN9_LNCF_NONIA_COHERENT_ATOMICS_ENABLE, 0);
		wa_write_clr_set(wal, GEN8_L3SQCREG4,
				 GEN8_LQSQ_NONIA_COHERENT_ATOMICS_ENABLE, 0);
		wa_write_clr_set(wal, GEN9_SCRATCH1,
				 EVICTION_PERF_FIX_ENABLE, 0);
	}

	if (IS_HASWELL(i915)) {
		/* WaSampleCChickenBitEnable:hsw */
		wa_masked_en(wal,
			     HALF_SLICE_CHICKEN3, HSW_SAMPLE_C_PERFORMANCE);

		wa_masked_dis(wal,
			      CACHE_MODE_0_GEN7,
			      /* enable HiZ Raw Stall Optimization */
			      HIZ_RAW_STALL_OPT_DISABLE);
	}

	if (IS_VALLEYVIEW(i915)) {
		/* WaDisableEarlyCull:vlv */
		wa_masked_en(wal,
			     _3D_CHICKEN3,
			     _3D_CHICKEN_SF_DISABLE_OBJEND_CULL);

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

		/* WaPsdDispatchEnable:vlv */
		/* WaDisablePSDDualDispatchEnable:vlv */
		wa_masked_en(wal,
			     GEN7_HALF_SLICE_CHICKEN1,
			     GEN7_MAX_PS_THREAD_DEP |
			     GEN7_PSD_SINGLE_PORT_DISPATCH_ENABLE);
	}

	if (IS_IVYBRIDGE(i915)) {
		/* WaDisableEarlyCull:ivb */
		wa_masked_en(wal,
			     _3D_CHICKEN3,
			     _3D_CHICKEN_SF_DISABLE_OBJEND_CULL);

		if (0) { /* causes HiZ corruption on ivb:gt1 */
			/* enable HiZ Raw Stall Optimization */
			wa_masked_dis(wal,
				      CACHE_MODE_0_GEN7,
				      HIZ_RAW_STALL_OPT_DISABLE);
		}

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

		/* WaDisablePSDDualDispatchEnable:ivb */
		if (IS_IVB_GT1(i915))
			wa_masked_en(wal,
				     GEN7_HALF_SLICE_CHICKEN1,
				     GEN7_PSD_SINGLE_PORT_DISPATCH_ENABLE);
	}

	if (GRAPHICS_VER(i915) == 7) {
		/* WaBCSVCSTlbInvalidationMode:ivb,vlv,hsw */
		wa_masked_en(wal,
			     RING_MODE_GEN7(RENDER_RING_BASE),
			     GFX_TLB_INVALIDATE_EXPLICIT | GFX_REPLAY_MODE);

		/* WaDisable_RenderCache_OperationalFlush:ivb,vlv,hsw */
		wa_masked_dis(wal, CACHE_MODE_0_GEN7, RC_OP_FLUSH_ENABLE);

		/*
		 * BSpec says this must be set, even though
		 * WaDisable4x2SubspanOptimization:ivb,hsw
		 * WaDisable4x2SubspanOptimization isn't listed for VLV.
		 */
		wa_masked_en(wal,
			     CACHE_MODE_1,
			     PIXEL_SUBSPAN_COLLECT_OPT_DISABLE);

		/*
		 * BSpec recommends 8x4 when MSAA is used,
		 * however in practice 16x4 seems fastest.
		 *
		 * Note that PS/WM thread counts depend on the WIZ hashing
		 * disable bit, which we don't touch here, but it's good
		 * to keep in mind (see 3DSTATE_PS and 3DSTATE_WM).
		 */
		wa_masked_field_set(wal,
				    GEN7_GT_MODE,
				    GEN6_WIZ_HASHING_MASK,
				    GEN6_WIZ_HASHING_16x4);
	}

	if (IS_GRAPHICS_VER(i915, 6, 7))
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

	if (GRAPHICS_VER(i915) == 6) {
		/*
		 * Required for the hardware to program scanline values for
		 * waiting
		 * WaEnableFlushTlbInvalidationMode:snb
		 */
		wa_masked_en(wal,
			     GFX_MODE,
			     GFX_TLB_INVALIDATE_EXPLICIT);

		/* WaDisableHiZPlanesWhenMSAAEnabled:snb */
		wa_masked_en(wal,
			     _3D_CHICKEN,
			     _3D_CHICKEN_HIZ_PLANE_DISABLE_MSAA_4X_SNB);

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

		/*
		 * BSpec recommends 8x4 when MSAA is used,
		 * however in practice 16x4 seems fastest.
		 *
		 * Note that PS/WM thread counts depend on the WIZ hashing
		 * disable bit, which we don't touch here, but it's good
		 * to keep in mind (see 3DSTATE_PS and 3DSTATE_WM).
		 */
		wa_masked_field_set(wal,
				    GEN6_GT_MODE,
				    GEN6_WIZ_HASHING_MASK,
				    GEN6_WIZ_HASHING_16x4);

		/* WaDisable_RenderCache_OperationalFlush:snb */
		wa_masked_dis(wal, CACHE_MODE_0, RC_OP_FLUSH_ENABLE);

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

	if (IS_GRAPHICS_VER(i915, 4, 6))
		/* WaTimedSingleVertexDispatch:cl,bw,ctg,elk,ilk,snb */
		wa_add(wal, MI_MODE,
		       0, _MASKED_BIT_ENABLE(VS_TIMER_DISPATCH),
		       /* XXX bit doesn't stick on Broadwater */
		       IS_I965G(i915) ? 0 : VS_TIMER_DISPATCH, true);

	if (GRAPHICS_VER(i915) == 4)
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
		wa_add(wal, ECOSKPD(RENDER_RING_BASE),
		       0, _MASKED_BIT_ENABLE(ECO_CONSTANT_BUFFER_SR_DISABLE),
		       0 /* XXX bit doesn't stick on Broadwater */,
		       true);
}

static void
xcs_engine_wa_init(struct intel_engine_cs *engine, struct i915_wa_list *wal)
{
	struct drm_i915_private *i915 = engine->i915;

	/* WaKBLVECSSemaphoreWaitPoll:kbl */
	if (IS_KBL_GRAPHICS_STEP(i915, STEP_A0, STEP_F0)) {
		wa_write(wal,
			 RING_SEMA_WAIT_POLL(engine->mmio_base),
			 1);
	}
}

static void
engine_init_workarounds(struct intel_engine_cs *engine, struct i915_wa_list *wal)
{
	if (I915_SELFTEST_ONLY(GRAPHICS_VER(engine->i915) < 4))
		return;

	engine_fake_wa_init(engine, wal);

	if (engine->class == RENDER_CLASS)
		rcs_engine_wa_init(engine, wal);
	else
		xcs_engine_wa_init(engine, wal);
}

void intel_engine_init_workarounds(struct intel_engine_cs *engine)
{
	struct i915_wa_list *wal = &engine->wa_list;

	if (GRAPHICS_VER(engine->i915) < 4)
		return;

	wa_init_start(wal, "engine", engine->name);
	engine_init_workarounds(engine, wal);
	wa_init_finish(wal);
}

void intel_engine_apply_workarounds(struct intel_engine_cs *engine)
{
	wa_list_apply(engine->gt, &engine->wa_list);
}

static const struct i915_range mcr_ranges_gen8[] = {
	{ .start = 0x5500, .end = 0x55ff },
	{ .start = 0x7000, .end = 0x7fff },
	{ .start = 0x9400, .end = 0x97ff },
	{ .start = 0xb000, .end = 0xb3ff },
	{ .start = 0xe000, .end = 0xe7ff },
	{},
};

static const struct i915_range mcr_ranges_gen12[] = {
	{ .start =  0x8150, .end =  0x815f },
	{ .start =  0x9520, .end =  0x955f },
	{ .start =  0xb100, .end =  0xb3ff },
	{ .start =  0xde80, .end =  0xe8ff },
	{ .start = 0x24a00, .end = 0x24a7f },
	{},
};

static const struct i915_range mcr_ranges_xehp[] = {
	{ .start =  0x4000, .end =  0x4aff },
	{ .start =  0x5200, .end =  0x52ff },
	{ .start =  0x5400, .end =  0x7fff },
	{ .start =  0x8140, .end =  0x815f },
	{ .start =  0x8c80, .end =  0x8dff },
	{ .start =  0x94d0, .end =  0x955f },
	{ .start =  0x9680, .end =  0x96ff },
	{ .start =  0xb000, .end =  0xb3ff },
	{ .start =  0xc800, .end =  0xcfff },
	{ .start =  0xd800, .end =  0xd8ff },
	{ .start =  0xdc00, .end =  0xffff },
	{ .start = 0x17000, .end = 0x17fff },
	{ .start = 0x24a00, .end = 0x24a7f },
	{},
};

static bool mcr_range(struct drm_i915_private *i915, u32 offset)
{
	const struct i915_range *mcr_ranges;
	int i;

	if (GRAPHICS_VER_FULL(i915) >= IP_VER(12, 50))
		mcr_ranges = mcr_ranges_xehp;
	else if (GRAPHICS_VER(i915) >= 12)
		mcr_ranges = mcr_ranges_gen12;
	else if (GRAPHICS_VER(i915) >= 8)
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
	if (GRAPHICS_VER(i915) >= 8)
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

	vma = __vm_create_scratch_for_read(&ce->engine->gt->ggtt->vm,
					   wal->count * sizeof(u32));
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

	err = i915_vma_pin_ww(vma, &ww, 0, 0,
			   i915_vma_is_ggtt(vma) ? PIN_GLOBAL : PIN_USER);
	if (err)
		goto err_unpin;

	rq = i915_request_create(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_vma;
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
err_vma:
	i915_vma_unpin(vma);
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

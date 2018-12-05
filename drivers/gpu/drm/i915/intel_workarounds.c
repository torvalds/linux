/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2018 Intel Corporation
 */

#include "i915_drv.h"
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
 * ''''''
 *
 * Keep things in this file ordered by WA type, as per the above (context, GT,
 * display, register whitelist, batchbuffer). Then, inside each type, keep the
 * following order:
 *
 * - Infrastructure functions and macros
 * - WAs per platform in standard gen/chrono order
 * - Public functions to init or apply the given workaround type.
 */

static void wa_init_start(struct i915_wa_list *wal, const char *name)
{
	wal->name = name;
}

static void wa_init_finish(struct i915_wa_list *wal)
{
	if (!wal->count)
		return;

	DRM_DEBUG_DRIVER("Initialized %u %s workarounds\n",
			 wal->count, wal->name);
}

static void wa_add(struct drm_i915_private *i915,
		   i915_reg_t reg, const u32 mask, const u32 val)
{
	struct i915_workarounds *wa = &i915->workarounds;
	unsigned int start = 0, end = wa->count;
	unsigned int addr = i915_mmio_reg_offset(reg);
	struct i915_wa_reg *r;

	while (start < end) {
		unsigned int mid = start + (end - start) / 2;

		if (wa->reg[mid].addr < addr) {
			start = mid + 1;
		} else if (wa->reg[mid].addr > addr) {
			end = mid;
		} else {
			r = &wa->reg[mid];

			if ((mask & ~r->mask) == 0) {
				DRM_ERROR("Discarding overwritten w/a for reg %04x (mask: %08x, value: %08x)\n",
					  addr, r->mask, r->value);

				r->value &= ~mask;
			}

			r->value |= val;
			r->mask  |= mask;
			return;
		}
	}

	if (WARN_ON_ONCE(wa->count >= I915_MAX_WA_REGS)) {
		DRM_ERROR("Dropping w/a for reg %04x (mask: %08x, value: %08x)\n",
			  addr, mask, val);
		return;
	}

	r = &wa->reg[wa->count++];
	r->addr  = addr;
	r->value = val;
	r->mask  = mask;

	while (r-- > wa->reg) {
		GEM_BUG_ON(r[0].addr == r[1].addr);
		if (r[1].addr > r[0].addr)
			break;

		swap(r[1], r[0]);
	}
}

#define WA_REG(addr, mask, val) wa_add(dev_priv, (addr), (mask), (val))

#define WA_SET_BIT_MASKED(addr, mask) \
	WA_REG(addr, (mask), _MASKED_BIT_ENABLE(mask))

#define WA_CLR_BIT_MASKED(addr, mask) \
	WA_REG(addr, (mask), _MASKED_BIT_DISABLE(mask))

#define WA_SET_FIELD_MASKED(addr, mask, value) \
	WA_REG(addr, (mask), _MASKED_FIELD(mask, value))

static int gen8_ctx_workarounds_init(struct drm_i915_private *dev_priv)
{
	WA_SET_BIT_MASKED(INSTPM, INSTPM_FORCE_ORDERING);

	/* WaDisableAsyncFlipPerfMode:bdw,chv */
	WA_SET_BIT_MASKED(MI_MODE, ASYNC_FLIP_PERF_DISABLE);

	/* WaDisablePartialInstShootdown:bdw,chv */
	WA_SET_BIT_MASKED(GEN8_ROW_CHICKEN,
			  PARTIAL_INSTRUCTION_SHOOTDOWN_DISABLE);

	/* Use Force Non-Coherent whenever executing a 3D context. This is a
	 * workaround for for a possible hang in the unlikely event a TLB
	 * invalidation occurs during a PSD flush.
	 */
	/* WaForceEnableNonCoherent:bdw,chv */
	/* WaHdcDisableFetchWhenMasked:bdw,chv */
	WA_SET_BIT_MASKED(HDC_CHICKEN0,
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
	WA_CLR_BIT_MASKED(CACHE_MODE_0_GEN7, HIZ_RAW_STALL_OPT_DISABLE);

	/* Wa4x4STCOptimizationDisable:bdw,chv */
	WA_SET_BIT_MASKED(CACHE_MODE_1, GEN8_4x4_STC_OPTIMIZATION_DISABLE);

	/*
	 * BSpec recommends 8x4 when MSAA is used,
	 * however in practice 16x4 seems fastest.
	 *
	 * Note that PS/WM thread counts depend on the WIZ hashing
	 * disable bit, which we don't touch here, but it's good
	 * to keep in mind (see 3DSTATE_PS and 3DSTATE_WM).
	 */
	WA_SET_FIELD_MASKED(GEN7_GT_MODE,
			    GEN6_WIZ_HASHING_MASK,
			    GEN6_WIZ_HASHING_16x4);

	return 0;
}

static int bdw_ctx_workarounds_init(struct drm_i915_private *dev_priv)
{
	int ret;

	ret = gen8_ctx_workarounds_init(dev_priv);
	if (ret)
		return ret;

	/* WaDisableThreadStallDopClockGating:bdw (pre-production) */
	WA_SET_BIT_MASKED(GEN8_ROW_CHICKEN, STALL_DOP_GATING_DISABLE);

	/* WaDisableDopClockGating:bdw
	 *
	 * Also see the related UCGTCL1 write in broadwell_init_clock_gating()
	 * to disable EUTC clock gating.
	 */
	WA_SET_BIT_MASKED(GEN7_ROW_CHICKEN2,
			  DOP_CLOCK_GATING_DISABLE);

	WA_SET_BIT_MASKED(HALF_SLICE_CHICKEN3,
			  GEN8_SAMPLER_POWER_BYPASS_DIS);

	WA_SET_BIT_MASKED(HDC_CHICKEN0,
			  /* WaForceContextSaveRestoreNonCoherent:bdw */
			  HDC_FORCE_CONTEXT_SAVE_RESTORE_NON_COHERENT |
			  /* WaDisableFenceDestinationToSLM:bdw (pre-prod) */
			  (IS_BDW_GT3(dev_priv) ? HDC_FENCE_DEST_SLM_DISABLE : 0));

	return 0;
}

static int chv_ctx_workarounds_init(struct drm_i915_private *dev_priv)
{
	int ret;

	ret = gen8_ctx_workarounds_init(dev_priv);
	if (ret)
		return ret;

	/* WaDisableThreadStallDopClockGating:chv */
	WA_SET_BIT_MASKED(GEN8_ROW_CHICKEN, STALL_DOP_GATING_DISABLE);

	/* Improve HiZ throughput on CHV. */
	WA_SET_BIT_MASKED(HIZ_CHICKEN, CHV_HZ_8X8_MODE_IN_1X);

	return 0;
}

static int gen9_ctx_workarounds_init(struct drm_i915_private *dev_priv)
{
	if (HAS_LLC(dev_priv)) {
		/* WaCompressedResourceSamplerPbeMediaNewHashMode:skl,kbl
		 *
		 * Must match Display Engine. See
		 * WaCompressedResourceDisplayNewHashMode.
		 */
		WA_SET_BIT_MASKED(COMMON_SLICE_CHICKEN2,
				  GEN9_PBE_COMPRESSED_HASH_SELECTION);
		WA_SET_BIT_MASKED(GEN9_HALF_SLICE_CHICKEN7,
				  GEN9_SAMPLER_HASH_COMPRESSED_READ_ADDR);
	}

	/* WaClearFlowControlGpgpuContextSave:skl,bxt,kbl,glk,cfl */
	/* WaDisablePartialInstShootdown:skl,bxt,kbl,glk,cfl */
	WA_SET_BIT_MASKED(GEN8_ROW_CHICKEN,
			  FLOW_CONTROL_ENABLE |
			  PARTIAL_INSTRUCTION_SHOOTDOWN_DISABLE);

	/* Syncing dependencies between camera and graphics:skl,bxt,kbl */
	if (!IS_COFFEELAKE(dev_priv))
		WA_SET_BIT_MASKED(HALF_SLICE_CHICKEN3,
				  GEN9_DISABLE_OCL_OOB_SUPPRESS_LOGIC);

	/* WaEnableYV12BugFixInHalfSliceChicken7:skl,bxt,kbl,glk,cfl */
	/* WaEnableSamplerGPGPUPreemptionSupport:skl,bxt,kbl,cfl */
	WA_SET_BIT_MASKED(GEN9_HALF_SLICE_CHICKEN7,
			  GEN9_ENABLE_YV12_BUGFIX |
			  GEN9_ENABLE_GPGPU_PREEMPTION);

	/* Wa4x4STCOptimizationDisable:skl,bxt,kbl,glk,cfl */
	/* WaDisablePartialResolveInVc:skl,bxt,kbl,cfl */
	WA_SET_BIT_MASKED(CACHE_MODE_1,
			  GEN8_4x4_STC_OPTIMIZATION_DISABLE |
			  GEN9_PARTIAL_RESOLVE_IN_VC_DISABLE);

	/* WaCcsTlbPrefetchDisable:skl,bxt,kbl,glk,cfl */
	WA_CLR_BIT_MASKED(GEN9_HALF_SLICE_CHICKEN5,
			  GEN9_CCS_TLB_PREFETCH_ENABLE);

	/* WaForceContextSaveRestoreNonCoherent:skl,bxt,kbl,cfl */
	WA_SET_BIT_MASKED(HDC_CHICKEN0,
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
	WA_SET_BIT_MASKED(HDC_CHICKEN0,
			  HDC_FORCE_NON_COHERENT);

	/* WaDisableSamplerPowerBypassForSOPingPong:skl,bxt,kbl,cfl */
	if (IS_SKYLAKE(dev_priv) ||
	    IS_KABYLAKE(dev_priv) ||
	    IS_COFFEELAKE(dev_priv))
		WA_SET_BIT_MASKED(HALF_SLICE_CHICKEN3,
				  GEN8_SAMPLER_POWER_BYPASS_DIS);

	/* WaDisableSTUnitPowerOptimization:skl,bxt,kbl,glk,cfl */
	WA_SET_BIT_MASKED(HALF_SLICE_CHICKEN2, GEN8_ST_PO_DISABLE);

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
	WA_CLR_BIT_MASKED(GEN8_CS_CHICKEN1, GEN9_PREEMPT_3D_OBJECT_LEVEL);

	/* WaDisableGPGPUMidCmdPreemption:skl,bxt,blk,cfl,[cnl] */
	WA_SET_FIELD_MASKED(GEN8_CS_CHICKEN1,
			    GEN9_PREEMPT_GPGPU_LEVEL_MASK,
			    GEN9_PREEMPT_GPGPU_COMMAND_LEVEL);

	/* WaClearHIZ_WM_CHICKEN3:bxt,glk */
	if (IS_GEN9_LP(dev_priv))
		WA_SET_BIT_MASKED(GEN9_WM_CHICKEN3, GEN9_FACTOR_IN_CLR_VAL_HIZ);

	return 0;
}

static int skl_tune_iz_hashing(struct drm_i915_private *dev_priv)
{
	u8 vals[3] = { 0, 0, 0 };
	unsigned int i;

	for (i = 0; i < 3; i++) {
		u8 ss;

		/*
		 * Only consider slices where one, and only one, subslice has 7
		 * EUs
		 */
		if (!is_power_of_2(INTEL_INFO(dev_priv)->sseu.subslice_7eu[i]))
			continue;

		/*
		 * subslice_7eu[i] != 0 (because of the check above) and
		 * ss_max == 4 (maximum number of subslices possible per slice)
		 *
		 * ->    0 <= ss <= 3;
		 */
		ss = ffs(INTEL_INFO(dev_priv)->sseu.subslice_7eu[i]) - 1;
		vals[i] = 3 - ss;
	}

	if (vals[0] == 0 && vals[1] == 0 && vals[2] == 0)
		return 0;

	/* Tune IZ hashing. See intel_device_info_runtime_init() */
	WA_SET_FIELD_MASKED(GEN7_GT_MODE,
			    GEN9_IZ_HASHING_MASK(2) |
			    GEN9_IZ_HASHING_MASK(1) |
			    GEN9_IZ_HASHING_MASK(0),
			    GEN9_IZ_HASHING(2, vals[2]) |
			    GEN9_IZ_HASHING(1, vals[1]) |
			    GEN9_IZ_HASHING(0, vals[0]));

	return 0;
}

static int skl_ctx_workarounds_init(struct drm_i915_private *dev_priv)
{
	int ret;

	ret = gen9_ctx_workarounds_init(dev_priv);
	if (ret)
		return ret;

	return skl_tune_iz_hashing(dev_priv);
}

static int bxt_ctx_workarounds_init(struct drm_i915_private *dev_priv)
{
	int ret;

	ret = gen9_ctx_workarounds_init(dev_priv);
	if (ret)
		return ret;

	/* WaDisableThreadStallDopClockGating:bxt */
	WA_SET_BIT_MASKED(GEN8_ROW_CHICKEN,
			  STALL_DOP_GATING_DISABLE);

	/* WaToEnableHwFixForPushConstHWBug:bxt */
	WA_SET_BIT_MASKED(COMMON_SLICE_CHICKEN2,
			  GEN8_SBE_DISABLE_REPLAY_BUF_OPTIMIZATION);

	return 0;
}

static int kbl_ctx_workarounds_init(struct drm_i915_private *dev_priv)
{
	int ret;

	ret = gen9_ctx_workarounds_init(dev_priv);
	if (ret)
		return ret;

	/* WaDisableFenceDestinationToSLM:kbl (pre-prod) */
	if (IS_KBL_REVID(dev_priv, KBL_REVID_A0, KBL_REVID_A0))
		WA_SET_BIT_MASKED(HDC_CHICKEN0,
				  HDC_FENCE_DEST_SLM_DISABLE);

	/* WaToEnableHwFixForPushConstHWBug:kbl */
	if (IS_KBL_REVID(dev_priv, KBL_REVID_C0, REVID_FOREVER))
		WA_SET_BIT_MASKED(COMMON_SLICE_CHICKEN2,
				  GEN8_SBE_DISABLE_REPLAY_BUF_OPTIMIZATION);

	/* WaDisableSbeCacheDispatchPortSharing:kbl */
	WA_SET_BIT_MASKED(GEN7_HALF_SLICE_CHICKEN1,
			  GEN7_SBE_SS_CACHE_DISPATCH_PORT_SHARING_DISABLE);

	return 0;
}

static int glk_ctx_workarounds_init(struct drm_i915_private *dev_priv)
{
	int ret;

	ret = gen9_ctx_workarounds_init(dev_priv);
	if (ret)
		return ret;

	/* WaToEnableHwFixForPushConstHWBug:glk */
	WA_SET_BIT_MASKED(COMMON_SLICE_CHICKEN2,
			  GEN8_SBE_DISABLE_REPLAY_BUF_OPTIMIZATION);

	return 0;
}

static int cfl_ctx_workarounds_init(struct drm_i915_private *dev_priv)
{
	int ret;

	ret = gen9_ctx_workarounds_init(dev_priv);
	if (ret)
		return ret;

	/* WaToEnableHwFixForPushConstHWBug:cfl */
	WA_SET_BIT_MASKED(COMMON_SLICE_CHICKEN2,
			  GEN8_SBE_DISABLE_REPLAY_BUF_OPTIMIZATION);

	/* WaDisableSbeCacheDispatchPortSharing:cfl */
	WA_SET_BIT_MASKED(GEN7_HALF_SLICE_CHICKEN1,
			  GEN7_SBE_SS_CACHE_DISPATCH_PORT_SHARING_DISABLE);

	return 0;
}

static int cnl_ctx_workarounds_init(struct drm_i915_private *dev_priv)
{
	/* WaForceContextSaveRestoreNonCoherent:cnl */
	WA_SET_BIT_MASKED(CNL_HDC_CHICKEN0,
			  HDC_FORCE_CONTEXT_SAVE_RESTORE_NON_COHERENT);

	/* WaThrottleEUPerfToAvoidTDBackPressure:cnl(pre-prod) */
	if (IS_CNL_REVID(dev_priv, CNL_REVID_B0, CNL_REVID_B0))
		WA_SET_BIT_MASKED(GEN8_ROW_CHICKEN, THROTTLE_12_5);

	/* WaDisableReplayBufferBankArbitrationOptimization:cnl */
	WA_SET_BIT_MASKED(COMMON_SLICE_CHICKEN2,
			  GEN8_SBE_DISABLE_REPLAY_BUF_OPTIMIZATION);

	/* WaDisableEnhancedSBEVertexCaching:cnl (pre-prod) */
	if (IS_CNL_REVID(dev_priv, 0, CNL_REVID_B0))
		WA_SET_BIT_MASKED(COMMON_SLICE_CHICKEN2,
				  GEN8_CSC2_SBE_VUE_CACHE_CONSERVATIVE);

	/* WaPushConstantDereferenceHoldDisable:cnl */
	WA_SET_BIT_MASKED(GEN7_ROW_CHICKEN2, PUSH_CONSTANT_DEREF_DISABLE);

	/* FtrEnableFastAnisoL1BankingFix:cnl */
	WA_SET_BIT_MASKED(HALF_SLICE_CHICKEN3, CNL_FAST_ANISO_L1_BANKING_FIX);

	/* WaDisable3DMidCmdPreemption:cnl */
	WA_CLR_BIT_MASKED(GEN8_CS_CHICKEN1, GEN9_PREEMPT_3D_OBJECT_LEVEL);

	/* WaDisableGPGPUMidCmdPreemption:cnl */
	WA_SET_FIELD_MASKED(GEN8_CS_CHICKEN1,
			    GEN9_PREEMPT_GPGPU_LEVEL_MASK,
			    GEN9_PREEMPT_GPGPU_COMMAND_LEVEL);

	/* WaDisableEarlyEOT:cnl */
	WA_SET_BIT_MASKED(GEN8_ROW_CHICKEN, DISABLE_EARLY_EOT);

	return 0;
}

static int icl_ctx_workarounds_init(struct drm_i915_private *dev_priv)
{
	/* Wa_1604370585:icl (pre-prod)
	 * Formerly known as WaPushConstantDereferenceHoldDisable
	 */
	if (IS_ICL_REVID(dev_priv, ICL_REVID_A0, ICL_REVID_B0))
		WA_SET_BIT_MASKED(GEN7_ROW_CHICKEN2,
				  PUSH_CONSTANT_DEREF_DISABLE);

	/* WaForceEnableNonCoherent:icl
	 * This is not the same workaround as in early Gen9 platforms, where
	 * lacking this could cause system hangs, but coherency performance
	 * overhead is high and only a few compute workloads really need it
	 * (the register is whitelisted in hardware now, so UMDs can opt in
	 * for coherency if they have a good reason).
	 */
	WA_SET_BIT_MASKED(ICL_HDC_MODE, HDC_FORCE_NON_COHERENT);

	/* Wa_2006611047:icl (pre-prod)
	 * Formerly known as WaDisableImprovedTdlClkGating
	 */
	if (IS_ICL_REVID(dev_priv, ICL_REVID_A0, ICL_REVID_A0))
		WA_SET_BIT_MASKED(GEN7_ROW_CHICKEN2,
				  GEN11_TDL_CLOCK_GATING_FIX_DISABLE);

	/* WaEnableStateCacheRedirectToCS:icl */
	WA_SET_BIT_MASKED(GEN9_SLICE_COMMON_ECO_CHICKEN1,
			  GEN11_STATE_CACHE_REDIRECT_TO_CS);

	/* Wa_2006665173:icl (pre-prod) */
	if (IS_ICL_REVID(dev_priv, ICL_REVID_A0, ICL_REVID_A0))
		WA_SET_BIT_MASKED(GEN11_COMMON_SLICE_CHICKEN3,
				  GEN11_BLEND_EMB_FIX_DISABLE_IN_RCC);

	return 0;
}

int intel_ctx_workarounds_init(struct drm_i915_private *dev_priv)
{
	int err = 0;

	dev_priv->workarounds.count = 0;

	if (INTEL_GEN(dev_priv) < 8)
		err = 0;
	else if (IS_BROADWELL(dev_priv))
		err = bdw_ctx_workarounds_init(dev_priv);
	else if (IS_CHERRYVIEW(dev_priv))
		err = chv_ctx_workarounds_init(dev_priv);
	else if (IS_SKYLAKE(dev_priv))
		err = skl_ctx_workarounds_init(dev_priv);
	else if (IS_BROXTON(dev_priv))
		err = bxt_ctx_workarounds_init(dev_priv);
	else if (IS_KABYLAKE(dev_priv))
		err = kbl_ctx_workarounds_init(dev_priv);
	else if (IS_GEMINILAKE(dev_priv))
		err = glk_ctx_workarounds_init(dev_priv);
	else if (IS_COFFEELAKE(dev_priv))
		err = cfl_ctx_workarounds_init(dev_priv);
	else if (IS_CANNONLAKE(dev_priv))
		err = cnl_ctx_workarounds_init(dev_priv);
	else if (IS_ICELAKE(dev_priv))
		err = icl_ctx_workarounds_init(dev_priv);
	else
		MISSING_CASE(INTEL_GEN(dev_priv));
	if (err)
		return err;

	DRM_DEBUG_DRIVER("Number of context specific w/a: %d\n",
			 dev_priv->workarounds.count);
	return 0;
}

int intel_ctx_workarounds_emit(struct i915_request *rq)
{
	struct i915_workarounds *w = &rq->i915->workarounds;
	u32 *cs;
	int ret, i;

	if (w->count == 0)
		return 0;

	ret = rq->engine->emit_flush(rq, EMIT_BARRIER);
	if (ret)
		return ret;

	cs = intel_ring_begin(rq, (w->count * 2 + 2));
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_LOAD_REGISTER_IMM(w->count);
	for (i = 0; i < w->count; i++) {
		*cs++ = w->reg[i].addr;
		*cs++ = w->reg[i].value;
	}
	*cs++ = MI_NOOP;

	intel_ring_advance(rq, cs);

	ret = rq->engine->emit_flush(rq, EMIT_BARRIER);
	if (ret)
		return ret;

	return 0;
}

static void
wal_add(struct i915_wa_list *wal, const struct i915_wa *wa)
{
	const unsigned int grow = 1 << 4;

	GEM_BUG_ON(!is_power_of_2(grow));

	if (IS_ALIGNED(wal->count, grow)) { /* Either uninitialized or full. */
		struct i915_wa *list;

		list = kmalloc_array(ALIGN(wal->count + 1, grow), sizeof(*wa),
				     GFP_KERNEL);
		if (!list) {
			DRM_ERROR("No space for workaround init!\n");
			return;
		}

		if (wal->list)
			memcpy(list, wal->list, sizeof(*wa) * wal->count);

		wal->list = list;
	}

	wal->list[wal->count++] = *wa;
}

static void
wa_masked_en(struct i915_wa_list *wal, i915_reg_t reg, u32 val)
{
	struct i915_wa wa = {
		.reg = reg,
		.mask = val,
		.val = _MASKED_BIT_ENABLE(val)
	};

	wal_add(wal, &wa);
}

static void
wa_write_masked_or(struct i915_wa_list *wal, i915_reg_t reg, u32 mask,
		   u32 val)
{
	struct i915_wa wa = {
		.reg = reg,
		.mask = mask,
		.val = val
	};

	wal_add(wal, &wa);
}

static void
wa_write(struct i915_wa_list *wal, i915_reg_t reg, u32 val)
{
	wa_write_masked_or(wal, reg, ~0, val);
}

static void
wa_write_or(struct i915_wa_list *wal, i915_reg_t reg, u32 val)
{
	wa_write_masked_or(wal, reg, val, val);
}

static void gen9_gt_workarounds_init(struct drm_i915_private *i915)
{
	struct i915_wa_list *wal = &i915->gt_wa_list;

	/* WaContextSwitchWithConcurrentTLBInvalidate:skl,bxt,kbl,glk,cfl */
	wa_masked_en(wal,
		     GEN9_CSFE_CHICKEN1_RCS,
		     GEN9_PREEMPT_GPGPU_SYNC_SWITCH_DISABLE);


	/* WaEnableLbsSlaRetryTimerDecrement:skl,bxt,kbl,glk,cfl */
	wa_write_or(wal,
		    BDW_SCRATCH1,
		    GEN9_LBS_SLA_RETRY_TIMER_DECREMENT_ENABLE);

	/* WaDisableKillLogic:bxt,skl,kbl */
	if (!IS_COFFEELAKE(i915))
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

	/* WaProgramL3SqcReg1DefaultForPerf:bxt,glk */
	if (IS_GEN9_LP(i915))
		wa_write_masked_or(wal,
				   GEN8_L3SQCREG1,
				   L3_PRIO_CREDITS_MASK,
				   L3_GENERAL_PRIO_CREDITS(62) |
				   L3_HIGH_PRIO_CREDITS(2));

	/* WaOCLCoherentLineFlush:skl,bxt,kbl,cfl */
	wa_write_or(wal,
		    GEN8_L3SQCREG4,
		    GEN8_LQSC_FLUSH_COHERENT_LINES);

	/* WaEnablePreemptionGranularityControlByUMD:skl,bxt,kbl,cfl,[cnl] */
	wa_masked_en(wal,
		     GEN7_FF_SLICE_CS_CHICKEN1,
		     GEN9_FFSC_PERCTX_PREEMPT_CTRL);
}

static void skl_gt_workarounds_init(struct drm_i915_private *i915)
{
	struct i915_wa_list *wal = &i915->gt_wa_list;

	gen9_gt_workarounds_init(i915);

	/* WaEnableGapsTsvCreditFix:skl */
	wa_write_or(wal,
		    GEN8_GARBCNTL,
		    GEN9_GAPS_TSV_CREDIT_DISABLE);

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

static void bxt_gt_workarounds_init(struct drm_i915_private *i915)
{
	struct i915_wa_list *wal = &i915->gt_wa_list;

	gen9_gt_workarounds_init(i915);

	/* WaDisablePooledEuLoadBalancingFix:bxt */
	wa_masked_en(wal,
		     FF_SLICE_CS_CHICKEN2,
		     GEN9_POOLED_EU_LOAD_BALANCING_FIX_DISABLE);

	/* WaInPlaceDecompressionHang:bxt */
	wa_write_or(wal,
		    GEN9_GAMT_ECO_REG_RW_IA,
		    GAMT_ECO_ENABLE_IN_PLACE_DECOMPRESS);
}

static void kbl_gt_workarounds_init(struct drm_i915_private *i915)
{
	struct i915_wa_list *wal = &i915->gt_wa_list;

	gen9_gt_workarounds_init(i915);

	/* WaEnableGapsTsvCreditFix:kbl */
	wa_write_or(wal,
		    GEN8_GARBCNTL,
		    GEN9_GAPS_TSV_CREDIT_DISABLE);

	/* WaDisableDynamicCreditSharing:kbl */
	if (IS_KBL_REVID(i915, 0, KBL_REVID_B0))
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

	/* WaKBLVECSSemaphoreWaitPoll:kbl */
	if (IS_KBL_REVID(i915, KBL_REVID_A0, KBL_REVID_E0)) {
		struct intel_engine_cs *engine;
		unsigned int tmp;

		for_each_engine(engine, i915, tmp) {
			if (engine->id == RCS)
				continue;

			wa_write(wal,
				 RING_SEMA_WAIT_POLL(engine->mmio_base),
				 1);
		}
	}
}

static void glk_gt_workarounds_init(struct drm_i915_private *i915)
{
	gen9_gt_workarounds_init(i915);
}

static void cfl_gt_workarounds_init(struct drm_i915_private *i915)
{
	struct i915_wa_list *wal = &i915->gt_wa_list;

	gen9_gt_workarounds_init(i915);

	/* WaEnableGapsTsvCreditFix:cfl */
	wa_write_or(wal,
		    GEN8_GARBCNTL,
		    GEN9_GAPS_TSV_CREDIT_DISABLE);

	/* WaDisableGafsUnitClkGating:cfl */
	wa_write_or(wal,
		    GEN7_UCGCTL4,
		    GEN8_EU_GAUNIT_CLOCK_GATE_DISABLE);

	/* WaInPlaceDecompressionHang:cfl */
	wa_write_or(wal,
		    GEN9_GAMT_ECO_REG_RW_IA,
		    GAMT_ECO_ENABLE_IN_PLACE_DECOMPRESS);
}

static void wa_init_mcr(struct drm_i915_private *dev_priv)
{
	const struct sseu_dev_info *sseu = &(INTEL_INFO(dev_priv)->sseu);
	struct i915_wa_list *wal = &dev_priv->gt_wa_list;
	u32 mcr_slice_subslice_mask;

	/*
	 * WaProgramMgsrForL3BankSpecificMmioReads: cnl,icl
	 * L3Banks could be fused off in single slice scenario. If that is
	 * the case, we might need to program MCR select to a valid L3Bank
	 * by default, to make sure we correctly read certain registers
	 * later on (in the range 0xB100 - 0xB3FF).
	 * This might be incompatible with
	 * WaProgramMgsrForCorrectSliceSpecificMmioReads.
	 * Fortunately, this should not happen in production hardware, so
	 * we only assert that this is the case (instead of implementing
	 * something more complex that requires checking the range of every
	 * MMIO read).
	 */
	if (INTEL_GEN(dev_priv) >= 10 &&
	    is_power_of_2(sseu->slice_mask)) {
		/*
		 * read FUSE3 for enabled L3 Bank IDs, if L3 Bank matches
		 * enabled subslice, no need to redirect MCR packet
		 */
		u32 slice = fls(sseu->slice_mask);
		u32 fuse3 = I915_READ(GEN10_MIRROR_FUSE3);
		u8 ss_mask = sseu->subslice_mask[slice];

		u8 enabled_mask = (ss_mask | ss_mask >>
				   GEN10_L3BANK_PAIR_COUNT) & GEN10_L3BANK_MASK;
		u8 disabled_mask = fuse3 & GEN10_L3BANK_MASK;

		/*
		 * Production silicon should have matched L3Bank and
		 * subslice enabled
		 */
		WARN_ON((enabled_mask & disabled_mask) != enabled_mask);
	}

	if (INTEL_GEN(dev_priv) >= 11)
		mcr_slice_subslice_mask = GEN11_MCR_SLICE_MASK |
					  GEN11_MCR_SUBSLICE_MASK;
	else
		mcr_slice_subslice_mask = GEN8_MCR_SLICE_MASK |
					  GEN8_MCR_SUBSLICE_MASK;
	/*
	 * WaProgramMgsrForCorrectSliceSpecificMmioReads:cnl,icl
	 * Before any MMIO read into slice/subslice specific registers, MCR
	 * packet control register needs to be programmed to point to any
	 * enabled s/ss pair. Otherwise, incorrect values will be returned.
	 * This means each subsequent MMIO read will be forwarded to an
	 * specific s/ss combination, but this is OK since these registers
	 * are consistent across s/ss in almost all cases. In the rare
	 * occasions, such as INSTDONE, where this value is dependent
	 * on s/ss combo, the read should be done with read_subslice_reg.
	 */
	wa_write_masked_or(wal,
			   GEN8_MCR_SELECTOR,
			   mcr_slice_subslice_mask,
			   intel_calculate_mcr_s_ss_select(dev_priv));
}

static void cnl_gt_workarounds_init(struct drm_i915_private *i915)
{
	struct i915_wa_list *wal = &i915->gt_wa_list;

	wa_init_mcr(i915);

	/* WaDisableI2mCycleOnWRPort:cnl (pre-prod) */
	if (IS_CNL_REVID(i915, CNL_REVID_B0, CNL_REVID_B0))
		wa_write_or(wal,
			    GAMT_CHKN_BIT_REG,
			    GAMT_CHKN_DISABLE_I2M_CYCLE_ON_WR_PORT);

	/* WaInPlaceDecompressionHang:cnl */
	wa_write_or(wal,
		    GEN9_GAMT_ECO_REG_RW_IA,
		    GAMT_ECO_ENABLE_IN_PLACE_DECOMPRESS);

	/* WaEnablePreemptionGranularityControlByUMD:cnl */
	wa_masked_en(wal,
		     GEN7_FF_SLICE_CS_CHICKEN1,
		     GEN9_FFSC_PERCTX_PREEMPT_CTRL);
}

static void icl_gt_workarounds_init(struct drm_i915_private *i915)
{
	struct i915_wa_list *wal = &i915->gt_wa_list;

	wa_init_mcr(i915);

	/* This is not an Wa. Enable for better image quality */
	wa_masked_en(wal,
		     _3D_CHICKEN3,
		     _3D_CHICKEN3_AA_LINE_QUALITY_FIX_ENABLE);

	/* WaInPlaceDecompressionHang:icl */
	wa_write_or(wal,
		    GEN9_GAMT_ECO_REG_RW_IA,
		    GAMT_ECO_ENABLE_IN_PLACE_DECOMPRESS);

	/* WaPipelineFlushCoherentLines:icl */
	wa_write_or(wal,
		    GEN8_L3SQCREG4,
		    GEN8_LQSC_FLUSH_COHERENT_LINES);

	/* Wa_1405543622:icl
	 * Formerly known as WaGAPZPriorityScheme
	 */
	wa_write_or(wal,
		    GEN8_GARBCNTL,
		    GEN11_ARBITRATION_PRIO_ORDER_MASK);

	/* Wa_1604223664:icl
	 * Formerly known as WaL3BankAddressHashing
	 */
	wa_write_masked_or(wal,
			   GEN8_GARBCNTL,
			   GEN11_HASH_CTRL_EXCL_MASK,
			   GEN11_HASH_CTRL_EXCL_BIT0);
	wa_write_masked_or(wal,
			   GEN11_GLBLINVL,
			   GEN11_BANK_HASH_ADDR_EXCL_MASK,
			   GEN11_BANK_HASH_ADDR_EXCL_BIT0);

	/* WaModifyGamTlbPartitioning:icl */
	wa_write_masked_or(wal,
			   GEN11_GACB_PERF_CTRL,
			   GEN11_HASH_CTRL_MASK,
			   GEN11_HASH_CTRL_BIT0 | GEN11_HASH_CTRL_BIT4);

	/* Wa_1405733216:icl
	 * Formerly known as WaDisableCleanEvicts
	 */
	wa_write_or(wal,
		    GEN8_L3SQCREG4,
		    GEN11_LQSC_CLEAN_EVICT_DISABLE);

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

	/* Wa_1406680159:icl */
	wa_write_or(wal,
		    SUBSLICE_UNIT_LEVEL_CLKGATE,
		    GWUNIT_CLKGATE_DIS);

	/* Wa_1604302699:icl */
	wa_write_or(wal,
		    GEN10_L3_CHICKEN_MODE_REGISTER,
		    GEN11_I2M_WRITE_DISABLE);

	/* Wa_1406838659:icl (pre-prod) */
	if (IS_ICL_REVID(i915, ICL_REVID_A0, ICL_REVID_B0))
		wa_write_or(wal,
			    INF_UNIT_LEVEL_CLKGATE,
			    CGPSF_CLKGATE_DIS);

	/* WaForwardProgressSoftReset:icl */
	wa_write_or(wal,
		    GEN10_SCRATCH_LNCF2,
		    PMFLUSHDONE_LNICRSDROP |
		    PMFLUSH_GAPL3UNBLOCK |
		    PMFLUSHDONE_LNEBLK);

	/* Wa_1406463099:icl
	 * Formerly known as WaGamTlbPendError
	 */
	wa_write_or(wal,
		    GAMT_CHKN_BIT_REG,
		    GAMT_CHKN_DISABLE_L3_COH_PIPE);
}

void intel_gt_init_workarounds(struct drm_i915_private *i915)
{
	struct i915_wa_list *wal = &i915->gt_wa_list;

	wa_init_start(wal, "GT");

	if (INTEL_GEN(i915) < 8)
		return;
	else if (IS_BROADWELL(i915))
		return;
	else if (IS_CHERRYVIEW(i915))
		return;
	else if (IS_SKYLAKE(i915))
		skl_gt_workarounds_init(i915);
	else if (IS_BROXTON(i915))
		bxt_gt_workarounds_init(i915);
	else if (IS_KABYLAKE(i915))
		kbl_gt_workarounds_init(i915);
	else if (IS_GEMINILAKE(i915))
		glk_gt_workarounds_init(i915);
	else if (IS_COFFEELAKE(i915))
		cfl_gt_workarounds_init(i915);
	else if (IS_CANNONLAKE(i915))
		cnl_gt_workarounds_init(i915);
	else if (IS_ICELAKE(i915))
		icl_gt_workarounds_init(i915);
	else
		MISSING_CASE(INTEL_GEN(i915));

	wa_init_finish(wal);
}

static enum forcewake_domains
wal_get_fw_for_rmw(struct drm_i915_private *dev_priv,
		   const struct i915_wa_list *wal)
{
	enum forcewake_domains fw = 0;
	struct i915_wa *wa;
	unsigned int i;

	for (i = 0, wa = wal->list; i < wal->count; i++, wa++)
		fw |= intel_uncore_forcewake_for_reg(dev_priv,
						     wa->reg,
						     FW_REG_READ |
						     FW_REG_WRITE);

	return fw;
}

static void
wa_list_apply(struct drm_i915_private *dev_priv, const struct i915_wa_list *wal)
{
	enum forcewake_domains fw;
	unsigned long flags;
	struct i915_wa *wa;
	unsigned int i;

	if (!wal->count)
		return;

	fw = wal_get_fw_for_rmw(dev_priv, wal);

	spin_lock_irqsave(&dev_priv->uncore.lock, flags);
	intel_uncore_forcewake_get__locked(dev_priv, fw);

	for (i = 0, wa = wal->list; i < wal->count; i++, wa++) {
		u32 val = I915_READ_FW(wa->reg);

		val &= ~wa->mask;
		val |= wa->val;

		I915_WRITE_FW(wa->reg, val);
	}

	intel_uncore_forcewake_put__locked(dev_priv, fw);
	spin_unlock_irqrestore(&dev_priv->uncore.lock, flags);

	DRM_DEBUG_DRIVER("Applied %u %s workarounds\n", wal->count, wal->name);
}

void intel_gt_apply_workarounds(struct drm_i915_private *dev_priv)
{
	wa_list_apply(dev_priv, &dev_priv->gt_wa_list);
}

struct whitelist {
	i915_reg_t reg[RING_MAX_NONPRIV_SLOTS];
	unsigned int count;
	u32 nopid;
};

static void whitelist_reg(struct whitelist *w, i915_reg_t reg)
{
	if (GEM_WARN_ON(w->count >= RING_MAX_NONPRIV_SLOTS))
		return;

	w->reg[w->count++] = reg;
}

static void bdw_whitelist_build(struct whitelist *w)
{
}

static void chv_whitelist_build(struct whitelist *w)
{
}

static void gen9_whitelist_build(struct whitelist *w)
{
	/* WaVFEStateAfterPipeControlwithMediaStateClear:skl,bxt,glk,cfl */
	whitelist_reg(w, GEN9_CTX_PREEMPT_REG);

	/* WaEnablePreemptionGranularityControlByUMD:skl,bxt,kbl,cfl,[cnl] */
	whitelist_reg(w, GEN8_CS_CHICKEN1);

	/* WaAllowUMDToModifyHDCChicken1:skl,bxt,kbl,glk,cfl */
	whitelist_reg(w, GEN8_HDC_CHICKEN1);
}

static void skl_whitelist_build(struct whitelist *w)
{
	gen9_whitelist_build(w);

	/* WaDisableLSQCROPERFforOCL:skl */
	whitelist_reg(w, GEN8_L3SQCREG4);
}

static void bxt_whitelist_build(struct whitelist *w)
{
	gen9_whitelist_build(w);
}

static void kbl_whitelist_build(struct whitelist *w)
{
	gen9_whitelist_build(w);

	/* WaDisableLSQCROPERFforOCL:kbl */
	whitelist_reg(w, GEN8_L3SQCREG4);
}

static void glk_whitelist_build(struct whitelist *w)
{
	gen9_whitelist_build(w);

	/* WA #0862: Userspace has to set "Barrier Mode" to avoid hangs. */
	whitelist_reg(w, GEN9_SLICE_COMMON_ECO_CHICKEN1);
}

static void cfl_whitelist_build(struct whitelist *w)
{
	gen9_whitelist_build(w);
}

static void cnl_whitelist_build(struct whitelist *w)
{
	/* WaEnablePreemptionGranularityControlByUMD:cnl */
	whitelist_reg(w, GEN8_CS_CHICKEN1);
}

static void icl_whitelist_build(struct whitelist *w)
{
}

static struct whitelist *whitelist_build(struct intel_engine_cs *engine,
					 struct whitelist *w)
{
	struct drm_i915_private *i915 = engine->i915;

	GEM_BUG_ON(engine->id != RCS);

	w->count = 0;
	w->nopid = i915_mmio_reg_offset(RING_NOPID(engine->mmio_base));

	if (INTEL_GEN(i915) < 8)
		return NULL;
	else if (IS_BROADWELL(i915))
		bdw_whitelist_build(w);
	else if (IS_CHERRYVIEW(i915))
		chv_whitelist_build(w);
	else if (IS_SKYLAKE(i915))
		skl_whitelist_build(w);
	else if (IS_BROXTON(i915))
		bxt_whitelist_build(w);
	else if (IS_KABYLAKE(i915))
		kbl_whitelist_build(w);
	else if (IS_GEMINILAKE(i915))
		glk_whitelist_build(w);
	else if (IS_COFFEELAKE(i915))
		cfl_whitelist_build(w);
	else if (IS_CANNONLAKE(i915))
		cnl_whitelist_build(w);
	else if (IS_ICELAKE(i915))
		icl_whitelist_build(w);
	else
		MISSING_CASE(INTEL_GEN(i915));

	return w;
}

static void whitelist_apply(struct intel_engine_cs *engine,
			    const struct whitelist *w)
{
	struct drm_i915_private *dev_priv = engine->i915;
	const u32 base = engine->mmio_base;
	unsigned int i;

	if (!w)
		return;

	intel_uncore_forcewake_get(engine->i915, FORCEWAKE_ALL);

	for (i = 0; i < w->count; i++)
		I915_WRITE_FW(RING_FORCE_TO_NONPRIV(base, i),
			      i915_mmio_reg_offset(w->reg[i]));

	/* And clear the rest just in case of garbage */
	for (; i < RING_MAX_NONPRIV_SLOTS; i++)
		I915_WRITE_FW(RING_FORCE_TO_NONPRIV(base, i), w->nopid);

	intel_uncore_forcewake_put(engine->i915, FORCEWAKE_ALL);
}

void intel_whitelist_workarounds_apply(struct intel_engine_cs *engine)
{
	struct whitelist w;

	whitelist_apply(engine, whitelist_build(engine, &w));
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/intel_workarounds.c"
#endif

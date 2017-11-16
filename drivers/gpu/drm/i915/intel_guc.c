/*
 * Copyright © 2014-2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include "intel_guc.h"
#include "intel_guc_submission.h"
#include "i915_drv.h"

static void gen8_guc_raise_irq(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);

	I915_WRITE(GUC_SEND_INTERRUPT, GUC_SEND_TRIGGER);
}

static inline i915_reg_t guc_send_reg(struct intel_guc *guc, u32 i)
{
	GEM_BUG_ON(!guc->send_regs.base);
	GEM_BUG_ON(!guc->send_regs.count);
	GEM_BUG_ON(i >= guc->send_regs.count);

	return _MMIO(guc->send_regs.base + 4 * i);
}

void intel_guc_init_send_regs(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	enum forcewake_domains fw_domains = 0;
	unsigned int i;

	guc->send_regs.base = i915_mmio_reg_offset(SOFT_SCRATCH(0));
	guc->send_regs.count = SOFT_SCRATCH_COUNT - 1;

	for (i = 0; i < guc->send_regs.count; i++) {
		fw_domains |= intel_uncore_forcewake_for_reg(dev_priv,
					guc_send_reg(guc, i),
					FW_REG_READ | FW_REG_WRITE);
	}
	guc->send_regs.fw_domains = fw_domains;
}

void intel_guc_init_early(struct intel_guc *guc)
{
	intel_guc_ct_init_early(&guc->ct);

	mutex_init(&guc->send_mutex);
	guc->send = intel_guc_send_nop;
	guc->notify = gen8_guc_raise_irq;
}

static u32 get_gt_type(struct drm_i915_private *dev_priv)
{
	/* XXX: GT type based on PCI device ID? field seems unused by fw */
	return 0;
}

static u32 get_core_family(struct drm_i915_private *dev_priv)
{
	u32 gen = INTEL_GEN(dev_priv);

	switch (gen) {
	case 9:
		return GUC_CORE_FAMILY_GEN9;

	default:
		MISSING_CASE(gen);
		return GUC_CORE_FAMILY_UNKNOWN;
	}
}

/*
 * Initialise the GuC parameter block before starting the firmware
 * transfer. These parameters are read by the firmware on startup
 * and cannot be changed thereafter.
 */
void intel_guc_init_params(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	u32 params[GUC_CTL_MAX_DWORDS];
	int i;

	memset(params, 0, sizeof(params));

	params[GUC_CTL_DEVICE_INFO] |=
		(get_gt_type(dev_priv) << GUC_CTL_GT_TYPE_SHIFT) |
		(get_core_family(dev_priv) << GUC_CTL_CORE_FAMILY_SHIFT);

	/*
	 * GuC ARAT increment is 10 ns. GuC default scheduler quantum is one
	 * second. This ARAR is calculated by:
	 * Scheduler-Quantum-in-ns / ARAT-increment-in-ns = 1000000000 / 10
	 */
	params[GUC_CTL_ARAT_HIGH] = 0;
	params[GUC_CTL_ARAT_LOW] = 100000000;

	params[GUC_CTL_WA] |= GUC_CTL_WA_UK_BY_DRIVER;

	params[GUC_CTL_FEATURE] |= GUC_CTL_DISABLE_SCHEDULER |
			GUC_CTL_VCS2_ENABLED;

	params[GUC_CTL_LOG_PARAMS] = guc->log.flags;

	if (i915_modparams.guc_log_level >= 0) {
		params[GUC_CTL_DEBUG] =
			i915_modparams.guc_log_level << GUC_LOG_VERBOSITY_SHIFT;
	} else {
		params[GUC_CTL_DEBUG] = GUC_LOG_DISABLED;
	}

	/* If GuC submission is enabled, set up additional parameters here */
	if (i915_modparams.enable_guc_submission) {
		u32 ads = guc_ggtt_offset(guc->ads_vma) >> PAGE_SHIFT;
		u32 pgs = guc_ggtt_offset(dev_priv->guc.stage_desc_pool);
		u32 ctx_in_16 = GUC_MAX_STAGE_DESCRIPTORS / 16;

		params[GUC_CTL_DEBUG] |= ads << GUC_ADS_ADDR_SHIFT;
		params[GUC_CTL_DEBUG] |= GUC_ADS_ENABLED;

		pgs >>= PAGE_SHIFT;
		params[GUC_CTL_CTXINFO] = (pgs << GUC_CTL_BASE_ADDR_SHIFT) |
			(ctx_in_16 << GUC_CTL_CTXNUM_IN16_SHIFT);

		params[GUC_CTL_FEATURE] |= GUC_CTL_KERNEL_SUBMISSIONS;

		/* Unmask this bit to enable the GuC's internal scheduler */
		params[GUC_CTL_FEATURE] &= ~GUC_CTL_DISABLE_SCHEDULER;
	}

	/*
	 * All SOFT_SCRATCH registers are in FORCEWAKE_BLITTER domain and
	 * they are power context saved so it's ok to release forcewake
	 * when we are done here and take it again at xfer time.
	 */
	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_BLITTER);

	I915_WRITE(SOFT_SCRATCH(0), 0);

	for (i = 0; i < GUC_CTL_MAX_DWORDS; i++)
		I915_WRITE(SOFT_SCRATCH(1 + i), params[i]);

	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_BLITTER);
}

int intel_guc_send_nop(struct intel_guc *guc, const u32 *action, u32 len)
{
	WARN(1, "Unexpected send: action=%#x\n", *action);
	return -ENODEV;
}

/*
 * This function implements the MMIO based host to GuC interface.
 */
int intel_guc_send_mmio(struct intel_guc *guc, const u32 *action, u32 len)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	u32 status;
	int i;
	int ret;

	GEM_BUG_ON(!len);
	GEM_BUG_ON(len > guc->send_regs.count);

	/* If CT is available, we expect to use MMIO only during init/fini */
	GEM_BUG_ON(HAS_GUC_CT(dev_priv) &&
		*action != INTEL_GUC_ACTION_REGISTER_COMMAND_TRANSPORT_BUFFER &&
		*action != INTEL_GUC_ACTION_DEREGISTER_COMMAND_TRANSPORT_BUFFER);

	mutex_lock(&guc->send_mutex);
	intel_uncore_forcewake_get(dev_priv, guc->send_regs.fw_domains);

	for (i = 0; i < len; i++)
		I915_WRITE(guc_send_reg(guc, i), action[i]);

	POSTING_READ(guc_send_reg(guc, i - 1));

	intel_guc_notify(guc);

	/*
	 * No GuC command should ever take longer than 10ms.
	 * Fast commands should still complete in 10us.
	 */
	ret = __intel_wait_for_register_fw(dev_priv,
					   guc_send_reg(guc, 0),
					   INTEL_GUC_RECV_MASK,
					   INTEL_GUC_RECV_MASK,
					   10, 10, &status);
	if (status != INTEL_GUC_STATUS_SUCCESS) {
		/*
		 * Either the GuC explicitly returned an error (which
		 * we convert to -EIO here) or no response at all was
		 * received within the timeout limit (-ETIMEDOUT)
		 */
		if (ret != -ETIMEDOUT)
			ret = -EIO;

		DRM_WARN("INTEL_GUC_SEND: Action 0x%X failed;"
			 " ret=%d status=0x%08X response=0x%08X\n",
			 action[0], ret, status, I915_READ(SOFT_SCRATCH(15)));
	}

	intel_uncore_forcewake_put(dev_priv, guc->send_regs.fw_domains);
	mutex_unlock(&guc->send_mutex);

	return ret;
}

int intel_guc_sample_forcewake(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	u32 action[2];

	action[0] = INTEL_GUC_ACTION_SAMPLE_FORCEWAKE;
	/* WaRsDisableCoarsePowerGating:skl,bxt */
	if (!intel_rc6_enabled() ||
	    NEEDS_WaRsDisableCoarsePowerGating(dev_priv))
		action[1] = 0;
	else
		/* bit 0 and 1 are for Render and Media domain separately */
		action[1] = GUC_FORCEWAKE_RENDER | GUC_FORCEWAKE_MEDIA;

	return intel_guc_send(guc, action, ARRAY_SIZE(action));
}

/**
 * intel_guc_auth_huc() - Send action to GuC to authenticate HuC ucode
 * @guc: intel_guc structure
 * @rsa_offset: rsa offset w.r.t ggtt base of huc vma
 *
 * Triggers a HuC firmware authentication request to the GuC via intel_guc_send
 * INTEL_GUC_ACTION_AUTHENTICATE_HUC interface. This function is invoked by
 * intel_huc_auth().
 *
 * Return:	non-zero code on error
 */
int intel_guc_auth_huc(struct intel_guc *guc, u32 rsa_offset)
{
	u32 action[] = {
		INTEL_GUC_ACTION_AUTHENTICATE_HUC,
		rsa_offset
	};

	return intel_guc_send(guc, action, ARRAY_SIZE(action));
}

/**
 * intel_guc_suspend() - notify GuC entering suspend state
 * @dev_priv:	i915 device private
 */
int intel_guc_suspend(struct drm_i915_private *dev_priv)
{
	struct intel_guc *guc = &dev_priv->guc;
	u32 data[3];

	if (guc->fw.load_status != INTEL_UC_FIRMWARE_SUCCESS)
		return 0;

	gen9_disable_guc_interrupts(dev_priv);

	data[0] = INTEL_GUC_ACTION_ENTER_S_STATE;
	/* any value greater than GUC_POWER_D0 */
	data[1] = GUC_POWER_D1;
	data[2] = guc_ggtt_offset(guc->shared_data);

	return intel_guc_send(guc, data, ARRAY_SIZE(data));
}

/**
 * intel_guc_reset_engine() - ask GuC to reset an engine
 * @guc:	intel_guc structure
 * @engine:	engine to be reset
 */
int intel_guc_reset_engine(struct intel_guc *guc,
			   struct intel_engine_cs *engine)
{
	u32 data[7];

	GEM_BUG_ON(!guc->execbuf_client);

	data[0] = INTEL_GUC_ACTION_REQUEST_ENGINE_RESET;
	data[1] = engine->guc_id;
	data[2] = 0;
	data[3] = 0;
	data[4] = 0;
	data[5] = guc->execbuf_client->stage_id;
	data[6] = guc_ggtt_offset(guc->shared_data);

	return intel_guc_send(guc, data, ARRAY_SIZE(data));
}

/**
 * intel_guc_resume() - notify GuC resuming from suspend state
 * @dev_priv:	i915 device private
 */
int intel_guc_resume(struct drm_i915_private *dev_priv)
{
	struct intel_guc *guc = &dev_priv->guc;
	u32 data[3];

	if (guc->fw.load_status != INTEL_UC_FIRMWARE_SUCCESS)
		return 0;

	if (i915_modparams.guc_log_level >= 0)
		gen9_enable_guc_interrupts(dev_priv);

	data[0] = INTEL_GUC_ACTION_EXIT_S_STATE;
	data[1] = GUC_POWER_D0;
	data[2] = guc_ggtt_offset(guc->shared_data);

	return intel_guc_send(guc, data, ARRAY_SIZE(data));
}

/**
 * intel_guc_allocate_vma() - Allocate a GGTT VMA for GuC usage
 * @guc:	the guc
 * @size:	size of area to allocate (both virtual space and memory)
 *
 * This is a wrapper to create an object for use with the GuC. In order to
 * use it inside the GuC, an object needs to be pinned lifetime, so we allocate
 * both some backing storage and a range inside the Global GTT. We must pin
 * it in the GGTT somewhere other than than [0, GUC_WOPCM_TOP) because that
 * range is reserved inside GuC.
 *
 * Return:	A i915_vma if successful, otherwise an ERR_PTR.
 */
struct i915_vma *intel_guc_allocate_vma(struct intel_guc *guc, u32 size)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int ret;

	obj = i915_gem_object_create(dev_priv, size);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	vma = i915_vma_instance(obj, &dev_priv->ggtt.base, NULL);
	if (IS_ERR(vma))
		goto err;

	ret = i915_vma_pin(vma, 0, PAGE_SIZE,
			   PIN_GLOBAL | PIN_OFFSET_BIAS | GUC_WOPCM_TOP);
	if (ret) {
		vma = ERR_PTR(ret);
		goto err;
	}

	return vma;

err:
	i915_gem_object_put(obj);
	return vma;
}

u32 intel_guc_wopcm_size(struct drm_i915_private *dev_priv)
{
	u32 wopcm_size = GUC_WOPCM_TOP;

	/* On BXT, the top of WOPCM is reserved for RC6 context */
	if (IS_GEN9_LP(dev_priv))
		wopcm_size -= BXT_GUC_WOPCM_RC6_RESERVED;

	return wopcm_size;
}

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
	if (!intel_enable_rc6() || NEEDS_WaRsDisableCoarsePowerGating(dev_priv))
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
	struct i915_gem_context *ctx;
	u32 data[3];

	if (guc->fw.load_status != INTEL_UC_FIRMWARE_SUCCESS)
		return 0;

	gen9_disable_guc_interrupts(dev_priv);

	ctx = dev_priv->kernel_context;

	data[0] = INTEL_GUC_ACTION_ENTER_S_STATE;
	/* any value greater than GUC_POWER_D0 */
	data[1] = GUC_POWER_D1;
	/* first page is shared data with GuC */
	data[2] = guc_ggtt_offset(ctx->engine[RCS].state) +
		  LRC_GUCSHR_PN * PAGE_SIZE;

	return intel_guc_send(guc, data, ARRAY_SIZE(data));
}

/**
 * intel_guc_resume() - notify GuC resuming from suspend state
 * @dev_priv:	i915 device private
 */
int intel_guc_resume(struct drm_i915_private *dev_priv)
{
	struct intel_guc *guc = &dev_priv->guc;
	struct i915_gem_context *ctx;
	u32 data[3];

	if (guc->fw.load_status != INTEL_UC_FIRMWARE_SUCCESS)
		return 0;

	if (i915_modparams.guc_log_level >= 0)
		gen9_enable_guc_interrupts(dev_priv);

	ctx = dev_priv->kernel_context;

	data[0] = INTEL_GUC_ACTION_EXIT_S_STATE;
	data[1] = GUC_POWER_D0;
	/* first page is shared data with GuC */
	data[2] = guc_ggtt_offset(ctx->engine[RCS].state) +
		  LRC_GUCSHR_PN * PAGE_SIZE;

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

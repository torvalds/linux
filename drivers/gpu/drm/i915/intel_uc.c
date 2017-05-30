/*
 * Copyright © 2016 Intel Corporation
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

#include "i915_drv.h"
#include "intel_uc.h"
#include <linux/firmware.h>

/* Cleans up uC firmware by releasing the firmware GEM obj.
 */
static void __intel_uc_fw_fini(struct intel_uc_fw *uc_fw)
{
	struct drm_i915_gem_object *obj;

	obj = fetch_and_zero(&uc_fw->obj);
	if (obj)
		i915_gem_object_put(obj);

	uc_fw->fetch_status = INTEL_UC_FIRMWARE_NONE;
}

/* Reset GuC providing us with fresh state for both GuC and HuC.
 */
static int __intel_uc_reset_hw(struct drm_i915_private *dev_priv)
{
	int ret;
	u32 guc_status;

	ret = intel_guc_reset(dev_priv);
	if (ret) {
		DRM_ERROR("GuC reset failed, ret = %d\n", ret);
		return ret;
	}

	guc_status = I915_READ(GUC_STATUS);
	WARN(!(guc_status & GS_MIA_IN_RESET),
	     "GuC status: 0x%x, MIA core expected to be in reset\n",
	     guc_status);

	return ret;
}

void intel_uc_sanitize_options(struct drm_i915_private *dev_priv)
{
	if (!HAS_GUC(dev_priv)) {
		if (i915.enable_guc_loading > 0 ||
		    i915.enable_guc_submission > 0)
			DRM_INFO("Ignoring GuC options, no hardware\n");

		i915.enable_guc_loading = 0;
		i915.enable_guc_submission = 0;
		return;
	}

	/* A negative value means "use platform default" */
	if (i915.enable_guc_loading < 0)
		i915.enable_guc_loading = HAS_GUC_UCODE(dev_priv);

	/* Verify firmware version */
	if (i915.enable_guc_loading) {
		if (HAS_HUC_UCODE(dev_priv))
			intel_huc_select_fw(&dev_priv->huc);

		if (intel_guc_select_fw(&dev_priv->guc))
			i915.enable_guc_loading = 0;
	}

	/* Can't enable guc submission without guc loaded */
	if (!i915.enable_guc_loading)
		i915.enable_guc_submission = 0;

	/* A negative value means "use platform default" */
	if (i915.enable_guc_submission < 0)
		i915.enable_guc_submission = HAS_GUC_SCHED(dev_priv);
}

static void guc_write_irq_trigger(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);

	I915_WRITE(GUC_SEND_INTERRUPT, GUC_SEND_TRIGGER);
}

void intel_uc_init_early(struct drm_i915_private *dev_priv)
{
	struct intel_guc *guc = &dev_priv->guc;

	intel_guc_ct_init_early(&guc->ct);

	mutex_init(&guc->send_mutex);
	guc->send = intel_guc_send_nop;
	guc->notify = guc_write_irq_trigger;
}

static void fetch_uc_fw(struct drm_i915_private *dev_priv,
			struct intel_uc_fw *uc_fw)
{
	struct pci_dev *pdev = dev_priv->drm.pdev;
	struct drm_i915_gem_object *obj;
	const struct firmware *fw = NULL;
	struct uc_css_header *css;
	size_t size;
	int err;

	if (!uc_fw->path)
		return;

	uc_fw->fetch_status = INTEL_UC_FIRMWARE_PENDING;

	DRM_DEBUG_DRIVER("before requesting firmware: uC fw fetch status %s\n",
			 intel_uc_fw_status_repr(uc_fw->fetch_status));

	err = request_firmware(&fw, uc_fw->path, &pdev->dev);
	if (err)
		goto fail;
	if (!fw)
		goto fail;

	DRM_DEBUG_DRIVER("fetch uC fw from %s succeeded, fw %p\n",
			 uc_fw->path, fw);

	/* Check the size of the blob before examining buffer contents */
	if (fw->size < sizeof(struct uc_css_header)) {
		DRM_NOTE("Firmware header is missing\n");
		goto fail;
	}

	css = (struct uc_css_header *)fw->data;

	/* Firmware bits always start from header */
	uc_fw->header_offset = 0;
	uc_fw->header_size = (css->header_size_dw - css->modulus_size_dw -
			      css->key_size_dw - css->exponent_size_dw) * sizeof(u32);

	if (uc_fw->header_size != sizeof(struct uc_css_header)) {
		DRM_NOTE("CSS header definition mismatch\n");
		goto fail;
	}

	/* then, uCode */
	uc_fw->ucode_offset = uc_fw->header_offset + uc_fw->header_size;
	uc_fw->ucode_size = (css->size_dw - css->header_size_dw) * sizeof(u32);

	/* now RSA */
	if (css->key_size_dw != UOS_RSA_SCRATCH_MAX_COUNT) {
		DRM_NOTE("RSA key size is bad\n");
		goto fail;
	}
	uc_fw->rsa_offset = uc_fw->ucode_offset + uc_fw->ucode_size;
	uc_fw->rsa_size = css->key_size_dw * sizeof(u32);

	/* At least, it should have header, uCode and RSA. Size of all three. */
	size = uc_fw->header_size + uc_fw->ucode_size + uc_fw->rsa_size;
	if (fw->size < size) {
		DRM_NOTE("Missing firmware components\n");
		goto fail;
	}

	/*
	 * The GuC firmware image has the version number embedded at a
	 * well-known offset within the firmware blob; note that major / minor
	 * version are TWO bytes each (i.e. u16), although all pointers and
	 * offsets are defined in terms of bytes (u8).
	 */
	switch (uc_fw->type) {
	case INTEL_UC_FW_TYPE_GUC:
		/* Header and uCode will be loaded to WOPCM. Size of the two. */
		size = uc_fw->header_size + uc_fw->ucode_size;

		/* Top 32k of WOPCM is reserved (8K stack + 24k RC6 context). */
		if (size > intel_guc_wopcm_size(dev_priv)) {
			DRM_ERROR("Firmware is too large to fit in WOPCM\n");
			goto fail;
		}
		uc_fw->major_ver_found = css->guc.sw_version >> 16;
		uc_fw->minor_ver_found = css->guc.sw_version & 0xFFFF;
		break;

	case INTEL_UC_FW_TYPE_HUC:
		uc_fw->major_ver_found = css->huc.sw_version >> 16;
		uc_fw->minor_ver_found = css->huc.sw_version & 0xFFFF;
		break;

	default:
		DRM_ERROR("Unknown firmware type %d\n", uc_fw->type);
		err = -ENOEXEC;
		goto fail;
	}

	if (uc_fw->major_ver_wanted == 0 && uc_fw->minor_ver_wanted == 0) {
		DRM_NOTE("Skipping %s firmware version check\n",
			 intel_uc_fw_type_repr(uc_fw->type));
	} else if (uc_fw->major_ver_found != uc_fw->major_ver_wanted ||
		   uc_fw->minor_ver_found < uc_fw->minor_ver_wanted) {
		DRM_NOTE("%s firmware version %d.%d, required %d.%d\n",
			 intel_uc_fw_type_repr(uc_fw->type),
			 uc_fw->major_ver_found, uc_fw->minor_ver_found,
			 uc_fw->major_ver_wanted, uc_fw->minor_ver_wanted);
		err = -ENOEXEC;
		goto fail;
	}

	DRM_DEBUG_DRIVER("firmware version %d.%d OK (minimum %d.%d)\n",
			 uc_fw->major_ver_found, uc_fw->minor_ver_found,
			 uc_fw->major_ver_wanted, uc_fw->minor_ver_wanted);

	obj = i915_gem_object_create_from_data(dev_priv, fw->data, fw->size);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto fail;
	}

	uc_fw->obj = obj;
	uc_fw->size = fw->size;

	DRM_DEBUG_DRIVER("uC fw fetch status SUCCESS, obj %p\n",
			 uc_fw->obj);

	release_firmware(fw);
	uc_fw->fetch_status = INTEL_UC_FIRMWARE_SUCCESS;
	return;

fail:
	DRM_WARN("Failed to fetch valid uC firmware from %s (error %d)\n",
		 uc_fw->path, err);
	DRM_DEBUG_DRIVER("uC fw fetch status FAIL; err %d, fw %p, obj %p\n",
			 err, fw, uc_fw->obj);

	release_firmware(fw);		/* OK even if fw is NULL */
	uc_fw->fetch_status = INTEL_UC_FIRMWARE_FAIL;
}

void intel_uc_init_fw(struct drm_i915_private *dev_priv)
{
	fetch_uc_fw(dev_priv, &dev_priv->huc.fw);
	fetch_uc_fw(dev_priv, &dev_priv->guc.fw);
}

void intel_uc_fini_fw(struct drm_i915_private *dev_priv)
{
	__intel_uc_fw_fini(&dev_priv->guc.fw);
	__intel_uc_fw_fini(&dev_priv->huc.fw);
}

static inline i915_reg_t guc_send_reg(struct intel_guc *guc, u32 i)
{
	GEM_BUG_ON(!guc->send_regs.base);
	GEM_BUG_ON(!guc->send_regs.count);
	GEM_BUG_ON(i >= guc->send_regs.count);

	return _MMIO(guc->send_regs.base + 4 * i);
}

static void guc_init_send_regs(struct intel_guc *guc)
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

static void guc_capture_load_err_log(struct intel_guc *guc)
{
	if (!guc->log.vma || i915.guc_log_level < 0)
		return;

	if (!guc->load_err_log)
		guc->load_err_log = i915_gem_object_get(guc->log.vma->obj);

	return;
}

static void guc_free_load_err_log(struct intel_guc *guc)
{
	if (guc->load_err_log)
		i915_gem_object_put(guc->load_err_log);
}

static int guc_enable_communication(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);

	guc_init_send_regs(guc);

	if (HAS_GUC_CT(dev_priv))
		return intel_guc_enable_ct(guc);

	guc->send = intel_guc_send_mmio;
	return 0;
}

static void guc_disable_communication(struct intel_guc *guc)
{
	struct drm_i915_private *dev_priv = guc_to_i915(guc);

	if (HAS_GUC_CT(dev_priv))
		intel_guc_disable_ct(guc);

	guc->send = intel_guc_send_nop;
}

int intel_uc_init_hw(struct drm_i915_private *dev_priv)
{
	struct intel_guc *guc = &dev_priv->guc;
	int ret, attempts;

	if (!i915.enable_guc_loading)
		return 0;

	guc_disable_communication(guc);
	gen9_reset_guc_interrupts(dev_priv);

	/* We need to notify the guc whenever we change the GGTT */
	i915_ggtt_enable_guc(dev_priv);

	if (i915.enable_guc_submission) {
		/*
		 * This is stuff we need to have available at fw load time
		 * if we are planning to enable submission later
		 */
		ret = i915_guc_submission_init(dev_priv);
		if (ret)
			goto err_guc;
	}

	/* init WOPCM */
	I915_WRITE(GUC_WOPCM_SIZE, intel_guc_wopcm_size(dev_priv));
	I915_WRITE(DMA_GUC_WOPCM_OFFSET,
		   GUC_WOPCM_OFFSET_VALUE | HUC_LOADING_AGENT_GUC);

	/* WaEnableuKernelHeaderValidFix:skl */
	/* WaEnableGuCBootHashCheckNotSet:skl,bxt,kbl */
	if (IS_GEN9(dev_priv))
		attempts = 3;
	else
		attempts = 1;

	while (attempts--) {
		/*
		 * Always reset the GuC just before (re)loading, so
		 * that the state and timing are fairly predictable
		 */
		ret = __intel_uc_reset_hw(dev_priv);
		if (ret)
			goto err_submission;

		intel_huc_init_hw(&dev_priv->huc);
		ret = intel_guc_init_hw(&dev_priv->guc);
		if (ret == 0 || ret != -EAGAIN)
			break;

		DRM_DEBUG_DRIVER("GuC fw load failed: %d; will reset and "
				 "retry %d more time(s)\n", ret, attempts);
	}

	/* Did we succeded or run out of retries? */
	if (ret)
		goto err_log_capture;

	ret = guc_enable_communication(guc);
	if (ret)
		goto err_log_capture;

	intel_guc_auth_huc(dev_priv);
	if (i915.enable_guc_submission) {
		if (i915.guc_log_level >= 0)
			gen9_enable_guc_interrupts(dev_priv);

		ret = i915_guc_submission_enable(dev_priv);
		if (ret)
			goto err_interrupts;
	}

	return 0;

	/*
	 * We've failed to load the firmware :(
	 *
	 * Decide whether to disable GuC submission and fall back to
	 * execlist mode, and whether to hide the error by returning
	 * zero or to return -EIO, which the caller will treat as a
	 * nonfatal error (i.e. it doesn't prevent driver load, but
	 * marks the GPU as wedged until reset).
	 */
err_interrupts:
	guc_disable_communication(guc);
	gen9_disable_guc_interrupts(dev_priv);
err_log_capture:
	guc_capture_load_err_log(guc);
err_submission:
	if (i915.enable_guc_submission)
		i915_guc_submission_fini(dev_priv);
err_guc:
	i915_ggtt_disable_guc(dev_priv);

	DRM_ERROR("GuC init failed\n");
	if (i915.enable_guc_loading > 1 || i915.enable_guc_submission > 1)
		ret = -EIO;
	else
		ret = 0;

	if (i915.enable_guc_submission) {
		i915.enable_guc_submission = 0;
		DRM_NOTE("Falling back from GuC submission to execlist mode\n");
	}

	return ret;
}

void intel_uc_fini_hw(struct drm_i915_private *dev_priv)
{
	if (!i915.enable_guc_loading)
		return;

	guc_free_load_err_log(&dev_priv->guc);

	if (i915.enable_guc_submission)
		i915_guc_submission_disable(dev_priv);

	guc_disable_communication(&dev_priv->guc);

	if (i915.enable_guc_submission) {
		gen9_disable_guc_interrupts(dev_priv);
		i915_guc_submission_fini(dev_priv);
	}

	i915_ggtt_disable_guc(dev_priv);
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

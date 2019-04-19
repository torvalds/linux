/*
 * Copyright © 2016-2017 Intel Corporation
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

#include <linux/types.h>

#include "intel_huc.h"
#include "i915_drv.h"

void intel_huc_init_early(struct intel_huc *huc)
{
	intel_huc_fw_init_early(huc);
}

int intel_huc_init_misc(struct intel_huc *huc)
{
	struct drm_i915_private *i915 = huc_to_i915(huc);

	intel_uc_fw_fetch(i915, &huc->fw);
	return 0;
}

static int intel_huc_rsa_data_create(struct intel_huc *huc)
{
	struct drm_i915_private *i915 = huc_to_i915(huc);
	struct intel_guc *guc = &i915->guc;
	struct i915_vma *vma;
	void *vaddr;

	/*
	 * HuC firmware will sit above GUC_GGTT_TOP and will not map
	 * through GTT. Unfortunately, this means GuC cannot perform
	 * the HuC auth. as the rsa offset now falls within the GuC
	 * inaccessible range. We resort to perma-pinning an additional
	 * vma within the accessible range that only contains the rsa
	 * signature. The GuC can use this extra pinning to perform
	 * the authentication since its GGTT offset will be GuC
	 * accessible.
	 */
	vma = intel_guc_allocate_vma(guc, PAGE_SIZE);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	vaddr = i915_gem_object_pin_map(vma->obj, I915_MAP_WB);
	if (IS_ERR(vaddr)) {
		i915_vma_unpin_and_release(&vma, 0);
		return PTR_ERR(vaddr);
	}

	huc->rsa_data = vma;
	huc->rsa_data_vaddr = vaddr;

	return 0;
}

static void intel_huc_rsa_data_destroy(struct intel_huc *huc)
{
	i915_vma_unpin_and_release(&huc->rsa_data, I915_VMA_RELEASE_MAP);
}

int intel_huc_init(struct intel_huc *huc)
{
	int err;

	err = intel_huc_rsa_data_create(huc);
	if (err)
		return err;

	return intel_uc_fw_init(&huc->fw);
}

void intel_huc_fini(struct intel_huc *huc)
{
	intel_uc_fw_fini(&huc->fw);
	intel_huc_rsa_data_destroy(huc);
}

/**
 * intel_huc_auth() - Authenticate HuC uCode
 * @huc: intel_huc structure
 *
 * Called after HuC and GuC firmware loading during intel_uc_init_hw().
 *
 * This function pins HuC firmware image object into GGTT.
 * Then it invokes GuC action to authenticate passing the offset to RSA
 * signature through intel_guc_auth_huc(). It then waits for 50ms for
 * firmware verification ACK and unpins the object.
 */
int intel_huc_auth(struct intel_huc *huc)
{
	struct drm_i915_private *i915 = huc_to_i915(huc);
	struct intel_guc *guc = &i915->guc;
	u32 status;
	int ret;

	if (huc->fw.load_status != INTEL_UC_FIRMWARE_SUCCESS)
		return -ENOEXEC;

	ret = intel_guc_auth_huc(guc,
				 intel_guc_ggtt_offset(guc, huc->rsa_data));
	if (ret) {
		DRM_ERROR("HuC: GuC did not ack Auth request %d\n", ret);
		goto fail;
	}

	/* Check authentication status, it should be done by now */
	ret = __intel_wait_for_register(&i915->uncore,
					HUC_STATUS2,
					HUC_FW_VERIFIED,
					HUC_FW_VERIFIED,
					2, 50, &status);
	if (ret) {
		DRM_ERROR("HuC: Firmware not verified %#x\n", status);
		goto fail;
	}

	return 0;

fail:
	huc->fw.load_status = INTEL_UC_FIRMWARE_FAIL;

	DRM_ERROR("HuC: Authentication failed %d\n", ret);
	return ret;
}

/**
 * intel_huc_check_status() - check HuC status
 * @huc: intel_huc structure
 *
 * This function reads status register to verify if HuC
 * firmware was successfully loaded.
 *
 * Returns: 1 if HuC firmware is loaded and verified,
 * 0 if HuC firmware is not loaded and -ENODEV if HuC
 * is not present on this platform.
 */
int intel_huc_check_status(struct intel_huc *huc)
{
	struct drm_i915_private *dev_priv = huc_to_i915(huc);
	intel_wakeref_t wakeref;
	bool status = false;

	if (!HAS_HUC(dev_priv))
		return -ENODEV;

	with_intel_runtime_pm(dev_priv, wakeref)
		status = I915_READ(HUC_STATUS2) & HUC_FW_VERIFIED;

	return status;
}

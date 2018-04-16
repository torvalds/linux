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
	struct i915_vma *vma;
	u32 status;
	int ret;

	if (huc->fw.load_status != INTEL_UC_FIRMWARE_SUCCESS)
		return -ENOEXEC;

	vma = i915_gem_object_ggtt_pin(huc->fw.obj, NULL, 0, 0,
				PIN_OFFSET_BIAS | GUC_WOPCM_TOP);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		DRM_ERROR("HuC: Failed to pin huc fw object %d\n", ret);
		goto fail;
	}

	ret = intel_guc_auth_huc(guc,
				 guc_ggtt_offset(vma) + huc->fw.rsa_offset);
	if (ret) {
		DRM_ERROR("HuC: GuC did not ack Auth request %d\n", ret);
		goto fail_unpin;
	}

	/* Check authentication status, it should be done by now */
	ret = __intel_wait_for_register(i915,
					HUC_STATUS2,
					HUC_FW_VERIFIED,
					HUC_FW_VERIFIED,
					2, 50, &status);
	if (ret) {
		DRM_ERROR("HuC: Firmware not verified %#x\n", status);
		goto fail_unpin;
	}

	i915_vma_unpin(vma);
	return 0;

fail_unpin:
	i915_vma_unpin(vma);
fail:
	huc->fw.load_status = INTEL_UC_FIRMWARE_FAIL;

	DRM_ERROR("HuC: Authentication failed %d\n", ret);
	return ret;
}

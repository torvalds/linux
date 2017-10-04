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

#include <linux/firmware.h>

#include "intel_uc_fw.h"
#include "i915_drv.h"

/**
 * intel_uc_fw_fetch - fetch uC firmware
 *
 * @dev_priv: device private
 * @uc_fw: uC firmware
 *
 * Fetch uC firmware into GEM obj.
 */
void intel_uc_fw_fetch(struct drm_i915_private *dev_priv,
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
			      css->key_size_dw - css->exponent_size_dw) *
			     sizeof(u32);

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

/**
 * intel_uc_fw_fini - cleanup uC firmware
 *
 * @uc_fw: uC firmware
 *
 * Cleans up uC firmware by releasing the firmware GEM obj.
 */
void intel_uc_fw_fini(struct intel_uc_fw *uc_fw)
{
	struct drm_i915_gem_object *obj;

	obj = fetch_and_zero(&uc_fw->obj);
	if (obj)
		i915_gem_object_put(obj);

	uc_fw->fetch_status = INTEL_UC_FIRMWARE_NONE;
}

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
#include <drm/drm_print.h>

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

	DRM_DEBUG_DRIVER("%s fw fetch %s\n",
			 intel_uc_fw_type_repr(uc_fw->type), uc_fw->path);

	if (!uc_fw->path)
		return;

	uc_fw->fetch_status = INTEL_UC_FIRMWARE_PENDING;
	DRM_DEBUG_DRIVER("%s fw fetch %s\n",
			 intel_uc_fw_type_repr(uc_fw->type),
			 intel_uc_fw_status_repr(uc_fw->fetch_status));

	err = request_firmware(&fw, uc_fw->path, &pdev->dev);
	if (err) {
		DRM_DEBUG_DRIVER("%s fw request_firmware err=%d\n",
				 intel_uc_fw_type_repr(uc_fw->type), err);
		goto fail;
	}

	DRM_DEBUG_DRIVER("%s fw size %zu ptr %p\n",
			 intel_uc_fw_type_repr(uc_fw->type), fw->size, fw);

	/* Check the size of the blob before examining buffer contents */
	if (fw->size < sizeof(struct uc_css_header)) {
		DRM_WARN("%s: Unexpected firmware size (%zu, min %zu)\n",
			 intel_uc_fw_type_repr(uc_fw->type),
			 fw->size, sizeof(struct uc_css_header));
		err = -ENODATA;
		goto fail;
	}

	css = (struct uc_css_header *)fw->data;

	/* Firmware bits always start from header */
	uc_fw->header_offset = 0;
	uc_fw->header_size = (css->header_size_dw - css->modulus_size_dw -
			      css->key_size_dw - css->exponent_size_dw) *
			     sizeof(u32);

	if (uc_fw->header_size != sizeof(struct uc_css_header)) {
		DRM_WARN("%s: Mismatched firmware header definition\n",
			 intel_uc_fw_type_repr(uc_fw->type));
		err = -ENOEXEC;
		goto fail;
	}

	/* then, uCode */
	uc_fw->ucode_offset = uc_fw->header_offset + uc_fw->header_size;
	uc_fw->ucode_size = (css->size_dw - css->header_size_dw) * sizeof(u32);

	/* now RSA */
	if (css->key_size_dw != UOS_RSA_SCRATCH_COUNT) {
		DRM_WARN("%s: Mismatched firmware RSA key size (%u)\n",
			 intel_uc_fw_type_repr(uc_fw->type), css->key_size_dw);
		err = -ENOEXEC;
		goto fail;
	}
	uc_fw->rsa_offset = uc_fw->ucode_offset + uc_fw->ucode_size;
	uc_fw->rsa_size = css->key_size_dw * sizeof(u32);

	/* At least, it should have header, uCode and RSA. Size of all three. */
	size = uc_fw->header_size + uc_fw->ucode_size + uc_fw->rsa_size;
	if (fw->size < size) {
		DRM_WARN("%s: Truncated firmware (%zu, expected %zu)\n",
			 intel_uc_fw_type_repr(uc_fw->type), fw->size, size);
		err = -ENOEXEC;
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
		uc_fw->major_ver_found = css->guc.sw_version >> 16;
		uc_fw->minor_ver_found = css->guc.sw_version & 0xFFFF;
		break;

	case INTEL_UC_FW_TYPE_HUC:
		uc_fw->major_ver_found = css->huc.sw_version >> 16;
		uc_fw->minor_ver_found = css->huc.sw_version & 0xFFFF;
		break;

	default:
		MISSING_CASE(uc_fw->type);
		break;
	}

	DRM_DEBUG_DRIVER("%s fw version %u.%u (wanted %u.%u)\n",
			 intel_uc_fw_type_repr(uc_fw->type),
			 uc_fw->major_ver_found, uc_fw->minor_ver_found,
			 uc_fw->major_ver_wanted, uc_fw->minor_ver_wanted);

	if (uc_fw->major_ver_wanted == 0 && uc_fw->minor_ver_wanted == 0) {
		DRM_NOTE("%s: Skipping firmware version check\n",
			 intel_uc_fw_type_repr(uc_fw->type));
	} else if (uc_fw->major_ver_found != uc_fw->major_ver_wanted ||
		   uc_fw->minor_ver_found < uc_fw->minor_ver_wanted) {
		DRM_NOTE("%s: Wrong firmware version (%u.%u, required %u.%u)\n",
			 intel_uc_fw_type_repr(uc_fw->type),
			 uc_fw->major_ver_found, uc_fw->minor_ver_found,
			 uc_fw->major_ver_wanted, uc_fw->minor_ver_wanted);
		err = -ENOEXEC;
		goto fail;
	}

	obj = i915_gem_object_create_from_data(dev_priv, fw->data, fw->size);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		DRM_DEBUG_DRIVER("%s fw object_create err=%d\n",
				 intel_uc_fw_type_repr(uc_fw->type), err);
		goto fail;
	}

	uc_fw->obj = obj;
	uc_fw->size = fw->size;
	uc_fw->fetch_status = INTEL_UC_FIRMWARE_SUCCESS;
	DRM_DEBUG_DRIVER("%s fw fetch %s\n",
			 intel_uc_fw_type_repr(uc_fw->type),
			 intel_uc_fw_status_repr(uc_fw->fetch_status));

	release_firmware(fw);
	return;

fail:
	uc_fw->fetch_status = INTEL_UC_FIRMWARE_FAIL;
	DRM_DEBUG_DRIVER("%s fw fetch %s\n",
			 intel_uc_fw_type_repr(uc_fw->type),
			 intel_uc_fw_status_repr(uc_fw->fetch_status));

	DRM_WARN("%s: Failed to fetch firmware %s (error %d)\n",
		 intel_uc_fw_type_repr(uc_fw->type), uc_fw->path, err);
	DRM_INFO("%s: Firmware can be downloaded from %s\n",
		 intel_uc_fw_type_repr(uc_fw->type), INTEL_UC_FIRMWARE_URL);

	release_firmware(fw);		/* OK even if fw is NULL */
}

/**
 * intel_uc_fw_upload - load uC firmware using custom loader
 * @uc_fw: uC firmware
 * @xfer: custom uC firmware loader function
 *
 * Loads uC firmware using custom loader and updates internal flags.
 *
 * Return: 0 on success, non-zero on failure.
 */
int intel_uc_fw_upload(struct intel_uc_fw *uc_fw,
		       int (*xfer)(struct intel_uc_fw *uc_fw,
				   struct i915_vma *vma))
{
	struct i915_vma *vma;
	u32 ggtt_pin_bias;
	int err;

	DRM_DEBUG_DRIVER("%s fw load %s\n",
			 intel_uc_fw_type_repr(uc_fw->type), uc_fw->path);

	if (uc_fw->fetch_status != INTEL_UC_FIRMWARE_SUCCESS)
		return -ENOEXEC;

	uc_fw->load_status = INTEL_UC_FIRMWARE_PENDING;
	DRM_DEBUG_DRIVER("%s fw load %s\n",
			 intel_uc_fw_type_repr(uc_fw->type),
			 intel_uc_fw_status_repr(uc_fw->load_status));

	/* Pin object with firmware */
	err = i915_gem_object_set_to_gtt_domain(uc_fw->obj, false);
	if (err) {
		DRM_DEBUG_DRIVER("%s fw set-domain err=%d\n",
				 intel_uc_fw_type_repr(uc_fw->type), err);
		goto fail;
	}

	ggtt_pin_bias = to_i915(uc_fw->obj->base.dev)->guc.ggtt_pin_bias;
	vma = i915_gem_object_ggtt_pin(uc_fw->obj, NULL, 0, 0,
				       PIN_OFFSET_BIAS | ggtt_pin_bias);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		DRM_DEBUG_DRIVER("%s fw ggtt-pin err=%d\n",
				 intel_uc_fw_type_repr(uc_fw->type), err);
		goto fail;
	}

	/* Call custom loader */
	err = xfer(uc_fw, vma);

	/*
	 * We keep the object pages for reuse during resume. But we can unpin it
	 * now that DMA has completed, so it doesn't continue to take up space.
	 */
	i915_vma_unpin(vma);

	if (err)
		goto fail;

	uc_fw->load_status = INTEL_UC_FIRMWARE_SUCCESS;
	DRM_DEBUG_DRIVER("%s fw load %s\n",
			 intel_uc_fw_type_repr(uc_fw->type),
			 intel_uc_fw_status_repr(uc_fw->load_status));

	DRM_INFO("%s: Loaded firmware %s (version %u.%u)\n",
		 intel_uc_fw_type_repr(uc_fw->type),
		 uc_fw->path,
		 uc_fw->major_ver_found, uc_fw->minor_ver_found);

	return 0;

fail:
	uc_fw->load_status = INTEL_UC_FIRMWARE_FAIL;
	DRM_DEBUG_DRIVER("%s fw load %s\n",
			 intel_uc_fw_type_repr(uc_fw->type),
			 intel_uc_fw_status_repr(uc_fw->load_status));

	DRM_WARN("%s: Failed to load firmware %s (error %d)\n",
		 intel_uc_fw_type_repr(uc_fw->type), uc_fw->path, err);

	return err;
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

/**
 * intel_uc_fw_dump - dump information about uC firmware
 * @uc_fw: uC firmware
 * @p: the &drm_printer
 *
 * Pretty printer for uC firmware.
 */
void intel_uc_fw_dump(const struct intel_uc_fw *uc_fw, struct drm_printer *p)
{
	drm_printf(p, "%s firmware: %s\n",
		   intel_uc_fw_type_repr(uc_fw->type), uc_fw->path);
	drm_printf(p, "\tstatus: fetch %s, load %s\n",
		   intel_uc_fw_status_repr(uc_fw->fetch_status),
		   intel_uc_fw_status_repr(uc_fw->load_status));
	drm_printf(p, "\tversion: wanted %u.%u, found %u.%u\n",
		   uc_fw->major_ver_wanted, uc_fw->minor_ver_wanted,
		   uc_fw->major_ver_found, uc_fw->minor_ver_found);
	drm_printf(p, "\theader: offset %u, size %u\n",
		   uc_fw->header_offset, uc_fw->header_size);
	drm_printf(p, "\tuCode: offset %u, size %u\n",
		   uc_fw->ucode_offset, uc_fw->ucode_size);
	drm_printf(p, "\tRSA: offset %u, size %u\n",
		   uc_fw->rsa_offset, uc_fw->rsa_size);
}

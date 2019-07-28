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

#include <linux/bitfield.h>
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

	if (!uc_fw->path) {
		dev_info(dev_priv->drm.dev,
			 "%s: No firmware was defined for %s!\n",
			 intel_uc_fw_type_repr(uc_fw->type),
			 intel_platform_name(INTEL_INFO(dev_priv)->platform));
		return;
	}

	DRM_DEBUG_DRIVER("%s fw fetch %s\n",
			 intel_uc_fw_type_repr(uc_fw->type), uc_fw->path);

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

	/* Get version numbers from the CSS header */
	switch (uc_fw->type) {
	case INTEL_UC_FW_TYPE_GUC:
		uc_fw->major_ver_found = FIELD_GET(CSS_SW_VERSION_GUC_MAJOR,
						   css->sw_version);
		uc_fw->minor_ver_found = FIELD_GET(CSS_SW_VERSION_GUC_MINOR,
						   css->sw_version);
		break;

	case INTEL_UC_FW_TYPE_HUC:
		uc_fw->major_ver_found = FIELD_GET(CSS_SW_VERSION_HUC_MAJOR,
						   css->sw_version);
		uc_fw->minor_ver_found = FIELD_GET(CSS_SW_VERSION_HUC_MINOR,
						   css->sw_version);
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

	obj = i915_gem_object_create_shmem_from_data(dev_priv,
						     fw->data, fw->size);
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

static void intel_uc_fw_ggtt_bind(struct intel_uc_fw *uc_fw)
{
	struct drm_i915_gem_object *obj = uc_fw->obj;
	struct i915_ggtt *ggtt = &to_i915(obj->base.dev)->ggtt;
	struct i915_vma dummy = {
		.node.start = intel_uc_fw_ggtt_offset(uc_fw),
		.node.size = obj->base.size,
		.pages = obj->mm.pages,
		.vm = &ggtt->vm,
	};

	GEM_BUG_ON(!i915_gem_object_has_pinned_pages(obj));
	GEM_BUG_ON(dummy.node.size > ggtt->uc_fw.size);

	/* uc_fw->obj cache domains were not controlled across suspend */
	drm_clflush_sg(dummy.pages);

	ggtt->vm.insert_entries(&ggtt->vm, &dummy, I915_CACHE_NONE, 0);
}

static void intel_uc_fw_ggtt_unbind(struct intel_uc_fw *uc_fw)
{
	struct drm_i915_gem_object *obj = uc_fw->obj;
	struct i915_ggtt *ggtt = &to_i915(obj->base.dev)->ggtt;
	u64 start = intel_uc_fw_ggtt_offset(uc_fw);

	ggtt->vm.clear_range(&ggtt->vm, start, obj->base.size);
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
		       int (*xfer)(struct intel_uc_fw *uc_fw))
{
	int err;

	DRM_DEBUG_DRIVER("%s fw load %s\n",
			 intel_uc_fw_type_repr(uc_fw->type), uc_fw->path);

	if (uc_fw->fetch_status != INTEL_UC_FIRMWARE_SUCCESS)
		return -ENOEXEC;

	uc_fw->load_status = INTEL_UC_FIRMWARE_PENDING;
	DRM_DEBUG_DRIVER("%s fw load %s\n",
			 intel_uc_fw_type_repr(uc_fw->type),
			 intel_uc_fw_status_repr(uc_fw->load_status));

	/* Call custom loader */
	intel_uc_fw_ggtt_bind(uc_fw);
	err = xfer(uc_fw);
	intel_uc_fw_ggtt_unbind(uc_fw);
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

int intel_uc_fw_init(struct intel_uc_fw *uc_fw)
{
	int err;

	if (uc_fw->fetch_status != INTEL_UC_FIRMWARE_SUCCESS)
		return -ENOEXEC;

	err = i915_gem_object_pin_pages(uc_fw->obj);
	if (err)
		DRM_DEBUG_DRIVER("%s fw pin-pages err=%d\n",
				 intel_uc_fw_type_repr(uc_fw->type), err);

	return err;
}

void intel_uc_fw_fini(struct intel_uc_fw *uc_fw)
{
	if (uc_fw->fetch_status != INTEL_UC_FIRMWARE_SUCCESS)
		return;

	i915_gem_object_unpin_pages(uc_fw->obj);
}

u32 intel_uc_fw_ggtt_offset(struct intel_uc_fw *uc_fw)
{
	struct drm_i915_private *i915 = to_i915(uc_fw->obj->base.dev);
	struct i915_ggtt *ggtt = &i915->ggtt;
	struct drm_mm_node *node = &ggtt->uc_fw;

	GEM_BUG_ON(!node->allocated);
	GEM_BUG_ON(upper_32_bits(node->start));
	GEM_BUG_ON(upper_32_bits(node->start + node->size - 1));

	return lower_32_bits(node->start);
}

/**
 * intel_uc_fw_cleanup_fetch - cleanup uC firmware
 *
 * @uc_fw: uC firmware
 *
 * Cleans up uC firmware by releasing the firmware GEM obj.
 */
void intel_uc_fw_cleanup_fetch(struct intel_uc_fw *uc_fw)
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

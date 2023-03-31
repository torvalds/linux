// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/firmware.h>

#include <drm/drm_managed.h>

#include "xe_bo.h"
#include "xe_device_types.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_guc_reg.h"
#include "xe_map.h"
#include "xe_mmio.h"
#include "xe_uc_fw.h"

static struct xe_gt *
__uc_fw_to_gt(struct xe_uc_fw *uc_fw, enum xe_uc_fw_type type)
{
	if (type == XE_UC_FW_TYPE_GUC)
		return container_of(uc_fw, struct xe_gt, uc.guc.fw);

	XE_BUG_ON(type != XE_UC_FW_TYPE_HUC);
	return container_of(uc_fw, struct xe_gt, uc.huc.fw);
}

static struct xe_gt *uc_fw_to_gt(struct xe_uc_fw *uc_fw)
{
	return __uc_fw_to_gt(uc_fw, uc_fw->type);
}

static struct xe_device *uc_fw_to_xe(struct xe_uc_fw *uc_fw)
{
	return gt_to_xe(uc_fw_to_gt(uc_fw));
}

/*
 * List of required GuC and HuC binaries per-platform.
 * Must be ordered based on platform, from newer to older.
 */
#define XE_GUC_FIRMWARE_DEFS(fw_def, guc_def) \
	fw_def(METEORLAKE,   guc_def(mtl,  70, 5, 2)) \
	fw_def(PVC,          guc_def(pvc,  70, 5, 2)) \
	fw_def(DG2,          guc_def(dg2,  70, 5, 2)) \
	fw_def(DG1,          guc_def(dg1,  70, 5, 2)) \
	fw_def(ALDERLAKE_P,  guc_def(adlp,  70, 5, 2)) \
	fw_def(ALDERLAKE_S,  guc_def(tgl,  70, 5, 2)) \
	fw_def(TIGERLAKE,    guc_def(tgl,  70, 5, 2))

#define XE_HUC_FIRMWARE_DEFS(fw_def, huc_def, huc_ver) \
	fw_def(ALDERLAKE_S,	huc_def(tgl)) \
	fw_def(DG1,		huc_def(dg1)) \
	fw_def(TIGERLAKE,	huc_def(tgl))

#define __MAKE_HUC_FW_PATH(prefix_, name_) \
        "i915/" \
        __stringify(prefix_) "_" name_ ".bin"

#define __MAKE_UC_FW_PATH_MAJOR(prefix_, name_, major_) \
	"i915/" \
	__stringify(prefix_) "_" name_ "_" \
	__stringify(major_) ".bin"

#define __MAKE_UC_FW_PATH_FULL_VER(prefix_, name_, major_, minor_, patch_) \
        "i915/" \
       __stringify(prefix_) "_" name_ "_" \
       __stringify(major_) "." \
       __stringify(minor_) "." \
       __stringify(patch_) ".bin"

#define MAKE_GUC_FW_PATH(prefix_, major_, minor_, patch_) \
	__MAKE_UC_FW_PATH_MAJOR(prefix_, "guc", major_)

#define MAKE_HUC_FW_PATH(prefix_) \
	__MAKE_HUC_FW_PATH(prefix_, "huc")

#define MAKE_HUC_FW_PATH_FULL_VER(prefix_, major_, minor_, patch_) \
	__MAKE_UC_FW_PATH_FULL_VER(prefix_, "huc", major_, minor_, patch_)


/* All blobs need to be declared via MODULE_FIRMWARE() */
#define XE_UC_MODULE_FW(platform_, uc_) \
	MODULE_FIRMWARE(uc_);

XE_GUC_FIRMWARE_DEFS(XE_UC_MODULE_FW, MAKE_GUC_FW_PATH)
XE_HUC_FIRMWARE_DEFS(XE_UC_MODULE_FW, MAKE_HUC_FW_PATH, MAKE_HUC_FW_PATH_FULL_VER)

/* The below structs and macros are used to iterate across the list of blobs */
struct __packed uc_fw_blob {
	u8 major;
	u8 minor;
	const char *path;
};

#define UC_FW_BLOB(major_, minor_, path_) \
	{ .major = major_, .minor = minor_, .path = path_ }

#define GUC_FW_BLOB(prefix_, major_, minor_, patch_) \
	UC_FW_BLOB(major_, minor_, \
		   MAKE_GUC_FW_PATH(prefix_, major_, minor_, patch_))

#define HUC_FW_BLOB(prefix_) \
	UC_FW_BLOB(0, 0, MAKE_HUC_FW_PATH(prefix_))

#define HUC_FW_VERSION_BLOB(prefix_, major_, minor_, bld_num_) \
	UC_FW_BLOB(major_, minor_, \
		   MAKE_HUC_FW_PATH_FULL_VER(prefix_, major_, minor_, bld_num_))

struct uc_fw_platform_requirement {
	enum xe_platform p;
	const struct uc_fw_blob blob;
};

#define MAKE_FW_LIST(platform_, uc_) \
{ \
	.p = XE_##platform_, \
	.blob = uc_, \
},

struct fw_blobs_by_type {
	const struct uc_fw_platform_requirement *blobs;
	u32 count;
};

static void
uc_fw_auto_select(struct xe_device *xe, struct xe_uc_fw *uc_fw)
{
	static const struct uc_fw_platform_requirement blobs_guc[] = {
		XE_GUC_FIRMWARE_DEFS(MAKE_FW_LIST, GUC_FW_BLOB)
	};
	static const struct uc_fw_platform_requirement blobs_huc[] = {
		XE_HUC_FIRMWARE_DEFS(MAKE_FW_LIST, HUC_FW_BLOB, HUC_FW_VERSION_BLOB)
	};
	static const struct fw_blobs_by_type blobs_all[XE_UC_FW_NUM_TYPES] = {
		[XE_UC_FW_TYPE_GUC] = { blobs_guc, ARRAY_SIZE(blobs_guc) },
		[XE_UC_FW_TYPE_HUC] = { blobs_huc, ARRAY_SIZE(blobs_huc) },
	};
	static const struct uc_fw_platform_requirement *fw_blobs;
	enum xe_platform p = xe->info.platform;
	u32 fw_count;
	int i;

	XE_BUG_ON(uc_fw->type >= ARRAY_SIZE(blobs_all));
	fw_blobs = blobs_all[uc_fw->type].blobs;
	fw_count = blobs_all[uc_fw->type].count;

	for (i = 0; i < fw_count && p <= fw_blobs[i].p; i++) {
		if (p == fw_blobs[i].p) {
			const struct uc_fw_blob *blob = &fw_blobs[i].blob;

			uc_fw->path = blob->path;
			uc_fw->major_ver_wanted = blob->major;
			uc_fw->minor_ver_wanted = blob->minor;
			break;
		}
	}
}

/**
 * xe_uc_fw_copy_rsa - copy fw RSA to buffer
 *
 * @uc_fw: uC firmware
 * @dst: dst buffer
 * @max_len: max number of bytes to copy
 *
 * Return: number of copied bytes.
 */
size_t xe_uc_fw_copy_rsa(struct xe_uc_fw *uc_fw, void *dst, u32 max_len)
{
	struct xe_device *xe = uc_fw_to_xe(uc_fw);
	u32 size = min_t(u32, uc_fw->rsa_size, max_len);

	XE_BUG_ON(size % 4);
	XE_BUG_ON(!xe_uc_fw_is_available(uc_fw));

	xe_map_memcpy_from(xe, dst, &uc_fw->bo->vmap,
			   xe_uc_fw_rsa_offset(uc_fw), size);

	return size;
}

static void uc_fw_fini(struct drm_device *drm, void *arg)
{
	struct xe_uc_fw *uc_fw = arg;

	if (!xe_uc_fw_is_available(uc_fw))
		return;

	xe_bo_unpin_map_no_vm(uc_fw->bo);
	xe_uc_fw_change_status(uc_fw, XE_UC_FIRMWARE_SELECTED);
}

static void guc_read_css_info(struct xe_uc_fw *uc_fw, struct uc_css_header *css)
{
	struct xe_gt *gt = uc_fw_to_gt(uc_fw);
	struct xe_guc *guc = &gt->uc.guc;

	XE_BUG_ON(uc_fw->type != XE_UC_FW_TYPE_GUC);
	XE_WARN_ON(uc_fw->major_ver_found < 70);

	if (uc_fw->major_ver_found > 70 || uc_fw->minor_ver_found >= 6) {
		/* v70.6.0 adds CSS header support */
		guc->submission_state.version.major =
			FIELD_GET(CSS_SW_VERSION_UC_MAJOR,
				  css->submission_version);
		guc->submission_state.version.minor =
			FIELD_GET(CSS_SW_VERSION_UC_MINOR,
				  css->submission_version);
		guc->submission_state.version.patch =
			FIELD_GET(CSS_SW_VERSION_UC_PATCH,
				  css->submission_version);
	} else if (uc_fw->minor_ver_found >= 3) {
		/* v70.3.0 introduced v1.1.0 */
		guc->submission_state.version.major = 1;
		guc->submission_state.version.minor = 1;
		guc->submission_state.version.patch = 0;
	} else {
		/* v70.0.0 introduced v1.0.0 */
		guc->submission_state.version.major = 1;
		guc->submission_state.version.minor = 0;
		guc->submission_state.version.patch = 0;
	}

	uc_fw->private_data_size = css->private_data_size;
}

int xe_uc_fw_init(struct xe_uc_fw *uc_fw)
{
	struct xe_device *xe = uc_fw_to_xe(uc_fw);
	struct xe_gt *gt = uc_fw_to_gt(uc_fw);
	struct device *dev = xe->drm.dev;
	const struct firmware *fw = NULL;
	struct uc_css_header *css;
	struct xe_bo *obj;
	size_t size;
	int err;

	/*
	 * we use FIRMWARE_UNINITIALIZED to detect checks against uc_fw->status
	 * before we're looked at the HW caps to see if we have uc support
	 */
	BUILD_BUG_ON(XE_UC_FIRMWARE_UNINITIALIZED);
	XE_BUG_ON(uc_fw->status);
	XE_BUG_ON(uc_fw->path);

	uc_fw_auto_select(xe, uc_fw);
	xe_uc_fw_change_status(uc_fw, uc_fw->path ? *uc_fw->path ?
			       XE_UC_FIRMWARE_SELECTED :
			       XE_UC_FIRMWARE_DISABLED :
			       XE_UC_FIRMWARE_NOT_SUPPORTED);

	/* Transform no huc in the list into firmware disabled */
	if (uc_fw->type == XE_UC_FW_TYPE_HUC && !xe_uc_fw_is_supported(uc_fw)) {
		xe_uc_fw_change_status(uc_fw, XE_UC_FIRMWARE_DISABLED);
		err = -ENOPKG;
		return err;
	}
	err = request_firmware(&fw, uc_fw->path, dev);
	if (err)
		goto fail;

	/* Check the size of the blob before examining buffer contents */
	if (unlikely(fw->size < sizeof(struct uc_css_header))) {
		drm_warn(&xe->drm, "%s firmware %s: invalid size: %zu < %zu\n",
			 xe_uc_fw_type_repr(uc_fw->type), uc_fw->path,
			 fw->size, sizeof(struct uc_css_header));
		err = -ENODATA;
		goto fail;
	}

	css = (struct uc_css_header *)fw->data;

	/* Check integrity of size values inside CSS header */
	size = (css->header_size_dw - css->key_size_dw - css->modulus_size_dw -
		css->exponent_size_dw) * sizeof(u32);
	if (unlikely(size != sizeof(struct uc_css_header))) {
		drm_warn(&xe->drm,
			 "%s firmware %s: unexpected header size: %zu != %zu\n",
			 xe_uc_fw_type_repr(uc_fw->type), uc_fw->path,
			 fw->size, sizeof(struct uc_css_header));
		err = -EPROTO;
		goto fail;
	}

	/* uCode size must calculated from other sizes */
	uc_fw->ucode_size = (css->size_dw - css->header_size_dw) * sizeof(u32);

	/* now RSA */
	uc_fw->rsa_size = css->key_size_dw * sizeof(u32);

	/* At least, it should have header, uCode and RSA. Size of all three. */
	size = sizeof(struct uc_css_header) + uc_fw->ucode_size +
		uc_fw->rsa_size;
	if (unlikely(fw->size < size)) {
		drm_warn(&xe->drm, "%s firmware %s: invalid size: %zu < %zu\n",
			 xe_uc_fw_type_repr(uc_fw->type), uc_fw->path,
			 fw->size, size);
		err = -ENOEXEC;
		goto fail;
	}

	/* Get version numbers from the CSS header */
	uc_fw->major_ver_found = FIELD_GET(CSS_SW_VERSION_UC_MAJOR,
					   css->sw_version);
	uc_fw->minor_ver_found = FIELD_GET(CSS_SW_VERSION_UC_MINOR,
					   css->sw_version);

	if (uc_fw->major_ver_wanted) {
		if (uc_fw->major_ver_found != uc_fw->major_ver_wanted ||
		    uc_fw->minor_ver_found < uc_fw->minor_ver_wanted) {
			drm_notice(&xe->drm, "%s firmware %s: unexpected version: %u.%u != %u.%u\n",
				   xe_uc_fw_type_repr(uc_fw->type), uc_fw->path,
				   uc_fw->major_ver_found, uc_fw->minor_ver_found,
				   uc_fw->major_ver_wanted, uc_fw->minor_ver_wanted);
			if (!xe_uc_fw_is_overridden(uc_fw)) {
				err = -ENOEXEC;
				goto fail;
			}
		}
	}

	if (uc_fw->type == XE_UC_FW_TYPE_GUC)
		guc_read_css_info(uc_fw, css);

	obj = xe_bo_create_from_data(xe, gt, fw->data, fw->size,
				     ttm_bo_type_kernel,
				     XE_BO_CREATE_VRAM_IF_DGFX(gt) |
				     XE_BO_CREATE_GGTT_BIT);
	if (IS_ERR(obj)) {
		drm_notice(&xe->drm, "%s firmware %s: failed to create / populate bo",
			   xe_uc_fw_type_repr(uc_fw->type), uc_fw->path);
		err = PTR_ERR(obj);
		goto fail;
	}

	uc_fw->bo = obj;
	uc_fw->size = fw->size;
	xe_uc_fw_change_status(uc_fw, XE_UC_FIRMWARE_AVAILABLE);

	release_firmware(fw);

	err = drmm_add_action_or_reset(&xe->drm, uc_fw_fini, uc_fw);
	if (err)
		return err;

	return 0;

fail:
	xe_uc_fw_change_status(uc_fw, err == -ENOENT ?
			       XE_UC_FIRMWARE_MISSING :
			       XE_UC_FIRMWARE_ERROR);

	drm_notice(&xe->drm, "%s firmware %s: fetch failed with error %d\n",
		   xe_uc_fw_type_repr(uc_fw->type), uc_fw->path, err);
	drm_info(&xe->drm, "%s firmware(s) can be downloaded from %s\n",
		 xe_uc_fw_type_repr(uc_fw->type), XE_UC_FIRMWARE_URL);

	release_firmware(fw);		/* OK even if fw is NULL */
	return err;
}

static u32 uc_fw_ggtt_offset(struct xe_uc_fw *uc_fw)
{
	return xe_bo_ggtt_addr(uc_fw->bo);
}

static int uc_fw_xfer(struct xe_uc_fw *uc_fw, u32 offset, u32 dma_flags)
{
	struct xe_device *xe = uc_fw_to_xe(uc_fw);
	struct xe_gt *gt = uc_fw_to_gt(uc_fw);
	u32 src_offset, dma_ctrl;
	int ret;

	xe_force_wake_assert_held(gt_to_fw(gt), XE_FW_GT);

	/* Set the source address for the uCode */
	src_offset = uc_fw_ggtt_offset(uc_fw);
	xe_mmio_write32(gt, DMA_ADDR_0_LOW.reg, lower_32_bits(src_offset));
	xe_mmio_write32(gt, DMA_ADDR_0_HIGH.reg, upper_32_bits(src_offset));

	/* Set the DMA destination */
	xe_mmio_write32(gt, DMA_ADDR_1_LOW.reg, offset);
	xe_mmio_write32(gt, DMA_ADDR_1_HIGH.reg, DMA_ADDRESS_SPACE_WOPCM);

	/*
	 * Set the transfer size. The header plus uCode will be copied to WOPCM
	 * via DMA, excluding any other components
	 */
	xe_mmio_write32(gt, DMA_COPY_SIZE.reg,
			sizeof(struct uc_css_header) + uc_fw->ucode_size);

	/* Start the DMA */
	xe_mmio_write32(gt, DMA_CTRL.reg,
			_MASKED_BIT_ENABLE(dma_flags | START_DMA));

	/* Wait for DMA to finish */
	ret = xe_mmio_wait32(gt, DMA_CTRL.reg, 0, START_DMA, 100000, &dma_ctrl,
			     false);
	if (ret)
		drm_err(&xe->drm, "DMA for %s fw failed, DMA_CTRL=%u\n",
			xe_uc_fw_type_repr(uc_fw->type), dma_ctrl);

	/* Disable the bits once DMA is over */
	xe_mmio_write32(gt, DMA_CTRL.reg, _MASKED_BIT_DISABLE(dma_flags));

	return ret;
}

int xe_uc_fw_upload(struct xe_uc_fw *uc_fw, u32 offset, u32 dma_flags)
{
	struct xe_device *xe = uc_fw_to_xe(uc_fw);
	int err;

	/* make sure the status was cleared the last time we reset the uc */
	XE_BUG_ON(xe_uc_fw_is_loaded(uc_fw));

	if (!xe_uc_fw_is_loadable(uc_fw))
		return -ENOEXEC;

	/* Call custom loader */
	err = uc_fw_xfer(uc_fw, offset, dma_flags);
	if (err)
		goto fail;

	xe_uc_fw_change_status(uc_fw, XE_UC_FIRMWARE_TRANSFERRED);
	return 0;

fail:
	drm_err(&xe->drm, "Failed to load %s firmware %s (%d)\n",
		xe_uc_fw_type_repr(uc_fw->type), uc_fw->path,
		err);
	xe_uc_fw_change_status(uc_fw, XE_UC_FIRMWARE_LOAD_FAIL);
	return err;
}


void xe_uc_fw_print(struct xe_uc_fw *uc_fw, struct drm_printer *p)
{
	drm_printf(p, "%s firmware: %s\n",
		   xe_uc_fw_type_repr(uc_fw->type), uc_fw->path);
	drm_printf(p, "\tstatus: %s\n",
		   xe_uc_fw_status_repr(uc_fw->status));
	drm_printf(p, "\tversion: wanted %u.%u, found %u.%u\n",
		   uc_fw->major_ver_wanted, uc_fw->minor_ver_wanted,
		   uc_fw->major_ver_found, uc_fw->minor_ver_found);
	drm_printf(p, "\tuCode: %u bytes\n", uc_fw->ucode_size);
	drm_printf(p, "\tRSA: %u bytes\n", uc_fw->rsa_size);

	if (uc_fw->type == XE_UC_FW_TYPE_GUC) {
		struct xe_gt *gt = uc_fw_to_gt(uc_fw);
		struct xe_guc *guc = &gt->uc.guc;

		drm_printf(p, "\tSubmit version: %u.%u.%u\n",
			   guc->submission_state.version.major,
			   guc->submission_state.version.minor,
			   guc->submission_state.version.patch);
	}
}

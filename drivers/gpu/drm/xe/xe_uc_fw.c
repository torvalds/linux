// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/firmware.h>

#include <drm/drm_managed.h>

#include "regs/xe_guc_regs.h"
#include "xe_bo.h"
#include "xe_device_types.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_map.h"
#include "xe_mmio.h"
#include "xe_uc_fw.h"

/*
 * List of required GuC and HuC binaries per-platform. They must be ordered
 * based on platform, from newer to older.
 *
 * Versioning follows the guidelines from
 * Documentation/driver-api/firmware/firmware-usage-guidelines.rst. There is a
 * distinction for platforms being officially supported by the driver or not.
 * Platforms not available publicly or not yet officially supported by the
 * driver (under force-probe), use the mmp_ver(): the firmware autoselect logic
 * will select the firmware from disk with filename that matches the full
 * "mpp version", i.e. major.minor.patch. mmp_ver() should only be used for
 * this case.
 *
 * For platforms officially supported by the driver, the filename always only
 * ever contains the major version (GuC) or no version at all (HuC).
 *
 * After loading the file, the driver parses the versions embedded in the blob.
 * The major version needs to match a major version supported by the driver (if
 * any). The minor version is also checked and a notice emitted to the log if
 * the version found is smaller than the version wanted. This is done only for
 * informational purposes so users may have a chance to upgrade, but the driver
 * still loads and use the older firmware.
 *
 * Examples:
 *
 *	1) Platform officially supported by i915 - using Tigerlake as example.
 *	   Driver loads the following firmware blobs from disk:
 *
 *		- i915/tgl_guc_<major>.bin
 *		- i915/tgl_huc.bin
 *
 *	   <major> number for GuC is checked that it matches the version inside
 *	   the blob. <minor> version is checked and if smaller than the expected
 *	   an info message is emitted about that.
 *
 *	1) XE_<FUTUREINTELPLATFORM>, still under require_force_probe. Using
 *	   "wipplat" as a short-name. Driver loads the following firmware blobs
 *	   from disk:
 *
 *		- xe/wipplat_guc_<major>.<minor>.<patch>.bin
 *		- xe/wipplat_huc_<major>.<minor>.<patch>.bin
 *
 *	   <major> and <minor> are checked that they match the version inside
 *	   the blob. Both of them need to match exactly what the driver is
 *	   expecting, otherwise it fails.
 *
 *	3) Platform officially supported by xe and out of force-probe. Using
 *	   "plat" as a short-name. Except for the different directory, the
 *	   behavior is the same as (1). Driver loads the following firmware
 *	   blobs from disk:
 *
 *		- xe/plat_guc_<major>.bin
 *		- xe/plat_huc.bin
 *
 *	   <major> number for GuC is checked that it matches the version inside
 *	   the blob. <minor> version is checked and if smaller than the expected
 *	   an info message is emitted about that.
 *
 * For the platforms already released with a major version, they should never be
 * removed from the table. Instead new entries with newer versions may be added
 * before them, so they take precedence.
 *
 * TODO: Currently there's no fallback on major version. That's because xe
 * driver only supports the one major version of each firmware in the table.
 * This needs to be fixed when the major version of GuC is updated.
 */

struct uc_fw_entry {
	enum xe_platform platform;
	struct {
		const char *path;
		u16 major;
		u16 minor;
		bool full_ver_required;
	};
};

struct fw_blobs_by_type {
	const struct uc_fw_entry *entries;
	u32 count;
};

#define XE_GUC_FIRMWARE_DEFS(fw_def, mmp_ver, major_ver)			\
	fw_def(METEORLAKE,	mmp_ver(i915,	guc,	mtl,	70, 6, 4))	\
	fw_def(PVC,		mmp_ver(xe,	guc,	pvc,	70, 6, 4))	\
	fw_def(DG2,		major_ver(i915,	guc,	dg2,	70, 5))		\
	fw_def(DG1,		major_ver(i915,	guc,	dg1,	70, 5))		\
	fw_def(ALDERLAKE_N,	major_ver(i915,	guc,	tgl,	70, 5))		\
	fw_def(ALDERLAKE_P,	major_ver(i915,	guc,	adlp,	70, 5))		\
	fw_def(ALDERLAKE_S,	major_ver(i915,	guc,	tgl,	70, 5))		\
	fw_def(ROCKETLAKE,	major_ver(i915,	guc,	tgl,	70, 5))		\
	fw_def(TIGERLAKE,	major_ver(i915,	guc,	tgl,	70, 5))

#define XE_HUC_FIRMWARE_DEFS(fw_def, mmp_ver, no_ver)				\
	fw_def(DG1,		no_ver(i915,	huc,	dg1))			\
	fw_def(ALDERLAKE_P,	no_ver(i915,	huc,	tgl))			\
	fw_def(ALDERLAKE_S,	no_ver(i915,	huc,	tgl))			\
	fw_def(ROCKETLAKE,	no_ver(i915,	huc,	tgl))			\
	fw_def(TIGERLAKE,	no_ver(i915,	huc,	tgl))

#define MAKE_FW_PATH(dir__, uc__, shortname__, version__)			\
	__stringify(dir__) "/" __stringify(shortname__) "_" __stringify(uc__) version__ ".bin"

#define fw_filename_mmp_ver(dir_, uc_, shortname_, a, b, c)			\
	MAKE_FW_PATH(dir_, uc_, shortname_, "_" __stringify(a ## . ## b ## . ## c))
#define fw_filename_major_ver(dir_, uc_, shortname_, a, b)			\
	MAKE_FW_PATH(dir_, uc_, shortname_, "_" __stringify(a))
#define fw_filename_no_ver(dir_, uc_, shortname_)				\
	MAKE_FW_PATH(dir_, uc_, shortname_, "")

#define uc_fw_entry_mmp_ver(dir_, uc_, shortname_, a, b, c)			\
	{ fw_filename_mmp_ver(dir_, uc_, shortname_, a, b, c),			\
	  a, b, true }
#define uc_fw_entry_major_ver(dir_, uc_, shortname_, a, b)			\
	{ fw_filename_major_ver(dir_, uc_, shortname_, a, b),			\
	  a, b }
#define uc_fw_entry_no_ver(dir_, uc_, shortname_)				\
	{ fw_filename_no_ver(dir_, uc_, shortname_),				\
	  0, 0 }

/* All blobs need to be declared via MODULE_FIRMWARE() */
#define XE_UC_MODULE_FIRMWARE(platform__, fw_filename)				\
	MODULE_FIRMWARE(fw_filename);

#define XE_UC_FW_ENTRY(platform__, entry__)					\
	{									\
		.platform = XE_ ## platform__,					\
		entry__,							\
	},

XE_GUC_FIRMWARE_DEFS(XE_UC_MODULE_FIRMWARE,
		     fw_filename_mmp_ver, fw_filename_major_ver)
XE_HUC_FIRMWARE_DEFS(XE_UC_MODULE_FIRMWARE,
		     fw_filename_mmp_ver, fw_filename_no_ver)

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

static void
uc_fw_auto_select(struct xe_device *xe, struct xe_uc_fw *uc_fw)
{
	static const struct uc_fw_entry entries_guc[] = {
		XE_GUC_FIRMWARE_DEFS(XE_UC_FW_ENTRY,
				     uc_fw_entry_mmp_ver,
				     uc_fw_entry_major_ver)
	};
	static const struct uc_fw_entry entries_huc[] = {
		XE_HUC_FIRMWARE_DEFS(XE_UC_FW_ENTRY,
				     uc_fw_entry_mmp_ver,
				     uc_fw_entry_no_ver)
	};
	static const struct fw_blobs_by_type blobs_all[XE_UC_FW_NUM_TYPES] = {
		[XE_UC_FW_TYPE_GUC] = { entries_guc, ARRAY_SIZE(entries_guc) },
		[XE_UC_FW_TYPE_HUC] = { entries_huc, ARRAY_SIZE(entries_huc) },
	};
	static const struct uc_fw_entry *entries;
	enum xe_platform p = xe->info.platform;
	u32 count;
	int i;

	XE_BUG_ON(uc_fw->type >= ARRAY_SIZE(blobs_all));
	entries = blobs_all[uc_fw->type].entries;
	count = blobs_all[uc_fw->type].count;

	for (i = 0; i < count && p <= entries[i].platform; i++) {
		if (p == entries[i].platform) {
			uc_fw->path = entries[i].path;
			uc_fw->major_ver_wanted = entries[i].major;
			uc_fw->minor_ver_wanted = entries[i].minor;
			uc_fw->full_ver_required = entries[i].full_ver_required;
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

static int uc_fw_check_version_requirements(struct xe_uc_fw *uc_fw)
{
	struct xe_device *xe = uc_fw_to_xe(uc_fw);

	/* Driver has no requirement on any version, any is good. */
	if (!uc_fw->major_ver_wanted)
		return 0;

	/*
	 * If full version is required, both major and minor should match.
	 * Otherwise, at least the major version.
	 */
	if (uc_fw->major_ver_wanted != uc_fw->major_ver_found ||
	    (uc_fw->full_ver_required &&
	     uc_fw->minor_ver_wanted != uc_fw->minor_ver_found)) {
		drm_notice(&xe->drm, "%s firmware %s: unexpected version: %u.%u != %u.%u\n",
			   xe_uc_fw_type_repr(uc_fw->type), uc_fw->path,
			   uc_fw->major_ver_found, uc_fw->minor_ver_found,
			   uc_fw->major_ver_wanted, uc_fw->minor_ver_wanted);
		goto fail;
	}

	if (uc_fw->minor_ver_wanted > uc_fw->minor_ver_found) {
		drm_notice(&xe->drm, "%s firmware (%u.%u) is recommended, but only (%u.%u) was found in %s\n",
			   xe_uc_fw_type_repr(uc_fw->type),
			   uc_fw->major_ver_wanted, uc_fw->minor_ver_wanted,
			   uc_fw->major_ver_found, uc_fw->minor_ver_found,
			   uc_fw->path);
		drm_info(&xe->drm, "Consider updating your linux-firmware pkg or downloading from %s\n",
			 XE_UC_FIRMWARE_URL);
	}

	return 0;

fail:
	if (xe_uc_fw_is_overridden(uc_fw))
		return 0;

	return -ENOEXEC;
}

int xe_uc_fw_init(struct xe_uc_fw *uc_fw)
{
	struct xe_device *xe = uc_fw_to_xe(uc_fw);
	struct xe_gt *gt = uc_fw_to_gt(uc_fw);
	struct xe_tile *tile = gt_to_tile(gt);
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

	drm_info(&xe->drm, "Using %s firmware (%u.%u) from %s\n",
		 xe_uc_fw_type_repr(uc_fw->type),
		 uc_fw->major_ver_found, uc_fw->minor_ver_found,
		 uc_fw->path);

	err = uc_fw_check_version_requirements(uc_fw);
	if (err)
		goto fail;

	if (uc_fw->type == XE_UC_FW_TYPE_GUC)
		guc_read_css_info(uc_fw, css);

	obj = xe_bo_create_from_data(xe, tile, fw->data, fw->size,
				     ttm_bo_type_kernel,
				     XE_BO_CREATE_VRAM_IF_DGFX(tile) |
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
	xe_mmio_write32(gt, DMA_ADDR_0_LOW, lower_32_bits(src_offset));
	xe_mmio_write32(gt, DMA_ADDR_0_HIGH, upper_32_bits(src_offset));

	/* Set the DMA destination */
	xe_mmio_write32(gt, DMA_ADDR_1_LOW, offset);
	xe_mmio_write32(gt, DMA_ADDR_1_HIGH, DMA_ADDRESS_SPACE_WOPCM);

	/*
	 * Set the transfer size. The header plus uCode will be copied to WOPCM
	 * via DMA, excluding any other components
	 */
	xe_mmio_write32(gt, DMA_COPY_SIZE,
			sizeof(struct uc_css_header) + uc_fw->ucode_size);

	/* Start the DMA */
	xe_mmio_write32(gt, DMA_CTRL,
			_MASKED_BIT_ENABLE(dma_flags | START_DMA));

	/* Wait for DMA to finish */
	ret = xe_mmio_wait32(gt, DMA_CTRL, 0, START_DMA, 100000, &dma_ctrl,
			     false);
	if (ret)
		drm_err(&xe->drm, "DMA for %s fw failed, DMA_CTRL=%u\n",
			xe_uc_fw_type_repr(uc_fw->type), dma_ctrl);

	/* Disable the bits once DMA is over */
	xe_mmio_write32(gt, DMA_CTRL, _MASKED_BIT_DISABLE(dma_flags));

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

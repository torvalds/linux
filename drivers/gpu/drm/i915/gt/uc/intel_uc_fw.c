// SPDX-License-Identifier: MIT
/*
 * Copyright © 2016-2019 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/firmware.h>
#include <drm/drm_print.h>

#include "intel_uc_fw.h"
#include "intel_uc_fw_abi.h"
#include "i915_drv.h"

static inline struct intel_gt *
____uc_fw_to_gt(struct intel_uc_fw *uc_fw, enum intel_uc_fw_type type)
{
	if (type == INTEL_UC_FW_TYPE_GUC)
		return container_of(uc_fw, struct intel_gt, uc.guc.fw);

	GEM_BUG_ON(type != INTEL_UC_FW_TYPE_HUC);
	return container_of(uc_fw, struct intel_gt, uc.huc.fw);
}

static inline struct intel_gt *__uc_fw_to_gt(struct intel_uc_fw *uc_fw)
{
	GEM_BUG_ON(uc_fw->status == INTEL_UC_FIRMWARE_UNINITIALIZED);
	return ____uc_fw_to_gt(uc_fw, uc_fw->type);
}

#ifdef CONFIG_DRM_I915_DEBUG_GUC
void intel_uc_fw_change_status(struct intel_uc_fw *uc_fw,
			       enum intel_uc_fw_status status)
{
	uc_fw->__status =  status;
	drm_dbg(&__uc_fw_to_gt(uc_fw)->i915->drm,
		"%s firmware -> %s\n",
		intel_uc_fw_type_repr(uc_fw->type),
		status == INTEL_UC_FIRMWARE_SELECTED ?
		uc_fw->path : intel_uc_fw_status_repr(status));
}
#endif

/*
 * List of required GuC and HuC binaries per-platform.
 * Must be ordered based on platform + revid, from newer to older.
 *
 * TGL 35.2 is interface-compatible with 33.0 for previous Gens. The deltas
 * between 33.0 and 35.2 are only related to new additions to support new Gen12
 * features.
 *
 * Note that RKL uses the same firmware as TGL.
 */
#define INTEL_UC_FIRMWARE_DEFS(fw_def, guc_def, huc_def) \
	fw_def(ROCKETLAKE,  0, guc_def(tgl, 35, 2, 0), huc_def(tgl,  7, 5, 0)) \
	fw_def(TIGERLAKE,   0, guc_def(tgl, 35, 2, 0), huc_def(tgl,  7, 5, 0)) \
	fw_def(ELKHARTLAKE, 0, guc_def(ehl, 33, 0, 4), huc_def(ehl,  9, 0, 0)) \
	fw_def(ICELAKE,     0, guc_def(icl, 33, 0, 0), huc_def(icl,  9, 0, 0)) \
	fw_def(COMETLAKE,   5, guc_def(cml, 33, 0, 0), huc_def(cml,  4, 0, 0)) \
	fw_def(COFFEELAKE,  0, guc_def(kbl, 33, 0, 0), huc_def(kbl,  4, 0, 0)) \
	fw_def(GEMINILAKE,  0, guc_def(glk, 33, 0, 0), huc_def(glk,  4, 0, 0)) \
	fw_def(KABYLAKE,    0, guc_def(kbl, 33, 0, 0), huc_def(kbl,  4, 0, 0)) \
	fw_def(BROXTON,     0, guc_def(bxt, 33, 0, 0), huc_def(bxt,  2, 0, 0)) \
	fw_def(SKYLAKE,     0, guc_def(skl, 33, 0, 0), huc_def(skl,  2, 0, 0))

#define __MAKE_UC_FW_PATH(prefix_, name_, major_, minor_, patch_) \
	"i915/" \
	__stringify(prefix_) name_ \
	__stringify(major_) "." \
	__stringify(minor_) "." \
	__stringify(patch_) ".bin"

#define MAKE_GUC_FW_PATH(prefix_, major_, minor_, patch_) \
	__MAKE_UC_FW_PATH(prefix_, "_guc_", major_, minor_, patch_)

#define MAKE_HUC_FW_PATH(prefix_, major_, minor_, bld_num_) \
	__MAKE_UC_FW_PATH(prefix_, "_huc_", major_, minor_, bld_num_)

/* All blobs need to be declared via MODULE_FIRMWARE() */
#define INTEL_UC_MODULE_FW(platform_, revid_, guc_, huc_) \
	MODULE_FIRMWARE(guc_); \
	MODULE_FIRMWARE(huc_);

INTEL_UC_FIRMWARE_DEFS(INTEL_UC_MODULE_FW, MAKE_GUC_FW_PATH, MAKE_HUC_FW_PATH)

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

#define HUC_FW_BLOB(prefix_, major_, minor_, bld_num_) \
	UC_FW_BLOB(major_, minor_, \
		   MAKE_HUC_FW_PATH(prefix_, major_, minor_, bld_num_))

struct __packed uc_fw_platform_requirement {
	enum intel_platform p;
	u8 rev; /* first platform rev using this FW */
	const struct uc_fw_blob blobs[INTEL_UC_FW_NUM_TYPES];
};

#define MAKE_FW_LIST(platform_, revid_, guc_, huc_) \
{ \
	.p = INTEL_##platform_, \
	.rev = revid_, \
	.blobs[INTEL_UC_FW_TYPE_GUC] = guc_, \
	.blobs[INTEL_UC_FW_TYPE_HUC] = huc_, \
},

static void
__uc_fw_auto_select(struct drm_i915_private *i915, struct intel_uc_fw *uc_fw)
{
	static const struct uc_fw_platform_requirement fw_blobs[] = {
		INTEL_UC_FIRMWARE_DEFS(MAKE_FW_LIST, GUC_FW_BLOB, HUC_FW_BLOB)
	};
	enum intel_platform p = INTEL_INFO(i915)->platform;
	u8 rev = INTEL_REVID(i915);
	int i;

	for (i = 0; i < ARRAY_SIZE(fw_blobs) && p <= fw_blobs[i].p; i++) {
		if (p == fw_blobs[i].p && rev >= fw_blobs[i].rev) {
			const struct uc_fw_blob *blob =
					&fw_blobs[i].blobs[uc_fw->type];
			uc_fw->path = blob->path;
			uc_fw->major_ver_wanted = blob->major;
			uc_fw->minor_ver_wanted = blob->minor;
			break;
		}
	}

	/* make sure the list is ordered as expected */
	if (IS_ENABLED(CONFIG_DRM_I915_SELFTEST)) {
		for (i = 1; i < ARRAY_SIZE(fw_blobs); i++) {
			if (fw_blobs[i].p < fw_blobs[i - 1].p)
				continue;

			if (fw_blobs[i].p == fw_blobs[i - 1].p &&
			    fw_blobs[i].rev < fw_blobs[i - 1].rev)
				continue;

			pr_err("invalid FW blob order: %s r%u comes before %s r%u\n",
			       intel_platform_name(fw_blobs[i - 1].p),
			       fw_blobs[i - 1].rev,
			       intel_platform_name(fw_blobs[i].p),
			       fw_blobs[i].rev);

			uc_fw->path = NULL;
		}
	}

	/* We don't want to enable GuC/HuC on pre-Gen11 by default */
	if (i915->params.enable_guc == -1 && p < INTEL_ICELAKE)
		uc_fw->path = NULL;
}

static const char *__override_guc_firmware_path(struct drm_i915_private *i915)
{
	if (i915->params.enable_guc & (ENABLE_GUC_SUBMISSION |
				       ENABLE_GUC_LOAD_HUC))
		return i915->params.guc_firmware_path;
	return "";
}

static const char *__override_huc_firmware_path(struct drm_i915_private *i915)
{
	if (i915->params.enable_guc & ENABLE_GUC_LOAD_HUC)
		return i915->params.huc_firmware_path;
	return "";
}

static void __uc_fw_user_override(struct drm_i915_private *i915, struct intel_uc_fw *uc_fw)
{
	const char *path = NULL;

	switch (uc_fw->type) {
	case INTEL_UC_FW_TYPE_GUC:
		path = __override_guc_firmware_path(i915);
		break;
	case INTEL_UC_FW_TYPE_HUC:
		path = __override_huc_firmware_path(i915);
		break;
	}

	if (unlikely(path)) {
		uc_fw->path = path;
		uc_fw->user_overridden = true;
	}
}

/**
 * intel_uc_fw_init_early - initialize the uC object and select the firmware
 * @uc_fw: uC firmware
 * @type: type of uC
 *
 * Initialize the state of our uC object and relevant tracking and select the
 * firmware to fetch and load.
 */
void intel_uc_fw_init_early(struct intel_uc_fw *uc_fw,
			    enum intel_uc_fw_type type)
{
	struct drm_i915_private *i915 = ____uc_fw_to_gt(uc_fw, type)->i915;

	/*
	 * we use FIRMWARE_UNINITIALIZED to detect checks against uc_fw->status
	 * before we're looked at the HW caps to see if we have uc support
	 */
	BUILD_BUG_ON(INTEL_UC_FIRMWARE_UNINITIALIZED);
	GEM_BUG_ON(uc_fw->status);
	GEM_BUG_ON(uc_fw->path);

	uc_fw->type = type;

	if (HAS_GT_UC(i915)) {
		__uc_fw_auto_select(i915, uc_fw);
		__uc_fw_user_override(i915, uc_fw);
	}

	intel_uc_fw_change_status(uc_fw, uc_fw->path ? *uc_fw->path ?
				  INTEL_UC_FIRMWARE_SELECTED :
				  INTEL_UC_FIRMWARE_DISABLED :
				  INTEL_UC_FIRMWARE_NOT_SUPPORTED);
}

static void __force_fw_fetch_failures(struct intel_uc_fw *uc_fw, int e)
{
	struct drm_i915_private *i915 = __uc_fw_to_gt(uc_fw)->i915;
	bool user = e == -EINVAL;

	if (i915_inject_probe_error(i915, e)) {
		/* non-existing blob */
		uc_fw->path = "<invalid>";
		uc_fw->user_overridden = user;
	} else if (i915_inject_probe_error(i915, e)) {
		/* require next major version */
		uc_fw->major_ver_wanted += 1;
		uc_fw->minor_ver_wanted = 0;
		uc_fw->user_overridden = user;
	} else if (i915_inject_probe_error(i915, e)) {
		/* require next minor version */
		uc_fw->minor_ver_wanted += 1;
		uc_fw->user_overridden = user;
	} else if (uc_fw->major_ver_wanted &&
		   i915_inject_probe_error(i915, e)) {
		/* require prev major version */
		uc_fw->major_ver_wanted -= 1;
		uc_fw->minor_ver_wanted = 0;
		uc_fw->user_overridden = user;
	} else if (uc_fw->minor_ver_wanted &&
		   i915_inject_probe_error(i915, e)) {
		/* require prev minor version - hey, this should work! */
		uc_fw->minor_ver_wanted -= 1;
		uc_fw->user_overridden = user;
	} else if (user && i915_inject_probe_error(i915, e)) {
		/* officially unsupported platform */
		uc_fw->major_ver_wanted = 0;
		uc_fw->minor_ver_wanted = 0;
		uc_fw->user_overridden = true;
	}
}

/**
 * intel_uc_fw_fetch - fetch uC firmware
 * @uc_fw: uC firmware
 *
 * Fetch uC firmware into GEM obj.
 *
 * Return: 0 on success, a negative errno code on failure.
 */
int intel_uc_fw_fetch(struct intel_uc_fw *uc_fw)
{
	struct drm_i915_private *i915 = __uc_fw_to_gt(uc_fw)->i915;
	struct device *dev = i915->drm.dev;
	struct drm_i915_gem_object *obj;
	const struct firmware *fw = NULL;
	struct uc_css_header *css;
	size_t size;
	int err;

	GEM_BUG_ON(!i915->wopcm.size);
	GEM_BUG_ON(!intel_uc_fw_is_enabled(uc_fw));

	err = i915_inject_probe_error(i915, -ENXIO);
	if (err)
		goto fail;

	__force_fw_fetch_failures(uc_fw, -EINVAL);
	__force_fw_fetch_failures(uc_fw, -ESTALE);

	err = request_firmware(&fw, uc_fw->path, dev);
	if (err)
		goto fail;

	/* Check the size of the blob before examining buffer contents */
	if (unlikely(fw->size < sizeof(struct uc_css_header))) {
		drm_warn(&i915->drm, "%s firmware %s: invalid size: %zu < %zu\n",
			 intel_uc_fw_type_repr(uc_fw->type), uc_fw->path,
			 fw->size, sizeof(struct uc_css_header));
		err = -ENODATA;
		goto fail;
	}

	css = (struct uc_css_header *)fw->data;

	/* Check integrity of size values inside CSS header */
	size = (css->header_size_dw - css->key_size_dw - css->modulus_size_dw -
		css->exponent_size_dw) * sizeof(u32);
	if (unlikely(size != sizeof(struct uc_css_header))) {
		drm_warn(&i915->drm,
			 "%s firmware %s: unexpected header size: %zu != %zu\n",
			 intel_uc_fw_type_repr(uc_fw->type), uc_fw->path,
			 fw->size, sizeof(struct uc_css_header));
		err = -EPROTO;
		goto fail;
	}

	/* uCode size must calculated from other sizes */
	uc_fw->ucode_size = (css->size_dw - css->header_size_dw) * sizeof(u32);

	/* now RSA */
	if (unlikely(css->key_size_dw != UOS_RSA_SCRATCH_COUNT)) {
		drm_warn(&i915->drm, "%s firmware %s: unexpected key size: %u != %u\n",
			 intel_uc_fw_type_repr(uc_fw->type), uc_fw->path,
			 css->key_size_dw, UOS_RSA_SCRATCH_COUNT);
		err = -EPROTO;
		goto fail;
	}
	uc_fw->rsa_size = css->key_size_dw * sizeof(u32);

	/* At least, it should have header, uCode and RSA. Size of all three. */
	size = sizeof(struct uc_css_header) + uc_fw->ucode_size + uc_fw->rsa_size;
	if (unlikely(fw->size < size)) {
		drm_warn(&i915->drm, "%s firmware %s: invalid size: %zu < %zu\n",
			 intel_uc_fw_type_repr(uc_fw->type), uc_fw->path,
			 fw->size, size);
		err = -ENOEXEC;
		goto fail;
	}

	/* Sanity check whether this fw is not larger than whole WOPCM memory */
	size = __intel_uc_fw_get_upload_size(uc_fw);
	if (unlikely(size >= i915->wopcm.size)) {
		drm_warn(&i915->drm, "%s firmware %s: invalid size: %zu > %zu\n",
			 intel_uc_fw_type_repr(uc_fw->type), uc_fw->path,
			 size, (size_t)i915->wopcm.size);
		err = -E2BIG;
		goto fail;
	}

	/* Get version numbers from the CSS header */
	uc_fw->major_ver_found = FIELD_GET(CSS_SW_VERSION_UC_MAJOR,
					   css->sw_version);
	uc_fw->minor_ver_found = FIELD_GET(CSS_SW_VERSION_UC_MINOR,
					   css->sw_version);

	if (uc_fw->major_ver_found != uc_fw->major_ver_wanted ||
	    uc_fw->minor_ver_found < uc_fw->minor_ver_wanted) {
		drm_notice(&i915->drm, "%s firmware %s: unexpected version: %u.%u != %u.%u\n",
			   intel_uc_fw_type_repr(uc_fw->type), uc_fw->path,
			   uc_fw->major_ver_found, uc_fw->minor_ver_found,
			   uc_fw->major_ver_wanted, uc_fw->minor_ver_wanted);
		if (!intel_uc_fw_is_overridden(uc_fw)) {
			err = -ENOEXEC;
			goto fail;
		}
	}

	obj = i915_gem_object_create_shmem_from_data(i915, fw->data, fw->size);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto fail;
	}

	uc_fw->obj = obj;
	uc_fw->size = fw->size;
	intel_uc_fw_change_status(uc_fw, INTEL_UC_FIRMWARE_AVAILABLE);

	release_firmware(fw);
	return 0;

fail:
	intel_uc_fw_change_status(uc_fw, err == -ENOENT ?
				  INTEL_UC_FIRMWARE_MISSING :
				  INTEL_UC_FIRMWARE_ERROR);

	drm_notice(&i915->drm, "%s firmware %s: fetch failed with error %d\n",
		   intel_uc_fw_type_repr(uc_fw->type), uc_fw->path, err);
	drm_info(&i915->drm, "%s firmware(s) can be downloaded from %s\n",
		 intel_uc_fw_type_repr(uc_fw->type), INTEL_UC_FIRMWARE_URL);

	release_firmware(fw);		/* OK even if fw is NULL */
	return err;
}

static u32 uc_fw_ggtt_offset(struct intel_uc_fw *uc_fw)
{
	struct i915_ggtt *ggtt = __uc_fw_to_gt(uc_fw)->ggtt;
	struct drm_mm_node *node = &ggtt->uc_fw;

	GEM_BUG_ON(!drm_mm_node_allocated(node));
	GEM_BUG_ON(upper_32_bits(node->start));
	GEM_BUG_ON(upper_32_bits(node->start + node->size - 1));

	return lower_32_bits(node->start);
}

static void uc_fw_bind_ggtt(struct intel_uc_fw *uc_fw)
{
	struct drm_i915_gem_object *obj = uc_fw->obj;
	struct i915_ggtt *ggtt = __uc_fw_to_gt(uc_fw)->ggtt;
	struct i915_vma dummy = {
		.node.start = uc_fw_ggtt_offset(uc_fw),
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

static void uc_fw_unbind_ggtt(struct intel_uc_fw *uc_fw)
{
	struct drm_i915_gem_object *obj = uc_fw->obj;
	struct i915_ggtt *ggtt = __uc_fw_to_gt(uc_fw)->ggtt;
	u64 start = uc_fw_ggtt_offset(uc_fw);

	ggtt->vm.clear_range(&ggtt->vm, start, obj->base.size);
}

static int uc_fw_xfer(struct intel_uc_fw *uc_fw, u32 dst_offset, u32 dma_flags)
{
	struct intel_gt *gt = __uc_fw_to_gt(uc_fw);
	struct intel_uncore *uncore = gt->uncore;
	u64 offset;
	int ret;

	ret = i915_inject_probe_error(gt->i915, -ETIMEDOUT);
	if (ret)
		return ret;

	intel_uncore_forcewake_get(uncore, FORCEWAKE_ALL);

	/* Set the source address for the uCode */
	offset = uc_fw_ggtt_offset(uc_fw);
	GEM_BUG_ON(upper_32_bits(offset) & 0xFFFF0000);
	intel_uncore_write_fw(uncore, DMA_ADDR_0_LOW, lower_32_bits(offset));
	intel_uncore_write_fw(uncore, DMA_ADDR_0_HIGH, upper_32_bits(offset));

	/* Set the DMA destination */
	intel_uncore_write_fw(uncore, DMA_ADDR_1_LOW, dst_offset);
	intel_uncore_write_fw(uncore, DMA_ADDR_1_HIGH, DMA_ADDRESS_SPACE_WOPCM);

	/*
	 * Set the transfer size. The header plus uCode will be copied to WOPCM
	 * via DMA, excluding any other components
	 */
	intel_uncore_write_fw(uncore, DMA_COPY_SIZE,
			      sizeof(struct uc_css_header) + uc_fw->ucode_size);

	/* Start the DMA */
	intel_uncore_write_fw(uncore, DMA_CTRL,
			      _MASKED_BIT_ENABLE(dma_flags | START_DMA));

	/* Wait for DMA to finish */
	ret = intel_wait_for_register_fw(uncore, DMA_CTRL, START_DMA, 0, 100);
	if (ret)
		drm_err(&gt->i915->drm, "DMA for %s fw failed, DMA_CTRL=%u\n",
			intel_uc_fw_type_repr(uc_fw->type),
			intel_uncore_read_fw(uncore, DMA_CTRL));

	/* Disable the bits once DMA is over */
	intel_uncore_write_fw(uncore, DMA_CTRL, _MASKED_BIT_DISABLE(dma_flags));

	intel_uncore_forcewake_put(uncore, FORCEWAKE_ALL);

	return ret;
}

/**
 * intel_uc_fw_upload - load uC firmware using custom loader
 * @uc_fw: uC firmware
 * @dst_offset: destination offset
 * @dma_flags: flags for flags for dma ctrl
 *
 * Loads uC firmware and updates internal flags.
 *
 * Return: 0 on success, non-zero on failure.
 */
int intel_uc_fw_upload(struct intel_uc_fw *uc_fw, u32 dst_offset, u32 dma_flags)
{
	struct intel_gt *gt = __uc_fw_to_gt(uc_fw);
	int err;

	/* make sure the status was cleared the last time we reset the uc */
	GEM_BUG_ON(intel_uc_fw_is_loaded(uc_fw));

	err = i915_inject_probe_error(gt->i915, -ENOEXEC);
	if (err)
		return err;

	if (!intel_uc_fw_is_loadable(uc_fw))
		return -ENOEXEC;

	/* Call custom loader */
	uc_fw_bind_ggtt(uc_fw);
	err = uc_fw_xfer(uc_fw, dst_offset, dma_flags);
	uc_fw_unbind_ggtt(uc_fw);
	if (err)
		goto fail;

	intel_uc_fw_change_status(uc_fw, INTEL_UC_FIRMWARE_TRANSFERRED);
	return 0;

fail:
	i915_probe_error(gt->i915, "Failed to load %s firmware %s (%d)\n",
			 intel_uc_fw_type_repr(uc_fw->type), uc_fw->path,
			 err);
	intel_uc_fw_change_status(uc_fw, INTEL_UC_FIRMWARE_FAIL);
	return err;
}

int intel_uc_fw_init(struct intel_uc_fw *uc_fw)
{
	int err;

	/* this should happen before the load! */
	GEM_BUG_ON(intel_uc_fw_is_loaded(uc_fw));

	if (!intel_uc_fw_is_available(uc_fw))
		return -ENOEXEC;

	err = i915_gem_object_pin_pages(uc_fw->obj);
	if (err) {
		DRM_DEBUG_DRIVER("%s fw pin-pages err=%d\n",
				 intel_uc_fw_type_repr(uc_fw->type), err);
		intel_uc_fw_change_status(uc_fw, INTEL_UC_FIRMWARE_FAIL);
	}

	return err;
}

void intel_uc_fw_fini(struct intel_uc_fw *uc_fw)
{
	if (i915_gem_object_has_pinned_pages(uc_fw->obj))
		i915_gem_object_unpin_pages(uc_fw->obj);

	intel_uc_fw_change_status(uc_fw, INTEL_UC_FIRMWARE_AVAILABLE);
}

/**
 * intel_uc_fw_cleanup_fetch - cleanup uC firmware
 * @uc_fw: uC firmware
 *
 * Cleans up uC firmware by releasing the firmware GEM obj.
 */
void intel_uc_fw_cleanup_fetch(struct intel_uc_fw *uc_fw)
{
	if (!intel_uc_fw_is_available(uc_fw))
		return;

	i915_gem_object_put(fetch_and_zero(&uc_fw->obj));

	intel_uc_fw_change_status(uc_fw, INTEL_UC_FIRMWARE_SELECTED);
}

/**
 * intel_uc_fw_copy_rsa - copy fw RSA to buffer
 *
 * @uc_fw: uC firmware
 * @dst: dst buffer
 * @max_len: max number of bytes to copy
 *
 * Return: number of copied bytes.
 */
size_t intel_uc_fw_copy_rsa(struct intel_uc_fw *uc_fw, void *dst, u32 max_len)
{
	struct sg_table *pages = uc_fw->obj->mm.pages;
	u32 size = min_t(u32, uc_fw->rsa_size, max_len);
	u32 offset = sizeof(struct uc_css_header) + uc_fw->ucode_size;

	GEM_BUG_ON(!intel_uc_fw_is_available(uc_fw));

	return sg_pcopy_to_buffer(pages->sgl, pages->nents, dst, size, offset);
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
	drm_printf(p, "\tstatus: %s\n",
		   intel_uc_fw_status_repr(uc_fw->status));
	drm_printf(p, "\tversion: wanted %u.%u, found %u.%u\n",
		   uc_fw->major_ver_wanted, uc_fw->minor_ver_wanted,
		   uc_fw->major_ver_found, uc_fw->minor_ver_found);
	drm_printf(p, "\tuCode: %u bytes\n", uc_fw->ucode_size);
	drm_printf(p, "\tRSA: %u bytes\n", uc_fw->rsa_size);
}

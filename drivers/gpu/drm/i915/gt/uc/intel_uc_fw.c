// SPDX-License-Identifier: MIT
/*
 * Copyright © 2016-2019 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/firmware.h>
#include <linux/highmem.h>

#include <drm/drm_cache.h>
#include <drm/drm_print.h>

#include "gem/i915_gem_lmem.h"
#include "intel_uc_fw.h"
#include "intel_uc_fw_abi.h"
#include "i915_drv.h"
#include "i915_reg.h"

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
		uc_fw->file_selected.path : intel_uc_fw_status_repr(status));
}
#endif

/*
 * List of required GuC and HuC binaries per-platform.
 * Must be ordered based on platform + revid, from newer to older.
 *
 * Note that RKL and ADL-S have the same GuC/HuC device ID's and use the same
 * firmware as TGL.
 *
 * Version numbers:
 * Originally, the driver required an exact match major/minor/patch furmware
 * file and only supported that one version for any given platform. However,
 * the new direction from upstream is to be backwards compatible with all
 * prior releases and to be as flexible as possible as to what firmware is
 * loaded.
 *
 * For GuC, the major version number signifies a backwards breaking API change.
 * So, new format GuC firmware files are labelled by their major version only.
 * For HuC, there is no KMD interaction, hence no version matching requirement.
 * So, new format HuC firmware files have no version number at all.
 *
 * All of which means that the table below must keep all old format files with
 * full three point version number. But newer files have reduced requirements.
 * Having said that, the driver still needs to track the minor version number
 * for GuC at least. As it is useful to report to the user that they are not
 * running with a recent enough version for all KMD supported features,
 * security fixes, etc. to be enabled.
 */
#define INTEL_GUC_FIRMWARE_DEFS(fw_def, guc_maj, guc_mmp) \
	fw_def(DG2,          0, guc_maj(dg2,  70, 5)) \
	fw_def(ALDERLAKE_P,  0, guc_maj(adlp, 70, 5)) \
	fw_def(ALDERLAKE_P,  0, guc_mmp(adlp, 70, 1, 1)) \
	fw_def(ALDERLAKE_P,  0, guc_mmp(adlp, 69, 0, 3)) \
	fw_def(ALDERLAKE_S,  0, guc_maj(tgl,  70, 5)) \
	fw_def(ALDERLAKE_S,  0, guc_mmp(tgl,  70, 1, 1)) \
	fw_def(ALDERLAKE_S,  0, guc_mmp(tgl,  69, 0, 3)) \
	fw_def(DG1,          0, guc_maj(dg1,  70, 5)) \
	fw_def(ROCKETLAKE,   0, guc_mmp(tgl,  70, 1, 1)) \
	fw_def(TIGERLAKE,    0, guc_mmp(tgl,  70, 1, 1)) \
	fw_def(JASPERLAKE,   0, guc_mmp(ehl,  70, 1, 1)) \
	fw_def(ELKHARTLAKE,  0, guc_mmp(ehl,  70, 1, 1)) \
	fw_def(ICELAKE,      0, guc_mmp(icl,  70, 1, 1)) \
	fw_def(COMETLAKE,    5, guc_mmp(cml,  70, 1, 1)) \
	fw_def(COMETLAKE,    0, guc_mmp(kbl,  70, 1, 1)) \
	fw_def(COFFEELAKE,   0, guc_mmp(kbl,  70, 1, 1)) \
	fw_def(GEMINILAKE,   0, guc_mmp(glk,  70, 1, 1)) \
	fw_def(KABYLAKE,     0, guc_mmp(kbl,  70, 1, 1)) \
	fw_def(BROXTON,      0, guc_mmp(bxt,  70, 1, 1)) \
	fw_def(SKYLAKE,      0, guc_mmp(skl,  70, 1, 1))

#define INTEL_HUC_FIRMWARE_DEFS(fw_def, huc_raw, huc_mmp) \
	fw_def(ALDERLAKE_P,  0, huc_raw(tgl)) \
	fw_def(ALDERLAKE_P,  0, huc_mmp(tgl,  7, 9, 3)) \
	fw_def(ALDERLAKE_S,  0, huc_raw(tgl)) \
	fw_def(ALDERLAKE_S,  0, huc_mmp(tgl,  7, 9, 3)) \
	fw_def(DG1,          0, huc_raw(dg1)) \
	fw_def(ROCKETLAKE,   0, huc_mmp(tgl,  7, 9, 3)) \
	fw_def(TIGERLAKE,    0, huc_mmp(tgl,  7, 9, 3)) \
	fw_def(JASPERLAKE,   0, huc_mmp(ehl,  9, 0, 0)) \
	fw_def(ELKHARTLAKE,  0, huc_mmp(ehl,  9, 0, 0)) \
	fw_def(ICELAKE,      0, huc_mmp(icl,  9, 0, 0)) \
	fw_def(COMETLAKE,    5, huc_mmp(cml,  4, 0, 0)) \
	fw_def(COMETLAKE,    0, huc_mmp(kbl,  4, 0, 0)) \
	fw_def(COFFEELAKE,   0, huc_mmp(kbl,  4, 0, 0)) \
	fw_def(GEMINILAKE,   0, huc_mmp(glk,  4, 0, 0)) \
	fw_def(KABYLAKE,     0, huc_mmp(kbl,  4, 0, 0)) \
	fw_def(BROXTON,      0, huc_mmp(bxt,  2, 0, 0)) \
	fw_def(SKYLAKE,      0, huc_mmp(skl,  2, 0, 0))

/*
 * Set of macros for producing a list of filenames from the above table.
 */
#define __MAKE_UC_FW_PATH_BLANK(prefix_, name_) \
	"i915/" \
	__stringify(prefix_) name_ ".bin"

#define __MAKE_UC_FW_PATH_MAJOR(prefix_, name_, major_) \
	"i915/" \
	__stringify(prefix_) name_ \
	__stringify(major_) ".bin"

#define __MAKE_UC_FW_PATH_MMP(prefix_, name_, major_, minor_, patch_) \
	"i915/" \
	__stringify(prefix_) name_ \
	__stringify(major_) "." \
	__stringify(minor_) "." \
	__stringify(patch_) ".bin"

/* Minor for internal driver use, not part of file name */
#define MAKE_GUC_FW_PATH_MAJOR(prefix_, major_, minor_) \
	__MAKE_UC_FW_PATH_MAJOR(prefix_, "_guc_", major_)

#define MAKE_GUC_FW_PATH_MMP(prefix_, major_, minor_, patch_) \
	__MAKE_UC_FW_PATH_MMP(prefix_, "_guc_", major_, minor_, patch_)

#define MAKE_HUC_FW_PATH_BLANK(prefix_) \
	__MAKE_UC_FW_PATH_BLANK(prefix_, "_huc")

#define MAKE_HUC_FW_PATH_MMP(prefix_, major_, minor_, patch_) \
	__MAKE_UC_FW_PATH_MMP(prefix_, "_huc_", major_, minor_, patch_)

/*
 * All blobs need to be declared via MODULE_FIRMWARE().
 * This first expansion of the table macros is solely to provide
 * that declaration.
 */
#define INTEL_UC_MODULE_FW(platform_, revid_, uc_) \
	MODULE_FIRMWARE(uc_);

INTEL_GUC_FIRMWARE_DEFS(INTEL_UC_MODULE_FW, MAKE_GUC_FW_PATH_MAJOR, MAKE_GUC_FW_PATH_MMP)
INTEL_HUC_FIRMWARE_DEFS(INTEL_UC_MODULE_FW, MAKE_HUC_FW_PATH_BLANK, MAKE_HUC_FW_PATH_MMP)

/*
 * The next expansion of the table macros (in __uc_fw_auto_select below) provides
 * actual data structures with both the filename and the version information.
 * These structure arrays are then iterated over to the list of suitable files
 * for the current platform and to then attempt to load those files, in the order
 * listed, until one is successfully found.
 */
struct __packed uc_fw_blob {
	const char *path;
	bool legacy;
	u8 major;
	u8 minor;
	u8 patch;
};

#define UC_FW_BLOB_BASE(major_, minor_, patch_, path_) \
	.major = major_, \
	.minor = minor_, \
	.patch = patch_, \
	.path = path_,

#define UC_FW_BLOB_NEW(major_, minor_, patch_, path_) \
	{ UC_FW_BLOB_BASE(major_, minor_, patch_, path_) \
	  .legacy = false }

#define UC_FW_BLOB_OLD(major_, minor_, patch_, path_) \
	{ UC_FW_BLOB_BASE(major_, minor_, patch_, path_) \
	  .legacy = true }

#define GUC_FW_BLOB(prefix_, major_, minor_) \
	UC_FW_BLOB_NEW(major_, minor_, 0, \
		       MAKE_GUC_FW_PATH_MAJOR(prefix_, major_, minor_))

#define GUC_FW_BLOB_MMP(prefix_, major_, minor_, patch_) \
	UC_FW_BLOB_OLD(major_, minor_, patch_, \
		       MAKE_GUC_FW_PATH_MMP(prefix_, major_, minor_, patch_))

#define HUC_FW_BLOB(prefix_) \
	UC_FW_BLOB_NEW(0, 0, 0, MAKE_HUC_FW_PATH_BLANK(prefix_))

#define HUC_FW_BLOB_MMP(prefix_, major_, minor_, patch_) \
	UC_FW_BLOB_OLD(major_, minor_, patch_, \
		       MAKE_HUC_FW_PATH_MMP(prefix_, major_, minor_, patch_))

struct __packed uc_fw_platform_requirement {
	enum intel_platform p;
	u8 rev; /* first platform rev using this FW */
	const struct uc_fw_blob blob;
};

#define MAKE_FW_LIST(platform_, revid_, uc_) \
{ \
	.p = INTEL_##platform_, \
	.rev = revid_, \
	.blob = uc_, \
},

struct fw_blobs_by_type {
	const struct uc_fw_platform_requirement *blobs;
	u32 count;
};

static void
__uc_fw_auto_select(struct drm_i915_private *i915, struct intel_uc_fw *uc_fw)
{
	static const struct uc_fw_platform_requirement blobs_guc[] = {
		INTEL_GUC_FIRMWARE_DEFS(MAKE_FW_LIST, GUC_FW_BLOB, GUC_FW_BLOB_MMP)
	};
	static const struct uc_fw_platform_requirement blobs_huc[] = {
		INTEL_HUC_FIRMWARE_DEFS(MAKE_FW_LIST, HUC_FW_BLOB, HUC_FW_BLOB_MMP)
	};
	static const struct fw_blobs_by_type blobs_all[INTEL_UC_FW_NUM_TYPES] = {
		[INTEL_UC_FW_TYPE_GUC] = { blobs_guc, ARRAY_SIZE(blobs_guc) },
		[INTEL_UC_FW_TYPE_HUC] = { blobs_huc, ARRAY_SIZE(blobs_huc) },
	};
	static bool verified;
	const struct uc_fw_platform_requirement *fw_blobs;
	enum intel_platform p = INTEL_INFO(i915)->platform;
	u32 fw_count;
	u8 rev = INTEL_REVID(i915);
	int i;
	bool found;

	/*
	 * The only difference between the ADL GuC FWs is the HWConfig support.
	 * ADL-N does not support HWConfig, so we should use the same binary as
	 * ADL-S, otherwise the GuC might attempt to fetch a config table that
	 * does not exist.
	 */
	if (IS_ADLP_N(i915))
		p = INTEL_ALDERLAKE_S;

	GEM_BUG_ON(uc_fw->type >= ARRAY_SIZE(blobs_all));
	fw_blobs = blobs_all[uc_fw->type].blobs;
	fw_count = blobs_all[uc_fw->type].count;

	found = false;
	for (i = 0; i < fw_count && p <= fw_blobs[i].p; i++) {
		const struct uc_fw_blob *blob = &fw_blobs[i].blob;

		if (p != fw_blobs[i].p)
			continue;

		if (rev < fw_blobs[i].rev)
			continue;

		if (uc_fw->file_selected.path) {
			if (uc_fw->file_selected.path == blob->path)
				uc_fw->file_selected.path = NULL;

			continue;
		}

		uc_fw->file_selected.path = blob->path;
		uc_fw->file_wanted.path = blob->path;
		uc_fw->file_wanted.major_ver = blob->major;
		uc_fw->file_wanted.minor_ver = blob->minor;
		found = true;
		break;
	}

	if (!found && uc_fw->file_selected.path) {
		/* Failed to find a match for the last attempt?! */
		uc_fw->file_selected.path = NULL;
	}

	/* make sure the list is ordered as expected */
	if (IS_ENABLED(CONFIG_DRM_I915_SELFTEST) && !verified) {
		verified = true;

		for (i = 1; i < fw_count; i++) {
			/* Next platform is good: */
			if (fw_blobs[i].p < fw_blobs[i - 1].p)
				continue;

			/* Next platform revision is good: */
			if (fw_blobs[i].p == fw_blobs[i - 1].p &&
			    fw_blobs[i].rev < fw_blobs[i - 1].rev)
				continue;

			/* Platform/revision must be in order: */
			if (fw_blobs[i].p != fw_blobs[i - 1].p ||
			    fw_blobs[i].rev != fw_blobs[i - 1].rev)
				goto bad;

			/* Next major version is good: */
			if (fw_blobs[i].blob.major < fw_blobs[i - 1].blob.major)
				continue;

			/* New must be before legacy: */
			if (!fw_blobs[i].blob.legacy && fw_blobs[i - 1].blob.legacy)
				goto bad;

			/* New to legacy also means 0.0 to X.Y (HuC), or X.0 to X.Y (GuC) */
			if (fw_blobs[i].blob.legacy && !fw_blobs[i - 1].blob.legacy) {
				if (!fw_blobs[i - 1].blob.major)
					continue;

				if (fw_blobs[i].blob.major == fw_blobs[i - 1].blob.major)
					continue;
			}

			/* Major versions must be in order: */
			if (fw_blobs[i].blob.major != fw_blobs[i - 1].blob.major)
				goto bad;

			/* Next minor version is good: */
			if (fw_blobs[i].blob.minor < fw_blobs[i - 1].blob.minor)
				continue;

			/* Minor versions must be in order: */
			if (fw_blobs[i].blob.minor != fw_blobs[i - 1].blob.minor)
				goto bad;

			/* Patch versions must be in order: */
			if (fw_blobs[i].blob.patch <= fw_blobs[i - 1].blob.patch)
				continue;

bad:
			drm_err(&i915->drm, "Invalid FW blob order: %s r%u %s%d.%d.%d comes before %s r%u %s%d.%d.%d\n",
				intel_platform_name(fw_blobs[i - 1].p), fw_blobs[i - 1].rev,
				fw_blobs[i - 1].blob.legacy ? "L" : "v",
				fw_blobs[i - 1].blob.major,
				fw_blobs[i - 1].blob.minor,
				fw_blobs[i - 1].blob.patch,
				intel_platform_name(fw_blobs[i].p), fw_blobs[i].rev,
				fw_blobs[i].blob.legacy ? "L" : "v",
				fw_blobs[i].blob.major,
				fw_blobs[i].blob.minor,
				fw_blobs[i].blob.patch);

			uc_fw->file_selected.path = NULL;
		}
	}
}

static const char *__override_guc_firmware_path(struct drm_i915_private *i915)
{
	if (i915->params.enable_guc & ENABLE_GUC_MASK)
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
		uc_fw->file_selected.path = path;
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
	GEM_BUG_ON(uc_fw->file_selected.path);

	uc_fw->type = type;

	if (HAS_GT_UC(i915)) {
		__uc_fw_auto_select(i915, uc_fw);
		__uc_fw_user_override(i915, uc_fw);
	}

	intel_uc_fw_change_status(uc_fw, uc_fw->file_selected.path ? *uc_fw->file_selected.path ?
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
		uc_fw->file_selected.path = "<invalid>";
		uc_fw->user_overridden = user;
	} else if (i915_inject_probe_error(i915, e)) {
		/* require next major version */
		uc_fw->file_wanted.major_ver += 1;
		uc_fw->file_wanted.minor_ver = 0;
		uc_fw->user_overridden = user;
	} else if (i915_inject_probe_error(i915, e)) {
		/* require next minor version */
		uc_fw->file_wanted.minor_ver += 1;
		uc_fw->user_overridden = user;
	} else if (uc_fw->file_wanted.major_ver &&
		   i915_inject_probe_error(i915, e)) {
		/* require prev major version */
		uc_fw->file_wanted.major_ver -= 1;
		uc_fw->file_wanted.minor_ver = 0;
		uc_fw->user_overridden = user;
	} else if (uc_fw->file_wanted.minor_ver &&
		   i915_inject_probe_error(i915, e)) {
		/* require prev minor version - hey, this should work! */
		uc_fw->file_wanted.minor_ver -= 1;
		uc_fw->user_overridden = user;
	} else if (user && i915_inject_probe_error(i915, e)) {
		/* officially unsupported platform */
		uc_fw->file_wanted.major_ver = 0;
		uc_fw->file_wanted.minor_ver = 0;
		uc_fw->user_overridden = true;
	}
}

static int check_gsc_manifest(const struct firmware *fw,
			      struct intel_uc_fw *uc_fw)
{
	u32 *dw = (u32 *)fw->data;
	u32 version_hi = dw[HUC_GSC_VERSION_HI_DW];
	u32 version_lo = dw[HUC_GSC_VERSION_LO_DW];

	uc_fw->file_selected.major_ver = FIELD_GET(HUC_GSC_MAJOR_VER_HI_MASK, version_hi);
	uc_fw->file_selected.minor_ver = FIELD_GET(HUC_GSC_MINOR_VER_HI_MASK, version_hi);
	uc_fw->file_selected.patch_ver = FIELD_GET(HUC_GSC_PATCH_VER_LO_MASK, version_lo);

	return 0;
}

static int check_ccs_header(struct drm_i915_private *i915,
			    const struct firmware *fw,
			    struct intel_uc_fw *uc_fw)
{
	struct uc_css_header *css;
	size_t size;

	/* Check the size of the blob before examining buffer contents */
	if (unlikely(fw->size < sizeof(struct uc_css_header))) {
		drm_warn(&i915->drm, "%s firmware %s: invalid size: %zu < %zu\n",
			 intel_uc_fw_type_repr(uc_fw->type), uc_fw->file_selected.path,
			 fw->size, sizeof(struct uc_css_header));
		return -ENODATA;
	}

	css = (struct uc_css_header *)fw->data;

	/* Check integrity of size values inside CSS header */
	size = (css->header_size_dw - css->key_size_dw - css->modulus_size_dw -
		css->exponent_size_dw) * sizeof(u32);
	if (unlikely(size != sizeof(struct uc_css_header))) {
		drm_warn(&i915->drm,
			 "%s firmware %s: unexpected header size: %zu != %zu\n",
			 intel_uc_fw_type_repr(uc_fw->type), uc_fw->file_selected.path,
			 fw->size, sizeof(struct uc_css_header));
		return -EPROTO;
	}

	/* uCode size must calculated from other sizes */
	uc_fw->ucode_size = (css->size_dw - css->header_size_dw) * sizeof(u32);

	/* now RSA */
	uc_fw->rsa_size = css->key_size_dw * sizeof(u32);

	/* At least, it should have header, uCode and RSA. Size of all three. */
	size = sizeof(struct uc_css_header) + uc_fw->ucode_size + uc_fw->rsa_size;
	if (unlikely(fw->size < size)) {
		drm_warn(&i915->drm, "%s firmware %s: invalid size: %zu < %zu\n",
			 intel_uc_fw_type_repr(uc_fw->type), uc_fw->file_selected.path,
			 fw->size, size);
		return -ENOEXEC;
	}

	/* Sanity check whether this fw is not larger than whole WOPCM memory */
	size = __intel_uc_fw_get_upload_size(uc_fw);
	if (unlikely(size >= i915->wopcm.size)) {
		drm_warn(&i915->drm, "%s firmware %s: invalid size: %zu > %zu\n",
			 intel_uc_fw_type_repr(uc_fw->type), uc_fw->file_selected.path,
			 size, (size_t)i915->wopcm.size);
		return -E2BIG;
	}

	/* Get version numbers from the CSS header */
	uc_fw->file_selected.major_ver = FIELD_GET(CSS_SW_VERSION_UC_MAJOR,
						   css->sw_version);
	uc_fw->file_selected.minor_ver = FIELD_GET(CSS_SW_VERSION_UC_MINOR,
						   css->sw_version);
	uc_fw->file_selected.patch_ver = FIELD_GET(CSS_SW_VERSION_UC_PATCH,
						   css->sw_version);

	if (uc_fw->type == INTEL_UC_FW_TYPE_GUC)
		uc_fw->private_data_size = css->private_data_size;

	return 0;
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
	struct intel_uc_fw_file file_ideal;
	struct device *dev = i915->drm.dev;
	struct drm_i915_gem_object *obj;
	const struct firmware *fw = NULL;
	bool old_ver = false;
	int err;

	GEM_BUG_ON(!i915->wopcm.size);
	GEM_BUG_ON(!intel_uc_fw_is_enabled(uc_fw));

	err = i915_inject_probe_error(i915, -ENXIO);
	if (err)
		goto fail;

	__force_fw_fetch_failures(uc_fw, -EINVAL);
	__force_fw_fetch_failures(uc_fw, -ESTALE);

	err = firmware_request_nowarn(&fw, uc_fw->file_selected.path, dev);
	memcpy(&file_ideal, &uc_fw->file_wanted, sizeof(file_ideal));

	/* Any error is terminal if overriding. Don't bother searching for older versions */
	if (err && intel_uc_fw_is_overridden(uc_fw))
		goto fail;

	while (err == -ENOENT) {
		old_ver = true;

		__uc_fw_auto_select(i915, uc_fw);
		if (!uc_fw->file_selected.path) {
			/*
			 * No more options! But set the path back to something
			 * valid just in case it gets dereferenced.
			 */
			uc_fw->file_selected.path = file_ideal.path;

			/* Also, preserve the version that was really wanted */
			memcpy(&uc_fw->file_wanted, &file_ideal, sizeof(uc_fw->file_wanted));
			break;
		}

		err = firmware_request_nowarn(&fw, uc_fw->file_selected.path, dev);
	}

	if (err)
		goto fail;

	if (uc_fw->loaded_via_gsc)
		err = check_gsc_manifest(fw, uc_fw);
	else
		err = check_ccs_header(i915, fw, uc_fw);
	if (err)
		goto fail;

	if (uc_fw->file_wanted.major_ver) {
		/* Check the file's major version was as it claimed */
		if (uc_fw->file_selected.major_ver != uc_fw->file_wanted.major_ver) {
			drm_notice(&i915->drm, "%s firmware %s: unexpected version: %u.%u != %u.%u\n",
				   intel_uc_fw_type_repr(uc_fw->type), uc_fw->file_selected.path,
				   uc_fw->file_selected.major_ver, uc_fw->file_selected.minor_ver,
				   uc_fw->file_wanted.major_ver, uc_fw->file_wanted.minor_ver);
			if (!intel_uc_fw_is_overridden(uc_fw)) {
				err = -ENOEXEC;
				goto fail;
			}
		} else {
			if (uc_fw->file_selected.minor_ver < uc_fw->file_wanted.minor_ver)
				old_ver = true;
		}
	}

	if (old_ver) {
		/* Preserve the version that was really wanted */
		memcpy(&uc_fw->file_wanted, &file_ideal, sizeof(uc_fw->file_wanted));

		drm_notice(&i915->drm,
			   "%s firmware %s (%d.%d) is recommended, but only %s (%d.%d) was found\n",
			   intel_uc_fw_type_repr(uc_fw->type),
			   uc_fw->file_wanted.path,
			   uc_fw->file_wanted.major_ver, uc_fw->file_wanted.minor_ver,
			   uc_fw->file_selected.path,
			   uc_fw->file_selected.major_ver, uc_fw->file_selected.minor_ver);
		drm_info(&i915->drm,
			 "Consider updating your linux-firmware pkg or downloading from %s\n",
			 INTEL_UC_FIRMWARE_URL);
	}

	if (HAS_LMEM(i915)) {
		obj = i915_gem_object_create_lmem_from_data(i915, fw->data, fw->size);
		if (!IS_ERR(obj))
			obj->flags |= I915_BO_ALLOC_PM_EARLY;
	} else {
		obj = i915_gem_object_create_shmem_from_data(i915, fw->data, fw->size);
	}

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

	i915_probe_error(i915, "%s firmware %s: fetch failed with error %d\n",
			 intel_uc_fw_type_repr(uc_fw->type), uc_fw->file_selected.path, err);
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
	struct i915_vma_resource *dummy = &uc_fw->dummy;
	u32 pte_flags = 0;

	dummy->start = uc_fw_ggtt_offset(uc_fw);
	dummy->node_size = obj->base.size;
	dummy->bi.pages = obj->mm.pages;

	GEM_BUG_ON(!i915_gem_object_has_pinned_pages(obj));
	GEM_BUG_ON(dummy->node_size > ggtt->uc_fw.size);

	/* uc_fw->obj cache domains were not controlled across suspend */
	if (i915_gem_object_has_struct_page(obj))
		drm_clflush_sg(dummy->bi.pages);

	if (i915_gem_object_is_lmem(obj))
		pte_flags |= PTE_LM;

	if (ggtt->vm.raw_insert_entries)
		ggtt->vm.raw_insert_entries(&ggtt->vm, dummy, I915_CACHE_NONE, pte_flags);
	else
		ggtt->vm.insert_entries(&ggtt->vm, dummy, I915_CACHE_NONE, pte_flags);
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
			 intel_uc_fw_type_repr(uc_fw->type), uc_fw->file_selected.path,
			 err);
	intel_uc_fw_change_status(uc_fw, INTEL_UC_FIRMWARE_LOAD_FAIL);
	return err;
}

static inline bool uc_fw_need_rsa_in_memory(struct intel_uc_fw *uc_fw)
{
	/*
	 * The HW reads the GuC RSA from memory if the key size is > 256 bytes,
	 * while it reads it from the 64 RSA registers if it is smaller.
	 * The HuC RSA is always read from memory.
	 */
	return uc_fw->type == INTEL_UC_FW_TYPE_HUC || uc_fw->rsa_size > 256;
}

static int uc_fw_rsa_data_create(struct intel_uc_fw *uc_fw)
{
	struct intel_gt *gt = __uc_fw_to_gt(uc_fw);
	struct i915_vma *vma;
	size_t copied;
	void *vaddr;
	int err;

	err = i915_inject_probe_error(gt->i915, -ENXIO);
	if (err)
		return err;

	if (!uc_fw_need_rsa_in_memory(uc_fw))
		return 0;

	/*
	 * uC firmwares will sit above GUC_GGTT_TOP and will not map through
	 * GGTT. Unfortunately, this means that the GuC HW cannot perform the uC
	 * authentication from memory, as the RSA offset now falls within the
	 * GuC inaccessible range. We resort to perma-pinning an additional vma
	 * within the accessible range that only contains the RSA signature.
	 * The GuC HW can use this extra pinning to perform the authentication
	 * since its GGTT offset will be GuC accessible.
	 */
	GEM_BUG_ON(uc_fw->rsa_size > PAGE_SIZE);
	vma = intel_guc_allocate_vma(&gt->uc.guc, PAGE_SIZE);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	vaddr = i915_gem_object_pin_map_unlocked(vma->obj,
						 i915_coherent_map_type(gt->i915, vma->obj, true));
	if (IS_ERR(vaddr)) {
		i915_vma_unpin_and_release(&vma, 0);
		err = PTR_ERR(vaddr);
		goto unpin_out;
	}

	copied = intel_uc_fw_copy_rsa(uc_fw, vaddr, vma->size);
	i915_gem_object_unpin_map(vma->obj);

	if (copied < uc_fw->rsa_size) {
		err = -ENOMEM;
		goto unpin_out;
	}

	uc_fw->rsa_data = vma;

	return 0;

unpin_out:
	i915_vma_unpin_and_release(&vma, 0);
	return err;
}

static void uc_fw_rsa_data_destroy(struct intel_uc_fw *uc_fw)
{
	i915_vma_unpin_and_release(&uc_fw->rsa_data, 0);
}

int intel_uc_fw_init(struct intel_uc_fw *uc_fw)
{
	int err;

	/* this should happen before the load! */
	GEM_BUG_ON(intel_uc_fw_is_loaded(uc_fw));

	if (!intel_uc_fw_is_available(uc_fw))
		return -ENOEXEC;

	err = i915_gem_object_pin_pages_unlocked(uc_fw->obj);
	if (err) {
		DRM_DEBUG_DRIVER("%s fw pin-pages err=%d\n",
				 intel_uc_fw_type_repr(uc_fw->type), err);
		goto out;
	}

	err = uc_fw_rsa_data_create(uc_fw);
	if (err) {
		DRM_DEBUG_DRIVER("%s fw rsa data creation failed, err=%d\n",
				 intel_uc_fw_type_repr(uc_fw->type), err);
		goto out_unpin;
	}

	return 0;

out_unpin:
	i915_gem_object_unpin_pages(uc_fw->obj);
out:
	intel_uc_fw_change_status(uc_fw, INTEL_UC_FIRMWARE_INIT_FAIL);
	return err;
}

void intel_uc_fw_fini(struct intel_uc_fw *uc_fw)
{
	uc_fw_rsa_data_destroy(uc_fw);

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
	struct intel_memory_region *mr = uc_fw->obj->mm.region;
	u32 size = min_t(u32, uc_fw->rsa_size, max_len);
	u32 offset = sizeof(struct uc_css_header) + uc_fw->ucode_size;
	struct sgt_iter iter;
	size_t count = 0;
	int idx;

	/* Called during reset handling, must be atomic [no fs_reclaim] */
	GEM_BUG_ON(!intel_uc_fw_is_available(uc_fw));

	idx = offset >> PAGE_SHIFT;
	offset = offset_in_page(offset);
	if (i915_gem_object_has_struct_page(uc_fw->obj)) {
		struct page *page;

		for_each_sgt_page(page, iter, uc_fw->obj->mm.pages) {
			u32 len = min_t(u32, size, PAGE_SIZE - offset);
			void *vaddr;

			if (idx > 0) {
				idx--;
				continue;
			}

			vaddr = kmap_atomic(page);
			memcpy(dst, vaddr + offset, len);
			kunmap_atomic(vaddr);

			offset = 0;
			dst += len;
			size -= len;
			count += len;
			if (!size)
				break;
		}
	} else {
		dma_addr_t addr;

		for_each_sgt_daddr(addr, iter, uc_fw->obj->mm.pages) {
			u32 len = min_t(u32, size, PAGE_SIZE - offset);
			void __iomem *vaddr;

			if (idx > 0) {
				idx--;
				continue;
			}

			vaddr = io_mapping_map_atomic_wc(&mr->iomap,
							 addr - mr->region.start);
			memcpy_fromio(dst, vaddr + offset, len);
			io_mapping_unmap_atomic(vaddr);

			offset = 0;
			dst += len;
			size -= len;
			count += len;
			if (!size)
				break;
		}
	}

	return count;
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
	u32 ver_sel, ver_want;

	drm_printf(p, "%s firmware: %s\n",
		   intel_uc_fw_type_repr(uc_fw->type), uc_fw->file_selected.path);
	if (uc_fw->file_selected.path != uc_fw->file_wanted.path)
		drm_printf(p, "%s firmware wanted: %s\n",
			   intel_uc_fw_type_repr(uc_fw->type), uc_fw->file_wanted.path);
	drm_printf(p, "\tstatus: %s\n",
		   intel_uc_fw_status_repr(uc_fw->status));
	ver_sel = MAKE_UC_VER(uc_fw->file_selected.major_ver,
			      uc_fw->file_selected.minor_ver,
			      uc_fw->file_selected.patch_ver);
	ver_want = MAKE_UC_VER(uc_fw->file_wanted.major_ver,
			       uc_fw->file_wanted.minor_ver,
			       uc_fw->file_wanted.patch_ver);
	if (ver_sel < ver_want)
		drm_printf(p, "\tversion: wanted %u.%u.%u, found %u.%u.%u\n",
			   uc_fw->file_wanted.major_ver,
			   uc_fw->file_wanted.minor_ver,
			   uc_fw->file_wanted.patch_ver,
			   uc_fw->file_selected.major_ver,
			   uc_fw->file_selected.minor_ver,
			   uc_fw->file_selected.patch_ver);
	else
		drm_printf(p, "\tversion: found %u.%u.%u\n",
			   uc_fw->file_selected.major_ver,
			   uc_fw->file_selected.minor_ver,
			   uc_fw->file_selected.patch_ver);
	drm_printf(p, "\tuCode: %u bytes\n", uc_fw->ucode_size);
	drm_printf(p, "\tRSA: %u bytes\n", uc_fw->rsa_size);
}

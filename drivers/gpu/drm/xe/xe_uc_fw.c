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
#include "xe_gsc.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_map.h"
#include "xe_mmio.h"
#include "xe_module.h"
#include "xe_sriov.h"
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
		u16 patch;
		bool full_ver_required;
	};
};

struct fw_blobs_by_type {
	const struct uc_fw_entry *entries;
	u32 count;
};

#define XE_GUC_FIRMWARE_DEFS(fw_def, mmp_ver, major_ver)			\
	fw_def(LUNARLAKE,	major_ver(xe,	guc,	lnl,	70, 19, 2))	\
	fw_def(METEORLAKE,	major_ver(i915,	guc,	mtl,	70, 19, 2))	\
	fw_def(DG2,		major_ver(i915,	guc,	dg2,	70, 19, 2))	\
	fw_def(DG1,		major_ver(i915,	guc,	dg1,	70, 19, 2))	\
	fw_def(ALDERLAKE_N,	major_ver(i915,	guc,	tgl,	70, 19, 2))	\
	fw_def(ALDERLAKE_P,	major_ver(i915,	guc,	adlp,	70, 19, 2))	\
	fw_def(ALDERLAKE_S,	major_ver(i915,	guc,	tgl,	70, 19, 2))	\
	fw_def(ROCKETLAKE,	major_ver(i915,	guc,	tgl,	70, 19, 2))	\
	fw_def(TIGERLAKE,	major_ver(i915,	guc,	tgl,	70, 19, 2))

#define XE_HUC_FIRMWARE_DEFS(fw_def, mmp_ver, no_ver)		\
	fw_def(METEORLAKE,	no_ver(i915,	huc_gsc,	mtl))		\
	fw_def(DG1,		no_ver(i915,	huc,		dg1))		\
	fw_def(ALDERLAKE_P,	no_ver(i915,	huc,		tgl))		\
	fw_def(ALDERLAKE_S,	no_ver(i915,	huc,		tgl))		\
	fw_def(ROCKETLAKE,	no_ver(i915,	huc,		tgl))		\
	fw_def(TIGERLAKE,	no_ver(i915,	huc,		tgl))

/* for the GSC FW we match the compatibility version and not the release one */
#define XE_GSC_FIRMWARE_DEFS(fw_def, major_ver)		\
	fw_def(METEORLAKE,	major_ver(i915,	gsc,	mtl,	1, 0, 0))

#define MAKE_FW_PATH(dir__, uc__, shortname__, version__)			\
	__stringify(dir__) "/" __stringify(shortname__) "_" __stringify(uc__) version__ ".bin"

#define fw_filename_mmp_ver(dir_, uc_, shortname_, a, b, c)			\
	MAKE_FW_PATH(dir_, uc_, shortname_, "_" __stringify(a ## . ## b ## . ## c))
#define fw_filename_major_ver(dir_, uc_, shortname_, a, b, c)			\
	MAKE_FW_PATH(dir_, uc_, shortname_, "_" __stringify(a))
#define fw_filename_no_ver(dir_, uc_, shortname_)				\
	MAKE_FW_PATH(dir_, uc_, shortname_, "")

#define uc_fw_entry_mmp_ver(dir_, uc_, shortname_, a, b, c)			\
	{ fw_filename_mmp_ver(dir_, uc_, shortname_, a, b, c),			\
	  a, b, c, true }
#define uc_fw_entry_major_ver(dir_, uc_, shortname_, a, b, c)			\
	{ fw_filename_major_ver(dir_, uc_, shortname_, a, b, c),		\
	  a, b, c }
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
XE_GSC_FIRMWARE_DEFS(XE_UC_MODULE_FIRMWARE, fw_filename_major_ver)

static struct xe_gt *
__uc_fw_to_gt(struct xe_uc_fw *uc_fw, enum xe_uc_fw_type type)
{
	XE_WARN_ON(type >= XE_UC_FW_NUM_TYPES);

	switch (type) {
	case XE_UC_FW_TYPE_GUC:
		return container_of(uc_fw, struct xe_gt, uc.guc.fw);
	case XE_UC_FW_TYPE_HUC:
		return container_of(uc_fw, struct xe_gt, uc.huc.fw);
	case XE_UC_FW_TYPE_GSC:
		return container_of(uc_fw, struct xe_gt, uc.gsc.fw);
	default:
		return NULL;
	}
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
	static const struct uc_fw_entry entries_gsc[] = {
		XE_GSC_FIRMWARE_DEFS(XE_UC_FW_ENTRY, uc_fw_entry_major_ver)
	};
	static const struct fw_blobs_by_type blobs_all[XE_UC_FW_NUM_TYPES] = {
		[XE_UC_FW_TYPE_GUC] = { entries_guc, ARRAY_SIZE(entries_guc) },
		[XE_UC_FW_TYPE_HUC] = { entries_huc, ARRAY_SIZE(entries_huc) },
		[XE_UC_FW_TYPE_GSC] = { entries_gsc, ARRAY_SIZE(entries_gsc) },
	};
	static const struct uc_fw_entry *entries;
	enum xe_platform p = xe->info.platform;
	u32 count;
	int i;

	xe_assert(xe, uc_fw->type < ARRAY_SIZE(blobs_all));
	entries = blobs_all[uc_fw->type].entries;
	count = blobs_all[uc_fw->type].count;

	for (i = 0; i < count && p <= entries[i].platform; i++) {
		if (p == entries[i].platform) {
			uc_fw->path = entries[i].path;
			uc_fw->versions.wanted.major = entries[i].major;
			uc_fw->versions.wanted.minor = entries[i].minor;
			uc_fw->versions.wanted.patch = entries[i].patch;
			uc_fw->full_ver_required = entries[i].full_ver_required;

			if (uc_fw->type == XE_UC_FW_TYPE_GSC)
				uc_fw->versions.wanted_type = XE_UC_FW_VER_COMPATIBILITY;
			else
				uc_fw->versions.wanted_type = XE_UC_FW_VER_RELEASE;

			break;
		}
	}
}

static void
uc_fw_override(struct xe_uc_fw *uc_fw)
{
	char *path_override = NULL;

	/* empty string disables, but it's not allowed for GuC */
	switch (uc_fw->type) {
	case XE_UC_FW_TYPE_GUC:
		if (xe_modparam.guc_firmware_path && *xe_modparam.guc_firmware_path)
			path_override = xe_modparam.guc_firmware_path;
		break;
	case XE_UC_FW_TYPE_HUC:
		path_override = xe_modparam.huc_firmware_path;
		break;
	case XE_UC_FW_TYPE_GSC:
		path_override = xe_modparam.gsc_firmware_path;
		break;
	default:
		break;
	}

	if (path_override) {
		uc_fw->path = path_override;
		uc_fw->user_overridden = true;
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

	xe_assert(xe, !(size % 4));
	xe_assert(xe, xe_uc_fw_is_available(uc_fw));

	xe_map_memcpy_from(xe, dst, &uc_fw->bo->vmap,
			   xe_uc_fw_rsa_offset(uc_fw), size);

	return size;
}

static void uc_fw_fini(struct drm_device *drm, void *arg)
{
	struct xe_uc_fw *uc_fw = arg;

	if (!xe_uc_fw_is_available(uc_fw))
		return;

	xe_uc_fw_change_status(uc_fw, XE_UC_FIRMWARE_SELECTED);
}

static int guc_read_css_info(struct xe_uc_fw *uc_fw, struct uc_css_header *css)
{
	struct xe_gt *gt = uc_fw_to_gt(uc_fw);
	struct xe_uc_fw_version *release = &uc_fw->versions.found[XE_UC_FW_VER_RELEASE];
	struct xe_uc_fw_version *compatibility = &uc_fw->versions.found[XE_UC_FW_VER_COMPATIBILITY];

	xe_gt_assert(gt, uc_fw->type == XE_UC_FW_TYPE_GUC);

	/* We don't support GuC releases older than 70.19 */
	if (release->major < 70 || (release->major == 70 && release->minor < 19)) {
		xe_gt_err(gt, "Unsupported GuC v%u.%u! v70.19 or newer is required\n",
			  release->major, release->minor);
		return -EINVAL;
	}

	compatibility->major = FIELD_GET(CSS_SW_VERSION_UC_MAJOR, css->submission_version);
	compatibility->minor = FIELD_GET(CSS_SW_VERSION_UC_MINOR, css->submission_version);
	compatibility->patch = FIELD_GET(CSS_SW_VERSION_UC_PATCH, css->submission_version);

	uc_fw->private_data_size = css->private_data_size;

	return 0;
}

int xe_uc_fw_check_version_requirements(struct xe_uc_fw *uc_fw)
{
	struct xe_device *xe = uc_fw_to_xe(uc_fw);
	struct xe_uc_fw_version *wanted = &uc_fw->versions.wanted;
	struct xe_uc_fw_version *found = &uc_fw->versions.found[uc_fw->versions.wanted_type];

	/* Driver has no requirement on any version, any is good. */
	if (!wanted->major)
		return 0;

	/*
	 * If full version is required, both major and minor should match.
	 * Otherwise, at least the major version.
	 */
	if (wanted->major != found->major ||
	    (uc_fw->full_ver_required &&
	     ((wanted->minor != found->minor) ||
	      (wanted->patch != found->patch)))) {
		drm_notice(&xe->drm, "%s firmware %s: unexpected version: %u.%u.%u != %u.%u.%u\n",
			   xe_uc_fw_type_repr(uc_fw->type), uc_fw->path,
			   found->major, found->minor, found->patch,
			   wanted->major, wanted->minor, wanted->patch);
		goto fail;
	}

	if (wanted->minor > found->minor ||
	    (wanted->minor == found->minor && wanted->patch > found->patch)) {
		drm_notice(&xe->drm, "%s firmware (%u.%u.%u) is recommended, but only (%u.%u.%u) was found in %s\n",
			   xe_uc_fw_type_repr(uc_fw->type),
			   wanted->major, wanted->minor, wanted->patch,
			   found->major, found->minor, found->patch,
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

/* Refer to the "CSS-based Firmware Layout" documentation entry for details */
static int parse_css_header(struct xe_uc_fw *uc_fw, const void *fw_data, size_t fw_size)
{
	struct xe_device *xe = uc_fw_to_xe(uc_fw);
	struct xe_uc_fw_version *release = &uc_fw->versions.found[XE_UC_FW_VER_RELEASE];
	struct uc_css_header *css;
	size_t size;

	/* Check the size of the blob before examining buffer contents */
	if (unlikely(fw_size < sizeof(struct uc_css_header))) {
		drm_warn(&xe->drm, "%s firmware %s: invalid size: %zu < %zu\n",
			 xe_uc_fw_type_repr(uc_fw->type), uc_fw->path,
			 fw_size, sizeof(struct uc_css_header));
		return -ENODATA;
	}

	css = (struct uc_css_header *)fw_data;

	/* Check integrity of size values inside CSS header */
	size = (css->header_size_dw - css->key_size_dw - css->modulus_size_dw -
		css->exponent_size_dw) * sizeof(u32);
	if (unlikely(size != sizeof(struct uc_css_header))) {
		drm_warn(&xe->drm,
			 "%s firmware %s: unexpected header size: %zu != %zu\n",
			 xe_uc_fw_type_repr(uc_fw->type), uc_fw->path,
			 fw_size, sizeof(struct uc_css_header));
		return -EPROTO;
	}

	/* uCode size must calculated from other sizes */
	uc_fw->ucode_size = (css->size_dw - css->header_size_dw) * sizeof(u32);

	/* now RSA */
	uc_fw->rsa_size = css->key_size_dw * sizeof(u32);

	/* At least, it should have header, uCode and RSA. Size of all three. */
	size = sizeof(struct uc_css_header) + uc_fw->ucode_size +
		uc_fw->rsa_size;
	if (unlikely(fw_size < size)) {
		drm_warn(&xe->drm, "%s firmware %s: invalid size: %zu < %zu\n",
			 xe_uc_fw_type_repr(uc_fw->type), uc_fw->path,
			 fw_size, size);
		return -ENOEXEC;
	}

	/* Get version numbers from the CSS header */
	release->major = FIELD_GET(CSS_SW_VERSION_UC_MAJOR, css->sw_version);
	release->minor = FIELD_GET(CSS_SW_VERSION_UC_MINOR, css->sw_version);
	release->patch = FIELD_GET(CSS_SW_VERSION_UC_PATCH, css->sw_version);

	if (uc_fw->type == XE_UC_FW_TYPE_GUC)
		return guc_read_css_info(uc_fw, css);

	return 0;
}

static bool is_cpd_header(const void *data)
{
	const u32 *marker = data;

	return *marker == GSC_CPD_HEADER_MARKER;
}

static u32 entry_offset(const struct gsc_cpd_header_v2 *header, const char *name)
{
	const struct gsc_cpd_entry *entry;
	int i;

	entry = (void *)header + header->header_length;

	for (i = 0; i < header->num_of_entries; i++, entry++)
		if (strcmp(entry->name, name) == 0)
			return entry->offset & GSC_CPD_ENTRY_OFFSET_MASK;

	return 0;
}

/* Refer to the "GSC-based Firmware Layout" documentation entry for details */
static int parse_cpd_header(struct xe_uc_fw *uc_fw, const void *data, size_t size,
			    const char *manifest_entry, const char *css_entry)
{
	struct xe_gt *gt = uc_fw_to_gt(uc_fw);
	struct xe_device *xe = gt_to_xe(gt);
	const struct gsc_cpd_header_v2 *header = data;
	struct xe_uc_fw_version *release = &uc_fw->versions.found[XE_UC_FW_VER_RELEASE];
	const struct gsc_manifest_header *manifest;
	size_t min_size = sizeof(*header);
	u32 offset;

	/* manifest_entry is mandatory, css_entry is optional */
	xe_assert(xe, manifest_entry);

	if (size < min_size || !is_cpd_header(header))
		return -ENOENT;

	if (header->header_length < sizeof(struct gsc_cpd_header_v2)) {
		xe_gt_err(gt, "invalid CPD header length %u!\n", header->header_length);
		return -EINVAL;
	}

	min_size = header->header_length + sizeof(struct gsc_cpd_entry) * header->num_of_entries;
	if (size < min_size) {
		xe_gt_err(gt, "FW too small! %zu < %zu\n", size, min_size);
		return -ENODATA;
	}

	/* Look for the manifest first */
	offset = entry_offset(header, manifest_entry);
	if (!offset) {
		xe_gt_err(gt, "Failed to find %s manifest!\n",
			  xe_uc_fw_type_repr(uc_fw->type));
		return -ENODATA;
	}

	min_size = offset + sizeof(struct gsc_manifest_header);
	if (size < min_size) {
		xe_gt_err(gt, "FW too small! %zu < %zu\n", size, min_size);
		return -ENODATA;
	}

	manifest = data + offset;

	release->major = manifest->fw_version.major;
	release->minor = manifest->fw_version.minor;
	release->patch = manifest->fw_version.hotfix;

	if (uc_fw->type == XE_UC_FW_TYPE_GSC) {
		struct xe_gsc *gsc = container_of(uc_fw, struct xe_gsc, fw);

		release->build = manifest->fw_version.build;
		gsc->security_version = manifest->security_version;
	}

	/* then optionally look for the css header */
	if (css_entry) {
		int ret;

		/*
		 * This section does not contain a CSS entry on DG2. We
		 * don't support DG2 HuC right now, so no need to handle
		 * it, just add a reminder in case that changes.
		 */
		xe_assert(xe, xe->info.platform != XE_DG2);

		offset = entry_offset(header, css_entry);

		/* the CSS header parser will check that the CSS header fits */
		if (offset > size) {
			xe_gt_err(gt, "FW too small! %zu < %u\n", size, offset);
			return -ENODATA;
		}

		ret = parse_css_header(uc_fw, data + offset, size - offset);
		if (ret)
			return ret;

		uc_fw->css_offset = offset;
	}

	uc_fw->has_gsc_headers = true;

	return 0;
}

static int parse_gsc_layout(struct xe_uc_fw *uc_fw, const void *data, size_t size)
{
	struct xe_gt *gt = uc_fw_to_gt(uc_fw);
	const struct gsc_layout_pointers *layout = data;
	const struct gsc_bpdt_header *bpdt_header = NULL;
	const struct gsc_bpdt_entry *bpdt_entry = NULL;
	size_t min_size = sizeof(*layout);
	int i;

	if (size < min_size) {
		xe_gt_err(gt, "GSC FW too small! %zu < %zu\n", size, min_size);
		return -ENODATA;
	}

	min_size = layout->boot1.offset + layout->boot1.size;
	if (size < min_size) {
		xe_gt_err(gt, "GSC FW too small for boot section! %zu < %zu\n",
			  size, min_size);
		return -ENODATA;
	}

	min_size = sizeof(*bpdt_header);
	if (layout->boot1.size < min_size) {
		xe_gt_err(gt, "GSC FW boot section too small for BPDT header: %u < %zu\n",
			  layout->boot1.size, min_size);
		return -ENODATA;
	}

	bpdt_header = data + layout->boot1.offset;
	if (bpdt_header->signature != GSC_BPDT_HEADER_SIGNATURE) {
		xe_gt_err(gt, "invalid signature for BPDT header: 0x%08x!\n",
			  bpdt_header->signature);
		return -EINVAL;
	}

	min_size += sizeof(*bpdt_entry) * bpdt_header->descriptor_count;
	if (layout->boot1.size < min_size) {
		xe_gt_err(gt, "GSC FW boot section too small for BPDT entries: %u < %zu\n",
			  layout->boot1.size, min_size);
		return -ENODATA;
	}

	bpdt_entry = (void *)bpdt_header + sizeof(*bpdt_header);
	for (i = 0; i < bpdt_header->descriptor_count; i++, bpdt_entry++) {
		if ((bpdt_entry->type & GSC_BPDT_ENTRY_TYPE_MASK) !=
		    GSC_BPDT_ENTRY_TYPE_GSC_RBE)
			continue;

		min_size = bpdt_entry->sub_partition_offset;

		/* the CPD header parser will check that the CPD header fits */
		if (layout->boot1.size < min_size) {
			xe_gt_err(gt, "GSC FW boot section too small for CPD offset: %u < %zu\n",
				  layout->boot1.size, min_size);
			return -ENODATA;
		}

		return parse_cpd_header(uc_fw,
					(void *)bpdt_header + min_size,
					layout->boot1.size - min_size,
					"RBEP.man", NULL);
	}

	xe_gt_err(gt, "couldn't find CPD header in GSC binary!\n");
	return -ENODATA;
}

static int parse_headers(struct xe_uc_fw *uc_fw, const struct firmware *fw)
{
	int ret;

	/*
	 * All GuC releases and older HuC ones use CSS headers, while newer HuC
	 * releases use GSC CPD headers.
	 */
	switch (uc_fw->type) {
	case XE_UC_FW_TYPE_GSC:
		return parse_gsc_layout(uc_fw, fw->data, fw->size);
	case XE_UC_FW_TYPE_HUC:
		ret = parse_cpd_header(uc_fw, fw->data, fw->size, "HUCP.man", "huc_fw");
		if (!ret || ret != -ENOENT)
			return ret;
		fallthrough;
	case XE_UC_FW_TYPE_GUC:
		return parse_css_header(uc_fw, fw->data, fw->size);
	default:
		return -EINVAL;
	}

	return 0;
}

#define print_uc_fw_version(p_, version_, prefix_, ...) \
do { \
	struct xe_uc_fw_version *ver_ = (version_); \
	if (ver_->build) \
		drm_printf(p_, prefix_ " version %u.%u.%u.%u\n", ##__VA_ARGS__, \
			   ver_->major, ver_->minor, \
			   ver_->patch, ver_->build); \
	else \
		drm_printf(p_, prefix_ " version %u.%u.%u\n", ##__VA_ARGS__, \
			  ver_->major, ver_->minor, ver_->patch); \
} while (0)

static int uc_fw_request(struct xe_uc_fw *uc_fw, const struct firmware **firmware_p)
{
	struct xe_device *xe = uc_fw_to_xe(uc_fw);
	struct device *dev = xe->drm.dev;
	struct drm_printer p = drm_info_printer(dev);
	const struct firmware *fw = NULL;
	int err;

	/*
	 * we use FIRMWARE_UNINITIALIZED to detect checks against uc_fw->status
	 * before we're looked at the HW caps to see if we have uc support
	 */
	BUILD_BUG_ON(XE_UC_FIRMWARE_UNINITIALIZED);
	xe_assert(xe, !uc_fw->status);
	xe_assert(xe, !uc_fw->path);

	uc_fw_auto_select(xe, uc_fw);

	if (IS_SRIOV_VF(xe)) {
		/* VF will support only firmwares that driver can autoselect */
		xe_uc_fw_change_status(uc_fw, uc_fw->path ?
				       XE_UC_FIRMWARE_PRELOADED :
				       XE_UC_FIRMWARE_NOT_SUPPORTED);
		return 0;
	}

	uc_fw_override(uc_fw);

	xe_uc_fw_change_status(uc_fw, uc_fw->path ?
			       XE_UC_FIRMWARE_SELECTED :
			       XE_UC_FIRMWARE_NOT_SUPPORTED);

	if (!xe_uc_fw_is_supported(uc_fw)) {
		if (uc_fw->type == XE_UC_FW_TYPE_GUC) {
			drm_err(&xe->drm, "No GuC firmware defined for platform\n");
			return -ENOENT;
		}
		return 0;
	}

	/* an empty path means the firmware is disabled */
	if (!xe_device_uc_enabled(xe) || !(*uc_fw->path)) {
		xe_uc_fw_change_status(uc_fw, XE_UC_FIRMWARE_DISABLED);
		drm_dbg(&xe->drm, "%s disabled", xe_uc_fw_type_repr(uc_fw->type));
		return 0;
	}

	err = request_firmware(&fw, uc_fw->path, dev);
	if (err)
		goto fail;

	err = parse_headers(uc_fw, fw);
	if (err)
		goto fail;

	print_uc_fw_version(&p,
			    &uc_fw->versions.found[XE_UC_FW_VER_RELEASE],
			    "Using %s firmware from %s",
			    xe_uc_fw_type_repr(uc_fw->type), uc_fw->path);

	/* for GSC FW we want the compatibility version, which we query after load */
	if (uc_fw->type != XE_UC_FW_TYPE_GSC) {
		err = xe_uc_fw_check_version_requirements(uc_fw);
		if (err)
			goto fail;
	}

	*firmware_p = fw;

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

static void uc_fw_release(const struct firmware *fw)
{
	release_firmware(fw);
}

static int uc_fw_copy(struct xe_uc_fw *uc_fw, const void *data, size_t size, u32 flags)
{
	struct xe_device *xe = uc_fw_to_xe(uc_fw);
	struct xe_gt *gt = uc_fw_to_gt(uc_fw);
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_bo *obj;
	int err;

	obj = xe_managed_bo_create_from_data(xe, tile, data, size, flags);
	if (IS_ERR(obj)) {
		drm_notice(&xe->drm, "%s firmware %s: failed to create / populate bo",
			   xe_uc_fw_type_repr(uc_fw->type), uc_fw->path);
		err = PTR_ERR(obj);
		goto fail;
	}

	uc_fw->bo = obj;
	uc_fw->size = size;

	xe_uc_fw_change_status(uc_fw, XE_UC_FIRMWARE_AVAILABLE);

	err = drmm_add_action_or_reset(&xe->drm, uc_fw_fini, uc_fw);
	if (err)
		goto fail;

	return 0;

fail:
	xe_uc_fw_change_status(uc_fw, XE_UC_FIRMWARE_ERROR);
	drm_notice(&xe->drm, "%s firmware %s: copy failed with error %d\n",
		   xe_uc_fw_type_repr(uc_fw->type), uc_fw->path, err);

	return err;
}

int xe_uc_fw_init(struct xe_uc_fw *uc_fw)
{
	const struct firmware *fw = NULL;
	int err;

	err = uc_fw_request(uc_fw, &fw);
	if (err)
		return err;

	/* no error and no firmware means nothing to copy */
	if (!fw)
		return 0;

	err = uc_fw_copy(uc_fw, fw->data, fw->size,
			 XE_BO_FLAG_SYSTEM | XE_BO_FLAG_GGTT |
			 XE_BO_FLAG_GGTT_INVALIDATE);

	uc_fw_release(fw);

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
	u64 src_offset;
	u32 dma_ctrl;
	int ret;

	xe_force_wake_assert_held(gt_to_fw(gt), XE_FW_GT);

	/* Set the source address for the uCode */
	src_offset = uc_fw_ggtt_offset(uc_fw) + uc_fw->css_offset;
	xe_mmio_write32(gt, DMA_ADDR_0_LOW, lower_32_bits(src_offset));
	xe_mmio_write32(gt, DMA_ADDR_0_HIGH,
			upper_32_bits(src_offset) | DMA_ADDRESS_SPACE_GGTT);

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
	ret = xe_mmio_wait32(gt, DMA_CTRL, START_DMA, 0, 100000, &dma_ctrl,
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
	xe_assert(xe, !xe_uc_fw_is_loaded(uc_fw));

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

static const char *version_type_repr(enum xe_uc_fw_version_types type)
{
	switch (type) {
	case XE_UC_FW_VER_RELEASE:
		return "release";
	case XE_UC_FW_VER_COMPATIBILITY:
		return "compatibility";
	default:
		return "Unknown version type";
	}
}

void xe_uc_fw_print(struct xe_uc_fw *uc_fw, struct drm_printer *p)
{
	int i;

	drm_printf(p, "%s firmware: %s\n",
		   xe_uc_fw_type_repr(uc_fw->type), uc_fw->path);
	drm_printf(p, "\tstatus: %s\n",
		   xe_uc_fw_status_repr(uc_fw->status));

	print_uc_fw_version(p, &uc_fw->versions.wanted, "\twanted %s",
			    version_type_repr(uc_fw->versions.wanted_type));

	for (i = 0; i < XE_UC_FW_VER_TYPE_COUNT; i++) {
		struct xe_uc_fw_version *ver = &uc_fw->versions.found[i];

		if (ver->major)
			print_uc_fw_version(p, ver, "\tfound %s",
					    version_type_repr(i));
	}

	if (uc_fw->ucode_size)
		drm_printf(p, "\tuCode: %u bytes\n", uc_fw->ucode_size);
	if (uc_fw->rsa_size)
		drm_printf(p, "\tRSA: %u bytes\n", uc_fw->rsa_size);
}

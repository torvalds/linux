/*
 * Copyright © 2014-2017 Intel Corporation
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

#ifndef _INTEL_UC_FW_H_
#define _INTEL_UC_FW_H_

#include <linux/types.h>
#include "intel_uc_fw_abi.h"
#include "i915_gem.h"

struct drm_printer;
struct drm_i915_private;
struct intel_gt;

/* Home of GuC, HuC and DMC firmwares */
#define INTEL_UC_FIRMWARE_URL "https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/i915"

enum intel_uc_fw_status {
	INTEL_UC_FIRMWARE_FAIL = -3, /* failed to xfer or init/auth the fw */
	INTEL_UC_FIRMWARE_MISSING = -2, /* blob not found on the system */
	INTEL_UC_FIRMWARE_NOT_SUPPORTED = -1, /* no uc HW */
	INTEL_UC_FIRMWARE_UNINITIALIZED = 0, /* used to catch checks done too early */
	INTEL_UC_FIRMWARE_SELECTED, /* selected the blob we want to load */
	INTEL_UC_FIRMWARE_AVAILABLE, /* blob found and copied in mem */
	INTEL_UC_FIRMWARE_TRANSFERRED, /* dma xfer done */
	INTEL_UC_FIRMWARE_RUNNING /* init/auth done */
};

enum intel_uc_fw_type {
	INTEL_UC_FW_TYPE_GUC = 0,
	INTEL_UC_FW_TYPE_HUC
};
#define INTEL_UC_FW_NUM_TYPES 2

/*
 * This structure encapsulates all the data needed during the process
 * of fetching, caching, and loading the firmware image into the uC.
 */
struct intel_uc_fw {
	enum intel_uc_fw_type type;
	enum intel_uc_fw_status status;
	const char *path;
	bool user_overridden;
	size_t size;
	struct drm_i915_gem_object *obj;

	/*
	 * The firmware build process will generate a version header file with major and
	 * minor version defined. The versions are built into CSS header of firmware.
	 * i915 kernel driver set the minimal firmware version required per platform.
	 */
	u16 major_ver_wanted;
	u16 minor_ver_wanted;
	u16 major_ver_found;
	u16 minor_ver_found;

	u32 rsa_size;
	u32 ucode_size;
};

static inline
const char *intel_uc_fw_status_repr(enum intel_uc_fw_status status)
{
	switch (status) {
	case INTEL_UC_FIRMWARE_FAIL:
		return "FAIL";
	case INTEL_UC_FIRMWARE_MISSING:
		return "MISSING";
	case INTEL_UC_FIRMWARE_NOT_SUPPORTED:
		return "N/A";
	case INTEL_UC_FIRMWARE_UNINITIALIZED:
		return "UNINITIALIZED";
	case INTEL_UC_FIRMWARE_SELECTED:
		return "SELECTED";
	case INTEL_UC_FIRMWARE_AVAILABLE:
		return "AVAILABLE";
	case INTEL_UC_FIRMWARE_TRANSFERRED:
		return "TRANSFERRED";
	case INTEL_UC_FIRMWARE_RUNNING:
		return "RUNNING";
	}
	return "<invalid>";
}

static inline const char *intel_uc_fw_type_repr(enum intel_uc_fw_type type)
{
	switch (type) {
	case INTEL_UC_FW_TYPE_GUC:
		return "GuC";
	case INTEL_UC_FW_TYPE_HUC:
		return "HuC";
	}
	return "uC";
}

static inline enum intel_uc_fw_status
__intel_uc_fw_status(struct intel_uc_fw *uc_fw)
{
	/* shouldn't call this before checking hw/blob availability */
	GEM_BUG_ON(uc_fw->status == INTEL_UC_FIRMWARE_UNINITIALIZED);
	return uc_fw->status;
}

static inline bool intel_uc_fw_is_available(struct intel_uc_fw *uc_fw)
{
	return __intel_uc_fw_status(uc_fw) >= INTEL_UC_FIRMWARE_AVAILABLE;
}

static inline bool intel_uc_fw_is_loaded(struct intel_uc_fw *uc_fw)
{
	return __intel_uc_fw_status(uc_fw) >= INTEL_UC_FIRMWARE_TRANSFERRED;
}

static inline bool intel_uc_fw_is_running(struct intel_uc_fw *uc_fw)
{
	return __intel_uc_fw_status(uc_fw) == INTEL_UC_FIRMWARE_RUNNING;
}

static inline bool intel_uc_fw_supported(struct intel_uc_fw *uc_fw)
{
	return __intel_uc_fw_status(uc_fw) != INTEL_UC_FIRMWARE_NOT_SUPPORTED;
}

static inline bool intel_uc_fw_is_overridden(const struct intel_uc_fw *uc_fw)
{
	return uc_fw->user_overridden;
}

static inline void intel_uc_fw_sanitize(struct intel_uc_fw *uc_fw)
{
	if (intel_uc_fw_is_loaded(uc_fw))
		uc_fw->status = INTEL_UC_FIRMWARE_AVAILABLE;
}

/**
 * intel_uc_fw_get_upload_size() - Get size of firmware needed to be uploaded.
 * @uc_fw: uC firmware.
 *
 * Get the size of the firmware and header that will be uploaded to WOPCM.
 *
 * Return: Upload firmware size, or zero on firmware fetch failure.
 */
static inline u32 intel_uc_fw_get_upload_size(struct intel_uc_fw *uc_fw)
{
	if (!intel_uc_fw_is_available(uc_fw))
		return 0;

	return sizeof(struct uc_css_header) + uc_fw->ucode_size;
}

void intel_uc_fw_init_early(struct intel_uc_fw *uc_fw,
			    enum intel_uc_fw_type type,
			    struct drm_i915_private *i915);
void intel_uc_fw_fetch(struct intel_uc_fw *uc_fw,
		       struct drm_i915_private *i915);
void intel_uc_fw_cleanup_fetch(struct intel_uc_fw *uc_fw);
int intel_uc_fw_upload(struct intel_uc_fw *uc_fw, struct intel_gt *gt,
		       u32 wopcm_offset, u32 dma_flags);
int intel_uc_fw_init(struct intel_uc_fw *uc_fw);
void intel_uc_fw_fini(struct intel_uc_fw *uc_fw);
size_t intel_uc_fw_copy_rsa(struct intel_uc_fw *uc_fw, void *dst, u32 max_len);
void intel_uc_fw_dump(const struct intel_uc_fw *uc_fw, struct drm_printer *p);

#endif

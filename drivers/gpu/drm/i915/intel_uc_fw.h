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

struct drm_printer;
struct drm_i915_private;
struct i915_vma;

/* Home of GuC, HuC and DMC firmwares */
#define INTEL_UC_FIRMWARE_URL "https://01.org/linuxgraphics/downloads/firmware"

enum intel_uc_fw_status {
	INTEL_UC_FIRMWARE_FAIL = -1,
	INTEL_UC_FIRMWARE_NONE = 0,
	INTEL_UC_FIRMWARE_PENDING,
	INTEL_UC_FIRMWARE_SUCCESS
};

enum intel_uc_fw_type {
	INTEL_UC_FW_TYPE_GUC,
	INTEL_UC_FW_TYPE_HUC
};

/*
 * This structure encapsulates all the data needed during the process
 * of fetching, caching, and loading the firmware image into the uC.
 */
struct intel_uc_fw {
	const char *path;
	size_t size;
	struct drm_i915_gem_object *obj;
	enum intel_uc_fw_status fetch_status;
	enum intel_uc_fw_status load_status;

	/*
	 * The firmware build process will generate a version header file with major and
	 * minor version defined. The versions are built into CSS header of firmware.
	 * i915 kernel driver set the minimal firmware version required per platform.
	 */
	u16 major_ver_wanted;
	u16 minor_ver_wanted;
	u16 major_ver_found;
	u16 minor_ver_found;

	enum intel_uc_fw_type type;
	u32 header_size;
	u32 header_offset;
	u32 rsa_size;
	u32 rsa_offset;
	u32 ucode_size;
	u32 ucode_offset;
};

static inline
const char *intel_uc_fw_status_repr(enum intel_uc_fw_status status)
{
	switch (status) {
	case INTEL_UC_FIRMWARE_FAIL:
		return "FAIL";
	case INTEL_UC_FIRMWARE_NONE:
		return "NONE";
	case INTEL_UC_FIRMWARE_PENDING:
		return "PENDING";
	case INTEL_UC_FIRMWARE_SUCCESS:
		return "SUCCESS";
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

static inline
void intel_uc_fw_init(struct intel_uc_fw *uc_fw, enum intel_uc_fw_type type)
{
	uc_fw->path = NULL;
	uc_fw->fetch_status = INTEL_UC_FIRMWARE_NONE;
	uc_fw->load_status = INTEL_UC_FIRMWARE_NONE;
	uc_fw->type = type;
}

void intel_uc_fw_fetch(struct drm_i915_private *dev_priv,
		       struct intel_uc_fw *uc_fw);
int intel_uc_fw_upload(struct intel_uc_fw *uc_fw,
		       int (*xfer)(struct intel_uc_fw *uc_fw,
				   struct i915_vma *vma));
void intel_uc_fw_fini(struct intel_uc_fw *uc_fw);
void intel_uc_fw_dump(struct intel_uc_fw *uc_fw, struct drm_printer *p);

#endif

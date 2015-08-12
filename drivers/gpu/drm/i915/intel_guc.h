/*
 * Copyright © 2014 Intel Corporation
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
#ifndef _INTEL_GUC_H_
#define _INTEL_GUC_H_

#include "intel_guc_fwif.h"
#include "i915_guc_reg.h"

enum intel_guc_fw_status {
	GUC_FIRMWARE_FAIL = -1,
	GUC_FIRMWARE_NONE = 0,
	GUC_FIRMWARE_PENDING,
	GUC_FIRMWARE_SUCCESS
};

/*
 * This structure encapsulates all the data needed during the process
 * of fetching, caching, and loading the firmware image into the GuC.
 */
struct intel_guc_fw {
	struct drm_device *		guc_dev;
	const char *			guc_fw_path;
	size_t				guc_fw_size;
	struct drm_i915_gem_object *	guc_fw_obj;
	enum intel_guc_fw_status	guc_fw_fetch_status;
	enum intel_guc_fw_status	guc_fw_load_status;

	uint16_t			guc_fw_major_wanted;
	uint16_t			guc_fw_minor_wanted;
	uint16_t			guc_fw_major_found;
	uint16_t			guc_fw_minor_found;
};

struct intel_guc {
	struct intel_guc_fw guc_fw;

	uint32_t log_flags;
	struct drm_i915_gem_object *log_obj;

	struct drm_i915_gem_object *ctx_pool_obj;
	struct ida ctx_ids;
};

/* intel_guc_loader.c */
extern void intel_guc_ucode_init(struct drm_device *dev);
extern int intel_guc_ucode_load(struct drm_device *dev);
extern void intel_guc_ucode_fini(struct drm_device *dev);
extern const char *intel_guc_fw_status_repr(enum intel_guc_fw_status status);

/* i915_guc_submission.c */
int i915_guc_submission_init(struct drm_device *dev);
void i915_guc_submission_fini(struct drm_device *dev);

#endif

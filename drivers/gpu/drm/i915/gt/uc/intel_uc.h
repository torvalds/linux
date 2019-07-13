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
#ifndef _INTEL_UC_H_
#define _INTEL_UC_H_

#include "intel_guc.h"
#include "intel_huc.h"
#include "i915_params.h"

struct intel_uc {
	struct intel_guc guc;
	struct intel_huc huc;
};

void intel_uc_init_early(struct intel_uc *uc);
void intel_uc_cleanup_early(struct intel_uc *uc);
void intel_uc_init_mmio(struct intel_uc *uc);
void intel_uc_fetch_firmwares(struct intel_uc *uc);
void intel_uc_cleanup_firmwares(struct intel_uc *uc);
void intel_uc_sanitize(struct intel_uc *uc);
int intel_uc_init_hw(struct intel_uc *uc);
void intel_uc_fini_hw(struct intel_uc *uc);
int intel_uc_init(struct intel_uc *uc);
void intel_uc_fini(struct intel_uc *uc);
void intel_uc_reset_prepare(struct intel_uc *uc);
void intel_uc_suspend(struct intel_uc *uc);
void intel_uc_runtime_suspend(struct intel_uc *uc);
int intel_uc_resume(struct intel_uc *uc);

static inline bool intel_uc_is_using_guc(struct intel_uc *uc)
{
	GEM_BUG_ON(i915_modparams.enable_guc < 0);
	return i915_modparams.enable_guc > 0;
}

static inline bool intel_uc_is_using_guc_submission(struct intel_uc *uc)
{
	GEM_BUG_ON(i915_modparams.enable_guc < 0);
	return i915_modparams.enable_guc & ENABLE_GUC_SUBMISSION;
}

static inline bool intel_uc_is_using_huc(struct intel_uc *uc)
{
	GEM_BUG_ON(i915_modparams.enable_guc < 0);
	return i915_modparams.enable_guc & ENABLE_GUC_LOAD_HUC;
}

#endif

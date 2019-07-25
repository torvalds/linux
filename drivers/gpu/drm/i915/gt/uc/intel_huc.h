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

#ifndef _INTEL_HUC_H_
#define _INTEL_HUC_H_

#include "i915_reg.h"
#include "intel_uc_fw.h"
#include "intel_huc_fw.h"

struct intel_huc {
	/* Generic uC firmware management */
	struct intel_uc_fw fw;

	/* HuC-specific additions */
	struct i915_vma *rsa_data;

	struct {
		i915_reg_t reg;
		u32 mask;
		u32 value;
	} status;
};

void intel_huc_init_early(struct intel_huc *huc);
int intel_huc_init(struct intel_huc *huc);
void intel_huc_fini(struct intel_huc *huc);
int intel_huc_auth(struct intel_huc *huc);
int intel_huc_check_status(struct intel_huc *huc);

static inline int intel_huc_sanitize(struct intel_huc *huc)
{
	intel_uc_fw_sanitize(&huc->fw);
	return 0;
}

static inline bool intel_huc_is_authenticated(struct intel_huc *huc)
{
	return intel_uc_fw_is_running(&huc->fw);
}

#endif

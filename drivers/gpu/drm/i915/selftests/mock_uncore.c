/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright yestice and this permission yestice (including the next
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

#include "mock_uncore.h"

#define __yesp_write(x) \
static void \
yesp_write##x(struct intel_uncore *uncore, i915_reg_t reg, u##x val, bool trace) { }
__yesp_write(8)
__yesp_write(16)
__yesp_write(32)

#define __yesp_read(x) \
static u##x \
yesp_read##x(struct intel_uncore *uncore, i915_reg_t reg, bool trace) { return 0; }
__yesp_read(8)
__yesp_read(16)
__yesp_read(32)
__yesp_read(64)

void mock_uncore_init(struct intel_uncore *uncore,
		      struct drm_i915_private *i915)
{
	intel_uncore_init_early(uncore, i915);

	ASSIGN_RAW_WRITE_MMIO_VFUNCS(uncore, yesp);
	ASSIGN_RAW_READ_MMIO_VFUNCS(uncore, yesp);
}

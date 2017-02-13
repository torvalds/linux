/*
 * Copyright © 2016 Intel Corporation
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

#include "../i915_selftest.h"

static int intel_fw_table_check(struct drm_i915_private *i915)
{
	const struct intel_forcewake_range *ranges;
	unsigned int num_ranges, i;
	s32 prev;

	ranges = i915->uncore.fw_domains_table;
	if (!ranges)
		return 0;

	num_ranges = i915->uncore.fw_domains_table_entries;
	for (i = 0, prev = -1; i < num_ranges; i++, ranges++) {
		/* Check that the table is watertight */
		if (IS_GEN9(i915) && (prev + 1) != (s32)ranges->start) {
			pr_err("%s: entry[%d]:(%x, %x) is not watertight to previous (%x)\n",
			       __func__, i, ranges->start, ranges->end, prev);
			return -EINVAL;
		}

		/* Check that the table never goes backwards */
		if (prev >= (s32)ranges->start) {
			pr_err("%s: entry[%d]:(%x, %x) is less than the previous (%x)\n",
			       __func__, i, ranges->start, ranges->end, prev);
			return -EINVAL;
		}

		/* Check that the entry is valid */
		if (ranges->start >= ranges->end) {
			pr_err("%s: entry[%d]:(%x, %x) has negative length\n",
			       __func__, i, ranges->start, ranges->end);
			return -EINVAL;
		}

		prev = ranges->end;
	}

	return 0;
}

static int intel_shadow_table_check(void)
{
	const i915_reg_t *reg = gen8_shadowed_regs;
	unsigned int i;
	s32 prev;

	for (i = 0, prev = -1; i < ARRAY_SIZE(gen8_shadowed_regs); i++, reg++) {
		u32 offset = i915_mmio_reg_offset(*reg);

		if (prev >= (s32)offset) {
			pr_err("%s: entry[%d]:(%x) is before previous (%x)\n",
			       __func__, i, offset, prev);
			return -EINVAL;
		}

		prev = offset;
	}

	return 0;
}

int intel_uncore_live_selftests(struct drm_i915_private *i915)
{
	int err;

	err = intel_fw_table_check(i915);
	if (err)
		return err;

	err = intel_shadow_table_check();
	if (err)
		return err;

	return 0;
}

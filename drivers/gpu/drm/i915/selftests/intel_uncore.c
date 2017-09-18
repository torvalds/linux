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

static int intel_fw_table_check(const struct intel_forcewake_range *ranges,
				unsigned int num_ranges,
				bool is_watertight)
{
	unsigned int i;
	s32 prev;

	for (i = 0, prev = -1; i < num_ranges; i++, ranges++) {
		/* Check that the table is watertight */
		if (is_watertight && (prev + 1) != (s32)ranges->start) {
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

int intel_uncore_mock_selftests(void)
{
	struct {
		const struct intel_forcewake_range *ranges;
		unsigned int num_ranges;
		bool is_watertight;
	} fw[] = {
		{ __vlv_fw_ranges, ARRAY_SIZE(__vlv_fw_ranges), false },
		{ __chv_fw_ranges, ARRAY_SIZE(__chv_fw_ranges), false },
		{ __gen9_fw_ranges, ARRAY_SIZE(__gen9_fw_ranges), true },
	};
	int err, i;

	for (i = 0; i < ARRAY_SIZE(fw); i++) {
		err = intel_fw_table_check(fw[i].ranges,
					   fw[i].num_ranges,
					   fw[i].is_watertight);
		if (err)
			return err;
	}

	err = intel_shadow_table_check();
	if (err)
		return err;

	return 0;
}

static int intel_uncore_check_forcewake_domains(struct drm_i915_private *dev_priv)
{
#define FW_RANGE 0x40000
	unsigned long *valid;
	u32 offset;
	int err;

	if (!HAS_FPGA_DBG_UNCLAIMED(dev_priv) &&
	    !IS_VALLEYVIEW(dev_priv) &&
	    !IS_CHERRYVIEW(dev_priv))
		return 0;

	if (IS_VALLEYVIEW(dev_priv)) /* XXX system lockup! */
		return 0;

	if (IS_BROADWELL(dev_priv)) /* XXX random GPU hang afterwards! */
		return 0;

	valid = kzalloc(BITS_TO_LONGS(FW_RANGE) * sizeof(*valid),
			GFP_KERNEL);
	if (!valid)
		return -ENOMEM;

	intel_uncore_forcewake_get(dev_priv, FORCEWAKE_ALL);

	check_for_unclaimed_mmio(dev_priv);
	for (offset = 0; offset < FW_RANGE; offset += 4) {
		i915_reg_t reg = { offset };

		(void)I915_READ_FW(reg);
		if (!check_for_unclaimed_mmio(dev_priv))
			set_bit(offset, valid);
	}

	intel_uncore_forcewake_put(dev_priv, FORCEWAKE_ALL);

	err = 0;
	for_each_set_bit(offset, valid, FW_RANGE) {
		i915_reg_t reg = { offset };

		intel_uncore_forcewake_reset(dev_priv, false);
		check_for_unclaimed_mmio(dev_priv);

		(void)I915_READ(reg);
		if (check_for_unclaimed_mmio(dev_priv)) {
			pr_err("Unclaimed mmio read to register 0x%04x\n",
			       offset);
			err = -EINVAL;
		}
	}

	kfree(valid);
	return err;
}

int intel_uncore_live_selftests(struct drm_i915_private *i915)
{
	int err;

	/* Confirm the table we load is still valid */
	err = intel_fw_table_check(i915->uncore.fw_domains_table,
				   i915->uncore.fw_domains_table_entries,
				   INTEL_GEN(i915) >= 9);
	if (err)
		return err;

	err = intel_uncore_check_forcewake_domains(i915);
	if (err)
		return err;

	return 0;
}

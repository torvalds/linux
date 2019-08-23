/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_lrc_reg.h"
#include "intel_sseu.h"

void intel_sseu_set_info(struct sseu_dev_info *sseu, u8 max_slices,
			 u8 max_subslices, u8 max_eus_per_subslice)
{
	sseu->max_slices = max_slices;
	sseu->max_subslices = max_subslices;
	sseu->max_eus_per_subslice = max_eus_per_subslice;

	sseu->ss_stride = GEN_SSEU_STRIDE(sseu->max_subslices);
	GEM_BUG_ON(sseu->ss_stride > GEN_MAX_SUBSLICE_STRIDE);
	sseu->eu_stride = GEN_SSEU_STRIDE(sseu->max_eus_per_subslice);
	GEM_BUG_ON(sseu->eu_stride > GEN_MAX_EU_STRIDE);
}

unsigned int
intel_sseu_subslice_total(const struct sseu_dev_info *sseu)
{
	unsigned int i, total = 0;

	for (i = 0; i < ARRAY_SIZE(sseu->subslice_mask); i++)
		total += hweight8(sseu->subslice_mask[i]);

	return total;
}

u32 intel_sseu_get_subslices(const struct sseu_dev_info *sseu, u8 slice)
{
	int i, offset = slice * sseu->ss_stride;
	u32 mask = 0;

	GEM_BUG_ON(slice >= sseu->max_slices);

	for (i = 0; i < sseu->ss_stride; i++)
		mask |= (u32)sseu->subslice_mask[offset + i] <<
			i * BITS_PER_BYTE;

	return mask;
}

void intel_sseu_set_subslices(struct sseu_dev_info *sseu, int slice,
			      u32 ss_mask)
{
	int offset = slice * sseu->ss_stride;

	memcpy(&sseu->subslice_mask[offset], &ss_mask, sseu->ss_stride);
}

unsigned int
intel_sseu_subslices_per_slice(const struct sseu_dev_info *sseu, u8 slice)
{
	return hweight32(intel_sseu_get_subslices(sseu, slice));
}

u32 intel_sseu_make_rpcs(struct drm_i915_private *i915,
			 const struct intel_sseu *req_sseu)
{
	const struct sseu_dev_info *sseu = &RUNTIME_INFO(i915)->sseu;
	bool subslice_pg = sseu->has_subslice_pg;
	struct intel_sseu ctx_sseu;
	u8 slices, subslices;
	u32 rpcs = 0;

	/*
	 * No explicit RPCS request is needed to ensure full
	 * slice/subslice/EU enablement prior to Gen9.
	 */
	if (INTEL_GEN(i915) < 9)
		return 0;

	/*
	 * If i915/perf is active, we want a stable powergating configuration
	 * on the system.
	 *
	 * We could choose full enablement, but on ICL we know there are use
	 * cases which disable slices for functional, apart for performance
	 * reasons. So in this case we select a known stable subset.
	 */
	if (!i915->perf.exclusive_stream) {
		ctx_sseu = *req_sseu;
	} else {
		ctx_sseu = intel_sseu_from_device_info(sseu);

		if (IS_GEN(i915, 11)) {
			/*
			 * We only need subslice count so it doesn't matter
			 * which ones we select - just turn off low bits in the
			 * amount of half of all available subslices per slice.
			 */
			ctx_sseu.subslice_mask =
				~(~0 << (hweight8(ctx_sseu.subslice_mask) / 2));
			ctx_sseu.slice_mask = 0x1;
		}
	}

	slices = hweight8(ctx_sseu.slice_mask);
	subslices = hweight8(ctx_sseu.subslice_mask);

	/*
	 * Since the SScount bitfield in GEN8_R_PWR_CLK_STATE is only three bits
	 * wide and Icelake has up to eight subslices, specfial programming is
	 * needed in order to correctly enable all subslices.
	 *
	 * According to documentation software must consider the configuration
	 * as 2x4x8 and hardware will translate this to 1x8x8.
	 *
	 * Furthemore, even though SScount is three bits, maximum documented
	 * value for it is four. From this some rules/restrictions follow:
	 *
	 * 1.
	 * If enabled subslice count is greater than four, two whole slices must
	 * be enabled instead.
	 *
	 * 2.
	 * When more than one slice is enabled, hardware ignores the subslice
	 * count altogether.
	 *
	 * From these restrictions it follows that it is not possible to enable
	 * a count of subslices between the SScount maximum of four restriction,
	 * and the maximum available number on a particular SKU. Either all
	 * subslices are enabled, or a count between one and four on the first
	 * slice.
	 */
	if (IS_GEN(i915, 11) &&
	    slices == 1 &&
	    subslices > min_t(u8, 4, hweight8(sseu->subslice_mask[0]) / 2)) {
		GEM_BUG_ON(subslices & 1);

		subslice_pg = false;
		slices *= 2;
	}

	/*
	 * Starting in Gen9, render power gating can leave
	 * slice/subslice/EU in a partially enabled state. We
	 * must make an explicit request through RPCS for full
	 * enablement.
	 */
	if (sseu->has_slice_pg) {
		u32 mask, val = slices;

		if (INTEL_GEN(i915) >= 11) {
			mask = GEN11_RPCS_S_CNT_MASK;
			val <<= GEN11_RPCS_S_CNT_SHIFT;
		} else {
			mask = GEN8_RPCS_S_CNT_MASK;
			val <<= GEN8_RPCS_S_CNT_SHIFT;
		}

		GEM_BUG_ON(val & ~mask);
		val &= mask;

		rpcs |= GEN8_RPCS_ENABLE | GEN8_RPCS_S_CNT_ENABLE | val;
	}

	if (subslice_pg) {
		u32 val = subslices;

		val <<= GEN8_RPCS_SS_CNT_SHIFT;

		GEM_BUG_ON(val & ~GEN8_RPCS_SS_CNT_MASK);
		val &= GEN8_RPCS_SS_CNT_MASK;

		rpcs |= GEN8_RPCS_ENABLE | GEN8_RPCS_SS_CNT_ENABLE | val;
	}

	if (sseu->has_eu_pg) {
		u32 val;

		val = ctx_sseu.min_eus_per_subslice << GEN8_RPCS_EU_MIN_SHIFT;
		GEM_BUG_ON(val & ~GEN8_RPCS_EU_MIN_MASK);
		val &= GEN8_RPCS_EU_MIN_MASK;

		rpcs |= val;

		val = ctx_sseu.max_eus_per_subslice << GEN8_RPCS_EU_MAX_SHIFT;
		GEM_BUG_ON(val & ~GEN8_RPCS_EU_MAX_MASK);
		val &= GEN8_RPCS_EU_MAX_MASK;

		rpcs |= val;

		rpcs |= GEN8_RPCS_ENABLE;
	}

	return rpcs;
}

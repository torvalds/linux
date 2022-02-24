// SPDX-License-Identifier: MIT
/*
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
			      u8 *subslice_mask, u32 ss_mask)
{
	int offset = slice * sseu->ss_stride;

	memcpy(&subslice_mask[offset], &ss_mask, sseu->ss_stride);
}

unsigned int
intel_sseu_subslices_per_slice(const struct sseu_dev_info *sseu, u8 slice)
{
	return hweight32(intel_sseu_get_subslices(sseu, slice));
}

static int sseu_eu_idx(const struct sseu_dev_info *sseu, int slice,
		       int subslice)
{
	int slice_stride = sseu->max_subslices * sseu->eu_stride;

	return slice * slice_stride + subslice * sseu->eu_stride;
}

static u16 sseu_get_eus(const struct sseu_dev_info *sseu, int slice,
			int subslice)
{
	int i, offset = sseu_eu_idx(sseu, slice, subslice);
	u16 eu_mask = 0;

	for (i = 0; i < sseu->eu_stride; i++)
		eu_mask |=
			((u16)sseu->eu_mask[offset + i]) << (i * BITS_PER_BYTE);

	return eu_mask;
}

static void sseu_set_eus(struct sseu_dev_info *sseu, int slice, int subslice,
			 u16 eu_mask)
{
	int i, offset = sseu_eu_idx(sseu, slice, subslice);

	for (i = 0; i < sseu->eu_stride; i++)
		sseu->eu_mask[offset + i] =
			(eu_mask >> (BITS_PER_BYTE * i)) & 0xff;
}

static u16 compute_eu_total(const struct sseu_dev_info *sseu)
{
	u16 i, total = 0;

	for (i = 0; i < ARRAY_SIZE(sseu->eu_mask); i++)
		total += hweight8(sseu->eu_mask[i]);

	return total;
}

static u32 get_ss_stride_mask(struct sseu_dev_info *sseu, u8 s, u32 ss_en)
{
	u32 ss_mask;

	ss_mask = ss_en >> (s * sseu->max_subslices);
	ss_mask &= GENMASK(sseu->max_subslices - 1, 0);

	return ss_mask;
}

static void gen11_compute_sseu_info(struct sseu_dev_info *sseu, u8 s_en,
				    u32 g_ss_en, u32 c_ss_en, u16 eu_en)
{
	int s, ss;

	/* g_ss_en/c_ss_en represent entire subslice mask across all slices */
	GEM_BUG_ON(sseu->max_slices * sseu->max_subslices >
		   sizeof(g_ss_en) * BITS_PER_BYTE);

	for (s = 0; s < sseu->max_slices; s++) {
		if ((s_en & BIT(s)) == 0)
			continue;

		sseu->slice_mask |= BIT(s);

		/*
		 * XeHP introduces the concept of compute vs geometry DSS. To
		 * reduce variation between GENs around subslice usage, store a
		 * mask for both the geometry and compute enabled masks since
		 * userspace will need to be able to query these masks
		 * independently.  Also compute a total enabled subslice count
		 * for the purposes of selecting subslices to use in a
		 * particular GEM context.
		 */
		intel_sseu_set_subslices(sseu, s, sseu->compute_subslice_mask,
					 get_ss_stride_mask(sseu, s, c_ss_en));
		intel_sseu_set_subslices(sseu, s, sseu->geometry_subslice_mask,
					 get_ss_stride_mask(sseu, s, g_ss_en));
		intel_sseu_set_subslices(sseu, s, sseu->subslice_mask,
					 get_ss_stride_mask(sseu, s,
							    g_ss_en | c_ss_en));

		for (ss = 0; ss < sseu->max_subslices; ss++)
			if (intel_sseu_has_subslice(sseu, s, ss))
				sseu_set_eus(sseu, s, ss, eu_en);
	}
	sseu->eu_per_subslice = hweight16(eu_en);
	sseu->eu_total = compute_eu_total(sseu);
}

static void gen12_sseu_info_init(struct intel_gt *gt)
{
	struct sseu_dev_info *sseu = &gt->info.sseu;
	struct intel_uncore *uncore = gt->uncore;
	u32 g_dss_en, c_dss_en = 0;
	u16 eu_en = 0;
	u8 eu_en_fuse;
	u8 s_en;
	int eu;

	/*
	 * Gen12 has Dual-Subslices, which behave similarly to 2 gen11 SS.
	 * Instead of splitting these, provide userspace with an array
	 * of DSS to more closely represent the hardware resource.
	 *
	 * In addition, the concept of slice has been removed in Xe_HP.
	 * To be compatible with prior generations, assume a single slice
	 * across the entire device. Then calculate out the DSS for each
	 * workload type within that software slice.
	 */
	if (IS_DG2(gt->i915) || IS_XEHPSDV(gt->i915))
		intel_sseu_set_info(sseu, 1, 32, 16);
	else
		intel_sseu_set_info(sseu, 1, 6, 16);

	/*
	 * As mentioned above, Xe_HP does not have the concept of a slice.
	 * Enable one for software backwards compatibility.
	 */
	if (GRAPHICS_VER_FULL(gt->i915) >= IP_VER(12, 50))
		s_en = 0x1;
	else
		s_en = intel_uncore_read(uncore, GEN11_GT_SLICE_ENABLE) &
		       GEN11_GT_S_ENA_MASK;

	g_dss_en = intel_uncore_read(uncore, GEN12_GT_GEOMETRY_DSS_ENABLE);
	if (GRAPHICS_VER_FULL(gt->i915) >= IP_VER(12, 50))
		c_dss_en = intel_uncore_read(uncore, GEN12_GT_COMPUTE_DSS_ENABLE);

	/* one bit per pair of EUs */
	if (GRAPHICS_VER_FULL(gt->i915) >= IP_VER(12, 50))
		eu_en_fuse = intel_uncore_read(uncore, XEHP_EU_ENABLE) & XEHP_EU_ENA_MASK;
	else
		eu_en_fuse = ~(intel_uncore_read(uncore, GEN11_EU_DISABLE) &
			       GEN11_EU_DIS_MASK);

	for (eu = 0; eu < sseu->max_eus_per_subslice / 2; eu++)
		if (eu_en_fuse & BIT(eu))
			eu_en |= BIT(eu * 2) | BIT(eu * 2 + 1);

	gen11_compute_sseu_info(sseu, s_en, g_dss_en, c_dss_en, eu_en);

	/* TGL only supports slice-level power gating */
	sseu->has_slice_pg = 1;
}

static void gen11_sseu_info_init(struct intel_gt *gt)
{
	struct sseu_dev_info *sseu = &gt->info.sseu;
	struct intel_uncore *uncore = gt->uncore;
	u32 ss_en;
	u8 eu_en;
	u8 s_en;

	if (IS_JSL_EHL(gt->i915))
		intel_sseu_set_info(sseu, 1, 4, 8);
	else
		intel_sseu_set_info(sseu, 1, 8, 8);

	s_en = intel_uncore_read(uncore, GEN11_GT_SLICE_ENABLE) &
		GEN11_GT_S_ENA_MASK;
	ss_en = ~intel_uncore_read(uncore, GEN11_GT_SUBSLICE_DISABLE);

	eu_en = ~(intel_uncore_read(uncore, GEN11_EU_DISABLE) &
		  GEN11_EU_DIS_MASK);

	gen11_compute_sseu_info(sseu, s_en, ss_en, 0, eu_en);

	/* ICL has no power gating restrictions. */
	sseu->has_slice_pg = 1;
	sseu->has_subslice_pg = 1;
	sseu->has_eu_pg = 1;
}

static void cherryview_sseu_info_init(struct intel_gt *gt)
{
	struct sseu_dev_info *sseu = &gt->info.sseu;
	u32 fuse;
	u8 subslice_mask = 0;

	fuse = intel_uncore_read(gt->uncore, CHV_FUSE_GT);

	sseu->slice_mask = BIT(0);
	intel_sseu_set_info(sseu, 1, 2, 8);

	if (!(fuse & CHV_FGT_DISABLE_SS0)) {
		u8 disabled_mask =
			((fuse & CHV_FGT_EU_DIS_SS0_R0_MASK) >>
			 CHV_FGT_EU_DIS_SS0_R0_SHIFT) |
			(((fuse & CHV_FGT_EU_DIS_SS0_R1_MASK) >>
			  CHV_FGT_EU_DIS_SS0_R1_SHIFT) << 4);

		subslice_mask |= BIT(0);
		sseu_set_eus(sseu, 0, 0, ~disabled_mask);
	}

	if (!(fuse & CHV_FGT_DISABLE_SS1)) {
		u8 disabled_mask =
			((fuse & CHV_FGT_EU_DIS_SS1_R0_MASK) >>
			 CHV_FGT_EU_DIS_SS1_R0_SHIFT) |
			(((fuse & CHV_FGT_EU_DIS_SS1_R1_MASK) >>
			  CHV_FGT_EU_DIS_SS1_R1_SHIFT) << 4);

		subslice_mask |= BIT(1);
		sseu_set_eus(sseu, 0, 1, ~disabled_mask);
	}

	intel_sseu_set_subslices(sseu, 0, sseu->subslice_mask, subslice_mask);

	sseu->eu_total = compute_eu_total(sseu);

	/*
	 * CHV expected to always have a uniform distribution of EU
	 * across subslices.
	 */
	sseu->eu_per_subslice = intel_sseu_subslice_total(sseu) ?
		sseu->eu_total /
		intel_sseu_subslice_total(sseu) :
		0;
	/*
	 * CHV supports subslice power gating on devices with more than
	 * one subslice, and supports EU power gating on devices with
	 * more than one EU pair per subslice.
	 */
	sseu->has_slice_pg = 0;
	sseu->has_subslice_pg = intel_sseu_subslice_total(sseu) > 1;
	sseu->has_eu_pg = (sseu->eu_per_subslice > 2);
}

static void gen9_sseu_info_init(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	struct intel_device_info *info = mkwrite_device_info(i915);
	struct sseu_dev_info *sseu = &gt->info.sseu;
	struct intel_uncore *uncore = gt->uncore;
	u32 fuse2, eu_disable, subslice_mask;
	const u8 eu_mask = 0xff;
	int s, ss;

	fuse2 = intel_uncore_read(uncore, GEN8_FUSE2);
	sseu->slice_mask = (fuse2 & GEN8_F2_S_ENA_MASK) >> GEN8_F2_S_ENA_SHIFT;

	/* BXT has a single slice and at most 3 subslices. */
	intel_sseu_set_info(sseu, IS_GEN9_LP(i915) ? 1 : 3,
			    IS_GEN9_LP(i915) ? 3 : 4, 8);

	/*
	 * The subslice disable field is global, i.e. it applies
	 * to each of the enabled slices.
	 */
	subslice_mask = (1 << sseu->max_subslices) - 1;
	subslice_mask &= ~((fuse2 & GEN9_F2_SS_DIS_MASK) >>
			   GEN9_F2_SS_DIS_SHIFT);

	/*
	 * Iterate through enabled slices and subslices to
	 * count the total enabled EU.
	 */
	for (s = 0; s < sseu->max_slices; s++) {
		if (!(sseu->slice_mask & BIT(s)))
			/* skip disabled slice */
			continue;

		intel_sseu_set_subslices(sseu, s, sseu->subslice_mask,
					 subslice_mask);

		eu_disable = intel_uncore_read(uncore, GEN9_EU_DISABLE(s));
		for (ss = 0; ss < sseu->max_subslices; ss++) {
			int eu_per_ss;
			u8 eu_disabled_mask;

			if (!intel_sseu_has_subslice(sseu, s, ss))
				/* skip disabled subslice */
				continue;

			eu_disabled_mask = (eu_disable >> (ss * 8)) & eu_mask;

			sseu_set_eus(sseu, s, ss, ~eu_disabled_mask);

			eu_per_ss = sseu->max_eus_per_subslice -
				hweight8(eu_disabled_mask);

			/*
			 * Record which subslice(s) has(have) 7 EUs. we
			 * can tune the hash used to spread work among
			 * subslices if they are unbalanced.
			 */
			if (eu_per_ss == 7)
				sseu->subslice_7eu[s] |= BIT(ss);
		}
	}

	sseu->eu_total = compute_eu_total(sseu);

	/*
	 * SKL is expected to always have a uniform distribution
	 * of EU across subslices with the exception that any one
	 * EU in any one subslice may be fused off for die
	 * recovery. BXT is expected to be perfectly uniform in EU
	 * distribution.
	 */
	sseu->eu_per_subslice =
		intel_sseu_subslice_total(sseu) ?
		DIV_ROUND_UP(sseu->eu_total, intel_sseu_subslice_total(sseu)) :
		0;

	/*
	 * SKL+ supports slice power gating on devices with more than
	 * one slice, and supports EU power gating on devices with
	 * more than one EU pair per subslice. BXT+ supports subslice
	 * power gating on devices with more than one subslice, and
	 * supports EU power gating on devices with more than one EU
	 * pair per subslice.
	 */
	sseu->has_slice_pg =
		!IS_GEN9_LP(i915) && hweight8(sseu->slice_mask) > 1;
	sseu->has_subslice_pg =
		IS_GEN9_LP(i915) && intel_sseu_subslice_total(sseu) > 1;
	sseu->has_eu_pg = sseu->eu_per_subslice > 2;

	if (IS_GEN9_LP(i915)) {
#define IS_SS_DISABLED(ss)	(!(sseu->subslice_mask[0] & BIT(ss)))
		info->has_pooled_eu = hweight8(sseu->subslice_mask[0]) == 3;

		sseu->min_eu_in_pool = 0;
		if (info->has_pooled_eu) {
			if (IS_SS_DISABLED(2) || IS_SS_DISABLED(0))
				sseu->min_eu_in_pool = 3;
			else if (IS_SS_DISABLED(1))
				sseu->min_eu_in_pool = 6;
			else
				sseu->min_eu_in_pool = 9;
		}
#undef IS_SS_DISABLED
	}
}

static void bdw_sseu_info_init(struct intel_gt *gt)
{
	struct sseu_dev_info *sseu = &gt->info.sseu;
	struct intel_uncore *uncore = gt->uncore;
	int s, ss;
	u32 fuse2, subslice_mask, eu_disable[3]; /* s_max */
	u32 eu_disable0, eu_disable1, eu_disable2;

	fuse2 = intel_uncore_read(uncore, GEN8_FUSE2);
	sseu->slice_mask = (fuse2 & GEN8_F2_S_ENA_MASK) >> GEN8_F2_S_ENA_SHIFT;
	intel_sseu_set_info(sseu, 3, 3, 8);

	/*
	 * The subslice disable field is global, i.e. it applies
	 * to each of the enabled slices.
	 */
	subslice_mask = GENMASK(sseu->max_subslices - 1, 0);
	subslice_mask &= ~((fuse2 & GEN8_F2_SS_DIS_MASK) >>
			   GEN8_F2_SS_DIS_SHIFT);
	eu_disable0 = intel_uncore_read(uncore, GEN8_EU_DISABLE0);
	eu_disable1 = intel_uncore_read(uncore, GEN8_EU_DISABLE1);
	eu_disable2 = intel_uncore_read(uncore, GEN8_EU_DISABLE2);
	eu_disable[0] = eu_disable0 & GEN8_EU_DIS0_S0_MASK;
	eu_disable[1] = (eu_disable0 >> GEN8_EU_DIS0_S1_SHIFT) |
		((eu_disable1 & GEN8_EU_DIS1_S1_MASK) <<
		 (32 - GEN8_EU_DIS0_S1_SHIFT));
	eu_disable[2] = (eu_disable1 >> GEN8_EU_DIS1_S2_SHIFT) |
		((eu_disable2 & GEN8_EU_DIS2_S2_MASK) <<
		 (32 - GEN8_EU_DIS1_S2_SHIFT));

	/*
	 * Iterate through enabled slices and subslices to
	 * count the total enabled EU.
	 */
	for (s = 0; s < sseu->max_slices; s++) {
		if (!(sseu->slice_mask & BIT(s)))
			/* skip disabled slice */
			continue;

		intel_sseu_set_subslices(sseu, s, sseu->subslice_mask,
					 subslice_mask);

		for (ss = 0; ss < sseu->max_subslices; ss++) {
			u8 eu_disabled_mask;
			u32 n_disabled;

			if (!intel_sseu_has_subslice(sseu, s, ss))
				/* skip disabled subslice */
				continue;

			eu_disabled_mask =
				eu_disable[s] >> (ss * sseu->max_eus_per_subslice);

			sseu_set_eus(sseu, s, ss, ~eu_disabled_mask);

			n_disabled = hweight8(eu_disabled_mask);

			/*
			 * Record which subslices have 7 EUs.
			 */
			if (sseu->max_eus_per_subslice - n_disabled == 7)
				sseu->subslice_7eu[s] |= 1 << ss;
		}
	}

	sseu->eu_total = compute_eu_total(sseu);

	/*
	 * BDW is expected to always have a uniform distribution of EU across
	 * subslices with the exception that any one EU in any one subslice may
	 * be fused off for die recovery.
	 */
	sseu->eu_per_subslice =
		intel_sseu_subslice_total(sseu) ?
		DIV_ROUND_UP(sseu->eu_total, intel_sseu_subslice_total(sseu)) :
		0;

	/*
	 * BDW supports slice power gating on devices with more than
	 * one slice.
	 */
	sseu->has_slice_pg = hweight8(sseu->slice_mask) > 1;
	sseu->has_subslice_pg = 0;
	sseu->has_eu_pg = 0;
}

static void hsw_sseu_info_init(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	struct sseu_dev_info *sseu = &gt->info.sseu;
	u32 fuse1;
	u8 subslice_mask = 0;
	int s, ss;

	/*
	 * There isn't a register to tell us how many slices/subslices. We
	 * work off the PCI-ids here.
	 */
	switch (INTEL_INFO(i915)->gt) {
	default:
		MISSING_CASE(INTEL_INFO(i915)->gt);
		fallthrough;
	case 1:
		sseu->slice_mask = BIT(0);
		subslice_mask = BIT(0);
		break;
	case 2:
		sseu->slice_mask = BIT(0);
		subslice_mask = BIT(0) | BIT(1);
		break;
	case 3:
		sseu->slice_mask = BIT(0) | BIT(1);
		subslice_mask = BIT(0) | BIT(1);
		break;
	}

	fuse1 = intel_uncore_read(gt->uncore, HSW_PAVP_FUSE1);
	switch (REG_FIELD_GET(HSW_F1_EU_DIS_MASK, fuse1)) {
	default:
		MISSING_CASE(REG_FIELD_GET(HSW_F1_EU_DIS_MASK, fuse1));
		fallthrough;
	case HSW_F1_EU_DIS_10EUS:
		sseu->eu_per_subslice = 10;
		break;
	case HSW_F1_EU_DIS_8EUS:
		sseu->eu_per_subslice = 8;
		break;
	case HSW_F1_EU_DIS_6EUS:
		sseu->eu_per_subslice = 6;
		break;
	}

	intel_sseu_set_info(sseu, hweight8(sseu->slice_mask),
			    hweight8(subslice_mask),
			    sseu->eu_per_subslice);

	for (s = 0; s < sseu->max_slices; s++) {
		intel_sseu_set_subslices(sseu, s, sseu->subslice_mask,
					 subslice_mask);

		for (ss = 0; ss < sseu->max_subslices; ss++) {
			sseu_set_eus(sseu, s, ss,
				     (1UL << sseu->eu_per_subslice) - 1);
		}
	}

	sseu->eu_total = compute_eu_total(sseu);

	/* No powergating for you. */
	sseu->has_slice_pg = 0;
	sseu->has_subslice_pg = 0;
	sseu->has_eu_pg = 0;
}

void intel_sseu_info_init(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;

	if (IS_HASWELL(i915))
		hsw_sseu_info_init(gt);
	else if (IS_CHERRYVIEW(i915))
		cherryview_sseu_info_init(gt);
	else if (IS_BROADWELL(i915))
		bdw_sseu_info_init(gt);
	else if (GRAPHICS_VER(i915) == 9)
		gen9_sseu_info_init(gt);
	else if (GRAPHICS_VER(i915) == 11)
		gen11_sseu_info_init(gt);
	else if (GRAPHICS_VER(i915) >= 12)
		gen12_sseu_info_init(gt);
}

u32 intel_sseu_make_rpcs(struct intel_gt *gt,
			 const struct intel_sseu *req_sseu)
{
	struct drm_i915_private *i915 = gt->i915;
	const struct sseu_dev_info *sseu = &gt->info.sseu;
	bool subslice_pg = sseu->has_subslice_pg;
	u8 slices, subslices;
	u32 rpcs = 0;

	/*
	 * No explicit RPCS request is needed to ensure full
	 * slice/subslice/EU enablement prior to Gen9.
	 */
	if (GRAPHICS_VER(i915) < 9)
		return 0;

	/*
	 * If i915/perf is active, we want a stable powergating configuration
	 * on the system. Use the configuration pinned by i915/perf.
	 */
	if (i915->perf.exclusive_stream)
		req_sseu = &i915->perf.sseu;

	slices = hweight8(req_sseu->slice_mask);
	subslices = hweight8(req_sseu->subslice_mask);

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
	if (GRAPHICS_VER(i915) == 11 &&
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

		if (GRAPHICS_VER(i915) >= 11) {
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

		val = req_sseu->min_eus_per_subslice << GEN8_RPCS_EU_MIN_SHIFT;
		GEM_BUG_ON(val & ~GEN8_RPCS_EU_MIN_MASK);
		val &= GEN8_RPCS_EU_MIN_MASK;

		rpcs |= val;

		val = req_sseu->max_eus_per_subslice << GEN8_RPCS_EU_MAX_SHIFT;
		GEM_BUG_ON(val & ~GEN8_RPCS_EU_MAX_MASK);
		val &= GEN8_RPCS_EU_MAX_MASK;

		rpcs |= val;

		rpcs |= GEN8_RPCS_ENABLE;
	}

	return rpcs;
}

void intel_sseu_dump(const struct sseu_dev_info *sseu, struct drm_printer *p)
{
	int s;

	drm_printf(p, "slice total: %u, mask=%04x\n",
		   hweight8(sseu->slice_mask), sseu->slice_mask);
	drm_printf(p, "subslice total: %u\n", intel_sseu_subslice_total(sseu));
	for (s = 0; s < sseu->max_slices; s++) {
		drm_printf(p, "slice%d: %u subslices, mask=%08x\n",
			   s, intel_sseu_subslices_per_slice(sseu, s),
			   intel_sseu_get_subslices(sseu, s));
	}
	drm_printf(p, "EU total: %u\n", sseu->eu_total);
	drm_printf(p, "EU per subslice: %u\n", sseu->eu_per_subslice);
	drm_printf(p, "has slice power gating: %s\n",
		   yesno(sseu->has_slice_pg));
	drm_printf(p, "has subslice power gating: %s\n",
		   yesno(sseu->has_subslice_pg));
	drm_printf(p, "has EU power gating: %s\n", yesno(sseu->has_eu_pg));
}

void intel_sseu_print_topology(const struct sseu_dev_info *sseu,
			       struct drm_printer *p)
{
	int s, ss;

	if (sseu->max_slices == 0) {
		drm_printf(p, "Unavailable\n");
		return;
	}

	for (s = 0; s < sseu->max_slices; s++) {
		drm_printf(p, "slice%d: %u subslice(s) (0x%08x):\n",
			   s, intel_sseu_subslices_per_slice(sseu, s),
			   intel_sseu_get_subslices(sseu, s));

		for (ss = 0; ss < sseu->max_subslices; ss++) {
			u16 enabled_eus = sseu_get_eus(sseu, s, ss);

			drm_printf(p, "\tsubslice%d: %u EUs (0x%hx)\n",
				   ss, hweight16(enabled_eus), enabled_eus);
		}
	}
}

u16 intel_slicemask_from_dssmask(u64 dss_mask, int dss_per_slice)
{
	u16 slice_mask = 0;
	int i;

	WARN_ON(sizeof(dss_mask) * 8 / dss_per_slice > 8 * sizeof(slice_mask));

	for (i = 0; dss_mask; i++) {
		if (dss_mask & GENMASK(dss_per_slice - 1, 0))
			slice_mask |= BIT(i);

		dss_mask >>= dss_per_slice;
	}

	return slice_mask;
}


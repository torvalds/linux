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

#include <drm/drm_print.h>

#include "intel_device_info.h"
#include "i915_drv.h"

#define PLATFORM_NAME(x) [INTEL_##x] = #x
static const char * const platform_names[] = {
	PLATFORM_NAME(I830),
	PLATFORM_NAME(I845G),
	PLATFORM_NAME(I85X),
	PLATFORM_NAME(I865G),
	PLATFORM_NAME(I915G),
	PLATFORM_NAME(I915GM),
	PLATFORM_NAME(I945G),
	PLATFORM_NAME(I945GM),
	PLATFORM_NAME(G33),
	PLATFORM_NAME(PINEVIEW),
	PLATFORM_NAME(I965G),
	PLATFORM_NAME(I965GM),
	PLATFORM_NAME(G45),
	PLATFORM_NAME(GM45),
	PLATFORM_NAME(IRONLAKE),
	PLATFORM_NAME(SANDYBRIDGE),
	PLATFORM_NAME(IVYBRIDGE),
	PLATFORM_NAME(VALLEYVIEW),
	PLATFORM_NAME(HASWELL),
	PLATFORM_NAME(BROADWELL),
	PLATFORM_NAME(CHERRYVIEW),
	PLATFORM_NAME(SKYLAKE),
	PLATFORM_NAME(BROXTON),
	PLATFORM_NAME(KABYLAKE),
	PLATFORM_NAME(GEMINILAKE),
	PLATFORM_NAME(COFFEELAKE),
	PLATFORM_NAME(CANNONLAKE),
	PLATFORM_NAME(ICELAKE),
	PLATFORM_NAME(ELKHARTLAKE),
};
#undef PLATFORM_NAME

const char *intel_platform_name(enum intel_platform platform)
{
	BUILD_BUG_ON(ARRAY_SIZE(platform_names) != INTEL_MAX_PLATFORMS);

	if (WARN_ON_ONCE(platform >= ARRAY_SIZE(platform_names) ||
			 platform_names[platform] == NULL))
		return "<unknown>";

	return platform_names[platform];
}

void intel_device_info_dump_flags(const struct intel_device_info *info,
				  struct drm_printer *p)
{
#define PRINT_FLAG(name) drm_printf(p, "%s: %s\n", #name, yesno(info->name));
	DEV_INFO_FOR_EACH_FLAG(PRINT_FLAG);
#undef PRINT_FLAG

#define PRINT_FLAG(name) drm_printf(p, "%s: %s\n", #name, yesno(info->display.name));
	DEV_INFO_DISPLAY_FOR_EACH_FLAG(PRINT_FLAG);
#undef PRINT_FLAG
}

static void sseu_dump(const struct sseu_dev_info *sseu, struct drm_printer *p)
{
	int s;

	drm_printf(p, "slice total: %u, mask=%04x\n",
		   hweight8(sseu->slice_mask), sseu->slice_mask);
	drm_printf(p, "subslice total: %u\n", sseu_subslice_total(sseu));
	for (s = 0; s < sseu->max_slices; s++) {
		drm_printf(p, "slice%d: %u subslices, mask=%04x\n",
			   s, hweight8(sseu->subslice_mask[s]),
			   sseu->subslice_mask[s]);
	}
	drm_printf(p, "EU total: %u\n", sseu->eu_total);
	drm_printf(p, "EU per subslice: %u\n", sseu->eu_per_subslice);
	drm_printf(p, "has slice power gating: %s\n",
		   yesno(sseu->has_slice_pg));
	drm_printf(p, "has subslice power gating: %s\n",
		   yesno(sseu->has_subslice_pg));
	drm_printf(p, "has EU power gating: %s\n", yesno(sseu->has_eu_pg));
}

void intel_device_info_dump_runtime(const struct intel_runtime_info *info,
				    struct drm_printer *p)
{
	sseu_dump(&info->sseu, p);

	drm_printf(p, "CS timestamp frequency: %u kHz\n",
		   info->cs_timestamp_frequency_khz);
}

void intel_device_info_dump_topology(const struct sseu_dev_info *sseu,
				     struct drm_printer *p)
{
	int s, ss;

	if (sseu->max_slices == 0) {
		drm_printf(p, "Unavailable\n");
		return;
	}

	for (s = 0; s < sseu->max_slices; s++) {
		drm_printf(p, "slice%d: %u subslice(s) (0x%hhx):\n",
			   s, hweight8(sseu->subslice_mask[s]),
			   sseu->subslice_mask[s]);

		for (ss = 0; ss < sseu->max_subslices; ss++) {
			u16 enabled_eus = sseu_get_eus(sseu, s, ss);

			drm_printf(p, "\tsubslice%d: %u EUs (0x%hx)\n",
				   ss, hweight16(enabled_eus), enabled_eus);
		}
	}
}

static u16 compute_eu_total(const struct sseu_dev_info *sseu)
{
	u16 i, total = 0;

	for (i = 0; i < ARRAY_SIZE(sseu->eu_mask); i++)
		total += hweight8(sseu->eu_mask[i]);

	return total;
}

static void gen11_sseu_info_init(struct drm_i915_private *dev_priv)
{
	struct sseu_dev_info *sseu = &RUNTIME_INFO(dev_priv)->sseu;
	u8 s_en;
	u32 ss_en, ss_en_mask;
	u8 eu_en;
	int s;

	if (IS_ELKHARTLAKE(dev_priv)) {
		sseu->max_slices = 1;
		sseu->max_subslices = 4;
		sseu->max_eus_per_subslice = 8;
	} else {
		sseu->max_slices = 1;
		sseu->max_subslices = 8;
		sseu->max_eus_per_subslice = 8;
	}

	s_en = I915_READ(GEN11_GT_SLICE_ENABLE) & GEN11_GT_S_ENA_MASK;
	ss_en = ~I915_READ(GEN11_GT_SUBSLICE_DISABLE);
	ss_en_mask = BIT(sseu->max_subslices) - 1;
	eu_en = ~(I915_READ(GEN11_EU_DISABLE) & GEN11_EU_DIS_MASK);

	for (s = 0; s < sseu->max_slices; s++) {
		if (s_en & BIT(s)) {
			int ss_idx = sseu->max_subslices * s;
			int ss;

			sseu->slice_mask |= BIT(s);
			sseu->subslice_mask[s] = (ss_en >> ss_idx) & ss_en_mask;
			for (ss = 0; ss < sseu->max_subslices; ss++) {
				if (sseu->subslice_mask[s] & BIT(ss))
					sseu_set_eus(sseu, s, ss, eu_en);
			}
		}
	}
	sseu->eu_per_subslice = hweight8(eu_en);
	sseu->eu_total = compute_eu_total(sseu);

	/* ICL has no power gating restrictions. */
	sseu->has_slice_pg = 1;
	sseu->has_subslice_pg = 1;
	sseu->has_eu_pg = 1;
}

static void gen10_sseu_info_init(struct drm_i915_private *dev_priv)
{
	struct sseu_dev_info *sseu = &RUNTIME_INFO(dev_priv)->sseu;
	const u32 fuse2 = I915_READ(GEN8_FUSE2);
	int s, ss;
	const int eu_mask = 0xff;
	u32 subslice_mask, eu_en;

	sseu->slice_mask = (fuse2 & GEN10_F2_S_ENA_MASK) >>
			    GEN10_F2_S_ENA_SHIFT;
	sseu->max_slices = 6;
	sseu->max_subslices = 4;
	sseu->max_eus_per_subslice = 8;

	subslice_mask = (1 << 4) - 1;
	subslice_mask &= ~((fuse2 & GEN10_F2_SS_DIS_MASK) >>
			   GEN10_F2_SS_DIS_SHIFT);

	/*
	 * Slice0 can have up to 3 subslices, but there are only 2 in
	 * slice1/2.
	 */
	sseu->subslice_mask[0] = subslice_mask;
	for (s = 1; s < sseu->max_slices; s++)
		sseu->subslice_mask[s] = subslice_mask & 0x3;

	/* Slice0 */
	eu_en = ~I915_READ(GEN8_EU_DISABLE0);
	for (ss = 0; ss < sseu->max_subslices; ss++)
		sseu_set_eus(sseu, 0, ss, (eu_en >> (8 * ss)) & eu_mask);
	/* Slice1 */
	sseu_set_eus(sseu, 1, 0, (eu_en >> 24) & eu_mask);
	eu_en = ~I915_READ(GEN8_EU_DISABLE1);
	sseu_set_eus(sseu, 1, 1, eu_en & eu_mask);
	/* Slice2 */
	sseu_set_eus(sseu, 2, 0, (eu_en >> 8) & eu_mask);
	sseu_set_eus(sseu, 2, 1, (eu_en >> 16) & eu_mask);
	/* Slice3 */
	sseu_set_eus(sseu, 3, 0, (eu_en >> 24) & eu_mask);
	eu_en = ~I915_READ(GEN8_EU_DISABLE2);
	sseu_set_eus(sseu, 3, 1, eu_en & eu_mask);
	/* Slice4 */
	sseu_set_eus(sseu, 4, 0, (eu_en >> 8) & eu_mask);
	sseu_set_eus(sseu, 4, 1, (eu_en >> 16) & eu_mask);
	/* Slice5 */
	sseu_set_eus(sseu, 5, 0, (eu_en >> 24) & eu_mask);
	eu_en = ~I915_READ(GEN10_EU_DISABLE3);
	sseu_set_eus(sseu, 5, 1, eu_en & eu_mask);

	/* Do a second pass where we mark the subslices disabled if all their
	 * eus are off.
	 */
	for (s = 0; s < sseu->max_slices; s++) {
		for (ss = 0; ss < sseu->max_subslices; ss++) {
			if (sseu_get_eus(sseu, s, ss) == 0)
				sseu->subslice_mask[s] &= ~BIT(ss);
		}
	}

	sseu->eu_total = compute_eu_total(sseu);

	/*
	 * CNL is expected to always have a uniform distribution
	 * of EU across subslices with the exception that any one
	 * EU in any one subslice may be fused off for die
	 * recovery.
	 */
	sseu->eu_per_subslice = sseu_subslice_total(sseu) ?
				DIV_ROUND_UP(sseu->eu_total,
					     sseu_subslice_total(sseu)) : 0;

	/* No restrictions on Power Gating */
	sseu->has_slice_pg = 1;
	sseu->has_subslice_pg = 1;
	sseu->has_eu_pg = 1;
}

static void cherryview_sseu_info_init(struct drm_i915_private *dev_priv)
{
	struct sseu_dev_info *sseu = &RUNTIME_INFO(dev_priv)->sseu;
	u32 fuse;

	fuse = I915_READ(CHV_FUSE_GT);

	sseu->slice_mask = BIT(0);
	sseu->max_slices = 1;
	sseu->max_subslices = 2;
	sseu->max_eus_per_subslice = 8;

	if (!(fuse & CHV_FGT_DISABLE_SS0)) {
		u8 disabled_mask =
			((fuse & CHV_FGT_EU_DIS_SS0_R0_MASK) >>
			 CHV_FGT_EU_DIS_SS0_R0_SHIFT) |
			(((fuse & CHV_FGT_EU_DIS_SS0_R1_MASK) >>
			  CHV_FGT_EU_DIS_SS0_R1_SHIFT) << 4);

		sseu->subslice_mask[0] |= BIT(0);
		sseu_set_eus(sseu, 0, 0, ~disabled_mask);
	}

	if (!(fuse & CHV_FGT_DISABLE_SS1)) {
		u8 disabled_mask =
			((fuse & CHV_FGT_EU_DIS_SS1_R0_MASK) >>
			 CHV_FGT_EU_DIS_SS1_R0_SHIFT) |
			(((fuse & CHV_FGT_EU_DIS_SS1_R1_MASK) >>
			  CHV_FGT_EU_DIS_SS1_R1_SHIFT) << 4);

		sseu->subslice_mask[0] |= BIT(1);
		sseu_set_eus(sseu, 0, 1, ~disabled_mask);
	}

	sseu->eu_total = compute_eu_total(sseu);

	/*
	 * CHV expected to always have a uniform distribution of EU
	 * across subslices.
	*/
	sseu->eu_per_subslice = sseu_subslice_total(sseu) ?
				sseu->eu_total / sseu_subslice_total(sseu) :
				0;
	/*
	 * CHV supports subslice power gating on devices with more than
	 * one subslice, and supports EU power gating on devices with
	 * more than one EU pair per subslice.
	*/
	sseu->has_slice_pg = 0;
	sseu->has_subslice_pg = sseu_subslice_total(sseu) > 1;
	sseu->has_eu_pg = (sseu->eu_per_subslice > 2);
}

static void gen9_sseu_info_init(struct drm_i915_private *dev_priv)
{
	struct intel_device_info *info = mkwrite_device_info(dev_priv);
	struct sseu_dev_info *sseu = &RUNTIME_INFO(dev_priv)->sseu;
	int s, ss;
	u32 fuse2, eu_disable, subslice_mask;
	const u8 eu_mask = 0xff;

	fuse2 = I915_READ(GEN8_FUSE2);
	sseu->slice_mask = (fuse2 & GEN8_F2_S_ENA_MASK) >> GEN8_F2_S_ENA_SHIFT;

	/* BXT has a single slice and at most 3 subslices. */
	sseu->max_slices = IS_GEN9_LP(dev_priv) ? 1 : 3;
	sseu->max_subslices = IS_GEN9_LP(dev_priv) ? 3 : 4;
	sseu->max_eus_per_subslice = 8;

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

		sseu->subslice_mask[s] = subslice_mask;

		eu_disable = I915_READ(GEN9_EU_DISABLE(s));
		for (ss = 0; ss < sseu->max_subslices; ss++) {
			int eu_per_ss;
			u8 eu_disabled_mask;

			if (!(sseu->subslice_mask[s] & BIT(ss)))
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
	sseu->eu_per_subslice = sseu_subslice_total(sseu) ?
				DIV_ROUND_UP(sseu->eu_total,
					     sseu_subslice_total(sseu)) : 0;
	/*
	 * SKL+ supports slice power gating on devices with more than
	 * one slice, and supports EU power gating on devices with
	 * more than one EU pair per subslice. BXT+ supports subslice
	 * power gating on devices with more than one subslice, and
	 * supports EU power gating on devices with more than one EU
	 * pair per subslice.
	*/
	sseu->has_slice_pg =
		!IS_GEN9_LP(dev_priv) && hweight8(sseu->slice_mask) > 1;
	sseu->has_subslice_pg =
		IS_GEN9_LP(dev_priv) && sseu_subslice_total(sseu) > 1;
	sseu->has_eu_pg = sseu->eu_per_subslice > 2;

	if (IS_GEN9_LP(dev_priv)) {
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

static void broadwell_sseu_info_init(struct drm_i915_private *dev_priv)
{
	struct sseu_dev_info *sseu = &RUNTIME_INFO(dev_priv)->sseu;
	int s, ss;
	u32 fuse2, subslice_mask, eu_disable[3]; /* s_max */

	fuse2 = I915_READ(GEN8_FUSE2);
	sseu->slice_mask = (fuse2 & GEN8_F2_S_ENA_MASK) >> GEN8_F2_S_ENA_SHIFT;
	sseu->max_slices = 3;
	sseu->max_subslices = 3;
	sseu->max_eus_per_subslice = 8;

	/*
	 * The subslice disable field is global, i.e. it applies
	 * to each of the enabled slices.
	 */
	subslice_mask = GENMASK(sseu->max_subslices - 1, 0);
	subslice_mask &= ~((fuse2 & GEN8_F2_SS_DIS_MASK) >>
			   GEN8_F2_SS_DIS_SHIFT);

	eu_disable[0] = I915_READ(GEN8_EU_DISABLE0) & GEN8_EU_DIS0_S0_MASK;
	eu_disable[1] = (I915_READ(GEN8_EU_DISABLE0) >> GEN8_EU_DIS0_S1_SHIFT) |
			((I915_READ(GEN8_EU_DISABLE1) & GEN8_EU_DIS1_S1_MASK) <<
			 (32 - GEN8_EU_DIS0_S1_SHIFT));
	eu_disable[2] = (I915_READ(GEN8_EU_DISABLE1) >> GEN8_EU_DIS1_S2_SHIFT) |
			((I915_READ(GEN8_EU_DISABLE2) & GEN8_EU_DIS2_S2_MASK) <<
			 (32 - GEN8_EU_DIS1_S2_SHIFT));

	/*
	 * Iterate through enabled slices and subslices to
	 * count the total enabled EU.
	 */
	for (s = 0; s < sseu->max_slices; s++) {
		if (!(sseu->slice_mask & BIT(s)))
			/* skip disabled slice */
			continue;

		sseu->subslice_mask[s] = subslice_mask;

		for (ss = 0; ss < sseu->max_subslices; ss++) {
			u8 eu_disabled_mask;
			u32 n_disabled;

			if (!(sseu->subslice_mask[s] & BIT(ss)))
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
	sseu->eu_per_subslice = sseu_subslice_total(sseu) ?
				DIV_ROUND_UP(sseu->eu_total,
					     sseu_subslice_total(sseu)) : 0;

	/*
	 * BDW supports slice power gating on devices with more than
	 * one slice.
	 */
	sseu->has_slice_pg = hweight8(sseu->slice_mask) > 1;
	sseu->has_subslice_pg = 0;
	sseu->has_eu_pg = 0;
}

static void haswell_sseu_info_init(struct drm_i915_private *dev_priv)
{
	struct sseu_dev_info *sseu = &RUNTIME_INFO(dev_priv)->sseu;
	u32 fuse1;
	int s, ss;

	/*
	 * There isn't a register to tell us how many slices/subslices. We
	 * work off the PCI-ids here.
	 */
	switch (INTEL_INFO(dev_priv)->gt) {
	default:
		MISSING_CASE(INTEL_INFO(dev_priv)->gt);
		/* fall through */
	case 1:
		sseu->slice_mask = BIT(0);
		sseu->subslice_mask[0] = BIT(0);
		break;
	case 2:
		sseu->slice_mask = BIT(0);
		sseu->subslice_mask[0] = BIT(0) | BIT(1);
		break;
	case 3:
		sseu->slice_mask = BIT(0) | BIT(1);
		sseu->subslice_mask[0] = BIT(0) | BIT(1);
		sseu->subslice_mask[1] = BIT(0) | BIT(1);
		break;
	}

	sseu->max_slices = hweight8(sseu->slice_mask);
	sseu->max_subslices = hweight8(sseu->subslice_mask[0]);

	fuse1 = I915_READ(HSW_PAVP_FUSE1);
	switch ((fuse1 & HSW_F1_EU_DIS_MASK) >> HSW_F1_EU_DIS_SHIFT) {
	default:
		MISSING_CASE((fuse1 & HSW_F1_EU_DIS_MASK) >>
			     HSW_F1_EU_DIS_SHIFT);
		/* fall through */
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
	sseu->max_eus_per_subslice = sseu->eu_per_subslice;

	for (s = 0; s < sseu->max_slices; s++) {
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

static u32 read_reference_ts_freq(struct drm_i915_private *dev_priv)
{
	u32 ts_override = I915_READ(GEN9_TIMESTAMP_OVERRIDE);
	u32 base_freq, frac_freq;

	base_freq = ((ts_override & GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DIVIDER_MASK) >>
		     GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DIVIDER_SHIFT) + 1;
	base_freq *= 1000;

	frac_freq = ((ts_override &
		      GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DENOMINATOR_MASK) >>
		     GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DENOMINATOR_SHIFT);
	frac_freq = 1000 / (frac_freq + 1);

	return base_freq + frac_freq;
}

static u32 gen10_get_crystal_clock_freq(struct drm_i915_private *dev_priv,
					u32 rpm_config_reg)
{
	u32 f19_2_mhz = 19200;
	u32 f24_mhz = 24000;
	u32 crystal_clock = (rpm_config_reg &
			     GEN9_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_MASK) >>
			    GEN9_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_SHIFT;

	switch (crystal_clock) {
	case GEN9_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_19_2_MHZ:
		return f19_2_mhz;
	case GEN9_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_24_MHZ:
		return f24_mhz;
	default:
		MISSING_CASE(crystal_clock);
		return 0;
	}
}

static u32 gen11_get_crystal_clock_freq(struct drm_i915_private *dev_priv,
					u32 rpm_config_reg)
{
	u32 f19_2_mhz = 19200;
	u32 f24_mhz = 24000;
	u32 f25_mhz = 25000;
	u32 f38_4_mhz = 38400;
	u32 crystal_clock = (rpm_config_reg &
			     GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_MASK) >>
			    GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_SHIFT;

	switch (crystal_clock) {
	case GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_24_MHZ:
		return f24_mhz;
	case GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_19_2_MHZ:
		return f19_2_mhz;
	case GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_38_4_MHZ:
		return f38_4_mhz;
	case GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_25_MHZ:
		return f25_mhz;
	default:
		MISSING_CASE(crystal_clock);
		return 0;
	}
}

static u32 read_timestamp_frequency(struct drm_i915_private *dev_priv)
{
	u32 f12_5_mhz = 12500;
	u32 f19_2_mhz = 19200;
	u32 f24_mhz = 24000;

	if (INTEL_GEN(dev_priv) <= 4) {
		/* PRMs say:
		 *
		 *     "The value in this register increments once every 16
		 *      hclks." (through the “Clocking Configuration”
		 *      (“CLKCFG”) MCHBAR register)
		 */
		return dev_priv->rawclk_freq / 16;
	} else if (INTEL_GEN(dev_priv) <= 8) {
		/* PRMs say:
		 *
		 *     "The PCU TSC counts 10ns increments; this timestamp
		 *      reflects bits 38:3 of the TSC (i.e. 80ns granularity,
		 *      rolling over every 1.5 hours).
		 */
		return f12_5_mhz;
	} else if (INTEL_GEN(dev_priv) <= 9) {
		u32 ctc_reg = I915_READ(CTC_MODE);
		u32 freq = 0;

		if ((ctc_reg & CTC_SOURCE_PARAMETER_MASK) == CTC_SOURCE_DIVIDE_LOGIC) {
			freq = read_reference_ts_freq(dev_priv);
		} else {
			freq = IS_GEN9_LP(dev_priv) ? f19_2_mhz : f24_mhz;

			/* Now figure out how the command stream's timestamp
			 * register increments from this frequency (it might
			 * increment only every few clock cycle).
			 */
			freq >>= 3 - ((ctc_reg & CTC_SHIFT_PARAMETER_MASK) >>
				      CTC_SHIFT_PARAMETER_SHIFT);
		}

		return freq;
	} else if (INTEL_GEN(dev_priv) <= 11) {
		u32 ctc_reg = I915_READ(CTC_MODE);
		u32 freq = 0;

		/* First figure out the reference frequency. There are 2 ways
		 * we can compute the frequency, either through the
		 * TIMESTAMP_OVERRIDE register or through RPM_CONFIG. CTC_MODE
		 * tells us which one we should use.
		 */
		if ((ctc_reg & CTC_SOURCE_PARAMETER_MASK) == CTC_SOURCE_DIVIDE_LOGIC) {
			freq = read_reference_ts_freq(dev_priv);
		} else {
			u32 rpm_config_reg = I915_READ(RPM_CONFIG0);

			if (INTEL_GEN(dev_priv) <= 10)
				freq = gen10_get_crystal_clock_freq(dev_priv,
								rpm_config_reg);
			else
				freq = gen11_get_crystal_clock_freq(dev_priv,
								rpm_config_reg);

			/* Now figure out how the command stream's timestamp
			 * register increments from this frequency (it might
			 * increment only every few clock cycle).
			 */
			freq >>= 3 - ((rpm_config_reg &
				       GEN10_RPM_CONFIG0_CTC_SHIFT_PARAMETER_MASK) >>
				      GEN10_RPM_CONFIG0_CTC_SHIFT_PARAMETER_SHIFT);
		}

		return freq;
	}

	MISSING_CASE("Unknown gen, unable to read command streamer timestamp frequency\n");
	return 0;
}

#undef INTEL_VGA_DEVICE
#define INTEL_VGA_DEVICE(id, info) (id)

static const u16 subplatform_ult_ids[] = {
	INTEL_HSW_ULT_GT1_IDS(0),
	INTEL_HSW_ULT_GT2_IDS(0),
	INTEL_HSW_ULT_GT3_IDS(0),
	INTEL_BDW_ULT_GT1_IDS(0),
	INTEL_BDW_ULT_GT2_IDS(0),
	INTEL_BDW_ULT_GT3_IDS(0),
	INTEL_BDW_ULT_RSVD_IDS(0),
	INTEL_SKL_ULT_GT1_IDS(0),
	INTEL_SKL_ULT_GT2_IDS(0),
	INTEL_SKL_ULT_GT3_IDS(0),
	INTEL_KBL_ULT_GT1_IDS(0),
	INTEL_KBL_ULT_GT2_IDS(0),
	INTEL_KBL_ULT_GT3_IDS(0),
	INTEL_CFL_U_GT2_IDS(0),
	INTEL_CFL_U_GT3_IDS(0),
	INTEL_WHL_U_GT1_IDS(0),
	INTEL_WHL_U_GT2_IDS(0),
	INTEL_WHL_U_GT3_IDS(0)
};

static const u16 subplatform_ulx_ids[] = {
	INTEL_HSW_ULX_GT1_IDS(0),
	INTEL_HSW_ULX_GT2_IDS(0),
	INTEL_BDW_ULX_GT1_IDS(0),
	INTEL_BDW_ULX_GT2_IDS(0),
	INTEL_BDW_ULX_GT3_IDS(0),
	INTEL_BDW_ULX_RSVD_IDS(0),
	INTEL_SKL_ULX_GT1_IDS(0),
	INTEL_SKL_ULX_GT2_IDS(0),
	INTEL_KBL_ULX_GT1_IDS(0),
	INTEL_KBL_ULX_GT2_IDS(0)
};

static const u16 subplatform_aml_ids[] = {
	INTEL_AML_KBL_GT2_IDS(0),
	INTEL_AML_CFL_GT2_IDS(0)
};

static const u16 subplatform_portf_ids[] = {
	INTEL_CNL_PORT_F_IDS(0),
	INTEL_ICL_PORT_F_IDS(0)
};

static bool find_devid(u16 id, const u16 *p, unsigned int num)
{
	for (; num; num--, p++) {
		if (*p == id)
			return true;
	}

	return false;
}

void intel_device_info_subplatform_init(struct drm_i915_private *i915)
{
	const struct intel_device_info *info = INTEL_INFO(i915);
	const struct intel_runtime_info *rinfo = RUNTIME_INFO(i915);
	const unsigned int pi = __platform_mask_index(rinfo, info->platform);
	const unsigned int pb = __platform_mask_bit(rinfo, info->platform);
	u16 devid = INTEL_DEVID(i915);
	u32 mask;

	/* Make sure IS_<platform> checks are working. */
	RUNTIME_INFO(i915)->platform_mask[pi] = BIT(pb);

	/* Find and mark subplatform bits based on the PCI device id. */
	if (find_devid(devid, subplatform_ult_ids,
		       ARRAY_SIZE(subplatform_ult_ids))) {
		mask = BIT(INTEL_SUBPLATFORM_ULT);
	} else if (find_devid(devid, subplatform_ulx_ids,
			      ARRAY_SIZE(subplatform_ulx_ids))) {
		mask = BIT(INTEL_SUBPLATFORM_ULX);
		if (IS_HASWELL(i915) || IS_BROADWELL(i915)) {
			/* ULX machines are also considered ULT. */
			mask |= BIT(INTEL_SUBPLATFORM_ULT);
		}
	} else if (find_devid(devid, subplatform_aml_ids,
			      ARRAY_SIZE(subplatform_aml_ids))) {
		mask = BIT(INTEL_SUBPLATFORM_AML);
	} else if (find_devid(devid, subplatform_portf_ids,
			      ARRAY_SIZE(subplatform_portf_ids))) {
		mask = BIT(INTEL_SUBPLATFORM_PORTF);
	}

	GEM_BUG_ON(mask & ~INTEL_SUBPLATFORM_BITS);

	RUNTIME_INFO(i915)->platform_mask[pi] |= mask;
}

/**
 * intel_device_info_runtime_init - initialize runtime info
 * @dev_priv: the i915 device
 *
 * Determine various intel_device_info fields at runtime.
 *
 * Use it when either:
 *   - it's judged too laborious to fill n static structures with the limit
 *     when a simple if statement does the job,
 *   - run-time checks (eg read fuse/strap registers) are needed.
 *
 * This function needs to be called:
 *   - after the MMIO has been setup as we are reading registers,
 *   - after the PCH has been detected,
 *   - before the first usage of the fields it can tweak.
 */
void intel_device_info_runtime_init(struct drm_i915_private *dev_priv)
{
	struct intel_device_info *info = mkwrite_device_info(dev_priv);
	struct intel_runtime_info *runtime = RUNTIME_INFO(dev_priv);
	enum pipe pipe;

	if (INTEL_GEN(dev_priv) >= 10) {
		for_each_pipe(dev_priv, pipe)
			runtime->num_scalers[pipe] = 2;
	} else if (IS_GEN(dev_priv, 9)) {
		runtime->num_scalers[PIPE_A] = 2;
		runtime->num_scalers[PIPE_B] = 2;
		runtime->num_scalers[PIPE_C] = 1;
	}

	BUILD_BUG_ON(BITS_PER_TYPE(intel_engine_mask_t) < I915_NUM_ENGINES);

	if (INTEL_GEN(dev_priv) >= 11)
		for_each_pipe(dev_priv, pipe)
			runtime->num_sprites[pipe] = 6;
	else if (IS_GEN(dev_priv, 10) || IS_GEMINILAKE(dev_priv))
		for_each_pipe(dev_priv, pipe)
			runtime->num_sprites[pipe] = 3;
	else if (IS_BROXTON(dev_priv)) {
		/*
		 * Skylake and Broxton currently don't expose the topmost plane as its
		 * use is exclusive with the legacy cursor and we only want to expose
		 * one of those, not both. Until we can safely expose the topmost plane
		 * as a DRM_PLANE_TYPE_CURSOR with all the features exposed/supported,
		 * we don't expose the topmost plane at all to prevent ABI breakage
		 * down the line.
		 */

		runtime->num_sprites[PIPE_A] = 2;
		runtime->num_sprites[PIPE_B] = 2;
		runtime->num_sprites[PIPE_C] = 1;
	} else if (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) {
		for_each_pipe(dev_priv, pipe)
			runtime->num_sprites[pipe] = 2;
	} else if (INTEL_GEN(dev_priv) >= 5 || IS_G4X(dev_priv)) {
		for_each_pipe(dev_priv, pipe)
			runtime->num_sprites[pipe] = 1;
	}

	if (i915_modparams.disable_display) {
		DRM_INFO("Display disabled (module parameter)\n");
		info->num_pipes = 0;
	} else if (HAS_DISPLAY(dev_priv) &&
		   (IS_GEN_RANGE(dev_priv, 7, 8)) &&
		   HAS_PCH_SPLIT(dev_priv)) {
		u32 fuse_strap = I915_READ(FUSE_STRAP);
		u32 sfuse_strap = I915_READ(SFUSE_STRAP);

		/*
		 * SFUSE_STRAP is supposed to have a bit signalling the display
		 * is fused off. Unfortunately it seems that, at least in
		 * certain cases, fused off display means that PCH display
		 * reads don't land anywhere. In that case, we read 0s.
		 *
		 * On CPT/PPT, we can detect this case as SFUSE_STRAP_FUSE_LOCK
		 * should be set when taking over after the firmware.
		 */
		if (fuse_strap & ILK_INTERNAL_DISPLAY_DISABLE ||
		    sfuse_strap & SFUSE_STRAP_DISPLAY_DISABLED ||
		    (HAS_PCH_CPT(dev_priv) &&
		     !(sfuse_strap & SFUSE_STRAP_FUSE_LOCK))) {
			DRM_INFO("Display fused off, disabling\n");
			info->num_pipes = 0;
		} else if (fuse_strap & IVB_PIPE_C_DISABLE) {
			DRM_INFO("PipeC fused off\n");
			info->num_pipes -= 1;
		}
	} else if (HAS_DISPLAY(dev_priv) && INTEL_GEN(dev_priv) >= 9) {
		u32 dfsm = I915_READ(SKL_DFSM);
		u8 disabled_mask = 0;
		bool invalid;
		int num_bits;

		if (dfsm & SKL_DFSM_PIPE_A_DISABLE)
			disabled_mask |= BIT(PIPE_A);
		if (dfsm & SKL_DFSM_PIPE_B_DISABLE)
			disabled_mask |= BIT(PIPE_B);
		if (dfsm & SKL_DFSM_PIPE_C_DISABLE)
			disabled_mask |= BIT(PIPE_C);

		num_bits = hweight8(disabled_mask);

		switch (disabled_mask) {
		case BIT(PIPE_A):
		case BIT(PIPE_B):
		case BIT(PIPE_A) | BIT(PIPE_B):
		case BIT(PIPE_A) | BIT(PIPE_C):
			invalid = true;
			break;
		default:
			invalid = false;
		}

		if (num_bits > info->num_pipes || invalid)
			DRM_ERROR("invalid pipe fuse configuration: 0x%x\n",
				  disabled_mask);
		else
			info->num_pipes -= num_bits;
	}

	/* Initialize slice/subslice/EU info */
	if (IS_HASWELL(dev_priv))
		haswell_sseu_info_init(dev_priv);
	else if (IS_CHERRYVIEW(dev_priv))
		cherryview_sseu_info_init(dev_priv);
	else if (IS_BROADWELL(dev_priv))
		broadwell_sseu_info_init(dev_priv);
	else if (IS_GEN(dev_priv, 9))
		gen9_sseu_info_init(dev_priv);
	else if (IS_GEN(dev_priv, 10))
		gen10_sseu_info_init(dev_priv);
	else if (INTEL_GEN(dev_priv) >= 11)
		gen11_sseu_info_init(dev_priv);

	if (IS_GEN(dev_priv, 6) && intel_vtd_active()) {
		DRM_INFO("Disabling ppGTT for VT-d support\n");
		info->ppgtt_type = INTEL_PPGTT_NONE;
	}

	/* Initialize command stream timestamp frequency */
	runtime->cs_timestamp_frequency_khz = read_timestamp_frequency(dev_priv);
}

void intel_driver_caps_print(const struct intel_driver_caps *caps,
			     struct drm_printer *p)
{
	drm_printf(p, "Has logical contexts? %s\n",
		   yesno(caps->has_logical_contexts));
	drm_printf(p, "scheduler: %x\n", caps->scheduler);
}

/*
 * Determine which engines are fused off in our particular hardware. Since the
 * fuse register is in the blitter powerwell, we need forcewake to be ready at
 * this point (but later we need to prune the forcewake domains for engines that
 * are indeed fused off).
 */
void intel_device_info_init_mmio(struct drm_i915_private *dev_priv)
{
	struct intel_device_info *info = mkwrite_device_info(dev_priv);
	unsigned int logical_vdbox = 0;
	unsigned int i;
	u32 media_fuse;
	u16 vdbox_mask;
	u16 vebox_mask;

	if (INTEL_GEN(dev_priv) < 11)
		return;

	media_fuse = ~I915_READ(GEN11_GT_VEBOX_VDBOX_DISABLE);

	vdbox_mask = media_fuse & GEN11_GT_VDBOX_DISABLE_MASK;
	vebox_mask = (media_fuse & GEN11_GT_VEBOX_DISABLE_MASK) >>
		      GEN11_GT_VEBOX_DISABLE_SHIFT;

	for (i = 0; i < I915_MAX_VCS; i++) {
		if (!HAS_ENGINE(dev_priv, _VCS(i)))
			continue;

		if (!(BIT(i) & vdbox_mask)) {
			info->engine_mask &= ~BIT(_VCS(i));
			DRM_DEBUG_DRIVER("vcs%u fused off\n", i);
			continue;
		}

		/*
		 * In Gen11, only even numbered logical VDBOXes are
		 * hooked up to an SFC (Scaler & Format Converter) unit.
		 */
		if (logical_vdbox++ % 2 == 0)
			RUNTIME_INFO(dev_priv)->vdbox_sfc_access |= BIT(i);
	}
	DRM_DEBUG_DRIVER("vdbox enable: %04x, instances: %04lx\n",
			 vdbox_mask, VDBOX_MASK(dev_priv));
	GEM_BUG_ON(vdbox_mask != VDBOX_MASK(dev_priv));

	for (i = 0; i < I915_MAX_VECS; i++) {
		if (!HAS_ENGINE(dev_priv, _VECS(i)))
			continue;

		if (!(BIT(i) & vebox_mask)) {
			info->engine_mask &= ~BIT(_VECS(i));
			DRM_DEBUG_DRIVER("vecs%u fused off\n", i);
		}
	}
	DRM_DEBUG_DRIVER("vebox enable: %04x, instances: %04lx\n",
			 vebox_mask, VEBOX_MASK(dev_priv));
	GEM_BUG_ON(vebox_mask != VEBOX_MASK(dev_priv));
}

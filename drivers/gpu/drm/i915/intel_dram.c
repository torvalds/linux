// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_dram.h"
#include "intel_sideband.h"

struct dram_dimm_info {
	u16 size;
	u8 width, ranks;
};

struct dram_channel_info {
	struct dram_dimm_info dimm_l, dimm_s;
	u8 ranks;
	bool is_16gb_dimm;
};

#define DRAM_TYPE_STR(type) [INTEL_DRAM_ ## type] = #type

static const char *intel_dram_type_str(enum intel_dram_type type)
{
	static const char * const str[] = {
		DRAM_TYPE_STR(UNKNOWN),
		DRAM_TYPE_STR(DDR3),
		DRAM_TYPE_STR(DDR4),
		DRAM_TYPE_STR(LPDDR3),
		DRAM_TYPE_STR(LPDDR4),
	};

	if (type >= ARRAY_SIZE(str))
		type = INTEL_DRAM_UNKNOWN;

	return str[type];
}

#undef DRAM_TYPE_STR

static int intel_dimm_num_devices(const struct dram_dimm_info *dimm)
{
	return dimm->ranks * 64 / (dimm->width ?: 1);
}

/* Returns total Gb for the whole DIMM */
static int skl_get_dimm_size(u16 val)
{
	return (val & SKL_DRAM_SIZE_MASK) * 8;
}

static int skl_get_dimm_width(u16 val)
{
	if (skl_get_dimm_size(val) == 0)
		return 0;

	switch (val & SKL_DRAM_WIDTH_MASK) {
	case SKL_DRAM_WIDTH_X8:
	case SKL_DRAM_WIDTH_X16:
	case SKL_DRAM_WIDTH_X32:
		val = (val & SKL_DRAM_WIDTH_MASK) >> SKL_DRAM_WIDTH_SHIFT;
		return 8 << val;
	default:
		MISSING_CASE(val);
		return 0;
	}
}

static int skl_get_dimm_ranks(u16 val)
{
	if (skl_get_dimm_size(val) == 0)
		return 0;

	val = (val & SKL_DRAM_RANK_MASK) >> SKL_DRAM_RANK_SHIFT;

	return val + 1;
}

/* Returns total Gb for the whole DIMM */
static int cnl_get_dimm_size(u16 val)
{
	return (val & CNL_DRAM_SIZE_MASK) * 8 / 2;
}

static int cnl_get_dimm_width(u16 val)
{
	if (cnl_get_dimm_size(val) == 0)
		return 0;

	switch (val & CNL_DRAM_WIDTH_MASK) {
	case CNL_DRAM_WIDTH_X8:
	case CNL_DRAM_WIDTH_X16:
	case CNL_DRAM_WIDTH_X32:
		val = (val & CNL_DRAM_WIDTH_MASK) >> CNL_DRAM_WIDTH_SHIFT;
		return 8 << val;
	default:
		MISSING_CASE(val);
		return 0;
	}
}

static int cnl_get_dimm_ranks(u16 val)
{
	if (cnl_get_dimm_size(val) == 0)
		return 0;

	val = (val & CNL_DRAM_RANK_MASK) >> CNL_DRAM_RANK_SHIFT;

	return val + 1;
}

static bool
skl_is_16gb_dimm(const struct dram_dimm_info *dimm)
{
	/* Convert total Gb to Gb per DRAM device */
	return dimm->size / (intel_dimm_num_devices(dimm) ?: 1) == 16;
}

static void
skl_dram_get_dimm_info(struct drm_i915_private *i915,
		       struct dram_dimm_info *dimm,
		       int channel, char dimm_name, u16 val)
{
	if (GRAPHICS_VER(i915) >= 10) {
		dimm->size = cnl_get_dimm_size(val);
		dimm->width = cnl_get_dimm_width(val);
		dimm->ranks = cnl_get_dimm_ranks(val);
	} else {
		dimm->size = skl_get_dimm_size(val);
		dimm->width = skl_get_dimm_width(val);
		dimm->ranks = skl_get_dimm_ranks(val);
	}

	drm_dbg_kms(&i915->drm,
		    "CH%u DIMM %c size: %u Gb, width: X%u, ranks: %u, 16Gb DIMMs: %s\n",
		    channel, dimm_name, dimm->size, dimm->width, dimm->ranks,
		    yesno(skl_is_16gb_dimm(dimm)));
}

static int
skl_dram_get_channel_info(struct drm_i915_private *i915,
			  struct dram_channel_info *ch,
			  int channel, u32 val)
{
	skl_dram_get_dimm_info(i915, &ch->dimm_l,
			       channel, 'L', val & 0xffff);
	skl_dram_get_dimm_info(i915, &ch->dimm_s,
			       channel, 'S', val >> 16);

	if (ch->dimm_l.size == 0 && ch->dimm_s.size == 0) {
		drm_dbg_kms(&i915->drm, "CH%u not populated\n", channel);
		return -EINVAL;
	}

	if (ch->dimm_l.ranks == 2 || ch->dimm_s.ranks == 2)
		ch->ranks = 2;
	else if (ch->dimm_l.ranks == 1 && ch->dimm_s.ranks == 1)
		ch->ranks = 2;
	else
		ch->ranks = 1;

	ch->is_16gb_dimm = skl_is_16gb_dimm(&ch->dimm_l) ||
		skl_is_16gb_dimm(&ch->dimm_s);

	drm_dbg_kms(&i915->drm, "CH%u ranks: %u, 16Gb DIMMs: %s\n",
		    channel, ch->ranks, yesno(ch->is_16gb_dimm));

	return 0;
}

static bool
intel_is_dram_symmetric(const struct dram_channel_info *ch0,
			const struct dram_channel_info *ch1)
{
	return !memcmp(ch0, ch1, sizeof(*ch0)) &&
		(ch0->dimm_s.size == 0 ||
		 !memcmp(&ch0->dimm_l, &ch0->dimm_s, sizeof(ch0->dimm_l)));
}

static int
skl_dram_get_channels_info(struct drm_i915_private *i915)
{
	struct dram_info *dram_info = &i915->dram_info;
	struct dram_channel_info ch0 = {}, ch1 = {};
	u32 val;
	int ret;

	val = intel_uncore_read(&i915->uncore,
				SKL_MAD_DIMM_CH0_0_0_0_MCHBAR_MCMAIN);
	ret = skl_dram_get_channel_info(i915, &ch0, 0, val);
	if (ret == 0)
		dram_info->num_channels++;

	val = intel_uncore_read(&i915->uncore,
				SKL_MAD_DIMM_CH1_0_0_0_MCHBAR_MCMAIN);
	ret = skl_dram_get_channel_info(i915, &ch1, 1, val);
	if (ret == 0)
		dram_info->num_channels++;

	if (dram_info->num_channels == 0) {
		drm_info(&i915->drm, "Number of memory channels is zero\n");
		return -EINVAL;
	}

	if (ch0.ranks == 0 && ch1.ranks == 0) {
		drm_info(&i915->drm, "couldn't get memory rank information\n");
		return -EINVAL;
	}

	dram_info->wm_lv_0_adjust_needed = ch0.is_16gb_dimm || ch1.is_16gb_dimm;

	dram_info->symmetric_memory = intel_is_dram_symmetric(&ch0, &ch1);

	drm_dbg_kms(&i915->drm, "Memory configuration is symmetric? %s\n",
		    yesno(dram_info->symmetric_memory));

	return 0;
}

static enum intel_dram_type
skl_get_dram_type(struct drm_i915_private *i915)
{
	u32 val;

	val = intel_uncore_read(&i915->uncore,
				SKL_MAD_INTER_CHANNEL_0_0_0_MCHBAR_MCMAIN);

	switch (val & SKL_DRAM_DDR_TYPE_MASK) {
	case SKL_DRAM_DDR_TYPE_DDR3:
		return INTEL_DRAM_DDR3;
	case SKL_DRAM_DDR_TYPE_DDR4:
		return INTEL_DRAM_DDR4;
	case SKL_DRAM_DDR_TYPE_LPDDR3:
		return INTEL_DRAM_LPDDR3;
	case SKL_DRAM_DDR_TYPE_LPDDR4:
		return INTEL_DRAM_LPDDR4;
	default:
		MISSING_CASE(val);
		return INTEL_DRAM_UNKNOWN;
	}
}

static int
skl_get_dram_info(struct drm_i915_private *i915)
{
	struct dram_info *dram_info = &i915->dram_info;
	u32 mem_freq_khz, val;
	int ret;

	dram_info->type = skl_get_dram_type(i915);
	drm_dbg_kms(&i915->drm, "DRAM type: %s\n",
		    intel_dram_type_str(dram_info->type));

	ret = skl_dram_get_channels_info(i915);
	if (ret)
		return ret;

	val = intel_uncore_read(&i915->uncore,
				SKL_MC_BIOS_DATA_0_0_0_MCHBAR_PCU);
	mem_freq_khz = DIV_ROUND_UP((val & SKL_REQ_DATA_MASK) *
				    SKL_MEMORY_FREQ_MULTIPLIER_HZ, 1000);

	if (dram_info->num_channels * mem_freq_khz == 0) {
		drm_info(&i915->drm,
			 "Couldn't get system memory bandwidth\n");
		return -EINVAL;
	}

	return 0;
}

/* Returns Gb per DRAM device */
static int bxt_get_dimm_size(u32 val)
{
	switch (val & BXT_DRAM_SIZE_MASK) {
	case BXT_DRAM_SIZE_4GBIT:
		return 4;
	case BXT_DRAM_SIZE_6GBIT:
		return 6;
	case BXT_DRAM_SIZE_8GBIT:
		return 8;
	case BXT_DRAM_SIZE_12GBIT:
		return 12;
	case BXT_DRAM_SIZE_16GBIT:
		return 16;
	default:
		MISSING_CASE(val);
		return 0;
	}
}

static int bxt_get_dimm_width(u32 val)
{
	if (!bxt_get_dimm_size(val))
		return 0;

	val = (val & BXT_DRAM_WIDTH_MASK) >> BXT_DRAM_WIDTH_SHIFT;

	return 8 << val;
}

static int bxt_get_dimm_ranks(u32 val)
{
	if (!bxt_get_dimm_size(val))
		return 0;

	switch (val & BXT_DRAM_RANK_MASK) {
	case BXT_DRAM_RANK_SINGLE:
		return 1;
	case BXT_DRAM_RANK_DUAL:
		return 2;
	default:
		MISSING_CASE(val);
		return 0;
	}
}

static enum intel_dram_type bxt_get_dimm_type(u32 val)
{
	if (!bxt_get_dimm_size(val))
		return INTEL_DRAM_UNKNOWN;

	switch (val & BXT_DRAM_TYPE_MASK) {
	case BXT_DRAM_TYPE_DDR3:
		return INTEL_DRAM_DDR3;
	case BXT_DRAM_TYPE_LPDDR3:
		return INTEL_DRAM_LPDDR3;
	case BXT_DRAM_TYPE_DDR4:
		return INTEL_DRAM_DDR4;
	case BXT_DRAM_TYPE_LPDDR4:
		return INTEL_DRAM_LPDDR4;
	default:
		MISSING_CASE(val);
		return INTEL_DRAM_UNKNOWN;
	}
}

static void bxt_get_dimm_info(struct dram_dimm_info *dimm, u32 val)
{
	dimm->width = bxt_get_dimm_width(val);
	dimm->ranks = bxt_get_dimm_ranks(val);

	/*
	 * Size in register is Gb per DRAM device. Convert to total
	 * Gb to match the way we report this for non-LP platforms.
	 */
	dimm->size = bxt_get_dimm_size(val) * intel_dimm_num_devices(dimm);
}

static int bxt_get_dram_info(struct drm_i915_private *i915)
{
	struct dram_info *dram_info = &i915->dram_info;
	u32 dram_channels;
	u32 mem_freq_khz, val;
	u8 num_active_channels, valid_ranks = 0;
	int i;

	val = intel_uncore_read(&i915->uncore, BXT_P_CR_MC_BIOS_REQ_0_0_0);
	mem_freq_khz = DIV_ROUND_UP((val & BXT_REQ_DATA_MASK) *
				    BXT_MEMORY_FREQ_MULTIPLIER_HZ, 1000);

	dram_channels = val & BXT_DRAM_CHANNEL_ACTIVE_MASK;
	num_active_channels = hweight32(dram_channels);

	if (mem_freq_khz * num_active_channels == 0) {
		drm_info(&i915->drm,
			 "Couldn't get system memory bandwidth\n");
		return -EINVAL;
	}

	/*
	 * Now read each DUNIT8/9/10/11 to check the rank of each dimms.
	 */
	for (i = BXT_D_CR_DRP0_DUNIT_START; i <= BXT_D_CR_DRP0_DUNIT_END; i++) {
		struct dram_dimm_info dimm;
		enum intel_dram_type type;

		val = intel_uncore_read(&i915->uncore, BXT_D_CR_DRP0_DUNIT(i));
		if (val == 0xFFFFFFFF)
			continue;

		dram_info->num_channels++;

		bxt_get_dimm_info(&dimm, val);
		type = bxt_get_dimm_type(val);

		drm_WARN_ON(&i915->drm, type != INTEL_DRAM_UNKNOWN &&
			    dram_info->type != INTEL_DRAM_UNKNOWN &&
			    dram_info->type != type);

		drm_dbg_kms(&i915->drm,
			    "CH%u DIMM size: %u Gb, width: X%u, ranks: %u, type: %s\n",
			    i - BXT_D_CR_DRP0_DUNIT_START,
			    dimm.size, dimm.width, dimm.ranks,
			    intel_dram_type_str(type));

		if (valid_ranks == 0)
			valid_ranks = dimm.ranks;

		if (type != INTEL_DRAM_UNKNOWN)
			dram_info->type = type;
	}

	if (dram_info->type == INTEL_DRAM_UNKNOWN || valid_ranks == 0) {
		drm_info(&i915->drm, "couldn't get memory information\n");
		return -EINVAL;
	}

	return 0;
}

static int icl_pcode_read_mem_global_info(struct drm_i915_private *dev_priv)
{
	struct dram_info *dram_info = &dev_priv->dram_info;
	u32 val = 0;
	int ret;

	ret = sandybridge_pcode_read(dev_priv,
				     ICL_PCODE_MEM_SUBSYSYSTEM_INFO |
				     ICL_PCODE_MEM_SS_READ_GLOBAL_INFO,
				     &val, NULL);
	if (ret)
		return ret;

	if (GRAPHICS_VER(dev_priv) == 12) {
		switch (val & 0xf) {
		case 0:
			dram_info->type = INTEL_DRAM_DDR4;
			break;
		case 1:
			dram_info->type = INTEL_DRAM_DDR5;
			break;
		case 2:
			dram_info->type = INTEL_DRAM_LPDDR5;
			break;
		case 3:
			dram_info->type = INTEL_DRAM_LPDDR4;
			break;
		case 4:
			dram_info->type = INTEL_DRAM_DDR3;
			break;
		case 5:
			dram_info->type = INTEL_DRAM_LPDDR3;
			break;
		default:
			MISSING_CASE(val & 0xf);
			return -1;
		}
	} else {
		switch (val & 0xf) {
		case 0:
			dram_info->type = INTEL_DRAM_DDR4;
			break;
		case 1:
			dram_info->type = INTEL_DRAM_DDR3;
			break;
		case 2:
			dram_info->type = INTEL_DRAM_LPDDR3;
			break;
		case 3:
			dram_info->type = INTEL_DRAM_LPDDR4;
			break;
		default:
			MISSING_CASE(val & 0xf);
			return -1;
		}
	}

	dram_info->num_channels = (val & 0xf0) >> 4;
	dram_info->num_qgv_points = (val & 0xf00) >> 8;

	return 0;
}

static int gen11_get_dram_info(struct drm_i915_private *i915)
{
	int ret = skl_get_dram_info(i915);

	if (ret)
		return ret;

	return icl_pcode_read_mem_global_info(i915);
}

static int gen12_get_dram_info(struct drm_i915_private *i915)
{
	/* Always needed for GEN12+ */
	i915->dram_info.wm_lv_0_adjust_needed = true;

	return icl_pcode_read_mem_global_info(i915);
}

void intel_dram_detect(struct drm_i915_private *i915)
{
	struct dram_info *dram_info = &i915->dram_info;
	int ret;

	/*
	 * Assume level 0 watermark latency adjustment is needed until proven
	 * otherwise, this w/a is not needed by bxt/glk.
	 */
	dram_info->wm_lv_0_adjust_needed = !IS_GEN9_LP(i915);

	if (GRAPHICS_VER(i915) < 9 || !HAS_DISPLAY(i915))
		return;

	if (GRAPHICS_VER(i915) >= 12)
		ret = gen12_get_dram_info(i915);
	else if (GRAPHICS_VER(i915) >= 11)
		ret = gen11_get_dram_info(i915);
	else if (IS_GEN9_LP(i915))
		ret = bxt_get_dram_info(i915);
	else
		ret = skl_get_dram_info(i915);
	if (ret)
		return;

	drm_dbg_kms(&i915->drm, "DRAM channels: %u\n", dram_info->num_channels);

	drm_dbg_kms(&i915->drm, "Watermark level 0 adjustment needed: %s\n",
		    yesno(dram_info->wm_lv_0_adjust_needed));
}

static u32 gen9_edram_size_mb(struct drm_i915_private *i915, u32 cap)
{
	static const u8 ways[8] = { 4, 8, 12, 16, 16, 16, 16, 16 };
	static const u8 sets[4] = { 1, 1, 2, 2 };

	return EDRAM_NUM_BANKS(cap) *
		ways[EDRAM_WAYS_IDX(cap)] *
		sets[EDRAM_SETS_IDX(cap)];
}

void intel_dram_edram_detect(struct drm_i915_private *i915)
{
	u32 edram_cap = 0;

	if (!(IS_HASWELL(i915) || IS_BROADWELL(i915) || GRAPHICS_VER(i915) >= 9))
		return;

	edram_cap = __raw_uncore_read32(&i915->uncore, HSW_EDRAM_CAP);

	/* NB: We can't write IDICR yet because we don't have gt funcs set up */

	if (!(edram_cap & EDRAM_ENABLED))
		return;

	/*
	 * The needed capability bits for size calculation are not there with
	 * pre gen9 so return 128MB always.
	 */
	if (GRAPHICS_VER(i915) < 9)
		i915->edram_size_mb = 128;
	else
		i915->edram_size_mb = gen9_edram_size_mb(i915, edram_cap);

	drm_info(&i915->drm, "Found %uMB of eDRAM\n", i915->edram_size_mb);
}

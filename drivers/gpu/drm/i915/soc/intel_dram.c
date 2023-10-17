// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/string_helpers.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_dram.h"
#include "intel_mchbar_regs.h"
#include "intel_pcode.h"
#include "vlv_sideband.h"

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

static void pnv_detect_mem_freq(struct drm_i915_private *dev_priv)
{
	u32 tmp;

	tmp = intel_uncore_read(&dev_priv->uncore, CLKCFG);

	switch (tmp & CLKCFG_FSB_MASK) {
	case CLKCFG_FSB_533:
		dev_priv->fsb_freq = 533; /* 133*4 */
		break;
	case CLKCFG_FSB_800:
		dev_priv->fsb_freq = 800; /* 200*4 */
		break;
	case CLKCFG_FSB_667:
		dev_priv->fsb_freq =  667; /* 167*4 */
		break;
	case CLKCFG_FSB_400:
		dev_priv->fsb_freq = 400; /* 100*4 */
		break;
	}

	switch (tmp & CLKCFG_MEM_MASK) {
	case CLKCFG_MEM_533:
		dev_priv->mem_freq = 533;
		break;
	case CLKCFG_MEM_667:
		dev_priv->mem_freq = 667;
		break;
	case CLKCFG_MEM_800:
		dev_priv->mem_freq = 800;
		break;
	}

	/* detect pineview DDR3 setting */
	tmp = intel_uncore_read(&dev_priv->uncore, CSHRDDR3CTL);
	dev_priv->is_ddr3 = (tmp & CSHRDDR3CTL_DDR3) ? 1 : 0;
}

static void ilk_detect_mem_freq(struct drm_i915_private *dev_priv)
{
	u16 ddrpll, csipll;

	ddrpll = intel_uncore_read16(&dev_priv->uncore, DDRMPLL1);
	switch (ddrpll & 0xff) {
	case 0xc:
		dev_priv->mem_freq = 800;
		break;
	case 0x10:
		dev_priv->mem_freq = 1066;
		break;
	case 0x14:
		dev_priv->mem_freq = 1333;
		break;
	case 0x18:
		dev_priv->mem_freq = 1600;
		break;
	default:
		drm_dbg(&dev_priv->drm, "unknown memory frequency 0x%02x\n",
			ddrpll & 0xff);
		dev_priv->mem_freq = 0;
		break;
	}

	csipll = intel_uncore_read16(&dev_priv->uncore, CSIPLL0);
	switch (csipll & 0x3ff) {
	case 0x00c:
		dev_priv->fsb_freq = 3200;
		break;
	case 0x00e:
		dev_priv->fsb_freq = 3733;
		break;
	case 0x010:
		dev_priv->fsb_freq = 4266;
		break;
	case 0x012:
		dev_priv->fsb_freq = 4800;
		break;
	case 0x014:
		dev_priv->fsb_freq = 5333;
		break;
	case 0x016:
		dev_priv->fsb_freq = 5866;
		break;
	case 0x018:
		dev_priv->fsb_freq = 6400;
		break;
	default:
		drm_dbg(&dev_priv->drm, "unknown fsb frequency 0x%04x\n",
			csipll & 0x3ff);
		dev_priv->fsb_freq = 0;
		break;
	}
}

static void chv_detect_mem_freq(struct drm_i915_private *i915)
{
	u32 val;

	vlv_iosf_sb_get(i915, BIT(VLV_IOSF_SB_CCK));
	val = vlv_cck_read(i915, CCK_FUSE_REG);
	vlv_iosf_sb_put(i915, BIT(VLV_IOSF_SB_CCK));

	switch ((val >> 2) & 0x7) {
	case 3:
		i915->mem_freq = 2000;
		break;
	default:
		i915->mem_freq = 1600;
		break;
	}
}

static void vlv_detect_mem_freq(struct drm_i915_private *i915)
{
	u32 val;

	vlv_iosf_sb_get(i915, BIT(VLV_IOSF_SB_PUNIT));
	val = vlv_punit_read(i915, PUNIT_REG_GPU_FREQ_STS);
	vlv_iosf_sb_put(i915, BIT(VLV_IOSF_SB_PUNIT));

	switch ((val >> 6) & 3) {
	case 0:
	case 1:
		i915->mem_freq = 800;
		break;
	case 2:
		i915->mem_freq = 1066;
		break;
	case 3:
		i915->mem_freq = 1333;
		break;
	}
}

static void detect_mem_freq(struct drm_i915_private *i915)
{
	if (IS_PINEVIEW(i915))
		pnv_detect_mem_freq(i915);
	else if (GRAPHICS_VER(i915) == 5)
		ilk_detect_mem_freq(i915);
	else if (IS_CHERRYVIEW(i915))
		chv_detect_mem_freq(i915);
	else if (IS_VALLEYVIEW(i915))
		vlv_detect_mem_freq(i915);

	if (i915->mem_freq)
		drm_dbg(&i915->drm, "DDR speed: %d MHz\n", i915->mem_freq);
}

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
static int icl_get_dimm_size(u16 val)
{
	return (val & ICL_DRAM_SIZE_MASK) * 8 / 2;
}

static int icl_get_dimm_width(u16 val)
{
	if (icl_get_dimm_size(val) == 0)
		return 0;

	switch (val & ICL_DRAM_WIDTH_MASK) {
	case ICL_DRAM_WIDTH_X8:
	case ICL_DRAM_WIDTH_X16:
	case ICL_DRAM_WIDTH_X32:
		val = (val & ICL_DRAM_WIDTH_MASK) >> ICL_DRAM_WIDTH_SHIFT;
		return 8 << val;
	default:
		MISSING_CASE(val);
		return 0;
	}
}

static int icl_get_dimm_ranks(u16 val)
{
	if (icl_get_dimm_size(val) == 0)
		return 0;

	val = (val & ICL_DRAM_RANK_MASK) >> ICL_DRAM_RANK_SHIFT;

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
	if (GRAPHICS_VER(i915) >= 11) {
		dimm->size = icl_get_dimm_size(val);
		dimm->width = icl_get_dimm_width(val);
		dimm->ranks = icl_get_dimm_ranks(val);
	} else {
		dimm->size = skl_get_dimm_size(val);
		dimm->width = skl_get_dimm_width(val);
		dimm->ranks = skl_get_dimm_ranks(val);
	}

	drm_dbg_kms(&i915->drm,
		    "CH%u DIMM %c size: %u Gb, width: X%u, ranks: %u, 16Gb DIMMs: %s\n",
		    channel, dimm_name, dimm->size, dimm->width, dimm->ranks,
		    str_yes_no(skl_is_16gb_dimm(dimm)));
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
		    channel, ch->ranks, str_yes_no(ch->is_16gb_dimm));

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
		    str_yes_no(dram_info->symmetric_memory));

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
	int ret;

	dram_info->type = skl_get_dram_type(i915);
	drm_dbg_kms(&i915->drm, "DRAM type: %s\n",
		    intel_dram_type_str(dram_info->type));

	ret = skl_dram_get_channels_info(i915);
	if (ret)
		return ret;

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
	u32 val;
	u8 valid_ranks = 0;
	int i;

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

	ret = snb_pcode_read(&dev_priv->uncore, ICL_PCODE_MEM_SUBSYSYSTEM_INFO |
			     ICL_PCODE_MEM_SS_READ_GLOBAL_INFO, &val, NULL);
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
			return -EINVAL;
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
			return -EINVAL;
		}
	}

	dram_info->num_channels = (val & 0xf0) >> 4;
	dram_info->num_qgv_points = (val & 0xf00) >> 8;
	dram_info->num_psf_gv_points = (val & 0x3000) >> 12;

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
	i915->dram_info.wm_lv_0_adjust_needed = false;

	return icl_pcode_read_mem_global_info(i915);
}

static int xelpdp_get_dram_info(struct drm_i915_private *i915)
{
	u32 val = intel_uncore_read(&i915->uncore, MTL_MEM_SS_INFO_GLOBAL);
	struct dram_info *dram_info = &i915->dram_info;

	switch (REG_FIELD_GET(MTL_DDR_TYPE_MASK, val)) {
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
		MISSING_CASE(val);
		return -EINVAL;
	}

	dram_info->num_channels = REG_FIELD_GET(MTL_N_OF_POPULATED_CH_MASK, val);
	dram_info->num_qgv_points = REG_FIELD_GET(MTL_N_OF_ENABLED_QGV_POINTS_MASK, val);
	/* PSF GV points not supported in D14+ */

	return 0;
}

void intel_dram_detect(struct drm_i915_private *i915)
{
	struct dram_info *dram_info = &i915->dram_info;
	int ret;

	detect_mem_freq(i915);

	if (GRAPHICS_VER(i915) < 9 || IS_DG2(i915) || !HAS_DISPLAY(i915))
		return;

	/*
	 * Assume level 0 watermark latency adjustment is needed until proven
	 * otherwise, this w/a is not needed by bxt/glk.
	 */
	dram_info->wm_lv_0_adjust_needed = !IS_GEN9_LP(i915);

	if (DISPLAY_VER(i915) >= 14)
		ret = xelpdp_get_dram_info(i915);
	else if (GRAPHICS_VER(i915) >= 12)
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
		    str_yes_no(dram_info->wm_lv_0_adjust_needed));
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

	edram_cap = intel_uncore_read_fw(&i915->uncore, HSW_EDRAM_CAP);

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

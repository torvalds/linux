// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <linux/string_helpers.h>

#include <drm/drm_managed.h>
#include <drm/drm_print.h>

#include "i915_reg.h"
#include "intel_display_core.h"
#include "intel_display_utils.h"
#include "intel_dram.h"
#include "intel_mchbar_regs.h"
#include "intel_pcode.h"
#include "intel_uncore.h"
#include "vlv_iosf_sb.h"

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

const char *intel_dram_type_str(enum intel_dram_type type)
{
	static const char * const str[] = {
		DRAM_TYPE_STR(UNKNOWN),
		DRAM_TYPE_STR(DDR2),
		DRAM_TYPE_STR(DDR3),
		DRAM_TYPE_STR(DDR4),
		DRAM_TYPE_STR(LPDDR3),
		DRAM_TYPE_STR(LPDDR4),
		DRAM_TYPE_STR(DDR5),
		DRAM_TYPE_STR(LPDDR5),
		DRAM_TYPE_STR(GDDR),
		DRAM_TYPE_STR(GDDR_ECC),
	};

	BUILD_BUG_ON(ARRAY_SIZE(str) != __INTEL_DRAM_TYPE_MAX);

	if (type >= ARRAY_SIZE(str))
		type = INTEL_DRAM_UNKNOWN;

	return str[type];
}

#undef DRAM_TYPE_STR

static enum intel_dram_type pnv_dram_type(struct intel_display *display)
{
	struct intel_uncore *uncore = to_intel_uncore(display->drm);

	return intel_uncore_read(uncore, CSHRDDR3CTL) & CSHRDDR3CTL_DDR3 ?
		INTEL_DRAM_DDR3 : INTEL_DRAM_DDR2;
}

static unsigned int pnv_mem_freq(struct intel_display *display)
{
	struct intel_uncore *uncore = to_intel_uncore(display->drm);
	u32 tmp;

	tmp = intel_uncore_read(uncore, CLKCFG);

	switch (tmp & CLKCFG_MEM_MASK) {
	case CLKCFG_MEM_533:
		return 533333;
	case CLKCFG_MEM_667:
		return 666667;
	case CLKCFG_MEM_800:
		return 800000;
	}

	return 0;
}

static unsigned int ilk_mem_freq(struct intel_display *display)
{
	struct intel_uncore *uncore = to_intel_uncore(display->drm);
	u16 ddrpll;

	ddrpll = intel_uncore_read16(uncore, DDRMPLL1);
	switch (ddrpll & 0xff) {
	case 0xc:
		return 800000;
	case 0x10:
		return 1066667;
	case 0x14:
		return 1333333;
	case 0x18:
		return 1600000;
	default:
		drm_dbg_kms(display->drm, "unknown memory frequency 0x%02x\n",
			    ddrpll & 0xff);
		return 0;
	}
}

static unsigned int chv_mem_freq(struct intel_display *display)
{
	u32 val;

	vlv_iosf_sb_get(display->drm, BIT(VLV_IOSF_SB_CCK));
	val = vlv_iosf_sb_read(display->drm, VLV_IOSF_SB_CCK, CCK_FUSE_REG);
	vlv_iosf_sb_put(display->drm, BIT(VLV_IOSF_SB_CCK));

	switch ((val >> 2) & 0x7) {
	case 3:
		return 2000000;
	default:
		return 1600000;
	}
}

static unsigned int vlv_mem_freq(struct intel_display *display)
{
	u32 val;

	vlv_iosf_sb_get(display->drm, BIT(VLV_IOSF_SB_PUNIT));
	val = vlv_iosf_sb_read(display->drm, VLV_IOSF_SB_PUNIT, PUNIT_REG_GPU_FREQ_STS);
	vlv_iosf_sb_put(display->drm, BIT(VLV_IOSF_SB_PUNIT));

	switch ((val >> 6) & 3) {
	case 0:
	case 1:
		return 800000;
	case 2:
		return 1066667;
	case 3:
		return 1333333;
	}

	return 0;
}

unsigned int intel_mem_freq(struct intel_display *display)
{
	if (display->platform.pineview)
		return pnv_mem_freq(display);
	else if (DISPLAY_VER(display) == 5)
		return ilk_mem_freq(display);
	else if (display->platform.cherryview)
		return chv_mem_freq(display);
	else if (display->platform.valleyview)
		return vlv_mem_freq(display);
	else
		return 0;
}

static unsigned int i9xx_fsb_freq(struct intel_display *display)
{
	struct intel_uncore *uncore = to_intel_uncore(display->drm);
	u32 fsb;

	/*
	 * Note that this only reads the state of the FSB
	 * straps, not the actual FSB frequency. Some BIOSen
	 * let you configure each independently. Ideally we'd
	 * read out the actual FSB frequency but sadly we
	 * don't know which registers have that information,
	 * and all the relevant docs have gone to bit heaven :(
	 */
	fsb = intel_uncore_read(uncore, CLKCFG) & CLKCFG_FSB_MASK;

	if (display->platform.pineview || display->platform.mobile) {
		switch (fsb) {
		case CLKCFG_FSB_400:
			return 400000;
		case CLKCFG_FSB_533:
			return 533333;
		case CLKCFG_FSB_667:
			return 666667;
		case CLKCFG_FSB_800:
			return 800000;
		case CLKCFG_FSB_1067:
			return 1066667;
		case CLKCFG_FSB_1333:
			return 1333333;
		default:
			MISSING_CASE(fsb);
			return 1333333;
		}
	} else {
		switch (fsb) {
		case CLKCFG_FSB_400_ALT:
			return 400000;
		case CLKCFG_FSB_533:
			return 533333;
		case CLKCFG_FSB_667:
			return 666667;
		case CLKCFG_FSB_800:
			return 800000;
		case CLKCFG_FSB_1067_ALT:
			return 1066667;
		case CLKCFG_FSB_1333_ALT:
			return 1333333;
		case CLKCFG_FSB_1600_ALT:
			return 1600000;
		default:
			MISSING_CASE(fsb);
			return 1333333;
		}
	}
}

static unsigned int ilk_fsb_freq(struct intel_display *display)
{
	struct intel_uncore *uncore = to_intel_uncore(display->drm);
	u16 fsb;

	fsb = intel_uncore_read16(uncore, CSIPLL0) & 0x3ff;

	switch (fsb) {
	case 0x00c:
		return 3200000;
	case 0x00e:
		return 3733333;
	case 0x010:
		return 4266667;
	case 0x012:
		return 4800000;
	case 0x014:
		return 5333333;
	case 0x016:
		return 5866667;
	case 0x018:
		return 6400000;
	default:
		drm_dbg_kms(display->drm, "unknown fsb frequency 0x%04x\n", fsb);
		return 0;
	}
}

unsigned int intel_fsb_freq(struct intel_display *display)
{
	if (DISPLAY_VER(display) == 5)
		return ilk_fsb_freq(display);
	else if (IS_DISPLAY_VER(display, 3, 4))
		return i9xx_fsb_freq(display);
	else
		return 0;
}

static int i915_get_dram_info(struct intel_display *display, struct dram_info *dram_info)
{
	dram_info->fsb_freq = intel_fsb_freq(display);
	if (dram_info->fsb_freq)
		drm_dbg_kms(display->drm, "FSB frequency: %d kHz\n", dram_info->fsb_freq);

	dram_info->mem_freq = intel_mem_freq(display);
	if (dram_info->mem_freq)
		drm_dbg_kms(display->drm, "DDR speed: %d kHz\n", dram_info->mem_freq);

	if (display->platform.pineview)
		dram_info->type = pnv_dram_type(display);

	return 0;
}

static int intel_dimm_num_devices(const struct dram_dimm_info *dimm)
{
	return dimm->ranks * 64 / (dimm->width ?: 1);
}

/* Returns total Gb for the whole DIMM */
static int skl_get_dimm_s_size(u32 val)
{
	return REG_FIELD_GET(SKL_DIMM_S_SIZE_MASK, val) * 8;
}

static int skl_get_dimm_l_size(u32 val)
{
	return REG_FIELD_GET(SKL_DIMM_L_SIZE_MASK, val) * 8;
}

static int skl_get_dimm_s_width(u32 val)
{
	if (skl_get_dimm_s_size(val) == 0)
		return 0;

	switch (val & SKL_DIMM_S_WIDTH_MASK) {
	case SKL_DIMM_S_WIDTH_X8:
	case SKL_DIMM_S_WIDTH_X16:
	case SKL_DIMM_S_WIDTH_X32:
		return 8 << REG_FIELD_GET(SKL_DIMM_S_WIDTH_MASK, val);
	default:
		MISSING_CASE(val);
		return 0;
	}
}

static int skl_get_dimm_l_width(u32 val)
{
	if (skl_get_dimm_l_size(val) == 0)
		return 0;

	switch (val & SKL_DIMM_L_WIDTH_MASK) {
	case SKL_DIMM_L_WIDTH_X8:
	case SKL_DIMM_L_WIDTH_X16:
	case SKL_DIMM_L_WIDTH_X32:
		return 8 << REG_FIELD_GET(SKL_DIMM_L_WIDTH_MASK, val);
	default:
		MISSING_CASE(val);
		return 0;
	}
}

static int skl_get_dimm_s_ranks(u32 val)
{
	if (skl_get_dimm_s_size(val) == 0)
		return 0;

	return REG_FIELD_GET(SKL_DIMM_S_RANK_MASK, val) + 1;
}

static int skl_get_dimm_l_ranks(u32 val)
{
	if (skl_get_dimm_l_size(val) == 0)
		return 0;

	return REG_FIELD_GET(SKL_DIMM_L_RANK_MASK, val) + 1;
}

/* Returns total Gb for the whole DIMM */
static int icl_get_dimm_s_size(u32 val)
{
	return REG_FIELD_GET(ICL_DIMM_S_SIZE_MASK, val) * 8 / 2;
}

static int icl_get_dimm_l_size(u32 val)
{
	return REG_FIELD_GET(ICL_DIMM_L_SIZE_MASK, val) * 8 / 2;
}

static int icl_get_dimm_s_width(u32 val)
{
	if (icl_get_dimm_s_size(val) == 0)
		return 0;

	switch (val & ICL_DIMM_S_WIDTH_MASK) {
	case ICL_DIMM_S_WIDTH_X8:
	case ICL_DIMM_S_WIDTH_X16:
	case ICL_DIMM_S_WIDTH_X32:
		return 8 << REG_FIELD_GET(ICL_DIMM_S_WIDTH_MASK, val);
	default:
		MISSING_CASE(val);
		return 0;
	}
}

static int icl_get_dimm_l_width(u32 val)
{
	if (icl_get_dimm_l_size(val) == 0)
		return 0;

	switch (val & ICL_DIMM_L_WIDTH_MASK) {
	case ICL_DIMM_L_WIDTH_X8:
	case ICL_DIMM_L_WIDTH_X16:
	case ICL_DIMM_L_WIDTH_X32:
		return 8 << REG_FIELD_GET(ICL_DIMM_L_WIDTH_MASK, val);
	default:
		MISSING_CASE(val);
		return 0;
	}
}

static int icl_get_dimm_s_ranks(u32 val)
{
	if (icl_get_dimm_s_size(val) == 0)
		return 0;

	return REG_FIELD_GET(ICL_DIMM_S_RANK_MASK, val) + 1;
}

static int icl_get_dimm_l_ranks(u32 val)
{
	if (icl_get_dimm_l_size(val) == 0)
		return 0;

	return REG_FIELD_GET(ICL_DIMM_L_RANK_MASK, val) + 1;
}

static bool
skl_is_16gb_dimm(const struct dram_dimm_info *dimm)
{
	/* Convert total Gb to Gb per DRAM device */
	return dimm->size / (intel_dimm_num_devices(dimm) ?: 1) >= 16;
}

static void
skl_dram_print_dimm_info(struct intel_display *display,
			 struct dram_dimm_info *dimm,
			 int channel, char dimm_name)
{
	drm_dbg_kms(display->drm,
		    "CH%u DIMM %c size: %u Gb, width: X%u, ranks: %u, 16Gb+ DIMMs: %s\n",
		    channel, dimm_name, dimm->size, dimm->width, dimm->ranks,
		    str_yes_no(skl_is_16gb_dimm(dimm)));
}

static void
skl_dram_get_dimm_l_info(struct intel_display *display,
			 struct dram_dimm_info *dimm,
			 int channel, u32 val)
{
	if (DISPLAY_VER(display) >= 11) {
		dimm->size = icl_get_dimm_l_size(val);
		dimm->width = icl_get_dimm_l_width(val);
		dimm->ranks = icl_get_dimm_l_ranks(val);
	} else {
		dimm->size = skl_get_dimm_l_size(val);
		dimm->width = skl_get_dimm_l_width(val);
		dimm->ranks = skl_get_dimm_l_ranks(val);
	}

	skl_dram_print_dimm_info(display, dimm, channel, 'L');
}

static void
skl_dram_get_dimm_s_info(struct intel_display *display,
			 struct dram_dimm_info *dimm,
			 int channel, u32 val)
{
	if (DISPLAY_VER(display) >= 11) {
		dimm->size = icl_get_dimm_s_size(val);
		dimm->width = icl_get_dimm_s_width(val);
		dimm->ranks = icl_get_dimm_s_ranks(val);
	} else {
		dimm->size = skl_get_dimm_s_size(val);
		dimm->width = skl_get_dimm_s_width(val);
		dimm->ranks = skl_get_dimm_s_ranks(val);
	}

	skl_dram_print_dimm_info(display, dimm, channel, 'S');
}

static int
skl_dram_get_channel_info(struct intel_display *display,
			  struct dram_channel_info *ch,
			  int channel, u32 val)
{
	skl_dram_get_dimm_l_info(display, &ch->dimm_l, channel, val);
	skl_dram_get_dimm_s_info(display, &ch->dimm_s, channel, val);

	if (ch->dimm_l.size == 0 && ch->dimm_s.size == 0) {
		drm_dbg_kms(display->drm, "CH%u not populated\n", channel);
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

	drm_dbg_kms(display->drm, "CH%u ranks: %u, 16Gb+ DIMMs: %s\n",
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
skl_dram_get_channels_info(struct intel_display *display, struct dram_info *dram_info)
{
	struct intel_uncore *uncore = to_intel_uncore(display->drm);
	struct dram_channel_info ch0 = {}, ch1 = {};
	u32 val;
	int ret;

	/* Assume 16Gb+ DIMMs are present until proven otherwise */
	dram_info->has_16gb_dimms = true;

	val = intel_uncore_read(uncore, SKL_MAD_DIMM_CH0_0_0_0_MCHBAR_MCMAIN);
	ret = skl_dram_get_channel_info(display, &ch0, 0, val);
	if (ret == 0)
		dram_info->num_channels++;

	val = intel_uncore_read(uncore, SKL_MAD_DIMM_CH1_0_0_0_MCHBAR_MCMAIN);
	ret = skl_dram_get_channel_info(display, &ch1, 1, val);
	if (ret == 0)
		dram_info->num_channels++;

	if (dram_info->num_channels == 0) {
		drm_info(display->drm, "Number of memory channels is zero\n");
		return -EINVAL;
	}

	if (ch0.ranks == 0 && ch1.ranks == 0) {
		drm_info(display->drm, "couldn't get memory rank information\n");
		return -EINVAL;
	}

	dram_info->has_16gb_dimms = ch0.is_16gb_dimm || ch1.is_16gb_dimm;

	dram_info->symmetric_memory = intel_is_dram_symmetric(&ch0, &ch1);

	drm_dbg_kms(display->drm, "Memory configuration is symmetric? %s\n",
		    str_yes_no(dram_info->symmetric_memory));

	drm_dbg_kms(display->drm, "16Gb+ DIMMs: %s\n",
		    str_yes_no(dram_info->has_16gb_dimms));

	return 0;
}

static enum intel_dram_type
skl_get_dram_type(struct intel_display *display)
{
	struct intel_uncore *uncore = to_intel_uncore(display->drm);
	u32 val;

	val = intel_uncore_read(uncore, SKL_MAD_INTER_CHANNEL_0_0_0_MCHBAR_MCMAIN);

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
skl_get_dram_info(struct intel_display *display, struct dram_info *dram_info)
{
	int ret;

	dram_info->type = skl_get_dram_type(display);

	ret = skl_dram_get_channels_info(display, dram_info);
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

static int bxt_get_dram_info(struct intel_display *display, struct dram_info *dram_info)
{
	struct intel_uncore *uncore = to_intel_uncore(display->drm);
	u32 val;
	u8 valid_ranks = 0;
	int i;

	/*
	 * Now read each DUNIT8/9/10/11 to check the rank of each dimms.
	 */
	for (i = BXT_D_CR_DRP0_DUNIT_START; i <= BXT_D_CR_DRP0_DUNIT_END; i++) {
		struct dram_dimm_info dimm;
		enum intel_dram_type type;

		val = intel_uncore_read(uncore, BXT_D_CR_DRP0_DUNIT(i));
		if (val == 0xFFFFFFFF)
			continue;

		dram_info->num_channels++;

		bxt_get_dimm_info(&dimm, val);
		type = bxt_get_dimm_type(val);

		drm_WARN_ON(display->drm, type != INTEL_DRAM_UNKNOWN &&
			    dram_info->type != INTEL_DRAM_UNKNOWN &&
			    dram_info->type != type);

		drm_dbg_kms(display->drm,
			    "CH%u DIMM size: %u Gb, width: X%u, ranks: %u\n",
			    i - BXT_D_CR_DRP0_DUNIT_START,
			    dimm.size, dimm.width, dimm.ranks);

		if (valid_ranks == 0)
			valid_ranks = dimm.ranks;

		if (type != INTEL_DRAM_UNKNOWN)
			dram_info->type = type;
	}

	if (dram_info->type == INTEL_DRAM_UNKNOWN || valid_ranks == 0) {
		drm_info(display->drm, "couldn't get memory information\n");
		return -EINVAL;
	}

	return 0;
}

static int icl_pcode_read_mem_global_info(struct intel_display *display,
					  struct dram_info *dram_info)
{
	u32 val = 0;
	int ret;

	ret = intel_pcode_read(display->drm, ICL_PCODE_MEM_SUBSYSYSTEM_INFO |
			       ICL_PCODE_MEM_SS_READ_GLOBAL_INFO, &val, NULL);
	if (ret)
		return ret;

	if (DISPLAY_VER(display) >= 12) {
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

static int gen11_get_dram_info(struct intel_display *display, struct dram_info *dram_info)
{
	int ret;

	ret = skl_dram_get_channels_info(display, dram_info);
	if (ret)
		return ret;

	return icl_pcode_read_mem_global_info(display, dram_info);
}

static int gen12_get_dram_info(struct intel_display *display, struct dram_info *dram_info)
{
	return icl_pcode_read_mem_global_info(display, dram_info);
}

static int xelpdp_get_dram_info(struct intel_display *display, struct dram_info *dram_info)
{
	struct intel_uncore *uncore = to_intel_uncore(display->drm);
	u32 val = intel_uncore_read(uncore, MTL_MEM_SS_INFO_GLOBAL);

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
	case 8:
		drm_WARN_ON(display->drm, !display->platform.dgfx);
		dram_info->type = INTEL_DRAM_GDDR;
		break;
	case 9:
		drm_WARN_ON(display->drm, !display->platform.dgfx);
		dram_info->type = INTEL_DRAM_GDDR_ECC;
		break;
	default:
		MISSING_CASE(val);
		return -EINVAL;
	}

	dram_info->num_channels = REG_FIELD_GET(MTL_N_OF_POPULATED_CH_MASK, val);
	dram_info->num_qgv_points = REG_FIELD_GET(MTL_N_OF_ENABLED_QGV_POINTS_MASK, val);
	/* PSF GV points not supported in D14+ */

	if (DISPLAY_VER(display) >= 35)
		dram_info->ecc_impacting_de_bw = REG_FIELD_GET(XE3P_ECC_IMPACTING_DE, val);

	return 0;
}

int intel_dram_detect(struct intel_display *display)
{
	struct dram_info *dram_info;
	int ret;

	if (display->platform.dg2 || !HAS_DISPLAY(display))
		return 0;

	dram_info = drmm_kzalloc(display->drm, sizeof(*dram_info), GFP_KERNEL);
	if (!dram_info)
		return -ENOMEM;

	display->dram.info = dram_info;

	if (DISPLAY_VER(display) >= 14)
		ret = xelpdp_get_dram_info(display, dram_info);
	else if (DISPLAY_VER(display) >= 12)
		ret = gen12_get_dram_info(display, dram_info);
	else if (DISPLAY_VER(display) >= 11)
		ret = gen11_get_dram_info(display, dram_info);
	else if (display->platform.broxton || display->platform.geminilake)
		ret = bxt_get_dram_info(display, dram_info);
	else if (DISPLAY_VER(display) >= 9)
		ret = skl_get_dram_info(display, dram_info);
	else
		ret = i915_get_dram_info(display, dram_info);

	drm_dbg_kms(display->drm, "DRAM type: %s\n",
		    intel_dram_type_str(dram_info->type));

	drm_dbg_kms(display->drm, "DRAM channels: %u\n", dram_info->num_channels);

	drm_dbg_kms(display->drm, "Num QGV points %u\n", dram_info->num_qgv_points);
	drm_dbg_kms(display->drm, "Num PSF GV points %u\n", dram_info->num_psf_gv_points);

	/* TODO: Do we want to abort probe on dram detection failures? */
	if (ret)
		return 0;

	return 0;
}

/*
 * Returns NULL for platforms that don't have dram info. Avoid overzealous NULL
 * checks, and prefer not dereferencing on platforms that shouldn't look at dram
 * info, to catch accidental and incorrect dram info checks.
 */
const struct dram_info *intel_dram_info(struct intel_display *display)
{
	return display->dram.info;
}

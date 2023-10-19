// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) Jernej Skrabec <jernej.skrabec@siol.net>
 */

#include <drm/drm_print.h>

#include "sun8i_csc.h"
#include "sun8i_mixer.h"

static const u32 ccsc_base[][2] = {
	[CCSC_MIXER0_LAYOUT]	= {CCSC00_OFFSET, CCSC01_OFFSET},
	[CCSC_MIXER1_LAYOUT]	= {CCSC10_OFFSET, CCSC11_OFFSET},
	[CCSC_D1_MIXER0_LAYOUT]	= {CCSC00_OFFSET, CCSC01_D1_OFFSET},
};

/*
 * Factors are in two's complement format, 10 bits for fractinal part.
 * First tree values in each line are multiplication factor and last
 * value is constant, which is added at the end.
 */

static const u32 yuv2rgb[2][2][12] = {
	[DRM_COLOR_YCBCR_LIMITED_RANGE] = {
		[DRM_COLOR_YCBCR_BT601] = {
			0x000004A8, 0x00000000, 0x00000662, 0xFFFC8451,
			0x000004A8, 0xFFFFFE6F, 0xFFFFFCC0, 0x00021E4D,
			0x000004A8, 0x00000811, 0x00000000, 0xFFFBACA9,
		},
		[DRM_COLOR_YCBCR_BT709] = {
			0x000004A8, 0x00000000, 0x0000072B, 0xFFFC1F99,
			0x000004A8, 0xFFFFFF26, 0xFFFFFDDF, 0x00013383,
			0x000004A8, 0x00000873, 0x00000000, 0xFFFB7BEF,
		}
	},
	[DRM_COLOR_YCBCR_FULL_RANGE] = {
		[DRM_COLOR_YCBCR_BT601] = {
			0x00000400, 0x00000000, 0x0000059B, 0xFFFD322E,
			0x00000400, 0xFFFFFEA0, 0xFFFFFD25, 0x00021DD5,
			0x00000400, 0x00000716, 0x00000000, 0xFFFC74BD,
		},
		[DRM_COLOR_YCBCR_BT709] = {
			0x00000400, 0x00000000, 0x0000064C, 0xFFFCD9B4,
			0x00000400, 0xFFFFFF41, 0xFFFFFE21, 0x00014F96,
			0x00000400, 0x0000076C, 0x00000000, 0xFFFC49EF,
		}
	},
};

/*
 * DE3 has a bit different CSC units. Factors are in two's complement format.
 * First three factors in a row are multiplication factors which have 17 bits
 * for fractional part. Fourth value in a row is comprised of two factors.
 * Upper 16 bits represents difference, which is subtracted from the input
 * value before multiplication and lower 16 bits represents constant, which
 * is addes at the end.
 *
 * x' = c00 * (x + d0) + c01 * (y + d1) + c02 * (z + d2) + const0
 * y' = c10 * (x + d0) + c11 * (y + d1) + c12 * (z + d2) + const1
 * z' = c20 * (x + d0) + c21 * (y + d1) + c22 * (z + d2) + const2
 *
 * Please note that above formula is true only for Blender CSC. Other DE3 CSC
 * units takes only positive value for difference. From what can be deducted
 * from BSP driver code, those units probably automatically assume that
 * difference has to be subtracted.
 *
 * Layout of factors in table:
 * c00 c01 c02 [d0 const0]
 * c10 c11 c12 [d1 const1]
 * c20 c21 c22 [d2 const2]
 */

static const u32 yuv2rgb_de3[2][3][12] = {
	[DRM_COLOR_YCBCR_LIMITED_RANGE] = {
		[DRM_COLOR_YCBCR_BT601] = {
			0x0002542A, 0x00000000, 0x0003312A, 0xFFC00000,
			0x0002542A, 0xFFFF376B, 0xFFFE5FC3, 0xFE000000,
			0x0002542A, 0x000408D2, 0x00000000, 0xFE000000,
		},
		[DRM_COLOR_YCBCR_BT709] = {
			0x0002542A, 0x00000000, 0x000395E2, 0xFFC00000,
			0x0002542A, 0xFFFF92D2, 0xFFFEEF27, 0xFE000000,
			0x0002542A, 0x0004398C, 0x00000000, 0xFE000000,
		},
		[DRM_COLOR_YCBCR_BT2020] = {
			0x0002542A, 0x00000000, 0x00035B7B, 0xFFC00000,
			0x0002542A, 0xFFFFA017, 0xFFFEB2FC, 0xFE000000,
			0x0002542A, 0x00044896, 0x00000000, 0xFE000000,
		}
	},
	[DRM_COLOR_YCBCR_FULL_RANGE] = {
		[DRM_COLOR_YCBCR_BT601] = {
			0x00020000, 0x00000000, 0x0002CDD2, 0x00000000,
			0x00020000, 0xFFFF4FCE, 0xFFFE925D, 0xFE000000,
			0x00020000, 0x00038B43, 0x00000000, 0xFE000000,
		},
		[DRM_COLOR_YCBCR_BT709] = {
			0x00020000, 0x00000000, 0x0003264C, 0x00000000,
			0x00020000, 0xFFFFA018, 0xFFFF1053, 0xFE000000,
			0x00020000, 0x0003B611, 0x00000000, 0xFE000000,
		},
		[DRM_COLOR_YCBCR_BT2020] = {
			0x00020000, 0x00000000, 0x0002F2FE, 0x00000000,
			0x00020000, 0xFFFFABC0, 0xFFFEDB78, 0xFE000000,
			0x00020000, 0x0003C346, 0x00000000, 0xFE000000,
		}
	},
};

static void sun8i_csc_set_coefficients(struct regmap *map, u32 base,
				       enum sun8i_csc_mode mode,
				       enum drm_color_encoding encoding,
				       enum drm_color_range range)
{
	const u32 *table;
	u32 base_reg;
	int i;

	table = yuv2rgb[range][encoding];

	switch (mode) {
	case SUN8I_CSC_MODE_YUV2RGB:
		base_reg = SUN8I_CSC_COEFF(base, 0);
		regmap_bulk_write(map, base_reg, table, 12);
		break;
	case SUN8I_CSC_MODE_YVU2RGB:
		for (i = 0; i < 12; i++) {
			if ((i & 3) == 1)
				base_reg = SUN8I_CSC_COEFF(base, i + 1);
			else if ((i & 3) == 2)
				base_reg = SUN8I_CSC_COEFF(base, i - 1);
			else
				base_reg = SUN8I_CSC_COEFF(base, i);
			regmap_write(map, base_reg, table[i]);
		}
		break;
	default:
		DRM_WARN("Wrong CSC mode specified.\n");
		return;
	}
}

static void sun8i_de3_ccsc_set_coefficients(struct regmap *map, int layer,
					    enum sun8i_csc_mode mode,
					    enum drm_color_encoding encoding,
					    enum drm_color_range range)
{
	const u32 *table;
	u32 addr;
	int i;

	table = yuv2rgb_de3[range][encoding];

	switch (mode) {
	case SUN8I_CSC_MODE_YUV2RGB:
		addr = SUN50I_MIXER_BLEND_CSC_COEFF(DE3_BLD_BASE, layer, 0);
		regmap_bulk_write(map, addr, table, 12);
		break;
	case SUN8I_CSC_MODE_YVU2RGB:
		for (i = 0; i < 12; i++) {
			if ((i & 3) == 1)
				addr = SUN50I_MIXER_BLEND_CSC_COEFF(DE3_BLD_BASE,
								    layer,
								    i + 1);
			else if ((i & 3) == 2)
				addr = SUN50I_MIXER_BLEND_CSC_COEFF(DE3_BLD_BASE,
								    layer,
								    i - 1);
			else
				addr = SUN50I_MIXER_BLEND_CSC_COEFF(DE3_BLD_BASE,
								    layer, i);
			regmap_write(map, addr, table[i]);
		}
		break;
	default:
		DRM_WARN("Wrong CSC mode specified.\n");
		return;
	}
}

static void sun8i_csc_enable(struct regmap *map, u32 base, bool enable)
{
	u32 val;

	if (enable)
		val = SUN8I_CSC_CTRL_EN;
	else
		val = 0;

	regmap_update_bits(map, SUN8I_CSC_CTRL(base), SUN8I_CSC_CTRL_EN, val);
}

static void sun8i_de3_ccsc_enable(struct regmap *map, int layer, bool enable)
{
	u32 val, mask;

	mask = SUN50I_MIXER_BLEND_CSC_CTL_EN(layer);

	if (enable)
		val = mask;
	else
		val = 0;

	regmap_update_bits(map, SUN50I_MIXER_BLEND_CSC_CTL(DE3_BLD_BASE),
			   mask, val);
}

void sun8i_csc_set_ccsc_coefficients(struct sun8i_mixer *mixer, int layer,
				     enum sun8i_csc_mode mode,
				     enum drm_color_encoding encoding,
				     enum drm_color_range range)
{
	u32 base;

	if (mixer->cfg->is_de3) {
		sun8i_de3_ccsc_set_coefficients(mixer->engine.regs, layer,
						mode, encoding, range);
		return;
	}

	base = ccsc_base[mixer->cfg->ccsc][layer];

	sun8i_csc_set_coefficients(mixer->engine.regs, base,
				   mode, encoding, range);
}

void sun8i_csc_enable_ccsc(struct sun8i_mixer *mixer, int layer, bool enable)
{
	u32 base;

	if (mixer->cfg->is_de3) {
		sun8i_de3_ccsc_enable(mixer->engine.regs, layer, enable);
		return;
	}

	base = ccsc_base[mixer->cfg->ccsc][layer];

	sun8i_csc_enable(mixer->engine.regs, base, enable);
}

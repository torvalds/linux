/*
 * Copyright (C) Jernej Skrabec <jernej.skrabec@siol.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <drm/drmP.h>

#include "sun8i_csc.h"
#include "sun8i_mixer.h"

static const u32 ccsc_base[2][2] = {
	{CCSC00_OFFSET, CCSC01_OFFSET},
	{CCSC10_OFFSET, CCSC11_OFFSET},
};

/*
 * Factors are in two's complement format, 10 bits for fractinal part.
 * First tree values in each line are multiplication factor and last
 * value is constant, which is added at the end.
 */
static const u32 yuv2rgb[] = {
	0x000004A8, 0x00000000, 0x00000662, 0xFFFC845A,
	0x000004A8, 0xFFFFFE6F, 0xFFFFFCBF, 0x00021DF4,
	0x000004A8, 0x00000813, 0x00000000, 0xFFFBAC4A,
};

static const u32 yvu2rgb[] = {
	0x000004A8, 0x00000662, 0x00000000, 0xFFFC845A,
	0x000004A8, 0xFFFFFCBF, 0xFFFFFE6F, 0x00021DF4,
	0x000004A8, 0x00000000, 0x00000813, 0xFFFBAC4A,
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

static const u32 yuv2rgb_de3[] = {
	0x0002542a, 0x00000000, 0x0003312a, 0xffc00000,
	0x0002542a, 0xffff376b, 0xfffe5fc3, 0xfe000000,
	0x0002542a, 0x000408d3, 0x00000000, 0xfe000000,
};

static const u32 yvu2rgb_de3[] = {
	0x0002542a, 0x0003312a, 0x00000000, 0xffc00000,
	0x0002542a, 0xfffe5fc3, 0xffff376b, 0xfe000000,
	0x0002542a, 0x00000000, 0x000408d3, 0xfe000000,
};

static void sun8i_csc_set_coefficients(struct regmap *map, u32 base,
				       enum sun8i_csc_mode mode)
{
	const u32 *table;
	int i, data;

	switch (mode) {
	case SUN8I_CSC_MODE_YUV2RGB:
		table = yuv2rgb;
		break;
	case SUN8I_CSC_MODE_YVU2RGB:
		table = yvu2rgb;
		break;
	default:
		DRM_WARN("Wrong CSC mode specified.\n");
		return;
	}

	for (i = 0; i < 12; i++) {
		data = table[i];
		/* For some reason, 0x200 must be added to constant parts */
		if (((i + 1) & 3) == 0)
			data += 0x200;
		regmap_write(map, SUN8I_CSC_COEFF(base, i), data);
	}
}

static void sun8i_de3_ccsc_set_coefficients(struct regmap *map, int layer,
					    enum sun8i_csc_mode mode)
{
	const u32 *table;
	u32 base_reg;

	switch (mode) {
	case SUN8I_CSC_MODE_YUV2RGB:
		table = yuv2rgb_de3;
		break;
	case SUN8I_CSC_MODE_YVU2RGB:
		table = yvu2rgb_de3;
		break;
	default:
		DRM_WARN("Wrong CSC mode specified.\n");
		return;
	}

	base_reg = SUN50I_MIXER_BLEND_CSC_COEFF(DE3_BLD_BASE, layer, 0, 0);
	regmap_bulk_write(map, base_reg, table, 12);
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
				     enum sun8i_csc_mode mode)
{
	u32 base;

	if (mixer->cfg->is_de3) {
		sun8i_de3_ccsc_set_coefficients(mixer->engine.regs,
						layer, mode);
		return;
	}

	base = ccsc_base[mixer->cfg->ccsc][layer];

	sun8i_csc_set_coefficients(mixer->engine.regs, base, mode);
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

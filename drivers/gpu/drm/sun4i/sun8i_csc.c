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

static void sun8i_csc_enable(struct regmap *map, u32 base, bool enable)
{
	u32 val;

	if (enable)
		val = SUN8I_CSC_CTRL_EN;
	else
		val = 0;

	regmap_update_bits(map, SUN8I_CSC_CTRL(base), SUN8I_CSC_CTRL_EN, val);
}

void sun8i_csc_set_ccsc_coefficients(struct sun8i_mixer *mixer, int layer,
				     enum sun8i_csc_mode mode)
{
	u32 base;

	base = ccsc_base[mixer->cfg->ccsc][layer];

	sun8i_csc_set_coefficients(mixer->engine.regs, base, mode);
}

void sun8i_csc_enable_ccsc(struct sun8i_mixer *mixer, int layer, bool enable)
{
	u32 base;

	base = ccsc_base[mixer->cfg->ccsc][layer];

	sun8i_csc_enable(mixer->engine.regs, base, enable);
}

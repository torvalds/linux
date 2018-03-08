/*
 * Copyright (C) Jernej Skrabec <jernej.skrabec@siol.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#ifndef _SUN8I_CSC_H_
#define _SUN8I_CSC_H_

struct sun8i_mixer;

/* VI channel CSC units offsets */
#define CCSC00_OFFSET 0xAA050
#define CCSC01_OFFSET 0xFA000
#define CCSC10_OFFSET 0xA0000
#define CCSC11_OFFSET 0xF0000

#define SUN8I_CSC_CTRL(base)		(base + 0x0)
#define SUN8I_CSC_COEFF(base, i)	(base + 0x10 + 4 * i)

#define SUN8I_CSC_CTRL_EN		BIT(0)

enum sun8i_csc_mode {
	SUN8I_CSC_MODE_OFF,
	SUN8I_CSC_MODE_YUV2RGB,
	SUN8I_CSC_MODE_YVU2RGB,
};

void sun8i_csc_set_ccsc_coefficients(struct sun8i_mixer *mixer, int layer,
				     enum sun8i_csc_mode mode);
void sun8i_csc_enable_ccsc(struct sun8i_mixer *mixer, int layer, bool enable);

#endif

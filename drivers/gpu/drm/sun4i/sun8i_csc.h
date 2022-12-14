/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) Jernej Skrabec <jernej.skrabec@siol.net>
 */

#ifndef _SUN8I_CSC_H_
#define _SUN8I_CSC_H_

#include <drm/drm_color_mgmt.h>

struct sun8i_mixer;

/* VI channel CSC units offsets */
#define CCSC00_OFFSET 0xAA050
#define CCSC01_OFFSET 0xFA050
#define CCSC01_D1_OFFSET 0xFA000
#define CCSC10_OFFSET 0xA0000
#define CCSC11_OFFSET 0xF0000

#define SUN8I_CSC_CTRL(base)		((base) + 0x0)
#define SUN8I_CSC_COEFF(base, i)	((base) + 0x10 + 4 * (i))

#define SUN8I_CSC_CTRL_EN		BIT(0)

enum sun8i_csc_mode {
	SUN8I_CSC_MODE_OFF,
	SUN8I_CSC_MODE_YUV2RGB,
	SUN8I_CSC_MODE_YVU2RGB,
};

void sun8i_csc_set_ccsc_coefficients(struct sun8i_mixer *mixer, int layer,
				     enum sun8i_csc_mode mode,
				     enum drm_color_encoding encoding,
				     enum drm_color_range range);
void sun8i_csc_enable_ccsc(struct sun8i_mixer *mixer, int layer, bool enable);

#endif

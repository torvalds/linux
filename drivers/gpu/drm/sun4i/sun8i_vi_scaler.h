/*
 * Copyright (C) 2017 Jernej Skrabec <jernej.skrabec@siol.net>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef _SUN8I_VI_SCALER_H_
#define _SUN8I_VI_SCALER_H_

#include <drm/drm_fourcc.h>
#include "sun8i_mixer.h"

/* this two macros assumes 16 fractional bits which is standard in DRM */
#define SUN8I_VI_SCALER_SCALE_MIN		1
#define SUN8I_VI_SCALER_SCALE_MAX		((1UL << 20) - 1)

#define SUN8I_VI_SCALER_SCALE_FRAC		20
#define SUN8I_VI_SCALER_PHASE_FRAC		20
#define SUN8I_VI_SCALER_COEFF_COUNT		32
#define SUN8I_VI_SCALER_SIZE(w, h)		(((h) - 1) << 16 | ((w) - 1))

#define SUN8I_SCALER_VSU_CTRL(ch)	(0x20000 + 0x20000 * (ch) + 0x0)
#define SUN8I_SCALER_VSU_OUTSIZE(ch)	(0x20000 + 0x20000 * (ch) + 0x40)
#define SUN8I_SCALER_VSU_YINSIZE(ch)	(0x20000 + 0x20000 * (ch) + 0x80)
#define SUN8I_SCALER_VSU_YHSTEP(ch)	(0x20000 + 0x20000 * (ch) + 0x88)
#define SUN8I_SCALER_VSU_YVSTEP(ch)	(0x20000 + 0x20000 * (ch) + 0x8c)
#define SUN8I_SCALER_VSU_YHPHASE(ch)	(0x20000 + 0x20000 * (ch) + 0x90)
#define SUN8I_SCALER_VSU_YVPHASE(ch)	(0x20000 + 0x20000 * (ch) + 0x98)
#define SUN8I_SCALER_VSU_CINSIZE(ch)	(0x20000 + 0x20000 * (ch) + 0xc0)
#define SUN8I_SCALER_VSU_CHSTEP(ch)	(0x20000 + 0x20000 * (ch) + 0xc8)
#define SUN8I_SCALER_VSU_CVSTEP(ch)	(0x20000 + 0x20000 * (ch) + 0xcc)
#define SUN8I_SCALER_VSU_CHPHASE(ch)	(0x20000 + 0x20000 * (ch) + 0xd0)
#define SUN8I_SCALER_VSU_CVPHASE(ch)	(0x20000 + 0x20000 * (ch) + 0xd8)
#define SUN8I_SCALER_VSU_YHCOEFF0(ch, i) \
	(0x20000 + 0x20000 * (ch) + 0x200 + 0x4 * (i))
#define SUN8I_SCALER_VSU_YHCOEFF1(ch, i) \
	(0x20000 + 0x20000 * (ch) + 0x300 + 0x4 * (i))
#define SUN8I_SCALER_VSU_YVCOEFF(ch, i) \
	(0x20000 + 0x20000 * (ch) + 0x400 + 0x4 * (i))
#define SUN8I_SCALER_VSU_CHCOEFF0(ch, i) \
	(0x20000 + 0x20000 * (ch) + 0x600 + 0x4 * (i))
#define SUN8I_SCALER_VSU_CHCOEFF1(ch, i) \
	(0x20000 + 0x20000 * (ch) + 0x700 + 0x4 * (i))
#define SUN8I_SCALER_VSU_CVCOEFF(ch, i) \
	(0x20000 + 0x20000 * (ch) + 0x800 + 0x4 * (i))

#define SUN8I_SCALER_VSU_CTRL_EN		BIT(0)
#define SUN8I_SCALER_VSU_CTRL_COEFF_RDY		BIT(4)

void sun8i_vi_scaler_enable(struct sun8i_mixer *mixer, int layer, bool enable);
void sun8i_vi_scaler_setup(struct sun8i_mixer *mixer, int layer,
			   u32 src_w, u32 src_h, u32 dst_w, u32 dst_h,
			   u32 hscale, u32 vscale, u32 hphase, u32 vphase,
			   const struct drm_format_info *format);

#endif

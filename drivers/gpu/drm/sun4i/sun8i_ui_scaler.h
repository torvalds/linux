/*
 * Copyright (C) 2017 Jernej Skrabec <jernej.skrabec@siol.net>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef _SUN8I_UI_SCALER_H_
#define _SUN8I_UI_SCALER_H_

#include "sun8i_mixer.h"

#define DE2_UI_SCALER_UNIT_SIZE 0x10000
#define DE3_UI_SCALER_UNIT_SIZE 0x08000

/* this two macros assumes 16 fractional bits which is standard in DRM */
#define SUN8I_UI_SCALER_SCALE_MIN		1
#define SUN8I_UI_SCALER_SCALE_MAX		((1UL << 20) - 1)

#define SUN8I_UI_SCALER_SCALE_FRAC		20
#define SUN8I_UI_SCALER_PHASE_FRAC		20
#define SUN8I_UI_SCALER_COEFF_COUNT		16
#define SUN8I_UI_SCALER_SIZE(w, h)		(((h) - 1) << 16 | ((w) - 1))

#define SUN8I_SCALER_GSU_CTRL(base)		((base) + 0x0)
#define SUN8I_SCALER_GSU_OUTSIZE(base)		((base) + 0x40)
#define SUN8I_SCALER_GSU_INSIZE(base)		((base) + 0x80)
#define SUN8I_SCALER_GSU_HSTEP(base)		((base) + 0x88)
#define SUN8I_SCALER_GSU_VSTEP(base)		((base) + 0x8c)
#define SUN8I_SCALER_GSU_HPHASE(base)		((base) + 0x90)
#define SUN8I_SCALER_GSU_VPHASE(base)		((base) + 0x98)
#define SUN8I_SCALER_GSU_HCOEFF(base, index)	((base) + 0x200 + 0x4 * (index))

#define SUN8I_SCALER_GSU_CTRL_EN		BIT(0)
#define SUN8I_SCALER_GSU_CTRL_COEFF_RDY		BIT(4)

void sun8i_ui_scaler_enable(struct sun8i_mixer *mixer, int layer, bool enable);
void sun8i_ui_scaler_setup(struct sun8i_mixer *mixer, int layer,
			   u32 src_w, u32 src_h, u32 dst_w, u32 dst_h,
			   u32 hscale, u32 vscale, u32 hphase, u32 vphase);

#endif

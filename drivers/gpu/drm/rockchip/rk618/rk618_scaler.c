/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "rk618_output.h"

#define RK618_SCALER_REG0		0x0030
#define SCL_VER_DOWN_MODE(x)		HIWORD_UPDATE(x, 8, 8)
#define SCL_HOR_DOWN_MODE(x)		HIWORD_UPDATE(x, 7, 7)
#define SCL_BIC_COE_SEL(x)		HIWORD_UPDATE(x, 6, 5)
#define SCL_VER_MODE(x)			HIWORD_UPDATE(x, 4, 3)
#define SCL_HOR_MODE(x)			HIWORD_UPDATE(x, 2, 1)
#define SCL_ENABLE			HIWORD_UPDATE(1, 0, 0)
#define SCL_DISABLE			HIWORD_UPDATE(0, 0, 0)
#define RK618_SCALER_REG1		0x0034
#define SCL_V_FACTOR(x)			UPDATE(x, 31, 16)
#define SCL_H_FACTOR(x)			UPDATE(x, 15, 0)
#define RK618_SCALER_REG2		0x0038
#define DSP_FRAME_VST(x)		UPDATE(x, 27, 16)
#define DSP_FRAME_HST(x)		UPDATE(x, 11, 0)
#define RK618_SCALER_REG3		0x003c
#define DSP_HS_END(x)			UPDATE(x, 23, 16)
#define DSP_HTOTAL(x)			UPDATE(x, 11, 0)
#define RK618_SCALER_REG4		0x0040
#define DSP_HACT_END(x)			UPDATE(x, 27, 16)
#define DSP_HACT_ST(x)			UPDATE(x, 11, 0)
#define RK618_SCALER_REG5		0x0044
#define DSP_VS_END(x)			UPDATE(x, 23, 16)
#define DSP_VTOTAL(x)			UPDATE(x, 11, 0)
#define RK618_SCALER_REG6		0x0048
#define DSP_VACT_END(x)			UPDATE(x, 27, 16)
#define DSP_VACT_ST(x)			UPDATE(x, 11, 0)
#define RK618_SCALER_REG7		0x004c
#define DSP_HBOR_END(x)			UPDATE(x, 27, 16)
#define DSP_HBOR_ST(x)			UPDATE(x, 11, 0)
#define RK618_SCALER_REG8		0x0050
#define DSP_VBOR_END(x)			UPDATE(x, 27, 16)
#define DSP_VBOR_ST(x)			UPDATE(x, 11, 0)

void rk618_scaler_enable(struct rk618 *rk618)
{
	regmap_write(rk618->regmap, RK618_SCALER_REG0, SCL_ENABLE);
}
EXPORT_SYMBOL(rk618_scaler_enable);

void rk618_scaler_disable(struct rk618 *rk618)
{
	regmap_write(rk618->regmap, RK618_SCALER_REG0, SCL_DISABLE);
}
EXPORT_SYMBOL(rk618_scaler_disable);

static void calc_dsp_frm_hst_vst(const struct videomode *src,
				 const struct videomode *dst,
				 u32 *dsp_frame_hst, u32 *dsp_frame_vst)
{
	u32 bp_in, bp_out;
	u32 v_scale_ratio;
	long long t_frm_st;
	u64 t_bp_in, t_bp_out, t_delta, tin;
	u32 src_pixclock, dst_pixclock;
	u32 dsp_htotal, dsp_vtotal, src_htotal, src_vtotal;

	src_pixclock = div_u64(1000000000000llu, src->pixelclock);
	dst_pixclock = div_u64(1000000000000llu, dst->pixelclock);

	src_htotal = src->hsync_len + src->hback_porch + src->hactive +
		     src->hfront_porch;
	src_vtotal = src->vsync_len + src->vback_porch + src->vactive +
		     src->vfront_porch;
	dsp_htotal = dst->hsync_len + dst->hback_porch + dst->hactive +
		     dst->hfront_porch;
	dsp_vtotal = dst->vsync_len + dst->vback_porch + dst->vactive +
		     dst->vfront_porch;

	bp_in = (src->vback_porch + src->vsync_len) * src_htotal +
		src->hsync_len + src->hback_porch;
	bp_out = (dst->vback_porch + dst->vsync_len) * dsp_htotal +
		 dst->hsync_len + dst->hback_porch;

	t_bp_in = bp_in * src_pixclock;
	t_bp_out = bp_out * dst_pixclock;
	tin = src_vtotal * src_htotal * src_pixclock;

	v_scale_ratio = src->vactive / dst->vactive;
	if (v_scale_ratio <= 2)
		t_delta = 5 * src_htotal * src_pixclock;
	else
		t_delta = 12 * src_htotal * src_pixclock;

	if (t_bp_in + t_delta > t_bp_out)
		t_frm_st = (t_bp_in + t_delta - t_bp_out);
	else
		t_frm_st = tin - (t_bp_out - (t_bp_in + t_delta));

	do_div(t_frm_st, src_pixclock);
	*dsp_frame_hst = do_div(t_frm_st, src_htotal) - 1;
	*dsp_frame_vst = t_frm_st;
}

void rk618_scaler_configure(struct rk618 *rk618,
			    const struct drm_display_mode *scale_mode,
			    const struct drm_display_mode *panel_mode)
{
	struct device *dev = rk618->dev;
	struct videomode src, dst;
	u32 dsp_frame_hst, dsp_frame_vst;
	u32 scl_hor_mode, scl_ver_mode;
	u32 scl_v_factor, scl_h_factor;
	u32 src_htotal, src_vtotal;
	u32 dsp_htotal, dsp_hs_end, dsp_hact_st, dsp_hact_end;
	u32 dsp_vtotal, dsp_vs_end, dsp_vact_st, dsp_vact_end;
	u32 dsp_hbor_end, dsp_hbor_st, dsp_vbor_end, dsp_vbor_st;
	u16 bor_right = 0, bor_left = 0, bor_up = 0, bor_down = 0;
	u8 hor_down_mode = 0, ver_down_mode = 0;

	drm_display_mode_to_videomode(scale_mode, &src);
	drm_display_mode_to_videomode(panel_mode, &dst);

	src_htotal = src.hsync_len + src.hback_porch + src.hactive +
		     src.hfront_porch;
	src_vtotal = src.vsync_len + src.vback_porch + src.vactive +
		     src.vfront_porch;
	dsp_htotal = dst.hsync_len + dst.hback_porch + dst.hactive +
		     dst.hfront_porch;
	dsp_vtotal = dst.vsync_len + dst.vback_porch + dst.vactive +
		     dst.vfront_porch;
	dsp_hs_end = dst.hsync_len;
	dsp_vs_end = dst.vsync_len;
	dsp_hbor_end = dst.hsync_len + dst.hback_porch + dst.hactive;
	dsp_hbor_st = dst.hsync_len + dst.hback_porch;
	dsp_vbor_end = dst.vsync_len + dst.vback_porch + dst.vactive;
	dsp_vbor_st = dst.vsync_len + dst.vback_porch;
	dsp_hact_st = dsp_hbor_st + bor_left;
	dsp_hact_end = dsp_hbor_end - bor_right;
	dsp_vact_st = dsp_vbor_st + bor_up;
	dsp_vact_end = dsp_vbor_end - bor_down;

	calc_dsp_frm_hst_vst(&src, &dst, &dsp_frame_hst, &dsp_frame_vst);
	dev_dbg(dev, "dsp_frame_vst=%d, dsp_frame_hst=%d\n",
		dsp_frame_vst, dsp_frame_hst);

	if (src.hactive > dst.hactive) {
		scl_hor_mode = 2;

		if (hor_down_mode == 0) {
			if ((src.hactive - 1) / (dst.hactive - 1) > 2)
				scl_h_factor = ((src.hactive - 1) << 14) /
					       (dst.hactive - 1);
			else
				scl_h_factor = ((src.hactive - 2) << 14) /
					       (dst.hactive - 1);
		} else {
			scl_h_factor = (dst.hactive << 16) /
				       (src.hactive - 1);
		}

		dev_dbg(rk618->dev, "horizontal scale down\n");
	} else if (src.hactive == dst.hactive) {
		scl_hor_mode = 0;
		scl_h_factor = 0;

		dev_dbg(rk618->dev, "horizontal no scale\n");
	} else {
		scl_hor_mode = 1;
		scl_h_factor = ((src.hactive - 1) << 16) / (dst.hactive - 1);

		dev_dbg(rk618->dev, "horizontal scale up\n");
	}

	if (src.vactive > dst.vactive) {
		scl_ver_mode = 2;

		if (ver_down_mode == 0) {
			if ((src.vactive - 1) / (dst.vactive - 1) > 2)
				scl_v_factor = ((src.vactive - 1) << 14) /
					       (dst.vactive - 1);
			else
				scl_v_factor = ((src.vactive - 2) << 14) /
					       (dst.vactive - 1);
		} else {
			scl_v_factor = (dst.vactive << 16) /
				       (src.vactive - 1);
		}

		dev_dbg(rk618->dev, "vertical scale down\n");
	} else if (src.vactive == dst.vactive) {
		scl_ver_mode = 0;
		scl_v_factor = 0;

		dev_dbg(rk618->dev, "vertical no scale\n");
	} else {
		scl_ver_mode = 1;
		scl_v_factor = ((src.vactive - 1) << 16) / (dst.vactive - 1);

		dev_dbg(rk618->dev, "vertical scale up\n");
	}

	regmap_write(rk618->regmap, RK618_SCALER_REG0,
		     SCL_VER_MODE(scl_ver_mode) | SCL_HOR_MODE(scl_hor_mode));
	regmap_write(rk618->regmap, RK618_SCALER_REG1,
		     SCL_V_FACTOR(scl_v_factor) | SCL_H_FACTOR(scl_h_factor));
	regmap_write(rk618->regmap, RK618_SCALER_REG2,
		     DSP_FRAME_VST(dsp_frame_vst) |
		     DSP_FRAME_HST(dsp_frame_hst));
	regmap_write(rk618->regmap, RK618_SCALER_REG3,
		     DSP_HS_END(dsp_hs_end) | DSP_HTOTAL(dsp_htotal));
	regmap_write(rk618->regmap, RK618_SCALER_REG4,
		     DSP_HACT_END(dsp_hact_end) | DSP_HACT_ST(dsp_hact_st));
	regmap_write(rk618->regmap, RK618_SCALER_REG5,
		     DSP_VS_END(dsp_vs_end) | DSP_VTOTAL(dsp_vtotal));
	regmap_write(rk618->regmap, RK618_SCALER_REG6,
		     DSP_VACT_END(dsp_vact_end) | DSP_VACT_ST(dsp_vact_st));
	regmap_write(rk618->regmap, RK618_SCALER_REG7,
		     DSP_HBOR_END(dsp_hbor_end) | DSP_HBOR_ST(dsp_hbor_st));
	regmap_write(rk618->regmap, RK618_SCALER_REG8,
		     DSP_VBOR_END(dsp_vbor_end) | DSP_VBOR_ST(dsp_vbor_st));
}
EXPORT_SYMBOL(rk618_scaler_configure);

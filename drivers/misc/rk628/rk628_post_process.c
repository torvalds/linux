// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */
#include "rk628.h"
#include "rk628_config.h"
#include "rk628_cru.h"

static void calc_dsp_frm_hst_vst(const struct rk628_display_mode *src,
				 const struct rk628_display_mode *dst,
				 u32 *dsp_frame_hst,
				 u32 *dsp_frame_vst)
{
	u32 bp_in, bp_out;
	u32 v_scale_ratio;
	u64 t_frm_st;
	u64 t_bp_in, t_bp_out, t_delta, tin;
	u32 src_pixclock, dst_pixclock;
	u32 dst_htotal, dst_hsync_len, dst_hback_porch;
	u32 dst_vsync_len, dst_vback_porch, dst_vactive;
	u32 src_htotal, src_hsync_len, src_hback_porch;
	u32 src_vtotal, src_vsync_len, src_vback_porch, src_vactive;
	u32 rem;
	u32 x;

	src_pixclock = div_u64(1000000000llu, src->clock);
	dst_pixclock = div_u64(1000000000llu, dst->clock);

	src_hsync_len = src->hsync_end - src->hsync_start;
	src_hback_porch = src->htotal - src->hsync_end;
	src_htotal = src->htotal;
	src_vsync_len = src->vsync_end - src->vsync_start;
	src_vback_porch = src->vtotal - src->vsync_end;
	src_vactive = src->vdisplay;
	src_vtotal = src->vtotal;

	dst_hsync_len = dst->hsync_end - dst->hsync_start;
	dst_hback_porch = dst->htotal - dst->hsync_end;
	dst_htotal = dst->htotal;
	dst_vsync_len = dst->vsync_end - dst->vsync_start;
	dst_vback_porch = dst->vtotal - dst->vsync_end;
	dst_vactive = dst->vdisplay;

	bp_in = (src_vback_porch + src_vsync_len) * src_htotal +
		src_hsync_len + src_hback_porch;
	bp_out = (dst_vback_porch + dst_vsync_len) * dst_htotal +
		 dst_hsync_len + dst_hback_porch;

	t_bp_in = bp_in * src_pixclock;
	t_bp_out = bp_out * dst_pixclock;
	tin = src_vtotal * src_htotal * src_pixclock;

	v_scale_ratio = src_vactive / dst_vactive;
	x = 5;
__retry:
	if (v_scale_ratio <= 2)
		t_delta = x * src_htotal * src_pixclock;
	else
		t_delta = 12 * src_htotal * src_pixclock;

	if (t_bp_in + t_delta > t_bp_out)
		t_frm_st = (t_bp_in + t_delta - t_bp_out);
	else
		t_frm_st = tin - (t_bp_out - (t_bp_in + t_delta));

	do_div(t_frm_st, src_pixclock);
	rem = do_div(t_frm_st, src_htotal);
	if ((t_frm_st < 2 || t_frm_st > 14) && x < 12) {
		x++;
		goto __retry;
	}
	if (t_frm_st < 2 || t_frm_st > 14)
		t_frm_st = 4;

	*dsp_frame_hst = rem;
	*dsp_frame_vst = t_frm_st;
}

static void rk628_post_process_scaler_init(struct rk628 *rk628,
					   struct rk628_display_mode *src,
					   const struct rk628_display_mode *dst)
{
	u32 dsp_frame_hst, dsp_frame_vst;
	u32 scl_hor_mode, scl_ver_mode;
	u32 scl_v_factor, scl_h_factor;
	u32 dsp_htotal, dsp_hs_end, dsp_hact_st, dsp_hact_end;
	u32 dsp_vtotal, dsp_vs_end, dsp_vact_st, dsp_vact_end;
	u32 dsp_hbor_end, dsp_hbor_st, dsp_vbor_end, dsp_vbor_st;
	u16 bor_right = 0, bor_left = 0, bor_up = 0, bor_down = 0;
	u8 hor_down_mode = 0, ver_down_mode = 0;
	u32 dst_hsync_len, dst_hback_porch, dst_hfront_porch, dst_hactive;
	u32 dst_vsync_len, dst_vback_porch, dst_vfront_porch, dst_vactive;
	u32 src_hactive;
	u32 src_vactive;

	src_hactive = src->hdisplay;
	src_vactive = src->vdisplay;

	dst_hactive = dst->hdisplay;
	dst_hsync_len = dst->hsync_end - dst->hsync_start;
	dst_hback_porch = dst->htotal - dst->hsync_end;
	dst_hfront_porch = dst->hsync_start - dst->hdisplay;
	dst_vsync_len = dst->vsync_end - dst->vsync_start;
	dst_vback_porch = dst->vtotal - dst->vsync_end;
	dst_vfront_porch = dst->vsync_start - dst->vdisplay;
	dst_vactive = dst->vdisplay;

	dsp_htotal = dst_hsync_len + dst_hback_porch +
		     dst_hactive + dst_hfront_porch;
	dsp_vtotal = dst_vsync_len + dst_vback_porch +
		     dst_vactive + dst_vfront_porch;
	dsp_hs_end = dst_hsync_len;
	dsp_vs_end = dst_vsync_len;
	dsp_hbor_end = dst_hsync_len + dst_hback_porch + dst_hactive;
	dsp_hbor_st = dst_hsync_len + dst_hback_porch;
	dsp_vbor_end = dst_vsync_len + dst_vback_porch + dst_vactive;
	dsp_vbor_st = dst_vsync_len + dst_vback_porch;
	dsp_hact_st = dsp_hbor_st + bor_left;
	dsp_hact_end = dsp_hbor_end - bor_right;
	dsp_vact_st = dsp_vbor_st + bor_up;
	dsp_vact_end = dsp_vbor_end - bor_down;

	calc_dsp_frm_hst_vst(src, dst, &dsp_frame_hst, &dsp_frame_vst);
	dev_info(rk628->dev, "dsp_frame_vst:%d  dsp_frame_hst:%d\n",
		 dsp_frame_vst, dsp_frame_hst);

	if (src_hactive > dst_hactive) {
		scl_hor_mode = 2;

		if (hor_down_mode == 0) {
			if ((src_hactive - 1) / (dst_hactive - 1) > 2)
				scl_h_factor = ((src_hactive - 1) << 14) /
						(dst_hactive - 1);
			else
				scl_h_factor = ((src_hactive - 2) << 14) /
						(dst_hactive - 1);
		} else {
			scl_h_factor = (dst_hactive << 16) / (src_hactive - 1);
		}

	} else if (src_hactive == dst_hactive) {
		scl_hor_mode = 0;
		scl_h_factor = 0;
	} else {
		scl_hor_mode = 1;
		scl_h_factor = ((src_hactive - 1) << 16) / (dst_hactive - 1);
	}

	if (src_vactive > dst_vactive) {
		scl_ver_mode = 2;

		if (ver_down_mode == 0) {
			if ((src_vactive - 1) / (dst_vactive - 1) > 2)
				scl_v_factor = ((src_vactive - 1) << 14) /
						(dst_vactive - 1);
			else
				scl_v_factor = ((src_vactive - 2) << 14) /
						(dst_vactive - 1);
		} else {
			scl_v_factor = (dst_vactive << 16) / (src_vactive - 1);
		}

	} else if (src_vactive == dst_vactive) {
		scl_ver_mode = 0;
		scl_v_factor = 0;
	} else {
		scl_ver_mode = 1;
		scl_v_factor = ((src_vactive - 1) << 16) / (dst_vactive - 1);
	}

	rk628_i2c_update_bits(rk628, GRF_RGB_DEC_CON0, SW_HRES_MASK,
			      SW_HRES(src_hactive));
	rk628_i2c_write(rk628, GRF_SCALER_CON0, SCL_VER_DOWN_MODE(ver_down_mode) |
			SCL_HOR_DOWN_MODE(hor_down_mode) |
			SCL_VER_MODE(scl_ver_mode) |
			SCL_HOR_MODE(scl_hor_mode));
	rk628_i2c_write(rk628, GRF_SCALER_CON1, SCL_V_FACTOR(scl_v_factor) |
			SCL_H_FACTOR(scl_h_factor));
	rk628_i2c_write(rk628, GRF_SCALER_CON2, DSP_FRAME_VST(dsp_frame_vst) |
			DSP_FRAME_HST(dsp_frame_hst));
	rk628_i2c_write(rk628, GRF_SCALER_CON3, DSP_HS_END(dsp_hs_end) |
			DSP_HTOTAL(dsp_htotal));
	rk628_i2c_write(rk628, GRF_SCALER_CON4, DSP_HACT_END(dsp_hact_end) |
			DSP_HACT_ST(dsp_hact_st));
	rk628_i2c_write(rk628, GRF_SCALER_CON5, DSP_VS_END(dsp_vs_end) |
			DSP_VTOTAL(dsp_vtotal));
	rk628_i2c_write(rk628, GRF_SCALER_CON6, DSP_VACT_END(dsp_vact_end) |
			DSP_VACT_ST(dsp_vact_st));
	rk628_i2c_write(rk628, GRF_SCALER_CON7, DSP_HBOR_END(dsp_hbor_end) |
			DSP_HBOR_ST(dsp_hbor_st));
	rk628_i2c_write(rk628, GRF_SCALER_CON8, DSP_VBOR_END(dsp_vbor_end) |
			DSP_VBOR_ST(dsp_vbor_st));
}

void rk628_post_process_init(struct rk628 *rk628)
{
	struct rk628_display_mode *src = &rk628->src_mode;
	const struct rk628_display_mode *dst = &rk628->dst_mode;
	u64 dst_rate, src_rate;

	src_rate = src->clock * 1000;
	dst_rate = src_rate * dst->vdisplay * dst->htotal;
	do_div(dst_rate, (src->vdisplay * src->htotal));
	do_div(dst_rate, 1000);
	dev_info(rk628->dev, "src %dx%d clock:%d\n",
		 src->hdisplay, src->vdisplay, src->clock);

	dev_info(rk628->dev, "dst %dx%d clock:%llu\n",
		 dst->hdisplay, dst->vdisplay, dst_rate);

	rk628_cru_clk_set_rate(rk628, CGU_CLK_RX_READ, src->clock * 1000);
	rk628_cru_clk_set_rate(rk628, CGU_SCLK_VOP, dst_rate * 1000);

	if (rk628->output_mode == OUTPUT_MODE_HDMI) {
		rk628_i2c_update_bits(rk628, GRF_SYSTEM_CON0, SW_VSYNC_POL_MASK,
				      SW_VSYNC_POL(rk628->sync_pol));
		rk628_i2c_update_bits(rk628, GRF_SYSTEM_CON0, SW_HSYNC_POL_MASK,
				      SW_HSYNC_POL(rk628->sync_pol));
	} else {
		if (src->flags & DRM_MODE_FLAG_PVSYNC)
			rk628_i2c_update_bits(rk628, GRF_SYSTEM_CON0,
					      SW_VSYNC_POL_MASK, SW_VSYNC_POL(1));
		if (src->flags & DRM_MODE_FLAG_PHSYNC)
			rk628_i2c_update_bits(rk628, GRF_SYSTEM_CON0,
					      SW_HSYNC_POL_MASK,
					      SW_HSYNC_POL(1));
	}

	rk628_post_process_scaler_init(rk628, src, dst);
}

static void rk628_post_process_csc(struct rk628 *rk628)
{
	enum bus_format in_fmt, out_fmt;

	in_fmt = rk628_get_input_bus_format(rk628);
	out_fmt = rk628_get_output_bus_format(rk628);

	if (in_fmt == out_fmt) {
		if (out_fmt == BUS_FMT_YUV422) {
			rk628_i2c_write(rk628, GRF_CSC_CTRL_CON,
					SW_YUV2VYU_SWP(1) |
					SW_R2Y_EN(0));
			return;
		}
		rk628_i2c_write(rk628, GRF_CSC_CTRL_CON, SW_R2Y_EN(0));
		rk628_i2c_write(rk628, GRF_CSC_CTRL_CON, SW_Y2R_EN(0));
		return;
	}

	if (in_fmt == BUS_FMT_RGB)
		rk628_i2c_write(rk628, GRF_CSC_CTRL_CON, SW_R2Y_EN(1));
	else if (out_fmt == BUS_FMT_RGB)
		rk628_i2c_write(rk628, GRF_CSC_CTRL_CON, SW_Y2R_EN(1));
}

void rk628_post_process_enable(struct rk628 *rk628)
{
	rk628_post_process_csc(rk628);
	rk628_i2c_write(rk628, GRF_SCALER_CON0, SCL_EN(1));
}

void rk628_post_process_disable(struct rk628 *rk628)
{
	rk628_i2c_write(rk628, GRF_SCALER_CON0, SCL_EN(0));
}

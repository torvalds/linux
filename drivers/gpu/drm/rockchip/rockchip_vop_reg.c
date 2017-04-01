/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author:Mark Yao <mark.yao@rock-chips.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>

#include <linux/kernel.h>
#include <linux/component.h>

#include "rockchip_drm_vop.h"
#include "rockchip_vop_reg.h"

#define VOP_REG_VER_MASK(off, _mask, s, _write_mask, _major, \
		         _begin_minor, _end_minor) \
		{.offset = off, \
		 .mask = _mask, \
		 .shift = s, \
		 .write_mask = _write_mask, \
		 .major = _major, \
		 .begin_minor = _begin_minor, \
		 .end_minor = _end_minor,}

#define VOP_REG(off, _mask, s) \
		VOP_REG_VER_MASK(off, _mask, s, false, 0, 0, -1)

#define VOP_REG_MASK(off, _mask, s) \
		VOP_REG_VER_MASK(off, _mask, s, true, 0, 0, -1)

#define VOP_REG_VER(off, _mask, s, _major, _begin_minor, _end_minor) \
		VOP_REG_VER_MASK(off, _mask, s, false, \
				 _major, _begin_minor, _end_minor)


static const uint32_t formats_win_full[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV16,
	DRM_FORMAT_NV24,
	DRM_FORMAT_NV12_10,
	DRM_FORMAT_NV16_10,
	DRM_FORMAT_NV24_10,
};

static const uint32_t formats_win_lite[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
};

static const struct vop_scl_extension rk3288_win_full_scl_ext = {
	.cbcr_vsd_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 31),
	.cbcr_vsu_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 30),
	.cbcr_hsd_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x3, 28),
	.cbcr_ver_scl_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x3, 26),
	.cbcr_hor_scl_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x3, 24),
	.yrgb_vsd_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 23),
	.yrgb_vsu_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 22),
	.yrgb_hsd_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x3, 20),
	.yrgb_ver_scl_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x3, 18),
	.yrgb_hor_scl_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x3, 16),
	.line_load_mode = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 15),
	.cbcr_axi_gather_num = VOP_REG(RK3288_WIN0_CTRL1, 0x7, 12),
	.yrgb_axi_gather_num = VOP_REG(RK3288_WIN0_CTRL1, 0xf, 8),
	.vsd_cbcr_gt2 = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 7),
	.vsd_cbcr_gt4 = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 6),
	.vsd_yrgb_gt2 = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 5),
	.vsd_yrgb_gt4 = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 4),
	.bic_coe_sel = VOP_REG(RK3288_WIN0_CTRL1, 0x3, 2),
	.cbcr_axi_gather_en = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 1),
	.yrgb_axi_gather_en = VOP_REG(RK3288_WIN0_CTRL1, 0x1, 0),
	.lb_mode = VOP_REG(RK3288_WIN0_CTRL0, 0x7, 5),
};

static const struct vop_scl_regs rk3288_win_full_scl = {
	.ext = &rk3288_win_full_scl_ext,
	.scale_yrgb_x = VOP_REG(RK3288_WIN0_SCL_FACTOR_YRGB, 0xffff, 0x0),
	.scale_yrgb_y = VOP_REG(RK3288_WIN0_SCL_FACTOR_YRGB, 0xffff, 16),
	.scale_cbcr_x = VOP_REG(RK3288_WIN0_SCL_FACTOR_CBR, 0xffff, 0x0),
	.scale_cbcr_y = VOP_REG(RK3288_WIN0_SCL_FACTOR_CBR, 0xffff, 16),
};

static const struct vop_win_phy rk3288_win01_data = {
	.scl = &rk3288_win_full_scl,
	.data_formats = formats_win_full,
	.nformats = ARRAY_SIZE(formats_win_full),
	.enable = VOP_REG(RK3288_WIN0_CTRL0, 0x1, 0),
	.format = VOP_REG(RK3288_WIN0_CTRL0, 0x7, 1),
	.fmt_10 = VOP_REG(RK3288_WIN0_CTRL0, 0x7, 4),
	.rb_swap = VOP_REG(RK3288_WIN0_CTRL0, 0x1, 12),
	.xmirror = VOP_REG_VER(RK3368_WIN0_CTRL0, 0x1, 21, 3, 2, -1),
	.ymirror = VOP_REG_VER(RK3368_WIN0_CTRL0, 0x1, 22, 3, 2, -1),
	.act_info = VOP_REG(RK3288_WIN0_ACT_INFO, 0x1fff1fff, 0),
	.dsp_info = VOP_REG(RK3288_WIN0_DSP_INFO, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3288_WIN0_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3288_WIN0_YRGB_MST, 0xffffffff, 0),
	.uv_mst = VOP_REG(RK3288_WIN0_CBR_MST, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3288_WIN0_VIR, 0x3fff, 0),
	.uv_vir = VOP_REG(RK3288_WIN0_VIR, 0x3fff, 16),
	.src_alpha_ctl = VOP_REG(RK3288_WIN0_SRC_ALPHA_CTRL, 0xffffffff, 0),
	.dst_alpha_ctl = VOP_REG(RK3288_WIN0_DST_ALPHA_CTRL, 0xffffffff, 0),
	.channel = VOP_REG_VER(RK3288_WIN0_CTRL2, 0xff, 0, 3, 8, 8),
};

static const struct vop_win_phy rk3288_win23_data = {
	.data_formats = formats_win_lite,
	.nformats = ARRAY_SIZE(formats_win_lite),
	.gate = VOP_REG(RK3288_WIN2_CTRL0, 0x1, 0),
	.enable = VOP_REG(RK3288_WIN2_CTRL0, 0x1, 4),
	.format = VOP_REG(RK3288_WIN2_CTRL0, 0x7, 1),
	.rb_swap = VOP_REG(RK3288_WIN2_CTRL0, 0x1, 12),
	.dsp_info = VOP_REG(RK3288_WIN2_DSP_INFO0, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3288_WIN2_DSP_ST0, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3288_WIN2_MST0, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3288_WIN2_VIR0_1, 0x1fff, 0),
	.src_alpha_ctl = VOP_REG(RK3288_WIN2_SRC_ALPHA_CTRL, 0xff, 0),
	.dst_alpha_ctl = VOP_REG(RK3288_WIN2_DST_ALPHA_CTRL, 0xff, 0),
};

static const struct vop_win_phy rk3288_area1_data = {
	.enable = VOP_REG(RK3288_WIN2_CTRL0, 0x1, 5),
	.dsp_info = VOP_REG(RK3288_WIN2_DSP_INFO1, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3288_WIN2_DSP_ST1, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3288_WIN2_MST1, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3288_WIN2_VIR0_1, 0x1fff, 16),
};

static const struct vop_win_phy rk3288_area2_data = {
	.enable = VOP_REG(RK3288_WIN2_CTRL0, 0x1, 6),
	.dsp_info = VOP_REG(RK3288_WIN2_DSP_INFO2, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3288_WIN2_DSP_ST2, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3288_WIN2_MST2, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3288_WIN2_VIR2_3, 0x1fff, 0),
};

static const struct vop_win_phy rk3288_area3_data = {
	.enable = VOP_REG(RK3288_WIN2_CTRL0, 0x1, 7),
	.dsp_info = VOP_REG(RK3288_WIN2_DSP_INFO3, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3288_WIN2_DSP_ST3, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3288_WIN2_MST3, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3288_WIN2_VIR2_3, 0x1fff, 16),
};

static const struct vop_win_phy *rk3288_area_data[] = {
	&rk3288_area1_data,
	&rk3288_area2_data,
	&rk3288_area3_data
};

static const struct vop_ctrl rk3288_ctrl_data = {
	.standby = VOP_REG(RK3288_SYS_CTRL, 0x1, 22),
	.htotal_pw = VOP_REG(RK3288_DSP_HTOTAL_HS_END, 0x1fff1fff, 0),
	.hact_st_end = VOP_REG(RK3288_DSP_HACT_ST_END, 0x1fff1fff, 0),
	.vtotal_pw = VOP_REG(RK3288_DSP_VTOTAL_VS_END, 0x1fff1fff, 0),
	.vact_st_end = VOP_REG(RK3288_DSP_VACT_ST_END, 0x1fff1fff, 0),
	.vact_st_end_f1 = VOP_REG(RK3288_DSP_VACT_ST_END_F1, 0x1fff1fff, 0),
	.vs_st_end_f1 = VOP_REG(RK3288_DSP_VS_ST_END_F1, 0x1fff1fff, 0),
	.hpost_st_end = VOP_REG(RK3288_POST_DSP_HACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end = VOP_REG(RK3288_POST_DSP_VACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end_f1 = VOP_REG(RK3288_POST_DSP_VACT_INFO_F1, 0x1fff1fff, 0),
	.post_scl_factor = VOP_REG(RK3288_POST_SCL_FACTOR_YRGB, 0xffffffff, 0),
	.post_scl_ctrl = VOP_REG(RK3288_POST_SCL_CTRL, 0x3, 0),

	.dsp_interlace = VOP_REG(RK3288_DSP_CTRL0, 0x1, 10),
	.auto_gate_en = VOP_REG(RK3288_SYS_CTRL, 0x1, 23),
	.dsp_layer_sel = VOP_REG(RK3288_DSP_CTRL1, 0xff, 8),
	.post_lb_mode = VOP_REG_VER(RK3288_SYS_CTRL, 0x1, 18, 3, 2, -1),
	.global_regdone_en = VOP_REG_VER(RK3288_SYS_CTRL, 0x1, 11, 3, 2, -1),
	.overlay_mode = VOP_REG_VER(RK3288_SYS_CTRL, 0x1, 16, 3, 2, -1),
	.core_dclk_div = VOP_REG_VER(RK3399_DSP_CTRL0, 0x1, 4, 3, 4, -1),
	.p2i_en = VOP_REG_VER(RK3399_DSP_CTRL0, 0x1, 5, 3, 4, -1),
	.dclk_ddr = VOP_REG_VER(RK3399_DSP_CTRL0, 0x1, 8, 3, 4, -1),
	.dp_en = VOP_REG(RK3399_SYS_CTRL, 0x1, 11),
	.rgb_en = VOP_REG(RK3288_SYS_CTRL, 0x1, 12),
	.hdmi_en = VOP_REG(RK3288_SYS_CTRL, 0x1, 13),
	.edp_en = VOP_REG(RK3288_SYS_CTRL, 0x1, 14),
	.mipi_en = VOP_REG(RK3288_SYS_CTRL, 0x1, 15),
	.pin_pol = VOP_REG_VER(RK3288_DSP_CTRL0, 0xf, 4, 3, 0, 1),
	.dp_pin_pol = VOP_REG_VER(RK3368_DSP_CTRL1, 0xf, 16, 3, 2, -1),
	.rgb_pin_pol = VOP_REG_VER(RK3368_DSP_CTRL1, 0xf, 16, 3, 2, -1),
	.hdmi_pin_pol = VOP_REG_VER(RK3368_DSP_CTRL1, 0xf, 20, 3, 2, -1),
	.edp_pin_pol = VOP_REG_VER(RK3368_DSP_CTRL1, 0xf, 24, 3, 2, -1),
	.mipi_pin_pol = VOP_REG_VER(RK3368_DSP_CTRL1, 0xf, 28, 3, 2, -1),

	.dither_down = VOP_REG(RK3288_DSP_CTRL1, 0xf, 1),
	.dither_up = VOP_REG(RK3288_DSP_CTRL1, 0x1, 6),

	.dsp_out_yuv = VOP_REG_VER(RK3399_POST_SCL_CTRL, 0x1, 2, 3, 5, -1),
	.dsp_data_swap = VOP_REG(RK3288_DSP_CTRL0, 0x1f, 12),
	.dsp_ccir656_avg = VOP_REG(RK3288_DSP_CTRL0, 0x1, 20),
	.dsp_blank = VOP_REG(RK3288_DSP_CTRL0, 0x3, 18),
	.dsp_lut_en = VOP_REG(RK3288_DSP_CTRL1, 0x1, 0),
	.out_mode = VOP_REG(RK3288_DSP_CTRL0, 0xf, 0),

	.afbdc_rstn = VOP_REG(RK3399_AFBCD0_CTRL, 0x1, 3),
	.afbdc_en = VOP_REG(RK3399_AFBCD0_CTRL, 0x1, 0),
	.afbdc_sel = VOP_REG(RK3399_AFBCD0_CTRL, 0x3, 1),
	.afbdc_format = VOP_REG(RK3399_AFBCD0_CTRL, 0x1f, 16),
	.afbdc_hreg_block_split = VOP_REG(RK3399_AFBCD0_CTRL, 0x1, 21),
	.afbdc_hdr_ptr = VOP_REG(RK3399_AFBCD0_HDR_PTR, 0xffffffff, 0),
	.afbdc_pic_size = VOP_REG(RK3399_AFBCD0_PIC_SIZE, 0xffffffff, 0),

	.xmirror = VOP_REG(RK3288_DSP_CTRL0, 0x1, 22),
	.ymirror = VOP_REG(RK3288_DSP_CTRL0, 0x1, 23),

	.dsp_background = VOP_REG(RK3288_DSP_BG, 0xffffffff, 0),

	.cfg_done = VOP_REG(RK3288_REG_CFG_DONE, 0x1, 0),
};

/*
 * Note: rk3288 has a dedicated 'cursor' window, however, that window requires
 * special support to get alpha blending working.  For now, just use overlay
 * window 3 for the drm cursor.
 *
 */
static const struct vop_win_data rk3288_vop_win_data[] = {
	{ .base = 0x00, .phy = &rk3288_win01_data,
	  .type = DRM_PLANE_TYPE_PRIMARY },
	{ .base = 0x40, .phy = &rk3288_win01_data,
	  .type = DRM_PLANE_TYPE_OVERLAY },
	{ .base = 0x00, .phy = &rk3288_win23_data,
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .area = rk3288_area_data,
	  .area_size = ARRAY_SIZE(rk3288_area_data), },
	{ .base = 0x50, .phy = &rk3288_win23_data,
	  .type = DRM_PLANE_TYPE_CURSOR,
	  .area = rk3288_area_data,
	  .area_size = ARRAY_SIZE(rk3288_area_data), },
};

static const int rk3288_vop_intrs[] = {
	DSP_HOLD_VALID_INTR,
	FS_INTR,
	LINE_FLAG_INTR,
	BUS_ERROR_INTR,
};

static const struct vop_intr rk3288_vop_intr = {
	.intrs = rk3288_vop_intrs,
	.nintrs = ARRAY_SIZE(rk3288_vop_intrs),
	.line_flag_num[0] = VOP_REG(RK3288_INTR_CTRL0, 0x1fff, 12),
	.status = VOP_REG(RK3288_INTR_CTRL0, 0xf, 0),
	.enable = VOP_REG(RK3288_INTR_CTRL0, 0xf, 4),
	.clear = VOP_REG(RK3288_INTR_CTRL0, 0xf, 8),
};

static const struct vop_data rk3288_vop = {
	.version = VOP_VERSION(3, 1),
	.feature = VOP_FEATURE_OUTPUT_10BIT,
	.max_input = {4096, 8192},
	/*
	 * TODO: rk3288 have two vop, big one support 3840x2160,
	 * little one only support 2560x1600.
	 * Now force use 3840x2160.
	 */
	.max_output = {3840, 2160},
	.intr = &rk3288_vop_intr,
	.ctrl = &rk3288_ctrl_data,
	.win = rk3288_vop_win_data,
	.win_size = ARRAY_SIZE(rk3288_vop_win_data),
};

static const int rk3368_vop_intrs[] = {
	FS_INTR,
	FS_NEW_INTR,
	ADDR_SAME_INTR,
	LINE_FLAG_INTR,
	LINE_FLAG1_INTR,
	BUS_ERROR_INTR,
	WIN0_EMPTY_INTR,
	WIN1_EMPTY_INTR,
	WIN2_EMPTY_INTR,
	WIN3_EMPTY_INTR,
	HWC_EMPTY_INTR,
	POST_BUF_EMPTY_INTR,
	PWM_GEN_INTR,
	DSP_HOLD_VALID_INTR,
};

static const struct vop_intr rk3368_vop_intr = {
	.intrs = rk3368_vop_intrs,
	.nintrs = ARRAY_SIZE(rk3368_vop_intrs),
	.line_flag_num[0] = VOP_REG(RK3368_LINE_FLAG, 0xffff, 0),
	.line_flag_num[1] = VOP_REG(RK3368_LINE_FLAG, 0xffff, 16),
	.status = VOP_REG_MASK(RK3368_INTR_STATUS, 0x3fff, 0),
	.enable = VOP_REG_MASK(RK3368_INTR_EN, 0x3fff, 0),
	.clear = VOP_REG_MASK(RK3368_INTR_CLEAR, 0x3fff, 0),
};

static const struct vop_win_phy rk3368_win23_data = {
	.data_formats = formats_win_lite,
	.nformats = ARRAY_SIZE(formats_win_lite),
	.gate = VOP_REG(RK3368_WIN2_CTRL0, 0x1, 0),
	.enable = VOP_REG(RK3368_WIN2_CTRL0, 0x1, 4),
	.format = VOP_REG(RK3368_WIN2_CTRL0, 0x3, 5),
	.ymirror = VOP_REG(RK3368_WIN2_CTRL1, 0x1, 15),
	.rb_swap = VOP_REG(RK3368_WIN2_CTRL0, 0x1, 20),
	.dsp_info = VOP_REG(RK3368_WIN2_DSP_INFO0, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3368_WIN2_DSP_ST0, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3368_WIN2_MST0, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3368_WIN2_VIR0_1, 0x1fff, 0),
	.src_alpha_ctl = VOP_REG(RK3368_WIN2_SRC_ALPHA_CTRL, 0xff, 0),
	.dst_alpha_ctl = VOP_REG(RK3368_WIN2_DST_ALPHA_CTRL, 0xff, 0),
};

static const struct vop_win_phy rk3368_area1_data = {
	.enable = VOP_REG(RK3368_WIN2_CTRL0, 0x1, 8),
	.format = VOP_REG(RK3368_WIN2_CTRL0, 0x3, 9),
	.rb_swap = VOP_REG(RK3368_WIN2_CTRL0, 0x1, 23),
	.dsp_info = VOP_REG(RK3368_WIN2_DSP_INFO1, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3368_WIN2_DSP_ST1, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3368_WIN2_MST1, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3368_WIN2_VIR0_1, 0x1fff, 16),
};

static const struct vop_win_phy rk3368_area2_data = {
	.enable = VOP_REG(RK3368_WIN2_CTRL0, 0x1, 12),
	.format = VOP_REG(RK3368_WIN2_CTRL0, 0x3, 13),
	.rb_swap = VOP_REG(RK3368_WIN2_CTRL0, 0x1, 26),
	.dsp_info = VOP_REG(RK3368_WIN2_DSP_INFO2, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3368_WIN2_DSP_ST2, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3368_WIN2_MST2, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3368_WIN2_VIR2_3, 0x1fff, 0),
};

static const struct vop_win_phy rk3368_area3_data = {
	.enable = VOP_REG(RK3368_WIN2_CTRL0, 0x1, 16),
	.format = VOP_REG(RK3368_WIN2_CTRL0, 0x3, 17),
	.rb_swap = VOP_REG(RK3368_WIN2_CTRL0, 0x1, 29),
	.dsp_info = VOP_REG(RK3368_WIN2_DSP_INFO3, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3368_WIN2_DSP_ST3, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3368_WIN2_MST3, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3368_WIN2_VIR2_3, 0x1fff, 16),
};

static const struct vop_win_phy *rk3368_area_data[] = {
	&rk3368_area1_data,
	&rk3368_area2_data,
	&rk3368_area3_data
};

static const struct vop_win_data rk3368_vop_win_data[] = {
	{ .base = 0x00, .phy = &rk3288_win01_data,
	  .type = DRM_PLANE_TYPE_PRIMARY },
	{ .base = 0x40, .phy = &rk3288_win01_data,
	  .type = DRM_PLANE_TYPE_OVERLAY },
	{ .base = 0x00, .phy = &rk3368_win23_data,
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .area = rk3368_area_data,
	  .area_size = ARRAY_SIZE(rk3368_area_data), },
	{ .base = 0x50, .phy = &rk3368_win23_data,
	  .type = DRM_PLANE_TYPE_CURSOR,
	  .area = rk3368_area_data,
	  .area_size = ARRAY_SIZE(rk3368_area_data), },
};

static const struct vop_data rk3368_vop = {
	.version = VOP_VERSION(3, 2),
	.max_input = {4096, 8192},
	.max_output = {4096, 2160},
	.intr = &rk3368_vop_intr,
	.ctrl = &rk3288_ctrl_data,
	.win = rk3368_vop_win_data,
	.win_size = ARRAY_SIZE(rk3368_vop_win_data),
};

static const struct vop_intr rk3366_vop_intr = {
	.intrs = rk3368_vop_intrs,
	.nintrs = ARRAY_SIZE(rk3368_vop_intrs),
	.line_flag_num[0] = VOP_REG(RK3366_LINE_FLAG, 0xffff, 0),
	.line_flag_num[1] = VOP_REG(RK3366_LINE_FLAG, 0xffff, 16),
	.status = VOP_REG_MASK(RK3366_INTR_STATUS0, 0xffff, 0),
	.enable = VOP_REG_MASK(RK3366_INTR_EN0, 0xffff, 0),
	.clear = VOP_REG_MASK(RK3366_INTR_CLEAR0, 0xffff, 0),
};

static const struct vop_data rk3366_vop = {
	.version = VOP_VERSION(3, 4),
	.max_input = {4096, 8192},
	.max_output = {4096, 2160},
	.intr = &rk3366_vop_intr,
	.ctrl = &rk3288_ctrl_data,
	.win = rk3368_vop_win_data,
	.win_size = ARRAY_SIZE(rk3368_vop_win_data),
};

static const uint32_t vop_csc_y2r_bt601[] = {
	0x00000400, 0x0400059c, 0xfd25fea0, 0x07170400,
	0x00000000, 0xfffecab4, 0x00087932, 0xfff1d4f2,
};

static const uint32_t vop_csc_y2r_bt601_12_235[] = {
	0x000004a8, 0x04a80662, 0xfcbffe6f, 0x081204a8,
	0x00000000, 0xfff2134e, 0x00087b58, 0xffeeb4b0,
};

static const uint32_t vop_csc_r2y_bt601[] = {
	0x02590132, 0xff530075, 0x0200fead, 0xfe530200,
	0x0000ffad, 0x00000200, 0x00080200, 0x00080200,
};

static const uint32_t vop_csc_r2y_bt601_12_235[] = {
	0x02040107, 0xff680064, 0x01c2fed6, 0xffb7fe87,
	0x0000ffb7, 0x00010200, 0x00080200, 0x00080200,
};

static const uint32_t vop_csc_y2r_bt709[] = {
	0x000004a8, 0x04a8072c, 0xfddeff26, 0x087304a8,
	0x00000000, 0xfff08077, 0x0004cfed, 0xffedf1b8,
};

static const uint32_t vop_csc_r2y_bt709[] = {
	0x027500bb, 0xff99003f, 0x01c2fea5, 0xfe6801c2,
	0x0000ffd7, 0x00010200, 0x00080200, 0x00080200,
};

static const uint32_t vop_csc_y2r_bt2020[] = {
	0x000004a8, 0x04a806b6, 0xfd66ff40, 0x089004a8,
	0x00000000, 0xfff16bfc, 0x00058ae9, 0xffedb828,
};

static const uint32_t vop_csc_r2y_bt2020[] = {
	0x025300e6, 0xff830034, 0x01c1febd, 0xfe6401c1,
	0x0000ffdc, 0x00010200, 0x00080200, 0x00080200,
};

static const uint32_t vop_csc_r2r_bt709_to_bt2020[] = {
	0xfda606a4, 0xff80ffb5, 0xfff80488, 0xff99ffed,
	0x0000047a, 0x00000200, 0x00000200, 0x00000200,
};

static const uint32_t vop_csc_r2r_bt2020_to_bt709[] = {
	0x01510282, 0x0047002c, 0x000c03ae, 0x005a0011,
	0x00000394, 0x00000200, 0x00000200, 0x00000200,
};

static const struct vop_csc_table rk3399_csc_table = {
	.y2r_bt601		= vop_csc_y2r_bt601,
	.y2r_bt601_12_235	= vop_csc_y2r_bt601_12_235,
	.r2y_bt601		= vop_csc_r2y_bt601,
	.r2y_bt601_12_235	= vop_csc_r2y_bt601_12_235,

	.y2r_bt709		= vop_csc_y2r_bt709,
	.r2y_bt709		= vop_csc_r2y_bt709,

	.y2r_bt2020		= vop_csc_y2r_bt2020,
	.r2y_bt2020		= vop_csc_r2y_bt2020,

	.r2r_bt709_to_bt2020	= vop_csc_r2r_bt709_to_bt2020,
	.r2r_bt2020_to_bt709	= vop_csc_r2r_bt2020_to_bt709,
};

static const struct vop_csc rk3399_win0_csc = {
	.r2r_en = VOP_REG(RK3399_YUV2YUV_WIN, 0x1, 0),
	.y2r_en = VOP_REG(RK3399_YUV2YUV_WIN, 0x1, 1),
	.r2y_en = VOP_REG(RK3399_YUV2YUV_WIN, 0x1, 2),
	.y2r_offset = RK3399_WIN0_YUV2YUV_Y2R,
	.r2r_offset = RK3399_WIN0_YUV2YUV_3X3,
	.r2y_offset = RK3399_WIN0_YUV2YUV_R2Y,
};

static const struct vop_csc rk3399_win1_csc = {
	.r2r_en = VOP_REG(RK3399_YUV2YUV_WIN, 0x1, 8),
	.y2r_en = VOP_REG(RK3399_YUV2YUV_WIN, 0x1, 9),
	.r2y_en = VOP_REG(RK3399_YUV2YUV_WIN, 0x1, 10),
	.y2r_offset = RK3399_WIN1_YUV2YUV_Y2R,
	.r2r_offset = RK3399_WIN1_YUV2YUV_3X3,
	.r2y_offset = RK3399_WIN1_YUV2YUV_R2Y,
};

static const struct vop_win_data rk3399_vop_win_data[] = {
	{ .base = 0x00, .phy = &rk3288_win01_data, .csc = &rk3399_win0_csc,
	  .type = DRM_PLANE_TYPE_PRIMARY },
	{ .base = 0x40, .phy = &rk3288_win01_data, .csc = &rk3399_win1_csc,
	  .type = DRM_PLANE_TYPE_OVERLAY },
	{ .base = 0x00, .phy = &rk3368_win23_data,
	  .type = DRM_PLANE_TYPE_OVERLAY,
	  .area = rk3368_area_data,
	  .area_size = ARRAY_SIZE(rk3368_area_data), },
	{ .base = 0x50, .phy = &rk3368_win23_data,
	  .type = DRM_PLANE_TYPE_CURSOR,
	  .area = rk3368_area_data,
	  .area_size = ARRAY_SIZE(rk3368_area_data), },
};

static const struct vop_data rk3399_vop_big = {
	.version = VOP_VERSION(3, 5),
	.csc_table = &rk3399_csc_table,
	.feature = VOP_FEATURE_OUTPUT_10BIT | VOP_FEATURE_AFBDC,
	.max_input = {4096, 8192},
	.max_output = {4096, 2160},
	.intr = &rk3366_vop_intr,
	.ctrl = &rk3288_ctrl_data,
	.win = rk3399_vop_win_data,
	.win_size = ARRAY_SIZE(rk3399_vop_win_data),
};

static const struct vop_win_data rk3399_vop_lit_win_data[] = {
	{ .base = 0x00, .phy = &rk3288_win01_data, .csc = &rk3399_win0_csc,
	  .type = DRM_PLANE_TYPE_PRIMARY },
	{ .phy = NULL },
	{ .base = 0x00, .phy = &rk3368_win23_data,
	  .type = DRM_PLANE_TYPE_CURSOR,
	  .area = rk3368_area_data,
	  .area_size = ARRAY_SIZE(rk3368_area_data), },
	{ .phy = NULL },
};


static const struct vop_data rk3399_vop_lit = {
	.version = VOP_VERSION(3, 6),
	.csc_table = &rk3399_csc_table,
	.max_input = {4096, 8192},
	.max_output = {2560, 1600},
	.intr = &rk3366_vop_intr,
	.ctrl = &rk3288_ctrl_data,
	.win = rk3399_vop_lit_win_data,
	.win_size = ARRAY_SIZE(rk3399_vop_lit_win_data),
};

static const struct vop_data rk322x_vop = {
	.version = VOP_VERSION(3, 7),
	.feature = VOP_FEATURE_OUTPUT_10BIT,
	.max_input = {4096, 8192},
	.max_output = {4096, 2160},
	.intr = &rk3366_vop_intr,
	.ctrl = &rk3288_ctrl_data,
	.win = rk3368_vop_win_data,
	.win_size = ARRAY_SIZE(rk3368_vop_win_data),
};

static const struct vop_ctrl rk3328_ctrl_data = {
	.standby = VOP_REG(RK3328_SYS_CTRL, 0x1, 22),
	.auto_gate_en = VOP_REG(RK3328_SYS_CTRL, 0x1, 23),
	.htotal_pw = VOP_REG(RK3328_DSP_HTOTAL_HS_END, 0x1fff1fff, 0),
	.hact_st_end = VOP_REG(RK3328_DSP_HACT_ST_END, 0x1fff1fff, 0),
	.vtotal_pw = VOP_REG(RK3328_DSP_VTOTAL_VS_END, 0x1fff1fff, 0),
	.vact_st_end = VOP_REG(RK3328_DSP_VACT_ST_END, 0x1fff1fff, 0),
	.vact_st_end_f1 = VOP_REG(RK3328_DSP_VACT_ST_END_F1, 0x1fff1fff, 0),
	.vs_st_end_f1 = VOP_REG(RK3328_DSP_VS_ST_END_F1, 0x1fff1fff, 0),
	.hpost_st_end = VOP_REG(RK3328_POST_DSP_HACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end = VOP_REG(RK3328_POST_DSP_VACT_INFO, 0x1fff1fff, 0),
	.vpost_st_end_f1 = VOP_REG(RK3328_POST_DSP_VACT_INFO_F1, 0x1fff1fff, 0),
	.dsp_interlace = VOP_REG(RK3328_DSP_CTRL0, 0x1, 10),
	.dsp_layer_sel = VOP_REG(RK3328_DSP_CTRL1, 0xff, 8),
	.post_lb_mode = VOP_REG(RK3328_SYS_CTRL, 0x1, 18),
	.global_regdone_en = VOP_REG(RK3328_SYS_CTRL, 0x1, 11),
	.overlay_mode = VOP_REG(RK3328_SYS_CTRL, 0x1, 16),
	.core_dclk_div = VOP_REG(RK3328_DSP_CTRL0, 0x1, 4),
	.p2i_en = VOP_REG(RK3328_DSP_CTRL0, 0x1, 5),
	.rgb_en = VOP_REG(RK3328_SYS_CTRL, 0x1, 12),
	.hdmi_en = VOP_REG(RK3328_SYS_CTRL, 0x1, 13),
	.edp_en = VOP_REG(RK3328_SYS_CTRL, 0x1, 14),
	.mipi_en = VOP_REG(RK3328_SYS_CTRL, 0x1, 15),
	.rgb_pin_pol = VOP_REG(RK3328_DSP_CTRL1, 0xf, 16),
	.hdmi_pin_pol = VOP_REG(RK3328_DSP_CTRL1, 0xf, 20),
	.edp_pin_pol = VOP_REG(RK3328_DSP_CTRL1, 0xf, 24),
	.mipi_pin_pol = VOP_REG(RK3328_DSP_CTRL1, 0xf, 28),

	.dither_down = VOP_REG(RK3328_DSP_CTRL1, 0xf, 1),
	.dither_up = VOP_REG(RK3328_DSP_CTRL1, 0x1, 6),

	.dsp_data_swap = VOP_REG(RK3328_DSP_CTRL0, 0x1f, 12),
	.dsp_ccir656_avg = VOP_REG(RK3328_DSP_CTRL0, 0x1, 20),
	.dsp_blank = VOP_REG(RK3328_DSP_CTRL0, 0x3, 18),
	.dsp_lut_en = VOP_REG(RK3328_DSP_CTRL1, 0x1, 0),
	.out_mode = VOP_REG(RK3328_DSP_CTRL0, 0xf, 0),

	.xmirror = VOP_REG(RK3328_DSP_CTRL0, 0x1, 22),
	.ymirror = VOP_REG(RK3328_DSP_CTRL0, 0x1, 23),

	.dsp_background = VOP_REG(RK3328_DSP_BG, 0xffffffff, 0),

	.cfg_done = VOP_REG(RK3328_REG_CFG_DONE, 0x1, 0),
};

static const struct vop_intr rk3328_vop_intr = {
	.intrs = rk3368_vop_intrs,
	.nintrs = ARRAY_SIZE(rk3368_vop_intrs),
	.line_flag_num[0] = VOP_REG(RK3328_LINE_FLAG, 0xffff, 0),
	.line_flag_num[1] = VOP_REG(RK3328_LINE_FLAG, 0xffff, 16),
	.status = VOP_REG_MASK(RK3328_INTR_STATUS0, 0xffff, 0),
	.enable = VOP_REG_MASK(RK3328_INTR_EN0, 0xffff, 0),
	.clear = VOP_REG_MASK(RK3328_INTR_CLEAR0, 0xffff, 0),
};

static const struct vop_win_data rk3328_vop_win_data[] = {
	{ .base = 0xd0, .phy = &rk3288_win01_data,
	  .type = DRM_PLANE_TYPE_PRIMARY },
	{ .base = 0x1d0, .phy = &rk3288_win01_data,
	  .type = DRM_PLANE_TYPE_OVERLAY },
	{ .base = 0x2d0, .phy = &rk3288_win01_data,
	  .type = DRM_PLANE_TYPE_CURSOR },
};

static const struct vop_data rk3328_vop = {
	.version = VOP_VERSION(3, 8),
	.feature = VOP_FEATURE_OUTPUT_10BIT,
	.max_input = {4096, 8192},
	.max_output = {4096, 2160},
	.intr = &rk3328_vop_intr,
	.ctrl = &rk3328_ctrl_data,
	.win = rk3328_vop_win_data,
	.win_size = ARRAY_SIZE(rk3328_vop_win_data),
};

static const struct vop_scl_regs rk3066_win_scl = {
	.scale_yrgb_x = VOP_REG(RK3036_WIN0_SCL_FACTOR_YRGB, 0xffff, 0x0),
	.scale_yrgb_y = VOP_REG(RK3036_WIN0_SCL_FACTOR_YRGB, 0xffff, 16),
	.scale_cbcr_x = VOP_REG(RK3036_WIN0_SCL_FACTOR_CBR, 0xffff, 0x0),
	.scale_cbcr_y = VOP_REG(RK3036_WIN0_SCL_FACTOR_CBR, 0xffff, 16),
};

static const struct vop_win_phy rk3036_win0_data = {
	.scl = &rk3066_win_scl,
	.data_formats = formats_win_full,
	.nformats = ARRAY_SIZE(formats_win_full),
	.enable = VOP_REG(RK3036_SYS_CTRL, 0x1, 0),
	.format = VOP_REG(RK3036_SYS_CTRL, 0x7, 3),
	.rb_swap = VOP_REG(RK3036_SYS_CTRL, 0x1, 15),
	.act_info = VOP_REG(RK3036_WIN0_ACT_INFO, 0x1fff1fff, 0),
	.dsp_info = VOP_REG(RK3036_WIN0_DSP_INFO, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3036_WIN0_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3036_WIN0_YRGB_MST, 0xffffffff, 0),
	.uv_mst = VOP_REG(RK3036_WIN0_CBR_MST, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3036_WIN0_VIR, 0xffff, 0),
	.uv_vir = VOP_REG(RK3036_WIN0_VIR, 0x1fff, 16),
	.alpha_mode = VOP_REG(RK3036_DSP_CTRL0, 0x1, 18),
	.alpha_en = VOP_REG(RK3036_ALPHA_CTRL, 0x1, 0)
};

static const struct vop_win_phy rk3036_win1_data = {
	.data_formats = formats_win_lite,
	.nformats = ARRAY_SIZE(formats_win_lite),
	.enable = VOP_REG(RK3036_SYS_CTRL, 0x1, 1),
	.format = VOP_REG(RK3036_SYS_CTRL, 0x7, 6),
	.rb_swap = VOP_REG(RK3036_SYS_CTRL, 0x1, 19),
	.act_info = VOP_REG(RK3036_WIN1_ACT_INFO, 0x1fff1fff, 0),
	.dsp_info = VOP_REG(RK3036_WIN1_DSP_INFO, 0x0fff0fff, 0),
	.dsp_st = VOP_REG(RK3036_WIN1_DSP_ST, 0x1fff1fff, 0),
	.yrgb_mst = VOP_REG(RK3036_WIN1_MST, 0xffffffff, 0),
	.yrgb_vir = VOP_REG(RK3036_WIN1_VIR, 0xffff, 0),
	.alpha_mode = VOP_REG(RK3036_DSP_CTRL0, 0x1, 19),
	.alpha_en = VOP_REG(RK3036_ALPHA_CTRL, 0x1, 1)
};

static const struct vop_win_data rk3036_vop_win_data[] = {
	{ .base = 0x00, .phy = &rk3036_win0_data,
	  .type = DRM_PLANE_TYPE_PRIMARY },
	{ .base = 0x00, .phy = &rk3036_win1_data,
	  .type = DRM_PLANE_TYPE_CURSOR },
};

static const int rk3036_vop_intrs[] = {
	DSP_HOLD_VALID_INTR,
	FS_INTR,
	LINE_FLAG_INTR,
	BUS_ERROR_INTR,
};

static const struct vop_intr rk3036_intr = {
	.intrs = rk3036_vop_intrs,
	.nintrs = ARRAY_SIZE(rk3036_vop_intrs),
	.line_flag_num[0] = VOP_REG(RK3036_INT_STATUS, 0xfff, 12),
	.status = VOP_REG(RK3036_INT_STATUS, 0xf, 0),
	.enable = VOP_REG(RK3036_INT_STATUS, 0xf, 4),
	.clear = VOP_REG(RK3036_INT_STATUS, 0xf, 8),
};

static const struct vop_ctrl rk3036_ctrl_data = {
	.standby = VOP_REG(RK3036_SYS_CTRL, 0x1, 30),
	.out_mode = VOP_REG(RK3036_DSP_CTRL0, 0xf, 0),
	.dsp_blank = VOP_REG(RK3036_DSP_CTRL1, 0x1, 24),
	.pin_pol = VOP_REG(RK3036_DSP_CTRL0, 0xf, 4),
	.dsp_layer_sel = VOP_REG(RK3036_DSP_CTRL0, 0x1, 8),
	.htotal_pw = VOP_REG(RK3036_DSP_HTOTAL_HS_END, 0x1fff1fff, 0),
	.hact_st_end = VOP_REG(RK3036_DSP_HACT_ST_END, 0x1fff1fff, 0),
	.vtotal_pw = VOP_REG(RK3036_DSP_VTOTAL_VS_END, 0x1fff1fff, 0),
	.vact_st_end = VOP_REG(RK3036_DSP_VACT_ST_END, 0x1fff1fff, 0),
	.cfg_done = VOP_REG(RK3036_REG_CFG_DONE, 0x1, 0),
};

static const struct vop_data rk3036_vop = {
	.version = VOP_VERSION(2, 2),
	.max_input = {1920, 1080},
	.max_output = {1920, 1080},
	.ctrl = &rk3036_ctrl_data,
	.intr = &rk3036_intr,
	.win = rk3036_vop_win_data,
	.win_size = ARRAY_SIZE(rk3036_vop_win_data),
};

static const struct of_device_id vop_driver_dt_match[] = {
	{ .compatible = "rockchip,rk3036-vop",
	  .data = &rk3036_vop },
	{ .compatible = "rockchip,rk3288-vop",
	  .data = &rk3288_vop },
	{ .compatible = "rockchip,rk3368-vop",
	  .data = &rk3368_vop },
	{ .compatible = "rockchip,rk3366-vop",
	  .data = &rk3366_vop },
	{ .compatible = "rockchip,rk3399-vop-big",
	  .data = &rk3399_vop_big },
	{ .compatible = "rockchip,rk3399-vop-lit",
	  .data = &rk3399_vop_lit },
	{ .compatible = "rockchip,rk322x-vop",
	  .data = &rk322x_vop },
	{ .compatible = "rockchip,rk3328-vop",
	  .data = &rk3328_vop },
	{},
};
MODULE_DEVICE_TABLE(of, vop_driver_dt_match);

static int vop_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	if (!dev->of_node) {
		dev_err(dev, "can't find vop devices\n");
		return -ENODEV;
	}

	return component_add(dev, &vop_component_ops);
}

static int vop_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &vop_component_ops);

	return 0;
}

struct platform_driver vop_platform_driver = {
	.probe = vop_probe,
	.remove = vop_remove,
	.driver = {
		.name = "rockchip-vop",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(vop_driver_dt_match),
	},
};

module_platform_driver(vop_platform_driver);

MODULE_AUTHOR("Mark Yao <mark.yao@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP VOP Driver");
MODULE_LICENSE("GPL v2");

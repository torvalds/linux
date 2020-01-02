// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>	/* for ISP params */
#include <linux/rk-preisp.h>
#include "dev.h"
#include "regs.h"
#include "regs_v2x.h"
#include "isp_params_v2x.h"

#define ISP2X_PACK_4BYTE(a, b, c, d)	\
	(((a) & 0xFF) << 0 | ((b) & 0xFF) << 8 | \
	 ((c) & 0xFF) << 16 | ((d) & 0xFF) << 24)

#define ISP2X_PACK_2SHORT(a, b)	\
	(((a) & 0xFFFF) << 0 | ((b) & 0xFFFF) << 16)

#define ISP2X_REG_WR_MASK BIT(31) //disable write protect

static inline void
rkisp_iowrite32(struct rkisp_isp_params_vdev *params_vdev,
		u32 value, u32 addr)
{
	iowrite32(value, params_vdev->dev->base_addr + addr);
}

static inline u32
rkisp_ioread32(struct rkisp_isp_params_vdev *params_vdev,
	       u32 addr)
{
	return ioread32(params_vdev->dev->base_addr + addr);
}

static inline void
isp_param_set_bits(struct rkisp_isp_params_vdev *params_vdev,
		   u32 reg, u32 bit_mask)
{
	u32 val;

	val = rkisp_ioread32(params_vdev, reg);
	rkisp_iowrite32(params_vdev, val | bit_mask, reg);
}

static inline void
isp_param_clear_bits(struct rkisp_isp_params_vdev *params_vdev,
		     u32 reg, u32 bit_mask)
{
	u32 val;

	val = rkisp_ioread32(params_vdev, reg);
	rkisp_iowrite32(params_vdev, val & ~bit_mask, reg);
}

static void
isp_dpcc_config(struct rkisp_isp_params_vdev *params_vdev,
		const struct isp2x_dpcc_cfg *arg)
{
}

static void
isp_dpcc_enable(struct rkisp_isp_params_vdev *params_vdev,
		bool en)
{
}

static void
isp_bls_config(struct rkisp_isp_params_vdev *params_vdev,
	       const struct isp2x_bls_cfg *arg)
{
}

static void
isp_bls_enable(struct rkisp_isp_params_vdev *params_vdev,
	       bool en)
{
}

static void
isp_sdg_config(struct rkisp_isp_params_vdev *params_vdev,
	       const struct isp2x_sdg_cfg *arg)
{
}

static void
isp_sdg_enable(struct rkisp_isp_params_vdev *params_vdev,
	       bool en)
{
}

static void
isp_sihst_config(struct rkisp_isp_params_vdev *params_vdev,
		 const struct isp2x_sihst_cfg *arg)
{
	u32 i, j;
	u32 value;
	u32 hist_ctrl;
	u32 block_hsize, block_vsize;
	u32 wnd_num_idx, hist_weight_num;
	u8 weight15x15[ISP2X_SIHST_WEIGHT_REG_SIZE];
	const u32 hist_wnd_num[] = {
		5, 9, 15, 15
	};

	wnd_num_idx = arg->wnd_num;
	for (i = 0; i < ISP2X_HIST_WIN_NUM; i++) {
		/* avoid to override the old enable value */
		hist_ctrl = rkisp_ioread32(params_vdev, ISP_HIST_HIST_CTRL + i * 0x10);
		hist_ctrl &= ISP2X_SIHST_CTRL_INTRSEL_MASK |
				ISP2X_SIHST_CTRL_WNDNUM_MASK |
			    ISP2X_SIHST_CTRL_EN_MASK;
		hist_ctrl = hist_ctrl |
			    ISP2X_SIHST_CTRL_DATASEL_SET(arg->win_cfg[i].data_sel) |
			    ISP2X_SIHST_CTRL_WATERLINE_SET(arg->win_cfg[i].waterline) |
			    ISP2X_SIHST_CTRL_AUTOSTOP_SET(arg->win_cfg[i].auto_stop) |
			    ISP2X_SIHST_CTRL_MODE_SET(arg->win_cfg[i].mode) |
			    ISP2X_SIHST_CTRL_STEPSIZE_SET(arg->win_cfg[i].stepsize);
		rkisp_iowrite32(params_vdev, hist_ctrl, ISP_HIST_HIST_CTRL + i * 0x10);

		rkisp_iowrite32(params_vdev,
				 ISP2X_SIHST_OFFS_SET(arg->win_cfg[i].win.h_offs,
						      arg->win_cfg[i].win.v_offs),
				 ISP_HIST_HIST_OFFS + i * 0x10);

		block_hsize = arg->win_cfg[i].win.h_size / hist_wnd_num[wnd_num_idx] - 1;
		block_vsize = arg->win_cfg[i].win.v_size / hist_wnd_num[wnd_num_idx] - 1;
		rkisp_iowrite32(params_vdev,
				 ISP2X_SIHST_SIZE_SET(block_hsize, block_vsize),
				 ISP_HIST_HIST_SIZE + i * 0x10);
	}

	memset(weight15x15, 0x00, sizeof(weight15x15));
	for (i = 0; i < hist_wnd_num[wnd_num_idx]; i++) {
		for (j = 0; j < hist_wnd_num[wnd_num_idx]; j++) {
			weight15x15[i * ISP2X_SIHST_ROW_NUM + j] =
				arg->hist_weight[i * hist_wnd_num[wnd_num_idx] + j];
		}
	}

	hist_weight_num = ISP2X_SIHST_WEIGHT_REG_SIZE;
	for (i = 0; i < (hist_weight_num / 4); i++) {
		value = ISP2X_SIHST_WEIGHT_SET(
				 weight15x15[4 * i + 0],
				 weight15x15[4 * i + 1],
				 weight15x15[4 * i + 2],
				 weight15x15[4 * i + 3]);
		rkisp_iowrite32(params_vdev, value,
				 ISP_HIST_HIST_WEIGHT_0 + 4 * i);
	}
	value = ISP2X_SIHST_WEIGHT_SET(
				 weight15x15[4 * i + 0], 0, 0, 0);
	rkisp_iowrite32(params_vdev, value,
				 ISP_HIST_HIST_WEIGHT_0 + 4 * i);

	hist_ctrl = rkisp_ioread32(params_vdev, ISP_HIST_HIST_CTRL);
	hist_ctrl &= ~ISP2X_SIHST_CTRL_WNDNUM_MASK;
	hist_ctrl |= ISP2X_SIHST_CTRL_WNDNUM_SET(arg->wnd_num);
	rkisp_iowrite32(params_vdev, hist_ctrl, ISP_HIST_HIST_CTRL);
}

static void
isp_sihst_enable(struct rkisp_isp_params_vdev *params_vdev,
		 bool en)
{
	u32 hist_ctrl;

	hist_ctrl = rkisp_ioread32(params_vdev, ISP_HIST_HIST_CTRL);
	hist_ctrl &= ~ISP2X_SIHST_CTRL_EN_MASK;
	if (en)
		hist_ctrl |= ISP2X_SIHST_CTRL_EN_SET(0x1);

	rkisp_iowrite32(params_vdev, hist_ctrl, ISP_HIST_HIST_CTRL);
}

static void
isp_lsc_config(struct rkisp_isp_params_vdev *params_vdev,
	       const struct isp2x_lsc_cfg *arg)
{
}

static void
isp_lsc_enable(struct rkisp_isp_params_vdev *params_vdev,
	       bool en)
{
}

static void
isp_awbgain_config(struct rkisp_isp_params_vdev *params_vdev,
		   const struct isp2x_awb_gain_cfg *arg)
{
	rkisp_iowrite32(params_vdev,
			 CIF_ISP_AWB_GAIN_R_SET(arg->gain_green_r) |
			 arg->gain_green_b, CIF_ISP_AWB_GAIN_G_V12);

	rkisp_iowrite32(params_vdev, CIF_ISP_AWB_GAIN_R_SET(arg->gain_red) |
			 arg->gain_blue, CIF_ISP_AWB_GAIN_RB_V12);
}

static void
isp_awbgain_enable(struct rkisp_isp_params_vdev *params_vdev,
		   bool en)
{
	if (!en) {
		rkisp_iowrite32(params_vdev,
			CIF_ISP_AWB_GAIN_R_SET(0x0100) | 0x100, CIF_ISP_AWB_GAIN_G_V12);

		rkisp_iowrite32(params_vdev,
			CIF_ISP_AWB_GAIN_R_SET(0x0100) | 0x100, CIF_ISP_AWB_GAIN_RB_V12);
	}
}

static void
isp_bdm_config(struct rkisp_isp_params_vdev *params_vdev,
	       const struct isp2x_bdm_config *arg)
{
}

static void
isp_bdm_enable(struct rkisp_isp_params_vdev *params_vdev,
	       bool en)
{
}

static void
isp_ctk_config(struct rkisp_isp_params_vdev *params_vdev,
	       const struct isp2x_ctk_cfg *arg)
{
}

static void
isp_ctk_enable(struct rkisp_isp_params_vdev *params_vdev,
	       bool en)
{
}

static void
isp_goc_config(struct rkisp_isp_params_vdev *params_vdev,
	       const struct isp2x_gammaout_cfg *arg)
{
}

static void
isp_goc_enable(struct rkisp_isp_params_vdev *params_vdev,
	       bool en)
{
}

static void
isp_cproc_config(struct rkisp_isp_params_vdev *params_vdev,
		 const struct isp2x_cproc_cfg *arg)
{
}

static void
isp_cproc_enable(struct rkisp_isp_params_vdev *params_vdev,
		 bool en)
{
}

static void
isp_siaf_config(struct rkisp_isp_params_vdev *params_vdev,
		const struct isp2x_siaf_cfg *arg)
{
	unsigned int i;
	size_t num_of_win;
	u32 afm_ctrl;

	num_of_win = min_t(size_t, ARRAY_SIZE(arg->afm_win),
				  arg->num_afm_win);
	afm_ctrl = rkisp_ioread32(params_vdev, ISP_AFM_CTRL);

	/* Switch off to configure. */
	isp_param_clear_bits(params_vdev, ISP_AFM_CTRL, ISP2X_SIAF_ENA);
	for (i = 0; i < num_of_win; i++) {
		rkisp_iowrite32(params_vdev,
				 ISP2X_SIAF_WIN_X(arg->afm_win[i].win.h_offs) |
				 ISP2X_SIAF_WIN_Y(arg->afm_win[i].win.v_offs),
				 ISP_AFM_LT_A + i * 8);
		rkisp_iowrite32(params_vdev,
				 ISP2X_SIAF_WIN_X(arg->afm_win[i].win.h_size +
						      arg->afm_win[i].win.h_offs) |
				 ISP2X_SIAF_WIN_Y(arg->afm_win[i].win.v_size +
						      arg->afm_win[i].win.v_offs),
				 ISP_AFM_RB_A + i * 8);
	}
	rkisp_iowrite32(params_vdev, arg->thres, ISP_AFM_THRES);

	rkisp_iowrite32(params_vdev,
		ISP2X_SIAF_SET_SHIFT_A(arg->afm_win[0].lum_shift, arg->afm_win[0].sum_shift) |
		ISP2X_SIAF_SET_SHIFT_B(arg->afm_win[1].lum_shift, arg->afm_win[1].sum_shift) |
		ISP2X_SIAF_SET_SHIFT_C(arg->afm_win[2].lum_shift, arg->afm_win[2].sum_shift),
		ISP_AFM_VAR_SHIFT);

	/* restore afm status */
	rkisp_iowrite32(params_vdev, afm_ctrl, ISP_AFM_CTRL);
}

static void
isp_siaf_enable(struct rkisp_isp_params_vdev *params_vdev,
		bool en)
{
	u32 afm_ctrl = rkisp_ioread32(params_vdev, ISP_AFM_CTRL);

	if (en)
		afm_ctrl |= ISP2X_SIAF_ENA;
	else
		afm_ctrl &= ~ISP2X_SIAF_ENA;

	rkisp_iowrite32(params_vdev, afm_ctrl, ISP_AFM_CTRL);
}

static void
isp_siawb_config(struct rkisp_isp_params_vdev *params_vdev,
		 const struct isp2x_siawb_meas_cfg *arg)
{
	u32 reg_val = 0;
	/* based on the mode,configure the awb module */
	if (arg->awb_mode == CIFISP_AWB_MODE_YCBCR) {
		/* Reference Cb and Cr */
		rkisp_iowrite32(params_vdev,
				 CIF_ISP_AWB_REF_CR_SET(arg->awb_ref_cr) |
				 arg->awb_ref_cb, CIF_ISP_AWB_REF_V10);
		/* Yc Threshold */
		rkisp_iowrite32(params_vdev,
				 CIF_ISP_AWB_MAX_Y_SET(arg->max_y) |
				 CIF_ISP_AWB_MIN_Y_SET(arg->min_y) |
				 CIF_ISP_AWB_MAX_CS_SET(arg->max_csum) |
				 arg->min_c, CIF_ISP_AWB_THRESH_V10);
	}

	reg_val = rkisp_ioread32(params_vdev, CIF_ISP_AWB_PROP_V10);
	if (arg->enable_ymax_cmp)
		reg_val |= CIF_ISP_AWB_YMAX_CMP_EN;
	else
		reg_val &= ~CIF_ISP_AWB_YMAX_CMP_EN;
	if (arg->awb_mode != CIFISP_AWB_MODE_YCBCR)
		reg_val |= CIF_ISP_AWB_MODE_RGB;
	else
		reg_val &= ~CIF_ISP_AWB_MODE_RGB;
	rkisp_iowrite32(params_vdev, reg_val, CIF_ISP_AWB_PROP_V10);

	/* window offset */
	rkisp_iowrite32(params_vdev,
			 arg->awb_wnd.v_offs, CIF_ISP_AWB_WND_V_OFFS_V10);
	rkisp_iowrite32(params_vdev,
			 arg->awb_wnd.h_offs, CIF_ISP_AWB_WND_H_OFFS_V10);
	/* AWB window size */
	rkisp_iowrite32(params_vdev,
			 arg->awb_wnd.v_size, CIF_ISP_AWB_WND_V_SIZE_V10);
	rkisp_iowrite32(params_vdev,
			 arg->awb_wnd.h_size, CIF_ISP_AWB_WND_H_SIZE_V10);
	/* Number of frames */
	rkisp_iowrite32(params_vdev,
			 arg->frames, CIF_ISP_AWB_FRAMES_V10);
}

static void
isp_siawb_enable(struct rkisp_isp_params_vdev *params_vdev,
		 bool en)
{
	u32 reg_val = rkisp_ioread32(params_vdev, CIF_ISP_AWB_PROP_V10);

	/* switch off */
	reg_val &= CIF_ISP_AWB_MODE_MASK_NONE;

	if (en) {
		reg_val |= CIF_ISP_AWB_ENABLE;

		rkisp_iowrite32(params_vdev, reg_val, CIF_ISP_AWB_PROP_V10);

		/* Measurements require AWB block be active. */
		/* TODO: need to enable here ? awb_gain_enable has done this */
		isp_param_set_bits(params_vdev, CIF_ISP_CTRL,
				   CIF_ISP_CTRL_ISP_AWB_ENA);
	} else {
		rkisp_iowrite32(params_vdev,
				 reg_val, CIF_ISP_AWB_PROP_V10);
		isp_param_clear_bits(params_vdev, CIF_ISP_CTRL,
				     CIF_ISP_CTRL_ISP_AWB_ENA);
	}
}

static void
isp_ie_config(struct rkisp_isp_params_vdev *params_vdev,
	      const struct isp2x_ie_cfg *arg)
{
}

static void
isp_ie_enable(struct rkisp_isp_params_vdev *params_vdev,
	      bool en)
{
}

static void
isp_yuvae_config(struct rkisp_isp_params_vdev *params_vdev,
		 const struct isp2x_yuvae_meas_cfg *arg)
{
	u32 i;
	u32 exp_ctrl;
	u32 block_hsize, block_vsize;
	u32 wnd_num_idx = 0;
	const u32 ae_wnd_num[] = {
		1, 15
	};

	/* avoid to override the old enable value */
	exp_ctrl = rkisp_ioread32(params_vdev, ISP_YUVAE_CTRL);
	exp_ctrl &= ~(ISP2X_YUVAE_WNDNUM_SET |
		      ISP2X_YUVAE_SUBWIN1_EN |
		      ISP2X_YUVAE_SUBWIN2_EN |
		      ISP2X_YUVAE_SUBWIN3_EN |
		      ISP2X_YUVAE_SUBWIN4_EN |
		      ISP2X_YUVAE_YSEL |
		      ISP2X_REG_WR_MASK);
	if (arg->ysel)
		exp_ctrl |= ISP2X_YUVAE_YSEL;
	if (arg->wnd_num) {
		exp_ctrl |= ISP2X_YUVAE_WNDNUM_SET;
		wnd_num_idx = 1;
	}
	if (arg->subwin_en[0])
		exp_ctrl |= ISP2X_YUVAE_SUBWIN1_EN;
	if (arg->subwin_en[1])
		exp_ctrl |= ISP2X_YUVAE_SUBWIN2_EN;
	if (arg->subwin_en[2])
		exp_ctrl |= ISP2X_YUVAE_SUBWIN3_EN;
	if (arg->subwin_en[3])
		exp_ctrl |= ISP2X_YUVAE_SUBWIN4_EN;

	rkisp_iowrite32(params_vdev, exp_ctrl, ISP_YUVAE_CTRL);

	rkisp_iowrite32(params_vdev,
			 ISP2X_YUVAE_V_OFFSET_SET(arg->win.v_offs) |
			 ISP2X_YUVAE_H_OFFSET_SET(arg->win.h_offs),
			 ISP_YUVAE_OFFSET);

	block_hsize = arg->win.h_size / ae_wnd_num[wnd_num_idx] - 1;
	block_vsize = arg->win.v_size / ae_wnd_num[wnd_num_idx] - 1;
	rkisp_iowrite32(params_vdev,
			 ISP2X_YUVAE_V_SIZE_SET(block_vsize) |
			 ISP2X_YUVAE_H_SIZE_SET(block_hsize),
			 ISP_YUVAE_BLK_SIZE);

	for (i = 0; i < ISP2X_YUVAE_SUBWIN_NUM; i++) {
		rkisp_iowrite32(params_vdev,
			 ISP2X_YUVAE_SUBWIN_V_OFFSET_SET(arg->subwin[i].v_offs) |
			 ISP2X_YUVAE_SUBWIN_H_OFFSET_SET(arg->subwin[i].h_offs),
			 ISP_YUVAE_WND1_OFFSET + 8 * i);

		rkisp_iowrite32(params_vdev,
			 ISP2X_YUVAE_SUBWIN_V_SIZE_SET(arg->subwin[i].v_size + arg->subwin[i].v_offs) |
			 ISP2X_YUVAE_SUBWIN_H_SIZE_SET(arg->subwin[i].h_size + arg->subwin[i].h_offs),
			 ISP_YUVAE_WND1_SIZE + 8 * i);
	}
}

static void
isp_yuvae_enable(struct rkisp_isp_params_vdev *params_vdev,
		 bool en)
{
	u32 exp_ctrl;

	exp_ctrl = rkisp_ioread32(params_vdev, ISP_YUVAE_CTRL);
	exp_ctrl &= ~ISP2X_REG_WR_MASK;
	if (en)
		exp_ctrl |= ISP2X_YUVAE_ENA;
	else
		exp_ctrl &= ~ISP2X_YUVAE_ENA;

	rkisp_iowrite32(params_vdev, exp_ctrl, ISP_YUVAE_CTRL);
}

static void
isp_wdr_config(struct rkisp_isp_params_vdev *params_vdev,
	       const struct isp2x_wdr_cfg *arg)
{
}

static void
isp_wdr_enable(struct rkisp_isp_params_vdev *params_vdev,
	       bool en)
{
}

static void
isp_iesharp_config(struct rkisp_isp_params_vdev *params_vdev,
		   const struct isp2x_rkiesharp_cfg *arg)
{
}

static void
isp_iesharp_enable(struct rkisp_isp_params_vdev *params_vdev,
		   bool en)
{
}

static void
isp_rawaf_config(struct rkisp_isp_params_vdev *params_vdev,
		 const struct isp2x_rawaf_meas_cfg *arg)
{
	u32 i, var;
	u16 h_size, v_size;
	u16 h_offs, v_offs;
	size_t num_of_win = min_t(size_t, ARRAY_SIZE(arg->win),
				  arg->num_afm_win);
	u32 value = rkisp_ioread32(params_vdev, ISP_RAWAF_CTRL);

	for (i = 0; i < num_of_win; i++) {
		h_size = arg->win[i].h_size;
		v_size = arg->win[i].v_size;
		h_offs = arg->win[i].h_offs < 2 ? 2 : arg->win[i].h_offs;
		v_offs = arg->win[i].v_offs < 1 ? 1 : arg->win[i].v_offs;

		if (i == 0) {
			h_size = h_size / 15 * 15;
			v_size = v_size / 15 * 15;
		}

		// (horizontal left row), value must be greater or equal 2
		// (vertical top line), value must be greater or equal 1
		rkisp_iowrite32(params_vdev,
				 ISP2X_PACK_2SHORT(v_offs, h_offs),
				 ISP_RAWAF_LT_A + i * 8);

		// value must be smaller than [width of picture -2]
		// value must be lower than (number of lines -2)
		rkisp_iowrite32(params_vdev,
				 ISP2X_PACK_2SHORT(v_size, h_size),
				 ISP_RAWAF_RB_A + i * 8);
	}

	var = 0;
	for (i = 0; i < ISP2X_RAWAF_LINE_NUM; i++) {
		if (arg->line_en[i])
			var |= ISP2X_RAWAF_INT_LINE0_EN << i;
		var |= ISP2X_RAWAF_INT_LINE0_NUM(arg->line_num[i]) << 4 * i;
	}
	rkisp_iowrite32(params_vdev, var, ISP_RAWAF_INT_LINE);

	rkisp_iowrite32(params_vdev,
		ISP2X_PACK_4BYTE(arg->gaus_coe_h0, arg->gaus_coe_h1, arg->gaus_coe_h2, 0),
		ISP_RAWAF_GAUS_COE);

	var = rkisp_ioread32(params_vdev, ISP_RAWAF_THRES);
	var &= ~(ISP2X_RAWAF_THRES(0xFFFF));
	var |= arg->afm_thres;
	rkisp_iowrite32(params_vdev, var, ISP_RAWAF_THRES);

	rkisp_iowrite32(params_vdev,
		ISP2X_RAWAF_SET_SHIFT_A(arg->lum_var_shift[0], arg->afm_var_shift[0]) |
		ISP2X_RAWAF_SET_SHIFT_B(arg->lum_var_shift[1], arg->afm_var_shift[1]),
		ISP_RAWAF_VAR_SHIFT);

	for (i = 0; i < ISP2X_RAWAF_GAMMA_NUM / 2; i++)
		rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->gamma_y[2 * i], arg->gamma_y[2 * i + 1]),
			ISP_RAWAF_GAMMA_Y0 + i * 4);

	rkisp_iowrite32(params_vdev,
		ISP2X_PACK_2SHORT(arg->gamma_y[16], 0),
		ISP_RAWAF_GAMMA_Y8);

	value &= ~ISP2X_RAWAF_ENA;
	if (arg->gamma_en)
		value |= ISP2X_RAWAF_GAMMA_ENA;
	else
		value &= ~ISP2X_RAWAF_GAMMA_ENA;
	if (arg->gaus_en)
		value |= ISP2X_RAWAF_GAUS_ENA;
	else
		value &= ~ISP2X_RAWAF_GAUS_ENA;
	value &= ~ISP2X_REG_WR_MASK;
	rkisp_iowrite32(params_vdev, value, ISP_RAWAF_CTRL);

	value = rkisp_ioread32(params_vdev, CTRL_VI_ISP_PATH);
	value &= ~(ISP2X_ISPPATH_RAWAF_SEL_SET(3));
	value |= ISP2X_ISPPATH_RAWAF_SEL_SET(arg->rawaf_sel);
	rkisp_iowrite32(params_vdev, value, CTRL_VI_ISP_PATH);
}

static void
isp_rawaf_enable(struct rkisp_isp_params_vdev *params_vdev,
		 bool en)
{
	u32 afm_ctrl = rkisp_ioread32(params_vdev, ISP_RAWAF_CTRL);

	afm_ctrl &= ~ISP2X_REG_WR_MASK;
	if (en)
		afm_ctrl |= ISP2X_RAWAF_ENA;
	else
		afm_ctrl &= ~ISP2X_RAWAF_ENA;

	rkisp_iowrite32(params_vdev, afm_ctrl, ISP_RAWAF_CTRL);
}

static void
isp_rawaelite_config(struct rkisp_isp_params_vdev *params_vdev,
		     const struct isp2x_rawaelite_meas_cfg *arg)
{
	u32 value;
	u32 block_hsize, block_vsize;
	u32 wnd_num_idx = 0;
	const u32 ae_wnd_num[] = {
		1, 5
	};

	value = rkisp_ioread32(params_vdev, ISP_RAWAE_LITE_CTRL);
	value &= ~(ISP2X_RAWAE_LITE_WNDNUM_SET(0x1));
	if (arg->wnd_num) {
		value |= ISP2X_RAWAE_LITE_WNDNUM_SET(0x1);
		wnd_num_idx = 1;
	}
	value &= ~ISP2X_REG_WR_MASK;
	rkisp_iowrite32(params_vdev, value, ISP_RAWAE_LITE_CTRL);

	rkisp_iowrite32(params_vdev,
			ISP2X_RAWAE_LITE_V_OFFSET_SET(arg->win.v_offs) |
			ISP2X_RAWAE_LITE_H_OFFSET_SET(arg->win.h_offs),
			ISP_RAWAE_LITE_OFFSET);

	block_hsize = arg->win.h_size / ae_wnd_num[wnd_num_idx] - 1;
	block_vsize = arg->win.v_size / ae_wnd_num[wnd_num_idx] - 1;
	rkisp_iowrite32(params_vdev,
			 ISP2X_RAWAE_LITE_V_SIZE_SET(block_vsize) |
			 ISP2X_RAWAE_LITE_H_SIZE_SET(block_hsize),
			 ISP_RAWAE_LITE_BLK_SIZ);
}

static void
isp_rawaelite_enable(struct rkisp_isp_params_vdev *params_vdev,
		     bool en)
{
	u32 exp_ctrl;

	exp_ctrl = rkisp_ioread32(params_vdev, ISP_RAWAE_LITE_CTRL);
	exp_ctrl &= ~ISP2X_REG_WR_MASK;
	if (en)
		exp_ctrl |= ISP2X_RAWAE_LITE_ENA;
	else
		exp_ctrl &= ~ISP2X_RAWAE_LITE_ENA;

	rkisp_iowrite32(params_vdev, exp_ctrl, ISP_RAWAE_LITE_CTRL);
}

static void
isp_rawaebig_config(struct rkisp_isp_params_vdev *params_vdev,
		    const struct isp2x_rawaebig_meas_cfg *arg, u32 blk_no)
{
	u32 i;
	u32 value;
	u32 block_hsize, block_vsize;
	u32 wnd_num_idx = 0;
	const u32 ae_wnd_num[] = {
		1, 5, 15, 15
	};
	u32 addr;

	switch (blk_no) {
	case 0:
		addr = RAWAE_BIG1_BASE;
		break;
	case 1:
		addr = RAWAE_BIG2_BASE;
		break;
	case 2:
		addr = RAWAE_BIG3_BASE;
		break;
	default:
		addr = RAWAE_BIG1_BASE;
		break;
	}

	/* avoid to override the old enable value */
	value = rkisp_ioread32(params_vdev, addr + RAWAE_BIG_CTRL);
	value &= ~(ISP2X_RAWAEBIG_WNDNUM_SET(0x3) |
		   ISP2X_RAWAEBIG_SUBWIN1_EN |
		   ISP2X_RAWAEBIG_SUBWIN2_EN |
		   ISP2X_RAWAEBIG_SUBWIN3_EN |
		   ISP2X_RAWAEBIG_SUBWIN4_EN |
		   ISP2X_REG_WR_MASK);

	wnd_num_idx = arg->wnd_num;
	value |= ISP2X_RAWAEBIG_WNDNUM_SET(wnd_num_idx);

	if (arg->subwin_en[0])
		value |= ISP2X_RAWAEBIG_SUBWIN1_EN;
	if (arg->subwin_en[1])
		value |= ISP2X_RAWAEBIG_SUBWIN2_EN;
	if (arg->subwin_en[2])
		value |= ISP2X_RAWAEBIG_SUBWIN3_EN;
	if (arg->subwin_en[3])
		value |= ISP2X_RAWAEBIG_SUBWIN4_EN;

	rkisp_iowrite32(params_vdev, value, addr + RAWAE_BIG_CTRL);

	rkisp_iowrite32(params_vdev,
			 ISP2X_RAWAEBIG_V_OFFSET_SET(arg->win.v_offs) |
			 ISP2X_RAWAEBIG_H_OFFSET_SET(arg->win.h_offs),
			 addr + RAWAE_BIG_OFFSET);

	block_hsize = arg->win.h_size / ae_wnd_num[wnd_num_idx] - 1;
	block_vsize = arg->win.v_size / ae_wnd_num[wnd_num_idx] - 1;
	rkisp_iowrite32(params_vdev,
			 ISP2X_RAWAEBIG_V_SIZE_SET(block_vsize) |
			 ISP2X_RAWAEBIG_H_SIZE_SET(block_hsize),
			 addr + RAWAE_BIG_BLK_SIZE);

	for (i = 0; i < ISP2X_RAWAEBIG_SUBWIN_NUM; i++) {
		rkisp_iowrite32(params_vdev,
			 ISP2X_RAWAEBIG_SUBWIN_V_OFFSET_SET(arg->subwin[i].v_offs) |
			 ISP2X_RAWAEBIG_SUBWIN_H_OFFSET_SET(arg->subwin[i].h_offs),
			 addr + RAWAE_BIG_WND1_OFFSET + 8 * i);

		rkisp_iowrite32(params_vdev,
			 ISP2X_RAWAEBIG_SUBWIN_V_SIZE_SET(arg->subwin[i].v_size + arg->subwin[i].v_offs) |
			 ISP2X_RAWAEBIG_SUBWIN_H_SIZE_SET(arg->subwin[i].h_size + arg->subwin[i].h_offs),
			 addr + RAWAE_BIG_WND1_SIZE + 8 * i);
	}

	if (blk_no == 0) {
		value = rkisp_ioread32(params_vdev, CTRL_VI_ISP_PATH);
		value &= ~(ISP2X_ISPPATH_RAWAE_SEL_SET(3));
		value |= ISP2X_ISPPATH_RAWAE_SEL_SET(arg->rawae_sel);
		rkisp_iowrite32(params_vdev, value, CTRL_VI_ISP_PATH);
	}
}

static void
isp_rawaebig_enable(struct rkisp_isp_params_vdev *params_vdev,
		    bool en, u32 blk_no)
{
	u32 exp_ctrl;
	u32 addr;

	switch (blk_no) {
	case 0:
		addr = RAWAE_BIG1_BASE;
		break;
	case 1:
		addr = RAWAE_BIG2_BASE;
		break;
	case 2:
		addr = RAWAE_BIG3_BASE;
		break;
	default:
		addr = RAWAE_BIG1_BASE;
		break;
	}

	exp_ctrl = rkisp_ioread32(params_vdev, addr + RAWAE_BIG_CTRL);
	exp_ctrl &= ~ISP2X_REG_WR_MASK;
	if (en)
		exp_ctrl |= ISP2X_RAWAEBIG_ENA;
	else
		exp_ctrl &= ~ISP2X_RAWAEBIG_ENA;

	rkisp_iowrite32(params_vdev, exp_ctrl, addr + RAWAE_BIG_CTRL);
}

static void
isp_rawaebig1_config(struct rkisp_isp_params_vdev *params_vdev,
		     const struct isp2x_rawaebig_meas_cfg *arg)
{
	isp_rawaebig_config(params_vdev, arg, 0);
}

static void
isp_rawaebig1_enable(struct rkisp_isp_params_vdev *params_vdev,
		     bool en)
{
	isp_rawaebig_enable(params_vdev, en, 0);
}

static void
isp_rawaebig2_config(struct rkisp_isp_params_vdev *params_vdev,
		     const struct isp2x_rawaebig_meas_cfg *arg)
{
	isp_rawaebig_config(params_vdev, arg, 1);
}

static void
isp_rawaebig2_enable(struct rkisp_isp_params_vdev *params_vdev,
		     bool en)
{
	isp_rawaebig_enable(params_vdev, en, 1);
}

static void
isp_rawaebig3_config(struct rkisp_isp_params_vdev *params_vdev,
		     const struct isp2x_rawaebig_meas_cfg *arg)
{
	isp_rawaebig_config(params_vdev, arg, 2);
}

static void
isp_rawaebig3_enable(struct rkisp_isp_params_vdev *params_vdev,
		     bool en)
{
	isp_rawaebig_enable(params_vdev, en, 2);
}

static void
isp_rawawb_config(struct rkisp_isp_params_vdev *params_vdev,
		  const struct isp2x_rawawb_meas_cfg *arg)
{
	u32 value;

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_h_offs, arg->sw_rawawb_v_offs),
			 ISP_RAWAWB_WIN_OFFS);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_h_size, arg->sw_rawawb_v_size),
			 ISP_RAWAWB_WIN_SIZE);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_r_max, arg->sw_rawawb_g_max),
			 ISP_RAWAWB_LIMIT_RG_MAX);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_b_max, arg->sw_rawawb_y_max),
			 ISP_RAWAWB_LIMIT_BY_MAX);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_r_min, arg->sw_rawawb_g_min),
			 ISP_RAWAWB_LIMIT_RG_MIN);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_b_min, arg->sw_rawawb_y_min),
			 ISP_RAWAWB_LIMIT_BY_MIN);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_coeff_y_r, arg->sw_rawawb_coeff_y_g),
			 ISP_RAWAWB_RGB2Y_0);
	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_coeff_y_b, 0),
			 ISP_RAWAWB_RGB2Y_1);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_coeff_u_r, arg->sw_rawawb_coeff_u_g),
			 ISP_RAWAWB_RGB2U_0);
	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_coeff_u_b, 0),
			 ISP_RAWAWB_RGB2U_1);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_coeff_v_r, arg->sw_rawawb_coeff_v_g),
			 ISP_RAWAWB_RGB2V_0);
	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_coeff_v_b, 0),
			 ISP_RAWAWB_RGB2V_1);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex0_u_0, arg->sw_rawawb_vertex0_v_0),
			 ISP_RAWAWB_UV_DETC_VERTEX0_0);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex1_u_0, arg->sw_rawawb_vertex1_v_0),
			 ISP_RAWAWB_UV_DETC_VERTEX1_0);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex2_u_0, arg->sw_rawawb_vertex2_v_0),
			 ISP_RAWAWB_UV_DETC_VERTEX2_0);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex3_u_0, arg->sw_rawawb_vertex3_v_0),
			 ISP_RAWAWB_UV_DETC_VERTEX3_0);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope01_0,
			 ISP_RAWAWB_UV_DETC_ISLOPE01_0);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope12_0,
			 ISP_RAWAWB_UV_DETC_ISLOPE12_0);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope23_0,
			 ISP_RAWAWB_UV_DETC_ISLOPE23_0);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope30_0,
			 ISP_RAWAWB_UV_DETC_ISLOPE30_0);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex0_u_1, arg->sw_rawawb_vertex0_v_1),
			 ISP_RAWAWB_UV_DETC_VERTEX0_1);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex1_u_1, arg->sw_rawawb_vertex1_v_1),
			 ISP_RAWAWB_UV_DETC_VERTEX1_1);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex2_u_1, arg->sw_rawawb_vertex2_v_1),
			 ISP_RAWAWB_UV_DETC_VERTEX2_1);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex3_u_1, arg->sw_rawawb_vertex3_v_1),
			 ISP_RAWAWB_UV_DETC_VERTEX3_1);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope01_1,
			 ISP_RAWAWB_UV_DETC_ISLOPE01_1);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope12_1,
			 ISP_RAWAWB_UV_DETC_ISLOPE12_1);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope23_1,
			 ISP_RAWAWB_UV_DETC_ISLOPE23_1);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope30_1,
			 ISP_RAWAWB_UV_DETC_ISLOPE30_1);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex0_u_2, arg->sw_rawawb_vertex0_v_2),
			 ISP_RAWAWB_UV_DETC_VERTEX0_2);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex1_u_2, arg->sw_rawawb_vertex1_v_2),
			 ISP_RAWAWB_UV_DETC_VERTEX1_2);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex2_u_2, arg->sw_rawawb_vertex2_v_2),
			 ISP_RAWAWB_UV_DETC_VERTEX2_2);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex3_u_2, arg->sw_rawawb_vertex3_v_2),
			 ISP_RAWAWB_UV_DETC_VERTEX3_2);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope01_2,
			 ISP_RAWAWB_UV_DETC_ISLOPE01_2);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope12_2,
			 ISP_RAWAWB_UV_DETC_ISLOPE12_2);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope23_2,
			 ISP_RAWAWB_UV_DETC_ISLOPE23_2);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope30_2,
			 ISP_RAWAWB_UV_DETC_ISLOPE30_2);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex0_u_3, arg->sw_rawawb_vertex0_v_3),
			 ISP_RAWAWB_UV_DETC_VERTEX0_3);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex1_u_3, arg->sw_rawawb_vertex1_v_3),
			 ISP_RAWAWB_UV_DETC_VERTEX1_3);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex2_u_3, arg->sw_rawawb_vertex2_v_3),
			 ISP_RAWAWB_UV_DETC_VERTEX2_3);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex3_u_3, arg->sw_rawawb_vertex3_v_3),
			 ISP_RAWAWB_UV_DETC_VERTEX3_3);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope01_3,
			 ISP_RAWAWB_UV_DETC_ISLOPE01_3);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope12_3,
			 ISP_RAWAWB_UV_DETC_ISLOPE12_3);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope23_3,
			 ISP_RAWAWB_UV_DETC_ISLOPE23_3);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope30_3,
			 ISP_RAWAWB_UV_DETC_ISLOPE30_3);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex0_u_4, arg->sw_rawawb_vertex0_v_4),
			 ISP_RAWAWB_UV_DETC_VERTEX0_4);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex1_u_4, arg->sw_rawawb_vertex1_v_4),
			 ISP_RAWAWB_UV_DETC_VERTEX1_4);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex2_u_4, arg->sw_rawawb_vertex2_v_4),
			 ISP_RAWAWB_UV_DETC_VERTEX2_4);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex3_u_4, arg->sw_rawawb_vertex3_v_4),
			 ISP_RAWAWB_UV_DETC_VERTEX3_4);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope01_4,
			 ISP_RAWAWB_UV_DETC_ISLOPE01_4);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope12_4,
			 ISP_RAWAWB_UV_DETC_ISLOPE12_4);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope23_4,
			 ISP_RAWAWB_UV_DETC_ISLOPE23_4);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope30_4,
			 ISP_RAWAWB_UV_DETC_ISLOPE30_4);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex0_u_5, arg->sw_rawawb_vertex0_v_5),
			 ISP_RAWAWB_UV_DETC_VERTEX0_5);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex1_u_5, arg->sw_rawawb_vertex1_v_5),
			 ISP_RAWAWB_UV_DETC_VERTEX1_5);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex2_u_5, arg->sw_rawawb_vertex2_v_5),
			 ISP_RAWAWB_UV_DETC_VERTEX2_5);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex3_u_5, arg->sw_rawawb_vertex3_v_5),
			 ISP_RAWAWB_UV_DETC_VERTEX3_5);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope01_5,
			 ISP_RAWAWB_UV_DETC_ISLOPE01_5);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope12_5,
			 ISP_RAWAWB_UV_DETC_ISLOPE12_5);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope23_5,
			 ISP_RAWAWB_UV_DETC_ISLOPE23_5);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope30_5,
			 ISP_RAWAWB_UV_DETC_ISLOPE30_5);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex0_u_6, arg->sw_rawawb_vertex0_v_6),
			 ISP_RAWAWB_UV_DETC_VERTEX0_6);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex1_u_6, arg->sw_rawawb_vertex1_v_6),
			 ISP_RAWAWB_UV_DETC_VERTEX1_6);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex2_u_6, arg->sw_rawawb_vertex2_v_6),
			 ISP_RAWAWB_UV_DETC_VERTEX2_6);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex3_u_6, arg->sw_rawawb_vertex3_v_6),
			 ISP_RAWAWB_UV_DETC_VERTEX3_6);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope01_6,
			 ISP_RAWAWB_UV_DETC_ISLOPE01_6);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope12_6,
			 ISP_RAWAWB_UV_DETC_ISLOPE12_6);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope23_6,
			 ISP_RAWAWB_UV_DETC_ISLOPE23_6);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_islope30_6,
			 ISP_RAWAWB_UV_DETC_ISLOPE30_6);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_b_uv_0,
			 ISP_RAWAWB_YUV_DETC_B_UV_0);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_slope_vtcuv_0,
			 ISP_RAWAWB_YUV_DETC_SLOPE_VTCUV_0);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_inv_dslope_0,
			 ISP_RAWAWB_YUV_DETC_INV_DSLOPE_0);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_slope_ydis_0,
			 ISP_RAWAWB_YUV_DETC_SLOPE_YDIS_0);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_b_ydis_0,
			 ISP_RAWAWB_YUV_DETC_B_YDIS_0);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_b_uv_1,
			 ISP_RAWAWB_YUV_DETC_B_UV_1);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_slope_vtcuv_1,
			 ISP_RAWAWB_YUV_DETC_SLOPE_VTCUV_1);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_inv_dslope_1,
			 ISP_RAWAWB_YUV_DETC_INV_DSLOPE_1);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_slope_ydis_1,
			 ISP_RAWAWB_YUV_DETC_SLOPE_YDIS_1);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_b_ydis_1,
			 ISP_RAWAWB_YUV_DETC_B_YDIS_1);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_b_uv_2,
			 ISP_RAWAWB_YUV_DETC_B_UV_2);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_slope_vtcuv_2,
			 ISP_RAWAWB_YUV_DETC_SLOPE_VTCUV_2);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_inv_dslope_2,
			 ISP_RAWAWB_YUV_DETC_INV_DSLOPE_2);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_slope_ydis_2,
			 ISP_RAWAWB_YUV_DETC_SLOPE_YDIS_2);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_b_ydis_2,
			 ISP_RAWAWB_YUV_DETC_B_YDIS_2);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_b_uv_3,
			 ISP_RAWAWB_YUV_DETC_B_UV_3);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_slope_vtcuv_3,
			 ISP_RAWAWB_YUV_DETC_SLOPE_VTCUV_3);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_inv_dslope_3,
			 ISP_RAWAWB_YUV_DETC_INV_DSLOPE_3);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_slope_ydis_3,
			 ISP_RAWAWB_YUV_DETC_SLOPE_YDIS_3);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_b_ydis_3,
			 ISP_RAWAWB_YUV_DETC_B_YDIS_3);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_ref_u,
			 ISP_RAWAWB_YUV_DETC_REF_U);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_4BYTE(arg->sw_rawawb_ref_v_0, arg->sw_rawawb_ref_v_1,
					  arg->sw_rawawb_ref_v_2, arg->sw_rawawb_ref_v_3),
			 ISP_RAWAWB_YUV_DETC_REF_V);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_dis0_0, arg->sw_rawawb_dis1_0),
			 ISP_RAWAWB_YUV_DETC_DIS01_0);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_dis2_0, arg->sw_rawawb_dis3_0),
			 ISP_RAWAWB_YUV_DETC_DIS23_0);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_dis4_0, arg->sw_rawawb_dis5_0),
			 ISP_RAWAWB_YUV_DETC_DIS45_0);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_4BYTE(arg->sw_rawawb_th0_0, arg->sw_rawawb_th1_0,
					  arg->sw_rawawb_th2_0, arg->sw_rawawb_th3_0),
			 ISP_RAWAWB_YUV_DETC_TH03_0);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_4BYTE(arg->sw_rawawb_th4_0, arg->sw_rawawb_th5_0,
					  0, 0),
			 ISP_RAWAWB_YUV_DETC_TH45_0);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_dis0_1, arg->sw_rawawb_dis1_1),
			 ISP_RAWAWB_YUV_DETC_DIS01_1);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_dis2_1, arg->sw_rawawb_dis3_1),
			 ISP_RAWAWB_YUV_DETC_DIS23_1);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_dis4_1, arg->sw_rawawb_dis5_1),
			 ISP_RAWAWB_YUV_DETC_DIS45_1);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_4BYTE(arg->sw_rawawb_th0_1, arg->sw_rawawb_th1_1,
					  arg->sw_rawawb_th2_1, arg->sw_rawawb_th3_1),
			 ISP_RAWAWB_YUV_DETC_TH03_1);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_4BYTE(arg->sw_rawawb_th4_1, arg->sw_rawawb_th5_1,
					  0, 0),
			 ISP_RAWAWB_YUV_DETC_TH45_1);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_dis0_2, arg->sw_rawawb_dis1_2),
			 ISP_RAWAWB_YUV_DETC_DIS01_2);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_dis2_2, arg->sw_rawawb_dis3_2),
			 ISP_RAWAWB_YUV_DETC_DIS23_2);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_dis4_2, arg->sw_rawawb_dis5_2),
			 ISP_RAWAWB_YUV_DETC_DIS45_2);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_4BYTE(arg->sw_rawawb_th0_2, arg->sw_rawawb_th1_2,
					  arg->sw_rawawb_th2_2, arg->sw_rawawb_th3_2),
			 ISP_RAWAWB_YUV_DETC_TH03_2);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_4BYTE(arg->sw_rawawb_th4_2, arg->sw_rawawb_th5_2,
					  0, 0),
			 ISP_RAWAWB_YUV_DETC_TH45_2);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_dis0_3, arg->sw_rawawb_dis1_3),
			 ISP_RAWAWB_YUV_DETC_DIS01_3);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_dis2_3, arg->sw_rawawb_dis3_3),
			 ISP_RAWAWB_YUV_DETC_DIS23_3);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_dis4_3, arg->sw_rawawb_dis5_3),
			 ISP_RAWAWB_YUV_DETC_DIS45_3);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_4BYTE(arg->sw_rawawb_th0_3, arg->sw_rawawb_th1_3,
					  arg->sw_rawawb_th2_3, arg->sw_rawawb_th3_3),
			 ISP_RAWAWB_YUV_DETC_TH03_3);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_4BYTE(arg->sw_rawawb_th4_3, arg->sw_rawawb_th5_3,
					  0, 0),
			 ISP_RAWAWB_YUV_DETC_TH45_3);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_wt0, arg->sw_rawawb_wt1),
			 ISP_RAWAWB_RGB2XY_WT01);

	rkisp_iowrite32(params_vdev,
			 arg->sw_rawawb_wt2,
			 ISP_RAWAWB_RGB2XY_WT2);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_mat0_x, arg->sw_rawawb_mat0_y),
			 ISP_RAWAWB_RGB2XY_MAT0_XY);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_mat1_x, arg->sw_rawawb_mat1_y),
			 ISP_RAWAWB_RGB2XY_MAT1_XY);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_mat2_x, arg->sw_rawawb_mat2_y),
			 ISP_RAWAWB_RGB2XY_MAT2_XY);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_x0_0, arg->sw_rawawb_nor_x1_0),
			 ISP_RAWAWB_XY_DETC_NOR_X_0);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_y0_0, arg->sw_rawawb_nor_y1_0),
			 ISP_RAWAWB_XY_DETC_NOR_Y_0);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_big_x0_0, arg->sw_rawawb_big_x1_0),
			 ISP_RAWAWB_XY_DETC_BIG_X_0);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_big_y0_0, arg->sw_rawawb_big_y1_0),
			 ISP_RAWAWB_XY_DETC_BIG_Y_0);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_sma_x0_0, arg->sw_rawawb_sma_x1_0),
			 ISP_RAWAWB_XY_DETC_SMA_X_0);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_sma_y0_0, arg->sw_rawawb_sma_y1_0),
			 ISP_RAWAWB_XY_DETC_SMA_Y_0);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_x0_1, arg->sw_rawawb_nor_x1_1),
			 ISP_RAWAWB_XY_DETC_NOR_X_1);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_y0_1, arg->sw_rawawb_nor_y1_1),
			 ISP_RAWAWB_XY_DETC_NOR_Y_1);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_big_x0_1, arg->sw_rawawb_big_x1_1),
			 ISP_RAWAWB_XY_DETC_BIG_X_1);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_big_y0_1, arg->sw_rawawb_big_y1_1),
			 ISP_RAWAWB_XY_DETC_BIG_Y_1);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_sma_x0_1, arg->sw_rawawb_sma_x1_1),
			 ISP_RAWAWB_XY_DETC_SMA_X_1);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_sma_y0_1, arg->sw_rawawb_sma_y1_1),
			 ISP_RAWAWB_XY_DETC_SMA_Y_1);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_x0_2, arg->sw_rawawb_nor_x1_2),
			 ISP_RAWAWB_XY_DETC_NOR_X_2);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_y0_2, arg->sw_rawawb_nor_y1_2),
			 ISP_RAWAWB_XY_DETC_NOR_Y_2);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_big_x0_2, arg->sw_rawawb_big_x1_2),
			 ISP_RAWAWB_XY_DETC_BIG_X_2);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_big_y0_2, arg->sw_rawawb_big_y1_2),
			 ISP_RAWAWB_XY_DETC_BIG_Y_2);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_sma_x0_2, arg->sw_rawawb_sma_x1_2),
			 ISP_RAWAWB_XY_DETC_SMA_X_2);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_sma_y0_2, arg->sw_rawawb_sma_y1_2),
			 ISP_RAWAWB_XY_DETC_SMA_Y_2);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_x0_3, arg->sw_rawawb_nor_x1_3),
			 ISP_RAWAWB_XY_DETC_NOR_X_3);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_y0_3, arg->sw_rawawb_nor_y1_3),
			 ISP_RAWAWB_XY_DETC_NOR_Y_3);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_big_x0_3, arg->sw_rawawb_big_x1_3),
			 ISP_RAWAWB_XY_DETC_BIG_X_3);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_big_y0_3, arg->sw_rawawb_big_y1_3),
			 ISP_RAWAWB_XY_DETC_BIG_Y_3);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_sma_x0_3, arg->sw_rawawb_sma_x1_3),
			 ISP_RAWAWB_XY_DETC_SMA_X_3);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_sma_y0_3, arg->sw_rawawb_sma_y1_3),
			 ISP_RAWAWB_XY_DETC_SMA_Y_3);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_x0_4, arg->sw_rawawb_nor_x1_4),
			 ISP_RAWAWB_XY_DETC_NOR_X_4);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_y0_4, arg->sw_rawawb_nor_y1_4),
			 ISP_RAWAWB_XY_DETC_NOR_Y_4);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_big_x0_4, arg->sw_rawawb_big_x1_4),
			 ISP_RAWAWB_XY_DETC_BIG_X_4);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_big_y0_4, arg->sw_rawawb_big_y1_4),
			 ISP_RAWAWB_XY_DETC_BIG_Y_4);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_sma_x0_4, arg->sw_rawawb_sma_x1_4),
			 ISP_RAWAWB_XY_DETC_SMA_X_4);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_sma_y0_4, arg->sw_rawawb_sma_y1_4),
			 ISP_RAWAWB_XY_DETC_SMA_Y_4);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_x0_5, arg->sw_rawawb_nor_x1_5),
			 ISP_RAWAWB_XY_DETC_NOR_X_5);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_y0_5, arg->sw_rawawb_nor_y1_5),
			 ISP_RAWAWB_XY_DETC_NOR_Y_5);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_big_x0_5, arg->sw_rawawb_big_x1_5),
			 ISP_RAWAWB_XY_DETC_BIG_X_5);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_big_y0_5, arg->sw_rawawb_big_y1_5),
			 ISP_RAWAWB_XY_DETC_BIG_Y_5);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_sma_x0_5, arg->sw_rawawb_sma_x1_5),
			 ISP_RAWAWB_XY_DETC_SMA_X_5);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_sma_y0_5, arg->sw_rawawb_sma_y1_5),
			 ISP_RAWAWB_XY_DETC_SMA_Y_5);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_x0_6, arg->sw_rawawb_nor_x1_6),
			 ISP_RAWAWB_XY_DETC_NOR_X_6);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_y0_6, arg->sw_rawawb_nor_y1_6),
			 ISP_RAWAWB_XY_DETC_NOR_Y_6);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_big_x0_6, arg->sw_rawawb_big_x1_6),
			 ISP_RAWAWB_XY_DETC_BIG_X_6);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_big_y0_6, arg->sw_rawawb_big_y1_6),
			 ISP_RAWAWB_XY_DETC_BIG_Y_6);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_sma_x0_6, arg->sw_rawawb_sma_x1_6),
			 ISP_RAWAWB_XY_DETC_SMA_X_6);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_sma_y0_6, arg->sw_rawawb_sma_y1_6),
			 ISP_RAWAWB_XY_DETC_SMA_Y_6);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_multiwindow0_h_offs, arg->sw_rawawb_multiwindow0_v_offs),
			 ISP_RAWAWB_MULTIWINDOW0_OFFS);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_multiwindow0_h_size, arg->sw_rawawb_multiwindow0_v_size),
			 ISP_RAWAWB_MULTIWINDOW0_SIZE);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_multiwindow1_h_offs, arg->sw_rawawb_multiwindow1_v_offs),
			 ISP_RAWAWB_MULTIWINDOW1_OFFS);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_multiwindow1_h_size, arg->sw_rawawb_multiwindow1_v_size),
			 ISP_RAWAWB_MULTIWINDOW1_SIZE);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_multiwindow2_h_offs, arg->sw_rawawb_multiwindow2_v_offs),
			 ISP_RAWAWB_MULTIWINDOW2_OFFS);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_multiwindow2_h_size, arg->sw_rawawb_multiwindow2_v_size),
			 ISP_RAWAWB_MULTIWINDOW2_SIZE);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_multiwindow3_h_offs, arg->sw_rawawb_multiwindow3_v_offs),
			 ISP_RAWAWB_MULTIWINDOW3_OFFS);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_multiwindow3_h_size, arg->sw_rawawb_multiwindow3_v_size),
			 ISP_RAWAWB_MULTIWINDOW3_SIZE);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_multiwindow4_h_offs, arg->sw_rawawb_multiwindow4_v_offs),
			 ISP_RAWAWB_MULTIWINDOW4_OFFS);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_multiwindow4_h_size, arg->sw_rawawb_multiwindow4_v_size),
			 ISP_RAWAWB_MULTIWINDOW4_SIZE);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_multiwindow5_h_offs, arg->sw_rawawb_multiwindow5_v_offs),
			 ISP_RAWAWB_MULTIWINDOW5_OFFS);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_multiwindow5_h_size, arg->sw_rawawb_multiwindow5_v_size),
			 ISP_RAWAWB_MULTIWINDOW5_SIZE);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_multiwindow6_h_offs, arg->sw_rawawb_multiwindow6_v_offs),
			 ISP_RAWAWB_MULTIWINDOW6_OFFS);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_multiwindow6_h_size, arg->sw_rawawb_multiwindow6_v_size),
			 ISP_RAWAWB_MULTIWINDOW6_SIZE);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_multiwindow7_h_offs, arg->sw_rawawb_multiwindow7_v_offs),
			 ISP_RAWAWB_MULTIWINDOW7_OFFS);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_multiwindow7_h_size, arg->sw_rawawb_multiwindow7_v_size),
			 ISP_RAWAWB_MULTIWINDOW7_SIZE);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region0_xu0, arg->sw_rawawb_exc_wp_region0_xu1),
			 ISP_RAWAWB_EXC_WP_REGION0_XU);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region0_yv0, arg->sw_rawawb_exc_wp_region0_yv1),
			 ISP_RAWAWB_EXC_WP_REGION0_YV);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region1_xu0, arg->sw_rawawb_exc_wp_region1_xu1),
			 ISP_RAWAWB_EXC_WP_REGION1_XU);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region1_yv0, arg->sw_rawawb_exc_wp_region1_yv1),
			 ISP_RAWAWB_EXC_WP_REGION1_YV);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region2_xu0, arg->sw_rawawb_exc_wp_region2_xu1),
			 ISP_RAWAWB_EXC_WP_REGION2_XU);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region2_yv0, arg->sw_rawawb_exc_wp_region2_yv1),
			 ISP_RAWAWB_EXC_WP_REGION2_YV);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region3_xu0, arg->sw_rawawb_exc_wp_region3_xu1),
			 ISP_RAWAWB_EXC_WP_REGION3_XU);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region3_yv0, arg->sw_rawawb_exc_wp_region3_yv1),
			 ISP_RAWAWB_EXC_WP_REGION3_YV);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region4_xu0, arg->sw_rawawb_exc_wp_region4_xu1),
			 ISP_RAWAWB_EXC_WP_REGION4_XU);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region4_yv0, arg->sw_rawawb_exc_wp_region4_yv1),
			 ISP_RAWAWB_EXC_WP_REGION4_YV);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region5_xu0, arg->sw_rawawb_exc_wp_region5_xu1),
			 ISP_RAWAWB_EXC_WP_REGION5_XU);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region5_yv0, arg->sw_rawawb_exc_wp_region5_yv1),
			 ISP_RAWAWB_EXC_WP_REGION5_YV);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region6_xu0, arg->sw_rawawb_exc_wp_region6_xu1),
			 ISP_RAWAWB_EXC_WP_REGION6_XU);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region6_yv0, arg->sw_rawawb_exc_wp_region6_yv1),
			 ISP_RAWAWB_EXC_WP_REGION6_YV);

	rkisp_iowrite32(params_vdev,
			 (arg->sw_rawawb_multiwindow_en & 0x1) << 31 |
			 (arg->sw_rawawb_exc_wp_region6_domain & 0x1) << 26 |
			 (arg->sw_rawawb_exc_wp_region6_measen & 0x1) << 25 |
			 (arg->sw_rawawb_exc_wp_region6_excen & 0x1) << 24 |
			 (arg->sw_rawawb_exc_wp_region5_domain & 0x1) << 22 |
			 (arg->sw_rawawb_exc_wp_region5_measen & 0x1) << 21 |
			 (arg->sw_rawawb_exc_wp_region5_excen & 0x1) << 20 |
			 (arg->sw_rawawb_exc_wp_region4_domain & 0x1) << 18 |
			 (arg->sw_rawawb_exc_wp_region4_measen & 0x1) << 17 |
			 (arg->sw_rawawb_exc_wp_region4_excen & 0x1) << 16 |
			 (arg->sw_rawawb_exc_wp_region3_domain & 0x1) << 14 |
			 (arg->sw_rawawb_exc_wp_region3_measen & 0x1) << 13 |
			 (arg->sw_rawawb_exc_wp_region3_excen & 0x1) << 12 |
			 (arg->sw_rawawb_exc_wp_region2_domain & 0x1) << 10 |
			 (arg->sw_rawawb_exc_wp_region2_measen & 0x1) << 9 |
			 (arg->sw_rawawb_exc_wp_region2_excen & 0x1) << 8 |
			 (arg->sw_rawawb_exc_wp_region1_domain & 0x1) << 6 |
			 (arg->sw_rawawb_exc_wp_region1_measen & 0x1) << 5 |
			 (arg->sw_rawawb_exc_wp_region1_excen & 0x1) << 4 |
			 (arg->sw_rawawb_exc_wp_region0_domain & 0x1) << 2 |
			 (arg->sw_rawawb_exc_wp_region0_measen & 0x1) << 1 |
			 (arg->sw_rawawb_exc_wp_region0_excen & 0x1) << 0,
			 ISP_RAWAWB_MULTIWINDOW_EXC_CTRL);

	rkisp_iowrite32(params_vdev,
			 (arg->sw_rawawb_store_wp_flag_ls_idx0 & 0x7) |
			 (arg->sw_rawawb_store_wp_flag_ls_idx1 & 0x7) << 3 |
			 (arg->sw_rawawb_store_wp_flag_ls_idx2 & 0x7) << 6 |
			 (arg->sw_rawawb_blk_measure_mode & 0x3) << 12,
			 ISP_RAWAWB_BLK_CTRL);

	/* avoid to override the old enable value */
	value = rkisp_ioread32(params_vdev, ISP_RAWAWB_CTRL);
	value &= ISP2X_RAWAWB_ENA;
	value &= ~ISP2X_REG_WR_MASK;
	rkisp_iowrite32(params_vdev,
			 value |
			 (arg->sw_rawawb_uv_en & 0x1) << 1 |
			 (arg->sw_rawawb_xy_en & 0x1) << 2 |
			 (arg->sw_rawawb_3dyuv_ls_idx0 & 0x7) << 4 |
			 (arg->sw_rawawb_3dyuv_ls_idx1 & 0x7) << 7 |
			 (arg->sw_rawawb_3dyuv_ls_idx2 & 0x7) << 10 |
			 (arg->sw_rawawb_3dyuv_ls_idx3 & 0x7) << 13 |
			 (arg->sw_rawawb_y_range & 0x1) << 16 |
			 (arg->sw_rawawb_c_range & 0x1) << 17 |
			 (arg->sw_rawawb_wind_size & 0x1) << 18 |
			 (arg->sw_rawawb_light_num & 0x7) << 20,
			 ISP_RAWAWB_CTRL);

	value = rkisp_ioread32(params_vdev, CTRL_VI_ISP_PATH);
	value &= ~(ISP2X_ISPPATH_RAWAWB_SEL_SET(3));
	value |= ISP2X_ISPPATH_RAWAWB_SEL_SET(arg->rawawb_sel);
	rkisp_iowrite32(params_vdev, value, CTRL_VI_ISP_PATH);
}

static void
isp_rawawb_enable(struct rkisp_isp_params_vdev *params_vdev,
		  bool en)
{
	u32 awb_ctrl;

	awb_ctrl = rkisp_ioread32(params_vdev, ISP_RAWAWB_CTRL);
	awb_ctrl &= ~ISP2X_REG_WR_MASK;
	if (en)
		awb_ctrl |= ISP2X_RAWAWB_ENA;
	else
		awb_ctrl &= ~ISP2X_RAWAWB_ENA;

	rkisp_iowrite32(params_vdev, awb_ctrl, ISP_RAWAWB_CTRL);
}

static void
isp_rawhstlite_config(struct rkisp_isp_params_vdev *params_vdev,
		      const struct isp2x_rawhistlite_cfg *arg)
{
	u32 i;
	u32 value;
	u32 hist_ctrl;
	u32 block_hsize, block_vsize;

	/* avoid to override the old enable value */
	hist_ctrl = rkisp_ioread32(params_vdev,
		ISP_RAWHIST_LITE_CTRL);
	hist_ctrl &= ISP2X_RAWHSTLITE_CTRL_EN_MASK;
	hist_ctrl &= ~ISP2X_REG_WR_MASK;
	hist_ctrl = hist_ctrl |
		    ISP2X_RAWHSTLITE_CTRL_MODE_SET(arg->mode) |
		    ISP2X_RAWHSTLITE_CTRL_DATASEL_SET(arg->data_sel) |
		    ISP2X_RAWHSTLITE_CTRL_WATERLINE_SET(arg->waterline) |
		    ISP2X_RAWHSTLITE_CTRL_STEPSIZE_SET(arg->stepsize);
	rkisp_iowrite32(params_vdev, hist_ctrl,
		ISP_RAWHIST_LITE_CTRL);

	rkisp_iowrite32(params_vdev,
			 ISP2X_RAWHSTLITE_OFFS_SET(arg->win.h_offs & 0xFFFE,
						   arg->win.v_offs & 0xFFFE),
			 ISP_RAWHIST_LITE_OFFS);

	block_hsize = arg->win.h_size / ISP2X_RAWHSTLITE_ROW_NUM - 1;
	block_vsize = arg->win.v_size / ISP2X_RAWHSTLITE_COLUMN_NUM - 1;
	block_hsize &= 0xFFFE;
	block_vsize &= 0xFFFE;
	rkisp_iowrite32(params_vdev,
			 ISP2X_RAWHSTLITE_SIZE_SET(block_hsize, block_vsize),
			 ISP_RAWHIST_LITE_SIZE);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_4BYTE(arg->rcc, arg->gcc, arg->bcc, arg->off),
			 ISP_RAWHIST_LITE_RAW2Y_CC);

	for (i = 0; i < (ISP2X_RAWHSTLITE_WEIGHT_REG_SIZE / 4); i++) {
		value = ISP2X_RAWHSTLITE_WEIGHT_SET(
				 arg->weight[4 * i + 0],
				 arg->weight[4 * i + 1],
				 arg->weight[4 * i + 2],
				 arg->weight[4 * i + 3]);
		rkisp_iowrite32(params_vdev, value,
				 ISP_RAWHIST_LITE_WEIGHT + 4 * i);
	}

	value = ISP2X_RAWHSTLITE_WEIGHT_SET(
				 arg->weight[4 * i + 0], 0, 0, 0);
	rkisp_iowrite32(params_vdev, value,
			 ISP_RAWHIST_LITE_WEIGHT + 4 * i);
}

static void
isp_rawhstlite_enable(struct rkisp_isp_params_vdev *params_vdev,
		      bool en)
{
	u32 hist_ctrl;

	hist_ctrl = rkisp_ioread32(params_vdev,
		ISP_RAWHIST_LITE_CTRL);
	hist_ctrl &= ~(ISP2X_RAWHSTLITE_CTRL_EN_MASK | ISP2X_REG_WR_MASK);

	if (en)
		hist_ctrl |= ISP2X_RAWHSTLITE_CTRL_EN_SET(0x1);

	rkisp_iowrite32(params_vdev, hist_ctrl,
		ISP_RAWHIST_LITE_CTRL);
}

static void
isp_rawhstbig_config(struct rkisp_isp_params_vdev *params_vdev,
		     const struct isp2x_rawhistbig_cfg *arg, u32 blk_no)
{
	u32 i, j;
	u32 value;
	u32 hist_ctrl;
	u32 block_hsize, block_vsize;
	u32 wnd_num_idx, hist_weight_num;
	u8 weight15x15[ISP2X_RAWHSTBIG_WEIGHT_REG_SIZE];
	const u32 hist_wnd_num[] = {
		5, 5, 15, 15
	};
	u32 addr;

	switch (blk_no) {
	case 0:
		addr = ISP_RAWHIST_BIG1_BASE;
		break;
	case 1:
		addr = ISP_RAWHIST_BIG2_BASE;
		break;
	case 2:
		addr = ISP_RAWHIST_BIG3_BASE;
		break;
	default:
		addr = ISP_RAWHIST_BIG1_BASE;
		break;
	}

	wnd_num_idx = arg->wnd_num;
	memset(weight15x15, 0x00, sizeof(weight15x15));
	/* avoid to override the old enable value */
	hist_ctrl = rkisp_ioread32(params_vdev, addr + ISP_RAWHIST_BIG_CTRL);
	hist_ctrl &= ISP2X_RAWHSTBIG_CTRL_EN_MASK;
	hist_ctrl &= ~ISP2X_REG_WR_MASK;
	hist_ctrl = hist_ctrl |
		    ISP2X_RAWHSTBIG_CTRL_MODE_SET(arg->mode) |
		    ISP2X_RAWHSTBIG_CTRL_DATASEL_SET(arg->data_sel) |
		    ISP2X_RAWHSTBIG_CTRL_WATERLINE_SET(arg->waterline) |
		    ISP2X_RAWHSTBIG_CTRL_WNDNUM_SET(arg->wnd_num) |
		    ISP2X_RAWHSTBIG_CTRL_STEPSIZE_SET(arg->stepsize);
	rkisp_iowrite32(params_vdev, hist_ctrl, addr + ISP_RAWHIST_BIG_CTRL);

	rkisp_iowrite32(params_vdev,
			 ISP2X_RAWHSTBIG_OFFS_SET(arg->win.h_offs & 0xFFFE,
						  arg->win.v_offs & 0xFFFE),
			 addr + ISP_RAWHIST_BIG_OFFS);

	block_hsize = arg->win.h_size / hist_wnd_num[wnd_num_idx] - 1;
	block_vsize = arg->win.v_size / hist_wnd_num[wnd_num_idx] - 1;
	block_hsize &= 0xFFFE;
	block_vsize &= 0xFFFE;
	rkisp_iowrite32(params_vdev,
			 ISP2X_RAWHSTBIG_SIZE_SET(block_hsize, block_vsize),
			 addr + ISP_RAWHIST_BIG_SIZE);

	rkisp_iowrite32(params_vdev,
			 ISP2X_PACK_4BYTE(arg->rcc, arg->gcc, arg->bcc, arg->off),
			 addr + ISP_RAWHIST_BIG_RAW2Y_CC);

	for (i = 0; i < hist_wnd_num[wnd_num_idx]; i++) {
		for (j = 0; j < hist_wnd_num[wnd_num_idx]; j++) {
			weight15x15[i * ISP2X_RAWHSTBIG_ROW_NUM + j] =
				arg->weight[i * hist_wnd_num[wnd_num_idx] + j];
		}
	}

	rkisp_iowrite32(params_vdev, ISP2X_RAWHSTBIG_WRAM_EN, ISP_RAWHIST_BIG_WRAM_CTRL);
	hist_weight_num = ISP2X_RAWHSTBIG_WEIGHT_REG_SIZE;
	for (i = 0; i < (hist_weight_num / 5); i++) {
		value = ISP2X_RAWHSTBIG_WEIGHT_SET(
				 weight15x15[5 * i + 0],
				 weight15x15[5 * i + 1],
				 weight15x15[5 * i + 2],
				 weight15x15[5 * i + 3],
				 weight15x15[5 * i + 4]);
		rkisp_iowrite32(params_vdev, value,
				 addr + ISP_RAWHIST_BIG_WEIGHT_BASE);
	}
}

static void
isp_rawhstbig_enable(struct rkisp_isp_params_vdev *params_vdev,
		     bool en, u32 blk_no)
{
	u32 hist_ctrl;
	u32 addr;

	switch (blk_no) {
	case 0:
		addr = ISP_RAWHIST_BIG1_BASE;
		break;
	case 1:
		addr = ISP_RAWHIST_BIG2_BASE;
		break;
	case 2:
		addr = ISP_RAWHIST_BIG3_BASE;
		break;
	default:
		addr = ISP_RAWHIST_BIG1_BASE;
		break;
	}

	hist_ctrl = rkisp_ioread32(params_vdev, addr + ISP_RAWHIST_BIG_CTRL);
	hist_ctrl &= ~(ISP2X_RAWHSTBIG_CTRL_EN_MASK | ISP2X_REG_WR_MASK);
	if (en)
		hist_ctrl |= ISP2X_RAWHSTBIG_CTRL_EN_SET(0x1);

	rkisp_iowrite32(params_vdev, hist_ctrl, addr + ISP_RAWHIST_BIG_CTRL);
}

static void
isp_rawhstbig1_config(struct rkisp_isp_params_vdev *params_vdev,
		      const struct isp2x_rawhistbig_cfg *arg)
{
	isp_rawhstbig_config(params_vdev, arg, 0);
}

static void
isp_rawhstbig1_enable(struct rkisp_isp_params_vdev *params_vdev,
		      bool en)
{
	isp_rawhstbig_enable(params_vdev, en, 0);
}

static void
isp_rawhstbig2_config(struct rkisp_isp_params_vdev *params_vdev,
		      const struct isp2x_rawhistbig_cfg *arg)
{
	isp_rawhstbig_config(params_vdev, arg, 1);
}

static void
isp_rawhstbig2_enable(struct rkisp_isp_params_vdev *params_vdev,
		      bool en)
{
	isp_rawhstbig_enable(params_vdev, en, 1);
}

static void
isp_rawhstbig3_config(struct rkisp_isp_params_vdev *params_vdev,
		      const struct isp2x_rawhistbig_cfg *arg)
{
	isp_rawhstbig_config(params_vdev, arg, 2);
}

static void
isp_rawhstbig3_enable(struct rkisp_isp_params_vdev *params_vdev,
		      bool en)
{
	isp_rawhstbig_enable(params_vdev, en, 2);
}

static void
isp_hdrmge_config(struct rkisp_isp_params_vdev *params_vdev,
		  const struct isp2x_hdrmge_cfg *arg)
{
}

static void
isp_hdrmge_enable(struct rkisp_isp_params_vdev *params_vdev,
		  bool en)
{
}

static void
isp_rawnr_config(struct rkisp_isp_params_vdev *params_vdev,
		 const struct isp2x_rawnr_cfg *arg)
{
}

static void
isp_rawnr_enable(struct rkisp_isp_params_vdev *params_vdev,
		 bool en)
{
}

static void
isp_hdrtmo_config(struct rkisp_isp_params_vdev *params_vdev,
		  const struct isp2x_hdrtmo_cfg *arg)
{
}

static void
isp_hdrtmo_enable(struct rkisp_isp_params_vdev *params_vdev,
		  bool en)
{
}

static void
isp_gic_config(struct rkisp_isp_params_vdev *params_vdev,
	       const struct isp2x_gic_cfg *arg)
{
}

static void
isp_gic_enable(struct rkisp_isp_params_vdev *params_vdev,
	       bool en)
{
}

static void
isp_dhaz_config(struct rkisp_isp_params_vdev *params_vdev,
		const struct isp2x_dhaz_cfg *arg)
{
}

static void
isp_dhaz_enable(struct rkisp_isp_params_vdev *params_vdev,
		bool en)
{
}

static void
isp_3dlut_config(struct rkisp_isp_params_vdev *params_vdev,
		 const struct isp2x_3dlut_cfg *arg)
{
}

static void
isp_3dlut_enable(struct rkisp_isp_params_vdev *params_vdev,
		 bool en)
{
}

struct rkisp_isp_params_v2x_ops rkisp_v2x_isp_params_ops = {
	.dpcc_config = isp_dpcc_config,
	.dpcc_enable = isp_dpcc_enable,
	.bls_config = isp_bls_config,
	.bls_enable = isp_bls_enable,
	.sdg_config = isp_sdg_config,
	.sdg_enable = isp_sdg_enable,
	.sihst_config = isp_sihst_config,
	.sihst_enable = isp_sihst_enable,
	.lsc_config = isp_lsc_config,
	.lsc_enable = isp_lsc_enable,
	.awbgain_config = isp_awbgain_config,
	.awbgain_enable = isp_awbgain_enable,
	.bdm_config = isp_bdm_config,
	.bdm_enable = isp_bdm_enable,
	.ctk_config = isp_ctk_config,
	.ctk_enable = isp_ctk_enable,
	.goc_config = isp_goc_config,
	.goc_enable = isp_goc_enable,
	.cproc_config = isp_cproc_config,
	.cproc_enable = isp_cproc_enable,
	.siaf_config = isp_siaf_config,
	.siaf_enable = isp_siaf_enable,
	.siawb_config = isp_siawb_config,
	.siawb_enable = isp_siawb_enable,
	.ie_config = isp_ie_config,
	.ie_enable = isp_ie_enable,
	.yuvae_config = isp_yuvae_config,
	.yuvae_enable = isp_yuvae_enable,
	.wdr_config = isp_wdr_config,
	.wdr_enable = isp_wdr_enable,
	.iesharp_config = isp_iesharp_config,
	.iesharp_enable = isp_iesharp_enable,
	.rawaf_config = isp_rawaf_config,
	.rawaf_enable = isp_rawaf_enable,
	.rawaelite_config = isp_rawaelite_config,
	.rawaelite_enable = isp_rawaelite_enable,
	.rawaebig1_config = isp_rawaebig1_config,
	.rawaebig1_enable = isp_rawaebig1_enable,
	.rawaebig2_config = isp_rawaebig2_config,
	.rawaebig2_enable = isp_rawaebig2_enable,
	.rawaebig3_config = isp_rawaebig3_config,
	.rawaebig3_enable = isp_rawaebig3_enable,
	.rawawb_config = isp_rawawb_config,
	.rawawb_enable = isp_rawawb_enable,
	.rawhstlite_config = isp_rawhstlite_config,
	.rawhstlite_enable = isp_rawhstlite_enable,
	.rawhstbig1_config = isp_rawhstbig1_config,
	.rawhstbig1_enable = isp_rawhstbig1_enable,
	.rawhstbig2_config = isp_rawhstbig2_config,
	.rawhstbig2_enable = isp_rawhstbig2_enable,
	.rawhstbig3_config = isp_rawhstbig3_config,
	.rawhstbig3_enable = isp_rawhstbig3_enable,
	.hdrmge_config = isp_hdrmge_config,
	.hdrmge_enable = isp_hdrmge_enable,
	.rawnr_config = isp_rawnr_config,
	.rawnr_enable = isp_rawnr_enable,
	.hdrtmo_config = isp_hdrtmo_config,
	.hdrtmo_enable = isp_hdrtmo_enable,
	.gic_config = isp_gic_config,
	.gic_enable = isp_gic_enable,
	.dhaz_config = isp_dhaz_config,
	.dhaz_enable = isp_dhaz_enable,
	.isp3dlut_config = isp_3dlut_config,
	.isp3dlut_enable = isp_3dlut_enable,
};

static __maybe_unused
void __isp_isr_other_config(struct rkisp_isp_params_vdev *params_vdev,
			    const struct isp2x_isp_params_cfg *new_params)
{
	u64 module_en_update, module_cfg_update, module_ens;
	struct rkisp_isp_params_v2x_ops *ops =
		(struct rkisp_isp_params_v2x_ops *)params_vdev->priv_ops;

	module_en_update = new_params->module_en_update;
	module_cfg_update = new_params->module_cfg_update;
	module_ens = new_params->module_ens;

	if ((module_en_update & ISP2X_MODULE_DPCC) ||
	    (module_cfg_update & ISP2X_MODULE_DPCC)) {
		if ((module_cfg_update & ISP2X_MODULE_DPCC))
			ops->dpcc_config(params_vdev,
				&new_params->others.dpcc_cfg);

		if (module_en_update & ISP2X_MODULE_DPCC)
			ops->dpcc_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_DPCC));
	}

	if ((module_en_update & ISP2X_MODULE_BLS) ||
	    (module_cfg_update & ISP2X_MODULE_BLS)) {
		if ((module_cfg_update & ISP2X_MODULE_BLS))
			ops->bls_config(params_vdev,
				&new_params->others.bls_cfg);

		if (module_en_update & ISP2X_MODULE_BLS)
			ops->bls_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_BLS));
	}

	if ((module_en_update & ISP2X_MODULE_SDG) ||
	    (module_cfg_update & ISP2X_MODULE_SDG)) {
		if ((module_cfg_update & ISP2X_MODULE_SDG))
			ops->sdg_config(params_vdev,
				&new_params->others.sdg_cfg);

		if (module_en_update & ISP2X_MODULE_SDG)
			ops->sdg_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_SDG));
	}

	if ((module_en_update & ISP2X_MODULE_LSC) ||
	    (module_cfg_update & ISP2X_MODULE_LSC)) {
		if ((module_cfg_update & ISP2X_MODULE_LSC))
			ops->lsc_config(params_vdev,
				&new_params->others.lsc_cfg);

		if (module_en_update & ISP2X_MODULE_LSC)
			ops->lsc_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_LSC));
	}

	if ((module_en_update & ISP2X_MODULE_AWB_GAIN) ||
	    (module_cfg_update & ISP2X_MODULE_AWB_GAIN)) {
		if ((module_cfg_update & ISP2X_MODULE_AWB_GAIN))
			ops->awbgain_config(params_vdev,
				&new_params->others.awb_gain_cfg);

		if (module_en_update & ISP2X_MODULE_AWB_GAIN)
			ops->awbgain_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_AWB_GAIN));
	}

	if ((module_en_update & ISP2X_MODULE_BDM) ||
	    (module_cfg_update & ISP2X_MODULE_BDM)) {
		if ((module_cfg_update & ISP2X_MODULE_BDM))
			ops->bdm_config(params_vdev,
				&new_params->others.bdm_cfg);

		if (module_en_update & ISP2X_MODULE_BDM)
			ops->bdm_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_BDM));
	}

	if ((module_en_update & ISP2X_MODULE_CTK) ||
	    (module_cfg_update & ISP2X_MODULE_CTK)) {
		if ((module_cfg_update & ISP2X_MODULE_CTK))
			ops->ctk_config(params_vdev,
				&new_params->others.ctk_cfg);

		if (module_en_update & ISP2X_MODULE_CTK)
			ops->ctk_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_CTK));
	}

	if ((module_en_update & ISP2X_MODULE_GOC) ||
	    (module_cfg_update & ISP2X_MODULE_GOC)) {
		if ((module_cfg_update & ISP2X_MODULE_GOC))
			ops->goc_config(params_vdev,
				&new_params->others.gammaout_cfg);

		if (module_en_update & ISP2X_MODULE_GOC)
			ops->goc_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_GOC));
	}

	if ((module_en_update & ISP2X_MODULE_CPROC) ||
	    (module_cfg_update & ISP2X_MODULE_CPROC)) {
		if ((module_cfg_update & ISP2X_MODULE_CPROC))
			ops->cproc_config(params_vdev,
				&new_params->others.cproc_cfg);

		if (module_en_update & ISP2X_MODULE_CPROC)
			ops->cproc_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_CPROC));
	}

	if ((module_en_update & ISP2X_MODULE_IE) ||
	    (module_cfg_update & ISP2X_MODULE_IE)) {
		if ((module_cfg_update & ISP2X_MODULE_IE))
			ops->ie_config(params_vdev,
				&new_params->others.ie_cfg);

		if (module_en_update & ISP2X_MODULE_IE)
			ops->ie_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_IE));
	}

	if ((module_en_update & ISP2X_MODULE_WDR) ||
	    (module_cfg_update & ISP2X_MODULE_WDR)) {
		if ((module_cfg_update & ISP2X_MODULE_WDR))
			ops->wdr_config(params_vdev,
				&new_params->others.wdr_cfg);

		if (module_en_update & ISP2X_MODULE_WDR)
			ops->wdr_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_WDR));
	}

	if ((module_en_update & ISP2X_MODULE_RK_IESHARP) ||
	    (module_cfg_update & ISP2X_MODULE_RK_IESHARP)) {
		if ((module_cfg_update & ISP2X_MODULE_RK_IESHARP))
			ops->iesharp_config(params_vdev,
				&new_params->others.rkiesharp_cfg);

		if (module_en_update & ISP2X_MODULE_RK_IESHARP)
			ops->iesharp_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_RK_IESHARP));
	}

	if ((module_en_update & ISP2X_MODULE_HDRMGE) ||
	    (module_cfg_update & ISP2X_MODULE_HDRMGE)) {
		if ((module_cfg_update & ISP2X_MODULE_HDRMGE))
			ops->hdrmge_config(params_vdev,
				&new_params->others.hdrmge_cfg);

		if (module_en_update & ISP2X_MODULE_HDRMGE)
			ops->hdrmge_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_HDRMGE));
	}

	if ((module_en_update & ISP2X_MODULE_RAWNR) ||
	    (module_cfg_update & ISP2X_MODULE_RAWNR)) {
		if ((module_cfg_update & ISP2X_MODULE_RAWNR))
			ops->rawnr_config(params_vdev,
				&new_params->others.rawnr_cfg);

		if (module_en_update & ISP2X_MODULE_RAWNR)
			ops->rawnr_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_RAWNR));
	}

	if ((module_en_update & ISP2X_MODULE_HDRTMO) ||
	    (module_cfg_update & ISP2X_MODULE_HDRTMO)) {
		if ((module_cfg_update & ISP2X_MODULE_HDRTMO))
			ops->hdrtmo_config(params_vdev,
				&new_params->others.hdrtmo_cfg);

		if (module_en_update & ISP2X_MODULE_HDRTMO)
			ops->hdrtmo_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_HDRTMO));
	}

	if ((module_en_update & ISP2X_MODULE_GIC) ||
	    (module_cfg_update & ISP2X_MODULE_GIC)) {
		if ((module_cfg_update & ISP2X_MODULE_GIC))
			ops->gic_config(params_vdev,
				&new_params->others.gic_cfg);

		if (module_en_update & ISP2X_MODULE_GIC)
			ops->gic_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_GIC));
	}

	if ((module_en_update & ISP2X_MODULE_DHAZ) ||
	    (module_cfg_update & ISP2X_MODULE_DHAZ)) {
		if ((module_cfg_update & ISP2X_MODULE_DHAZ))
			ops->dhaz_config(params_vdev,
				&new_params->others.dhaz_cfg);

		if (module_en_update & ISP2X_MODULE_DHAZ)
			ops->dhaz_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_DHAZ));
	}

	if ((module_en_update & ISP2X_MODULE_3DLUT) ||
	    (module_cfg_update & ISP2X_MODULE_3DLUT)) {
		if ((module_cfg_update & ISP2X_MODULE_3DLUT))
			ops->isp3dlut_config(params_vdev,
				&new_params->others.isp3dlut_cfg);

		if (module_en_update & ISP2X_MODULE_3DLUT)
			ops->isp3dlut_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_3DLUT));
	}
}

static __maybe_unused
void __isp_isr_meas_config(struct rkisp_isp_params_vdev *params_vdev,
			   struct isp2x_isp_params_cfg *new_params)
{
	u64 module_en_update, module_cfg_update, module_ens;
	struct rkisp_isp_params_v2x_ops *ops =
		(struct rkisp_isp_params_v2x_ops *)params_vdev->priv_ops;

	module_en_update = new_params->module_en_update;
	module_cfg_update = new_params->module_cfg_update;
	module_ens = new_params->module_ens;

	if ((module_en_update & ISP2X_MODULE_YUVAE) ||
	    (module_cfg_update & ISP2X_MODULE_YUVAE)) {
		if ((module_cfg_update & ISP2X_MODULE_YUVAE))
			ops->yuvae_config(params_vdev,
				&new_params->meas.yuvae);

		if (module_en_update & ISP2X_MODULE_YUVAE)
			ops->yuvae_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_YUVAE));
	}

	if ((module_en_update & ISP2X_MODULE_RAWAE_LITE) ||
	    (module_cfg_update & ISP2X_MODULE_RAWAE_LITE)) {
		if ((module_cfg_update & ISP2X_MODULE_RAWAE_LITE))
			ops->rawaelite_config(params_vdev,
				&new_params->meas.rawaelite);

		if (module_en_update & ISP2X_MODULE_RAWAE_LITE)
			ops->rawaelite_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_RAWAE_LITE));
	}

	if ((module_en_update & ISP2X_MODULE_RAWAE_BIG1) ||
	    (module_cfg_update & ISP2X_MODULE_RAWAE_BIG1)) {
		if ((module_cfg_update & ISP2X_MODULE_RAWAE_BIG1))
			ops->rawaebig1_config(params_vdev,
				&new_params->meas.rawaebig1);

		if (module_en_update & ISP2X_MODULE_RAWAE_BIG1)
			ops->rawaebig1_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_RAWAE_BIG1));
	}

	if ((module_en_update & ISP2X_MODULE_RAWAE_BIG2) ||
	    (module_cfg_update & ISP2X_MODULE_RAWAE_BIG2)) {
		if ((module_cfg_update & ISP2X_MODULE_RAWAE_BIG2))
			ops->rawaebig2_config(params_vdev,
				&new_params->meas.rawaebig2);

		if (module_en_update & ISP2X_MODULE_RAWAE_BIG2)
			ops->rawaebig2_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_RAWAE_BIG2));
	}

	if ((module_en_update & ISP2X_MODULE_RAWAE_BIG3) ||
	    (module_cfg_update & ISP2X_MODULE_RAWAE_BIG3)) {
		if ((module_cfg_update & ISP2X_MODULE_RAWAE_BIG3))
			ops->rawaebig3_config(params_vdev,
				&new_params->meas.rawaebig3);

		if (module_en_update & ISP2X_MODULE_RAWAE_BIG3)
			ops->rawaebig3_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_RAWAE_BIG3));
	}

	if ((module_en_update & ISP2X_MODULE_SIHST) ||
	    (module_cfg_update & ISP2X_MODULE_SIHST)) {
		if ((module_cfg_update & ISP2X_MODULE_SIHST))
			ops->sihst_config(params_vdev,
				&new_params->meas.sihst);

		if (module_en_update & ISP2X_MODULE_SIHST)
			ops->sihst_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_SIHST));
	}

	if ((module_en_update & ISP2X_MODULE_RAWHIST_LITE) ||
	    (module_cfg_update & ISP2X_MODULE_RAWHIST_LITE)) {
		if ((module_cfg_update & ISP2X_MODULE_RAWHIST_LITE))
			ops->rawhstlite_config(params_vdev,
				&new_params->meas.rawhstlite);

		if (module_en_update & ISP2X_MODULE_RAWHIST_LITE)
			ops->rawhstlite_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_RAWHIST_LITE));
	}

	if ((module_en_update & ISP2X_MODULE_RAWHIST_BIG1) ||
	    (module_cfg_update & ISP2X_MODULE_RAWHIST_BIG1)) {
		if ((module_cfg_update & ISP2X_MODULE_RAWHIST_BIG1))
			ops->rawhstbig1_config(params_vdev,
				&new_params->meas.rawhstbig1);

		if (module_en_update & ISP2X_MODULE_RAWHIST_BIG1)
			ops->rawhstbig1_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_RAWHIST_BIG1));
	}

	if ((module_en_update & ISP2X_MODULE_RAWHIST_BIG2) ||
	    (module_cfg_update & ISP2X_MODULE_RAWHIST_BIG2)) {
		if ((module_cfg_update & ISP2X_MODULE_RAWHIST_BIG2))
			ops->rawhstbig2_config(params_vdev,
				&new_params->meas.rawhstbig2);

		if (module_en_update & ISP2X_MODULE_RAWHIST_BIG2)
			ops->rawhstbig2_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_RAWHIST_BIG2));
	}

	if ((module_en_update & ISP2X_MODULE_RAWHIST_BIG3) ||
	    (module_cfg_update & ISP2X_MODULE_RAWHIST_BIG3)) {
		if ((module_cfg_update & ISP2X_MODULE_RAWHIST_BIG3))
			ops->rawhstbig3_config(params_vdev,
				&new_params->meas.rawhstbig3);

		if (module_en_update & ISP2X_MODULE_RAWHIST_BIG3)
			ops->rawhstbig3_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_RAWHIST_BIG3));
	}

	if ((module_en_update & ISP2X_MODULE_SIAWB) ||
	    (module_cfg_update & ISP2X_MODULE_SIAWB)) {
		if ((module_cfg_update & ISP2X_MODULE_SIAWB))
			ops->siawb_config(params_vdev,
				&new_params->meas.siawb);

		if (module_en_update & ISP2X_MODULE_SIAWB)
			ops->siawb_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_SIAWB));
	}

	if ((module_en_update & ISP2X_MODULE_RAWAWB) ||
	    (module_cfg_update & ISP2X_MODULE_RAWAWB)) {
		if ((module_cfg_update & ISP2X_MODULE_RAWAWB))
			ops->rawawb_config(params_vdev,
				&new_params->meas.rawawb);

		if (module_en_update & ISP2X_MODULE_RAWAWB)
			ops->rawawb_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_RAWAWB));
	}

	if ((module_en_update & ISP2X_MODULE_SIAF) ||
	    (module_cfg_update & ISP2X_MODULE_SIAF)) {
		if ((module_cfg_update & ISP2X_MODULE_SIAF))
			ops->siaf_config(params_vdev,
				&new_params->meas.siaf);

		if (module_en_update & ISP2X_MODULE_SIAF)
			ops->siaf_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_SIAF));
	}

	if ((module_en_update & ISP2X_MODULE_RAWAF) ||
	    (module_cfg_update & ISP2X_MODULE_RAWAF)) {
		if ((module_cfg_update & ISP2X_MODULE_RAWAF))
			ops->rawaf_config(params_vdev,
				&new_params->meas.rawaf);

		if (module_en_update & ISP2X_MODULE_RAWAF)
			ops->rawaf_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_RAWAF));
	}
}

static __maybe_unused
void __preisp_isr_update_hdrae_para(struct rkisp_isp_params_vdev *params_vdev,
				    struct isp2x_isp_params_cfg *new_params)
{
}

/* Not called when the camera active, thus not isr protection. */
static void
rkisp_params_configure_isp_v2x(struct rkisp_isp_params_vdev *params_vdev)
{
	struct device *dev = params_vdev->dev->dev;

	spin_lock(&params_vdev->config_lock);
	/* override the default things */
	if (!params_vdev->isp2x_params.module_cfg_update &&
	    !params_vdev->isp2x_params.module_en_update)
		dev_warn(dev, "can not get first iq setting in stream on\n");

	__isp_isr_other_config(params_vdev, &params_vdev->isp2x_params);
	__isp_isr_meas_config(params_vdev, &params_vdev->isp2x_params);
	__preisp_isr_update_hdrae_para(params_vdev, &params_vdev->isp2x_params);
	spin_unlock(&params_vdev->config_lock);
}

static void rkisp1_save_first_param_v2x(struct rkisp_isp_params_vdev *params_vdev, void *param)
{
	struct isp2x_isp_params_cfg *new_params;

	new_params = (struct isp2x_isp_params_cfg *)param;
	params_vdev->isp2x_params = *new_params;
}

static void rkisp1_clear_first_param_v2x(struct rkisp_isp_params_vdev *params_vdev)
{
	params_vdev->isp2x_params.module_cfg_update = 0;
	params_vdev->isp2x_params.module_en_update = 0;
}

/* Not called when the camera active, thus not isr protection. */
static void
rkisp_params_disable_isp_v2x(struct rkisp_isp_params_vdev *params_vdev)
{
	struct rkisp_isp_params_v2x_ops *ops =
		(struct rkisp_isp_params_v2x_ops *)params_vdev->priv_ops;

	ops->dpcc_enable(params_vdev, false);
	ops->bls_enable(params_vdev, false);
	ops->sdg_enable(params_vdev, false);
	ops->sihst_enable(params_vdev, false);
	ops->lsc_enable(params_vdev, false);
	ops->awbgain_enable(params_vdev, false);
	ops->bdm_enable(params_vdev, false);
	ops->ctk_enable(params_vdev, false);
	ops->goc_enable(params_vdev, false);
	ops->cproc_enable(params_vdev, false);
	ops->siaf_enable(params_vdev, false);
	ops->siawb_enable(params_vdev, false);
	ops->ie_enable(params_vdev, false);
	ops->yuvae_enable(params_vdev, false);
	ops->wdr_enable(params_vdev, false);
	ops->iesharp_enable(params_vdev, false);
	ops->rawaf_enable(params_vdev, false);
	ops->rawaelite_enable(params_vdev, false);
	ops->rawaebig1_enable(params_vdev, false);
	ops->rawaebig2_enable(params_vdev, false);
	ops->rawaebig3_enable(params_vdev, false);
	ops->rawawb_enable(params_vdev, false);
	ops->rawhstlite_enable(params_vdev, false);
	ops->rawhstbig1_enable(params_vdev, false);
	ops->rawhstbig2_enable(params_vdev, false);
	ops->rawhstbig3_enable(params_vdev, false);
	ops->hdrmge_enable(params_vdev, false);
	ops->rawnr_enable(params_vdev, false);
	ops->hdrtmo_enable(params_vdev, false);
	ops->gic_enable(params_vdev, false);
	ops->dhaz_enable(params_vdev, false);
	ops->isp3dlut_enable(params_vdev, false);
}

static void
rkisp_params_isr_v2x(struct rkisp_isp_params_vdev *params_vdev,
		     u32 isp_mis)
{
	struct isp2x_isp_params_cfg *new_params;
	struct rkisp_buffer *cur_buf = NULL;
	unsigned int cur_frame_id =
		atomic_read(&params_vdev->dev->isp_sdev.frm_sync_seq) - 1;

	spin_lock(&params_vdev->config_lock);
	if (!params_vdev->streamon)
		goto unlock;

	/* get one empty buffer */
	if (!list_empty(&params_vdev->params))
		cur_buf = list_first_entry(&params_vdev->params,
					   struct rkisp_buffer, queue);
	if (!cur_buf)
		goto unlock;

	new_params = (struct isp2x_isp_params_cfg *)(cur_buf->vaddr[0]);
	if (isp_mis & CIF_ISP_FRAME) {
		u32 isp_ctrl;

		list_del(&cur_buf->queue);

		__isp_isr_other_config(params_vdev, new_params);
		__isp_isr_meas_config(params_vdev, new_params);

		/* update shadow register immediately */
		isp_ctrl = rkisp_ioread32(params_vdev, CIF_ISP_CTRL);
		isp_ctrl |= CIF_ISP_CTRL_ISP_CFG_UPD;
		rkisp_iowrite32(params_vdev, isp_ctrl, CIF_ISP_CTRL);

		cur_buf->vb.sequence = cur_frame_id;
		vb2_buffer_done(&cur_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}

unlock:
	spin_unlock(&params_vdev->config_lock);
}

static struct rkisp_isp_params_ops rkisp_isp_params_ops_tbl = {
	.save_first_param = rkisp1_save_first_param_v2x,
	.clear_first_param = rkisp1_clear_first_param_v2x,
	.config_isp = rkisp_params_configure_isp_v2x,
	.disable_isp = rkisp_params_disable_isp_v2x,
	.isr_hdl = rkisp_params_isr_v2x,
};

void rkisp_init_params_vdev_v2x(struct rkisp_isp_params_vdev *params_vdev)
{
	params_vdev->ops = &rkisp_isp_params_ops_tbl;
	params_vdev->priv_ops = &rkisp_v2x_isp_params_ops;
}


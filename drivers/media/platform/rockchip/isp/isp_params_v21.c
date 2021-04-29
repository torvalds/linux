// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Rockchip Electronics Co., Ltd. */

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>	/* for ISP params */
#include <linux/rk-preisp.h>
#include "dev.h"
#include "regs.h"
#include "regs_v2x.h"
#include "isp_params_v21.h"

#define ISP2X_PACK_4BYTE(a, b, c, d)	\
	(((a) & 0xFF) << 0 | ((b) & 0xFF) << 8 | \
	 ((c) & 0xFF) << 16 | ((d) & 0xFF) << 24)

#define ISP2X_PACK_2SHORT(a, b)	\
	(((a) & 0xFFFF) << 0 | ((b) & 0xFFFF) << 16)

#define ISP2X_REG_WR_MASK		BIT(31) //disable write protect
#define ISP2X_NOBIG_OVERFLOW_SIZE	(2688 * 1536)
#define ISP2X_AUTO_BIGMODE_WIDTH	2688

static inline void
rkisp_iowrite32(struct rkisp_isp_params_vdev *params_vdev,
		u32 value, u32 addr)
{
	rkisp_write(params_vdev->dev, addr, value, false);
}

static inline u32
rkisp_ioread32(struct rkisp_isp_params_vdev *params_vdev,
	       u32 addr)
{
	return rkisp_read(params_vdev->dev, addr, false);
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

static inline size_t
isp_param_get_insize(struct rkisp_isp_params_vdev *params_vdev)
{
	struct rkisp_device *dev = params_vdev->dev;
	struct rkisp_isp_subdev *isp_sdev = &dev->isp_sdev;

	return isp_sdev->in_crop.width * isp_sdev->in_crop.height;
}

static void
isp_dpcc_config(struct rkisp_isp_params_vdev *params_vdev,
		const struct isp2x_dpcc_cfg *arg)
{
	u32 value;
	int i;

	value = rkisp_ioread32(params_vdev, ISP_DPCC0_MODE);
	value &= ISP_DPCC_EN;

	value |= (arg->stage1_enable & 0x01) << 2 |
		 (arg->grayscale_mode & 0x01) << 1;
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_MODE);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_MODE);

	value = (arg->sw_rk_out_sel & 0x03) << 5 |
		(arg->sw_dpcc_output_sel & 0x01) << 4 |
		(arg->stage1_rb_3x3 & 0x01) << 3 |
		(arg->stage1_g_3x3 & 0x01) << 2 |
		(arg->stage1_incl_rb_center & 0x01) << 1 |
		(arg->stage1_incl_green_center & 0x01);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_OUTPUT_MODE);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_OUTPUT_MODE);

	value = (arg->stage1_use_fix_set & 0x01) << 3 |
		(arg->stage1_use_set_3 & 0x01) << 2 |
		(arg->stage1_use_set_2 & 0x01) << 1 |
		(arg->stage1_use_set_1 & 0x01);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_SET_USE);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_SET_USE);

	value = (arg->sw_rk_red_blue1_en & 0x01) << 13 |
		(arg->rg_red_blue1_enable & 0x01) << 12 |
		(arg->rnd_red_blue1_enable & 0x01) << 11 |
		(arg->ro_red_blue1_enable & 0x01) << 10 |
		(arg->lc_red_blue1_enable & 0x01) << 9 |
		(arg->pg_red_blue1_enable & 0x01) << 8 |
		(arg->sw_rk_green1_en & 0x01) << 5 |
		(arg->rg_green1_enable & 0x01) << 4 |
		(arg->rnd_green1_enable & 0x01) << 3 |
		(arg->ro_green1_enable & 0x01) << 2 |
		(arg->lc_green1_enable & 0x01) << 1 |
		(arg->pg_green1_enable & 0x01);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_METHODS_SET_1);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_METHODS_SET_1);

	value = (arg->sw_rk_red_blue2_en & 0x01) << 13 |
		(arg->rg_red_blue2_enable & 0x01) << 12 |
		(arg->rnd_red_blue2_enable & 0x01) << 11 |
		(arg->ro_red_blue2_enable & 0x01) << 10 |
		(arg->lc_red_blue2_enable & 0x01) << 9 |
		(arg->pg_red_blue2_enable & 0x01) << 8 |
		(arg->sw_rk_green2_en & 0x01) << 5 |
		(arg->rg_green2_enable & 0x01) << 4 |
		(arg->rnd_green2_enable & 0x01) << 3 |
		(arg->ro_green2_enable & 0x01) << 2 |
		(arg->lc_green2_enable & 0x01) << 1 |
		(arg->pg_green2_enable & 0x01);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_METHODS_SET_2);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_METHODS_SET_2);

	value = (arg->sw_rk_red_blue3_en & 0x01) << 13 |
		(arg->rg_red_blue3_enable & 0x01) << 12 |
		(arg->rnd_red_blue3_enable & 0x01) << 11 |
		(arg->ro_red_blue3_enable & 0x01) << 10 |
		(arg->lc_red_blue3_enable & 0x01) << 9 |
		(arg->pg_red_blue3_enable & 0x01) << 8 |
		(arg->sw_rk_green3_en & 0x01) << 5 |
		(arg->rg_green3_enable & 0x01) << 4 |
		(arg->rnd_green3_enable & 0x01) << 3 |
		(arg->ro_green3_enable & 0x01) << 2 |
		(arg->lc_green3_enable & 0x01) << 1 |
		(arg->pg_green3_enable & 0x01);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_METHODS_SET_3);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_METHODS_SET_3);

	value = ISP2X_PACK_4BYTE(arg->line_thr_1_g, arg->line_thr_1_rb,
				 arg->sw_mindis1_g, arg->sw_mindis1_rb);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_LINE_THRESH_1);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_LINE_THRESH_1);

	value = ISP2X_PACK_4BYTE(arg->line_mad_fac_1_g, arg->line_mad_fac_1_rb,
				 arg->sw_dis_scale_max1, arg->sw_dis_scale_min1);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_LINE_MAD_FAC_1);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_LINE_MAD_FAC_1);

	value = ISP2X_PACK_4BYTE(arg->pg_fac_1_g, arg->pg_fac_1_rb, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_PG_FAC_1);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_PG_FAC_1);

	value = ISP2X_PACK_4BYTE(arg->rnd_thr_1_g, arg->rnd_thr_1_rb, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_RND_THRESH_1);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_RND_THRESH_1);

	value = ISP2X_PACK_4BYTE(arg->rg_fac_1_g, arg->rg_fac_1_rb, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_RG_FAC_1);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_RG_FAC_1);

	value = ISP2X_PACK_4BYTE(arg->line_thr_2_g, arg->line_thr_2_rb,
				 arg->sw_mindis2_g, arg->sw_mindis2_rb);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_LINE_THRESH_2);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_LINE_THRESH_2);

	value = ISP2X_PACK_4BYTE(arg->line_mad_fac_2_g, arg->line_mad_fac_2_rb,
				 arg->sw_dis_scale_max2, arg->sw_dis_scale_min2);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_LINE_MAD_FAC_2);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_LINE_MAD_FAC_2);

	value = ISP2X_PACK_4BYTE(arg->pg_fac_2_g, arg->pg_fac_2_rb, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_PG_FAC_2);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_PG_FAC_2);

	value = ISP2X_PACK_4BYTE(arg->rnd_thr_2_g, arg->rnd_thr_2_rb, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_RND_THRESH_2);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_RND_THRESH_2);

	value = ISP2X_PACK_4BYTE(arg->rg_fac_2_g, arg->rg_fac_2_rb, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_RG_FAC_2);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_RG_FAC_2);

	value = ISP2X_PACK_4BYTE(arg->line_thr_3_g, arg->line_thr_3_rb,
				 arg->sw_mindis3_g, arg->sw_mindis3_rb);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_LINE_THRESH_3);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_LINE_THRESH_3);

	value = ISP2X_PACK_4BYTE(arg->line_mad_fac_3_g, arg->line_mad_fac_3_rb,
				 arg->sw_dis_scale_max3, arg->sw_dis_scale_min3);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_LINE_MAD_FAC_3);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_LINE_MAD_FAC_3);

	value = ISP2X_PACK_4BYTE(arg->pg_fac_3_g, arg->pg_fac_3_rb, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_PG_FAC_3);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_PG_FAC_3);

	value = ISP2X_PACK_4BYTE(arg->rnd_thr_3_g, arg->rnd_thr_3_rb, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_RND_THRESH_3);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_RND_THRESH_3);

	value = ISP2X_PACK_4BYTE(arg->rg_fac_3_g, arg->rg_fac_3_rb, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_RG_FAC_3);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_RG_FAC_3);

	value = (arg->ro_lim_3_rb & 0x03) << 10 |
		(arg->ro_lim_3_g & 0x03) << 8 |
		(arg->ro_lim_2_rb & 0x03) << 6 |
		(arg->ro_lim_2_g & 0x03) << 4 |
		(arg->ro_lim_1_rb & 0x03) << 2 |
		(arg->ro_lim_1_g & 0x03);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_RO_LIMITS);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_RO_LIMITS);

	value = (arg->rnd_offs_3_rb & 0x03) << 10 |
		(arg->rnd_offs_3_g & 0x03) << 8 |
		(arg->rnd_offs_2_rb & 0x03) << 6 |
		(arg->rnd_offs_2_g & 0x03) << 4 |
		(arg->rnd_offs_1_rb & 0x03) << 2 |
		(arg->rnd_offs_1_g & 0x03);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_RND_OFFS);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_RND_OFFS);

	value = (arg->bpt_rb_3x3 & 0x01) << 11 |
		(arg->bpt_g_3x3 & 0x01) << 10 |
		(arg->bpt_incl_rb_center & 0x01) << 9 |
		(arg->bpt_incl_green_center & 0x01) << 8 |
		(arg->bpt_use_fix_set & 0x01) << 7 |
		(arg->bpt_use_set_3 & 0x01) << 6 |
		(arg->bpt_use_set_2 & 0x01) << 5 |
		(arg->bpt_use_set_1 & 0x01) << 4 |
		(arg->bpt_cor_en & 0x01) << 1 |
		(arg->bpt_det_en & 0x01);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_BPT_CTRL);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_BPT_CTRL);

	rkisp_iowrite32(params_vdev, arg->bp_number, ISP_DPCC0_BPT_NUMBER);
	rkisp_iowrite32(params_vdev, arg->bp_number, ISP_DPCC1_BPT_NUMBER);
	rkisp_iowrite32(params_vdev, arg->bp_table_addr, ISP_DPCC0_BPT_ADDR);
	rkisp_iowrite32(params_vdev, arg->bp_table_addr, ISP_DPCC1_BPT_ADDR);

	value = ISP2X_PACK_2SHORT(arg->bpt_h_addr, arg->bpt_v_addr);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_BPT_DATA);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_BPT_DATA);

	rkisp_iowrite32(params_vdev, arg->bp_cnt, ISP_DPCC0_BP_CNT);
	rkisp_iowrite32(params_vdev, arg->bp_cnt, ISP_DPCC1_BP_CNT);

	rkisp_iowrite32(params_vdev, arg->sw_pdaf_en, ISP_DPCC0_PDAF_EN);
	rkisp_iowrite32(params_vdev, arg->sw_pdaf_en, ISP_DPCC1_PDAF_EN);

	value = 0;
	for (i = 0; i < ISP2X_DPCC_PDAF_POINT_NUM; i++)
		value |= (arg->pdaf_point_en[i] & 0x01) << i;
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_PDAF_POINT_EN);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_PDAF_POINT_EN);

	value = ISP2X_PACK_2SHORT(arg->pdaf_offsetx, arg->pdaf_offsety);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_PDAF_OFFSET);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_PDAF_OFFSET);

	value = ISP2X_PACK_2SHORT(arg->pdaf_wrapx, arg->pdaf_wrapy);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_PDAF_WRAP);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_PDAF_WRAP);

	value = ISP2X_PACK_2SHORT(arg->pdaf_wrapx_num, arg->pdaf_wrapy_num);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_PDAF_SCOPE);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_PDAF_SCOPE);

	for (i = 0; i < ISP2X_DPCC_PDAF_POINT_NUM / 2; i++) {
		value = ISP2X_PACK_4BYTE(arg->point[2 * i].x, arg->point[2 * i].y,
					 arg->point[2 * i + 1].x, arg->point[2 * i + 1].y);
		rkisp_iowrite32(params_vdev, value, ISP_DPCC0_PDAF_POINT_0 + 4 * i);
		rkisp_iowrite32(params_vdev, value, ISP_DPCC1_PDAF_POINT_0 + 4 * i);
	}

	rkisp_iowrite32(params_vdev, arg->pdaf_forward_med, ISP_DPCC0_BPT_ADDR);
	rkisp_iowrite32(params_vdev, arg->pdaf_forward_med, ISP_DPCC1_BPT_ADDR);
}

static void
isp_dpcc_enable(struct rkisp_isp_params_vdev *params_vdev,
		bool en)
{
	u32 value;

	value = rkisp_ioread32(params_vdev, ISP_DPCC0_MODE);
	value &= ~ISP_DPCC_EN;

	if (en)
		value |= ISP_DPCC_EN;
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_MODE);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_MODE);
}

static void
isp_bls_config(struct rkisp_isp_params_vdev *params_vdev,
	       const struct isp21_bls_cfg *arg)
{
	const struct isp2x_bls_fixed_val *pval;
	u32 new_control, value;

	new_control = rkisp_ioread32(params_vdev, ISP_BLS_CTRL);
	new_control &= ISP_BLS_ENA;

	pval = &arg->bls1_val;
	if (arg->bls1_en) {
		new_control |= ISP_BLS_BLS1_EN;

		switch (params_vdev->raw_type) {
		case RAW_BGGR:
			rkisp_iowrite32(params_vdev,
					pval->r, ISP_BLS1_D_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->gr, ISP_BLS1_C_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->gb, ISP_BLS1_B_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->b, ISP_BLS1_A_FIXED);
			break;
		case RAW_GBRG:
			rkisp_iowrite32(params_vdev,
					pval->r, ISP_BLS1_C_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->gr, ISP_BLS1_D_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->gb, ISP_BLS1_A_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->b, ISP_BLS1_B_FIXED);
			break;
		case RAW_GRBG:
			rkisp_iowrite32(params_vdev,
					pval->r, ISP_BLS1_B_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->gr, ISP_BLS1_A_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->gb, ISP_BLS1_D_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->b, ISP_BLS1_C_FIXED);
			break;
		case RAW_RGGB:
			rkisp_iowrite32(params_vdev,
					pval->r, ISP_BLS1_A_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->gr, ISP_BLS1_B_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->gb, ISP_BLS1_C_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->b, ISP_BLS1_D_FIXED);
			break;
		default:
			break;
		}
	}

	/* fixed subtraction values */
	pval = &arg->fixed_val;
	if (!arg->enable_auto) {
		switch (params_vdev->raw_type) {
		case RAW_BGGR:
			rkisp_iowrite32(params_vdev,
					pval->r, ISP_BLS_D_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->gr, ISP_BLS_C_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->gb, ISP_BLS_B_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->b, ISP_BLS_A_FIXED);
			break;
		case RAW_GBRG:
			rkisp_iowrite32(params_vdev,
					pval->r, ISP_BLS_C_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->gr, ISP_BLS_D_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->gb, ISP_BLS_A_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->b, ISP_BLS_B_FIXED);
			break;
		case RAW_GRBG:
			rkisp_iowrite32(params_vdev,
					pval->r, ISP_BLS_B_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->gr, ISP_BLS_A_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->gb, ISP_BLS_D_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->b, ISP_BLS_C_FIXED);
			break;
		case RAW_RGGB:
			rkisp_iowrite32(params_vdev,
					pval->r, ISP_BLS_A_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->gr, ISP_BLS_B_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->gb, ISP_BLS_C_FIXED);
			rkisp_iowrite32(params_vdev,
					pval->b, ISP_BLS_D_FIXED);
			break;
		default:
			break;
		}
	} else {
		if (arg->en_windows & BIT(1)) {
			rkisp_iowrite32(params_vdev, arg->bls_window2.h_offs,
					ISP_BLS_H2_START);
			value = arg->bls_window2.h_offs + arg->bls_window2.h_size;
			rkisp_iowrite32(params_vdev, value, ISP_BLS_H2_STOP);
			rkisp_iowrite32(params_vdev, arg->bls_window2.v_offs,
					ISP_BLS_V2_START);
			value = arg->bls_window2.v_offs + arg->bls_window2.v_size;
			rkisp_iowrite32(params_vdev, value, ISP_BLS_V2_STOP);
			new_control |= ISP_BLS_WINDOW_2;
		}

		if (arg->en_windows & BIT(0)) {
			rkisp_iowrite32(params_vdev, arg->bls_window1.h_offs,
					ISP_BLS_H1_START);
			value = arg->bls_window1.h_offs + arg->bls_window1.h_size;
			rkisp_iowrite32(params_vdev, value, ISP_BLS_H1_STOP);
			rkisp_iowrite32(params_vdev, arg->bls_window1.v_offs,
					ISP_BLS_V1_START);
			value = arg->bls_window1.v_offs + arg->bls_window1.v_size;
			rkisp_iowrite32(params_vdev, value, ISP_BLS_V1_STOP);
			new_control |= ISP_BLS_WINDOW_1;
		}

		rkisp_iowrite32(params_vdev, arg->bls_samples,
				ISP_BLS_SAMPLES);

		new_control |= ISP_BLS_MODE_MEASURED;
	}
	rkisp_iowrite32(params_vdev, new_control, ISP_BLS_CTRL);
}

static void
isp_bls_enable(struct rkisp_isp_params_vdev *params_vdev,
	       bool en)
{
	u32 new_control;

	new_control = rkisp_ioread32(params_vdev, ISP_BLS_CTRL);
	if (en)
		new_control |= ISP_BLS_ENA;
	else
		new_control &= ~ISP_BLS_ENA;
	rkisp_iowrite32(params_vdev, new_control, ISP_BLS_CTRL);
}

static void
isp_sdg_config(struct rkisp_isp_params_vdev *params_vdev,
	       const struct isp2x_sdg_cfg *arg)
{
	int i;

	rkisp_iowrite32(params_vdev,
			arg->xa_pnts.gamma_dx0, ISP_GAMMA_DX_LO);
	rkisp_iowrite32(params_vdev,
			arg->xa_pnts.gamma_dx1, ISP_GAMMA_DX_HI);

	for (i = 0; i < ISP2X_DEGAMMA_CURVE_SIZE; i++) {
		rkisp_iowrite32(params_vdev, arg->curve_r.gamma_y[i],
				ISP_GAMMA_R_Y_0 + i * 4);
		rkisp_iowrite32(params_vdev, arg->curve_g.gamma_y[i],
				ISP_GAMMA_G_Y_0 + i * 4);
		rkisp_iowrite32(params_vdev, arg->curve_b.gamma_y[i],
				ISP_GAMMA_B_Y_0 + i * 4);
	}
}

static void
isp_sdg_enable(struct rkisp_isp_params_vdev *params_vdev,
	       bool en)
{
	if (en) {
		isp_param_set_bits(params_vdev,
				   CIF_ISP_CTRL,
				   CIF_ISP_CTRL_ISP_GAMMA_IN_ENA);
	} else {
		isp_param_clear_bits(params_vdev,
				     CIF_ISP_CTRL,
				     CIF_ISP_CTRL_ISP_GAMMA_IN_ENA);
	}
}

static void __maybe_unused
isp_lsc_matrix_cfg_sram(struct rkisp_isp_params_vdev *params_vdev,
			const struct isp2x_lsc_cfg *pconfig, bool is_direct)
{
	int i, j;
	unsigned int sram_addr;
	unsigned int data;

	/* CIF_ISP_LSC_TABLE_ADDRESS_153 = ( 17 * 18 ) >> 1 */
	sram_addr = CIF_ISP_LSC_TABLE_ADDRESS_0;
	rkisp_write(params_vdev->dev, ISP_LSC_R_TABLE_ADDR, sram_addr, is_direct);
	rkisp_write(params_vdev->dev, ISP_LSC_GR_TABLE_ADDR, sram_addr, is_direct);
	rkisp_write(params_vdev->dev, ISP_LSC_GB_TABLE_ADDR, sram_addr, is_direct);
	rkisp_write(params_vdev->dev, ISP_LSC_B_TABLE_ADDR, sram_addr, is_direct);

	/* program data tables (table size is 9 * 17 = 153) */
	for (i = 0; i < CIF_ISP_LSC_SECTORS_MAX * CIF_ISP_LSC_SECTORS_MAX;
	     i += CIF_ISP_LSC_SECTORS_MAX) {
		/*
		 * 17 sectors with 2 values in one DWORD = 9
		 * DWORDs (2nd value of last DWORD unused)
		 */
		for (j = 0; j < CIF_ISP_LSC_SECTORS_MAX - 1; j += 2) {
			data = ISP_ISP_LSC_TABLE_DATA(
					pconfig->r_data_tbl[i + j],
					pconfig->r_data_tbl[i + j + 1]);
			rkisp_write(params_vdev->dev, ISP_LSC_R_TABLE_DATA, data, is_direct);

			data = ISP_ISP_LSC_TABLE_DATA(
					pconfig->gr_data_tbl[i + j],
					pconfig->gr_data_tbl[i + j + 1]);
			rkisp_write(params_vdev->dev, ISP_LSC_GR_TABLE_DATA, data, is_direct);

			data = ISP_ISP_LSC_TABLE_DATA(
					pconfig->gb_data_tbl[i + j],
					pconfig->gb_data_tbl[i + j + 1]);
			rkisp_write(params_vdev->dev, ISP_LSC_GB_TABLE_DATA, data, is_direct);

			data = ISP_ISP_LSC_TABLE_DATA(
					pconfig->b_data_tbl[i + j],
					pconfig->b_data_tbl[i + j + 1]);
			rkisp_write(params_vdev->dev, ISP_LSC_B_TABLE_DATA, data, is_direct);
		}

		data = ISP_ISP_LSC_TABLE_DATA(
				pconfig->r_data_tbl[i + j],
				0);
		rkisp_write(params_vdev->dev, ISP_LSC_R_TABLE_DATA, data, is_direct);

		data = ISP_ISP_LSC_TABLE_DATA(
				pconfig->gr_data_tbl[i + j],
				0);
		rkisp_write(params_vdev->dev, ISP_LSC_GR_TABLE_DATA, data, is_direct);

		data = ISP_ISP_LSC_TABLE_DATA(
				pconfig->gb_data_tbl[i + j],
				0);
		rkisp_write(params_vdev->dev, ISP_LSC_GB_TABLE_DATA, data, is_direct);

		data = ISP_ISP_LSC_TABLE_DATA(
				pconfig->b_data_tbl[i + j],
				0);
		rkisp_write(params_vdev->dev, ISP_LSC_B_TABLE_DATA, data, is_direct);
	}
}

static void __maybe_unused
isp_lsc_matrix_cfg_ddr(struct rkisp_isp_params_vdev *params_vdev,
		       const struct isp2x_lsc_cfg *pconfig)
{
	struct rkisp_isp_params_val_v21 *priv_val;
	u32 data, buf_idx, *vaddr[4], index[4];
	void *buf_vaddr;
	int i, j;

	memset(&index[0], 0, sizeof(index));
	priv_val = (struct rkisp_isp_params_val_v21 *)params_vdev->priv_val;
	buf_idx = (priv_val->buf_lsclut_idx++) % RKISP_PARAM_LSC_LUT_BUF_NUM;
	buf_vaddr = priv_val->buf_lsclut[buf_idx].vaddr;

	vaddr[0] = buf_vaddr;
	vaddr[1] = buf_vaddr + RKISP_PARAM_LSC_LUT_TBL_SIZE;
	vaddr[2] = buf_vaddr + RKISP_PARAM_LSC_LUT_TBL_SIZE * 2;
	vaddr[3] = buf_vaddr + RKISP_PARAM_LSC_LUT_TBL_SIZE * 3;

	/* program data tables (table size is 9 * 17 = 153) */
	for (i = 0; i < CIF_ISP_LSC_SECTORS_MAX * CIF_ISP_LSC_SECTORS_MAX;
	     i += CIF_ISP_LSC_SECTORS_MAX) {
		/*
		 * 17 sectors with 2 values in one DWORD = 9
		 * DWORDs (2nd value of last DWORD unused)
		 */
		for (j = 0; j < CIF_ISP_LSC_SECTORS_MAX - 1; j += 2) {
			data = ISP_ISP_LSC_TABLE_DATA(
					pconfig->r_data_tbl[i + j],
					pconfig->r_data_tbl[i + j + 1]);
			vaddr[0][index[0]++] = data;

			data = ISP_ISP_LSC_TABLE_DATA(
					pconfig->gr_data_tbl[i + j],
					pconfig->gr_data_tbl[i + j + 1]);
			vaddr[1][index[1]++] = data;

			data = ISP_ISP_LSC_TABLE_DATA(
					pconfig->b_data_tbl[i + j],
					pconfig->b_data_tbl[i + j + 1]);
			vaddr[2][index[2]++] = data;

			data = ISP_ISP_LSC_TABLE_DATA(
					pconfig->gb_data_tbl[i + j],
					pconfig->gb_data_tbl[i + j + 1]);
			vaddr[3][index[3]++] = data;
		}

		data = ISP_ISP_LSC_TABLE_DATA(
				pconfig->r_data_tbl[i + j],
				0);
		vaddr[0][index[0]++] = data;

		data = ISP_ISP_LSC_TABLE_DATA(
				pconfig->gr_data_tbl[i + j],
				0);
		vaddr[1][index[1]++] = data;

		data = ISP_ISP_LSC_TABLE_DATA(
				pconfig->b_data_tbl[i + j],
				0);
		vaddr[2][index[2]++] = data;

		data = ISP_ISP_LSC_TABLE_DATA(
				pconfig->gb_data_tbl[i + j],
				0);
		vaddr[3][index[3]++] = data;
	}

	data = priv_val->buf_lsclut[buf_idx].dma_addr;
	rkisp_iowrite32(params_vdev, data, MI_LUT_LSC_RD_BASE);
	rkisp_iowrite32(params_vdev, RKISP_PARAM_LSC_LUT_BUF_SIZE, MI_LUT_LSC_RD_WSIZE);
}

static void
isp_lsc_config(struct rkisp_isp_params_vdev *params_vdev,
	       const struct isp2x_lsc_cfg *arg)
{
	struct rkisp_device *dev = params_vdev->dev;
	unsigned int data;
	u32 lsc_ctrl;
	int i;

	/* To config must be off , store the current status firstly */
	lsc_ctrl = rkisp_ioread32(params_vdev, ISP_LSC_CTRL);
	isp_param_clear_bits(params_vdev, ISP_LSC_CTRL,
			     ISP_LSC_EN);
	if (!IS_HDR_RDBK(dev->rd_mode))
		isp_lsc_matrix_cfg_ddr(params_vdev, arg);

	for (i = 0; i < 4; i++) {
		/* program x size tables */
		data = CIF_ISP_LSC_SECT_SIZE(arg->x_size_tbl[i * 2],
					     arg->x_size_tbl[i * 2 + 1]);
		rkisp_iowrite32(params_vdev, data,
				ISP_LSC_XSIZE_01 + i * 4);

		/* program x grad tables */
		data = CIF_ISP_LSC_SECT_SIZE(arg->x_grad_tbl[i * 2],
					     arg->x_grad_tbl[i * 2 + 1]);
		rkisp_iowrite32(params_vdev, data,
				ISP_LSC_XGRAD_01 + i * 4);

		/* program y size tables */
		data = CIF_ISP_LSC_SECT_SIZE(arg->y_size_tbl[i * 2],
					     arg->y_size_tbl[i * 2 + 1]);
		rkisp_iowrite32(params_vdev, data,
				ISP_LSC_YSIZE_01 + i * 4);

		/* program y grad tables */
		data = CIF_ISP_LSC_SECT_SIZE(arg->y_grad_tbl[i * 2],
					     arg->y_grad_tbl[i * 2 + 1]);
		rkisp_iowrite32(params_vdev, data,
				ISP_LSC_YGRAD_01 + i * 4);
	}

	/* restore the lsc ctrl status */
	if (lsc_ctrl & ISP_LSC_EN) {
		isp_param_set_bits(params_vdev,
				   ISP_LSC_CTRL,
				   ISP_LSC_EN);
	} else {
		isp_param_clear_bits(params_vdev,
				     ISP_LSC_CTRL,
				     ISP_LSC_EN);
	}

	params_vdev->cur_lsccfg = *arg;
}

static void
isp_lsc_enable(struct rkisp_isp_params_vdev *params_vdev,
	       bool en)
{
	struct rkisp_device *dev = params_vdev->dev;
	u32 val = ISP_LSC_EN;

	if (!IS_HDR_RDBK(dev->rd_mode))
		val |= ISP_LSC_LUT_EN;

	if (en)
		isp_param_set_bits(params_vdev, ISP_LSC_CTRL, val);
	else
		isp_param_clear_bits(params_vdev, ISP_LSC_CTRL, ISP_LSC_EN);
}

static void
isp_debayer_config(struct rkisp_isp_params_vdev *params_vdev,
		   const struct isp2x_debayer_cfg *arg)
{
	u32 value;

	value = rkisp_ioread32(params_vdev, ISP_DEBAYER_CONTROL);
	value &= ISP_DEBAYER_EN;

	value |= (arg->filter_c_en & 0x01) << 8 |
		 (arg->filter_g_en & 0x01) << 4;
	rkisp_iowrite32(params_vdev, value, ISP_DEBAYER_CONTROL);

	value = (arg->thed1 & 0x0F) << 12 |
		(arg->thed0 & 0x0F) << 8 |
		(arg->dist_scale & 0x0F) << 4 |
		(arg->max_ratio & 0x07) << 1 |
		(arg->clip_en & 0x01);
	rkisp_iowrite32(params_vdev, value, ISP_DEBAYER_G_INTERP);

	value = (arg->filter1_coe5 & 0x0F) << 16 |
		(arg->filter1_coe4 & 0x0F) << 12 |
		(arg->filter1_coe3 & 0x0F) << 8 |
		(arg->filter1_coe2 & 0x0F) << 4 |
		(arg->filter1_coe1 & 0x0F);
	rkisp_iowrite32(params_vdev, value, ISP_DEBAYER_G_INTERP_FILTER1);

	value = (arg->filter2_coe5 & 0x0F) << 16 |
		(arg->filter2_coe4 & 0x0F) << 12 |
		(arg->filter2_coe3 & 0x0F) << 8 |
		(arg->filter2_coe2 & 0x0F) << 4 |
		(arg->filter2_coe1 & 0x0F);
	rkisp_iowrite32(params_vdev, value, ISP_DEBAYER_G_INTERP_FILTER2);

	value = (arg->hf_offset & 0xFFFF) << 16 |
		(arg->gain_offset & 0x0F) << 8 |
		(arg->offset & 0x1F);
	rkisp_iowrite32(params_vdev, value, ISP_DEBAYER_G_FILTER);

	value = (arg->shift_num & 0x03) << 16 |
		(arg->order_max & 0x1F) << 8 |
		(arg->order_min & 0x1F);
	rkisp_iowrite32(params_vdev, value, ISP_DEBAYER_C_FILTER);
}

static void
isp_debayer_enable(struct rkisp_isp_params_vdev *params_vdev,
		   bool en)
{
	if (en)
		isp_param_set_bits(params_vdev,
				   ISP_DEBAYER_CONTROL,
				   ISP_DEBAYER_EN);
	else
		isp_param_clear_bits(params_vdev,
				     ISP_DEBAYER_CONTROL,
				     ISP_DEBAYER_EN);
}

static void
isp_awbgain_config(struct rkisp_isp_params_vdev *params_vdev,
		   const struct isp21_awb_gain_cfg *arg)
{
	struct rkisp_device *dev = params_vdev->dev;

	if (!arg->gain0_red || !arg->gain0_blue ||
	    !arg->gain1_red || !arg->gain1_blue ||
	    !arg->gain2_red || !arg->gain2_blue ||
	    !arg->gain0_green_r || !arg->gain0_green_b ||
	    !arg->gain1_green_r || !arg->gain1_green_b ||
	    !arg->gain2_green_r || !arg->gain2_green_b) {
		dev_err(dev->dev, "awb gain is zero!\n");
		return;
	}

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->gain0_green_b, arg->gain0_green_r),
			ISP21_AWB_GAIN0_G);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->gain0_blue, arg->gain0_red),
			ISP21_AWB_GAIN0_RB);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->gain1_green_b, arg->gain1_green_r),
			ISP21_AWB_GAIN1_G);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->gain1_blue, arg->gain1_red),
			ISP21_AWB_GAIN1_RB);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->gain2_green_b, arg->gain2_green_r),
			ISP21_AWB_GAIN2_G);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->gain2_blue, arg->gain2_red),
			ISP21_AWB_GAIN2_RB);
}

static void
isp_awbgain_enable(struct rkisp_isp_params_vdev *params_vdev,
		   bool en)
{
	if (en)
		isp_param_set_bits(params_vdev, CIF_ISP_CTRL,
				   CIF_ISP_CTRL_ISP_AWB_ENA);
	else
		isp_param_clear_bits(params_vdev, CIF_ISP_CTRL,
				     CIF_ISP_CTRL_ISP_AWB_ENA);
}

static void
isp_ccm_config(struct rkisp_isp_params_vdev *params_vdev,
	       const struct isp21_ccm_cfg *arg)
{
	u32 value;
	u32 i;

	value = rkisp_ioread32(params_vdev, ISP_CCM_CTRL);
	value &= ISP_CCM_EN;

	value |= (arg->highy_adjust_dis & 0x01) << 1;
	rkisp_iowrite32(params_vdev, value, ISP_CCM_CTRL);

	value = ISP2X_PACK_2SHORT(arg->coeff0_r, arg->coeff1_r);
	rkisp_iowrite32(params_vdev, value, ISP_CCM_COEFF0_R);

	value = ISP2X_PACK_2SHORT(arg->coeff2_r, arg->offset_r);
	rkisp_iowrite32(params_vdev, value, ISP_CCM_COEFF1_R);

	value = ISP2X_PACK_2SHORT(arg->coeff0_g, arg->coeff1_g);
	rkisp_iowrite32(params_vdev, value, ISP_CCM_COEFF0_G);

	value = ISP2X_PACK_2SHORT(arg->coeff2_g, arg->offset_g);
	rkisp_iowrite32(params_vdev, value, ISP_CCM_COEFF1_G);

	value = ISP2X_PACK_2SHORT(arg->coeff0_b, arg->coeff1_b);
	rkisp_iowrite32(params_vdev, value, ISP_CCM_COEFF0_B);

	value = ISP2X_PACK_2SHORT(arg->coeff2_b, arg->offset_b);
	rkisp_iowrite32(params_vdev, value, ISP_CCM_COEFF1_B);

	value = ISP2X_PACK_2SHORT(arg->coeff0_y, arg->coeff1_y);
	rkisp_iowrite32(params_vdev, value, ISP_CCM_COEFF0_Y);

	value = ISP2X_PACK_2SHORT(arg->coeff2_y, 0);
	rkisp_iowrite32(params_vdev, value, ISP_CCM_COEFF1_Y);

	for (i = 0; i < ISP2X_CCM_CURVE_NUM / 2; i++) {
		value = ISP2X_PACK_2SHORT(arg->alp_y[2 * i], arg->alp_y[2 * i + 1]);
		rkisp_iowrite32(params_vdev, value, ISP_CCM_ALP_Y0 + 4 * i);
	}
	value = ISP2X_PACK_2SHORT(arg->alp_y[2 * i], 0);
	rkisp_iowrite32(params_vdev, value, ISP_CCM_ALP_Y0 + 4 * i);

	value = arg->bound_bit & 0x0F;
	rkisp_iowrite32(params_vdev, value, ISP_CCM_BOUND_BIT);
}

static void
isp_ccm_enable(struct rkisp_isp_params_vdev *params_vdev,
	       bool en)
{
	if (en)
		isp_param_set_bits(params_vdev, ISP_CCM_CTRL, ISP_CCM_EN);
	else
		isp_param_clear_bits(params_vdev, ISP_CCM_CTRL, ISP_CCM_EN);
}

static void
isp_goc_config(struct rkisp_isp_params_vdev *params_vdev,
	       const struct isp2x_gammaout_cfg *arg)
{
	int i;
	u32 value;

	if (arg->equ_segm)
		isp_param_set_bits(params_vdev, ISP_GAMMA_OUT_CTRL, 0x02);
	else
		isp_param_clear_bits(params_vdev, ISP_GAMMA_OUT_CTRL, 0x02);

	rkisp_iowrite32(params_vdev, arg->offset, ISP_GAMMA_OUT_OFFSET);
	for (i = 0; i < ISP2X_GAMMA_OUT_MAX_SAMPLES / 2; i++) {
		value = ISP2X_PACK_2SHORT(
			arg->gamma_y[2 * i],
			arg->gamma_y[2 * i + 1]);
		rkisp_iowrite32(params_vdev, value, ISP_GAMMA_OUT_Y0 + i * 4);
	}

	rkisp_iowrite32(params_vdev, arg->gamma_y[2 * i], ISP_GAMMA_OUT_Y0 + i * 4);
}

static void
isp_goc_enable(struct rkisp_isp_params_vdev *params_vdev,
	       bool en)
{
	if (en)
		isp_param_set_bits(params_vdev, ISP_GAMMA_OUT_CTRL, 0x01);
	else
		isp_param_clear_bits(params_vdev, ISP_GAMMA_OUT_CTRL, 0x01);
}

static void
isp_cproc_config(struct rkisp_isp_params_vdev *params_vdev,
		 const struct isp2x_cproc_cfg *arg)
{
	struct isp21_isp_other_cfg *cur_other_cfg =
		&params_vdev->isp21_params->others;
	struct isp2x_ie_cfg *cur_ie_config = &cur_other_cfg->ie_cfg;
	u32 effect = cur_ie_config->effect;
	u32 quantization = params_vdev->quantization;

	rkisp_iowrite32(params_vdev, arg->contrast, CPROC_CONTRAST);
	rkisp_iowrite32(params_vdev, arg->hue, CPROC_HUE);
	rkisp_iowrite32(params_vdev, arg->sat, CPROC_SATURATION);
	rkisp_iowrite32(params_vdev, arg->brightness, CPROC_BRIGHTNESS);

	if (quantization != V4L2_QUANTIZATION_FULL_RANGE ||
	    effect != V4L2_COLORFX_NONE) {
		isp_param_clear_bits(params_vdev, CPROC_CTRL,
				     CIF_C_PROC_YOUT_FULL |
				     CIF_C_PROC_YIN_FULL |
				     CIF_C_PROC_COUT_FULL);
	} else {
		isp_param_set_bits(params_vdev, CPROC_CTRL,
				   CIF_C_PROC_YOUT_FULL |
				   CIF_C_PROC_YIN_FULL |
				   CIF_C_PROC_COUT_FULL);
	}
}

static void
isp_cproc_enable(struct rkisp_isp_params_vdev *params_vdev,
		 bool en)
{
	if (en)
		isp_param_set_bits(params_vdev,
				   CPROC_CTRL,
				   CIF_C_PROC_CTR_ENABLE);
	else
		isp_param_clear_bits(params_vdev,
				   CPROC_CTRL,
				   CIF_C_PROC_CTR_ENABLE);
}

static void
isp_ie_config(struct rkisp_isp_params_vdev *params_vdev,
	      const struct isp2x_ie_cfg *arg)
{
	u32 eff_ctrl;

	eff_ctrl = rkisp_ioread32(params_vdev, CIF_IMG_EFF_CTRL);
	eff_ctrl &= ~CIF_IMG_EFF_CTRL_MODE_MASK;

	if (params_vdev->quantization == V4L2_QUANTIZATION_FULL_RANGE)
		eff_ctrl |= CIF_IMG_EFF_CTRL_YCBCR_FULL;

	switch (arg->effect) {
	case V4L2_COLORFX_SEPIA:
		eff_ctrl |= CIF_IMG_EFF_CTRL_MODE_SEPIA;
		break;
	case V4L2_COLORFX_SET_CBCR:
		rkisp_iowrite32(params_vdev, arg->eff_tint, CIF_IMG_EFF_TINT);
		eff_ctrl |= CIF_IMG_EFF_CTRL_MODE_SEPIA;
		break;
		/*
		 * Color selection is similar to water color(AQUA):
		 * grayscale + selected color w threshold
		 */
	case V4L2_COLORFX_AQUA:
		eff_ctrl |= CIF_IMG_EFF_CTRL_MODE_COLOR_SEL;
		rkisp_iowrite32(params_vdev, arg->color_sel,
				CIF_IMG_EFF_COLOR_SEL);
		break;
	case V4L2_COLORFX_EMBOSS:
		eff_ctrl |= CIF_IMG_EFF_CTRL_MODE_EMBOSS;
		rkisp_iowrite32(params_vdev, arg->eff_mat_1,
				CIF_IMG_EFF_MAT_1);
		rkisp_iowrite32(params_vdev, arg->eff_mat_2,
				CIF_IMG_EFF_MAT_2);
		rkisp_iowrite32(params_vdev, arg->eff_mat_3,
				CIF_IMG_EFF_MAT_3);
		break;
	case V4L2_COLORFX_SKETCH:
		eff_ctrl |= CIF_IMG_EFF_CTRL_MODE_SKETCH;
		rkisp_iowrite32(params_vdev, arg->eff_mat_3,
				CIF_IMG_EFF_MAT_3);
		rkisp_iowrite32(params_vdev, arg->eff_mat_4,
				CIF_IMG_EFF_MAT_4);
		rkisp_iowrite32(params_vdev, arg->eff_mat_5,
				CIF_IMG_EFF_MAT_5);
		break;
	case V4L2_COLORFX_BW:
		eff_ctrl |= CIF_IMG_EFF_CTRL_MODE_BLACKWHITE;
		break;
	case V4L2_COLORFX_NEGATIVE:
		eff_ctrl |= CIF_IMG_EFF_CTRL_MODE_NEGATIVE;
		break;
	default:
		break;
	}

	rkisp_iowrite32(params_vdev, eff_ctrl, CIF_IMG_EFF_CTRL);
}

static void
isp_ie_enable(struct rkisp_isp_params_vdev *params_vdev,
	      bool en)
{
	if (en) {
		isp_param_set_bits(params_vdev, CIF_IMG_EFF_CTRL,
				   CIF_IMG_EFF_CTRL_ENABLE);
		isp_param_set_bits(params_vdev, CIF_IMG_EFF_CTRL,
				   CIF_IMG_EFF_CTRL_CFG_UPD);
	} else {
		isp_param_clear_bits(params_vdev, CIF_IMG_EFF_CTRL,
				     CIF_IMG_EFF_CTRL_ENABLE);
	}
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

	value &= ISP2X_RAWAF_ENA;
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
	struct rkisp_device *ispdev = params_vdev->dev;
	struct v4l2_rect *out_crop = &ispdev->isp_sdev.out_crop;
	u32 block_hsize, block_vsize, value;
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

	block_hsize = arg->win.h_size / ae_wnd_num[wnd_num_idx];
	value = block_hsize * ae_wnd_num[wnd_num_idx] + arg->win.h_offs;
	if (value + 1 > out_crop->width)
		block_hsize -= 1;
	block_vsize = arg->win.v_size / ae_wnd_num[wnd_num_idx];
	value = block_vsize * ae_wnd_num[wnd_num_idx] + arg->win.v_offs;
	if (value + 2 > out_crop->height)
		block_vsize -= 1;
	if (block_vsize % 2)
		block_vsize -= 1;
	rkisp_iowrite32(params_vdev,
			ISP2X_RAWAE_LITE_V_SIZE_SET(block_vsize) |
			ISP2X_RAWAE_LITE_H_SIZE_SET(block_hsize),
			ISP_RAWAE_LITE_BLK_SIZ);

	value = rkisp_ioread32(params_vdev, CTRL_VI_ISP_PATH);
	value &= ~(ISP2X_ISPPATH_RAWAE_SWAP_SET(3));
	value |= ISP2X_ISPPATH_RAWAE_SWAP_SET(arg->rawae_sel);
	rkisp_iowrite32(params_vdev, value, CTRL_VI_ISP_PATH);
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
	struct rkisp_device *ispdev = params_vdev->dev;
	struct v4l2_rect *out_crop = &ispdev->isp_sdev.out_crop;
	u32 block_hsize, block_vsize;
	u32 addr, i, value, h_size, v_size;
	u32 wnd_num_idx = 0;
	const u32 ae_wnd_num[] = {
		1, 5, 15, 15
	};

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

	block_hsize = arg->win.h_size / ae_wnd_num[wnd_num_idx];
	value = block_hsize * ae_wnd_num[wnd_num_idx] + arg->win.h_offs;
	if (value + 1 > out_crop->width)
		block_hsize -= 1;
	block_vsize = arg->win.v_size / ae_wnd_num[wnd_num_idx];
	value = block_vsize * ae_wnd_num[wnd_num_idx] + arg->win.v_offs;
	if (value + 2 > out_crop->height)
		block_vsize -= 1;
	if (block_vsize % 2)
		block_vsize -= 1;
	rkisp_iowrite32(params_vdev,
			ISP2X_RAWAEBIG_V_SIZE_SET(block_vsize) |
			ISP2X_RAWAEBIG_H_SIZE_SET(block_hsize),
			addr + RAWAE_BIG_BLK_SIZE);

	for (i = 0; i < ISP2X_RAWAEBIG_SUBWIN_NUM; i++) {
		rkisp_iowrite32(params_vdev,
			ISP2X_RAWAEBIG_SUBWIN_V_OFFSET_SET(arg->subwin[i].v_offs) |
			ISP2X_RAWAEBIG_SUBWIN_H_OFFSET_SET(arg->subwin[i].h_offs),
			addr + RAWAE_BIG_WND1_OFFSET + 8 * i);

		v_size = arg->subwin[i].v_size + arg->subwin[i].v_offs;
		h_size = arg->subwin[i].h_size + arg->subwin[i].h_offs;
		rkisp_iowrite32(params_vdev,
			ISP2X_RAWAEBIG_SUBWIN_V_SIZE_SET(v_size) |
			ISP2X_RAWAEBIG_SUBWIN_H_SIZE_SET(h_size),
			addr + RAWAE_BIG_WND1_SIZE + 8 * i);
	}

	if (blk_no == 0) {
		value = rkisp_ioread32(params_vdev, CTRL_VI_ISP_PATH);
		value &= ~(ISP2X_ISPPATH_RAWAE_SEL_SET(3));
		value |= ISP2X_ISPPATH_RAWAE_SEL_SET(arg->rawae_sel);
		rkisp_iowrite32(params_vdev, value, CTRL_VI_ISP_PATH);
	} else {
		value = rkisp_ioread32(params_vdev, CTRL_VI_ISP_PATH);
		value &= ~(ISP2X_ISPPATH_RAWAE_SWAP_SET(3));
		value |= ISP2X_ISPPATH_RAWAE_SWAP_SET(arg->rawae_sel);
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
isp_rawae1_config(struct rkisp_isp_params_vdev *params_vdev,
		  const struct isp2x_rawaebig_meas_cfg *arg)
{
	isp_rawaebig_config(params_vdev, arg, 1);
}

static void
isp_rawae1_enable(struct rkisp_isp_params_vdev *params_vdev,
		  bool en)
{
	isp_rawaebig_enable(params_vdev, en, 1);
}

static void
isp_rawae2_config(struct rkisp_isp_params_vdev *params_vdev,
		  const struct isp2x_rawaebig_meas_cfg *arg)
{
	isp_rawaebig_config(params_vdev, arg, 2);
}

static void
isp_rawae2_enable(struct rkisp_isp_params_vdev *params_vdev,
		  bool en)
{
	isp_rawaebig_enable(params_vdev, en, 2);
}

static void
isp_rawae3_config(struct rkisp_isp_params_vdev *params_vdev,
		  const struct isp2x_rawaebig_meas_cfg *arg)
{
	isp_rawaebig_config(params_vdev, arg, 0);
}

static void
isp_rawae3_enable(struct rkisp_isp_params_vdev *params_vdev,
		  bool en)
{
	isp_rawaebig_enable(params_vdev, en, 0);
}

static void
isp_rawawb_config(struct rkisp_isp_params_vdev *params_vdev,
		  const struct isp21_rawawb_meas_cfg *arg)
{
	u32 i, value;

	rkisp_iowrite32(params_vdev,
			(arg->sw_rawawb_blk_measure_enable & 0x1) |
			(arg->sw_rawawb_blk_measure_mode & 0x1) << 1 |
			(arg->sw_rawawb_blk_measure_xytype & 0x1) << 2 |
			(arg->sw_rawawb_blk_rtdw_measure_en & 0x1) << 3 |
			(arg->sw_rawawb_blk_measure_illu_idx & 0x7) << 4 |
			(arg->sw_rawawb_blk_with_luma_wei_en & 0x1) << 8,
			ISP21_RAWAWB_BLK_CTRL);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_h_offs, arg->sw_rawawb_v_offs),
			ISP21_RAWAWB_WIN_OFFS);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_h_size, arg->sw_rawawb_v_size),
			ISP21_RAWAWB_WIN_SIZE);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_r_max, arg->sw_rawawb_g_max),
			ISP21_RAWAWB_LIMIT_RG_MAX);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_b_max, arg->sw_rawawb_y_max),
			ISP21_RAWAWB_LIMIT_BY_MAX);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_r_min, arg->sw_rawawb_g_min),
			ISP21_RAWAWB_LIMIT_RG_MIN);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_b_min, arg->sw_rawawb_y_min),
			ISP21_RAWAWB_LIMIT_BY_MIN);

	rkisp_iowrite32(params_vdev,
			(arg->sw_rawawb_wp_luma_wei_en0 & 0x1) |
			(arg->sw_rawawb_wp_luma_wei_en1 & 0x1) << 1 |
			(arg->sw_rawawb_wp_blk_wei_en0 & 0x1) << 2 |
			(arg->sw_rawawb_wp_blk_wei_en1 & 0x1) << 3 |
			(arg->sw_rawawb_wp_hist_xytype & 0x1) << 4,
			ISP21_RAWAWB_WEIGHT_CURVE_CTRL);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_4BYTE(arg->sw_rawawb_wp_luma_weicurve_y0,
					 arg->sw_rawawb_wp_luma_weicurve_y1,
					 arg->sw_rawawb_wp_luma_weicurve_y2,
					 arg->sw_rawawb_wp_luma_weicurve_y3),
			ISP21_RAWAWB_YWEIGHT_CURVE_XCOOR03);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_4BYTE(arg->sw_rawawb_wp_luma_weicurve_y4,
					 arg->sw_rawawb_wp_luma_weicurve_y5,
					 arg->sw_rawawb_wp_luma_weicurve_y6,
					 arg->sw_rawawb_wp_luma_weicurve_y7),
			ISP21_RAWAWB_YWEIGHT_CURVE_XCOOR47);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_wp_luma_weicurve_y8,
			ISP21_RAWAWB_YWEIGHT_CURVE_XCOOR8);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_4BYTE(arg->sw_rawawb_wp_luma_weicurve_w0,
					 arg->sw_rawawb_wp_luma_weicurve_w1,
					 arg->sw_rawawb_wp_luma_weicurve_w2,
					 arg->sw_rawawb_wp_luma_weicurve_w3),
			ISP21_RAWAWB_YWEIGHT_CURVE_YCOOR03);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_4BYTE(arg->sw_rawawb_wp_luma_weicurve_w4,
					 arg->sw_rawawb_wp_luma_weicurve_w5,
					 arg->sw_rawawb_wp_luma_weicurve_w6,
					 arg->sw_rawawb_wp_luma_weicurve_w7),
			ISP21_RAWAWB_YWEIGHT_CURVE_YCOOR47);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_wp_luma_weicurve_w8,
					  arg->sw_rawawb_pre_wbgain_inv_r),
			ISP21_RAWAWB_YWEIGHT_CURVE_YCOOR8);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_pre_wbgain_inv_g,
					  arg->sw_rawawb_pre_wbgain_inv_b),
			ISP21_RAWAWB_PRE_WBGAIN_INV);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex0_u_0,
					  arg->sw_rawawb_vertex0_v_0),
			ISP21_RAWAWB_UV_DETC_VERTEX0_0);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex1_u_0,
					  arg->sw_rawawb_vertex1_v_0),
			ISP21_RAWAWB_UV_DETC_VERTEX1_0);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex2_u_0,
					  arg->sw_rawawb_vertex2_v_0),
			ISP21_RAWAWB_UV_DETC_VERTEX2_0);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex3_u_0,
					  arg->sw_rawawb_vertex3_v_0),
			ISP21_RAWAWB_UV_DETC_VERTEX3_0);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope01_0,
			ISP21_RAWAWB_UV_DETC_ISLOPE01_0);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope12_0,
			ISP21_RAWAWB_UV_DETC_ISLOPE12_0);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope23_0,
			ISP21_RAWAWB_UV_DETC_ISLOPE23_0);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope30_0,
			ISP21_RAWAWB_UV_DETC_ISLOPE30_0);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex0_u_1,
					  arg->sw_rawawb_vertex0_v_1),
			ISP21_RAWAWB_UV_DETC_VERTEX0_1);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex1_u_1,
					  arg->sw_rawawb_vertex1_v_1),
			ISP21_RAWAWB_UV_DETC_VERTEX1_1);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex2_u_1,
					  arg->sw_rawawb_vertex2_v_1),
			ISP21_RAWAWB_UV_DETC_VERTEX2_1);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex3_u_1,
					  arg->sw_rawawb_vertex3_v_1),
			ISP21_RAWAWB_UV_DETC_VERTEX3_1);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope01_1,
			ISP21_RAWAWB_UV_DETC_ISLOPE01_1);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope12_1,
			ISP21_RAWAWB_UV_DETC_ISLOPE12_1);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope23_1,
			ISP21_RAWAWB_UV_DETC_ISLOPE23_1);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope30_1,
			ISP21_RAWAWB_UV_DETC_ISLOPE30_1);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex0_u_2,
					  arg->sw_rawawb_vertex0_v_2),
			ISP21_RAWAWB_UV_DETC_VERTEX0_2);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex1_u_2,
					  arg->sw_rawawb_vertex1_v_2),
			ISP21_RAWAWB_UV_DETC_VERTEX1_2);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex2_u_2,
					  arg->sw_rawawb_vertex2_v_2),
			ISP21_RAWAWB_UV_DETC_VERTEX2_2);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex3_u_2,
					  arg->sw_rawawb_vertex3_v_2),
			ISP21_RAWAWB_UV_DETC_VERTEX3_2);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope01_2,
			ISP21_RAWAWB_UV_DETC_ISLOPE01_2);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope12_2,
			ISP21_RAWAWB_UV_DETC_ISLOPE12_2);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope23_2,
			ISP21_RAWAWB_UV_DETC_ISLOPE23_2);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope30_2,
			ISP21_RAWAWB_UV_DETC_ISLOPE30_2);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex0_u_3,
					  arg->sw_rawawb_vertex0_v_3),
			ISP21_RAWAWB_UV_DETC_VERTEX0_3);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex1_u_3,
					  arg->sw_rawawb_vertex1_v_3),
			ISP21_RAWAWB_UV_DETC_VERTEX1_3);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex2_u_3,
					  arg->sw_rawawb_vertex2_v_3),
			ISP21_RAWAWB_UV_DETC_VERTEX2_3);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex3_u_3,
					  arg->sw_rawawb_vertex3_v_3),
			ISP21_RAWAWB_UV_DETC_VERTEX3_3);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope01_3,
			ISP21_RAWAWB_UV_DETC_ISLOPE01_3);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope12_3,
			ISP21_RAWAWB_UV_DETC_ISLOPE12_3);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope23_3,
			ISP21_RAWAWB_UV_DETC_ISLOPE23_3);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope30_3,
			ISP21_RAWAWB_UV_DETC_ISLOPE30_3);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex0_u_4,
					  arg->sw_rawawb_vertex0_v_4),
			ISP21_RAWAWB_UV_DETC_VERTEX0_4);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex1_u_4,
					  arg->sw_rawawb_vertex1_v_4),
			ISP21_RAWAWB_UV_DETC_VERTEX1_4);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex2_u_4,
					  arg->sw_rawawb_vertex2_v_4),
			ISP21_RAWAWB_UV_DETC_VERTEX2_4);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex3_u_4,
					  arg->sw_rawawb_vertex3_v_4),
			ISP21_RAWAWB_UV_DETC_VERTEX3_4);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope01_4,
			ISP21_RAWAWB_UV_DETC_ISLOPE01_4);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope12_4,
			ISP21_RAWAWB_UV_DETC_ISLOPE12_4);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope23_4,
			ISP21_RAWAWB_UV_DETC_ISLOPE23_4);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope30_4,
			ISP21_RAWAWB_UV_DETC_ISLOPE30_4);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex0_u_5,
					  arg->sw_rawawb_vertex0_v_5),
			ISP21_RAWAWB_UV_DETC_VERTEX0_5);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex1_u_5,
					  arg->sw_rawawb_vertex1_v_5),
			ISP21_RAWAWB_UV_DETC_VERTEX1_5);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex2_u_5,
					  arg->sw_rawawb_vertex2_v_5),
			ISP21_RAWAWB_UV_DETC_VERTEX2_5);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex3_u_5,
					  arg->sw_rawawb_vertex3_v_5),
			ISP21_RAWAWB_UV_DETC_VERTEX3_5);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope01_5,
			ISP21_RAWAWB_UV_DETC_ISLOPE01_5);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope12_5,
			ISP21_RAWAWB_UV_DETC_ISLOPE10_5);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope23_5,
			ISP21_RAWAWB_UV_DETC_ISLOPE23_5);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope30_5,
			ISP21_RAWAWB_UV_DETC_ISLOPE30_5);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex0_u_6,
					  arg->sw_rawawb_vertex0_v_6),
			ISP21_RAWAWB_UV_DETC_VERTEX0_6);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex1_u_6,
					  arg->sw_rawawb_vertex1_v_6),
			ISP21_RAWAWB_UV_DETC_VERTEX1_6);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex2_u_6,
					  arg->sw_rawawb_vertex2_v_6),
			ISP21_RAWAWB_UV_DETC_VERTEX2_6);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_vertex3_u_6,
					  arg->sw_rawawb_vertex3_v_6),
			ISP21_RAWAWB_UV_DETC_VERTEX3_6);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope01_6,
			ISP21_RAWAWB_UV_DETC_ISLOPE01_6);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope12_6,
			ISP21_RAWAWB_UV_DETC_ISLOPE10_6);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope23_6,
			ISP21_RAWAWB_UV_DETC_ISLOPE23_6);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_islope30_6,
			ISP21_RAWAWB_UV_DETC_ISLOPE30_6);


	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_rgb2ryuvmat0_y,
					  arg->sw_rawawb_rgb2ryuvmat1_y),
			ISP21_RAWAWB_YUV_RGB2ROTY_0);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_rgb2ryuvmat2_y,
					  arg->sw_rawawb_rgb2ryuvofs_y),
			ISP21_RAWAWB_YUV_RGB2ROTY_1);


	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_rgb2ryuvmat0_u,
					  arg->sw_rawawb_rgb2ryuvmat1_u),
			ISP21_RAWAWB_YUV_RGB2ROTU_0);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_rgb2ryuvmat2_u,
					  arg->sw_rawawb_rgb2ryuvofs_u),
			ISP21_RAWAWB_YUV_RGB2ROTU_1);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_rgb2ryuvmat0_v,
					  arg->sw_rawawb_rgb2ryuvmat1_v),
			ISP21_RAWAWB_YUV_RGB2ROTV_0);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_rgb2ryuvmat2_v,
					  arg->sw_rawawb_rgb2ryuvofs_v),
			ISP21_RAWAWB_YUV_RGB2ROTV_1);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_coor_x1_ls0_y,
					  arg->sw_rawawb_vec_x21_ls0_y),
			ISP21_RAWAWB_YUV_X_COOR_Y_0);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_coor_x1_ls0_u,
					  arg->sw_rawawb_vec_x21_ls0_u),
			ISP21_RAWAWB_YUV_X_COOR_U_0);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_coor_x1_ls0_v,
					  arg->sw_rawawb_vec_x21_ls0_v),
			ISP21_RAWAWB_YUV_X_COOR_V_0);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_4BYTE(arg->sw_rawawb_dis_x1x2_ls0,
					 0,
					 arg->sw_rawawb_rotu0_ls0,
					 arg->sw_rawawb_rotu1_ls0),
			ISP21_RAWAWB_YUV_X1X2_DIS_0);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_4BYTE(arg->sw_rawawb_rotu2_ls0,
					 arg->sw_rawawb_rotu3_ls0,
					 arg->sw_rawawb_rotu4_ls0,
					 arg->sw_rawawb_rotu5_ls0),
			ISP21_RAWAWB_YUV_INTERP_CURVE_UCOOR_0);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_th0_ls0,
					  arg->sw_rawawb_th1_ls0),
			ISP21_RAWAWB_YUV_INTERP_CURVE_TH0_0);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_th2_ls0,
					  arg->sw_rawawb_th3_ls0),
			ISP21_RAWAWB_YUV_INTERP_CURVE_TH1_0);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_th4_ls0,
					  arg->sw_rawawb_th5_ls0),
			ISP21_RAWAWB_YUV_INTERP_CURVE_TH2_0);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_coor_x1_ls1_y,
					  arg->sw_rawawb_vec_x21_ls1_y),
			ISP21_RAWAWB_YUV_X_COOR_Y_1);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_coor_x1_ls1_u,
					  arg->sw_rawawb_vec_x21_ls1_u),
			ISP21_RAWAWB_YUV_X_COOR_U_1);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_coor_x1_ls1_v,
					  arg->sw_rawawb_vec_x21_ls1_v),
			ISP21_RAWAWB_YUV_X_COOR_V_1);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_4BYTE(arg->sw_rawawb_dis_x1x2_ls1,
					 0,
					 arg->sw_rawawb_rotu0_ls1,
					 arg->sw_rawawb_rotu1_ls1),
			ISP21_RAWAWB_YUV_X1X2_DIS_1);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_4BYTE(arg->sw_rawawb_rotu2_ls1,
					 arg->sw_rawawb_rotu3_ls1,
					 arg->sw_rawawb_rotu4_ls1,
					 arg->sw_rawawb_rotu5_ls1),
			ISP21_RAWAWB_YUV_INTERP_CURVE_UCOOR_1);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_th0_ls1,
					  arg->sw_rawawb_th1_ls1),
			ISP21_RAWAWB_YUV_INTERP_CURVE_TH0_1);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_th2_ls1,
					  arg->sw_rawawb_th3_ls1),
			ISP21_RAWAWB_YUV_INTERP_CURVE_TH1_1);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_th4_ls1,
					  arg->sw_rawawb_th5_ls1),
			ISP21_RAWAWB_YUV_INTERP_CURVE_TH2_1);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_coor_x1_ls2_y,
					  arg->sw_rawawb_vec_x21_ls2_y),
			ISP21_RAWAWB_YUV_X_COOR_Y_2);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_coor_x1_ls2_u,
					  arg->sw_rawawb_vec_x21_ls2_u),
			ISP21_RAWAWB_YUV_X_COOR_U_2);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_coor_x1_ls2_v,
					  arg->sw_rawawb_vec_x21_ls2_v),
			ISP21_RAWAWB_YUV_X_COOR_V_2);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_4BYTE(arg->sw_rawawb_dis_x1x2_ls2,
					 0,
					 arg->sw_rawawb_rotu0_ls2,
					 arg->sw_rawawb_rotu1_ls2),
			ISP21_RAWAWB_YUV_X1X2_DIS_2);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_4BYTE(arg->sw_rawawb_rotu2_ls2,
					 arg->sw_rawawb_rotu3_ls2,
					 arg->sw_rawawb_rotu4_ls2,
					 arg->sw_rawawb_rotu5_ls2),
			ISP21_RAWAWB_YUV_INTERP_CURVE_UCOOR_2);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_th0_ls2,
					  arg->sw_rawawb_th1_ls2),
			ISP21_RAWAWB_YUV_INTERP_CURVE_TH0_2);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_th2_ls2,
					  arg->sw_rawawb_th3_ls2),
			ISP21_RAWAWB_YUV_INTERP_CURVE_TH1_2);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_th4_ls2,
					  arg->sw_rawawb_th5_ls2),
			ISP21_RAWAWB_YUV_INTERP_CURVE_TH2_2);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_coor_x1_ls3_y,
					  arg->sw_rawawb_vec_x21_ls3_y),
			ISP21_RAWAWB_YUV_X_COOR_Y_3);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_coor_x1_ls3_u,
					  arg->sw_rawawb_vec_x21_ls3_u),
			ISP21_RAWAWB_YUV_X_COOR_U_3);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_coor_x1_ls3_v,
					  arg->sw_rawawb_vec_x21_ls3_v),
			ISP21_RAWAWB_YUV_X_COOR_V_3);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_4BYTE(arg->sw_rawawb_dis_x1x2_ls3,
					 0,
					 arg->sw_rawawb_rotu0_ls3,
					 arg->sw_rawawb_rotu1_ls3),
			ISP21_RAWAWB_YUV_X1X2_DIS_3);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_4BYTE(arg->sw_rawawb_rotu2_ls3,
					 arg->sw_rawawb_rotu3_ls3,
					 arg->sw_rawawb_rotu4_ls3,
					 arg->sw_rawawb_rotu5_ls3),
			ISP21_RAWAWB_YUV_INTERP_CURVE_UCOOR_3);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_th0_ls3,
					  arg->sw_rawawb_th1_ls3),
			ISP21_RAWAWB_YUV_INTERP_CURVE_TH0_3);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_th2_ls3,
					  arg->sw_rawawb_th3_ls3),
			ISP21_RAWAWB_YUV_INTERP_CURVE_TH1_3);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_th4_ls3,
					  arg->sw_rawawb_th5_ls3),
			ISP21_RAWAWB_YUV_INTERP_CURVE_TH2_3);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_wt0,
					  arg->sw_rawawb_wt1),
			ISP21_RAWAWB_RGB2XY_WT01);

	rkisp_iowrite32(params_vdev,
			arg->sw_rawawb_wt2,
			ISP21_RAWAWB_RGB2XY_WT2);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_mat0_x,
					  arg->sw_rawawb_mat0_y),
			ISP21_RAWAWB_RGB2XY_MAT0_XY);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_mat1_x,
					  arg->sw_rawawb_mat1_y),
			ISP21_RAWAWB_RGB2XY_MAT1_XY);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_mat2_x,
					  arg->sw_rawawb_mat2_y),
			ISP21_RAWAWB_RGB2XY_MAT2_XY);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_x0_0,
					  arg->sw_rawawb_nor_x1_0),
			ISP21_RAWAWB_XY_DETC_NOR_X_0);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_y0_0,
					  arg->sw_rawawb_nor_y1_0),
			ISP21_RAWAWB_XY_DETC_NOR_Y_0);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_big_x0_0,
					  arg->sw_rawawb_big_x1_0),
			ISP21_RAWAWB_XY_DETC_BIG_X_0);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_big_y0_0,
					  arg->sw_rawawb_big_y1_0),
			ISP21_RAWAWB_XY_DETC_BIG_Y_0);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_x0_1,
					  arg->sw_rawawb_nor_x1_1),
			ISP21_RAWAWB_XY_DETC_NOR_X_1);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_y0_1,
					  arg->sw_rawawb_nor_y1_1),
			ISP21_RAWAWB_XY_DETC_NOR_Y_1);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_big_x0_1,
					  arg->sw_rawawb_big_x1_1),
			ISP21_RAWAWB_XY_DETC_BIG_X_1);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_big_y0_1,
					  arg->sw_rawawb_big_y1_1),
			ISP21_RAWAWB_XY_DETC_BIG_Y_1);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_x0_2,
					  arg->sw_rawawb_nor_x1_2),
			ISP21_RAWAWB_XY_DETC_NOR_X_2);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_y0_2,
					  arg->sw_rawawb_nor_y1_2),
			ISP21_RAWAWB_XY_DETC_NOR_Y_2);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_big_x0_2,
					  arg->sw_rawawb_big_x1_2),
			ISP21_RAWAWB_XY_DETC_BIG_X_2);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_big_y0_2,
					  arg->sw_rawawb_big_y1_2),
			ISP21_RAWAWB_XY_DETC_BIG_Y_2);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_x0_3,
					  arg->sw_rawawb_nor_x1_3),
			ISP21_RAWAWB_XY_DETC_NOR_X_3);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_y0_3,
					  arg->sw_rawawb_nor_y1_3),
			ISP21_RAWAWB_XY_DETC_NOR_Y_3);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_big_x0_3,
					  arg->sw_rawawb_big_x1_3),
			ISP21_RAWAWB_XY_DETC_BIG_X_3);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_big_y0_3,
					  arg->sw_rawawb_big_y1_3),
			ISP21_RAWAWB_XY_DETC_BIG_Y_3);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_x0_4,
					  arg->sw_rawawb_nor_x1_4),
			ISP21_RAWAWB_XY_DETC_NOR_X_4);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_y0_4,
					  arg->sw_rawawb_nor_y1_4),
			ISP21_RAWAWB_XY_DETC_NOR_Y_4);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_big_x0_4,
					  arg->sw_rawawb_big_x1_4),
			ISP21_RAWAWB_XY_DETC_BIG_X_4);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_big_y0_4,
					  arg->sw_rawawb_big_y1_4),
			ISP21_RAWAWB_XY_DETC_BIG_Y_4);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_x0_5,
					  arg->sw_rawawb_nor_x1_5),
			ISP21_RAWAWB_XY_DETC_NOR_X_5);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_y0_5,
					  arg->sw_rawawb_nor_y1_5),
			ISP21_RAWAWB_XY_DETC_NOR_Y_5);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_big_x0_5,
					  arg->sw_rawawb_big_x1_5),
			ISP21_RAWAWB_XY_DETC_BIG_X_5);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_big_y0_5,
					  arg->sw_rawawb_big_y1_5),
			ISP21_RAWAWB_XY_DETC_BIG_Y_5);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_x0_6,
					  arg->sw_rawawb_nor_x1_6),
			ISP21_RAWAWB_XY_DETC_NOR_X_6);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_nor_y0_6,
					  arg->sw_rawawb_nor_y1_6),
			ISP21_RAWAWB_XY_DETC_NOR_Y_6);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_big_x0_6,
					  arg->sw_rawawb_big_x1_6),
			ISP21_RAWAWB_XY_DETC_BIG_X_6);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_big_y0_6,
					  arg->sw_rawawb_big_y1_6),
			ISP21_RAWAWB_XY_DETC_BIG_Y_6);

	rkisp_iowrite32(params_vdev,
			(arg->sw_rawawb_exc_wp_region0_excen0 & 0x1) << 0 |
			(arg->sw_rawawb_exc_wp_region0_excen1 & 0x1) << 1 |
			(arg->sw_rawawb_exc_wp_region0_domain & 0x1) << 3 |
			(arg->sw_rawawb_exc_wp_region1_excen0 & 0x1) << 4 |
			(arg->sw_rawawb_exc_wp_region1_excen1 & 0x1) << 5 |
			(arg->sw_rawawb_exc_wp_region1_domain & 0x1) << 7 |
			(arg->sw_rawawb_exc_wp_region2_excen0 & 0x1) << 8 |
			(arg->sw_rawawb_exc_wp_region2_excen1 & 0x1) << 9 |
			(arg->sw_rawawb_exc_wp_region2_domain & 0x1) << 11 |
			(arg->sw_rawawb_exc_wp_region3_excen0 & 0x1) << 12 |
			(arg->sw_rawawb_exc_wp_region3_excen1 & 0x1) << 13 |
			(arg->sw_rawawb_exc_wp_region3_domain & 0x1) << 15 |
			(arg->sw_rawawb_exc_wp_region4_excen0 & 0x1) << 16 |
			(arg->sw_rawawb_exc_wp_region4_excen1 & 0x1) << 17 |
			(arg->sw_rawawb_exc_wp_region4_domain & 0x1) << 19 |
			(arg->sw_rawawb_exc_wp_region5_excen0 & 0x1) << 20 |
			(arg->sw_rawawb_exc_wp_region5_excen1 & 0x1) << 21 |
			(arg->sw_rawawb_exc_wp_region5_domain & 0x1) << 23 |
			(arg->sw_rawawb_exc_wp_region6_excen0 & 0x1) << 24 |
			(arg->sw_rawawb_exc_wp_region6_excen1 & 0x1) << 25 |
			(arg->sw_rawawb_exc_wp_region6_domain & 0x1) << 27,
			ISP21_RAWAWB_MULTIWINDOW_EXC_CTRL);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region0_xu0,
					  arg->sw_rawawb_exc_wp_region0_xu1),
			ISP21_RAWAWB_EXC_WP_REGION0_XU);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region0_yv0,
					  arg->sw_rawawb_exc_wp_region0_yv1),
			ISP21_RAWAWB_EXC_WP_REGION0_YV);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region1_xu0,
					  arg->sw_rawawb_exc_wp_region1_xu1),
			ISP21_RAWAWB_EXC_WP_REGION1_XU);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region1_yv0,
					  arg->sw_rawawb_exc_wp_region1_yv1),
			ISP21_RAWAWB_EXC_WP_REGION1_YV);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region2_xu0,
					  arg->sw_rawawb_exc_wp_region2_xu1),
			ISP21_RAWAWB_EXC_WP_REGION2_XU);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region2_yv0,
					  arg->sw_rawawb_exc_wp_region2_yv1),
			ISP21_RAWAWB_EXC_WP_REGION2_YV);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region3_xu0,
					  arg->sw_rawawb_exc_wp_region3_xu1),
			ISP21_RAWAWB_EXC_WP_REGION3_XU);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region3_yv0,
					  arg->sw_rawawb_exc_wp_region3_yv1),
			ISP21_RAWAWB_EXC_WP_REGION3_YV);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region4_xu0,
					  arg->sw_rawawb_exc_wp_region4_xu1),
			ISP21_RAWAWB_EXC_WP_REGION4_XU);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region4_yv0,
					  arg->sw_rawawb_exc_wp_region4_yv1),
			ISP21_RAWAWB_EXC_WP_REGION4_YV);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region5_xu0,
					  arg->sw_rawawb_exc_wp_region5_xu1),
			ISP21_RAWAWB_EXC_WP_REGION5_XU);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region5_yv0,
					  arg->sw_rawawb_exc_wp_region5_yv1),
			ISP21_RAWAWB_EXC_WP_REGION5_YV);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region6_xu0,
					  arg->sw_rawawb_exc_wp_region6_xu1),
			ISP21_RAWAWB_EXC_WP_REGION6_XU);

	rkisp_iowrite32(params_vdev,
			ISP2X_PACK_2SHORT(arg->sw_rawawb_exc_wp_region6_yv0,
					  arg->sw_rawawb_exc_wp_region6_yv1),
			ISP21_RAWAWB_EXC_WP_REGION6_YV);

	for (i = 0; i < ISP21_RAWAWB_WEIGHT_NUM / 5; i++) {
		rkisp_iowrite32(params_vdev,
			(arg->sw_rawawb_wp_blk_wei_w[5 * i] & 0x3f) << 0 |
			(arg->sw_rawawb_wp_blk_wei_w[5 * i + 1] & 0x3f) << 6 |
			(arg->sw_rawawb_wp_blk_wei_w[5 * i + 2] & 0x3f) << 12 |
			(arg->sw_rawawb_wp_blk_wei_w[5 * i + 3] & 0x3f) << 18 |
			(arg->sw_rawawb_wp_blk_wei_w[5 * i + 4] & 0x3f) << 24,
			ISP21_RAWAWB_WRAM_DATA_BASE);
	}

	/* avoid to override the old enable value */
	value = rkisp_ioread32(params_vdev, ISP21_RAWAWB_CTRL);
	value &= ISP2X_RAWAWB_ENA;
	value &= ~ISP2X_REG_WR_MASK;
	rkisp_iowrite32(params_vdev,
			value |
			(arg->sw_rawawb_uv_en0 & 0x1) << 1 |
			(arg->sw_rawawb_xy_en0 & 0x1) << 2 |
			(arg->sw_rawawb_3dyuv_en0 & 0x1) << 3 |
			(arg->sw_rawawb_3dyuv_ls_idx0 & 0x7) << 4 |
			(arg->sw_rawawb_3dyuv_ls_idx1 & 0x7) << 7 |
			(arg->sw_rawawb_3dyuv_ls_idx2 & 0x7) << 10 |
			(arg->sw_rawawb_3dyuv_ls_idx3 & 0x7) << 13 |
			(arg->sw_rawawb_wind_size & 0x1) << 18 |
			(arg->sw_rawlsc_bypass_en & 0x1) << 19 |
			(arg->sw_rawawb_light_num & 0x7) << 20 |
			(arg->sw_rawawb_uv_en1 & 0x1) << 24 |
			(arg->sw_rawawb_xy_en1 & 0x1) << 25 |
			(arg->sw_rawawb_3dyuv_en1 & 0x1) << 26,
			ISP21_RAWAWB_CTRL);

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

	awb_ctrl = rkisp_ioread32(params_vdev, ISP21_RAWAWB_CTRL);
	awb_ctrl &= ~ISP2X_REG_WR_MASK;
	if (en)
		awb_ctrl |= ISP2X_RAWAWB_ENA;
	else
		awb_ctrl &= ~ISP2X_RAWAWB_ENA;

	rkisp_iowrite32(params_vdev, awb_ctrl, ISP21_RAWAWB_CTRL);
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
isp_rawhst1_config(struct rkisp_isp_params_vdev *params_vdev,
		   const struct isp2x_rawhistbig_cfg *arg)
{
	isp_rawhstbig_config(params_vdev, arg, 1);
}

static void
isp_rawhst1_enable(struct rkisp_isp_params_vdev *params_vdev,
		   bool en)
{
	isp_rawhstbig_enable(params_vdev, en, 1);
}

static void
isp_rawhst2_config(struct rkisp_isp_params_vdev *params_vdev,
		   const struct isp2x_rawhistbig_cfg *arg)
{
	isp_rawhstbig_config(params_vdev, arg, 2);
}

static void
isp_rawhst2_enable(struct rkisp_isp_params_vdev *params_vdev,
		   bool en)
{
	isp_rawhstbig_enable(params_vdev, en, 2);
}

static void
isp_rawhst3_config(struct rkisp_isp_params_vdev *params_vdev,
		   const struct isp2x_rawhistbig_cfg *arg)
{
	isp_rawhstbig_config(params_vdev, arg, 0);
}

static void
isp_rawhst3_enable(struct rkisp_isp_params_vdev *params_vdev,
		   bool en)
{
	isp_rawhstbig_enable(params_vdev, en, 0);
}

static void
isp_hdrmge_config(struct rkisp_isp_params_vdev *params_vdev,
		  const struct isp2x_hdrmge_cfg *arg, enum rkisp_params_type type)
{
	u32 value;
	int i;

	if (type == RKISP_PARAMS_SHD || type == RKISP_PARAMS_ALL) {
		value = ISP2X_PACK_2SHORT(arg->gain0, arg->gain0_inv);
		rkisp_iowrite32(params_vdev, value, ISP_HDRMGE_GAIN0);

		value = ISP2X_PACK_2SHORT(arg->gain1, arg->gain1_inv);
		rkisp_iowrite32(params_vdev, value, ISP_HDRMGE_GAIN1);

		value = arg->gain2;
		rkisp_iowrite32(params_vdev, value, ISP_HDRMGE_GAIN2);
	}

	if (type == RKISP_PARAMS_IMD || type == RKISP_PARAMS_ALL) {
		value = ISP2X_PACK_4BYTE(arg->ms_dif_0p8, arg->ms_diff_0p15,
					 arg->lm_dif_0p9, arg->lm_dif_0p15);
		rkisp_iowrite32(params_vdev, value, ISP_HDRMGE_CONS_DIFF);

		for (i = 0; i < ISP2X_HDRMGE_L_CURVE_NUM; i++) {
			value = ISP2X_PACK_2SHORT(arg->curve.curve_0[i], arg->curve.curve_1[i]);
			rkisp_iowrite32(params_vdev, value, ISP_HDRMGE_DIFF_Y0 + 4 * i);
		}

		for (i = 0; i < ISP2X_HDRMGE_E_CURVE_NUM; i++) {
			value = arg->e_y[i];
			rkisp_iowrite32(params_vdev, value, ISP_HDRMGE_OVER_Y0 + 4 * i);
		}
	}
}

static void
isp_hdrmge_enable(struct rkisp_isp_params_vdev *params_vdev,
		  bool en)
{
}

static void
isp_hdrdrc_config(struct rkisp_isp_params_vdev *params_vdev,
		  const struct isp21_drc_cfg *arg, enum rkisp_params_type type)
{
	u32 i, value;

	if (type == RKISP_PARAMS_IMD)
		return;

	value = (arg->sw_drc_offset_pow2 & 0x0F) << 28 |
		(arg->sw_drc_compres_scl & 0x1FFF) << 14 |
		(arg->sw_drc_position & 0x03FFF);
	rkisp_iowrite32(params_vdev, value, ISP21_DRC_CTRL1);

	value = (arg->sw_drc_delta_scalein & 0xFF) << 24 |
		(arg->sw_drc_hpdetail_ratio & 0xFFF) << 12 |
		(arg->sw_drc_lpdetail_ratio & 0xFFF);
	rkisp_iowrite32(params_vdev, value, ISP21_DRC_LPRATIO);

	value = ISP2X_PACK_4BYTE(0, 0, arg->sw_drc_weipre_frame, arg->sw_drc_weicur_pix);
	rkisp_iowrite32(params_vdev, value, ISP21_DRC_EXPLRATIO);

	value = (arg->sw_drc_force_sgm_inv0 & 0xFFFF) << 16 |
		(arg->sw_drc_motion_scl & 0xFF) << 8 |
		(arg->sw_drc_edge_scl & 0xFF);
	rkisp_iowrite32(params_vdev, value, ISP21_DRC_SIGMA);

	value = ISP2X_PACK_2SHORT(arg->sw_drc_space_sgm_inv0, arg->sw_drc_space_sgm_inv1);
	rkisp_iowrite32(params_vdev, value, ISP21_DRC_SPACESGM);

	value = ISP2X_PACK_2SHORT(arg->sw_drc_range_sgm_inv0, arg->sw_drc_range_sgm_inv1);
	rkisp_iowrite32(params_vdev, value, ISP21_DRC_RANESGM);

	value = ISP2X_PACK_4BYTE(arg->sw_drc_weig_bilat, arg->sw_drc_weig_maxl, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP21_DRC_BILAT);

	for (i = 0; i < ISP21_DRC_Y_NUM / 2; i++) {
		value = ISP2X_PACK_2SHORT(arg->sw_drc_gain_y[2 * i],
					  arg->sw_drc_gain_y[2 * i + 1]);
		rkisp_iowrite32(params_vdev, value, ISP21_DRC_GAIN_Y0 + 4 * i);
	}

	value = ISP2X_PACK_2SHORT(arg->sw_drc_gain_y[2 * i], 0);
	rkisp_iowrite32(params_vdev, value, ISP21_DRC_GAIN_Y0 + 4 * i);

	for (i = 0; i < ISP21_DRC_Y_NUM / 2; i++) {
		value = ISP2X_PACK_2SHORT(arg->sw_drc_compres_y[2 * i],
					  arg->sw_drc_compres_y[2 * i + 1]);
		rkisp_iowrite32(params_vdev, value, ISP21_DRC_COMPRES_Y0 + 4 * i);
	}

	value = ISP2X_PACK_2SHORT(arg->sw_drc_compres_y[2 * i], 0);
	rkisp_iowrite32(params_vdev, value, ISP21_DRC_COMPRES_Y0 + 4 * i);

	for (i = 0; i < ISP21_DRC_Y_NUM / 2; i++) {
		value = ISP2X_PACK_2SHORT(arg->sw_drc_scale_y[2 * i],
					  arg->sw_drc_scale_y[2 * i + 1]);
		rkisp_iowrite32(params_vdev, value, ISP21_DRC_SCALE_Y0 + 4 * i);
	}

	value = ISP2X_PACK_2SHORT(arg->sw_drc_scale_y[2 * i], 0);
	rkisp_iowrite32(params_vdev, value, ISP21_DRC_SCALE_Y0 + 4 * i);

	value = ISP2X_PACK_2SHORT(arg->sw_drc_min_ogain, arg->sw_drc_iir_weight);
	rkisp_iowrite32(params_vdev, value, ISP21_DRC_IIRWG_GAIN);
}

static void
isp_hdrdrc_enable(struct rkisp_isp_params_vdev *params_vdev,
		  bool en)
{
	u32 value;
	bool real_en;

	value = rkisp_ioread32(params_vdev, ISP21_DRC_CTRL0);
	real_en = !!(value & ISP_DRC_EN);
	if ((en && real_en) || (!en && !real_en))
		return;

	if (en) {
		value |= ISP_DRC_EN;
		rkisp_set_bits(params_vdev->dev, ISP_CTRL1,
			       ISP2X_SYS_ADRC_FST, ISP2X_SYS_ADRC_FST, false);
	} else {
		value = 0;
	}
	rkisp_iowrite32(params_vdev, value, ISP21_DRC_CTRL0);
}

static void
isp_gic_config(struct rkisp_isp_params_vdev *params_vdev,
	       const struct isp21_gic_cfg *arg)
{
	u32 value;
	s32 i;

	value = (arg->regmingradthrdark2 & 0x03FF) << 20 |
		(arg->regmingradthrdark1 & 0x03FF) << 10 |
		(arg->regminbusythre & 0x03FF);
	rkisp_iowrite32(params_vdev, value, ISP_GIC_DIFF_PARA1);

	value = (arg->regdarkthre & 0x07FF) << 21 |
		(arg->regmaxcorvboth & 0x03FF) << 11 |
		(arg->regdarktthrehi & 0x07FF);
	rkisp_iowrite32(params_vdev, value, ISP_GIC_DIFF_PARA2);

	value = (arg->regkgrad2dark & 0x0F) << 28 |
		(arg->regkgrad1dark & 0x0F) << 24 |
		(arg->regstrengthglobal_fix & 0xFF) << 16 |
		(arg->regdarkthrestep & 0x0F) << 12 |
		(arg->regkgrad2 & 0x0F) << 8 |
		(arg->regkgrad1 & 0x0F) << 4 |
		(arg->reggbthre & 0x0F);
	rkisp_iowrite32(params_vdev, value, ISP_GIC_DIFF_PARA3);

	value = (arg->regmaxcorv & 0x03FF) << 20 |
		(arg->regmingradthr2 & 0x03FF) << 10 |
		(arg->regmingradthr1 & 0x03FF);
	rkisp_iowrite32(params_vdev, value, ISP_GIC_DIFF_PARA4);

	value = (arg->gr_ratio & 0x03) << 28 |
		(arg->noise_scale & 0x7F) << 12 |
		(arg->noise_base & 0xFFF);
	rkisp_iowrite32(params_vdev, value, ISP_GIC_NOISE_PARA1);

	rkisp_iowrite32(params_vdev, arg->diff_clip, ISP_GIC_NOISE_PARA2);

	for (i = 0; i < ISP2X_GIC_SIGMA_Y_NUM / 2; i++) {
		value = ISP2X_PACK_2SHORT(arg->sigma_y[2 * i], arg->sigma_y[2 * i + 1]);
		rkisp_iowrite32(params_vdev, value, ISP_GIC_SIGMA_VALUE0 + 4 * i);
	}
	value = ISP2X_PACK_2SHORT(arg->sigma_y[2 * i], 0);
	rkisp_iowrite32(params_vdev, value, ISP_GIC_SIGMA_VALUE0 + 4 * i);
}

static void
isp_gic_enable(struct rkisp_isp_params_vdev *params_vdev,
	       bool en)
{
	u32 value = 0;

	if (en)
		value |= ISP_GIC_ENA;
	rkisp_iowrite32(params_vdev, value, ISP_GIC_CONTROL);
}

static void
isp_dhaz_config(struct rkisp_isp_params_vdev *params_vdev,
		const struct isp21_dhaz_cfg *arg)
{
	u32 i, value;

	value = rkisp_ioread32(params_vdev, ISP21_DHAZ_CTRL);
	value &= ISP_DHAZ_ENMUX;

	value |= (arg->enhance_en & 0x1) << 20 |
		 (arg->air_lc_en & 0x1) << 16 |
		 (arg->hpara_en & 0x1) << 12 |
		 (arg->hist_en & 0x1) << 8 |
		 (arg->dc_en & 0x1) << 4;
	rkisp_iowrite32(params_vdev, value, ISP21_DHAZ_CTRL);

	value = ISP2X_PACK_4BYTE(arg->dc_min_th, arg->dc_max_th,
				 arg->yhist_th, arg->yblk_th);
	rkisp_iowrite32(params_vdev, value, ISP21_DHAZ_ADP0);

	value = ISP2X_PACK_4BYTE(arg->bright_min, arg->bright_max,
				 arg->wt_max, 0);
	rkisp_iowrite32(params_vdev, value, ISP21_DHAZ_ADP1);

	value = ISP2X_PACK_4BYTE(arg->air_min, arg->air_max,
				 arg->dark_th, arg->tmax_base);
	rkisp_iowrite32(params_vdev, value, ISP21_DHAZ_ADP2);

	value = ISP2X_PACK_2SHORT(arg->tmax_off, arg->tmax_max);
	rkisp_iowrite32(params_vdev, value, ISP21_DHAZ_ADP_TMAX);

	value = (arg->hist_min & 0xFFFF) << 16 |
		(arg->hist_th_off & 0xFF) << 8 |
		(arg->hist_k & 0x1F);
	rkisp_iowrite32(params_vdev, value, ISP21_DHAZ_ADP_HIST0);

	value = ISP2X_PACK_2SHORT(arg->hist_scale, arg->hist_gratio);
	rkisp_iowrite32(params_vdev, value, ISP21_DHAZ_ADP_HIST1);

	value = ISP2X_PACK_2SHORT(arg->enhance_chroma, arg->enhance_value);
	rkisp_iowrite32(params_vdev, value, ISP21_DHAZ_ENHANCE);

	value = (arg->iir_wt_sigma & 0x07FF) << 16 |
		(arg->iir_sigma & 0xFF) << 8 |
		(arg->stab_fnum & 0x1F);
	rkisp_iowrite32(params_vdev, value, ISP21_DHAZ_IIR0);

	value = (arg->iir_pre_wet & 0x0F) << 24 |
		(arg->iir_tmax_sigma & 0x7FF) << 8 |
		(arg->iir_air_sigma & 0xFF);
	rkisp_iowrite32(params_vdev, value, ISP21_DHAZ_IIR1);

	value = (arg->cfg_wt & 0x01FF) << 16 |
		(arg->cfg_air & 0xFF) << 8 |
		(arg->cfg_alpha & 0xFF);
	rkisp_iowrite32(params_vdev, value, ISP21_DHAZ_SOFT_CFG0);

	value = ISP2X_PACK_2SHORT(arg->cfg_tmax, arg->cfg_gratio);
	rkisp_iowrite32(params_vdev, value, ISP21_DHAZ_SOFT_CFG1);

	value = (arg->range_sima & 0x01FF) << 16 |
		(arg->space_sigma_pre & 0xFF) << 8 |
		(arg->space_sigma_cur & 0xFF);
	rkisp_iowrite32(params_vdev, value, ISP21_DHAZ_BF_SIGMA);

	value = ISP2X_PACK_2SHORT(arg->bf_weight, arg->dc_weitcur);
	rkisp_iowrite32(params_vdev, value, ISP21_DHAZ_BF_WET);

	for (i = 0; i < ISP21_DHAZ_ENH_CURVE_NUM / 2; i++) {
		value = ISP2X_PACK_2SHORT(arg->enh_curve[2 * i], arg->enh_curve[2 * i + 1]);
		rkisp_iowrite32(params_vdev, value, ISP21_DHAZ_ENH_CURVE0 + 4 * i);
	}

	value = ISP2X_PACK_2SHORT(arg->enh_curve[2 * i], 0);
	rkisp_iowrite32(params_vdev, value, ISP21_DHAZ_ENH_CURVE0 + 4 * i);

	value = ISP2X_PACK_4BYTE(arg->gaus_h0, arg->gaus_h1, arg->gaus_h2, 0);
	rkisp_iowrite32(params_vdev, value, ISP21_DHAZ_GAUS);
}

static void
isp_dhaz_enable(struct rkisp_isp_params_vdev *params_vdev,
		bool en)
{
	u32 value;
	bool real_en;

	value = rkisp_ioread32(params_vdev, ISP21_DHAZ_CTRL);
	real_en = !!(value & ISP_DHAZ_ENMUX);
	if ((en && real_en) || (!en && !real_en))
		return;

	if (en) {
		value |= ISP21_SELF_FORCE_UPD | ISP_DHAZ_ENMUX;
		rkisp_set_bits(params_vdev->dev, ISP_CTRL1,
			       ISP2X_SYS_DHAZ_FST, ISP2X_SYS_DHAZ_FST, false);
	} else {
		value &= ~ISP_DHAZ_ENMUX;
	}

	rkisp_iowrite32(params_vdev, value, ISP21_DHAZ_CTRL);
}

static void
isp_3dlut_config(struct rkisp_isp_params_vdev *params_vdev,
		 const struct isp2x_3dlut_cfg *arg)
{
	struct rkisp_isp_params_val_v21 *priv_val;
	u32 value, buf_idx, i;
	u32 *data;

	priv_val = (struct rkisp_isp_params_val_v21 *)params_vdev->priv_val;
	buf_idx = (priv_val->buf_3dlut_idx++) % RKISP_PARAM_3DLUT_BUF_NUM;

	data = (u32 *)priv_val->buf_3dlut[buf_idx].vaddr;
	for (i = 0; i < arg->actual_size; i++)
		data[i] = (arg->lut_b[i] & 0x3FF) |
			  (arg->lut_g[i] & 0xFFF) << 10 |
			  (arg->lut_r[i] & 0x3FF) << 22;

	value = priv_val->buf_3dlut[buf_idx].dma_addr;
	rkisp_iowrite32(params_vdev, value, MI_LUT_3D_RD_BASE);
	rkisp_iowrite32(params_vdev, arg->actual_size, MI_LUT_3D_RD_WSIZE);

	value = rkisp_ioread32(params_vdev, ISP_3DLUT_CTRL);
	value &= ISP_3DLUT_EN;

	if (value)
		isp_param_set_bits(params_vdev, ISP_3DLUT_UPDATE, 0x01);

	if (arg->bypass_en)
		value |= ISP_3DLUT_BYPASS;

	rkisp_iowrite32(params_vdev, value, ISP_3DLUT_CTRL);
}

static void
isp_3dlut_enable(struct rkisp_isp_params_vdev *params_vdev,
		 bool en)
{
	u32 value;
	bool en_state;

	value = rkisp_ioread32(params_vdev, ISP_3DLUT_CTRL);
	en_state = (value & ISP_3DLUT_EN) ? true : false;

	if (en == en_state)
		return;

	if (en) {
		isp_param_set_bits(params_vdev, ISP_3DLUT_CTRL, 0x01);
		isp_param_set_bits(params_vdev, ISP_3DLUT_UPDATE, 0x01);
	} else {
		isp_param_clear_bits(params_vdev, ISP_3DLUT_CTRL, 0x01);
		isp_param_clear_bits(params_vdev, ISP_3DLUT_UPDATE, 0x01);
	}
}

static void
isp_ldch_config(struct rkisp_isp_params_vdev *params_vdev,
		const struct isp2x_ldch_cfg *arg)
{
	struct rkisp_device *dev = params_vdev->dev;
	struct rkisp_isp_params_val_v21 *priv_val;
	struct isp2x_ldch_head *ldch_head;
	int buf_idx, i;
	u32 value;

	priv_val = (struct rkisp_isp_params_val_v21 *)params_vdev->priv_val;
	for (i = 0; i < ISP2X_LDCH_BUF_NUM; i++) {
		if (arg->buf_fd == priv_val->buf_ldch[i].dma_fd)
			break;
	}
	if (i == ISP2X_LDCH_BUF_NUM) {
		dev_err(dev->dev, "cannot find ldch buf fd(%d)\n", arg->buf_fd);
		return;
	}

	if (!priv_val->buf_ldch[i].vaddr) {
		dev_err(dev->dev, "no ldch buffer allocated\n");
		return;
	}

	buf_idx = priv_val->buf_ldch_idx;
	ldch_head = (struct isp2x_ldch_head *)priv_val->buf_ldch[buf_idx].vaddr;
	ldch_head->stat = LDCH_BUF_INIT;

	buf_idx = i;
	ldch_head = (struct isp2x_ldch_head *)priv_val->buf_ldch[buf_idx].vaddr;
	ldch_head->stat = LDCH_BUF_CHIPINUSE;
	priv_val->buf_ldch_idx = buf_idx;

	value = priv_val->buf_ldch[buf_idx].dma_addr + ldch_head->data_oft;
	rkisp_iowrite32(params_vdev, value, MI_LUT_LDCH_RD_BASE);
	rkisp_iowrite32(params_vdev, arg->hsize, MI_LUT_LDCH_RD_H_WSIZE);
	rkisp_iowrite32(params_vdev, arg->vsize, MI_LUT_LDCH_RD_V_SIZE);
}

static void
isp_ldch_enable(struct rkisp_isp_params_vdev *params_vdev,
		bool en)
{
	struct rkisp_device *dev = params_vdev->dev;
	struct rkisp_isp_params_val_v21 *priv_val;
	u32 buf_idx;

	priv_val = (struct rkisp_isp_params_val_v21 *)params_vdev->priv_val;
	if (en) {
		buf_idx = priv_val->buf_ldch_idx;
		if (!priv_val->buf_ldch[buf_idx].vaddr) {
			dev_err(dev->dev, "no ldch buffer allocated\n");
			return;
		}
		isp_param_set_bits(params_vdev, ISP_LDCH_STS, 0x01);
	} else {
		isp_param_clear_bits(params_vdev, ISP_LDCH_STS, 0x01);
	}
}

static void
isp_ynr_config(struct rkisp_isp_params_vdev *params_vdev,
	       const struct isp21_ynr_cfg *arg)
{
	u32 i, value;

	value = rkisp_ioread32(params_vdev, ISP21_YNR_GLOBAL_CTRL);
	value &= ISP21_YNR_EN;

	value |= (arg->sw_ynr_thumb_mix_cur_en & 0x1) << 24 |
		 (arg->sw_ynr_global_gain_alpha & 0xF) << 20 |
		 (arg->sw_ynr_global_gain & 0x3FF) << 8 |
		 (arg->sw_ynr_flt1x1_bypass_sel & 0x3) << 6 |
		 (arg->sw_ynr_sft5x5_bypass & 0x1) << 5 |
		 (arg->sw_ynr_flt1x1_bypass & 0x1) << 4 |
		 (arg->sw_ynr_lgft3x3_bypass & 0x1) << 3 |
		 (arg->sw_ynr_lbft5x5_bypass & 0x1) << 2 |
		 (arg->sw_ynr_bft3x3_bypass & 0x1) << 1;
	rkisp_iowrite32(params_vdev, value, ISP21_YNR_GLOBAL_CTRL);

	rkisp_iowrite32(params_vdev, arg->sw_ynr_rnr_max_r, ISP21_YNR_RNR_MAX_R);

	value = ISP2X_PACK_2SHORT(arg->sw_ynr_low_bf_inv0, arg->sw_ynr_low_bf_inv1);
	rkisp_iowrite32(params_vdev, value, ISP21_YNR_LOWNR_CTRL0);

	value = ISP2X_PACK_2SHORT(arg->sw_ynr_low_thred_adj, arg->sw_ynr_low_peak_supress);
	rkisp_iowrite32(params_vdev, value, ISP21_YNR_LOWNR_CTRL1);

	value = ISP2X_PACK_2SHORT(arg->sw_ynr_low_edge_adj_thresh, arg->sw_ynr_low_dist_adj);
	rkisp_iowrite32(params_vdev, value, ISP21_YNR_LOWNR_CTRL2);

	value = (arg->sw_ynr_low_bi_weight & 0xFF) << 24 |
		(arg->sw_ynr_low_weight & 0xFF) << 16 |
		(arg->sw_ynr_low_center_weight & 0xFFFF);
	rkisp_iowrite32(params_vdev, value, ISP21_YNR_LOWNR_CTRL3);

	value = ISP2X_PACK_2SHORT(arg->sw_ynr_high_thred_adj, arg->sw_ynr_hi_min_adj);
	rkisp_iowrite32(params_vdev, value, ISP21_YNR_HIGHNR_CTRL0);

	value = ISP2X_PACK_2SHORT(arg->sw_ynr_hi_edge_thed, arg->sw_ynr_high_retain_weight);
	rkisp_iowrite32(params_vdev, value, ISP21_YNR_HIGHNR_CTRL1);

	value = ISP2X_PACK_4BYTE(arg->sw_ynr_base_filter_weight0,
				 arg->sw_ynr_base_filter_weight1,
				 arg->sw_ynr_base_filter_weight2,
				 0);
	rkisp_iowrite32(params_vdev, value, ISP21_YNR_HIGHNR_BASE_FILTER_WEIGHT);

	value = (arg->sw_ynr_low_gauss1_coeff2 & 0xFFFF) << 16 |
		(arg->sw_ynr_low_gauss1_coeff1 & 0xFF) << 8 |
		(arg->sw_ynr_low_gauss1_coeff0 & 0xFF);
	rkisp_iowrite32(params_vdev, value, ISP21_YNR_GAUSS1_COEFF);

	value = (arg->sw_ynr_low_gauss2_coeff2 & 0xFFFF) << 16 |
		(arg->sw_ynr_low_gauss2_coeff1 & 0xFF) << 8 |
		(arg->sw_ynr_low_gauss2_coeff0 & 0xFF);
	rkisp_iowrite32(params_vdev, value, ISP21_YNR_GAUSS2_COEFF);

	value = ISP2X_PACK_4BYTE(arg->sw_ynr_direction_weight0,
				 arg->sw_ynr_direction_weight1,
				 arg->sw_ynr_direction_weight2,
				 arg->sw_ynr_direction_weight3);
	rkisp_iowrite32(params_vdev, value, ISP21_YNR_DIRECTION_W_0_3);

	value = ISP2X_PACK_4BYTE(arg->sw_ynr_direction_weight4,
				 arg->sw_ynr_direction_weight5,
				 arg->sw_ynr_direction_weight6,
				 arg->sw_ynr_direction_weight7);
	rkisp_iowrite32(params_vdev, value, ISP21_YNR_DIRECTION_W_4_7);

	for (i = 0; i < ISP21_YNR_XY_NUM / 2; i++) {
		value = ISP2X_PACK_2SHORT(arg->sw_ynr_luma_points_x[2 * i],
					  arg->sw_ynr_luma_points_x[2 * i + 1]);
		rkisp_iowrite32(params_vdev, value, ISP21_YNR_SGM_DX_0_1 + 4 * i);
	}

	value = ISP2X_PACK_2SHORT(arg->sw_ynr_luma_points_x[2 * i], 0);
	rkisp_iowrite32(params_vdev, value, ISP21_YNR_SGM_DX_0_1 + 4 * i);

	for (i = 0; i < ISP21_YNR_XY_NUM / 2; i++) {
		value = ISP2X_PACK_2SHORT(arg->sw_ynr_lsgm_y[2 * i],
					  arg->sw_ynr_lsgm_y[2 * i + 1]);
		rkisp_iowrite32(params_vdev, value, ISP21_YNR_LSGM_Y_0_1 + 4 * i);
	}

	value = ISP2X_PACK_2SHORT(arg->sw_ynr_lsgm_y[2 * i], 0);
	rkisp_iowrite32(params_vdev, value, ISP21_YNR_LSGM_Y_0_1 + 4 * i);

	for (i = 0; i < ISP21_YNR_XY_NUM / 2; i++) {
		value = ISP2X_PACK_2SHORT(arg->sw_ynr_hsgm_y[2 * i],
					  arg->sw_ynr_hsgm_y[2 * i + 1]);
		rkisp_iowrite32(params_vdev, value, ISP21_YNR_HSGM_Y_0_1 + 4 * i);
	}

	value = ISP2X_PACK_2SHORT(arg->sw_ynr_hsgm_y[2 * i], 0);
	rkisp_iowrite32(params_vdev, value, ISP21_YNR_HSGM_Y_0_1 + 4 * i);

	for (i = 0; i < ISP21_YNR_XY_NUM / 4; i++) {
		value = ISP2X_PACK_4BYTE(arg->sw_ynr_rnr_strength3[4 * i],
					 arg->sw_ynr_rnr_strength3[4 * i + 1],
					 arg->sw_ynr_rnr_strength3[4 * i + 2],
					 arg->sw_ynr_rnr_strength3[4 * i + 3]);
		rkisp_iowrite32(params_vdev, value, ISP21_YNR_RNR_STRENGTH03 + 4 * i);
	}

	value = ISP2X_PACK_4BYTE(arg->sw_ynr_rnr_strength3[4 * i], 0, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP21_YNR_RNR_STRENGTH03 + 4 * i);
}

static void
isp_ynr_enable(struct rkisp_isp_params_vdev *params_vdev,
	       bool en, const struct isp21_ynr_cfg *arg)
{
	u32 ynr_ctrl, value = 0;
	bool real_en;

	if (arg) {
		value = (arg->sw_ynr_thumb_mix_cur_en & 0x1) << 24 |
			(arg->sw_ynr_global_gain_alpha & 0xF) << 20 |
			(arg->sw_ynr_global_gain & 0x3FF) << 8 |
			(arg->sw_ynr_flt1x1_bypass_sel & 0x3) << 6 |
			(arg->sw_ynr_sft5x5_bypass & 0x1) << 5 |
			(arg->sw_ynr_flt1x1_bypass & 0x1) << 4 |
			(arg->sw_ynr_lgft3x3_bypass & 0x1) << 3 |
			(arg->sw_ynr_lbft5x5_bypass & 0x1) << 2 |
			(arg->sw_ynr_bft3x3_bypass & 0x1) << 1;
	}

	ynr_ctrl = rkisp_ioread32(params_vdev, ISP21_YNR_GLOBAL_CTRL);
	real_en = !!(ynr_ctrl & ISP21_YNR_EN);
	if ((en && real_en) || (!en && !real_en))
		return;

	if (en) {
		value |= ISP21_YNR_EN;
		rkisp_set_bits(params_vdev->dev, ISP_CTRL1,
			       ISP2X_SYS_YNR_FST, ISP2X_SYS_YNR_FST, false);
	}

	rkisp_iowrite32(params_vdev, value, ISP21_YNR_GLOBAL_CTRL);
}

static void
isp_cnr_config(struct rkisp_isp_params_vdev *params_vdev,
	       const struct isp21_cnr_cfg *arg)
{
	u32 value;

	value = rkisp_ioread32(params_vdev, ISP21_CNR_CTRL);
	value &= ISP21_CNR_EN;

	value |= (arg->sw_cnr_thumb_mix_cur_en & 0x1) << 4 |
		 (arg->sw_cnr_lq_bila_bypass & 0x1) << 3 |
		 (arg->sw_cnr_hq_bila_bypass & 0x1) << 2 |
		 (arg->sw_cnr_exgain_bypass & 0x1) << 1;
	rkisp_iowrite32(params_vdev, value, ISP21_CNR_CTRL);

	rkisp_iowrite32(params_vdev, arg->sw_cnr_exgain_mux, ISP21_CNR_EXGAIN);

	value = ISP2X_PACK_4BYTE(arg->sw_cnr_gain_1sigma, arg->sw_cnr_gain_offset,
				 arg->sw_cnr_gain_iso, 0);
	rkisp_iowrite32(params_vdev, value, ISP21_CNR_GAIN_PARA);

	value = ISP2X_PACK_4BYTE(arg->sw_cnr_gain_uvgain0, arg->sw_cnr_gain_uvgain1, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP21_CNR_GAIN_UV_PARA);

	rkisp_iowrite32(params_vdev, arg->sw_cnr_lmed3_alpha, ISP21_CNR_LMED3);

	value = ISP2X_PACK_4BYTE(arg->sw_cnr_lbf5_gain_c, arg->sw_cnr_lbf5_gain_y, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP21_CNR_LBF5_GAIN);

	value = ISP2X_PACK_4BYTE(arg->sw_cnr_lbf5_weit_d0, arg->sw_cnr_lbf5_weit_d1,
				 arg->sw_cnr_lbf5_weit_d2, arg->sw_cnr_lbf5_weit_d3);
	rkisp_iowrite32(params_vdev, value, ISP21_CNR_LBF5_WEITD0_3);

	rkisp_iowrite32(params_vdev, arg->sw_cnr_lbf5_weit_d4, ISP21_CNR_LBF5_WEITD4);

	rkisp_iowrite32(params_vdev, arg->sw_cnr_hmed3_alpha, ISP21_CNR_HMED3);

	value = (arg->sw_cnr_hbf5_weit_src & 0xFF) << 24 |
		(arg->sw_cnr_hbf5_min_wgt & 0xFF) << 16 |
		(arg->sw_cnr_hbf5_sigma & 0xFFFF);
	rkisp_iowrite32(params_vdev, value, ISP21_CNR_HBF5);

	value = ISP2X_PACK_2SHORT(arg->sw_cnr_lbf3_sigma, arg->sw_cnr_lbf5_weit_src);
	rkisp_iowrite32(params_vdev, value, ISP21_CNR_LBF3);
}

static void
isp_cnr_enable(struct rkisp_isp_params_vdev *params_vdev,
	       bool en, const struct isp21_cnr_cfg *arg)
{
	u32 cnr_ctrl, value = 0;
	bool real_en;

	if (arg) {
		value = (arg->sw_cnr_thumb_mix_cur_en & 0x1) << 4 |
			(arg->sw_cnr_lq_bila_bypass & 0x1) << 3 |
			(arg->sw_cnr_hq_bila_bypass & 0x1) << 2 |
			(arg->sw_cnr_exgain_bypass & 0x1) << 1;
	}

	cnr_ctrl = rkisp_ioread32(params_vdev, ISP21_CNR_CTRL);
	real_en = !!(cnr_ctrl & ISP21_CNR_EN);
	if ((en && real_en) || (!en && !real_en))
		return;

	if (en) {
		value |= ISP21_CNR_EN;
		rkisp_set_bits(params_vdev->dev, ISP_CTRL1,
			       ISP2X_SYS_CNR_FST, ISP2X_SYS_CNR_FST, false);
	}

	rkisp_iowrite32(params_vdev, value, ISP21_CNR_CTRL);
}

static void
isp_sharp_config(struct rkisp_isp_params_vdev *params_vdev,
		 const struct isp21_sharp_cfg *arg)
{
	u32 value;

	value = rkisp_ioread32(params_vdev, ISP21_SHARP_SHARP_EN);
	value &= ISP21_SHARP_EN;

	value |= (arg->sw_sharp_bypass & 0x1) << 1;
	rkisp_iowrite32(params_vdev, value, ISP21_SHARP_SHARP_EN);

	value = ISP2X_PACK_4BYTE(arg->sw_sharp_pbf_ratio, arg->sw_sharp_gaus_ratio,
				 arg->sw_sharp_bf_ratio, arg->sw_sharp_sharp_ratio);
	rkisp_iowrite32(params_vdev, value, ISP21_SHARP_SHARP_RATIO);

	value = (arg->sw_sharp_luma_dx[6] & 0x0F) << 24 |
		(arg->sw_sharp_luma_dx[5] & 0x0F) << 20 |
		(arg->sw_sharp_luma_dx[4] & 0x0F) << 16 |
		(arg->sw_sharp_luma_dx[3] & 0x0F) << 12 |
		(arg->sw_sharp_luma_dx[2] & 0x0F) << 8 |
		(arg->sw_sharp_luma_dx[1] & 0x0F) << 4 |
		(arg->sw_sharp_luma_dx[0] & 0x0F);
	rkisp_iowrite32(params_vdev, value, ISP21_SHARP_SHARP_LUMA_DX);

	value = (arg->sw_sharp_pbf_sigma_inv[2] & 0x3FF) << 20 |
		(arg->sw_sharp_pbf_sigma_inv[1] & 0x3FF) << 10 |
		(arg->sw_sharp_pbf_sigma_inv[0] & 0x3FF);
	rkisp_iowrite32(params_vdev, value, ISP21_SHARP_SHARP_PBF_SIGMA_INV_0);

	value = (arg->sw_sharp_pbf_sigma_inv[5] & 0x3FF) << 20 |
		(arg->sw_sharp_pbf_sigma_inv[4] & 0x3FF) << 10 |
		(arg->sw_sharp_pbf_sigma_inv[3] & 0x3FF);
	rkisp_iowrite32(params_vdev, value, ISP21_SHARP_SHARP_PBF_SIGMA_INV_1);

	value = (arg->sw_sharp_pbf_sigma_inv[7] & 0x3FF) << 10 |
		(arg->sw_sharp_pbf_sigma_inv[6] & 0x3FF);
	rkisp_iowrite32(params_vdev, value, ISP21_SHARP_SHARP_PBF_SIGMA_INV_2);

	value = (arg->sw_sharp_bf_sigma_inv[2] & 0x3FF) << 20 |
		(arg->sw_sharp_bf_sigma_inv[1] & 0x3FF) << 10 |
		(arg->sw_sharp_bf_sigma_inv[0] & 0x3FF);
	rkisp_iowrite32(params_vdev, value, ISP21_SHARP_SHARP_BF_SIGMA_INV_0);

	value = (arg->sw_sharp_bf_sigma_inv[5] & 0x3FF) << 20 |
		(arg->sw_sharp_bf_sigma_inv[4] & 0x3FF) << 10 |
		(arg->sw_sharp_bf_sigma_inv[3] & 0x3FF);
	rkisp_iowrite32(params_vdev, value, ISP21_SHARP_SHARP_BF_SIGMA_INV_1);

	value = (arg->sw_sharp_bf_sigma_inv[7] & 0x3FF) << 10 |
		(arg->sw_sharp_bf_sigma_inv[6] & 0x3FF);
	rkisp_iowrite32(params_vdev, value, ISP21_SHARP_SHARP_BF_SIGMA_INV_2);

	value = (arg->sw_sharp_bf_sigma_shift & 0x0F) << 4 |
		(arg->sw_sharp_pbf_sigma_shift & 0x0F);
	rkisp_iowrite32(params_vdev, value, ISP21_SHARP_SHARP_SIGMA_SHIFT);

	value = (arg->sw_sharp_ehf_th[2] & 0x3FF) << 20 |
		(arg->sw_sharp_ehf_th[1] & 0x3FF) << 10 |
		(arg->sw_sharp_ehf_th[0] & 0x3FF);
	rkisp_iowrite32(params_vdev, value, ISP21_SHARP_SHARP_EHF_TH_0);

	value = (arg->sw_sharp_ehf_th[5] & 0x3FF) << 20 |
		(arg->sw_sharp_ehf_th[4] & 0x3FF) << 10 |
		(arg->sw_sharp_ehf_th[3] & 0x3FF);
	rkisp_iowrite32(params_vdev, value, ISP21_SHARP_SHARP_EHF_TH_1);

	value = (arg->sw_sharp_ehf_th[7] & 0x3FF) << 10 |
		(arg->sw_sharp_ehf_th[6] & 0x3FF);
	rkisp_iowrite32(params_vdev, value, ISP21_SHARP_SHARP_EHF_TH_2);

	value = (arg->sw_sharp_clip_hf[2] & 0x3FF) << 20 |
		(arg->sw_sharp_clip_hf[1] & 0x3FF) << 10 |
		(arg->sw_sharp_clip_hf[0] & 0x3FF);
	rkisp_iowrite32(params_vdev, value, ISP21_SHARP_SHARP_CLIP_HF_0);

	value = (arg->sw_sharp_clip_hf[5] & 0x3FF) << 20 |
		(arg->sw_sharp_clip_hf[4] & 0x3FF) << 10 |
		(arg->sw_sharp_clip_hf[3] & 0x3FF);
	rkisp_iowrite32(params_vdev, value, ISP21_SHARP_SHARP_CLIP_HF_1);

	value = (arg->sw_sharp_clip_hf[7] & 0x3FF) << 10 |
		(arg->sw_sharp_clip_hf[6] & 0x3FF);
	rkisp_iowrite32(params_vdev, value, ISP21_SHARP_SHARP_CLIP_HF_2);

	value = ISP2X_PACK_4BYTE(arg->sw_sharp_pbf_coef_0, arg->sw_sharp_pbf_coef_1,
				 arg->sw_sharp_pbf_coef_2, 0);
	rkisp_iowrite32(params_vdev, value, ISP21_SHARP_SHARP_PBF_COEF);

	value = ISP2X_PACK_4BYTE(arg->sw_sharp_bf_coef_0, arg->sw_sharp_bf_coef_1,
				 arg->sw_sharp_bf_coef_2, 0);
	rkisp_iowrite32(params_vdev, value, ISP21_SHARP_SHARP_BF_COEF);

	value = ISP2X_PACK_4BYTE(arg->sw_sharp_gaus_coef_0, arg->sw_sharp_gaus_coef_1,
				 arg->sw_sharp_gaus_coef_2, 0);
	rkisp_iowrite32(params_vdev, value, ISP21_SHARP_SHARP_GAUS_COEF);
}

static void
isp_sharp_enable(struct rkisp_isp_params_vdev *params_vdev,
		 bool en)
{
	u32 value;

	value = rkisp_ioread32(params_vdev, ISP21_SHARP_SHARP_EN);
	value &= ~ISP21_SHARP_EN;

	if (en)
		value |= ISP21_SHARP_EN;

	rkisp_iowrite32(params_vdev, value, ISP21_SHARP_SHARP_EN);
}

static void
isp_baynr_config(struct rkisp_isp_params_vdev *params_vdev,
		 const struct isp21_baynr_cfg *arg)
{
	u32 i, value;

	value = rkisp_ioread32(params_vdev, ISP21_BAYNR_CTRL);
	value &= ISP21_BAYNR_EN;

	value |= (arg->sw_baynr_gauss_en & 0x1) << 8 |
		 (arg->sw_baynr_log_bypass & 0x1) << 4;
	rkisp_iowrite32(params_vdev, value, ISP21_BAYNR_CTRL);

	value = ISP2X_PACK_2SHORT(arg->sw_baynr_dgain0, arg->sw_baynr_dgain1);
	rkisp_iowrite32(params_vdev, value, ISP21_BAYNR_DGAIN0);

	rkisp_iowrite32(params_vdev, arg->sw_baynr_dgain2, ISP21_BAYNR_DGAIN1);
	rkisp_iowrite32(params_vdev, arg->sw_baynr_pix_diff, ISP21_BAYNR_PIXDIFF);

	value = ISP2X_PACK_2SHORT(arg->sw_baynr_softthld, arg->sw_baynr_diff_thld);
	rkisp_iowrite32(params_vdev, value, ISP21_BAYNR_THLD);

	value = ISP2X_PACK_2SHORT(arg->sw_baynr_reg_w1, arg->sw_bltflt_streng);
	rkisp_iowrite32(params_vdev, value, ISP21_BAYNR_W1_STRENG);

	for (i = 0; i < ISP21_BAYNR_XY_NUM / 2; i++) {
		value = ISP2X_PACK_2SHORT(arg->sw_sigma_x[2 * i], arg->sw_sigma_x[2 * i + 1]);
		rkisp_iowrite32(params_vdev, value, ISP21_BAYNR_SIGMAX01 + 4 * i);
	}

	for (i = 0; i < ISP21_BAYNR_XY_NUM / 2; i++) {
		value = ISP2X_PACK_2SHORT(arg->sw_sigma_y[2 * i], arg->sw_sigma_y[2 * i + 1]);
		rkisp_iowrite32(params_vdev, value, ISP21_BAYNR_SIGMAY01 + 4 * i);
	}

	value = (arg->weit_d2 & 0x3FF) << 20 |
		(arg->weit_d1 & 0x3FF) << 10 |
		(arg->weit_d0 & 0x3FF);
	rkisp_iowrite32(params_vdev, value, ISP21_BAYNR_WRIT_D);
}

static void
isp_baynr_enable(struct rkisp_isp_params_vdev *params_vdev,
		 bool en)
{
	u32 value;

	value = rkisp_ioread32(params_vdev, ISP21_BAYNR_CTRL);
	value &= ~ISP21_BAYNR_EN;

	if (en)
		value |= ISP21_BAYNR_EN;

	rkisp_iowrite32(params_vdev, value, ISP21_BAYNR_CTRL);
}

static void
isp_bay3d_config(struct rkisp_isp_params_vdev *params_vdev,
		 const struct isp21_bay3d_cfg *arg)
{
	u32 i, value;

	value = rkisp_ioread32(params_vdev, ISP21_BAY3D_CTRL);
	value &= ISP21_BAY3D_EN;

	value |= (arg->sw_bay3d_exp_sel & 0x1) << 16 |
		 (arg->sw_bay3d_bypass_en & 0x1) << 12 |
		 (arg->sw_bay3d_pk_en & 0x1) << 4;
	rkisp_iowrite32(params_vdev, value, ISP21_BAY3D_CTRL);

	value = ISP2X_PACK_2SHORT(arg->sw_bay3d_sigratio, arg->sw_bay3d_softwgt);
	rkisp_iowrite32(params_vdev, value, ISP21_BAY3D_KALRATIO);

	rkisp_iowrite32(params_vdev, arg->sw_bay3d_glbpk2, ISP21_BAY3D_GLBPK2);

	value = ISP2X_PACK_2SHORT(arg->sw_bay3d_str, arg->sw_bay3d_exp_str);
	rkisp_iowrite32(params_vdev, value, ISP21_BAY3D_KALSTR);

	value = ISP2X_PACK_2SHORT(arg->sw_bay3d_wgtlmt_l, arg->sw_bay3d_wgtlmt_h);
	rkisp_iowrite32(params_vdev, value, ISP21_BAY3D_WGTLMT);

	for (i = 0; i < ISP21_BAY3D_XY_NUM / 2; i++) {
		value = ISP2X_PACK_2SHORT(arg->sw_bay3d_sig_x[2 * i],
					  arg->sw_bay3d_sig_x[2 * i + 1]);
		rkisp_iowrite32(params_vdev, value, ISP21_BAY3D_SIG_X0 + 4 * i);
	}

	for (i = 0; i < ISP21_BAY3D_XY_NUM / 2; i++) {
		value = ISP2X_PACK_2SHORT(arg->sw_bay3d_sig_y[2 * i],
					  arg->sw_bay3d_sig_y[2 * i + 1]);
		rkisp_iowrite32(params_vdev, value, ISP21_BAY3D_SIG_Y0 + 4 * i);
	}
}

static void
isp_bay3d_enable(struct rkisp_isp_params_vdev *params_vdev,
		 bool en)
{
	struct rkisp_device *ispdev = params_vdev->dev;
	struct rkisp_isp_params_val_v21 *priv_val;
	u32 value, bay3d_ctrl;

	priv_val = (struct rkisp_isp_params_val_v21 *)params_vdev->priv_val;
	bay3d_ctrl = rkisp_ioread32(params_vdev, ISP21_BAY3D_CTRL);
	if ((en && (bay3d_ctrl & ISP21_BAY3D_EN)) ||
	    (!en && !(bay3d_ctrl & ISP21_BAY3D_EN)))
		return;

	if (en) {
		if (!priv_val->buf_3dnr.size) {
			dev_err(ispdev->dev, "no bay3d buffer available\n");
			return;
		}

		value = priv_val->buf_3dnr.size;
		rkisp_iowrite32(params_vdev, value, ISP21_MI_BAY3D_WR_SIZE);
		value = priv_val->buf_3dnr.dma_addr;
		rkisp_iowrite32(params_vdev, value, ISP21_MI_BAY3D_WR_BASE);
		rkisp_iowrite32(params_vdev, value, ISP21_MI_BAY3D_RD_BASE);

		rkisp_set_bits(params_vdev->dev, MI_RD_CTRL2,
			       BAY3D_RW_ONEADDR_EN, BAY3D_RW_ONEADDR_EN, false);
		rkisp_set_bits(params_vdev->dev, MI_WR_CTRL2,
			       SW_BAY3D_WR_AUTOUPD | SW_BAY3D_FORCEUPD,
			       SW_BAY3D_WR_AUTOUPD | SW_BAY3D_FORCEUPD, false);

		rkisp_set_bits(params_vdev->dev, ISP_CTRL1,
			       ISP2X_SYS_BAY3D_FST, ISP2X_SYS_BAY3D_FST, false);

		bay3d_ctrl |= ISP21_BAY3D_EN;
	} else {
		bay3d_ctrl &= ~ISP21_BAY3D_EN;
	}

	rkisp_iowrite32(params_vdev, bay3d_ctrl, ISP21_BAY3D_CTRL);
}

static void
isp_csm_config(struct rkisp_isp_params_vdev *params_vdev,
	       bool full_range)
{
	const u16 full_range_coeff[] = {
		0x0026, 0x004b, 0x000f,
		0x01ea, 0x01d6, 0x0040,
		0x0040, 0x01ca, 0x01f6
	};
	const u16 limited_range_coeff[] = {
		0x0021, 0x0040, 0x000d,
		0x01ed, 0x01db, 0x0038,
		0x0038, 0x01d1, 0x01f7,
	};
	unsigned int i;

	if (full_range) {
		for (i = 0; i < ARRAY_SIZE(full_range_coeff); i++)
			rkisp_iowrite32(params_vdev, full_range_coeff[i],
					ISP_CC_COEFF_0 + i * 4);

		isp_param_set_bits(params_vdev, ISP_CTRL,
				   CIF_ISP_CTRL_ISP_CSM_Y_FULL_ENA |
				   CIF_ISP_CTRL_ISP_CSM_C_FULL_ENA);
	} else {
		for (i = 0; i < ARRAY_SIZE(limited_range_coeff); i++)
			rkisp_iowrite32(params_vdev, limited_range_coeff[i],
					CIF_ISP_CC_COEFF_0 + i * 4);

		isp_param_clear_bits(params_vdev, ISP_CTRL,
				     CIF_ISP_CTRL_ISP_CSM_Y_FULL_ENA |
				     CIF_ISP_CTRL_ISP_CSM_C_FULL_ENA);
	}
}

struct rkisp_isp_params_v21_ops rkisp_v21_isp_params_ops = {
	.dpcc_config = isp_dpcc_config,
	.dpcc_enable = isp_dpcc_enable,
	.bls_config = isp_bls_config,
	.bls_enable = isp_bls_enable,
	.sdg_config = isp_sdg_config,
	.sdg_enable = isp_sdg_enable,
	.lsc_config = isp_lsc_config,
	.lsc_enable = isp_lsc_enable,
	.awbgain_config = isp_awbgain_config,
	.awbgain_enable = isp_awbgain_enable,
	.debayer_config = isp_debayer_config,
	.debayer_enable = isp_debayer_enable,
	.ccm_config = isp_ccm_config,
	.ccm_enable = isp_ccm_enable,
	.goc_config = isp_goc_config,
	.goc_enable = isp_goc_enable,
	.cproc_config = isp_cproc_config,
	.cproc_enable = isp_cproc_enable,
	.ie_config = isp_ie_config,
	.ie_enable = isp_ie_enable,
	.rawaf_config = isp_rawaf_config,
	.rawaf_enable = isp_rawaf_enable,
	.rawae0_config = isp_rawaelite_config,
	.rawae0_enable = isp_rawaelite_enable,
	.rawae1_config = isp_rawae1_config,
	.rawae1_enable = isp_rawae1_enable,
	.rawae2_config = isp_rawae2_config,
	.rawae2_enable = isp_rawae2_enable,
	.rawae3_config = isp_rawae3_config,
	.rawae3_enable = isp_rawae3_enable,
	.rawawb_config = isp_rawawb_config,
	.rawawb_enable = isp_rawawb_enable,
	.rawhst0_config = isp_rawhstlite_config,
	.rawhst0_enable = isp_rawhstlite_enable,
	.rawhst1_config = isp_rawhst1_config,
	.rawhst1_enable = isp_rawhst1_enable,
	.rawhst2_config = isp_rawhst2_config,
	.rawhst2_enable = isp_rawhst2_enable,
	.rawhst3_config = isp_rawhst3_config,
	.rawhst3_enable = isp_rawhst3_enable,
	.hdrmge_config = isp_hdrmge_config,
	.hdrmge_enable = isp_hdrmge_enable,
	.hdrdrc_config = isp_hdrdrc_config,
	.hdrdrc_enable = isp_hdrdrc_enable,
	.gic_config = isp_gic_config,
	.gic_enable = isp_gic_enable,
	.dhaz_config = isp_dhaz_config,
	.dhaz_enable = isp_dhaz_enable,
	.isp3dlut_config = isp_3dlut_config,
	.isp3dlut_enable = isp_3dlut_enable,
	.ldch_config = isp_ldch_config,
	.ldch_enable = isp_ldch_enable,
	.ynr_config = isp_ynr_config,
	.ynr_enable = isp_ynr_enable,
	.cnr_config = isp_cnr_config,
	.cnr_enable = isp_cnr_enable,
	.sharp_config = isp_sharp_config,
	.sharp_enable = isp_sharp_enable,
	.baynr_config = isp_baynr_config,
	.baynr_enable = isp_baynr_enable,
	.bay3d_config = isp_bay3d_config,
	.bay3d_enable = isp_bay3d_enable,
	.csm_config = isp_csm_config,
};

static __maybe_unused
void __isp_isr_other_config(struct rkisp_isp_params_vdev *params_vdev,
			    const struct isp21_isp_params_cfg *new_params,
			    enum rkisp_params_type type)
{
	u64 module_en_update, module_cfg_update, module_ens;
	struct rkisp_isp_params_v21_ops *ops =
		(struct rkisp_isp_params_v21_ops *)params_vdev->priv_ops;
	struct rkisp_isp_params_val_v21 *priv_val =
		(struct rkisp_isp_params_val_v21 *)params_vdev->priv_val;
	struct rkisp_device *ispdev = params_vdev->dev;
	bool is_feature_on = ispdev->hw_dev->is_feature_on;
	u64 iq_feature = ispdev->hw_dev->iq_feature;

	module_en_update = new_params->module_en_update;
	module_cfg_update = new_params->module_cfg_update;
	module_ens = new_params->module_ens;

	if (is_feature_on) {
		module_en_update &= ~ISP2X_MODULE_HDRMGE;
		if (module_en_update & ~iq_feature) {
			dev_err(ispdev->dev,
				"some iq features(0x%llx, 0x%llx) are not supported\n",
				module_en_update, iq_feature);
			module_en_update &= iq_feature;
		}
	}

	if (type == RKISP_PARAMS_SHD) {
		if ((module_en_update & ISP2X_MODULE_HDRMGE) ||
		    (module_cfg_update & ISP2X_MODULE_HDRMGE)) {
			if ((module_cfg_update & ISP2X_MODULE_HDRMGE))
				ops->hdrmge_config(params_vdev,
					&new_params->others.hdrmge_cfg, type);

			if (module_en_update & ISP2X_MODULE_HDRMGE) {
				ops->hdrmge_enable(params_vdev,
					!!(module_ens & ISP2X_MODULE_HDRMGE));
				priv_val->mge_en = !!(module_ens & ISP2X_MODULE_HDRMGE);
			}
		}

		if ((module_en_update & ISP2X_MODULE_DRC) ||
		    (module_cfg_update & ISP2X_MODULE_DRC)) {
			if ((module_cfg_update & ISP2X_MODULE_DRC))
				ops->hdrdrc_config(params_vdev,
					&new_params->others.drc_cfg, type);

			if (module_en_update & ISP2X_MODULE_DRC)
				ops->hdrdrc_enable(params_vdev,
					!!(module_ens & ISP2X_MODULE_DRC));
		}

		return;
	}

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

		if (module_en_update & ISP2X_MODULE_LSC) {
			ops->lsc_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_LSC));
			priv_val->lsc_en = !!(module_ens & ISP2X_MODULE_LSC);
		}
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

	if ((module_en_update & ISP2X_MODULE_DEBAYER) ||
	    (module_cfg_update & ISP2X_MODULE_DEBAYER)) {
		if ((module_cfg_update & ISP2X_MODULE_DEBAYER))
			ops->debayer_config(params_vdev,
				&new_params->others.debayer_cfg);

		if (module_en_update & ISP2X_MODULE_DEBAYER)
			ops->debayer_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_DEBAYER));
	}

	if ((module_en_update & ISP2X_MODULE_CCM) ||
	    (module_cfg_update & ISP2X_MODULE_CCM)) {
		if ((module_cfg_update & ISP2X_MODULE_CCM))
			ops->ccm_config(params_vdev,
				&new_params->others.ccm_cfg);

		if (module_en_update & ISP2X_MODULE_CCM)
			ops->ccm_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_CCM));
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

	if ((module_en_update & ISP2X_MODULE_HDRMGE) ||
	    (module_cfg_update & ISP2X_MODULE_HDRMGE)) {
		if ((module_cfg_update & ISP2X_MODULE_HDRMGE))
			ops->hdrmge_config(params_vdev,
				&new_params->others.hdrmge_cfg, type);

		if (module_en_update & ISP2X_MODULE_HDRMGE) {
			ops->hdrmge_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_HDRMGE));
			priv_val->mge_en = !!(module_ens & ISP2X_MODULE_HDRMGE);
		}
	}

	if ((module_en_update & ISP2X_MODULE_DRC) ||
	    (module_cfg_update & ISP2X_MODULE_DRC)) {
		if ((module_cfg_update & ISP2X_MODULE_DRC))
			ops->hdrdrc_config(params_vdev,
				&new_params->others.drc_cfg, type);

		if (module_en_update & ISP2X_MODULE_DRC)
			ops->hdrdrc_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_DRC));
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

		if (module_en_update & ISP2X_MODULE_DHAZ) {
			ops->dhaz_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_DHAZ));
			priv_val->dhaz_en = !!(module_ens & ISP2X_MODULE_DHAZ);
		}
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

	if ((module_en_update & ISP2X_MODULE_LDCH) ||
	    (module_cfg_update & ISP2X_MODULE_LDCH)) {
		if ((module_cfg_update & ISP2X_MODULE_LDCH))
			ops->ldch_config(params_vdev,
				&new_params->others.ldch_cfg);

		if (module_en_update & ISP2X_MODULE_LDCH)
			ops->ldch_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_LDCH));
	}

	if ((module_en_update & ISP2X_MODULE_YNR) ||
	    (module_cfg_update & ISP2X_MODULE_YNR)) {
		if ((module_cfg_update & ISP2X_MODULE_YNR))
			ops->ynr_config(params_vdev,
				&new_params->others.ynr_cfg);

		if (module_en_update & ISP2X_MODULE_YNR)
			ops->ynr_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_YNR), &new_params->others.ynr_cfg);
	}

	if ((module_en_update & ISP2X_MODULE_CNR) ||
	    (module_cfg_update & ISP2X_MODULE_CNR)) {
		if ((module_cfg_update & ISP2X_MODULE_CNR))
			ops->cnr_config(params_vdev,
				&new_params->others.cnr_cfg);

		if (module_en_update & ISP2X_MODULE_CNR)
			ops->cnr_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_CNR), &new_params->others.cnr_cfg);
	}

	if ((module_en_update & ISP2X_MODULE_SHARP) ||
	    (module_cfg_update & ISP2X_MODULE_SHARP)) {
		if ((module_cfg_update & ISP2X_MODULE_SHARP))
			ops->sharp_config(params_vdev,
				&new_params->others.sharp_cfg);

		if (module_en_update & ISP2X_MODULE_SHARP)
			ops->sharp_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_SHARP));
	}

	if ((module_en_update & ISP2X_MODULE_BAYNR) ||
	    (module_cfg_update & ISP2X_MODULE_BAYNR)) {
		if ((module_cfg_update & ISP2X_MODULE_BAYNR))
			ops->baynr_config(params_vdev,
				&new_params->others.baynr_cfg);

		if (module_en_update & ISP2X_MODULE_BAYNR)
			ops->baynr_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_BAYNR));
	}

	if ((module_en_update & ISP2X_MODULE_BAY3D) ||
	    (module_cfg_update & ISP2X_MODULE_BAY3D)) {
		if ((module_cfg_update & ISP2X_MODULE_BAY3D))
			ops->bay3d_config(params_vdev,
				&new_params->others.bay3d_cfg);

		if (module_en_update & ISP2X_MODULE_BAY3D)
			ops->bay3d_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_BAY3D));
	}
}

static __maybe_unused
void __isp_isr_meas_config(struct rkisp_isp_params_vdev *params_vdev,
			   struct isp21_isp_params_cfg *new_params,
			   enum rkisp_params_type type)
{
	u64 module_en_update, module_cfg_update, module_ens;
	struct rkisp_isp_params_v21_ops *ops =
		(struct rkisp_isp_params_v21_ops *)params_vdev->priv_ops;
	struct rkisp_device *ispdev = params_vdev->dev;
	bool is_feature_on = ispdev->hw_dev->is_feature_on;
	u64 iq_feature = ispdev->hw_dev->iq_feature;

	if (type == RKISP_PARAMS_SHD)
		return;

	module_en_update = new_params->module_en_update;
	module_cfg_update = new_params->module_cfg_update;
	module_ens = new_params->module_ens;

	if (is_feature_on) {
		module_en_update &= ~ISP2X_MODULE_HDRMGE;
		if (module_en_update & ~iq_feature) {
			dev_err(ispdev->dev,
				"some iq features(0x%llx, 0x%llx) are not supported\n",
				module_en_update, iq_feature);
			module_en_update &= iq_feature;
		}
	}

	if ((module_en_update & ISP2X_MODULE_RAWAE0) ||
	    (module_cfg_update & ISP2X_MODULE_RAWAE0)) {
		if ((module_cfg_update & ISP2X_MODULE_RAWAE0))
			ops->rawae0_config(params_vdev,
				&new_params->meas.rawae0);

		if (module_en_update & ISP2X_MODULE_RAWAE0)
			ops->rawae0_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_RAWAE0));
	}

	if ((module_en_update & ISP2X_MODULE_RAWAE1) ||
	    (module_cfg_update & ISP2X_MODULE_RAWAE1)) {
		if ((module_cfg_update & ISP2X_MODULE_RAWAE1))
			ops->rawae1_config(params_vdev,
				&new_params->meas.rawae1);

		if (module_en_update & ISP2X_MODULE_RAWAE1)
			ops->rawae1_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_RAWAE1));
	}

	if ((module_en_update & ISP2X_MODULE_RAWAE2) ||
	    (module_cfg_update & ISP2X_MODULE_RAWAE2)) {
		if ((module_cfg_update & ISP2X_MODULE_RAWAE2))
			ops->rawae2_config(params_vdev,
				&new_params->meas.rawae2);

		if (module_en_update & ISP2X_MODULE_RAWAE2)
			ops->rawae2_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_RAWAE2));
	}

	if ((module_en_update & ISP2X_MODULE_RAWAE3) ||
	    (module_cfg_update & ISP2X_MODULE_RAWAE3)) {
		if ((module_cfg_update & ISP2X_MODULE_RAWAE3))
			ops->rawae3_config(params_vdev,
				&new_params->meas.rawae3);

		if (module_en_update & ISP2X_MODULE_RAWAE3)
			ops->rawae3_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_RAWAE3));
	}

	if ((module_en_update & ISP2X_MODULE_RAWHIST0) ||
	    (module_cfg_update & ISP2X_MODULE_RAWHIST0)) {
		if ((module_cfg_update & ISP2X_MODULE_RAWHIST0))
			ops->rawhst0_config(params_vdev,
				&new_params->meas.rawhist0);

		if (module_en_update & ISP2X_MODULE_RAWHIST0)
			ops->rawhst0_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_RAWHIST0));
	}

	if ((module_en_update & ISP2X_MODULE_RAWHIST1) ||
	    (module_cfg_update & ISP2X_MODULE_RAWHIST1)) {
		if ((module_cfg_update & ISP2X_MODULE_RAWHIST1))
			ops->rawhst1_config(params_vdev,
				&new_params->meas.rawhist1);

		if (module_en_update & ISP2X_MODULE_RAWHIST1)
			ops->rawhst1_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_RAWHIST1));
	}

	if ((module_en_update & ISP2X_MODULE_RAWHIST2) ||
	    (module_cfg_update & ISP2X_MODULE_RAWHIST2)) {
		if ((module_cfg_update & ISP2X_MODULE_RAWHIST2))
			ops->rawhst2_config(params_vdev,
				&new_params->meas.rawhist2);

		if (module_en_update & ISP2X_MODULE_RAWHIST2)
			ops->rawhst2_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_RAWHIST2));
	}

	if ((module_en_update & ISP2X_MODULE_RAWHIST3) ||
	    (module_cfg_update & ISP2X_MODULE_RAWHIST3)) {
		if ((module_cfg_update & ISP2X_MODULE_RAWHIST3))
			ops->rawhst3_config(params_vdev,
				&new_params->meas.rawhist3);

		if (module_en_update & ISP2X_MODULE_RAWHIST3)
			ops->rawhst3_enable(params_vdev,
				!!(module_ens & ISP2X_MODULE_RAWHIST3));
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
void __isp_config_hdrshd(struct rkisp_isp_params_vdev *params_vdev)
{
	struct rkisp_isp_params_v21_ops *ops =
		(struct rkisp_isp_params_v21_ops *)params_vdev->priv_ops;

	ops->hdrmge_config(params_vdev,
			   &params_vdev->last_hdrmge, RKISP_PARAMS_SHD);

	ops->hdrdrc_config(params_vdev,
			   &params_vdev->last_hdrdrc, RKISP_PARAMS_SHD);
}

static __maybe_unused
void __preisp_isr_update_hdrae_para(struct rkisp_isp_params_vdev *params_vdev,
				    struct isp21_isp_params_cfg *new_params)
{
}

static
void rkisp_params_cfgsram_v2x(struct rkisp_isp_params_vdev *params_vdev)
{
	isp_lsc_matrix_cfg_sram(params_vdev,
				&params_vdev->cur_lsccfg, true);
}

static void
rkisp_alloc_bay3d_buf(struct rkisp_isp_params_vdev *params_vdev,
		      const struct isp21_isp_params_cfg *new_params)
{
	struct rkisp_device *ispdev = params_vdev->dev;
	struct rkisp_isp_subdev *isp_sdev = &ispdev->isp_sdev;
	struct rkisp_isp_params_val_v21 *priv_val;
	u64 module_en_update, module_ens;
	u32 w, h, size;
	int ret;

	module_en_update = new_params->module_en_update;
	module_ens = new_params->module_ens;

	if ((module_en_update & ISP2X_MODULE_BAY3D) &&
	    (module_ens & ISP2X_MODULE_BAY3D)) {
		w = isp_sdev->in_crop.width;
		h = isp_sdev->in_crop.height;
		size = 2 * ALIGN((ALIGN(w, 16) + (w + 15) / 8) * h, 16);
		priv_val = (struct rkisp_isp_params_val_v21 *)params_vdev->priv_val;
		rkisp_free_buffer(ispdev, &priv_val->buf_3dnr);
		priv_val->buf_3dnr.size = size;
		ret = rkisp_alloc_buffer(ispdev, &priv_val->buf_3dnr);
		if (ret)
			dev_err(ispdev->dev, "can not alloc bay3d buffer\n");
	}
}

/* Not called when the camera active, thus not isr protection. */
static void
rkisp_params_first_cfg_v2x(struct rkisp_isp_params_vdev *params_vdev)
{
	struct device *dev = params_vdev->dev->dev;
	struct rkisp_isp_params_val_v21 *priv_val =
		(struct rkisp_isp_params_val_v21 *)params_vdev->priv_val;
	struct rkisp_hw_dev *hw = params_vdev->dev->hw_dev;
	struct v4l2_rect *out_crop = &params_vdev->dev->isp_sdev.out_crop;
	u32 width = hw->max_in.w ? hw->max_in.w : out_crop->width;
	u32 size = hw->max_in.w ? hw->max_in.w * hw->max_in.h : isp_param_get_insize(params_vdev);

	rkisp_alloc_bay3d_buf(params_vdev, params_vdev->isp21_params);
	spin_lock(&params_vdev->config_lock);
	/* override the default things */
	if (!params_vdev->isp21_params->module_cfg_update &&
	    !params_vdev->isp21_params->module_en_update)
		dev_warn(dev, "can not get first iq setting in stream on\n");

	priv_val->dhaz_en = 0;
	priv_val->wdr_en = 0;
	priv_val->tmo_en = 0;
	priv_val->lsc_en = 0;
	priv_val->mge_en = 0;
	__isp_isr_other_config(params_vdev, params_vdev->isp21_params, RKISP_PARAMS_ALL);
	__isp_isr_meas_config(params_vdev, params_vdev->isp21_params, RKISP_PARAMS_ALL);
	__preisp_isr_update_hdrae_para(params_vdev, params_vdev->isp21_params);
	if (width <= ISP2X_AUTO_BIGMODE_WIDTH && size > ISP2X_NOBIG_OVERFLOW_SIZE) {
		rkisp_set_bits(params_vdev->dev, ISP_CTRL1,
			       ISP2X_SYS_BIGMODE_MANUAL | ISP2X_SYS_BIGMODE_FORCEEN,
			       ISP2X_SYS_BIGMODE_MANUAL | ISP2X_SYS_BIGMODE_FORCEEN, false);
	}

	params_vdev->cur_hdrmge = params_vdev->isp21_params->others.hdrmge_cfg;
	params_vdev->cur_hdrdrc = params_vdev->isp21_params->others.drc_cfg;
	params_vdev->last_hdrmge = params_vdev->cur_hdrmge;
	params_vdev->last_hdrdrc = params_vdev->cur_hdrdrc;
	spin_unlock(&params_vdev->config_lock);
}

static void rkisp_save_first_param_v2x(struct rkisp_isp_params_vdev *params_vdev, void *param)
{
	struct isp21_isp_params_cfg *new_params;

	new_params = (struct isp21_isp_params_cfg *)param;
	*params_vdev->isp21_params = *new_params;
}

static void rkisp_clear_first_param_v2x(struct rkisp_isp_params_vdev *params_vdev)
{
	params_vdev->isp21_params->module_cfg_update = 0;
	params_vdev->isp21_params->module_en_update = 0;
}

static u32 rkisp_get_ldch_meshsize(struct rkisp_isp_params_vdev *params_vdev,
				   struct rkisp_ldchbuf_size *ldchsize)
{
	int mesh_w, mesh_h, map_align;

	mesh_w = ((ldchsize->meas_width + (1 << 4) - 1) >> 4) + 1;
	mesh_h = ((ldchsize->meas_height + (1 << 3) - 1) >> 3) + 1;

	map_align = ((mesh_w + 1) >> 1) << 1;
	return map_align * mesh_h;
}

static void rkisp_deinit_ldch_buf(struct rkisp_isp_params_vdev *params_vdev)
{
	struct rkisp_isp_params_val_v21 *priv_val;
	int i;

	priv_val = params_vdev->priv_val;
	if (!priv_val)
		return;

	for (i = 0; i < ISP2X_LDCH_BUF_NUM; i++)
		rkisp_free_buffer(params_vdev->dev, &priv_val->buf_ldch[i]);
}

static int rkisp_init_ldch_buf(struct rkisp_isp_params_vdev *params_vdev,
			       struct rkisp_ldchbuf_size *ldchsize)
{
	struct device *dev = params_vdev->dev->dev;
	struct rkisp_isp_params_val_v21 *priv_val;
	struct isp2x_ldch_head *ldch_head;
	u32 mesh_size, buf_size;
	int i, ret;

	priv_val = params_vdev->priv_val;
	if (!priv_val) {
		dev_err(dev, "priv_val is NULL\n");
		return -EINVAL;
	}

	priv_val->buf_ldch_idx = 0;
	mesh_size = rkisp_get_ldch_meshsize(params_vdev, ldchsize);
	buf_size = PAGE_ALIGN(mesh_size * sizeof(u16) + ALIGN(sizeof(struct isp2x_ldch_head), 16));
	for (i = 0; i < ISP2X_LDCH_BUF_NUM; i++) {
		priv_val->buf_ldch[i].is_need_vaddr = true;
		priv_val->buf_ldch[i].is_need_dbuf = true;
		priv_val->buf_ldch[i].is_need_dmafd = true;
		priv_val->buf_ldch[i].size = buf_size;
		ret = rkisp_alloc_buffer(params_vdev->dev, &priv_val->buf_ldch[i]);
		if (ret) {
			dev_err(dev, "can not alloc buffer\n");
			goto err;
		}

		ldch_head = (struct isp2x_ldch_head *)priv_val->buf_ldch[i].vaddr;
		ldch_head->stat = LDCH_BUF_INIT;
		ldch_head->data_oft = ALIGN(sizeof(struct isp2x_ldch_head), 16);
	}

	return 0;

err:
	rkisp_deinit_ldch_buf(params_vdev);

	return -ENOMEM;

}

static void
rkisp_get_param_size_v2x(struct rkisp_isp_params_vdev *params_vdev,
			 unsigned int sizes[])
{
	sizes[0] = sizeof(struct isp2x_isp_params_cfg);
}

static void
rkisp_params_get_ldchbuf_inf_v2x(struct rkisp_isp_params_vdev *params_vdev,
				 struct rkisp_ldchbuf_info *ldchbuf)
{
	struct rkisp_isp_params_val_v21 *priv_val;
	int i;

	priv_val = params_vdev->priv_val;
	for (i = 0; i < ISP2X_LDCH_BUF_NUM; i++) {
		ldchbuf->buf_fd[i] = priv_val->buf_ldch[i].dma_fd;
		ldchbuf->buf_size[i] = priv_val->buf_ldch[i].size;
	}
}

static void
rkisp_params_set_ldchbuf_size_v2x(struct rkisp_isp_params_vdev *params_vdev,
				  struct rkisp_ldchbuf_size *ldchsize)
{
	rkisp_deinit_ldch_buf(params_vdev);
	rkisp_init_ldch_buf(params_vdev, ldchsize);
}

static void
rkisp_params_stream_stop_v2x(struct rkisp_isp_params_vdev *params_vdev)
{
	struct rkisp_device *ispdev = params_vdev->dev;
	struct rkisp_isp_params_val_v21 *priv_val;

	priv_val = (struct rkisp_isp_params_val_v21 *)params_vdev->priv_val;
	rkisp_free_buffer(ispdev, &priv_val->buf_3dnr);
}

static void
rkisp_params_fop_release_v2x(struct rkisp_isp_params_vdev *params_vdev)
{
	rkisp_deinit_ldch_buf(params_vdev);
}

/* Not called when the camera active, thus not isr protection. */
static void
rkisp_params_disable_isp_v2x(struct rkisp_isp_params_vdev *params_vdev)
{
	struct rkisp_isp_params_v21_ops *ops =
		(struct rkisp_isp_params_v21_ops *)params_vdev->priv_ops;

	ops->dpcc_enable(params_vdev, false);
	ops->bls_enable(params_vdev, false);
	ops->sdg_enable(params_vdev, false);
	ops->lsc_enable(params_vdev, false);
	ops->awbgain_enable(params_vdev, false);
	ops->debayer_enable(params_vdev, false);
	ops->ccm_enable(params_vdev, false);
	ops->goc_enable(params_vdev, false);
	ops->cproc_enable(params_vdev, false);
	ops->ie_enable(params_vdev, false);
	ops->rawaf_enable(params_vdev, false);
	ops->rawae0_enable(params_vdev, false);
	ops->rawae1_enable(params_vdev, false);
	ops->rawae2_enable(params_vdev, false);
	ops->rawae3_enable(params_vdev, false);
	ops->rawawb_enable(params_vdev, false);
	ops->rawhst0_enable(params_vdev, false);
	ops->rawhst1_enable(params_vdev, false);
	ops->rawhst2_enable(params_vdev, false);
	ops->rawhst3_enable(params_vdev, false);
	ops->hdrmge_enable(params_vdev, false);
	ops->hdrdrc_enable(params_vdev, false);
	ops->gic_enable(params_vdev, false);
	ops->dhaz_enable(params_vdev, false);
	ops->isp3dlut_enable(params_vdev, false);
	ops->ldch_enable(params_vdev, false);
	ops->ynr_enable(params_vdev, false, NULL);
	ops->cnr_enable(params_vdev, false, NULL);
	ops->sharp_enable(params_vdev, false);
	ops->baynr_enable(params_vdev, false);
	ops->bay3d_enable(params_vdev, false);
}

static void
rkisp_params_cfg_v2x(struct rkisp_isp_params_vdev *params_vdev,
		     u32 frame_id, enum rkisp_params_type type)
{
	struct isp21_isp_params_cfg *new_params = NULL;
	struct rkisp_buffer *cur_buf = params_vdev->cur_buf;
	struct rkisp_device *dev = params_vdev->dev;
	struct rkisp_hw_dev *hw_dev = dev->hw_dev;

	spin_lock(&params_vdev->config_lock);
	if (!params_vdev->streamon)
		goto unlock;

	/* get buffer by frame_id */
	while (!list_empty(&params_vdev->params) && !cur_buf) {
		cur_buf = list_first_entry(&params_vdev->params,
				struct rkisp_buffer, queue);

		if (!IS_HDR_RDBK(dev->rd_mode)) {
			list_del(&cur_buf->queue);
			break;
		}

		new_params = (struct isp21_isp_params_cfg *)(cur_buf->vaddr[0]);
		if (new_params->frame_id < frame_id) {
			list_del(&cur_buf->queue);
			vb2_buffer_done(&cur_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
			cur_buf = NULL;
			continue;
		} else if (new_params->frame_id == frame_id) {
			list_del(&cur_buf->queue);
		} else {
			cur_buf = NULL;
		}
		break;
	}

	if (!cur_buf)
		goto unlock;

	new_params = (struct isp21_isp_params_cfg *)(cur_buf->vaddr[0]);
	__isp_isr_other_config(params_vdev, new_params, type);
	__isp_isr_meas_config(params_vdev, new_params, type);
	if (!hw_dev->is_single && type != RKISP_PARAMS_SHD)
		__isp_config_hdrshd(params_vdev);

	if (type != RKISP_PARAMS_IMD) {
		params_vdev->last_hdrmge = params_vdev->cur_hdrmge;
		params_vdev->last_hdrdrc = params_vdev->cur_hdrdrc;
		params_vdev->cur_hdrmge = new_params->others.hdrmge_cfg;
		params_vdev->cur_hdrdrc = new_params->others.drc_cfg;
		vb2_buffer_done(&cur_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		cur_buf = NULL;
	}

unlock:
	params_vdev->cur_buf = cur_buf;
	spin_unlock(&params_vdev->config_lock);
}

static void
rkisp_params_clear_fstflg(struct rkisp_isp_params_vdev *params_vdev)
{
	u32 value;

	value = rkisp_ioread32(params_vdev, ISP_CTRL1);
	if (value & ISP2X_SYS_YNR_FST)
		rkisp_clear_bits(params_vdev->dev, ISP_CTRL1,
			   ISP2X_SYS_YNR_FST, false);
	if (value & ISP2X_SYS_ADRC_FST)
		rkisp_clear_bits(params_vdev->dev, ISP_CTRL1,
			   ISP2X_SYS_ADRC_FST, false);
	if (value & ISP2X_SYS_DHAZ_FST)
		rkisp_clear_bits(params_vdev->dev, ISP_CTRL1,
			   ISP2X_SYS_DHAZ_FST, false);
	if (value & ISP2X_SYS_CNR_FST)
		rkisp_clear_bits(params_vdev->dev, ISP_CTRL1,
			   ISP2X_SYS_CNR_FST, false);
	if (value & ISP2X_SYS_BAY3D_FST)
		rkisp_clear_bits(params_vdev->dev, ISP_CTRL1,
			   ISP2X_SYS_BAY3D_FST, false);
}

static void
rkisp_params_isr_v2x(struct rkisp_isp_params_vdev *params_vdev,
		     u32 isp_mis)
{
	struct rkisp_device *dev = params_vdev->dev;
	u32 cur_frame_id;

	rkisp_dmarx_get_frame(dev, &cur_frame_id, NULL, NULL, true);
	if (isp_mis & CIF_ISP_V_START) {
		if (params_vdev->rdbk_times)
			params_vdev->rdbk_times--;
		if (!params_vdev->cur_buf)
			return;

		if (IS_HDR_RDBK(dev->rd_mode) && !params_vdev->rdbk_times) {
			rkisp_params_cfg_v2x(params_vdev, cur_frame_id, RKISP_PARAMS_SHD);
			return;
		}
	}

	if (isp_mis & CIF_ISP_FRAME)
		rkisp_params_clear_fstflg(params_vdev);

	if ((isp_mis & CIF_ISP_FRAME) && !IS_HDR_RDBK(dev->rd_mode))
		rkisp_params_cfg_v2x(params_vdev, cur_frame_id, RKISP_PARAMS_ALL);
}

static struct rkisp_isp_params_ops rkisp_isp_params_ops_tbl = {
	.save_first_param = rkisp_save_first_param_v2x,
	.clear_first_param = rkisp_clear_first_param_v2x,
	.get_param_size = rkisp_get_param_size_v2x,
	.first_cfg = rkisp_params_first_cfg_v2x,
	.disable_isp = rkisp_params_disable_isp_v2x,
	.isr_hdl = rkisp_params_isr_v2x,
	.param_cfg = rkisp_params_cfg_v2x,
	.param_cfgsram = rkisp_params_cfgsram_v2x,
	.get_ldchbuf_inf = rkisp_params_get_ldchbuf_inf_v2x,
	.set_ldchbuf_size = rkisp_params_set_ldchbuf_size_v2x,
	.stream_stop = rkisp_params_stream_stop_v2x,
	.fop_release = rkisp_params_fop_release_v2x,
};

int rkisp_init_params_vdev_v21(struct rkisp_isp_params_vdev *params_vdev)
{
	struct device *dev = params_vdev->dev->dev;
	struct rkisp_isp_params_val_v21 *priv_val;
	int i, ret;

	priv_val = kzalloc(sizeof(*priv_val), GFP_KERNEL);
	if (!priv_val)
		return -ENOMEM;

	params_vdev->isp21_params = vmalloc(sizeof(*params_vdev->isp21_params));
	if (!params_vdev->isp21_params) {
		kfree(priv_val);
		return -ENOMEM;
	}

	priv_val->buf_3dlut_idx = 0;
	for (i = 0; i < RKISP_PARAM_3DLUT_BUF_NUM; i++) {
		priv_val->buf_3dlut[i].is_need_vaddr = true;
		priv_val->buf_3dlut[i].size = RKISP_PARAM_3DLUT_BUF_SIZE;
		ret = rkisp_alloc_buffer(params_vdev->dev, &priv_val->buf_3dlut[i]);
		if (ret) {
			dev_err(dev, "can not alloc buffer\n");
			goto err;
		}
	}

	priv_val->buf_lsclut_idx = 0;
	for (i = 0; i < RKISP_PARAM_LSC_LUT_BUF_NUM; i++) {
		priv_val->buf_lsclut[i].is_need_vaddr = true;
		priv_val->buf_lsclut[i].size = RKISP_PARAM_LSC_LUT_BUF_SIZE;
		ret = rkisp_alloc_buffer(params_vdev->dev, &priv_val->buf_lsclut[i]);
		if (ret) {
			dev_err(dev, "can not alloc buffer\n");
			goto err;
		}
	}

	params_vdev->priv_val = (void *)priv_val;
	params_vdev->ops = &rkisp_isp_params_ops_tbl;
	params_vdev->priv_ops = &rkisp_v21_isp_params_ops;
	rkisp_clear_first_param_v2x(params_vdev);
	return 0;

err:
	for (i = 0; i < RKISP_PARAM_3DLUT_BUF_NUM; i++)
		rkisp_free_buffer(params_vdev->dev, &priv_val->buf_3dlut[i]);

	for (i = 0; i < RKISP_PARAM_LSC_LUT_BUF_NUM; i++)
		rkisp_free_buffer(params_vdev->dev, &priv_val->buf_lsclut[i]);
	vfree(params_vdev->isp21_params);

	return ret;
}

void rkisp_uninit_params_vdev_v21(struct rkisp_isp_params_vdev *params_vdev)
{
	struct rkisp_isp_params_val_v21 *priv_val;
	int i;

	priv_val = params_vdev->priv_val;
	if (!priv_val)
		return;

	rkisp_deinit_ldch_buf(params_vdev);
	for (i = 0; i < RKISP_PARAM_3DLUT_BUF_NUM; i++)
		rkisp_free_buffer(params_vdev->dev, &priv_val->buf_3dlut[i]);

	for (i = 0; i < RKISP_PARAM_LSC_LUT_BUF_NUM; i++)
		rkisp_free_buffer(params_vdev->dev, &priv_val->buf_lsclut[i]);
	vfree(params_vdev->isp21_params);
	kfree(priv_val);
	params_vdev->priv_val = NULL;
}


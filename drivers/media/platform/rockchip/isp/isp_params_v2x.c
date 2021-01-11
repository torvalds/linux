// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>   /* for ISP params */
#include <linux/delay.h>
#include <linux/rk-preisp.h>
#include <linux/slab.h>
#include "dev.h"
#include "regs.h"
#include "regs_v2x.h"
#include "isp_params_v2x.h"

#define ISP2X_PACK_4BYTE(a, b, c, d)	\
	(((a) & 0xFF) << 0 | ((b) & 0xFF) << 8 | \
	 ((c) & 0xFF) << 16 | ((d) & 0xFF) << 24)

#define ISP2X_PACK_2SHORT(a, b)	\
	(((a) & 0xFFFF) << 0 | ((b) & 0xFFFF) << 16)

#define ISP2X_REG_WR_MASK		BIT(31) //disable write protect
#define ISP2X_NOBIG_OVERFLOW_SIZE	(2560 * 1440)

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
	u32 height = isp_sdev->in_crop.height;

	if (dev->isp_ver == ISP_V20 &&
	    dev->rd_mode == HDR_RDBK_FRAME1)
		height += RKMODULE_EXTEND_LINE;

	return isp_sdev->in_crop.width * height;
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
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_MODE);

	value = (arg->sw_rk_out_sel & 0x03) << 5 |
		(arg->sw_dpcc_output_sel & 0x01) << 4 |
		(arg->stage1_rb_3x3 & 0x01) << 3 |
		(arg->stage1_g_3x3 & 0x01) << 2 |
		(arg->stage1_incl_rb_center & 0x01) << 1 |
		(arg->stage1_incl_green_center & 0x01);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_OUTPUT_MODE);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_OUTPUT_MODE);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_OUTPUT_MODE);

	value = (arg->stage1_use_fix_set & 0x01) << 3 |
		(arg->stage1_use_set_3 & 0x01) << 2 |
		(arg->stage1_use_set_2 & 0x01) << 1 |
		(arg->stage1_use_set_1 & 0x01);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_SET_USE);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_SET_USE);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_SET_USE);

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
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_METHODS_SET_1);

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
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_METHODS_SET_2);

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
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_METHODS_SET_3);

	value = ISP2X_PACK_4BYTE(arg->line_thr_1_g, arg->line_thr_1_rb,
				 arg->sw_mindis1_g, arg->sw_mindis1_rb);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_LINE_THRESH_1);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_LINE_THRESH_1);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_LINE_THRESH_1);

	value = ISP2X_PACK_4BYTE(arg->line_mad_fac_1_g, arg->line_mad_fac_1_rb,
				 arg->sw_dis_scale_max1, arg->sw_dis_scale_min1);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_LINE_MAD_FAC_1);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_LINE_MAD_FAC_1);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_LINE_MAD_FAC_1);

	value = ISP2X_PACK_4BYTE(arg->pg_fac_1_g, arg->pg_fac_1_rb, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_PG_FAC_1);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_PG_FAC_1);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_PG_FAC_1);

	value = ISP2X_PACK_4BYTE(arg->rnd_thr_1_g, arg->rnd_thr_1_rb, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_RND_THRESH_1);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_RND_THRESH_1);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_RND_THRESH_1);

	value = ISP2X_PACK_4BYTE(arg->rg_fac_1_g, arg->rg_fac_1_rb, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_RG_FAC_1);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_RG_FAC_1);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_RG_FAC_1);

	value = ISP2X_PACK_4BYTE(arg->line_thr_2_g, arg->line_thr_2_rb,
				 arg->sw_mindis2_g, arg->sw_mindis2_rb);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_LINE_THRESH_2);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_LINE_THRESH_2);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_LINE_THRESH_2);

	value = ISP2X_PACK_4BYTE(arg->line_mad_fac_2_g, arg->line_mad_fac_2_rb,
				 arg->sw_dis_scale_max2, arg->sw_dis_scale_min2);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_LINE_MAD_FAC_2);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_LINE_MAD_FAC_2);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_LINE_MAD_FAC_2);

	value = ISP2X_PACK_4BYTE(arg->pg_fac_2_g, arg->pg_fac_2_rb, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_PG_FAC_2);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_PG_FAC_2);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_PG_FAC_2);

	value = ISP2X_PACK_4BYTE(arg->rnd_thr_2_g, arg->rnd_thr_2_rb, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_RND_THRESH_2);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_RND_THRESH_2);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_RND_THRESH_2);

	value = ISP2X_PACK_4BYTE(arg->rg_fac_2_g, arg->rg_fac_2_rb, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_RG_FAC_2);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_RG_FAC_2);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_RG_FAC_2);

	value = ISP2X_PACK_4BYTE(arg->line_thr_3_g, arg->line_thr_3_rb,
				 arg->sw_mindis3_g, arg->sw_mindis3_rb);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_LINE_THRESH_3);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_LINE_THRESH_3);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_LINE_THRESH_3);

	value = ISP2X_PACK_4BYTE(arg->line_mad_fac_3_g, arg->line_mad_fac_3_rb,
				 arg->sw_dis_scale_max3, arg->sw_dis_scale_min3);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_LINE_MAD_FAC_3);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_LINE_MAD_FAC_3);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_LINE_MAD_FAC_3);

	value = ISP2X_PACK_4BYTE(arg->pg_fac_3_g, arg->pg_fac_3_rb, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_PG_FAC_3);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_PG_FAC_3);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_PG_FAC_3);

	value = ISP2X_PACK_4BYTE(arg->rnd_thr_3_g, arg->rnd_thr_3_rb, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_RND_THRESH_3);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_RND_THRESH_3);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_RND_THRESH_3);

	value = ISP2X_PACK_4BYTE(arg->rg_fac_3_g, arg->rg_fac_3_rb, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_RG_FAC_3);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_RG_FAC_3);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_RG_FAC_3);

	value = (arg->ro_lim_3_rb & 0x03) << 10 |
		(arg->ro_lim_3_g & 0x03) << 8 |
		(arg->ro_lim_2_rb & 0x03) << 6 |
		(arg->ro_lim_2_g & 0x03) << 4 |
		(arg->ro_lim_1_rb & 0x03) << 2 |
		(arg->ro_lim_1_g & 0x03);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_RO_LIMITS);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_RO_LIMITS);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_RO_LIMITS);

	value = (arg->rnd_offs_3_rb & 0x03) << 10 |
		(arg->rnd_offs_3_g & 0x03) << 8 |
		(arg->rnd_offs_2_rb & 0x03) << 6 |
		(arg->rnd_offs_2_g & 0x03) << 4 |
		(arg->rnd_offs_1_rb & 0x03) << 2 |
		(arg->rnd_offs_1_g & 0x03);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_RND_OFFS);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_RND_OFFS);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_RND_OFFS);

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
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_BPT_CTRL);

	rkisp_iowrite32(params_vdev, arg->bp_number, ISP_DPCC0_BPT_NUMBER);
	rkisp_iowrite32(params_vdev, arg->bp_number, ISP_DPCC1_BPT_NUMBER);
	rkisp_iowrite32(params_vdev, arg->bp_number, ISP_DPCC2_BPT_NUMBER);
	rkisp_iowrite32(params_vdev, arg->bp_table_addr, ISP_DPCC0_BPT_ADDR);
	rkisp_iowrite32(params_vdev, arg->bp_table_addr, ISP_DPCC1_BPT_ADDR);
	rkisp_iowrite32(params_vdev, arg->bp_table_addr, ISP_DPCC2_BPT_ADDR);

	value = ISP2X_PACK_2SHORT(arg->bpt_h_addr, arg->bpt_v_addr);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_BPT_DATA);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_BPT_DATA);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_BPT_DATA);

	rkisp_iowrite32(params_vdev, arg->bp_cnt, ISP_DPCC0_BP_CNT);
	rkisp_iowrite32(params_vdev, arg->bp_cnt, ISP_DPCC1_BP_CNT);
	rkisp_iowrite32(params_vdev, arg->bp_cnt, ISP_DPCC2_BP_CNT);

	rkisp_iowrite32(params_vdev, arg->sw_pdaf_en, ISP_DPCC0_PDAF_EN);
	rkisp_iowrite32(params_vdev, arg->sw_pdaf_en, ISP_DPCC1_PDAF_EN);
	rkisp_iowrite32(params_vdev, arg->sw_pdaf_en, ISP_DPCC2_PDAF_EN);

	value = 0;
	for (i = 0; i < ISP2X_DPCC_PDAF_POINT_NUM; i++)
		value |= (arg->pdaf_point_en[i] & 0x01) << i;
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_PDAF_POINT_EN);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_PDAF_POINT_EN);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_PDAF_POINT_EN);

	value = ISP2X_PACK_2SHORT(arg->pdaf_offsetx, arg->pdaf_offsety);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_PDAF_OFFSET);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_PDAF_OFFSET);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_PDAF_OFFSET);

	value = ISP2X_PACK_2SHORT(arg->pdaf_wrapx, arg->pdaf_wrapy);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_PDAF_WRAP);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_PDAF_WRAP);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_PDAF_WRAP);

	value = ISP2X_PACK_2SHORT(arg->pdaf_wrapx_num, arg->pdaf_wrapy_num);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC0_PDAF_SCOPE);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC1_PDAF_SCOPE);
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_PDAF_SCOPE);

	for (i = 0; i < ISP2X_DPCC_PDAF_POINT_NUM / 2; i++) {
		value = ISP2X_PACK_4BYTE(arg->point[2 * i].x, arg->point[2 * i].y,
					 arg->point[2 * i + 1].x, arg->point[2 * i + 1].y);
		rkisp_iowrite32(params_vdev, value, ISP_DPCC0_PDAF_POINT_0 + 4 * i);
		rkisp_iowrite32(params_vdev, value, ISP_DPCC1_PDAF_POINT_0 + 4 * i);
		rkisp_iowrite32(params_vdev, value, ISP_DPCC2_PDAF_POINT_0 + 4 * i);
	}

	rkisp_iowrite32(params_vdev, arg->pdaf_forward_med, ISP_DPCC0_PDAF_FORWARD_MED);
	rkisp_iowrite32(params_vdev, arg->pdaf_forward_med, ISP_DPCC1_PDAF_FORWARD_MED);
	rkisp_iowrite32(params_vdev, arg->pdaf_forward_med, ISP_DPCC2_PDAF_FORWARD_MED);
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
	rkisp_iowrite32(params_vdev, value, ISP_DPCC2_MODE);
}

static void
isp_bls_config(struct rkisp_isp_params_vdev *params_vdev,
	       const struct isp2x_bls_cfg *arg)
{
	/* avoid to override the old enable value */
	u32 new_control;

	new_control = rkisp_ioread32(params_vdev, ISP_BLS_CTRL);
	new_control &= ISP_BLS_ENA;
	/* fixed subtraction values */
	if (!arg->enable_auto) {
		const struct isp2x_bls_fixed_val *pval = &arg->fixed_val;

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
			rkisp_iowrite32(params_vdev, arg->bls_window2.h_offs + arg->bls_window2.h_size,
					ISP_BLS_H2_STOP);
			rkisp_iowrite32(params_vdev, arg->bls_window2.v_offs,
					ISP_BLS_V2_START);
			rkisp_iowrite32(params_vdev, arg->bls_window2.v_offs + arg->bls_window2.v_size,
					ISP_BLS_V2_STOP);
			new_control |= ISP_BLS_WINDOW_2;
		}

		if (arg->en_windows & BIT(0)) {
			rkisp_iowrite32(params_vdev, arg->bls_window1.h_offs,
					ISP_BLS_H1_START);
			rkisp_iowrite32(params_vdev, arg->bls_window1.h_offs + arg->bls_window1.h_size,
					ISP_BLS_H1_STOP);
			rkisp_iowrite32(params_vdev, arg->bls_window1.v_offs,
					ISP_BLS_V1_START);
			rkisp_iowrite32(params_vdev, arg->bls_window1.v_offs + arg->bls_window1.v_size,
					ISP_BLS_V1_STOP);
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
	for (i = 0; i < ISP2X_SIHIST_WIN_NUM; i++) {
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
	struct rkisp_isp_params_val_v2x *priv_val;
	u32 data, buf_idx, *vaddr[4], index[4];
	void *buf_vaddr;
	int i, j;

	memset(&index[0], 0, sizeof(index));
	priv_val = (struct rkisp_isp_params_val_v2x *)params_vdev->priv_val;
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
	rkisp_prepare_buffer(params_vdev->dev, &priv_val->buf_lsclut[buf_idx]);
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
		if (!IS_HDR_RDBK(dev->rd_mode))
			lsc_ctrl |= ISP_LSC_LUT_EN;
		isp_param_set_bits(params_vdev, ISP_LSC_CTRL, lsc_ctrl);
	} else {
		isp_param_clear_bits(params_vdev, ISP_LSC_CTRL, ISP_LSC_EN);
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
isp_awbgain_config(struct rkisp_isp_params_vdev *params_vdev,
		   const struct isp2x_awb_gain_cfg *arg)
{
	struct rkisp_device *dev = params_vdev->dev;

	if (!arg->gain_green_r || !arg->gain_green_b ||
	    !arg->gain_red || !arg->gain_blue) {
		dev_err(dev->dev, "awb gain is zero!\n");
		return;
	}

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
	if (en)
		isp_param_set_bits(params_vdev, CIF_ISP_CTRL,
				   CIF_ISP_CTRL_ISP_AWB_ENA);
	else
		isp_param_clear_bits(params_vdev, CIF_ISP_CTRL,
				     CIF_ISP_CTRL_ISP_AWB_ENA);
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
isp_ccm_config(struct rkisp_isp_params_vdev *params_vdev,
	       const struct isp2x_ccm_cfg *arg)
{
	u32 value;
	u32 i;

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
	struct isp2x_isp_other_cfg *cur_other_cfg =
		&params_vdev->isp2x_params->others;
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
	} else {
		rkisp_iowrite32(params_vdev,
				reg_val, CIF_ISP_AWB_PROP_V10);
	}
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
	int i;

	for (i = 0; i < ISP2X_WDR_SIZE; i++) {
		if (i <= 39)
			rkisp_iowrite32(params_vdev, arg->c_wdr[i],
					ISP_WDR_CTRL + i * 4);
		else
			rkisp_iowrite32(params_vdev, arg->c_wdr[i],
					ISP_WDR_CTRL0 + (i - 40) * 4);
	}
}

static void
isp_wdr_enable(struct rkisp_isp_params_vdev *params_vdev,
	       bool en)
{
	if (en)
		rkisp_iowrite32(params_vdev, 0x030cf1,
				ISP_WDR_CTRL0);
	else
		rkisp_iowrite32(params_vdev, 0x030cf0,
				ISP_WDR_CTRL0);
}

static void
isp_iesharp_config(struct rkisp_isp_params_vdev *params_vdev,
		   const struct isp2x_rkiesharp_cfg *arg)
{
	u32 i;
	u32 val;
	u32 eff_ctrl;
	u32 minmax[5];

	val = CIF_ISP_PACK_4BYTE(arg->yavg_thr[0],
				 arg->yavg_thr[1],
				 arg->yavg_thr[2],
				 arg->yavg_thr[3]);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_YAVG_THR);

	val = CIF_ISP_PACK_4BYTE(arg->delta1[0],
				 arg->delta2[0],
				 arg->delta1[1],
				 arg->delta2[1]);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_DELTA_P0_P1);

	val = CIF_ISP_PACK_4BYTE(arg->delta1[2],
				 arg->delta2[2],
				 arg->delta1[3],
				 arg->delta2[3]);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_DELTA_P2_P3);

	val = CIF_ISP_PACK_4BYTE(arg->delta1[4],
				 arg->delta2[4],
				 0,
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_DELTA_P4);

	for (i = 0; i < 5; i++)
		minmax[i] = arg->minnumber[i] << 4 | arg->maxnumber[i];
	val = CIF_ISP_PACK_4BYTE(minmax[0],
				 minmax[1],
				 minmax[2],
				 minmax[3]);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_NPIXEL_P0_P1_P2_P3);
	rkisp_iowrite32(params_vdev, minmax[4],
			 CIF_RKSHARP_NPIXEL_P4);

	val = CIF_ISP_PACK_4BYTE(arg->gauss_flat_coe[0],
				 arg->gauss_flat_coe[1],
				 arg->gauss_flat_coe[2],
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_GAUSS_FLAT_COE1);

	val = CIF_ISP_PACK_4BYTE(arg->gauss_flat_coe[3],
				 arg->gauss_flat_coe[4],
				 arg->gauss_flat_coe[5],
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_GAUSS_FLAT_COE2);

	val = CIF_ISP_PACK_4BYTE(arg->gauss_flat_coe[6],
				 arg->gauss_flat_coe[7],
				 arg->gauss_flat_coe[8],
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_GAUSS_FLAT_COE3);

	val = CIF_ISP_PACK_4BYTE(arg->gauss_noise_coe[0],
				 arg->gauss_noise_coe[1],
				 arg->gauss_noise_coe[2],
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_GAUSS_NOISE_COE1);

	val = CIF_ISP_PACK_4BYTE(arg->gauss_noise_coe[3],
				 arg->gauss_noise_coe[4],
				 arg->gauss_noise_coe[5],
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_GAUSS_NOISE_COE2);

	val = CIF_ISP_PACK_4BYTE(arg->gauss_noise_coe[6],
				 arg->gauss_noise_coe[7],
				 arg->gauss_noise_coe[8],
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_GAUSS_NOISE_COE3);

	val = CIF_ISP_PACK_4BYTE(arg->gauss_other_coe[0],
				 arg->gauss_other_coe[1],
				 arg->gauss_other_coe[2],
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_GAUSS_OTHER_COE1);

	val = CIF_ISP_PACK_4BYTE(arg->gauss_other_coe[3],
				 arg->gauss_other_coe[4],
				 arg->gauss_other_coe[5],
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_GAUSS_OTHER_COE2);

	val = CIF_ISP_PACK_4BYTE(arg->gauss_other_coe[6],
				 arg->gauss_other_coe[7],
				 arg->gauss_other_coe[8],
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_GAUSS_OTHER_COE3);

	val = CIF_ISP_PACK_4BYTE(arg->line1_filter_coe[0],
				 arg->line1_filter_coe[1],
				 arg->line1_filter_coe[2],
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_LINE1_FILTER_COE1);

	val = CIF_ISP_PACK_4BYTE(arg->line1_filter_coe[3],
				 arg->line1_filter_coe[4],
				 arg->line1_filter_coe[5],
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_LINE1_FILTER_COE2);

	val = CIF_ISP_PACK_4BYTE(arg->line2_filter_coe[0],
				 arg->line2_filter_coe[1],
				 arg->line2_filter_coe[2],
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_LINE2_FILTER_COE1);

	val = CIF_ISP_PACK_4BYTE(arg->line2_filter_coe[3],
				 arg->line2_filter_coe[4],
				 arg->line2_filter_coe[5],
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_LINE2_FILTER_COE2);

	val = CIF_ISP_PACK_4BYTE(arg->line2_filter_coe[6],
				 arg->line2_filter_coe[7],
				 arg->line2_filter_coe[8],
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_LINE2_FILTER_COE3);

	val = CIF_ISP_PACK_4BYTE(arg->line3_filter_coe[0],
				 arg->line3_filter_coe[1],
				 arg->line3_filter_coe[2],
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_LINE3_FILTER_COE1);

	val = CIF_ISP_PACK_4BYTE(arg->line3_filter_coe[3],
				 arg->line3_filter_coe[4],
				 arg->line3_filter_coe[5],
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_LINE3_FILTER_COE2);

	val = CIF_ISP_PACK_2SHORT(arg->grad_seq[0],
				  arg->grad_seq[1]);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_GRAD_SEQ_P0_P1);

	val = CIF_ISP_PACK_2SHORT(arg->grad_seq[2],
				  arg->grad_seq[3]);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_GRAD_SEQ_P2_P3);

	val = CIF_ISP_PACK_4BYTE(arg->sharp_factor[0],
				 arg->sharp_factor[1],
				 arg->sharp_factor[2],
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_SHARP_FACTOR_P0_P1_P2);

	val = CIF_ISP_PACK_4BYTE(arg->sharp_factor[3],
				 arg->sharp_factor[4],
				 0,
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_SHARP_FACTOR_P3_P4);

	val = CIF_ISP_PACK_4BYTE(arg->uv_gauss_flat_coe[0],
				 arg->uv_gauss_flat_coe[1],
				 arg->uv_gauss_flat_coe[2],
				 arg->uv_gauss_flat_coe[3]);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_UV_GAUSS_FLAT_COE11_COE14);

	val = CIF_ISP_PACK_4BYTE(arg->uv_gauss_flat_coe[4],
				 arg->uv_gauss_flat_coe[5],
				 arg->uv_gauss_flat_coe[6],
				 arg->uv_gauss_flat_coe[7]);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_UV_GAUSS_FLAT_COE15_COE23);

	val = CIF_ISP_PACK_4BYTE(arg->uv_gauss_flat_coe[8],
				 arg->uv_gauss_flat_coe[9],
				 arg->uv_gauss_flat_coe[10],
				 arg->uv_gauss_flat_coe[11]);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_UV_GAUSS_FLAT_COE24_COE32);

	val = CIF_ISP_PACK_4BYTE(arg->uv_gauss_flat_coe[12],
				 arg->uv_gauss_flat_coe[13],
				 arg->uv_gauss_flat_coe[14],
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_UV_GAUSS_FLAT_COE33_COE35);

	val = CIF_ISP_PACK_4BYTE(arg->uv_gauss_noise_coe[0],
				 arg->uv_gauss_noise_coe[1],
				 arg->uv_gauss_noise_coe[2],
				 arg->uv_gauss_noise_coe[3]);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_UV_GAUSS_NOISE_COE11_COE14);

	val = CIF_ISP_PACK_4BYTE(arg->uv_gauss_noise_coe[4],
				 arg->uv_gauss_noise_coe[5],
				 arg->uv_gauss_noise_coe[6],
				 arg->uv_gauss_noise_coe[7]);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_UV_GAUSS_NOISE_COE15_COE23);

	val = CIF_ISP_PACK_4BYTE(arg->uv_gauss_noise_coe[8],
				 arg->uv_gauss_noise_coe[9],
				 arg->uv_gauss_noise_coe[10],
				 arg->uv_gauss_noise_coe[11]);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_UV_GAUSS_NOISE_COE24_COE32);

	val = CIF_ISP_PACK_4BYTE(arg->uv_gauss_noise_coe[12],
				 arg->uv_gauss_noise_coe[13],
				 arg->uv_gauss_noise_coe[14],
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_UV_GAUSS_NOISE_COE33_COE35);

	val = CIF_ISP_PACK_4BYTE(arg->uv_gauss_other_coe[0],
				 arg->uv_gauss_other_coe[1],
				 arg->uv_gauss_other_coe[2],
				 arg->uv_gauss_other_coe[3]);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_UV_GAUSS_OTHER_COE11_COE14);

	val = CIF_ISP_PACK_4BYTE(arg->uv_gauss_other_coe[4],
				 arg->uv_gauss_other_coe[5],
				 arg->uv_gauss_other_coe[6],
				 arg->uv_gauss_other_coe[7]);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_UV_GAUSS_OTHER_COE15_COE23);

	val = CIF_ISP_PACK_4BYTE(arg->uv_gauss_other_coe[8],
				 arg->uv_gauss_other_coe[9],
				 arg->uv_gauss_other_coe[10],
				 arg->uv_gauss_other_coe[11]);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_UV_GAUSS_OTHER_COE24_COE32);

	val = CIF_ISP_PACK_4BYTE(arg->uv_gauss_other_coe[12],
				 arg->uv_gauss_other_coe[13],
				 arg->uv_gauss_other_coe[14],
				 0);
	rkisp_iowrite32(params_vdev, val,
			 CIF_RKSHARP_UV_GAUSS_OTHER_COE33_COE35);

	rkisp_iowrite32(params_vdev, arg->switch_avg,
			 CIF_RKSHARP_CTRL);

	rkisp_iowrite32(params_vdev,
			 arg->coring_thr,
			 CIF_IMG_EFF_SHARPEN);

	val = rkisp_ioread32(params_vdev, CIF_IMG_EFF_MAT_3) & 0x0F;
	val |= (arg->lap_mat_coe[0] & 0x0F) << 4 |
	       (arg->lap_mat_coe[1] & 0x0F) << 8 |
	       (arg->lap_mat_coe[2] & 0x0F) << 12;
	rkisp_iowrite32(params_vdev, val, CIF_IMG_EFF_MAT_3);

	val = (arg->lap_mat_coe[3] & 0x0F) << 0 |
	       (arg->lap_mat_coe[4] & 0x0F) << 4 |
	       (arg->lap_mat_coe[5] & 0x0F) << 8 |
	       (arg->lap_mat_coe[6] & 0x0F) << 12;
	rkisp_iowrite32(params_vdev, val, CIF_IMG_EFF_MAT_4);

	val = (arg->lap_mat_coe[7] & 0x0F) << 0 |
	       (arg->lap_mat_coe[8] & 0x0F) << 4;
	rkisp_iowrite32(params_vdev, val, CIF_IMG_EFF_MAT_5);

	eff_ctrl = rkisp_ioread32(params_vdev, CIF_IMG_EFF_CTRL);
	eff_ctrl &= ~CIF_IMG_EFF_CTRL_MODE_MASK;
	eff_ctrl |= CIF_IMG_EFF_CTRL_MODE_RKSHARPEN;

	if (arg->full_range)
		eff_ctrl |= CIF_IMG_EFF_CTRL_YCBCR_FULL;

	rkisp_iowrite32(params_vdev, eff_ctrl, CIF_IMG_EFF_CTRL);
}

static void
isp_iesharp_enable(struct rkisp_isp_params_vdev *params_vdev,
		   bool en)
{
	return isp_ie_enable(params_vdev, en);
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
	u32 addr, i, value;
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
			(arg->sw_rawawb_blk_measure_mode & 0x3) << 12 |
			(arg->sw_rawawb_store_wp_th0 & 0x1FF) << 14 |
			(arg->sw_rawawb_store_wp_th1 & 0x1FF) << 23,
			ISP_RAWAWB_BLK_CTRL);

	value = rkisp_ioread32(params_vdev, ISP_RAWAWB_RAM_CTRL);
	value &= ~(ISP2X_RAWAWB_WPTH2_SET(0x1FF));
	value |= ISP2X_RAWAWB_WPTH2_SET(arg->sw_rawawb_store_wp_th2);
	rkisp_iowrite32(params_vdev, value, ISP_RAWAWB_RAM_CTRL);

	/* avoid to override the old enable value */
	value = rkisp_ioread32(params_vdev, ISP_RAWAWB_CTRL);
	value &= ISP2X_RAWAWB_ENA;
	value &= ~ISP2X_REG_WR_MASK;
	rkisp_iowrite32(params_vdev,
			value |
			(arg->sw_rawawb_uv_en & 0x1) << 1 |
			(arg->sw_rawawb_xy_en & 0x1) << 2 |
			(arg->sw_rawlsc_bypass_en & 0x1) << 3 |
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
isp_rawnr_config(struct rkisp_isp_params_vdev *params_vdev,
		 const struct isp2x_rawnr_cfg *arg)
{
	u32 value;
	int i;

	value = rkisp_ioread32(params_vdev, ISP_RAWNR_CTRL);
	value &= ISP_RAWNR_EN;

	value |= (arg->gauss_en & 0x01) << 20 |
		 (arg->log_bypass & 0x01) << 12;
	rkisp_iowrite32(params_vdev, value, ISP_RAWNR_CTRL);
	rkisp_iowrite32(params_vdev, arg->filtpar0, ISP_RAWNR_FILTPAR0);
	rkisp_iowrite32(params_vdev, arg->filtpar1, ISP_RAWNR_FILTPAR1);
	rkisp_iowrite32(params_vdev, arg->filtpar2, ISP_RAWNR_FILTPAR2);
	rkisp_iowrite32(params_vdev, arg->dgain0, ISP_RAWNR_DGAIN0);
	rkisp_iowrite32(params_vdev, arg->dgain1, ISP_RAWNR_DGAIN1);
	rkisp_iowrite32(params_vdev, arg->dgain2, ISP_RAWNR_DGAIN2);

	for (i = 0; i < ISP2X_RAWNR_LUMA_RATION_NUM / 2; i++) {
		value = ISP2X_PACK_2SHORT(arg->luration[2 * i], arg->luration[2 * i + 1]);
		rkisp_iowrite32(params_vdev, value, ISP_RAWNR_LURTION0_1 + 4 * i);
	}

	for (i = 0; i < ISP2X_RAWNR_LUMA_RATION_NUM / 2; i++) {
		value = ISP2X_PACK_2SHORT(arg->lulevel[2 * i], arg->lulevel[2 * i + 1]);
		rkisp_iowrite32(params_vdev, value, ISP_RAWNR_LULEVEL0_1 + 4 * i);
	}

	rkisp_iowrite32(params_vdev, arg->gauss, ISP_RAWNR_GAUSS);
	rkisp_iowrite32(params_vdev, arg->sigma, ISP_RAWNR_SIGMA);
	rkisp_iowrite32(params_vdev, arg->pix_diff, ISP_RAWNR_PIX_DIFF);
	rkisp_iowrite32(params_vdev, arg->thld_diff, ISP_RAWNR_HILD_DIFF);

	value = (arg->gas_weig_scl2 & 0xFF) << 24 |
		 (arg->gas_weig_scl1 & 0xFF) << 16 |
		 (arg->thld_chanelw & 0x07FF);
	rkisp_iowrite32(params_vdev, value, ISP_RAWNR_THLD_CHANELW);

	rkisp_iowrite32(params_vdev, arg->lamda, ISP_RAWNR_LAMDA);

	value = ISP2X_PACK_2SHORT(arg->fixw0, arg->fixw1);
	rkisp_iowrite32(params_vdev, value, ISP_RAWNR_FIXW0_1);

	value = ISP2X_PACK_2SHORT(arg->fixw2, arg->fixw3);
	rkisp_iowrite32(params_vdev, value, ISP_RAWNR_FIXW2_3);

	rkisp_iowrite32(params_vdev, arg->wlamda0, ISP_RAWNR_WLAMDA0);
	rkisp_iowrite32(params_vdev, arg->wlamda1, ISP_RAWNR_WLAMDA1);
	rkisp_iowrite32(params_vdev, arg->wlamda2, ISP_RAWNR_WLAMDA2);

	value = ISP2X_PACK_2SHORT(arg->bgain_filp, arg->rgain_filp);
	rkisp_iowrite32(params_vdev, value, ISP_RAWNR_RGBAIN_FLIP);
}

static void
isp_rawnr_enable(struct rkisp_isp_params_vdev *params_vdev,
		 bool en)
{
	u32 value;

	value = rkisp_ioread32(params_vdev, ISP_RAWNR_CTRL);
	value &= ~ISP_RAWNR_EN;

	if (en)
		value |= ISP_RAWNR_EN;
	rkisp_iowrite32(params_vdev, value, ISP_RAWNR_CTRL);
}

static void isp_hdrtmo_wait_first_line(struct rkisp_isp_params_vdev *params_vdev)
{
	s32 retry = 10;
	u32 value, line_cnt, frame_id;
	struct v4l2_rect *out_crop = &params_vdev->dev->isp_sdev.out_crop;

	rkisp_dmarx_get_frame(params_vdev->dev, &frame_id, NULL, NULL, true);

	do {
		value = rkisp_read(params_vdev->dev, ISP_HDRTMO_LG_RO5, true);
		line_cnt = value & 0x1fff;

		if (frame_id != 0 && (line_cnt < 1 || line_cnt >= out_crop->height))
			udelay(10);
		else
			break;
	} while (retry-- > 0);
}

static void
isp_hdrtmo_config(struct rkisp_isp_params_vdev *params_vdev,
		  const struct isp2x_hdrtmo_cfg *arg, enum rkisp_params_type type)
{
	u8 big_en, nobig_en;
	u32 value;

	if (type == RKISP_PARAMS_SHD || type == RKISP_PARAMS_ALL) {
		value = rkisp_ioread32(params_vdev, ISP_HDRTMO_CTRL_CFG);
		value &= 0xff;
		value |= (arg->expl_lgratio & 0xFFFF) << 16 |
			 (arg->lgscl_ratio & 0xFF) << 8;
		rkisp_iowrite32(params_vdev, value, ISP_HDRTMO_CTRL_CFG);

		value = ISP2X_PACK_2SHORT(arg->lgscl, arg->lgscl_inv);
		rkisp_iowrite32(params_vdev, value, ISP_HDRTMO_LG_SCL);

		if (type == RKISP_PARAMS_SHD)
			isp_hdrtmo_wait_first_line(params_vdev);

		value = ISP2X_PACK_2SHORT(arg->set_palpha, arg->set_gainoff);
		rkisp_iowrite32(params_vdev, value, ISP_HDRTMO_LG_CFG0);

		value = ISP2X_PACK_2SHORT(arg->set_lgmin, arg->set_lgmax);
		rkisp_iowrite32(params_vdev, value, ISP_HDRTMO_LG_CFG1);

		value = ISP2X_PACK_2SHORT(arg->set_lgmean, arg->set_weightkey);
		rkisp_iowrite32(params_vdev, value, ISP_HDRTMO_LG_CFG2);

		value = ISP2X_PACK_2SHORT(arg->set_lgrange0, arg->set_lgrange1);
		rkisp_iowrite32(params_vdev, value, ISP_HDRTMO_LG_CFG3);

		value = arg->set_lgavgmax;
		rkisp_iowrite32(params_vdev, value, ISP_HDRTMO_LG_CFG4);
	}

	if (type == RKISP_PARAMS_IMD || type == RKISP_PARAMS_ALL) {
		big_en = arg->big_en & 0x01;
		nobig_en = arg->nobig_en & 0x01;
		if (isp_param_get_insize(params_vdev) > ISP2X_NOBIG_OVERFLOW_SIZE) {
			big_en = 1;
			nobig_en = 0;
		}

		value = rkisp_ioread32(params_vdev, ISP_HDRTMO_CTRL);
		value &= ISP_HDRTMO_EN;
		value |= (arg->cnt_vsize & 0x1FFF) << 16 |
			 (arg->gain_ld_off2 & 0x0F) << 12 |
			 (arg->gain_ld_off1 & 0x0F) << 8 |
			 (big_en & 0x01) << 5 |
			 (nobig_en & 0x01) << 4 |
			 (arg->newhst_en & 0x01) << 2 |
			 (arg->cnt_mode & 0x01) << 1;
		rkisp_iowrite32(params_vdev, value, ISP_HDRTMO_CTRL);

		/*
		 * expl_lgratio/lgscl_ratio will reconfigure in vs
		 * when rx perform 'back read' for the last time.
		 *
		 * expl_lgratio[31:16] is fixed at 0x0 and
		 * lgscl_ratio[15:8] is fixed at 0x80
		 * during rx perform 'back read', so set value to 0x8000.
		 */
		value = 0x8000;
		value |= arg->cfg_alpha & 0xFF;
		rkisp_iowrite32(params_vdev, value, ISP_HDRTMO_CTRL_CFG);

		value = (arg->clipgap1_i & 0x0F) << 28 |
			(arg->clipgap0_i & 0x0F) << 24 |
			(arg->clipratio1 & 0xFF) << 16 |
			(arg->clipratio0 & 0xFF) << 8 |
			(arg->ratiol & 0xFF);
		rkisp_iowrite32(params_vdev, value, ISP_HDRTMO_CLIPRATIO);

		value = arg->lgmax;
		rkisp_iowrite32(params_vdev, value, ISP_HDRTMO_LG_MAX);

		value = ISP2X_PACK_2SHORT(arg->hist_min, arg->hist_low);
		rkisp_iowrite32(params_vdev, value, ISP_HDRTMO_HIST_LOW);

		value = (arg->hist_shift & 0x07) << 28 |
			(arg->hist_0p3 & 0x07FF) << 16 |
			(arg->hist_high & 0x3FFF);
		rkisp_iowrite32(params_vdev, value, ISP_HDRTMO_HIST_HIGH);

		value = (arg->palpha_lwscl & 0x3F) << 26 |
			(arg->palpha_lw0p5 & 0x03FF) << 16 |
			(arg->palpha_0p18 & 0x03FF);
		rkisp_iowrite32(params_vdev, value, ISP_HDRTMO_PALPHA);

		value = ISP2X_PACK_2SHORT(arg->maxpalpha, arg->maxgain);
		rkisp_iowrite32(params_vdev, value, ISP_HDRTMO_MAXGAIN);
	}
}

static void
isp_hdrtmo_enable(struct rkisp_isp_params_vdev *params_vdev,
		  bool en)
{
	u32 value;

	params_vdev->hdrtmo_en = en;
	value = rkisp_ioread32(params_vdev, ISP_HDRTMO_CTRL);
	if (en)
		value |= ISP_HDRTMO_EN;
	else
		value &= ~ISP_HDRTMO_EN;
	rkisp_iowrite32(params_vdev, value, ISP_HDRTMO_CTRL);
}

static void
isp_gic_config(struct rkisp_isp_params_vdev *params_vdev,
	       const struct isp2x_gic_cfg *arg)
{
	u32 value;
	s32 i;

	value = rkisp_ioread32(params_vdev, ISP_GIC_CONTROL);
	value &= ISP_GIC_ENA;

	if (arg->edge_open)
		value |= ISP_GIC_EDGE_OPEN;
	rkisp_iowrite32(params_vdev, value, ISP_GIC_CONTROL);

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
		(arg->dnloscale & 0x07FF) << 15 |
		(arg->dnhiscale & 0x07FF) << 4 |
		(arg->reglumapointsstep & 0x0F);
	rkisp_iowrite32(params_vdev, value, ISP_GIC_NOISE_PARA1);

	value = (arg->gvaluelimitlo & 0x0FFF) << 20 |
		(arg->gvaluelimithi & 0x0FFF) << 8 |
		(arg->fusionratiohilimt1 & 0xFF);
	rkisp_iowrite32(params_vdev, value, ISP_GIC_NOISE_PARA2);

	value = arg->regstrength_fix & 0xFF;
	rkisp_iowrite32(params_vdev, value, ISP_GIC_NOISE_PARA3);

	for (i = 0; i < ISP2X_GIC_SIGMA_Y_NUM / 2; i++) {
		value = ISP2X_PACK_2SHORT(arg->sigma_y[2 * i], arg->sigma_y[2 * i + 1]);
		rkisp_iowrite32(params_vdev, value, ISP_GIC_SIGMA_VALUE0 + 4 * i);
	}
	value = ISP2X_PACK_2SHORT(arg->sigma_y[2 * i], 0);
	rkisp_iowrite32(params_vdev, value, ISP_GIC_SIGMA_VALUE0 + 4 * i);

	value = (arg->noise_coe_a & 0x07FF) << 4 |
		(arg->noise_cut_en & 0x01);
	rkisp_iowrite32(params_vdev, value, ISP_GIC_NOISE_CTRL0);

	value = ISP2X_PACK_2SHORT(arg->noise_coe_b, arg->diff_clip);
	rkisp_iowrite32(params_vdev, value, ISP_GIC_NOISE_CTRL1);
}

static void
isp_gic_enable(struct rkisp_isp_params_vdev *params_vdev,
	       bool en)
{
	u32 value;

	value = rkisp_ioread32(params_vdev, ISP_GIC_CONTROL);
	value &= ISP_GIC_EDGE_OPEN;

	if (en)
		value |= ISP_GIC_ENA;
	rkisp_iowrite32(params_vdev, value, ISP_GIC_CONTROL);
}

static void
isp_dhaz_config(struct rkisp_isp_params_vdev *params_vdev,
		const struct isp2x_dhaz_cfg *arg)
{
	u8 big_en, nobig_en;
	u32 value;

	big_en = arg->big_en & 0x01;
	nobig_en = arg->nobig_en & 0x01;
	if (isp_param_get_insize(params_vdev) > ISP2X_NOBIG_OVERFLOW_SIZE) {
		big_en = 1;
		nobig_en = 0;
	}

	value = rkisp_ioread32(params_vdev, ISP_DHAZ_CTRL);
	value &= ISP_DHAZ_ENMUX;
	if (nobig_en)
		value |= ISP_DHAZ_NOBIGEN;
	if (big_en)
		value |= ISP_DHAZ_BIGEN;
	if (arg->dc_en)
		value |= ISP_DHAZ_DCEN;
	if (arg->hist_en)
		value |= ISP_DHAZ_HSTEN;
	if (arg->hpara_en)
		value |= ISP_DHAZ_HPARAEN;
	if (arg->hist_chn)
		value |= ISP_DHAZ_HSTCHN;
	if (arg->enhance_en)
		value |= ISP_DHAZ_ENHANCE;
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_CTRL);

	value = ISP2X_PACK_4BYTE(arg->dc_min_th, arg->dc_max_th,
				 arg->yhist_th, arg->yblk_th);
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_ADP0);

	value = ISP2X_PACK_4BYTE(arg->bright_min, arg->bright_max,
				 arg->wt_max, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_ADP1);

	value = ISP2X_PACK_4BYTE(arg->air_min, arg->air_max,
				 arg->dark_th, arg->tmax_base);
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_ADP2);

	value = ISP2X_PACK_2SHORT(arg->tmax_off, arg->tmax_max);
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_ADP_TMAX);

	value = ISP2X_PACK_2SHORT(arg->hist_gratio, arg->hist_th_off);
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_ADP_HIST0);

	value = ISP2X_PACK_2SHORT(arg->hist_k, arg->hist_min);
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_ADP_HIST1);

	value = ISP2X_PACK_2SHORT(arg->hist_scale, arg->enhance_value);
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_HIST_ENH);

	value = (arg->iir_wt_sigma & 0x07FF) << 16 |
		(arg->iir_sigma & 0xFF) << 8 |
		(arg->stab_fnum & 0x1F);
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_IIR0);

	value = ISP2X_PACK_2SHORT(arg->iir_air_sigma, arg->iir_tmax_sigma);
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_IIR1);

	value = (arg->cfg_wt & 0x01FF) << 16 |
		(arg->cfg_air & 0xFF) << 8 |
		(arg->cfg_alpha & 0xFF);
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_ALPHA0);

	value = ISP2X_PACK_2SHORT(arg->cfg_tmax, arg->cfg_gratio);
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_ALPHA1);

	value = ISP2X_PACK_2SHORT(arg->dc_thed, arg->dc_weitcur);
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_BI_DC);

	value = ISP2X_PACK_4BYTE(arg->sw_dhaz_dc_bf_h0, arg->sw_dhaz_dc_bf_h1,
				 arg->sw_dhaz_dc_bf_h2, arg->sw_dhaz_dc_bf_h3);
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_DC_BF0);

	value = ISP2X_PACK_4BYTE(arg->sw_dhaz_dc_bf_h4, arg->sw_dhaz_dc_bf_h5, 0, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_DC_BF1);

	value = ISP2X_PACK_2SHORT(arg->air_thed, arg->air_weitcur);
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_BI_AIR);

	value = ISP2X_PACK_4BYTE(arg->air_bf_h0, arg->air_bf_h1, arg->air_bf_h2, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_AIR_BF);

	value = ISP2X_PACK_4BYTE(arg->gaus_h0, arg->gaus_h1, arg->gaus_h2, 0);
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_GAUS);

	value = (arg->conv_t0[5] & 0x0F) << 20 |
		(arg->conv_t0[4] & 0x0F) << 16 |
		(arg->conv_t0[3] & 0x0F) << 12 |
		(arg->conv_t0[2] & 0x0F) << 8 |
		(arg->conv_t0[1] & 0x0F) << 4 |
		(arg->conv_t0[0] & 0x0F);
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_HIST_CONV0);

	value = (arg->conv_t1[5] & 0x0F) << 20 |
		(arg->conv_t1[4] & 0x0F) << 16 |
		(arg->conv_t1[3] & 0x0F) << 12 |
		(arg->conv_t1[2] & 0x0F) << 8 |
		(arg->conv_t1[1] & 0x0F) << 4 |
		(arg->conv_t1[0] & 0x0F);
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_HIST_CONV1);

	value = (arg->conv_t2[5] & 0x0F) << 20 |
		(arg->conv_t2[4] & 0x0F) << 16 |
		(arg->conv_t2[3] & 0x0F) << 12 |
		(arg->conv_t2[2] & 0x0F) << 8 |
		(arg->conv_t2[1] & 0x0F) << 4 |
		(arg->conv_t2[0] & 0x0F);
	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_HIST_CONV2);
}

static void
isp_dhaz_enable(struct rkisp_isp_params_vdev *params_vdev,
		bool en)
{
	u32 value;

	value = rkisp_ioread32(params_vdev, ISP_DHAZ_CTRL);
	value &= ~ISP_DHAZ_ENMUX;

	if (en)
		value |= ISP_DHAZ_ENMUX;

	rkisp_iowrite32(params_vdev, value, ISP_DHAZ_CTRL);
}

static void
isp_gain_config(struct rkisp_isp_params_vdev *params_vdev,
		const struct isp2x_gain_cfg *arg)
{
	struct rkisp_device *dev = params_vdev->dev;
	struct rkisp_isp_params_val_v2x *priv_val =
		(struct rkisp_isp_params_val_v2x *)params_vdev->priv_val;
	u32 value, i, gain_wsize;
	u8 mge_en;

	if (dev->rd_mode != HDR_NORMAL &&
	    dev->rd_mode != HDR_RDBK_FRAME1)
		mge_en = 1;
	else
		mge_en = 0;

	gain_wsize = rkisp_ioread32(params_vdev, MI_GAIN_WR_SIZE);
	gain_wsize &= 0x0FFFFFF0;
	if (gain_wsize)
		value = (priv_val->dhaz_en & 0x01) << 16 |
			(priv_val->wdr_en & 0x01) << 12 |
			(priv_val->tmo_en & 0x01) << 8 |
			(priv_val->lsc_en & 0x01) << 4 |
			(mge_en & 0x01);
	else
		value = 0;

	rkisp_iowrite32(params_vdev, value, ISP_GAIN_CTRL);

	value = arg->mge_gain[0];
	rkisp_iowrite32(params_vdev, value, ISP_GAIN_G0);

	value = ISP2X_PACK_2SHORT(arg->mge_gain[1], arg->mge_gain[2]);
	rkisp_iowrite32(params_vdev, value, ISP_GAIN_G1_G2);

	for (i = 0; i < ISP2X_GAIN_IDX_NUM / 4; i++) {
		value = ISP2X_PACK_4BYTE(arg->idx[4 * i], arg->idx[4 * i + 1],
					 arg->idx[4 * i + 2], arg->idx[4 * i + 3]);
		rkisp_iowrite32(params_vdev, value, ISP_GAIN_IDX0 + 4 * i);
	}
	value = ISP2X_PACK_4BYTE(arg->idx[4 * i], arg->idx[4 * i + 1],
				 arg->idx[4 * i + 2], 0);
	rkisp_iowrite32(params_vdev, value, ISP_GAIN_IDX0 + 4 * i);

	for (i = 0; i < ISP2X_GAIN_LUT_NUM / 2; i++) {
		value = ISP2X_PACK_2SHORT(arg->lut[2 * i], arg->lut[2 * i + 1]);
		rkisp_iowrite32(params_vdev, value, ISP_GAIN_LUT0 + 4 * i);
	}
	value = ISP2X_PACK_2SHORT(arg->lut[2 * i], 0);
	rkisp_iowrite32(params_vdev, value, ISP_GAIN_LUT0 + 4 * i);
}

static void
isp_gain_enable(struct rkisp_isp_params_vdev *params_vdev,
		bool en)
{
}

static void
isp_3dlut_config(struct rkisp_isp_params_vdev *params_vdev,
		 const struct isp2x_3dlut_cfg *arg)
{
	struct rkisp_isp_params_val_v2x *priv_val;
	u32 value, buf_idx, i;
	u32 *data;

	priv_val = (struct rkisp_isp_params_val_v2x *)params_vdev->priv_val;
	buf_idx = (priv_val->buf_3dlut_idx++) % RKISP_PARAM_3DLUT_BUF_NUM;

	data = (u32 *)priv_val->buf_3dlut[buf_idx].vaddr;
	for (i = 0; i < arg->actual_size; i++)
		data[i] = (arg->lut_b[i] & 0x3FF) |
			  (arg->lut_g[i] & 0xFFF) << 10 |
			  (arg->lut_r[i] & 0x3FF) << 22;
	rkisp_prepare_buffer(params_vdev->dev, &priv_val->buf_3dlut[buf_idx]);
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
	struct rkisp_isp_params_val_v2x *priv_val;
	struct isp2x_ldch_head *ldch_head;
	int buf_idx, i;
	u32 value, vsize;

	priv_val = (struct rkisp_isp_params_val_v2x *)params_vdev->priv_val;
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

	vsize = arg->vsize;
	/* normal extend line for ldch mesh */
	if (dev->isp_ver == ISP_V20) {
		void *buf = priv_val->buf_ldch[buf_idx].vaddr + ldch_head->data_oft;
		u32 cnt = RKMODULE_EXTEND_LINE / 8;

		value = arg->hsize * 4;
		memcpy(buf + value * vsize, buf + value * (vsize - cnt), cnt * value);
		if (dev->rd_mode == HDR_RDBK_FRAME1)
			vsize += cnt;
	}
	rkisp_prepare_buffer(dev, &priv_val->buf_ldch[buf_idx]);
	value = priv_val->buf_ldch[buf_idx].dma_addr + ldch_head->data_oft;
	rkisp_iowrite32(params_vdev, value, MI_LUT_LDCH_RD_BASE);
	rkisp_iowrite32(params_vdev, arg->hsize, MI_LUT_LDCH_RD_H_WSIZE);
	rkisp_iowrite32(params_vdev, vsize, MI_LUT_LDCH_RD_V_SIZE);
}

static void
isp_ldch_enable(struct rkisp_isp_params_vdev *params_vdev,
		bool en)
{
	struct rkisp_device *dev = params_vdev->dev;
	struct rkisp_isp_params_val_v2x *priv_val;
	u32 buf_idx;

	priv_val = (struct rkisp_isp_params_val_v2x *)params_vdev->priv_val;
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
	.debayer_config = isp_debayer_config,
	.debayer_enable = isp_debayer_enable,
	.ccm_config = isp_ccm_config,
	.ccm_enable = isp_ccm_enable,
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
	.rawnr_config = isp_rawnr_config,
	.rawnr_enable = isp_rawnr_enable,
	.hdrtmo_config = isp_hdrtmo_config,
	.hdrtmo_enable = isp_hdrtmo_enable,
	.gic_config = isp_gic_config,
	.gic_enable = isp_gic_enable,
	.dhaz_config = isp_dhaz_config,
	.dhaz_enable = isp_dhaz_enable,
	.gain_config = isp_gain_config,
	.gain_enable = isp_gain_enable,
	.isp3dlut_config = isp_3dlut_config,
	.isp3dlut_enable = isp_3dlut_enable,
	.ldch_config = isp_ldch_config,
	.ldch_enable = isp_ldch_enable,
	.csm_config = isp_csm_config,
};

static __maybe_unused
void __isp_isr_other_config(struct rkisp_isp_params_vdev *params_vdev,
			    const struct isp2x_isp_params_cfg *new_params, enum rkisp_params_type type)
{
	struct rkisp_isp_params_v2x_ops *ops =
		(struct rkisp_isp_params_v2x_ops *)params_vdev->priv_ops;
	u64 module_cfg_update = new_params->module_cfg_update;

	if (type == RKISP_PARAMS_SHD) {
		if ((module_cfg_update & ISP2X_MODULE_HDRMGE))
			ops->hdrmge_config(params_vdev,	&new_params->others.hdrmge_cfg, type);

		if ((module_cfg_update & ISP2X_MODULE_HDRTMO))
			ops->hdrtmo_config(params_vdev, &new_params->others.hdrtmo_cfg, type);

		return;
	}

	if ((module_cfg_update & ISP2X_MODULE_DPCC))
		ops->dpcc_config(params_vdev, &new_params->others.dpcc_cfg);

	if ((module_cfg_update & ISP2X_MODULE_BLS))
		ops->bls_config(params_vdev, &new_params->others.bls_cfg);

	if ((module_cfg_update & ISP2X_MODULE_SDG))
		ops->sdg_config(params_vdev, &new_params->others.sdg_cfg);

	if ((module_cfg_update & ISP2X_MODULE_LSC))
		ops->lsc_config(params_vdev, &new_params->others.lsc_cfg);

	if ((module_cfg_update & ISP2X_MODULE_AWB_GAIN))
		ops->awbgain_config(params_vdev, &new_params->others.awb_gain_cfg);

	if ((module_cfg_update & ISP2X_MODULE_DEBAYER))
		ops->debayer_config(params_vdev, &new_params->others.debayer_cfg);

	if ((module_cfg_update & ISP2X_MODULE_CCM))
		ops->ccm_config(params_vdev, &new_params->others.ccm_cfg);

	if ((module_cfg_update & ISP2X_MODULE_GOC))
		ops->goc_config(params_vdev, &new_params->others.gammaout_cfg);

	if ((module_cfg_update & ISP2X_MODULE_CPROC))
		ops->cproc_config(params_vdev, &new_params->others.cproc_cfg);

	if ((module_cfg_update & ISP2X_MODULE_IE))
		ops->ie_config(params_vdev, &new_params->others.ie_cfg);

	if ((module_cfg_update & ISP2X_MODULE_WDR))
		ops->wdr_config(params_vdev, &new_params->others.wdr_cfg);

	if ((module_cfg_update & ISP2X_MODULE_RK_IESHARP))
		ops->iesharp_config(params_vdev, &new_params->others.rkiesharp_cfg);

	if ((module_cfg_update & ISP2X_MODULE_HDRMGE))
		ops->hdrmge_config(params_vdev, &new_params->others.hdrmge_cfg, type);

	if ((module_cfg_update & ISP2X_MODULE_RAWNR))
		ops->rawnr_config(params_vdev, &new_params->others.rawnr_cfg);

	if ((module_cfg_update & ISP2X_MODULE_HDRTMO))
		ops->hdrtmo_config(params_vdev, &new_params->others.hdrtmo_cfg, type);

	if ((module_cfg_update & ISP2X_MODULE_GIC))
		ops->gic_config(params_vdev, &new_params->others.gic_cfg);

	if ((module_cfg_update & ISP2X_MODULE_DHAZ))
		ops->dhaz_config(params_vdev, &new_params->others.dhaz_cfg);

	if ((module_cfg_update & ISP2X_MODULE_3DLUT))
		ops->isp3dlut_config(params_vdev, &new_params->others.isp3dlut_cfg);

	if ((module_cfg_update & ISP2X_MODULE_LDCH))
		ops->ldch_config(params_vdev, &new_params->others.ldch_cfg);

	if ((module_cfg_update & ISP2X_MODULE_GAIN))
		ops->gain_config(params_vdev, &new_params->others.gain_cfg);
}

static __maybe_unused
void __isp_isr_other_en(struct rkisp_isp_params_vdev *params_vdev,
			const struct isp2x_isp_params_cfg *new_params, enum rkisp_params_type type)
{
	struct rkisp_isp_params_v2x_ops *ops =
		(struct rkisp_isp_params_v2x_ops *)params_vdev->priv_ops;
	struct rkisp_isp_params_val_v2x *priv_val =
		(struct rkisp_isp_params_val_v2x *)params_vdev->priv_val;
	u64 module_en_update = new_params->module_en_update;
	u64 module_ens = new_params->module_ens;

	if (type == RKISP_PARAMS_SHD)
		return;

	if (module_en_update & ISP2X_MODULE_HDRMGE) {
		ops->hdrmge_enable(params_vdev, !!(module_ens & ISP2X_MODULE_HDRMGE));
		priv_val->mge_en = !!(module_ens & ISP2X_MODULE_HDRMGE);
	}

	if (module_en_update & ISP2X_MODULE_HDRTMO) {
		ops->hdrtmo_enable(params_vdev,	!!(module_ens & ISP2X_MODULE_HDRTMO));
		priv_val->tmo_en = !!(module_ens & ISP2X_MODULE_HDRTMO);
	}

	if (module_en_update & ISP2X_MODULE_DPCC)
		ops->dpcc_enable(params_vdev, !!(module_ens & ISP2X_MODULE_DPCC));

	if (module_en_update & ISP2X_MODULE_BLS)
		ops->bls_enable(params_vdev, !!(module_ens & ISP2X_MODULE_BLS));

	if (module_en_update & ISP2X_MODULE_SDG)
		ops->sdg_enable(params_vdev, !!(module_ens & ISP2X_MODULE_SDG));

	if (module_en_update & ISP2X_MODULE_LSC) {
		ops->lsc_enable(params_vdev, !!(module_ens & ISP2X_MODULE_LSC));
		priv_val->lsc_en = !!(module_ens & ISP2X_MODULE_LSC);
	}

	if (module_en_update & ISP2X_MODULE_AWB_GAIN)
		ops->awbgain_enable(params_vdev, !!(module_ens & ISP2X_MODULE_AWB_GAIN));

	if (module_en_update & ISP2X_MODULE_DEBAYER)
		ops->debayer_enable(params_vdev, !!(module_ens & ISP2X_MODULE_DEBAYER));

	if (module_en_update & ISP2X_MODULE_CCM)
		ops->ccm_enable(params_vdev, !!(module_ens & ISP2X_MODULE_CCM));

	if (module_en_update & ISP2X_MODULE_GOC)
		ops->goc_enable(params_vdev, !!(module_ens & ISP2X_MODULE_GOC));

	if (module_en_update & ISP2X_MODULE_CPROC)
		ops->cproc_enable(params_vdev, !!(module_ens & ISP2X_MODULE_CPROC));

	if (module_en_update & ISP2X_MODULE_IE)
		ops->ie_enable(params_vdev, !!(module_ens & ISP2X_MODULE_IE));

	if (module_en_update & ISP2X_MODULE_WDR) {
		ops->wdr_enable(params_vdev, !!(module_ens & ISP2X_MODULE_WDR));
		priv_val->wdr_en = !!(module_ens & ISP2X_MODULE_WDR);
	}

	if (module_en_update & ISP2X_MODULE_RK_IESHARP)
		ops->iesharp_enable(params_vdev, !!(module_ens & ISP2X_MODULE_RK_IESHARP));

	if (module_en_update & ISP2X_MODULE_HDRMGE) {
		ops->hdrmge_enable(params_vdev, !!(module_ens & ISP2X_MODULE_HDRMGE));
		priv_val->mge_en = !!(module_ens & ISP2X_MODULE_HDRMGE);
	}

	if (module_en_update & ISP2X_MODULE_RAWNR)
		ops->rawnr_enable(params_vdev, !!(module_ens & ISP2X_MODULE_RAWNR));

	if (module_en_update & ISP2X_MODULE_HDRTMO) {
		ops->hdrtmo_enable(params_vdev, !!(module_ens & ISP2X_MODULE_HDRTMO));
		priv_val->tmo_en = !!(module_ens & ISP2X_MODULE_HDRTMO);
	}

	if (module_en_update & ISP2X_MODULE_GIC)
		ops->gic_enable(params_vdev, !!(module_ens & ISP2X_MODULE_GIC));

	if (module_en_update & ISP2X_MODULE_DHAZ) {
		ops->dhaz_enable(params_vdev, !!(module_ens & ISP2X_MODULE_DHAZ));
		priv_val->dhaz_en = !!(module_ens & ISP2X_MODULE_DHAZ);
	}

	if (module_en_update & ISP2X_MODULE_3DLUT)
		ops->isp3dlut_enable(params_vdev, !!(module_ens & ISP2X_MODULE_3DLUT));

	if (module_en_update & ISP2X_MODULE_LDCH) {
		/*
		 * lsc read table from sram in mult-isp mode,
		 * so don't delay in mult-isp mode.
		 */
		if (params_vdev->first_cfg_params &&
		    !!(module_ens & ISP2X_MODULE_LDCH) &&
		    params_vdev->dev->hw_dev->is_single)
			priv_val->delay_en_ldch = true;
		else
			ops->ldch_enable(params_vdev,
					!!(module_ens & ISP2X_MODULE_LDCH));
	}

	if (module_en_update & ISP2X_MODULE_GAIN)
		ops->gain_enable(params_vdev, !!(module_ens & ISP2X_MODULE_GAIN));
}

static __maybe_unused
void __isp_isr_meas_config(struct rkisp_isp_params_vdev *params_vdev,
			   struct isp2x_isp_params_cfg *new_params, enum rkisp_params_type type)
{
	struct rkisp_isp_params_v2x_ops *ops =
		(struct rkisp_isp_params_v2x_ops *)params_vdev->priv_ops;
	u64 module_cfg_update = new_params->module_cfg_update;

	if (type == RKISP_PARAMS_SHD)
		return;

	if ((module_cfg_update & ISP2X_MODULE_YUVAE))
		ops->yuvae_config(params_vdev, &new_params->meas.yuvae);

	if ((module_cfg_update & ISP2X_MODULE_RAWAE0))
		ops->rawae0_config(params_vdev, &new_params->meas.rawae0);

	if ((module_cfg_update & ISP2X_MODULE_RAWAE1))
		ops->rawae1_config(params_vdev, &new_params->meas.rawae1);

	if ((module_cfg_update & ISP2X_MODULE_RAWAE2))
		ops->rawae2_config(params_vdev, &new_params->meas.rawae2);

	if ((module_cfg_update & ISP2X_MODULE_RAWAE3))
		ops->rawae3_config(params_vdev, &new_params->meas.rawae3);

	if ((module_cfg_update & ISP2X_MODULE_SIHST))
		ops->sihst_config(params_vdev, &new_params->meas.sihst);

	if ((module_cfg_update & ISP2X_MODULE_RAWHIST0))
		ops->rawhst0_config(params_vdev, &new_params->meas.rawhist0);

	if ((module_cfg_update & ISP2X_MODULE_RAWHIST1))
		ops->rawhst1_config(params_vdev, &new_params->meas.rawhist1);

	if ((module_cfg_update & ISP2X_MODULE_RAWHIST2))
		ops->rawhst2_config(params_vdev, &new_params->meas.rawhist2);

	if ((module_cfg_update & ISP2X_MODULE_RAWHIST3))
		ops->rawhst3_config(params_vdev, &new_params->meas.rawhist3);

	if ((module_cfg_update & ISP2X_MODULE_SIAWB))
		ops->siawb_config(params_vdev, &new_params->meas.siawb);

	if ((module_cfg_update & ISP2X_MODULE_RAWAWB))
		ops->rawawb_config(params_vdev, &new_params->meas.rawawb);

	if ((module_cfg_update & ISP2X_MODULE_SIAF))
		ops->siaf_config(params_vdev, &new_params->meas.siaf);

	if ((module_cfg_update & ISP2X_MODULE_RAWAF))
		ops->rawaf_config(params_vdev, &new_params->meas.rawaf);
}

static __maybe_unused
void __isp_isr_meas_en(struct rkisp_isp_params_vdev *params_vdev,
		       struct isp2x_isp_params_cfg *new_params, enum rkisp_params_type type)
{
	struct rkisp_isp_params_v2x_ops *ops =
		(struct rkisp_isp_params_v2x_ops *)params_vdev->priv_ops;
	u64 module_en_update = new_params->module_en_update;
	u64 module_ens = new_params->module_ens;

	if (type == RKISP_PARAMS_SHD)
		return;

	if (module_en_update & ISP2X_MODULE_YUVAE)
		ops->yuvae_enable(params_vdev, !!(module_ens & ISP2X_MODULE_YUVAE));

	if (module_en_update & ISP2X_MODULE_RAWAE0)
		ops->rawae0_enable(params_vdev, !!(module_ens & ISP2X_MODULE_RAWAE0));

	if (module_en_update & ISP2X_MODULE_RAWAE1)
		ops->rawae1_enable(params_vdev, !!(module_ens & ISP2X_MODULE_RAWAE1));

	if (module_en_update & ISP2X_MODULE_RAWAE2)
		ops->rawae2_enable(params_vdev, !!(module_ens & ISP2X_MODULE_RAWAE2));

	if (module_en_update & ISP2X_MODULE_RAWAE3)
		ops->rawae3_enable(params_vdev, !!(module_ens & ISP2X_MODULE_RAWAE3));

	if (module_en_update & ISP2X_MODULE_SIHST)
		ops->sihst_enable(params_vdev, !!(module_ens & ISP2X_MODULE_SIHST));

	if (module_en_update & ISP2X_MODULE_RAWHIST0)
		ops->rawhst0_enable(params_vdev, !!(module_ens & ISP2X_MODULE_RAWHIST0));

	if (module_en_update & ISP2X_MODULE_RAWHIST1)
		ops->rawhst1_enable(params_vdev, !!(module_ens & ISP2X_MODULE_RAWHIST1));

	if (module_en_update & ISP2X_MODULE_RAWHIST2)
		ops->rawhst2_enable(params_vdev, !!(module_ens & ISP2X_MODULE_RAWHIST2));

	if (module_en_update & ISP2X_MODULE_RAWHIST3)
		ops->rawhst3_enable(params_vdev, !!(module_ens & ISP2X_MODULE_RAWHIST3));

	if (module_en_update & ISP2X_MODULE_SIAWB)
		ops->siawb_enable(params_vdev, !!(module_ens & ISP2X_MODULE_SIAWB));

	if (module_en_update & ISP2X_MODULE_RAWAWB)
		ops->rawawb_enable(params_vdev, !!(module_ens & ISP2X_MODULE_RAWAWB));

	if (module_en_update & ISP2X_MODULE_SIAF)
		ops->siaf_enable(params_vdev, !!(module_ens & ISP2X_MODULE_SIAF));

	if (module_en_update & ISP2X_MODULE_RAWAF)
		ops->rawaf_enable(params_vdev, !!(module_ens & ISP2X_MODULE_RAWAF));
}

static __maybe_unused
void __isp_config_hdrshd(struct rkisp_isp_params_vdev *params_vdev)
{
	struct rkisp_isp_params_v2x_ops *ops =
		(struct rkisp_isp_params_v2x_ops *)params_vdev->priv_ops;

	ops->hdrmge_config(params_vdev,
			   &params_vdev->last_hdrmge, RKISP_PARAMS_ALL);
	ops->hdrtmo_config(params_vdev,
			   &params_vdev->last_hdrtmo, RKISP_PARAMS_ALL);
}

static
void rkisp_params_cfgsram_v2x(struct rkisp_isp_params_vdev *params_vdev)
{
	isp_lsc_matrix_cfg_sram(params_vdev,
				&params_vdev->cur_lsccfg, true);
}

/* Not called when the camera active, thus not isr protection. */
static void
rkisp_params_first_cfg_v2x(struct rkisp_isp_params_vdev *params_vdev)
{
	struct device *dev = params_vdev->dev->dev;
	struct rkisp_isp_params_val_v2x *priv_val =
		(struct rkisp_isp_params_val_v2x *)params_vdev->priv_val;

	spin_lock(&params_vdev->config_lock);
	/* override the default things */
	if (!params_vdev->isp2x_params->module_cfg_update &&
	    !params_vdev->isp2x_params->module_en_update)
		dev_warn(dev, "can not get first iq setting in stream on\n");

	priv_val->dhaz_en = 0;
	priv_val->wdr_en = 0;
	priv_val->tmo_en = 0;
	priv_val->lsc_en = 0;
	priv_val->mge_en = 0;
	priv_val->delay_en_ldch = false;
	params_vdev->first_cfg_params = true;
	__isp_isr_other_config(params_vdev, params_vdev->isp2x_params, RKISP_PARAMS_ALL);
	__isp_isr_other_en(params_vdev, params_vdev->isp2x_params, RKISP_PARAMS_ALL);
	__isp_isr_meas_config(params_vdev, params_vdev->isp2x_params, RKISP_PARAMS_ALL);
	__isp_isr_meas_en(params_vdev, params_vdev->isp2x_params, RKISP_PARAMS_ALL);
	params_vdev->first_cfg_params = false;

	params_vdev->cur_hdrtmo = params_vdev->isp2x_params->others.hdrtmo_cfg;
	params_vdev->cur_hdrmge = params_vdev->isp2x_params->others.hdrmge_cfg;
	params_vdev->last_hdrtmo = params_vdev->cur_hdrtmo;
	params_vdev->last_hdrmge = params_vdev->cur_hdrmge;
	spin_unlock(&params_vdev->config_lock);
}

static void rkisp_save_first_param_v2x(struct rkisp_isp_params_vdev *params_vdev, void *param)
{
	struct isp2x_isp_params_cfg *new_params;

	new_params = (struct isp2x_isp_params_cfg *)param;
	*params_vdev->isp2x_params = *new_params;
}

static void rkisp_clear_first_param_v2x(struct rkisp_isp_params_vdev *params_vdev)
{
	params_vdev->isp2x_params->module_cfg_update = 0;
	params_vdev->isp2x_params->module_en_update = 0;
}

static u32 rkisp_get_ldch_meshsize(struct rkisp_isp_params_vdev *params_vdev,
				   struct rkisp_ldchbuf_size *ldchsize)
{
	int mesh_w, mesh_h, map_align, height;

	height = ldchsize->meas_height;
	if (params_vdev->dev->isp_ver == ISP_V20)
		height += RKMODULE_EXTEND_LINE;

	mesh_w = ((ldchsize->meas_width + (1 << 4) - 1) >> 4) + 1;
	mesh_h = ((height + (1 << 3) - 1) >> 3) + 1;

	map_align = ((mesh_w + 1) >> 1) << 1;
	return map_align * mesh_h;
}

static void rkisp_deinit_ldch_buf(struct rkisp_isp_params_vdev *params_vdev)
{
	struct rkisp_isp_params_val_v2x *priv_val;
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
	struct rkisp_isp_params_val_v2x *priv_val;
	struct isp2x_ldch_head *ldch_head;
	u32 mesh_size;
	int i, ret;

	priv_val = params_vdev->priv_val;
	if (!priv_val) {
		dev_err(dev, "priv_val is NULL\n");
		return -EINVAL;
	}

	priv_val->buf_ldch_idx = 0;
	mesh_size = rkisp_get_ldch_meshsize(params_vdev, ldchsize);
	for (i = 0; i < ISP2X_LDCH_BUF_NUM; i++) {
		priv_val->buf_ldch[i].is_need_vaddr = true;
		priv_val->buf_ldch[i].is_need_dbuf = true;
		priv_val->buf_ldch[i].is_need_dmafd = true;
		priv_val->buf_ldch[i].size =
			PAGE_ALIGN(mesh_size * sizeof(u16) + ALIGN(sizeof(struct isp2x_ldch_head), 16));
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
	struct rkisp_isp_params_val_v2x *priv_val;
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
rkisp_params_fop_release_v2x(struct rkisp_isp_params_vdev *params_vdev)
{
	rkisp_deinit_ldch_buf(params_vdev);
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
	ops->debayer_enable(params_vdev, false);
	ops->ccm_enable(params_vdev, false);
	ops->goc_enable(params_vdev, false);
	ops->cproc_enable(params_vdev, false);
	ops->siaf_enable(params_vdev, false);
	ops->siawb_enable(params_vdev, false);
	ops->ie_enable(params_vdev, false);
	ops->yuvae_enable(params_vdev, false);
	ops->wdr_enable(params_vdev, false);
	ops->iesharp_enable(params_vdev, false);
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
	ops->rawnr_enable(params_vdev, false);
	ops->hdrtmo_enable(params_vdev, false);
	ops->gic_enable(params_vdev, false);
	ops->dhaz_enable(params_vdev, false);
	ops->isp3dlut_enable(params_vdev, false);
}

static void
ldch_data_abandon(struct rkisp_isp_params_vdev *params_vdev,
		  struct isp2x_isp_params_cfg *params)
{
	const struct isp2x_ldch_cfg *arg = &params->others.ldch_cfg;
	struct rkisp_isp_params_val_v2x *priv_val;
	struct isp2x_ldch_head *ldch_head;
	int i;

	priv_val = (struct rkisp_isp_params_val_v2x *)params_vdev->priv_val;
	for (i = 0; i < ISP2X_LDCH_BUF_NUM; i++) {
		if (arg->buf_fd == priv_val->buf_ldch[i].dma_fd &&
		    priv_val->buf_ldch[i].vaddr) {
			ldch_head = (struct isp2x_ldch_head *)priv_val->buf_ldch[i].vaddr;
			ldch_head->stat = LDCH_BUF_CHIPINUSE;
			break;
		}
	}
}

static void
rkisp_params_cfg_v2x(struct rkisp_isp_params_vdev *params_vdev,
		     u32 frame_id, enum rkisp_params_type type)
{
	struct isp2x_isp_params_cfg *new_params = NULL;
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

		new_params = (struct isp2x_isp_params_cfg *)(cur_buf->vaddr[0]);
		if (new_params->frame_id < frame_id) {
			list_del(&cur_buf->queue);
			if (list_empty(&params_vdev->params))
				break;
			else if (new_params->module_en_update) {
				/* update en immediately */
				__isp_isr_other_en(params_vdev, new_params, type);
				__isp_isr_meas_en(params_vdev, new_params, type);
			}
			if (new_params->module_cfg_update & ISP2X_MODULE_LDCH)
				ldch_data_abandon(params_vdev, new_params);
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

	new_params = (struct isp2x_isp_params_cfg *)(cur_buf->vaddr[0]);
	__isp_isr_other_config(params_vdev, new_params, type);
	__isp_isr_other_en(params_vdev, new_params, type);
	__isp_isr_meas_config(params_vdev, new_params, type);
	__isp_isr_meas_en(params_vdev, new_params, type);
	if (!hw_dev->is_single && type != RKISP_PARAMS_SHD)
		__isp_config_hdrshd(params_vdev);

	if (type != RKISP_PARAMS_IMD) {
		params_vdev->last_hdrtmo = params_vdev->cur_hdrtmo;
		params_vdev->last_hdrmge = params_vdev->cur_hdrmge;
		params_vdev->cur_hdrtmo = new_params->others.hdrtmo_cfg;
		params_vdev->cur_hdrmge = new_params->others.hdrmge_cfg;
		vb2_buffer_done(&cur_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		cur_buf = NULL;
	}

	params_vdev->exposure = new_params->exposure;
unlock:
	params_vdev->cur_buf = cur_buf;
	spin_unlock(&params_vdev->config_lock);
}

static void isp_hdrtmo_palhpa_reconfig(struct rkisp_isp_params_vdev *params_vdev, u32 lgmean)
{
	u16 set_lgmin, set_lgmax, palpha_0p18;
	u32 palpha, max_palpha;
	u32 cur_frame_id = 0;
	u32 value = 0;

	set_lgmin = params_vdev->cur_hdrtmo.set_lgmin;
	set_lgmax = params_vdev->cur_hdrtmo.set_lgmax;
	palpha_0p18 = params_vdev->cur_hdrtmo.palpha_0p18;
	max_palpha = params_vdev->cur_hdrtmo.maxpalpha;

	palpha = palpha_0p18 * (4 * lgmean - 3 * set_lgmin - set_lgmax) / (set_lgmax - set_lgmin);
	palpha = min(palpha, max_palpha);

	rkisp_dmarx_get_frame(params_vdev->dev, &cur_frame_id, NULL, NULL, true);

	value = rkisp_read(params_vdev->dev, ISP_HDRTMO_LG_CFG0, true) & 0xfffffc00;
	value |= palpha;
	rkisp_write(params_vdev->dev, ISP_HDRTMO_LG_CFG0, value, true);

	v4l2_dbg(5, rkisp_debug, &params_vdev->dev->v4l2_dev,
		 "frame(%d), palpha(%d)\n", cur_frame_id, palpha);
}

static void isp_hdrtmo_lgavgmax_reconfig(struct rkisp_isp_params_vdev *params_vdev,
					 s32 lgmean)
{
	u8 weight_key;
	u16 set_lgmax;
	s32 lgrange1 = 0, lgavgmax = 0;
	u32 cur_frame_id, value;

	set_lgmax = params_vdev->cur_hdrtmo.set_lgmax;
	lgrange1 = params_vdev->cur_hdrtmo.set_lgrange1;
	weight_key = params_vdev->cur_hdrtmo.set_weightkey;

	if (params_vdev->cur_hdrtmo.predict.global_tmo) {
		lgavgmax = lgmean;
	} else {
		lgavgmax = weight_key * set_lgmax + (256 - weight_key) * lgmean;
		lgavgmax = min(lgavgmax / 256, lgrange1);
	}

	value = rkisp_read(params_vdev->dev, ISP_HDRTMO_LG_CFG4, true) & 0xffff0000;
	value |= lgavgmax;
	rkisp_write(params_vdev->dev, ISP_HDRTMO_LG_CFG4, value, true);

	rkisp_dmarx_get_frame(params_vdev->dev, &cur_frame_id, NULL, NULL, true);

	v4l2_dbg(5, rkisp_debug, &params_vdev->dev->v4l2_dev,
		 "frame(%d), global_tmo(%d), lgavgmax(%d)\n",
		 cur_frame_id, params_vdev->cur_hdrtmo.predict.global_tmo, lgavgmax);
}

static void isp_hdrtmo_lgrange1_reconfig(struct rkisp_isp_params_vdev *params_vdev,
					 s32 lgmean)
{
	if (params_vdev->cur_hdrtmo.predict.global_tmo) {
		s32 lgrange1 = 0;
		u32 cur_frame_id, value;

		lgrange1 = lgmean;
		value = rkisp_read(params_vdev->dev, ISP_HDRTMO_LG_CFG3, true) & 0xffff;
		value |= lgrange1 << 16;
		rkisp_write(params_vdev->dev, ISP_HDRTMO_LG_CFG3, value, true);

		rkisp_dmarx_get_frame(params_vdev->dev, &cur_frame_id, NULL, NULL, true);

		v4l2_dbg(5, rkisp_debug, &params_vdev->dev->v4l2_dev,
			 "frame(%d), global_tmo(%d), lgrange1(%d)\n",
			 cur_frame_id, params_vdev->cur_hdrtmo.predict.global_tmo, lgrange1);
	}
}

static u16 isp_hdrtmo_lgmean_reconfig(struct rkisp_isp_params_vdev *params_vdev)
{
	u16 default_lgmean = 40000;
	u16 lgmean = default_lgmean;
	u32 value = 0;
	s32 cur_frame_id = 0;
	static s32 prev_lgmean = 40000;

	rkisp_dmarx_get_frame(params_vdev->dev, &cur_frame_id, NULL, NULL, true);
	if (params_vdev->cur_hdrtmo.predict.iir < params_vdev->cur_hdrtmo.predict.iir_max) {
		u32 ro_lgmean;
		s32 iir = 0;
		s32 global_tmo_strength = params_vdev->cur_hdrtmo.predict.global_tmo_strength;

		value = rkisp_read(params_vdev->dev, ISP_HDRTMO_LG_RO2, true);
		ro_lgmean = value & 0xffff;

		iir = min(cur_frame_id + 1, params_vdev->cur_hdrtmo.predict.iir);
		default_lgmean += global_tmo_strength;
		ro_lgmean +=  global_tmo_strength;
		if (params_vdev->cur_hdrtmo.predict.scene_stable) {
			if (cur_frame_id == 0)
				lgmean = default_lgmean;
			else
				lgmean = ((iir - 1) * prev_lgmean + ro_lgmean) / iir;
		} else {
			if (cur_frame_id == 0)
				lgmean = default_lgmean;
			else
				lgmean = prev_lgmean;
		}
	}

	value = rkisp_read(params_vdev->dev, ISP_HDRTMO_LG_CFG2, true) & 0xffff0000;
	value |= lgmean;
	rkisp_write(params_vdev->dev, ISP_HDRTMO_LG_CFG2, value, true);

	prev_lgmean = lgmean;

	v4l2_dbg(5, rkisp_debug, &params_vdev->dev->v4l2_dev,
		 "frame(%d), scene_stable(%d), k_rolgmean(%d), iir(%d), lgmean(%d)\n",
		 cur_frame_id, params_vdev->cur_hdrtmo.predict.scene_stable,
		 params_vdev->cur_hdrtmo.predict.k_rolgmean,
		 params_vdev->cur_hdrtmo.predict.iir, lgmean);

	return lgmean;
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
			struct rkisp_isp_params_val_v2x *priv_val =
				(struct rkisp_isp_params_val_v2x *)params_vdev->priv_val;

			if (priv_val->delay_en_ldch) {
				struct rkisp_isp_params_v2x_ops *ops =
					(struct rkisp_isp_params_v2x_ops *)params_vdev->priv_ops;

				ops->ldch_enable(params_vdev, true);
				priv_val->delay_en_ldch = false;
			}

			rkisp_params_cfg_v2x(params_vdev, cur_frame_id, RKISP_PARAMS_SHD);
			return;
		}
	}

	if (isp_mis & ISP2X_HDR_DONE) {
		u16 lgmean = 0;

		lgmean = isp_hdrtmo_lgmean_reconfig(params_vdev);
		isp_hdrtmo_palhpa_reconfig(params_vdev, lgmean);
		isp_hdrtmo_lgrange1_reconfig(params_vdev, lgmean);
		isp_hdrtmo_lgavgmax_reconfig(params_vdev, lgmean);

		writel(ISP2X_HDR_DONE, dev->base_addr + ISP_ISP_ICR);
	}

	if ((isp_mis & CIF_ISP_FRAME) && !IS_HDR_RDBK(dev->rd_mode))
		rkisp_params_cfg_v2x(params_vdev, cur_frame_id + 1, RKISP_PARAMS_ALL);
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
	.fop_release = rkisp_params_fop_release_v2x,
};

int rkisp_init_params_vdev_v2x(struct rkisp_isp_params_vdev *params_vdev)
{
	struct device *dev = params_vdev->dev->dev;
	struct rkisp_isp_params_val_v2x *priv_val;
	int i, ret;

	priv_val = kzalloc(sizeof(*priv_val), GFP_KERNEL);
	if (!priv_val) {
		dev_err(dev, "can not get memory\n");
		return -ENOMEM;
	}

	params_vdev->isp2x_params = vmalloc(sizeof(*params_vdev->isp2x_params));
	if (!params_vdev->isp2x_params) {
		dev_err(dev, "call vmalloc failure\n");
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

	rkisp_clear_first_param_v2x(params_vdev);
	params_vdev->priv_val = (void *)priv_val;
	params_vdev->ops = &rkisp_isp_params_ops_tbl;
	params_vdev->priv_ops = &rkisp_v2x_isp_params_ops;
	return 0;

err:
	for (i = 0; i < RKISP_PARAM_3DLUT_BUF_NUM; i++)
		rkisp_free_buffer(params_vdev->dev, &priv_val->buf_3dlut[i]);

	for (i = 0; i < RKISP_PARAM_LSC_LUT_BUF_NUM; i++)
		rkisp_free_buffer(params_vdev->dev, &priv_val->buf_lsclut[i]);
	vfree(params_vdev->isp2x_params);

	return ret;
}

void rkisp_uninit_params_vdev_v2x(struct rkisp_isp_params_vdev *params_vdev)
{
	struct rkisp_isp_params_val_v2x *priv_val;
	int i;

	priv_val = params_vdev->priv_val;
	if (!priv_val)
		return;

	rkisp_deinit_ldch_buf(params_vdev);
	for (i = 0; i < RKISP_PARAM_3DLUT_BUF_NUM; i++)
		rkisp_free_buffer(params_vdev->dev, &priv_val->buf_3dlut[i]);

	for (i = 0; i < RKISP_PARAM_LSC_LUT_BUF_NUM; i++)
		rkisp_free_buffer(params_vdev->dev, &priv_val->buf_lsclut[i]);
	vfree(params_vdev->isp2x_params);
	kfree(priv_val);
	params_vdev->priv_val = NULL;
}


// SPDX-License-Identifier: (GPL-2.0-only OR MIT)
/*
 * Copyright (C) 2024 Amlogic, Inc. All rights reserved
 */

#include <linux/cleanup.h>
#include <linux/media/amlogic/c3-isp-config.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-vmalloc.h>

#include "c3-isp-common.h"
#include "c3-isp-regs.h"

/*
 * union c3_isp_params_block - Generalisation of a parameter block
 *
 * This union allows the driver to treat a block as a generic struct to this
 * union and safely access the header and block-specific struct without having
 * to resort to casting. The header member is accessed first, and the type field
 * checked which allows the driver to determine which of the other members
 * should be used.
 *
 * @header:		The shared header struct embedded as the first member
 *			of all the possible other members. This member would be
 *			accessed first and the type field checked to determine
 *			which of the other members should be accessed.
 * @awb_gains:		For header.type == C3_ISP_PARAMS_BLOCK_AWB_GAINS
 * @awb_cfg:		For header.type == C3_ISP_PARAMS_BLOCK_AWB_CONFIG
 * @ae_cfg:		For header.type == C3_ISP_PARAMS_BLOCK_AE_CONFIG
 * @af_cfg:		For header.type == C3_ISP_PARAMS_BLOCK_AF_CONFIG
 * @pst_gamma:		For header.type == C3_ISP_PARAMS_BLOCK_PST_GAMMA
 * @ccm:		For header.type == C3_ISP_PARAMS_BLOCK_CCM
 * @csc:		For header.type == C3_ISP_PARAMS_BLOCK_CSC
 * @blc:		For header.type == C3_ISP_PARAMS_BLOCK_BLC
 */
union c3_isp_params_block {
	struct c3_isp_params_block_header header;
	struct c3_isp_params_awb_gains awb_gains;
	struct c3_isp_params_awb_config awb_cfg;
	struct c3_isp_params_ae_config ae_cfg;
	struct c3_isp_params_af_config af_cfg;
	struct c3_isp_params_pst_gamma pst_gamma;
	struct c3_isp_params_ccm ccm;
	struct c3_isp_params_csc csc;
	struct c3_isp_params_blc blc;
};

typedef void (*c3_isp_block_handler)(struct c3_isp_device *isp,
				     const union c3_isp_params_block *block);

struct c3_isp_params_handler {
	size_t size;
	c3_isp_block_handler handler;
};

#define to_c3_isp_params_buffer(vbuf) \
	container_of(vbuf, struct c3_isp_params_buffer, vb)

/* Hardware configuration */

static void c3_isp_params_cfg_awb_gains(struct c3_isp_device *isp,
					const union c3_isp_params_block *block)
{
	const struct c3_isp_params_awb_gains *awb_gains = &block->awb_gains;

	if (block->header.flags & C3_ISP_PARAMS_BLOCK_FL_DISABLE) {
		c3_isp_update_bits(isp, ISP_TOP_BEO_CTRL,
				   ISP_TOP_BEO_CTRL_WB_EN_MASK,
				   ISP_TOP_BEO_CTRL_WB_DIS);
		return;
	}

	c3_isp_update_bits(isp, ISP_LSWB_WB_GAIN0,
			   ISP_LSWB_WB_GAIN0_GR_GAIN_MASK,
			   ISP_LSWB_WB_GAIN0_GR_GAIN(awb_gains->gr_gain));
	c3_isp_update_bits(isp, ISP_LSWB_WB_GAIN0,
			   ISP_LSWB_WB_GAIN0_R_GAIN_MASK,
			   ISP_LSWB_WB_GAIN0_R_GAIN(awb_gains->r_gain));
	c3_isp_update_bits(isp, ISP_LSWB_WB_GAIN1,
			   ISP_LSWB_WB_GAIN1_B_GAIN_MASK,
			   ISP_LSWB_WB_GAIN1_B_GAIN(awb_gains->b_gain));
	c3_isp_update_bits(isp, ISP_LSWB_WB_GAIN1,
			   ISP_LSWB_WB_GAIN1_GB_GAIN_MASK,
			   ISP_LSWB_WB_GAIN1_GB_GAIN(awb_gains->gb_gain));
	c3_isp_update_bits(isp, ISP_LSWB_WB_GAIN2,
			   ISP_LSWB_WB_GAIN2_IR_GAIN_MASK,
			   ISP_LSWB_WB_GAIN2_IR_GAIN(awb_gains->gb_gain));

	if (block->header.flags & C3_ISP_PARAMS_BLOCK_FL_ENABLE)
		c3_isp_update_bits(isp, ISP_TOP_BEO_CTRL,
				   ISP_TOP_BEO_CTRL_WB_EN_MASK,
				   ISP_TOP_BEO_CTRL_WB_EN);
}

static void c3_isp_params_awb_wt(struct c3_isp_device *isp,
				 const struct c3_isp_params_awb_config *cfg)
{
	unsigned int zones_num;
	unsigned int base;
	unsigned int data;
	unsigned int i;

	/* Set the weight address to 0 position */
	c3_isp_write(isp, ISP_AWB_BLK_WT_ADDR, 0);

	zones_num = cfg->horiz_zones_num * cfg->vert_zones_num;

	/* Need to write 8 weights at once */
	for (i = 0; i < zones_num / 8; i++) {
		base = i * 8;
		data = ISP_AWB_BLK_WT_DATA_WT(0, cfg->zone_weight[base + 0]) |
		       ISP_AWB_BLK_WT_DATA_WT(1, cfg->zone_weight[base + 1]) |
		       ISP_AWB_BLK_WT_DATA_WT(2, cfg->zone_weight[base + 2]) |
		       ISP_AWB_BLK_WT_DATA_WT(3, cfg->zone_weight[base + 3]) |
		       ISP_AWB_BLK_WT_DATA_WT(4, cfg->zone_weight[base + 4]) |
		       ISP_AWB_BLK_WT_DATA_WT(5, cfg->zone_weight[base + 5]) |
		       ISP_AWB_BLK_WT_DATA_WT(6, cfg->zone_weight[base + 6]) |
		       ISP_AWB_BLK_WT_DATA_WT(7, cfg->zone_weight[base + 7]);
		c3_isp_write(isp, ISP_AWB_BLK_WT_DATA, data);
	}

	if (zones_num % 8 == 0)
		return;

	data = 0;
	base = i * 8;

	for (i = 0; i < zones_num % 8; i++)
		data |= ISP_AWB_BLK_WT_DATA_WT(i, cfg->zone_weight[base + i]);

	c3_isp_write(isp, ISP_AWB_BLK_WT_DATA, data);
}

static void c3_isp_params_awb_cood(struct c3_isp_device *isp,
				   const struct c3_isp_params_awb_config *cfg)
{
	unsigned int max_point_num;

	/* The number of points is one more than the number of edges */
	max_point_num = max(cfg->horiz_zones_num, cfg->vert_zones_num) + 1;

	/* Set the index address to 0 position */
	c3_isp_write(isp, ISP_AWB_IDX_ADDR, 0);

	for (unsigned int i = 0; i < max_point_num; i++)
		c3_isp_write(isp, ISP_AWB_IDX_DATA,
			     ISP_AWB_IDX_DATA_HIDX_DATA(cfg->horiz_coord[i]) |
			     ISP_AWB_IDX_DATA_VIDX_DATA(cfg->vert_coord[i]));
}

static void c3_isp_params_cfg_awb_config(struct c3_isp_device *isp,
					 const union c3_isp_params_block *block)
{
	const struct c3_isp_params_awb_config *awb_cfg = &block->awb_cfg;

	if (block->header.flags & C3_ISP_PARAMS_BLOCK_FL_DISABLE) {
		c3_isp_update_bits(isp, ISP_TOP_3A_STAT_CRTL,
				   ISP_TOP_3A_STAT_CRTL_AWB_STAT_EN_MASK,
				   ISP_TOP_3A_STAT_CRTL_AWB_STAT_DIS);
		return;
	}

	c3_isp_update_bits(isp, ISP_TOP_3A_STAT_CRTL,
			   ISP_TOP_3A_STAT_CRTL_AWB_POINT_MASK,
			   ISP_TOP_3A_STAT_CRTL_AWB_POINT(awb_cfg->tap_point));

	c3_isp_update_bits(isp, ISP_AWB_STAT_CTRL2,
			   ISP_AWB_STAT_CTRL2_SATUR_CTRL_MASK,
			   ISP_AWB_STAT_CTRL2_SATUR_CTRL(awb_cfg->satur_vald));

	c3_isp_update_bits(isp, ISP_AWB_HV_BLKNUM,
			   ISP_AWB_HV_BLKNUM_H_NUM_MASK,
			   ISP_AWB_HV_BLKNUM_H_NUM(awb_cfg->horiz_zones_num));
	c3_isp_update_bits(isp, ISP_AWB_HV_BLKNUM,
			   ISP_AWB_HV_BLKNUM_V_NUM_MASK,
			   ISP_AWB_HV_BLKNUM_V_NUM(awb_cfg->vert_zones_num));

	c3_isp_update_bits(isp, ISP_AWB_STAT_RG, ISP_AWB_STAT_RG_MIN_VALUE_MASK,
			   ISP_AWB_STAT_RG_MIN_VALUE(awb_cfg->rg_min));
	c3_isp_update_bits(isp, ISP_AWB_STAT_RG, ISP_AWB_STAT_RG_MAX_VALUE_MASK,
			   ISP_AWB_STAT_RG_MAX_VALUE(awb_cfg->rg_max));

	c3_isp_update_bits(isp, ISP_AWB_STAT_BG, ISP_AWB_STAT_BG_MIN_VALUE_MASK,
			   ISP_AWB_STAT_BG_MIN_VALUE(awb_cfg->bg_min));
	c3_isp_update_bits(isp, ISP_AWB_STAT_BG, ISP_AWB_STAT_BG_MAX_VALUE_MASK,
			   ISP_AWB_STAT_BG_MAX_VALUE(awb_cfg->bg_max));

	c3_isp_update_bits(isp, ISP_AWB_STAT_RG_HL,
			   ISP_AWB_STAT_RG_HL_LOW_VALUE_MASK,
			   ISP_AWB_STAT_RG_HL_LOW_VALUE(awb_cfg->rg_low));
	c3_isp_update_bits(isp, ISP_AWB_STAT_RG_HL,
			   ISP_AWB_STAT_RG_HL_HIGH_VALUE_MASK,
			   ISP_AWB_STAT_RG_HL_HIGH_VALUE(awb_cfg->rg_high));

	c3_isp_update_bits(isp, ISP_AWB_STAT_BG_HL,
			   ISP_AWB_STAT_BG_HL_LOW_VALUE_MASK,
			   ISP_AWB_STAT_BG_HL_LOW_VALUE(awb_cfg->bg_low));
	c3_isp_update_bits(isp, ISP_AWB_STAT_BG_HL,
			   ISP_AWB_STAT_BG_HL_HIGH_VALUE_MASK,
			   ISP_AWB_STAT_BG_HL_HIGH_VALUE(awb_cfg->bg_high));

	c3_isp_params_awb_wt(isp, awb_cfg);
	c3_isp_params_awb_cood(isp, awb_cfg);

	if (block->header.flags & C3_ISP_PARAMS_BLOCK_FL_ENABLE)
		c3_isp_update_bits(isp, ISP_TOP_3A_STAT_CRTL,
				   ISP_TOP_3A_STAT_CRTL_AWB_STAT_EN_MASK,
				   ISP_TOP_3A_STAT_CRTL_AWB_STAT_EN);
}

static void c3_isp_params_ae_wt(struct c3_isp_device *isp,
				const struct c3_isp_params_ae_config *cfg)
{
	unsigned int zones_num;
	unsigned int base;
	unsigned int data;
	unsigned int i;

	/* Set the weight address to 0 position */
	c3_isp_write(isp, ISP_AE_BLK_WT_ADDR, 0);

	zones_num = cfg->horiz_zones_num * cfg->vert_zones_num;

	/* Need to write 8 weights at once */
	for (i = 0; i < zones_num / 8; i++) {
		base = i * 8;
		data = ISP_AE_BLK_WT_DATA_WT(0, cfg->zone_weight[base + 0]) |
		       ISP_AE_BLK_WT_DATA_WT(1, cfg->zone_weight[base + 1]) |
		       ISP_AE_BLK_WT_DATA_WT(2, cfg->zone_weight[base + 2]) |
		       ISP_AE_BLK_WT_DATA_WT(3, cfg->zone_weight[base + 3]) |
		       ISP_AE_BLK_WT_DATA_WT(4, cfg->zone_weight[base + 4]) |
		       ISP_AE_BLK_WT_DATA_WT(5, cfg->zone_weight[base + 5]) |
		       ISP_AE_BLK_WT_DATA_WT(6, cfg->zone_weight[base + 6]) |
		       ISP_AE_BLK_WT_DATA_WT(7, cfg->zone_weight[base + 7]);
		c3_isp_write(isp, ISP_AE_BLK_WT_DATA, data);
	}

	if (zones_num % 8 == 0)
		return;

	data = 0;
	base = i * 8;

	/* Write the last weights data */
	for (i = 0; i < zones_num % 8; i++)
		data |= ISP_AE_BLK_WT_DATA_WT(i, cfg->zone_weight[base + i]);

	c3_isp_write(isp, ISP_AE_BLK_WT_DATA, data);
}

static void c3_isp_params_ae_cood(struct c3_isp_device *isp,
				  const struct c3_isp_params_ae_config *cfg)
{
	unsigned int max_point_num;

	/* The number of points is one more than the number of edges */
	max_point_num = max(cfg->horiz_zones_num, cfg->vert_zones_num) + 1;

	/* Set the index address to 0 position */
	c3_isp_write(isp, ISP_AE_IDX_ADDR, 0);

	for (unsigned int i = 0; i < max_point_num; i++)
		c3_isp_write(isp, ISP_AE_IDX_DATA,
			     ISP_AE_IDX_DATA_HIDX_DATA(cfg->horiz_coord[i]) |
			     ISP_AE_IDX_DATA_VIDX_DATA(cfg->vert_coord[i]));
}

static void c3_isp_params_cfg_ae_config(struct c3_isp_device *isp,
					const union c3_isp_params_block *block)
{
	const struct c3_isp_params_ae_config *ae_cfg = &block->ae_cfg;

	if (block->header.flags & C3_ISP_PARAMS_BLOCK_FL_DISABLE) {
		c3_isp_update_bits(isp, ISP_TOP_3A_STAT_CRTL,
				   ISP_TOP_3A_STAT_CRTL_AE_STAT_EN_MASK,
				   ISP_TOP_3A_STAT_CRTL_AE_STAT_DIS);
		return;
	}

	c3_isp_update_bits(isp, ISP_TOP_3A_STAT_CRTL,
			   ISP_TOP_3A_STAT_CRTL_AE_POINT_MASK,
			   ISP_TOP_3A_STAT_CRTL_AE_POINT(ae_cfg->tap_point));

	if (ae_cfg->tap_point == C3_ISP_AE_STATS_TAP_GE)
		c3_isp_update_bits(isp, ISP_AE_CTRL,
				   ISP_AE_CTRL_INPUT_2LINE_MASK,
				   ISP_AE_CTRL_INPUT_2LINE_EN);
	else
		c3_isp_update_bits(isp, ISP_AE_CTRL,
				   ISP_AE_CTRL_INPUT_2LINE_MASK,
				   ISP_AE_CTRL_INPUT_2LINE_DIS);

	c3_isp_update_bits(isp, ISP_AE_HV_BLKNUM,
			   ISP_AE_HV_BLKNUM_H_NUM_MASK,
			   ISP_AE_HV_BLKNUM_H_NUM(ae_cfg->horiz_zones_num));
	c3_isp_update_bits(isp, ISP_AE_HV_BLKNUM,
			   ISP_AE_HV_BLKNUM_V_NUM_MASK,
			   ISP_AE_HV_BLKNUM_V_NUM(ae_cfg->vert_zones_num));

	c3_isp_params_ae_wt(isp, ae_cfg);
	c3_isp_params_ae_cood(isp, ae_cfg);

	if (block->header.flags & C3_ISP_PARAMS_BLOCK_FL_ENABLE)
		c3_isp_update_bits(isp, ISP_TOP_3A_STAT_CRTL,
				   ISP_TOP_3A_STAT_CRTL_AE_STAT_EN_MASK,
				   ISP_TOP_3A_STAT_CRTL_AE_STAT_EN);
}

static void c3_isp_params_af_cood(struct c3_isp_device *isp,
				  const struct c3_isp_params_af_config *cfg)
{
	unsigned int max_point_num;

	/* The number of points is one more than the number of edges */
	max_point_num = max(cfg->horiz_zones_num, cfg->vert_zones_num) + 1;

	/* Set the index address to 0 position */
	c3_isp_write(isp, ISP_AF_IDX_ADDR, 0);

	for (unsigned int i = 0; i < max_point_num; i++)
		c3_isp_write(isp, ISP_AF_IDX_DATA,
			     ISP_AF_IDX_DATA_HIDX_DATA(cfg->horiz_coord[i]) |
			     ISP_AF_IDX_DATA_VIDX_DATA(cfg->vert_coord[i]));
}

static void c3_isp_params_cfg_af_config(struct c3_isp_device *isp,
					const union c3_isp_params_block *block)
{
	const struct c3_isp_params_af_config *af_cfg = &block->af_cfg;

	if (block->header.flags & C3_ISP_PARAMS_BLOCK_FL_DISABLE) {
		c3_isp_update_bits(isp, ISP_TOP_3A_STAT_CRTL,
				   ISP_TOP_3A_STAT_CRTL_AF_STAT_EN_MASK,
				   ISP_TOP_3A_STAT_CRTL_AF_STAT_DIS);
		return;
	}

	c3_isp_update_bits(isp, ISP_TOP_3A_STAT_CRTL,
			   ISP_TOP_3A_STAT_CRTL_AF_POINT_MASK,
			   ISP_TOP_3A_STAT_CRTL_AF_POINT(af_cfg->tap_point));

	c3_isp_update_bits(isp, ISP_AF_HV_BLKNUM,
			   ISP_AF_HV_BLKNUM_H_NUM_MASK,
			   ISP_AF_HV_BLKNUM_H_NUM(af_cfg->horiz_zones_num));
	c3_isp_update_bits(isp, ISP_AF_HV_BLKNUM,
			   ISP_AF_HV_BLKNUM_V_NUM_MASK,
			   ISP_AF_HV_BLKNUM_V_NUM(af_cfg->vert_zones_num));

	c3_isp_params_af_cood(isp, af_cfg);

	if (block->header.flags & C3_ISP_PARAMS_BLOCK_FL_ENABLE)
		c3_isp_update_bits(isp, ISP_TOP_3A_STAT_CRTL,
				   ISP_TOP_3A_STAT_CRTL_AF_STAT_EN_MASK,
				   ISP_TOP_3A_STAT_CRTL_AF_STAT_EN);
}

static void c3_isp_params_cfg_pst_gamma(struct c3_isp_device *isp,
					const union c3_isp_params_block *block)
{
	const struct c3_isp_params_pst_gamma *gm = &block->pst_gamma;
	unsigned int base;
	unsigned int i;

	if (block->header.flags & C3_ISP_PARAMS_BLOCK_FL_DISABLE) {
		c3_isp_update_bits(isp, ISP_TOP_BED_CTRL,
				   ISP_TOP_BED_CTRL_PST_GAMMA_EN_MASK,
				   ISP_TOP_BED_CTRL_PST_GAMMA_DIS);
		return;
	}

	/* R, G and B channels use the same gamma lut */
	for (unsigned int j = 0; j < 3; j++) {
		/* Set the channel lut address */
		c3_isp_write(isp, ISP_PST_GAMMA_LUT_ADDR,
			     ISP_PST_GAMMA_LUT_ADDR_IDX_ADDR(j));

		/* Need to write 2 lut values at once */
		for (i = 0; i < ARRAY_SIZE(gm->lut) / 2; i++) {
			base = i * 2;
			c3_isp_write(isp, ISP_PST_GAMMA_LUT_DATA,
				     ISP_PST_GM_LUT_DATA0(gm->lut[base]) |
				     ISP_PST_GM_LUT_DATA1(gm->lut[base + 1]));
		}

		/* Write the last one */
		if (ARRAY_SIZE(gm->lut) % 2) {
			base = i * 2;
			c3_isp_write(isp, ISP_PST_GAMMA_LUT_DATA,
				     ISP_PST_GM_LUT_DATA0(gm->lut[base]));
		}
	}

	if (block->header.flags & C3_ISP_PARAMS_BLOCK_FL_ENABLE)
		c3_isp_update_bits(isp, ISP_TOP_BED_CTRL,
				   ISP_TOP_BED_CTRL_PST_GAMMA_EN_MASK,
				   ISP_TOP_BED_CTRL_PST_GAMMA_EN);
}

/* Configure 3 x 3 ccm matrix */
static void c3_isp_params_cfg_ccm(struct c3_isp_device *isp,
				  const union c3_isp_params_block *block)
{
	const struct c3_isp_params_ccm *ccm = &block->ccm;

	if (block->header.flags & C3_ISP_PARAMS_BLOCK_FL_DISABLE) {
		c3_isp_update_bits(isp, ISP_TOP_BED_CTRL,
				   ISP_TOP_BED_CTRL_CCM_EN_MASK,
				   ISP_TOP_BED_CTRL_CCM_DIS);
		return;
	}

	c3_isp_update_bits(isp, ISP_CCM_MTX_00_01,
			   ISP_CCM_MTX_00_01_MTX_00_MASK,
			   ISP_CCM_MTX_00_01_MTX_00(ccm->matrix[0][0]));
	c3_isp_update_bits(isp, ISP_CCM_MTX_00_01,
			   ISP_CCM_MTX_00_01_MTX_01_MASK,
			   ISP_CCM_MTX_00_01_MTX_01(ccm->matrix[0][1]));
	c3_isp_update_bits(isp, ISP_CCM_MTX_02_03,
			   ISP_CCM_MTX_02_03_MTX_02_MASK,
			   ISP_CCM_MTX_02_03_MTX_02(ccm->matrix[0][2]));

	c3_isp_update_bits(isp, ISP_CCM_MTX_10_11,
			   ISP_CCM_MTX_10_11_MTX_10_MASK,
			   ISP_CCM_MTX_10_11_MTX_10(ccm->matrix[1][0]));
	c3_isp_update_bits(isp, ISP_CCM_MTX_10_11,
			   ISP_CCM_MTX_10_11_MTX_11_MASK,
			   ISP_CCM_MTX_10_11_MTX_11(ccm->matrix[1][1]));
	c3_isp_update_bits(isp, ISP_CCM_MTX_12_13,
			   ISP_CCM_MTX_12_13_MTX_12_MASK,
			   ISP_CCM_MTX_12_13_MTX_12(ccm->matrix[1][2]));

	c3_isp_update_bits(isp, ISP_CCM_MTX_20_21,
			   ISP_CCM_MTX_20_21_MTX_20_MASK,
			   ISP_CCM_MTX_20_21_MTX_20(ccm->matrix[2][0]));
	c3_isp_update_bits(isp, ISP_CCM_MTX_20_21,
			   ISP_CCM_MTX_20_21_MTX_21_MASK,
			   ISP_CCM_MTX_20_21_MTX_21(ccm->matrix[2][1]));
	c3_isp_update_bits(isp, ISP_CCM_MTX_22_23_RS,
			   ISP_CCM_MTX_22_23_RS_MTX_22_MASK,
			   ISP_CCM_MTX_22_23_RS_MTX_22(ccm->matrix[2][2]));

	if (block->header.flags & C3_ISP_PARAMS_BLOCK_FL_ENABLE)
		c3_isp_update_bits(isp, ISP_TOP_BED_CTRL,
				   ISP_TOP_BED_CTRL_CCM_EN_MASK,
				   ISP_TOP_BED_CTRL_CCM_EN);
}

/* Configure color space conversion matrix parameters */
static void c3_isp_params_cfg_csc(struct c3_isp_device *isp,
				  const union c3_isp_params_block *block)
{
	const struct c3_isp_params_csc *csc = &block->csc;

	if (block->header.flags & C3_ISP_PARAMS_BLOCK_FL_DISABLE) {
		c3_isp_update_bits(isp, ISP_TOP_BED_CTRL,
				   ISP_TOP_BED_CTRL_CM0_EN_MASK,
				   ISP_TOP_BED_CTRL_CM0_DIS);
		return;
	}

	c3_isp_update_bits(isp, ISP_CM0_COEF00_01,
			   ISP_CM0_COEF00_01_MTX_00_MASK,
			   ISP_CM0_COEF00_01_MTX_00(csc->matrix[0][0]));
	c3_isp_update_bits(isp, ISP_CM0_COEF00_01,
			   ISP_CM0_COEF00_01_MTX_01_MASK,
			   ISP_CM0_COEF00_01_MTX_01(csc->matrix[0][1]));
	c3_isp_update_bits(isp, ISP_CM0_COEF02_10,
			   ISP_CM0_COEF02_10_MTX_02_MASK,
			   ISP_CM0_COEF02_10_MTX_02(csc->matrix[0][2]));

	c3_isp_update_bits(isp, ISP_CM0_COEF02_10,
			   ISP_CM0_COEF02_10_MTX_10_MASK,
			   ISP_CM0_COEF02_10_MTX_10(csc->matrix[1][0]));
	c3_isp_update_bits(isp, ISP_CM0_COEF11_12,
			   ISP_CM0_COEF11_12_MTX_11_MASK,
			   ISP_CM0_COEF11_12_MTX_11(csc->matrix[1][1]));
	c3_isp_update_bits(isp, ISP_CM0_COEF11_12,
			   ISP_CM0_COEF11_12_MTX_12_MASK,
			   ISP_CM0_COEF11_12_MTX_12(csc->matrix[1][2]));

	c3_isp_update_bits(isp, ISP_CM0_COEF20_21,
			   ISP_CM0_COEF20_21_MTX_20_MASK,
			   ISP_CM0_COEF20_21_MTX_20(csc->matrix[2][0]));
	c3_isp_update_bits(isp, ISP_CM0_COEF20_21,
			   ISP_CM0_COEF20_21_MTX_21_MASK,
			   ISP_CM0_COEF20_21_MTX_21(csc->matrix[2][1]));
	c3_isp_update_bits(isp, ISP_CM0_COEF22_OUP_OFST0,
			   ISP_CM0_COEF22_OUP_OFST0_MTX_22_MASK,
			   ISP_CM0_COEF22_OUP_OFST0_MTX_22(csc->matrix[2][2]));

	if (block->header.flags & C3_ISP_PARAMS_BLOCK_FL_ENABLE)
		c3_isp_update_bits(isp, ISP_TOP_BED_CTRL,
				   ISP_TOP_BED_CTRL_CM0_EN_MASK,
				   ISP_TOP_BED_CTRL_CM0_EN);
}

/* Set blc offset of each color channel */
static void c3_isp_params_cfg_blc(struct c3_isp_device *isp,
				  const union c3_isp_params_block *block)
{
	const struct c3_isp_params_blc *blc = &block->blc;

	if (block->header.flags & C3_ISP_PARAMS_BLOCK_FL_DISABLE) {
		c3_isp_update_bits(isp, ISP_TOP_BEO_CTRL,
				   ISP_TOP_BEO_CTRL_BLC_EN_MASK,
				   ISP_TOP_BEO_CTRL_BLC_DIS);
		return;
	}

	c3_isp_write(isp, ISP_LSWB_BLC_OFST0,
		     ISP_LSWB_BLC_OFST0_R_OFST(blc->r_ofst) |
		     ISP_LSWB_BLC_OFST0_GR_OFST(blc->gr_ofst));
	c3_isp_write(isp, ISP_LSWB_BLC_OFST1,
		     ISP_LSWB_BLC_OFST1_GB_OFST(blc->gb_ofst) |
		     ISP_LSWB_BLC_OFST1_B_OFST(blc->b_ofst));

	if (block->header.flags & C3_ISP_PARAMS_BLOCK_FL_ENABLE)
		c3_isp_update_bits(isp, ISP_TOP_BEO_CTRL,
				   ISP_TOP_BEO_CTRL_BLC_EN_MASK,
				   ISP_TOP_BEO_CTRL_BLC_EN);
}

static const struct c3_isp_params_handler c3_isp_params_handlers[] = {
	[C3_ISP_PARAMS_BLOCK_AWB_GAINS] = {
		.size = sizeof(struct c3_isp_params_awb_gains),
		.handler = c3_isp_params_cfg_awb_gains,
	},
	[C3_ISP_PARAMS_BLOCK_AWB_CONFIG] = {
		.size = sizeof(struct c3_isp_params_awb_config),
		.handler = c3_isp_params_cfg_awb_config,
	},
	[C3_ISP_PARAMS_BLOCK_AE_CONFIG] = {
		.size = sizeof(struct c3_isp_params_ae_config),
		.handler = c3_isp_params_cfg_ae_config,
	},
	[C3_ISP_PARAMS_BLOCK_AF_CONFIG] = {
		.size = sizeof(struct c3_isp_params_af_config),
		.handler = c3_isp_params_cfg_af_config,
	},
	[C3_ISP_PARAMS_BLOCK_PST_GAMMA] = {
		.size = sizeof(struct c3_isp_params_pst_gamma),
		.handler = c3_isp_params_cfg_pst_gamma,
	},
	[C3_ISP_PARAMS_BLOCK_CCM] = {
		.size = sizeof(struct c3_isp_params_ccm),
		.handler = c3_isp_params_cfg_ccm,
	},
	[C3_ISP_PARAMS_BLOCK_CSC] = {
		.size = sizeof(struct c3_isp_params_csc),
		.handler = c3_isp_params_cfg_csc,
	},
	[C3_ISP_PARAMS_BLOCK_BLC] = {
		.size = sizeof(struct c3_isp_params_blc),
		.handler = c3_isp_params_cfg_blc,
	},
};

static void c3_isp_params_cfg_blocks(struct c3_isp_params *params)
{
	struct c3_isp_params_cfg *config = params->buff->cfg;
	size_t block_offset = 0;

	if (WARN_ON(!config))
		return;

	/* Walk the list of parameter blocks and process them */
	while (block_offset < config->data_size) {
		const struct c3_isp_params_handler *block_handler;
		const union c3_isp_params_block *block;

		block = (const union c3_isp_params_block *)
			 &config->data[block_offset];

		block_handler = &c3_isp_params_handlers[block->header.type];
		block_handler->handler(params->isp, block);

		block_offset += block->header.size;
	}
}

void c3_isp_params_pre_cfg(struct c3_isp_device *isp)
{
	struct c3_isp_params *params = &isp->params;

	/* Disable some unused modules */
	c3_isp_update_bits(isp, ISP_TOP_FEO_CTRL0,
			   ISP_TOP_FEO_CTRL0_INPUT_FMT_EN_MASK,
			   ISP_TOP_FEO_CTRL0_INPUT_FMT_DIS);

	c3_isp_update_bits(isp, ISP_TOP_FEO_CTRL1_0,
			   ISP_TOP_FEO_CTRL1_0_DPC_EN_MASK,
			   ISP_TOP_FEO_CTRL1_0_DPC_DIS);
	c3_isp_update_bits(isp, ISP_TOP_FEO_CTRL1_0,
			   ISP_TOP_FEO_CTRL1_0_OG_EN_MASK,
			   ISP_TOP_FEO_CTRL1_0_OG_DIS);

	c3_isp_update_bits(isp, ISP_TOP_FED_CTRL, ISP_TOP_FED_CTRL_PDPC_EN_MASK,
			   ISP_TOP_FED_CTRL_PDPC_DIS);
	c3_isp_update_bits(isp, ISP_TOP_FED_CTRL,
			   ISP_TOP_FED_CTRL_RAWCNR_EN_MASK,
			   ISP_TOP_FED_CTRL_RAWCNR_DIS);
	c3_isp_update_bits(isp, ISP_TOP_FED_CTRL, ISP_TOP_FED_CTRL_SNR1_EN_MASK,
			   ISP_TOP_FED_CTRL_SNR1_DIS);
	c3_isp_update_bits(isp, ISP_TOP_FED_CTRL, ISP_TOP_FED_CTRL_TNR0_EN_MASK,
			   ISP_TOP_FED_CTRL_TNR0_DIS);
	c3_isp_update_bits(isp, ISP_TOP_FED_CTRL,
			   ISP_TOP_FED_CTRL_CUBIC_CS_EN_MASK,
			   ISP_TOP_FED_CTRL_CUBIC_CS_DIS);
	c3_isp_update_bits(isp, ISP_TOP_FED_CTRL, ISP_TOP_FED_CTRL_SQRT_EN_MASK,
			   ISP_TOP_FED_CTRL_SQRT_DIS);
	c3_isp_update_bits(isp, ISP_TOP_FED_CTRL,
			   ISP_TOP_FED_CTRL_DGAIN_EN_MASK,
			   ISP_TOP_FED_CTRL_DGAIN_DIS);

	c3_isp_update_bits(isp, ISP_TOP_BEO_CTRL,
			   ISP_TOP_BEO_CTRL_INV_DGAIN_EN_MASK,
			   ISP_TOP_BEO_CTRL_INV_DGAIN_DIS);
	c3_isp_update_bits(isp, ISP_TOP_BEO_CTRL, ISP_TOP_BEO_CTRL_EOTF_EN_MASK,
			   ISP_TOP_BEO_CTRL_EOTF_DIS);

	c3_isp_update_bits(isp, ISP_TOP_BED_CTRL,
			   ISP_TOP_BED_CTRL_YHS_STAT_EN_MASK,
			   ISP_TOP_BED_CTRL_YHS_STAT_DIS);
	c3_isp_update_bits(isp, ISP_TOP_BED_CTRL,
			   ISP_TOP_BED_CTRL_GRPH_STAT_EN_MASK,
			   ISP_TOP_BED_CTRL_GRPH_STAT_DIS);
	c3_isp_update_bits(isp, ISP_TOP_BED_CTRL,
			   ISP_TOP_BED_CTRL_FMETER_EN_MASK,
			   ISP_TOP_BED_CTRL_FMETER_DIS);
	c3_isp_update_bits(isp, ISP_TOP_BED_CTRL, ISP_TOP_BED_CTRL_BSC_EN_MASK,
			   ISP_TOP_BED_CTRL_BSC_DIS);
	c3_isp_update_bits(isp, ISP_TOP_BED_CTRL, ISP_TOP_BED_CTRL_CNR2_EN_MASK,
			   ISP_TOP_BED_CTRL_CNR2_DIS);
	c3_isp_update_bits(isp, ISP_TOP_BED_CTRL, ISP_TOP_BED_CTRL_CM1_EN_MASK,
			   ISP_TOP_BED_CTRL_CM1_DIS);
	c3_isp_update_bits(isp, ISP_TOP_BED_CTRL,
			   ISP_TOP_BED_CTRL_LUT3D_EN_MASK,
			   ISP_TOP_BED_CTRL_LUT3D_DIS);
	c3_isp_update_bits(isp, ISP_TOP_BED_CTRL,
			   ISP_TOP_BED_CTRL_PST_TNR_LITE_EN_MASK,
			   ISP_TOP_BED_CTRL_PST_TNR_LITE_DIS);
	c3_isp_update_bits(isp, ISP_TOP_BED_CTRL, ISP_TOP_BED_CTRL_AMCM_EN_MASK,
			   ISP_TOP_BED_CTRL_AMCM_DIS);

	/*
	 * Disable AE, AF and AWB stat module. Please configure the parameters
	 * in userspace algorithm if need to enable these switch.
	 */
	c3_isp_update_bits(isp, ISP_TOP_3A_STAT_CRTL,
			   ISP_TOP_3A_STAT_CRTL_AE_STAT_EN_MASK,
			   ISP_TOP_3A_STAT_CRTL_AE_STAT_DIS);
	c3_isp_update_bits(isp, ISP_TOP_3A_STAT_CRTL,
			   ISP_TOP_3A_STAT_CRTL_AWB_STAT_EN_MASK,
			   ISP_TOP_3A_STAT_CRTL_AWB_STAT_DIS);
	c3_isp_update_bits(isp, ISP_TOP_3A_STAT_CRTL,
			   ISP_TOP_3A_STAT_CRTL_AF_STAT_EN_MASK,
			   ISP_TOP_3A_STAT_CRTL_AF_STAT_DIS);

	c3_isp_write(isp, ISP_LSWB_WB_LIMIT0,
		     ISP_LSWB_WB_LIMIT0_WB_LIMIT_R_MAX |
		     ISP_LSWB_WB_LIMIT0_WB_LIMIT_GR_MAX);
	c3_isp_write(isp, ISP_LSWB_WB_LIMIT1,
		     ISP_LSWB_WB_LIMIT1_WB_LIMIT_GB_MAX |
		     ISP_LSWB_WB_LIMIT1_WB_LIMIT_B_MAX);

	guard(spinlock_irqsave)(&params->buff_lock);

	/* Only use the first buffer to initialize ISP */
	params->buff =
		list_first_entry_or_null(&params->pending,
					 struct c3_isp_params_buffer, list);
	if (params->buff)
		c3_isp_params_cfg_blocks(params);
}

/* V4L2 video operations */

static int c3_isp_params_querycap(struct file *file, void *fh,
				  struct v4l2_capability *cap)
{
	strscpy(cap->driver, C3_ISP_DRIVER_NAME, sizeof(cap->driver));
	strscpy(cap->card, "AML C3 ISP", sizeof(cap->card));

	return 0;
}

static int c3_isp_params_enum_fmt(struct file *file, void *fh,
				  struct v4l2_fmtdesc *f)
{
	if (f->index)
		return -EINVAL;

	f->pixelformat = V4L2_META_FMT_C3ISP_PARAMS;

	return 0;
}

static int c3_isp_params_g_fmt(struct file *file, void *fh,
			       struct v4l2_format *f)
{
	struct c3_isp_params *params = video_drvdata(file);

	f->fmt.meta = params->vfmt.fmt.meta;

	return 0;
}

static const struct v4l2_ioctl_ops isp_params_v4l2_ioctl_ops = {
	.vidioc_querycap                = c3_isp_params_querycap,
	.vidioc_enum_fmt_meta_out       = c3_isp_params_enum_fmt,
	.vidioc_g_fmt_meta_out          = c3_isp_params_g_fmt,
	.vidioc_s_fmt_meta_out          = c3_isp_params_g_fmt,
	.vidioc_try_fmt_meta_out        = c3_isp_params_g_fmt,
	.vidioc_reqbufs                 = vb2_ioctl_reqbufs,
	.vidioc_querybuf                = vb2_ioctl_querybuf,
	.vidioc_qbuf                    = vb2_ioctl_qbuf,
	.vidioc_expbuf                  = vb2_ioctl_expbuf,
	.vidioc_dqbuf                   = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf             = vb2_ioctl_prepare_buf,
	.vidioc_create_bufs             = vb2_ioctl_create_bufs,
	.vidioc_streamon                = vb2_ioctl_streamon,
	.vidioc_streamoff               = vb2_ioctl_streamoff,
};

static const struct v4l2_file_operations isp_params_v4l2_fops = {
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.poll = vb2_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = vb2_fop_mmap,
};

static int c3_isp_params_vb2_queue_setup(struct vb2_queue *q,
					 unsigned int *num_buffers,
					 unsigned int *num_planes,
					 unsigned int sizes[],
					 struct device *alloc_devs[])
{
	if (*num_planes) {
		if (*num_planes != 1)
			return -EINVAL;

		if (sizes[0] < sizeof(struct c3_isp_params_cfg))
			return -EINVAL;

		return 0;
	}

	*num_planes = 1;
	sizes[0] = sizeof(struct c3_isp_params_cfg);

	return 0;
}

static void c3_isp_params_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *v4l2_buf = to_vb2_v4l2_buffer(vb);
	struct c3_isp_params_buffer *buf = to_c3_isp_params_buffer(v4l2_buf);
	struct c3_isp_params *params = vb2_get_drv_priv(vb->vb2_queue);

	guard(spinlock_irqsave)(&params->buff_lock);

	list_add_tail(&buf->list, &params->pending);
}

static int c3_isp_params_vb2_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct c3_isp_params_buffer *buf = to_c3_isp_params_buffer(vbuf);
	struct c3_isp_params *params = vb2_get_drv_priv(vb->vb2_queue);
	struct c3_isp_params_cfg *cfg = buf->cfg;
	struct c3_isp_params_cfg *usr_cfg = vb2_plane_vaddr(vb, 0);
	size_t payload_size = vb2_get_plane_payload(vb, 0);
	size_t header_size = offsetof(struct c3_isp_params_cfg, data);
	size_t block_offset = 0;
	size_t cfg_size;

	/* Payload size can't be greater than the destination buffer size */
	if (payload_size > params->vfmt.fmt.meta.buffersize) {
		dev_dbg(params->isp->dev,
			"Payload size is too large: %zu\n", payload_size);
		return -EINVAL;
	}

	/* Payload size can't be smaller than the header size */
	if (payload_size < header_size) {
		dev_dbg(params->isp->dev,
			"Payload size is too small: %zu\n", payload_size);
		return -EINVAL;
	}

	/*
	 * Use the internal scratch buffer to avoid userspace modifying
	 * the buffer content while the driver is processing it.
	 */
	memcpy(cfg, usr_cfg, payload_size);

	/* Only v0 is supported at the moment */
	if (cfg->version != C3_ISP_PARAMS_BUFFER_V0) {
		dev_dbg(params->isp->dev,
			"Invalid params buffer version: %u\n", cfg->version);
		return -EINVAL;
	}

	/* Validate the size reported in the parameter buffer header */
	cfg_size = header_size + cfg->data_size;
	if (cfg_size != payload_size) {
		dev_dbg(params->isp->dev,
			"Data size %zu and payload size %zu are different\n",
			cfg_size, payload_size);
		return -EINVAL;
	}

	/* Walk the list of parameter blocks and validate them */
	cfg_size = cfg->data_size;
	while (cfg_size >= sizeof(struct c3_isp_params_block_header)) {
		const struct c3_isp_params_block_header *block;
		const struct c3_isp_params_handler *handler;

		block = (struct c3_isp_params_block_header *)
			&cfg->data[block_offset];

		if (block->type >= ARRAY_SIZE(c3_isp_params_handlers)) {
			dev_dbg(params->isp->dev,
				"Invalid params block type\n");
			return -EINVAL;
		}

		if (block->size > cfg_size) {
			dev_dbg(params->isp->dev,
				"Block size is greater than cfg size\n");
			return -EINVAL;
		}

		if ((block->flags & (C3_ISP_PARAMS_BLOCK_FL_ENABLE |
				     C3_ISP_PARAMS_BLOCK_FL_DISABLE)) ==
		    (C3_ISP_PARAMS_BLOCK_FL_ENABLE |
		     C3_ISP_PARAMS_BLOCK_FL_DISABLE)) {
			dev_dbg(params->isp->dev,
				"Invalid parameters block flags\n");
			return -EINVAL;
		}

		handler = &c3_isp_params_handlers[block->type];
		if (block->size != handler->size) {
			dev_dbg(params->isp->dev,
				"Invalid params block size\n");
			return -EINVAL;
		}

		block_offset += block->size;
		cfg_size -= block->size;
	}

	if (cfg_size) {
		dev_dbg(params->isp->dev,
			"Unexpected data after the params buffer end\n");
		return -EINVAL;
	}

	return 0;
}

static int c3_isp_params_vb2_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *v4l2_buf = to_vb2_v4l2_buffer(vb);
	struct c3_isp_params *params = vb2_get_drv_priv(vb->vb2_queue);
	struct c3_isp_params_buffer *buf = to_c3_isp_params_buffer(v4l2_buf);

	buf->cfg = kvmalloc(params->vfmt.fmt.meta.buffersize, GFP_KERNEL);
	if (!buf->cfg)
		return -ENOMEM;

	return 0;
}

static void c3_isp_params_vb2_buf_cleanup(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *v4l2_buf = to_vb2_v4l2_buffer(vb);
	struct c3_isp_params_buffer *buf = to_c3_isp_params_buffer(v4l2_buf);

	kvfree(buf->cfg);
	buf->cfg = NULL;
}

static void c3_isp_params_vb2_stop_streaming(struct vb2_queue *q)
{
	struct c3_isp_params *params = vb2_get_drv_priv(q);
	struct c3_isp_params_buffer *buff;

	guard(spinlock_irqsave)(&params->buff_lock);

	while (!list_empty(&params->pending)) {
		buff = list_first_entry(&params->pending,
					struct c3_isp_params_buffer, list);
		list_del(&buff->list);
		vb2_buffer_done(&buff->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}
}

static const struct vb2_ops isp_params_vb2_ops = {
	.queue_setup = c3_isp_params_vb2_queue_setup,
	.buf_queue = c3_isp_params_vb2_buf_queue,
	.buf_prepare = c3_isp_params_vb2_buf_prepare,
	.buf_init = c3_isp_params_vb2_buf_init,
	.buf_cleanup = c3_isp_params_vb2_buf_cleanup,
	.stop_streaming = c3_isp_params_vb2_stop_streaming,
};

int c3_isp_params_register(struct c3_isp_device *isp)
{
	struct c3_isp_params *params = &isp->params;
	struct video_device *vdev = &params->vdev;
	struct vb2_queue *vb2_q = &params->vb2_q;
	int ret;

	memset(params, 0, sizeof(*params));
	params->vfmt.fmt.meta.dataformat = V4L2_META_FMT_C3ISP_PARAMS;
	params->vfmt.fmt.meta.buffersize = sizeof(struct c3_isp_params_cfg);
	params->isp = isp;
	INIT_LIST_HEAD(&params->pending);
	spin_lock_init(&params->buff_lock);
	mutex_init(&params->lock);

	snprintf(vdev->name, sizeof(vdev->name), "c3-isp-params");
	vdev->fops = &isp_params_v4l2_fops;
	vdev->ioctl_ops = &isp_params_v4l2_ioctl_ops;
	vdev->v4l2_dev = &isp->v4l2_dev;
	vdev->lock = &params->lock;
	vdev->minor = -1;
	vdev->queue = vb2_q;
	vdev->release = video_device_release_empty;
	vdev->device_caps = V4L2_CAP_META_OUTPUT | V4L2_CAP_STREAMING;
	vdev->vfl_dir = VFL_DIR_TX;
	video_set_drvdata(vdev, params);

	vb2_q->drv_priv = params;
	vb2_q->mem_ops = &vb2_vmalloc_memops;
	vb2_q->ops = &isp_params_vb2_ops;
	vb2_q->type = V4L2_BUF_TYPE_META_OUTPUT;
	vb2_q->io_modes = VB2_DMABUF | VB2_MMAP;
	vb2_q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	vb2_q->buf_struct_size = sizeof(struct c3_isp_params_buffer);
	vb2_q->dev = isp->dev;
	vb2_q->lock = &params->lock;
	vb2_q->min_queued_buffers = 1;

	ret = vb2_queue_init(vb2_q);
	if (ret)
		goto err_detroy;

	params->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&vdev->entity, 1, &params->pad);
	if (ret)
		goto err_queue_release;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		dev_err(isp->dev,
			"Failed to register %s: %d\n", vdev->name, ret);
		goto err_entity_cleanup;
	}

	return 0;

err_entity_cleanup:
	media_entity_cleanup(&vdev->entity);
err_queue_release:
	vb2_queue_release(vb2_q);
err_detroy:
	mutex_destroy(&params->lock);
	return ret;
}

void c3_isp_params_unregister(struct c3_isp_device *isp)
{
	struct c3_isp_params *params = &isp->params;

	vb2_queue_release(&params->vb2_q);
	media_entity_cleanup(&params->vdev.entity);
	video_unregister_device(&params->vdev);
	mutex_destroy(&params->lock);
}

void c3_isp_params_isr(struct c3_isp_device *isp)
{
	struct c3_isp_params *params = &isp->params;

	guard(spinlock_irqsave)(&params->buff_lock);

	params->buff =
		list_first_entry_or_null(&params->pending,
					 struct c3_isp_params_buffer, list);
	if (!params->buff)
		return;

	list_del(&params->buff->list);

	c3_isp_params_cfg_blocks(params);

	params->buff->vb.sequence = params->isp->frm_sequence;
	params->buff->vb.vb2_buf.timestamp = ktime_get();
	params->buff->vb.field = V4L2_FIELD_NONE;
	vb2_buffer_done(&params->buff->vb.vb2_buf, VB2_BUF_STATE_DONE);
}

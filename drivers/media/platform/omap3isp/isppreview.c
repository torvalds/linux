/*
 * isppreview.c
 *
 * TI OMAP3 ISP driver - Preview module
 *
 * Copyright (C) 2010 Nokia Corporation
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/device.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#include "isp.h"
#include "ispreg.h"
#include "isppreview.h"

/* Default values in Office Fluorescent Light for RGBtoRGB Blending */
static struct omap3isp_prev_rgbtorgb flr_rgb2rgb = {
	{	/* RGB-RGB Matrix */
		{0x01E2, 0x0F30, 0x0FEE},
		{0x0F9B, 0x01AC, 0x0FB9},
		{0x0FE0, 0x0EC0, 0x0260}
	},	/* RGB Offset */
	{0x0000, 0x0000, 0x0000}
};

/* Default values in Office Fluorescent Light for RGB to YUV Conversion*/
static struct omap3isp_prev_csc flr_prev_csc = {
	{	/* CSC Coef Matrix */
		{66, 129, 25},
		{-38, -75, 112},
		{112, -94 , -18}
	},	/* CSC Offset */
	{0x0, 0x0, 0x0}
};

/* Default values in Office Fluorescent Light for CFA Gradient*/
#define FLR_CFA_GRADTHRS_HORZ	0x28
#define FLR_CFA_GRADTHRS_VERT	0x28

/* Default values in Office Fluorescent Light for Chroma Suppression*/
#define FLR_CSUP_GAIN		0x0D
#define FLR_CSUP_THRES		0xEB

/* Default values in Office Fluorescent Light for Noise Filter*/
#define FLR_NF_STRGTH		0x03

/* Default values for White Balance */
#define FLR_WBAL_DGAIN		0x100
#define FLR_WBAL_COEF		0x20

/* Default values in Office Fluorescent Light for Black Adjustment*/
#define FLR_BLKADJ_BLUE		0x0
#define FLR_BLKADJ_GREEN	0x0
#define FLR_BLKADJ_RED		0x0

#define DEF_DETECT_CORRECT_VAL	0xe

/*
 * Margins and image size limits.
 *
 * The preview engine crops several rows and columns internally depending on
 * which filters are enabled. To avoid format changes when the filters are
 * enabled or disabled (which would prevent them from being turned on or off
 * during streaming), the driver assumes all filters that can be configured
 * during streaming are enabled when computing sink crop and source format
 * limits.
 *
 * If a filter is disabled, additional cropping is automatically added at the
 * preview engine input by the driver to avoid overflow at line and frame end.
 * This is completely transparent for applications.
 *
 * Median filter		4 pixels
 * Noise filter,
 * Faulty pixels correction	4 pixels, 4 lines
 * Color suppression		2 pixels
 * or luma enhancement
 * -------------------------------------------------------------
 * Maximum total		10 pixels, 4 lines
 *
 * The color suppression and luma enhancement filters are applied after bayer to
 * YUV conversion. They thus can crop one pixel on the left and one pixel on the
 * right side of the image without changing the color pattern. When both those
 * filters are disabled, the driver must crop the two pixels on the same side of
 * the image to avoid changing the bayer pattern. The left margin is thus set to
 * 6 pixels and the right margin to 4 pixels.
 */

#define PREV_MARGIN_LEFT	6
#define PREV_MARGIN_RIGHT	4
#define PREV_MARGIN_TOP		2
#define PREV_MARGIN_BOTTOM	2

#define PREV_MIN_IN_WIDTH	64
#define PREV_MIN_IN_HEIGHT	8
#define PREV_MAX_IN_HEIGHT	16384

#define PREV_MIN_OUT_WIDTH		0
#define PREV_MIN_OUT_HEIGHT		0
#define PREV_MAX_OUT_WIDTH_REV_1	1280
#define PREV_MAX_OUT_WIDTH_REV_2	3300
#define PREV_MAX_OUT_WIDTH_REV_15	4096

/*
 * Coeficient Tables for the submodules in Preview.
 * Array is initialised with the values from.the tables text file.
 */

/*
 * CFA Filter Coefficient Table
 *
 */
static u32 cfa_coef_table[4][OMAP3ISP_PREV_CFA_BLK_SIZE] = {
#include "cfa_coef_table.h"
};

/*
 * Default Gamma Correction Table - All components
 */
static u32 gamma_table[] = {
#include "gamma_table.h"
};

/*
 * Noise Filter Threshold table
 */
static u32 noise_filter_table[] = {
#include "noise_filter_table.h"
};

/*
 * Luminance Enhancement Table
 */
static u32 luma_enhance_table[] = {
#include "luma_enhance_table.h"
};

/*
 * preview_config_luma_enhancement - Configure the Luminance Enhancement table
 */
static void
preview_config_luma_enhancement(struct isp_prev_device *prev,
				const struct prev_params *params)
{
	struct isp_device *isp = to_isp_device(prev);
	const struct omap3isp_prev_luma *yt = &params->luma;
	unsigned int i;

	isp_reg_writel(isp, ISPPRV_YENH_TABLE_ADDR,
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_ADDR);
	for (i = 0; i < OMAP3ISP_PREV_YENH_TBL_SIZE; i++) {
		isp_reg_writel(isp, yt->table[i],
			       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_DATA);
	}
}

/*
 * preview_enable_luma_enhancement - Enable/disable Luminance Enhancement
 */
static void
preview_enable_luma_enhancement(struct isp_prev_device *prev, bool enable)
{
	struct isp_device *isp = to_isp_device(prev);

	if (enable)
		isp_reg_set(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_YNENHEN);
	else
		isp_reg_clr(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_YNENHEN);
}

/*
 * preview_enable_invalaw - Enable/disable Inverse A-Law decompression
 */
static void preview_enable_invalaw(struct isp_prev_device *prev, bool enable)
{
	struct isp_device *isp = to_isp_device(prev);

	if (enable)
		isp_reg_set(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_INVALAW);
	else
		isp_reg_clr(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_INVALAW);
}

/*
 * preview_config_hmed - Configure the Horizontal Median Filter
 */
static void preview_config_hmed(struct isp_prev_device *prev,
				const struct prev_params *params)
{
	struct isp_device *isp = to_isp_device(prev);
	const struct omap3isp_prev_hmed *hmed = &params->hmed;

	isp_reg_writel(isp, (hmed->odddist == 1 ? 0 : ISPPRV_HMED_ODDDIST) |
		       (hmed->evendist == 1 ? 0 : ISPPRV_HMED_EVENDIST) |
		       (hmed->thres << ISPPRV_HMED_THRESHOLD_SHIFT),
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_HMED);
}

/*
 * preview_enable_hmed - Enable/disable the Horizontal Median Filter
 */
static void preview_enable_hmed(struct isp_prev_device *prev, bool enable)
{
	struct isp_device *isp = to_isp_device(prev);

	if (enable)
		isp_reg_set(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_HMEDEN);
	else
		isp_reg_clr(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_HMEDEN);
}

/*
 * preview_config_cfa - Configure CFA Interpolation for Bayer formats
 *
 * The CFA table is organised in four blocks, one per Bayer component. The
 * hardware expects blocks to follow the Bayer order of the input data, while
 * the driver stores the table in GRBG order in memory. The blocks need to be
 * reordered to support non-GRBG Bayer patterns.
 */
static void preview_config_cfa(struct isp_prev_device *prev,
			       const struct prev_params *params)
{
	static const unsigned int cfa_coef_order[4][4] = {
		{ 0, 1, 2, 3 }, /* GRBG */
		{ 1, 0, 3, 2 }, /* RGGB */
		{ 2, 3, 0, 1 }, /* BGGR */
		{ 3, 2, 1, 0 }, /* GBRG */
	};
	const unsigned int *order = cfa_coef_order[prev->params.cfa_order];
	const struct omap3isp_prev_cfa *cfa = &params->cfa;
	struct isp_device *isp = to_isp_device(prev);
	unsigned int i;
	unsigned int j;

	isp_reg_writel(isp,
		(cfa->gradthrs_vert << ISPPRV_CFA_GRADTH_VER_SHIFT) |
		(cfa->gradthrs_horz << ISPPRV_CFA_GRADTH_HOR_SHIFT),
		OMAP3_ISP_IOMEM_PREV, ISPPRV_CFA);

	isp_reg_writel(isp, ISPPRV_CFA_TABLE_ADDR,
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_ADDR);

	for (i = 0; i < 4; ++i) {
		const __u32 *block = cfa->table[order[i]];

		for (j = 0; j < OMAP3ISP_PREV_CFA_BLK_SIZE; ++j)
			isp_reg_writel(isp, block[j], OMAP3_ISP_IOMEM_PREV,
				       ISPPRV_SET_TBL_DATA);
	}
}

/*
 * preview_config_chroma_suppression - Configure Chroma Suppression
 */
static void
preview_config_chroma_suppression(struct isp_prev_device *prev,
				  const struct prev_params *params)
{
	struct isp_device *isp = to_isp_device(prev);
	const struct omap3isp_prev_csup *cs = &params->csup;

	isp_reg_writel(isp,
		       cs->gain | (cs->thres << ISPPRV_CSUP_THRES_SHIFT) |
		       (cs->hypf_en << ISPPRV_CSUP_HPYF_SHIFT),
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_CSUP);
}

/*
 * preview_enable_chroma_suppression - Enable/disable Chrominance Suppression
 */
static void
preview_enable_chroma_suppression(struct isp_prev_device *prev, bool enable)
{
	struct isp_device *isp = to_isp_device(prev);

	if (enable)
		isp_reg_set(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_SUPEN);
	else
		isp_reg_clr(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_SUPEN);
}

/*
 * preview_config_whitebalance - Configure White Balance parameters
 *
 * Coefficient matrix always with default values.
 */
static void
preview_config_whitebalance(struct isp_prev_device *prev,
			    const struct prev_params *params)
{
	struct isp_device *isp = to_isp_device(prev);
	const struct omap3isp_prev_wbal *wbal = &params->wbal;
	u32 val;

	isp_reg_writel(isp, wbal->dgain, OMAP3_ISP_IOMEM_PREV, ISPPRV_WB_DGAIN);

	val = wbal->coef0 << ISPPRV_WBGAIN_COEF0_SHIFT;
	val |= wbal->coef1 << ISPPRV_WBGAIN_COEF1_SHIFT;
	val |= wbal->coef2 << ISPPRV_WBGAIN_COEF2_SHIFT;
	val |= wbal->coef3 << ISPPRV_WBGAIN_COEF3_SHIFT;
	isp_reg_writel(isp, val, OMAP3_ISP_IOMEM_PREV, ISPPRV_WBGAIN);

	isp_reg_writel(isp,
		       ISPPRV_WBSEL_COEF0 << ISPPRV_WBSEL_N0_0_SHIFT |
		       ISPPRV_WBSEL_COEF1 << ISPPRV_WBSEL_N0_1_SHIFT |
		       ISPPRV_WBSEL_COEF0 << ISPPRV_WBSEL_N0_2_SHIFT |
		       ISPPRV_WBSEL_COEF1 << ISPPRV_WBSEL_N0_3_SHIFT |
		       ISPPRV_WBSEL_COEF2 << ISPPRV_WBSEL_N1_0_SHIFT |
		       ISPPRV_WBSEL_COEF3 << ISPPRV_WBSEL_N1_1_SHIFT |
		       ISPPRV_WBSEL_COEF2 << ISPPRV_WBSEL_N1_2_SHIFT |
		       ISPPRV_WBSEL_COEF3 << ISPPRV_WBSEL_N1_3_SHIFT |
		       ISPPRV_WBSEL_COEF0 << ISPPRV_WBSEL_N2_0_SHIFT |
		       ISPPRV_WBSEL_COEF1 << ISPPRV_WBSEL_N2_1_SHIFT |
		       ISPPRV_WBSEL_COEF0 << ISPPRV_WBSEL_N2_2_SHIFT |
		       ISPPRV_WBSEL_COEF1 << ISPPRV_WBSEL_N2_3_SHIFT |
		       ISPPRV_WBSEL_COEF2 << ISPPRV_WBSEL_N3_0_SHIFT |
		       ISPPRV_WBSEL_COEF3 << ISPPRV_WBSEL_N3_1_SHIFT |
		       ISPPRV_WBSEL_COEF2 << ISPPRV_WBSEL_N3_2_SHIFT |
		       ISPPRV_WBSEL_COEF3 << ISPPRV_WBSEL_N3_3_SHIFT,
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_WBSEL);
}

/*
 * preview_config_blkadj - Configure Black Adjustment
 */
static void
preview_config_blkadj(struct isp_prev_device *prev,
		      const struct prev_params *params)
{
	struct isp_device *isp = to_isp_device(prev);
	const struct omap3isp_prev_blkadj *blkadj = &params->blkadj;

	isp_reg_writel(isp, (blkadj->blue << ISPPRV_BLKADJOFF_B_SHIFT) |
		       (blkadj->green << ISPPRV_BLKADJOFF_G_SHIFT) |
		       (blkadj->red << ISPPRV_BLKADJOFF_R_SHIFT),
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_BLKADJOFF);
}

/*
 * preview_config_rgb_blending - Configure RGB-RGB Blending
 */
static void
preview_config_rgb_blending(struct isp_prev_device *prev,
			    const struct prev_params *params)
{
	struct isp_device *isp = to_isp_device(prev);
	const struct omap3isp_prev_rgbtorgb *rgbrgb = &params->rgb2rgb;
	u32 val;

	val = (rgbrgb->matrix[0][0] & 0xfff) << ISPPRV_RGB_MAT1_MTX_RR_SHIFT;
	val |= (rgbrgb->matrix[0][1] & 0xfff) << ISPPRV_RGB_MAT1_MTX_GR_SHIFT;
	isp_reg_writel(isp, val, OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_MAT1);

	val = (rgbrgb->matrix[0][2] & 0xfff) << ISPPRV_RGB_MAT2_MTX_BR_SHIFT;
	val |= (rgbrgb->matrix[1][0] & 0xfff) << ISPPRV_RGB_MAT2_MTX_RG_SHIFT;
	isp_reg_writel(isp, val, OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_MAT2);

	val = (rgbrgb->matrix[1][1] & 0xfff) << ISPPRV_RGB_MAT3_MTX_GG_SHIFT;
	val |= (rgbrgb->matrix[1][2] & 0xfff) << ISPPRV_RGB_MAT3_MTX_BG_SHIFT;
	isp_reg_writel(isp, val, OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_MAT3);

	val = (rgbrgb->matrix[2][0] & 0xfff) << ISPPRV_RGB_MAT4_MTX_RB_SHIFT;
	val |= (rgbrgb->matrix[2][1] & 0xfff) << ISPPRV_RGB_MAT4_MTX_GB_SHIFT;
	isp_reg_writel(isp, val, OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_MAT4);

	val = (rgbrgb->matrix[2][2] & 0xfff) << ISPPRV_RGB_MAT5_MTX_BB_SHIFT;
	isp_reg_writel(isp, val, OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_MAT5);

	val = (rgbrgb->offset[0] & 0x3ff) << ISPPRV_RGB_OFF1_MTX_OFFR_SHIFT;
	val |= (rgbrgb->offset[1] & 0x3ff) << ISPPRV_RGB_OFF1_MTX_OFFG_SHIFT;
	isp_reg_writel(isp, val, OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_OFF1);

	val = (rgbrgb->offset[2] & 0x3ff) << ISPPRV_RGB_OFF2_MTX_OFFB_SHIFT;
	isp_reg_writel(isp, val, OMAP3_ISP_IOMEM_PREV, ISPPRV_RGB_OFF2);
}

/*
 * preview_config_csc - Configure Color Space Conversion (RGB to YCbYCr)
 */
static void
preview_config_csc(struct isp_prev_device *prev,
		   const struct prev_params *params)
{
	struct isp_device *isp = to_isp_device(prev);
	const struct omap3isp_prev_csc *csc = &params->csc;
	u32 val;

	val = (csc->matrix[0][0] & 0x3ff) << ISPPRV_CSC0_RY_SHIFT;
	val |= (csc->matrix[0][1] & 0x3ff) << ISPPRV_CSC0_GY_SHIFT;
	val |= (csc->matrix[0][2] & 0x3ff) << ISPPRV_CSC0_BY_SHIFT;
	isp_reg_writel(isp, val, OMAP3_ISP_IOMEM_PREV, ISPPRV_CSC0);

	val = (csc->matrix[1][0] & 0x3ff) << ISPPRV_CSC1_RCB_SHIFT;
	val |= (csc->matrix[1][1] & 0x3ff) << ISPPRV_CSC1_GCB_SHIFT;
	val |= (csc->matrix[1][2] & 0x3ff) << ISPPRV_CSC1_BCB_SHIFT;
	isp_reg_writel(isp, val, OMAP3_ISP_IOMEM_PREV, ISPPRV_CSC1);

	val = (csc->matrix[2][0] & 0x3ff) << ISPPRV_CSC2_RCR_SHIFT;
	val |= (csc->matrix[2][1] & 0x3ff) << ISPPRV_CSC2_GCR_SHIFT;
	val |= (csc->matrix[2][2] & 0x3ff) << ISPPRV_CSC2_BCR_SHIFT;
	isp_reg_writel(isp, val, OMAP3_ISP_IOMEM_PREV, ISPPRV_CSC2);

	val = (csc->offset[0] & 0xff) << ISPPRV_CSC_OFFSET_Y_SHIFT;
	val |= (csc->offset[1] & 0xff) << ISPPRV_CSC_OFFSET_CB_SHIFT;
	val |= (csc->offset[2] & 0xff) << ISPPRV_CSC_OFFSET_CR_SHIFT;
	isp_reg_writel(isp, val, OMAP3_ISP_IOMEM_PREV, ISPPRV_CSC_OFFSET);
}

/*
 * preview_config_yc_range - Configure the max and min Y and C values
 */
static void
preview_config_yc_range(struct isp_prev_device *prev,
			const struct prev_params *params)
{
	struct isp_device *isp = to_isp_device(prev);
	const struct omap3isp_prev_yclimit *yc = &params->yclimit;

	isp_reg_writel(isp,
		       yc->maxC << ISPPRV_SETUP_YC_MAXC_SHIFT |
		       yc->maxY << ISPPRV_SETUP_YC_MAXY_SHIFT |
		       yc->minC << ISPPRV_SETUP_YC_MINC_SHIFT |
		       yc->minY << ISPPRV_SETUP_YC_MINY_SHIFT,
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_SETUP_YC);
}

/*
 * preview_config_dcor - Configure Couplet Defect Correction
 */
static void
preview_config_dcor(struct isp_prev_device *prev,
		    const struct prev_params *params)
{
	struct isp_device *isp = to_isp_device(prev);
	const struct omap3isp_prev_dcor *dcor = &params->dcor;

	isp_reg_writel(isp, dcor->detect_correct[0],
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_CDC_THR0);
	isp_reg_writel(isp, dcor->detect_correct[1],
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_CDC_THR1);
	isp_reg_writel(isp, dcor->detect_correct[2],
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_CDC_THR2);
	isp_reg_writel(isp, dcor->detect_correct[3],
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_CDC_THR3);
	isp_reg_clr_set(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			ISPPRV_PCR_DCCOUP,
			dcor->couplet_mode_en ? ISPPRV_PCR_DCCOUP : 0);
}

/*
 * preview_enable_dcor - Enable/disable Couplet Defect Correction
 */
static void preview_enable_dcor(struct isp_prev_device *prev, bool enable)
{
	struct isp_device *isp = to_isp_device(prev);

	if (enable)
		isp_reg_set(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_DCOREN);
	else
		isp_reg_clr(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_DCOREN);
}

/*
 * preview_enable_drkframe_capture - Enable/disable Dark Frame Capture
 */
static void
preview_enable_drkframe_capture(struct isp_prev_device *prev, bool enable)
{
	struct isp_device *isp = to_isp_device(prev);

	if (enable)
		isp_reg_set(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_DRKFCAP);
	else
		isp_reg_clr(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_DRKFCAP);
}

/*
 * preview_enable_drkframe - Enable/disable Dark Frame Subtraction
 */
static void preview_enable_drkframe(struct isp_prev_device *prev, bool enable)
{
	struct isp_device *isp = to_isp_device(prev);

	if (enable)
		isp_reg_set(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_DRKFEN);
	else
		isp_reg_clr(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_DRKFEN);
}

/*
 * preview_config_noisefilter - Configure the Noise Filter
 */
static void
preview_config_noisefilter(struct isp_prev_device *prev,
			   const struct prev_params *params)
{
	struct isp_device *isp = to_isp_device(prev);
	const struct omap3isp_prev_nf *nf = &params->nf;
	unsigned int i;

	isp_reg_writel(isp, nf->spread, OMAP3_ISP_IOMEM_PREV, ISPPRV_NF);
	isp_reg_writel(isp, ISPPRV_NF_TABLE_ADDR,
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_ADDR);
	for (i = 0; i < OMAP3ISP_PREV_NF_TBL_SIZE; i++) {
		isp_reg_writel(isp, nf->table[i],
			       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_DATA);
	}
}

/*
 * preview_enable_noisefilter - Enable/disable the Noise Filter
 */
static void
preview_enable_noisefilter(struct isp_prev_device *prev, bool enable)
{
	struct isp_device *isp = to_isp_device(prev);

	if (enable)
		isp_reg_set(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_NFEN);
	else
		isp_reg_clr(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_NFEN);
}

/*
 * preview_config_gammacorrn - Configure the Gamma Correction tables
 */
static void
preview_config_gammacorrn(struct isp_prev_device *prev,
			  const struct prev_params *params)
{
	struct isp_device *isp = to_isp_device(prev);
	const struct omap3isp_prev_gtables *gt = &params->gamma;
	unsigned int i;

	isp_reg_writel(isp, ISPPRV_REDGAMMA_TABLE_ADDR,
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_ADDR);
	for (i = 0; i < OMAP3ISP_PREV_GAMMA_TBL_SIZE; i++)
		isp_reg_writel(isp, gt->red[i], OMAP3_ISP_IOMEM_PREV,
			       ISPPRV_SET_TBL_DATA);

	isp_reg_writel(isp, ISPPRV_GREENGAMMA_TABLE_ADDR,
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_ADDR);
	for (i = 0; i < OMAP3ISP_PREV_GAMMA_TBL_SIZE; i++)
		isp_reg_writel(isp, gt->green[i], OMAP3_ISP_IOMEM_PREV,
			       ISPPRV_SET_TBL_DATA);

	isp_reg_writel(isp, ISPPRV_BLUEGAMMA_TABLE_ADDR,
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_SET_TBL_ADDR);
	for (i = 0; i < OMAP3ISP_PREV_GAMMA_TBL_SIZE; i++)
		isp_reg_writel(isp, gt->blue[i], OMAP3_ISP_IOMEM_PREV,
			       ISPPRV_SET_TBL_DATA);
}

/*
 * preview_enable_gammacorrn - Enable/disable Gamma Correction
 *
 * When gamma correction is disabled, the module is bypassed and its output is
 * the 8 MSB of the 10-bit input .
 */
static void
preview_enable_gammacorrn(struct isp_prev_device *prev, bool enable)
{
	struct isp_device *isp = to_isp_device(prev);

	if (enable)
		isp_reg_clr(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_GAMMA_BYPASS);
	else
		isp_reg_set(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_GAMMA_BYPASS);
}

/*
 * preview_config_contrast - Configure the Contrast
 *
 * Value should be programmed before enabling the module.
 */
static void
preview_config_contrast(struct isp_prev_device *prev,
			const struct prev_params *params)
{
	struct isp_device *isp = to_isp_device(prev);

	isp_reg_clr_set(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_CNT_BRT,
			0xff << ISPPRV_CNT_BRT_CNT_SHIFT,
			params->contrast << ISPPRV_CNT_BRT_CNT_SHIFT);
}

/*
 * preview_config_brightness - Configure the Brightness
 */
static void
preview_config_brightness(struct isp_prev_device *prev,
			  const struct prev_params *params)
{
	struct isp_device *isp = to_isp_device(prev);

	isp_reg_clr_set(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_CNT_BRT,
			0xff << ISPPRV_CNT_BRT_BRT_SHIFT,
			params->brightness << ISPPRV_CNT_BRT_BRT_SHIFT);
}

/*
 * preview_update_contrast - Updates the contrast.
 * @contrast: Pointer to hold the current programmed contrast value.
 *
 * Value should be programmed before enabling the module.
 */
static void
preview_update_contrast(struct isp_prev_device *prev, u8 contrast)
{
	struct prev_params *params;
	unsigned long flags;

	spin_lock_irqsave(&prev->params.lock, flags);
	params = (prev->params.active & OMAP3ISP_PREV_CONTRAST)
	       ? &prev->params.params[0] : &prev->params.params[1];

	if (params->contrast != (contrast * ISPPRV_CONTRAST_UNITS)) {
		params->contrast = contrast * ISPPRV_CONTRAST_UNITS;
		params->update |= OMAP3ISP_PREV_CONTRAST;
	}
	spin_unlock_irqrestore(&prev->params.lock, flags);
}

/*
 * preview_update_brightness - Updates the brightness in preview module.
 * @brightness: Pointer to hold the current programmed brightness value.
 *
 */
static void
preview_update_brightness(struct isp_prev_device *prev, u8 brightness)
{
	struct prev_params *params;
	unsigned long flags;

	spin_lock_irqsave(&prev->params.lock, flags);
	params = (prev->params.active & OMAP3ISP_PREV_BRIGHTNESS)
	       ? &prev->params.params[0] : &prev->params.params[1];

	if (params->brightness != (brightness * ISPPRV_BRIGHT_UNITS)) {
		params->brightness = brightness * ISPPRV_BRIGHT_UNITS;
		params->update |= OMAP3ISP_PREV_BRIGHTNESS;
	}
	spin_unlock_irqrestore(&prev->params.lock, flags);
}

static u32
preview_params_lock(struct isp_prev_device *prev, u32 update, bool shadow)
{
	u32 active = prev->params.active;

	if (shadow) {
		/* Mark all shadow parameters we are going to touch as busy. */
		prev->params.params[0].busy |= ~active & update;
		prev->params.params[1].busy |= active & update;
	} else {
		/* Mark all active parameters we are going to touch as busy. */
		update = (prev->params.params[0].update & active)
		       | (prev->params.params[1].update & ~active);

		prev->params.params[0].busy |= active & update;
		prev->params.params[1].busy |= ~active & update;
	}

	return update;
}

static void
preview_params_unlock(struct isp_prev_device *prev, u32 update, bool shadow)
{
	u32 active = prev->params.active;

	if (shadow) {
		/* Set the update flag for shadow parameters that have been
		 * updated and clear the busy flag for all shadow parameters.
		 */
		prev->params.params[0].update |= (~active & update);
		prev->params.params[1].update |= (active & update);
		prev->params.params[0].busy &= active;
		prev->params.params[1].busy &= ~active;
	} else {
		/* Clear the update flag for active parameters that have been
		 * applied and the busy flag for all active parameters.
		 */
		prev->params.params[0].update &= ~(active & update);
		prev->params.params[1].update &= ~(~active & update);
		prev->params.params[0].busy &= ~active;
		prev->params.params[1].busy &= active;
	}
}

static void preview_params_switch(struct isp_prev_device *prev)
{
	u32 to_switch;

	/* Switch active parameters with updated shadow parameters when the
	 * shadow parameter has been updated and neither the active not the
	 * shadow parameter is busy.
	 */
	to_switch = (prev->params.params[0].update & ~prev->params.active)
		  | (prev->params.params[1].update & prev->params.active);
	to_switch &= ~(prev->params.params[0].busy |
		       prev->params.params[1].busy);
	if (to_switch == 0)
		return;

	prev->params.active ^= to_switch;

	/* Remove the update flag for the shadow copy of parameters we have
	 * switched.
	 */
	prev->params.params[0].update &= ~(~prev->params.active & to_switch);
	prev->params.params[1].update &= ~(prev->params.active & to_switch);
}

/* preview parameters update structure */
struct preview_update {
	void (*config)(struct isp_prev_device *, const struct prev_params *);
	void (*enable)(struct isp_prev_device *, bool);
	unsigned int param_offset;
	unsigned int param_size;
	unsigned int config_offset;
	bool skip;
};

/* Keep the array indexed by the OMAP3ISP_PREV_* bit number. */
static const struct preview_update update_attrs[] = {
	/* OMAP3ISP_PREV_LUMAENH */ {
		preview_config_luma_enhancement,
		preview_enable_luma_enhancement,
		offsetof(struct prev_params, luma),
		FIELD_SIZEOF(struct prev_params, luma),
		offsetof(struct omap3isp_prev_update_config, luma),
	}, /* OMAP3ISP_PREV_INVALAW */ {
		NULL,
		preview_enable_invalaw,
	}, /* OMAP3ISP_PREV_HRZ_MED */ {
		preview_config_hmed,
		preview_enable_hmed,
		offsetof(struct prev_params, hmed),
		FIELD_SIZEOF(struct prev_params, hmed),
		offsetof(struct omap3isp_prev_update_config, hmed),
	}, /* OMAP3ISP_PREV_CFA */ {
		preview_config_cfa,
		NULL,
		offsetof(struct prev_params, cfa),
		FIELD_SIZEOF(struct prev_params, cfa),
		offsetof(struct omap3isp_prev_update_config, cfa),
	}, /* OMAP3ISP_PREV_CHROMA_SUPP */ {
		preview_config_chroma_suppression,
		preview_enable_chroma_suppression,
		offsetof(struct prev_params, csup),
		FIELD_SIZEOF(struct prev_params, csup),
		offsetof(struct omap3isp_prev_update_config, csup),
	}, /* OMAP3ISP_PREV_WB */ {
		preview_config_whitebalance,
		NULL,
		offsetof(struct prev_params, wbal),
		FIELD_SIZEOF(struct prev_params, wbal),
		offsetof(struct omap3isp_prev_update_config, wbal),
	}, /* OMAP3ISP_PREV_BLKADJ */ {
		preview_config_blkadj,
		NULL,
		offsetof(struct prev_params, blkadj),
		FIELD_SIZEOF(struct prev_params, blkadj),
		offsetof(struct omap3isp_prev_update_config, blkadj),
	}, /* OMAP3ISP_PREV_RGB2RGB */ {
		preview_config_rgb_blending,
		NULL,
		offsetof(struct prev_params, rgb2rgb),
		FIELD_SIZEOF(struct prev_params, rgb2rgb),
		offsetof(struct omap3isp_prev_update_config, rgb2rgb),
	}, /* OMAP3ISP_PREV_COLOR_CONV */ {
		preview_config_csc,
		NULL,
		offsetof(struct prev_params, csc),
		FIELD_SIZEOF(struct prev_params, csc),
		offsetof(struct omap3isp_prev_update_config, csc),
	}, /* OMAP3ISP_PREV_YC_LIMIT */ {
		preview_config_yc_range,
		NULL,
		offsetof(struct prev_params, yclimit),
		FIELD_SIZEOF(struct prev_params, yclimit),
		offsetof(struct omap3isp_prev_update_config, yclimit),
	}, /* OMAP3ISP_PREV_DEFECT_COR */ {
		preview_config_dcor,
		preview_enable_dcor,
		offsetof(struct prev_params, dcor),
		FIELD_SIZEOF(struct prev_params, dcor),
		offsetof(struct omap3isp_prev_update_config, dcor),
	}, /* Previously OMAP3ISP_PREV_GAMMABYPASS, not used anymore */ {
		NULL,
		NULL,
	}, /* OMAP3ISP_PREV_DRK_FRM_CAPTURE */ {
		NULL,
		preview_enable_drkframe_capture,
	}, /* OMAP3ISP_PREV_DRK_FRM_SUBTRACT */ {
		NULL,
		preview_enable_drkframe,
	}, /* OMAP3ISP_PREV_LENS_SHADING */ {
		NULL,
		preview_enable_drkframe,
	}, /* OMAP3ISP_PREV_NF */ {
		preview_config_noisefilter,
		preview_enable_noisefilter,
		offsetof(struct prev_params, nf),
		FIELD_SIZEOF(struct prev_params, nf),
		offsetof(struct omap3isp_prev_update_config, nf),
	}, /* OMAP3ISP_PREV_GAMMA */ {
		preview_config_gammacorrn,
		preview_enable_gammacorrn,
		offsetof(struct prev_params, gamma),
		FIELD_SIZEOF(struct prev_params, gamma),
		offsetof(struct omap3isp_prev_update_config, gamma),
	}, /* OMAP3ISP_PREV_CONTRAST */ {
		preview_config_contrast,
		NULL,
		0, 0, 0, true,
	}, /* OMAP3ISP_PREV_BRIGHTNESS */ {
		preview_config_brightness,
		NULL,
		0, 0, 0, true,
	},
};

/*
 * preview_config - Copy and update local structure with userspace preview
 *                  configuration.
 * @prev: ISP preview engine
 * @cfg: Configuration
 *
 * Return zero if success or -EFAULT if the configuration can't be copied from
 * userspace.
 */
static int preview_config(struct isp_prev_device *prev,
			  struct omap3isp_prev_update_config *cfg)
{
	unsigned long flags;
	unsigned int i;
	int rval = 0;
	u32 update;
	u32 active;

	if (cfg->update == 0)
		return 0;

	/* Mark the shadow parameters we're going to update as busy. */
	spin_lock_irqsave(&prev->params.lock, flags);
	preview_params_lock(prev, cfg->update, true);
	active = prev->params.active;
	spin_unlock_irqrestore(&prev->params.lock, flags);

	update = 0;

	for (i = 0; i < ARRAY_SIZE(update_attrs); i++) {
		const struct preview_update *attr = &update_attrs[i];
		struct prev_params *params;
		unsigned int bit = 1 << i;

		if (attr->skip || !(cfg->update & bit))
			continue;

		params = &prev->params.params[!!(active & bit)];

		if (cfg->flag & bit) {
			void __user *from = *(void * __user *)
				((void *)cfg + attr->config_offset);
			void *to = (void *)params + attr->param_offset;
			size_t size = attr->param_size;

			if (to && from && size) {
				if (copy_from_user(to, from, size)) {
					rval = -EFAULT;
					break;
				}
			}
			params->features |= bit;
		} else {
			params->features &= ~bit;
		}

		update |= bit;
	}

	spin_lock_irqsave(&prev->params.lock, flags);
	preview_params_unlock(prev, update, true);
	preview_params_switch(prev);
	spin_unlock_irqrestore(&prev->params.lock, flags);

	return rval;
}

/*
 * preview_setup_hw - Setup preview registers and/or internal memory
 * @prev: pointer to preview private structure
 * @update: Bitmask of parameters to setup
 * @active: Bitmask of parameters active in set 0
 * Note: can be called from interrupt context
 * Return none
 */
static void preview_setup_hw(struct isp_prev_device *prev, u32 update,
			     u32 active)
{
	unsigned int i;
	u32 features;

	if (update == 0)
		return;

	features = (prev->params.params[0].features & active)
		 | (prev->params.params[1].features & ~active);

	for (i = 0; i < ARRAY_SIZE(update_attrs); i++) {
		const struct preview_update *attr = &update_attrs[i];
		struct prev_params *params;
		unsigned int bit = 1 << i;

		if (!(update & bit))
			continue;

		params = &prev->params.params[!(active & bit)];

		if (params->features & bit) {
			if (attr->config)
				attr->config(prev, params);
			if (attr->enable)
				attr->enable(prev, true);
		} else {
			if (attr->enable)
				attr->enable(prev, false);
		}
	}
}

/*
 * preview_config_ycpos - Configure byte layout of YUV image.
 * @mode: Indicates the required byte layout.
 */
static void
preview_config_ycpos(struct isp_prev_device *prev,
		     enum v4l2_mbus_pixelcode pixelcode)
{
	struct isp_device *isp = to_isp_device(prev);
	enum preview_ycpos_mode mode;

	switch (pixelcode) {
	case V4L2_MBUS_FMT_YUYV8_1X16:
		mode = YCPOS_CrYCbY;
		break;
	case V4L2_MBUS_FMT_UYVY8_1X16:
		mode = YCPOS_YCrYCb;
		break;
	default:
		return;
	}

	isp_reg_clr_set(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			ISPPRV_PCR_YCPOS_CrYCbY,
			mode << ISPPRV_PCR_YCPOS_SHIFT);
}

/*
 * preview_config_averager - Enable / disable / configure averager
 * @average: Average value to be configured.
 */
static void preview_config_averager(struct isp_prev_device *prev, u8 average)
{
	struct isp_device *isp = to_isp_device(prev);

	isp_reg_writel(isp, ISPPRV_AVE_EVENDIST_2 << ISPPRV_AVE_EVENDIST_SHIFT |
		       ISPPRV_AVE_ODDDIST_2 << ISPPRV_AVE_ODDDIST_SHIFT |
		       average, OMAP3_ISP_IOMEM_PREV, ISPPRV_AVE);
}


/*
 * preview_config_input_format - Configure the input format
 * @prev: The preview engine
 * @info: Sink pad format information
 *
 * Enable and configure CFA interpolation for Bayer formats and disable it for
 * greyscale formats.
 *
 * The CFA table is organised in four blocks, one per Bayer component. The
 * hardware expects blocks to follow the Bayer order of the input data, while
 * the driver stores the table in GRBG order in memory. The blocks need to be
 * reordered to support non-GRBG Bayer patterns.
 */
static void preview_config_input_format(struct isp_prev_device *prev,
					const struct isp_format_info *info)
{
	struct isp_device *isp = to_isp_device(prev);
	struct prev_params *params;

	if (info->width == 8)
		isp_reg_set(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_WIDTH);
	else
		isp_reg_clr(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_WIDTH);

	switch (info->flavor) {
	case V4L2_MBUS_FMT_SGRBG8_1X8:
		prev->params.cfa_order = 0;
		break;
	case V4L2_MBUS_FMT_SRGGB8_1X8:
		prev->params.cfa_order = 1;
		break;
	case V4L2_MBUS_FMT_SBGGR8_1X8:
		prev->params.cfa_order = 2;
		break;
	case V4L2_MBUS_FMT_SGBRG8_1X8:
		prev->params.cfa_order = 3;
		break;
	default:
		/* Disable CFA for non-Bayer formats. */
		isp_reg_clr(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_CFAEN);
		return;
	}

	isp_reg_set(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR, ISPPRV_PCR_CFAEN);
	isp_reg_clr_set(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			ISPPRV_PCR_CFAFMT_MASK, ISPPRV_PCR_CFAFMT_BAYER);

	params = (prev->params.active & OMAP3ISP_PREV_CFA)
	       ? &prev->params.params[0] : &prev->params.params[1];

	preview_config_cfa(prev, params);
}

/*
 * preview_config_input_size - Configure the input frame size
 *
 * The preview engine crops several rows and columns internally depending on
 * which processing blocks are enabled. The driver assumes all those blocks are
 * enabled when reporting source pad formats to userspace. If this assumption is
 * not true, rows and columns must be manually cropped at the preview engine
 * input to avoid overflows at the end of lines and frames.
 *
 * See the explanation at the PREV_MARGIN_* definitions for more details.
 */
static void preview_config_input_size(struct isp_prev_device *prev, u32 active)
{
	const struct v4l2_mbus_framefmt *format = &prev->formats[PREV_PAD_SINK];
	struct isp_device *isp = to_isp_device(prev);
	unsigned int sph = prev->crop.left;
	unsigned int eph = prev->crop.left + prev->crop.width - 1;
	unsigned int slv = prev->crop.top;
	unsigned int elv = prev->crop.top + prev->crop.height - 1;
	u32 features;

	if (format->code != V4L2_MBUS_FMT_Y8_1X8 &&
	    format->code != V4L2_MBUS_FMT_Y10_1X10) {
		sph -= 2;
		eph += 2;
		slv -= 2;
		elv += 2;
	}

	features = (prev->params.params[0].features & active)
		 | (prev->params.params[1].features & ~active);

	if (features & (OMAP3ISP_PREV_DEFECT_COR | OMAP3ISP_PREV_NF)) {
		sph -= 2;
		eph += 2;
		slv -= 2;
		elv += 2;
	}
	if (features & OMAP3ISP_PREV_HRZ_MED) {
		sph -= 2;
		eph += 2;
	}
	if (features & (OMAP3ISP_PREV_CHROMA_SUPP | OMAP3ISP_PREV_LUMAENH))
		sph -= 2;

	isp_reg_writel(isp, (sph << ISPPRV_HORZ_INFO_SPH_SHIFT) | eph,
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_HORZ_INFO);
	isp_reg_writel(isp, (slv << ISPPRV_VERT_INFO_SLV_SHIFT) | elv,
		       OMAP3_ISP_IOMEM_PREV, ISPPRV_VERT_INFO);
}

/*
 * preview_config_inlineoffset - Configures the Read address line offset.
 * @prev: Preview module
 * @offset: Line offset
 *
 * According to the TRM, the line offset must be aligned on a 32 bytes boundary.
 * However, a hardware bug requires the memory start address to be aligned on a
 * 64 bytes boundary, so the offset probably should be aligned on 64 bytes as
 * well.
 */
static void
preview_config_inlineoffset(struct isp_prev_device *prev, u32 offset)
{
	struct isp_device *isp = to_isp_device(prev);

	isp_reg_writel(isp, offset & 0xffff, OMAP3_ISP_IOMEM_PREV,
		       ISPPRV_RADR_OFFSET);
}

/*
 * preview_set_inaddr - Sets memory address of input frame.
 * @addr: 32bit memory address aligned on 32byte boundary.
 *
 * Configures the memory address from which the input frame is to be read.
 */
static void preview_set_inaddr(struct isp_prev_device *prev, u32 addr)
{
	struct isp_device *isp = to_isp_device(prev);

	isp_reg_writel(isp, addr, OMAP3_ISP_IOMEM_PREV, ISPPRV_RSDR_ADDR);
}

/*
 * preview_config_outlineoffset - Configures the Write address line offset.
 * @offset: Line Offset for the preview output.
 *
 * The offset must be a multiple of 32 bytes.
 */
static void preview_config_outlineoffset(struct isp_prev_device *prev,
				    u32 offset)
{
	struct isp_device *isp = to_isp_device(prev);

	isp_reg_writel(isp, offset & 0xffff, OMAP3_ISP_IOMEM_PREV,
		       ISPPRV_WADD_OFFSET);
}

/*
 * preview_set_outaddr - Sets the memory address to store output frame
 * @addr: 32bit memory address aligned on 32byte boundary.
 *
 * Configures the memory address to which the output frame is written.
 */
static void preview_set_outaddr(struct isp_prev_device *prev, u32 addr)
{
	struct isp_device *isp = to_isp_device(prev);

	isp_reg_writel(isp, addr, OMAP3_ISP_IOMEM_PREV, ISPPRV_WSDR_ADDR);
}

static void preview_adjust_bandwidth(struct isp_prev_device *prev)
{
	struct isp_pipeline *pipe = to_isp_pipeline(&prev->subdev.entity);
	struct isp_device *isp = to_isp_device(prev);
	const struct v4l2_mbus_framefmt *ifmt = &prev->formats[PREV_PAD_SINK];
	unsigned long l3_ick = pipe->l3_ick;
	struct v4l2_fract *timeperframe;
	unsigned int cycles_per_frame;
	unsigned int requests_per_frame;
	unsigned int cycles_per_request;
	unsigned int minimum;
	unsigned int maximum;
	unsigned int value;

	if (prev->input != PREVIEW_INPUT_MEMORY) {
		isp_reg_clr(isp, OMAP3_ISP_IOMEM_SBL, ISPSBL_SDR_REQ_EXP,
			    ISPSBL_SDR_REQ_PRV_EXP_MASK);
		return;
	}

	/* Compute the minimum number of cycles per request, based on the
	 * pipeline maximum data rate. This is an absolute lower bound if we
	 * don't want SBL overflows, so round the value up.
	 */
	cycles_per_request = div_u64((u64)l3_ick / 2 * 256 + pipe->max_rate - 1,
				     pipe->max_rate);
	minimum = DIV_ROUND_UP(cycles_per_request, 32);

	/* Compute the maximum number of cycles per request, based on the
	 * requested frame rate. This is a soft upper bound to achieve a frame
	 * rate equal or higher than the requested value, so round the value
	 * down.
	 */
	timeperframe = &pipe->max_timeperframe;

	requests_per_frame = DIV_ROUND_UP(ifmt->width * 2, 256) * ifmt->height;
	cycles_per_frame = div_u64((u64)l3_ick * timeperframe->numerator,
				   timeperframe->denominator);
	cycles_per_request = cycles_per_frame / requests_per_frame;

	maximum = cycles_per_request / 32;

	value = max(minimum, maximum);

	dev_dbg(isp->dev, "%s: cycles per request = %u\n", __func__, value);
	isp_reg_clr_set(isp, OMAP3_ISP_IOMEM_SBL, ISPSBL_SDR_REQ_EXP,
			ISPSBL_SDR_REQ_PRV_EXP_MASK,
			value << ISPSBL_SDR_REQ_PRV_EXP_SHIFT);
}

/*
 * omap3isp_preview_busy - Gets busy state of preview module.
 */
int omap3isp_preview_busy(struct isp_prev_device *prev)
{
	struct isp_device *isp = to_isp_device(prev);

	return isp_reg_readl(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR)
		& ISPPRV_PCR_BUSY;
}

/*
 * omap3isp_preview_restore_context - Restores the values of preview registers
 */
void omap3isp_preview_restore_context(struct isp_device *isp)
{
	struct isp_prev_device *prev = &isp->isp_prev;
	const u32 update = OMAP3ISP_PREV_FEATURES_END - 1;

	prev->params.params[0].update = prev->params.active & update;
	prev->params.params[1].update = ~prev->params.active & update;

	preview_setup_hw(prev, update, prev->params.active);

	prev->params.params[0].update = 0;
	prev->params.params[1].update = 0;
}

/*
 * preview_print_status - Dump preview module registers to the kernel log
 */
#define PREV_PRINT_REGISTER(isp, name)\
	dev_dbg(isp->dev, "###PRV " #name "=0x%08x\n", \
		isp_reg_readl(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_##name))

static void preview_print_status(struct isp_prev_device *prev)
{
	struct isp_device *isp = to_isp_device(prev);

	dev_dbg(isp->dev, "-------------Preview Register dump----------\n");

	PREV_PRINT_REGISTER(isp, PCR);
	PREV_PRINT_REGISTER(isp, HORZ_INFO);
	PREV_PRINT_REGISTER(isp, VERT_INFO);
	PREV_PRINT_REGISTER(isp, RSDR_ADDR);
	PREV_PRINT_REGISTER(isp, RADR_OFFSET);
	PREV_PRINT_REGISTER(isp, DSDR_ADDR);
	PREV_PRINT_REGISTER(isp, DRKF_OFFSET);
	PREV_PRINT_REGISTER(isp, WSDR_ADDR);
	PREV_PRINT_REGISTER(isp, WADD_OFFSET);
	PREV_PRINT_REGISTER(isp, AVE);
	PREV_PRINT_REGISTER(isp, HMED);
	PREV_PRINT_REGISTER(isp, NF);
	PREV_PRINT_REGISTER(isp, WB_DGAIN);
	PREV_PRINT_REGISTER(isp, WBGAIN);
	PREV_PRINT_REGISTER(isp, WBSEL);
	PREV_PRINT_REGISTER(isp, CFA);
	PREV_PRINT_REGISTER(isp, BLKADJOFF);
	PREV_PRINT_REGISTER(isp, RGB_MAT1);
	PREV_PRINT_REGISTER(isp, RGB_MAT2);
	PREV_PRINT_REGISTER(isp, RGB_MAT3);
	PREV_PRINT_REGISTER(isp, RGB_MAT4);
	PREV_PRINT_REGISTER(isp, RGB_MAT5);
	PREV_PRINT_REGISTER(isp, RGB_OFF1);
	PREV_PRINT_REGISTER(isp, RGB_OFF2);
	PREV_PRINT_REGISTER(isp, CSC0);
	PREV_PRINT_REGISTER(isp, CSC1);
	PREV_PRINT_REGISTER(isp, CSC2);
	PREV_PRINT_REGISTER(isp, CSC_OFFSET);
	PREV_PRINT_REGISTER(isp, CNT_BRT);
	PREV_PRINT_REGISTER(isp, CSUP);
	PREV_PRINT_REGISTER(isp, SETUP_YC);
	PREV_PRINT_REGISTER(isp, SET_TBL_ADDR);
	PREV_PRINT_REGISTER(isp, CDC_THR0);
	PREV_PRINT_REGISTER(isp, CDC_THR1);
	PREV_PRINT_REGISTER(isp, CDC_THR2);
	PREV_PRINT_REGISTER(isp, CDC_THR3);

	dev_dbg(isp->dev, "--------------------------------------------\n");
}

/*
 * preview_init_params - init image processing parameters.
 * @prev: pointer to previewer private structure
 */
static void preview_init_params(struct isp_prev_device *prev)
{
	struct prev_params *params;
	unsigned int i;

	spin_lock_init(&prev->params.lock);

	prev->params.active = ~0;
	prev->params.params[0].busy = 0;
	prev->params.params[0].update = OMAP3ISP_PREV_FEATURES_END - 1;
	prev->params.params[1].busy = 0;
	prev->params.params[1].update = 0;

	params = &prev->params.params[0];

	/* Init values */
	params->contrast = ISPPRV_CONTRAST_DEF * ISPPRV_CONTRAST_UNITS;
	params->brightness = ISPPRV_BRIGHT_DEF * ISPPRV_BRIGHT_UNITS;
	params->cfa.format = OMAP3ISP_CFAFMT_BAYER;
	memcpy(params->cfa.table, cfa_coef_table,
	       sizeof(params->cfa.table));
	params->cfa.gradthrs_horz = FLR_CFA_GRADTHRS_HORZ;
	params->cfa.gradthrs_vert = FLR_CFA_GRADTHRS_VERT;
	params->csup.gain = FLR_CSUP_GAIN;
	params->csup.thres = FLR_CSUP_THRES;
	params->csup.hypf_en = 0;
	memcpy(params->luma.table, luma_enhance_table,
	       sizeof(params->luma.table));
	params->nf.spread = FLR_NF_STRGTH;
	memcpy(params->nf.table, noise_filter_table, sizeof(params->nf.table));
	params->dcor.couplet_mode_en = 1;
	for (i = 0; i < OMAP3ISP_PREV_DETECT_CORRECT_CHANNELS; i++)
		params->dcor.detect_correct[i] = DEF_DETECT_CORRECT_VAL;
	memcpy(params->gamma.blue, gamma_table, sizeof(params->gamma.blue));
	memcpy(params->gamma.green, gamma_table, sizeof(params->gamma.green));
	memcpy(params->gamma.red, gamma_table, sizeof(params->gamma.red));
	params->wbal.dgain = FLR_WBAL_DGAIN;
	params->wbal.coef0 = FLR_WBAL_COEF;
	params->wbal.coef1 = FLR_WBAL_COEF;
	params->wbal.coef2 = FLR_WBAL_COEF;
	params->wbal.coef3 = FLR_WBAL_COEF;
	params->blkadj.red = FLR_BLKADJ_RED;
	params->blkadj.green = FLR_BLKADJ_GREEN;
	params->blkadj.blue = FLR_BLKADJ_BLUE;
	params->rgb2rgb = flr_rgb2rgb;
	params->csc = flr_prev_csc;
	params->yclimit.minC = ISPPRV_YC_MIN;
	params->yclimit.maxC = ISPPRV_YC_MAX;
	params->yclimit.minY = ISPPRV_YC_MIN;
	params->yclimit.maxY = ISPPRV_YC_MAX;

	params->features = OMAP3ISP_PREV_CFA | OMAP3ISP_PREV_DEFECT_COR
			 | OMAP3ISP_PREV_NF | OMAP3ISP_PREV_GAMMA
			 | OMAP3ISP_PREV_BLKADJ | OMAP3ISP_PREV_YC_LIMIT
			 | OMAP3ISP_PREV_RGB2RGB | OMAP3ISP_PREV_COLOR_CONV
			 | OMAP3ISP_PREV_WB | OMAP3ISP_PREV_BRIGHTNESS
			 | OMAP3ISP_PREV_CONTRAST;
}

/*
 * preview_max_out_width - Handle previewer hardware ouput limitations
 * @isp_revision : ISP revision
 * returns maximum width output for current isp revision
 */
static unsigned int preview_max_out_width(struct isp_prev_device *prev)
{
	struct isp_device *isp = to_isp_device(prev);

	switch (isp->revision) {
	case ISP_REVISION_1_0:
		return PREV_MAX_OUT_WIDTH_REV_1;

	case ISP_REVISION_2_0:
	default:
		return PREV_MAX_OUT_WIDTH_REV_2;

	case ISP_REVISION_15_0:
		return PREV_MAX_OUT_WIDTH_REV_15;
	}
}

static void preview_configure(struct isp_prev_device *prev)
{
	struct isp_device *isp = to_isp_device(prev);
	const struct isp_format_info *info;
	struct v4l2_mbus_framefmt *format;
	unsigned long flags;
	u32 update;
	u32 active;

	spin_lock_irqsave(&prev->params.lock, flags);
	/* Mark all active parameters we are going to touch as busy. */
	update = preview_params_lock(prev, 0, false);
	active = prev->params.active;
	spin_unlock_irqrestore(&prev->params.lock, flags);

	/* PREV_PAD_SINK */
	format = &prev->formats[PREV_PAD_SINK];
	info = omap3isp_video_format_info(format->code);

	preview_adjust_bandwidth(prev);

	preview_config_input_format(prev, info);
	preview_config_input_size(prev, active);

	if (prev->input == PREVIEW_INPUT_CCDC)
		preview_config_inlineoffset(prev, 0);
	else
		preview_config_inlineoffset(prev, ALIGN(format->width, 0x20) *
					    info->bpp);

	preview_setup_hw(prev, update, active);

	/* PREV_PAD_SOURCE */
	format = &prev->formats[PREV_PAD_SOURCE];

	if (prev->output & PREVIEW_OUTPUT_MEMORY)
		isp_reg_set(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_SDRPORT);
	else
		isp_reg_clr(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_SDRPORT);

	if (prev->output & PREVIEW_OUTPUT_RESIZER)
		isp_reg_set(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_RSZPORT);
	else
		isp_reg_clr(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_RSZPORT);

	if (prev->output & PREVIEW_OUTPUT_MEMORY)
		preview_config_outlineoffset(prev,
				ALIGN(format->width, 0x10) * 2);

	preview_config_averager(prev, 0);
	preview_config_ycpos(prev, format->code);

	spin_lock_irqsave(&prev->params.lock, flags);
	preview_params_unlock(prev, update, false);
	spin_unlock_irqrestore(&prev->params.lock, flags);
}

/* -----------------------------------------------------------------------------
 * Interrupt handling
 */

static void preview_enable_oneshot(struct isp_prev_device *prev)
{
	struct isp_device *isp = to_isp_device(prev);

	/* The PCR.SOURCE bit is automatically reset to 0 when the PCR.ENABLE
	 * bit is set. As the preview engine is used in single-shot mode, we
	 * need to set PCR.SOURCE before enabling the preview engine.
	 */
	if (prev->input == PREVIEW_INPUT_MEMORY)
		isp_reg_set(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
			    ISPPRV_PCR_SOURCE);

	isp_reg_set(isp, OMAP3_ISP_IOMEM_PREV, ISPPRV_PCR,
		    ISPPRV_PCR_EN | ISPPRV_PCR_ONESHOT);
}

void omap3isp_preview_isr_frame_sync(struct isp_prev_device *prev)
{
	/*
	 * If ISP_VIDEO_DMAQUEUE_QUEUED is set, DMA queue had an underrun
	 * condition, the module was paused and now we have a buffer queued
	 * on the output again. Restart the pipeline if running in continuous
	 * mode.
	 */
	if (prev->state == ISP_PIPELINE_STREAM_CONTINUOUS &&
	    prev->video_out.dmaqueue_flags & ISP_VIDEO_DMAQUEUE_QUEUED) {
		preview_enable_oneshot(prev);
		isp_video_dmaqueue_flags_clr(&prev->video_out);
	}
}

static void preview_isr_buffer(struct isp_prev_device *prev)
{
	struct isp_pipeline *pipe = to_isp_pipeline(&prev->subdev.entity);
	struct isp_buffer *buffer;
	int restart = 0;

	if (prev->input == PREVIEW_INPUT_MEMORY) {
		buffer = omap3isp_video_buffer_next(&prev->video_in);
		if (buffer != NULL)
			preview_set_inaddr(prev, buffer->isp_addr);
		pipe->state |= ISP_PIPELINE_IDLE_INPUT;
	}

	if (prev->output & PREVIEW_OUTPUT_MEMORY) {
		buffer = omap3isp_video_buffer_next(&prev->video_out);
		if (buffer != NULL) {
			preview_set_outaddr(prev, buffer->isp_addr);
			restart = 1;
		}
		pipe->state |= ISP_PIPELINE_IDLE_OUTPUT;
	}

	switch (prev->state) {
	case ISP_PIPELINE_STREAM_SINGLESHOT:
		if (isp_pipeline_ready(pipe))
			omap3isp_pipeline_set_stream(pipe,
						ISP_PIPELINE_STREAM_SINGLESHOT);
		break;

	case ISP_PIPELINE_STREAM_CONTINUOUS:
		/* If an underrun occurs, the video queue operation handler will
		 * restart the preview engine. Otherwise restart it immediately.
		 */
		if (restart)
			preview_enable_oneshot(prev);
		break;

	case ISP_PIPELINE_STREAM_STOPPED:
	default:
		return;
	}
}

/*
 * omap3isp_preview_isr - ISP preview engine interrupt handler
 *
 * Manage the preview engine video buffers and configure shadowed registers.
 */
void omap3isp_preview_isr(struct isp_prev_device *prev)
{
	unsigned long flags;
	u32 update;
	u32 active;

	if (omap3isp_module_sync_is_stopping(&prev->wait, &prev->stopping))
		return;

	spin_lock_irqsave(&prev->params.lock, flags);
	preview_params_switch(prev);
	update = preview_params_lock(prev, 0, false);
	active = prev->params.active;
	spin_unlock_irqrestore(&prev->params.lock, flags);

	preview_setup_hw(prev, update, active);
	preview_config_input_size(prev, active);

	if (prev->input == PREVIEW_INPUT_MEMORY ||
	    prev->output & PREVIEW_OUTPUT_MEMORY)
		preview_isr_buffer(prev);
	else if (prev->state == ISP_PIPELINE_STREAM_CONTINUOUS)
		preview_enable_oneshot(prev);

	spin_lock_irqsave(&prev->params.lock, flags);
	preview_params_unlock(prev, update, false);
	spin_unlock_irqrestore(&prev->params.lock, flags);
}

/* -----------------------------------------------------------------------------
 * ISP video operations
 */

static int preview_video_queue(struct isp_video *video,
			       struct isp_buffer *buffer)
{
	struct isp_prev_device *prev = &video->isp->isp_prev;

	if (video->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		preview_set_inaddr(prev, buffer->isp_addr);

	if (video->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		preview_set_outaddr(prev, buffer->isp_addr);

	return 0;
}

static const struct isp_video_operations preview_video_ops = {
	.queue = preview_video_queue,
};

/* -----------------------------------------------------------------------------
 * V4L2 subdev operations
 */

/*
 * preview_s_ctrl - Handle set control subdev method
 * @ctrl: pointer to v4l2 control structure
 */
static int preview_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct isp_prev_device *prev =
		container_of(ctrl->handler, struct isp_prev_device, ctrls);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		preview_update_brightness(prev, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		preview_update_contrast(prev, ctrl->val);
		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops preview_ctrl_ops = {
	.s_ctrl = preview_s_ctrl,
};

/*
 * preview_ioctl - Handle preview module private ioctl's
 * @prev: pointer to preview context structure
 * @cmd: configuration command
 * @arg: configuration argument
 * return -EINVAL or zero on success
 */
static long preview_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct isp_prev_device *prev = v4l2_get_subdevdata(sd);

	switch (cmd) {
	case VIDIOC_OMAP3ISP_PRV_CFG:
		return preview_config(prev, arg);

	default:
		return -ENOIOCTLCMD;
	}
}

/*
 * preview_set_stream - Enable/Disable streaming on preview subdev
 * @sd    : pointer to v4l2 subdev structure
 * @enable: 1 == Enable, 0 == Disable
 * return -EINVAL or zero on success
 */
static int preview_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct isp_prev_device *prev = v4l2_get_subdevdata(sd);
	struct isp_video *video_out = &prev->video_out;
	struct isp_device *isp = to_isp_device(prev);
	struct device *dev = to_device(prev);

	if (prev->state == ISP_PIPELINE_STREAM_STOPPED) {
		if (enable == ISP_PIPELINE_STREAM_STOPPED)
			return 0;

		omap3isp_subclk_enable(isp, OMAP3_ISP_SUBCLK_PREVIEW);
		preview_configure(prev);
		atomic_set(&prev->stopping, 0);
		preview_print_status(prev);
	}

	switch (enable) {
	case ISP_PIPELINE_STREAM_CONTINUOUS:
		if (prev->output & PREVIEW_OUTPUT_MEMORY)
			omap3isp_sbl_enable(isp, OMAP3_ISP_SBL_PREVIEW_WRITE);

		if (video_out->dmaqueue_flags & ISP_VIDEO_DMAQUEUE_QUEUED ||
		    !(prev->output & PREVIEW_OUTPUT_MEMORY))
			preview_enable_oneshot(prev);

		isp_video_dmaqueue_flags_clr(video_out);
		break;

	case ISP_PIPELINE_STREAM_SINGLESHOT:
		if (prev->input == PREVIEW_INPUT_MEMORY)
			omap3isp_sbl_enable(isp, OMAP3_ISP_SBL_PREVIEW_READ);
		if (prev->output & PREVIEW_OUTPUT_MEMORY)
			omap3isp_sbl_enable(isp, OMAP3_ISP_SBL_PREVIEW_WRITE);

		preview_enable_oneshot(prev);
		break;

	case ISP_PIPELINE_STREAM_STOPPED:
		if (omap3isp_module_sync_idle(&sd->entity, &prev->wait,
					      &prev->stopping))
			dev_dbg(dev, "%s: stop timeout.\n", sd->name);
		omap3isp_sbl_disable(isp, OMAP3_ISP_SBL_PREVIEW_READ);
		omap3isp_sbl_disable(isp, OMAP3_ISP_SBL_PREVIEW_WRITE);
		omap3isp_subclk_disable(isp, OMAP3_ISP_SUBCLK_PREVIEW);
		isp_video_dmaqueue_flags_clr(video_out);
		break;
	}

	prev->state = enable;
	return 0;
}

static struct v4l2_mbus_framefmt *
__preview_get_format(struct isp_prev_device *prev, struct v4l2_subdev_fh *fh,
		     unsigned int pad, enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(fh, pad);
	else
		return &prev->formats[pad];
}

static struct v4l2_rect *
__preview_get_crop(struct isp_prev_device *prev, struct v4l2_subdev_fh *fh,
		   enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_crop(fh, PREV_PAD_SINK);
	else
		return &prev->crop;
}

/* previewer format descriptions */
static const unsigned int preview_input_fmts[] = {
	V4L2_MBUS_FMT_Y8_1X8,
	V4L2_MBUS_FMT_SGRBG8_1X8,
	V4L2_MBUS_FMT_SRGGB8_1X8,
	V4L2_MBUS_FMT_SBGGR8_1X8,
	V4L2_MBUS_FMT_SGBRG8_1X8,
	V4L2_MBUS_FMT_Y10_1X10,
	V4L2_MBUS_FMT_SGRBG10_1X10,
	V4L2_MBUS_FMT_SRGGB10_1X10,
	V4L2_MBUS_FMT_SBGGR10_1X10,
	V4L2_MBUS_FMT_SGBRG10_1X10,
};

static const unsigned int preview_output_fmts[] = {
	V4L2_MBUS_FMT_UYVY8_1X16,
	V4L2_MBUS_FMT_YUYV8_1X16,
};

/*
 * preview_try_format - Validate a format
 * @prev: ISP preview engine
 * @fh: V4L2 subdev file handle
 * @pad: pad number
 * @fmt: format to be validated
 * @which: try/active format selector
 *
 * Validate and adjust the given format for the given pad based on the preview
 * engine limits and the format and crop rectangles on other pads.
 */
static void preview_try_format(struct isp_prev_device *prev,
			       struct v4l2_subdev_fh *fh, unsigned int pad,
			       struct v4l2_mbus_framefmt *fmt,
			       enum v4l2_subdev_format_whence which)
{
	enum v4l2_mbus_pixelcode pixelcode;
	struct v4l2_rect *crop;
	unsigned int i;

	switch (pad) {
	case PREV_PAD_SINK:
		/* When reading data from the CCDC, the input size has already
		 * been mangled by the CCDC output pad so it can be accepted
		 * as-is.
		 *
		 * When reading data from memory, clamp the requested width and
		 * height. The TRM doesn't specify a minimum input height, make
		 * sure we got enough lines to enable the noise filter and color
		 * filter array interpolation.
		 */
		if (prev->input == PREVIEW_INPUT_MEMORY) {
			fmt->width = clamp_t(u32, fmt->width, PREV_MIN_IN_WIDTH,
					     preview_max_out_width(prev));
			fmt->height = clamp_t(u32, fmt->height,
					      PREV_MIN_IN_HEIGHT,
					      PREV_MAX_IN_HEIGHT);
		}

		fmt->colorspace = V4L2_COLORSPACE_SRGB;

		for (i = 0; i < ARRAY_SIZE(preview_input_fmts); i++) {
			if (fmt->code == preview_input_fmts[i])
				break;
		}

		/* If not found, use SGRBG10 as default */
		if (i >= ARRAY_SIZE(preview_input_fmts))
			fmt->code = V4L2_MBUS_FMT_SGRBG10_1X10;
		break;

	case PREV_PAD_SOURCE:
		pixelcode = fmt->code;
		*fmt = *__preview_get_format(prev, fh, PREV_PAD_SINK, which);

		switch (pixelcode) {
		case V4L2_MBUS_FMT_YUYV8_1X16:
		case V4L2_MBUS_FMT_UYVY8_1X16:
			fmt->code = pixelcode;
			break;

		default:
			fmt->code = V4L2_MBUS_FMT_YUYV8_1X16;
			break;
		}

		/* The preview module output size is configurable through the
		 * averager (horizontal scaling by 1/1, 1/2, 1/4 or 1/8). This
		 * is not supported yet, hardcode the output size to the crop
		 * rectangle size.
		 */
		crop = __preview_get_crop(prev, fh, which);
		fmt->width = crop->width;
		fmt->height = crop->height;

		fmt->colorspace = V4L2_COLORSPACE_JPEG;
		break;
	}

	fmt->field = V4L2_FIELD_NONE;
}

/*
 * preview_try_crop - Validate a crop rectangle
 * @prev: ISP preview engine
 * @sink: format on the sink pad
 * @crop: crop rectangle to be validated
 *
 * The preview engine crops lines and columns for its internal operation,
 * depending on which filters are enabled. Enforce minimum crop margins to
 * handle that transparently for userspace.
 *
 * See the explanation at the PREV_MARGIN_* definitions for more details.
 */
static void preview_try_crop(struct isp_prev_device *prev,
			     const struct v4l2_mbus_framefmt *sink,
			     struct v4l2_rect *crop)
{
	unsigned int left = PREV_MARGIN_LEFT;
	unsigned int right = sink->width - PREV_MARGIN_RIGHT;
	unsigned int top = PREV_MARGIN_TOP;
	unsigned int bottom = sink->height - PREV_MARGIN_BOTTOM;

	/* When processing data on-the-fly from the CCDC, at least 2 pixels must
	 * be cropped from the left and right sides of the image. As we don't
	 * know which filters will be enabled, increase the left and right
	 * margins by two.
	 */
	if (prev->input == PREVIEW_INPUT_CCDC) {
		left += 2;
		right -= 2;
	}

	/* The CFA filter crops 4 lines and 4 columns in Bayer mode, and 2 lines
	 * and no columns in other modes. Increase the margins based on the sink
	 * format.
	 */
	if (sink->code != V4L2_MBUS_FMT_Y8_1X8 &&
	    sink->code != V4L2_MBUS_FMT_Y10_1X10) {
		left += 2;
		right -= 2;
		top += 2;
		bottom -= 2;
	}

	/* Restrict left/top to even values to keep the Bayer pattern. */
	crop->left &= ~1;
	crop->top &= ~1;

	crop->left = clamp_t(u32, crop->left, left, right - PREV_MIN_OUT_WIDTH);
	crop->top = clamp_t(u32, crop->top, top, bottom - PREV_MIN_OUT_HEIGHT);
	crop->width = clamp_t(u32, crop->width, PREV_MIN_OUT_WIDTH,
			      right - crop->left);
	crop->height = clamp_t(u32, crop->height, PREV_MIN_OUT_HEIGHT,
			       bottom - crop->top);
}

/*
 * preview_enum_mbus_code - Handle pixel format enumeration
 * @sd     : pointer to v4l2 subdev structure
 * @fh     : V4L2 subdev file handle
 * @code   : pointer to v4l2_subdev_mbus_code_enum structure
 * return -EINVAL or zero on success
 */
static int preview_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	switch (code->pad) {
	case PREV_PAD_SINK:
		if (code->index >= ARRAY_SIZE(preview_input_fmts))
			return -EINVAL;

		code->code = preview_input_fmts[code->index];
		break;
	case PREV_PAD_SOURCE:
		if (code->index >= ARRAY_SIZE(preview_output_fmts))
			return -EINVAL;

		code->code = preview_output_fmts[code->index];
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int preview_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_fh *fh,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct isp_prev_device *prev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt format;

	if (fse->index != 0)
		return -EINVAL;

	format.code = fse->code;
	format.width = 1;
	format.height = 1;
	preview_try_format(prev, fh, fse->pad, &format, V4L2_SUBDEV_FORMAT_TRY);
	fse->min_width = format.width;
	fse->min_height = format.height;

	if (format.code != fse->code)
		return -EINVAL;

	format.code = fse->code;
	format.width = -1;
	format.height = -1;
	preview_try_format(prev, fh, fse->pad, &format, V4L2_SUBDEV_FORMAT_TRY);
	fse->max_width = format.width;
	fse->max_height = format.height;

	return 0;
}

/*
 * preview_get_selection - Retrieve a selection rectangle on a pad
 * @sd: ISP preview V4L2 subdevice
 * @fh: V4L2 subdev file handle
 * @sel: Selection rectangle
 *
 * The only supported rectangles are the crop rectangles on the sink pad.
 *
 * Return 0 on success or a negative error code otherwise.
 */
static int preview_get_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_selection *sel)
{
	struct isp_prev_device *prev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	if (sel->pad != PREV_PAD_SINK)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = INT_MAX;
		sel->r.height = INT_MAX;

		format = __preview_get_format(prev, fh, PREV_PAD_SINK,
					      sel->which);
		preview_try_crop(prev, format, &sel->r);
		break;

	case V4L2_SEL_TGT_CROP:
		sel->r = *__preview_get_crop(prev, fh, sel->which);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * preview_set_selection - Set a selection rectangle on a pad
 * @sd: ISP preview V4L2 subdevice
 * @fh: V4L2 subdev file handle
 * @sel: Selection rectangle
 *
 * The only supported rectangle is the actual crop rectangle on the sink pad.
 *
 * Return 0 on success or a negative error code otherwise.
 */
static int preview_set_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_fh *fh,
				 struct v4l2_subdev_selection *sel)
{
	struct isp_prev_device *prev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	if (sel->target != V4L2_SEL_TGT_CROP ||
	    sel->pad != PREV_PAD_SINK)
		return -EINVAL;

	/* The crop rectangle can't be changed while streaming. */
	if (prev->state != ISP_PIPELINE_STREAM_STOPPED)
		return -EBUSY;

	/* Modifying the crop rectangle always changes the format on the source
	 * pad. If the KEEP_CONFIG flag is set, just return the current crop
	 * rectangle.
	 */
	if (sel->flags & V4L2_SEL_FLAG_KEEP_CONFIG) {
		sel->r = *__preview_get_crop(prev, fh, sel->which);
		return 0;
	}

	format = __preview_get_format(prev, fh, PREV_PAD_SINK, sel->which);
	preview_try_crop(prev, format, &sel->r);
	*__preview_get_crop(prev, fh, sel->which) = sel->r;

	/* Update the source format. */
	format = __preview_get_format(prev, fh, PREV_PAD_SOURCE, sel->which);
	preview_try_format(prev, fh, PREV_PAD_SOURCE, format, sel->which);

	return 0;
}

/*
 * preview_get_format - Handle get format by pads subdev method
 * @sd : pointer to v4l2 subdev structure
 * @fh : V4L2 subdev file handle
 * @fmt: pointer to v4l2 subdev format structure
 * return -EINVAL or zero on success
 */
static int preview_get_format(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
			      struct v4l2_subdev_format *fmt)
{
	struct isp_prev_device *prev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;

	format = __preview_get_format(prev, fh, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	fmt->format = *format;
	return 0;
}

/*
 * preview_set_format - Handle set format by pads subdev method
 * @sd : pointer to v4l2 subdev structure
 * @fh : V4L2 subdev file handle
 * @fmt: pointer to v4l2 subdev format structure
 * return -EINVAL or zero on success
 */
static int preview_set_format(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh,
			      struct v4l2_subdev_format *fmt)
{
	struct isp_prev_device *prev = v4l2_get_subdevdata(sd);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;

	format = __preview_get_format(prev, fh, fmt->pad, fmt->which);
	if (format == NULL)
		return -EINVAL;

	preview_try_format(prev, fh, fmt->pad, &fmt->format, fmt->which);
	*format = fmt->format;

	/* Propagate the format from sink to source */
	if (fmt->pad == PREV_PAD_SINK) {
		/* Reset the crop rectangle. */
		crop = __preview_get_crop(prev, fh, fmt->which);
		crop->left = 0;
		crop->top = 0;
		crop->width = fmt->format.width;
		crop->height = fmt->format.height;

		preview_try_crop(prev, &fmt->format, crop);

		/* Update the source format. */
		format = __preview_get_format(prev, fh, PREV_PAD_SOURCE,
					      fmt->which);
		preview_try_format(prev, fh, PREV_PAD_SOURCE, format,
				   fmt->which);
	}

	return 0;
}

/*
 * preview_init_formats - Initialize formats on all pads
 * @sd: ISP preview V4L2 subdevice
 * @fh: V4L2 subdev file handle
 *
 * Initialize all pad formats with default values. If fh is not NULL, try
 * formats are initialized on the file handle. Otherwise active formats are
 * initialized on the device.
 */
static int preview_init_formats(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	struct v4l2_subdev_format format;

	memset(&format, 0, sizeof(format));
	format.pad = PREV_PAD_SINK;
	format.which = fh ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	format.format.code = V4L2_MBUS_FMT_SGRBG10_1X10;
	format.format.width = 4096;
	format.format.height = 4096;
	preview_set_format(sd, fh, &format);

	return 0;
}

/* subdev core operations */
static const struct v4l2_subdev_core_ops preview_v4l2_core_ops = {
	.ioctl = preview_ioctl,
};

/* subdev video operations */
static const struct v4l2_subdev_video_ops preview_v4l2_video_ops = {
	.s_stream = preview_set_stream,
};

/* subdev pad operations */
static const struct v4l2_subdev_pad_ops preview_v4l2_pad_ops = {
	.enum_mbus_code = preview_enum_mbus_code,
	.enum_frame_size = preview_enum_frame_size,
	.get_fmt = preview_get_format,
	.set_fmt = preview_set_format,
	.get_selection = preview_get_selection,
	.set_selection = preview_set_selection,
};

/* subdev operations */
static const struct v4l2_subdev_ops preview_v4l2_ops = {
	.core = &preview_v4l2_core_ops,
	.video = &preview_v4l2_video_ops,
	.pad = &preview_v4l2_pad_ops,
};

/* subdev internal operations */
static const struct v4l2_subdev_internal_ops preview_v4l2_internal_ops = {
	.open = preview_init_formats,
};

/* -----------------------------------------------------------------------------
 * Media entity operations
 */

/*
 * preview_link_setup - Setup previewer connections.
 * @entity : Pointer to media entity structure
 * @local  : Pointer to local pad array
 * @remote : Pointer to remote pad array
 * @flags  : Link flags
 * return -EINVAL or zero on success
 */
static int preview_link_setup(struct media_entity *entity,
			      const struct media_pad *local,
			      const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct isp_prev_device *prev = v4l2_get_subdevdata(sd);

	switch (local->index | media_entity_type(remote->entity)) {
	case PREV_PAD_SINK | MEDIA_ENT_T_DEVNODE:
		/* read from memory */
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (prev->input == PREVIEW_INPUT_CCDC)
				return -EBUSY;
			prev->input = PREVIEW_INPUT_MEMORY;
		} else {
			if (prev->input == PREVIEW_INPUT_MEMORY)
				prev->input = PREVIEW_INPUT_NONE;
		}
		break;

	case PREV_PAD_SINK | MEDIA_ENT_T_V4L2_SUBDEV:
		/* read from ccdc */
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (prev->input == PREVIEW_INPUT_MEMORY)
				return -EBUSY;
			prev->input = PREVIEW_INPUT_CCDC;
		} else {
			if (prev->input == PREVIEW_INPUT_CCDC)
				prev->input = PREVIEW_INPUT_NONE;
		}
		break;

	/*
	 * The ISP core doesn't support pipelines with multiple video outputs.
	 * Revisit this when it will be implemented, and return -EBUSY for now.
	 */

	case PREV_PAD_SOURCE | MEDIA_ENT_T_DEVNODE:
		/* write to memory */
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (prev->output & ~PREVIEW_OUTPUT_MEMORY)
				return -EBUSY;
			prev->output |= PREVIEW_OUTPUT_MEMORY;
		} else {
			prev->output &= ~PREVIEW_OUTPUT_MEMORY;
		}
		break;

	case PREV_PAD_SOURCE | MEDIA_ENT_T_V4L2_SUBDEV:
		/* write to resizer */
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (prev->output & ~PREVIEW_OUTPUT_RESIZER)
				return -EBUSY;
			prev->output |= PREVIEW_OUTPUT_RESIZER;
		} else {
			prev->output &= ~PREVIEW_OUTPUT_RESIZER;
		}
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/* media operations */
static const struct media_entity_operations preview_media_ops = {
	.link_setup = preview_link_setup,
	.link_validate = v4l2_subdev_link_validate,
};

void omap3isp_preview_unregister_entities(struct isp_prev_device *prev)
{
	v4l2_device_unregister_subdev(&prev->subdev);
	omap3isp_video_unregister(&prev->video_in);
	omap3isp_video_unregister(&prev->video_out);
}

int omap3isp_preview_register_entities(struct isp_prev_device *prev,
	struct v4l2_device *vdev)
{
	int ret;

	/* Register the subdev and video nodes. */
	ret = v4l2_device_register_subdev(vdev, &prev->subdev);
	if (ret < 0)
		goto error;

	ret = omap3isp_video_register(&prev->video_in, vdev);
	if (ret < 0)
		goto error;

	ret = omap3isp_video_register(&prev->video_out, vdev);
	if (ret < 0)
		goto error;

	return 0;

error:
	omap3isp_preview_unregister_entities(prev);
	return ret;
}

/* -----------------------------------------------------------------------------
 * ISP previewer initialisation and cleanup
 */

/*
 * preview_init_entities - Initialize subdev and media entity.
 * @prev : Pointer to preview structure
 * return -ENOMEM or zero on success
 */
static int preview_init_entities(struct isp_prev_device *prev)
{
	struct v4l2_subdev *sd = &prev->subdev;
	struct media_pad *pads = prev->pads;
	struct media_entity *me = &sd->entity;
	int ret;

	prev->input = PREVIEW_INPUT_NONE;

	v4l2_subdev_init(sd, &preview_v4l2_ops);
	sd->internal_ops = &preview_v4l2_internal_ops;
	strlcpy(sd->name, "OMAP3 ISP preview", sizeof(sd->name));
	sd->grp_id = 1 << 16;	/* group ID for isp subdevs */
	v4l2_set_subdevdata(sd, prev);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	v4l2_ctrl_handler_init(&prev->ctrls, 2);
	v4l2_ctrl_new_std(&prev->ctrls, &preview_ctrl_ops, V4L2_CID_BRIGHTNESS,
			  ISPPRV_BRIGHT_LOW, ISPPRV_BRIGHT_HIGH,
			  ISPPRV_BRIGHT_STEP, ISPPRV_BRIGHT_DEF);
	v4l2_ctrl_new_std(&prev->ctrls, &preview_ctrl_ops, V4L2_CID_CONTRAST,
			  ISPPRV_CONTRAST_LOW, ISPPRV_CONTRAST_HIGH,
			  ISPPRV_CONTRAST_STEP, ISPPRV_CONTRAST_DEF);
	v4l2_ctrl_handler_setup(&prev->ctrls);
	sd->ctrl_handler = &prev->ctrls;

	pads[PREV_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	pads[PREV_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	me->ops = &preview_media_ops;
	ret = media_entity_init(me, PREV_PADS_NUM, pads, 0);
	if (ret < 0)
		return ret;

	preview_init_formats(sd, NULL);

	/* According to the OMAP34xx TRM, video buffers need to be aligned on a
	 * 32 bytes boundary. However, an undocumented hardware bug requires a
	 * 64 bytes boundary at the preview engine input.
	 */
	prev->video_in.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	prev->video_in.ops = &preview_video_ops;
	prev->video_in.isp = to_isp_device(prev);
	prev->video_in.capture_mem = PAGE_ALIGN(4096 * 4096) * 2 * 3;
	prev->video_in.bpl_alignment = 64;
	prev->video_out.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	prev->video_out.ops = &preview_video_ops;
	prev->video_out.isp = to_isp_device(prev);
	prev->video_out.capture_mem = PAGE_ALIGN(4096 * 4096) * 2 * 3;
	prev->video_out.bpl_alignment = 32;

	ret = omap3isp_video_init(&prev->video_in, "preview");
	if (ret < 0)
		goto error_video_in;

	ret = omap3isp_video_init(&prev->video_out, "preview");
	if (ret < 0)
		goto error_video_out;

	/* Connect the video nodes to the previewer subdev. */
	ret = media_entity_create_link(&prev->video_in.video.entity, 0,
			&prev->subdev.entity, PREV_PAD_SINK, 0);
	if (ret < 0)
		goto error_link;

	ret = media_entity_create_link(&prev->subdev.entity, PREV_PAD_SOURCE,
			&prev->video_out.video.entity, 0, 0);
	if (ret < 0)
		goto error_link;

	return 0;

error_link:
	omap3isp_video_cleanup(&prev->video_out);
error_video_out:
	omap3isp_video_cleanup(&prev->video_in);
error_video_in:
	media_entity_cleanup(&prev->subdev.entity);
	return ret;
}

/*
 * omap3isp_preview_init - Previewer initialization.
 * @dev : Pointer to ISP device
 * return -ENOMEM or zero on success
 */
int omap3isp_preview_init(struct isp_device *isp)
{
	struct isp_prev_device *prev = &isp->isp_prev;

	init_waitqueue_head(&prev->wait);

	preview_init_params(prev);

	return preview_init_entities(prev);
}

void omap3isp_preview_cleanup(struct isp_device *isp)
{
	struct isp_prev_device *prev = &isp->isp_prev;

	v4l2_ctrl_handler_free(&prev->ctrls);
	omap3isp_video_cleanup(&prev->video_in);
	omap3isp_video_cleanup(&prev->video_out);
	media_entity_cleanup(&prev->subdev.entity);
}

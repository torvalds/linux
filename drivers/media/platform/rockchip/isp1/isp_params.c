/*
 * Rockchip isp1 driver
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>	/* for ISP params */
#include <linux/rk-preisp.h>
#include "dev.h"
#include "regs.h"

#define RKISP1_ISP_PARAMS_REQ_BUFS_MIN	2
#define RKISP1_ISP_PARAMS_REQ_BUFS_MAX	8

#define BLS_START_H_MAX_IS_VALID(val)	((val) < CIFISP_BLS_START_H_MAX)
#define BLS_STOP_H_MAX_IS_VALID(val)	((val) < CIFISP_BLS_STOP_H_MAX)

#define BLS_START_V_MAX_IS_VALID(val)	((val) < CIFISP_BLS_START_V_MAX)
#define BLS_STOP_V_MAX_IS_VALID(val)	((val) < CIFISP_BLS_STOP_V_MAX)

#define BLS_SAMPLE_MAX_IS_VALID(val)	((val) < CIFISP_BLS_SAMPLES_MAX)

#define BLS_FIX_SUB_IS_VALID(val)	\
	((val) > (s16) CIFISP_BLS_FIX_SUB_MIN && (val) < CIFISP_BLS_FIX_SUB_MAX)

#define RKISP1_ISP_DPCC_LINE_THRESH(n)	(CIF_ISP_DPCC_LINE_THRESH_1 + 0x14 * (n))
#define RKISP1_ISP_DPCC_LINE_MAD_FAC(n) (CIF_ISP_DPCC_LINE_MAD_FAC_1 + 0x14 * (n))
#define RKISP1_ISP_DPCC_PG_FAC(n)	(CIF_ISP_DPCC_PG_FAC_1 + 0x14 * (n))
#define RKISP1_ISP_DPCC_RND_THRESH(n)	(CIF_ISP_DPCC_RND_THRESH_1 + 0x14 * (n))
#define RKISP1_ISP_DPCC_RG_FAC(n)	(CIF_ISP_DPCC_RG_FAC_1 + 0x14 * (n))
#define RKISP1_ISP_CC_COEFF(n)		(CIF_ISP_CC_COEFF_0 + (n) * 4)

static inline void rkisp1_iowrite32(struct rkisp1_isp_params_vdev *params_vdev,
				    u32 value, u32 addr)
{
	iowrite32(value, params_vdev->dev->base_addr + addr);
}

static inline u32 rkisp1_ioread32(struct rkisp1_isp_params_vdev *params_vdev,
				  u32 addr)
{
	return ioread32(params_vdev->dev->base_addr + addr);
}

static inline void isp_param_set_bits(struct rkisp1_isp_params_vdev
					     *params_vdev,
				      u32 reg, u32 bit_mask)
{
	u32 val;

	val = rkisp1_ioread32(params_vdev, reg);
	rkisp1_iowrite32(params_vdev, val | bit_mask, reg);
}

static inline void isp_param_clear_bits(struct rkisp1_isp_params_vdev
					       *params_vdev,
					u32 reg, u32 bit_mask)
{
	u32 val;

	val = rkisp1_ioread32(params_vdev, reg);
	rkisp1_iowrite32(params_vdev, val & ~bit_mask, reg);
}

/* ISP BP interface function */
static void isp_dpcc_config(struct rkisp1_isp_params_vdev *params_vdev,
			    const struct cifisp_dpcc_config *arg)
{
	unsigned int i;
	u32 mode;

	/* avoid to override the old enable value */
	mode = rkisp1_ioread32(params_vdev, CIF_ISP_DPCC_MODE);
	mode &= CIF_ISP_DPCC_ENA;
	mode |= arg->mode & ~CIF_ISP_DPCC_ENA;
	rkisp1_iowrite32(params_vdev, mode, CIF_ISP_DPCC_MODE);
	rkisp1_iowrite32(params_vdev, arg->output_mode,
			 CIF_ISP_DPCC_OUTPUT_MODE);
	rkisp1_iowrite32(params_vdev, arg->set_use, CIF_ISP_DPCC_SET_USE);

	rkisp1_iowrite32(params_vdev, arg->methods[0].method,
			 CIF_ISP_DPCC_METHODS_SET_1);
	rkisp1_iowrite32(params_vdev, arg->methods[1].method,
			 CIF_ISP_DPCC_METHODS_SET_2);
	rkisp1_iowrite32(params_vdev, arg->methods[2].method,
			 CIF_ISP_DPCC_METHODS_SET_3);
	for (i = 0; i < CIFISP_DPCC_METHODS_MAX; i++) {
		rkisp1_iowrite32(params_vdev, arg->methods[i].line_thresh,
				 RKISP1_ISP_DPCC_LINE_THRESH(i));
		rkisp1_iowrite32(params_vdev, arg->methods[i].line_mad_fac,
				 RKISP1_ISP_DPCC_LINE_MAD_FAC(i));
		rkisp1_iowrite32(params_vdev, arg->methods[i].pg_fac,
				 RKISP1_ISP_DPCC_PG_FAC(i));
		rkisp1_iowrite32(params_vdev, arg->methods[i].rnd_thresh,
				 RKISP1_ISP_DPCC_RND_THRESH(i));
		rkisp1_iowrite32(params_vdev, arg->methods[i].rg_fac,
				 RKISP1_ISP_DPCC_RG_FAC(i));
	}

	rkisp1_iowrite32(params_vdev, arg->rnd_offs, CIF_ISP_DPCC_RND_OFFS);
	rkisp1_iowrite32(params_vdev, arg->ro_limits, CIF_ISP_DPCC_RO_LIMITS);
}

/* ISP black level subtraction interface function */
static void isp_bls_config(struct rkisp1_isp_params_vdev *params_vdev,
			   const struct cifisp_bls_config *arg)
{
	/* avoid to override the old enable value */
	u32 new_control;

	new_control = rkisp1_ioread32(params_vdev, CIF_ISP_BLS_CTRL);
	new_control &= CIF_ISP_BLS_ENA;
	/* fixed subtraction values */
	if (!arg->enable_auto) {
		const struct cifisp_bls_fixed_val *pval = &arg->fixed_val;

		switch (params_vdev->raw_type) {
		case RAW_BGGR:
			rkisp1_iowrite32(params_vdev,
					 pval->r, CIF_ISP_BLS_D_FIXED);
			rkisp1_iowrite32(params_vdev,
					 pval->gr, CIF_ISP_BLS_C_FIXED);
			rkisp1_iowrite32(params_vdev,
					 pval->gb, CIF_ISP_BLS_B_FIXED);
			rkisp1_iowrite32(params_vdev,
					 pval->b, CIF_ISP_BLS_A_FIXED);
			break;
		case RAW_GBRG:
			rkisp1_iowrite32(params_vdev,
					 pval->r, CIF_ISP_BLS_C_FIXED);
			rkisp1_iowrite32(params_vdev,
					 pval->gr, CIF_ISP_BLS_D_FIXED);
			rkisp1_iowrite32(params_vdev,
					 pval->gb, CIF_ISP_BLS_A_FIXED);
			rkisp1_iowrite32(params_vdev,
					 pval->b, CIF_ISP_BLS_B_FIXED);
			break;
		case RAW_GRBG:
			rkisp1_iowrite32(params_vdev,
					 pval->r, CIF_ISP_BLS_B_FIXED);
			rkisp1_iowrite32(params_vdev,
					 pval->gr, CIF_ISP_BLS_A_FIXED);
			rkisp1_iowrite32(params_vdev,
					 pval->gb, CIF_ISP_BLS_D_FIXED);
			rkisp1_iowrite32(params_vdev,
					 pval->b, CIF_ISP_BLS_C_FIXED);
			break;
		case RAW_RGGB:
			rkisp1_iowrite32(params_vdev,
					 pval->r, CIF_ISP_BLS_A_FIXED);
			rkisp1_iowrite32(params_vdev,
					 pval->gr, CIF_ISP_BLS_B_FIXED);
			rkisp1_iowrite32(params_vdev,
					 pval->gb, CIF_ISP_BLS_C_FIXED);
			rkisp1_iowrite32(params_vdev,
					 pval->b, CIF_ISP_BLS_D_FIXED);
			break;
		default:
			break;
		}

	} else {
		if (arg->en_windows & BIT(1)) {
			rkisp1_iowrite32(params_vdev, arg->bls_window2.h_offs,
					 CIF_ISP_BLS_H2_START);
			rkisp1_iowrite32(params_vdev, arg->bls_window2.h_size,
					 CIF_ISP_BLS_H2_STOP);
			rkisp1_iowrite32(params_vdev, arg->bls_window2.v_offs,
					 CIF_ISP_BLS_V2_START);
			rkisp1_iowrite32(params_vdev, arg->bls_window2.v_size,
					 CIF_ISP_BLS_V2_STOP);
			new_control |= CIF_ISP_BLS_WINDOW_2;
		}

		if (arg->en_windows & BIT(0)) {
			rkisp1_iowrite32(params_vdev, arg->bls_window1.h_offs,
					 CIF_ISP_BLS_H1_START);
			rkisp1_iowrite32(params_vdev, arg->bls_window1.h_size,
					 CIF_ISP_BLS_H1_STOP);
			rkisp1_iowrite32(params_vdev, arg->bls_window1.v_offs,
					 CIF_ISP_BLS_V1_START);
			rkisp1_iowrite32(params_vdev, arg->bls_window1.v_size,
					 CIF_ISP_BLS_V1_STOP);
			new_control |= CIF_ISP_BLS_WINDOW_1;
		}

		rkisp1_iowrite32(params_vdev, arg->bls_samples,
				 CIF_ISP_BLS_SAMPLES);

		new_control |= CIF_ISP_BLS_MODE_MEASURED;
	}
	rkisp1_iowrite32(params_vdev, new_control, CIF_ISP_BLS_CTRL);
}

/* ISP LS correction interface function */
static void
isp_lsc_matrix_config_v10(struct rkisp1_isp_params_vdev *params_vdev,
			  const struct cifisp_lsc_config *pconfig)
{
	int i, j;
	unsigned int isp_lsc_status, sram_addr, isp_lsc_table_sel;
	unsigned int data;

	isp_lsc_status = rkisp1_ioread32(params_vdev, CIF_ISP_LSC_STATUS);

	/* CIF_ISP_LSC_TABLE_ADDRESS_153 = ( 17 * 18 ) >> 1 */
	sram_addr = (isp_lsc_status & CIF_ISP_LSC_ACTIVE_TABLE) ?
		     CIF_ISP_LSC_TABLE_ADDRESS_0 :
		     CIF_ISP_LSC_TABLE_ADDRESS_153;
	rkisp1_iowrite32(params_vdev, sram_addr, CIF_ISP_LSC_R_TABLE_ADDR);
	rkisp1_iowrite32(params_vdev, sram_addr, CIF_ISP_LSC_GR_TABLE_ADDR);
	rkisp1_iowrite32(params_vdev, sram_addr, CIF_ISP_LSC_GB_TABLE_ADDR);
	rkisp1_iowrite32(params_vdev, sram_addr, CIF_ISP_LSC_B_TABLE_ADDR);

	/* program data tables (table size is 9 * 17 = 153) */
	for (i = 0; i < CIF_ISP_LSC_SECTORS_MAX * CIF_ISP_LSC_SECTORS_MAX;
	     i += CIF_ISP_LSC_SECTORS_MAX) {
		/*
		 * 17 sectors with 2 values in one DWORD = 9
		 * DWORDs (2nd value of last DWORD unused)
		 */
		for (j = 0; j < CIF_ISP_LSC_SECTORS_MAX - 1; j += 2) {
			data = CIF_ISP_LSC_TABLE_DATA_V10(
					pconfig->r_data_tbl[i + j],
					pconfig->r_data_tbl[i + j + 1]);
			rkisp1_iowrite32(params_vdev, data,
					 CIF_ISP_LSC_R_TABLE_DATA);

			data = CIF_ISP_LSC_TABLE_DATA_V10(
					pconfig->gr_data_tbl[i + j],
					pconfig->gr_data_tbl[i + j + 1]);
			rkisp1_iowrite32(params_vdev, data,
					 CIF_ISP_LSC_GR_TABLE_DATA);

			data = CIF_ISP_LSC_TABLE_DATA_V10(
					pconfig->gb_data_tbl[i + j],
					pconfig->gb_data_tbl[i + j + 1]);
			rkisp1_iowrite32(params_vdev, data,
					 CIF_ISP_LSC_GB_TABLE_DATA);

			data = CIF_ISP_LSC_TABLE_DATA_V10(
					pconfig->b_data_tbl[i + j],
					pconfig->b_data_tbl[i + j + 1]);
			rkisp1_iowrite32(params_vdev, data,
					 CIF_ISP_LSC_B_TABLE_DATA);
		}

		data = CIF_ISP_LSC_TABLE_DATA_V10(
				pconfig->r_data_tbl[i + j],
				0);
		rkisp1_iowrite32(params_vdev, data,
				CIF_ISP_LSC_R_TABLE_DATA);

		data = CIF_ISP_LSC_TABLE_DATA_V10(
				pconfig->gr_data_tbl[i + j],
				0);
		rkisp1_iowrite32(params_vdev, data,
				CIF_ISP_LSC_GR_TABLE_DATA);

		data = CIF_ISP_LSC_TABLE_DATA_V10(
				pconfig->gb_data_tbl[i + j],
				0);
		rkisp1_iowrite32(params_vdev, data,
				CIF_ISP_LSC_GB_TABLE_DATA);

		data = CIF_ISP_LSC_TABLE_DATA_V10(
				pconfig->b_data_tbl[i + j],
				0);
		rkisp1_iowrite32(params_vdev, data,
				CIF_ISP_LSC_B_TABLE_DATA);
	}
	isp_lsc_table_sel = (isp_lsc_status & CIF_ISP_LSC_ACTIVE_TABLE) ?
				CIF_ISP_LSC_TABLE_0 : CIF_ISP_LSC_TABLE_1;
	rkisp1_iowrite32(params_vdev, isp_lsc_table_sel, CIF_ISP_LSC_TABLE_SEL);
}

static void
isp_lsc_matrix_config_v12(struct rkisp1_isp_params_vdev *params_vdev,
			  const struct cifisp_lsc_config *pconfig)
{
	int i, j;
	unsigned int isp_lsc_status, sram_addr, isp_lsc_table_sel;
	unsigned int data;

	isp_lsc_status = rkisp1_ioread32(params_vdev, CIF_ISP_LSC_STATUS);

	/* CIF_ISP_LSC_TABLE_ADDRESS_153 = ( 17 * 18 ) >> 1 */
	sram_addr = (isp_lsc_status & CIF_ISP_LSC_ACTIVE_TABLE) ?
		     CIF_ISP_LSC_TABLE_ADDRESS_0 :
		     CIF_ISP_LSC_TABLE_ADDRESS_153;
	rkisp1_iowrite32(params_vdev, sram_addr, CIF_ISP_LSC_R_TABLE_ADDR);
	rkisp1_iowrite32(params_vdev, sram_addr, CIF_ISP_LSC_GR_TABLE_ADDR);
	rkisp1_iowrite32(params_vdev, sram_addr, CIF_ISP_LSC_GB_TABLE_ADDR);
	rkisp1_iowrite32(params_vdev, sram_addr, CIF_ISP_LSC_B_TABLE_ADDR);

	/* program data tables (table size is 9 * 17 = 153) */
	for (i = 0; i < CIF_ISP_LSC_SECTORS_MAX * CIF_ISP_LSC_SECTORS_MAX;
	     i += CIF_ISP_LSC_SECTORS_MAX) {
		/*
		 * 17 sectors with 2 values in one DWORD = 9
		 * DWORDs (2nd value of last DWORD unused)
		 */
		for (j = 0; j < CIF_ISP_LSC_SECTORS_MAX - 1; j += 2) {
			data = CIF_ISP_LSC_TABLE_DATA_V12(
					pconfig->r_data_tbl[i + j],
					pconfig->r_data_tbl[i + j + 1]);
			rkisp1_iowrite32(params_vdev, data,
					 CIF_ISP_LSC_R_TABLE_DATA);

			data = CIF_ISP_LSC_TABLE_DATA_V12(
					pconfig->gr_data_tbl[i + j],
					pconfig->gr_data_tbl[i + j + 1]);
			rkisp1_iowrite32(params_vdev, data,
					 CIF_ISP_LSC_GR_TABLE_DATA);

			data = CIF_ISP_LSC_TABLE_DATA_V12(
					pconfig->gb_data_tbl[i + j],
					pconfig->gb_data_tbl[i + j + 1]);
			rkisp1_iowrite32(params_vdev, data,
					 CIF_ISP_LSC_GB_TABLE_DATA);

			data = CIF_ISP_LSC_TABLE_DATA_V12(
					pconfig->b_data_tbl[i + j],
					pconfig->b_data_tbl[i + j + 1]);
			rkisp1_iowrite32(params_vdev, data,
					 CIF_ISP_LSC_B_TABLE_DATA);
		}

		data = CIF_ISP_LSC_TABLE_DATA_V12(
				pconfig->r_data_tbl[i + j],
				0);
		rkisp1_iowrite32(params_vdev, data,
				CIF_ISP_LSC_R_TABLE_DATA);

		data = CIF_ISP_LSC_TABLE_DATA_V12(
				pconfig->gr_data_tbl[i + j],
				0);
		rkisp1_iowrite32(params_vdev, data,
				CIF_ISP_LSC_GR_TABLE_DATA);

		data = CIF_ISP_LSC_TABLE_DATA_V12(
				pconfig->gb_data_tbl[i + j],
				0);
		rkisp1_iowrite32(params_vdev, data,
				CIF_ISP_LSC_GB_TABLE_DATA);

		data = CIF_ISP_LSC_TABLE_DATA_V12(
				pconfig->b_data_tbl[i + j],
				0);
		rkisp1_iowrite32(params_vdev, data,
				CIF_ISP_LSC_B_TABLE_DATA);
	}
	isp_lsc_table_sel = (isp_lsc_status & CIF_ISP_LSC_ACTIVE_TABLE) ?
				CIF_ISP_LSC_TABLE_0 : CIF_ISP_LSC_TABLE_1;
	rkisp1_iowrite32(params_vdev, isp_lsc_table_sel, CIF_ISP_LSC_TABLE_SEL);
}

static void isp_lsc_config(struct rkisp1_isp_params_vdev *params_vdev,
			   const struct cifisp_lsc_config *arg)
{
	int i;
	u32 lsc_ctrl;
	unsigned int data;

	/* To config must be off , store the current status firstly */
	lsc_ctrl = rkisp1_ioread32(params_vdev, CIF_ISP_LSC_CTRL);
	isp_param_clear_bits(params_vdev, CIF_ISP_LSC_CTRL,
			     CIF_ISP_LSC_CTRL_ENA);
	params_vdev->ops->lsc_matrix_config(params_vdev, arg);

	for (i = 0; i < 4; i++) {
		/* program x size tables */
		data = CIF_ISP_LSC_SECT_SIZE(arg->x_size_tbl[i * 2],
					arg->x_size_tbl[i * 2 + 1]);
		rkisp1_iowrite32(params_vdev, data,
				 CIF_ISP_LSC_XSIZE_01 + i * 4);

		/* program x grad tables */
		data = CIF_ISP_LSC_SECT_SIZE(arg->x_grad_tbl[i * 2],
					arg->x_grad_tbl[i * 2 + 1]);
		rkisp1_iowrite32(params_vdev, data,
				 CIF_ISP_LSC_XGRAD_01 + i * 4);

		/* program y size tables */
		data = CIF_ISP_LSC_SECT_SIZE(arg->y_size_tbl[i * 2],
					arg->y_size_tbl[i * 2 + 1]);
		rkisp1_iowrite32(params_vdev, data,
				 CIF_ISP_LSC_YSIZE_01 + i * 4);

		/* program y grad tables */
		data = CIF_ISP_LSC_SECT_SIZE(arg->y_grad_tbl[i * 2],
					arg->y_grad_tbl[i * 2 + 1]);
		rkisp1_iowrite32(params_vdev, data,
				 CIF_ISP_LSC_YGRAD_01 + i * 4);
	}

	/* restore the lsc ctrl status */
	if (lsc_ctrl & CIF_ISP_LSC_CTRL_ENA) {
		isp_param_set_bits(params_vdev,
				   CIF_ISP_LSC_CTRL,
				   CIF_ISP_LSC_CTRL_ENA);
	} else {
		isp_param_clear_bits(params_vdev,
				     CIF_ISP_LSC_CTRL,
				     CIF_ISP_LSC_CTRL_ENA);
	}
}

/* ISP Filtering function */
static void isp_flt_config(struct rkisp1_isp_params_vdev *params_vdev,
			   const struct cifisp_flt_config *arg)
{
	u32 filt_mode;

	rkisp1_iowrite32(params_vdev, arg->thresh_bl0, CIF_ISP_FILT_THRESH_BL0);
	rkisp1_iowrite32(params_vdev, arg->thresh_bl1, CIF_ISP_FILT_THRESH_BL1);
	rkisp1_iowrite32(params_vdev, arg->thresh_sh0, CIF_ISP_FILT_THRESH_SH0);
	rkisp1_iowrite32(params_vdev, arg->thresh_sh1, CIF_ISP_FILT_THRESH_SH1);
	rkisp1_iowrite32(params_vdev, arg->fac_bl0, CIF_ISP_FILT_FAC_BL0);
	rkisp1_iowrite32(params_vdev, arg->fac_bl1, CIF_ISP_FILT_FAC_BL1);
	rkisp1_iowrite32(params_vdev, arg->fac_mid, CIF_ISP_FILT_FAC_MID);
	rkisp1_iowrite32(params_vdev, arg->fac_sh0, CIF_ISP_FILT_FAC_SH0);
	rkisp1_iowrite32(params_vdev, arg->fac_sh1, CIF_ISP_FILT_FAC_SH1);
	rkisp1_iowrite32(params_vdev, arg->lum_weight, CIF_ISP_FILT_LUM_WEIGHT);

	/* avoid to override the old enable value */
	filt_mode = rkisp1_ioread32(params_vdev, CIF_ISP_FILT_MODE);
	filt_mode &= CIF_ISP_FLT_ENA;
	if (arg->mode)
		filt_mode |= CIF_ISP_FLT_MODE_DNR;
	filt_mode |= CIF_ISP_FLT_CHROMA_V_MODE(arg->chr_v_mode) |
				 CIF_ISP_FLT_CHROMA_H_MODE(arg->chr_h_mode) |
				 CIF_ISP_FLT_GREEN_STAGE1(arg->grn_stage1);
	rkisp1_iowrite32(params_vdev, filt_mode, CIF_ISP_FILT_MODE);
}

/* ISP demosaic interface function */
static void isp_bdm_config(struct rkisp1_isp_params_vdev *params_vdev,
			   const struct cifisp_bdm_config *arg)
{
	u32 bdm_th;

	/* avoid to override the old enable value */
	bdm_th = rkisp1_ioread32(params_vdev, CIF_ISP_DEMOSAIC);
	bdm_th &= CIF_ISP_DEMOSAIC_BYPASS;
	bdm_th |= arg->demosaic_th & ~CIF_ISP_DEMOSAIC_BYPASS;
	/* set demosaic threshold */
	rkisp1_iowrite32(params_vdev, bdm_th, CIF_ISP_DEMOSAIC);
}

/* ISP GAMMA correction interface function */
static void isp_sdg_config(struct rkisp1_isp_params_vdev *params_vdev,
			   const struct cifisp_sdg_config *arg)
{
	int i;

	rkisp1_iowrite32(params_vdev,
			 arg->xa_pnts.gamma_dx0, CIF_ISP_GAMMA_DX_LO);
	rkisp1_iowrite32(params_vdev,
			 arg->xa_pnts.gamma_dx1, CIF_ISP_GAMMA_DX_HI);

	for (i = 0; i < CIFISP_DEGAMMA_CURVE_SIZE; i++) {
		rkisp1_iowrite32(params_vdev, arg->curve_r.gamma_y[i],
				 CIF_ISP_GAMMA_R_Y0 + i * 4);
		rkisp1_iowrite32(params_vdev, arg->curve_g.gamma_y[i],
				 CIF_ISP_GAMMA_G_Y0 + i * 4);
		rkisp1_iowrite32(params_vdev, arg->curve_b.gamma_y[i],
				 CIF_ISP_GAMMA_B_Y0 + i * 4);
	}
}

/* ISP GAMMA correction interface function */
static void isp_goc_config_v10(struct rkisp1_isp_params_vdev *params_vdev,
			       const struct cifisp_goc_config *arg)
{
	int i;

	isp_param_clear_bits(params_vdev, CIF_ISP_CTRL,
			     CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA);
	rkisp1_iowrite32(params_vdev, arg->mode, CIF_ISP_GAMMA_OUT_MODE_V10);

	for (i = 0; i < params_vdev->config->gamma_out_max_samples; i++)
		rkisp1_iowrite32(params_vdev, arg->gamma_y[i],
				 CIF_ISP_GAMMA_OUT_Y_0_V10 + i * 4);
}

static void isp_goc_config_v12(struct rkisp1_isp_params_vdev *params_vdev,
			       const struct cifisp_goc_config *arg)
{
	int i;
	u32 value;

	isp_param_clear_bits(params_vdev, CIF_ISP_CTRL,
			     CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA);
	rkisp1_iowrite32(params_vdev, arg->mode, CIF_ISP_GAMMA_OUT_MODE_V12);

	for (i = 0; i < params_vdev->config->gamma_out_max_samples / 2; i++) {
		value = CIF_ISP_GAMMA_REG_VALUE_V12(
			arg->gamma_y[2 * i + 1],
			arg->gamma_y[2 * i]);
		rkisp1_iowrite32(params_vdev, value,
				 CIF_ISP_GAMMA_OUT_Y_0_V12 + i * 4);
	}
}

/* ISP Cross Talk */
static void isp_ctk_config(struct rkisp1_isp_params_vdev *params_vdev,
			   const struct cifisp_ctk_config *arg)
{
	rkisp1_iowrite32(params_vdev, arg->coeff0, CIF_ISP_CT_COEFF_0);
	rkisp1_iowrite32(params_vdev, arg->coeff1, CIF_ISP_CT_COEFF_1);
	rkisp1_iowrite32(params_vdev, arg->coeff2, CIF_ISP_CT_COEFF_2);
	rkisp1_iowrite32(params_vdev, arg->coeff3, CIF_ISP_CT_COEFF_3);
	rkisp1_iowrite32(params_vdev, arg->coeff4, CIF_ISP_CT_COEFF_4);
	rkisp1_iowrite32(params_vdev, arg->coeff5, CIF_ISP_CT_COEFF_5);
	rkisp1_iowrite32(params_vdev, arg->coeff6, CIF_ISP_CT_COEFF_6);
	rkisp1_iowrite32(params_vdev, arg->coeff7, CIF_ISP_CT_COEFF_7);
	rkisp1_iowrite32(params_vdev, arg->coeff8, CIF_ISP_CT_COEFF_8);
	rkisp1_iowrite32(params_vdev, arg->ct_offset_r, CIF_ISP_CT_OFFSET_R);
	rkisp1_iowrite32(params_vdev, arg->ct_offset_g, CIF_ISP_CT_OFFSET_G);
	rkisp1_iowrite32(params_vdev, arg->ct_offset_b, CIF_ISP_CT_OFFSET_B);
}

static void isp_ctk_enable(struct rkisp1_isp_params_vdev *params_vdev, bool en)
{
	if (en)
		return;

	/* Write back the default values. */
	rkisp1_iowrite32(params_vdev, 0x80, CIF_ISP_CT_COEFF_0);
	rkisp1_iowrite32(params_vdev, 0, CIF_ISP_CT_COEFF_1);
	rkisp1_iowrite32(params_vdev, 0, CIF_ISP_CT_COEFF_2);
	rkisp1_iowrite32(params_vdev, 0, CIF_ISP_CT_COEFF_3);
	rkisp1_iowrite32(params_vdev, 0x80, CIF_ISP_CT_COEFF_4);
	rkisp1_iowrite32(params_vdev, 0, CIF_ISP_CT_COEFF_5);
	rkisp1_iowrite32(params_vdev, 0, CIF_ISP_CT_COEFF_6);
	rkisp1_iowrite32(params_vdev, 0, CIF_ISP_CT_COEFF_7);
	rkisp1_iowrite32(params_vdev, 0x80, CIF_ISP_CT_COEFF_8);

	rkisp1_iowrite32(params_vdev, 0, CIF_ISP_CT_OFFSET_R);
	rkisp1_iowrite32(params_vdev, 0, CIF_ISP_CT_OFFSET_G);
	rkisp1_iowrite32(params_vdev, 0, CIF_ISP_CT_OFFSET_B);
}

/* ISP White Balance Mode */
static void isp_awb_meas_config_v10(struct rkisp1_isp_params_vdev *params_vdev,
				    const struct cifisp_awb_meas_config *arg)
{
	u32 reg_val = 0;
	/* based on the mode,configure the awb module */
	if (arg->awb_mode == CIFISP_AWB_MODE_YCBCR) {
		/* Reference Cb and Cr */
		rkisp1_iowrite32(params_vdev,
				 CIF_ISP_AWB_REF_CR_SET(arg->awb_ref_cr) |
				 arg->awb_ref_cb, CIF_ISP_AWB_REF_V10);
		/* Yc Threshold */
		rkisp1_iowrite32(params_vdev,
				 CIF_ISP_AWB_MAX_Y_SET(arg->max_y) |
				 CIF_ISP_AWB_MIN_Y_SET(arg->min_y) |
				 CIF_ISP_AWB_MAX_CS_SET(arg->max_csum) |
				 arg->min_c, CIF_ISP_AWB_THRESH_V10);
	}

	reg_val = rkisp1_ioread32(params_vdev, CIF_ISP_AWB_PROP_V10);
	if (arg->enable_ymax_cmp)
		reg_val |= CIF_ISP_AWB_YMAX_CMP_EN;
	else
		reg_val &= ~CIF_ISP_AWB_YMAX_CMP_EN;
	rkisp1_iowrite32(params_vdev, reg_val, CIF_ISP_AWB_PROP_V10);

	/* window offset */
	rkisp1_iowrite32(params_vdev,
			 arg->awb_wnd.v_offs, CIF_ISP_AWB_WND_V_OFFS_V10);
	rkisp1_iowrite32(params_vdev,
			 arg->awb_wnd.h_offs, CIF_ISP_AWB_WND_H_OFFS_V10);
	/* AWB window size */
	rkisp1_iowrite32(params_vdev,
			 arg->awb_wnd.v_size, CIF_ISP_AWB_WND_V_SIZE_V10);
	rkisp1_iowrite32(params_vdev,
			 arg->awb_wnd.h_size, CIF_ISP_AWB_WND_H_SIZE_V10);
	/* Number of frames */
	rkisp1_iowrite32(params_vdev,
			 arg->frames, CIF_ISP_AWB_FRAMES_V10);
}

static void isp_awb_meas_config_v12(struct rkisp1_isp_params_vdev *params_vdev,
				    const struct cifisp_awb_meas_config *arg)
{
	u32 reg_val = 0;
	/* based on the mode,configure the awb module */
	if (arg->awb_mode == CIFISP_AWB_MODE_YCBCR) {
		/* Reference Cb and Cr */
		rkisp1_iowrite32(params_vdev,
				 CIF_ISP_AWB_REF_CR_SET(arg->awb_ref_cr) |
				 arg->awb_ref_cb, CIF_ISP_AWB_REF_V12);
		/* Yc Threshold */
		rkisp1_iowrite32(params_vdev,
				 CIF_ISP_AWB_MAX_Y_SET(arg->max_y) |
				 CIF_ISP_AWB_MIN_Y_SET(arg->min_y) |
				 CIF_ISP_AWB_MAX_CS_SET(arg->max_csum) |
				 arg->min_c, CIF_ISP_AWB_THRESH_V12);
	}

	reg_val = rkisp1_ioread32(params_vdev, CIF_ISP_AWB_PROP_V12);
	if (arg->enable_ymax_cmp)
		reg_val |= CIF_ISP_AWB_YMAX_CMP_EN;
	else
		reg_val &= ~CIF_ISP_AWB_YMAX_CMP_EN;
	reg_val &= ~CIF_ISP_AWB_SET_FRAMES_MASK_V12;
	reg_val |= CIF_ISP_AWB_SET_FRAMES_V12(arg->frames);
	rkisp1_iowrite32(params_vdev, reg_val, CIF_ISP_AWB_PROP_V12);

	/* window offset */
	rkisp1_iowrite32(params_vdev,
			 arg->awb_wnd.v_offs << 16 |
			 arg->awb_wnd.h_offs,
			 CIF_ISP_AWB_OFFS_V12);
	/* AWB window size */
	rkisp1_iowrite32(params_vdev,
			 arg->awb_wnd.v_size << 16 |
			 arg->awb_wnd.h_size,
			 CIF_ISP_AWB_SIZE_V12);
}

static void isp_awb_meas_enable_v10(struct rkisp1_isp_params_vdev *params_vdev,
				    const struct cifisp_awb_meas_config *arg, bool en)
{
	u32 reg_val = rkisp1_ioread32(params_vdev, CIF_ISP_AWB_PROP_V10);

	/* switch off */
	reg_val &= CIF_ISP_AWB_MODE_MASK_NONE;

	if (en) {
		if (arg->awb_mode == CIFISP_AWB_MODE_RGB)
			reg_val |= CIF_ISP_AWB_MODE_RGB_EN;
		else
			reg_val |= CIF_ISP_AWB_MODE_YCBCR_EN;

		rkisp1_iowrite32(params_vdev, reg_val, CIF_ISP_AWB_PROP_V10);

		/* Measurements require AWB block be active. */
		/* TODO: need to enable here ? awb_gain_enable has done this */
		isp_param_set_bits(params_vdev, CIF_ISP_CTRL,
				   CIF_ISP_CTRL_ISP_AWB_ENA);
	} else {
		rkisp1_iowrite32(params_vdev,
				 reg_val, CIF_ISP_AWB_PROP_V10);
		isp_param_clear_bits(params_vdev, CIF_ISP_CTRL,
				     CIF_ISP_CTRL_ISP_AWB_ENA);
	}
}

static void isp_awb_meas_enable_v12(struct rkisp1_isp_params_vdev *params_vdev,
				    const struct cifisp_awb_meas_config *arg, bool en)
{
	u32 reg_val = rkisp1_ioread32(params_vdev, CIF_ISP_AWB_PROP_V12);

	/* switch off */
	reg_val &= CIF_ISP_AWB_MODE_MASK_NONE;

	if (en) {
		if (arg->awb_mode == CIFISP_AWB_MODE_RGB)
			reg_val |= CIF_ISP_AWB_MODE_RGB_EN;
		else
			reg_val |= CIF_ISP_AWB_MODE_YCBCR_EN;

		rkisp1_iowrite32(params_vdev, reg_val, CIF_ISP_AWB_PROP_V12);

		/* Measurements require AWB block be active. */
		/* TODO: need to enable here ? awb_gain_enable has done this */
		isp_param_set_bits(params_vdev, CIF_ISP_CTRL,
				   CIF_ISP_CTRL_ISP_AWB_ENA);
	} else {
		rkisp1_iowrite32(params_vdev,
				 reg_val, CIF_ISP_AWB_PROP_V12);
		isp_param_clear_bits(params_vdev, CIF_ISP_CTRL,
				     CIF_ISP_CTRL_ISP_AWB_ENA);
	}
}

static void isp_awb_gain_config_v10(struct rkisp1_isp_params_vdev *params_vdev,
				    const struct cifisp_awb_gain_config *arg)
{
	rkisp1_iowrite32(params_vdev,
			 CIF_ISP_AWB_GAIN_R_SET(arg->gain_green_r) |
			 arg->gain_green_b, CIF_ISP_AWB_GAIN_G_V10);

	rkisp1_iowrite32(params_vdev, CIF_ISP_AWB_GAIN_R_SET(arg->gain_red) |
			 arg->gain_blue, CIF_ISP_AWB_GAIN_RB_V10);
}

static void isp_awb_gain_config_v12(struct rkisp1_isp_params_vdev *params_vdev,
				    const struct cifisp_awb_gain_config *arg)
{
	rkisp1_iowrite32(params_vdev,
			 CIF_ISP_AWB_GAIN_R_SET(arg->gain_green_r) |
			 arg->gain_green_b, CIF_ISP_AWB_GAIN_G_V12);

	rkisp1_iowrite32(params_vdev, CIF_ISP_AWB_GAIN_R_SET(arg->gain_red) |
			 arg->gain_blue, CIF_ISP_AWB_GAIN_RB_V12);
}

static void isp_aec_config_v10(struct rkisp1_isp_params_vdev *params_vdev,
			       const struct cifisp_aec_config *arg)
{
	unsigned int block_hsize, block_vsize;
	u32 exp_ctrl;

	/* avoid to override the old enable value */
	exp_ctrl = rkisp1_ioread32(params_vdev, CIF_ISP_EXP_CTRL);
	exp_ctrl &= CIF_ISP_EXP_ENA;
	if (arg->autostop)
		exp_ctrl |= CIF_ISP_EXP_CTRL_AUTOSTOP;
	if (arg->mode == CIFISP_EXP_MEASURING_MODE_1)
		exp_ctrl |= CIF_ISP_EXP_CTRL_MEASMODE_1;
	rkisp1_iowrite32(params_vdev, exp_ctrl, CIF_ISP_EXP_CTRL);

	rkisp1_iowrite32(params_vdev,
			 arg->meas_window.h_offs, CIF_ISP_EXP_H_OFFSET_V10);
	rkisp1_iowrite32(params_vdev,
			 arg->meas_window.v_offs, CIF_ISP_EXP_V_OFFSET_V10);

	block_hsize = arg->meas_window.h_size / CIF_ISP_EXP_COLUMN_NUM_V10 - 1;
	block_vsize = arg->meas_window.v_size / CIF_ISP_EXP_ROW_NUM_V10 - 1;

	rkisp1_iowrite32(params_vdev, CIF_ISP_EXP_H_SIZE_SET_V10(block_hsize),
			 CIF_ISP_EXP_H_SIZE_V10);
	rkisp1_iowrite32(params_vdev, CIF_ISP_EXP_V_SIZE_SET_V10(block_vsize),
			 CIF_ISP_EXP_V_SIZE_V10);
}

static void isp_aec_config_v12(struct rkisp1_isp_params_vdev *params_vdev,
			       const struct cifisp_aec_config *arg)
{
	u32 exp_ctrl;
	u32 block_hsize, block_vsize;
	u32 wnd_num_idx = 1;
	const u32 ae_wnd_num[] = {
		5, 9, 15, 15
	};

	/* avoid to override the old enable value */
	exp_ctrl = rkisp1_ioread32(params_vdev, CIF_ISP_EXP_CTRL);
	exp_ctrl &= CIF_ISP_EXP_ENA;
	if (arg->autostop)
		exp_ctrl |= CIF_ISP_EXP_CTRL_AUTOSTOP;
	if (arg->mode == CIFISP_EXP_MEASURING_MODE_1)
		exp_ctrl |= CIF_ISP_EXP_CTRL_MEASMODE_1;
	exp_ctrl |= CIF_ISP_EXP_CTRL_WNDNUM_SET_V12(wnd_num_idx);
	rkisp1_iowrite32(params_vdev, exp_ctrl, CIF_ISP_EXP_CTRL);

	rkisp1_iowrite32(params_vdev,
			 CIF_ISP_EXP_V_OFFSET_SET_V12(arg->meas_window.v_offs) |
			 CIF_ISP_EXP_H_OFFSET_SET_V12(arg->meas_window.h_offs),
			 CIF_ISP_EXP_OFFS_V12);

	block_hsize = arg->meas_window.h_size / ae_wnd_num[wnd_num_idx] - 1;
	block_vsize = arg->meas_window.v_size / ae_wnd_num[wnd_num_idx] - 1;

	rkisp1_iowrite32(params_vdev,
			 CIF_ISP_EXP_V_SIZE_SET_V12(block_vsize) |
			 CIF_ISP_EXP_H_SIZE_SET_V12(block_hsize),
			 CIF_ISP_EXP_SIZE_V12);
}

static void isp_cproc_config(struct rkisp1_isp_params_vdev *params_vdev,
			     const struct cifisp_cproc_config *arg)
{
	struct cifisp_isp_other_cfg *cur_other_cfg = &params_vdev->cur_params.others;
	struct cifisp_ie_config *cur_ie_config = &cur_other_cfg->ie_config;
	u32 effect = cur_ie_config->effect;
	u32 quantization = params_vdev->quantization;

	rkisp1_iowrite32(params_vdev, arg->contrast, CIF_C_PROC_CONTRAST);
	rkisp1_iowrite32(params_vdev, arg->hue, CIF_C_PROC_HUE);
	rkisp1_iowrite32(params_vdev, arg->sat, CIF_C_PROC_SATURATION);
	rkisp1_iowrite32(params_vdev, arg->brightness, CIF_C_PROC_BRIGHTNESS);

	if (quantization != V4L2_QUANTIZATION_FULL_RANGE ||
	    effect != V4L2_COLORFX_NONE) {
		isp_param_clear_bits(params_vdev, CIF_C_PROC_CTRL,
				     CIF_C_PROC_YOUT_FULL |
				     CIF_C_PROC_YIN_FULL |
				     CIF_C_PROC_COUT_FULL);
	} else {
		isp_param_set_bits(params_vdev, CIF_C_PROC_CTRL,
				   CIF_C_PROC_YOUT_FULL |
				   CIF_C_PROC_YIN_FULL |
				   CIF_C_PROC_COUT_FULL);
	}
}

static void isp_hst_config_v10(struct rkisp1_isp_params_vdev *params_vdev,
			       const struct cifisp_hst_config *arg)
{
	unsigned int block_hsize, block_vsize;
	const u32 hist_weight_regs[] = {
		CIF_ISP_HIST_WEIGHT_00TO30_V10, CIF_ISP_HIST_WEIGHT_40TO21_V10,
		CIF_ISP_HIST_WEIGHT_31TO12_V10, CIF_ISP_HIST_WEIGHT_22TO03_V10,
		CIF_ISP_HIST_WEIGHT_13TO43_V10, CIF_ISP_HIST_WEIGHT_04TO34_V10,
		CIF_ISP_HIST_WEIGHT_44_V10,
	};
	int i;
	const u8 *weight;
	u32 hist_prop;

	/* avoid to override the old enable value */
	hist_prop = rkisp1_ioread32(params_vdev, CIF_ISP_HIST_PROP_V10);
	hist_prop &= CIF_ISP_HIST_PROP_MODE_MASK_V10;
	hist_prop |= CIF_ISP_HIST_PREDIV_SET_V10(arg->histogram_predivider);
	rkisp1_iowrite32(params_vdev, hist_prop, CIF_ISP_HIST_PROP_V10);
	rkisp1_iowrite32(params_vdev,
			arg->meas_window.h_offs,
			CIF_ISP_HIST_H_OFFS_V10);
	rkisp1_iowrite32(params_vdev,
			arg->meas_window.v_offs,
			CIF_ISP_HIST_V_OFFS_V10);

	block_hsize = arg->meas_window.h_size / CIF_ISP_HIST_COLUMN_NUM_V10 - 1;
	block_vsize = arg->meas_window.v_size / CIF_ISP_HIST_ROW_NUM_V10 - 1;

	rkisp1_iowrite32(params_vdev, block_hsize, CIF_ISP_HIST_H_SIZE_V10);
	rkisp1_iowrite32(params_vdev, block_vsize, CIF_ISP_HIST_V_SIZE_V10);

	weight = arg->hist_weight;
	for (i = 0; i < ARRAY_SIZE(hist_weight_regs); ++i, weight += 4)
		rkisp1_iowrite32(params_vdev, CIF_ISP_HIST_WEIGHT_SET_V10(
				 weight[0], weight[1], weight[2], weight[3]),
				 hist_weight_regs[i]);
}

static void isp_hst_config_v12(struct rkisp1_isp_params_vdev *params_vdev,
			       const struct cifisp_hst_config *arg)
{
	u32 i, j;
	u32 value;
	u32 hist_ctrl;
	u32 block_hsize, block_vsize;
	u32 wnd_num_idx, hist_weight_num;
	u8 weight15x15[CIF_ISP_HIST_WEIGHT_REG_SIZE_V12];
	const u32 hist_wnd_num[] = {
		5, 9, 15, 15
	};

	/* now we just support 9x9 window */
	wnd_num_idx = 1;
	memset(weight15x15, 0x00, sizeof(weight15x15));
	/* avoid to override the old enable value */
	hist_ctrl = rkisp1_ioread32(params_vdev, CIF_ISP_HIST_CTRL_V12);
	hist_ctrl &= CIF_ISP_HIST_CTRL_MODE_MASK_V12 |
		     CIF_ISP_HIST_CTRL_EN_MASK_V12;
	hist_ctrl = hist_ctrl |
		    CIF_ISP_HIST_CTRL_INTRSEL_SET_V12(1) |
		    CIF_ISP_HIST_CTRL_DATASEL_SET_V12(0) |
		    CIF_ISP_HIST_CTRL_WATERLINE_SET_V12(0) |
		    CIF_ISP_HIST_CTRL_AUTOSTOP_SET_V12(0) |
		    CIF_ISP_HIST_CTRL_WNDNUM_SET_V12(1) |
		    CIF_ISP_HIST_CTRL_STEPSIZE_SET_V12(arg->histogram_predivider);
	rkisp1_iowrite32(params_vdev, hist_ctrl, CIF_ISP_HIST_CTRL_V12);

	rkisp1_iowrite32(params_vdev,
			 CIF_ISP_HIST_OFFS_SET_V12(arg->meas_window.h_offs,
						   arg->meas_window.v_offs),
			 CIF_ISP_HIST_OFFS_V12);

	block_hsize = arg->meas_window.h_size / hist_wnd_num[wnd_num_idx] - 1;
	block_vsize = arg->meas_window.v_size / hist_wnd_num[wnd_num_idx] - 1;
	rkisp1_iowrite32(params_vdev,
			 CIF_ISP_HIST_SIZE_SET_V12(block_hsize, block_vsize),
			 CIF_ISP_HIST_SIZE_V12);

	for (i = 0; i < hist_wnd_num[wnd_num_idx]; i++) {
		for (j = 0; j < hist_wnd_num[wnd_num_idx]; j++) {
			weight15x15[i * CIF_ISP_HIST_ROW_NUM_V12 + j] =
				arg->hist_weight[i * hist_wnd_num[wnd_num_idx] + j];
		}
	}

	hist_weight_num = CIF_ISP_HIST_WEIGHT_REG_SIZE_V12;
	for (i = 0; i < (hist_weight_num / 4); i++) {
		value = CIF_ISP_HIST_WEIGHT_SET_V12(
				 weight15x15[4 * i + 0],
				 weight15x15[4 * i + 1],
				 weight15x15[4 * i + 2],
				 weight15x15[4 * i + 3]);
		rkisp1_iowrite32(params_vdev, value,
				 CIF_ISP_HIST_WEIGHT_V12 + 4 * i);
	}
	value = CIF_ISP_HIST_WEIGHT_SET_V12(
				 weight15x15[4 * i + 0], 0, 0, 0);
	rkisp1_iowrite32(params_vdev, value,
				 CIF_ISP_HIST_WEIGHT_V12 + 4 * i);
}

static void isp_hst_enable_v10(struct rkisp1_isp_params_vdev *params_vdev,
			       const struct cifisp_hst_config *arg, bool en)
{
	if (en) {
		u32 hist_prop = rkisp1_ioread32(params_vdev, CIF_ISP_HIST_PROP_V10);

		hist_prop &= ~CIF_ISP_HIST_PROP_MODE_MASK_V10;
		hist_prop |= arg->mode;
		isp_param_set_bits(params_vdev, CIF_ISP_HIST_PROP_V10, hist_prop);
	} else {
		isp_param_clear_bits(params_vdev, CIF_ISP_HIST_PROP_V10,
				CIF_ISP_HIST_PROP_MODE_MASK_V10);
	}
}

static void isp_hst_enable_v12(struct rkisp1_isp_params_vdev *params_vdev,
			       const struct cifisp_hst_config *arg, bool en)
{
	if (en) {
		u32 hist_ctrl = rkisp1_ioread32(params_vdev, CIF_ISP_HIST_CTRL_V12);

		hist_ctrl &= ~CIF_ISP_HIST_CTRL_MODE_MASK_V12;
		hist_ctrl |= CIF_ISP_HIST_CTRL_MODE_SET_V12(arg->mode);
		hist_ctrl |= CIF_ISP_HIST_CTRL_EN_SET_V12(1);
		isp_param_set_bits(params_vdev, CIF_ISP_HIST_CTRL_V12, hist_ctrl);
	} else {
		isp_param_clear_bits(params_vdev, CIF_ISP_HIST_CTRL_V12,
				CIF_ISP_HIST_CTRL_MODE_MASK_V12 |
				CIF_ISP_HIST_CTRL_EN_MASK_V12);
	}
}

static void isp_afm_config_v10(struct rkisp1_isp_params_vdev *params_vdev,
			       const struct cifisp_afc_config *arg)
{
	int i;
	size_t num_of_win = min_t(size_t, ARRAY_SIZE(arg->afm_win),
				  arg->num_afm_win);
	u32 afm_ctrl = rkisp1_ioread32(params_vdev, CIF_ISP_AFM_CTRL);

	/* Switch off to configure. */
	isp_param_clear_bits(params_vdev, CIF_ISP_AFM_CTRL, CIF_ISP_AFM_ENA);

	for (i = 0; i < num_of_win; i++) {
		rkisp1_iowrite32(params_vdev,
				 CIF_ISP_AFM_WINDOW_X(arg->afm_win[i].h_offs) |
				 CIF_ISP_AFM_WINDOW_Y(arg->afm_win[i].v_offs),
				 CIF_ISP_AFM_LT_A + i * 8);
		rkisp1_iowrite32(params_vdev,
				 CIF_ISP_AFM_WINDOW_X(arg->afm_win[i].h_size +
						      arg->afm_win[i].h_offs) |
				 CIF_ISP_AFM_WINDOW_Y(arg->afm_win[i].v_size +
						      arg->afm_win[i].v_offs),
				 CIF_ISP_AFM_RB_A + i * 8);
	}
	rkisp1_iowrite32(params_vdev, arg->thres, CIF_ISP_AFM_THRES);
	rkisp1_iowrite32(params_vdev, arg->var_shift, CIF_ISP_AFM_VAR_SHIFT);
	/* restore afm status */
	rkisp1_iowrite32(params_vdev, afm_ctrl, CIF_ISP_AFM_CTRL);
}

static void isp_afm_config_v12(struct rkisp1_isp_params_vdev *params_vdev,
			       const struct cifisp_afc_config *arg)
{
	unsigned int i;
	u32 lum_var_shift, afm_var_shift;
	size_t num_of_win = min_t(size_t, ARRAY_SIZE(arg->afm_win),
				  arg->num_afm_win);
	u32 afm_ctrl = rkisp1_ioread32(params_vdev, CIF_ISP_AFM_CTRL);

	/* Switch off to configure. */
	isp_param_clear_bits(params_vdev, CIF_ISP_AFM_CTRL, CIF_ISP_AFM_ENA);

	for (i = 0; i < num_of_win; i++) {
		rkisp1_iowrite32(params_vdev,
				 CIF_ISP_AFM_WINDOW_X(arg->afm_win[i].h_offs) |
				 CIF_ISP_AFM_WINDOW_Y(arg->afm_win[i].v_offs),
				 CIF_ISP_AFM_LT_A + i * 8);
		rkisp1_iowrite32(params_vdev,
				 CIF_ISP_AFM_WINDOW_X(arg->afm_win[i].h_size +
						      arg->afm_win[i].h_offs) |
				 CIF_ISP_AFM_WINDOW_Y(arg->afm_win[i].v_size +
						      arg->afm_win[i].v_offs),
				 CIF_ISP_AFM_RB_A + i * 8);
	}
	rkisp1_iowrite32(params_vdev, arg->thres, CIF_ISP_AFM_THRES);

	lum_var_shift = CIF_ISP_AFM_GET_LUM_SHIFT_a_V12(arg->var_shift);
	afm_var_shift = CIF_ISP_AFM_GET_AFM_SHIFT_a_V12(arg->var_shift);
	rkisp1_iowrite32(params_vdev,
		CIF_ISP_AFM_SET_SHIFT_a_V12(lum_var_shift, afm_var_shift) |
		CIF_ISP_AFM_SET_SHIFT_b_V12(lum_var_shift, afm_var_shift) |
		CIF_ISP_AFM_SET_SHIFT_c_V12(lum_var_shift, afm_var_shift),
		CIF_ISP_AFM_VAR_SHIFT);

	/* restore afm status */
	rkisp1_iowrite32(params_vdev, afm_ctrl, CIF_ISP_AFM_CTRL);
}

static void isp_ie_config(struct rkisp1_isp_params_vdev *params_vdev,
			  const struct cifisp_ie_config *arg)
{
	u32 eff_ctrl;

	eff_ctrl = rkisp1_ioread32(params_vdev, CIF_IMG_EFF_CTRL);
	eff_ctrl &= ~CIF_IMG_EFF_CTRL_MODE_MASK;

	if (params_vdev->quantization == V4L2_QUANTIZATION_FULL_RANGE)
		eff_ctrl |= CIF_IMG_EFF_CTRL_YCBCR_FULL;

	switch (arg->effect) {
	case V4L2_COLORFX_SEPIA:
		eff_ctrl |= CIF_IMG_EFF_CTRL_MODE_SEPIA;
		break;
	case V4L2_COLORFX_SET_CBCR:
		rkisp1_iowrite32(params_vdev, arg->eff_tint, CIF_IMG_EFF_TINT);
		eff_ctrl |= CIF_IMG_EFF_CTRL_MODE_SEPIA;
		break;
		/*
		 * Color selection is similar to water color(AQUA):
		 * grayscale + selected color w threshold
		 */
	case V4L2_COLORFX_AQUA:
		eff_ctrl |= CIF_IMG_EFF_CTRL_MODE_COLOR_SEL;
		rkisp1_iowrite32(params_vdev, arg->color_sel,
				 CIF_IMG_EFF_COLOR_SEL);
		break;
	case V4L2_COLORFX_EMBOSS:
		eff_ctrl |= CIF_IMG_EFF_CTRL_MODE_EMBOSS;
		rkisp1_iowrite32(params_vdev, arg->eff_mat_1,
				 CIF_IMG_EFF_MAT_1);
		rkisp1_iowrite32(params_vdev, arg->eff_mat_2,
				 CIF_IMG_EFF_MAT_2);
		rkisp1_iowrite32(params_vdev, arg->eff_mat_3,
				 CIF_IMG_EFF_MAT_3);
		break;
	case V4L2_COLORFX_SKETCH:
		eff_ctrl |= CIF_IMG_EFF_CTRL_MODE_SKETCH;
		rkisp1_iowrite32(params_vdev, arg->eff_mat_3,
				 CIF_IMG_EFF_MAT_3);
		rkisp1_iowrite32(params_vdev, arg->eff_mat_4,
				 CIF_IMG_EFF_MAT_4);
		rkisp1_iowrite32(params_vdev, arg->eff_mat_5,
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

	rkisp1_iowrite32(params_vdev, eff_ctrl, CIF_IMG_EFF_CTRL);
}

static void isp_ie_enable(struct rkisp1_isp_params_vdev *params_vdev, bool en)
{
	if (en) {
		isp_param_set_bits(params_vdev, CIF_ICCL, CIF_ICCL_IE_CLK);
		rkisp1_iowrite32(params_vdev, CIF_IMG_EFF_CTRL_ENABLE,
				 CIF_IMG_EFF_CTRL);
		isp_param_set_bits(params_vdev, CIF_IMG_EFF_CTRL,
				   CIF_IMG_EFF_CTRL_CFG_UPD);
	} else {
		isp_param_clear_bits(params_vdev, CIF_IMG_EFF_CTRL,
				     CIF_IMG_EFF_CTRL_ENABLE);
		isp_param_clear_bits(params_vdev, CIF_ICCL, CIF_ICCL_IE_CLK);
	}
}

static void isp_csm_config(struct rkisp1_isp_params_vdev *params_vdev,
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
			rkisp1_iowrite32(params_vdev, full_range_coeff[i],
					 CIF_ISP_CC_COEFF_0 + i * 4);

		isp_param_set_bits(params_vdev, CIF_ISP_CTRL,
				   CIF_ISP_CTRL_ISP_CSM_Y_FULL_ENA |
				   CIF_ISP_CTRL_ISP_CSM_C_FULL_ENA);
	} else {
		for (i = 0; i < ARRAY_SIZE(limited_range_coeff); i++)
			rkisp1_iowrite32(params_vdev, limited_range_coeff[i],
					 CIF_ISP_CC_COEFF_0 + i * 4);

		isp_param_clear_bits(params_vdev, CIF_ISP_CTRL,
				     CIF_ISP_CTRL_ISP_CSM_Y_FULL_ENA |
				     CIF_ISP_CTRL_ISP_CSM_C_FULL_ENA);
	}
}

/* ISP De-noise Pre-Filter(DPF) function */
static void isp_dpf_config(struct rkisp1_isp_params_vdev *params_vdev,
			   const struct cifisp_dpf_config *arg)
{
	unsigned int isp_dpf_mode;
	unsigned int spatial_coeff;
	unsigned int i;

	switch (arg->gain.mode) {
	case CIFISP_DPF_GAIN_USAGE_NF_GAINS:
		isp_dpf_mode = CIF_ISP_DPF_MODE_USE_NF_GAIN |
				CIF_ISP_DPF_MODE_AWB_GAIN_COMP;
		break;
	case CIFISP_DPF_GAIN_USAGE_LSC_GAINS:
		isp_dpf_mode = CIF_ISP_DPF_MODE_LSC_GAIN_COMP;
		break;
	case CIFISP_DPF_GAIN_USAGE_NF_LSC_GAINS:
		isp_dpf_mode = CIF_ISP_DPF_MODE_USE_NF_GAIN |
				CIF_ISP_DPF_MODE_AWB_GAIN_COMP |
				CIF_ISP_DPF_MODE_LSC_GAIN_COMP;
		break;
	case CIFISP_DPF_GAIN_USAGE_AWB_GAINS:
		isp_dpf_mode = CIF_ISP_DPF_MODE_AWB_GAIN_COMP;
		break;
	case CIFISP_DPF_GAIN_USAGE_AWB_LSC_GAINS:
		isp_dpf_mode = CIF_ISP_DPF_MODE_LSC_GAIN_COMP |
				CIF_ISP_DPF_MODE_AWB_GAIN_COMP;
		break;
	case CIFISP_DPF_GAIN_USAGE_DISABLED:
	default:
		isp_dpf_mode = 0;
		break;
	}

	if (arg->nll.scale_mode == CIFISP_NLL_SCALE_LOGARITHMIC)
		isp_dpf_mode |= CIF_ISP_DPF_MODE_NLL_SEGMENTATION;
	if (arg->rb_flt.fltsize == CIFISP_DPF_RB_FILTERSIZE_9x9)
		isp_dpf_mode |= CIF_ISP_DPF_MODE_RB_FLTSIZE_9x9;
	if (!arg->rb_flt.r_enable)
		isp_dpf_mode |= CIF_ISP_DPF_MODE_R_FLT_DIS;
	if (!arg->rb_flt.b_enable)
		isp_dpf_mode |= CIF_ISP_DPF_MODE_B_FLT_DIS;
	if (!arg->g_flt.gb_enable)
		isp_dpf_mode |= CIF_ISP_DPF_MODE_GB_FLT_DIS;
	if (!arg->g_flt.gr_enable)
		isp_dpf_mode |= CIF_ISP_DPF_MODE_GR_FLT_DIS;

	isp_param_set_bits(params_vdev, CIF_ISP_DPF_MODE, isp_dpf_mode);
	rkisp1_iowrite32(params_vdev, arg->gain.nf_b_gain,
			 CIF_ISP_DPF_NF_GAIN_B);
	rkisp1_iowrite32(params_vdev, arg->gain.nf_r_gain,
			 CIF_ISP_DPF_NF_GAIN_R);
	rkisp1_iowrite32(params_vdev, arg->gain.nf_gb_gain,
			 CIF_ISP_DPF_NF_GAIN_GB);
	rkisp1_iowrite32(params_vdev, arg->gain.nf_gr_gain,
			 CIF_ISP_DPF_NF_GAIN_GR);

	for (i = 0; i < CIFISP_DPF_MAX_NLF_COEFFS; i++) {
		rkisp1_iowrite32(params_vdev, arg->nll.coeff[i],
				 CIF_ISP_DPF_NULL_COEFF_0 + i * 4);
	}

	spatial_coeff = arg->g_flt.spatial_coeff[0] |
			(arg->g_flt.spatial_coeff[1] << 8) |
			(arg->g_flt.spatial_coeff[2] << 16) |
			(arg->g_flt.spatial_coeff[3] << 24);
	rkisp1_iowrite32(params_vdev, spatial_coeff,
			 CIF_ISP_DPF_S_WEIGHT_G_1_4);

	spatial_coeff = arg->g_flt.spatial_coeff[4] |
			(arg->g_flt.spatial_coeff[5] << 8);
	rkisp1_iowrite32(params_vdev, spatial_coeff,
			 CIF_ISP_DPF_S_WEIGHT_G_5_6);

	spatial_coeff = arg->rb_flt.spatial_coeff[0] |
			(arg->rb_flt.spatial_coeff[1] << 8) |
			(arg->rb_flt.spatial_coeff[2] << 16) |
			(arg->rb_flt.spatial_coeff[3] << 24);
	rkisp1_iowrite32(params_vdev, spatial_coeff,
			 CIF_ISP_DPF_S_WEIGHT_RB_1_4);

	spatial_coeff = arg->rb_flt.spatial_coeff[4] |
			(arg->rb_flt.spatial_coeff[5] << 8);
	rkisp1_iowrite32(params_vdev, spatial_coeff,
			CIF_ISP_DPF_S_WEIGHT_RB_5_6);
}

static void isp_dpf_strength_config(struct rkisp1_isp_params_vdev *params_vdev,
				    const struct cifisp_dpf_strength_config *arg)
{
	rkisp1_iowrite32(params_vdev, arg->b, CIF_ISP_DPF_STRENGTH_B);
	rkisp1_iowrite32(params_vdev, arg->g, CIF_ISP_DPF_STRENGTH_G);
	rkisp1_iowrite32(params_vdev, arg->r, CIF_ISP_DPF_STRENGTH_R);
}

static struct rkisp1_isp_params_ops rkisp1_v10_isp_params_ops = {
	.dpcc_config = isp_dpcc_config,
	.bls_config = isp_bls_config,
	.lsc_config = isp_lsc_config,
	.lsc_matrix_config = isp_lsc_matrix_config_v10,
	.flt_config = isp_flt_config,
	.bdm_config = isp_bdm_config,
	.sdg_config = isp_sdg_config,
	.goc_config = isp_goc_config_v10,
	.ctk_config = isp_ctk_config,
	.ctk_enable = isp_ctk_enable,
	.awb_meas_config = isp_awb_meas_config_v10,
	.awb_meas_enable = isp_awb_meas_enable_v10,
	.awb_gain_config = isp_awb_gain_config_v10,
	.aec_config = isp_aec_config_v10,
	.cproc_config = isp_cproc_config,
	.hst_config = isp_hst_config_v10,
	.hst_enable = isp_hst_enable_v10,
	.afm_config = isp_afm_config_v10,
	.ie_config = isp_ie_config,
	.ie_enable = isp_ie_enable,
	.csm_config = isp_csm_config,
	.dpf_config = isp_dpf_config,
	.dpf_strength_config = isp_dpf_strength_config,
};

static struct rkisp1_isp_params_ops rkisp1_v12_isp_params_ops = {
	.dpcc_config = isp_dpcc_config,
	.bls_config = isp_bls_config,
	.lsc_config = isp_lsc_config,
	.lsc_matrix_config = isp_lsc_matrix_config_v12,
	.flt_config = isp_flt_config,
	.bdm_config = isp_bdm_config,
	.sdg_config = isp_sdg_config,
	.goc_config = isp_goc_config_v12,
	.ctk_config = isp_ctk_config,
	.ctk_enable = isp_ctk_enable,
	.awb_meas_config = isp_awb_meas_config_v12,
	.awb_meas_enable = isp_awb_meas_enable_v12,
	.awb_gain_config = isp_awb_gain_config_v12,
	.aec_config = isp_aec_config_v12,
	.cproc_config = isp_cproc_config,
	.hst_config = isp_hst_config_v12,
	.hst_enable = isp_hst_enable_v12,
	.afm_config = isp_afm_config_v12,
	.ie_config = isp_ie_config,
	.ie_enable = isp_ie_enable,
	.csm_config = isp_csm_config,
	.dpf_config = isp_dpf_config,
	.dpf_strength_config = isp_dpf_strength_config,
};

static struct rkisp1_isp_params_config rkisp1_v10_isp_params_config = {
	.gamma_out_max_samples = 17,
	.hst_weight_grids_size = 28,
};

static struct rkisp1_isp_params_config rkisp1_v12_isp_params_config = {
	.gamma_out_max_samples = 34,
	.hst_weight_grids_size = 81,
};

static __maybe_unused
void __isp_isr_other_config(struct rkisp1_isp_params_vdev *params_vdev,
			    const struct rkisp1_isp_params_cfg *new_params)
{
	unsigned int module_en_update, module_cfg_update, module_ens;
	struct rkisp1_isp_params_ops *ops = params_vdev->ops;
	struct ispsd_in_fmt *in_fmt = &params_vdev->dev->isp_sdev.in_fmt;
	bool is_grey_sensor;

	is_grey_sensor = in_fmt->mbus_code == MEDIA_BUS_FMT_Y8_1X8 ||
			 in_fmt->mbus_code == MEDIA_BUS_FMT_Y10_1X10 ||
			 in_fmt->mbus_code == MEDIA_BUS_FMT_Y12_1X12;

	module_en_update = new_params->module_en_update;
	module_cfg_update = new_params->module_cfg_update;
	module_ens = new_params->module_ens;

	if ((module_en_update & CIFISP_MODULE_DPCC) ||
	    (module_cfg_update & CIFISP_MODULE_DPCC)) {
		/*update dpc config */
		if ((module_cfg_update & CIFISP_MODULE_DPCC))
			ops->dpcc_config(params_vdev,
					 &new_params->others.dpcc_config);

		if (module_en_update & CIFISP_MODULE_DPCC) {
			if (!!(module_ens & CIFISP_MODULE_DPCC))
				isp_param_set_bits(params_vdev,
						   CIF_ISP_DPCC_MODE,
						   CIF_ISP_DPCC_ENA);
			else
				isp_param_clear_bits(params_vdev,
						     CIF_ISP_DPCC_MODE,
						     CIF_ISP_DPCC_ENA);
		}
	}

	if ((module_en_update & CIFISP_MODULE_BLS) ||
	    (module_cfg_update & CIFISP_MODULE_BLS)) {
		/* update bls config */
		if ((module_cfg_update & CIFISP_MODULE_BLS))
			ops->bls_config(params_vdev, &new_params->others.bls_config);

		if (module_en_update & CIFISP_MODULE_BLS) {
			if (!!(module_ens & CIFISP_MODULE_BLS))
				isp_param_set_bits(params_vdev,
						   CIF_ISP_BLS_CTRL,
						   CIF_ISP_BLS_ENA);
			else
				isp_param_clear_bits(params_vdev,
						     CIF_ISP_BLS_CTRL,
						     CIF_ISP_BLS_ENA);
		}
	}

	if ((module_en_update & CIFISP_MODULE_SDG) ||
	    (module_cfg_update & CIFISP_MODULE_SDG)) {
		/* update sdg config */
		if ((module_cfg_update & CIFISP_MODULE_SDG))
			ops->sdg_config(params_vdev, &new_params->others.sdg_config);

		if (module_en_update & CIFISP_MODULE_SDG) {
			if (!!(module_ens & CIFISP_MODULE_SDG))
				isp_param_set_bits(params_vdev,
						CIF_ISP_CTRL,
						CIF_ISP_CTRL_ISP_GAMMA_IN_ENA);
			else
				isp_param_clear_bits(params_vdev,
						CIF_ISP_CTRL,
						CIF_ISP_CTRL_ISP_GAMMA_IN_ENA);
		}
	}

	if ((module_en_update & CIFISP_MODULE_LSC) ||
	    (module_cfg_update & CIFISP_MODULE_LSC)) {
		/* update lsc config */
		if ((module_cfg_update & CIFISP_MODULE_LSC))
			ops->lsc_config(params_vdev, &new_params->others.lsc_config);

		if (module_en_update & CIFISP_MODULE_LSC) {
			if (!!(module_ens & CIFISP_MODULE_LSC))
				isp_param_set_bits(params_vdev,
						   CIF_ISP_LSC_CTRL,
						   CIF_ISP_LSC_CTRL_ENA);
			else
				isp_param_clear_bits(params_vdev,
						     CIF_ISP_LSC_CTRL,
						     CIF_ISP_LSC_CTRL_ENA);
		}
	}

	if ((module_en_update & CIFISP_MODULE_AWB_GAIN) ||
	    (module_cfg_update & CIFISP_MODULE_AWB_GAIN)) {
		/* update awb gains */
		if ((module_cfg_update & CIFISP_MODULE_AWB_GAIN))
			ops->awb_gain_config(params_vdev,
					     &new_params->others.awb_gain_config);

		if (module_en_update & CIFISP_MODULE_AWB_GAIN) {
			if (!!(module_ens & CIFISP_MODULE_AWB_GAIN))
				isp_param_set_bits(params_vdev,
						   CIF_ISP_CTRL,
						   CIF_ISP_CTRL_ISP_AWB_ENA);
			else
				isp_param_clear_bits(params_vdev,
						     CIF_ISP_CTRL,
						     CIF_ISP_CTRL_ISP_AWB_ENA);
		}
	}

	if (((module_en_update & CIFISP_MODULE_BDM) ||
	    (module_cfg_update & CIFISP_MODULE_BDM)) &&
	    !is_grey_sensor) {
		/* update bdm config */
		if ((module_cfg_update & CIFISP_MODULE_BDM))
			ops->bdm_config(params_vdev, &new_params->others.bdm_config);

		if (module_en_update & CIFISP_MODULE_BDM) {
			if (!!(module_ens & CIFISP_MODULE_BDM))
				isp_param_set_bits(params_vdev,
						   CIF_ISP_DEMOSAIC,
						   CIF_ISP_DEMOSAIC_BYPASS);
			else
				isp_param_clear_bits(params_vdev,
						     CIF_ISP_DEMOSAIC,
						     CIF_ISP_DEMOSAIC_BYPASS);
		}
	}

	if ((module_en_update & CIFISP_MODULE_FLT) ||
	    (module_cfg_update & CIFISP_MODULE_FLT)) {
		/* update filter config */
		if ((module_cfg_update & CIFISP_MODULE_FLT))
			ops->flt_config(params_vdev, &new_params->others.flt_config);

		if (module_en_update & CIFISP_MODULE_FLT) {
			if (!!(module_ens & CIFISP_MODULE_FLT))
				isp_param_set_bits(params_vdev,
						   CIF_ISP_FILT_MODE,
						   CIF_ISP_FLT_ENA);
			else
				isp_param_clear_bits(params_vdev,
						     CIF_ISP_FILT_MODE,
						     CIF_ISP_FLT_ENA);
		}
	}

	if ((module_en_update & CIFISP_MODULE_CTK) ||
	    (module_cfg_update & CIFISP_MODULE_CTK)) {
		/* update ctk config */
		if ((module_cfg_update & CIFISP_MODULE_CTK))
			ops->ctk_config(params_vdev, &new_params->others.ctk_config);

		if (module_en_update & CIFISP_MODULE_CTK)
			ops->ctk_enable(params_vdev,
					!!(module_ens & CIFISP_MODULE_CTK));
	}

	if ((module_en_update & CIFISP_MODULE_GOC) ||
	    (module_cfg_update & CIFISP_MODULE_GOC)) {
		/* update goc config */
		if ((module_cfg_update & CIFISP_MODULE_GOC))
			ops->goc_config(params_vdev, &new_params->others.goc_config);

		if (module_en_update & CIFISP_MODULE_GOC) {
			if (!!(module_ens & CIFISP_MODULE_GOC))
				isp_param_set_bits(params_vdev,
						CIF_ISP_CTRL,
						CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA);
			else
				isp_param_clear_bits(params_vdev,
						CIF_ISP_CTRL,
						CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA);
		}
	}

	if ((module_en_update & CIFISP_MODULE_CPROC) ||
	    (module_cfg_update & CIFISP_MODULE_CPROC)) {
		/* update cproc config */
		if ((module_cfg_update & CIFISP_MODULE_CPROC)) {
			ops->cproc_config(params_vdev,
					  &new_params->others.cproc_config);

		}

		if (module_en_update & CIFISP_MODULE_CPROC) {
			if (!!(module_ens & CIFISP_MODULE_CPROC))
				isp_param_set_bits(params_vdev,
						   CIF_C_PROC_CTRL,
						   CIF_C_PROC_CTR_ENABLE);
			else
				isp_param_clear_bits(params_vdev,
						   CIF_C_PROC_CTRL,
						   CIF_C_PROC_CTR_ENABLE);
		}
	}


	if ((module_en_update & CIFISP_MODULE_IE) ||
	    (module_cfg_update & CIFISP_MODULE_IE)) {
		/* update ie config */
		if ((module_cfg_update & CIFISP_MODULE_IE))
			ops->ie_config(params_vdev, &new_params->others.ie_config);

		if (module_en_update & CIFISP_MODULE_IE)
			ops->ie_enable(params_vdev,
				       !!(module_ens & CIFISP_MODULE_IE));
	}

	if ((module_en_update & CIFISP_MODULE_DPF) ||
	    (module_cfg_update & CIFISP_MODULE_DPF)) {
		/* update dpf  config */
		if ((module_cfg_update & CIFISP_MODULE_DPF))
			ops->dpf_config(params_vdev, &new_params->others.dpf_config);

		if (module_en_update & CIFISP_MODULE_DPF) {
			if (!!(module_ens & CIFISP_MODULE_DPF))
				isp_param_set_bits(params_vdev,
						   CIF_ISP_DPF_MODE,
						   CIF_ISP_DPF_MODE_EN);
			else
				isp_param_clear_bits(params_vdev,
						     CIF_ISP_DPF_MODE,
						     CIF_ISP_DPF_MODE_EN);
		}
	}

	if ((module_en_update & CIFISP_MODULE_DPF_STRENGTH) ||
	    (module_cfg_update & CIFISP_MODULE_DPF_STRENGTH)) {
		/* update dpf strength config */
		ops->dpf_strength_config(params_vdev,
					 &new_params->others.dpf_strength_config);
	}
}

static __maybe_unused
void __isp_isr_meas_config(struct rkisp1_isp_params_vdev *params_vdev,
			   struct  rkisp1_isp_params_cfg *new_params)
{
	unsigned int module_en_update, module_cfg_update, module_ens;
	struct rkisp1_isp_params_ops *ops = params_vdev->ops;

	module_en_update = new_params->module_en_update;
	module_cfg_update = new_params->module_cfg_update;
	module_ens = new_params->module_ens;

	if ((module_en_update & CIFISP_MODULE_AWB) ||
	    (module_cfg_update & CIFISP_MODULE_AWB)) {
		/* update awb config */
		if ((module_cfg_update & CIFISP_MODULE_AWB))
			ops->awb_meas_config(params_vdev,
					     &new_params->meas.awb_meas_config);

		if (module_en_update & CIFISP_MODULE_AWB)
			ops->awb_meas_enable(params_vdev,
					     &new_params->meas.awb_meas_config,
					     !!(module_ens & CIFISP_MODULE_AWB));
	}

	if ((module_en_update & CIFISP_MODULE_AFC) ||
	    (module_cfg_update & CIFISP_MODULE_AFC)) {
		/* update afc config */
		if ((module_cfg_update & CIFISP_MODULE_AFC))
			ops->afm_config(params_vdev, &new_params->meas.afc_config);

		if (module_en_update & CIFISP_MODULE_AFC) {
			if (!!(module_ens & CIFISP_MODULE_AFC))
				isp_param_set_bits(params_vdev,
						   CIF_ISP_AFM_CTRL,
						   CIF_ISP_AFM_ENA);
			else
				isp_param_clear_bits(params_vdev,
						     CIF_ISP_AFM_CTRL,
						     CIF_ISP_AFM_ENA);
		}
	}

	if ((module_en_update & CIFISP_MODULE_HST) ||
	    (module_cfg_update & CIFISP_MODULE_HST)) {
		/* update hst config */
		if ((module_cfg_update & CIFISP_MODULE_HST))
			ops->hst_config(params_vdev, &new_params->meas.hst_config);

		if (module_en_update & CIFISP_MODULE_HST)
			ops->hst_enable(params_vdev,
					&new_params->meas.hst_config,
					!!(module_ens & CIFISP_MODULE_HST));
	}

	if ((module_en_update & CIFISP_MODULE_AEC) ||
	    (module_cfg_update & CIFISP_MODULE_AEC)) {
		/* update aec config */
		if ((module_cfg_update & CIFISP_MODULE_AEC))
			ops->aec_config(params_vdev, &new_params->meas.aec_config);

		if (module_en_update & CIFISP_MODULE_AEC) {
			if (!!(module_ens & CIFISP_MODULE_AEC))
				isp_param_set_bits(params_vdev,
						   CIF_ISP_EXP_CTRL,
						   CIF_ISP_EXP_ENA);
			else
				isp_param_clear_bits(params_vdev,
						     CIF_ISP_EXP_CTRL,
						     CIF_ISP_EXP_ENA);
		}
	}
}

static __maybe_unused
void __preisp_isr_update_hdrae_para(struct rkisp1_isp_params_vdev *params_vdev,
				    struct rkisp1_isp_params_cfg *new_params)
{
	struct preisp_hdrae_para_s *hdrae;
	struct cifisp_lsc_config *lsc;
	struct cifisp_awb_gain_config *awb_gain;
	unsigned int module_en_update, module_cfg_update, module_ens;
	int i, ret;

	hdrae = &params_vdev->hdrae_para;
	module_en_update = new_params->module_en_update;
	module_cfg_update = new_params->module_cfg_update;
	module_ens = new_params->module_ens;
	lsc = &new_params->others.lsc_config;
	awb_gain = &new_params->others.awb_gain_config;

	if (!params_vdev->dev->hdr_sensor)
		return;

	if ((module_en_update & CIFISP_MODULE_AWB_GAIN) ||
	    (module_cfg_update & CIFISP_MODULE_AWB_GAIN)) {
		/* update awb gains */
		if ((module_cfg_update & CIFISP_MODULE_AWB_GAIN)) {
			hdrae->r_gain = awb_gain->gain_red;
			hdrae->b_gain = awb_gain->gain_blue;
			hdrae->gr_gain = awb_gain->gain_green_r;
			hdrae->gb_gain = awb_gain->gain_green_b;
		}

		if (module_en_update & CIFISP_MODULE_AWB_GAIN) {
			if (!(module_ens & CIFISP_MODULE_AWB_GAIN)) {
				hdrae->r_gain = 0x0100;
				hdrae->b_gain = 0x0100;
				hdrae->gr_gain = 0x0100;
				hdrae->gb_gain = 0x0100;
			}
		}
	}

	if ((module_en_update & CIFISP_MODULE_LSC) ||
	    (module_cfg_update & CIFISP_MODULE_LSC)) {
		/* update lsc config */
		if ((module_cfg_update & CIFISP_MODULE_LSC))
			memcpy(hdrae->lsc_table, lsc->gr_data_tbl,
				PREISP_LSCTBL_SIZE);

		if (module_en_update & CIFISP_MODULE_LSC) {
			if (!(module_ens & CIFISP_MODULE_LSC))
				for (i = 0; i < PREISP_LSCTBL_SIZE; i++)
					hdrae->lsc_table[i] = 0x0400;
		}
	}

	ret = v4l2_subdev_call(params_vdev->dev->hdr_sensor, core, ioctl,
			       PREISP_CMD_SAVE_HDRAE_PARAM, hdrae);
	if (ret)
		params_vdev->dev->hdr_sensor = NULL;
}

void rkisp1_params_isr(struct rkisp1_isp_params_vdev *params_vdev, u32 isp_mis)
{
	struct rkisp1_isp_params_cfg *new_params;
	struct rkisp1_buffer *cur_buf = NULL;
	unsigned int cur_frame_id = -1;
	cur_frame_id = atomic_read(&params_vdev->dev->isp_sdev.frm_sync_seq) - 1;

	spin_lock(&params_vdev->config_lock);
	if (!params_vdev->streamon) {
		spin_unlock(&params_vdev->config_lock);
		return;
	}

	/* get one empty buffer */
	if (!list_empty(&params_vdev->params))
		cur_buf = list_first_entry(&params_vdev->params,
					   struct rkisp1_buffer, queue);
	if (!cur_buf) {
		spin_unlock(&params_vdev->config_lock);
		return;
	}

	new_params = (struct rkisp1_isp_params_cfg *)(cur_buf->vaddr[0]);

	if (isp_mis & CIF_ISP_FRAME) {
		u32 isp_ctrl;

		list_del(&cur_buf->queue);

		__isp_isr_other_config(params_vdev, new_params);
		__isp_isr_meas_config(params_vdev, new_params);

		/* update shadow register immediately */
		isp_ctrl = rkisp1_ioread32(params_vdev, CIF_ISP_CTRL);
		isp_ctrl |= CIF_ISP_CTRL_ISP_CFG_UPD;
		rkisp1_iowrite32(params_vdev, isp_ctrl, CIF_ISP_CTRL);

		__preisp_isr_update_hdrae_para(params_vdev, new_params);

		cur_buf->vb.sequence = cur_frame_id;
		vb2_buffer_done(&cur_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}
	spin_unlock(&params_vdev->config_lock);
}

static const struct cifisp_awb_meas_config awb_params_default_config = {
	{
		0, 0, RKISP1_DEFAULT_WIDTH, RKISP1_DEFAULT_HEIGHT
	},
	CIFISP_AWB_MODE_YCBCR, 200, 30, 20, 20, 0, 128, 128
};

static const struct cifisp_aec_config aec_params_default_config = {
	CIFISP_EXP_MEASURING_MODE_0,
	CIFISP_EXP_CTRL_AUTOSTOP_0,
	{
		RKISP1_DEFAULT_WIDTH >> 2, RKISP1_DEFAULT_HEIGHT >> 2,
		RKISP1_DEFAULT_WIDTH >> 1, RKISP1_DEFAULT_HEIGHT >> 1
	}
};

static const struct cifisp_hst_config hst_params_default_config = {
	CIFISP_HISTOGRAM_MODE_RGB_COMBINED,
	3,
	{
		RKISP1_DEFAULT_WIDTH >> 2, RKISP1_DEFAULT_HEIGHT >> 2,
		RKISP1_DEFAULT_WIDTH >> 1, RKISP1_DEFAULT_HEIGHT >> 1
	},
	{
		0, /* To be filled in with 0x01 at runtime. */
	}
};

static const struct cifisp_afc_config afc_params_default_config = {
	1,
	{
		{
			300, 225, 200, 150
		}
	},
	4,
	14
};

static
void rkisp1_params_config_parameter(struct rkisp1_isp_params_vdev *params_vdev)
{
	struct rkisp1_isp_params_ops *ops = params_vdev->ops;
	struct cifisp_hst_config hst = hst_params_default_config;
	int i;

	spin_lock(&params_vdev->config_lock);

	ops->awb_meas_config(params_vdev, &awb_params_default_config);
	ops->awb_meas_enable(params_vdev, &awb_params_default_config, true);

	ops->aec_config(params_vdev, &aec_params_default_config);
	isp_param_set_bits(params_vdev, CIF_ISP_EXP_CTRL, CIF_ISP_EXP_ENA);

	ops->afm_config(params_vdev, &afc_params_default_config);
	isp_param_set_bits(params_vdev, CIF_ISP_AFM_CTRL, CIF_ISP_AFM_ENA);

	memset(hst.hist_weight, 0x01, sizeof(hst.hist_weight));
	ops->hst_config(params_vdev, &hst);
	if (params_vdev->dev->isp_ver == ISP_V12 ||
	    params_vdev->dev->isp_ver == ISP_V13) {
		isp_param_set_bits(params_vdev, CIF_ISP_HIST_CTRL_V12,
			   ~CIF_ISP_HIST_CTRL_MODE_MASK_V12 |
			   hst_params_default_config.mode);
	} else {
		isp_param_set_bits(params_vdev, CIF_ISP_HIST_PROP_V10,
			   ~CIF_ISP_HIST_PROP_MODE_MASK_V10 |
			   hst_params_default_config.mode);
	}

	/* set the  range */
	if (params_vdev->quantization == V4L2_QUANTIZATION_FULL_RANGE)
		ops->csm_config(params_vdev, true);
	else
		ops->csm_config(params_vdev, false);

	/* disable color related config for grey sensor */
	if (params_vdev->in_mbus_code == MEDIA_BUS_FMT_Y8_1X8 ||
	    params_vdev->in_mbus_code == MEDIA_BUS_FMT_Y10_1X10 ||
	    params_vdev->in_mbus_code == MEDIA_BUS_FMT_Y12_1X12) {
		ops->ctk_enable(params_vdev, false);
		isp_param_clear_bits(params_vdev,
				     CIF_ISP_CTRL,
				     CIF_ISP_CTRL_ISP_AWB_ENA);
		isp_param_clear_bits(params_vdev,
				     CIF_ISP_LSC_CTRL,
				     CIF_ISP_LSC_CTRL_ENA);
	}

	params_vdev->hdrae_para.r_gain = 0x0100;
	params_vdev->hdrae_para.b_gain = 0x0100;
	params_vdev->hdrae_para.gr_gain = 0x0100;
	params_vdev->hdrae_para.gb_gain = 0x0100;
	for (i = 0; i < PREISP_LSCTBL_SIZE; i++)
		params_vdev->hdrae_para.lsc_table[i] = 0x0400;

	/* override the default things */
	__isp_isr_other_config(params_vdev, &params_vdev->cur_params);
	__isp_isr_meas_config(params_vdev, &params_vdev->cur_params);
	__preisp_isr_update_hdrae_para(params_vdev, &params_vdev->cur_params);

	spin_unlock(&params_vdev->config_lock);
}

/* Not called when the camera active, thus not isr protection. */
void rkisp1_params_configure_isp(struct rkisp1_isp_params_vdev *params_vdev,
			  struct ispsd_in_fmt *in_fmt,
			  enum v4l2_quantization quantization)
{
	params_vdev->quantization = quantization;
	params_vdev->raw_type = in_fmt->bayer_pat;
	params_vdev->in_mbus_code = in_fmt->mbus_code;
	rkisp1_params_config_parameter(params_vdev);
}

/* Not called when the camera active, thus not isr protection. */
void rkisp1_params_disable_isp(struct rkisp1_isp_params_vdev *params_vdev)
{
	struct rkisp1_isp_params_ops *ops = params_vdev->ops;

	isp_param_clear_bits(params_vdev, CIF_ISP_DPCC_MODE, CIF_ISP_DPCC_ENA);
	isp_param_clear_bits(params_vdev, CIF_ISP_LSC_CTRL,
			     CIF_ISP_LSC_CTRL_ENA);
	isp_param_clear_bits(params_vdev, CIF_ISP_BLS_CTRL, CIF_ISP_BLS_ENA);
	isp_param_clear_bits(params_vdev, CIF_ISP_CTRL,
			     CIF_ISP_CTRL_ISP_GAMMA_IN_ENA);
	isp_param_clear_bits(params_vdev, CIF_ISP_CTRL,
			     CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA);
	isp_param_clear_bits(params_vdev, CIF_ISP_DEMOSAIC,
			     CIF_ISP_DEMOSAIC_BYPASS);
	isp_param_clear_bits(params_vdev, CIF_ISP_FILT_MODE, CIF_ISP_FLT_ENA);
	ops->awb_meas_enable(params_vdev, NULL, false);
	isp_param_clear_bits(params_vdev, CIF_ISP_CTRL,
			     CIF_ISP_CTRL_ISP_AWB_ENA);
	isp_param_clear_bits(params_vdev, CIF_ISP_EXP_CTRL, CIF_ISP_EXP_ENA);
	ops->ctk_enable(params_vdev, false);
	isp_param_clear_bits(params_vdev, CIF_C_PROC_CTRL,
			     CIF_C_PROC_CTR_ENABLE);
	ops->hst_enable(params_vdev, NULL, false);
	isp_param_clear_bits(params_vdev, CIF_ISP_AFM_CTRL, CIF_ISP_AFM_ENA);
	ops->ie_enable(params_vdev, false);
	isp_param_clear_bits(params_vdev, CIF_ISP_DPF_MODE,
			     CIF_ISP_DPF_MODE_EN);
}

static int rkisp1_params_enum_fmt_meta_out(struct file *file, void *priv,
					   struct v4l2_fmtdesc *f)
{
	struct video_device *video = video_devdata(file);
	struct rkisp1_isp_params_vdev *params_vdev = video_get_drvdata(video);

	if (f->index > 0 || f->type != video->queue->type)
		return -EINVAL;

	f->pixelformat = params_vdev->vdev_fmt.fmt.meta.dataformat;

	return 0;
}

static int rkisp1_params_g_fmt_meta_out(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct video_device *video = video_devdata(file);
	struct rkisp1_isp_params_vdev *params_vdev = video_get_drvdata(video);
	struct v4l2_meta_format *meta = &f->fmt.meta;

	if (f->type != video->queue->type)
		return -EINVAL;

	memset(meta, 0, sizeof(*meta));
	meta->dataformat = params_vdev->vdev_fmt.fmt.meta.dataformat;
	meta->buffersize = params_vdev->vdev_fmt.fmt.meta.buffersize;

	return 0;
}

static int rkisp1_params_querycap(struct file *file,
				  void *priv, struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);
	struct rkisp1_isp_params_vdev *params_vdev = video_get_drvdata(vdev);

	snprintf(cap->driver, sizeof(cap->driver),
		 "%s_v%02d", DRIVER_NAME, params_vdev->dev->isp_ver);
	strlcpy(cap->card, vdev->name, sizeof(cap->card));
	strlcpy(cap->bus_info, "platform: " DRIVER_NAME, sizeof(cap->bus_info));

	return 0;
}

/* ISP params video device IOCTLs */
static const struct v4l2_ioctl_ops rkisp1_params_ioctl = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_enum_fmt_meta_out = rkisp1_params_enum_fmt_meta_out,
	.vidioc_g_fmt_meta_out = rkisp1_params_g_fmt_meta_out,
	.vidioc_s_fmt_meta_out = rkisp1_params_g_fmt_meta_out,
	.vidioc_try_fmt_meta_out = rkisp1_params_g_fmt_meta_out,
	.vidioc_querycap = rkisp1_params_querycap
};

static int rkisp1_params_vb2_queue_setup(struct vb2_queue *vq,
					 const void *parg,
					 unsigned int *num_buffers,
					 unsigned int *num_planes,
					 unsigned int sizes[],
					 void *alloc_ctxs[])
{
	struct rkisp1_isp_params_vdev *params_vdev = vq->drv_priv;

	*num_buffers = clamp_t(u32, *num_buffers,
			       RKISP1_ISP_PARAMS_REQ_BUFS_MIN,
			       RKISP1_ISP_PARAMS_REQ_BUFS_MAX);

	*num_planes = 1;

	sizes[0] = sizeof(struct rkisp1_isp_params_cfg);

	INIT_LIST_HEAD(&params_vdev->params);
	params_vdev->first_params = true;

	return 0;
}

static void rkisp1_params_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkisp1_buffer *params_buf = to_rkisp1_buffer(vbuf);
	struct vb2_queue *vq = vb->vb2_queue;
	struct rkisp1_isp_params_vdev *params_vdev = vq->drv_priv;
	struct rkisp1_isp_params_cfg *new_params;
	unsigned long flags;

	unsigned int cur_frame_id = -1;
	cur_frame_id = atomic_read(&params_vdev->dev->isp_sdev.frm_sync_seq) - 1;

	if (params_vdev->first_params) {
		new_params = (struct rkisp1_isp_params_cfg *)
			(vb2_plane_vaddr(vb, 0));
		vbuf->sequence = cur_frame_id;
		vb2_buffer_done(&params_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		params_vdev->first_params = false;
		params_vdev->cur_params = *new_params;
		return;
	}

	params_buf->vaddr[0] = vb2_plane_vaddr(vb, 0);
	spin_lock_irqsave(&params_vdev->config_lock, flags);
	list_add_tail(&params_buf->queue, &params_vdev->params);
	spin_unlock_irqrestore(&params_vdev->config_lock, flags);
}

static void rkisp1_params_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct rkisp1_isp_params_vdev *params_vdev = vq->drv_priv;
	struct rkisp1_buffer *buf;
	unsigned long flags;
	int i;

	/* stop params input firstly */
	spin_lock_irqsave(&params_vdev->config_lock, flags);
	params_vdev->streamon = false;
	spin_unlock_irqrestore(&params_vdev->config_lock, flags);

	for (i = 0; i < RKISP1_ISP_PARAMS_REQ_BUFS_MAX; i++) {
		spin_lock_irqsave(&params_vdev->config_lock, flags);
		if (!list_empty(&params_vdev->params)) {
			buf = list_first_entry(&params_vdev->params,
					       struct rkisp1_buffer, queue);
			list_del(&buf->queue);
			spin_unlock_irqrestore(&params_vdev->config_lock,
					       flags);
		} else {
			spin_unlock_irqrestore(&params_vdev->config_lock,
					       flags);
			break;
		}

		if (buf)
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		buf = NULL;
	}

	/* clean module params */
	params_vdev->cur_params.module_cfg_update = 0;
	params_vdev->cur_params.module_en_update = 0;
}

static int
rkisp1_params_vb2_start_streaming(struct vb2_queue *queue, unsigned int count)
{
	struct rkisp1_isp_params_vdev *params_vdev = queue->drv_priv;
	unsigned long flags;

	spin_lock_irqsave(&params_vdev->config_lock, flags);
	params_vdev->streamon = true;
	spin_unlock_irqrestore(&params_vdev->config_lock, flags);

	return 0;
}

static struct vb2_ops rkisp1_params_vb2_ops = {
	.queue_setup = rkisp1_params_vb2_queue_setup,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.buf_queue = rkisp1_params_vb2_buf_queue,
	.start_streaming = rkisp1_params_vb2_start_streaming,
	.stop_streaming = rkisp1_params_vb2_stop_streaming,

};

struct v4l2_file_operations rkisp1_params_fops = {
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.open = v4l2_fh_open,
	.release = vb2_fop_release
};

static int
rkisp1_params_init_vb2_queue(struct vb2_queue *q,
			     struct rkisp1_isp_params_vdev *params_vdev)
{
	struct rkisp1_vdev_node *node;

	node = queue_to_node(q);

	q->type = V4L2_BUF_TYPE_META_OUTPUT;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->drv_priv = params_vdev;
	q->ops = &rkisp1_params_vb2_ops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->buf_struct_size = sizeof(struct rkisp1_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &node->vlock;

	return vb2_queue_init(q);
}

static void rkisp1_init_params_vdev(struct rkisp1_isp_params_vdev *params_vdev)
{
	params_vdev->vdev_fmt.fmt.meta.dataformat =
		V4L2_META_FMT_RK_ISP1_PARAMS;
	params_vdev->vdev_fmt.fmt.meta.buffersize =
		sizeof(struct rkisp1_isp_params_cfg);

	if (params_vdev->dev->isp_ver == ISP_V12 ||
	    params_vdev->dev->isp_ver == ISP_V13) {
		params_vdev->ops = &rkisp1_v12_isp_params_ops;
		params_vdev->config = &rkisp1_v12_isp_params_config;
	} else {
		params_vdev->ops = &rkisp1_v10_isp_params_ops;
		params_vdev->config = &rkisp1_v10_isp_params_config;
	}
}

int rkisp1_register_params_vdev(struct rkisp1_isp_params_vdev *params_vdev,
				struct v4l2_device *v4l2_dev,
				struct rkisp1_device *dev)
{
	int ret;
	struct rkisp1_vdev_node *node = &params_vdev->vnode;
	struct video_device *vdev = &node->vdev;

	params_vdev->dev = dev;
	mutex_init(&node->vlock);
	spin_lock_init(&params_vdev->config_lock);

	strlcpy(vdev->name, "rkisp1-input-params", sizeof(vdev->name));

	video_set_drvdata(vdev, params_vdev);
	vdev->ioctl_ops = &rkisp1_params_ioctl;
	vdev->fops = &rkisp1_params_fops;
	vdev->release = video_device_release_empty;
	/*
	 * Provide a mutex to v4l2 core. It will be used
	 * to protect all fops and v4l2 ioctls.
	 */
	vdev->lock = &node->vlock;
	vdev->v4l2_dev = v4l2_dev;
	vdev->queue = &node->buf_queue;
	vdev->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_META_OUTPUT;
	vdev->vfl_dir = VFL_DIR_TX;
	rkisp1_params_init_vb2_queue(vdev->queue, params_vdev);
	rkisp1_init_params_vdev(params_vdev);
	video_set_drvdata(vdev, params_vdev);

	node->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&vdev->entity, 1, &node->pad, 0);
	if (ret < 0)
		goto err_release_queue;
	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		dev_err(&vdev->dev,
			"could not register Video for Linux device\n");
		goto err_cleanup_media_entity;
	}
	return 0;
err_cleanup_media_entity:
	media_entity_cleanup(&vdev->entity);
err_release_queue:
	vb2_queue_release(vdev->queue);
	return ret;
}

void rkisp1_unregister_params_vdev(struct rkisp1_isp_params_vdev *params_vdev)
{
	struct rkisp1_vdev_node *node = &params_vdev->vnode;
	struct video_device *vdev = &node->vdev;

	video_unregister_device(vdev);
	media_entity_cleanup(&vdev->entity);
	vb2_queue_release(vdev->queue);
}

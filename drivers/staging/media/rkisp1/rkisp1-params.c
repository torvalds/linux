// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip ISP1 Driver - Params subdevice
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>	/* for ISP params */

#include "rkisp1-common.h"

#define RKISP1_PARAMS_DEV_NAME	RKISP1_DRIVER_NAME "_params"

#define RKISP1_ISP_PARAMS_REQ_BUFS_MIN	2
#define RKISP1_ISP_PARAMS_REQ_BUFS_MAX	8

#define RKISP1_ISP_DPCC_LINE_THRESH(n) \
			(RKISP1_CIF_ISP_DPCC_LINE_THRESH_1 + 0x14 * (n))
#define RKISP1_ISP_DPCC_LINE_MAD_FAC(n) \
			(RKISP1_CIF_ISP_DPCC_LINE_MAD_FAC_1 + 0x14 * (n))
#define RKISP1_ISP_DPCC_PG_FAC(n) \
			(RKISP1_CIF_ISP_DPCC_PG_FAC_1 + 0x14 * (n))
#define RKISP1_ISP_DPCC_RND_THRESH(n) \
			(RKISP1_CIF_ISP_DPCC_RND_THRESH_1 + 0x14 * (n))
#define RKISP1_ISP_DPCC_RG_FAC(n) \
			(RKISP1_CIF_ISP_DPCC_RG_FAC_1 + 0x14 * (n))
#define RKISP1_ISP_CC_COEFF(n) \
			(RKISP1_CIF_ISP_CC_COEFF_0 + (n) * 4)

static inline void
rkisp1_param_set_bits(struct rkisp1_params *params, u32 reg, u32 bit_mask)
{
	u32 val;

	val = rkisp1_read(params->rkisp1, reg);
	rkisp1_write(params->rkisp1, val | bit_mask, reg);
}

static inline void
rkisp1_param_clear_bits(struct rkisp1_params *params, u32 reg, u32 bit_mask)
{
	u32 val;

	val = rkisp1_read(params->rkisp1, reg);
	rkisp1_write(params->rkisp1, val & ~bit_mask, reg);
}

/* ISP BP interface function */
static void rkisp1_dpcc_config(struct rkisp1_params *params,
			       const struct rkisp1_cif_isp_dpcc_config *arg)
{
	unsigned int i;
	u32 mode;

	/* avoid to override the old enable value */
	mode = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_DPCC_MODE);
	mode &= RKISP1_CIF_ISP_DPCC_ENA;
	mode |= arg->mode & ~RKISP1_CIF_ISP_DPCC_ENA;
	rkisp1_write(params->rkisp1, mode, RKISP1_CIF_ISP_DPCC_MODE);
	rkisp1_write(params->rkisp1, arg->output_mode,
		     RKISP1_CIF_ISP_DPCC_OUTPUT_MODE);
	rkisp1_write(params->rkisp1, arg->set_use,
		     RKISP1_CIF_ISP_DPCC_SET_USE);

	rkisp1_write(params->rkisp1, arg->methods[0].method,
		     RKISP1_CIF_ISP_DPCC_METHODS_SET_1);
	rkisp1_write(params->rkisp1, arg->methods[1].method,
		     RKISP1_CIF_ISP_DPCC_METHODS_SET_2);
	rkisp1_write(params->rkisp1, arg->methods[2].method,
		     RKISP1_CIF_ISP_DPCC_METHODS_SET_3);
	for (i = 0; i < RKISP1_CIF_ISP_DPCC_METHODS_MAX; i++) {
		rkisp1_write(params->rkisp1, arg->methods[i].line_thresh,
			     RKISP1_ISP_DPCC_LINE_THRESH(i));
		rkisp1_write(params->rkisp1, arg->methods[i].line_mad_fac,
			     RKISP1_ISP_DPCC_LINE_MAD_FAC(i));
		rkisp1_write(params->rkisp1, arg->methods[i].pg_fac,
			     RKISP1_ISP_DPCC_PG_FAC(i));
		rkisp1_write(params->rkisp1, arg->methods[i].rnd_thresh,
			     RKISP1_ISP_DPCC_RND_THRESH(i));
		rkisp1_write(params->rkisp1, arg->methods[i].rg_fac,
			     RKISP1_ISP_DPCC_RG_FAC(i));
	}

	rkisp1_write(params->rkisp1, arg->rnd_offs,
		     RKISP1_CIF_ISP_DPCC_RND_OFFS);
	rkisp1_write(params->rkisp1, arg->ro_limits,
		     RKISP1_CIF_ISP_DPCC_RO_LIMITS);
}

/* ISP black level subtraction interface function */
static void rkisp1_bls_config(struct rkisp1_params *params,
			      const struct rkisp1_cif_isp_bls_config *arg)
{
	/* avoid to override the old enable value */
	u32 new_control;

	new_control = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_BLS_CTRL);
	new_control &= RKISP1_CIF_ISP_BLS_ENA;
	/* fixed subtraction values */
	if (!arg->enable_auto) {
		const struct rkisp1_cif_isp_bls_fixed_val *pval =
								&arg->fixed_val;

		switch (params->raw_type) {
		case RKISP1_RAW_BGGR:
			rkisp1_write(params->rkisp1,
				     pval->r, RKISP1_CIF_ISP_BLS_D_FIXED);
			rkisp1_write(params->rkisp1,
				     pval->gr, RKISP1_CIF_ISP_BLS_C_FIXED);
			rkisp1_write(params->rkisp1,
				     pval->gb, RKISP1_CIF_ISP_BLS_B_FIXED);
			rkisp1_write(params->rkisp1,
				     pval->b, RKISP1_CIF_ISP_BLS_A_FIXED);
			break;
		case RKISP1_RAW_GBRG:
			rkisp1_write(params->rkisp1,
				     pval->r, RKISP1_CIF_ISP_BLS_C_FIXED);
			rkisp1_write(params->rkisp1,
				     pval->gr, RKISP1_CIF_ISP_BLS_D_FIXED);
			rkisp1_write(params->rkisp1,
				     pval->gb, RKISP1_CIF_ISP_BLS_A_FIXED);
			rkisp1_write(params->rkisp1,
				     pval->b, RKISP1_CIF_ISP_BLS_B_FIXED);
			break;
		case RKISP1_RAW_GRBG:
			rkisp1_write(params->rkisp1,
				     pval->r, RKISP1_CIF_ISP_BLS_B_FIXED);
			rkisp1_write(params->rkisp1,
				     pval->gr, RKISP1_CIF_ISP_BLS_A_FIXED);
			rkisp1_write(params->rkisp1,
				     pval->gb, RKISP1_CIF_ISP_BLS_D_FIXED);
			rkisp1_write(params->rkisp1,
				     pval->b, RKISP1_CIF_ISP_BLS_C_FIXED);
			break;
		case RKISP1_RAW_RGGB:
			rkisp1_write(params->rkisp1,
				     pval->r, RKISP1_CIF_ISP_BLS_A_FIXED);
			rkisp1_write(params->rkisp1,
				     pval->gr, RKISP1_CIF_ISP_BLS_B_FIXED);
			rkisp1_write(params->rkisp1,
				     pval->gb, RKISP1_CIF_ISP_BLS_C_FIXED);
			rkisp1_write(params->rkisp1,
				     pval->b, RKISP1_CIF_ISP_BLS_D_FIXED);
			break;
		default:
			break;
		}

	} else {
		if (arg->en_windows & BIT(1)) {
			rkisp1_write(params->rkisp1, arg->bls_window2.h_offs,
				     RKISP1_CIF_ISP_BLS_H2_START);
			rkisp1_write(params->rkisp1, arg->bls_window2.h_size,
				     RKISP1_CIF_ISP_BLS_H2_STOP);
			rkisp1_write(params->rkisp1, arg->bls_window2.v_offs,
				     RKISP1_CIF_ISP_BLS_V2_START);
			rkisp1_write(params->rkisp1, arg->bls_window2.v_size,
				     RKISP1_CIF_ISP_BLS_V2_STOP);
			new_control |= RKISP1_CIF_ISP_BLS_WINDOW_2;
		}

		if (arg->en_windows & BIT(0)) {
			rkisp1_write(params->rkisp1, arg->bls_window1.h_offs,
				     RKISP1_CIF_ISP_BLS_H1_START);
			rkisp1_write(params->rkisp1, arg->bls_window1.h_size,
				     RKISP1_CIF_ISP_BLS_H1_STOP);
			rkisp1_write(params->rkisp1, arg->bls_window1.v_offs,
				     RKISP1_CIF_ISP_BLS_V1_START);
			rkisp1_write(params->rkisp1, arg->bls_window1.v_size,
				     RKISP1_CIF_ISP_BLS_V1_STOP);
			new_control |= RKISP1_CIF_ISP_BLS_WINDOW_1;
		}

		rkisp1_write(params->rkisp1, arg->bls_samples,
			     RKISP1_CIF_ISP_BLS_SAMPLES);

		new_control |= RKISP1_CIF_ISP_BLS_MODE_MEASURED;
	}
	rkisp1_write(params->rkisp1, new_control, RKISP1_CIF_ISP_BLS_CTRL);
}

/* ISP LS correction interface function */
static void
rkisp1_lsc_correct_matrix_config(struct rkisp1_params *params,
				const struct rkisp1_cif_isp_lsc_config *pconfig)
{
	unsigned int isp_lsc_status, sram_addr, isp_lsc_table_sel, i, j, data;

	isp_lsc_status = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_LSC_STATUS);

	/* RKISP1_CIF_ISP_LSC_TABLE_ADDRESS_153 = ( 17 * 18 ) >> 1 */
	sram_addr = (isp_lsc_status & RKISP1_CIF_ISP_LSC_ACTIVE_TABLE) ?
		    RKISP1_CIF_ISP_LSC_TABLE_ADDRESS_0 :
		    RKISP1_CIF_ISP_LSC_TABLE_ADDRESS_153;
	rkisp1_write(params->rkisp1, sram_addr,
		     RKISP1_CIF_ISP_LSC_R_TABLE_ADDR);
	rkisp1_write(params->rkisp1, sram_addr,
		     RKISP1_CIF_ISP_LSC_GR_TABLE_ADDR);
	rkisp1_write(params->rkisp1, sram_addr,
		     RKISP1_CIF_ISP_LSC_GB_TABLE_ADDR);
	rkisp1_write(params->rkisp1, sram_addr,
		     RKISP1_CIF_ISP_LSC_B_TABLE_ADDR);

	/* program data tables (table size is 9 * 17 = 153) */
	for (i = 0;
	     i < RKISP1_CIF_ISP_LSC_SECTORS_MAX * RKISP1_CIF_ISP_LSC_SECTORS_MAX;
	     i += RKISP1_CIF_ISP_LSC_SECTORS_MAX) {
		/*
		 * 17 sectors with 2 values in one DWORD = 9
		 * DWORDs (2nd value of last DWORD unused)
		 */
		for (j = 0; j < RKISP1_CIF_ISP_LSC_SECTORS_MAX - 1; j += 2) {
			data = RKISP1_CIF_ISP_LSC_TABLE_DATA(pconfig->r_data_tbl[i + j],
							     pconfig->r_data_tbl[i + j + 1]);
			rkisp1_write(params->rkisp1, data,
				     RKISP1_CIF_ISP_LSC_R_TABLE_DATA);

			data = RKISP1_CIF_ISP_LSC_TABLE_DATA(pconfig->gr_data_tbl[i + j],
							     pconfig->gr_data_tbl[i + j + 1]);
			rkisp1_write(params->rkisp1, data,
				     RKISP1_CIF_ISP_LSC_GR_TABLE_DATA);

			data = RKISP1_CIF_ISP_LSC_TABLE_DATA(pconfig->gb_data_tbl[i + j],
							     pconfig->gb_data_tbl[i + j + 1]);
			rkisp1_write(params->rkisp1, data,
				     RKISP1_CIF_ISP_LSC_GB_TABLE_DATA);

			data = RKISP1_CIF_ISP_LSC_TABLE_DATA(pconfig->b_data_tbl[i + j],
							     pconfig->b_data_tbl[i + j + 1]);
			rkisp1_write(params->rkisp1, data,
				     RKISP1_CIF_ISP_LSC_B_TABLE_DATA);
		}
		data = RKISP1_CIF_ISP_LSC_TABLE_DATA(pconfig->r_data_tbl[i + j], 0);
		rkisp1_write(params->rkisp1, data,
			     RKISP1_CIF_ISP_LSC_R_TABLE_DATA);

		data = RKISP1_CIF_ISP_LSC_TABLE_DATA(pconfig->gr_data_tbl[i + j], 0);
		rkisp1_write(params->rkisp1, data,
			     RKISP1_CIF_ISP_LSC_GR_TABLE_DATA);

		data = RKISP1_CIF_ISP_LSC_TABLE_DATA(pconfig->gb_data_tbl[i + j], 0);
		rkisp1_write(params->rkisp1, data,
			     RKISP1_CIF_ISP_LSC_GB_TABLE_DATA);

		data = RKISP1_CIF_ISP_LSC_TABLE_DATA(pconfig->b_data_tbl[i + j], 0);
		rkisp1_write(params->rkisp1, data,
			     RKISP1_CIF_ISP_LSC_B_TABLE_DATA);
	}
	isp_lsc_table_sel = (isp_lsc_status & RKISP1_CIF_ISP_LSC_ACTIVE_TABLE) ?
			    RKISP1_CIF_ISP_LSC_TABLE_0 :
			    RKISP1_CIF_ISP_LSC_TABLE_1;
	rkisp1_write(params->rkisp1, isp_lsc_table_sel,
		     RKISP1_CIF_ISP_LSC_TABLE_SEL);
}

static void rkisp1_lsc_config(struct rkisp1_params *params,
			      const struct rkisp1_cif_isp_lsc_config *arg)
{
	unsigned int i, data;
	u32 lsc_ctrl;

	/* To config must be off , store the current status firstly */
	lsc_ctrl = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_LSC_CTRL);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_LSC_CTRL,
				RKISP1_CIF_ISP_LSC_CTRL_ENA);
	rkisp1_lsc_correct_matrix_config(params, arg);

	for (i = 0; i < 4; i++) {
		/* program x size tables */
		data = RKISP1_CIF_ISP_LSC_SECT_SIZE(arg->x_size_tbl[i * 2],
						    arg->x_size_tbl[i * 2 + 1]);
		rkisp1_write(params->rkisp1, data,
			     RKISP1_CIF_ISP_LSC_XSIZE_01 + i * 4);

		/* program x grad tables */
		data = RKISP1_CIF_ISP_LSC_SECT_SIZE(arg->x_grad_tbl[i * 2],
						    arg->x_grad_tbl[i * 2 + 1]);
		rkisp1_write(params->rkisp1, data,
			     RKISP1_CIF_ISP_LSC_XGRAD_01 + i * 4);

		/* program y size tables */
		data = RKISP1_CIF_ISP_LSC_SECT_SIZE(arg->y_size_tbl[i * 2],
						    arg->y_size_tbl[i * 2 + 1]);
		rkisp1_write(params->rkisp1, data,
			     RKISP1_CIF_ISP_LSC_YSIZE_01 + i * 4);

		/* program y grad tables */
		data = RKISP1_CIF_ISP_LSC_SECT_SIZE(arg->y_grad_tbl[i * 2],
						    arg->y_grad_tbl[i * 2 + 1]);
		rkisp1_write(params->rkisp1, data,
			     RKISP1_CIF_ISP_LSC_YGRAD_01 + i * 4);
	}

	/* restore the lsc ctrl status */
	if (lsc_ctrl & RKISP1_CIF_ISP_LSC_CTRL_ENA) {
		rkisp1_param_set_bits(params,
				      RKISP1_CIF_ISP_LSC_CTRL,
				      RKISP1_CIF_ISP_LSC_CTRL_ENA);
	} else {
		rkisp1_param_clear_bits(params,
					RKISP1_CIF_ISP_LSC_CTRL,
					RKISP1_CIF_ISP_LSC_CTRL_ENA);
	}
}

/* ISP Filtering function */
static void rkisp1_flt_config(struct rkisp1_params *params,
			      const struct rkisp1_cif_isp_flt_config *arg)
{
	u32 filt_mode;

	rkisp1_write(params->rkisp1,
		     arg->thresh_bl0, RKISP1_CIF_ISP_FILT_THRESH_BL0);
	rkisp1_write(params->rkisp1,
		     arg->thresh_bl1, RKISP1_CIF_ISP_FILT_THRESH_BL1);
	rkisp1_write(params->rkisp1,
		     arg->thresh_sh0, RKISP1_CIF_ISP_FILT_THRESH_SH0);
	rkisp1_write(params->rkisp1,
		     arg->thresh_sh1, RKISP1_CIF_ISP_FILT_THRESH_SH1);
	rkisp1_write(params->rkisp1, arg->fac_bl0, RKISP1_CIF_ISP_FILT_FAC_BL0);
	rkisp1_write(params->rkisp1, arg->fac_bl1, RKISP1_CIF_ISP_FILT_FAC_BL1);
	rkisp1_write(params->rkisp1, arg->fac_mid, RKISP1_CIF_ISP_FILT_FAC_MID);
	rkisp1_write(params->rkisp1, arg->fac_sh0, RKISP1_CIF_ISP_FILT_FAC_SH0);
	rkisp1_write(params->rkisp1, arg->fac_sh1, RKISP1_CIF_ISP_FILT_FAC_SH1);
	rkisp1_write(params->rkisp1,
		     arg->lum_weight, RKISP1_CIF_ISP_FILT_LUM_WEIGHT);

	rkisp1_write(params->rkisp1,
		     (arg->mode ? RKISP1_CIF_ISP_FLT_MODE_DNR : 0) |
		     RKISP1_CIF_ISP_FLT_CHROMA_V_MODE(arg->chr_v_mode) |
		     RKISP1_CIF_ISP_FLT_CHROMA_H_MODE(arg->chr_h_mode) |
		     RKISP1_CIF_ISP_FLT_GREEN_STAGE1(arg->grn_stage1),
		     RKISP1_CIF_ISP_FILT_MODE);

	/* avoid to override the old enable value */
	filt_mode = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_FILT_MODE);
	filt_mode &= RKISP1_CIF_ISP_FLT_ENA;
	if (arg->mode)
		filt_mode |= RKISP1_CIF_ISP_FLT_MODE_DNR;
	filt_mode |= RKISP1_CIF_ISP_FLT_CHROMA_V_MODE(arg->chr_v_mode) |
		     RKISP1_CIF_ISP_FLT_CHROMA_H_MODE(arg->chr_h_mode) |
		     RKISP1_CIF_ISP_FLT_GREEN_STAGE1(arg->grn_stage1);
	rkisp1_write(params->rkisp1, filt_mode, RKISP1_CIF_ISP_FILT_MODE);
}

/* ISP demosaic interface function */
static int rkisp1_bdm_config(struct rkisp1_params *params,
			     const struct rkisp1_cif_isp_bdm_config *arg)
{
	u32 bdm_th;

	/* avoid to override the old enable value */
	bdm_th = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_DEMOSAIC);
	bdm_th &= RKISP1_CIF_ISP_DEMOSAIC_BYPASS;
	bdm_th |= arg->demosaic_th & ~RKISP1_CIF_ISP_DEMOSAIC_BYPASS;
	/* set demosaic threshold */
	rkisp1_write(params->rkisp1, bdm_th, RKISP1_CIF_ISP_DEMOSAIC);
	return 0;
}

/* ISP GAMMA correction interface function */
static void rkisp1_sdg_config(struct rkisp1_params *params,
			      const struct rkisp1_cif_isp_sdg_config *arg)
{
	unsigned int i;

	rkisp1_write(params->rkisp1,
		     arg->xa_pnts.gamma_dx0, RKISP1_CIF_ISP_GAMMA_DX_LO);
	rkisp1_write(params->rkisp1,
		     arg->xa_pnts.gamma_dx1, RKISP1_CIF_ISP_GAMMA_DX_HI);

	for (i = 0; i < RKISP1_CIF_ISP_DEGAMMA_CURVE_SIZE; i++) {
		rkisp1_write(params->rkisp1, arg->curve_r.gamma_y[i],
			     RKISP1_CIF_ISP_GAMMA_R_Y0 + i * 4);
		rkisp1_write(params->rkisp1, arg->curve_g.gamma_y[i],
			     RKISP1_CIF_ISP_GAMMA_G_Y0 + i * 4);
		rkisp1_write(params->rkisp1, arg->curve_b.gamma_y[i],
			     RKISP1_CIF_ISP_GAMMA_B_Y0 + i * 4);
	}
}

/* ISP GAMMA correction interface function */
static void rkisp1_goc_config(struct rkisp1_params *params,
			      const struct rkisp1_cif_isp_goc_config *arg)
{
	unsigned int i;

	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_CTRL,
				RKISP1_CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA);
	rkisp1_write(params->rkisp1, arg->mode, RKISP1_CIF_ISP_GAMMA_OUT_MODE);

	for (i = 0; i < RKISP1_CIF_ISP_GAMMA_OUT_MAX_SAMPLES; i++)
		rkisp1_write(params->rkisp1, arg->gamma_y[i],
			     RKISP1_CIF_ISP_GAMMA_OUT_Y_0 + i * 4);
}

/* ISP Cross Talk */
static void rkisp1_ctk_config(struct rkisp1_params *params,
			      const struct rkisp1_cif_isp_ctk_config *arg)
{
	rkisp1_write(params->rkisp1, arg->coeff0, RKISP1_CIF_ISP_CT_COEFF_0);
	rkisp1_write(params->rkisp1, arg->coeff1, RKISP1_CIF_ISP_CT_COEFF_1);
	rkisp1_write(params->rkisp1, arg->coeff2, RKISP1_CIF_ISP_CT_COEFF_2);
	rkisp1_write(params->rkisp1, arg->coeff3, RKISP1_CIF_ISP_CT_COEFF_3);
	rkisp1_write(params->rkisp1, arg->coeff4, RKISP1_CIF_ISP_CT_COEFF_4);
	rkisp1_write(params->rkisp1, arg->coeff5, RKISP1_CIF_ISP_CT_COEFF_5);
	rkisp1_write(params->rkisp1, arg->coeff6, RKISP1_CIF_ISP_CT_COEFF_6);
	rkisp1_write(params->rkisp1, arg->coeff7, RKISP1_CIF_ISP_CT_COEFF_7);
	rkisp1_write(params->rkisp1, arg->coeff8, RKISP1_CIF_ISP_CT_COEFF_8);
	rkisp1_write(params->rkisp1, arg->ct_offset_r,
		     RKISP1_CIF_ISP_CT_OFFSET_R);
	rkisp1_write(params->rkisp1, arg->ct_offset_g,
		     RKISP1_CIF_ISP_CT_OFFSET_G);
	rkisp1_write(params->rkisp1, arg->ct_offset_b,
		     RKISP1_CIF_ISP_CT_OFFSET_B);
}

static void rkisp1_ctk_enable(struct rkisp1_params *params, bool en)
{
	if (en)
		return;

	/* Write back the default values. */
	rkisp1_write(params->rkisp1, 0x80, RKISP1_CIF_ISP_CT_COEFF_0);
	rkisp1_write(params->rkisp1, 0, RKISP1_CIF_ISP_CT_COEFF_1);
	rkisp1_write(params->rkisp1, 0, RKISP1_CIF_ISP_CT_COEFF_2);
	rkisp1_write(params->rkisp1, 0, RKISP1_CIF_ISP_CT_COEFF_3);
	rkisp1_write(params->rkisp1, 0x80, RKISP1_CIF_ISP_CT_COEFF_4);
	rkisp1_write(params->rkisp1, 0, RKISP1_CIF_ISP_CT_COEFF_5);
	rkisp1_write(params->rkisp1, 0, RKISP1_CIF_ISP_CT_COEFF_6);
	rkisp1_write(params->rkisp1, 0, RKISP1_CIF_ISP_CT_COEFF_7);
	rkisp1_write(params->rkisp1, 0x80, RKISP1_CIF_ISP_CT_COEFF_8);

	rkisp1_write(params->rkisp1, 0, RKISP1_CIF_ISP_CT_OFFSET_R);
	rkisp1_write(params->rkisp1, 0, RKISP1_CIF_ISP_CT_OFFSET_G);
	rkisp1_write(params->rkisp1, 0, RKISP1_CIF_ISP_CT_OFFSET_B);
}

/* ISP White Balance Mode */
static void rkisp1_awb_meas_config(struct rkisp1_params *params,
			const struct rkisp1_cif_isp_awb_meas_config *arg)
{
	u32 reg_val = 0;
	/* based on the mode,configure the awb module */
	if (arg->awb_mode == RKISP1_CIF_ISP_AWB_MODE_YCBCR) {
		/* Reference Cb and Cr */
		rkisp1_write(params->rkisp1,
			     RKISP1_CIF_ISP_AWB_REF_CR_SET(arg->awb_ref_cr) |
			     arg->awb_ref_cb, RKISP1_CIF_ISP_AWB_REF);
		/* Yc Threshold */
		rkisp1_write(params->rkisp1,
			     RKISP1_CIF_ISP_AWB_MAX_Y_SET(arg->max_y) |
			     RKISP1_CIF_ISP_AWB_MIN_Y_SET(arg->min_y) |
			     RKISP1_CIF_ISP_AWB_MAX_CS_SET(arg->max_csum) |
			     arg->min_c, RKISP1_CIF_ISP_AWB_THRESH);
	}

	reg_val = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_AWB_PROP);
	if (arg->enable_ymax_cmp)
		reg_val |= RKISP1_CIF_ISP_AWB_YMAX_CMP_EN;
	else
		reg_val &= ~RKISP1_CIF_ISP_AWB_YMAX_CMP_EN;
	rkisp1_write(params->rkisp1, reg_val, RKISP1_CIF_ISP_AWB_PROP);

	/* window offset */
	rkisp1_write(params->rkisp1,
		     arg->awb_wnd.v_offs, RKISP1_CIF_ISP_AWB_WND_V_OFFS);
	rkisp1_write(params->rkisp1,
		     arg->awb_wnd.h_offs, RKISP1_CIF_ISP_AWB_WND_H_OFFS);
	/* AWB window size */
	rkisp1_write(params->rkisp1,
		     arg->awb_wnd.v_size, RKISP1_CIF_ISP_AWB_WND_V_SIZE);
	rkisp1_write(params->rkisp1,
		     arg->awb_wnd.h_size, RKISP1_CIF_ISP_AWB_WND_H_SIZE);
	/* Number of frames */
	rkisp1_write(params->rkisp1,
		     arg->frames, RKISP1_CIF_ISP_AWB_FRAMES);
}

static void
rkisp1_awb_meas_enable(struct rkisp1_params *params,
		       const struct rkisp1_cif_isp_awb_meas_config *arg,
		       bool en)
{
	u32 reg_val = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_AWB_PROP);

	/* switch off */
	reg_val &= RKISP1_CIF_ISP_AWB_MODE_MASK_NONE;

	if (en) {
		if (arg->awb_mode == RKISP1_CIF_ISP_AWB_MODE_RGB)
			reg_val |= RKISP1_CIF_ISP_AWB_MODE_RGB_EN;
		else
			reg_val |= RKISP1_CIF_ISP_AWB_MODE_YCBCR_EN;

		rkisp1_write(params->rkisp1, reg_val, RKISP1_CIF_ISP_AWB_PROP);

		/* Measurements require AWB block be active. */
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_CTRL,
				      RKISP1_CIF_ISP_CTRL_ISP_AWB_ENA);
	} else {
		rkisp1_write(params->rkisp1,
			     reg_val, RKISP1_CIF_ISP_AWB_PROP);
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_CTRL,
					RKISP1_CIF_ISP_CTRL_ISP_AWB_ENA);
	}
}

static void
rkisp1_awb_gain_config(struct rkisp1_params *params,
		       const struct rkisp1_cif_isp_awb_gain_config *arg)
{
	rkisp1_write(params->rkisp1,
		     RKISP1_CIF_ISP_AWB_GAIN_R_SET(arg->gain_green_r) |
		     arg->gain_green_b, RKISP1_CIF_ISP_AWB_GAIN_G);

	rkisp1_write(params->rkisp1,
		     RKISP1_CIF_ISP_AWB_GAIN_R_SET(arg->gain_red) |
		     arg->gain_blue, RKISP1_CIF_ISP_AWB_GAIN_RB);
}

static void rkisp1_aec_config(struct rkisp1_params *params,
			      const struct rkisp1_cif_isp_aec_config *arg)
{
	unsigned int block_hsize, block_vsize;
	u32 exp_ctrl;

	/* avoid to override the old enable value */
	exp_ctrl = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_EXP_CTRL);
	exp_ctrl &= RKISP1_CIF_ISP_EXP_ENA;
	if (arg->autostop)
		exp_ctrl |= RKISP1_CIF_ISP_EXP_CTRL_AUTOSTOP;
	if (arg->mode == RKISP1_CIF_ISP_EXP_MEASURING_MODE_1)
		exp_ctrl |= RKISP1_CIF_ISP_EXP_CTRL_MEASMODE_1;
	rkisp1_write(params->rkisp1, exp_ctrl, RKISP1_CIF_ISP_EXP_CTRL);

	rkisp1_write(params->rkisp1,
		     arg->meas_window.h_offs, RKISP1_CIF_ISP_EXP_H_OFFSET);
	rkisp1_write(params->rkisp1,
		     arg->meas_window.v_offs, RKISP1_CIF_ISP_EXP_V_OFFSET);

	block_hsize = arg->meas_window.h_size /
		      RKISP1_CIF_ISP_EXP_COLUMN_NUM - 1;
	block_vsize = arg->meas_window.v_size /
		      RKISP1_CIF_ISP_EXP_ROW_NUM - 1;

	rkisp1_write(params->rkisp1,
		     RKISP1_CIF_ISP_EXP_H_SIZE_SET(block_hsize),
		     RKISP1_CIF_ISP_EXP_H_SIZE);
	rkisp1_write(params->rkisp1,
		     RKISP1_CIF_ISP_EXP_V_SIZE_SET(block_vsize),
		     RKISP1_CIF_ISP_EXP_V_SIZE);
}

static void rkisp1_cproc_config(struct rkisp1_params *params,
				const struct rkisp1_cif_isp_cproc_config *arg)
{
	struct rkisp1_cif_isp_isp_other_cfg *cur_other_cfg =
						&params->cur_params.others;
	struct rkisp1_cif_isp_ie_config *cur_ie_config =
						&cur_other_cfg->ie_config;
	u32 effect = cur_ie_config->effect;
	u32 quantization = params->quantization;

	rkisp1_write(params->rkisp1, arg->contrast, RKISP1_CIF_C_PROC_CONTRAST);
	rkisp1_write(params->rkisp1, arg->hue, RKISP1_CIF_C_PROC_HUE);
	rkisp1_write(params->rkisp1, arg->sat, RKISP1_CIF_C_PROC_SATURATION);
	rkisp1_write(params->rkisp1, arg->brightness,
		     RKISP1_CIF_C_PROC_BRIGHTNESS);

	if (quantization != V4L2_QUANTIZATION_FULL_RANGE ||
	    effect != V4L2_COLORFX_NONE) {
		rkisp1_param_clear_bits(params, RKISP1_CIF_C_PROC_CTRL,
					RKISP1_CIF_C_PROC_YOUT_FULL |
					RKISP1_CIF_C_PROC_YIN_FULL |
					RKISP1_CIF_C_PROC_COUT_FULL);
	} else {
		rkisp1_param_set_bits(params, RKISP1_CIF_C_PROC_CTRL,
				      RKISP1_CIF_C_PROC_YOUT_FULL |
				      RKISP1_CIF_C_PROC_YIN_FULL |
				      RKISP1_CIF_C_PROC_COUT_FULL);
	}
}

static void rkisp1_hst_config(struct rkisp1_params *params,
			      const struct rkisp1_cif_isp_hst_config *arg)
{
	unsigned int block_hsize, block_vsize;
	static const u32 hist_weight_regs[] = {
		RKISP1_CIF_ISP_HIST_WEIGHT_00TO30,
		RKISP1_CIF_ISP_HIST_WEIGHT_40TO21,
		RKISP1_CIF_ISP_HIST_WEIGHT_31TO12,
		RKISP1_CIF_ISP_HIST_WEIGHT_22TO03,
		RKISP1_CIF_ISP_HIST_WEIGHT_13TO43,
		RKISP1_CIF_ISP_HIST_WEIGHT_04TO34,
		RKISP1_CIF_ISP_HIST_WEIGHT_44,
	};
	const u8 *weight;
	unsigned int i;
	u32 hist_prop;

	/* avoid to override the old enable value */
	hist_prop = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_HIST_PROP);
	hist_prop &= RKISP1_CIF_ISP_HIST_PROP_MODE_MASK;
	hist_prop |= RKISP1_CIF_ISP_HIST_PREDIV_SET(arg->histogram_predivider);
	rkisp1_write(params->rkisp1, hist_prop, RKISP1_CIF_ISP_HIST_PROP);
	rkisp1_write(params->rkisp1,
		     arg->meas_window.h_offs,
		     RKISP1_CIF_ISP_HIST_H_OFFS);
	rkisp1_write(params->rkisp1,
		     arg->meas_window.v_offs,
		     RKISP1_CIF_ISP_HIST_V_OFFS);

	block_hsize = arg->meas_window.h_size /
		      RKISP1_CIF_ISP_HIST_COLUMN_NUM - 1;
	block_vsize = arg->meas_window.v_size / RKISP1_CIF_ISP_HIST_ROW_NUM - 1;

	rkisp1_write(params->rkisp1, block_hsize, RKISP1_CIF_ISP_HIST_H_SIZE);
	rkisp1_write(params->rkisp1, block_vsize, RKISP1_CIF_ISP_HIST_V_SIZE);

	weight = arg->hist_weight;
	for (i = 0; i < ARRAY_SIZE(hist_weight_regs); ++i, weight += 4)
		rkisp1_write(params->rkisp1,
			     RKISP1_CIF_ISP_HIST_WEIGHT_SET(weight[0],
							    weight[1],
							    weight[2],
							    weight[3]),
				 hist_weight_regs[i]);
}

static void
rkisp1_hst_enable(struct rkisp1_params *params,
		  const struct rkisp1_cif_isp_hst_config *arg, bool en)
{
	if (en)	{
		u32 hist_prop = rkisp1_read(params->rkisp1,
					    RKISP1_CIF_ISP_HIST_PROP);

		hist_prop &= ~RKISP1_CIF_ISP_HIST_PROP_MODE_MASK;
		hist_prop |= arg->mode;
		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_HIST_PROP,
				      hist_prop);
	} else {
		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_HIST_PROP,
					RKISP1_CIF_ISP_HIST_PROP_MODE_MASK);
	}
}

static void rkisp1_afm_config(struct rkisp1_params *params,
			      const struct rkisp1_cif_isp_afc_config *arg)
{
	size_t num_of_win = min_t(size_t, ARRAY_SIZE(arg->afm_win),
				  arg->num_afm_win);
	u32 afm_ctrl = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_AFM_CTRL);
	unsigned int i;

	/* Switch off to configure. */
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_AFM_CTRL,
				RKISP1_CIF_ISP_AFM_ENA);

	for (i = 0; i < num_of_win; i++) {
		rkisp1_write(params->rkisp1,
			     RKISP1_CIF_ISP_AFM_WINDOW_X(arg->afm_win[i].h_offs) |
			     RKISP1_CIF_ISP_AFM_WINDOW_Y(arg->afm_win[i].v_offs),
			     RKISP1_CIF_ISP_AFM_LT_A + i * 8);
		rkisp1_write(params->rkisp1,
			     RKISP1_CIF_ISP_AFM_WINDOW_X(arg->afm_win[i].h_size +
							 arg->afm_win[i].h_offs) |
			     RKISP1_CIF_ISP_AFM_WINDOW_Y(arg->afm_win[i].v_size +
							 arg->afm_win[i].v_offs),
			     RKISP1_CIF_ISP_AFM_RB_A + i * 8);
	}
	rkisp1_write(params->rkisp1, arg->thres, RKISP1_CIF_ISP_AFM_THRES);
	rkisp1_write(params->rkisp1, arg->var_shift,
		     RKISP1_CIF_ISP_AFM_VAR_SHIFT);
	/* restore afm status */
	rkisp1_write(params->rkisp1, afm_ctrl, RKISP1_CIF_ISP_AFM_CTRL);
}

static void rkisp1_ie_config(struct rkisp1_params *params,
			     const struct rkisp1_cif_isp_ie_config *arg)
{
	u32 eff_ctrl;

	eff_ctrl = rkisp1_read(params->rkisp1, RKISP1_CIF_IMG_EFF_CTRL);
	eff_ctrl &= ~RKISP1_CIF_IMG_EFF_CTRL_MODE_MASK;

	if (params->quantization == V4L2_QUANTIZATION_FULL_RANGE)
		eff_ctrl |= RKISP1_CIF_IMG_EFF_CTRL_YCBCR_FULL;

	switch (arg->effect) {
	case V4L2_COLORFX_SEPIA:
		eff_ctrl |= RKISP1_CIF_IMG_EFF_CTRL_MODE_SEPIA;
		break;
	case V4L2_COLORFX_SET_CBCR:
		rkisp1_write(params->rkisp1, arg->eff_tint,
			     RKISP1_CIF_IMG_EFF_TINT);
		eff_ctrl |= RKISP1_CIF_IMG_EFF_CTRL_MODE_SEPIA;
		break;
		/*
		 * Color selection is similar to water color(AQUA):
		 * grayscale + selected color w threshold
		 */
	case V4L2_COLORFX_AQUA:
		eff_ctrl |= RKISP1_CIF_IMG_EFF_CTRL_MODE_COLOR_SEL;
		rkisp1_write(params->rkisp1, arg->color_sel,
			     RKISP1_CIF_IMG_EFF_COLOR_SEL);
		break;
	case V4L2_COLORFX_EMBOSS:
		eff_ctrl |= RKISP1_CIF_IMG_EFF_CTRL_MODE_EMBOSS;
		rkisp1_write(params->rkisp1, arg->eff_mat_1,
			     RKISP1_CIF_IMG_EFF_MAT_1);
		rkisp1_write(params->rkisp1, arg->eff_mat_2,
			     RKISP1_CIF_IMG_EFF_MAT_2);
		rkisp1_write(params->rkisp1, arg->eff_mat_3,
			     RKISP1_CIF_IMG_EFF_MAT_3);
		break;
	case V4L2_COLORFX_SKETCH:
		eff_ctrl |= RKISP1_CIF_IMG_EFF_CTRL_MODE_SKETCH;
		rkisp1_write(params->rkisp1, arg->eff_mat_3,
			     RKISP1_CIF_IMG_EFF_MAT_3);
		rkisp1_write(params->rkisp1, arg->eff_mat_4,
			     RKISP1_CIF_IMG_EFF_MAT_4);
		rkisp1_write(params->rkisp1, arg->eff_mat_5,
			     RKISP1_CIF_IMG_EFF_MAT_5);
		break;
	case V4L2_COLORFX_BW:
		eff_ctrl |= RKISP1_CIF_IMG_EFF_CTRL_MODE_BLACKWHITE;
		break;
	case V4L2_COLORFX_NEGATIVE:
		eff_ctrl |= RKISP1_CIF_IMG_EFF_CTRL_MODE_NEGATIVE;
		break;
	default:
		break;
	}

	rkisp1_write(params->rkisp1, eff_ctrl, RKISP1_CIF_IMG_EFF_CTRL);
}

static void rkisp1_ie_enable(struct rkisp1_params *params, bool en)
{
	if (en) {
		rkisp1_param_set_bits(params, RKISP1_CIF_ICCL,
				      RKISP1_CIF_ICCL_IE_CLK);
		rkisp1_write(params->rkisp1, RKISP1_CIF_IMG_EFF_CTRL_ENABLE,
			     RKISP1_CIF_IMG_EFF_CTRL);
		rkisp1_param_set_bits(params, RKISP1_CIF_IMG_EFF_CTRL,
				      RKISP1_CIF_IMG_EFF_CTRL_CFG_UPD);
	} else {
		rkisp1_param_clear_bits(params, RKISP1_CIF_IMG_EFF_CTRL,
					RKISP1_CIF_IMG_EFF_CTRL_ENABLE);
		rkisp1_param_clear_bits(params, RKISP1_CIF_ICCL,
					RKISP1_CIF_ICCL_IE_CLK);
	}
}

static void rkisp1_csm_config(struct rkisp1_params *params, bool full_range)
{
	static const u16 full_range_coeff[] = {
		0x0026, 0x004b, 0x000f,
		0x01ea, 0x01d6, 0x0040,
		0x0040, 0x01ca, 0x01f6
	};
	static const u16 limited_range_coeff[] = {
		0x0021, 0x0040, 0x000d,
		0x01ed, 0x01db, 0x0038,
		0x0038, 0x01d1, 0x01f7,
	};
	unsigned int i;

	if (full_range) {
		for (i = 0; i < ARRAY_SIZE(full_range_coeff); i++)
			rkisp1_write(params->rkisp1, full_range_coeff[i],
				     RKISP1_CIF_ISP_CC_COEFF_0 + i * 4);

		rkisp1_param_set_bits(params, RKISP1_CIF_ISP_CTRL,
				      RKISP1_CIF_ISP_CTRL_ISP_CSM_Y_FULL_ENA |
				      RKISP1_CIF_ISP_CTRL_ISP_CSM_C_FULL_ENA);
	} else {
		for (i = 0; i < ARRAY_SIZE(limited_range_coeff); i++)
			rkisp1_write(params->rkisp1, limited_range_coeff[i],
				     RKISP1_CIF_ISP_CC_COEFF_0 + i * 4);

		rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_CTRL,
					RKISP1_CIF_ISP_CTRL_ISP_CSM_Y_FULL_ENA |
					RKISP1_CIF_ISP_CTRL_ISP_CSM_C_FULL_ENA);
	}
}

/* ISP De-noise Pre-Filter(DPF) function */
static void rkisp1_dpf_config(struct rkisp1_params *params,
			      const struct rkisp1_cif_isp_dpf_config *arg)
{
	unsigned int isp_dpf_mode, spatial_coeff, i;

	switch (arg->gain.mode) {
	case RKISP1_CIF_ISP_DPF_GAIN_USAGE_NF_GAINS:
		isp_dpf_mode = RKISP1_CIF_ISP_DPF_MODE_USE_NF_GAIN |
			       RKISP1_CIF_ISP_DPF_MODE_AWB_GAIN_COMP;
		break;
	case RKISP1_CIF_ISP_DPF_GAIN_USAGE_LSC_GAINS:
		isp_dpf_mode = RKISP1_CIF_ISP_DPF_MODE_LSC_GAIN_COMP;
		break;
	case RKISP1_CIF_ISP_DPF_GAIN_USAGE_NF_LSC_GAINS:
		isp_dpf_mode = RKISP1_CIF_ISP_DPF_MODE_USE_NF_GAIN |
			       RKISP1_CIF_ISP_DPF_MODE_AWB_GAIN_COMP |
			       RKISP1_CIF_ISP_DPF_MODE_LSC_GAIN_COMP;
		break;
	case RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_GAINS:
		isp_dpf_mode = RKISP1_CIF_ISP_DPF_MODE_AWB_GAIN_COMP;
		break;
	case RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_LSC_GAINS:
		isp_dpf_mode = RKISP1_CIF_ISP_DPF_MODE_LSC_GAIN_COMP |
			       RKISP1_CIF_ISP_DPF_MODE_AWB_GAIN_COMP;
		break;
	case RKISP1_CIF_ISP_DPF_GAIN_USAGE_DISABLED:
	default:
		isp_dpf_mode = 0;
		break;
	}

	if (arg->nll.scale_mode == RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC)
		isp_dpf_mode |= RKISP1_CIF_ISP_DPF_MODE_NLL_SEGMENTATION;
	if (arg->rb_flt.fltsize == RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_9x9)
		isp_dpf_mode |= RKISP1_CIF_ISP_DPF_MODE_RB_FLTSIZE_9x9;
	if (!arg->rb_flt.r_enable)
		isp_dpf_mode |= RKISP1_CIF_ISP_DPF_MODE_R_FLT_DIS;
	if (!arg->rb_flt.b_enable)
		isp_dpf_mode |= RKISP1_CIF_ISP_DPF_MODE_B_FLT_DIS;
	if (!arg->g_flt.gb_enable)
		isp_dpf_mode |= RKISP1_CIF_ISP_DPF_MODE_GB_FLT_DIS;
	if (!arg->g_flt.gr_enable)
		isp_dpf_mode |= RKISP1_CIF_ISP_DPF_MODE_GR_FLT_DIS;

	rkisp1_param_set_bits(params, RKISP1_CIF_ISP_DPF_MODE,
			      isp_dpf_mode);
	rkisp1_write(params->rkisp1, arg->gain.nf_b_gain,
		     RKISP1_CIF_ISP_DPF_NF_GAIN_B);
	rkisp1_write(params->rkisp1, arg->gain.nf_r_gain,
		     RKISP1_CIF_ISP_DPF_NF_GAIN_R);
	rkisp1_write(params->rkisp1, arg->gain.nf_gb_gain,
		     RKISP1_CIF_ISP_DPF_NF_GAIN_GB);
	rkisp1_write(params->rkisp1, arg->gain.nf_gr_gain,
		     RKISP1_CIF_ISP_DPF_NF_GAIN_GR);

	for (i = 0; i < RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS; i++) {
		rkisp1_write(params->rkisp1, arg->nll.coeff[i],
			     RKISP1_CIF_ISP_DPF_NULL_COEFF_0 + i * 4);
	}

	spatial_coeff = arg->g_flt.spatial_coeff[0] |
			(arg->g_flt.spatial_coeff[1] << 8) |
			(arg->g_flt.spatial_coeff[2] << 16) |
			(arg->g_flt.spatial_coeff[3] << 24);
	rkisp1_write(params->rkisp1, spatial_coeff,
		     RKISP1_CIF_ISP_DPF_S_WEIGHT_G_1_4);

	spatial_coeff = arg->g_flt.spatial_coeff[4] |
			(arg->g_flt.spatial_coeff[5] << 8);
	rkisp1_write(params->rkisp1, spatial_coeff,
		     RKISP1_CIF_ISP_DPF_S_WEIGHT_G_5_6);

	spatial_coeff = arg->rb_flt.spatial_coeff[0] |
			(arg->rb_flt.spatial_coeff[1] << 8) |
			(arg->rb_flt.spatial_coeff[2] << 16) |
			(arg->rb_flt.spatial_coeff[3] << 24);
	rkisp1_write(params->rkisp1, spatial_coeff,
		     RKISP1_CIF_ISP_DPF_S_WEIGHT_RB_1_4);

	spatial_coeff = arg->rb_flt.spatial_coeff[4] |
			(arg->rb_flt.spatial_coeff[5] << 8);
	rkisp1_write(params->rkisp1, spatial_coeff,
		     RKISP1_CIF_ISP_DPF_S_WEIGHT_RB_5_6);
}

static void
rkisp1_dpf_strength_config(struct rkisp1_params *params,
			   const struct rkisp1_cif_isp_dpf_strength_config *arg)
{
	rkisp1_write(params->rkisp1, arg->b, RKISP1_CIF_ISP_DPF_STRENGTH_B);
	rkisp1_write(params->rkisp1, arg->g, RKISP1_CIF_ISP_DPF_STRENGTH_G);
	rkisp1_write(params->rkisp1, arg->r, RKISP1_CIF_ISP_DPF_STRENGTH_R);
}

static void
rkisp1_isp_isr_other_config(struct rkisp1_params *params,
			    const struct rkisp1_params_cfg *new_params)
{
	unsigned int module_en_update, module_cfg_update, module_ens;

	module_en_update = new_params->module_en_update;
	module_cfg_update = new_params->module_cfg_update;
	module_ens = new_params->module_ens;

	if ((module_en_update & RKISP1_CIF_ISP_MODULE_DPCC) ||
	    (module_cfg_update & RKISP1_CIF_ISP_MODULE_DPCC)) {
		/*update dpc config */
		if ((module_cfg_update & RKISP1_CIF_ISP_MODULE_DPCC))
			rkisp1_dpcc_config(params,
					   &new_params->others.dpcc_config);

		if (module_en_update & RKISP1_CIF_ISP_MODULE_DPCC) {
			if (!!(module_ens & RKISP1_CIF_ISP_MODULE_DPCC))
				rkisp1_param_set_bits(params,
						      RKISP1_CIF_ISP_DPCC_MODE,
						      RKISP1_CIF_ISP_DPCC_ENA);
			else
				rkisp1_param_clear_bits(params,
						RKISP1_CIF_ISP_DPCC_MODE,
						RKISP1_CIF_ISP_DPCC_ENA);
		}
	}

	if ((module_en_update & RKISP1_CIF_ISP_MODULE_BLS) ||
	    (module_cfg_update & RKISP1_CIF_ISP_MODULE_BLS)) {
		/* update bls config */
		if ((module_cfg_update & RKISP1_CIF_ISP_MODULE_BLS))
			rkisp1_bls_config(params,
					  &new_params->others.bls_config);

		if (module_en_update & RKISP1_CIF_ISP_MODULE_BLS) {
			if (!!(module_ens & RKISP1_CIF_ISP_MODULE_BLS))
				rkisp1_param_set_bits(params,
						      RKISP1_CIF_ISP_BLS_CTRL,
						      RKISP1_CIF_ISP_BLS_ENA);
			else
				rkisp1_param_clear_bits(params,
							RKISP1_CIF_ISP_BLS_CTRL,
							RKISP1_CIF_ISP_BLS_ENA);
		}
	}

	if ((module_en_update & RKISP1_CIF_ISP_MODULE_SDG) ||
	    (module_cfg_update & RKISP1_CIF_ISP_MODULE_SDG)) {
		/* update sdg config */
		if ((module_cfg_update & RKISP1_CIF_ISP_MODULE_SDG))
			rkisp1_sdg_config(params,
					  &new_params->others.sdg_config);

		if (module_en_update & RKISP1_CIF_ISP_MODULE_SDG) {
			if (!!(module_ens & RKISP1_CIF_ISP_MODULE_SDG))
				rkisp1_param_set_bits(params,
					RKISP1_CIF_ISP_CTRL,
					RKISP1_CIF_ISP_CTRL_ISP_GAMMA_IN_ENA);
			else
				rkisp1_param_clear_bits(params,
					RKISP1_CIF_ISP_CTRL,
					RKISP1_CIF_ISP_CTRL_ISP_GAMMA_IN_ENA);
		}
	}

	if ((module_en_update & RKISP1_CIF_ISP_MODULE_LSC) ||
	    (module_cfg_update & RKISP1_CIF_ISP_MODULE_LSC)) {
		/* update lsc config */
		if ((module_cfg_update & RKISP1_CIF_ISP_MODULE_LSC))
			rkisp1_lsc_config(params,
					  &new_params->others.lsc_config);

		if (module_en_update & RKISP1_CIF_ISP_MODULE_LSC) {
			if (!!(module_ens & RKISP1_CIF_ISP_MODULE_LSC))
				rkisp1_param_set_bits(params,
						RKISP1_CIF_ISP_LSC_CTRL,
						RKISP1_CIF_ISP_LSC_CTRL_ENA);
			else
				rkisp1_param_clear_bits(params,
						RKISP1_CIF_ISP_LSC_CTRL,
						RKISP1_CIF_ISP_LSC_CTRL_ENA);
		}
	}

	if ((module_en_update & RKISP1_CIF_ISP_MODULE_AWB_GAIN) ||
	    (module_cfg_update & RKISP1_CIF_ISP_MODULE_AWB_GAIN)) {
		/* update awb gains */
		if ((module_cfg_update & RKISP1_CIF_ISP_MODULE_AWB_GAIN))
			rkisp1_awb_gain_config(params,
					&new_params->others.awb_gain_config);

		if (module_en_update & RKISP1_CIF_ISP_MODULE_AWB_GAIN) {
			if (!!(module_ens & RKISP1_CIF_ISP_MODULE_AWB_GAIN))
				rkisp1_param_set_bits(params,
					RKISP1_CIF_ISP_CTRL,
					RKISP1_CIF_ISP_CTRL_ISP_AWB_ENA);
			else
				rkisp1_param_clear_bits(params,
					RKISP1_CIF_ISP_CTRL,
					RKISP1_CIF_ISP_CTRL_ISP_AWB_ENA);
		}
	}

	if ((module_en_update & RKISP1_CIF_ISP_MODULE_BDM) ||
	    (module_cfg_update & RKISP1_CIF_ISP_MODULE_BDM)) {
		/* update bdm config */
		if ((module_cfg_update & RKISP1_CIF_ISP_MODULE_BDM))
			rkisp1_bdm_config(params,
					  &new_params->others.bdm_config);

		if (module_en_update & RKISP1_CIF_ISP_MODULE_BDM) {
			if (!!(module_ens & RKISP1_CIF_ISP_MODULE_BDM))
				rkisp1_param_set_bits(params,
						RKISP1_CIF_ISP_DEMOSAIC,
						RKISP1_CIF_ISP_DEMOSAIC_BYPASS);
			else
				rkisp1_param_clear_bits(params,
						RKISP1_CIF_ISP_DEMOSAIC,
						RKISP1_CIF_ISP_DEMOSAIC_BYPASS);
		}
	}

	if ((module_en_update & RKISP1_CIF_ISP_MODULE_FLT) ||
	    (module_cfg_update & RKISP1_CIF_ISP_MODULE_FLT)) {
		/* update filter config */
		if ((module_cfg_update & RKISP1_CIF_ISP_MODULE_FLT))
			rkisp1_flt_config(params,
					  &new_params->others.flt_config);

		if (module_en_update & RKISP1_CIF_ISP_MODULE_FLT) {
			if (!!(module_ens & RKISP1_CIF_ISP_MODULE_FLT))
				rkisp1_param_set_bits(params,
						      RKISP1_CIF_ISP_FILT_MODE,
						      RKISP1_CIF_ISP_FLT_ENA);
			else
				rkisp1_param_clear_bits(params,
						RKISP1_CIF_ISP_FILT_MODE,
						RKISP1_CIF_ISP_FLT_ENA);
		}
	}

	if ((module_en_update & RKISP1_CIF_ISP_MODULE_CTK) ||
	    (module_cfg_update & RKISP1_CIF_ISP_MODULE_CTK)) {
		/* update ctk config */
		if ((module_cfg_update & RKISP1_CIF_ISP_MODULE_CTK))
			rkisp1_ctk_config(params,
					  &new_params->others.ctk_config);

		if (module_en_update & RKISP1_CIF_ISP_MODULE_CTK)
			rkisp1_ctk_enable(params,
				!!(module_ens & RKISP1_CIF_ISP_MODULE_CTK));
	}

	if ((module_en_update & RKISP1_CIF_ISP_MODULE_GOC) ||
	    (module_cfg_update & RKISP1_CIF_ISP_MODULE_GOC)) {
		/* update goc config */
		if ((module_cfg_update & RKISP1_CIF_ISP_MODULE_GOC))
			rkisp1_goc_config(params,
					  &new_params->others.goc_config);

		if (module_en_update & RKISP1_CIF_ISP_MODULE_GOC) {
			if (!!(module_ens & RKISP1_CIF_ISP_MODULE_GOC))
				rkisp1_param_set_bits(params,
					RKISP1_CIF_ISP_CTRL,
					RKISP1_CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA);
			else
				rkisp1_param_clear_bits(params,
					RKISP1_CIF_ISP_CTRL,
					RKISP1_CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA);
		}
	}

	if ((module_en_update & RKISP1_CIF_ISP_MODULE_CPROC) ||
	    (module_cfg_update & RKISP1_CIF_ISP_MODULE_CPROC)) {
		/* update cproc config */
		if ((module_cfg_update & RKISP1_CIF_ISP_MODULE_CPROC)) {
			rkisp1_cproc_config(params,
					    &new_params->others.cproc_config);
		}

		if (module_en_update & RKISP1_CIF_ISP_MODULE_CPROC) {
			if (!!(module_ens & RKISP1_CIF_ISP_MODULE_CPROC))
				rkisp1_param_set_bits(params,
						RKISP1_CIF_C_PROC_CTRL,
						RKISP1_CIF_C_PROC_CTR_ENABLE);
			else
				rkisp1_param_clear_bits(params,
						RKISP1_CIF_C_PROC_CTRL,
						RKISP1_CIF_C_PROC_CTR_ENABLE);
		}
	}

	if ((module_en_update & RKISP1_CIF_ISP_MODULE_IE) ||
	    (module_cfg_update & RKISP1_CIF_ISP_MODULE_IE)) {
		/* update ie config */
		if ((module_cfg_update & RKISP1_CIF_ISP_MODULE_IE))
			rkisp1_ie_config(params,
					 &new_params->others.ie_config);

		if (module_en_update & RKISP1_CIF_ISP_MODULE_IE)
			rkisp1_ie_enable(params,
				!!(module_ens & RKISP1_CIF_ISP_MODULE_IE));
	}

	if ((module_en_update & RKISP1_CIF_ISP_MODULE_DPF) ||
	    (module_cfg_update & RKISP1_CIF_ISP_MODULE_DPF)) {
		/* update dpf  config */
		if ((module_cfg_update & RKISP1_CIF_ISP_MODULE_DPF))
			rkisp1_dpf_config(params,
					  &new_params->others.dpf_config);

		if (module_en_update & RKISP1_CIF_ISP_MODULE_DPF) {
			if (!!(module_ens & RKISP1_CIF_ISP_MODULE_DPF))
				rkisp1_param_set_bits(params,
						   RKISP1_CIF_ISP_DPF_MODE,
						   RKISP1_CIF_ISP_DPF_MODE_EN);
			else
				rkisp1_param_clear_bits(params,
						RKISP1_CIF_ISP_DPF_MODE,
						RKISP1_CIF_ISP_DPF_MODE_EN);
		}
	}

	if ((module_en_update & RKISP1_CIF_ISP_MODULE_DPF_STRENGTH) ||
	    (module_cfg_update & RKISP1_CIF_ISP_MODULE_DPF_STRENGTH)) {
		/* update dpf strength config */
		rkisp1_dpf_strength_config(params,
				&new_params->others.dpf_strength_config);
	}
}

static void rkisp1_isp_isr_meas_config(struct rkisp1_params *params,
				       struct  rkisp1_params_cfg *new_params)
{
	unsigned int module_en_update, module_cfg_update, module_ens;

	module_en_update = new_params->module_en_update;
	module_cfg_update = new_params->module_cfg_update;
	module_ens = new_params->module_ens;

	if ((module_en_update & RKISP1_CIF_ISP_MODULE_AWB) ||
	    (module_cfg_update & RKISP1_CIF_ISP_MODULE_AWB)) {
		/* update awb config */
		if ((module_cfg_update & RKISP1_CIF_ISP_MODULE_AWB))
			rkisp1_awb_meas_config(params,
					&new_params->meas.awb_meas_config);

		if (module_en_update & RKISP1_CIF_ISP_MODULE_AWB)
			rkisp1_awb_meas_enable(params,
				&new_params->meas.awb_meas_config,
				!!(module_ens & RKISP1_CIF_ISP_MODULE_AWB));
	}

	if ((module_en_update & RKISP1_CIF_ISP_MODULE_AFC) ||
	    (module_cfg_update & RKISP1_CIF_ISP_MODULE_AFC)) {
		/* update afc config */
		if ((module_cfg_update & RKISP1_CIF_ISP_MODULE_AFC))
			rkisp1_afm_config(params,
					  &new_params->meas.afc_config);

		if (module_en_update & RKISP1_CIF_ISP_MODULE_AFC) {
			if (!!(module_ens & RKISP1_CIF_ISP_MODULE_AFC))
				rkisp1_param_set_bits(params,
						      RKISP1_CIF_ISP_AFM_CTRL,
						      RKISP1_CIF_ISP_AFM_ENA);
			else
				rkisp1_param_clear_bits(params,
							RKISP1_CIF_ISP_AFM_CTRL,
							RKISP1_CIF_ISP_AFM_ENA);
		}
	}

	if ((module_en_update & RKISP1_CIF_ISP_MODULE_HST) ||
	    (module_cfg_update & RKISP1_CIF_ISP_MODULE_HST)) {
		/* update hst config */
		if ((module_cfg_update & RKISP1_CIF_ISP_MODULE_HST))
			rkisp1_hst_config(params,
					  &new_params->meas.hst_config);

		if (module_en_update & RKISP1_CIF_ISP_MODULE_HST)
			rkisp1_hst_enable(params,
				&new_params->meas.hst_config,
				!!(module_ens & RKISP1_CIF_ISP_MODULE_HST));
	}

	if ((module_en_update & RKISP1_CIF_ISP_MODULE_AEC) ||
	    (module_cfg_update & RKISP1_CIF_ISP_MODULE_AEC)) {
		/* update aec config */
		if ((module_cfg_update & RKISP1_CIF_ISP_MODULE_AEC))
			rkisp1_aec_config(params,
					  &new_params->meas.aec_config);

		if (module_en_update & RKISP1_CIF_ISP_MODULE_AEC) {
			if (!!(module_ens & RKISP1_CIF_ISP_MODULE_AEC))
				rkisp1_param_set_bits(params,
						      RKISP1_CIF_ISP_EXP_CTRL,
						      RKISP1_CIF_ISP_EXP_ENA);
			else
				rkisp1_param_clear_bits(params,
							RKISP1_CIF_ISP_EXP_CTRL,
							RKISP1_CIF_ISP_EXP_ENA);
		}
	}
}

void rkisp1_params_isr(struct rkisp1_device *rkisp1, u32 isp_mis)
{
	unsigned int frame_sequence = atomic_read(&rkisp1->isp.frame_sequence);
	struct rkisp1_params *params = &rkisp1->params;
	struct rkisp1_params_cfg *new_params;
	struct rkisp1_buffer *cur_buf = NULL;

	spin_lock(&params->config_lock);
	if (!params->is_streaming) {
		spin_unlock(&params->config_lock);
		return;
	}

	/* get one empty buffer */
	if (!list_empty(&params->params))
		cur_buf = list_first_entry(&params->params,
					   struct rkisp1_buffer, queue);
	spin_unlock(&params->config_lock);

	if (!cur_buf)
		return;

	new_params = (struct rkisp1_params_cfg *)(cur_buf->vaddr[0]);

	if (isp_mis & RKISP1_CIF_ISP_FRAME) {
		u32 isp_ctrl;

		rkisp1_isp_isr_other_config(params, new_params);
		rkisp1_isp_isr_meas_config(params, new_params);

		/* update shadow register immediately */
		isp_ctrl = rkisp1_read(params->rkisp1, RKISP1_CIF_ISP_CTRL);
		isp_ctrl |= RKISP1_CIF_ISP_CTRL_ISP_CFG_UPD;
		rkisp1_write(params->rkisp1, isp_ctrl, RKISP1_CIF_ISP_CTRL);

		spin_lock(&params->config_lock);
		list_del(&cur_buf->queue);
		spin_unlock(&params->config_lock);

		cur_buf->vb.sequence = frame_sequence;
		vb2_buffer_done(&cur_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}
}

static const struct rkisp1_cif_isp_awb_meas_config rkisp1_awb_params_default_config = {
	{
		0, 0, RKISP1_DEFAULT_WIDTH, RKISP1_DEFAULT_HEIGHT
	},
	RKISP1_CIF_ISP_AWB_MODE_YCBCR, 200, 30, 20, 20, 0, 128, 128
};

static const struct rkisp1_cif_isp_aec_config rkisp1_aec_params_default_config = {
	RKISP1_CIF_ISP_EXP_MEASURING_MODE_0,
	RKISP1_CIF_ISP_EXP_CTRL_AUTOSTOP_0,
	{
		RKISP1_DEFAULT_WIDTH >> 2, RKISP1_DEFAULT_HEIGHT >> 2,
		RKISP1_DEFAULT_WIDTH >> 1, RKISP1_DEFAULT_HEIGHT >> 1
	}
};

static const struct rkisp1_cif_isp_hst_config rkisp1_hst_params_default_config = {
	RKISP1_CIF_ISP_HISTOGRAM_MODE_RGB_COMBINED,
	3,
	{
		RKISP1_DEFAULT_WIDTH >> 2, RKISP1_DEFAULT_HEIGHT >> 2,
		RKISP1_DEFAULT_WIDTH >> 1, RKISP1_DEFAULT_HEIGHT >> 1
	},
	{
		0, /* To be filled in with 0x01 at runtime. */
	}
};

static const struct rkisp1_cif_isp_afc_config rkisp1_afc_params_default_config = {
	1,
	{
		{
			300, 225, 200, 150
		}
	},
	4,
	14
};

static void rkisp1_params_config_parameter(struct rkisp1_params *params)
{
	struct rkisp1_cif_isp_hst_config hst = rkisp1_hst_params_default_config;

	spin_lock(&params->config_lock);

	rkisp1_awb_meas_config(params, &rkisp1_awb_params_default_config);
	rkisp1_awb_meas_enable(params, &rkisp1_awb_params_default_config,
			       true);

	rkisp1_aec_config(params, &rkisp1_aec_params_default_config);
	rkisp1_param_set_bits(params, RKISP1_CIF_ISP_EXP_CTRL,
			      RKISP1_CIF_ISP_EXP_ENA);

	rkisp1_afm_config(params, &rkisp1_afc_params_default_config);
	rkisp1_param_set_bits(params, RKISP1_CIF_ISP_AFM_CTRL,
			      RKISP1_CIF_ISP_AFM_ENA);

	memset(hst.hist_weight, 0x01, sizeof(hst.hist_weight));
	rkisp1_hst_config(params, &hst);
	rkisp1_param_set_bits(params, RKISP1_CIF_ISP_HIST_PROP,
			      ~RKISP1_CIF_ISP_HIST_PROP_MODE_MASK |
			      rkisp1_hst_params_default_config.mode);

	/* set the  range */
	if (params->quantization == V4L2_QUANTIZATION_FULL_RANGE)
		rkisp1_csm_config(params, true);
	else
		rkisp1_csm_config(params, false);

	/* override the default things */
	rkisp1_isp_isr_other_config(params, &params->cur_params);
	rkisp1_isp_isr_meas_config(params, &params->cur_params);

	spin_unlock(&params->config_lock);
}

/* Not called when the camera active, thus not isr protection. */
void rkisp1_params_configure(struct rkisp1_params *params,
			     enum rkisp1_fmt_raw_pat_type bayer_pat,
			     enum v4l2_quantization quantization)
{
	params->quantization = quantization;
	params->raw_type = bayer_pat;
	rkisp1_params_config_parameter(params);
}

/* Not called when the camera active, thus not isr protection. */
void rkisp1_params_disable(struct rkisp1_params *params)
{
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_DPCC_MODE,
				RKISP1_CIF_ISP_DPCC_ENA);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_LSC_CTRL,
				RKISP1_CIF_ISP_LSC_CTRL_ENA);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_BLS_CTRL,
				RKISP1_CIF_ISP_BLS_ENA);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_CTRL,
				RKISP1_CIF_ISP_CTRL_ISP_GAMMA_IN_ENA);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_CTRL,
				RKISP1_CIF_ISP_CTRL_ISP_GAMMA_OUT_ENA);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_DEMOSAIC,
				RKISP1_CIF_ISP_DEMOSAIC_BYPASS);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_FILT_MODE,
				RKISP1_CIF_ISP_FLT_ENA);
	rkisp1_awb_meas_enable(params, NULL, false);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_CTRL,
				RKISP1_CIF_ISP_CTRL_ISP_AWB_ENA);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_EXP_CTRL,
				RKISP1_CIF_ISP_EXP_ENA);
	rkisp1_ctk_enable(params, false);
	rkisp1_param_clear_bits(params, RKISP1_CIF_C_PROC_CTRL,
				RKISP1_CIF_C_PROC_CTR_ENABLE);
	rkisp1_hst_enable(params, NULL, false);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_AFM_CTRL,
				RKISP1_CIF_ISP_AFM_ENA);
	rkisp1_ie_enable(params, false);
	rkisp1_param_clear_bits(params, RKISP1_CIF_ISP_DPF_MODE,
				RKISP1_CIF_ISP_DPF_MODE_EN);
}

static int rkisp1_params_enum_fmt_meta_out(struct file *file, void *priv,
					   struct v4l2_fmtdesc *f)
{
	struct video_device *video = video_devdata(file);
	struct rkisp1_params *params = video_get_drvdata(video);

	if (f->index > 0 || f->type != video->queue->type)
		return -EINVAL;

	f->pixelformat = params->vdev_fmt.fmt.meta.dataformat;

	return 0;
}

static int rkisp1_params_g_fmt_meta_out(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct video_device *video = video_devdata(file);
	struct rkisp1_params *params = video_get_drvdata(video);
	struct v4l2_meta_format *meta = &f->fmt.meta;

	if (f->type != video->queue->type)
		return -EINVAL;

	memset(meta, 0, sizeof(*meta));
	meta->dataformat = params->vdev_fmt.fmt.meta.dataformat;
	meta->buffersize = params->vdev_fmt.fmt.meta.buffersize;

	return 0;
}

static int rkisp1_params_querycap(struct file *file,
				  void *priv, struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);

	strscpy(cap->driver, RKISP1_DRIVER_NAME, sizeof(cap->driver));
	strscpy(cap->card, vdev->name, sizeof(cap->card));
	strscpy(cap->bus_info, RKISP1_BUS_INFO, sizeof(cap->bus_info));

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
	.vidioc_querycap = rkisp1_params_querycap,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static int rkisp1_params_vb2_queue_setup(struct vb2_queue *vq,
					 unsigned int *num_buffers,
					 unsigned int *num_planes,
					 unsigned int sizes[],
					 struct device *alloc_devs[])
{
	struct rkisp1_params *params = vq->drv_priv;

	*num_buffers = clamp_t(u32, *num_buffers,
			       RKISP1_ISP_PARAMS_REQ_BUFS_MIN,
			       RKISP1_ISP_PARAMS_REQ_BUFS_MAX);

	*num_planes = 1;

	sizes[0] = sizeof(struct rkisp1_params_cfg);

	INIT_LIST_HEAD(&params->params);
	params->is_first_params = true;

	return 0;
}

static void rkisp1_params_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkisp1_buffer *params_buf =
		container_of(vbuf, struct rkisp1_buffer, vb);
	struct vb2_queue *vq = vb->vb2_queue;
	struct rkisp1_params *params = vq->drv_priv;
	struct rkisp1_params_cfg *new_params;
	unsigned long flags;
	unsigned int frame_sequence =
		atomic_read(&params->rkisp1->isp.frame_sequence);

	if (params->is_first_params) {
		new_params = (struct rkisp1_params_cfg *)
			(vb2_plane_vaddr(vb, 0));
		vbuf->sequence = frame_sequence;
		vb2_buffer_done(&params_buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		params->is_first_params = false;
		params->cur_params = *new_params;
		return;
	}

	params_buf->vaddr[0] = vb2_plane_vaddr(vb, 0);
	spin_lock_irqsave(&params->config_lock, flags);
	list_add_tail(&params_buf->queue, &params->params);
	spin_unlock_irqrestore(&params->config_lock, flags);
}

static int rkisp1_params_vb2_buf_prepare(struct vb2_buffer *vb)
{
	if (vb2_plane_size(vb, 0) < sizeof(struct rkisp1_params_cfg))
		return -EINVAL;

	vb2_set_plane_payload(vb, 0, sizeof(struct rkisp1_params_cfg));

	return 0;
}

static void rkisp1_params_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct rkisp1_params *params = vq->drv_priv;
	struct rkisp1_buffer *buf;
	unsigned long flags;
	unsigned int i;

	/* stop params input firstly */
	spin_lock_irqsave(&params->config_lock, flags);
	params->is_streaming = false;
	spin_unlock_irqrestore(&params->config_lock, flags);

	for (i = 0; i < RKISP1_ISP_PARAMS_REQ_BUFS_MAX; i++) {
		spin_lock_irqsave(&params->config_lock, flags);
		if (!list_empty(&params->params)) {
			buf = list_first_entry(&params->params,
					       struct rkisp1_buffer, queue);
			list_del(&buf->queue);
			spin_unlock_irqrestore(&params->config_lock,
					       flags);
		} else {
			spin_unlock_irqrestore(&params->config_lock,
					       flags);
			break;
		}

		if (buf)
			vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		buf = NULL;
	}
}

static int
rkisp1_params_vb2_start_streaming(struct vb2_queue *queue, unsigned int count)
{
	struct rkisp1_params *params = queue->drv_priv;
	unsigned long flags;

	spin_lock_irqsave(&params->config_lock, flags);
	params->is_streaming = true;
	spin_unlock_irqrestore(&params->config_lock, flags);

	return 0;
}

static struct vb2_ops rkisp1_params_vb2_ops = {
	.queue_setup = rkisp1_params_vb2_queue_setup,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.buf_queue = rkisp1_params_vb2_buf_queue,
	.buf_prepare = rkisp1_params_vb2_buf_prepare,
	.start_streaming = rkisp1_params_vb2_start_streaming,
	.stop_streaming = rkisp1_params_vb2_stop_streaming,

};

static struct v4l2_file_operations rkisp1_params_fops = {
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.open = v4l2_fh_open,
	.release = vb2_fop_release
};

static int rkisp1_params_init_vb2_queue(struct vb2_queue *q,
					struct rkisp1_params *params)
{
	struct rkisp1_vdev_node *node;

	node = container_of(q, struct rkisp1_vdev_node, buf_queue);

	q->type = V4L2_BUF_TYPE_META_OUTPUT;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	q->drv_priv = params;
	q->ops = &rkisp1_params_vb2_ops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->buf_struct_size = sizeof(struct rkisp1_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &node->vlock;

	return vb2_queue_init(q);
}

static void rkisp1_init_params(struct rkisp1_params *params)
{
	params->vdev_fmt.fmt.meta.dataformat =
		V4L2_META_FMT_RK_ISP1_PARAMS;
	params->vdev_fmt.fmt.meta.buffersize =
		sizeof(struct rkisp1_params_cfg);
}

int rkisp1_params_register(struct rkisp1_params *params,
			   struct v4l2_device *v4l2_dev,
			   struct rkisp1_device *rkisp1)
{
	struct rkisp1_vdev_node *node = &params->vnode;
	struct video_device *vdev = &node->vdev;
	int ret;

	params->rkisp1 = rkisp1;
	mutex_init(&node->vlock);
	spin_lock_init(&params->config_lock);

	strscpy(vdev->name, RKISP1_PARAMS_DEV_NAME, sizeof(vdev->name));

	video_set_drvdata(vdev, params);
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
	rkisp1_params_init_vb2_queue(vdev->queue, params);
	rkisp1_init_params(params);
	video_set_drvdata(vdev, params);

	node->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&vdev->entity, 1, &node->pad);
	if (ret)
		goto err_release_queue;
	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(&vdev->dev,
			"failed to register %s, ret=%d\n", vdev->name, ret);
		goto err_cleanup_media_entity;
	}
	return 0;
err_cleanup_media_entity:
	media_entity_cleanup(&vdev->entity);
err_release_queue:
	vb2_queue_release(vdev->queue);
	return ret;
}

void rkisp1_params_unregister(struct rkisp1_params *params)
{
	struct rkisp1_vdev_node *node = &params->vnode;
	struct video_device *vdev = &node->vdev;

	video_unregister_device(vdev);
	media_entity_cleanup(&vdev->entity);
	vb2_queue_release(vdev->queue);
}

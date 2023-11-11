/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018 Intel Corporation */

#ifndef __IPU3_TABLES_H
#define __IPU3_TABLES_H

#include <linux/bitops.h>

#include "ipu3-abi.h"

#define IMGU_BDS_GRANULARITY		32	/* Downscaling granularity */
#define IMGU_BDS_MIN_SF_INV		IMGU_BDS_GRANULARITY
#define IMGU_BDS_CONFIG_LEN		97

#define IMGU_SCALER_DOWNSCALE_4TAPS_LEN	128
#define IMGU_SCALER_DOWNSCALE_2TAPS_LEN	64
#define IMGU_SCALER_FP			BIT(31) /* 1.0 in fixed point */

#define IMGU_XNR3_VMEM_LUT_LEN		16

#define IMGU_GDC_LUT_UNIT		4
#define IMGU_GDC_LUT_LEN		256

struct imgu_css_bds_config {
	struct imgu_abi_bds_phase_arr hor_phase_arr;
	struct imgu_abi_bds_phase_arr ver_phase_arr;
	struct imgu_abi_bds_ptrn_arr ptrn_arr;
	u16 sample_patrn_length;
	u8 hor_ds_en;
	u8 ver_ds_en;
};

struct imgu_css_xnr3_vmem_defaults {
	s16 x[IMGU_XNR3_VMEM_LUT_LEN];
	s16 a[IMGU_XNR3_VMEM_LUT_LEN];
	s16 b[IMGU_XNR3_VMEM_LUT_LEN];
	s16 c[IMGU_XNR3_VMEM_LUT_LEN];
};

extern const struct imgu_css_bds_config
			imgu_css_bds_configs[IMGU_BDS_CONFIG_LEN];
extern const s32 imgu_css_downscale_4taps[IMGU_SCALER_DOWNSCALE_4TAPS_LEN];
extern const s32 imgu_css_downscale_2taps[IMGU_SCALER_DOWNSCALE_2TAPS_LEN];
extern const s16 imgu_css_gdc_lut[IMGU_GDC_LUT_UNIT][IMGU_GDC_LUT_LEN];
extern const struct imgu_css_xnr3_vmem_defaults imgu_css_xnr3_vmem_defaults;
extern const struct ipu3_uapi_bnr_static_config imgu_css_bnr_defaults;
extern const struct ipu3_uapi_dm_config imgu_css_dm_defaults;
extern const struct ipu3_uapi_ccm_mat_config imgu_css_ccm_defaults;
extern const struct ipu3_uapi_gamma_corr_lut imgu_css_gamma_lut;
extern const struct ipu3_uapi_csc_mat_config imgu_css_csc_defaults;
extern const struct ipu3_uapi_cds_params imgu_css_cds_defaults;
extern const struct ipu3_uapi_shd_config_static imgu_css_shd_defaults;
extern const struct ipu3_uapi_yuvp1_iefd_config imgu_css_iefd_defaults;
extern const struct ipu3_uapi_yuvp1_yds_config imgu_css_yds_defaults;
extern const struct ipu3_uapi_yuvp1_chnr_config imgu_css_chnr_defaults;
extern const struct ipu3_uapi_yuvp1_y_ee_nr_config imgu_css_y_ee_nr_defaults;
extern const struct ipu3_uapi_yuvp2_tcc_gain_pcwl_lut_static_config
						imgu_css_tcc_gain_pcwl_lut;
extern const struct ipu3_uapi_yuvp2_tcc_r_sqr_lut_static_config
						imgu_css_tcc_r_sqr_lut;
extern const struct imgu_abi_anr_config imgu_css_anr_defaults;
extern const struct ipu3_uapi_awb_fr_config_s imgu_css_awb_fr_defaults;
extern const struct ipu3_uapi_ae_grid_config imgu_css_ae_grid_defaults;
extern const struct ipu3_uapi_ae_ccm imgu_css_ae_ccm_defaults;
extern const struct ipu3_uapi_af_config_s imgu_css_af_defaults;
extern const struct ipu3_uapi_awb_config_s imgu_css_awb_defaults;

#endif

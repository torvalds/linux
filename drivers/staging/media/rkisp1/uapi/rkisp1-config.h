/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Rockchip ISP1 userspace API
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 */

/*
 * TODO: Improve documentation, mostly regarding abbreviation and hardware
 * specificities. Reference: "REF_01 - ISP_user_manual, Rev 2.57" (not public)
 */

#ifndef _UAPI_RKISP1_CONFIG_H
#define _UAPI_RKISP1_CONFIG_H

#include <linux/types.h>

/* Vendor specific - used for RK_ISP1 camera sub-system */
#define V4L2_META_FMT_RK_ISP1_PARAMS   v4l2_fourcc('R', 'K', '1', 'P') /* Rockchip ISP1 params */
#define V4L2_META_FMT_RK_ISP1_STAT_3A  v4l2_fourcc('R', 'K', '1', 'S') /* Rockchip ISP1 3A statistics */

#define RKISP1_CIF_ISP_MODULE_DPCC		BIT(0)
#define RKISP1_CIF_ISP_MODULE_BLS		BIT(1)
#define RKISP1_CIF_ISP_MODULE_SDG		BIT(2)
#define RKISP1_CIF_ISP_MODULE_HST		BIT(3)
#define RKISP1_CIF_ISP_MODULE_LSC		BIT(4)
#define RKISP1_CIF_ISP_MODULE_AWB_GAIN		BIT(5)
#define RKISP1_CIF_ISP_MODULE_FLT		BIT(6)
#define RKISP1_CIF_ISP_MODULE_BDM		BIT(7)
#define RKISP1_CIF_ISP_MODULE_CTK		BIT(8)
#define RKISP1_CIF_ISP_MODULE_GOC		BIT(9)
#define RKISP1_CIF_ISP_MODULE_CPROC		BIT(10)
#define RKISP1_CIF_ISP_MODULE_AFC		BIT(11)
#define RKISP1_CIF_ISP_MODULE_AWB		BIT(12)
#define RKISP1_CIF_ISP_MODULE_IE		BIT(13)
#define RKISP1_CIF_ISP_MODULE_AEC		BIT(14)
#define RKISP1_CIF_ISP_MODULE_WDR		BIT(15)
#define RKISP1_CIF_ISP_MODULE_DPF		BIT(16)
#define RKISP1_CIF_ISP_MODULE_DPF_STRENGTH	BIT(17)

#define RKISP1_CIF_ISP_CTK_COEFF_MAX            0x100
#define RKISP1_CIF_ISP_CTK_OFFSET_MAX           0x800

#define RKISP1_CIF_ISP_AE_MEAN_MAX              25
#define RKISP1_CIF_ISP_HIST_BIN_N_MAX           16
#define RKISP1_CIF_ISP_AFM_MAX_WINDOWS          3
#define RKISP1_CIF_ISP_DEGAMMA_CURVE_SIZE       17

#define RKISP1_CIF_ISP_BDM_MAX_TH               0xff

/*
 * Black level compensation
 */
/* maximum value for horizontal start address */
#define RKISP1_CIF_ISP_BLS_START_H_MAX             0x00000fff
/* maximum value for horizontal stop address */
#define RKISP1_CIF_ISP_BLS_STOP_H_MAX              0x00000fff
/* maximum value for vertical start address */
#define RKISP1_CIF_ISP_BLS_START_V_MAX             0x00000fff
/* maximum value for vertical stop address */
#define RKISP1_CIF_ISP_BLS_STOP_V_MAX              0x00000fff
/* maximum is 2^18 = 262144*/
#define RKISP1_CIF_ISP_BLS_SAMPLES_MAX             0x00000012
/* maximum value for fixed black level */
#define RKISP1_CIF_ISP_BLS_FIX_SUB_MAX             0x00000fff
/* minimum value for fixed black level */
#define RKISP1_CIF_ISP_BLS_FIX_SUB_MIN             0xfffff000
/* 13 bit range (signed)*/
#define RKISP1_CIF_ISP_BLS_FIX_MASK                0x00001fff

/*
 * Automatic white balance measurments
 */
#define RKISP1_CIF_ISP_AWB_MAX_GRID                1
#define RKISP1_CIF_ISP_AWB_MAX_FRAMES              7

/*
 * Gamma out
 */
/* Maximum number of color samples supported */
#define RKISP1_CIF_ISP_GAMMA_OUT_MAX_SAMPLES       17

/*
 * Lens shade correction
 */
#define RKISP1_CIF_ISP_LSC_GRAD_TBL_SIZE           8
#define RKISP1_CIF_ISP_LSC_SIZE_TBL_SIZE           8
/*
 * The following matches the tuning process,
 * not the max capabilities of the chip.
 * Last value unused.
 */
#define	RKISP1_CIF_ISP_LSC_DATA_TBL_SIZE           290

/*
 * Histogram calculation
 */
/* Last 3 values unused. */
#define RKISP1_CIF_ISP_HISTOGRAM_WEIGHT_GRIDS_SIZE 28

/*
 * Defect Pixel Cluster Correction
 */
#define RKISP1_CIF_ISP_DPCC_METHODS_MAX       3

/*
 * Denoising pre filter
 */
#define RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS      17
#define RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS  6

/*
 * Measurement types
 */
#define RKISP1_CIF_ISP_STAT_AWB           BIT(0)
#define RKISP1_CIF_ISP_STAT_AUTOEXP       BIT(1)
#define RKISP1_CIF_ISP_STAT_AFM_FIN       BIT(2)
#define RKISP1_CIF_ISP_STAT_HIST          BIT(3)

enum rkisp1_cif_isp_histogram_mode {
	RKISP1_CIF_ISP_HISTOGRAM_MODE_DISABLE,
	RKISP1_CIF_ISP_HISTOGRAM_MODE_RGB_COMBINED,
	RKISP1_CIF_ISP_HISTOGRAM_MODE_R_HISTOGRAM,
	RKISP1_CIF_ISP_HISTOGRAM_MODE_G_HISTOGRAM,
	RKISP1_CIF_ISP_HISTOGRAM_MODE_B_HISTOGRAM,
	RKISP1_CIF_ISP_HISTOGRAM_MODE_Y_HISTOGRAM
};

enum rkisp1_cif_isp_awb_mode_type {
	RKISP1_CIF_ISP_AWB_MODE_MANUAL,
	RKISP1_CIF_ISP_AWB_MODE_RGB,
	RKISP1_CIF_ISP_AWB_MODE_YCBCR
};

enum rkisp1_cif_isp_flt_mode {
	RKISP1_CIF_ISP_FLT_STATIC_MODE,
	RKISP1_CIF_ISP_FLT_DYNAMIC_MODE
};

/**
 * enum rkisp1_cif_isp_exp_ctrl_autostop - stop modes
 * @RKISP1_CIF_ISP_EXP_CTRL_AUTOSTOP_0: continuous measurement
 * @RKISP1_CIF_ISP_EXP_CTRL_AUTOSTOP_1: stop measuring after a complete frame
 */
enum rkisp1_cif_isp_exp_ctrl_autostop {
	RKISP1_CIF_ISP_EXP_CTRL_AUTOSTOP_0 = 0,
	RKISP1_CIF_ISP_EXP_CTRL_AUTOSTOP_1 = 1,
};

/**
 * enum rkisp1_cif_isp_exp_meas_mode - Exposure measure mode
 * @RKISP1_CIF_ISP_EXP_MEASURING_MODE_0: Y = 16 + 0.25R + 0.5G + 0.1094B
 * @RKISP1_CIF_ISP_EXP_MEASURING_MODE_1: Y = (R + G + B) x (85/256)
 */
enum rkisp1_cif_isp_exp_meas_mode {
	RKISP1_CIF_ISP_EXP_MEASURING_MODE_0,
	RKISP1_CIF_ISP_EXP_MEASURING_MODE_1,
};

/*---------- PART1: Input Parameters ------------*/

struct rkisp1_cif_isp_window {
	__u16 h_offs;
	__u16 v_offs;
	__u16 h_size;
	__u16 v_size;
} __packed;

/**
 * struct rkisp1_cif_isp_bls_fixed_val - BLS fixed subtraction values
 *
 * The values will be subtracted from the sensor
 * values. Therefore a negative value means addition instead of subtraction!
 *
 * @r: Fixed (signed!) subtraction value for Bayer pattern R
 * @gr: Fixed (signed!) subtraction value for Bayer pattern Gr
 * @gb: Fixed (signed!) subtraction value for Bayer pattern Gb
 * @b: Fixed (signed!) subtraction value for Bayer pattern B
 */
struct rkisp1_cif_isp_bls_fixed_val {
	__s16 r;
	__s16 gr;
	__s16 gb;
	__s16 b;
} __packed;

/**
 * struct rkisp1_cif_isp_bls_config - Configuration used by black level subtraction
 *
 * @enable_auto: Automatic mode activated means that the measured values
 *		 are subtracted. Otherwise the fixed subtraction
 *		 values will be subtracted.
 * @en_windows: enabled window
 * @bls_window1: Measurement window 1 size
 * @bls_window2: Measurement window 2 size
 * @bls_samples: Set amount of measured pixels for each Bayer position
 *		 (A, B,C and D) to 2^bls_samples.
 * @fixed_val: Fixed subtraction values
 */
struct rkisp1_cif_isp_bls_config {
	__u8 enable_auto;
	__u8 en_windows;
	struct rkisp1_cif_isp_window bls_window1;
	struct rkisp1_cif_isp_window bls_window2;
	__u8 bls_samples;
	struct rkisp1_cif_isp_bls_fixed_val fixed_val;
} __packed;

/**
 * struct rkisp1_cif_isp_dpcc_methods_config - Methods Configuration used by DPCC
 *
 * Methods Configuration used by Defect Pixel Cluster Correction
 *
 * @method: Method enable bits
 * @line_thresh: Line threshold
 * @line_mad_fac: Line MAD factor
 * @pg_fac: Peak gradient factor
 * @rnd_thresh: Rank Neighbor Difference threshold
 * @rg_fac: Rank gradient factor
 */
struct rkisp1_cif_isp_dpcc_methods_config {
	__u32 method;
	__u32 line_thresh;
	__u32 line_mad_fac;
	__u32 pg_fac;
	__u32 rnd_thresh;
	__u32 rg_fac;
} __packed;

/**
 * struct rkisp1_cif_isp_dpcc_config - Configuration used by DPCC
 *
 * Configuration used by Defect Pixel Cluster Correction
 *
 * @mode: dpcc output mode
 * @output_mode: whether use hard coded methods
 * @set_use: stage1 methods set
 * @methods: methods config
 * @ro_limits: rank order limits
 * @rnd_offs: differential rank offsets for rank neighbor difference
 */
struct rkisp1_cif_isp_dpcc_config {
	__u32 mode;
	__u32 output_mode;
	__u32 set_use;
	struct rkisp1_cif_isp_dpcc_methods_config methods[RKISP1_CIF_ISP_DPCC_METHODS_MAX];
	__u32 ro_limits;
	__u32 rnd_offs;
} __packed;

struct rkisp1_cif_isp_gamma_corr_curve {
	__u16 gamma_y[RKISP1_CIF_ISP_DEGAMMA_CURVE_SIZE];
} __packed;

struct rkisp1_cif_isp_gamma_curve_x_axis_pnts {
	__u32 gamma_dx0;
	__u32 gamma_dx1;
} __packed;

/**
 * struct rkisp1_cif_isp_sdg_config - Configuration used by sensor degamma
 *
 * @curve_x: gamma curve point definition axis for x
 * @xa_pnts: x increments
 */
struct rkisp1_cif_isp_sdg_config {
	struct rkisp1_cif_isp_gamma_corr_curve curve_r;
	struct rkisp1_cif_isp_gamma_corr_curve curve_g;
	struct rkisp1_cif_isp_gamma_corr_curve curve_b;
	struct rkisp1_cif_isp_gamma_curve_x_axis_pnts xa_pnts;
} __packed;

/**
 * struct rkisp1_cif_isp_lsc_config - Configuration used by Lens shading correction
 *
 * refer to REF_01 for details
 */
struct rkisp1_cif_isp_lsc_config {
	__u32 r_data_tbl[RKISP1_CIF_ISP_LSC_DATA_TBL_SIZE];
	__u32 gr_data_tbl[RKISP1_CIF_ISP_LSC_DATA_TBL_SIZE];
	__u32 gb_data_tbl[RKISP1_CIF_ISP_LSC_DATA_TBL_SIZE];
	__u32 b_data_tbl[RKISP1_CIF_ISP_LSC_DATA_TBL_SIZE];

	__u32 x_grad_tbl[RKISP1_CIF_ISP_LSC_GRAD_TBL_SIZE];
	__u32 y_grad_tbl[RKISP1_CIF_ISP_LSC_GRAD_TBL_SIZE];

	__u32 x_size_tbl[RKISP1_CIF_ISP_LSC_SIZE_TBL_SIZE];
	__u32 y_size_tbl[RKISP1_CIF_ISP_LSC_SIZE_TBL_SIZE];
	__u16 config_width;
	__u16 config_height;
} __packed;

/**
 * struct rkisp1_cif_isp_ie_config - Configuration used by image effects
 *
 * @eff_mat_1: 3x3 Matrix Coefficients for Emboss Effect 1
 * @eff_mat_2: 3x3 Matrix Coefficients for Emboss Effect 2
 * @eff_mat_3: 3x3 Matrix Coefficients for Emboss 3/Sketch 1
 * @eff_mat_4: 3x3 Matrix Coefficients for Sketch Effect 2
 * @eff_mat_5: 3x3 Matrix Coefficients for Sketch Effect 3
 * @eff_tint: Chrominance increment values of tint (used for sepia effect)
 */
struct rkisp1_cif_isp_ie_config {
	__u16 effect;
	__u16 color_sel;
	__u16 eff_mat_1;
	__u16 eff_mat_2;
	__u16 eff_mat_3;
	__u16 eff_mat_4;
	__u16 eff_mat_5;
	__u16 eff_tint;
} __packed;

/**
 * struct rkisp1_cif_isp_cproc_config - Configuration used by Color Processing
 *
 * @c_out_range: Chrominance pixel clipping range at output.
 *		 (0 for limit, 1 for full)
 * @y_in_range: Luminance pixel clipping range at output.
 * @y_out_range: Luminance pixel clipping range at output.
 * @contrast: 00~ff, 0.0~1.992
 * @brightness: 80~7F, -128~+127
 * @sat: saturation, 00~FF, 0.0~1.992
 * @hue: 80~7F, -90~+87.188
 */
struct rkisp1_cif_isp_cproc_config {
	__u8 c_out_range;
	__u8 y_in_range;
	__u8 y_out_range;
	__u8 contrast;
	__u8 brightness;
	__u8 sat;
	__u8 hue;
} __packed;

/**
 * struct rkisp1_cif_isp_awb_meas_config - Configuration used by auto white balance
 *
 * @awb_wnd: white balance measurement window (in pixels)
 *	     (from enum rkisp1_cif_isp_awb_mode_type)
 * @max_y: only pixels values < max_y contribute to awb measurement, set to 0
 *	   to disable this feature
 * @min_y: only pixels values > min_y contribute to awb measurement
 * @max_csum: Chrominance sum maximum value, only consider pixels with Cb+Cr,
 *	      smaller than threshold for awb measurements
 * @min_c: Chrominance minimum value, only consider pixels with Cb/Cr
 *	   each greater than threshold value for awb measurements
 * @frames: number of frames - 1 used for mean value calculation
 *	    (ucFrames=0 means 1 Frame)
 * @awb_ref_cr: reference Cr value for AWB regulation, target for AWB
 * @awb_ref_cb: reference Cb value for AWB regulation, target for AWB
 */
struct rkisp1_cif_isp_awb_meas_config {
	/*
	 * Note: currently the h and v offsets are mapped to grid offsets
	 */
	struct rkisp1_cif_isp_window awb_wnd;
	__u32 awb_mode;
	__u8 max_y;
	__u8 min_y;
	__u8 max_csum;
	__u8 min_c;
	__u8 frames;
	__u8 awb_ref_cr;
	__u8 awb_ref_cb;
	__u8 enable_ymax_cmp;
} __packed;

/**
 * struct rkisp1_cif_isp_awb_gain_config - Configuration used by auto white balance gain
 *
 * out_data_x = ( AWB_GEAIN_X * in_data + 128) >> 8
 */
struct rkisp1_cif_isp_awb_gain_config {
	__u16 gain_red;
	__u16 gain_green_r;
	__u16 gain_blue;
	__u16 gain_green_b;
} __packed;

/**
 * struct rkisp1_cif_isp_flt_config - Configuration used by ISP filtering
 *
 * @mode: ISP_FILT_MODE register fields (from enum rkisp1_cif_isp_flt_mode)
 * @grn_stage1: ISP_FILT_MODE register fields
 * @chr_h_mode: ISP_FILT_MODE register fields
 * @chr_v_mode: ISP_FILT_MODE register fields
 *
 * refer to REF_01 for details.
 */

struct rkisp1_cif_isp_flt_config {
	__u32 mode;
	__u8 grn_stage1;
	__u8 chr_h_mode;
	__u8 chr_v_mode;
	__u32 thresh_bl0;
	__u32 thresh_bl1;
	__u32 thresh_sh0;
	__u32 thresh_sh1;
	__u32 lum_weight;
	__u32 fac_sh1;
	__u32 fac_sh0;
	__u32 fac_mid;
	__u32 fac_bl0;
	__u32 fac_bl1;
} __packed;

/**
 * struct rkisp1_cif_isp_bdm_config - Configuration used by Bayer DeMosaic
 *
 * @demosaic_th: threshod for bayer demosaicing texture detection
 */
struct rkisp1_cif_isp_bdm_config {
	__u8 demosaic_th;
} __packed;

/**
 * struct rkisp1_cif_isp_ctk_config - Configuration used by Cross Talk correction
 *
 * @coeff: color correction matrix
 * @ct_offset_b: offset for the crosstalk correction matrix
 */
struct rkisp1_cif_isp_ctk_config {
	__u16 coeff0;
	__u16 coeff1;
	__u16 coeff2;
	__u16 coeff3;
	__u16 coeff4;
	__u16 coeff5;
	__u16 coeff6;
	__u16 coeff7;
	__u16 coeff8;
	__u16 ct_offset_r;
	__u16 ct_offset_g;
	__u16 ct_offset_b;
} __packed;

enum rkisp1_cif_isp_goc_mode {
	RKISP1_CIF_ISP_GOC_MODE_LOGARITHMIC,
	RKISP1_CIF_ISP_GOC_MODE_EQUIDISTANT
};

/**
 * struct rkisp1_cif_isp_goc_config - Configuration used by Gamma Out correction
 *
 * @mode: goc mode (from enum rkisp1_cif_isp_goc_mode)
 * @gamma_y: gamma out curve y-axis for all color components
 */
struct rkisp1_cif_isp_goc_config {
	__u32 mode;
	__u16 gamma_y[RKISP1_CIF_ISP_GAMMA_OUT_MAX_SAMPLES];
} __packed;

/**
 * struct rkisp1_cif_isp_hst_config - Configuration used by Histogram
 *
 * @mode: histogram mode (from enum rkisp1_cif_isp_histogram_mode)
 * @histogram_predivider: process every stepsize pixel, all other pixels are
 *			  skipped
 * @meas_window: coordinates of the measure window
 * @hist_weight: weighting factor for sub-windows
 */
struct rkisp1_cif_isp_hst_config {
	__u32 mode;
	__u8 histogram_predivider;
	struct rkisp1_cif_isp_window meas_window;
	__u8 hist_weight[RKISP1_CIF_ISP_HISTOGRAM_WEIGHT_GRIDS_SIZE];
} __packed;

/**
 * struct rkisp1_cif_isp_aec_config - Configuration used by Auto Exposure Control
 *
 * @mode: Exposure measure mode (from enum rkisp1_cif_isp_exp_meas_mode)
 * @autostop: stop mode (from enum rkisp1_cif_isp_exp_ctrl_autostop)
 * @meas_window: coordinates of the measure window
 */
struct rkisp1_cif_isp_aec_config {
	__u32 mode;
	__u32 autostop;
	struct rkisp1_cif_isp_window meas_window;
} __packed;

/**
 * struct rkisp1_cif_isp_afc_config - Configuration used by Auto Focus Control
 *
 * @num_afm_win: max RKISP1_CIF_ISP_AFM_MAX_WINDOWS
 * @afm_win: coordinates of the meas window
 * @thres: threshold used for minimizing the influence of noise
 * @var_shift: the number of bits for the shift operation at the end of the
 *	       calculation chain.
 */
struct rkisp1_cif_isp_afc_config {
	__u8 num_afm_win;
	struct rkisp1_cif_isp_window afm_win[RKISP1_CIF_ISP_AFM_MAX_WINDOWS];
	__u32 thres;
	__u32 var_shift;
} __packed;

/**
 * enum rkisp1_cif_isp_dpf_gain_usage - dpf gain usage
 * @RKISP1_CIF_ISP_DPF_GAIN_USAGE_DISABLED: don't use any gains in preprocessing stage
 * @RKISP1_CIF_ISP_DPF_GAIN_USAGE_NF_GAINS: use only the noise function gains from
 *				    registers DPF_NF_GAIN_R, ...
 * @RKISP1_CIF_ISP_DPF_GAIN_USAGE_LSC_GAINS:  use only the gains from LSC module
 * @RKISP1_CIF_ISP_DPF_GAIN_USAGE_NF_LSC_GAINS: use the noise function gains and the
 *					gains from LSC module
 * @RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_GAINS: use only the gains from AWB module
 * @RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_LSC_GAINS: use the gains from AWB and LSC module
 * @RKISP1_CIF_ISP_DPF_GAIN_USAGE_MAX: upper border (only for an internal evaluation)
 */
enum rkisp1_cif_isp_dpf_gain_usage {
	RKISP1_CIF_ISP_DPF_GAIN_USAGE_DISABLED,
	RKISP1_CIF_ISP_DPF_GAIN_USAGE_NF_GAINS,
	RKISP1_CIF_ISP_DPF_GAIN_USAGE_LSC_GAINS,
	RKISP1_CIF_ISP_DPF_GAIN_USAGE_NF_LSC_GAINS,
	RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_GAINS,
	RKISP1_CIF_ISP_DPF_GAIN_USAGE_AWB_LSC_GAINS,
	RKISP1_CIF_ISP_DPF_GAIN_USAGE_MAX
};

/**
 * enum rkisp1_cif_isp_dpf_rb_filtersize - Red and blue filter sizes
 * @RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_13x9: red and blue filter kernel size 13x9
 *				   (means 7x5 active pixel)
 * @RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_9x9: red and blue filter kernel size 9x9
 *				   (means 5x5 active pixel)
 */
enum rkisp1_cif_isp_dpf_rb_filtersize {
	RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_13x9,
	RKISP1_CIF_ISP_DPF_RB_FILTERSIZE_9x9,
};

/**
 * enum rkisp1_cif_isp_dpf_nll_scale_mode - dpf noise level scale mode
 * @RKISP1_CIF_ISP_NLL_SCALE_LINEAR: use a linear scaling
 * @RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC: use a logarithmic scaling
 */
enum rkisp1_cif_isp_dpf_nll_scale_mode {
	RKISP1_CIF_ISP_NLL_SCALE_LINEAR,
	RKISP1_CIF_ISP_NLL_SCALE_LOGARITHMIC,
};

/**
 * struct rkisp1_cif_isp_dpf_nll - Noise level lookup
 *
 * @coeff: Noise level Lookup coefficient
 * @scale_mode: dpf noise level scale mode (from enum rkisp1_cif_isp_dpf_nll_scale_mode)
 */
struct rkisp1_cif_isp_dpf_nll {
	__u16 coeff[RKISP1_CIF_ISP_DPF_MAX_NLF_COEFFS];
	__u32 scale_mode;
} __packed;

/**
 * struct rkisp1_cif_isp_dpf_rb_flt - Red blue filter config
 *
 * @fltsize: The filter size for the red and blue pixels
 *	     (from enum rkisp1_cif_isp_dpf_rb_filtersize)
 * @spatial_coeff: Spatial weights
 * @r_enable: enable filter processing for red pixels
 * @b_enable: enable filter processing for blue pixels
 */
struct rkisp1_cif_isp_dpf_rb_flt {
	__u32 fltsize;
	__u8 spatial_coeff[RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS];
	__u8 r_enable;
	__u8 b_enable;
} __packed;

/**
 * struct rkisp1_cif_isp_dpf_g_flt - Green filter Configuration
 *
 * @spatial_coeff: Spatial weights
 * @gr_enable: enable filter processing for green pixels in green/red lines
 * @gb_enable: enable filter processing for green pixels in green/blue lines
 */
struct rkisp1_cif_isp_dpf_g_flt {
	__u8 spatial_coeff[RKISP1_CIF_ISP_DPF_MAX_SPATIAL_COEFFS];
	__u8 gr_enable;
	__u8 gb_enable;
} __packed;

/**
 * struct rkisp1_cif_isp_dpf_gain - Noise function Configuration
 *
 * @mode: dpf gain usage  (from enum rkisp1_cif_isp_dpf_gain_usage)
 * @nf_r_gain: Noise function Gain that replaces the AWB gain for red pixels
 * @nf_b_gain: Noise function Gain that replaces the AWB gain for blue pixels
 * @nf_gr_gain: Noise function Gain that replaces the AWB gain
 *		for green pixels in a red line
 * @nf_gb_gain: Noise function Gain that replaces the AWB gain
 *		for green pixels in a blue line
 */
struct rkisp1_cif_isp_dpf_gain {
	__u32 mode;
	__u16 nf_r_gain;
	__u16 nf_b_gain;
	__u16 nf_gr_gain;
	__u16 nf_gb_gain;
} __packed;

/**
 * struct rkisp1_cif_isp_dpf_config - Configuration used by De-noising pre-filter
 *
 * @gain: noise function gain
 * @g_flt: green filter config
 * @rb_flt: red blue filter config
 * @nll: noise level lookup
 */
struct rkisp1_cif_isp_dpf_config {
	struct rkisp1_cif_isp_dpf_gain gain;
	struct rkisp1_cif_isp_dpf_g_flt g_flt;
	struct rkisp1_cif_isp_dpf_rb_flt rb_flt;
	struct rkisp1_cif_isp_dpf_nll nll;
} __packed;

/**
 * struct rkisp1_cif_isp_dpf_strength_config - strength of the filter
 *
 * @r: filter strength of the RED filter
 * @g: filter strength of the GREEN filter
 * @b: filter strength of the BLUE filter
 */
struct rkisp1_cif_isp_dpf_strength_config {
	__u8 r;
	__u8 g;
	__u8 b;
} __packed;

/**
 * struct rkisp1_cif_isp_isp_other_cfg - Parameters for some blocks in rockchip isp1
 *
 * @dpcc_config: Defect Pixel Cluster Correction config
 * @bls_config: Black Level Subtraction config
 * @sdg_config: sensor degamma config
 * @lsc_config: Lens Shade config
 * @awb_gain_config: Auto White balance gain config
 * @flt_config: filter config
 * @bdm_config: demosaic config
 * @ctk_config: cross talk config
 * @goc_config: gamma out config
 * @bls_config: black level subtraction config
 * @dpf_config: De-noising pre-filter config
 * @dpf_strength_config: dpf strength config
 * @cproc_config: color process config
 * @ie_config: image effects config
 */
struct rkisp1_cif_isp_isp_other_cfg {
	struct rkisp1_cif_isp_dpcc_config dpcc_config;
	struct rkisp1_cif_isp_bls_config bls_config;
	struct rkisp1_cif_isp_sdg_config sdg_config;
	struct rkisp1_cif_isp_lsc_config lsc_config;
	struct rkisp1_cif_isp_awb_gain_config awb_gain_config;
	struct rkisp1_cif_isp_flt_config flt_config;
	struct rkisp1_cif_isp_bdm_config bdm_config;
	struct rkisp1_cif_isp_ctk_config ctk_config;
	struct rkisp1_cif_isp_goc_config goc_config;
	struct rkisp1_cif_isp_dpf_config dpf_config;
	struct rkisp1_cif_isp_dpf_strength_config dpf_strength_config;
	struct rkisp1_cif_isp_cproc_config cproc_config;
	struct rkisp1_cif_isp_ie_config ie_config;
} __packed;

/**
 * struct rkisp1_cif_isp_isp_meas_cfg - Rockchip ISP1 Measure Parameters
 *
 * @awb_meas_config: auto white balance config
 * @hst_config: histogram config
 * @aec_config: auto exposure config
 * @afc_config: auto focus config
 */
struct rkisp1_cif_isp_isp_meas_cfg {
	struct rkisp1_cif_isp_awb_meas_config awb_meas_config;
	struct rkisp1_cif_isp_hst_config hst_config;
	struct rkisp1_cif_isp_aec_config aec_config;
	struct rkisp1_cif_isp_afc_config afc_config;
} __packed;

/**
 * struct rkisp1_params_cfg - Rockchip ISP1 Input Parameters Meta Data
 *
 * @module_en_update: mask the enable bits of which module should be updated
 * @module_ens: mask the enable value of each module, only update the module
 *		which correspond bit was set in module_en_update
 * @module_cfg_update: mask the config bits of which module should be updated
 * @meas: measurement config
 * @others: other config
 */
struct rkisp1_params_cfg {
	__u32 module_en_update;
	__u32 module_ens;
	__u32 module_cfg_update;

	struct rkisp1_cif_isp_isp_meas_cfg meas;
	struct rkisp1_cif_isp_isp_other_cfg others;
} __packed;

/*---------- PART2: Measurement Statistics ------------*/

/**
 * struct rkisp1_cif_isp_awb_meas - AWB measured values
 *
 * @cnt: White pixel count, number of "white pixels" found during last
 *	 measurement
 * @mean_y_or_g: Mean value of Y within window and frames,
 *		 Green if RGB is selected.
 * @mean_cb_or_b: Mean value of Cb within window and frames,
 *		  Blue if RGB is selected.
 * @mean_cr_or_r: Mean value of Cr within window and frames,
 *		  Red if RGB is selected.
 */
struct rkisp1_cif_isp_awb_meas {
	__u32 cnt;
	__u8 mean_y_or_g;
	__u8 mean_cb_or_b;
	__u8 mean_cr_or_r;
} __packed;

/**
 * struct rkisp1_cif_isp_awb_stat - statistics automatic white balance data
 *
 * @awb_mean: Mean measured data
 */
struct rkisp1_cif_isp_awb_stat {
	struct rkisp1_cif_isp_awb_meas awb_mean[RKISP1_CIF_ISP_AWB_MAX_GRID];
} __packed;

/**
 * struct rkisp1_cif_isp_bls_meas_val - BLS measured values
 *
 * @meas_r: Mean measured value for Bayer pattern R
 * @meas_gr: Mean measured value for Bayer pattern Gr
 * @meas_gb: Mean measured value for Bayer pattern Gb
 * @meas_b: Mean measured value for Bayer pattern B
 */
struct rkisp1_cif_isp_bls_meas_val {
	__u16 meas_r;
	__u16 meas_gr;
	__u16 meas_gb;
	__u16 meas_b;
} __packed;

/**
 * struct rkisp1_cif_isp_ae_stat - statistics auto exposure data
 *
 * @exp_mean: Mean luminance value of block xx
 * @bls_val:  BLS measured values
 *
 * Image is divided into 5x5 blocks.
 */
struct rkisp1_cif_isp_ae_stat {
	__u8 exp_mean[RKISP1_CIF_ISP_AE_MEAN_MAX];
	struct rkisp1_cif_isp_bls_meas_val bls_val;
} __packed;

/**
 * struct rkisp1_cif_isp_af_meas_val - AF measured values
 *
 * @sum: sharpness, refer to REF_01 for definition
 * @lum: luminance, refer to REF_01 for definition
 */
struct rkisp1_cif_isp_af_meas_val {
	__u32 sum;
	__u32 lum;
} __packed;

/**
 * struct rkisp1_cif_isp_af_stat - statistics auto focus data
 *
 * @window: AF measured value of window x
 *
 * The module measures the sharpness in 3 windows of selectable size via
 * register settings(ISP_AFM_*_A/B/C)
 */
struct rkisp1_cif_isp_af_stat {
	struct rkisp1_cif_isp_af_meas_val window[RKISP1_CIF_ISP_AFM_MAX_WINDOWS];
} __packed;

/**
 * struct rkisp1_cif_isp_hist_stat - statistics histogram data
 *
 * @hist_bins: measured bin counters
 *
 * Measurement window divided into 25 sub-windows, set
 * with ISP_HIST_XXX
 */
struct rkisp1_cif_isp_hist_stat {
	__u16 hist_bins[RKISP1_CIF_ISP_HIST_BIN_N_MAX];
} __packed;

/**
 * struct rkisp1_stat_buffer - Rockchip ISP1 Statistics Data
 *
 * @rkisp1_cif_isp_awb_stat: statistics data for automatic white balance
 * @rkisp1_cif_isp_ae_stat: statistics data for auto exposure
 * @rkisp1_cif_isp_af_stat: statistics data for auto focus
 * @rkisp1_cif_isp_hist_stat: statistics histogram data
 */
struct rkisp1_cif_isp_stat {
	struct rkisp1_cif_isp_awb_stat awb;
	struct rkisp1_cif_isp_ae_stat ae;
	struct rkisp1_cif_isp_af_stat af;
	struct rkisp1_cif_isp_hist_stat hist;
} __packed;

/**
 * struct rkisp1_stat_buffer - Rockchip ISP1 Statistics Meta Data
 *
 * @meas_type: measurement types (RKISP1_CIF_ISP_STAT_ definitions)
 * @frame_id: frame ID for sync
 * @params: statistics data
 */
struct rkisp1_stat_buffer {
	__u32 meas_type;
	__u32 frame_id;
	struct rkisp1_cif_isp_stat params;
} __packed;

#endif /* _UAPI_RKISP1_CONFIG_H */

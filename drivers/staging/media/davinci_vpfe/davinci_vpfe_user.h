/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2012 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Contributors:
 *      Manjunath Hadli <manjunath.hadli@ti.com>
 *      Prabhakar Lad <prabhakar.lad@ti.com>
 */

#ifndef _DAVINCI_VPFE_USER_H
#define _DAVINCI_VPFE_USER_H

#include <linux/types.h>
#include <linux/videodev2.h>

/*
 * Private IOCTL
 *
 * VIDIOC_VPFE_ISIF_S_RAW_PARAMS: Set raw params in isif
 * VIDIOC_VPFE_ISIF_G_RAW_PARAMS: Get raw params from isif
 * VIDIOC_VPFE_PRV_S_CONFIG: Set ipipe engine configuration
 * VIDIOC_VPFE_PRV_G_CONFIG: Get ipipe engine configuration
 * VIDIOC_VPFE_RSZ_S_CONFIG: Set resizer engine configuration
 * VIDIOC_VPFE_RSZ_G_CONFIG: Get resizer engine configuration
 */

#define VIDIOC_VPFE_ISIF_S_RAW_PARAMS \
	_IOW('V', BASE_VIDIOC_PRIVATE + 1,  struct vpfe_isif_raw_config)
#define VIDIOC_VPFE_ISIF_G_RAW_PARAMS \
	_IOR('V', BASE_VIDIOC_PRIVATE + 2, struct vpfe_isif_raw_config)
#define VIDIOC_VPFE_IPIPE_S_CONFIG \
	_IOWR('P', BASE_VIDIOC_PRIVATE + 3, struct vpfe_ipipe_config)
#define VIDIOC_VPFE_IPIPE_G_CONFIG \
	_IOWR('P', BASE_VIDIOC_PRIVATE + 4, struct vpfe_ipipe_config)
#define VIDIOC_VPFE_RSZ_S_CONFIG \
	_IOWR('R', BASE_VIDIOC_PRIVATE + 5, struct vpfe_rsz_config)
#define VIDIOC_VPFE_RSZ_G_CONFIG \
	_IOWR('R', BASE_VIDIOC_PRIVATE + 6, struct vpfe_rsz_config)

/*
 * Private Control's for ISIF
 */
#define VPFE_ISIF_CID_CRGAIN		(V4L2_CID_USER_BASE | 0xa001)
#define VPFE_ISIF_CID_CGRGAIN		(V4L2_CID_USER_BASE | 0xa002)
#define VPFE_ISIF_CID_CGBGAIN		(V4L2_CID_USER_BASE | 0xa003)
#define VPFE_ISIF_CID_CBGAIN		(V4L2_CID_USER_BASE | 0xa004)
#define VPFE_ISIF_CID_GAIN_OFFSET	(V4L2_CID_USER_BASE | 0xa005)

/*
 * Private Control's for ISIF and IPIPEIF
 */
#define VPFE_CID_DPCM_PREDICTOR		(V4L2_CID_USER_BASE | 0xa006)

/************************************************************************
 *   Vertical Defect Correction parameters
 ***********************************************************************/

/**
 * vertical defect correction methods
 */
enum vpfe_isif_vdfc_corr_mode {
	/* Defect level subtraction. Just fed through if saturating */
	VPFE_ISIF_VDFC_NORMAL,
	/**
	 * Defect level subtraction. Horizontal interpolation ((i-2)+(i+2))/2
	 * if data saturating
	 */
	VPFE_ISIF_VDFC_HORZ_INTERPOL_IF_SAT,
	/* Horizontal interpolation (((i-2)+(i+2))/2) */
	VPFE_ISIF_VDFC_HORZ_INTERPOL
};

/**
 * Max Size of the Vertical Defect Correction table
 */
#define VPFE_ISIF_VDFC_TABLE_SIZE	8

/**
 * Values used for shifting up the vdfc defect level
 */
enum vpfe_isif_vdfc_shift {
	/* No Shift */
	VPFE_ISIF_VDFC_NO_SHIFT,
	/* Shift by 1 bit */
	VPFE_ISIF_VDFC_SHIFT_1,
	/* Shift by 2 bit */
	VPFE_ISIF_VDFC_SHIFT_2,
	/* Shift by 3 bit */
	VPFE_ISIF_VDFC_SHIFT_3,
	/* Shift by 4 bit */
	VPFE_ISIF_VDFC_SHIFT_4
};

/**
 * Defect Correction (DFC) table entry
 */
struct vpfe_isif_vdfc_entry {
	/* vertical position of defect */
	unsigned short pos_vert;
	/* horizontal position of defect */
	unsigned short pos_horz;
	/**
	 * Defect level of Vertical line defect position. This is subtracted
	 * from the data at the defect position
	 */
	unsigned char level_at_pos;
	/**
	 * Defect level of the pixels upper than the vertical line defect.
	 * This is subtracted from the data
	 */
	unsigned char level_up_pixels;
	/**
	 * Defect level of the pixels lower than the vertical line defect.
	 * This is subtracted from the data
	 */
	unsigned char level_low_pixels;
};

/**
 * Structure for Defect Correction (DFC) parameter
 */
struct vpfe_isif_dfc {
	/* enable vertical defect correction */
	unsigned char en;
	/* Correction methods */
	enum vpfe_isif_vdfc_corr_mode corr_mode;
	/**
	 * 0 - whole line corrected, 1 - not
	 * pixels upper than the defect
	 */
	unsigned char corr_whole_line;
	/**
	 * defect level shift value. level_at_pos, level_upper_pos,
	 * and level_lower_pos can be shifted up by this value
	 */
	enum vpfe_isif_vdfc_shift def_level_shift;
	/* defect saturation level */
	unsigned short def_sat_level;
	/* number of vertical defects. Max is VPFE_ISIF_VDFC_TABLE_SIZE */
	short num_vdefects;
	/* VDFC table ptr */
	struct vpfe_isif_vdfc_entry table[VPFE_ISIF_VDFC_TABLE_SIZE];
};

/************************************************************************
 *   Digital/Black clamp or DC Subtract parameters
 ************************************************************************/
/**
 * Horizontal Black Clamp modes
 */
enum vpfe_isif_horz_bc_mode {
	/**
	 * Horizontal clamp disabled. Only vertical clamp
	 * value is subtracted
	 */
	VPFE_ISIF_HORZ_BC_DISABLE,
	/**
	 * Horizontal clamp value is calculated and subtracted
	 * from image data along with vertical clamp value
	 */
	VPFE_ISIF_HORZ_BC_CLAMP_CALC_ENABLED,
	/**
	 * Horizontal clamp value calculated from previous image
	 * is subtracted from image data along with vertical clamp
	 * value. How the horizontal clamp value for the first image
	 * is calculated in this case ???
	 */
	VPFE_ISIF_HORZ_BC_CLAMP_NOT_UPDATED
};

/**
 * Base window selection for Horizontal Black Clamp calculations
 */
enum vpfe_isif_horz_bc_base_win_sel {
	/* Select Most left window for bc calculation */
	VPFE_ISIF_SEL_MOST_LEFT_WIN,

	/* Select Most right window for bc calculation */
	VPFE_ISIF_SEL_MOST_RIGHT_WIN,
};

/* Size of window in horizontal direction for horizontal bc */
enum vpfe_isif_horz_bc_sz_h {
	VPFE_ISIF_HORZ_BC_SZ_H_2PIXELS,
	VPFE_ISIF_HORZ_BC_SZ_H_4PIXELS,
	VPFE_ISIF_HORZ_BC_SZ_H_8PIXELS,
	VPFE_ISIF_HORZ_BC_SZ_H_16PIXELS
};

/* Size of window in vertcal direction for vertical bc */
enum vpfe_isif_horz_bc_sz_v {
	VPFE_ISIF_HORZ_BC_SZ_H_32PIXELS,
	VPFE_ISIF_HORZ_BC_SZ_H_64PIXELS,
	VPFE_ISIF_HORZ_BC_SZ_H_128PIXELS,
	VPFE_ISIF_HORZ_BC_SZ_H_256PIXELS
};

/**
 * Structure for Horizontal Black Clamp config params
 */
struct vpfe_isif_horz_bclamp {
	/* horizontal clamp mode */
	enum vpfe_isif_horz_bc_mode mode;
	/**
	 * pixel value limit enable.
	 *  0 - limit disabled
	 *  1 - pixel value limited to 1023
	 */
	unsigned char clamp_pix_limit;
	/**
	 * Select most left or right window for clamp val
	 * calculation
	 */
	enum vpfe_isif_horz_bc_base_win_sel base_win_sel_calc;
	/* Window count per color for calculation. range 1-32 */
	unsigned char win_count_calc;
	/* Window start position - horizontal for calculation. 0 - 8191 */
	unsigned short win_start_h_calc;
	/* Window start position - vertical for calculation 0 - 8191 */
	unsigned short win_start_v_calc;
	/* Width of the sample window in pixels for calculation */
	enum vpfe_isif_horz_bc_sz_h win_h_sz_calc;
	/* Height of the sample window in pixels for calculation */
	enum vpfe_isif_horz_bc_sz_v win_v_sz_calc;
};

/**
 * Black Clamp vertical reset values
 */
enum vpfe_isif_vert_bc_reset_val_sel {
	/* Reset value used is the clamp value calculated */
	VPFE_ISIF_VERT_BC_USE_HORZ_CLAMP_VAL,
	/* Reset value used is reset_clamp_val configured */
	VPFE_ISIF_VERT_BC_USE_CONFIG_CLAMP_VAL,
	/* No update, previous image value is used */
	VPFE_ISIF_VERT_BC_NO_UPDATE
};

enum vpfe_isif_vert_bc_sz_h {
	VPFE_ISIF_VERT_BC_SZ_H_2PIXELS,
	VPFE_ISIF_VERT_BC_SZ_H_4PIXELS,
	VPFE_ISIF_VERT_BC_SZ_H_8PIXELS,
	VPFE_ISIF_VERT_BC_SZ_H_16PIXELS,
	VPFE_ISIF_VERT_BC_SZ_H_32PIXELS,
	VPFE_ISIF_VERT_BC_SZ_H_64PIXELS
};

/**
 * Structure for Vertical Black Clamp configuration params
 */
struct vpfe_isif_vert_bclamp {
	/* Reset value selection for vertical clamp calculation */
	enum vpfe_isif_vert_bc_reset_val_sel reset_val_sel;
	/* U12 value if reset_sel = ISIF_BC_VERT_USE_CONFIG_CLAMP_VAL */
	unsigned short reset_clamp_val;
	/**
	 * U8Q8. Line average coefficient used in vertical clamp
	 * calculation
	 */
	unsigned char line_ave_coef;
	/* Width in pixels of the optical black region used for calculation. */
	enum vpfe_isif_vert_bc_sz_h ob_h_sz_calc;
	/* Height of the optical black region for calculation */
	unsigned short ob_v_sz_calc;
	/* Optical black region start position - horizontal. 0 - 8191 */
	unsigned short ob_start_h;
	/* Optical black region start position - vertical 0 - 8191 */
	unsigned short ob_start_v;
};

/**
 * Structure for Black Clamp configuration params
 */
struct vpfe_isif_black_clamp {
	/**
	 * this offset value is added irrespective of the clamp
	 * enable status. S13
	 */
	unsigned short dc_offset;
	/**
	 * Enable black/digital clamp value to be subtracted
	 * from the image data
	 */
	unsigned char en;
	/**
	 * black clamp mode. same/separate clamp for 4 colors
	 * 0 - disable - same clamp value for all colors
	 * 1 - clamp value calculated separately for all colors
	 */
	unsigned char bc_mode_color;
	/* Vertical start position for bc subtraction */
	unsigned short vert_start_sub;
	/* Black clamp for horizontal direction */
	struct vpfe_isif_horz_bclamp horz;
	/* Black clamp for vertical direction */
	struct vpfe_isif_vert_bclamp vert;
};

/*************************************************************************
 ** Color Space Conversion (CSC)
 *************************************************************************/
/**
 * Number of Coefficient values used for CSC
 */
#define VPFE_ISIF_CSC_NUM_COEFF 16

struct float_8_bit {
	/* 8 bit integer part */
	__u8 integer;
	/* 8 bit decimal part */
	__u8 decimal;
};

struct float_16_bit {
	/* 16 bit integer part */
	__u16 integer;
	/* 16 bit decimal part */
	__u16 decimal;
};

/*************************************************************************
 **  Color Space Conversion parameters
 *************************************************************************/
/**
 * Structure used for CSC config params
 */
struct vpfe_isif_color_space_conv {
	/* Enable color space conversion */
	unsigned char en;
	/**
	 * csc coefficient table. S8Q5, M00 at index 0, M01 at index 1, and
	 * so forth
	 */
	struct float_8_bit coeff[VPFE_ISIF_CSC_NUM_COEFF];
};

enum vpfe_isif_datasft {
	/* No Shift */
	VPFE_ISIF_NO_SHIFT,
	/* 1 bit Shift */
	VPFE_ISIF_1BIT_SHIFT,
	/* 2 bit Shift */
	VPFE_ISIF_2BIT_SHIFT,
	/* 3 bit Shift */
	VPFE_ISIF_3BIT_SHIFT,
	/* 4 bit Shift */
	VPFE_ISIF_4BIT_SHIFT,
	/* 5 bit Shift */
	VPFE_ISIF_5BIT_SHIFT,
	/* 6 bit Shift */
	VPFE_ISIF_6BIT_SHIFT
};

#define VPFE_ISIF_LINEAR_TAB_SIZE		192
/*************************************************************************
 **  Linearization parameters
 *************************************************************************/
/**
 * Structure for Sensor data linearization
 */
struct vpfe_isif_linearize {
	/* Enable or Disable linearization of data */
	unsigned char en;
	/* Shift value applied */
	enum vpfe_isif_datasft corr_shft;
	/* scale factor applied U11Q10 */
	struct float_16_bit scale_fact;
	/* Size of the linear table */
	unsigned short table[VPFE_ISIF_LINEAR_TAB_SIZE];
};

/*************************************************************************
 **  ISIF Raw configuration parameters
 *************************************************************************/
enum vpfe_isif_fmt_mode {
	VPFE_ISIF_SPLIT,
	VPFE_ISIF_COMBINE
};

enum vpfe_isif_lnum {
	VPFE_ISIF_1LINE,
	VPFE_ISIF_2LINES,
	VPFE_ISIF_3LINES,
	VPFE_ISIF_4LINES
};

enum vpfe_isif_line {
	VPFE_ISIF_1STLINE,
	VPFE_ISIF_2NDLINE,
	VPFE_ISIF_3RDLINE,
	VPFE_ISIF_4THLINE
};

struct vpfe_isif_fmtplen {
	/**
	 * number of program entries for SET0, range 1 - 16
	 * when fmtmode is ISIF_SPLIT, 1 - 8 when fmtmode is
	 * ISIF_COMBINE
	 */
	unsigned short plen0;
	/**
	 * number of program entries for SET1, range 1 - 16
	 * when fmtmode is ISIF_SPLIT, 1 - 8 when fmtmode is
	 * ISIF_COMBINE
	 */
	unsigned short plen1;
	/**
	 * number of program entries for SET2, range 1 - 16
	 * when fmtmode is ISIF_SPLIT, 1 - 8 when fmtmode is
	 * ISIF_COMBINE
	 */
	unsigned short plen2;
	/**
	 * number of program entries for SET3, range 1 - 16
	 * when fmtmode is ISIF_SPLIT, 1 - 8 when fmtmode is
	 * ISIF_COMBINE
	 */
	unsigned short plen3;
};

struct vpfe_isif_fmt_cfg {
	/* Split or combine or line alternate */
	enum vpfe_isif_fmt_mode fmtmode;
	/* enable or disable line alternating mode */
	unsigned char ln_alter_en;
	/* Split/combine line number */
	enum vpfe_isif_lnum lnum;
	/* Address increment Range 1 - 16 */
	unsigned int addrinc;
};

struct vpfe_isif_fmt_addr_ptr {
	/* Initial address */
	unsigned int init_addr;
	/* output line number */
	enum vpfe_isif_line out_line;
};

struct vpfe_isif_fmtpgm_ap {
	/* program address pointer */
	unsigned char pgm_aptr;
	/* program address increment or decrement */
	unsigned char pgmupdt;
};

struct vpfe_isif_data_formatter {
	/* Enable/Disable data formatter */
	unsigned char en;
	/* data formatter configuration */
	struct vpfe_isif_fmt_cfg cfg;
	/* Formatter program entries length */
	struct vpfe_isif_fmtplen plen;
	/* first pixel in a line fed to formatter */
	unsigned short fmtrlen;
	/* HD interval for output line. Only valid when split line */
	unsigned short fmthcnt;
	/* formatter address pointers */
	struct vpfe_isif_fmt_addr_ptr fmtaddr_ptr[16];
	/* program enable/disable */
	unsigned char pgm_en[32];
	/* program address pointers */
	struct vpfe_isif_fmtpgm_ap fmtpgm_ap[32];
};

struct vpfe_isif_df_csc {
	/* Color Space Conversion configuration, 0 - csc, 1 - df */
	unsigned int df_or_csc;
	/* csc configuration valid if df_or_csc is 0 */
	struct vpfe_isif_color_space_conv csc;
	/* data formatter configuration valid if df_or_csc is 1 */
	struct vpfe_isif_data_formatter df;
	/* start pixel in a line at the input */
	unsigned int start_pix;
	/* number of pixels in input line */
	unsigned int num_pixels;
	/* start line at the input */
	unsigned int start_line;
	/* number of lines at the input */
	unsigned int num_lines;
};

struct vpfe_isif_gain_offsets_adj {
	/* Enable or Disable Gain adjustment for SDRAM data */
	unsigned char gain_sdram_en;
	/* Enable or Disable Gain adjustment for IPIPE data */
	unsigned char gain_ipipe_en;
	/* Enable or Disable Gain adjustment for H3A data */
	unsigned char gain_h3a_en;
	/* Enable or Disable Gain adjustment for SDRAM data */
	unsigned char offset_sdram_en;
	/* Enable or Disable Gain adjustment for IPIPE data */
	unsigned char offset_ipipe_en;
	/* Enable or Disable Gain adjustment for H3A data */
	unsigned char offset_h3a_en;
};

struct vpfe_isif_cul {
	/* Horizontal Cull pattern for odd lines */
	unsigned char hcpat_odd;
	/* Horizontal Cull pattern for even lines */
	unsigned char hcpat_even;
	/* Vertical Cull pattern */
	unsigned char vcpat;
	/* Enable or disable lpf. Apply when cull is enabled */
	unsigned char en_lpf;
};

/* all the stuff in this struct will be provided by userland */
struct vpfe_isif_raw_config {
	/* Linearization parameters for image sensor data input */
	struct vpfe_isif_linearize linearize;
	/* Data formatter or CSC */
	struct vpfe_isif_df_csc df_csc;
	/* Defect Pixel Correction (DFC) confguration */
	struct vpfe_isif_dfc dfc;
	/* Black/Digital Clamp configuration */
	struct vpfe_isif_black_clamp bclamp;
	/* Gain, offset adjustments */
	struct vpfe_isif_gain_offsets_adj gain_offset;
	/* Culling */
	struct vpfe_isif_cul culling;
	/* horizontal offset for Gain/LSC/DFC */
	unsigned short horz_offset;
	/* vertical offset for Gain/LSC/DFC */
	unsigned short vert_offset;
};

/**********************************************************************
 *	IPIPE API Structures
 **********************************************************************/

/* IPIPE module configurations */

/* IPIPE input configuration */
#define VPFE_IPIPE_INPUT_CONFIG		BIT(0)
/* LUT based Defect Pixel Correction */
#define VPFE_IPIPE_LUTDPC		BIT(1)
/* On the fly (OTF) Defect Pixel Correction */
#define VPFE_IPIPE_OTFDPC		BIT(2)
/* Noise Filter - 1 */
#define VPFE_IPIPE_NF1			BIT(3)
/* Noise Filter - 2 */
#define VPFE_IPIPE_NF2			BIT(4)
/* White Balance.  Also a control ID */
#define VPFE_IPIPE_WB			BIT(5)
/* 1st RGB to RBG Blend module */
#define VPFE_IPIPE_RGB2RGB_1		BIT(6)
/* 2nd RGB to RBG Blend module */
#define VPFE_IPIPE_RGB2RGB_2		BIT(7)
/* Gamma Correction */
#define VPFE_IPIPE_GAMMA		BIT(8)
/* 3D LUT color conversion */
#define VPFE_IPIPE_3D_LUT		BIT(9)
/* RGB to YCbCr module */
#define VPFE_IPIPE_RGB2YUV		BIT(10)
/* YUV 422 conversion module */
#define VPFE_IPIPE_YUV422_CONV		BIT(11)
/* Edge Enhancement */
#define VPFE_IPIPE_YEE			BIT(12)
/* Green Imbalance Correction */
#define VPFE_IPIPE_GIC			BIT(13)
/* CFA Interpolation */
#define VPFE_IPIPE_CFA			BIT(14)
/* Chroma Artifact Reduction */
#define VPFE_IPIPE_CAR			BIT(15)
/* Chroma Gain Suppression */
#define VPFE_IPIPE_CGS			BIT(16)
/* Global brightness and contrast control */
#define VPFE_IPIPE_GBCE			BIT(17)

#define VPFE_IPIPE_MAX_MODULES		18

struct ipipe_float_u16 {
	unsigned short integer;
	unsigned short decimal;
};

struct ipipe_float_s16 {
	short integer;
	unsigned short decimal;
};

struct ipipe_float_u8 {
	unsigned char integer;
	unsigned char decimal;
};

/* Copy method selection for vertical correction
 *  Used when ipipe_dfc_corr_meth is IPIPE_DPC_CTORB_AFTER_HINT
 */
enum vpfe_ipipe_dpc_corr_meth {
	/* replace by black or white dot specified by repl_white */
	VPFE_IPIPE_DPC_REPL_BY_DOT = 0,
	/* Copy from left */
	VPFE_IPIPE_DPC_CL = 1,
	/* Copy from right */
	VPFE_IPIPE_DPC_CR = 2,
	/* Horizontal interpolation */
	VPFE_IPIPE_DPC_H_INTP = 3,
	/* Vertical interpolation */
	VPFE_IPIPE_DPC_V_INTP = 4,
	/* Copy from top  */
	VPFE_IPIPE_DPC_CT = 5,
	/* Copy from bottom */
	VPFE_IPIPE_DPC_CB = 6,
	/* 2D interpolation */
	VPFE_IPIPE_DPC_2D_INTP = 7,
};

struct vpfe_ipipe_lutdpc_entry {
	/* Horizontal position */
	unsigned short horz_pos;
	/* vertical position */
	unsigned short vert_pos;
	enum vpfe_ipipe_dpc_corr_meth method;
};

#define VPFE_IPIPE_MAX_SIZE_DPC 256

/* Structure for configuring DPC module */
struct vpfe_ipipe_lutdpc {
	/* 0 - disable, 1 - enable */
	unsigned char en;
	/* 0 - replace with black dot, 1 - white dot when correction
	 * method is  IPIPE_DFC_REPL_BY_DOT=0,
	 */
	unsigned char repl_white;
	/* number of entries in the correction table. Currently only
	 * support up-to 256 entries. infinite mode is not supported
	 */
	unsigned short dpc_size;
	struct vpfe_ipipe_lutdpc_entry table[VPFE_IPIPE_MAX_SIZE_DPC];
};

enum vpfe_ipipe_otfdpc_det_meth {
	VPFE_IPIPE_DPC_OTF_MIN_MAX,
	VPFE_IPIPE_DPC_OTF_MIN_MAX2
};

struct vpfe_ipipe_otfdpc_thr {
	unsigned short r;
	unsigned short gr;
	unsigned short gb;
	unsigned short b;
};

enum vpfe_ipipe_otfdpc_alg {
	VPFE_IPIPE_OTFDPC_2_0,
	VPFE_IPIPE_OTFDPC_3_0
};

struct vpfe_ipipe_otfdpc_2_0_cfg {
	/* defect detection threshold for MIN_MAX2 method  (DPC 2.0 alg) */
	struct vpfe_ipipe_otfdpc_thr det_thr;
	/* defect correction threshold for MIN_MAX2 method (DPC 2.0 alg) or
	 * maximum value for MIN_MAX method
	 */
	struct vpfe_ipipe_otfdpc_thr corr_thr;
};

struct vpfe_ipipe_otfdpc_3_0_cfg {
	/* DPC3.0 activity adj shf. activity = (max2-min2) >> (6 -shf)
	 */
	unsigned char act_adj_shf;
	/* DPC3.0 detection threshold, THR */
	unsigned short det_thr;
	/* DPC3.0 detection threshold slope, SLP */
	unsigned short det_slp;
	/* DPC3.0 detection threshold min, MIN */
	unsigned short det_thr_min;
	/* DPC3.0 detection threshold max, MAX */
	unsigned short det_thr_max;
	/* DPC3.0 correction threshold, THR */
	unsigned short corr_thr;
	/* DPC3.0 correction threshold slope, SLP */
	unsigned short corr_slp;
	/* DPC3.0 correction threshold min, MIN */
	unsigned short corr_thr_min;
	/* DPC3.0 correction threshold max, MAX */
	unsigned short corr_thr_max;
};

struct vpfe_ipipe_otfdpc {
	/* 0 - disable, 1 - enable */
	unsigned char en;
	/* defect detection method */
	enum vpfe_ipipe_otfdpc_det_meth det_method;
	/* Algorithm used. Applicable only when IPIPE_DPC_OTF_MIN_MAX2 is
	 * used
	 */
	enum vpfe_ipipe_otfdpc_alg alg;
	union {
		/* if alg is IPIPE_OTFDPC_2_0 */
		struct vpfe_ipipe_otfdpc_2_0_cfg dpc_2_0;
		/* if alg is IPIPE_OTFDPC_3_0 */
		struct vpfe_ipipe_otfdpc_3_0_cfg dpc_3_0;
	} alg_cfg;
};

/* Threshold values table size */
#define VPFE_IPIPE_NF_THR_TABLE_SIZE		8
/* Intensity values table size */
#define VPFE_IPIPE_NF_STR_TABLE_SIZE		8

/* NF, sampling method for green pixels */
enum vpfe_ipipe_nf_sampl_meth {
	/* Same as R or B */
	VPFE_IPIPE_NF_BOX,
	/* Diamond mode */
	VPFE_IPIPE_NF_DIAMOND
};

/* Structure for configuring NF module */
struct vpfe_ipipe_nf {
	/* 0 - disable, 1 - enable */
	unsigned char en;
	/* Sampling method for green pixels */
	enum vpfe_ipipe_nf_sampl_meth gr_sample_meth;
	/* Down shift value in LUT reference address
	 */
	unsigned char shft_val;
	/* Spread value in NF algorithm
	 */
	unsigned char spread_val;
	/* Apply LSC gain to threshold. Enable this only if
	 * LSC is enabled in ISIF
	 */
	unsigned char apply_lsc_gain;
	/* Threshold values table */
	unsigned short thr[VPFE_IPIPE_NF_THR_TABLE_SIZE];
	/* intensity values table */
	unsigned char str[VPFE_IPIPE_NF_STR_TABLE_SIZE];
	/* Edge detection minimum threshold */
	unsigned short edge_det_min_thr;
	/* Edge detection maximum threshold */
	unsigned short edge_det_max_thr;
};

enum vpfe_ipipe_gic_alg {
	VPFE_IPIPE_GIC_ALG_CONST_GAIN,
	VPFE_IPIPE_GIC_ALG_ADAPT_GAIN
};

enum vpfe_ipipe_gic_thr_sel {
	VPFE_IPIPE_GIC_THR_REG,
	VPFE_IPIPE_GIC_THR_NF
};

enum vpfe_ipipe_gic_wt_fn_type {
	/* Use difference as index */
	VPFE_IPIPE_GIC_WT_FN_TYP_DIF,
	/* Use weight function as index */
	VPFE_IPIPE_GIC_WT_FN_TYP_HP_VAL
};

/* structure for Green Imbalance Correction */
struct vpfe_ipipe_gic {
	/* 0 - disable, 1 - enable */
	unsigned char en;
	/* 0 - Constant gain , 1 - Adaptive gain algorithm */
	enum vpfe_ipipe_gic_alg gic_alg;
	/* GIC gain or weight. Used for Constant gain and Adaptive algorithms
	 */
	unsigned short gain;
	/* Threshold selection. GIC register values or NF2 thr table */
	enum vpfe_ipipe_gic_thr_sel thr_sel;
	/* thr1. Used when thr_sel is  IPIPE_GIC_THR_REG */
	unsigned short thr;
	/* this value is used for thr2-thr1, thr3-thr2 or
	 * thr4-thr3 when wt_fn_type is index. Otherwise it
	 * is the
	 */
	unsigned short slope;
	/* Apply LSC gain to threshold. Enable this only if
	 * LSC is enabled in ISIF & thr_sel is IPIPE_GIC_THR_REG
	 */
	unsigned char apply_lsc_gain;
	/* Multiply Nf2 threshold by this gain. Use this when thr_sel
	 * is IPIPE_GIC_THR_NF
	 */
	struct ipipe_float_u8 nf2_thr_gain;
	/* Weight function uses difference as index or high pass value.
	 * Used for adaptive gain algorithm
	 */
	enum vpfe_ipipe_gic_wt_fn_type wt_fn_type;
};

/* Structure for configuring WB module */
struct vpfe_ipipe_wb {
	/* Offset (S12) for R */
	short ofst_r;
	/* Offset (S12) for Gr */
	short ofst_gr;
	/* Offset (S12) for Gb */
	short ofst_gb;
	/* Offset (S12) for B */
	short ofst_b;
	/* Gain (U13Q9) for Red */
	struct ipipe_float_u16 gain_r;
	/* Gain (U13Q9) for Gr */
	struct ipipe_float_u16 gain_gr;
	/* Gain (U13Q9) for Gb */
	struct ipipe_float_u16 gain_gb;
	/* Gain (U13Q9) for Blue */
	struct ipipe_float_u16 gain_b;
};

enum vpfe_ipipe_cfa_alg {
	/* Algorithm is 2DirAC */
	VPFE_IPIPE_CFA_ALG_2DIRAC,
	/* Algorithm is 2DirAC + Digital Antialiasing (DAA) */
	VPFE_IPIPE_CFA_ALG_2DIRAC_DAA,
	/* Algorithm is DAA */
	VPFE_IPIPE_CFA_ALG_DAA
};

/* Structure for CFA Interpolation */
struct vpfe_ipipe_cfa {
	/* 2DirAC or 2DirAC + DAA */
	enum vpfe_ipipe_cfa_alg alg;
	/* 2Dir CFA HP value Low Threshold */
	unsigned short hpf_thr_2dir;
	/* 2Dir CFA HP value slope */
	unsigned short hpf_slp_2dir;
	/* 2Dir CFA HP mix threshold */
	unsigned short hp_mix_thr_2dir;
	/* 2Dir CFA HP mix slope */
	unsigned short hp_mix_slope_2dir;
	/* 2Dir Direction threshold */
	unsigned short dir_thr_2dir;
	/* 2Dir Direction slope */
	unsigned short dir_slope_2dir;
	/* 2Dir Non Directional Weight */
	unsigned short nd_wt_2dir;
	/* DAA Mono Hue Fraction */
	unsigned short hue_fract_daa;
	/* DAA Mono Edge threshold */
	unsigned short edge_thr_daa;
	/* DAA Mono threshold minimum */
	unsigned short thr_min_daa;
	/* DAA Mono threshold slope */
	unsigned short thr_slope_daa;
	/* DAA Mono slope minimum */
	unsigned short slope_min_daa;
	/* DAA Mono slope slope */
	unsigned short slope_slope_daa;
	/* DAA Mono LP wight */
	unsigned short lp_wt_daa;
};

/* Struct for configuring RGB2RGB blending module */
struct vpfe_ipipe_rgb2rgb {
	/* Matrix coefficient for RR S12Q8 for ID = 1 and S11Q8 for ID = 2 */
	struct ipipe_float_s16 coef_rr;
	/* Matrix coefficient for GR S12Q8/S11Q8 */
	struct ipipe_float_s16 coef_gr;
	/* Matrix coefficient for BR S12Q8/S11Q8 */
	struct ipipe_float_s16 coef_br;
	/* Matrix coefficient for RG S12Q8/S11Q8 */
	struct ipipe_float_s16 coef_rg;
	/* Matrix coefficient for GG S12Q8/S11Q8 */
	struct ipipe_float_s16 coef_gg;
	/* Matrix coefficient for BG S12Q8/S11Q8 */
	struct ipipe_float_s16 coef_bg;
	/* Matrix coefficient for RB S12Q8/S11Q8 */
	struct ipipe_float_s16 coef_rb;
	/* Matrix coefficient for GB S12Q8/S11Q8 */
	struct ipipe_float_s16 coef_gb;
	/* Matrix coefficient for BB S12Q8/S11Q8 */
	struct ipipe_float_s16 coef_bb;
	/* Output offset for R S13/S11 */
	int out_ofst_r;
	/* Output offset for G S13/S11 */
	int out_ofst_g;
	/* Output offset for B S13/S11 */
	int out_ofst_b;
};

#define VPFE_IPIPE_MAX_SIZE_GAMMA		512

enum vpfe_ipipe_gamma_tbl_size {
	VPFE_IPIPE_GAMMA_TBL_SZ_64 = 64,
	VPFE_IPIPE_GAMMA_TBL_SZ_128 = 128,
	VPFE_IPIPE_GAMMA_TBL_SZ_256 = 256,
	VPFE_IPIPE_GAMMA_TBL_SZ_512 = 512,
};

enum vpfe_ipipe_gamma_tbl_sel {
	VPFE_IPIPE_GAMMA_TBL_RAM = 0,
	VPFE_IPIPE_GAMMA_TBL_ROM = 1,
};

struct vpfe_ipipe_gamma_entry {
	/* 10 bit slope */
	short slope;
	/* 10 bit offset */
	unsigned short offset;
};

/* Structure for configuring Gamma correction module */
struct vpfe_ipipe_gamma {
	/* 0 - Enable Gamma correction for Red
	 * 1 - bypass Gamma correction. Data is divided by 16
	 */
	unsigned char bypass_r;
	/* 0 - Enable Gamma correction for Blue
	 * 1 - bypass Gamma correction. Data is divided by 16
	 */
	unsigned char bypass_b;
	/* 0 - Enable Gamma correction for Green
	 * 1 - bypass Gamma correction. Data is divided by 16
	 */
	unsigned char bypass_g;
	/* IPIPE_GAMMA_TBL_RAM or IPIPE_GAMMA_TBL_ROM */
	enum vpfe_ipipe_gamma_tbl_sel tbl_sel;
	/* Table size for RAM gamma table.
	 */
	enum vpfe_ipipe_gamma_tbl_size tbl_size;
	/* R table */
	struct vpfe_ipipe_gamma_entry table_r[VPFE_IPIPE_MAX_SIZE_GAMMA];
	/* Blue table */
	struct vpfe_ipipe_gamma_entry table_b[VPFE_IPIPE_MAX_SIZE_GAMMA];
	/* Green table */
	struct vpfe_ipipe_gamma_entry table_g[VPFE_IPIPE_MAX_SIZE_GAMMA];
};

#define VPFE_IPIPE_MAX_SIZE_3D_LUT		729

struct vpfe_ipipe_3d_lut_entry {
	/* 10 bit entry for red */
	unsigned short r;
	/* 10 bit entry for green */
	unsigned short g;
	/* 10 bit entry for blue */
	unsigned short b;
};

/* structure for 3D-LUT */
struct vpfe_ipipe_3d_lut {
	/* enable/disable 3D lut */
	unsigned char en;
	/* 3D - LUT table entry */
	struct vpfe_ipipe_3d_lut_entry table[VPFE_IPIPE_MAX_SIZE_3D_LUT];
};

/* Struct for configuring rgb2ycbcr module */
struct vpfe_ipipe_rgb2yuv {
	/* Matrix coefficient for RY S12Q8 */
	struct ipipe_float_s16 coef_ry;
	/* Matrix coefficient for GY S12Q8 */
	struct ipipe_float_s16 coef_gy;
	/* Matrix coefficient for BY S12Q8 */
	struct ipipe_float_s16 coef_by;
	/* Matrix coefficient for RCb S12Q8 */
	struct ipipe_float_s16 coef_rcb;
	/* Matrix coefficient for GCb S12Q8 */
	struct ipipe_float_s16 coef_gcb;
	/* Matrix coefficient for BCb S12Q8 */
	struct ipipe_float_s16 coef_bcb;
	/* Matrix coefficient for RCr S12Q8 */
	struct ipipe_float_s16 coef_rcr;
	/* Matrix coefficient for GCr S12Q8 */
	struct ipipe_float_s16 coef_gcr;
	/* Matrix coefficient for BCr S12Q8 */
	struct ipipe_float_s16 coef_bcr;
	/* Output offset for R S11 */
	int out_ofst_y;
	/* Output offset for Cb S11 */
	int out_ofst_cb;
	/* Output offset for Cr S11 */
	int out_ofst_cr;
};

enum vpfe_ipipe_gbce_type {
	VPFE_IPIPE_GBCE_Y_VAL_TBL = 0,
	VPFE_IPIPE_GBCE_GAIN_TBL = 1,
};

#define VPFE_IPIPE_MAX_SIZE_GBCE_LUT		1024

/* structure for Global brightness and Contrast */
struct vpfe_ipipe_gbce {
	/* enable/disable GBCE */
	unsigned char en;
	/* Y - value table or Gain table */
	enum vpfe_ipipe_gbce_type type;
	/* ptr to LUT for GBCE with 1024 entries */
	unsigned short table[VPFE_IPIPE_MAX_SIZE_GBCE_LUT];
};

/* Chrominance position. Applicable only for YCbCr input
 * Applied after edge enhancement
 */
enum vpfe_chr_pos {
	/* Co-siting, same position with luminance */
	VPFE_IPIPE_YUV422_CHR_POS_COSITE = 0,
	/* Centering, In the middle of luminance */
	VPFE_IPIPE_YUV422_CHR_POS_CENTRE = 1,
};

/* Structure for configuring yuv422 conversion module */
struct vpfe_ipipe_yuv422_conv {
	/* Max Chrominance value */
	unsigned char en_chrom_lpf;
	/* 1 - enable LPF for chrminance, 0 - disable */
	enum vpfe_chr_pos chrom_pos;
};

#define VPFE_IPIPE_MAX_SIZE_YEE_LUT		1024

enum vpfe_ipipe_yee_merge_meth {
	VPFE_IPIPE_YEE_ABS_MAX = 0,
	VPFE_IPIPE_YEE_EE_ES = 1,
};

/* Structure for configuring YUV Edge Enhancement module */
struct vpfe_ipipe_yee {
	/* 1 - enable enhancement, 0 - disable */
	unsigned char en;
	/* enable/disable halo reduction in edge sharpner */
	unsigned char en_halo_red;
	/* Merge method between Edge Enhancer and Edge sharpner */
	enum vpfe_ipipe_yee_merge_meth merge_meth;
	/* HPF Shift length */
	unsigned char hpf_shft;
	/* HPF Coefficient 00, S10 */
	short hpf_coef_00;
	/* HPF Coefficient 01, S10 */
	short hpf_coef_01;
	/* HPF Coefficient 02, S10 */
	short hpf_coef_02;
	/* HPF Coefficient 10, S10 */
	short hpf_coef_10;
	/* HPF Coefficient 11, S10 */
	short hpf_coef_11;
	/* HPF Coefficient 12, S10 */
	short hpf_coef_12;
	/* HPF Coefficient 20, S10 */
	short hpf_coef_20;
	/* HPF Coefficient 21, S10 */
	short hpf_coef_21;
	/* HPF Coefficient 22, S10 */
	short hpf_coef_22;
	/* Lower threshold before referring to LUT */
	unsigned short yee_thr;
	/* Edge sharpener Gain */
	unsigned short es_gain;
	/* Edge sharpener lower threshold */
	unsigned short es_thr1;
	/* Edge sharpener upper threshold */
	unsigned short es_thr2;
	/* Edge sharpener gain on gradient */
	unsigned short es_gain_grad;
	/* Edge sharpener offset on gradient */
	unsigned short es_ofst_grad;
	/* Ptr to EE table. Must have 1024 entries */
	short table[VPFE_IPIPE_MAX_SIZE_YEE_LUT];
};

enum vpfe_ipipe_car_meth {
	/* Chromatic Gain Control */
	VPFE_IPIPE_CAR_CHR_GAIN_CTRL = 0,
	/* Dynamic switching between CHR_GAIN_CTRL
	 * and MED_FLTR
	 */
	VPFE_IPIPE_CAR_DYN_SWITCH = 1,
	/* Median Filter */
	VPFE_IPIPE_CAR_MED_FLTR = 2,
};

enum vpfe_ipipe_car_hpf_type {
	VPFE_IPIPE_CAR_HPF_Y = 0,
	VPFE_IPIPE_CAR_HPF_H = 1,
	VPFE_IPIPE_CAR_HPF_V = 2,
	VPFE_IPIPE_CAR_HPF_2D = 3,
	/* 2D HPF from YUV Edge Enhancement */
	VPFE_IPIPE_CAR_HPF_2D_YEE = 4,
};

struct vpfe_ipipe_car_gain {
	/* csup_gain */
	unsigned char gain;
	/* csup_shf. */
	unsigned char shft;
	/* gain minimum */
	unsigned short gain_min;
};

/* Structure for Chromatic Artifact Reduction */
struct vpfe_ipipe_car {
	/* enable/disable */
	unsigned char en;
	/* Gain control or Dynamic switching */
	enum vpfe_ipipe_car_meth meth;
	/* Gain1 function configuration for Gain control */
	struct vpfe_ipipe_car_gain gain1;
	/* Gain2 function configuration for Gain control */
	struct vpfe_ipipe_car_gain gain2;
	/* HPF type used for CAR */
	enum vpfe_ipipe_car_hpf_type hpf;
	/* csup_thr: HPF threshold for Gain control */
	unsigned char hpf_thr;
	/* Down shift value for hpf. 2 bits */
	unsigned char hpf_shft;
	/* switch limit for median filter */
	unsigned char sw0;
	/* switch coefficient for Gain control */
	unsigned char sw1;
};

/* structure for Chromatic Gain Suppression */
struct vpfe_ipipe_cgs {
	/* enable/disable */
	unsigned char en;
	/* gain1 bright side threshold */
	unsigned char h_thr;
	/* gain1 bright side slope */
	unsigned char h_slope;
	/* gain1 down shift value for bright side */
	unsigned char h_shft;
	/* gain1 bright side minimum gain */
	unsigned char h_min;
};

/* Max pixels allowed in the input. If above this either decimation
 * or frame division mode to be enabled
 */
#define VPFE_IPIPE_MAX_INPUT_WIDTH	2600

struct vpfe_ipipe_input_config {
	unsigned int vst;
	unsigned int hst;
};

/**
 * struct vpfe_ipipe_config - IPIPE engine configuration (user)
 * @input_config: Pointer to structure for ipipe configuration.
 * @flag: Specifies which ISP IPIPE functions should be enabled.
 * @lutdpc: Pointer to luma enhancement structure.
 * @otfdpc: Pointer to structure for defect correction.
 * @nf1: Pointer to structure for Noise Filter.
 * @nf2: Pointer to structure for Noise Filter.
 * @gic: Pointer to structure for Green Imbalance.
 * @wbal: Pointer to structure for White Balance.
 * @cfa: Pointer to structure containing the CFA interpolation.
 * @rgb2rgb1: Pointer to structure for RGB to RGB Blending.
 * @rgb2rgb2: Pointer to structure for RGB to RGB Blending.
 * @gamma: Pointer to gamma structure.
 * @lut: Pointer to structure for 3D LUT.
 * @rgb2yuv: Pointer to structure for RGB-YCbCr conversion.
 * @gbce: Pointer to structure for Global Brightness,Contrast Control.
 * @yuv422_conv: Pointer to structure for YUV 422 conversion.
 * @yee: Pointer to structure for Edge Enhancer.
 * @car: Pointer to structure for Chromatic Artifact Reduction.
 * @cgs: Pointer to structure for Chromatic Gain Suppression.
 */
struct vpfe_ipipe_config {
	__u32 flag;
	struct vpfe_ipipe_input_config __user *input_config;
	struct vpfe_ipipe_lutdpc __user *lutdpc;
	struct vpfe_ipipe_otfdpc __user *otfdpc;
	struct vpfe_ipipe_nf __user *nf1;
	struct vpfe_ipipe_nf __user *nf2;
	struct vpfe_ipipe_gic __user *gic;
	struct vpfe_ipipe_wb __user *wbal;
	struct vpfe_ipipe_cfa __user *cfa;
	struct vpfe_ipipe_rgb2rgb __user *rgb2rgb1;
	struct vpfe_ipipe_rgb2rgb __user *rgb2rgb2;
	struct vpfe_ipipe_gamma __user *gamma;
	struct vpfe_ipipe_3d_lut __user *lut;
	struct vpfe_ipipe_rgb2yuv __user *rgb2yuv;
	struct vpfe_ipipe_gbce __user *gbce;
	struct vpfe_ipipe_yuv422_conv __user *yuv422_conv;
	struct vpfe_ipipe_yee __user *yee;
	struct vpfe_ipipe_car __user *car;
	struct vpfe_ipipe_cgs __user *cgs;
};

/*******************************************************************
 **  Resizer API structures
 *******************************************************************/
/* Interpolation types used for horizontal rescale */
enum vpfe_rsz_intp_t {
	VPFE_RSZ_INTP_CUBIC,
	VPFE_RSZ_INTP_LINEAR
};

/* Horizontal LPF intensity selection */
enum vpfe_rsz_h_lpf_lse_t {
	VPFE_RSZ_H_LPF_LSE_INTERN,
	VPFE_RSZ_H_LPF_LSE_USER_VAL
};

enum vpfe_rsz_down_scale_ave_sz {
	VPFE_IPIPE_DWN_SCALE_1_OVER_2,
	VPFE_IPIPE_DWN_SCALE_1_OVER_4,
	VPFE_IPIPE_DWN_SCALE_1_OVER_8,
	VPFE_IPIPE_DWN_SCALE_1_OVER_16,
	VPFE_IPIPE_DWN_SCALE_1_OVER_32,
	VPFE_IPIPE_DWN_SCALE_1_OVER_64,
	VPFE_IPIPE_DWN_SCALE_1_OVER_128,
	VPFE_IPIPE_DWN_SCALE_1_OVER_256
};

struct vpfe_rsz_output_spec {
	/* enable horizontal flip */
	unsigned char h_flip;
	/* enable vertical flip */
	unsigned char v_flip;
	/* line start offset for y. */
	unsigned int vst_y;
	/* line start offset for c. Only for 420 */
	unsigned int vst_c;
	/* vertical rescale interpolation type, YCbCr or Luminance */
	enum vpfe_rsz_intp_t v_typ_y;
	/* vertical rescale interpolation type for Chrominance */
	enum vpfe_rsz_intp_t v_typ_c;
	/* vertical lpf intensity - Luminance */
	unsigned char v_lpf_int_y;
	/* vertical lpf intensity - Chrominance */
	unsigned char v_lpf_int_c;
	/* horizontal rescale interpolation types, YCbCr or Luminance  */
	enum vpfe_rsz_intp_t h_typ_y;
	/* horizontal rescale interpolation types, Chrominance */
	enum vpfe_rsz_intp_t h_typ_c;
	/* horizontal lpf intensity - Luminance */
	unsigned char h_lpf_int_y;
	/* horizontal lpf intensity - Chrominance */
	unsigned char h_lpf_int_c;
	/* Use down scale mode for scale down */
	unsigned char en_down_scale;
	/* if downscale, set the downscale more average size for horizontal
	 * direction. Used only if output width and height is less than
	 * input sizes
	 */
	enum vpfe_rsz_down_scale_ave_sz h_dscale_ave_sz;
	/* if downscale, set the downscale more average size for vertical
	 * direction. Used only if output width and height is less than
	 * input sizes
	 */
	enum vpfe_rsz_down_scale_ave_sz v_dscale_ave_sz;
	/* Y offset. If set, the offset would be added to the base address
	 */
	unsigned int user_y_ofst;
	/* C offset. If set, the offset would be added to the base address
	 */
	unsigned int user_c_ofst;
};

struct vpfe_rsz_config_params {
	unsigned int vst;
	/* horizontal start position of the image
	 * data to IPIPE
	 */
	unsigned int hst;
	/* output spec of the image data coming out of resizer - 0(UYVY).
	 */
	struct vpfe_rsz_output_spec output1;
	/* output spec of the image data coming out of resizer - 1(UYVY).
	 */
	struct vpfe_rsz_output_spec output2;
	/* 0 , chroma sample at odd pixel, 1 - even pixel */
	unsigned char chroma_sample_even;
	unsigned char frame_div_mode_en;
	unsigned char yuv_y_min;
	unsigned char yuv_y_max;
	unsigned char yuv_c_min;
	unsigned char yuv_c_max;
	enum vpfe_chr_pos out_chr_pos;
	unsigned char bypass;
};

/* Structure for VIDIOC_VPFE_RSZ_[S/G]_CONFIG IOCTLs */
struct vpfe_rsz_config {
	struct vpfe_rsz_config_params *config;
};

#endif		/* _DAVINCI_VPFE_USER_H */

/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __IA_CSS_S3A_TYPES_H
#define __IA_CSS_S3A_TYPES_H

/* @file
* CSS-API header file for 3A statistics parameters.
*/

#include <ia_css_frac.h>

#if (defined(SYSTEM_css_skycam_c0_system)) && (! defined(PIPE_GENERATION) )
#include "../../../../components/stats_3a/src/stats_3a_public.h"
#endif

/* 3A configuration. This configures the 3A statistics collection
 *  module.
 */

/* 3A statistics grid
 *
 *  ISP block: S3A1 (3A Support for 3A ver.1 (Histogram is not used for AE))
 *             S3A2 (3A Support for 3A ver.2 (Histogram is used for AE))
 *  ISP1: S3A1 is used.
 *  ISP2: S3A2 is used.
 */
struct ia_css_3a_grid_info {

#if defined(SYSTEM_css_skycam_c0_system)
	uint32_t ae_enable;					/** ae enabled in binary,
								   0:disabled, 1:enabled */
	struct ae_public_config_grid_config	ae_grd_info;	/** see description in ae_public.h*/

  	uint32_t awb_enable;					/** awb enabled in binary,
								   0:disabled, 1:enabled */
	struct awb_public_config_grid_config	awb_grd_info;	/** see description in awb_public.h*/

  	uint32_t af_enable;					/** af enabled in binary,
								   0:disabled, 1:enabled */
	struct af_public_grid_config		af_grd_info;	/** see description in af_public.h*/

  	uint32_t awb_fr_enable;					/** awb_fr enabled in binary,
								   0:disabled, 1:enabled */
	struct awb_fr_public_grid_config	awb_fr_grd_info;/** see description in awb_fr_public.h*/

        uint32_t elem_bit_depth;    /** TODO:Taken from BYT  - need input from AIQ
					if needed for SKC
					Bit depth of element used
					to calculate 3A statistics.
					This is 13, which is the normalized
					bayer bit depth in DSP. */

#else
	uint32_t enable;            /** 3A statistics enabled.
					0:disabled, 1:enabled */
	uint32_t use_dmem;          /** DMEM or VMEM determines layout.
					0:3A statistics are stored to VMEM,
					1:3A statistics are stored to DMEM */
	uint32_t has_histogram;     /** Statistics include histogram.
					0:no histogram, 1:has histogram */
	uint32_t width;	    	    /** Width of 3A grid table.
					(= Horizontal number of grid cells
					in table, which cells have effective
					statistics.) */
	uint32_t height;	    /** Height of 3A grid table.
					(= Vertical number of grid cells
					in table, which cells have effective
					statistics.) */
	uint32_t aligned_width;     /** Horizontal stride (for alloc).
					(= Horizontal number of grid cells
					in table, which means
					the allocated width.) */
	uint32_t aligned_height;    /** Vertical stride (for alloc).
					(= Vertical number of grid cells
					in table, which means
					the allocated height.) */
	uint32_t bqs_per_grid_cell; /** Grid cell size in BQ(Bayer Quad) unit.
					(1BQ means {Gr,R,B,Gb}(2x2 pixels).)
					Valid values are 8,16,32,64. */
	uint32_t deci_factor_log2;  /** log2 of bqs_per_grid_cell. */
	uint32_t elem_bit_depth;    /** Bit depth of element used
					to calculate 3A statistics.
					This is 13, which is the normalized
					bayer bit depth in DSP. */
#endif
};


/* This struct should be split into 3, for AE, AWB and AF.
 * However, that will require driver/ 3A lib modifications.
 */

/* 3A configuration. This configures the 3A statistics collection
 *  module.
 *
 *  ae_y_*: Coefficients to calculate luminance from bayer.
 *  awb_lg_*: Thresholds to check the saturated bayer pixels for AWB.
 *    Condition of effective pixel for AWB level gate check:
 *      bayer(sensor) <= awb_lg_high_raw &&
 *      bayer(when AWB statisitcs is calculated) >= awb_lg_low &&
 *      bayer(when AWB statisitcs is calculated) <= awb_lg_high
 *  af_fir*: Coefficients of high pass filter to calculate AF statistics.
 *
 *  ISP block: S3A1(ae_y_* for AE/AF, awb_lg_* for AWB)
 *             S3A2(ae_y_* for AF, awb_lg_* for AWB)
 *             SDVS1(ae_y_*)
 *             SDVS2(ae_y_*)
 *  ISP1: S3A1 and SDVS1 are used.
 *  ISP2: S3A2 and SDVS2 are used.
 */
struct ia_css_3a_config {
	ia_css_u0_16 ae_y_coef_r;	/** Weight of R for Y.
						u0.16, [0,65535],
						default/ineffective 25559 */
	ia_css_u0_16 ae_y_coef_g;	/** Weight of G for Y.
						u0.16, [0,65535],
						default/ineffective 32768 */
	ia_css_u0_16 ae_y_coef_b;	/** Weight of B for Y.
						u0.16, [0,65535],
						default/ineffective 7209 */
	ia_css_u0_16 awb_lg_high_raw;	/** AWB level gate high for raw.
						u0.16, [0,65535],
						default 65472(=1023*64),
						ineffective 65535 */
	ia_css_u0_16 awb_lg_low;	/** AWB level gate low.
						u0.16, [0,65535],
						default 64(=1*64),
						ineffective 0 */
	ia_css_u0_16 awb_lg_high;	/** AWB level gate high.
						u0.16, [0,65535],
						default 65535,
						ineffective 65535 */
	ia_css_s0_15 af_fir1_coef[7];	/** AF FIR coefficients of fir1.
						s0.15, [-32768,32767],
				default/ineffective
				-6689,-12207,-32768,32767,12207,6689,0 */
	ia_css_s0_15 af_fir2_coef[7];	/** AF FIR coefficients of fir2.
						s0.15, [-32768,32767],
				default/ineffective
				2053,0,-18437,32767,-18437,2053,0 */
};

/* 3A statistics. This structure describes the data stored
 *  in each 3A grid point.
 *
 *  ISP block: S3A1 (3A Support for 3A ver.1) (Histogram is not used for AE)
 *             S3A2 (3A Support for 3A ver.2) (Histogram is used for AE)
 *             - ae_y is used only for S3A1.
 *             - awb_* and af_* are used both for S3A1 and S3A2.
 *  ISP1: S3A1 is used.
 *  ISP2: S3A2 is used.
 */
struct ia_css_3a_output {
	int32_t ae_y;    /** Sum of Y in a statistics window, for AE.
				(u19.13) */
	int32_t awb_cnt; /** Number of effective pixels
				in a statistics window.
				Pixels passed by the AWB level gate check are
				judged as "effective". (u32) */
	int32_t awb_gr;  /** Sum of Gr in a statistics window, for AWB.
				All Gr pixels (not only for effective pixels)
				are summed. (u19.13) */
	int32_t awb_r;   /** Sum of R in a statistics window, for AWB.
				All R pixels (not only for effective pixels)
				are summed. (u19.13) */
	int32_t awb_b;   /** Sum of B in a statistics window, for AWB.
				All B pixels (not only for effective pixels)
				are summed. (u19.13) */
	int32_t awb_gb;  /** Sum of Gb in a statistics window, for AWB.
				All Gb pixels (not only for effective pixels)
				are summed. (u19.13) */
	int32_t af_hpf1; /** Sum of |Y| following high pass filter af_fir1
				within a statistics window, for AF. (u19.13) */
	int32_t af_hpf2; /** Sum of |Y| following high pass filter af_fir2
				within a statistics window, for AF. (u19.13) */
};


/* 3A Statistics. This structure describes the statistics that are generated
 *  using the provided configuration (ia_css_3a_config).
 */
struct ia_css_3a_statistics {
	struct ia_css_3a_grid_info    grid;	/** grid info contains the dimensions of the 3A grid */
	struct ia_css_3a_output      *data;	/** the pointer to 3a_output[grid.width * grid.height]
						     containing the 3A statistics */
	struct ia_css_3a_rgby_output *rgby_data;/** the pointer to 3a_rgby_output[256]
						     containing the histogram */
};

/* Histogram (Statistics for AE).
 *
 *  4 histograms(r,g,b,y),
 *  256 bins for each histogram, unsigned 24bit value for each bin.
 *    struct ia_css_3a_rgby_output data[256];

 *  ISP block: HIST2
 * (ISP1: HIST2 is not used.)
 *  ISP2: HIST2 is used.
 */
struct ia_css_3a_rgby_output {
	uint32_t r;	/** Number of R of one bin of the histogram R. (u24) */
	uint32_t g;	/** Number of G of one bin of the histogram G. (u24) */
	uint32_t b;	/** Number of B of one bin of the histogram B. (u24) */
	uint32_t y;	/** Number of Y of one bin of the histogram Y. (u24) */
};

#endif /* __IA_CSS_S3A_TYPES_H */


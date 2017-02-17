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

#ifndef __IA_CSS_IEFD2_6_TYPES_H
#define __IA_CSS_IEFD2_6_TYPES_H

/** @file
* CSS-API header file for Image Enhancement Filter directed algorithm parameters.
*/

#include "type_support.h"

/** Image Enhancement Filter directed configuration
 *
 * ISP2.6.1: IEFd2_6 is used.
 */

struct ia_css_iefd2_6_config {
	int32_t horver_diag_coeff;	   /**< Coefficient that compensates for different
						distance for vertical/horizontal and
						diagonal gradient calculation (~1/sqrt(2)).
						u1.6, [0,64], default 45, ineffective 0 */
	int32_t ed_horver_diag_coeff;	   /**< Radial Coefficient that compensates for
						different distance for vertical/horizontal
						and diagonal gradient calculation (~1/sqrt(2)).
						u1.6, [0,64], default 64, ineffective 0 */
	bool dir_smooth_enable;		   /**< Enable smooth best direction with second best.
						bool, [false, true], default true, ineffective false */
	int32_t dir_metric_update;	   /**< Update coefficient for direction metric.
						u1.4, [0,31], default 16, ineffective 0 */
	int32_t unsharp_c00;		   /**< Unsharp Mask filter coefs 0,0 (center).
						s0.8, [-256,255], default 60, ineffective 255 */
	int32_t unsharp_c01;		   /**< Unsharp Mask filter coefs 0,1.
						s0.8, [-256,255], default 30, ineffective 0 */
	int32_t unsharp_c02;		   /**< Unsharp Mask filter coefs 0,2.
						s0.8, [-256,255], default 16, ineffective 0 */
	int32_t unsharp_c11;		   /**< Unsharp Mask filter coefs 1,1.
						s0.8, [-256,255], default 1, ineffective 0 */
	int32_t unsharp_c12;		   /**< Unsharp Mask filter coefs 1,2.
						s1.8, [-512,511], default 2, ineffective 0 */
	int32_t unsharp_c22;		   /**< Unsharp Mask filter coefs 2,2.
						s0.8, [-256,255], default 0, ineffective 0 */
	int32_t unsharp_weight;		   /**< Unsharp Mask blending weight.
						u1.12, [0,4096], default 32, ineffective 0 */
	int32_t unsharp_amount;		   /**< Unsharp Mask amount.
						u3.6, [0,511], default 128, ineffective 0 */
	int32_t cu_dir_sharp_pow;	   /**< Power of cu_dir_sharp (power of direct sharpening).
						u2.4, [0,63], default 20, ineffective 0 */
	int32_t cu_dir_sharp_pow_bright;   /**< Power of cu_dir_sharp (power of direct sharpening) for
						Bright.
						u2.4, [0,63], default 20, ineffective 0 */
	int32_t cu_non_dir_sharp_pow;	   /**< Power of cu_non_dir_sharp (power of unsharp mask).
						u2.4, [0,63], default 24, ineffective 0 */
	int32_t cu_non_dir_sharp_pow_bright;	   /**< Power of cu_non_dir_sharp (power of unsharp mask)
							for Bright.
							u2.4, [0,63], default 24, ineffective 0 */
	int32_t dir_far_sharp_weight;	   /**< Weight of wide direct sharpening.
						u1.12, [0,4096], default 2, ineffective 0 */
	int32_t rad_cu_dir_sharp_x1;	   /**< X1point of cu_dir_sharp for radial/corner point.
						u9.0, [0,511], default 0, ineffective 0 */
	int32_t rad_cu_non_dir_sharp_x1;   /**< X1 point for cu_non_dir_sharp for radial/corner point.
						u9.0, [0,511], default 128, ineffective 0 */
	int32_t rad_dir_far_sharp_weight;  /**< Weight of wide direct sharpening.
						u1.12, [0,4096], default 8, ineffective 0 */
	int32_t sharp_nega_lmt_txt;	   /**< Sharpening limit for negative overshoots for texture.
						u13.0, [0,8191], default 1024, ineffective 0 */
	int32_t sharp_posi_lmt_txt;	   /**< Sharpening limit for positive overshoots for texture.
						u13.0, [0,8191], default 1024, ineffective 0 */
	int32_t sharp_nega_lmt_dir;	   /**< Sharpening limit for negative overshoots for direction
						(edge).
						u13.0, [0,8191], default 128, ineffective 0 */
	int32_t sharp_posi_lmt_dir;	   /**< Sharpening limit for positive overshoots for direction
						(edge).
						u13.0, [0,8191], default 128, ineffective 0 */
	int32_t clamp_stitch;		   /**< Slope to stitch between clamped and unclamped edge values.
						u6.0, [0,63], default 0, ineffective 0 */
	bool rad_enable;		   /**< Enable bit to update radial dependent parameters.
						bool, [false,true], default true, ineffective false */
	int32_t rad_x_origin;		   /**< Initial x coord. for radius computation.
						s13.0, [-8192,8191], default 0, ineffective 0 */
	int32_t rad_y_origin;		   /**< Initial y coord. for radius computation.
						s13.0, [-8192,8191], default 0, ineffective 0 */
	int32_t rad_nf;			   /**< Radial. R^2 normalization factor is scale down by
						2^-(15+scale).
						u4.0, [0,15], default 7, ineffective 0 */
	int32_t rad_inv_r2;		   /**< Radial R^-2 normelized to (0.5..1).
						u(8-m_rad_NF).m_rad_NF, [0,255], default 157,
						ineffective 0 */
	bool vssnlm_enable;		   /**< Enable bit to use VSSNLM output filter.
						bool, [false, true], default true, ineffective false */
	int32_t vssnlm_x0;		   /**< Vssnlm LUT x0.
						u8.0, [0,255], default 24, ineffective 0 */
	int32_t vssnlm_x1;		   /**< Vssnlm LUT x1.
						u8.0, [0,255], default 96, ineffective 0 */
	int32_t vssnlm_x2;		   /**< Vssnlm LUT x2.
						u8.0, [0,255], default 172, ineffective 0 */
	int32_t vssnlm_y1;		   /**< Vssnlm LUT y1.
						u4.0, [0,8], default 1, ineffective 8 */
	int32_t vssnlm_y2;		   /**< Vssnlm LUT y2.
						u4.0, [0,8], default 3, ineffective 8 */
	int32_t vssnlm_y3;		   /**< Vssnlm LUT y3.
						u4.0, [0,8], default 8, ineffective 8 */
	int32_t cu_ed_points_x[6];	   /**< PointsX of config unit ED.
						u0.12, [0,4095], default 0,256,656,2456,3272,4095,
						ineffective 0,0,0,0,0,0 */
	int32_t cu_ed_slopes_a[5];	   /**< SlopesA of config unit ED.
						s6.7, [-8192, 8191], default 4,160,0,0,0,
						ineffective 0,0,0,0,0 */
	int32_t cu_ed_slopes_b[5];	   /**< SlopesB of config unit ED.
						u0.9, [0,511], default 0,9,510,511,511,
						ineffective 0,0,0,0,0 */
	int32_t cu_ed2_points_x[2];	   /**< PointsX of config unit ED2..
						u0.9, [0,511], default 218,308, ineffective 0,0 */
	int32_t cu_ed2_slopes_a;	   /**< SlopesA of config unit ED2.
						s7,4, [-1024, 1024]. default 11, ineffective 0 */
	int32_t cu_ed2_slopes_b;	   /**< SlopesB of config unit ED2.
						u1.6, [0,0], default 0, ineffective 0 */
	int32_t cu_dir_sharp_points_x[4];  /**< PointsX of config unit DirSharp.
						u0.9, [0,511], default 247,298,342,448,
						ineffective 0,0,0,0 */
	int32_t cu_dir_sharp_slopes_a[3];  /**< SlopesA of config unit DirSharp
						s7,4, [0,511], default 14,4,0, ineffective 0,0,0 */
	int32_t cu_dir_sharp_slopes_b[3];  /**< SlopesB of config unit DirSharp.
						u1.6, [0,64], default 1,46,58, ineffective 0,0,0 */
	int32_t cu_non_dir_sharp_points_x[4];	   /**< PointsX of config unit NonDirSharp.
							u0.9, [0,511], default 26,45,81,500,
							ineffective 0,0,0,0 */
	int32_t cu_non_dir_sharp_slopes_a[3];	   /**< SlopesA of config unit NonDirSharp.
							s7.4, [-1024, 1024], default 39,7,0,
							ineffective 0,0,0 */
	int32_t cu_non_dir_sharp_slopes_b[3];	   /**< SlopesB of config unit NonDirSharp.
							u1.6, [0,64], default 1,47,63,
							ineffective 0,0,0 */
	int32_t cu_radial_points_x[6];	   /**< PointsX of Config Unit Radial.
						u0.8, [0,255], default 50,86,142,189,224,255,
						ineffective 0,0,0,0,0,0 */
	int32_t cu_radial_slopes_a[5];	   /**< SlopesA of Config Unit Radial.
						s5.8, [-8192, 8191], default 713,278,295,286,-1,
						ineffective 0,0,0,0,0 */
	int32_t cu_radial_slopes_b[5];	   /**< SlopesB of Config Unit Radial.
						u0.8, [0,255], default 1,101,162,216,255,
						ineffective 0,0,0,0,0 */
	int32_t cu_vssnlm_points_x[2];	   /**< PointsX of config unit VSSNLM.
						u0.9, [0,511], default 100,141, ineffective 0,0 */
	int32_t cu_vssnlm_slopes_a;	   /**< SlopesA of config unit VSSNLM.
						s7.4, [-1024,1024], default 25, ineffective 0 */
	int32_t cu_vssnlm_slopes_b;	   /**< SlopesB of config unit VSSNLM.
						u1.6, [0,0], default 0, ineffective 0 */
};


#endif /* __IA_CSS_IEFD2_6_TYPES_H */


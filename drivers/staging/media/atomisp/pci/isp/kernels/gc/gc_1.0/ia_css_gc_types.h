/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_GC_TYPES_H
#define __IA_CSS_GC_TYPES_H

/* @file
* CSS-API header file for Gamma Correction parameters.
*/

#include "isp/kernels/ctc/ctc_1.0/ia_css_ctc_types.h"  /* FIXME: Needed for ia_css_vamem_type */

/* Fractional bits for GAMMA gain */
#define IA_CSS_GAMMA_GAIN_K_SHIFT      13

/* Number of elements in the gamma table. */
#define IA_CSS_VAMEM_1_GAMMA_TABLE_SIZE_LOG2    10
#define IA_CSS_VAMEM_1_GAMMA_TABLE_SIZE         BIT(IA_CSS_VAMEM_1_GAMMA_TABLE_SIZE_LOG2)

/* Number of elements in the gamma table. */
#define IA_CSS_VAMEM_2_GAMMA_TABLE_SIZE_LOG2    8
#define IA_CSS_VAMEM_2_GAMMA_TABLE_SIZE         ((1U << IA_CSS_VAMEM_2_GAMMA_TABLE_SIZE_LOG2) + 1)

/* Gamma table, used for Y(Luma) Gamma Correction.
 *
 *  ISP block: GC1 (YUV Gamma Correction)
 *  ISP1: GC1 is used.
 * (ISP2: GC2(sRGB Gamma Correction) is used.)
 */
/** IA_CSS_VAMEM_TYPE_1(ISP2300) or
     IA_CSS_VAMEM_TYPE_2(ISP2400) */
union ia_css_gc_data {
	u16 vamem_1[IA_CSS_VAMEM_1_GAMMA_TABLE_SIZE];
	/** Y(Luma) Gamma table on vamem type 1. u0.8, [0,255] */
	u16 vamem_2[IA_CSS_VAMEM_2_GAMMA_TABLE_SIZE];
	/** Y(Luma) Gamma table on vamem type 2. u0.8, [0,255] */
};

struct ia_css_gamma_table {
	enum ia_css_vamem_type vamem_type;
	union ia_css_gc_data data;
};

/* Gamma Correction configuration (used only for YUV Gamma Correction).
 *
 *  ISP block: GC1 (YUV Gamma Correction)
 *  ISP1: GC1 is used.
 * (ISP2: GC2 (sRGB Gamma Correction) is used.)
  */
struct ia_css_gc_config {
	u16 gain_k1; /** Gain to adjust U after YUV Gamma Correction.
				u0.16, [0,65535],
				default/ineffective 19000(0.29) */
	u16 gain_k2; /** Gain to adjust V after YUV Gamma Correction.
				u0.16, [0,65535],
				default/ineffective 19000(0.29) */
};

/* Chroma Enhancement configuration.
 *
 *  This parameter specifies range of chroma output level.
 *  The standard range is [0,255] or [16,240].
 *
 *  ISP block: CE1
 *  ISP1: CE1 is used.
 * (ISP2: CE1 is not used.)
 */
struct ia_css_ce_config {
	u8 uv_level_min; /** Minimum of chroma output level.
				u0.8, [0,255], default/ineffective 0 */
	u8 uv_level_max; /** Maximum of chroma output level.
				u0.8, [0,255], default/ineffective 255 */
};

/* Multi-Axes Color Correction (MACC) configuration.
 *
 *  ISP block: MACC2 (MACC by matrix and exponent(ia_css_macc_config))
 * (ISP1: MACC1 (MACC by only matrix) is used.)
 *  ISP2: MACC2 is used.
 */
struct ia_css_macc_config {
	u8 exp;	/** Common exponent of ia_css_macc_table.
				u8.0, [0,13], default 1, ineffective 1 */
};

#endif /* __IA_CSS_GC_TYPES_H */

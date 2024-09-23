/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __IA_CSS_OB2_TYPES_H
#define __IA_CSS_OB2_TYPES_H

/* @file
* CSS-API header file for Optical Black algorithm parameters.
*/

/* Optical Black configuration
 *
 * ISP2.6.1: OB2 is used.
 */

#include "ia_css_frac.h"

struct ia_css_ob2_config {
	ia_css_u0_16 level_gr;    /** Black level for GR pixels.
					u0.16, [0,65535],
					default/ineffective 0 */
	ia_css_u0_16  level_r;     /** Black level for R pixels.
					u0.16, [0,65535],
					default/ineffective 0 */
	ia_css_u0_16  level_b;     /** Black level for B pixels.
					u0.16, [0,65535],
					default/ineffective 0 */
	ia_css_u0_16  level_gb;    /** Black level for GB pixels.
					u0.16, [0,65535],
					default/ineffective 0 */
};

#endif /* __IA_CSS_OB2_TYPES_H */

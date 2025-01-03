// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#include "system_global.h"
#include "ia_css_types.h"
#include "ia_css_macc_table.host.h"

/* Multi-Axes Color Correction table for ISP1.
 *	64values = 2x2matrix for 16area, [s2.13]
 *	ineffective: 16 of "identity 2x2 matrix" {8192,0,0,8192}
 */
const struct ia_css_macc_table default_macc_table = {
	{
		8192, 0, 0, 8192, 8192, 0, 0, 8192,
		8192, 0, 0, 8192, 8192, 0, 0, 8192,
		8192, 0, 0, 8192, 8192, 0, 0, 8192,
		8192, 0, 0, 8192, 8192, 0, 0, 8192,
		8192, 0, 0, 8192, 8192, 0, 0, 8192,
		8192, 0, 0, 8192, 8192, 0, 0, 8192,
		8192, 0, 0, 8192, 8192, 0, 0, 8192,
		8192, 0, 0, 8192, 8192, 0, 0, 8192
	}
};

/* Multi-Axes Color Correction table for ISP2.
 *	64values = 2x2matrix for 16area, [s1.12]
 *	ineffective: 16 of "identity 2x2 matrix" {4096,0,0,4096}
 */
const struct ia_css_macc_table default_macc2_table = {
	{
		4096, 0, 0, 4096, 4096, 0, 0, 4096,
		4096, 0, 0, 4096, 4096, 0, 0, 4096,
		4096, 0, 0, 4096, 4096, 0, 0, 4096,
		4096, 0, 0, 4096, 4096, 0, 0, 4096,
		4096, 0, 0, 4096, 4096, 0, 0, 4096,
		4096, 0, 0, 4096, 4096, 0, 0, 4096,
		4096, 0, 0, 4096, 4096, 0, 0, 4096,
		4096, 0, 0, 4096, 4096, 0, 0, 4096
	}
};

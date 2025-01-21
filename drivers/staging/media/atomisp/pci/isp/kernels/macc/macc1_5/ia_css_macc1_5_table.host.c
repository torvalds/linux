// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#include "system_global.h"
#include "ia_css_types.h"
#include "ia_css_macc1_5_table.host.h"

/* Multi-Axes Color Correction table for ISP2.
 *	64values = 2x2matrix for 16area, [s1.12]
 *	ineffective: 16 of "identity 2x2 matix" {4096,0,0,4096}
 */
const struct ia_css_macc1_5_table default_macc1_5_table = {
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

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

#include "system_global.h"
#include "ia_css_types.h"
#include "ia_css_macc1_5_table.host.h"

/* Multi-Axes Color Correction table for ISP2.
 *	64values = 2x2matrix for 16area, [s1.12]
 *	ineffective: 16 of "identity 2x2 matix" {4096,0,0,4096}
 */
const struct ia_css_macc1_5_table default_macc1_5_table = {
	      { 4096, 0, 0, 4096, 4096, 0, 0, 4096,
		4096, 0, 0, 4096, 4096, 0, 0, 4096,
		4096, 0, 0, 4096, 4096, 0, 0, 4096,
		4096, 0, 0, 4096, 4096, 0, 0, 4096,
		4096, 0, 0, 4096, 4096, 0, 0, 4096,
		4096, 0, 0, 4096, 4096, 0, 0, 4096,
		4096, 0, 0, 4096, 4096, 0, 0, 4096,
		4096, 0, 0, 4096, 4096, 0, 0, 4096 }
};

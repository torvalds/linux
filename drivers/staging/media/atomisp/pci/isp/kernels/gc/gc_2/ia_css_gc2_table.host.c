// SPDX-License-Identifier: GPL-2.0
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

#include <linux/string.h> /* for memcpy() */

#include <type_support.h>
#include "system_global.h"
#include "vamem.h"
#include "ia_css_types.h"
#include "ia_css_gc2_table.host.h"

struct ia_css_rgb_gamma_table default_r_gamma_table;
struct ia_css_rgb_gamma_table default_g_gamma_table;
struct ia_css_rgb_gamma_table default_b_gamma_table;

/* Identical default gamma table for R, G, and B. */


static const uint16_t
default_gamma_table_data[IA_CSS_VAMEM_2_RGB_GAMMA_TABLE_SIZE] = {
	0,   72,  144,  216,  288,  360,  426,  486,
	541,  592,  641,  687,  730,  772,  812,  850,
	887,  923,  958,  991, 1024, 1055, 1086, 1117,
	1146, 1175, 1203, 1230, 1257, 1284, 1310, 1335,
	1360, 1385, 1409, 1433, 1457, 1480, 1502, 1525,
	1547, 1569, 1590, 1612, 1632, 1653, 1674, 1694,
	1714, 1734, 1753, 1772, 1792, 1811, 1829, 1848,
	1866, 1884, 1902, 1920, 1938, 1955, 1973, 1990,
	2007, 2024, 2040, 2057, 2074, 2090, 2106, 2122,
	2138, 2154, 2170, 2185, 2201, 2216, 2231, 2247,
	2262, 2277, 2291, 2306, 2321, 2335, 2350, 2364,
	2378, 2393, 2407, 2421, 2435, 2449, 2462, 2476,
	2490, 2503, 2517, 2530, 2543, 2557, 2570, 2583,
	2596, 2609, 2622, 2634, 2647, 2660, 2673, 2685,
	2698, 2710, 2722, 2735, 2747, 2759, 2771, 2783,
	2795, 2807, 2819, 2831, 2843, 2855, 2867, 2878,
	2890, 2901, 2913, 2924, 2936, 2947, 2958, 2970,
	2981, 2992, 3003, 3014, 3025, 3036, 3047, 3058,
	3069, 3080, 3091, 3102, 3112, 3123, 3134, 3144,
	3155, 3165, 3176, 3186, 3197, 3207, 3217, 3228,
	3238, 3248, 3258, 3268, 3279, 3289, 3299, 3309,
	3319, 3329, 3339, 3349, 3358, 3368, 3378, 3388,
	3398, 3407, 3417, 3427, 3436, 3446, 3455, 3465,
	3474, 3484, 3493, 3503, 3512, 3521, 3531, 3540,
	3549, 3559, 3568, 3577, 3586, 3595, 3605, 3614,
	3623, 3632, 3641, 3650, 3659, 3668, 3677, 3686,
	3694, 3703, 3712, 3721, 3730, 3739, 3747, 3756,
	3765, 3773, 3782, 3791, 3799, 3808, 3816, 3825,
	3833, 3842, 3850, 3859, 3867, 3876, 3884, 3893,
	3901, 3909, 3918, 3926, 3934, 3942, 3951, 3959,
	3967, 3975, 3984, 3992, 4000, 4008, 4016, 4024,
	4032, 4040, 4048, 4056, 4064, 4072, 4080, 4088,
	4095
};

void
ia_css_config_rgb_gamma_tables(void)
{
	default_r_gamma_table.vamem_type   = IA_CSS_VAMEM_TYPE_2;
	default_g_gamma_table.vamem_type   = IA_CSS_VAMEM_TYPE_2;
	default_b_gamma_table.vamem_type   = IA_CSS_VAMEM_TYPE_2;
	memcpy(default_r_gamma_table.data.vamem_2, default_gamma_table_data,
	       sizeof(default_gamma_table_data));
	memcpy(default_g_gamma_table.data.vamem_2, default_gamma_table_data,
	       sizeof(default_gamma_table_data));
	memcpy(default_b_gamma_table.data.vamem_2, default_gamma_table_data,
	       sizeof(default_gamma_table_data));
}

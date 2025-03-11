// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#include <linux/string.h> /* for memcpy() */

#include <type_support.h>
#include "system_global.h"
#include "vamem.h"
#include "ia_css_types.h"
#include "ia_css_xnr_table.host.h"

struct ia_css_xnr_table default_xnr_table;


static const uint16_t
default_xnr_table_data[IA_CSS_VAMEM_2_XNR_TABLE_SIZE] = {
	/* 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 */
	8191 >> 1, 4096 >> 1, 2730 >> 1, 2048 >> 1, 1638 >> 1, 1365 >> 1, 1170 >> 1, 1024 >> 1, 910 >> 1, 819 >> 1, 744 >> 1, 682 >> 1, 630 >> 1, 585 >> 1,
	     546 >> 1, 512 >> 1,

	     /* 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 */
	     481 >> 1, 455 >> 1, 431 >> 1, 409 >> 1, 390 >> 1, 372 >> 1, 356 >> 1, 341 >> 1, 327 >> 1, 315 >> 1, 303 >> 1, 292 >> 1, 282 >> 1, 273 >> 1, 264 >> 1,
	     256 >> 1,

	     /* 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 */
	     248 >> 1, 240 >> 1, 234 >> 1, 227 >> 1, 221 >> 1, 215 >> 1, 210 >> 1, 204 >> 1, 199 >> 1, 195 >> 1, 190 >> 1, 186 >> 1, 182 >> 1, 178 >> 1, 174 >> 1,
	     170 >> 1,

	     /* 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 64 */
	     167 >> 1, 163 >> 1, 160 >> 1, 157 >> 1, 154 >> 1, 151 >> 1, 148 >> 1, 146 >> 1, 143 >> 1, 141 >> 1, 138 >> 1, 136 >> 1, 134 >> 1, 132 >> 1, 130 >> 1, 128 >> 1
};


void
ia_css_config_xnr_table(void)
{
	memcpy(default_xnr_table.data.vamem_2, default_xnr_table_data,
	       sizeof(default_xnr_table_data));
	default_xnr_table.vamem_type     = IA_CSS_VAMEM_TYPE_2;
}

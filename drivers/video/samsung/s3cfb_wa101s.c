/* linux/drivers/video/samsung/s3cfb_wa101s.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * 101WA01S 10.1" Landscape LCD module driver for the SMDK
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include "s3cfb.h"

static struct s3cfb_lcd wa101 = {
//	.width	= 1366,
	.width	= 1360,
	.height	= 768,
	.bpp	= 24,
	.freq	= 60,

	.timing = {
		.h_fp	= 48,
		.h_bp	= 80,
		.h_sw	= 32,
		.v_fp	= 3,
		.v_fpe	= 0,
		.v_bp	= 14,
		.v_bpe	= 0,
		.v_sw	= 5,
	},

	.polarity = {
		.rise_vclk	= 1,
		.inv_hsync	= 1,
		.inv_vsync	= 1,
		.inv_vden	= 0,
	},
};

/* name should be fixed as 's3cfb_set_lcd_info' */
void s3cfb_set_lcd_info(struct s3cfb_global *ctrl)
{
	wa101.init_ldi = NULL;
	ctrl->lcd = &wa101;
}

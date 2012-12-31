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

static struct s3cfb_lcd ltn101al03 = {
	.width	= 1280,
	.height	= 800,
	.bpp	= 24,
	.freq	= 60,
	.timing = {
		.h_fp	= 16,
		.h_sw	= 48,
		.h_bp	= 64,			
		
		.v_fp	= 1,
		.v_bp	= 12,
		.v_sw	= 3,		
		.v_fpe	= 0,
		.v_bpe	= 0,
	},

	.polarity = {
		.rise_vclk	= 1,
		.inv_hsync	= 0,
		.inv_vsync	= 0,
		.inv_vden	= 0,
	},
};

/* name should be fixed as 's3cfb_set_lcd_info' */
void s3cfb_set_lcd_info(struct s3cfb_global *ctrl)
{
	ltn101al03.init_ldi = NULL;
	ctrl->lcd = &ltn101al03;
	printk("registerd LTN101AL03 LCD Driver.\n");
}

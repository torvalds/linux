/* linux/drivers/video/samsung/s3cfb2_tl2796.c
 *
 * Samsung tl2796" WVGA Display Panel Support
 *
 * Jinsung Yang, Copyright (c) 2009 Samsung Electronics
 *	http://www.samsungsemi.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include "s3cfb2.h"

static struct s3cfb_lcd tl2796 = {
	.width = 480,
	.height = 800,
	.bpp = 24,
	.freq = 60,

	.timing = {
		.h_fp = 66,
		.h_bp = 2,
		.h_sw = 4,
		.v_fp = 15,
		.v_fpe = 1,
		.v_bp = 3,
		.v_bpe = 1,
		.v_sw = 5,
	},

	.polarity = {
		.rise_vclk = 1,
		.inv_hsync = 1,
		.inv_vsync = 1,
		.inv_vden = 1,
	},
};

/* name should be fixed as 's3cfb_set_lcd_info' */
void s3cfb_set_lcd_info(struct s3cfb_global *ctrl)
{
	tl2796.init_ldi = NULL;
	ctrl->lcd = &tl2796;
}


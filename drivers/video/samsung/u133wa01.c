/* linux/drivers/video/samsung/s3cfb_lp101wh1.c
 *
 * LG Display LP101WH1 10.1" WSVGA Display Panel Support
 *
 * Hakjoo Kim, Copyright (c) 2010 Hardkernel Inc.
 * 	ruppi.kim@hardkernel.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include "s3cfb.h"

static struct s3cfb_lcd u133wa01 = {
#if defined(CONFIG_TOUCHSCREEN_NEXIO_USB)   // 42" HDMI demo
	.width = 1920,
	.height = 1080,
#else
	.width = 1360,
	.height = 768,
#endif	
	.bpp = 24,
	.freq = 60,

	.timing = {
#if defined(CONFIG_TOUCHSCREEN_NEXIO_USB)   // 42" HDMI demo
		.h_fp = 1,
		.h_bp = 1,
		.h_sw = 1,
		.v_fp = 1,
		.v_fpe = 1,
		.v_bp = 1,
		.v_bpe = 1,
		.v_sw = 1,
#else
		.h_fp = 48,
		.h_bp = 80,
		.h_sw = 32,
		.v_fp = 3,
		.v_fpe = 2,
		.v_bp = 14,
		.v_bpe = 2,
		.v_sw = 5,
#endif		
	},

	.polarity = {
		.rise_vclk = 0,
		.inv_hsync = 1,
		.inv_vsync = 1,
		.inv_vden = 0,
	},
};

/* name should be fixed as 's3cfb_set_lcd_info' */
void s3cfb_set_lcd_info(struct s3cfb_global *ctrl)
{
	u133wa01.init_ldi	= NULL;
	
	ctrl->lcd = &u133wa01;
	printk("registerd u133wa01 LCD Driver.\n");
}


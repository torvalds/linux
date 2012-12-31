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

static struct s3cfb_lcd fake_fb = {
	.width = 1280,
	.height = 720,
	.bpp = 24,
	.freq = 60,

	.timing = {
		.h_fp = 1,
		.h_bp = 1,
		.h_sw = 1,
		.v_fp = 1,
		.v_fpe = 1,
		.v_bp = 1,
		.v_bpe = 1,
		.v_sw = 1,
	},

	.polarity = {
		.rise_vclk = 0,
		.inv_hsync = 1,
		.inv_vsync = 1,
		.inv_vden = 0,
	},
};

static int atoi(const char *str)
{
    int val = 0;
    
    for(;;str++)   {
        switch(*str)   {
            case    '0'...'9':  val = 10 * val + (*str-'0');    break;
            default :                                           return  val;
        }
    }
    return  val;
}

static  unsigned char   FbBootArgsX[5], FbBootArgsY[5];
static  unsigned char   SetEnableX = false, SetEnableY = false;

static int __init lcd_x_res(char *line)
{
    sprintf(FbBootArgsX, "%s", line);    SetEnableX = true;
    return  0;
}
__setup("fb_x_res=", lcd_x_res);

static int __init lcd_y_res(char *line)
{
    sprintf(FbBootArgsY, "%s", line);    SetEnableY = true;
    return  0;
}
__setup("fb_y_res=", lcd_y_res);

/* name should be fixed as 's3cfb_set_lcd_info' */
void s3cfb_set_lcd_info(struct s3cfb_global *ctrl)
{
	fake_fb.init_ldi	= NULL;

    if(SetEnableX)  fake_fb.width    = atoi(FbBootArgsX);
    if(SetEnableY)  fake_fb.height   = atoi(FbBootArgsY);
	
	ctrl->lcd = &fake_fb;
	
	printk("Registerd Fake FB Driver.\n");
	printk("Fake FB res X = %d\n", fake_fb.width);
	printk("Fake FB res Y = %d\n", fake_fb.height);
}


/*
 * Hardkernel HDMI Configuration
 *
 * Copyright (c) 2013 Hardkernel Co., Ltd.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published 
 * by the Free Software Foundiation. either version 2 of the License,
 * or (at your option) any later version
 */
#include "hdmi.h"

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h> 
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <media/v4l2-subdev.h>
#include <linux/module.h>   
#include <linux/interrupt.h>
#include <linux/irq.h>  
#include <linux/delay.h> 
#include <linux/bug.h>
#include <linux/pm_domain.h> 
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/videodev2_exynos_media.h>
#include <linux/sched.h>      
#include <plat/devs.h>   
#include <plat/tv-core.h>
#include <plat/cpu.h>

#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>   
#include <media/v4l2-device.h>
#include <media/exynos_mc.h>
#include "regs-hdmi-5xx0.h"

MODULE_AUTHOR("Mauro Ribeiro, <mauro.ribeiro@hardkernel.com.br>");
MODULE_DESCRIPTION("Hardkernel HDMI");
MODULE_LICENSE("GPL");

static unsigned char vout_mode[5];
static unsigned char hdmiphy_mode[10];

static int __init hdmi_vout_setup(char *value)
{
	sprintf(vout_mode, "%s", value);
	return 0;
}
__setup("vout=", hdmi_vout_setup);


static int __init hdmiphy_setup(char *value)
{
	sprintf(hdmiphy_mode, "%s", value);
	return 0;
}
__setup("hdmi_phy_res=", hdmiphy_setup);


/*
 * Will parse the string from CMDLINE to get the proper v4l2 id for the requested format
 * will fail to 720p60hz if not a valid value
*/ 
int hdmi_get_v4l2_dv_id() 
{
	if(strcmp(hdmiphy_mode, "480p60hz") == 0)
		return V4L2_DV_480P60;
	else if(strcmp(hdmiphy_mode, "576p50hz") == 0)
		return V4L2_DV_576P50;
	else if(strcmp(hdmiphy_mode, "720p60hz") == 0)
		return V4L2_DV_720P60;
	else if(strcmp(hdmiphy_mode, "720p50hz") == 0) 
		return V4L2_DV_720P50;
	else if(strcmp(hdmiphy_mode, "1080p60hz") == 0)
		return V4L2_DV_1080P60;
	else if(strcmp(hdmiphy_mode, "1080i60hz") == 0)
		return V4L2_DV_1080I60;
	else if(strcmp(hdmiphy_mode, "1080i50hz") == 0)
		return V4L2_DV_1080I50;  
	else if(strcmp(hdmiphy_mode, "1080p50hz") == 0)
		return V4L2_DV_1080P50; 
	else if(strcmp(hdmiphy_mode, "1080p30hz") == 0)
		return V4L2_DV_1080P30;
	else if(strcmp(hdmiphy_mode, "1080p25hz") == 0)
		return V4L2_DV_1080P25;
	else if(strcmp(hdmiphy_mode, "1080p24hz") == 0)
		return V4L2_DV_1080P24;
	else
		return V4L2_DV_720P60; // if vout= parameters isn't any of above, defaults to 720p60hz
}

/*
 * returns 1 for DVI and true for 0
 *
*/
int hdmi_get_phy_mode() 
{
	if(strcmp(vout_mode, "dvi") == 0) 
		return 1;
	else
		return 0;
}

/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/display-sys.h>
#include "rk610_tv.h"


#ifdef CONFIG_DISPLAY_KEY_LED_CONTROL
#define RK610_LED_YPbPr_PIN	RK29_PIN4_PD5
#else
#define RK610_LED_YPbPr_PIN	INVALID_GPIO
#endif
#define E(fmt, arg...) printk("<3>!!!%s:%d: " fmt, __FILE__, __LINE__, ##arg)

static const struct fb_videomode rk610_YPbPr_mode [] = {
		//name				refresh		xres	yres	pixclock	h_bp	h_fp	v_bp	v_fp	h_pw	v_pw	polariry	PorI	flag
	{	"YPbPr480p",		60,			720,	480,	27000000,	55,		19,		37,		5,		64,		5,		0,			0,		OUT_P888	},
	{	"YPbPr576p",		50,			720,	576,	27000000,	68,		12,		39,		5,		64,		5,		0,			0,		OUT_P888	},
	{	"YPbPr720p@50",		50,			1280,	720,	74250000,	600,	0,		20,		5,		100,	5,		0,			0,		OUT_P888	},
	{	"YPbPr720p@60",		60,			1280,	720,	74250000,	270,	0,		20,		5,		100,	5,		0,			0,		OUT_P888	},
	//{	"YPbPr1080i@50",	50,			1920,	1080,	148500000,	620,	0,		15,		2,		100,	5,		0,			1,		OUT_CCIR656	},
	{	"YPbPr1080i@60",	60,			1920,	1080,	148500000,	180,	0,		15,		2,		100,	5,		0,			1,		OUT_CCIR656	},
	{	"YPbPr1080p@50",	50,			1920,	1080,	148500000,	620,	0,		36,		4,		100,	5,		0,			0,		OUT_P888	},
	{	"YPbPr1080p@60",	60,			1920,	1080,	148500000,	180,	0,		36,		4,		100,	5,		0,			0,		OUT_P888	},
};

struct rk610_monspecs rk610_ypbpr_monspecs;

int rk610_tv_ypbpr_init(void)
{
	unsigned char TVE_Regs[9];
	unsigned char TVE_CON_Reg;
	int i, ret;
	
	rk610_tv_wirte_reg(TVE_HDTVCR, TVE_RESET);
	memset(TVE_Regs, 0, 9);	
	
	TVE_CON_Reg = 0x00;
	
	TVE_Regs[TVE_VINCR] 	=	TVE_VINCR_PIX_DATA_DELAY(0) | TVE_VINCR_H_SYNC_POLARITY_NEGTIVE | TVE_VINCR_V_SYNC_POLARITY_NEGTIVE | TVE_VINCR_VSYNC_FUNCTION_VSYNC;
	TVE_Regs[TVE_POWERCR]	=	TVE_DAC_CLK_INVERSE_DISABLE | TVE_DAC_Y_ENABLE | TVE_DAC_U_ENABLE | TVE_DAC_V_ENABLE;
	TVE_Regs[TVE_VOUTCR]	=	TVE_VOUTCR_OUTPUT_YPBPR;
	TVE_Regs[TVE_YADJCR]	=	0x17;
	TVE_Regs[TVE_YCBADJCR]	=	0x10;
	TVE_Regs[TVE_YCRADJCR]	=	0x10;
	
	switch(rk610_tv_output_status)
	{
		case TVOUT_YPbPr_720x480p_60:
			TVE_Regs[TVE_VFCR]		=	TVE_VFCR_BLACK_0_IRE;
			TVE_Regs[TVE_VINCR]		|=	TVE_VINCR_INPUT_FORMAT(INPUT_FORMAT_BT601_SLAVE);
			TVE_Regs[TVE_HDTVCR]	=	TVE_FILTER(0) | TVE_COLOR_CONVERT_REC601 | TVE_INPUT_DATA_RGB | TVE_OUTPUT_60HZ | TVE_OUTPUT_MODE_480P;
			break;
		case TVOUT_YPbPr_720x576p_50:
			TVE_Regs[TVE_VFCR]		=	TVE_VFCR_BLACK_0_IRE | TVE_VFCR_PAL_NC;
			TVE_Regs[TVE_VINCR]		|=	TVE_VINCR_INPUT_FORMAT(INPUT_FORMAT_BT601_SLAVE);
			TVE_Regs[TVE_HDTVCR]	=	TVE_FILTER(0) | TVE_COLOR_CONVERT_REC601 | TVE_INPUT_DATA_RGB | TVE_OUTPUT_50HZ | TVE_OUTPUT_MODE_576P;
			break;
		case TVOUT_YPbPr_1280x720p_50:
			TVE_Regs[TVE_VFCR]		=	TVE_VFCR_BLACK_0_IRE | TVE_VFCR_PAL_NC;
			TVE_Regs[TVE_VINCR]		|=	TVE_VINCR_INPUT_FORMAT(INPUT_FORMAT_BT601_SLAVE);
			TVE_Regs[TVE_HDTVCR]	=	TVE_FILTER(0) | TVE_COLOR_CONVERT_REC709 | TVE_INPUT_DATA_RGB | TVE_OUTPUT_50HZ | TVE_OUTPUT_MODE_720P;
			break;
		case TVOUT_YPbPr_1280x720p_60:
			TVE_Regs[TVE_VFCR]		=	TVE_VFCR_BLACK_0_IRE | TVE_VFCR_PAL_NC;
			TVE_Regs[TVE_VINCR]		|=	TVE_VINCR_INPUT_FORMAT(INPUT_FORMAT_BT601_SLAVE);
			TVE_Regs[TVE_HDTVCR]	=	TVE_FILTER(0) | TVE_COLOR_CONVERT_REC709 | TVE_INPUT_DATA_RGB | TVE_OUTPUT_60HZ | TVE_OUTPUT_MODE_720P;
			break;
		/*case TVOUT_YPbPr_1920x1080i_50:
			TVE_Regs[TVE_VFCR]		=	TVE_VFCR_BLACK_0_IRE | TVE_VFCR_PAL_NC;
			TVE_Regs[TVE_VINCR]		|=	TVE_VINCR_INPUT_FORMAT(INPUT_FORMAT_BT656);
			TVE_Regs[TVE_HDTVCR]	=	TVE_FILTER(0) | TVE_INPUT_DATA_YUV | TVE_OUTPUT_50HZ;
			TVE_Regs[TVE_YADJCR]	|=	TVE_OUTPUT_MODE_1080I;
			break;
			*/
		case TVOUT_YPbPr_1920x1080i_60:
			TVE_Regs[TVE_VFCR]		=	TVE_VFCR_BLACK_0_IRE | TVE_VFCR_PAL_NC;
			TVE_Regs[TVE_VINCR]		|=	TVE_VINCR_INPUT_FORMAT(INPUT_FORMAT_BT656);
			TVE_Regs[TVE_HDTVCR]	=	TVE_FILTER(0) | TVE_INPUT_DATA_YUV | TVE_OUTPUT_60HZ;
			TVE_Regs[TVE_YADJCR]	|=	TVE_OUTPUT_MODE_1080I;
			break;
		case TVOUT_YPbPr_1920x1080p_50:
			TVE_Regs[TVE_VFCR]		=	TVE_VFCR_BLACK_0_IRE | TVE_VFCR_PAL_NC;
			TVE_Regs[TVE_VINCR]		|=	TVE_VINCR_INPUT_FORMAT(INPUT_FORMAT_BT601_SLAVE);
			TVE_Regs[TVE_HDTVCR]	=	TVE_FILTER(0) | TVE_COLOR_CONVERT_REC709 | TVE_INPUT_DATA_RGB | TVE_OUTPUT_50HZ;
			TVE_Regs[TVE_YADJCR]	|=	TVE_OUTPUT_MODE_1080P;
			break;
		case TVOUT_YPbPr_1920x1080p_60:
			TVE_Regs[TVE_VFCR]		=	TVE_VFCR_BLACK_0_IRE | TVE_VFCR_PAL_NC;
			TVE_Regs[TVE_VINCR]		|=	TVE_VINCR_INPUT_FORMAT(INPUT_FORMAT_BT601_SLAVE);
			TVE_Regs[TVE_HDTVCR]	=	TVE_FILTER(0) | TVE_COLOR_CONVERT_REC709 | TVE_INPUT_DATA_RGB | TVE_OUTPUT_60HZ;
			TVE_Regs[TVE_YADJCR]	|=	TVE_OUTPUT_MODE_1080P;
			break;
		default:
			return -1;
	}
	
	rk610_control_send_byte(RK610_CONTROL_REG_TVE_CON, TVE_CON_Reg);
	
	for(i = 0; i < sizeof(TVE_Regs); i++){
//		printk(KERN_ERR "reg[%d] = 0x%02x\n", i, TVE_Regs[i]);
		ret = rk610_tv_wirte_reg(i, TVE_Regs[i]);
		if(ret < 0){
			E("rk610_tv_wirte_reg %d err!\n", i);
			return ret;
		}
	}
	return 0;
}

static int rk610_ypbpr_set_enable(struct rk_display_device *device, int enable)
{
	if(rk610_ypbpr_monspecs.enable != enable || rk610_ypbpr_monspecs.mode_set != rk610_tv_output_status)
	{
		if(enable == 0)
		{
			rk610_tv_standby(RK610_TVOUT_YPBPR);
			rk610_ypbpr_monspecs.enable = 0;
			if(RK610_LED_YPbPr_PIN != INVALID_GPIO)
				gpio_direction_output(RK610_LED_YPbPr_PIN, GPIO_HIGH);
		}
		else if(enable == 1)
		{
			rk610_switch_fb(rk610_ypbpr_monspecs.mode, rk610_ypbpr_monspecs.mode_set);
			rk610_ypbpr_monspecs.enable = 1;
			if(RK610_LED_YPbPr_PIN != INVALID_GPIO)
				gpio_direction_output(RK610_LED_YPbPr_PIN, GPIO_LOW);
		}
	}
	return 0;
}

static int rk610_ypbpr_get_enable(struct rk_display_device *device)
{
	return rk610_ypbpr_monspecs.enable;
}

static int rk610_ypbpr_get_status(struct rk_display_device *device)
{
	if(rk610_tv_output_status > TVOUT_CVBS_PAL)
		return 1;
	else
		return 0;
}

static int rk610_ypbpr_get_modelist(struct rk_display_device *device, struct list_head **modelist)
{
	*modelist = &(rk610_ypbpr_monspecs.modelist);
	return 0;
}

static int rk610_ypbpr_set_mode(struct rk_display_device *device, struct fb_videomode *mode)
{
	int i;

	for(i = 0; i < ARRAY_SIZE(rk610_YPbPr_mode); i++)
	{
		if(fb_mode_is_equal(&rk610_YPbPr_mode[i], mode))
		{	
			if( (i + 3) != rk610_tv_output_status )
			{
				rk610_ypbpr_monspecs.mode_set = i + 3;
				rk610_ypbpr_monspecs.mode = (struct fb_videomode *)&rk610_YPbPr_mode[i];
			}
			return 0;
		}
	}
	
	return -1;
}

static int rk610_ypbpr_get_mode(struct rk_display_device *device, struct fb_videomode *mode)
{
	*mode = *(rk610_ypbpr_monspecs.mode);
	return 0;
}

static struct rk_display_ops rk610_ypbpr_display_ops = {
	.setenable = rk610_ypbpr_set_enable,
	.getenable = rk610_ypbpr_get_enable,
	.getstatus = rk610_ypbpr_get_status,
	.getmodelist = rk610_ypbpr_get_modelist,
	.setmode = rk610_ypbpr_set_mode,
	.getmode = rk610_ypbpr_get_mode,
};

static int rk610_display_YPbPr_probe(struct rk_display_device *device, void *devdata)
{
	device->owner = THIS_MODULE;
	strcpy(device->type, "YPbPr");
	device->priority = DISPLAY_PRIORITY_YPbPr;
	device->priv_data = devdata;
	device->ops = &rk610_ypbpr_display_ops;
	return 1;
}

static struct rk_display_driver display_rk610_YPbPr = {
	.probe = rk610_display_YPbPr_probe,
};

int rk610_register_display_ypbpr(struct device *parent)
{
	int i;
	
	memset(&rk610_ypbpr_monspecs, 0, sizeof(struct rk610_monspecs));
	INIT_LIST_HEAD(&rk610_ypbpr_monspecs.modelist);
	for(i = 0; i < ARRAY_SIZE(rk610_YPbPr_mode); i++)
		fb_add_videomode(&rk610_YPbPr_mode[i], &rk610_ypbpr_monspecs.modelist);
	if(rk610_tv_output_status > TVOUT_CVBS_PAL) {
		rk610_ypbpr_monspecs.mode = (struct fb_videomode *)&(rk610_YPbPr_mode[rk610_tv_output_status - 3]);
		rk610_ypbpr_monspecs.mode_set = rk610_tv_output_status;
	}
	else {
		rk610_ypbpr_monspecs.mode = (struct fb_videomode *)&(rk610_YPbPr_mode[3]);
		rk610_ypbpr_monspecs.mode_set = TVOUT_YPbPr_1280x720p_60;
	}
	rk610_ypbpr_monspecs.ddev = rk_display_device_register(&display_rk610_YPbPr, parent, NULL);
	if(RK610_LED_YPbPr_PIN != INVALID_GPIO)
    {        
        if(gpio_request(RK610_LED_YPbPr_PIN, NULL) != 0)
        {
            gpio_free(RK610_LED_YPbPr_PIN);
            dev_err(rk610_ypbpr_monspecs.ddev->dev, ">>>>>> RK610_LED_YPbPr_PIN gpio_request err \n ");
            return -1;
        }
		gpio_pull_updown(RK610_LED_YPbPr_PIN,GPIOPullUp);
		gpio_direction_output(RK610_LED_YPbPr_PIN, GPIO_HIGH);
    }
	return 0;
}

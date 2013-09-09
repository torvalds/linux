/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/gpio.h>
#include <linux/pwm_backlight.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include <video/platform_lcd.h>
#include <video/s5p-dp.h>

#include <plat/cpu.h>
#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/fb.h>
#include <plat/fb-core.h>
#include <plat/regs-fb-v4.h>
#include <plat/dp.h>
#include <plat/backlight.h>
#include <plat/gpio-cfg.h>

#include <mach/map.h>

#include <plat/dsim.h>
#include <plat/mipi_dsi.h>

#define DEFAULT_FB_X    1280
#define DEFAULT_FB_Y    800

unsigned short  FrameBufferSizeX = DEFAULT_FB_X;
unsigned short  FrameBufferSizeY = DEFAULT_FB_Y;

// Used Input driver(Virtual Touch driver)
EXPORT_SYMBOL(FrameBufferSizeX);
EXPORT_SYMBOL(FrameBufferSizeY);

static  unsigned char   FbBootArgsX[5], FbBootArgsY[5];
static  unsigned char   SetVirtualFB = false;

static int __init lcd_x_res(char *line)
{
    sprintf(FbBootArgsX, "%s", line);    SetVirtualFB = true;
    return  0;
}
__setup("fb_x_res=", lcd_x_res);

static int __init lcd_y_res(char *line)
{
    sprintf(FbBootArgsY, "%s", line);    SetVirtualFB = true;
    return  0;
}
__setup("fb_y_res=", lcd_y_res);

static  unsigned char   VoutBootArgs[5];

static int __init vout_mode(char *line)
{
    sprintf(VoutBootArgs, "%s", line);    SetVirtualFB = true;
    return  0;
}
__setup("vout=", vout_mode);

static  unsigned char   FbLeft[5], FbRight[5], FbUpper[5], FbLower[5], FbHsync[5], FbVsync[5];

static int __init timming_left(char *line)
{
    sprintf(FbLeft, "%s", line);    return  0;
}
__setup("left=", timming_left);

static int __init timming_right(char *line)
{
    sprintf(FbRight, "%s", line);    return  0;
}
__setup("right=", timming_right);

static int __init timming_upper(char *line)
{
    sprintf(FbUpper, "%s", line);    return  0;
}
__setup("upper=", timming_upper);

static int __init timming_lower(char *line)
{
    sprintf(FbLower, "%s", line);    return  0;
}
__setup("lower=", timming_lower);

static int __init timming_hsync(char *line)
{
    sprintf(FbHsync, "%s", line);    return  0;
}
__setup("hsync=", timming_hsync);

static int __init timming_vsync(char *line)
{
    sprintf(FbVsync, "%s", line);    return  0;
}
__setup("vsync=", timming_vsync);


#define LCD_RESET       EXYNOS5410_GPA0(4)
#define LCD_BACKLIGHT   EXYNOS5410_GPA1(5)

static int mipi_lcd_power_control(struct mipi_dsim_device *dsim, unsigned int enable)
{
    // LCD BACLKIGHT OFF
	gpio_request_one(LCD_BACKLIGHT, GPIOF_OUT_INIT_HIGH, "LCD BACKLIGHT");
    // LCD RESET HIGH
	gpio_request_one(LCD_RESET, GPIOF_OUT_INIT_HIGH, "LCD RESET");
    gpio_set_value(LCD_RESET, 1);   

	// LCD BACLKIGHT ON/OFF
	if (enable) gpio_set_value(LCD_BACKLIGHT, 0);
	else		gpio_set_value(LCD_BACKLIGHT, 1);

	gpio_free(LCD_RESET);   gpio_free(LCD_BACKLIGHT);

	return  0;
}

#define ODROIDXU_FB_MAX  5

static struct s3c_fb_pd_win odroidxu_fb_default = {
	.win_mode = {
		.left_margin	= 4,
		.right_margin	= 4,
		.upper_margin	= 4,
		.lower_margin	= 4,
		.hsync_len	    = 4,
		.vsync_len	    = 4,
		.xres		    = DEFAULT_FB_X,
		.yres		    = DEFAULT_FB_Y,
	},
	.virtual_x		= DEFAULT_FB_X,
	.virtual_y		= DEFAULT_FB_Y * 2,
	.max_bpp		= 32,
	.default_bpp	= 24,
};


static struct s3c_fb_pd_win odroidxu_fb[ODROIDXU_FB_MAX];

static struct platform_device odroidxu_dp_lcd = {
	.name	= "platform-lcd",
	.dev	= {
		.parent		= &s5p_device_fimd1.dev,
	},
};

#if 0
static struct s3c_fb_pd_win odroidxu_fb_default = {
#if 0   // WQXGA LCD
	.win_mode = {
		.left_margin	= 80,
		.right_margin	= 48,
		.upper_margin	= 37,
		.lower_margin	= 3,
		.hsync_len	    = 32,
		.vsync_len	    = 6,
		.xres		    = 2560,
		.yres		    = 1600,
	},
	.virtual_x      = 2560,
	.virtual_y	    = 1600 * 2,
	.max_bpp	    = 32,
	.default_bpp    = 24,
#endif	
#if 1
// AOC I2269V 22"(1920 X 1080)
	.win_mode = {
		.left_margin	= 56,
		.right_margin	= 24,
		.upper_margin	= 3,
		.lower_margin	= 3,
		.hsync_len	    = 14,
		.vsync_len	    = 3,

		.xres		    = 1920,
		.yres		    = 1080,
	},
	.virtual_x      = 1920,
	.virtual_y	    = 1080 * 2,
	.max_bpp	    = 32,
	.default_bpp    = 24,
#endif
#if 0
// YAMAKASI Monitor 2560 * 1440
	.win_mode = {
		.left_margin	= 15,
		.right_margin	= 10,
		.upper_margin	= 10,
		.lower_margin	= 10,
		.hsync_len	    = 10,
		.vsync_len	    = 10,

		.xres		    = 2560,
		.yres		    = 1440,
	},
	.virtual_x      = 2560,
	.virtual_y	    = 1440 * 2,
	.max_bpp	    = 32,
	.default_bpp    = 24,
#endif	
#if 0
// X-Star Monitor 27" (2560 X 1440)
	.win_mode = {
		.left_margin	= 56,
		.right_margin	= 24,
		.upper_margin	= 3,
		.lower_margin	= 3,
		.hsync_len	    = 14,
		.vsync_len	    = 3,

		.xres		    = 2560,
		.yres		    = 1440,
	},
	.virtual_x      = 2560,
	.virtual_y	    = 1440 * 2,
	.max_bpp	    = 32,
	.default_bpp    = 24,
#endif	
#if 0   // LG Monitor 2560 * 1080
// 2560/1080, 1280/1080, 1680/720, 1496/640. 1872/800 
	.win_mode = {
		.left_margin	= 100,
		.right_margin	= 100,
		.upper_margin	= 100,
		.lower_margin	= 100,
		.hsync_len	    = 100,
		.vsync_len	    = 100,
		
		.xres		    = 2560,
		.yres		    = 1080,
#if 0	    
		.left_margin	= 56,
		.right_margin	= 248,
		.upper_margin	= 3,
		.lower_margin	= 3,
		.hsync_len	    = 144,
		.vsync_len	    = 3,
		
		.xres		    = 1680,
		.yres		    = 720,
#endif		
	},
	.virtual_x      = 2560,
	.virtual_y	    = 1080 * 2,
	.max_bpp	    = 32,
	.default_bpp    = 24,
#endif	
};
#endif

static void exynos_fimd_gpio_setup_24bpp(void)
{
	unsigned int reg = 0;

	/*
	 * Set DISP1BLK_CFG register for Display path selection
	 *
	 * FIMD of DISP1_BLK Bypass selection : DISP1BLK_CFG[15]
	 * ---------------------
	 *  0 | MIE/MDNIE
	 *  1 | FIMD : selected
	 */
	reg = __raw_readl(S3C_VA_SYS + 0x0214);
	reg &= ~(1 << 15);	/* To save other reset values */
	reg |= (1 << 15);
	__raw_writel(reg, S3C_VA_SYS + 0x0214);

    if(!strncmp(VoutBootArgs, "dp", sizeof("dp")))  {
    	/* Reference clcok selection for DPTX_PHY: PAD_OSC_IN */
    	reg = __raw_readl(S3C_VA_SYS + 0x04d4);
    	reg &= ~(1 << 0);
    	__raw_writel(reg, S3C_VA_SYS + 0x04d4);
    
    	/* DPTX_PHY: XXTI */
    	reg = __raw_readl(S3C_VA_SYS + 0x04d8);
    	reg &= ~(1 << 3);
    	__raw_writel(reg, S3C_VA_SYS + 0x04d8);
    }
}

static struct s3c_fb_platdata odroidxu_lcd1_pdata __initdata = {
	.win[0]		= &odroidxu_fb[0],
	.win[1]		= &odroidxu_fb[1],
	.win[2]		= &odroidxu_fb[2],
	.win[3]		= &odroidxu_fb[3],
	.win[4]		= &odroidxu_fb[4],
	.default_win	= 0,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
	.vidcon1	= VIDCON1_INV_VCLK,
	.setup_gpio	= exynos_fimd_gpio_setup_24bpp,
	.ip_version	= EXYNOS5_813,
};

static struct mipi_dsim_config dsim_info = {
	.e_interface	= DSIM_VIDEO,
	.e_pixel_format	= DSIM_24BPP_888,
	/* main frame fifo auto flush at VSYNC pulse */
	.auto_flush         = false,
	.eot_disable	    = false,
	.auto_vertical_cnt  = false,
	.hse = false,
	.hfp = false,
	.hbp = false,
	.hsa = false,

	.e_no_data_lane	= DSIM_DATA_LANE_4,
	.e_byte_clk	= DSIM_PLL_OUT_DIV8,
	.e_burst_mode	= DSIM_BURST,

    // 24Mhz/p * m / 2 = vfco = 240Mhz
    .p = 4,
    .m = 80,
    .s = 2,

	/* D-PHY PLL stable time spec :min = 200usec ~ max 400usec */
	.pll_stable_time = 500,

	.esc_clk = 0.4 * 1000000,		/* escape clk : 10MHz */

	/* stop state holding counter after bta change count 0 ~ 0xfff */
	.stop_holding_cnt   = 0x0f,
	.bta_timeout        = 0xff,		/* bta timeout 0 ~ 0xff */
	.rx_timeout         = 0xffff,	/* lp rx timeout 0 ~ 0xffff */

	.dsim_ddi_pd = &tc358764_mipi_lcd_driver,
};

static struct mipi_dsim_lcd_config dsim_lcd_info = {
	.rgb_timing.left_margin		= 0x4,
	.rgb_timing.right_margin	= 0x4,
	.rgb_timing.upper_margin	= 0x4,
	.rgb_timing.lower_margin	= 0x4,
	.rgb_timing.hsync_len		= 0x4,
	.rgb_timing.vsync_len		= 0x4,
	.rgb_timing.cmd_allow		= 0,
	.cpu_timing.cs_setup		= 0,
	.cpu_timing.wr_setup		= 1,
	.cpu_timing.wr_act	    	= 0,
	.cpu_timing.wr_hold		    = 0,
	.lcd_size.width			    = DEFAULT_FB_X,
	.lcd_size.height		    = DEFAULT_FB_Y,
};

static struct s5p_platform_mipi_dsim dsim_platform_data = {
	.clk_name		    = "dsim1",
	.dsim_config		= &dsim_info,
	.dsim_lcd_config	= &dsim_lcd_info,

	.mipi_power		    = mipi_lcd_power_control,
	.part_reset		    = s5p_dsim_part_reset,
	.init_d_phy		    = s5p_dsim_init_d_phy,
	.get_fb_frame_done	= NULL,
	.trigger		    = NULL,

	/*
	 * The stable time of needing to write data on SFR
	 * when the mipi mode becomes LP mode.
	 */
	.delay_for_stabilization = 600,
};

static struct video_info odroidxu_dp_config = {
	.name			= "DP Monitor",

	.h_sync_polarity	= 0,
	.v_sync_polarity	= 0,
	.interlaced		    = 0,

	.color_space		= COLOR_RGB,
	.dynamic_range		= VESA,
	.ycbcr_coeff		= COLOR_YCBCR601,
	.color_depth		= COLOR_8,

	.link_rate		= LINK_RATE_2_70GBPS,
	.lane_count		= LANE_COUNT4,
};

static struct s5p_dp_platdata odroidxu_dp_data __initdata = {
	.video_info	    = &odroidxu_dp_config,
	.phy_init	    = s5p_dp_phy_init,
	.phy_exit	    = s5p_dp_phy_exit,
};

static struct platform_device *odroidxu_display_devices[] __initdata = {
	&s5p_device_fimd1,
};

static struct platform_device *odroidxu_mipi_display_devices[] __initdata = {
	&s5p_device_mipi_dsim1,
};

static struct platform_device *odroidxu_dp_display_devices[] __initdata = {
	&s5p_device_dp,
	&odroidxu_dp_lcd,
};

/* LCD Backlight data */
static struct samsung_bl_gpio_info odroidxu_bl_gpio_info = {
	.no = EXYNOS5410_GPB2(3),
	.func = S3C_GPIO_SFN(2),
};

static struct platform_pwm_backlight_data odroidxu_bl_data = {
	.pwm_id = 3,
	.pwm_period_ns = 30000,
};

void    s5p_dp_set_parameter(void)
{
    odroidxu_fb_default.win_mode.left_margin	= simple_strtol(FbLeft , NULL, 10);
    odroidxu_fb_default.win_mode.right_margin	= simple_strtol(FbRight, NULL, 10);
    odroidxu_fb_default.win_mode.upper_margin	= simple_strtol(FbUpper, NULL, 10);
    odroidxu_fb_default.win_mode.lower_margin	= simple_strtol(FbLower, NULL, 10);
    odroidxu_fb_default.win_mode.hsync_len	    = simple_strtol(FbHsync, NULL, 10);
    odroidxu_fb_default.win_mode.vsync_len	    = simple_strtol(FbVsync, NULL, 10);

    if(!odroidxu_fb_default.win_mode.left_margin )  odroidxu_fb_default.win_mode.left_margin	= 56;
    if(!odroidxu_fb_default.win_mode.right_margin)  odroidxu_fb_default.win_mode.right_margin	= 24;
    if(!odroidxu_fb_default.win_mode.upper_margin)	odroidxu_fb_default.win_mode.upper_margin	= 3; 
    if(!odroidxu_fb_default.win_mode.lower_margin)	odroidxu_fb_default.win_mode.lower_margin	= 3; 
    if(!odroidxu_fb_default.win_mode.hsync_len	 )  odroidxu_fb_default.win_mode.hsync_len	    = 14;  
    if(!odroidxu_fb_default.win_mode.vsync_len	 )  odroidxu_fb_default.win_mode.vsync_len	    = 3;   

    printk("DP Timing left  = %d\n", odroidxu_fb_default.win_mode.left_margin );
    printk("DP Timing right = %d\n", odroidxu_fb_default.win_mode.right_margin);
    printk("DP Timing upper = %d\n", odroidxu_fb_default.win_mode.upper_margin);
    printk("DP Timing lower = %d\n", odroidxu_fb_default.win_mode.lower_margin);
    printk("DP Timing hsync = %d\n", odroidxu_fb_default.win_mode.hsync_len	  );
    printk("DP Timing vsync = %d\n", odroidxu_fb_default.win_mode.vsync_len	  );
}

void __init exynos5_odroidxu_display_init(void)
{
    int     i;

    if(!strncmp(VoutBootArgs, "dp", sizeof("dp")))   {
        printk("\n---------------------------------------------------------\n\n");
        printk("%s : Display Port(DP) Monitor!\n\n", __func__);         
        s5p_dp_set_parameter();
        printk("\n---------------------------------------------------------\n\n");
        odroidxu_lcd1_pdata.vidcon1	= 0,
    	s5p_dp_set_platdata(&odroidxu_dp_data);
    }
    else    {
        printk("\n---------------------------------------------------------\n\n");
        printk("%s : LCD or HDMI or DVI Monitor!\n", __func__);
        printk("\n---------------------------------------------------------\n\n");
    	s5p_dsim1_set_platdata(&dsim_platform_data);
    }
    
    if(SetVirtualFB)    {
        FrameBufferSizeX = simple_strtol(FbBootArgsX, NULL, 10);
        FrameBufferSizeY = simple_strtol(FbBootArgsY, NULL, 10);
        
        if(!FrameBufferSizeX || !FrameBufferSizeY)  {
            FrameBufferSizeX = DEFAULT_FB_X;
            FrameBufferSizeY = DEFAULT_FB_Y;
        }
        printk("\n---------------------------------------------------------\n\n");
        printk("Virtual FB Size from Boot Parameter : X(%d), y(%d)\n", FrameBufferSizeX, FrameBufferSizeY);         
        printk("\n---------------------------------------------------------\n\n");
    }
    else    {
        printk("\n---------------------------------------------------------\n\n");
        printk("FB Size : X(%d), y(%d)\n", FrameBufferSizeX, FrameBufferSizeY);         
        printk("\n---------------------------------------------------------\n\n");
    }
    odroidxu_fb_default.win_mode.xres = FrameBufferSizeX;
    odroidxu_fb_default.win_mode.yres = FrameBufferSizeY;
    odroidxu_fb_default.virtual_x = FrameBufferSizeX;
    odroidxu_fb_default.virtual_y = FrameBufferSizeY * 2;
    
	s5p_fimd1_set_platdata(&odroidxu_lcd1_pdata);

    for(i=0; i<ODROIDXU_FB_MAX; i++)
        memcpy(&odroidxu_fb[i], &odroidxu_fb_default, sizeof(odroidxu_fb_default));

	samsung_bl_set(&odroidxu_bl_gpio_info, &odroidxu_bl_data);
	platform_add_devices(odroidxu_display_devices, ARRAY_SIZE(odroidxu_display_devices));

    if(!strncmp(VoutBootArgs, "dp", sizeof("dp")))  {
    	platform_add_devices(odroidxu_dp_display_devices, ARRAY_SIZE(odroidxu_dp_display_devices));
    	exynos5_fimd1_setup_clock(&s5p_device_fimd1.dev, "sclk_fimd", "mout_mpll_bpll", 267 * MHZ);
    }
    else    {
    	platform_add_devices(odroidxu_mipi_display_devices, ARRAY_SIZE(odroidxu_mipi_display_devices));
    	exynos5_fimd1_setup_clock(&s5p_device_fimd1.dev, "sclk_fimd", "mout_cpll", 64 * MHZ);
    }
}

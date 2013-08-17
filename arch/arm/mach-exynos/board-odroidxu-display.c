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

#ifdef CONFIG_FB_MIPI_DSIM
#include <plat/dsim.h>
#include <plat/mipi_dsi.h>
#endif

#if defined(CONFIG_LCD_MIPI_TC358764)
    #define DEFAULT_FB_X    1280
    #define DEFAULT_FB_Y    800
#else
    #define DEFAULT_FB_X    1280
    #define DEFAULT_FB_Y    720
#endif

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

#if defined(CONFIG_LCD_MIPI_TC358764)

#define LCD_RESET       EXYNOS5410_GPA0(4)
#define LCD_BACKLIGHT   EXYNOS5410_GPA1(5)

static int mipi_lcd_power_control(struct mipi_dsim_device *dsim, unsigned int enable)
{
    // LCD BACLKIGHT OFF
	gpio_request_one(LCD_BACKLIGHT, GPIOF_OUT_INIT_HIGH, "LCD BACKLIGHT");
    // LCD RESET HIGH
	gpio_request_one(LCD_RESET, GPIOF_OUT_INIT_HIGH, "LCD RESET");
	usleep_range(20000, 21000);

	/* reset */
	if (enable)  {
    	/* fire nRESET on power up */
    	gpio_set_value(LCD_RESET, 0);   usleep_range(20000, 21000);
    	gpio_set_value(LCD_RESET, 1);   usleep_range(20000, 21000);
	    // LCD BACLKIGHT OFF
		gpio_set_value(LCD_BACKLIGHT, 0);
	}
	else    {
    	/* fire nRESET on power up */
    	gpio_set_value(LCD_RESET, 0);
	    // LCD BACLKIGHT OFF
		gpio_set_value(LCD_BACKLIGHT, 1);
	}

	gpio_free(LCD_RESET);   gpio_free(LCD_BACKLIGHT);
	return  0;
}

#define LCD_FB_MAX  5

static struct s3c_fb_pd_win lcd_fb_default = {
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
	.width			= 223,
	.height			= 125,
	.max_bpp		= 32,
	.default_bpp	= 24,
};


static struct s3c_fb_pd_win odroidxu_fb[LCD_FB_MAX];

#elif defined(CONFIG_S5P_DP)

static struct platform_device odroidxu_dp_lcd = {
	.name	= "platform-lcd",
	.dev	= {
		.parent		= &s5p_device_fimd1.dev,
	},
};

#define LCD_FB_MAX   5

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

static struct s3c_fb_pd_win odroidxu_fb[LCD_FB_MAX];
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

#if defined(CONFIG_S5P_DP)
	/* Reference clcok selection for DPTX_PHY: PAD_OSC_IN */
	reg = __raw_readl(S3C_VA_SYS + 0x04d4);
	reg &= ~(1 << 0);
	__raw_writel(reg, S3C_VA_SYS + 0x04d4);

	/* DPTX_PHY: XXTI */
	reg = __raw_readl(S3C_VA_SYS + 0x04d8);
	reg &= ~(1 << 3);
	__raw_writel(reg, S3C_VA_SYS + 0x04d8);
#endif
}

static struct s3c_fb_platdata odroidxu_lcd1_pdata __initdata = {
	.win[0]		= &odroidxu_fb[0],
	.win[1]		= &odroidxu_fb[1],
	.win[2]		= &odroidxu_fb[2],
	.win[3]		= &odroidxu_fb[3],
	.win[4]		= &odroidxu_fb[4],
	.default_win	= 0,
	.vidcon0	= VIDCON0_VIDOUT_RGB | VIDCON0_PNRMODE_RGB,
#if defined(CONFIG_S5P_DP)
	.vidcon1	= 0,
#else
	.vidcon1	= VIDCON1_INV_VCLK,
#endif
	.setup_gpio	= exynos_fimd_gpio_setup_24bpp,
	.ip_version	= EXYNOS5_813,
};

#ifdef CONFIG_FB_MIPI_DSIM

#if defined(CONFIG_LCD_MIPI_TC358764)

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
#endif

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
#endif

#ifdef CONFIG_S5P_DP
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
#endif

static struct platform_device *odroidxu_display_devices[] __initdata = {
#ifdef CONFIG_FB_MIPI_DSIM
	&s5p_device_mipi_dsim1,
#endif

	&s5p_device_fimd1,

#ifdef CONFIG_S5P_DP
	&s5p_device_dp,
	&odroidxu_dp_lcd,
#endif
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

void __init exynos5_odroidxu_display_init(void)
{
    int     i;
    
#ifdef CONFIG_FB_MIPI_DSIM
	s5p_dsim1_set_platdata(&dsim_platform_data);
#endif

#ifdef CONFIG_S5P_DP
	s5p_dp_set_platdata(&odroidxu_dp_data);
#endif

#ifdef CONFIG_S5P_DP
    for(i=0; i<LCD_FB_MAX; i++)
        memcpy(&odroidxu_fb[i], &odroidxu_fb_default, sizeof(odroidxu_fb_default));
#else
    if(SetVirtualFB)    {
        FrameBufferSizeX = simple_strtol(FbBootArgsX, NULL, 10);
        FrameBufferSizeY = simple_strtol(FbBootArgsY, NULL, 10);
        printk("Virtual FB Size : X(%d), y(%d)\n", FrameBufferSizeX, FrameBufferSizeY);         
    }
    lcd_fb_default.win_mode.xres = FrameBufferSizeX;
    lcd_fb_default.win_mode.yres = FrameBufferSizeY;
    lcd_fb_default.virtual_x = FrameBufferSizeX;
    lcd_fb_default.virtual_y = FrameBufferSizeY * 2;
    
    for(i=0; i<LCD_FB_MAX; i++)
        memcpy(&odroidxu_fb[i], &lcd_fb_default, sizeof(lcd_fb_default));
#endif
	s5p_fimd1_set_platdata(&odroidxu_lcd1_pdata);

	samsung_bl_set(&odroidxu_bl_gpio_info, &odroidxu_bl_data);
	platform_add_devices(odroidxu_display_devices,
			ARRAY_SIZE(odroidxu_display_devices));

#ifdef CONFIG_S5P_DP
	/* 64MHz = 320MHz@CPLL / 6 */
	exynos5_fimd1_setup_clock(&s5p_device_fimd1.dev,
			"sclk_fimd", "mout_mpll_bpll", 267 * MHZ);
#endif

#ifdef CONFIG_FB_MIPI_DSIM
	/* 64MHz = 320MHz@CPLL / 6 */
	exynos5_fimd1_setup_clock(&s5p_device_fimd1.dev,
			"sclk_fimd", "mout_cpll", 64 * MHZ);

#endif
}

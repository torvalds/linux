/*
 * Copyright (C) 2007-2012 Allwinner Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __DISP_DISPLAY_H__
#define __DISP_DISPLAY_H__

#include "disp_display_i.h"
#include "disp_layer.h"
#include "disp_scaler.h"
#include "disp_video.h"
#include <asm/div64.h>

#ifdef CONFIG_ARCH_SUN5I
#include "disp_iep.h"
#endif


#define IMAGE_USED	0x00000004
#define YUV_CH_USED	0x00000010
#define HWC_USED	0x00000040
#define LCDC_TCON0_USED	0x00000080
#define LCDC_TCON1_USED	0x00000100
#define SCALER_USED	0x00000200

#define LCD_ON		0x00010000
#define TV_ON		0x00020000
#define HDMI_ON		0x00040000
#define VGA_ON		0x00080000

#define VIDEO_PLL0_USED		0x00100000
#define VIDEO_PLL1_USED		0x00200000

#define IMAGE_OUTPUT_LCDC		0x00000001
#define IMAGE_OUTPUT_SCALER		0x00000002
#define IMAGE_OUTPUT_LCDC_AND_SCALER	0x00000003

#define DE_FLICKER_USED 0x01000000
#define DE_FLICKER_REQUIRED 0x02000000

#define SUNXI_DISP_MAX_LAYERS 4

static inline __u32 PICOS2HZ(__u32 picos)
{
	__u64 numerator = 1000000000000ULL + (picos / 2);
	do_div(numerator, picos);
	return numerator;
}

static inline __u32 HZ2PICOS(__u32 hz)
{
	__u64 numerator = 1000000000000ULL + (hz / 2);
	do_div(numerator, hz);
	return numerator;
}

typedef struct {
	__bool lcd_used;

	__bool lcd_bl_en_used;
	user_gpio_set_t lcd_bl_en;

	__bool lcd_power_used;
	user_gpio_set_t lcd_power;

	__bool lcd_pwm_used;
	user_gpio_set_t lcd_pwm;

	__bool lcd_gpio_used[4];
	user_gpio_set_t lcd_gpio[4];

	__bool lcd_io_used[28];
	user_gpio_set_t lcd_io[28];

	__u32 init_bright;
} __disp_lcd_cfg_t;

typedef struct {
	__u32 status;		/* display engine,lcd,tv,vga,hdmi status */
	__u32 lcdc_status;	/* tcon0 used, tcon1 used */
	__bool have_cfg_reg;
	__u32 cache_flag;
	__u32 cfg_cnt;

	__u32 screen_width;
	__u32 screen_height;
	__disp_color_t bk_color;
	__disp_colorkey_t color_key;
	__u32 bright;
	__u32 contrast;
	__u32 saturation;
	__u32 hue;
#ifdef CONFIG_ARCH_SUN4I
	__bool enhance_en;
#endif
	__u32 max_layers;
	__layer_man_t layer_manage[4];
#ifdef CONFIG_ARCH_SUN4I
	__u32 de_flicker_status;
#else
	__u32 iep_status;
#endif

	/*
	 * see macro definition IMAGE_OUTPUT_XXX above, it can be
	 * lcd only /lcd+scaler/ scaler only
	 */
	__u32 image_output_type;
	__u32 out_scaler_index;
	__u32 hdmi_index;	/* 0: internal hdmi; 1:external hdmi(if exit) */

	__bool use_edid;
	__bool b_out_interlace;
	__disp_output_type_t output_type;	/* sw status */
	__disp_vga_mode_t vga_mode;
	__disp_tv_mode_t tv_mode;
	__disp_tv_mode_t hdmi_mode;
	__disp_tv_dac_source dac_source[4];

	 __s32(*LCD_CPUIF_XY_Swap) (__s32 mode);
	void (*LCD_CPUIF_ISR) (void);
	__u32 pll_use_status;	/* lcdc0/lcdc1 using which video pll(0 or 1) */

	__u32 lcd_bright;
#ifdef CONFIG_ARCH_SUN5I
	/*
	 * IEP-drc backlight dimming rate:
	 * 0 -256 (256: no dimming; 0: the most dimming)
	 */
	__u32 lcd_bright_dimming;
#else
	__disp_color_range_t out_color_range;
	__csc_t out_csc;
#endif

	__disp_lcd_cfg_t lcd_cfg;
	__hdle gpio_hdl[4];
} __disp_screen_t;

typedef struct {
	__bool enable;
	__u32 freq;
	__u32 pre_scal;
	__u32 active_state;
	__u32 duty_ns;
	__u32 period_ns;
	__u32 entire_cycle;
	__u32 active_cycle;
} __disp_pwm_t;

typedef struct {
	__disp_bsp_init_para init_para;	/* para from driver */
	__disp_screen_t screen[2];
	__disp_scaler_t scaler[2];
	__disp_pwm_t pwm[2];
} __disp_dev_t;

extern __disp_dev_t gdisp;

#endif

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

#include "lcd_panel_cfg.h"

/*
 * comment out this line if you want to use the lcd para define in
 * sys_config1.fex
 */
//#define LCD_PARA_USE_CONFIG

#ifdef LCD_PARA_USE_CONFIG
static __u8 g_gamma_tbl[][2] = {
	/* {input value, corrected value} */
	{0, 0},
	{15, 15},
	{30, 30},
	{45, 45},
	{60, 60},
	{75, 75},
	{90, 90},
	{105, 105},
	{120, 120},
	{135, 135},
	{150, 150},
	{165, 165},
	{180, 180},
	{195, 195},
	{210, 210},
	{225, 225},
	{240, 240},
	{255, 255},
};

static void lcd_gamma_gen(__panel_para_t *info)
{
	__u32 items = sizeof(g_gamma_tbl) / 2;
	__u32 i, j;

	for (i = 0; i < items - 1; i++) {
		__u32 num = g_gamma_tbl[i + 1][0] - g_gamma_tbl[i][0];

		for (j = 0; j < num; j++) {
			__u32 value = 0;

			value = g_gamma_tbl[i][1] +
				((g_gamma_tbl[i + 1][1] -
				  g_gamma_tbl[i][1]) * j) / num;
			info->lcd_gamma_tbl[g_gamma_tbl[i][0] + j] =
				(value << 16) + (value << 8) + value;
		}
	}
	info->lcd_gamma_tbl[255] = (g_gamma_tbl[items - 1][1] << 16) +
		(g_gamma_tbl[items - 1][1] << 8) + g_gamma_tbl[items - 1][1];
}

static void LCD_cfg_panel_info(__panel_para_t *info)
{
	memset(info, 0, sizeof(__panel_para_t));

	info->lcd_x = 800;
	info->lcd_y = 480;
	info->lcd_dclk_freq = 33; /* MHz */

	info->lcd_ht = 1056; /* htotal */
	info->lcd_hbp = 216; /* h back porch */
	info->lcd_hv_hspw = 10; /* hsync */
	info->lcd_vt = 525 * 2; /* vtotal * 2 */
	info->lcd_vbp = 35; /* v back porch */
	info->lcd_hv_vspw = 10;	/* vsync */

	info->lcd_if = 0; /* 0:hv(sync+de); 1:cpu/8080; 2:ttl; 3:lvds */

	info->lcd_hv_if = 0; /* 0:hv parallel; 1:hv serial; 2:ccir656 */

	info->lcd_frm = 0; /* 0:direct; 1:rgb666 dither; 2:rgb656 dither */

	info->lcd_pwm_not_used = 0;
	info->lcd_pwm_ch = 0;
	info->lcd_pwm_freq = 12500; /* Hz */
	info->lcd_pwm_pol = 0;

	info->lcd_io_cfg0 = 0x10000000; /* clock phase */

	info->lcd_gamma_correction_en = 0;
	lcd_gamma_gen(info);
}
#endif

/*
 * lcd flow function
 * hv panel:first lcd_panel_init,than TCON_open
 */
static __s32 LCD_open_flow(__u32 sel)
{
	/* open lcd power, than delay 50ms */
	LCD_OPEN_FUNC(sel, LCD_power_on_generic, 50);
	/* lcd panel initial, than delay 50ms */
	LCD_OPEN_FUNC(sel, LCD_panel_init, 50);
	/* open lcd controller, than delay 500ms */
	LCD_OPEN_FUNC(sel, TCON_open, 500);
	/* open lcd backlight, than delay 0ms */
	LCD_OPEN_FUNC(sel, LCD_bl_open_generic, 0);

	return 0;
}

static __s32 LCD_close_flow(__u32 sel)
{
	/* close lcd backlight, and delay 0ms */
	LCD_CLOSE_FUNC(sel, LCD_bl_close_generic, 0);
	/* close lcd controller, and delay 0ms */
	LCD_CLOSE_FUNC(sel, TCON_close, 0);
	/* lcd panel exit, and delay 0ms */
	LCD_CLOSE_FUNC(sel, LCD_panel_exit, 0);
	/* close lcd power, and delay 1000ms */
	LCD_CLOSE_FUNC(sel, LCD_power_off_generic, 1000);

	return 0;
}

/*
 * lcd panel initial
 * serial io initial
 */
#define td043_spi_scen(sel, data) LCD_GPIO_write(sel, 2, data)
#define td043_spi_scl(sel, data) LCD_GPIO_write(sel, 1, data)
#define td043_spi_sda(sel, data) LCD_GPIO_write(sel, 0, data)

static void td043_spi_wr(__u32 sel, __u32 addr, __u32 value)
{
	__u32 i;
	__u32 data = (addr << 10 | value);
	td043_spi_scen(sel, 1);
	td043_spi_scl(sel, 0);
	td043_spi_scen(sel, 0);
	for (i = 0; i < 16; i++) {
		if (data & 0x8000)
			td043_spi_sda(sel, 1);
		else
			td043_spi_sda(sel, 0);
		data <<= 1;
		LCD_delay_us(10);
		td043_spi_scl(sel, 1);
		LCD_delay_us(10);
		td043_spi_scl(sel, 0);
	}
	td043_spi_scen(sel, 1);
}

static void td043_init(__u32 sel)
{
	td043_spi_wr(sel, 0x02, 0x07);
	td043_spi_wr(sel, 0x03, 0x5f);
	td043_spi_wr(sel, 0x04, 0x17);
	td043_spi_wr(sel, 0x05, 0x20);
	td043_spi_wr(sel, 0x06, 0x08);
	td043_spi_wr(sel, 0x07, 0x20);
	td043_spi_wr(sel, 0x08, 0x20);
	td043_spi_wr(sel, 0x09, 0x20);
	td043_spi_wr(sel, 0x0a, 0x20);
	td043_spi_wr(sel, 0x0b, 0x20);
	td043_spi_wr(sel, 0x0c, 0x20);
	td043_spi_wr(sel, 0x0d, 0x20);
	td043_spi_wr(sel, 0x0e, 0x10);
	td043_spi_wr(sel, 0x0f, 0x10);
	td043_spi_wr(sel, 0x10, 0x10);
	td043_spi_wr(sel, 0x11, 0x15);
	td043_spi_wr(sel, 0x12, 0xaa);
	td043_spi_wr(sel, 0x13, 0xff);
	td043_spi_wr(sel, 0x14, 0x86);
	td043_spi_wr(sel, 0x15, 0x8e);
	td043_spi_wr(sel, 0x16, 0xd6);
	td043_spi_wr(sel, 0x17, 0xfe);
	td043_spi_wr(sel, 0x18, 0x28);
	td043_spi_wr(sel, 0x19, 0x52);
	td043_spi_wr(sel, 0x1a, 0x7c);
	td043_spi_wr(sel, 0x1b, 0xe9);
	td043_spi_wr(sel, 0x1c, 0x42);
	td043_spi_wr(sel, 0x1d, 0x88);
	td043_spi_wr(sel, 0x1e, 0xb8);
	td043_spi_wr(sel, 0x1f, 0xff);
	td043_spi_wr(sel, 0x20, 0xf0);
}

static void LCD_panel_init(__u32 sel)
{
	td043_init(sel);
}

static void LCD_panel_exit(__u32 sel)
{

}

void LCD_get_panel_funs_0(__lcd_panel_fun_t *fun)
{
#ifdef LCD_PARA_USE_CONFIG
	fun->cfg_panel_info = LCD_cfg_panel_info;
#endif
	fun->cfg_open_flow = LCD_open_flow;
	fun->cfg_close_flow = LCD_close_flow;
}

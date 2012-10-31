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

	info->lcd_x = 1280;
	info->lcd_y = 768;
	info->lcd_dclk_freq = 68; /* MHz */

	info->lcd_ht = 1440; /* htotal */
	info->lcd_hbp = 3; /* h back porch */
	info->lcd_hv_hspw = 0; /* hsync */
	info->lcd_vt = 790 * 2; /* vtotal * 2 */
	info->lcd_vbp = 3; /* v back porch */
	info->lcd_hv_vspw = 0; /* vsync */

	info->lcd_if = 0; /* 0:hv(sync+de); 1:cpu/8080; 2:ttl; 3:lvds */

	info->lcd_hv_if = 0; /* 0:hv parallel; 1:hv serial; 2:ccir656 */
	info->lcd_hv_smode = 0; /* 0:RGB888 1:CCIR656 */
	info->lcd_hv_s888_if = 0; /* serial RGB format */
	info->lcd_hv_syuv_if = 0; /* serial YUV format */

	info->lcd_cpu_if = 0; /* 0:18bit 4:16bit */
	info->lcd_frm = 0; /* 0:direct; 1:rgb666 dither; 2:rgb656 dither */

	info->lcd_lvds_ch = 0; /* 0:single link; 1:dual link */
	info->lcd_lvds_mode = 0; /* 0:NS mode; 1:JEIDA mode */
	info->lcd_lvds_bitwidth = 0; /* 0:24bit; 1:18bit */
	info->lcd_lvds_io_cross = 0; /* 0:normal; 1:pn cross */

	info->lcd_pwm_not_used = 0;
	info->lcd_pwm_ch = 0;
	info->lcd_pwm_freq = 10000; /* Hz */
	info->lcd_pwm_pol = 0;

	info->lcd_io_cfg0 = 0x00000000; /* clock phase */

	info->lcd_gamma_correction_en = 0;
	if (info->lcd_gamma_correction_en)
		lcd_gamma_gen(info);
}
#endif

static void LCD_io_init(__u32 sel);
static void LCD_io_exit(__u32 sel);
static void LCD_open_cmd(__u32 sel);
static void LCD_close_cmd(__u32 sel);

static __s32 LCD_open_flow(__u32 sel)
{
	/* open lcd power, and delay 10ms */
	LCD_OPEN_FUNC(sel, LCD_power_on_generic, 50);
	/* request and init gpio, and delay 20ms */
	LCD_OPEN_FUNC(sel, LCD_io_init, 20);
	/* open lcd controller, and delay 200ms */
	LCD_OPEN_FUNC(sel, TCON_open, 500);
	/* use gpio to config lcd module to the  work mode, and delay 10ms */
	LCD_OPEN_FUNC(sel, LCD_open_cmd, 10);
	/* open lcd backlight, and delay 0ms */
	LCD_OPEN_FUNC(sel, LCD_bl_open_generic, 0);

	return 0;
}

static __s32 LCD_close_flow(__u32 sel)
{
	/* close lcd backlight, and delay 0ms */
	LCD_CLOSE_FUNC(sel, LCD_bl_close_generic, 0);
	/*
	 * use gpio to config lcd module to the powerdown/sleep mode,
	 * and delay 0ms
	 */
	LCD_CLOSE_FUNC(sel, LCD_close_cmd, 0);
	/* close lcd controller, and delay 0ms */
	LCD_CLOSE_FUNC(sel, TCON_close, 0);
	/* release gpio, and delay 0ms */
	LCD_CLOSE_FUNC(sel, LCD_io_exit, 0);
	/* close lcd power, and delay 1000ms */
	LCD_CLOSE_FUNC(sel, LCD_power_off_generic, 1000);

	return 0;
}

#define IIC_SCLB_LOW()		LCD_GPIO_write(0, 0, 0)
#define IIC_SCLB_HIGH()		LCD_GPIO_write(0, 0, 1)

#define IIC_SDAB_INPUT_SETUP()	LCD_GPIO_set_attr(0, 1, 0)
#define IIC_SDAB_OUTPUT_SETUP()	LCD_GPIO_set_attr(0, 1, 1)
#define CHECK_SDAB_HIGH()	LCD_GPIO_read(0, 1)
#define IIC_SDAB_LOW()		LCD_GPIO_write(0, 1, 0)
#define IIC_SDAB_HIGH()		LCD_GPIO_write(0, 1, 1)

static __bool i2cB_clock(void)
{
	__bool sample = 0;

	IIC_SCLB_HIGH();
	LCD_delay_us(5);
	IIC_SCLB_LOW();
	LCD_delay_us(5);
	return sample;
}

static __bool i2cB_ack(void)
{
	IIC_SCLB_HIGH();
	IIC_SDAB_INPUT_SETUP();
	LCD_delay_us(5);
	LCD_delay_us(5);
	if (CHECK_SDAB_HIGH()) {
		LCD_delay_us(5);
		IIC_SDAB_OUTPUT_SETUP();
		LCD_delay_us(5);
		IIC_SCLB_LOW();
		LCD_delay_us(5);
		IIC_SDAB_HIGH();
		LCD_delay_us(5);
		return 1;
	} else {
		LCD_delay_us(5);
		IIC_SDAB_OUTPUT_SETUP();
		LCD_delay_us(5);
		IIC_SCLB_LOW();
		LCD_delay_us(5);
		IIC_SDAB_HIGH();
		LCD_delay_us(5);
		return 0;
	}
}

static void i2cBStartA(void)
{
	IIC_SCLB_HIGH();
	IIC_SDAB_HIGH();
	LCD_delay_us(5);
	IIC_SDAB_LOW();
	LCD_delay_us(5);
	IIC_SCLB_LOW();
}

static __bool i2cBStart(void)
{
	IIC_SDAB_HIGH();
	IIC_SCLB_HIGH();
	LCD_delay_us(5);
	IIC_SDAB_INPUT_SETUP();
	if (CHECK_SDAB_HIGH()) {
		IIC_SDAB_OUTPUT_SETUP();
		i2cBStartA();
		return 1;
	}
	return 0;
}

static void i2cBStop(void)
{
	IIC_SDAB_OUTPUT_SETUP();
	IIC_SDAB_LOW();
	LCD_delay_us(5);
	IIC_SCLB_HIGH();
	LCD_delay_us(5);
	IIC_SDAB_HIGH();
	LCD_delay_us(5);
}

static __bool i2cBTransmit(__u8 value)
{
	register __u8 i;

	for (i = 0; i < 8; i++) {
		if ((value & 0x80) == 0x80)
			IIC_SDAB_HIGH();
		else
			IIC_SDAB_LOW();

		value = value << 1;
		i2cB_clock();
	}
	return !i2cB_ack();
}

static __bool i2cBTransmitSubAddr(__u16 value)
{
	register __u8 i;

	for (i = 0; i < 8; i++) {
		if ((value & 0x80) == 0x80)
			IIC_SDAB_HIGH();
		else
			IIC_SDAB_LOW();

		value = value << 1;
		i2cB_clock();
	}
	return !i2cB_ack();
}

static __bool i2cBLocateSubAddr(__u8 slave_addr, __u16 sub_addr)
{
	register __u8 i;
	__u8 sub_addr_hi, sub_addr_low;

	sub_addr_hi = sub_addr >> 8;
	sub_addr_low = sub_addr;
	for (i = 0; i < 3; i++) {
		/* Start I2C */
		if (i2cBStart()) {
			/* Slave address */
			if (i2cBTransmit(slave_addr)) {
				if (i2cBTransmitSubAddr(sub_addr_hi)) {
					if (i2cBTransmitSubAddr(sub_addr_low))
						return 1;
				}
			}
		}
		i2cBStop();
	}
	return 0;
}

static __bool IIC_Write(__u8 slave_addr, __u16 sub_addr, __u8 value)
{
	if (i2cBLocateSubAddr(slave_addr, sub_addr)) {
		if (i2cBTransmit(value)) {
			i2cBStop();
			return 1;
		}
	}
	i2cBStop();
	return 0;
}

static void LCD_io_init(__u32 sel)
{
	/* request SCLB gpio, and output high as default */
	LCD_GPIO_request(sel, 0);
	LCD_GPIO_set_attr(sel, 0, 1);
	LCD_GPIO_write(sel, 0, 1);

	/* request SDAB gpio, and output high as default */
	LCD_GPIO_request(sel, 1);
	LCD_GPIO_set_attr(sel, 1, 1);
	LCD_GPIO_write(sel, 1, 1);
}

static void LCD_io_exit(__u32 sel)
{
	/* release SCLB gpio */
	LCD_GPIO_release(sel, 0);

	/* release SDAB gpio */
	LCD_GPIO_release(sel, 1);
}

static void LCD_open_cmd(__u32 sel)
{
	IIC_Write(0xfc, 0xf830, 0xb2);
	IIC_Write(0xfc, 0xf833, 0xc2);
	IIC_Write(0xfc, 0xf831, 0xf0);
	IIC_Write(0xfc, 0xf840, 0x80);
	IIC_Write(0xfc, 0xf881, 0xec);
}

static void LCD_close_cmd(__u32 sel)
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

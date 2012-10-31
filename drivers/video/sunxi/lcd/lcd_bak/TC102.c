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
	{15, 16},
	{30, 40},
	{45, 55},
	{60, 66},
	{75, 82},
	{90, 96},
	{105, 112},
	{120, 131},
	{135, 145},
	{150, 160},
	{165, 173},
	{180, 187},
	{195, 199},
	{210, 213},
	{225, 224},
	{240, 234},
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
	info->lcd_vt = 790 * 2;	/* votal * 2 */
	info->lcd_vbp = 3; /* v back porch */
	info->lcd_hv_vspw = 0; /* vsync */

	info->lcd_if = 0; /* 0:hv(sync+de); 1:cpu/8080; 2:ttl; 3:lvds */

	info->lcd_hv_if = 0; /* 0:hv parallel; 1:hv serial; 2:ccir656 */
	info->lcd_hv_smode = 0; /* 0:RGB888 1:CCIR656 */
	info->lcd_hv_s888_if = 0; /* serial RGB format */
	info->lcd_hv_syuv_if = 0; /* serial YUV format */

	info->lcd_cpu_if = 0; /* 0:18bit 4:16bit */
	info->lcd_frm = 1; /* 0:direct; 1:rgb666 dither; 2:rgb656 dither */

	info->lcd_lvds_ch = 0; /* 0:single link; 1:dual link */
	info->lcd_lvds_mode = 0; /* 0:NS mode; 1:JEIDA mode */
	info->lcd_lvds_bitwidth = 0; /* 0:24bit; 1:18bit */
	info->lcd_lvds_io_cross = 0; /* 0:normal; 1:pn cross */

	info->lcd_pwm_not_used = 0;
	info->lcd_pwm_ch = 0;
	info->lcd_pwm_freq = 10000; /* Hz */
	info->lcd_pwm_pol = 0;

	info->lcd_io_cfg0 = 0x00000000; /* clock phase */

	info->lcd_gamma_correction_en = 1;
	if (info->lcd_gamma_correction_en)
		lcd_gamma_gen(info);
}
#endif

static void LCD_io_init(__u32 sel);
static void LCD_io_exit(__u32 sel);
static void LCD_open_cmd(__u32 sel);
static void LCD_close_cmd(__u32 sel);
static void LCD_vcc_on(__u32 sel);
static void LCD_vcc_off(__u32 sel);

static __s32 LCD_open_flow(__u32 sel)
{
	/* open lcd power, and delay 10ms */
	LCD_OPEN_FUNC(sel, LCD_vcc_on, 50);
	/* open lcd vcc, and delay 10ms */
	LCD_OPEN_FUNC(sel, LCD_power_on_generic, 50);
	/* request and init gpio, and delay 20ms */
	LCD_OPEN_FUNC(sel, LCD_io_init, 20);
	/* open lcd controller, and delay 200ms */
	LCD_OPEN_FUNC(sel, TCON_open, 500);
	/* use gpio to config lcd module to the work mode, and delay 10ms */
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
	/* close lcd vcc, and delay 0ms */
	LCD_CLOSE_FUNC(sel, LCD_power_off_generic, 0);
	/* close lcd power, and delay 1000ms */
	LCD_CLOSE_FUNC(sel, LCD_vcc_off, 1000);

	return 0;
}

static void LCD_vcc_on(__u32 sel)
{
	user_gpio_set_t gpio_list;
	int hdl;

	gpio_list.port = 8; /* 1:A; 2:B; 3:C; 4:d;5:e;6:f;7:g;8:h... */
	gpio_list.port_num = 6;
	gpio_list.mul_sel = 1;
	gpio_list.pull = 0;
	gpio_list.drv_level = 0;
	gpio_list.data = 1;

	hdl = gpio_request(&gpio_list, 1);
	gpio_release(hdl, 2);
}

static void LCD_vcc_off(__u32 sel)
{
	user_gpio_set_t gpio_list;
	int hdl;

	gpio_list.port = 8; /* 1:A; 2:B; 3:C; ... */
	gpio_list.port_num = 6;
	gpio_list.mul_sel = 1;
	gpio_list.pull = 0;
	gpio_list.drv_level = 0;
	gpio_list.data = 0;

	hdl = gpio_request(&gpio_list, 1);
	gpio_release(hdl, 2);
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
	LCD_delay_us(10);
	IIC_SCLB_LOW();
	LCD_delay_us(10);
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
	LCD_delay_us(10);
	IIC_SDAB_LOW();
	LCD_delay_us(10);
	IIC_SCLB_LOW();
}

static __bool i2cBStart(void)
{
	IIC_SDAB_HIGH();
	IIC_SCLB_HIGH();
	LCD_delay_us(10);
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
	IIC_SDAB_OUTPUT_SETUP();
	LCD_delay_us(5);
	for (i = 0; i < 8; i++) {
		if ((value & 0x80) == 0x80)
			IIC_SDAB_HIGH();
		else
			IIC_SDAB_LOW();

		value = value << 1;
		LCD_delay_us(10);
		i2cB_clock();

	}
	return !i2cB_ack();
}

static __bool i2cBTransmitSubAddr(__u8 value)
{
	register __u8 i;

	for (i = 0; i < 8; i++) {
		if ((value & 0x80) == 0x80)
			IIC_SDAB_HIGH();
		else
			IIC_SDAB_LOW();

		value = value << 1;
		LCD_delay_us(10);
		i2cB_clock();
	}
	return !i2cB_ack();
}

static __bool i2cBLocateSubAddr(__u8 slave_addr, __u8 sub_addr)
{
	register __u8 i;

	for (i = 0; i < 3; i++) {
		if (i2cBStart()) {
			if (i2cBTransmit(slave_addr)) {
				if (i2cBTransmitSubAddr(sub_addr))
					return 1;
			}
		}
		i2cBStop();
	}

	return 0;
}

static __bool IIC_Write_forT101(__u8 slave_addr, __u8 sub_addr, __u8 value)
{
	if (i2cBLocateSubAddr(slave_addr, sub_addr)) {
		if (i2cBTransmit(value)) {
			i2cBStop();
			return 1;
		}
	}
	i2cBStop();

	__inf("-------------DATA SEND FAIL-----------\n");
	return 0;
}

void i2cREAD(void)
{
	register __u8 i;
	__u8 value = 0;

	IIC_SDAB_INPUT_SETUP();

	for (i = 0; i < 8; i++) {
		value = value << 1;

		//i2cB_clock();
		LCD_delay_us(15);
		IIC_SCLB_HIGH();
		LCD_delay_us(10);
		IIC_SCLB_LOW();
		LCD_delay_us(10);

		LCD_delay_us(10);

		if (CHECK_SDAB_HIGH())
			value = value + 1;
	}

	i2cB_ack();
	i2cBStop();

}

static __bool IIC_Read_forT101(__u8 slave_addr1, __u8 sub_addr1)
{
	if (i2cBLocateSubAddr(slave_addr1, sub_addr1))
		i2cREAD();

#if 0
	i2cBLocateSubAddr(slave_addr, sub_addr);
	i2cBStop();
#endif

	return 0;
}

static void LCD_io_init(__u32 sel)
{
	__inf("------+++++++++++++lcd init*************\n");

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
	__inf("------+++++++++++++lcd exit*************\n");

	/* release SCLB gpio */
	LCD_GPIO_release(sel, 0);

	/* release SDAB gpio */
	LCD_GPIO_release(sel, 1);
}

static void LCD_open_cmd(__u32 sel)
{
	__inf("------+++++++++++++into  T201_Initialize*************\n");

	IIC_Write_forT101(0x6c, 0x2a, 0xa2);
	IIC_Write_forT101(0x6c, 0x2d, 0xc2);
	IIC_Write_forT101(0x6c, 0x33, 0x03);
	IIC_Write_forT101(0x6c, 0x36, 0x30);
	IIC_Write_forT101(0x6c, 0x46, 0x49);
	IIC_Write_forT101(0x6c, 0x47, 0x92);
	IIC_Write_forT101(0x6c, 0x48, 0x00);
	IIC_Write_forT101(0x6c, 0x5f, 0x00);
	IIC_Write_forT101(0x6c, 0x60, 0xa5);
	IIC_Write_forT101(0x6c, 0x61, 0x08);
	IIC_Write_forT101(0x6c, 0x62, 0xff);
	IIC_Write_forT101(0x6c, 0x64, 0x00);
	IIC_Write_forT101(0x6c, 0x80, 0x01);
	IIC_Write_forT101(0x6c, 0x81, 0xe4);
	IIC_Write_forT101(0x6c, 0x34, 0x01);

	__inf("-------------out  T201_Initialize*************\n");
}

static void LCD_close_cmd(__u32 sel)
{
}

/*
 * sel: 0:lcd0; 1:lcd1
 * para1 0:inter open 1:inter close 2:lense open 3:lense close
 */
static __s32 LCD_user_defined_func(__u32 sel, __u32 para1, __u32 para2,
				   __u32 para3)
{
	switch (para1) {
	case 0:
		IIC_Write_forT101(0x6c, 0x48, 0x00);
		break;
	case 1:
		IIC_Write_forT101(0x6c, 0x48, 0x03);
		break;
	case 2:
		IIC_Write_forT101(0x6c, 0x36, 0x30);
		break;
	case 3:
		IIC_Write_forT101(0x6c, 0x36, 0x00);
		break;
	default:
		break;
	}
	return 0;
}

void LCD_get_panel_funs_0(__lcd_panel_fun_t *fun)
{
#ifdef LCD_PARA_USE_CONFIG
	fun->cfg_panel_info = LCD_cfg_panel_info;
#endif
	fun->cfg_open_flow = LCD_open_flow;
	fun->cfg_close_flow = LCD_close_flow;
}

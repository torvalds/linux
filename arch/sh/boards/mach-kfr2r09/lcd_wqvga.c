/*
 * KFR2R09 LCD panel support
 *
 * Copyright (C) 2009 Magnus Damm
 *
 * Register settings based on the out-of-tree t33fb.c driver
 * Copyright (C) 2008 Lineo Solutions, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <video/sh_mobile_lcdc.h>
#include <mach/kfr2r09.h>
#include <cpu/sh7724.h>

/* The on-board LCD module is a Hitachi TX07D34VM0AAA. This module is made
 * up of a 240x400 LCD hooked up to a R61517 driver IC. The driver IC is
 * communicating with the main port of the LCDC using an 18-bit SYS interface.
 *
 * The device code for this LCD module is 0x01221517.
 */

static const unsigned char data_frame_if[] = {
	0x02, /* WEMODE: 1=cont, 0=one-shot */
	0x00, 0x00,
	0x00, /* EPF, DFM */
	0x02, /* RIM[1] : 1 (18bpp) */
};

static const unsigned char data_panel[] = {
	0x0b,
	0x63, /* 400 lines */
	0x04, 0x00, 0x00, 0x04, 0x11, 0x00, 0x00,
};

static const unsigned char data_timing[] = {
	0x00, 0x00, 0x13, 0x08, 0x08,
};

static const unsigned char data_timing_src[] = {
	0x11, 0x01, 0x00, 0x01,
};

static const unsigned char data_gamma[] = {
	0x01, 0x02, 0x08, 0x23,	0x03, 0x0c, 0x00, 0x06,	0x00, 0x00,
	0x01, 0x00, 0x0c, 0x23, 0x03, 0x08, 0x02, 0x06, 0x00, 0x00,
};

static const unsigned char data_power[] = {
	0x07, 0xc5, 0xdc, 0x02,	0x33, 0x0a,
};

static unsigned long read_reg(void *sohandle,
			      struct sh_mobile_lcdc_sys_bus_ops *so)
{
	return so->read_data(sohandle);
}

static void write_reg(void *sohandle,
		      struct sh_mobile_lcdc_sys_bus_ops *so,
		      int i, unsigned long v)
{
	if (i)
		so->write_data(sohandle, v); /* PTH4/LCDRS High [param, 17:0] */
	else
		so->write_index(sohandle, v); /* PTH4/LCDRS Low [cmd, 7:0] */
}

static void write_data(void *sohandle,
		       struct sh_mobile_lcdc_sys_bus_ops *so,
		       unsigned char const *data, int no_data)
{
	int i;

	for (i = 0; i < no_data; i++)
		write_reg(sohandle, so, 1, data[i]);
}

static unsigned long read_device_code(void *sohandle,
				      struct sh_mobile_lcdc_sys_bus_ops *so)
{
	unsigned long device_code;

	/* access protect OFF */
	write_reg(sohandle, so, 0, 0xb0);
	write_reg(sohandle, so, 1, 0x00);

	/* deep standby OFF */
	write_reg(sohandle, so, 0, 0xb1);
	write_reg(sohandle, so, 1, 0x00);

	/* device code command */
	write_reg(sohandle, so, 0, 0xbf);
	mdelay(50);

	/* dummy read */
	read_reg(sohandle, so);

	/* read device code */
	device_code = ((read_reg(sohandle, so) & 0xff) << 24);
	device_code |= ((read_reg(sohandle, so) & 0xff) << 16);
	device_code |= ((read_reg(sohandle, so) & 0xff) << 8);
	device_code |= (read_reg(sohandle, so) & 0xff);

	return device_code;
}

static void write_memory_start(void *sohandle,
			       struct sh_mobile_lcdc_sys_bus_ops *so)
{
	write_reg(sohandle, so, 0, 0x2c);
}

static void clear_memory(void *sohandle,
			 struct sh_mobile_lcdc_sys_bus_ops *so)
{
	int i;

	/* write start */
	write_memory_start(sohandle, so);

	/* paint it black */
	for (i = 0; i < (240 * 400); i++)
		write_reg(sohandle, so, 1, 0x00);
}

static void display_on(void *sohandle,
		       struct sh_mobile_lcdc_sys_bus_ops *so)
{
	/* access protect off */
	write_reg(sohandle, so, 0, 0xb0);
	write_reg(sohandle, so, 1, 0x00);

	/* exit deep standby mode */
	write_reg(sohandle, so, 0, 0xb1);
	write_reg(sohandle, so, 1, 0x00);

	/* frame memory I/F */
	write_reg(sohandle, so, 0, 0xb3);
	write_data(sohandle, so, data_frame_if, ARRAY_SIZE(data_frame_if));

	/* display mode and frame memory write mode */
	write_reg(sohandle, so, 0, 0xb4);
	write_reg(sohandle, so, 1, 0x00); /* DBI, internal clock */

	/* panel */
	write_reg(sohandle, so, 0, 0xc0);
	write_data(sohandle, so, data_panel, ARRAY_SIZE(data_panel));

	/* timing (normal) */
	write_reg(sohandle, so, 0, 0xc1);
	write_data(sohandle, so, data_timing, ARRAY_SIZE(data_timing));

	/* timing (partial) */
	write_reg(sohandle, so, 0, 0xc2);
	write_data(sohandle, so, data_timing, ARRAY_SIZE(data_timing));

	/* timing (idle) */
	write_reg(sohandle, so, 0, 0xc3);
	write_data(sohandle, so, data_timing, ARRAY_SIZE(data_timing));

	/* timing (source/VCOM/gate driving) */
	write_reg(sohandle, so, 0, 0xc4);
	write_data(sohandle, so, data_timing_src, ARRAY_SIZE(data_timing_src));

	/* gamma (red) */
	write_reg(sohandle, so, 0, 0xc8);
	write_data(sohandle, so, data_gamma, ARRAY_SIZE(data_gamma));

	/* gamma (green) */
	write_reg(sohandle, so, 0, 0xc9);
	write_data(sohandle, so, data_gamma, ARRAY_SIZE(data_gamma));

	/* gamma (blue) */
	write_reg(sohandle, so, 0, 0xca);
	write_data(sohandle, so, data_gamma, ARRAY_SIZE(data_gamma));

	/* power (common) */
	write_reg(sohandle, so, 0, 0xd0);
	write_data(sohandle, so, data_power, ARRAY_SIZE(data_power));

	/* VCOM */
	write_reg(sohandle, so, 0, 0xd1);
	write_reg(sohandle, so, 1, 0x00);
	write_reg(sohandle, so, 1, 0x0f);
	write_reg(sohandle, so, 1, 0x02);

	/* power (normal) */
	write_reg(sohandle, so, 0, 0xd2);
	write_reg(sohandle, so, 1, 0x63);
	write_reg(sohandle, so, 1, 0x24);

	/* power (partial) */
	write_reg(sohandle, so, 0, 0xd3);
	write_reg(sohandle, so, 1, 0x63);
	write_reg(sohandle, so, 1, 0x24);

	/* power (idle) */
	write_reg(sohandle, so, 0, 0xd4);
	write_reg(sohandle, so, 1, 0x63);
	write_reg(sohandle, so, 1, 0x24);

	write_reg(sohandle, so, 0, 0xd8);
	write_reg(sohandle, so, 1, 0x77);
	write_reg(sohandle, so, 1, 0x77);

	/* TE signal */
	write_reg(sohandle, so, 0, 0x35);
	write_reg(sohandle, so, 1, 0x00);

	/* TE signal line */
	write_reg(sohandle, so, 0, 0x44);
	write_reg(sohandle, so, 1, 0x00);
	write_reg(sohandle, so, 1, 0x00);

	/* column address */
	write_reg(sohandle, so, 0, 0x2a);
	write_reg(sohandle, so, 1, 0x00);
	write_reg(sohandle, so, 1, 0x00);
	write_reg(sohandle, so, 1, 0x00);
	write_reg(sohandle, so, 1, 0xef);

	/* page address */
	write_reg(sohandle, so, 0, 0x2b);
	write_reg(sohandle, so, 1, 0x00);
	write_reg(sohandle, so, 1, 0x00);
	write_reg(sohandle, so, 1, 0x01);
	write_reg(sohandle, so, 1, 0x8f);

	/* exit sleep mode */
	write_reg(sohandle, so, 0, 0x11);

	mdelay(120);

	/* clear vram */
	clear_memory(sohandle, so);

	/* display ON */
	write_reg(sohandle, so, 0, 0x29);
	mdelay(1);

	write_memory_start(sohandle, so);
}

int kfr2r09_lcd_setup(void *board_data, void *sohandle,
		      struct sh_mobile_lcdc_sys_bus_ops *so)
{
	/* power on */
	gpio_set_value(GPIO_PTF4, 0);  /* PROTECT/ -> L */
	gpio_set_value(GPIO_PTE4, 0);  /* LCD_RST/ -> L */
	gpio_set_value(GPIO_PTF4, 1);  /* PROTECT/ -> H */
	udelay(1100);
	gpio_set_value(GPIO_PTE4, 1);  /* LCD_RST/ -> H */
	udelay(10);
	gpio_set_value(GPIO_PTF4, 0);  /* PROTECT/ -> L */
	mdelay(20);

	if (read_device_code(sohandle, so) != 0x01221517)
		return -ENODEV;

	pr_info("KFR2R09 WQVGA LCD Module detected.\n");

	display_on(sohandle, so);
	return 0;
}

void kfr2r09_lcd_start(void *board_data, void *sohandle,
		       struct sh_mobile_lcdc_sys_bus_ops *so)
{
	write_memory_start(sohandle, so);
}

#define CTRL_CKSW       0x10
#define CTRL_C10        0x20
#define CTRL_CPSW       0x80
#define MAIN_MLED4      0x40
#define MAIN_MSW        0x80

static int kfr2r09_lcd_backlight(int on)
{
	struct i2c_adapter *a;
	struct i2c_msg msg;
	unsigned char buf[2];
	int ret;

	a = i2c_get_adapter(0);
	if (!a)
		return -ENODEV;

	buf[0] = 0x00;
	if (on)
		buf[1] = CTRL_CPSW | CTRL_C10 | CTRL_CKSW;
	else
		buf[1] = 0;

	msg.addr = 0x75;
	msg.buf = buf;
	msg.len = 2;
	msg.flags = 0;
	ret = i2c_transfer(a, &msg, 1);
	if (ret != 1)
		return -ENODEV;

	buf[0] = 0x01;
	if (on)
		buf[1] = MAIN_MSW | MAIN_MLED4 | 0x0c;
	else
		buf[1] = 0;

	msg.addr = 0x75;
	msg.buf = buf;
	msg.len = 2;
	msg.flags = 0;
	ret = i2c_transfer(a, &msg, 1);
	if (ret != 1)
		return -ENODEV;

	return 0;
}

void kfr2r09_lcd_on(void *board_data, struct fb_info *info)
{
	kfr2r09_lcd_backlight(1);
}

void kfr2r09_lcd_off(void *board_data)
{
	kfr2r09_lcd_backlight(0);
}

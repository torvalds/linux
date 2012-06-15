/*
 * Support for SuperH MigoR Quarter VGA LCD Panel
 *
 * Copyright (C) 2008 Magnus Damm
 *
 * Based on lcd_powertip.c from Kenati Technologies Pvt Ltd.
 * Copyright (c) 2007 Ujjwal Pande <ujjwal@kenati.com>,
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <video/sh_mobile_lcdc.h>
#include <cpu/sh7722.h>
#include <mach/migor.h>

/* LCD Module is a PH240320T according to board schematics. This module
 * is made up of a 240x320 LCD hooked up to a R61505U (or HX8347-A01?)
 * Driver IC. This IC is connected to the SH7722 built-in LCDC using a
 * SYS-80 interface configured in 16 bit mode.
 *
 * Index 0: "Device Code Read" returns 0x1505.
 */

static void reset_lcd_module(void)
{
	gpio_set_value(GPIO_PTH2, 0);
	mdelay(2);
	gpio_set_value(GPIO_PTH2, 1);
	mdelay(1);
}

/* DB0-DB7 are connected to D1-D8, and DB8-DB15 to D10-D17 */

static unsigned long adjust_reg18(unsigned short data)
{
	unsigned long tmp1, tmp2;

	tmp1 = (data<<1 | 0x00000001) & 0x000001FF;
	tmp2 = (data<<2 | 0x00000200) & 0x0003FE00;
	return tmp1 | tmp2;
}

static void write_reg(void *sys_ops_handle,
		       struct sh_mobile_lcdc_sys_bus_ops *sys_ops,
		       unsigned short reg, unsigned short data)
{
	sys_ops->write_index(sys_ops_handle, adjust_reg18(reg << 8 | data));
}

static void write_reg16(void *sys_ops_handle,
			struct sh_mobile_lcdc_sys_bus_ops *sys_ops,
			unsigned short reg, unsigned short data)
{
	sys_ops->write_index(sys_ops_handle, adjust_reg18(reg));
	sys_ops->write_data(sys_ops_handle, adjust_reg18(data));
}

static unsigned long read_reg16(void *sys_ops_handle,
				struct sh_mobile_lcdc_sys_bus_ops *sys_ops,
				unsigned short reg)
{
	unsigned long data;

	sys_ops->write_index(sys_ops_handle, adjust_reg18(reg));
	data = sys_ops->read_data(sys_ops_handle);
	return ((data >> 1) & 0xff) | ((data >> 2) & 0xff00);
}

static void migor_lcd_qvga_seq(void *sys_ops_handle,
			       struct sh_mobile_lcdc_sys_bus_ops *sys_ops,
			       unsigned short const *data, int no_data)
{
	int i;

	for (i = 0; i < no_data; i += 2)
		write_reg16(sys_ops_handle, sys_ops, data[i], data[i + 1]);
}

static const unsigned short sync_data[] = {
	0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
};

static const unsigned short magic0_data[] = {
	0x0060, 0x2700, 0x0008, 0x0808, 0x0090, 0x001A, 0x0007, 0x0001,
	0x0017, 0x0001, 0x0019, 0x0000, 0x0010, 0x17B0, 0x0011, 0x0116,
	0x0012, 0x0198, 0x0013, 0x1400, 0x0029, 0x000C, 0x0012, 0x01B8,
};

static const unsigned short magic1_data[] = {
	0x0030, 0x0307, 0x0031, 0x0303, 0x0032, 0x0603, 0x0033, 0x0202,
	0x0034, 0x0202, 0x0035, 0x0202, 0x0036, 0x1F1F, 0x0037, 0x0303,
	0x0038, 0x0303, 0x0039, 0x0603, 0x003A, 0x0202, 0x003B, 0x0102,
	0x003C, 0x0204, 0x003D, 0x0000, 0x0001, 0x0100, 0x0002, 0x0300,
	0x0003, 0x5028, 0x0020, 0x00ef, 0x0021, 0x0000, 0x0004, 0x0000,
	0x0009, 0x0000, 0x000A, 0x0008, 0x000C, 0x0000, 0x000D, 0x0000,
	0x0015, 0x8000,
};

static const unsigned short magic2_data[] = {
	0x0061, 0x0001, 0x0092, 0x0100, 0x0093, 0x0001, 0x0007, 0x0021,
};

static const unsigned short magic3_data[] = {
	0x0010, 0x16B0, 0x0011, 0x0111, 0x0007, 0x0061,
};

int migor_lcd_qvga_setup(void *sohandle, struct sh_mobile_lcdc_sys_bus_ops *so)
{
	unsigned long xres = 320;
	unsigned long yres = 240;
	int k;

	reset_lcd_module();
	migor_lcd_qvga_seq(sohandle, so, sync_data, ARRAY_SIZE(sync_data));

	if (read_reg16(sohandle, so, 0) != 0x1505)
		return -ENODEV;

	pr_info("Migo-R QVGA LCD Module detected.\n");

	migor_lcd_qvga_seq(sohandle, so, sync_data, ARRAY_SIZE(sync_data));
	write_reg16(sohandle, so, 0x00A4, 0x0001);
	mdelay(10);

	migor_lcd_qvga_seq(sohandle, so, magic0_data, ARRAY_SIZE(magic0_data));
	mdelay(100);

	migor_lcd_qvga_seq(sohandle, so, magic1_data, ARRAY_SIZE(magic1_data));
	write_reg16(sohandle, so, 0x0050, 0xef - (yres - 1));
	write_reg16(sohandle, so, 0x0051, 0x00ef);
	write_reg16(sohandle, so, 0x0052, 0x0000);
	write_reg16(sohandle, so, 0x0053, xres - 1);

	migor_lcd_qvga_seq(sohandle, so, magic2_data, ARRAY_SIZE(magic2_data));
	mdelay(10);

	migor_lcd_qvga_seq(sohandle, so, magic3_data, ARRAY_SIZE(magic3_data));
	mdelay(40);

	/* clear GRAM to avoid displaying garbage */

	write_reg16(sohandle, so, 0x0020, 0x0000); /* horiz addr */
	write_reg16(sohandle, so, 0x0021, 0x0000); /* vert addr */

	for (k = 0; k < (xres * 256); k++) /* yes, 256 words per line */
		write_reg16(sohandle, so, 0x0022, 0x0000);

	write_reg16(sohandle, so, 0x0020, 0x0000); /* reset horiz addr */
	write_reg16(sohandle, so, 0x0021, 0x0000); /* reset vert addr */
	write_reg16(sohandle, so, 0x0007, 0x0173);
	mdelay(40);

	/* enable display */
	write_reg(sohandle, so, 0x00, 0x22);
	mdelay(100);
	return 0;
}

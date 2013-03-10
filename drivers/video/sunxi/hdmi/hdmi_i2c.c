/*
 * Copyright (c) 2013 Jari Helaakoski <tekkuli@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <linux/i2c.h>
#include "../disp/sunxi_disp_regs.h"
#include "hdmi_core.h"

#define Abort_Current_Operation			0
#define Special_Offset_Address_Read		1
#define Explicit_Offset_Address_Write		2
#define Implicit_Offset_Address_Write		3
#define Explicit_Offset_Address_Read		4
#define Implicit_Offset_Address_Read		5
#define Explicit_Offset_Address_E_DDC_Read	6
#define Implicit_Offset_Address_E_DDC_Read	7

#define Command_Ok 0x11

struct i2c_adapter sunxi_hdmi_i2c_adapter;

static int init_connection(void __iomem *base_addr)
{
	/* Make sure that HDMI core functionality is initialized.
	 Currently support I2C only when HDMI is connected */
	if (!(readl(HDMI_HPD) & 0x01)) {
		pr_info("HDMI not connected\n");
		return -EIO;
	}

	/* Reset */
	writel(0, HDMI_I2C_GENERAL_2);
	writel(0x80000001, HDMI_I2C_GENERAL);
	hdmi_delay_ms(1);

	if (readl(HDMI_I2C_GENERAL) & 0x1) {
		pr_info("EDID not ready\n");
		return -EIO;
	}

	/* N = 5,M=1 Fscl= Ftmds/2/10/2^N/(M+1) */
	writel(0x0d, HDMI_I2C_CLK);

	/* ddc address  0x60 */
	/*writeb(0x60, HDMI_BASE + HDMI_I2C_LINE_CTRL);*/

	/* slave address  0xa0 */
	/*writeb(0xa0 >> 1, HDMI_BASE + HDMI_I2C_LINE_CTRL);*/

	/* enable analog sda/scl pad */
	writel((0 << 12) | (3 << 8), HDMI_I2C_LINE_CTRL);
	return 0;
}

static int do_command(void __iomem *base_addr,
		int command, u8 address, u8 len, u8 chip_addr)
{
	__u32 begin_ms, end_ms;
	u8 block = 0;

	 /* set FIFO read */
	writel(readl(HDMI_I2C_GENERAL) & 0xfffffeff,
		HDMI_I2C_GENERAL);

	writel((block << 24) | (0x60 << 16) | (chip_addr << 8) |
			address, HDMI_I2C_ADDR);

	/* FIFO address clear */
	writel(readl(HDMI_I2C_GENERAL_2) | 0x80000000,
		HDMI_I2C_GENERAL_2);

	 /* nbyte to access */
	writel(len, HDMI_I2C_DATA_LENGTH);

	 /* set cmd type */
	writel(command, HDMI_I2C_CMD);

	 /* start and cmd */
	writel(readl(HDMI_I2C_GENERAL) | 0x40000000,
			HDMI_I2C_GENERAL);

	begin_ms = (jiffies * 1000) / HZ;
	while (readl(HDMI_I2C_GENERAL) & 0x40000000) {
		end_ms = (jiffies * 1000) / HZ;
		if ((end_ms - begin_ms) > 1000) {
			pr_warning("ddc read timeout 0x%X\n",
				readl(HDMI_I2C_GENERAL));
			return -ETIMEDOUT;
		}
		schedule();
	}

	if (Command_Ok != readl(HDMI_I2C_STATUS))
		return -EIO;

	return 0;
}

static int do_read(void __iomem *base_addr,
		struct i2c_msg *msg, int command, u8 chip_addr)
{
	int i = 0;
	int err = 0;
	u8 bufPos = 0;

	while (bufPos < msg->len) {
		u8 readLen = (msg->len > 16) ? 16 : msg->len;

		err = do_command(base_addr, command,
				msg->addr, readLen,
				chip_addr);

		if (err != 0)
			return err;

		for (i = 0; i < readLen; i++)
			*msg->buf++ = readb(HDMI_I2C_DATA);

		bufPos += readLen;
		chip_addr += readLen;
	}
	return err;
}

static int hdmi_i2c_sunxi_xfer(struct i2c_adapter *adap,
		struct i2c_msg *msgs, int num)
{
	int i = 0;
	int err = 0;
	u8 chip_addr = 0;
	int command =  Implicit_Offset_Address_Read;
	void __iomem *base_addr = (void __iomem *)adap->algo_data;

	if (init_connection(base_addr))
		return -EIO;

	for (i = 0; i < num; i++) {
		if (msgs[i].flags & I2C_M_RD) {
			err = do_read(base_addr, &msgs[i], command, chip_addr);
		} else {
			command = Explicit_Offset_Address_Read;
			chip_addr = *msgs[i].buf;
			err = do_command(base_addr, command,
				msgs[i].addr, 0,
				chip_addr);
		}

		pr_debug("%s msgs[i].addr:0x%X msgs[i].len:%i"
				" msgs[i].flags:0x%X err:%i\n",
				__func__, msgs[i].addr, msgs[i].len,
				msgs[i].flags, err);

		if (err)
			break;

	}

	if (err)
		return err;

	return i;
}

static unsigned int hdmi_i2c_sunxi_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C|I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm hdmi_i2c_sunxi_algorithm = {
	.master_xfer	  = hdmi_i2c_sunxi_xfer,
	.functionality	  = hdmi_i2c_sunxi_functionality,
};

int hdmi_i2c_sunxi_probe(struct platform_device *dev)
{
	int ret;
	strlcpy(sunxi_hdmi_i2c_adapter.name, "sunxi-hdmi-i2c",
			sizeof(sunxi_hdmi_i2c_adapter.name));
	sunxi_hdmi_i2c_adapter.owner   = THIS_MODULE;
	sunxi_hdmi_i2c_adapter.retries = 2;
	sunxi_hdmi_i2c_adapter.timeout = 5*HZ;
	sunxi_hdmi_i2c_adapter.class   = I2C_CLASS_DDC;
	sunxi_hdmi_i2c_adapter.algo = &hdmi_i2c_sunxi_algorithm;
	sunxi_hdmi_i2c_adapter.dev.parent = &dev->dev;
	sunxi_hdmi_i2c_adapter.algo_data  = (void *)0xf1c16000;


	ret = i2c_add_adapter(&sunxi_hdmi_i2c_adapter);
	if (ret < 0) {
		pr_warning("I2C: Failed to add bus\n");
		return ret;
	}

	platform_set_drvdata(dev, &sunxi_hdmi_i2c_adapter);



	pr_info("I2C: %s: HDMI I2C adapter\n",
			dev_name(&sunxi_hdmi_i2c_adapter.dev));
	return 0;
}

int hdmi_i2c_sunxi_remove(struct platform_device *dev)
{
	struct i2c_adapter *i2c = platform_get_drvdata(dev);
	if (i2c)
		i2c_del_adapter(i2c);

	return 0;
}

#if 0 /* Legacy comment */
void send_ini_sequence()
{
	int i, j;

	set_wbit(HDMI_I2C_UNKNOWN_0, BIT3);
	for (i = 0; i < 9; i++) {
		for (j = 0; j < 200; j++) /*for simulation, delete it*/
			;
		clr_wbit(HDMI_I2C_UNKNOWN_0, BIT2);

		for (j = 0; j < 200; j++) /*for simulation, delete it*/
			;
		set_wbit(HDMI_I2C_UNKNOWN_0, BIT2);
	}

	clr_wbit(HDMI_I2C_UNKNOWN_0, BIT3);
	clr_wbit(HDMI_I2C_UNKNOWN_0, BIT1);
}
#endif

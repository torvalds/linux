/*
 *  Driver for the Conexant CX25821 PCIe bridge
 *
 *  Copyright (C) 2009 Conexant Systems Inc.
 *  Authors  <shu.lin@conexant.com>, <hiep.huynh@conexant.com>
 *	Based on Steven Toth <stoth@linuxtv.org> cx23885 driver
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/i2c.h>
#include "cx25821.h"

static unsigned int i2c_debug;
module_param(i2c_debug, int, 0644);
MODULE_PARM_DESC(i2c_debug, "enable debug messages [i2c]");

static unsigned int i2c_scan;
module_param(i2c_scan, int, 0444);
MODULE_PARM_DESC(i2c_scan, "scan i2c bus at insmod time");

#define dprintk(level, fmt, arg...)					\
do {									\
	if (i2c_debug >= level)						\
		printk(KERN_DEBUG "%s/0: " fmt, dev->name, ##arg);	\
} while (0)

#define I2C_WAIT_DELAY 32
#define I2C_WAIT_RETRY 64

#define I2C_EXTEND  (1 << 3)
#define I2C_NOSTOP  (1 << 4)

static inline int i2c_slave_did_ack(struct i2c_adapter *i2c_adap)
{
	struct cx25821_i2c *bus = i2c_adap->algo_data;
	struct cx25821_dev *dev = bus->dev;
	return cx_read(bus->reg_stat) & 0x01;
}

static inline int i2c_is_busy(struct i2c_adapter *i2c_adap)
{
	struct cx25821_i2c *bus = i2c_adap->algo_data;
	struct cx25821_dev *dev = bus->dev;
	return cx_read(bus->reg_stat) & 0x02 ? 1 : 0;
}

static int i2c_wait_done(struct i2c_adapter *i2c_adap)
{
	int count;

	for (count = 0; count < I2C_WAIT_RETRY; count++) {
		if (!i2c_is_busy(i2c_adap))
			break;
		udelay(I2C_WAIT_DELAY);
	}

	if (I2C_WAIT_RETRY == count)
		return 0;

	return 1;
}

static int i2c_sendbytes(struct i2c_adapter *i2c_adap,
			 const struct i2c_msg *msg, int joined_rlen)
{
	struct cx25821_i2c *bus = i2c_adap->algo_data;
	struct cx25821_dev *dev = bus->dev;
	u32 wdata, addr, ctrl;
	int retval, cnt;

	if (joined_rlen)
		dprintk(1, "%s(msg->wlen=%d, nextmsg->rlen=%d)\n", __func__,
			msg->len, joined_rlen);
	else
		dprintk(1, "%s(msg->len=%d)\n", __func__, msg->len);

	/* Deal with i2c probe functions with zero payload */
	if (msg->len == 0) {
		cx_write(bus->reg_addr, msg->addr << 25);
		cx_write(bus->reg_ctrl, bus->i2c_period | (1 << 2));

		if (!i2c_wait_done(i2c_adap))
			return -EIO;

		if (!i2c_slave_did_ack(i2c_adap))
			return -EIO;

		dprintk(1, "%s(): returns 0\n", __func__);
		return 0;
	}

	/* dev, reg + first byte */
	addr = (msg->addr << 25) | msg->buf[0];
	wdata = msg->buf[0];

	ctrl = bus->i2c_period | (1 << 12) | (1 << 2);

	if (msg->len > 1)
		ctrl |= I2C_NOSTOP | I2C_EXTEND;
	else if (joined_rlen)
		ctrl |= I2C_NOSTOP;

	cx_write(bus->reg_addr, addr);
	cx_write(bus->reg_wdata, wdata);
	cx_write(bus->reg_ctrl, ctrl);

	retval = i2c_wait_done(i2c_adap);
	if (retval < 0)
		goto err;

	if (retval == 0)
		goto eio;

	if (i2c_debug) {
		if (!(ctrl & I2C_NOSTOP))
			printk(" >\n");
	}

	for (cnt = 1; cnt < msg->len; cnt++) {
		/* following bytes */
		wdata = msg->buf[cnt];
		ctrl = bus->i2c_period | (1 << 12) | (1 << 2);

		if (cnt < msg->len - 1)
			ctrl |= I2C_NOSTOP | I2C_EXTEND;
		else if (joined_rlen)
			ctrl |= I2C_NOSTOP;

		cx_write(bus->reg_addr, addr);
		cx_write(bus->reg_wdata, wdata);
		cx_write(bus->reg_ctrl, ctrl);

		retval = i2c_wait_done(i2c_adap);
		if (retval < 0)
			goto err;

		if (retval == 0)
			goto eio;

		if (i2c_debug) {
			dprintk(1, " %02x", msg->buf[cnt]);
			if (!(ctrl & I2C_NOSTOP))
				dprintk(1, " >\n");
		}
	}

	return msg->len;

eio:
	retval = -EIO;
err:
	if (i2c_debug)
		pr_err(" ERR: %d\n", retval);
	return retval;
}

static int i2c_readbytes(struct i2c_adapter *i2c_adap,
			 const struct i2c_msg *msg, int joined)
{
	struct cx25821_i2c *bus = i2c_adap->algo_data;
	struct cx25821_dev *dev = bus->dev;
	u32 ctrl, cnt;
	int retval;

	if (i2c_debug && !joined)
		dprintk(1, "6-%s(msg->len=%d)\n", __func__, msg->len);

	/* Deal with i2c probe functions with zero payload */
	if (msg->len == 0) {
		cx_write(bus->reg_addr, msg->addr << 25);
		cx_write(bus->reg_ctrl, bus->i2c_period | (1 << 2) | 1);
		if (!i2c_wait_done(i2c_adap))
			return -EIO;
		if (!i2c_slave_did_ack(i2c_adap))
			return -EIO;

		dprintk(1, "%s(): returns 0\n", __func__);
		return 0;
	}

	if (i2c_debug) {
		if (joined)
			dprintk(1, " R");
		else
			dprintk(1, " <R %02x", (msg->addr << 1) + 1);
	}

	for (cnt = 0; cnt < msg->len; cnt++) {

		ctrl = bus->i2c_period | (1 << 12) | (1 << 2) | 1;

		if (cnt < msg->len - 1)
			ctrl |= I2C_NOSTOP | I2C_EXTEND;

		cx_write(bus->reg_addr, msg->addr << 25);
		cx_write(bus->reg_ctrl, ctrl);

		retval = i2c_wait_done(i2c_adap);
		if (retval < 0)
			goto err;
		if (retval == 0)
			goto eio;
		msg->buf[cnt] = cx_read(bus->reg_rdata) & 0xff;

		if (i2c_debug) {
			dprintk(1, " %02x", msg->buf[cnt]);
			if (!(ctrl & I2C_NOSTOP))
				dprintk(1, " >\n");
		}
	}

	return msg->len;
eio:
	retval = -EIO;
err:
	if (i2c_debug)
		pr_err(" ERR: %d\n", retval);
	return retval;
}

static int i2c_xfer(struct i2c_adapter *i2c_adap, struct i2c_msg *msgs, int num)
{
	struct cx25821_i2c *bus = i2c_adap->algo_data;
	struct cx25821_dev *dev = bus->dev;
	int i, retval = 0;

	dprintk(1, "%s(num = %d)\n", __func__, num);

	for (i = 0; i < num; i++) {
		dprintk(1, "%s(num = %d) addr = 0x%02x  len = 0x%x\n",
			__func__, num, msgs[i].addr, msgs[i].len);

		if (msgs[i].flags & I2C_M_RD) {
			/* read */
			retval = i2c_readbytes(i2c_adap, &msgs[i], 0);
		} else if (i + 1 < num && (msgs[i + 1].flags & I2C_M_RD) &&
			   msgs[i].addr == msgs[i + 1].addr) {
			/* write then read from same address */
			retval = i2c_sendbytes(i2c_adap, &msgs[i],
					msgs[i + 1].len);

			if (retval < 0)
				goto err;
			i++;
			retval = i2c_readbytes(i2c_adap, &msgs[i], 1);
		} else {
			/* write */
			retval = i2c_sendbytes(i2c_adap, &msgs[i], 0);
		}

		if (retval < 0)
			goto err;
	}
	return num;

err:
	return retval;
}


static u32 cx25821_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_EMUL | I2C_FUNC_I2C | I2C_FUNC_SMBUS_WORD_DATA |
		I2C_FUNC_SMBUS_READ_WORD_DATA | I2C_FUNC_SMBUS_WRITE_WORD_DATA;
}

static const struct i2c_algorithm cx25821_i2c_algo_template = {
	.master_xfer = i2c_xfer,
	.functionality = cx25821_functionality,
#ifdef NEED_ALGO_CONTROL
	.algo_control = dummy_algo_control,
#endif
};

static struct i2c_adapter cx25821_i2c_adap_template = {
	.name = "cx25821",
	.owner = THIS_MODULE,
	.algo = &cx25821_i2c_algo_template,
};

static struct i2c_client cx25821_i2c_client_template = {
	.name = "cx25821 internal",
};

/* init + register i2c adapter */
int cx25821_i2c_register(struct cx25821_i2c *bus)
{
	struct cx25821_dev *dev = bus->dev;

	dprintk(1, "%s(bus = %d)\n", __func__, bus->nr);

	bus->i2c_adap = cx25821_i2c_adap_template;
	bus->i2c_client = cx25821_i2c_client_template;
	bus->i2c_adap.dev.parent = &dev->pci->dev;

	strlcpy(bus->i2c_adap.name, bus->dev->name, sizeof(bus->i2c_adap.name));

	bus->i2c_adap.algo_data = bus;
	i2c_set_adapdata(&bus->i2c_adap, &dev->v4l2_dev);
	i2c_add_adapter(&bus->i2c_adap);

	bus->i2c_client.adapter = &bus->i2c_adap;

	/* set up the I2c */
	bus->i2c_client.addr = (0x88 >> 1);

	return bus->i2c_rc;
}

int cx25821_i2c_unregister(struct cx25821_i2c *bus)
{
	i2c_del_adapter(&bus->i2c_adap);
	return 0;
}

#if 0 /* Currently unused */
static void cx25821_av_clk(struct cx25821_dev *dev, int enable)
{
	/* write 0 to bus 2 addr 0x144 via i2x_xfer() */
	char buffer[3];
	struct i2c_msg msg;
	dprintk(1, "%s(enabled = %d)\n", __func__, enable);

	/* Register 0x144 */
	buffer[0] = 0x01;
	buffer[1] = 0x44;
	if (enable == 1)
		buffer[2] = 0x05;
	else
		buffer[2] = 0x00;

	msg.addr = 0x44;
	msg.flags = I2C_M_TEN;
	msg.len = 3;
	msg.buf = buffer;

	i2c_xfer(&dev->i2c_bus[0].i2c_adap, &msg, 1);
}
#endif

int cx25821_i2c_read(struct cx25821_i2c *bus, u16 reg_addr, int *value)
{
	struct i2c_client *client = &bus->i2c_client;
	int v = 0;
	u8 addr[2] = { 0, 0 };
	u8 buf[4] = { 0, 0, 0, 0 };

	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 2,
			.buf = addr,
		}, {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 4,
			.buf = buf,
		}
	};

	addr[0] = (reg_addr >> 8);
	addr[1] = (reg_addr & 0xff);
	msgs[0].addr = 0x44;
	msgs[1].addr = 0x44;

	i2c_xfer(client->adapter, msgs, 2);

	v = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
	*value = v;

	return v;
}

int cx25821_i2c_write(struct cx25821_i2c *bus, u16 reg_addr, int value)
{
	struct i2c_client *client = &bus->i2c_client;
	int retval = 0;
	u8 buf[6] = { 0, 0, 0, 0, 0, 0 };

	struct i2c_msg msgs[1] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 6,
			.buf = buf,
		}
	};

	buf[0] = reg_addr >> 8;
	buf[1] = reg_addr & 0xff;
	buf[5] = (value >> 24) & 0xff;
	buf[4] = (value >> 16) & 0xff;
	buf[3] = (value >> 8) & 0xff;
	buf[2] = value & 0xff;
	client->flags = 0;
	msgs[0].addr = 0x44;

	retval = i2c_xfer(client->adapter, msgs, 1);

	return retval;
}

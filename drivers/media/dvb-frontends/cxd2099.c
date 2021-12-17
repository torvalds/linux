// SPDX-License-Identifier: GPL-2.0
/*
 * cxd2099.c: Driver for the Sony CXD2099AR Common Interface Controller
 *
 * Copyright (C) 2010-2013 Digital Devices GmbH
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/io.h>

#include "cxd2099.h"

static int buffermode;
module_param(buffermode, int, 0444);
MODULE_PARM_DESC(buffermode, "Enable CXD2099AR buffer mode (default: disabled)");

static int read_data(struct dvb_ca_en50221 *ca, int slot, u8 *ebuf, int ecount);

struct cxd {
	struct dvb_ca_en50221 en;

	struct cxd2099_cfg cfg;
	struct i2c_client *client;
	struct regmap *regmap;

	u8     regs[0x23];
	u8     lastaddress;
	u8     clk_reg_f;
	u8     clk_reg_b;
	int    mode;
	int    ready;
	int    dr;
	int    write_busy;
	int    slot_stat;

	u8     amem[1024];
	int    amem_read;

	int    cammode;
	struct mutex lock; /* device access lock */

	u8     rbuf[1028];
	u8     wbuf[1028];
};

static int read_block(struct cxd *ci, u8 adr, u8 *data, u16 n)
{
	int status = 0;

	if (ci->lastaddress != adr)
		status = regmap_write(ci->regmap, 0, adr);
	if (!status) {
		ci->lastaddress = adr;

		while (n) {
			int len = n;

			if (ci->cfg.max_i2c && len > ci->cfg.max_i2c)
				len = ci->cfg.max_i2c;
			status = regmap_raw_read(ci->regmap, 1, data, len);
			if (status)
				return status;
			data += len;
			n -= len;
		}
	}
	return status;
}

static int read_reg(struct cxd *ci, u8 reg, u8 *val)
{
	return read_block(ci, reg, val, 1);
}

static int read_pccard(struct cxd *ci, u16 address, u8 *data, u8 n)
{
	int status;
	u8 addr[2] = {address & 0xff, address >> 8};

	status = regmap_raw_write(ci->regmap, 2, addr, 2);
	if (!status)
		status = regmap_raw_read(ci->regmap, 3, data, n);
	return status;
}

static int write_pccard(struct cxd *ci, u16 address, u8 *data, u8 n)
{
	int status;
	u8 addr[2] = {address & 0xff, address >> 8};

	status = regmap_raw_write(ci->regmap, 2, addr, 2);
	if (!status) {
		u8 buf[256];

		memcpy(buf, data, n);
		status = regmap_raw_write(ci->regmap, 3, buf, n);
	}
	return status;
}

static int read_io(struct cxd *ci, u16 address, unsigned int *val)
{
	int status;
	u8 addr[2] = {address & 0xff, address >> 8};

	status = regmap_raw_write(ci->regmap, 2, addr, 2);
	if (!status)
		status = regmap_read(ci->regmap, 3, val);
	return status;
}

static int write_io(struct cxd *ci, u16 address, u8 val)
{
	int status;
	u8 addr[2] = {address & 0xff, address >> 8};

	status = regmap_raw_write(ci->regmap, 2, addr, 2);
	if (!status)
		status = regmap_write(ci->regmap, 3, val);
	return status;
}

static int write_regm(struct cxd *ci, u8 reg, u8 val, u8 mask)
{
	int status = 0;
	unsigned int regval;

	if (ci->lastaddress != reg)
		status = regmap_write(ci->regmap, 0, reg);
	if (!status && reg >= 6 && reg <= 8 && mask != 0xff) {
		status = regmap_read(ci->regmap, 1, &regval);
		ci->regs[reg] = regval;
	}
	ci->lastaddress = reg;
	ci->regs[reg] = (ci->regs[reg] & (~mask)) | val;
	if (!status)
		status = regmap_write(ci->regmap, 1, ci->regs[reg]);
	if (reg == 0x20)
		ci->regs[reg] &= 0x7f;
	return status;
}

static int write_reg(struct cxd *ci, u8 reg, u8 val)
{
	return write_regm(ci, reg, val, 0xff);
}

static int write_block(struct cxd *ci, u8 adr, u8 *data, u16 n)
{
	int status = 0;
	u8 *buf = ci->wbuf;

	if (ci->lastaddress != adr)
		status = regmap_write(ci->regmap, 0, adr);
	if (status)
		return status;

	ci->lastaddress = adr;
	while (n) {
		int len = n;

		if (ci->cfg.max_i2c && (len + 1 > ci->cfg.max_i2c))
			len = ci->cfg.max_i2c - 1;
		memcpy(buf, data, len);
		status = regmap_raw_write(ci->regmap, 1, buf, len);
		if (status)
			return status;
		n -= len;
		data += len;
	}
	return status;
}

static void set_mode(struct cxd *ci, int mode)
{
	if (mode == ci->mode)
		return;

	switch (mode) {
	case 0x00: /* IO mem */
		write_regm(ci, 0x06, 0x00, 0x07);
		break;
	case 0x01: /* ATT mem */
		write_regm(ci, 0x06, 0x02, 0x07);
		break;
	default:
		break;
	}
	ci->mode = mode;
}

static void cam_mode(struct cxd *ci, int mode)
{
	u8 dummy;

	if (mode == ci->cammode)
		return;

	switch (mode) {
	case 0x00:
		write_regm(ci, 0x20, 0x80, 0x80);
		break;
	case 0x01:
		if (!ci->en.read_data)
			return;
		ci->write_busy = 0;
		dev_info(&ci->client->dev, "enable cam buffer mode\n");
		write_reg(ci, 0x0d, 0x00);
		write_reg(ci, 0x0e, 0x01);
		write_regm(ci, 0x08, 0x40, 0x40);
		read_reg(ci, 0x12, &dummy);
		write_regm(ci, 0x08, 0x80, 0x80);
		break;
	default:
		break;
	}
	ci->cammode = mode;
}

static int init(struct cxd *ci)
{
	int status;

	mutex_lock(&ci->lock);
	ci->mode = -1;
	do {
		status = write_reg(ci, 0x00, 0x00);
		if (status < 0)
			break;
		status = write_reg(ci, 0x01, 0x00);
		if (status < 0)
			break;
		status = write_reg(ci, 0x02, 0x10);
		if (status < 0)
			break;
		status = write_reg(ci, 0x03, 0x00);
		if (status < 0)
			break;
		status = write_reg(ci, 0x05, 0xFF);
		if (status < 0)
			break;
		status = write_reg(ci, 0x06, 0x1F);
		if (status < 0)
			break;
		status = write_reg(ci, 0x07, 0x1F);
		if (status < 0)
			break;
		status = write_reg(ci, 0x08, 0x28);
		if (status < 0)
			break;
		status = write_reg(ci, 0x14, 0x20);
		if (status < 0)
			break;

		/* TOSTRT = 8, Mode B (gated clock), falling Edge,
		 * Serial, POL=HIGH, MSB
		 */
		status = write_reg(ci, 0x0A, 0xA7);
		if (status < 0)
			break;

		status = write_reg(ci, 0x0B, 0x33);
		if (status < 0)
			break;
		status = write_reg(ci, 0x0C, 0x33);
		if (status < 0)
			break;

		status = write_regm(ci, 0x14, 0x00, 0x0F);
		if (status < 0)
			break;
		status = write_reg(ci, 0x15, ci->clk_reg_b);
		if (status < 0)
			break;
		status = write_regm(ci, 0x16, 0x00, 0x0F);
		if (status < 0)
			break;
		status = write_reg(ci, 0x17, ci->clk_reg_f);
		if (status < 0)
			break;

		if (ci->cfg.clock_mode == 2) {
			/* bitrate*2^13/ 72000 */
			u32 reg = ((ci->cfg.bitrate << 13) + 71999) / 72000;

			if (ci->cfg.polarity) {
				status = write_reg(ci, 0x09, 0x6f);
				if (status < 0)
					break;
			} else {
				status = write_reg(ci, 0x09, 0x6d);
				if (status < 0)
					break;
			}
			status = write_reg(ci, 0x20, 0x08);
			if (status < 0)
				break;
			status = write_reg(ci, 0x21, (reg >> 8) & 0xff);
			if (status < 0)
				break;
			status = write_reg(ci, 0x22, reg & 0xff);
			if (status < 0)
				break;
		} else if (ci->cfg.clock_mode == 1) {
			if (ci->cfg.polarity) {
				status = write_reg(ci, 0x09, 0x6f); /* D */
				if (status < 0)
					break;
			} else {
				status = write_reg(ci, 0x09, 0x6d);
				if (status < 0)
					break;
			}
			status = write_reg(ci, 0x20, 0x68);
			if (status < 0)
				break;
			status = write_reg(ci, 0x21, 0x00);
			if (status < 0)
				break;
			status = write_reg(ci, 0x22, 0x02);
			if (status < 0)
				break;
		} else {
			if (ci->cfg.polarity) {
				status = write_reg(ci, 0x09, 0x4f); /* C */
				if (status < 0)
					break;
			} else {
				status = write_reg(ci, 0x09, 0x4d);
				if (status < 0)
					break;
			}
			status = write_reg(ci, 0x20, 0x28);
			if (status < 0)
				break;
			status = write_reg(ci, 0x21, 0x00);
			if (status < 0)
				break;
			status = write_reg(ci, 0x22, 0x07);
			if (status < 0)
				break;
		}

		status = write_regm(ci, 0x20, 0x80, 0x80);
		if (status < 0)
			break;
		status = write_regm(ci, 0x03, 0x02, 0x02);
		if (status < 0)
			break;
		status = write_reg(ci, 0x01, 0x04);
		if (status < 0)
			break;
		status = write_reg(ci, 0x00, 0x31);
		if (status < 0)
			break;

		/* Put TS in bypass */
		status = write_regm(ci, 0x09, 0x08, 0x08);
		if (status < 0)
			break;
		ci->cammode = -1;
		cam_mode(ci, 0);
	} while (0);
	mutex_unlock(&ci->lock);

	return 0;
}

static int read_attribute_mem(struct dvb_ca_en50221 *ca,
			      int slot, int address)
{
	struct cxd *ci = ca->data;
	u8 val;

	mutex_lock(&ci->lock);
	set_mode(ci, 1);
	read_pccard(ci, address, &val, 1);
	mutex_unlock(&ci->lock);
	return val;
}

static int write_attribute_mem(struct dvb_ca_en50221 *ca, int slot,
			       int address, u8 value)
{
	struct cxd *ci = ca->data;

	mutex_lock(&ci->lock);
	set_mode(ci, 1);
	write_pccard(ci, address, &value, 1);
	mutex_unlock(&ci->lock);
	return 0;
}

static int read_cam_control(struct dvb_ca_en50221 *ca,
			    int slot, u8 address)
{
	struct cxd *ci = ca->data;
	unsigned int val;

	mutex_lock(&ci->lock);
	set_mode(ci, 0);
	read_io(ci, address, &val);
	mutex_unlock(&ci->lock);
	return val;
}

static int write_cam_control(struct dvb_ca_en50221 *ca, int slot,
			     u8 address, u8 value)
{
	struct cxd *ci = ca->data;

	mutex_lock(&ci->lock);
	set_mode(ci, 0);
	write_io(ci, address, value);
	mutex_unlock(&ci->lock);
	return 0;
}

static int slot_reset(struct dvb_ca_en50221 *ca, int slot)
{
	struct cxd *ci = ca->data;

	if (ci->cammode)
		read_data(ca, slot, ci->rbuf, 0);

	mutex_lock(&ci->lock);
	cam_mode(ci, 0);
	write_reg(ci, 0x00, 0x21);
	write_reg(ci, 0x06, 0x1F);
	write_reg(ci, 0x00, 0x31);
	write_regm(ci, 0x20, 0x80, 0x80);
	write_reg(ci, 0x03, 0x02);
	ci->ready = 0;
	ci->mode = -1;
	{
		int i;

		for (i = 0; i < 100; i++) {
			usleep_range(10000, 11000);
			if (ci->ready)
				break;
		}
	}
	mutex_unlock(&ci->lock);
	return 0;
}

static int slot_shutdown(struct dvb_ca_en50221 *ca, int slot)
{
	struct cxd *ci = ca->data;

	dev_dbg(&ci->client->dev, "%s\n", __func__);
	if (ci->cammode)
		read_data(ca, slot, ci->rbuf, 0);
	mutex_lock(&ci->lock);
	write_reg(ci, 0x00, 0x21);
	write_reg(ci, 0x06, 0x1F);
	msleep(300);

	write_regm(ci, 0x09, 0x08, 0x08);
	write_regm(ci, 0x20, 0x80, 0x80); /* Reset CAM Mode */
	write_regm(ci, 0x06, 0x07, 0x07); /* Clear IO Mode */

	ci->mode = -1;
	ci->write_busy = 0;
	mutex_unlock(&ci->lock);
	return 0;
}

static int slot_ts_enable(struct dvb_ca_en50221 *ca, int slot)
{
	struct cxd *ci = ca->data;

	mutex_lock(&ci->lock);
	write_regm(ci, 0x09, 0x00, 0x08);
	set_mode(ci, 0);
	cam_mode(ci, 1);
	mutex_unlock(&ci->lock);
	return 0;
}

static int campoll(struct cxd *ci)
{
	u8 istat;

	read_reg(ci, 0x04, &istat);
	if (!istat)
		return 0;
	write_reg(ci, 0x05, istat);

	if (istat & 0x40)
		ci->dr = 1;
	if (istat & 0x20)
		ci->write_busy = 0;

	if (istat & 2) {
		u8 slotstat;

		read_reg(ci, 0x01, &slotstat);
		if (!(2 & slotstat)) {
			if (!ci->slot_stat) {
				ci->slot_stat |=
					      DVB_CA_EN50221_POLL_CAM_PRESENT;
				write_regm(ci, 0x03, 0x08, 0x08);
			}

		} else {
			if (ci->slot_stat) {
				ci->slot_stat = 0;
				write_regm(ci, 0x03, 0x00, 0x08);
				dev_info(&ci->client->dev, "NO CAM\n");
				ci->ready = 0;
			}
		}
		if ((istat & 8) &&
		    ci->slot_stat == DVB_CA_EN50221_POLL_CAM_PRESENT) {
			ci->ready = 1;
			ci->slot_stat |= DVB_CA_EN50221_POLL_CAM_READY;
		}
	}
	return 0;
}

static int poll_slot_status(struct dvb_ca_en50221 *ca, int slot, int open)
{
	struct cxd *ci = ca->data;
	u8 slotstat;

	mutex_lock(&ci->lock);
	campoll(ci);
	read_reg(ci, 0x01, &slotstat);
	mutex_unlock(&ci->lock);

	return ci->slot_stat;
}

static int read_data(struct dvb_ca_en50221 *ca, int slot, u8 *ebuf, int ecount)
{
	struct cxd *ci = ca->data;
	u8 msb, lsb;
	u16 len;

	mutex_lock(&ci->lock);
	campoll(ci);
	mutex_unlock(&ci->lock);

	if (!ci->dr)
		return 0;

	mutex_lock(&ci->lock);
	read_reg(ci, 0x0f, &msb);
	read_reg(ci, 0x10, &lsb);
	len = ((u16)msb << 8) | lsb;
	if (len > ecount || len < 2) {
		/* read it anyway or cxd may hang */
		read_block(ci, 0x12, ci->rbuf, len);
		mutex_unlock(&ci->lock);
		return -EIO;
	}
	read_block(ci, 0x12, ebuf, len);
	ci->dr = 0;
	mutex_unlock(&ci->lock);
	return len;
}

static int write_data(struct dvb_ca_en50221 *ca, int slot, u8 *ebuf, int ecount)
{
	struct cxd *ci = ca->data;

	if (ci->write_busy)
		return -EAGAIN;
	mutex_lock(&ci->lock);
	write_reg(ci, 0x0d, ecount >> 8);
	write_reg(ci, 0x0e, ecount & 0xff);
	write_block(ci, 0x11, ebuf, ecount);
	ci->write_busy = 1;
	mutex_unlock(&ci->lock);
	return ecount;
}

static const struct dvb_ca_en50221 en_templ = {
	.read_attribute_mem  = read_attribute_mem,
	.write_attribute_mem = write_attribute_mem,
	.read_cam_control    = read_cam_control,
	.write_cam_control   = write_cam_control,
	.slot_reset          = slot_reset,
	.slot_shutdown       = slot_shutdown,
	.slot_ts_enable      = slot_ts_enable,
	.poll_slot_status    = poll_slot_status,
	.read_data           = read_data,
	.write_data          = write_data,
};

static int cxd2099_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct cxd *ci;
	struct cxd2099_cfg *cfg = client->dev.platform_data;
	static const struct regmap_config rm_cfg = {
		.reg_bits = 8,
		.val_bits = 8,
	};
	unsigned int val;
	int ret;

	ci = kzalloc(sizeof(*ci), GFP_KERNEL);
	if (!ci) {
		ret = -ENOMEM;
		goto err;
	}

	ci->client = client;
	memcpy(&ci->cfg, cfg, sizeof(ci->cfg));

	ci->regmap = regmap_init_i2c(client, &rm_cfg);
	if (IS_ERR(ci->regmap)) {
		ret = PTR_ERR(ci->regmap);
		goto err_kfree;
	}

	ret = regmap_read(ci->regmap, 0x00, &val);
	if (ret < 0) {
		dev_info(&client->dev, "No CXD2099AR detected at 0x%02x\n",
			 client->addr);
		goto err_rmexit;
	}

	mutex_init(&ci->lock);
	ci->lastaddress = 0xff;
	ci->clk_reg_b = 0x4a;
	ci->clk_reg_f = 0x1b;

	ci->en = en_templ;
	ci->en.data = ci;
	init(ci);
	dev_info(&client->dev, "Attached CXD2099AR at 0x%02x\n", client->addr);

	*cfg->en = &ci->en;

	if (!buffermode) {
		ci->en.read_data = NULL;
		ci->en.write_data = NULL;
	} else {
		dev_info(&client->dev, "Using CXD2099AR buffer mode");
	}

	i2c_set_clientdata(client, ci);

	return 0;

err_rmexit:
	regmap_exit(ci->regmap);
err_kfree:
	kfree(ci);
err:

	return ret;
}

static int cxd2099_remove(struct i2c_client *client)
{
	struct cxd *ci = i2c_get_clientdata(client);

	regmap_exit(ci->regmap);
	kfree(ci);

	return 0;
}

static const struct i2c_device_id cxd2099_id[] = {
	{"cxd2099", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, cxd2099_id);

static struct i2c_driver cxd2099_driver = {
	.driver = {
		.name	= "cxd2099",
	},
	.probe		= cxd2099_probe,
	.remove		= cxd2099_remove,
	.id_table	= cxd2099_id,
};

module_i2c_driver(cxd2099_driver);

MODULE_DESCRIPTION("Sony CXD2099AR Common Interface controller driver");
MODULE_AUTHOR("Ralph Metzler");
MODULE_LICENSE("GPL v2");

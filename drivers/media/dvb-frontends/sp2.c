/*
 * CIMaX SP2/SP2HF (Atmel T90FJR) CI driver
 *
 * Copyright (C) 2014 Olli Salonen <olli.salonen@iki.fi>
 *
 * Heavily based on CIMax2(R) SP2 driver in conjunction with NetUp Dual
 * DVB-S2 CI card (cimax2) with following copyrights:
 *
 *  Copyright (C) 2009 NetUP Inc.
 *  Copyright (C) 2009 Igor M. Liplianin <liplianin@netup.ru>
 *  Copyright (C) 2009 Abylay Ospan <aospan@netup.ru>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 */

#include "sp2_priv.h"

static int sp2_read_i2c(struct sp2 *s, u8 reg, u8 *buf, int len)
{
	int ret;
	struct i2c_client *client = s->client;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.buf = &reg,
			.len = 1
		}, {
			.addr = client->addr,
			.flags	= I2C_M_RD,
			.buf = buf,
			.len = len
		}
	};

	ret = i2c_transfer(adap, msg, 2);

	if (ret != 2) {
		dev_err(&client->dev, "i2c read error, reg = 0x%02x, status = %d\n",
				reg, ret);
		if (ret < 0)
			return ret;
		else
			return -EIO;
	}

	dev_dbg(&s->client->dev, "addr=0x%04x, reg = 0x%02x, data = %02x\n",
				client->addr, reg, buf[0]);

	return 0;
}

static int sp2_write_i2c(struct sp2 *s, u8 reg, u8 *buf, int len)
{
	int ret;
	u8 buffer[35];
	struct i2c_client *client = s->client;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.buf = &buffer[0],
		.len = len + 1
	};

	if ((len + 1) > sizeof(buffer)) {
		dev_err(&client->dev, "i2c wr reg=%02x: len=%d is too big!\n",
				reg, len);
		return -EINVAL;
	}

	buffer[0] = reg;
	memcpy(&buffer[1], buf, len);

	ret = i2c_transfer(adap, &msg, 1);

	if (ret != 1) {
		dev_err(&client->dev, "i2c write error, reg = 0x%02x, status = %d\n",
				reg, ret);
		if (ret < 0)
			return ret;
		else
			return -EIO;
	}

	return 0;
}

static int sp2_ci_op_cam(struct dvb_ca_en50221 *en50221, int slot, u8 acs,
			u8 read, int addr, u8 data)
{
	struct sp2 *s = en50221->data;
	u8 store;
	int mem, ret;
	int (*ci_op_cam)(void*, u8, int, u8, int*) = s->ci_control;

	dev_dbg(&s->client->dev, "slot=%d, acs=0x%02x, addr=0x%04x, data = 0x%02x",
			slot, acs, addr, data);

	if (slot != 0)
		return -EINVAL;

	/*
	 * change module access type between IO space and attribute memory
	 * when needed
	 */
	if (s->module_access_type != acs) {
		ret = sp2_read_i2c(s, 0x00, &store, 1);

		if (ret)
			return ret;

		store &= ~(SP2_MOD_CTL_ACS1 | SP2_MOD_CTL_ACS0);
		store |= acs;

		ret = sp2_write_i2c(s, 0x00, &store, 1);
		if (ret)
			return ret;
	}

	s->module_access_type = acs;

	/* implementation of ci_op_cam is device specific */
	if (ci_op_cam) {
		ret = ci_op_cam(s->priv, read, addr, data, &mem);
	} else {
		dev_err(&s->client->dev, "callback not defined");
		return -EINVAL;
	}

	if (ret)
		return ret;

	if (read) {
		dev_dbg(&s->client->dev, "cam read, addr=0x%04x, data = 0x%04x",
				addr, mem);
		return mem;
	} else {
		return 0;
	}
}

int sp2_ci_read_attribute_mem(struct dvb_ca_en50221 *en50221,
				int slot, int addr)
{
	return sp2_ci_op_cam(en50221, slot, SP2_CI_ATTR_ACS,
			SP2_CI_RD, addr, 0);
}

int sp2_ci_write_attribute_mem(struct dvb_ca_en50221 *en50221,
				int slot, int addr, u8 data)
{
	return sp2_ci_op_cam(en50221, slot, SP2_CI_ATTR_ACS,
			SP2_CI_WR, addr, data);
}

int sp2_ci_read_cam_control(struct dvb_ca_en50221 *en50221,
				int slot, u8 addr)
{
	return sp2_ci_op_cam(en50221, slot, SP2_CI_IO_ACS,
			SP2_CI_RD, addr, 0);
}

int sp2_ci_write_cam_control(struct dvb_ca_en50221 *en50221,
				int slot, u8 addr, u8 data)
{
	return sp2_ci_op_cam(en50221, slot, SP2_CI_IO_ACS,
			SP2_CI_WR, addr, data);
}

int sp2_ci_slot_reset(struct dvb_ca_en50221 *en50221, int slot)
{
	struct sp2 *s = en50221->data;
	u8 buf;
	int ret;

	dev_dbg(&s->client->dev, "slot: %d\n", slot);

	if (slot != 0)
		return -EINVAL;

	/* RST on */
	buf = SP2_MOD_CTL_RST;
	ret = sp2_write_i2c(s, 0x00, &buf, 1);

	if (ret)
		return ret;

	usleep_range(500, 600);

	/* RST off */
	buf = 0x00;
	ret = sp2_write_i2c(s, 0x00, &buf, 1);

	if (ret)
		return ret;

	msleep(1000);

	return 0;
}

int sp2_ci_slot_shutdown(struct dvb_ca_en50221 *en50221, int slot)
{
	struct sp2 *s = en50221->data;

	dev_dbg(&s->client->dev, "slot:%d\n", slot);

	/* not implemented */
	return 0;
}

int sp2_ci_slot_ts_enable(struct dvb_ca_en50221 *en50221, int slot)
{
	struct sp2 *s = en50221->data;
	u8 buf;

	dev_dbg(&s->client->dev, "slot:%d\n", slot);

	if (slot != 0)
		return -EINVAL;

	sp2_read_i2c(s, 0x00, &buf, 1);

	/* disable bypass and enable TS */
	buf |= (SP2_MOD_CTL_TSOEN | SP2_MOD_CTL_TSIEN);
	return sp2_write_i2c(s, 0, &buf, 1);
}

int sp2_ci_poll_slot_status(struct dvb_ca_en50221 *en50221,
				int slot, int open)
{
	struct sp2 *s = en50221->data;
	u8 buf[2];
	int ret;

	dev_dbg(&s->client->dev, "slot:%d open:%d\n", slot, open);

	/*
	 * CAM module INSERT/REMOVE processing. Slow operation because of i2c
	 * transfers. Throttle read to one per sec.
	 */
	if (time_after(jiffies, s->next_status_checked_time)) {
		ret = sp2_read_i2c(s, 0x00, buf, 1);
		s->next_status_checked_time = jiffies +	msecs_to_jiffies(1000);

		if (ret)
			return 0;

		if (buf[0] & SP2_MOD_CTL_DET)
			s->status = DVB_CA_EN50221_POLL_CAM_PRESENT |
					DVB_CA_EN50221_POLL_CAM_READY;
		else
			s->status = 0;
	}

	return s->status;
}

int sp2_init(struct sp2 *s)
{
	int ret = 0;
	u8 buf;
	u8 cimax_init[34] = {
		0x00, /* module A control*/
		0x00, /* auto select mask high A */
		0x00, /* auto select mask low A */
		0x00, /* auto select pattern high A */
		0x00, /* auto select pattern low A */
		0x44, /* memory access time A, 600 ns */
		0x00, /* invert input A */
		0x00, /* RFU */
		0x00, /* RFU */
		0x00, /* module B control*/
		0x00, /* auto select mask high B */
		0x00, /* auto select mask low B */
		0x00, /* auto select pattern high B */
		0x00, /* auto select pattern low B */
		0x44, /* memory access time B, 600 ns */
		0x00, /* invert input B */
		0x00, /* RFU */
		0x00, /* RFU */
		0x00, /* auto select mask high Ext */
		0x00, /* auto select mask low Ext */
		0x00, /* auto select pattern high Ext */
		0x00, /* auto select pattern low Ext */
		0x00, /* RFU */
		0x02, /* destination - module A */
		0x01, /* power control reg, VCC power on */
		0x00, /* RFU */
		0x00, /* int status read only */
		0x00, /* Interrupt Mask Register */
		0x05, /* EXTINT=active-high, INT=push-pull */
		0x00, /* USCG1 */
		0x04, /* ack active low */
		0x00, /* LOCK = 0 */
		0x22, /* unknown */
		0x00, /* synchronization? */
	};

	dev_dbg(&s->client->dev, "\n");

	s->ca.owner = THIS_MODULE;
	s->ca.read_attribute_mem = sp2_ci_read_attribute_mem;
	s->ca.write_attribute_mem = sp2_ci_write_attribute_mem;
	s->ca.read_cam_control = sp2_ci_read_cam_control;
	s->ca.write_cam_control = sp2_ci_write_cam_control;
	s->ca.slot_reset = sp2_ci_slot_reset;
	s->ca.slot_shutdown = sp2_ci_slot_shutdown;
	s->ca.slot_ts_enable = sp2_ci_slot_ts_enable;
	s->ca.poll_slot_status = sp2_ci_poll_slot_status;
	s->ca.data = s;
	s->module_access_type = 0;

	/* initialize all regs */
	ret = sp2_write_i2c(s, 0x00, &cimax_init[0], 34);
	if (ret)
		goto err;

	/* lock registers */
	buf = 1;
	ret = sp2_write_i2c(s, 0x1f, &buf, 1);
	if (ret)
		goto err;

	/* power on slots */
	ret = sp2_write_i2c(s, 0x18, &buf, 1);
	if (ret)
		goto err;

	ret = dvb_ca_en50221_init(s->dvb_adap, &s->ca, 0, 1);
	if (ret)
		goto err;

	return 0;

err:
	dev_dbg(&s->client->dev, "init failed=%d\n", ret);
	return ret;
}

int sp2_exit(struct i2c_client *client)
{
	struct sp2 *s;

	dev_dbg(&client->dev, "\n");

	if (client == NULL)
		return 0;

	s = i2c_get_clientdata(client);
	if (s == NULL)
		return 0;

	if (s->ca.data == NULL)
		return 0;

	dvb_ca_en50221_release(&s->ca);

	return 0;
}

static int sp2_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct sp2_config *cfg = client->dev.platform_data;
	struct sp2 *s;
	int ret;

	dev_dbg(&client->dev, "\n");

	s = kzalloc(sizeof(struct sp2), GFP_KERNEL);
	if (!s) {
		ret = -ENOMEM;
		dev_err(&client->dev, "kzalloc() failed\n");
		goto err;
	}

	s->client = client;
	s->dvb_adap = cfg->dvb_adap;
	s->priv = cfg->priv;
	s->ci_control = cfg->ci_control;

	i2c_set_clientdata(client, s);

	ret = sp2_init(s);
	if (ret)
		goto err;

	dev_info(&s->client->dev, "CIMaX SP2 successfully attached\n");
	return 0;
err:
	dev_dbg(&client->dev, "init failed=%d\n", ret);
	kfree(s);

	return ret;
}

static int sp2_remove(struct i2c_client *client)
{
	struct si2157 *s = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	sp2_exit(client);
	if (s != NULL)
		kfree(s);

	return 0;
}

static const struct i2c_device_id sp2_id[] = {
	{"sp2", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, sp2_id);

static struct i2c_driver sp2_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "sp2",
	},
	.probe		= sp2_probe,
	.remove		= sp2_remove,
	.id_table	= sp2_id,
};

module_i2c_driver(sp2_driver);

MODULE_DESCRIPTION("CIMaX SP2/HF CI driver");
MODULE_AUTHOR("Olli Salonen <olli.salonen@iki.fi>");
MODULE_LICENSE("GPL");

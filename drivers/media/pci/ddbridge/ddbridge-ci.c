/*
 * ddbridge-ci.c: Digital Devices bridge CI (DuoFlex, CI Bridge) support
 *
 * Copyright (C) 2010-2017 Digital Devices GmbH
 *                         Marcus Metzler <mocm@metzlerbros.de>
 *                         Ralph Metzler <rjkm@metzlerbros.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * To obtain the license, point your browser to
 * http://www.gnu.org/copyleft/gpl.html
 */

#include "ddbridge.h"
#include "ddbridge-regs.h"
#include "ddbridge-ci.h"
#include "ddbridge-io.h"
#include "ddbridge-i2c.h"

#include "cxd2099.h"

/* Octopus CI internal CI interface */

static int wait_ci_ready(struct ddb_ci *ci)
{
	u32 count = 10;

	ndelay(500);
	do {
		if (ddbreadl(ci->port->dev,
			     CI_CONTROL(ci->nr)) & CI_READY)
			break;
		usleep_range(1, 2);
		if ((--count) == 0)
			return -1;
	} while (1);
	return 0;
}

static int read_attribute_mem(struct dvb_ca_en50221 *ca,
			      int slot, int address)
{
	struct ddb_ci *ci = ca->data;
	u32 val, off = (address >> 1) & (CI_BUFFER_SIZE - 1);

	if (address > CI_BUFFER_SIZE)
		return -1;
	ddbwritel(ci->port->dev, CI_READ_CMD | (1 << 16) | address,
		  CI_DO_READ_ATTRIBUTES(ci->nr));
	wait_ci_ready(ci);
	val = 0xff & ddbreadl(ci->port->dev, CI_BUFFER(ci->nr) + off);
	return val;
}

static int write_attribute_mem(struct dvb_ca_en50221 *ca, int slot,
			       int address, u8 value)
{
	struct ddb_ci *ci = ca->data;

	ddbwritel(ci->port->dev, CI_WRITE_CMD | (value << 16) | address,
		  CI_DO_ATTRIBUTE_RW(ci->nr));
	wait_ci_ready(ci);
	return 0;
}

static int read_cam_control(struct dvb_ca_en50221 *ca,
			    int slot, u8 address)
{
	u32 count = 100;
	struct ddb_ci *ci = ca->data;
	u32 res;

	ddbwritel(ci->port->dev, CI_READ_CMD | address,
		  CI_DO_IO_RW(ci->nr));
	ndelay(500);
	do {
		res = ddbreadl(ci->port->dev, CI_READDATA(ci->nr));
		if (res & CI_READY)
			break;
		usleep_range(1, 2);
		if ((--count) == 0)
			return -1;
	} while (1);
	return 0xff & res;
}

static int write_cam_control(struct dvb_ca_en50221 *ca, int slot,
			     u8 address, u8 value)
{
	struct ddb_ci *ci = ca->data;

	ddbwritel(ci->port->dev, CI_WRITE_CMD | (value << 16) | address,
		  CI_DO_IO_RW(ci->nr));
	wait_ci_ready(ci);
	return 0;
}

static int slot_reset(struct dvb_ca_en50221 *ca, int slot)
{
	struct ddb_ci *ci = ca->data;

	ddbwritel(ci->port->dev, CI_POWER_ON,
		  CI_CONTROL(ci->nr));
	msleep(100);
	ddbwritel(ci->port->dev, CI_POWER_ON | CI_RESET_CAM,
		  CI_CONTROL(ci->nr));
	ddbwritel(ci->port->dev, CI_ENABLE | CI_POWER_ON | CI_RESET_CAM,
		  CI_CONTROL(ci->nr));
	usleep_range(20, 25);
	ddbwritel(ci->port->dev, CI_ENABLE | CI_POWER_ON,
		  CI_CONTROL(ci->nr));
	return 0;
}

static int slot_shutdown(struct dvb_ca_en50221 *ca, int slot)
{
	struct ddb_ci *ci = ca->data;

	ddbwritel(ci->port->dev, 0, CI_CONTROL(ci->nr));
	msleep(300);
	return 0;
}

static int slot_ts_enable(struct dvb_ca_en50221 *ca, int slot)
{
	struct ddb_ci *ci = ca->data;
	u32 val = ddbreadl(ci->port->dev, CI_CONTROL(ci->nr));

	ddbwritel(ci->port->dev, val | CI_BYPASS_DISABLE,
		  CI_CONTROL(ci->nr));
	return 0;
}

static int poll_slot_status(struct dvb_ca_en50221 *ca, int slot, int open)
{
	struct ddb_ci *ci = ca->data;
	u32 val = ddbreadl(ci->port->dev, CI_CONTROL(ci->nr));
	int stat = 0;

	if (val & CI_CAM_DETECT)
		stat |= DVB_CA_EN50221_POLL_CAM_PRESENT;
	if (val & CI_CAM_READY)
		stat |= DVB_CA_EN50221_POLL_CAM_READY;
	return stat;
}

static struct dvb_ca_en50221 en_templ = {
	.read_attribute_mem  = read_attribute_mem,
	.write_attribute_mem = write_attribute_mem,
	.read_cam_control    = read_cam_control,
	.write_cam_control   = write_cam_control,
	.slot_reset          = slot_reset,
	.slot_shutdown       = slot_shutdown,
	.slot_ts_enable      = slot_ts_enable,
	.poll_slot_status    = poll_slot_status,
};

static void ci_attach(struct ddb_port *port)
{
	struct ddb_ci *ci;

	ci = kzalloc(sizeof(*ci), GFP_KERNEL);
	if (!ci)
		return;
	memcpy(&ci->en, &en_templ, sizeof(en_templ));
	ci->en.data = ci;
	port->en = &ci->en;
	port->en_freedata = 1;
	ci->port = port;
	ci->nr = port->nr - 2;
}

/* DuoFlex Dual CI support */

static int write_creg(struct ddb_ci *ci, u8 data, u8 mask)
{
	struct i2c_adapter *i2c = &ci->port->i2c->adap;
	u8 adr = (ci->port->type == DDB_CI_EXTERNAL_XO2) ? 0x12 : 0x13;

	ci->port->creg = (ci->port->creg & ~mask) | data;
	return i2c_write_reg(i2c, adr, 0x02, ci->port->creg);
}

static int read_attribute_mem_xo2(struct dvb_ca_en50221 *ca,
				  int slot, int address)
{
	struct ddb_ci *ci = ca->data;
	struct i2c_adapter *i2c = &ci->port->i2c->adap;
	u8 adr = (ci->port->type == DDB_CI_EXTERNAL_XO2) ? 0x12 : 0x13;
	int res;
	u8 val;

	res = i2c_read_reg16(i2c, adr, 0x8000 | address, &val);
	return res ? res : val;
}

static int write_attribute_mem_xo2(struct dvb_ca_en50221 *ca, int slot,
				   int address, u8 value)
{
	struct ddb_ci *ci = ca->data;
	struct i2c_adapter *i2c = &ci->port->i2c->adap;
	u8 adr = (ci->port->type == DDB_CI_EXTERNAL_XO2) ? 0x12 : 0x13;

	return i2c_write_reg16(i2c, adr, 0x8000 | address, value);
}

static int read_cam_control_xo2(struct dvb_ca_en50221 *ca,
				int slot, u8 address)
{
	struct ddb_ci *ci = ca->data;
	struct i2c_adapter *i2c = &ci->port->i2c->adap;
	u8 adr = (ci->port->type == DDB_CI_EXTERNAL_XO2) ? 0x12 : 0x13;
	u8 val;
	int res;

	res = i2c_read_reg(i2c, adr, 0x20 | (address & 3), &val);
	return res ? res : val;
}

static int write_cam_control_xo2(struct dvb_ca_en50221 *ca, int slot,
				 u8 address, u8 value)
{
	struct ddb_ci *ci = ca->data;
	struct i2c_adapter *i2c = &ci->port->i2c->adap;
	u8 adr = (ci->port->type == DDB_CI_EXTERNAL_XO2) ? 0x12 : 0x13;

	return i2c_write_reg(i2c, adr, 0x20 | (address & 3), value);
}

static int slot_reset_xo2(struct dvb_ca_en50221 *ca, int slot)
{
	struct ddb_ci *ci = ca->data;

	dev_dbg(ci->port->dev->dev, "%s\n", __func__);
	write_creg(ci, 0x01, 0x01);
	write_creg(ci, 0x04, 0x04);
	msleep(20);
	write_creg(ci, 0x02, 0x02);
	write_creg(ci, 0x00, 0x04);
	write_creg(ci, 0x18, 0x18);
	return 0;
}

static int slot_shutdown_xo2(struct dvb_ca_en50221 *ca, int slot)
{
	struct ddb_ci *ci = ca->data;

	dev_dbg(ci->port->dev->dev, "%s\n", __func__);
	write_creg(ci, 0x10, 0xff);
	write_creg(ci, 0x08, 0x08);
	return 0;
}

static int slot_ts_enable_xo2(struct dvb_ca_en50221 *ca, int slot)
{
	struct ddb_ci *ci = ca->data;

	dev_dbg(ci->port->dev->dev, "%s\n", __func__);
	write_creg(ci, 0x00, 0x10);
	return 0;
}

static int poll_slot_status_xo2(struct dvb_ca_en50221 *ca, int slot, int open)
{
	struct ddb_ci *ci = ca->data;
	struct i2c_adapter *i2c = &ci->port->i2c->adap;
	u8 adr = (ci->port->type == DDB_CI_EXTERNAL_XO2) ? 0x12 : 0x13;
	u8 val = 0;
	int stat = 0;

	i2c_read_reg(i2c, adr, 0x01, &val);

	if (val & 2)
		stat |= DVB_CA_EN50221_POLL_CAM_PRESENT;
	if (val & 1)
		stat |= DVB_CA_EN50221_POLL_CAM_READY;
	return stat;
}

static struct dvb_ca_en50221 en_xo2_templ = {
	.read_attribute_mem  = read_attribute_mem_xo2,
	.write_attribute_mem = write_attribute_mem_xo2,
	.read_cam_control    = read_cam_control_xo2,
	.write_cam_control   = write_cam_control_xo2,
	.slot_reset          = slot_reset_xo2,
	.slot_shutdown       = slot_shutdown_xo2,
	.slot_ts_enable      = slot_ts_enable_xo2,
	.poll_slot_status    = poll_slot_status_xo2,
};

static void ci_xo2_attach(struct ddb_port *port)
{
	struct ddb_ci *ci;

	ci = kzalloc(sizeof(*ci), GFP_KERNEL);
	if (!ci)
		return;
	memcpy(&ci->en, &en_xo2_templ, sizeof(en_xo2_templ));
	ci->en.data = ci;
	port->en = &ci->en;
	port->en_freedata = 1;
	ci->port = port;
	ci->nr = port->nr - 2;
	ci->port->creg = 0;
	write_creg(ci, 0x10, 0xff);
	write_creg(ci, 0x08, 0x08);
}

static const struct cxd2099_cfg cxd_cfgtmpl = {
	.bitrate =  72000,
	.polarity = 1,
	.clock_mode = 1,
	.max_i2c = 512,
};

static int ci_cxd2099_attach(struct ddb_port *port, u32 bitrate)
{
	struct cxd2099_cfg cxd_cfg = cxd_cfgtmpl;
	struct i2c_client *client;

	cxd_cfg.bitrate = bitrate;
	cxd_cfg.en = &port->en;

	client = dvb_module_probe("cxd2099", NULL, &port->i2c->adap,
				  0x40, &cxd_cfg);
	if (!client)
		goto err;

	port->dvb[0].i2c_client[0] = client;
	port->en_freedata = 0;
	return 0;

err:
	dev_err(port->dev->dev, "CXD2099AR attach failed\n");
	return -ENODEV;
}

int ddb_ci_attach(struct ddb_port *port, u32 bitrate)
{
	int ret;

	switch (port->type) {
	case DDB_CI_EXTERNAL_SONY:
		ret = ci_cxd2099_attach(port, bitrate);
		if (ret)
			return -ENODEV;
		break;
	case DDB_CI_EXTERNAL_XO2:
	case DDB_CI_EXTERNAL_XO2_B:
		ci_xo2_attach(port);
		break;
	case DDB_CI_INTERNAL:
		ci_attach(port);
		break;
	default:
		return -ENODEV;
	}

	if (!port->en)
		return -ENODEV;
	dvb_ca_en50221_init(port->dvb[0].adap, port->en, 0, 1);
	return 0;
}

void ddb_ci_detach(struct ddb_port *port)
{
	if (port->dvb[0].dev)
		dvb_unregister_device(port->dvb[0].dev);
	if (port->en) {
		dvb_ca_en50221_release(port->en);

		dvb_module_release(port->dvb[0].i2c_client[0]);
		port->dvb[0].i2c_client[0] = NULL;

		/* free alloc'ed memory if needed */
		if (port->en_freedata)
			kfree(port->en->data);

		port->en = NULL;
	}
}

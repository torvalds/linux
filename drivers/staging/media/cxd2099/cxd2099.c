/*
 * cxd2099.c: Driver for the CXD2099AR Common Interface Controller
 *
 * Copyright (C) 2010-2011 Digital Devices GmbH
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/io.h>

#include "cxd2099.h"

#define MAX_BUFFER_SIZE 248

struct cxd {
	struct dvb_ca_en50221 en;

	struct i2c_adapter *i2c;
	struct cxd2099_cfg cfg;

	u8     regs[0x23];
	u8     lastaddress;
	u8     clk_reg_f;
	u8     clk_reg_b;
	int    mode;
	int    ready;
	int    dr;
	int    slot_stat;

	u8     amem[1024];
	int    amem_read;

	int    cammode;
	struct mutex lock;
};

static int i2c_write_reg(struct i2c_adapter *adapter, u8 adr,
			 u8 reg, u8 data)
{
	u8 m[2] = {reg, data};
	struct i2c_msg msg = {.addr = adr, .flags = 0, .buf = m, .len = 2};

	if (i2c_transfer(adapter, &msg, 1) != 1) {
		dev_err(&adapter->dev,
			"Failed to write to I2C register %02x@%02x!\n",
			reg, adr);
		return -1;
	}
	return 0;
}

static int i2c_write(struct i2c_adapter *adapter, u8 adr,
		     u8 *data, u8 len)
{
	struct i2c_msg msg = {.addr = adr, .flags = 0, .buf = data, .len = len};

	if (i2c_transfer(adapter, &msg, 1) != 1) {
		dev_err(&adapter->dev, "Failed to write to I2C!\n");
		return -1;
	}
	return 0;
}

static int i2c_read_reg(struct i2c_adapter *adapter, u8 adr,
			u8 reg, u8 *val)
{
	struct i2c_msg msgs[2] = {{.addr = adr, .flags = 0,
				   .buf = &reg, .len = 1},
				  {.addr = adr, .flags = I2C_M_RD,
				   .buf = val, .len = 1} };

	if (i2c_transfer(adapter, msgs, 2) != 2) {
		dev_err(&adapter->dev, "error in i2c_read_reg\n");
		return -1;
	}
	return 0;
}

static int i2c_read(struct i2c_adapter *adapter, u8 adr,
		    u8 reg, u8 *data, u8 n)
{
	struct i2c_msg msgs[2] = {{.addr = adr, .flags = 0,
				 .buf = &reg, .len = 1},
				{.addr = adr, .flags = I2C_M_RD,
				 .buf = data, .len = n} };

	if (i2c_transfer(adapter, msgs, 2) != 2) {
		dev_err(&adapter->dev, "error in i2c_read\n");
		return -1;
	}
	return 0;
}

static int read_block(struct cxd *ci, u8 adr, u8 *data, u8 n)
{
	int status;

	status = i2c_write_reg(ci->i2c, ci->cfg.adr, 0, adr);
	if (!status) {
		ci->lastaddress = adr;
		status = i2c_read(ci->i2c, ci->cfg.adr, 1, data, n);
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
	u8 addr[3] = {2, address & 0xff, address >> 8};

	status = i2c_write(ci->i2c, ci->cfg.adr, addr, 3);
	if (!status)
		status = i2c_read(ci->i2c, ci->cfg.adr, 3, data, n);
	return status;
}

static int write_pccard(struct cxd *ci, u16 address, u8 *data, u8 n)
{
	int status;
	u8 addr[3] = {2, address & 0xff, address >> 8};

	status = i2c_write(ci->i2c, ci->cfg.adr, addr, 3);
	if (!status) {
		u8 buf[256] = {3};

		memcpy(buf+1, data, n);
		status = i2c_write(ci->i2c, ci->cfg.adr, buf, n+1);
	}
	return status;
}

static int read_io(struct cxd *ci, u16 address, u8 *val)
{
	int status;
	u8 addr[3] = {2, address & 0xff, address >> 8};

	status = i2c_write(ci->i2c, ci->cfg.adr, addr, 3);
	if (!status)
		status = i2c_read(ci->i2c, ci->cfg.adr, 3, val, 1);
	return status;
}

static int write_io(struct cxd *ci, u16 address, u8 val)
{
	int status;
	u8 addr[3] = {2, address & 0xff, address >> 8};
	u8 buf[2] = {3, val};

	status = i2c_write(ci->i2c, ci->cfg.adr, addr, 3);
	if (!status)
		status = i2c_write(ci->i2c, ci->cfg.adr, buf, 2);
	return status;
}

#if 0
static int read_io_data(struct cxd *ci, u8 *data, u8 n)
{
	int status;
	u8 addr[3] = { 2, 0, 0 };

	status = i2c_write(ci->i2c, ci->cfg.adr, addr, 3);
	if (!status)
		status = i2c_read(ci->i2c, ci->cfg.adr, 3, data, n);
	return 0;
}

static int write_io_data(struct cxd *ci, u8 *data, u8 n)
{
	int status;
	u8 addr[3] = {2, 0, 0};

	status = i2c_write(ci->i2c, ci->cfg.adr, addr, 3);
	if (!status) {
		u8 buf[256] = {3};

		memcpy(buf+1, data, n);
		status = i2c_write(ci->i2c, ci->cfg.adr, buf, n + 1);
	}
	return 0;
}
#endif

static int write_regm(struct cxd *ci, u8 reg, u8 val, u8 mask)
{
	int status;

	status = i2c_write_reg(ci->i2c, ci->cfg.adr, 0, reg);
	if (!status && reg >= 6 && reg <= 8 && mask != 0xff)
		status = i2c_read_reg(ci->i2c, ci->cfg.adr, 1, &ci->regs[reg]);
	ci->regs[reg] = (ci->regs[reg] & (~mask)) | val;
	if (!status) {
		ci->lastaddress = reg;
		status = i2c_write_reg(ci->i2c, ci->cfg.adr, 1, ci->regs[reg]);
	}
	if (reg == 0x20)
		ci->regs[reg] &= 0x7f;
	return status;
}

static int write_reg(struct cxd *ci, u8 reg, u8 val)
{
	return write_regm(ci, reg, val, 0xff);
}

#ifdef BUFFER_MODE
static int write_block(struct cxd *ci, u8 adr, u8 *data, int n)
{
	int status;
	u8 buf[256] = {1};

	status = i2c_write_reg(ci->i2c, ci->cfg.adr, 0, adr);
	if (!status) {
		ci->lastaddress = adr;
		memcpy(buf + 1, data, n);
		status = i2c_write(ci->i2c, ci->cfg.adr, buf, n + 1);
	}
	return status;
}
#endif

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
	if (mode == ci->cammode)
		return;

	switch (mode) {
	case 0x00:
		write_regm(ci, 0x20, 0x80, 0x80);
		break;
	case 0x01:
#ifdef BUFFER_MODE
		if (!ci->en.read_data)
			return;
		dev_info(&ci->i2c->dev, "enable cam buffer mode\n");
		/* write_reg(ci, 0x0d, 0x00); */
		/* write_reg(ci, 0x0e, 0x01); */
		write_regm(ci, 0x08, 0x40, 0x40);
		/* read_reg(ci, 0x12, &dummy); */
		write_regm(ci, 0x08, 0x80, 0x80);
#endif
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

#if 0
		/* Input Mode C, BYPass Serial, TIVAL = low, MSB */
		status = write_reg(ci, 0x09, 0x4D);
		if (status < 0)
			break;
#endif
		/* TOSTRT = 8, Mode B (gated clock), falling Edge,
		 * Serial, POL=HIGH, MSB */
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

		if (ci->cfg.clock_mode) {
			if (ci->cfg.polarity) {
				status = write_reg(ci, 0x09, 0x6f);
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
				status = write_reg(ci, 0x09, 0x4f);
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
#if 0
	if (ci->amem_read) {
		if (address <= 0 || address > 1024)
			return -EIO;
		return ci->amem[address];
	}

	mutex_lock(&ci->lock);
	write_regm(ci, 0x06, 0x00, 0x05);
	read_pccard(ci, 0, &ci->amem[0], 128);
	read_pccard(ci, 128, &ci->amem[0], 128);
	read_pccard(ci, 256, &ci->amem[0], 128);
	read_pccard(ci, 384, &ci->amem[0], 128);
	write_regm(ci, 0x06, 0x05, 0x05);
	mutex_unlock(&ci->lock);
	return ci->amem[address];
#else
	u8 val;

	mutex_lock(&ci->lock);
	set_mode(ci, 1);
	read_pccard(ci, address, &val, 1);
	mutex_unlock(&ci->lock);
	/* printk(KERN_INFO "%02x:%02x\n", address,val); */
	return val;
#endif
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
	u8 val;

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

	mutex_lock(&ci->lock);
#if 0
	write_reg(ci, 0x00, 0x21);
	write_reg(ci, 0x06, 0x1F);
	write_reg(ci, 0x00, 0x31);
#else
#if 0
	write_reg(ci, 0x06, 0x1F);
	write_reg(ci, 0x06, 0x2F);
#else
	cam_mode(ci, 0);
	write_reg(ci, 0x00, 0x21);
	write_reg(ci, 0x06, 0x1F);
	write_reg(ci, 0x00, 0x31);
	write_regm(ci, 0x20, 0x80, 0x80);
	write_reg(ci, 0x03, 0x02);
	ci->ready = 0;
#endif
#endif
	ci->mode = -1;
	{
		int i;
#if 0
		u8 val;
#endif
		for (i = 0; i < 100; i++) {
			usleep_range(10000, 11000);
#if 0
			read_reg(ci, 0x06, &val);
			dev_info(&ci->i2c->dev, "%d:%02x\n", i, val);
			if (!(val&0x10))
				break;
#else
			if (ci->ready)
				break;
#endif
		}
	}
	mutex_unlock(&ci->lock);
	/* msleep(500); */
	return 0;
}

static int slot_shutdown(struct dvb_ca_en50221 *ca, int slot)
{
	struct cxd *ci = ca->data;

	dev_info(&ci->i2c->dev, "slot_shutdown\n");
	mutex_lock(&ci->lock);
	write_regm(ci, 0x09, 0x08, 0x08);
	write_regm(ci, 0x20, 0x80, 0x80); /* Reset CAM Mode */
	write_regm(ci, 0x06, 0x07, 0x07); /* Clear IO Mode */
	ci->mode = -1;
	mutex_unlock(&ci->lock);
	return 0;
}

static int slot_ts_enable(struct dvb_ca_en50221 *ca, int slot)
{
	struct cxd *ci = ca->data;

	mutex_lock(&ci->lock);
	write_regm(ci, 0x09, 0x00, 0x08);
	set_mode(ci, 0);
#ifdef BUFFER_MODE
	cam_mode(ci, 1);
#endif
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

	if (istat&0x40) {
		ci->dr = 1;
		dev_info(&ci->i2c->dev, "DR\n");
	}
	if (istat&0x20)
		dev_info(&ci->i2c->dev, "WC\n");

	if (istat&2) {
		u8 slotstat;

		read_reg(ci, 0x01, &slotstat);
		if (!(2&slotstat)) {
			if (!ci->slot_stat) {
				ci->slot_stat = DVB_CA_EN50221_POLL_CAM_PRESENT;
				write_regm(ci, 0x03, 0x08, 0x08);
			}

		} else {
			if (ci->slot_stat) {
				ci->slot_stat = 0;
				write_regm(ci, 0x03, 0x00, 0x08);
				dev_info(&ci->i2c->dev, "NO CAM\n");
				ci->ready = 0;
			}
		}
		if (istat&8 &&
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

#ifdef BUFFER_MODE
static int read_data(struct dvb_ca_en50221 *ca, int slot, u8 *ebuf, int ecount)
{
	struct cxd *ci = ca->data;
	u8 msb, lsb;
	u16 len;

	mutex_lock(&ci->lock);
	campoll(ci);
	mutex_unlock(&ci->lock);

	dev_info(&ci->i2c->dev, "read_data\n");
	if (!ci->dr)
		return 0;

	mutex_lock(&ci->lock);
	read_reg(ci, 0x0f, &msb);
	read_reg(ci, 0x10, &lsb);
	len = (msb<<8)|lsb;
	read_block(ci, 0x12, ebuf, len);
	ci->dr = 0;
	mutex_unlock(&ci->lock);

	return len;
}

static int write_data(struct dvb_ca_en50221 *ca, int slot, u8 *ebuf, int ecount)
{
	struct cxd *ci = ca->data;

	mutex_lock(&ci->lock);
	dev_info(&ci->i2c->dev, "write_data %d\n", ecount);
	write_reg(ci, 0x0d, ecount>>8);
	write_reg(ci, 0x0e, ecount&0xff);
	write_block(ci, 0x11, ebuf, ecount);
	mutex_unlock(&ci->lock);
	return ecount;
}
#endif

static struct dvb_ca_en50221 en_templ = {
	.read_attribute_mem  = read_attribute_mem,
	.write_attribute_mem = write_attribute_mem,
	.read_cam_control    = read_cam_control,
	.write_cam_control   = write_cam_control,
	.slot_reset          = slot_reset,
	.slot_shutdown       = slot_shutdown,
	.slot_ts_enable      = slot_ts_enable,
	.poll_slot_status    = poll_slot_status,
#ifdef BUFFER_MODE
	.read_data           = read_data,
	.write_data          = write_data,
#endif

};

struct dvb_ca_en50221 *cxd2099_attach(struct cxd2099_cfg *cfg,
				      void *priv,
				      struct i2c_adapter *i2c)
{
	struct cxd *ci;
	u8 val;

	if (i2c_read_reg(i2c, cfg->adr, 0, &val) < 0) {
		dev_info(&i2c->dev, "No CXD2099 detected at %02x\n", cfg->adr);
		return NULL;
	}

	ci = kzalloc(sizeof(struct cxd), GFP_KERNEL);
	if (!ci)
		return NULL;

	mutex_init(&ci->lock);
	ci->cfg = *cfg;
	ci->i2c = i2c;
	ci->lastaddress = 0xff;
	ci->clk_reg_b = 0x4a;
	ci->clk_reg_f = 0x1b;

	ci->en = en_templ;
	ci->en.data = ci;
	init(ci);
	dev_info(&i2c->dev, "Attached CXD2099AR at %02x\n", ci->cfg.adr);
	return &ci->en;
}
EXPORT_SYMBOL(cxd2099_attach);

MODULE_DESCRIPTION("cxd2099");
MODULE_AUTHOR("Ralph Metzler");
MODULE_LICENSE("GPL");

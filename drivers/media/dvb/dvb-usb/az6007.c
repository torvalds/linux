/*
 * Driver for AzureWave 6007 DVB-C/T USB2.0 and clones
 *
 * Copyright (c) Henry Wang <Henry.wang@AzureWave.com>
 *
 * This driver was made publicly available by Terratec, at:
 *	http://linux.terratec.de/files/TERRATEC_H7/20110323_TERRATEC_H7_Linux.tar.gz
 * The original driver's license is GPL, as declared with MODULE_LICENSE()
 *
 * Copyright (c) 2010-2011 Mauro Carvalho Chehab <mchehab@redhat.com>
 *	Driver modified by in order to work with upstream drxk driver, and
 *	tons of bugs got fixed.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation under version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "drxk.h"
#include "mt2063.h"
#include "dvb_ca_en50221.h"

#define DVB_USB_LOG_PREFIX "az6007"
#include "dvb-usb.h"

/* debug */
int dvb_usb_az6007_debug;
module_param_named(debug, dvb_usb_az6007_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info,xfer=2,rc=4 (or-able))."
		 DVB_USB_DEBUG_STATUS);

#define deb_info(args...) dprintk(dvb_usb_az6007_debug, 0x01, args)
#define deb_xfer(args...) dprintk(dvb_usb_az6007_debug, 0x02, args)
#define deb_rc(args...)   dprintk(dvb_usb_az6007_debug, 0x04, args)
#define deb_fe(args...)   dprintk(dvb_usb_az6007_debug, 0x08, args)

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

/* Known requests (Cypress FX2 firmware + az6007 "private" ones*/

#define FX2_OED			0xb5
#define AZ6007_READ_DATA	0xb7
#define AZ6007_I2C_RD		0xb9
#define AZ6007_POWER		0xbc
#define AZ6007_I2C_WR		0xbd
#define FX2_SCON1		0xc0
#define AZ6007_TS_THROUGH	0xc7
#define AZ6007_READ_IR		0xb4

struct az6007_device_state {
	struct mutex		mutex;
	struct mutex		ca_mutex;
	struct dvb_ca_en50221	ca;
	unsigned		warm:1;
	int			(*gate_ctrl) (struct dvb_frontend *, int);
	unsigned char		data[4096];
};

static struct drxk_config terratec_h7_drxk = {
	.adr = 0x29,
	.parallel_ts = true,
	.dynamic_clk = true,
	.single_master = true,
	.enable_merr_cfg = true,
	.no_i2c_bridge = false,
	.chunk_size = 64,
	.mpeg_out_clk_strength = 0x02,
	.microcode_name = "dvb-usb-terratec-h7-drxk.fw",
};

static int drxk_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct dvb_usb_adapter *adap = fe->sec_priv;
	struct az6007_device_state *st;
	int status = 0;

	deb_info("%s: %s\n", __func__, enable ? "enable" : "disable");

	if (!adap)
		return -EINVAL;

	st = adap->dev->priv;

	if (!st)
		return -EINVAL;

	if (enable)
		status = st->gate_ctrl(fe, 1);
	else
		status = st->gate_ctrl(fe, 0);

	return status;
}

static struct mt2063_config az6007_mt2063_config = {
	.tuner_address = 0x60,
	.refclock = 36125000,
};

static int __az6007_read(struct usb_device *udev, u8 req, u16 value,
			    u16 index, u8 *b, int blen)
{
	int ret;

	ret = usb_control_msg(udev,
			      usb_rcvctrlpipe(udev, 0),
			      req,
			      USB_TYPE_VENDOR | USB_DIR_IN,
			      value, index, b, blen, 5000);
	if (ret < 0) {
		warn("usb read operation failed. (%d)", ret);
		return -EIO;
	}

	deb_xfer("in: req. %02x, val: %04x, ind: %04x, buffer: ", req, value,
		 index);
	debug_dump(b, blen, deb_xfer);

	return ret;
}

static int az6007_read(struct dvb_usb_device *d, u8 req, u16 value,
			    u16 index, u8 *b, int blen)
{
	struct az6007_device_state *st = d->priv;
	int ret;

	if (mutex_lock_interruptible(&st->mutex) < 0)
		return -EAGAIN;

	ret = __az6007_read(d->udev, req, value, index, b, blen);

	mutex_unlock(&st->mutex);

	return ret;
}

static int __az6007_write(struct usb_device *udev, u8 req, u16 value,
			     u16 index, u8 *b, int blen)
{
	int ret;

	deb_xfer("out: req. %02x, val: %04x, ind: %04x, buffer: ", req, value,
		 index);
	debug_dump(b, blen, deb_xfer);

	if (blen > 64) {
		err("az6007: tried to write %d bytes, but I2C max size is 64 bytes\n",
		    blen);
		return -EOPNOTSUPP;
	}

	ret = usb_control_msg(udev,
			      usb_sndctrlpipe(udev, 0),
			      req,
			      USB_TYPE_VENDOR | USB_DIR_OUT,
			      value, index, b, blen, 5000);
	if (ret != blen) {
		err("usb write operation failed. (%d)", ret);
		return -EIO;
	}

	return 0;
}

static int az6007_write(struct dvb_usb_device *d, u8 req, u16 value,
			    u16 index, u8 *b, int blen)
{
	struct az6007_device_state *st = d->priv;
	int ret;

	if (mutex_lock_interruptible(&st->mutex) < 0)
		return -EAGAIN;

	ret = __az6007_write(d->udev, req, value, index, b, blen);

	mutex_unlock(&st->mutex);

	return ret;
}

static int az6007_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	struct dvb_usb_device *d = adap->dev;

	deb_info("%s: %s", __func__, onoff ? "enable" : "disable");

	return az6007_write(d, 0xbc, onoff, 0, NULL, 0);
}

/* remote control stuff (does not work with my box) */
static int az6007_rc_query(struct dvb_usb_device *d)
{
	struct az6007_device_state *st = d->priv;
	unsigned code = 0;

	az6007_read(d, AZ6007_READ_IR, 0, 0, st->data, 10);

	if (st->data[1] == 0x44)
		return 0;

	if ((st->data[1] ^ st->data[2]) == 0xff)
		code = st->data[1];
	else
		code = st->data[1] << 8 | st->data[2];

	if ((st->data[3] ^ st->data[4]) == 0xff)
		code = code << 8 | st->data[3];
	else
		code = code << 16 | st->data[3] << 8 | st->data[4];

	rc_keydown(d->rc_dev, code, st->data[5]);

	return 0;
}

static int az6007_ci_read_attribute_mem(struct dvb_ca_en50221 *ca,
					int slot,
					int address)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;
	struct az6007_device_state *state = (struct az6007_device_state *)d->priv;

	int ret;
	u8 req;
	u16 value;
	u16 index;
	int blen;
	u8 *b;

	if (slot != 0)
		return -EINVAL;

	b = kmalloc(12, GFP_KERNEL);
	if (!b)
		return -ENOMEM;

	mutex_lock(&state->ca_mutex);

	req = 0xC1;
	value = address;
	index = 0;
	blen = 1;

	ret = az6007_read(d, req, value, index, b, blen);
	if (ret < 0) {
		warn("usb in operation failed. (%d)", ret);
		ret = -EINVAL;
	} else {
		ret = b[0];
	}

	mutex_unlock(&state->ca_mutex);
	kfree(b);
	return ret;
}

static int az6007_ci_write_attribute_mem(struct dvb_ca_en50221 *ca,
					 int slot,
					 int address,
					 u8 value)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;
	struct az6007_device_state *state = (struct az6007_device_state *)d->priv;

	int ret;
	u8 req;
	u16 value1;
	u16 index;
	int blen;

	deb_info("%s %d", __func__, slot);
	if (slot != 0)
		return -EINVAL;

	mutex_lock(&state->ca_mutex);
	req = 0xC2;
	value1 = address;
	index = value;
	blen = 0;

	ret = az6007_write(d, req, value1, index, NULL, blen);
	if (ret != 0)
		warn("usb out operation failed. (%d)", ret);

	mutex_unlock(&state->ca_mutex);
	return ret;
}

static int az6007_ci_read_cam_control(struct dvb_ca_en50221 *ca,
				      int slot,
				      u8 address)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;
	struct az6007_device_state *state = (struct az6007_device_state *)d->priv;

	int ret;
	u8 req;
	u16 value;
	u16 index;
	int blen;
	u8 *b;

	if (slot != 0)
		return -EINVAL;

	b = kmalloc(12, GFP_KERNEL);
	if (!b)
		return -ENOMEM;

	mutex_lock(&state->ca_mutex);

	req = 0xC3;
	value = address;
	index = 0;
	blen = 2;

	ret = az6007_read(d, req, value, index, b, blen);
	if (ret < 0) {
		warn("usb in operation failed. (%d)", ret);
		ret = -EINVAL;
	} else {
		if (b[0] == 0)
			warn("Read CI IO error");

		ret = b[1];
		deb_info("read cam data = %x from 0x%x", b[1], value);
	}

	mutex_unlock(&state->ca_mutex);
	kfree(b);
	return ret;
}

static int az6007_ci_write_cam_control(struct dvb_ca_en50221 *ca,
				       int slot,
				       u8 address,
				       u8 value)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;
	struct az6007_device_state *state = (struct az6007_device_state *)d->priv;

	int ret;
	u8 req;
	u16 value1;
	u16 index;
	int blen;

	if (slot != 0)
		return -EINVAL;

	mutex_lock(&state->ca_mutex);
	req = 0xC4;
	value1 = address;
	index = value;
	blen = 0;

	ret = az6007_write(d, req, value1, index, NULL, blen);
	if (ret != 0) {
		warn("usb out operation failed. (%d)", ret);
		goto failed;
	}

failed:
	mutex_unlock(&state->ca_mutex);
	return ret;
}

static int CI_CamReady(struct dvb_ca_en50221 *ca, int slot)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;

	int ret;
	u8 req;
	u16 value;
	u16 index;
	int blen;
	u8 *b;

	b = kmalloc(12, GFP_KERNEL);
	if (!b)
		return -ENOMEM;

	req = 0xC8;
	value = 0;
	index = 0;
	blen = 1;

	ret = az6007_read(d, req, value, index, b, blen);
	if (ret < 0) {
		warn("usb in operation failed. (%d)", ret);
		ret = -EIO;
	} else{
		ret = b[0];
	}
	kfree(b);
	return ret;
}

static int az6007_ci_slot_reset(struct dvb_ca_en50221 *ca, int slot)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;
	struct az6007_device_state *state = (struct az6007_device_state *)d->priv;

	int ret, i;
	u8 req;
	u16 value;
	u16 index;
	int blen;

	mutex_lock(&state->ca_mutex);

	req = 0xC6;
	value = 1;
	index = 0;
	blen = 0;

	ret = az6007_write(d, req, value, index, NULL, blen);
	if (ret != 0) {
		warn("usb out operation failed. (%d)", ret);
		goto failed;
	}

	msleep(500);
	req = 0xC6;
	value = 0;
	index = 0;
	blen = 0;

	ret = az6007_write(d, req, value, index, NULL, blen);
	if (ret != 0) {
		warn("usb out operation failed. (%d)", ret);
		goto failed;
	}

	for (i = 0; i < 15; i++) {
		msleep(100);

		if (CI_CamReady(ca, slot)) {
			deb_info("CAM Ready");
			break;
		}
	}
	msleep(5000);

failed:
	mutex_unlock(&state->ca_mutex);
	return ret;
}

static int az6007_ci_slot_shutdown(struct dvb_ca_en50221 *ca, int slot)
{
	return 0;
}

static int az6007_ci_slot_ts_enable(struct dvb_ca_en50221 *ca, int slot)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;
	struct az6007_device_state *state = (struct az6007_device_state *)d->priv;

	int ret;
	u8 req;
	u16 value;
	u16 index;
	int blen;

	deb_info("%s", __func__);
	mutex_lock(&state->ca_mutex);
	req = 0xC7;
	value = 1;
	index = 0;
	blen = 0;

	ret = az6007_write(d, req, value, index, NULL, blen);
	if (ret != 0) {
		warn("usb out operation failed. (%d)", ret);
		goto failed;
	}

failed:
	mutex_unlock(&state->ca_mutex);
	return ret;
}

static int az6007_ci_poll_slot_status(struct dvb_ca_en50221 *ca, int slot, int open)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;
	struct az6007_device_state *state = (struct az6007_device_state *)d->priv;
	int ret;
	u8 req;
	u16 value;
	u16 index;
	int blen;
	u8 *b;

	b = kmalloc(12, GFP_KERNEL);
	if (!b)
		return -ENOMEM;
	mutex_lock(&state->ca_mutex);

	req = 0xC5;
	value = 0;
	index = 0;
	blen = 1;

	ret = az6007_read(d, req, value, index, b, blen);
	if (ret < 0) {
		warn("usb in operation failed. (%d)", ret);
		ret = -EIO;
	} else
		ret = 0;

	if (!ret && b[0] == 1) {
		ret = DVB_CA_EN50221_POLL_CAM_PRESENT |
		      DVB_CA_EN50221_POLL_CAM_READY;
	}

	mutex_unlock(&state->ca_mutex);
	kfree(b);
	return ret;
}


static void az6007_ci_uninit(struct dvb_usb_device *d)
{
	struct az6007_device_state *state;

	deb_info("%s", __func__);

	if (NULL == d)
		return;

	state = (struct az6007_device_state *)d->priv;
	if (NULL == state)
		return;

	if (NULL == state->ca.data)
		return;

	dvb_ca_en50221_release(&state->ca);

	memset(&state->ca, 0, sizeof(state->ca));
}


static int az6007_ci_init(struct dvb_usb_adapter *a)
{
	struct dvb_usb_device *d = a->dev;
	struct az6007_device_state *state = (struct az6007_device_state *)d->priv;
	int ret;

	deb_info("%s", __func__);

	mutex_init(&state->ca_mutex);

	state->ca.owner			= THIS_MODULE;
	state->ca.read_attribute_mem	= az6007_ci_read_attribute_mem;
	state->ca.write_attribute_mem	= az6007_ci_write_attribute_mem;
	state->ca.read_cam_control	= az6007_ci_read_cam_control;
	state->ca.write_cam_control	= az6007_ci_write_cam_control;
	state->ca.slot_reset		= az6007_ci_slot_reset;
	state->ca.slot_shutdown		= az6007_ci_slot_shutdown;
	state->ca.slot_ts_enable	= az6007_ci_slot_ts_enable;
	state->ca.poll_slot_status	= az6007_ci_poll_slot_status;
	state->ca.data			= d;

	ret = dvb_ca_en50221_init(&a->dvb_adap,
				  &state->ca,
				  0, /* flags */
				  1);/* n_slots */
	if (ret != 0) {
		err("Cannot initialize CI: Error %d.", ret);
		memset(&state->ca, 0, sizeof(state->ca));
		return ret;
	}

	deb_info("CI initialized.");

	return 0;
}

static int az6007_read_mac_addr(struct dvb_usb_device *d, u8 mac[6])
{
	struct az6007_device_state *st = d->priv;
	int ret;

	ret = az6007_read(d, AZ6007_READ_DATA, 6, 0, st->data, 6);
	memcpy(mac, st->data, sizeof(mac));

	if (ret > 0)
		deb_info("%s: mac is %pM\n", __func__, mac);

	return ret;
}

static int az6007_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct az6007_device_state *st = adap->dev->priv;

	deb_info("attaching demod drxk");

	adap->fe_adap[0].fe = dvb_attach(drxk_attach, &terratec_h7_drxk,
					 &adap->dev->i2c_adap);
	if (!adap->fe_adap[0].fe)
		return -EINVAL;

	adap->fe_adap[0].fe->sec_priv = adap;
	st->gate_ctrl = adap->fe_adap[0].fe->ops.i2c_gate_ctrl;
	adap->fe_adap[0].fe->ops.i2c_gate_ctrl = drxk_gate_ctrl;

	az6007_ci_init(adap);

	return 0;
}

static int az6007_tuner_attach(struct dvb_usb_adapter *adap)
{
	deb_info("attaching tuner mt2063");

	/* Attach mt2063 to DVB-C frontend */
	if (adap->fe_adap[0].fe->ops.i2c_gate_ctrl)
		adap->fe_adap[0].fe->ops.i2c_gate_ctrl(adap->fe_adap[0].fe, 1);
	if (!dvb_attach(mt2063_attach, adap->fe_adap[0].fe,
			&az6007_mt2063_config,
			&adap->dev->i2c_adap))
		return -EINVAL;

	if (adap->fe_adap[0].fe->ops.i2c_gate_ctrl)
		adap->fe_adap[0].fe->ops.i2c_gate_ctrl(adap->fe_adap[0].fe, 0);

	return 0;
}

int az6007_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	struct az6007_device_state *st = d->priv;
	int ret;

	deb_info("%s()\n", __func__);

	if (!st->warm) {
		mutex_init(&st->mutex);

		ret = az6007_write(d, AZ6007_POWER, 0, 2, NULL, 0);
		if (ret < 0)
			return ret;
		msleep(60);
		ret = az6007_write(d, AZ6007_POWER, 1, 4, NULL, 0);
		if (ret < 0)
			return ret;
		msleep(100);
		ret = az6007_write(d, AZ6007_POWER, 1, 3, NULL, 0);
		if (ret < 0)
			return ret;
		msleep(20);
		ret = az6007_write(d, AZ6007_POWER, 1, 4, NULL, 0);
		if (ret < 0)
			return ret;

		msleep(400);
		ret = az6007_write(d, FX2_SCON1, 0, 3, NULL, 0);
		if (ret < 0)
			return ret;
		msleep(150);
		ret = az6007_write(d, FX2_SCON1, 1, 3, NULL, 0);
		if (ret < 0)
			return ret;
		msleep(430);
		ret = az6007_write(d, AZ6007_POWER, 0, 0, NULL, 0);
		if (ret < 0)
			return ret;

		st->warm = true;

		return 0;
	}

	if (!onoff)
		return 0;

	az6007_write(d, AZ6007_POWER, 0, 0, NULL, 0);
	az6007_write(d, AZ6007_TS_THROUGH, 0, 0, NULL, 0);

	return 0;
}

/* I2C */
static int az6007_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[],
			   int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	struct az6007_device_state *st = d->priv;
	int i, j, len;
	int ret = 0;
	u16 index;
	u16 value;
	int length;
	u8 req, addr;

	if (mutex_lock_interruptible(&st->mutex) < 0)
		return -EAGAIN;

	for (i = 0; i < num; i++) {
		addr = msgs[i].addr << 1;
		if (((i + 1) < num)
		    && (msgs[i].len == 1)
		    && (!msgs[i].flags & I2C_M_RD)
		    && (msgs[i + 1].flags & I2C_M_RD)
		    && (msgs[i].addr == msgs[i + 1].addr)) {
			/*
			 * A write + read xfer for the same address, where
			 * the first xfer has just 1 byte length.
			 * Need to join both into one operation
			 */
			if (dvb_usb_az6007_debug & 2)
				printk(KERN_DEBUG
				       "az6007 I2C xfer write+read addr=0x%x len=%d/%d: ",
				       addr, msgs[i].len, msgs[i + 1].len);
			req = AZ6007_I2C_RD;
			index = msgs[i].buf[0];
			value = addr | (1 << 8);
			length = 6 + msgs[i + 1].len;
			len = msgs[i + 1].len;
			ret = __az6007_read(d->udev, req, value, index,
					    st->data, length);
			if (ret >= len) {
				for (j = 0; j < len; j++) {
					msgs[i + 1].buf[j] = st->data[j + 5];
					if (dvb_usb_az6007_debug & 2)
						printk(KERN_CONT
						       "0x%02x ",
						       msgs[i + 1].buf[j]);
				}
			} else
				ret = -EIO;
			i++;
		} else if (!(msgs[i].flags & I2C_M_RD)) {
			/* write bytes */
			if (dvb_usb_az6007_debug & 2)
				printk(KERN_DEBUG
				       "az6007 I2C xfer write addr=0x%x len=%d: ",
				       addr, msgs[i].len);
			req = AZ6007_I2C_WR;
			index = msgs[i].buf[0];
			value = addr | (1 << 8);
			length = msgs[i].len - 1;
			len = msgs[i].len - 1;
			if (dvb_usb_az6007_debug & 2)
				printk(KERN_CONT "(0x%02x) ", msgs[i].buf[0]);
			for (j = 0; j < len; j++) {
				st->data[j] = msgs[i].buf[j + 1];
				if (dvb_usb_az6007_debug & 2)
					printk(KERN_CONT "0x%02x ",
					       st->data[j]);
			}
			ret =  __az6007_write(d->udev, req, value, index,
					      st->data, length);
		} else {
			/* read bytes */
			if (dvb_usb_az6007_debug & 2)
				printk(KERN_DEBUG
				       "az6007 I2C xfer read addr=0x%x len=%d: ",
				       addr, msgs[i].len);
			req = AZ6007_I2C_RD;
			index = msgs[i].buf[0];
			value = addr;
			length = msgs[i].len + 6;
			len = msgs[i].len;
			ret = __az6007_read(d->udev, req, value, index,
					    st->data, length);
			for (j = 0; j < len; j++) {
				msgs[i].buf[j] = st->data[j + 5];
				if (dvb_usb_az6007_debug & 2)
					printk(KERN_CONT
					       "0x%02x ", st->data[j + 5]);
			}
		}
		if (dvb_usb_az6007_debug & 2)
			printk(KERN_CONT "\n");
		if (ret < 0)
			goto err;
	}
err:
	mutex_unlock(&st->mutex);

	if (ret < 0) {
		info("%s ERROR: %i", __func__, ret);
		return ret;
	}
	return num;
}

static u32 az6007_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm az6007_i2c_algo = {
	.master_xfer = az6007_i2c_xfer,
	.functionality = az6007_i2c_func,
};

int az6007_identify_state(struct usb_device *udev,
			  struct dvb_usb_device_properties *props,
			  struct dvb_usb_device_description **desc, int *cold)
{
	int ret;
	u8 *mac;

	mac = kmalloc(6, GFP_ATOMIC);
	if (!mac)
		return -ENOMEM;

	/* Try to read the mac address */
	ret = __az6007_read(udev, AZ6007_READ_DATA, 6, 0, mac, 6);
	if (ret == 6)
		*cold = 0;
	else
		*cold = 1;

	kfree(mac);

	if (*cold) {
		__az6007_write(udev, 0x09, 1, 0, NULL, 0);
		__az6007_write(udev, 0x00, 0, 0, NULL, 0);
		__az6007_write(udev, 0x00, 0, 0, NULL, 0);
	}

	deb_info("Device is on %s state\n", *cold ? "warm" : "cold");
	return 0;
}

static struct dvb_usb_device_properties az6007_properties;

static void az6007_usb_disconnect(struct usb_interface *intf)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);
	az6007_ci_uninit(d);
	dvb_usb_device_exit(intf);
}

static int az6007_usb_probe(struct usb_interface *intf,
			    const struct usb_device_id *id)
{
	return dvb_usb_device_init(intf, &az6007_properties,
				   THIS_MODULE, NULL, adapter_nr);
}

static struct usb_device_id az6007_usb_table[] = {
	{USB_DEVICE(USB_VID_AZUREWAVE, USB_PID_AZUREWAVE_6007)},
	{USB_DEVICE(USB_VID_TERRATEC, USB_PID_TERRATEC_H7)},
	{USB_DEVICE(USB_VID_TERRATEC, USB_PID_TERRATEC_H7_2)},
	{0},
};

MODULE_DEVICE_TABLE(usb, az6007_usb_table);

static struct dvb_usb_device_properties az6007_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,
	.usb_ctrl = CYPRESS_FX2,
	.firmware            = "dvb-usb-terratec-h7-az6007.fw",
	.no_reconnect        = 1,
	.size_of_priv        = sizeof(struct az6007_device_state),
	.identify_state	     = az6007_identify_state,
	.num_adapters = 1,
	.adapter = {
		{
		.num_frontends = 1,
		.fe = {{
			.streaming_ctrl   = az6007_streaming_ctrl,
			.tuner_attach     = az6007_tuner_attach,
			.frontend_attach  = az6007_frontend_attach,

			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 10,
				.endpoint = 0x02,
				.u = {
					.bulk = {
						.buffersize = 4096,
					}
				}
			},
		} }
	} },
	.power_ctrl       = az6007_power_ctrl,
	.read_mac_address = az6007_read_mac_addr,

	.rc.core = {
		.rc_interval      = 400,
		.rc_codes         = RC_MAP_NEC_TERRATEC_CINERGY_XS,
		.module_name	  = "az6007",
		.rc_query         = az6007_rc_query,
		.allowed_protos   = RC_TYPE_NEC,
	},
	.i2c_algo         = &az6007_i2c_algo,

	.num_device_descs = 2,
	.devices = {
		{ .name = "AzureWave DTV StarBox DVB-T/C USB2.0 (az6007)",
		  .cold_ids = { &az6007_usb_table[0], NULL },
		  .warm_ids = { NULL },
		},
		{ .name = "TerraTec DTV StarBox DVB-T/C USB2.0 (az6007)",
		  .cold_ids = { &az6007_usb_table[1], &az6007_usb_table[2], NULL },
		  .warm_ids = { NULL },
		},
		{ NULL },
	}
};

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver az6007_usb_driver = {
	.name		= "dvb_usb_az6007",
	.probe		= az6007_usb_probe,
	.disconnect	= az6007_usb_disconnect,
	.id_table	= az6007_usb_table,
};

/* module stuff */
static int __init az6007_usb_module_init(void)
{
	int result;
	deb_info("az6007 usb module init\n");

	result = usb_register(&az6007_usb_driver);
	if (result) {
		err("usb_register failed. (%d)", result);
		return result;
	}

	return 0;
}

static void __exit az6007_usb_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	deb_info("az6007 usb module exit\n");
	usb_deregister(&az6007_usb_driver);
}

module_init(az6007_usb_module_init);
module_exit(az6007_usb_module_exit);

MODULE_AUTHOR("Henry Wang <Henry.wang@AzureWave.com>");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
MODULE_DESCRIPTION("Driver for AzureWave 6007 DVB-C/T USB2.0 and clones");
MODULE_VERSION("1.1");
MODULE_LICENSE("GPL");

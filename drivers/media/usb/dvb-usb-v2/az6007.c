/*
 * Driver for AzureWave 6007 DVB-C/T USB2.0 and clones
 *
 * Copyright (c) Henry Wang <Henry.wang@AzureWave.com>
 *
 * This driver was made publicly available by Terratec, at:
 *	http://linux.terratec.de/files/TERRATEC_H7/20110323_TERRATEC_H7_Linux.tar.gz
 * The original driver's license is GPL, as declared with MODULE_LICENSE()
 *
 * Copyright (c) 2010-2012 Mauro Carvalho Chehab
 *	Driver modified by in order to work with upstream drxk driver, and
 *	tons of bugs got fixed, and converted to use dvb-usb-v2.
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
#include "dvb_usb.h"
#include "cypress_firmware.h"

#define AZ6007_FIRMWARE "dvb-usb-terratec-h7-az6007.fw"

static int az6007_xfer_debug;
module_param_named(xfer_debug, az6007_xfer_debug, int, 0644);
MODULE_PARM_DESC(xfer_debug, "Enable xfer debug");

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
	.qam_demod_parameter_count = 2,
	.microcode_name = "dvb-usb-terratec-h7-drxk.fw",
};

static struct drxk_config cablestar_hdci_drxk = {
	.adr = 0x29,
	.parallel_ts = true,
	.dynamic_clk = true,
	.single_master = true,
	.enable_merr_cfg = true,
	.no_i2c_bridge = false,
	.chunk_size = 64,
	.mpeg_out_clk_strength = 0x02,
	.qam_demod_parameter_count = 2,
	.microcode_name = "dvb-usb-technisat-cablestar-hdci-drxk.fw",
};

static int drxk_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct az6007_device_state *st = fe_to_priv(fe);
	struct dvb_usb_adapter *adap = fe->sec_priv;
	int status = 0;

	pr_debug("%s: %s\n", __func__, enable ? "enable" : "disable");

	if (!adap || !st)
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
		pr_warn("usb read operation failed. (%d)\n", ret);
		return -EIO;
	}

	if (az6007_xfer_debug) {
		printk(KERN_DEBUG "az6007: IN  req: %02x, value: %04x, index: %04x\n",
		       req, value, index);
		print_hex_dump_bytes("az6007: payload: ",
				     DUMP_PREFIX_NONE, b, blen);
	}

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

	if (az6007_xfer_debug) {
		printk(KERN_DEBUG "az6007: OUT req: %02x, value: %04x, index: %04x\n",
		       req, value, index);
		print_hex_dump_bytes("az6007: payload: ",
				     DUMP_PREFIX_NONE, b, blen);
	}

	if (blen > 64) {
		pr_err("az6007: tried to write %d bytes, but I2C max size is 64 bytes\n",
		       blen);
		return -EOPNOTSUPP;
	}

	ret = usb_control_msg(udev,
			      usb_sndctrlpipe(udev, 0),
			      req,
			      USB_TYPE_VENDOR | USB_DIR_OUT,
			      value, index, b, blen, 5000);
	if (ret != blen) {
		pr_err("usb write operation failed. (%d)\n", ret);
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

static int az6007_streaming_ctrl(struct dvb_frontend *fe, int onoff)
{
	struct dvb_usb_device *d = fe_to_d(fe);

	pr_debug("%s: %s\n", __func__, onoff ? "enable" : "disable");

	return az6007_write(d, 0xbc, onoff, 0, NULL, 0);
}

#if IS_ENABLED(CONFIG_RC_CORE)
/* remote control stuff (does not work with my box) */
static int az6007_rc_query(struct dvb_usb_device *d)
{
	struct az6007_device_state *st = d_to_priv(d);
	unsigned code;
	enum rc_proto proto;

	az6007_read(d, AZ6007_READ_IR, 0, 0, st->data, 10);

	if (st->data[1] == 0x44)
		return 0;

	if ((st->data[3] ^ st->data[4]) == 0xff) {
		if ((st->data[1] ^ st->data[2]) == 0xff) {
			code = RC_SCANCODE_NEC(st->data[1], st->data[3]);
			proto = RC_PROTO_NEC;
		} else {
			code = RC_SCANCODE_NECX(st->data[1] << 8 | st->data[2],
						st->data[3]);
			proto = RC_PROTO_NECX;
		}
	} else {
		code = RC_SCANCODE_NEC32(st->data[1] << 24 |
					 st->data[2] << 16 |
					 st->data[3] << 8  |
					 st->data[4]);
		proto = RC_PROTO_NEC32;
	}

	rc_keydown(d->rc_dev, proto, code, st->data[5]);

	return 0;
}

static int az6007_get_rc_config(struct dvb_usb_device *d, struct dvb_usb_rc *rc)
{
	pr_debug("Getting az6007 Remote Control properties\n");

	rc->allowed_protos = RC_PROTO_BIT_NEC | RC_PROTO_BIT_NECX |
						RC_PROTO_BIT_NEC32;
	rc->query          = az6007_rc_query;
	rc->interval       = 400;

	return 0;
}
#else
	#define az6007_get_rc_config NULL
#endif

static int az6007_ci_read_attribute_mem(struct dvb_ca_en50221 *ca,
					int slot,
					int address)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;
	struct az6007_device_state *state = d_to_priv(d);

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
		pr_warn("usb in operation failed. (%d)\n", ret);
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
	struct az6007_device_state *state = d_to_priv(d);

	int ret;
	u8 req;
	u16 value1;
	u16 index;
	int blen;

	pr_debug("%s(), slot %d\n", __func__, slot);
	if (slot != 0)
		return -EINVAL;

	mutex_lock(&state->ca_mutex);
	req = 0xC2;
	value1 = address;
	index = value;
	blen = 0;

	ret = az6007_write(d, req, value1, index, NULL, blen);
	if (ret != 0)
		pr_warn("usb out operation failed. (%d)\n", ret);

	mutex_unlock(&state->ca_mutex);
	return ret;
}

static int az6007_ci_read_cam_control(struct dvb_ca_en50221 *ca,
				      int slot,
				      u8 address)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;
	struct az6007_device_state *state = d_to_priv(d);

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
		pr_warn("usb in operation failed. (%d)\n", ret);
		ret = -EINVAL;
	} else {
		if (b[0] == 0)
			pr_warn("Read CI IO error\n");

		ret = b[1];
		pr_debug("read cam data = %x from 0x%x\n", b[1], value);
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
	struct az6007_device_state *state = d_to_priv(d);

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
		pr_warn("usb out operation failed. (%d)\n", ret);
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
		pr_warn("usb in operation failed. (%d)\n", ret);
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
	struct az6007_device_state *state = d_to_priv(d);

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
		pr_warn("usb out operation failed. (%d)\n", ret);
		goto failed;
	}

	msleep(500);
	req = 0xC6;
	value = 0;
	index = 0;
	blen = 0;

	ret = az6007_write(d, req, value, index, NULL, blen);
	if (ret != 0) {
		pr_warn("usb out operation failed. (%d)\n", ret);
		goto failed;
	}

	for (i = 0; i < 15; i++) {
		msleep(100);

		if (CI_CamReady(ca, slot)) {
			pr_debug("CAM Ready\n");
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
	struct az6007_device_state *state = d_to_priv(d);

	int ret;
	u8 req;
	u16 value;
	u16 index;
	int blen;

	pr_debug("%s()\n", __func__);
	mutex_lock(&state->ca_mutex);
	req = 0xC7;
	value = 1;
	index = 0;
	blen = 0;

	ret = az6007_write(d, req, value, index, NULL, blen);
	if (ret != 0) {
		pr_warn("usb out operation failed. (%d)\n", ret);
		goto failed;
	}

failed:
	mutex_unlock(&state->ca_mutex);
	return ret;
}

static int az6007_ci_poll_slot_status(struct dvb_ca_en50221 *ca, int slot, int open)
{
	struct dvb_usb_device *d = (struct dvb_usb_device *)ca->data;
	struct az6007_device_state *state = d_to_priv(d);
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
		pr_warn("usb in operation failed. (%d)\n", ret);
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

	pr_debug("%s()\n", __func__);

	if (NULL == d)
		return;

	state = d_to_priv(d);
	if (NULL == state)
		return;

	if (NULL == state->ca.data)
		return;

	dvb_ca_en50221_release(&state->ca);

	memset(&state->ca, 0, sizeof(state->ca));
}


static int az6007_ci_init(struct dvb_usb_adapter *adap)
{
	struct dvb_usb_device *d = adap_to_d(adap);
	struct az6007_device_state *state = adap_to_priv(adap);
	int ret;

	pr_debug("%s()\n", __func__);

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

	ret = dvb_ca_en50221_init(&adap->dvb_adap,
				  &state->ca,
				  0, /* flags */
				  1);/* n_slots */
	if (ret != 0) {
		pr_err("Cannot initialize CI: Error %d.\n", ret);
		memset(&state->ca, 0, sizeof(state->ca));
		return ret;
	}

	pr_debug("CI initialized.\n");

	return 0;
}

static int az6007_read_mac_addr(struct dvb_usb_adapter *adap, u8 mac[6])
{
	struct dvb_usb_device *d = adap_to_d(adap);
	struct az6007_device_state *st = adap_to_priv(adap);
	int ret;

	ret = az6007_read(d, AZ6007_READ_DATA, 6, 0, st->data, 6);
	memcpy(mac, st->data, 6);

	if (ret > 0)
		pr_debug("%s: mac is %pM\n", __func__, mac);

	return ret;
}

static int az6007_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct az6007_device_state *st = adap_to_priv(adap);
	struct dvb_usb_device *d = adap_to_d(adap);

	pr_debug("attaching demod drxk\n");

	adap->fe[0] = dvb_attach(drxk_attach, &terratec_h7_drxk,
				 &d->i2c_adap);
	if (!adap->fe[0])
		return -EINVAL;

	adap->fe[0]->sec_priv = adap;
	st->gate_ctrl = adap->fe[0]->ops.i2c_gate_ctrl;
	adap->fe[0]->ops.i2c_gate_ctrl = drxk_gate_ctrl;

	az6007_ci_init(adap);

	return 0;
}

static int az6007_cablestar_hdci_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct az6007_device_state *st = adap_to_priv(adap);
	struct dvb_usb_device *d = adap_to_d(adap);

	pr_debug("attaching demod drxk\n");

	adap->fe[0] = dvb_attach(drxk_attach, &cablestar_hdci_drxk,
				 &d->i2c_adap);
	if (!adap->fe[0])
		return -EINVAL;

	adap->fe[0]->sec_priv = adap;
	st->gate_ctrl = adap->fe[0]->ops.i2c_gate_ctrl;
	adap->fe[0]->ops.i2c_gate_ctrl = drxk_gate_ctrl;

	az6007_ci_init(adap);

	return 0;
}

static int az6007_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct dvb_usb_device *d = adap_to_d(adap);

	pr_debug("attaching tuner mt2063\n");

	/* Attach mt2063 to DVB-C frontend */
	if (adap->fe[0]->ops.i2c_gate_ctrl)
		adap->fe[0]->ops.i2c_gate_ctrl(adap->fe[0], 1);
	if (!dvb_attach(mt2063_attach, adap->fe[0],
			&az6007_mt2063_config,
			&d->i2c_adap))
		return -EINVAL;

	if (adap->fe[0]->ops.i2c_gate_ctrl)
		adap->fe[0]->ops.i2c_gate_ctrl(adap->fe[0], 0);

	return 0;
}

static int az6007_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	struct az6007_device_state *state = d_to_priv(d);
	int ret;

	pr_debug("%s()\n", __func__);

	if (!state->warm) {
		mutex_init(&state->mutex);

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

		state->warm = true;

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
	struct az6007_device_state *st = d_to_priv(d);
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
		    && ((msgs[i].flags & I2C_M_RD) != I2C_M_RD)
		    && (msgs[i + 1].flags & I2C_M_RD)
		    && (msgs[i].addr == msgs[i + 1].addr)) {
			/*
			 * A write + read xfer for the same address, where
			 * the first xfer has just 1 byte length.
			 * Need to join both into one operation
			 */
			if (az6007_xfer_debug)
				printk(KERN_DEBUG "az6007: I2C W/R addr=0x%x len=%d/%d\n",
				       addr, msgs[i].len, msgs[i + 1].len);
			req = AZ6007_I2C_RD;
			index = msgs[i].buf[0];
			value = addr | (1 << 8);
			length = 6 + msgs[i + 1].len;
			len = msgs[i + 1].len;
			ret = __az6007_read(d->udev, req, value, index,
					    st->data, length);
			if (ret >= len) {
				for (j = 0; j < len; j++)
					msgs[i + 1].buf[j] = st->data[j + 5];
			} else
				ret = -EIO;
			i++;
		} else if (!(msgs[i].flags & I2C_M_RD)) {
			/* write bytes */
			if (az6007_xfer_debug)
				printk(KERN_DEBUG "az6007: I2C W addr=0x%x len=%d\n",
				       addr, msgs[i].len);
			req = AZ6007_I2C_WR;
			index = msgs[i].buf[0];
			value = addr | (1 << 8);
			length = msgs[i].len - 1;
			len = msgs[i].len - 1;
			for (j = 0; j < len; j++)
				st->data[j] = msgs[i].buf[j + 1];
			ret =  __az6007_write(d->udev, req, value, index,
					      st->data, length);
		} else {
			/* read bytes */
			if (az6007_xfer_debug)
				printk(KERN_DEBUG "az6007: I2C R addr=0x%x len=%d\n",
				       addr, msgs[i].len);
			req = AZ6007_I2C_RD;
			index = msgs[i].buf[0];
			value = addr;
			length = msgs[i].len + 6;
			len = msgs[i].len;
			ret = __az6007_read(d->udev, req, value, index,
					    st->data, length);
			for (j = 0; j < len; j++)
				msgs[i].buf[j] = st->data[j + 5];
		}
		if (ret < 0)
			goto err;
	}
err:
	mutex_unlock(&st->mutex);

	if (ret < 0) {
		pr_info("%s ERROR: %i\n", __func__, ret);
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

static int az6007_identify_state(struct dvb_usb_device *d, const char **name)
{
	int ret;
	u8 *mac;

	pr_debug("Identifying az6007 state\n");

	mac = kmalloc(6, GFP_ATOMIC);
	if (!mac)
		return -ENOMEM;

	/* Try to read the mac address */
	ret = __az6007_read(d->udev, AZ6007_READ_DATA, 6, 0, mac, 6);
	if (ret == 6)
		ret = WARM;
	else
		ret = COLD;

	kfree(mac);

	if (ret == COLD) {
		__az6007_write(d->udev, 0x09, 1, 0, NULL, 0);
		__az6007_write(d->udev, 0x00, 0, 0, NULL, 0);
		__az6007_write(d->udev, 0x00, 0, 0, NULL, 0);
	}

	pr_debug("Device is on %s state\n",
		 ret == WARM ? "warm" : "cold");
	return ret;
}

static void az6007_usb_disconnect(struct usb_interface *intf)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);
	az6007_ci_uninit(d);
	dvb_usbv2_disconnect(intf);
}

static int az6007_download_firmware(struct dvb_usb_device *d,
	const struct firmware *fw)
{
	pr_debug("Loading az6007 firmware\n");

	return cypress_load_firmware(d->udev, fw, CYPRESS_FX2);
}

/* DVB USB Driver stuff */
static struct dvb_usb_device_properties az6007_props = {
	.driver_name         = KBUILD_MODNAME,
	.owner               = THIS_MODULE,
	.firmware            = AZ6007_FIRMWARE,

	.adapter_nr          = adapter_nr,
	.size_of_priv        = sizeof(struct az6007_device_state),
	.i2c_algo            = &az6007_i2c_algo,
	.tuner_attach        = az6007_tuner_attach,
	.frontend_attach     = az6007_frontend_attach,
	.streaming_ctrl      = az6007_streaming_ctrl,
	.get_rc_config       = az6007_get_rc_config,
	.read_mac_address    = az6007_read_mac_addr,
	.download_firmware   = az6007_download_firmware,
	.identify_state	     = az6007_identify_state,
	.power_ctrl          = az6007_power_ctrl,
	.num_adapters        = 1,
	.adapter             = {
		{ .stream = DVB_USB_STREAM_BULK(0x02, 10, 4096), }
	}
};

static struct dvb_usb_device_properties az6007_cablestar_hdci_props = {
	.driver_name         = KBUILD_MODNAME,
	.owner               = THIS_MODULE,
	.firmware            = AZ6007_FIRMWARE,

	.adapter_nr          = adapter_nr,
	.size_of_priv        = sizeof(struct az6007_device_state),
	.i2c_algo            = &az6007_i2c_algo,
	.tuner_attach        = az6007_tuner_attach,
	.frontend_attach     = az6007_cablestar_hdci_frontend_attach,
	.streaming_ctrl      = az6007_streaming_ctrl,
/* ditch get_rc_config as it can't work (TS35 remote, I believe it's rc5) */
	.get_rc_config       = NULL,
	.read_mac_address    = az6007_read_mac_addr,
	.download_firmware   = az6007_download_firmware,
	.identify_state	     = az6007_identify_state,
	.power_ctrl          = az6007_power_ctrl,
	.num_adapters        = 1,
	.adapter             = {
		{ .stream = DVB_USB_STREAM_BULK(0x02, 10, 4096), }
	}
};

static const struct usb_device_id az6007_usb_table[] = {
	{DVB_USB_DEVICE(USB_VID_AZUREWAVE, USB_PID_AZUREWAVE_6007,
		&az6007_props, "Azurewave 6007", RC_MAP_EMPTY)},
	{DVB_USB_DEVICE(USB_VID_TERRATEC, USB_PID_TERRATEC_H7,
		&az6007_props, "Terratec H7", RC_MAP_NEC_TERRATEC_CINERGY_XS)},
	{DVB_USB_DEVICE(USB_VID_TERRATEC, USB_PID_TERRATEC_H7_2,
		&az6007_props, "Terratec H7", RC_MAP_NEC_TERRATEC_CINERGY_XS)},
	{DVB_USB_DEVICE(USB_VID_TECHNISAT, USB_PID_TECHNISAT_USB2_CABLESTAR_HDCI,
		&az6007_cablestar_hdci_props, "Technisat CableStar Combo HD CI", RC_MAP_EMPTY)},
	{0},
};

MODULE_DEVICE_TABLE(usb, az6007_usb_table);

static int az6007_suspend(struct usb_interface *intf, pm_message_t msg)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);

	az6007_ci_uninit(d);
	return dvb_usbv2_suspend(intf, msg);
}

static int az6007_resume(struct usb_interface *intf)
{
	struct dvb_usb_device *d = usb_get_intfdata(intf);
	struct dvb_usb_adapter *adap = &d->adapter[0];

	az6007_ci_init(adap);
	return dvb_usbv2_resume(intf);
}

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver az6007_usb_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= az6007_usb_table,
	.probe		= dvb_usbv2_probe,
	.disconnect	= az6007_usb_disconnect,
	.no_dynamic_id	= 1,
	.soft_unbind	= 1,
	/*
	 * FIXME: need to implement reset_resume, likely with
	 * dvb-usb-v2 core support
	 */
	.suspend	= az6007_suspend,
	.resume		= az6007_resume,
};

module_usb_driver(az6007_usb_driver);

MODULE_AUTHOR("Henry Wang <Henry.wang@AzureWave.com>");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_DESCRIPTION("Driver for AzureWave 6007 DVB-C/T USB2.0 and clones");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(AZ6007_FIRMWARE);

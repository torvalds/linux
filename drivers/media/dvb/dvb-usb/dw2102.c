/* DVB USB framework compliant Linux driver for the
*	DVBWorld DVB-S 2101, 2102, DVB-S2 2104 Card
*
* Copyright (C) 2008 Igor M. Liplianin (liplianin@me.by)
*
*	This program is free software; you can redistribute it and/or modify it
*	under the terms of the GNU General Public License as published by the
*	Free Software Foundation, version 2.
*
* see Documentation/dvb/README.dvb-usb for more information
*/
#include "dw2102.h"
#include "si21xx.h"
#include "stv0299.h"
#include "z0194a.h"
#include "stv0288.h"
#include "stb6000.h"
#include "eds1547.h"
#include "cx24116.h"

#ifndef USB_PID_DW2102
#define USB_PID_DW2102 0x2102
#endif

#ifndef USB_PID_DW2104
#define USB_PID_DW2104 0x2104
#endif

#ifndef USB_PID_CINERGY_S
#define USB_PID_CINERGY_S 0x0064
#endif

#define DW210X_READ_MSG 0
#define DW210X_WRITE_MSG 1

#define REG_1F_SYMBOLRATE_BYTE0 0x1f
#define REG_20_SYMBOLRATE_BYTE1 0x20
#define REG_21_SYMBOLRATE_BYTE2 0x21
/* on my own*/
#define DW2102_VOLTAGE_CTRL (0x1800)
#define DW2102_RC_QUERY (0x1a00)

struct dw210x_state {
	u32 last_key_pressed;
};
struct dw210x_rc_keys {
	u32 keycode;
	u32 event;
};

/* debug */
static int dvb_usb_dw2102_debug;
module_param_named(debug, dvb_usb_dw2102_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info 2=xfer 4=rc(or-able))."
						DVB_USB_DEBUG_STATUS);


DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static int dw210x_op_rw(struct usb_device *dev, u8 request, u16 value,
			u16 index, u8 * data, u16 len, int flags)
{
	int ret;
	u8 u8buf[len];

	unsigned int pipe = (flags == DW210X_READ_MSG) ?
				usb_rcvctrlpipe(dev, 0) : usb_sndctrlpipe(dev, 0);
	u8 request_type = (flags == DW210X_READ_MSG) ? USB_DIR_IN : USB_DIR_OUT;

	if (flags == DW210X_WRITE_MSG)
		memcpy(u8buf, data, len);
	ret = usb_control_msg(dev, pipe, request, request_type | USB_TYPE_VENDOR,
				value, index , u8buf, len, 2000);

	if (flags == DW210X_READ_MSG)
		memcpy(data, u8buf, len);
	return ret;
}

/* I2C */
static int dw2102_i2c_transfer(struct i2c_adapter *adap, struct i2c_msg msg[],
		int num)
{
struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int i = 0, ret = 0;
	u8 buf6[] = {0x2c, 0x05, 0xc0, 0, 0, 0, 0};
	u16 value;

	if (!d)
		return -ENODEV;
	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	switch (num) {
	case 2:
		/* read stv0299 register */
		value = msg[0].buf[0];/* register */
		for (i = 0; i < msg[1].len; i++) {
			value = value + i;
			ret = dw210x_op_rw(d->udev, 0xb5, value, 0,
					buf6, 2, DW210X_READ_MSG);
			msg[1].buf[i] = buf6[0];
		}
		break;
	case 1:
		switch (msg[0].addr) {
		case 0x68:
			/* write to stv0299 register */
			buf6[0] = 0x2a;
			buf6[1] = msg[0].buf[0];
			buf6[2] = msg[0].buf[1];
			ret = dw210x_op_rw(d->udev, 0xb2, 0, 0,
					buf6, 3, DW210X_WRITE_MSG);
			break;
		case 0x60:
			if (msg[0].flags == 0) {
			/* write to tuner pll */
				buf6[0] = 0x2c;
				buf6[1] = 5;
				buf6[2] = 0xc0;
				buf6[3] = msg[0].buf[0];
				buf6[4] = msg[0].buf[1];
				buf6[5] = msg[0].buf[2];
				buf6[6] = msg[0].buf[3];
				ret = dw210x_op_rw(d->udev, 0xb2, 0, 0,
						buf6, 7, DW210X_WRITE_MSG);
			} else {
			/* read from tuner */
				ret = dw210x_op_rw(d->udev, 0xb5, 0, 0,
						buf6, 1, DW210X_READ_MSG);
				msg[0].buf[0] = buf6[0];
			}
			break;
		case (DW2102_RC_QUERY):
			ret  = dw210x_op_rw(d->udev, 0xb8, 0, 0,
					buf6, 2, DW210X_READ_MSG);
			msg[0].buf[0] = buf6[0];
			msg[0].buf[1] = buf6[1];
			break;
		case (DW2102_VOLTAGE_CTRL):
			buf6[0] = 0x30;
			buf6[1] = msg[0].buf[0];
			ret = dw210x_op_rw(d->udev, 0xb2, 0, 0,
					buf6, 2, DW210X_WRITE_MSG);
			break;
		}

		break;
	}

	mutex_unlock(&d->i2c_mutex);
	return num;
}

static int dw2102_serit_i2c_transfer(struct i2c_adapter *adap,
						struct i2c_msg msg[], int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int ret = 0;
	u8 buf6[] = {0, 0, 0, 0, 0, 0, 0};

	if (!d)
		return -ENODEV;
	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	switch (num) {
	case 2:
		/* read si2109 register by number */
		buf6[0] = 0xd0;
		buf6[1] = msg[0].len;
		buf6[2] = msg[0].buf[0];
		ret = dw210x_op_rw(d->udev, 0xc2, 0, 0,
				buf6, msg[0].len + 2, DW210X_WRITE_MSG);
		/* read si2109 register */
		ret = dw210x_op_rw(d->udev, 0xc3, 0xd0, 0,
				buf6, msg[1].len + 2, DW210X_READ_MSG);
		memcpy(msg[1].buf, buf6 + 2, msg[1].len);

		break;
	case 1:
		switch (msg[0].addr) {
		case 0x68:
			/* write to si2109 register */
			buf6[0] = 0xd0;
			buf6[1] = msg[0].len;
			memcpy(buf6 + 2, msg[0].buf, msg[0].len);
			ret = dw210x_op_rw(d->udev, 0xc2, 0, 0, buf6,
					msg[0].len + 2, DW210X_WRITE_MSG);
			break;
		case(DW2102_RC_QUERY):
			ret  = dw210x_op_rw(d->udev, 0xb8, 0, 0,
					buf6, 2, DW210X_READ_MSG);
			msg[0].buf[0] = buf6[0];
			msg[0].buf[1] = buf6[1];
			break;
		case(DW2102_VOLTAGE_CTRL):
			buf6[0] = 0x30;
			buf6[1] = msg[0].buf[0];
			ret = dw210x_op_rw(d->udev, 0xb2, 0, 0,
					buf6, 2, DW210X_WRITE_MSG);
			break;
		}
		break;
	}

	mutex_unlock(&d->i2c_mutex);
	return num;
}
static int dw2102_earda_i2c_transfer(struct i2c_adapter *adap, struct i2c_msg msg[], int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int ret = 0;

	if (!d)
		return -ENODEV;
	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	switch (num) {
	case 2: {
		/* read */
		/* first write first register number */
		u8 ibuf [msg[1].len + 2], obuf[3];
		obuf[0] = 0xd0;
		obuf[1] = msg[0].len;
		obuf[2] = msg[0].buf[0];
		ret = dw210x_op_rw(d->udev, 0xc2, 0, 0,
				obuf, msg[0].len + 2, DW210X_WRITE_MSG);
		/* second read registers */
		ret = dw210x_op_rw(d->udev, 0xc3, 0xd1 , 0,
				ibuf, msg[1].len + 2, DW210X_READ_MSG);
		memcpy(msg[1].buf, ibuf + 2, msg[1].len);

		break;
	}
	case 1:
		switch (msg[0].addr) {
		case 0x68: {
			/* write to register */
			u8 obuf[msg[0].len + 2];
			obuf[0] = 0xd0;
			obuf[1] = msg[0].len;
			memcpy(obuf + 2, msg[0].buf, msg[0].len);
			ret = dw210x_op_rw(d->udev, 0xc2, 0, 0,
					obuf, msg[0].len + 2, DW210X_WRITE_MSG);
			break;
		}
		case 0x61: {
			/* write to tuner */
			u8 obuf[msg[0].len + 2];
			obuf[0] = 0xc2;
			obuf[1] = msg[0].len;
			memcpy(obuf + 2, msg[0].buf, msg[0].len);
			ret = dw210x_op_rw(d->udev, 0xc2, 0, 0,
					obuf, msg[0].len + 2, DW210X_WRITE_MSG);
			break;
		}
		case(DW2102_RC_QUERY): {
			u8 ibuf[2];
			ret  = dw210x_op_rw(d->udev, 0xb8, 0, 0,
					ibuf, 2, DW210X_READ_MSG);
			memcpy(msg[0].buf, ibuf , 2);
			break;
		}
		case(DW2102_VOLTAGE_CTRL): {
			u8 obuf[2];
			obuf[0] = 0x30;
			obuf[1] = msg[0].buf[0];
			ret = dw210x_op_rw(d->udev, 0xb2, 0, 0,
					obuf, 2, DW210X_WRITE_MSG);
			break;
		}
		}

		break;
	}

	mutex_unlock(&d->i2c_mutex);
	return num;
}

static int dw2104_i2c_transfer(struct i2c_adapter *adap, struct i2c_msg msg[], int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int ret = 0;
	int len, i;

	if (!d)
		return -ENODEV;
	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	switch (num) {
	case 2: {
		/* read */
		/* first write first register number */
		u8 ibuf [msg[1].len + 2], obuf[3];
		obuf[0] = 0xaa;
		obuf[1] = msg[0].len;
		obuf[2] = msg[0].buf[0];
		ret = dw210x_op_rw(d->udev, 0xc2, 0, 0,
				obuf, msg[0].len + 2, DW210X_WRITE_MSG);
		/* second read registers */
		ret = dw210x_op_rw(d->udev, 0xc3, 0xab , 0,
				ibuf, msg[1].len + 2, DW210X_READ_MSG);
		memcpy(msg[1].buf, ibuf + 2, msg[1].len);

		break;
	}
	case 1:
		switch (msg[0].addr) {
		case 0x55: {
			if (msg[0].buf[0] == 0xf7) {
				/* firmware */
				/* Write in small blocks */
				u8 obuf[19];
				obuf[0] = 0xaa;
				obuf[1] = 0x11;
				obuf[2] = 0xf7;
				len = msg[0].len - 1;
				i = 1;
				do {
					memcpy(obuf + 3, msg[0].buf + i, (len > 16 ? 16 : len));
					ret = dw210x_op_rw(d->udev, 0xc2, 0, 0,
						obuf, (len > 16 ? 16 : len) + 3, DW210X_WRITE_MSG);
					i += 16;
					len -= 16;
				} while (len > 0);
			} else {
				/* write to register */
				u8 obuf[msg[0].len + 2];
				obuf[0] = 0xaa;
				obuf[1] = msg[0].len;
				memcpy(obuf + 2, msg[0].buf, msg[0].len);
				ret = dw210x_op_rw(d->udev, 0xc2, 0, 0,
						obuf, msg[0].len + 2, DW210X_WRITE_MSG);
			}
			break;
		}
		case(DW2102_RC_QUERY): {
			u8 ibuf[2];
			ret  = dw210x_op_rw(d->udev, 0xb8, 0, 0,
					ibuf, 2, DW210X_READ_MSG);
			memcpy(msg[0].buf, ibuf , 2);
			break;
		}
		case(DW2102_VOLTAGE_CTRL): {
			u8 obuf[2];
			obuf[0] = 0x30;
			obuf[1] = msg[0].buf[0];
			ret = dw210x_op_rw(d->udev, 0xb2, 0, 0,
					obuf, 2, DW210X_WRITE_MSG);
			break;
		}
		}

		break;
	}

	mutex_unlock(&d->i2c_mutex);
	return num;
}

static u32 dw210x_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm dw2102_i2c_algo = {
	.master_xfer = dw2102_i2c_transfer,
	.functionality = dw210x_i2c_func,
};

static struct i2c_algorithm dw2102_serit_i2c_algo = {
	.master_xfer = dw2102_serit_i2c_transfer,
	.functionality = dw210x_i2c_func,
};

static struct i2c_algorithm dw2102_earda_i2c_algo = {
	.master_xfer = dw2102_earda_i2c_transfer,
	.functionality = dw210x_i2c_func,
};

static struct i2c_algorithm dw2104_i2c_algo = {
	.master_xfer = dw2104_i2c_transfer,
	.functionality = dw210x_i2c_func,
};

static int dw210x_read_mac_address(struct dvb_usb_device *d, u8 mac[6])
{
	int i;
	u8 ibuf[] = {0, 0};
	u8 eeprom[256], eepromline[16];

	for (i = 0; i < 256; i++) {
		if (dw210x_op_rw(d->udev, 0xb6, 0xa0 , i, ibuf, 2, DW210X_READ_MSG) < 0) {
			err("read eeprom failed.");
			return -1;
		} else {
			eepromline[i%16] = ibuf[0];
			eeprom[i] = ibuf[0];
		}
		if ((i % 16) == 15) {
			deb_xfer("%02x: ", i - 15);
			debug_dump(eepromline, 16, deb_xfer);
		}
	}
	memcpy(mac, eeprom + 8, 6);
	return 0;
};

static int dw210x_set_voltage(struct dvb_frontend *fe, fe_sec_voltage_t voltage)
{
	static u8 command_13v[1] = {0x00};
	static u8 command_18v[1] = {0x01};
	struct i2c_msg msg[] = {
		{.addr = DW2102_VOLTAGE_CTRL, .flags = 0,
			.buf = command_13v, .len = 1},
	};

	struct dvb_usb_adapter *udev_adap =
		(struct dvb_usb_adapter *)(fe->dvb->priv);
	if (voltage == SEC_VOLTAGE_18)
		msg[0].buf = command_18v;
	i2c_transfer(&udev_adap->dev->i2c_adap, msg, 1);
	return 0;
}

static struct stv0299_config sharp_z0194a_config = {
	.demod_address = 0x68,
	.inittab = sharp_z0194a_inittab,
	.mclk = 88000000UL,
	.invert = 1,
	.skip_reinit = 0,
	.lock_output = STV0299_LOCKOUTPUT_1,
	.volt13_op0_op1 = STV0299_VOLT13_OP1,
	.min_delay_ms = 100,
	.set_symbol_rate = sharp_z0194a_set_symbol_rate,
};

static struct cx24116_config dw2104_config = {
	.demod_address = 0x55,
	.mpg_clk_pos_pol = 0x01,
};

static struct si21xx_config serit_sp1511lhb_config = {
	.demod_address = 0x68,
	.min_delay_ms = 100,

};

static int dw2104_frontend_attach(struct dvb_usb_adapter *d)
{
	if ((d->fe = dvb_attach(cx24116_attach, &dw2104_config,
			&d->dev->i2c_adap)) != NULL) {
		d->fe->ops.set_voltage = dw210x_set_voltage;
		info("Attached cx24116!\n");
		return 0;
	}
	return -EIO;
}

static struct dvb_usb_device_properties dw2102_properties;

static int dw2102_frontend_attach(struct dvb_usb_adapter *d)
{
	if (dw2102_properties.i2c_algo == &dw2102_serit_i2c_algo) {
		/*dw2102_properties.adapter->tuner_attach = NULL;*/
		d->fe = dvb_attach(si21xx_attach, &serit_sp1511lhb_config,
					&d->dev->i2c_adap);
		if (d->fe != NULL) {
			d->fe->ops.set_voltage = dw210x_set_voltage;
			info("Attached si21xx!\n");
			return 0;
		}
	}
	if (dw2102_properties.i2c_algo == &dw2102_earda_i2c_algo) {
		/*dw2102_properties.adapter->tuner_attach = dw2102_tuner_attach;*/
		d->fe = dvb_attach(stv0288_attach, &earda_config,
					&d->dev->i2c_adap);
		if (d->fe != NULL) {
			d->fe->ops.set_voltage = dw210x_set_voltage;
			info("Attached stv0288!\n");
			return 0;
		}
	}

	if (dw2102_properties.i2c_algo == &dw2102_i2c_algo) {
		/*dw2102_properties.adapter->tuner_attach = dw2102_tuner_attach;*/
		d->fe = dvb_attach(stv0299_attach, &sharp_z0194a_config,
					&d->dev->i2c_adap);
		if (d->fe != NULL) {
			d->fe->ops.set_voltage = dw210x_set_voltage;
			info("Attached stv0299!\n");
			return 0;
		}
	}
	return -EIO;
}

static int dw2102_tuner_attach(struct dvb_usb_adapter *adap)
{
	dvb_attach(dvb_pll_attach, adap->fe, 0x60,
		&adap->dev->i2c_adap, DVB_PLL_OPERA1);
	return 0;
}

static int dw2102_earda_tuner_attach(struct dvb_usb_adapter *adap)
{
	dvb_attach(stb6000_attach, adap->fe, 0x61,
		&adap->dev->i2c_adap);

	return 0;
}

static struct dvb_usb_rc_key dw210x_rc_keys[] = {
	{ 0xf8,	0x0a, KEY_Q },		/*power*/
	{ 0xf8,	0x0c, KEY_M },		/*mute*/
	{ 0xf8,	0x11, KEY_1 },
	{ 0xf8,	0x12, KEY_2 },
	{ 0xf8,	0x13, KEY_3 },
	{ 0xf8,	0x14, KEY_4 },
	{ 0xf8,	0x15, KEY_5 },
	{ 0xf8,	0x16, KEY_6 },
	{ 0xf8,	0x17, KEY_7 },
	{ 0xf8,	0x18, KEY_8 },
	{ 0xf8,	0x19, KEY_9 },
	{ 0xf8, 0x10, KEY_0 },
	{ 0xf8, 0x1c, KEY_PAGEUP },	/*ch+*/
	{ 0xf8, 0x0f, KEY_PAGEDOWN },	/*ch-*/
	{ 0xf8, 0x1a, KEY_O },		/*vol+*/
	{ 0xf8, 0x0e, KEY_Z },		/*vol-*/
	{ 0xf8, 0x04, KEY_R },		/*rec*/
	{ 0xf8, 0x09, KEY_D },		/*fav*/
	{ 0xf8, 0x08, KEY_BACKSPACE },	/*rewind*/
	{ 0xf8, 0x07, KEY_A },		/*fast*/
	{ 0xf8, 0x0b, KEY_P },		/*pause*/
	{ 0xf8, 0x02, KEY_ESC },	/*cancel*/
	{ 0xf8, 0x03, KEY_G },		/*tab*/
	{ 0xf8, 0x00, KEY_UP },		/*up*/
	{ 0xf8, 0x1f, KEY_ENTER },	/*ok*/
	{ 0xf8, 0x01, KEY_DOWN },	/*down*/
	{ 0xf8, 0x05, KEY_C },		/*cap*/
	{ 0xf8, 0x06, KEY_S },		/*stop*/
	{ 0xf8, 0x40, KEY_F },		/*full*/
	{ 0xf8, 0x1e, KEY_W },		/*tvmode*/
	{ 0xf8, 0x1b, KEY_B },		/*recall*/

};



static int dw2102_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{
	struct dw210x_state *st = d->priv;
	u8 key[2];
	struct i2c_msg msg = {
		.addr = DW2102_RC_QUERY,
		.flags = I2C_M_RD,
		.buf = key,
		.len = 2
	};
	int i;

	*state = REMOTE_NO_KEY_PRESSED;
	if (dw2102_i2c_transfer(&d->i2c_adap, &msg, 1) == 1) {
		for (i = 0; i < ARRAY_SIZE(dw210x_rc_keys); i++) {
			if (dw210x_rc_keys[i].data == msg.buf[0]) {
				*state = REMOTE_KEY_PRESSED;
				*event = dw210x_rc_keys[i].event;
				st->last_key_pressed =
					dw210x_rc_keys[i].event;
				break;
			}

		st->last_key_pressed = 0;
		}

		if ((*state) == REMOTE_KEY_PRESSED)
			deb_rc("%s: found rc key: %x, %x, event: %x\n",
					__func__, key[0], key[1], (*event));
		else if (key[0] != 0xff)
			deb_rc("%s: unknown rc key: %x, %x\n",
					__func__, key[0], key[1]);

	}

	return 0;
}

static struct usb_device_id dw2102_table[] = {
	{USB_DEVICE(USB_VID_CYPRESS, USB_PID_DW2102)},
	{USB_DEVICE(USB_VID_CYPRESS, 0x2101)},
	{USB_DEVICE(USB_VID_CYPRESS, 0x2104)},
	{USB_DEVICE(0x9022, 0xd650)},
	{USB_DEVICE(USB_VID_TERRATEC, USB_PID_CINERGY_S)},
	{ }
};

MODULE_DEVICE_TABLE(usb, dw2102_table);

static int dw2102_load_firmware(struct usb_device *dev,
			const struct firmware *frmwr)
{
	u8 *b, *p;
	int ret = 0, i;
	u8 reset;
	u8 reset16[] = {0, 0, 0, 0, 0, 0, 0};
	const struct firmware *fw;
	const char *filename = "dvb-usb-dw2101.fw";
	switch (dev->descriptor.idProduct) {
	case 0x2101:
		ret = request_firmware(&fw, filename, &dev->dev);
		if (ret != 0) {
			err("did not find the firmware file. (%s) "
			"Please see linux/Documentation/dvb/ for more details "
			"on firmware-problems.", filename);
			return ret;
		}
		break;
	default:
		fw = frmwr;
		break;
	}
	info("start downloading DW210X firmware");
	p = kmalloc(fw->size, GFP_KERNEL);
	reset = 1;
	/*stop the CPU*/
	dw210x_op_rw(dev, 0xa0, 0x7f92, 0, &reset, 1, DW210X_WRITE_MSG);
	dw210x_op_rw(dev, 0xa0, 0xe600, 0, &reset, 1, DW210X_WRITE_MSG);

	if (p != NULL) {
		memcpy(p, fw->data, fw->size);
		for (i = 0; i < fw->size; i += 0x40) {
			b = (u8 *) p + i;
			if (dw210x_op_rw(dev, 0xa0, i, 0, b , 0x40,
					DW210X_WRITE_MSG) != 0x40) {
				err("error while transferring firmware");
				ret = -EINVAL;
				break;
			}
		}
		/* restart the CPU */
		reset = 0;
		if (ret || dw210x_op_rw(dev, 0xa0, 0x7f92, 0, &reset, 1,
					DW210X_WRITE_MSG) != 1) {
			err("could not restart the USB controller CPU.");
			ret = -EINVAL;
		}
		if (ret || dw210x_op_rw(dev, 0xa0, 0xe600, 0, &reset, 1,
					DW210X_WRITE_MSG) != 1) {
			err("could not restart the USB controller CPU.");
			ret = -EINVAL;
		}
		/* init registers */
		switch (dev->descriptor.idProduct) {
		case USB_PID_DW2104:
		case 0xd650:
			reset = 1;
			dw210x_op_rw(dev, 0xc4, 0x0000, 0, &reset, 1,
					DW210X_WRITE_MSG);
			reset = 0;
			dw210x_op_rw(dev, 0xbf, 0x0040, 0, &reset, 0,
					DW210X_WRITE_MSG);
			break;
		case USB_PID_CINERGY_S:
		case USB_PID_DW2102:
			dw210x_op_rw(dev, 0xbf, 0x0040, 0, &reset, 0,
					DW210X_WRITE_MSG);
			dw210x_op_rw(dev, 0xb9, 0x0000, 0, &reset16[0], 2,
					DW210X_READ_MSG);
			/* check STV0299 frontend  */
			dw210x_op_rw(dev, 0xb5, 0, 0, &reset16[0], 2,
					DW210X_READ_MSG);
			if ((reset16[0] == 0xa1) || (reset16[0] == 0x80)) {
				dw2102_properties.i2c_algo = &dw2102_i2c_algo;
				dw2102_properties.adapter->tuner_attach = &dw2102_tuner_attach;
				break;
			} else {
				/* check STV0288 frontend  */
				reset16[0] = 0xd0;
				reset16[1] = 1;
				reset16[2] = 0;
				dw210x_op_rw(dev, 0xc2, 0, 0, &reset16[0], 3,
						DW210X_WRITE_MSG);
				dw210x_op_rw(dev, 0xc3, 0xd1, 0, &reset16[0], 3,
						DW210X_READ_MSG);
				if (reset16[2] == 0x11) {
					dw2102_properties.i2c_algo = &dw2102_earda_i2c_algo;
					dw2102_properties.adapter->tuner_attach = &dw2102_earda_tuner_attach;
					break;
				}
			}
		case 0x2101:
			dw210x_op_rw(dev, 0xbc, 0x0030, 0, &reset16[0], 2,
					DW210X_READ_MSG);
			dw210x_op_rw(dev, 0xba, 0x0000, 0, &reset16[0], 7,
					DW210X_READ_MSG);
			dw210x_op_rw(dev, 0xba, 0x0000, 0, &reset16[0], 7,
					DW210X_READ_MSG);
			dw210x_op_rw(dev, 0xb9, 0x0000, 0, &reset16[0], 2,
					DW210X_READ_MSG);
			break;
		}
		msleep(100);
		kfree(p);
	}
	return ret;
}

static struct dvb_usb_device_properties dw2102_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,
	.usb_ctrl = DEVICE_SPECIFIC,
	.firmware = "dvb-usb-dw2102.fw",
	.size_of_priv = sizeof(struct dw210x_state),
	.no_reconnect = 1,

	.i2c_algo = &dw2102_serit_i2c_algo,
	.rc_key_map = dw210x_rc_keys,
	.rc_key_map_size = ARRAY_SIZE(dw210x_rc_keys),
	.rc_interval = 150,
	.rc_query = dw2102_rc_query,

	.generic_bulk_ctrl_endpoint = 0x81,
	/* parameter for the MPEG2-data transfer */
	.num_adapters = 1,
	.download_firmware = dw2102_load_firmware,
	.read_mac_address = dw210x_read_mac_address,
		.adapter = {
		{
			.frontend_attach = dw2102_frontend_attach,
			.streaming_ctrl = NULL,
			.tuner_attach = NULL,
			.stream = {
				.type = USB_BULK,
				.count = 8,
				.endpoint = 0x82,
				.u = {
					.bulk = {
						.buffersize = 4096,
					}
				}
			},
		}
	},
	.num_device_descs = 3,
	.devices = {
		{"DVBWorld DVB-S 2102 USB2.0",
			{&dw2102_table[0], NULL},
			{NULL},
		},
		{"DVBWorld DVB-S 2101 USB2.0",
			{&dw2102_table[1], NULL},
			{NULL},
		},
		{"TerraTec Cinergy S USB",
			{&dw2102_table[4], NULL},
			{NULL},
		},
	}
};

static struct dvb_usb_device_properties dw2104_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,
	.usb_ctrl = DEVICE_SPECIFIC,
	.firmware = "dvb-usb-dw2104.fw",
	.size_of_priv = sizeof(struct dw210x_state),
	.no_reconnect = 1,

	.i2c_algo = &dw2104_i2c_algo,
	.rc_key_map = dw210x_rc_keys,
	.rc_key_map_size = ARRAY_SIZE(dw210x_rc_keys),
	.rc_interval = 150,
	.rc_query = dw2102_rc_query,

	.generic_bulk_ctrl_endpoint = 0x81,
	/* parameter for the MPEG2-data transfer */
	.num_adapters = 1,
	.download_firmware = dw2102_load_firmware,
	.read_mac_address = dw210x_read_mac_address,
	.adapter = {
		{
			.frontend_attach = dw2104_frontend_attach,
			.streaming_ctrl = NULL,
			/*.tuner_attach = dw2104_tuner_attach,*/
			.stream = {
				.type = USB_BULK,
				.count = 8,
				.endpoint = 0x82,
				.u = {
					.bulk = {
						.buffersize = 4096,
					}
				}
			},
		}
	},
	.num_device_descs = 2,
	.devices = {
		{ "DVBWorld DW2104 USB2.0",
			{&dw2102_table[2], NULL},
			{NULL},
		},
		{ "TeVii S650 USB2.0",
			{&dw2102_table[3], NULL},
			{NULL},
		},
	}
};

static int dw2102_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	if (0 == dvb_usb_device_init(intf, &dw2102_properties,
			THIS_MODULE, NULL, adapter_nr) ||
	    0 == dvb_usb_device_init(intf, &dw2104_properties,
			THIS_MODULE, NULL, adapter_nr)) {
		return 0;
	}
	return -ENODEV;
}

static struct usb_driver dw2102_driver = {
	.name = "dw2102",
	.probe = dw2102_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table = dw2102_table,
};

static int __init dw2102_module_init(void)
{
	int ret =  usb_register(&dw2102_driver);
	if (ret)
		err("usb_register failed. Error number %d", ret);

	return ret;
}

static void __exit dw2102_module_exit(void)
{
	usb_deregister(&dw2102_driver);
}

module_init(dw2102_module_init);
module_exit(dw2102_module_exit);

MODULE_AUTHOR("Igor M. Liplianin (c) liplianin@me.by");
MODULE_DESCRIPTION("Driver for DVBWorld DVB-S 2101, 2102, DVB-S2 2104 USB2.0 device");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");

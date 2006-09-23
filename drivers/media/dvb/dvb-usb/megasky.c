/* DVB USB compliant linux driver for MSI Mega Sky 580 DVB-T USB2.0 receiver
 *
 * Copyright (C) 2006 Aapo Tahkola (aet@rasterburn.org)
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include "megasky.h"

#include "mt352.h"
#include "mt352_priv.h"

/* debug */
int dvb_usb_megasky_debug;
module_param_named(debug,dvb_usb_megasky_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=rc (or-able))." DVB_USB_DEBUG_STATUS);

static struct dvb_usb_rc_key megasky_rc_keys [] = {
	{ 0x0, 0x12, KEY_POWER },
	{ 0x0, 0x1e, KEY_CYCLEWINDOWS }, /* min/max */
	{ 0x0, 0x02, KEY_CHANNELUP },
	{ 0x0, 0x05, KEY_CHANNELDOWN },
	{ 0x0, 0x03, KEY_VOLUMEUP },
	{ 0x0, 0x06, KEY_VOLUMEDOWN },
	{ 0x0, 0x04, KEY_MUTE },
	{ 0x0, 0x07, KEY_OK }, /* TS */
	{ 0x0, 0x08, KEY_STOP },
	{ 0x0, 0x09, KEY_MENU }, /* swap */
	{ 0x0, 0x0a, KEY_REWIND },
	{ 0x0, 0x1b, KEY_PAUSE },
	{ 0x0, 0x1f, KEY_FASTFORWARD },
	{ 0x0, 0x0c, KEY_RECORD },
	{ 0x0, 0x0d, KEY_CAMERA }, /* screenshot */
	{ 0x0, 0x0e, KEY_COFFEE }, /* "MTS" */
};

static inline int m9206_read(struct usb_device *udev, u8 request, u16 value, u16 index, void *data, int size)
{
	int ret;

	ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			      request, USB_TYPE_VENDOR | USB_DIR_IN,
			      value, index, data, size, 2000);
	if (ret < 0)
		return ret;

	if (ret != size)
		return -EIO;

	return 0;
}

static inline int m9206_write(struct usb_device *udev, u8 request, u16 value, u16 index)
{
	int ret;

	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			      request, USB_TYPE_VENDOR | USB_DIR_OUT,
			      value, index, NULL, 0, 2000);
	msleep(3);

	return ret;
}

static int m9206_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{
	int i, ret = 0;
	u8 rc_state[2];

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	if ((ret = m9206_read(d->udev, 0x22, 0x0, 0xff51, rc_state, 1)) != 0)
		goto unlock;

	if ((ret = m9206_read(d->udev, 0x22, 0x0, 0xff52, rc_state + 1, 1)) != 0)
		goto unlock;

	for (i = 0; i < ARRAY_SIZE(megasky_rc_keys); i++)
		if (megasky_rc_keys[i].data == rc_state[1]) {
			*event = megasky_rc_keys[i].event;

			switch(rc_state[0]) {
			case 0x80:
				*state = REMOTE_NO_KEY_PRESSED;
				goto unlock;

			case 0x93:
			case 0x92:
				*state = REMOTE_KEY_PRESSED;
				goto unlock;

			case 0x91:
				*state = REMOTE_KEY_REPEAT;
				goto unlock;

			default:
				deb_rc("Unexpected rc response %x\n", rc_state[0]);
				*state = REMOTE_NO_KEY_PRESSED;
				goto unlock;
			}
		}

	if (rc_state[1] != 0)
		deb_rc("Unknown rc key %x\n", rc_state[1]);

	*state = REMOTE_NO_KEY_PRESSED;

	unlock:
	mutex_unlock(&d->i2c_mutex);

	return ret;
}

/* I2C */

static int m9206_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msg[], int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int i;
	int ret = 0;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	if (num > 2)
		return -EINVAL;

	for (i = 0; i < num; i++) {
		u8 w_len;

		if ((ret = m9206_write(d->udev, 0x23, msg[i].addr, 0x80)) != 0)
			goto unlock;

		if ((ret = m9206_write(d->udev, 0x23, msg[i].buf[0], 0x0)) != 0)
			goto unlock;

		if (i + 1 < num && msg[i + 1].flags & I2C_M_RD) {
			if (msg[i].addr == 0x1e)
				w_len = 0x1f;
			else
				w_len = 0xc5;

			if ((ret = m9206_write(d->udev, 0x23, w_len, 0x80)) != 0)
				goto unlock;

			if ((ret = m9206_read(d->udev, 0x23, 0x0, 0x60, msg[i + 1].buf, msg[i + 1].len)) != 0)
				goto unlock;

			i++;
		} else {
			if (msg[i].len != 2)
				return -EINVAL;

			if ((ret = m9206_write(d->udev, 0x23, msg[i].buf[1], 0x40)) != 0)
				goto unlock;
		}
	}
	ret = i;
	unlock:
	mutex_unlock(&d->i2c_mutex);

	return ret;
}

static u32 m9206_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm m9206_i2c_algo = {
	.master_xfer   = m9206_i2c_xfer,
	.functionality = m9206_i2c_func,
};

/* Callbacks for DVB USB */
static int megasky_identify_state (struct usb_device *udev,
				   struct dvb_usb_properties *props,
				   struct dvb_usb_device_description **desc,
				   int *cold)
{
	struct usb_host_interface *alt;

	alt = usb_altnum_to_altsetting(usb_ifnum_to_if(udev, 0), 1);
	*cold = (alt == NULL) ? 1 : 0;

	return 0;
}

static int megasky_mt352_demod_init(struct dvb_frontend *fe)
{
	int i;
	static u8 buf1[] = {
	CONFIG, 0x3d,
	CLOCK_CTL, 0x30,
	RESET, 0x80,
	ADC_CTL_1, 0x40,
	AGC_TARGET, 0x1c,
	AGC_CTL, 0x20,
	0x69, 0x00,
	0x6a, 0xff,
	0x6b, 0xff,
	0x6c, 0x40,
	0x6d, 0xff,
	0x6e, 0x00,
	0x6f, 0x40,
	0x70, 0x40,
	0x93, 0x1a,
	0xb5, 0x7a,
	ACQ_CTL, 0x50,
	INPUT_FREQ_1, 0x31,
	INPUT_FREQ_0, 0x05,
	};

	for (i = 0; i < ARRAY_SIZE(buf1); i += 2)
		mt352_write(fe, &buf1[i], 2);

	deb_rc("Demod init!\n");

	return 0;
}

struct mt352_state;


#define W 0
#define R 1
/* Not actual hw limits. */
#define QT1010_MIN_STEP 2000000
#define QT1010_MIN_FREQ 48000000

int qt1010_set_params(struct dvb_frontend *fe, struct dvb_frontend_parameters *params, u8 *buf, int buf_len)
{
	int i;
	int div, mod;
	struct {
		u8 read, reg, value;
	} rd[46] = {	{ W, 0x01, 0x80 },
			{ W, 0x02, 0x3f },
			{ W, 0x05, 0xff }, /* c */
			{ W, 0x06, 0x44 },
			{ W, 0x07, 0xff }, /* c */
			{ W, 0x08, 0x08 },
			{ W, 0x09, 0xff }, /* c */
			{ W, 0x0a, 0xff }, /* c */
			{ W, 0x0b, 0xff }, /* c */
			{ W, 0x0c, 0xe1 },
			{ W, 0x1a, 0xff }, /* 10 c */
			{ W, 0x1b, 0x00 },
			{ W, 0x1c, 0x89 },
			{ W, 0x11, 0xff }, /* c */
			{ W, 0x12, 0x91 },
			{ W, 0x22, 0xff }, /* c */
			{ W, 0x1e, 0x00 },
			{ W, 0x1e, 0xd0 },
			{ R, 0x22, 0xff }, /* c read */
			{ W, 0x1e, 0x00 },
			{ R, 0x05, 0xff }, /* 20 c read */
			{ R, 0x22, 0xff }, /* c read */
			{ W, 0x23, 0xd0 },
			{ W, 0x1e, 0x00 },
			{ W, 0x1e, 0xe0 },
			{ R, 0x23, 0xff }, /* c read */
			{ W, 0x1e, 0x00 },
			{ W, 0x24, 0xd0 },
			{ W, 0x1e, 0x00 },
			{ W, 0x1e, 0xf0 },
			{ R, 0x24, 0xff }, /* 30 c read */
			{ W, 0x1e, 0x00 },
			{ W, 0x14, 0x7f },
			{ W, 0x15, 0x7f },
			{ W, 0x05, 0xff }, /* c */
			{ W, 0x06, 0x00 },
			{ W, 0x15, 0x1f },
			{ W, 0x16, 0xff },
			{ W, 0x18, 0xff },
			{ W, 0x1f, 0xff }, /* c */
			{ W, 0x20, 0xff }, /* 40 c */
			{ W, 0x21, 0x53 },
			{ W, 0x25, 0xbd },
			{ W, 0x26, 0x15 },
			{ W, 0x02, 0x00 },
			{ W, 0x01, 0x00 },
			};
	struct i2c_msg msg;
	struct dvb_usb_device *d = fe->dvb->priv;
	unsigned long freq = params->frequency;

	if (freq % QT1010_MIN_STEP)
		deb_rc("frequency not supported.\n");

	(void) buf;
	(void) buf_len;

	div = (freq - QT1010_MIN_FREQ) / QT1010_MIN_STEP;
	mod = (div + 16 - 9) % 16;

	/* 0x5 */
	if (div >= 377)
		rd[2].value = 0x74;
	else if (div >=  265)
		rd[2].value = 0x54;
	else if (div >=  121)
		rd[2].value = 0x34;
	else
		rd[2].value = 0x14;

	/* 0x7 */
	rd[4].value = (((freq - QT1010_MIN_FREQ) / 1000000) * 9975 + 12960000) / 320000;

	/* 09 */
	if (mod < 4)
		rd[6].value = 0x1d;
	else
		rd[6].value = 0x1c;

	/* 0a */
	if (mod < 2)
		rd[7].value = 0x09;
	else if (mod < 4)
		rd[7].value = 0x08;
	else if (mod < 6)
		rd[7].value = 0x0f;
	else if (mod < 8)
		rd[7].value = 0x0e;
	else if (mod < 10)
		rd[7].value = 0x0d;
	else if (mod < 12)
		rd[7].value = 0x0c;
	else if (mod < 14)
		rd[7].value = 0x0b;
	else
		rd[7].value = 0x0a;

	/* 0b */
	if (div & 1)
		rd[8].value = 0x45;
	else
		rd[8].value = 0x44;

	/* 1a */
	if (div & 1)
		rd[10].value = 0x78;
	else
		rd[10].value = 0xf8;

	/* 11 */
	if (div >= 265)
		rd[13].value = 0xf9;
	else if (div >=  121)
		rd[13].value = 0xfd;
	else
		rd[13].value = 0xf9;

	/* 22 */
	if (div < 201)
		rd[15].value = 0xd0;
	else if (div < 217)
		rd[15].value = 0xd3;
	else if (div < 233)
		rd[15].value = 0xd6;
	else if (div < 249)
		rd[15].value = 0xd9;
	else if (div < 265)
		rd[15].value = 0xda;
	else
		rd[15].value = 0xd0;

	/* 05 */
	if (div >= 377)
		rd[34].value = 0x70;
	else if (div >=  265)
		rd[34].value = 0x50;
	else if (div >=  121)
		rd[34].value = 0x30;
	else
		rd[34].value = 0x10;

	/* 1f */
	if (mod < 4)
		rd[39].value = 0x64;
	else if (mod < 6)
		rd[39].value = 0x66;
	else if (mod < 8)
		rd[39].value = 0x67;
	else if (mod < 12)
		rd[39].value = 0x68;
	else if (mod < 14)
		rd[39].value = 0x69;
	else
		rd[39].value = 0x6a;

	/* 20 */
	if (mod < 4)
		rd[40].value = 0x10;
	else if (mod < 6)
		rd[40].value = 0x11;
	else if (mod < 10)
		rd[40].value = 0x12;
	else if (mod < 12)
		rd[40].value = 0x13;
	else if (mod < 14)
		rd[40].value = 0x14;
	else
		rd[40].value = 0x15;

	deb_rc("Now tuning... ");
	for (i = 0; i < sizeof(rd) / sizeof(*rd); i++) {
		if (rd[i].read)
			continue;

		msg.flags = 0;
		msg.len = 2;
		msg.addr = 0xc4;
		msg.buf = &rd[i].reg;

		if (i2c_transfer(&d->i2c_adap, &msg, 1) != 1) {
			deb_rc("tuner write failed\n");
			return -EIO;
		}
	}
	deb_rc("done\n");

	return 0;
}
#undef W
#undef R

static struct mt352_config megasky_mt352_config = {
	.demod_address = 0x1e,
	.demod_init = megasky_mt352_demod_init,
};

static int megasky_frontend_attach(struct dvb_usb_device *d)
{
	deb_rc("megasky_frontend_attach!\n");

	if ((d->fe = mt352_attach(&megasky_mt352_config, &d->i2c_adap)) != NULL) {
		d->fe->ops.tuner_ops.calc_regs = qt1010_set_params;
		return 0;
	}
	return -EIO;
}

/* DVB USB Driver stuff */
static struct dvb_usb_properties megasky_properties;

static int megasky_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct dvb_usb_device *d;
	struct usb_host_interface *alt;
	int ret;

	if ((ret = dvb_usb_device_init(intf, &megasky_properties, THIS_MODULE, &d)) == 0) {
		deb_rc("probed!\n");

		alt = usb_altnum_to_altsetting(intf, 1);
		if (alt == NULL) {
			deb_rc("not alt found!\n");
			return -ENODEV;
		}

		ret = usb_set_interface(d->udev, alt->desc.bInterfaceNumber, alt->desc.bAlternateSetting);
		if (ret < 0)
			return ret;

		deb_rc("Changed to alternate setting!\n");

		/* Remote controller init. */
		if ((ret = m9206_write(d->udev, 0x22, 0xa8, 0xff55)) != 0)
			return ret;

		if ((ret = m9206_write(d->udev, 0x22, 0x51, 0xff54)) != 0)
			return ret;
	}
	return ret;
}

static struct usb_device_id megasky_table [] = {
		{ USB_DEVICE(USB_VID_MSI, USB_PID_MSI_MEGASKY580) },
		{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, megasky_table);

static int set_filter(struct dvb_usb_device *d, int type, int idx, int pid)
{
	int ret = 0;

	if (pid >= 0x8000)
		return -EINVAL;

	pid |= 0x8000;

	if ((ret = m9206_write(d->udev, 0x25, pid, (type << 8) | (idx * 4) )) != 0)
		return ret;

	if ((ret = m9206_write(d->udev, 0x25, 0, (type << 8) | (idx * 4) )) != 0)
		return ret;

	return ret;
}

static int m9206_pid_filter_ctrl(struct dvb_usb_device *d, int onoff)
{
	int ret = 0;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	deb_rc("filtering %s\n", onoff ? "on" : "off");
	if (onoff == 0) {
		if ((ret = set_filter(d, 0x81, 1, 0x00)) != 0)
			goto unlock;

		if ((ret = set_filter(d, 0x82, 0, 0x02f5)) != 0)
			goto unlock;
	}
	unlock:
	mutex_unlock(&d->i2c_mutex);

	return ret;
}

static int m9206_pid_filter(struct dvb_usb_device *d, int index, u16 pid, int onoff)
{
	int ret = 0;

	if (pid == 8192)
		return m9206_pid_filter_ctrl(d, !onoff);

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	deb_rc("filter %d, pid %x, %s\n", index, pid, onoff ? "on" : "off");
	if (onoff == 0)
		pid = 0;

	if ((ret = set_filter(d, 0x81, 1, 0x01)) != 0)
		goto unlock;

	if ((ret = set_filter(d, 0x81, index + 2, pid)) != 0)
		goto unlock;

	if ((ret = set_filter(d, 0x82, 0, 0x02f5)) != 0)
		goto unlock;

	unlock:
	mutex_unlock(&d->i2c_mutex);

	return ret;
}

static int m9206_firmware_download(struct usb_device *udev, const struct firmware *fw)
{
	u16 value, index, size;
	u8 read[4], *buff;
	int i, pass, ret = 0;

	buff = kmalloc(65536, GFP_KERNEL);

	if ((ret = m9206_read(udev, 0x25, 0x0, 0x8000, read, 4)) != 0)
		goto done;
	deb_rc("%x %x %x %x\n", read[0], read[1], read[2], read[3]);

	if ((ret = m9206_read(udev, 0x30, 0x0, 0x0, read, 1)) != 0)
		goto done;
	deb_rc("%x\n", read[0]);

	for (pass = 0; pass < 2; pass++) {
		for (i = 0; i + (sizeof(u16) * 3) < fw->size;) {
			value = le16_to_cpu(*(u16 *)(fw->data + i));
			i += sizeof(u16);

			index = le16_to_cpu(*(u16 *)(fw->data + i));
			i += sizeof(u16);

			size = le16_to_cpu(*(u16 *)(fw->data + i));
			i += sizeof(u16);

			if (pass == 1) {
				/* Will stall if using fw->data ... */
				memcpy(buff, fw->data + i, size);

				ret = usb_control_msg(udev, usb_sndctrlpipe(udev,0),
					    0x30, USB_TYPE_VENDOR | USB_DIR_OUT,
					    value, index, buff, size, 20);
				if (ret != size) {
					deb_rc("error while uploading fw!\n");
					ret = -EIO;
					goto done;
				}
				msleep(3);
			}
			i += size;
		}
		if (i != fw->size) {
			ret = -EINVAL;
			goto done;
		}
	}

	msleep(36);

	/* m9206 will disconnect itself from the bus after this. */
	(void) m9206_write(udev, 0x22, 0x01, 0xff69);
	deb_rc("firmware uploaded!\n");

	done:
	kfree(buff);

	return ret;
}

static struct dvb_usb_properties megasky_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER | DVB_USB_HAS_PID_FILTER |
		DVB_USB_PID_FILTER_CAN_BE_TURNED_OFF | DVB_USB_NEED_PID_FILTERING,
	.pid_filter_count = 8,

	.usb_ctrl = DEVICE_SPECIFIC,
	.firmware = "dvb-usb-megasky-02.fw",
	.download_firmware = m9206_firmware_download,

	.pid_filter       = m9206_pid_filter,
	.pid_filter_ctrl  = m9206_pid_filter_ctrl,
	.frontend_attach  = megasky_frontend_attach,

	.rc_interval      = 200,
	.rc_key_map       = megasky_rc_keys,
	.rc_key_map_size  = ARRAY_SIZE(megasky_rc_keys),
	.rc_query         = m9206_rc_query,

	.size_of_priv     = 0,

	.identify_state   = megasky_identify_state,
	.i2c_algo         = &m9206_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,
	.urb = {
		.type = DVB_USB_BULK,
		.count = 8,
		.endpoint = 0x81,
		.u = {
			.bulk = {
				.buffersize = 512,
			}
		}
	},
	.num_device_descs = 1,
	.devices = {
		{   "MSI Mega Sky 580 DVB-T USB2.0",
			{ &megasky_table[0], NULL },
			{ NULL },
		},
		{ NULL },
	}
};

static struct usb_driver megasky_driver = {
	.name		= "dvb_usb_megasky",
	.probe		= megasky_probe,
	.disconnect	= dvb_usb_device_exit,
	.id_table	= megasky_table,
};

/* module stuff */
static int __init megasky_module_init(void)
{
	int ret;

	if ((ret = usb_register(&megasky_driver))) {
		err("usb_register failed. Error number %d", ret);
		return ret;
	}

	return 0;
}

static void __exit megasky_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&megasky_driver);
}

module_init (megasky_module_init);
module_exit (megasky_module_exit);

MODULE_AUTHOR("Aapo Tahkola <aet@rasterburn.org>");
MODULE_DESCRIPTION("Driver for MSI Mega Sky 580 DVB-T USB2.0");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");

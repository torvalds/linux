/* DVB USB compliant linux driver for MSI Mega Sky 580 DVB-T USB2.0 receiver
 *
 * Copyright (C) 2006 Aapo Tahkola (aet@rasterburn.org)
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */

#include "m920x.h"

#include "mt352.h"
#include "mt352_priv.h"
#include "qt1010.h"
#include "tda1004x.h"
#include "tda827x.h"
#include <asm/unaligned.h>

/* debug */
static int dvb_usb_m920x_debug;
module_param_named(debug,dvb_usb_m920x_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=rc (or-able))." DVB_USB_DEBUG_STATUS);

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static int m920x_set_filter(struct dvb_usb_device *d, int type, int idx, int pid);

static inline int m920x_read(struct usb_device *udev, u8 request, u16 value,
			     u16 index, void *data, int size)
{
	int ret;

	ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
			      request, USB_TYPE_VENDOR | USB_DIR_IN,
			      value, index, data, size, 2000);
	if (ret < 0) {
		printk(KERN_INFO "m920x_read = error: %d\n", ret);
		return ret;
	}

	if (ret != size) {
		deb("m920x_read = no data\n");
		return -EIO;
	}

	return 0;
}

static inline int m920x_write(struct usb_device *udev, u8 request,
			      u16 value, u16 index)
{
	int ret;

	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			      request, USB_TYPE_VENDOR | USB_DIR_OUT,
			      value, index, NULL, 0, 2000);

	return ret;
}

static int m920x_init(struct dvb_usb_device *d, struct m920x_inits *rc_seq)
{
	int ret = 0, i, epi, flags = 0;
	int adap_enabled[M9206_MAX_ADAPTERS] = { 0 };

	/* Remote controller init. */
	if (d->props.rc_query) {
		deb("Initialising remote control\n");
		while (rc_seq->address) {
			if ((ret = m920x_write(d->udev, M9206_CORE,
					       rc_seq->data,
					       rc_seq->address)) != 0) {
				deb("Initialising remote control failed\n");
				return ret;
			}

			rc_seq++;
		}

		deb("Initialising remote control success\n");
	}

	for (i = 0; i < d->props.num_adapters; i++)
		flags |= d->adapter[i].props.caps;

	/* Some devices(Dposh) might crash if we attempt touch at all. */
	if (flags & DVB_USB_ADAP_HAS_PID_FILTER) {
		for (i = 0; i < d->props.num_adapters; i++) {
			epi = d->adapter[i].props.stream.endpoint - 0x81;

			if (epi < 0 || epi >= M9206_MAX_ADAPTERS) {
				printk(KERN_INFO "m920x: Unexpected adapter endpoint!\n");
				return -EINVAL;
			}

			adap_enabled[epi] = 1;
		}

		for (i = 0; i < M9206_MAX_ADAPTERS; i++) {
			if (adap_enabled[i])
				continue;

			if ((ret = m920x_set_filter(d, 0x81 + i, 0, 0x0)) != 0)
				return ret;

			if ((ret = m920x_set_filter(d, 0x81 + i, 0, 0x02f5)) != 0)
				return ret;
		}
	}

	return ret;
}

static int m920x_init_ep(struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_host_interface *alt;

	if ((alt = usb_altnum_to_altsetting(intf, 1)) == NULL) {
		deb("No alt found!\n");
		return -ENODEV;
	}

	return usb_set_interface(udev, alt->desc.bInterfaceNumber,
				 alt->desc.bAlternateSetting);
}

static int m920x_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{
	struct m920x_state *m = d->priv;
	int i, ret = 0;
	u8 rc_state[2];

	if ((ret = m920x_read(d->udev, M9206_CORE, 0x0, M9206_RC_STATE, rc_state, 1)) != 0)
		goto unlock;

	if ((ret = m920x_read(d->udev, M9206_CORE, 0x0, M9206_RC_KEY, rc_state + 1, 1)) != 0)
		goto unlock;

	for (i = 0; i < d->props.rc_key_map_size; i++)
		if (rc5_data(&d->props.rc_key_map[i]) == rc_state[1]) {
			*event = d->props.rc_key_map[i].event;

			switch(rc_state[0]) {
			case 0x80:
				*state = REMOTE_NO_KEY_PRESSED;
				goto unlock;

			case 0x88: /* framing error or "invalid code" */
			case 0x99:
			case 0xc0:
			case 0xd8:
				*state = REMOTE_NO_KEY_PRESSED;
				m->rep_count = 0;
				goto unlock;

			case 0x93:
			case 0x92:
				m->rep_count = 0;
				*state = REMOTE_KEY_PRESSED;
				goto unlock;

			case 0x91:
				/* prevent immediate auto-repeat */
				if (++m->rep_count > 2)
					*state = REMOTE_KEY_REPEAT;
				else
					*state = REMOTE_NO_KEY_PRESSED;
				goto unlock;

			default:
				deb("Unexpected rc state %02x\n", rc_state[0]);
				*state = REMOTE_NO_KEY_PRESSED;
				goto unlock;
			}
		}

	if (rc_state[1] != 0)
		deb("Unknown rc key %02x\n", rc_state[1]);

	*state = REMOTE_NO_KEY_PRESSED;

 unlock:

	return ret;
}

/* I2C */
static int m920x_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msg[], int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int i, j;
	int ret = 0;

	if (!num)
		return -EINVAL;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	for (i = 0; i < num; i++) {
		if (msg[i].flags & (I2C_M_NO_RD_ACK | I2C_M_IGNORE_NAK | I2C_M_TEN) || msg[i].len == 0) {
			/* For a 0 byte message, I think sending the address
			 * to index 0x80|0x40 would be the correct thing to
			 * do.  However, zero byte messages are only used for
			 * probing, and since we don't know how to get the
			 * slave's ack, we can't probe. */
			ret = -ENOTSUPP;
			goto unlock;
		}
		/* Send START & address/RW bit */
		if (!(msg[i].flags & I2C_M_NOSTART)) {
			if ((ret = m920x_write(d->udev, M9206_I2C,
					(msg[i].addr << 1) |
					(msg[i].flags & I2C_M_RD ? 0x01 : 0), 0x80)) != 0)
				goto unlock;
			/* Should check for ack here, if we knew how. */
		}
		if (msg[i].flags & I2C_M_RD) {
			for (j = 0; j < msg[i].len; j++) {
				/* Last byte of transaction?
				 * Send STOP, otherwise send ACK. */
				int stop = (i+1 == num && j+1 == msg[i].len) ? 0x40 : 0x01;

				if ((ret = m920x_read(d->udev, M9206_I2C, 0x0,
						      0x20 | stop,
						      &msg[i].buf[j], 1)) != 0)
					goto unlock;
			}
		} else {
			for (j = 0; j < msg[i].len; j++) {
				/* Last byte of transaction? Then send STOP. */
				int stop = (i+1 == num && j+1 == msg[i].len) ? 0x40 : 0x00;

				if ((ret = m920x_write(d->udev, M9206_I2C, msg[i].buf[j], stop)) != 0)
					goto unlock;
				/* Should check for ack here too. */
			}
		}
	}
	ret = num;

 unlock:
	mutex_unlock(&d->i2c_mutex);

	return ret;
}

static u32 m920x_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm m920x_i2c_algo = {
	.master_xfer   = m920x_i2c_xfer,
	.functionality = m920x_i2c_func,
};

/* pid filter */
static int m920x_set_filter(struct dvb_usb_device *d, int type, int idx, int pid)
{
	int ret = 0;

	if (pid >= 0x8000)
		return -EINVAL;

	pid |= 0x8000;

	if ((ret = m920x_write(d->udev, M9206_FILTER, pid, (type << 8) | (idx * 4) )) != 0)
		return ret;

	if ((ret = m920x_write(d->udev, M9206_FILTER, 0, (type << 8) | (idx * 4) )) != 0)
		return ret;

	return ret;
}

static int m920x_update_filters(struct dvb_usb_adapter *adap)
{
	struct m920x_state *m = adap->dev->priv;
	int enabled = m->filtering_enabled[adap->id];
	int i, ret = 0, filter = 0;
	int ep = adap->props.stream.endpoint;

	for (i = 0; i < M9206_MAX_FILTERS; i++)
		if (m->filters[adap->id][i] == 8192)
			enabled = 0;

	/* Disable all filters */
	if ((ret = m920x_set_filter(adap->dev, ep, 1, enabled)) != 0)
		return ret;

	for (i = 0; i < M9206_MAX_FILTERS; i++)
		if ((ret = m920x_set_filter(adap->dev, ep, i + 2, 0)) != 0)
			return ret;

	/* Set */
	if (enabled) {
		for (i = 0; i < M9206_MAX_FILTERS; i++) {
			if (m->filters[adap->id][i] == 0)
				continue;

			if ((ret = m920x_set_filter(adap->dev, ep, filter + 2, m->filters[adap->id][i])) != 0)
				return ret;

			filter++;
		}
	}

	return ret;
}

static int m920x_pid_filter_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	struct m920x_state *m = adap->dev->priv;

	m->filtering_enabled[adap->id] = onoff ? 1 : 0;

	return m920x_update_filters(adap);
}

static int m920x_pid_filter(struct dvb_usb_adapter *adap, int index, u16 pid, int onoff)
{
	struct m920x_state *m = adap->dev->priv;

	m->filters[adap->id][index] = onoff ? pid : 0;

	return m920x_update_filters(adap);
}

static int m920x_firmware_download(struct usb_device *udev, const struct firmware *fw)
{
	u16 value, index, size;
	u8 read[4], *buff;
	int i, pass, ret = 0;

	buff = kmalloc(65536, GFP_KERNEL);
	if (buff == NULL)
		return -ENOMEM;

	if ((ret = m920x_read(udev, M9206_FILTER, 0x0, 0x8000, read, 4)) != 0)
		goto done;
	deb("%x %x %x %x\n", read[0], read[1], read[2], read[3]);

	if ((ret = m920x_read(udev, M9206_FW, 0x0, 0x0, read, 1)) != 0)
		goto done;
	deb("%x\n", read[0]);

	for (pass = 0; pass < 2; pass++) {
		for (i = 0; i + (sizeof(u16) * 3) < fw->size;) {
			value = get_unaligned_le16(fw->data + i);
			i += sizeof(u16);

			index = get_unaligned_le16(fw->data + i);
			i += sizeof(u16);

			size = get_unaligned_le16(fw->data + i);
			i += sizeof(u16);

			if (pass == 1) {
				/* Will stall if using fw->data ... */
				memcpy(buff, fw->data + i, size);

				ret = usb_control_msg(udev, usb_sndctrlpipe(udev,0),
						      M9206_FW,
						      USB_TYPE_VENDOR | USB_DIR_OUT,
						      value, index, buff, size, 20);
				if (ret != size) {
					deb("error while uploading fw!\n");
					ret = -EIO;
					goto done;
				}
				msleep(3);
			}
			i += size;
		}
		if (i != fw->size) {
			deb("bad firmware file!\n");
			ret = -EINVAL;
			goto done;
		}
	}

	msleep(36);

	/* m920x will disconnect itself from the bus after this. */
	(void) m920x_write(udev, M9206_CORE, 0x01, M9206_FW_GO);
	deb("firmware uploaded!\n");

 done:
	kfree(buff);

	return ret;
}

/* Callbacks for DVB USB */
static int m920x_identify_state(struct usb_device *udev,
				struct dvb_usb_device_properties *props,
				struct dvb_usb_device_description **desc,
				int *cold)
{
	struct usb_host_interface *alt;

	alt = usb_altnum_to_altsetting(usb_ifnum_to_if(udev, 0), 1);
	*cold = (alt == NULL) ? 1 : 0;

	return 0;
}

/* demod configurations */
static int m920x_mt352_demod_init(struct dvb_frontend *fe)
{
	int ret;
	u8 config[] = { CONFIG, 0x3d };
	u8 clock[] = { CLOCK_CTL, 0x30 };
	u8 reset[] = { RESET, 0x80 };
	u8 adc_ctl[] = { ADC_CTL_1, 0x40 };
	u8 agc[] = { AGC_TARGET, 0x1c, 0x20 };
	u8 sec_agc[] = { 0x69, 0x00, 0xff, 0xff, 0x40, 0xff, 0x00, 0x40, 0x40 };
	u8 unk1[] = { 0x93, 0x1a };
	u8 unk2[] = { 0xb5, 0x7a };

	deb("Demod init!\n");

	if ((ret = mt352_write(fe, config, ARRAY_SIZE(config))) != 0)
		return ret;
	if ((ret = mt352_write(fe, clock, ARRAY_SIZE(clock))) != 0)
		return ret;
	if ((ret = mt352_write(fe, reset, ARRAY_SIZE(reset))) != 0)
		return ret;
	if ((ret = mt352_write(fe, adc_ctl, ARRAY_SIZE(adc_ctl))) != 0)
		return ret;
	if ((ret = mt352_write(fe, agc, ARRAY_SIZE(agc))) != 0)
		return ret;
	if ((ret = mt352_write(fe, sec_agc, ARRAY_SIZE(sec_agc))) != 0)
		return ret;
	if ((ret = mt352_write(fe, unk1, ARRAY_SIZE(unk1))) != 0)
		return ret;
	if ((ret = mt352_write(fe, unk2, ARRAY_SIZE(unk2))) != 0)
		return ret;

	return 0;
}

static struct mt352_config m920x_mt352_config = {
	.demod_address = 0x0f,
	.no_tuner = 1,
	.demod_init = m920x_mt352_demod_init,
};

static struct tda1004x_config m920x_tda10046_08_config = {
	.demod_address = 0x08,
	.invert = 0,
	.invert_oclk = 0,
	.ts_mode = TDA10046_TS_SERIAL,
	.xtal_freq = TDA10046_XTAL_16M,
	.if_freq = TDA10046_FREQ_045,
	.agc_config = TDA10046_AGC_TDA827X,
	.gpio_config = TDA10046_GPTRI,
	.request_firmware = NULL,
};

static struct tda1004x_config m920x_tda10046_0b_config = {
	.demod_address = 0x0b,
	.invert = 0,
	.invert_oclk = 0,
	.ts_mode = TDA10046_TS_SERIAL,
	.xtal_freq = TDA10046_XTAL_16M,
	.if_freq = TDA10046_FREQ_045,
	.agc_config = TDA10046_AGC_TDA827X,
	.gpio_config = TDA10046_GPTRI,
	.request_firmware = NULL, /* uses firmware EEPROM */
};

/* tuner configurations */
static struct qt1010_config m920x_qt1010_config = {
	.i2c_address = 0x62
};

/* Callbacks for DVB USB */
static int m920x_mt352_frontend_attach(struct dvb_usb_adapter *adap)
{
	deb("%s\n",__func__);

	if ((adap->fe = dvb_attach(mt352_attach,
				   &m920x_mt352_config,
				   &adap->dev->i2c_adap)) == NULL)
		return -EIO;

	return 0;
}

static int m920x_tda10046_08_frontend_attach(struct dvb_usb_adapter *adap)
{
	deb("%s\n",__func__);

	if ((adap->fe = dvb_attach(tda10046_attach,
				   &m920x_tda10046_08_config,
				   &adap->dev->i2c_adap)) == NULL)
		return -EIO;

	return 0;
}

static int m920x_tda10046_0b_frontend_attach(struct dvb_usb_adapter *adap)
{
	deb("%s\n",__func__);

	if ((adap->fe = dvb_attach(tda10046_attach,
				   &m920x_tda10046_0b_config,
				   &adap->dev->i2c_adap)) == NULL)
		return -EIO;

	return 0;
}

static int m920x_qt1010_tuner_attach(struct dvb_usb_adapter *adap)
{
	deb("%s\n",__func__);

	if (dvb_attach(qt1010_attach, adap->fe, &adap->dev->i2c_adap, &m920x_qt1010_config) == NULL)
		return -ENODEV;

	return 0;
}

static int m920x_tda8275_60_tuner_attach(struct dvb_usb_adapter *adap)
{
	deb("%s\n",__func__);

	if (dvb_attach(tda827x_attach, adap->fe, 0x60, &adap->dev->i2c_adap, NULL) == NULL)
		return -ENODEV;

	return 0;
}

static int m920x_tda8275_61_tuner_attach(struct dvb_usb_adapter *adap)
{
	deb("%s\n",__func__);

	if (dvb_attach(tda827x_attach, adap->fe, 0x61, &adap->dev->i2c_adap, NULL) == NULL)
		return -ENODEV;

	return 0;
}

/* device-specific initialization */
static struct m920x_inits megasky_rc_init [] = {
	{ M9206_RC_INIT2, 0xa8 },
	{ M9206_RC_INIT1, 0x51 },
	{ } /* terminating entry */
};

static struct m920x_inits tvwalkertwin_rc_init [] = {
	{ M9206_RC_INIT2, 0x00 },
	{ M9206_RC_INIT1, 0xef },
	{ 0xff28,         0x00 },
	{ 0xff23,         0x00 },
	{ 0xff21,         0x30 },
	{ } /* terminating entry */
};

/* ir keymaps */
static struct dvb_usb_rc_key megasky_rc_keys [] = {
	{ 0x0012, KEY_POWER },
	{ 0x001e, KEY_CYCLEWINDOWS }, /* min/max */
	{ 0x0002, KEY_CHANNELUP },
	{ 0x0005, KEY_CHANNELDOWN },
	{ 0x0003, KEY_VOLUMEUP },
	{ 0x0006, KEY_VOLUMEDOWN },
	{ 0x0004, KEY_MUTE },
	{ 0x0007, KEY_OK }, /* TS */
	{ 0x0008, KEY_STOP },
	{ 0x0009, KEY_MENU }, /* swap */
	{ 0x000a, KEY_REWIND },
	{ 0x001b, KEY_PAUSE },
	{ 0x001f, KEY_FASTFORWARD },
	{ 0x000c, KEY_RECORD },
	{ 0x000d, KEY_CAMERA }, /* screenshot */
	{ 0x000e, KEY_COFFEE }, /* "MTS" */
};

static struct dvb_usb_rc_key tvwalkertwin_rc_keys [] = {
	{ 0x0001, KEY_ZOOM }, /* Full Screen */
	{ 0x0002, KEY_CAMERA }, /* snapshot */
	{ 0x0003, KEY_MUTE },
	{ 0x0004, KEY_REWIND },
	{ 0x0005, KEY_PLAYPAUSE }, /* Play/Pause */
	{ 0x0006, KEY_FASTFORWARD },
	{ 0x0007, KEY_RECORD },
	{ 0x0008, KEY_STOP },
	{ 0x0009, KEY_TIME }, /* Timeshift */
	{ 0x000c, KEY_COFFEE }, /* Recall */
	{ 0x000e, KEY_CHANNELUP },
	{ 0x0012, KEY_POWER },
	{ 0x0015, KEY_MENU }, /* source */
	{ 0x0018, KEY_CYCLEWINDOWS }, /* TWIN PIP */
	{ 0x001a, KEY_CHANNELDOWN },
	{ 0x001b, KEY_VOLUMEDOWN },
	{ 0x001e, KEY_VOLUMEUP },
};

/* DVB USB Driver stuff */
static struct dvb_usb_device_properties megasky_properties;
static struct dvb_usb_device_properties digivox_mini_ii_properties;
static struct dvb_usb_device_properties tvwalkertwin_properties;
static struct dvb_usb_device_properties dposh_properties;

static int m920x_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	struct dvb_usb_device *d = NULL;
	int ret;
	struct m920x_inits *rc_init_seq = NULL;
	int bInterfaceNumber = intf->cur_altsetting->desc.bInterfaceNumber;

	deb("Probing for m920x device at interface %d\n", bInterfaceNumber);

	if (bInterfaceNumber == 0) {
		/* Single-tuner device, or first interface on
		 * multi-tuner device
		 */

		ret = dvb_usb_device_init(intf, &megasky_properties,
					  THIS_MODULE, &d, adapter_nr);
		if (ret == 0) {
			rc_init_seq = megasky_rc_init;
			goto found;
		}

		ret = dvb_usb_device_init(intf, &digivox_mini_ii_properties,
					  THIS_MODULE, &d, adapter_nr);
		if (ret == 0) {
			/* No remote control, so no rc_init_seq */
			goto found;
		}

		/* This configures both tuners on the TV Walker Twin */
		ret = dvb_usb_device_init(intf, &tvwalkertwin_properties,
					  THIS_MODULE, &d, adapter_nr);
		if (ret == 0) {
			rc_init_seq = tvwalkertwin_rc_init;
			goto found;
		}

		ret = dvb_usb_device_init(intf, &dposh_properties,
					  THIS_MODULE, &d, adapter_nr);
		if (ret == 0) {
			/* Remote controller not supported yet. */
			goto found;
		}

		return ret;
	} else {
		/* Another interface on a multi-tuner device */

		/* The LifeView TV Walker Twin gets here, but struct
		 * tvwalkertwin_properties already configured both
		 * tuners, so there is nothing for us to do here
		 */
	}

 found:
	if ((ret = m920x_init_ep(intf)) < 0)
		return ret;

	if (d && (ret = m920x_init(d, rc_init_seq)) != 0)
		return ret;

	return ret;
}

static struct usb_device_id m920x_table [] = {
		{ USB_DEVICE(USB_VID_MSI, USB_PID_MSI_MEGASKY580) },
		{ USB_DEVICE(USB_VID_ANUBIS_ELECTRONIC,
			     USB_PID_MSI_DIGI_VOX_MINI_II) },
		{ USB_DEVICE(USB_VID_ANUBIS_ELECTRONIC,
			     USB_PID_LIFEVIEW_TV_WALKER_TWIN_COLD) },
		{ USB_DEVICE(USB_VID_ANUBIS_ELECTRONIC,
			     USB_PID_LIFEVIEW_TV_WALKER_TWIN_WARM) },
		{ USB_DEVICE(USB_VID_DPOSH, USB_PID_DPOSH_M9206_COLD) },
		{ USB_DEVICE(USB_VID_DPOSH, USB_PID_DPOSH_M9206_WARM) },
		{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, m920x_table);

static struct dvb_usb_device_properties megasky_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = DEVICE_SPECIFIC,
	.firmware = "dvb-usb-megasky-02.fw",
	.download_firmware = m920x_firmware_download,

	.rc_interval      = 100,
	.rc_key_map       = megasky_rc_keys,
	.rc_key_map_size  = ARRAY_SIZE(megasky_rc_keys),
	.rc_query         = m920x_rc_query,

	.size_of_priv     = sizeof(struct m920x_state),

	.identify_state   = m920x_identify_state,
	.num_adapters = 1,
	.adapter = {{
		.caps = DVB_USB_ADAP_HAS_PID_FILTER |
			DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,

		.pid_filter_count = 8,
		.pid_filter       = m920x_pid_filter,
		.pid_filter_ctrl  = m920x_pid_filter_ctrl,

		.frontend_attach  = m920x_mt352_frontend_attach,
		.tuner_attach     = m920x_qt1010_tuner_attach,

		.stream = {
			.type = USB_BULK,
			.count = 8,
			.endpoint = 0x81,
			.u = {
				.bulk = {
					.buffersize = 512,
				}
			}
		},
	}},
	.i2c_algo         = &m920x_i2c_algo,

	.num_device_descs = 1,
	.devices = {
		{   "MSI Mega Sky 580 DVB-T USB2.0",
			{ &m920x_table[0], NULL },
			{ NULL },
		}
	}
};

static struct dvb_usb_device_properties digivox_mini_ii_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = DEVICE_SPECIFIC,
	.firmware = "dvb-usb-digivox-02.fw",
	.download_firmware = m920x_firmware_download,

	.size_of_priv     = sizeof(struct m920x_state),

	.identify_state   = m920x_identify_state,
	.num_adapters = 1,
	.adapter = {{
		.caps = DVB_USB_ADAP_HAS_PID_FILTER |
			DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,

		.pid_filter_count = 8,
		.pid_filter       = m920x_pid_filter,
		.pid_filter_ctrl  = m920x_pid_filter_ctrl,

		.frontend_attach  = m920x_tda10046_08_frontend_attach,
		.tuner_attach     = m920x_tda8275_60_tuner_attach,

		.stream = {
			.type = USB_BULK,
			.count = 8,
			.endpoint = 0x81,
			.u = {
				.bulk = {
					.buffersize = 0x4000,
				}
			}
		},
	}},
	.i2c_algo         = &m920x_i2c_algo,

	.num_device_descs = 1,
	.devices = {
		{   "MSI DIGI VOX mini II DVB-T USB2.0",
			{ &m920x_table[1], NULL },
			{ NULL },
		},
	}
};

/* LifeView TV Walker Twin support by Nick Andrew <nick@nick-andrew.net>
 *
 * LifeView TV Walker Twin has 1 x M9206, 2 x TDA10046, 2 x TDA8275A
 * TDA10046 #0 is located at i2c address 0x08
 * TDA10046 #1 is located at i2c address 0x0b
 * TDA8275A #0 is located at i2c address 0x60
 * TDA8275A #1 is located at i2c address 0x61
 */
static struct dvb_usb_device_properties tvwalkertwin_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = DEVICE_SPECIFIC,
	.firmware = "dvb-usb-tvwalkert.fw",
	.download_firmware = m920x_firmware_download,

	.rc_interval      = 100,
	.rc_key_map       = tvwalkertwin_rc_keys,
	.rc_key_map_size  = ARRAY_SIZE(tvwalkertwin_rc_keys),
	.rc_query         = m920x_rc_query,

	.size_of_priv     = sizeof(struct m920x_state),

	.identify_state   = m920x_identify_state,
	.num_adapters = 2,
	.adapter = {{
		.caps = DVB_USB_ADAP_HAS_PID_FILTER |
			DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,

		.pid_filter_count = 8,
		.pid_filter       = m920x_pid_filter,
		.pid_filter_ctrl  = m920x_pid_filter_ctrl,

		.frontend_attach  = m920x_tda10046_08_frontend_attach,
		.tuner_attach     = m920x_tda8275_60_tuner_attach,

		.stream = {
			.type = USB_BULK,
			.count = 8,
			.endpoint = 0x81,
			.u = {
				 .bulk = {
					 .buffersize = 512,
				 }
			}
		}},{
		.caps = DVB_USB_ADAP_HAS_PID_FILTER |
			DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,

		.pid_filter_count = 8,
		.pid_filter       = m920x_pid_filter,
		.pid_filter_ctrl  = m920x_pid_filter_ctrl,

		.frontend_attach  = m920x_tda10046_0b_frontend_attach,
		.tuner_attach     = m920x_tda8275_61_tuner_attach,

		.stream = {
			.type = USB_BULK,
			.count = 8,
			.endpoint = 0x82,
			.u = {
				 .bulk = {
					 .buffersize = 512,
				 }
			}
		},
	}},
	.i2c_algo         = &m920x_i2c_algo,

	.num_device_descs = 1,
	.devices = {
		{   .name = "LifeView TV Walker Twin DVB-T USB2.0",
		    .cold_ids = { &m920x_table[2], NULL },
		    .warm_ids = { &m920x_table[3], NULL },
		},
	}
};

static struct dvb_usb_device_properties dposh_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = DEVICE_SPECIFIC,
	.firmware = "dvb-usb-dposh-01.fw",
	.download_firmware = m920x_firmware_download,

	.size_of_priv     = sizeof(struct m920x_state),

	.identify_state   = m920x_identify_state,
	.num_adapters = 1,
	.adapter = {{
		/* Hardware pid filters don't work with this device/firmware */

		.frontend_attach  = m920x_mt352_frontend_attach,
		.tuner_attach     = m920x_qt1010_tuner_attach,

		.stream = {
			.type = USB_BULK,
			.count = 8,
			.endpoint = 0x81,
			.u = {
				 .bulk = {
					 .buffersize = 512,
				 }
			}
		},
	}},
	.i2c_algo         = &m920x_i2c_algo,

	.num_device_descs = 1,
	.devices = {
		 {   .name = "Dposh DVB-T USB2.0",
		     .cold_ids = { &m920x_table[4], NULL },
		     .warm_ids = { &m920x_table[5], NULL },
		 },
	 }
};

static struct usb_driver m920x_driver = {
	.name		= "dvb_usb_m920x",
	.probe		= m920x_probe,
	.disconnect	= dvb_usb_device_exit,
	.id_table	= m920x_table,
};

/* module stuff */
static int __init m920x_module_init(void)
{
	int ret;

	if ((ret = usb_register(&m920x_driver))) {
		err("usb_register failed. Error number %d", ret);
		return ret;
	}

	return 0;
}

static void __exit m920x_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&m920x_driver);
}

module_init (m920x_module_init);
module_exit (m920x_module_exit);

MODULE_AUTHOR("Aapo Tahkola <aet@rasterburn.org>");
MODULE_DESCRIPTION("DVB Driver for ULI M920x");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");

/*
 * Local variables:
 * c-basic-offset: 8
 */

/* DVB USB compliant linux driver for Conexant USB reference design.
 *
 * The Conexant reference design I saw on their website was only for analogue
 * capturing (using the cx25842). The box I took to write this driver (reverse
 * engineered) is the one labeled Medion MD95700. In addition to the cx25842
 * for analogue capturing it also has a cx22702 DVB-T demodulator on the main
 * board. Besides it has a atiremote (X10) and a USB2.0 hub onboard.
 *
 * Maybe it is a little bit premature to call this driver cxusb, but I assume
 * the USB protocol is identical or at least inherited from the reference
 * design, so it can be reused for the "analogue-only" device (if it will
 * appear at all).
 *
 * TODO: Use the cx25840-driver for the analogue part
 *
 * Copyright (C) 2005 Patrick Boettcher (patrick.boettcher@desy.de)
 * Copyright (C) 2005 Michael Krufky (mkrufky@m1k.net)
 * Copyright (C) 2006 Chris Pascoe (c.pascoe@itee.uq.edu.au)
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include "cxusb.h"

#include "cx22702.h"
#include "lgdt330x.h"
#include "lg_h06xf.h"
#include "mt352.h"
#include "mt352_priv.h"
#include "zl10353.h"

/* debug */
int dvb_usb_cxusb_debug;
module_param_named(debug,dvb_usb_cxusb_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=rc (or-able))." DVB_USB_DEBUG_STATUS);

static int cxusb_ctrl_msg(struct dvb_usb_device *d,
		u8 cmd, u8 *wbuf, int wlen, u8 *rbuf, int rlen)
{
	int wo = (rbuf == NULL || rlen == 0); /* write-only */
	u8 sndbuf[1+wlen];
	memset(sndbuf,0,1+wlen);

	sndbuf[0] = cmd;
	memcpy(&sndbuf[1],wbuf,wlen);
	if (wo)
		dvb_usb_generic_write(d,sndbuf,1+wlen);
	else
		dvb_usb_generic_rw(d,sndbuf,1+wlen,rbuf,rlen,0);

	return 0;
}

/* GPIO */
static void cxusb_gpio_tuner(struct dvb_usb_device *d, int onoff)
{
	struct cxusb_state *st = d->priv;
	u8 o[2],i;

	if (st->gpio_write_state[GPIO_TUNER] == onoff)
		return;

	o[0] = GPIO_TUNER;
	o[1] = onoff;
	cxusb_ctrl_msg(d,CMD_GPIO_WRITE,o,2,&i,1);

	if (i != 0x01)
		deb_info("gpio_write failed.\n");

	st->gpio_write_state[GPIO_TUNER] = onoff;
}

/* I2C */
static int cxusb_i2c_xfer(struct i2c_adapter *adap,struct i2c_msg msg[],int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int i;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	if (num > 2)
		warn("more than two i2c messages at a time is not handled yet. TODO.");

	for (i = 0; i < num; i++) {

		if (d->udev->descriptor.idVendor == USB_VID_MEDION)
			switch (msg[i].addr) {
				case 0x63:
					cxusb_gpio_tuner(d,0);
					break;
				default:
					cxusb_gpio_tuner(d,1);
					break;
			}

		/* read request */
		if (i+1 < num && (msg[i+1].flags & I2C_M_RD)) {
			u8 obuf[3+msg[i].len], ibuf[1+msg[i+1].len];
			obuf[0] = msg[i].len;
			obuf[1] = msg[i+1].len;
			obuf[2] = msg[i].addr;
			memcpy(&obuf[3],msg[i].buf,msg[i].len);

			if (cxusb_ctrl_msg(d, CMD_I2C_READ,
						obuf, 3+msg[i].len,
						ibuf, 1+msg[i+1].len) < 0)
				break;

			if (ibuf[0] != 0x08)
				deb_i2c("i2c read may have failed\n");

			memcpy(msg[i+1].buf,&ibuf[1],msg[i+1].len);

			i++;
		} else { /* write */
			u8 obuf[2+msg[i].len], ibuf;
			obuf[0] = msg[i].addr;
			obuf[1] = msg[i].len;
			memcpy(&obuf[2],msg[i].buf,msg[i].len);

			if (cxusb_ctrl_msg(d,CMD_I2C_WRITE, obuf, 2+msg[i].len, &ibuf,1) < 0)
				break;
			if (ibuf != 0x08)
				deb_i2c("i2c write may have failed\n");
		}
	}

	mutex_unlock(&d->i2c_mutex);
	return i;
}

static u32 cxusb_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm cxusb_i2c_algo = {
	.master_xfer   = cxusb_i2c_xfer,
	.functionality = cxusb_i2c_func,
};

static int cxusb_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	u8 b = 0;
	if (onoff)
		return cxusb_ctrl_msg(d, CMD_POWER_ON, &b, 1, NULL, 0);
	else
		return cxusb_ctrl_msg(d, CMD_POWER_OFF, &b, 1, NULL, 0);
}

static int cxusb_bluebird_power_ctrl(struct dvb_usb_device *d, int onoff)
{
	u8 b = 0;
	if (onoff)
		return cxusb_ctrl_msg(d, CMD_POWER_ON, &b, 1, NULL, 0);
	else
		return 0;
}

static int cxusb_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	u8 buf[2] = { 0x03, 0x00 };
	if (onoff)
		cxusb_ctrl_msg(adap->dev, CMD_STREAMING_ON, buf, 2, NULL, 0);
	else
		cxusb_ctrl_msg(adap->dev, CMD_STREAMING_OFF, NULL, 0, NULL, 0);

	return 0;
}

static int cxusb_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{
	struct dvb_usb_rc_key *keymap = d->props.rc_key_map;
	u8 ircode[4];
	int i;

	cxusb_ctrl_msg(d, CMD_GET_IR_CODE, NULL, 0, ircode, 4);

	*event = 0;
	*state = REMOTE_NO_KEY_PRESSED;

	for (i = 0; i < d->props.rc_key_map_size; i++) {
		if (keymap[i].custom == ircode[2] &&
		    keymap[i].data == ircode[3]) {
			*event = keymap[i].event;
			*state = REMOTE_KEY_PRESSED;

			return 0;
		}
	}

	return 0;
}

static struct dvb_usb_rc_key dvico_mce_rc_keys[] = {
	{ 0xfe, 0x02, KEY_TV },
	{ 0xfe, 0x0e, KEY_MP3 },
	{ 0xfe, 0x1a, KEY_DVD },
	{ 0xfe, 0x1e, KEY_FAVORITES },
	{ 0xfe, 0x16, KEY_SETUP },
	{ 0xfe, 0x46, KEY_POWER2 },
	{ 0xfe, 0x0a, KEY_EPG },
	{ 0xfe, 0x49, KEY_BACK },
	{ 0xfe, 0x4d, KEY_MENU },
	{ 0xfe, 0x51, KEY_UP },
	{ 0xfe, 0x5b, KEY_LEFT },
	{ 0xfe, 0x5f, KEY_RIGHT },
	{ 0xfe, 0x53, KEY_DOWN },
	{ 0xfe, 0x5e, KEY_OK },
	{ 0xfe, 0x59, KEY_INFO },
	{ 0xfe, 0x55, KEY_TAB },
	{ 0xfe, 0x0f, KEY_PREVIOUSSONG },/* Replay */
	{ 0xfe, 0x12, KEY_NEXTSONG },	/* Skip */
	{ 0xfe, 0x42, KEY_ENTER	 },	/* Windows/Start */
	{ 0xfe, 0x15, KEY_VOLUMEUP },
	{ 0xfe, 0x05, KEY_VOLUMEDOWN },
	{ 0xfe, 0x11, KEY_CHANNELUP },
	{ 0xfe, 0x09, KEY_CHANNELDOWN },
	{ 0xfe, 0x52, KEY_CAMERA },
	{ 0xfe, 0x5a, KEY_TUNER },	/* Live */
	{ 0xfe, 0x19, KEY_OPEN },
	{ 0xfe, 0x0b, KEY_1 },
	{ 0xfe, 0x17, KEY_2 },
	{ 0xfe, 0x1b, KEY_3 },
	{ 0xfe, 0x07, KEY_4 },
	{ 0xfe, 0x50, KEY_5 },
	{ 0xfe, 0x54, KEY_6 },
	{ 0xfe, 0x48, KEY_7 },
	{ 0xfe, 0x4c, KEY_8 },
	{ 0xfe, 0x58, KEY_9 },
	{ 0xfe, 0x13, KEY_ANGLE },	/* Aspect */
	{ 0xfe, 0x03, KEY_0 },
	{ 0xfe, 0x1f, KEY_ZOOM },
	{ 0xfe, 0x43, KEY_REWIND },
	{ 0xfe, 0x47, KEY_PLAYPAUSE },
	{ 0xfe, 0x4f, KEY_FASTFORWARD },
	{ 0xfe, 0x57, KEY_MUTE },
	{ 0xfe, 0x0d, KEY_STOP },
	{ 0xfe, 0x01, KEY_RECORD },
	{ 0xfe, 0x4e, KEY_POWER },
};

static struct dvb_usb_rc_key dvico_portable_rc_keys[] = {
	{ 0xfc, 0x02, KEY_SETUP },       /* Profile */
	{ 0xfc, 0x43, KEY_POWER2 },
	{ 0xfc, 0x06, KEY_EPG },
	{ 0xfc, 0x5a, KEY_BACK },
	{ 0xfc, 0x05, KEY_MENU },
	{ 0xfc, 0x47, KEY_INFO },
	{ 0xfc, 0x01, KEY_TAB },
	{ 0xfc, 0x42, KEY_PREVIOUSSONG },/* Replay */
	{ 0xfc, 0x49, KEY_VOLUMEUP },
	{ 0xfc, 0x09, KEY_VOLUMEDOWN },
	{ 0xfc, 0x54, KEY_CHANNELUP },
	{ 0xfc, 0x0b, KEY_CHANNELDOWN },
	{ 0xfc, 0x16, KEY_CAMERA },
	{ 0xfc, 0x40, KEY_TUNER },	/* ATV/DTV */
	{ 0xfc, 0x45, KEY_OPEN },
	{ 0xfc, 0x19, KEY_1 },
	{ 0xfc, 0x18, KEY_2 },
	{ 0xfc, 0x1b, KEY_3 },
	{ 0xfc, 0x1a, KEY_4 },
	{ 0xfc, 0x58, KEY_5 },
	{ 0xfc, 0x59, KEY_6 },
	{ 0xfc, 0x15, KEY_7 },
	{ 0xfc, 0x14, KEY_8 },
	{ 0xfc, 0x17, KEY_9 },
	{ 0xfc, 0x44, KEY_ANGLE },	/* Aspect */
	{ 0xfc, 0x55, KEY_0 },
	{ 0xfc, 0x07, KEY_ZOOM },
	{ 0xfc, 0x0a, KEY_REWIND },
	{ 0xfc, 0x08, KEY_PLAYPAUSE },
	{ 0xfc, 0x4b, KEY_FASTFORWARD },
	{ 0xfc, 0x5b, KEY_MUTE },
	{ 0xfc, 0x04, KEY_STOP },
	{ 0xfc, 0x56, KEY_RECORD },
	{ 0xfc, 0x57, KEY_POWER },
	{ 0xfc, 0x41, KEY_UNKNOWN },    /* INPUT */
	{ 0xfc, 0x00, KEY_UNKNOWN },    /* HD */
};

static int cxusb_dee1601_demod_init(struct dvb_frontend* fe)
{
	static u8 clock_config []  = { CLOCK_CTL,  0x38, 0x28 };
	static u8 reset []         = { RESET,      0x80 };
	static u8 adc_ctl_1_cfg [] = { ADC_CTL_1,  0x40 };
	static u8 agc_cfg []       = { AGC_TARGET, 0x28, 0x20 };
	static u8 gpp_ctl_cfg []   = { GPP_CTL,    0x33 };
	static u8 capt_range_cfg[] = { CAPT_RANGE, 0x32 };

	mt352_write(fe, clock_config,   sizeof(clock_config));
	udelay(200);
	mt352_write(fe, reset,          sizeof(reset));
	mt352_write(fe, adc_ctl_1_cfg,  sizeof(adc_ctl_1_cfg));

	mt352_write(fe, agc_cfg,        sizeof(agc_cfg));
	mt352_write(fe, gpp_ctl_cfg,    sizeof(gpp_ctl_cfg));
	mt352_write(fe, capt_range_cfg, sizeof(capt_range_cfg));

	return 0;
}

static int cxusb_mt352_demod_init(struct dvb_frontend* fe)
{	/* used in both lgz201 and th7579 */
	static u8 clock_config []  = { CLOCK_CTL,  0x38, 0x29 };
	static u8 reset []         = { RESET,      0x80 };
	static u8 adc_ctl_1_cfg [] = { ADC_CTL_1,  0x40 };
	static u8 agc_cfg []       = { AGC_TARGET, 0x24, 0x20 };
	static u8 gpp_ctl_cfg []   = { GPP_CTL,    0x33 };
	static u8 capt_range_cfg[] = { CAPT_RANGE, 0x32 };

	mt352_write(fe, clock_config,   sizeof(clock_config));
	udelay(200);
	mt352_write(fe, reset,          sizeof(reset));
	mt352_write(fe, adc_ctl_1_cfg,  sizeof(adc_ctl_1_cfg));

	mt352_write(fe, agc_cfg,        sizeof(agc_cfg));
	mt352_write(fe, gpp_ctl_cfg,    sizeof(gpp_ctl_cfg));
	mt352_write(fe, capt_range_cfg, sizeof(capt_range_cfg));
	return 0;
}

static int cxusb_lgh064f_tuner_set_params(struct dvb_frontend *fe,
					  struct dvb_frontend_parameters *fep)
{
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	return lg_h06xf_pll_set(fe, &adap->dev->i2c_adap, fep);
}

static struct cx22702_config cxusb_cx22702_config = {
	.demod_address = 0x63,

	.output_mode = CX22702_PARALLEL_OUTPUT,
};

static struct lgdt330x_config cxusb_lgdt3303_config = {
	.demod_address = 0x0e,
	.demod_chip    = LGDT3303,
};

static struct mt352_config cxusb_dee1601_config = {
	.demod_address = 0x0f,
	.demod_init    = cxusb_dee1601_demod_init,
};

static struct zl10353_config cxusb_zl10353_dee1601_config = {
	.demod_address = 0x0f,
	.parallel_ts = 1,
};

static struct mt352_config cxusb_mt352_config = {
	/* used in both lgz201 and th7579 */
	.demod_address = 0x0f,
	.demod_init    = cxusb_mt352_demod_init,
};

/* Callbacks for DVB USB */
static int cxusb_fmd1216me_tuner_attach(struct dvb_usb_adapter *adap)
{
	u8 bpll[4] = { 0x0b, 0xdc, 0x9c, 0xa0 };
	adap->pll_addr = 0x61;
	memcpy(adap->pll_init, bpll, 4);
	adap->pll_desc = &dvb_pll_fmd1216me;

	adap->fe->ops.tuner_ops.init = dvb_usb_tuner_init_i2c;
	adap->fe->ops.tuner_ops.set_params = dvb_usb_tuner_set_params_i2c;

	return 0;
}

static int cxusb_dee1601_tuner_attach(struct dvb_usb_adapter *adap)
{
	adap->pll_addr = 0x61;
	adap->pll_desc = &dvb_pll_thomson_dtt7579;
	adap->fe->ops.tuner_ops.calc_regs = dvb_usb_tuner_calc_regs;
	return 0;
}

static int cxusb_lgz201_tuner_attach(struct dvb_usb_adapter *adap)
{
	adap->pll_addr = 0x61;
	adap->pll_desc = &dvb_pll_lg_z201;
	adap->fe->ops.tuner_ops.calc_regs = dvb_usb_tuner_calc_regs;
	return 0;
}

static int cxusb_dtt7579_tuner_attach(struct dvb_usb_adapter *adap)
{
	adap->pll_addr = 0x60;
	adap->pll_desc = &dvb_pll_thomson_dtt7579;
	adap->fe->ops.tuner_ops.calc_regs = dvb_usb_tuner_calc_regs;
	return 0;
}

static int cxusb_lgdt3303_tuner_attach(struct dvb_usb_adapter *adap)
{
	adap->fe->ops.tuner_ops.set_params = cxusb_lgh064f_tuner_set_params;
	return 0;
}

static int cxusb_cx22702_frontend_attach(struct dvb_usb_adapter *adap)
{
	u8 b;
	if (usb_set_interface(adap->dev->udev, 0, 6) < 0)
		err("set interface failed");

	cxusb_ctrl_msg(adap->dev, CMD_DIGITAL, NULL, 0, &b, 1);

	if ((adap->fe = dvb_attach(cx22702_attach, &cxusb_cx22702_config, &adap->dev->i2c_adap)) != NULL)
		return 0;

	return -EIO;
}

static int cxusb_lgdt3303_frontend_attach(struct dvb_usb_adapter *adap)
{
	if (usb_set_interface(adap->dev->udev, 0, 7) < 0)
		err("set interface failed");

	cxusb_ctrl_msg(adap->dev, CMD_DIGITAL, NULL, 0, NULL, 0);

	if ((adap->fe = dvb_attach(lgdt330x_attach, &cxusb_lgdt3303_config, &adap->dev->i2c_adap)) != NULL)
		return 0;

	return -EIO;
}

static int cxusb_mt352_frontend_attach(struct dvb_usb_adapter *adap)
{
	/* used in both lgz201 and th7579 */
	if (usb_set_interface(adap->dev->udev, 0, 0) < 0)
		err("set interface failed");

	cxusb_ctrl_msg(adap->dev, CMD_DIGITAL, NULL, 0, NULL, 0);

	if ((adap->fe = dvb_attach(mt352_attach, &cxusb_mt352_config, &adap->dev->i2c_adap)) != NULL)
		return 0;

	return -EIO;
}

static int cxusb_dee1601_frontend_attach(struct dvb_usb_adapter *adap)
{
	if (usb_set_interface(adap->dev->udev, 0, 0) < 0)
		err("set interface failed");

	cxusb_ctrl_msg(adap->dev, CMD_DIGITAL, NULL, 0, NULL, 0);

	if (((adap->fe = dvb_attach(mt352_attach, &cxusb_dee1601_config, &adap->dev->i2c_adap)) != NULL) ||
		((adap->fe = dvb_attach(zl10353_attach, &cxusb_zl10353_dee1601_config, &adap->dev->i2c_adap)) != NULL))
		return 0;

	return -EIO;
}

/*
 * DViCO bluebird firmware needs the "warm" product ID to be patched into the
 * firmware file before download.
 */

#define BLUEBIRD_01_ID_OFFSET 6638
static int bluebird_patch_dvico_firmware_download(struct usb_device *udev, const struct firmware *fw)
{
	if (fw->size < BLUEBIRD_01_ID_OFFSET + 4)
		return -EINVAL;

	if (fw->data[BLUEBIRD_01_ID_OFFSET] == (USB_VID_DVICO & 0xff) &&
	    fw->data[BLUEBIRD_01_ID_OFFSET + 1] == USB_VID_DVICO >> 8) {

		fw->data[BLUEBIRD_01_ID_OFFSET + 2] = udev->descriptor.idProduct + 1;
		fw->data[BLUEBIRD_01_ID_OFFSET + 3] = udev->descriptor.idProduct >> 8;

		return usb_cypress_load_firmware(udev,fw,CYPRESS_FX2);
	}

	return -EINVAL;
}

/* DVB USB Driver stuff */
static struct dvb_usb_device_properties cxusb_medion_properties;
static struct dvb_usb_device_properties cxusb_bluebird_lgh064f_properties;
static struct dvb_usb_device_properties cxusb_bluebird_dee1601_properties;
static struct dvb_usb_device_properties cxusb_bluebird_lgz201_properties;
static struct dvb_usb_device_properties cxusb_bluebird_dtt7579_properties;

static int cxusb_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	if (dvb_usb_device_init(intf,&cxusb_medion_properties,THIS_MODULE,NULL) == 0 ||
		dvb_usb_device_init(intf,&cxusb_bluebird_lgh064f_properties,THIS_MODULE,NULL) == 0 ||
		dvb_usb_device_init(intf,&cxusb_bluebird_dee1601_properties,THIS_MODULE,NULL) == 0 ||
		dvb_usb_device_init(intf,&cxusb_bluebird_lgz201_properties,THIS_MODULE,NULL) == 0 ||
		dvb_usb_device_init(intf,&cxusb_bluebird_dtt7579_properties,THIS_MODULE,NULL) == 0) {
		return 0;
	}

	return -EINVAL;
}

static struct usb_device_id cxusb_table [] = {
		{ USB_DEVICE(USB_VID_MEDION, USB_PID_MEDION_MD95700) },
		{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_LG064F_COLD) },
		{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_LG064F_WARM) },
		{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_DUAL_1_COLD) },
		{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_DUAL_1_WARM) },
		{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_LGZ201_COLD) },
		{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_LGZ201_WARM) },
		{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_TH7579_COLD) },
		{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_TH7579_WARM) },
		{ USB_DEVICE(USB_VID_DVICO, USB_PID_DIGITALNOW_BLUEBIRD_DUAL_1_COLD) },
		{ USB_DEVICE(USB_VID_DVICO, USB_PID_DIGITALNOW_BLUEBIRD_DUAL_1_WARM) },
		{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_DUAL_2_COLD) },
		{ USB_DEVICE(USB_VID_DVICO, USB_PID_DVICO_BLUEBIRD_DUAL_2_WARM) },
		{}		/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, cxusb_table);

static struct dvb_usb_device_properties cxusb_medion_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl = CYPRESS_FX2,

	.size_of_priv     = sizeof(struct cxusb_state),

	.num_adapters = 1,
	.adapter = {
		{
			.streaming_ctrl   = cxusb_streaming_ctrl,
			.frontend_attach  = cxusb_cx22702_frontend_attach,
			.tuner_attach     = cxusb_fmd1216me_tuner_attach,
			/* parameter for the MPEG2-data transfer */
					.stream = {
						.type = USB_BULK,
				.count = 5,
				.endpoint = 0x02,
				.u = {
					.bulk = {
						.buffersize = 8192,
					}
				}
			},

		},
	},
	.power_ctrl       = cxusb_power_ctrl,

	.i2c_algo         = &cxusb_i2c_algo,

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 1,
	.devices = {
		{   "Medion MD95700 (MDUSBTV-HYBRID)",
			{ NULL },
			{ &cxusb_table[0], NULL },
		},
	}
};

static struct dvb_usb_device_properties cxusb_bluebird_lgh064f_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl          = DEVICE_SPECIFIC,
	.firmware          = "dvb-usb-bluebird-01.fw",
	.download_firmware = bluebird_patch_dvico_firmware_download,
	/* use usb alt setting 0 for EP4 transfer (dvb-t),
	   use usb alt setting 7 for EP2 transfer (atsc) */

	.size_of_priv     = sizeof(struct cxusb_state),

	.num_adapters = 1,
	.adapter = {
		{
			.streaming_ctrl   = cxusb_streaming_ctrl,
			.frontend_attach  = cxusb_lgdt3303_frontend_attach,
			.tuner_attach     = cxusb_lgdt3303_tuner_attach,

			/* parameter for the MPEG2-data transfer */
					.stream = {
						.type = USB_BULK,
				.count = 5,
				.endpoint = 0x02,
				.u = {
					.bulk = {
						.buffersize = 8192,
					}
				}
			},
		},
	},

	.power_ctrl       = cxusb_bluebird_power_ctrl,

	.i2c_algo         = &cxusb_i2c_algo,

	.rc_interval      = 100,
	.rc_key_map       = dvico_portable_rc_keys,
	.rc_key_map_size  = ARRAY_SIZE(dvico_portable_rc_keys),
	.rc_query         = cxusb_rc_query,

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 1,
	.devices = {
		{   "DViCO FusionHDTV5 USB Gold",
			{ &cxusb_table[1], NULL },
			{ &cxusb_table[2], NULL },
		},
	}
};

static struct dvb_usb_device_properties cxusb_bluebird_dee1601_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl          = DEVICE_SPECIFIC,
	.firmware          = "dvb-usb-bluebird-01.fw",
	.download_firmware = bluebird_patch_dvico_firmware_download,
	/* use usb alt setting 0 for EP4 transfer (dvb-t),
	   use usb alt setting 7 for EP2 transfer (atsc) */

	.size_of_priv     = sizeof(struct cxusb_state),

	.num_adapters = 1,
	.adapter = {
		{
			.streaming_ctrl   = cxusb_streaming_ctrl,
			.frontend_attach  = cxusb_dee1601_frontend_attach,
			.tuner_attach     = cxusb_dee1601_tuner_attach,
			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 5,
				.endpoint = 0x04,
				.u = {
					.bulk = {
						.buffersize = 8192,
					}
				}
			},
		},
	},

	.power_ctrl       = cxusb_bluebird_power_ctrl,

	.i2c_algo         = &cxusb_i2c_algo,

	.rc_interval      = 150,
	.rc_key_map       = dvico_mce_rc_keys,
	.rc_key_map_size  = ARRAY_SIZE(dvico_mce_rc_keys),
	.rc_query         = cxusb_rc_query,

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 3,
	.devices = {
		{   "DViCO FusionHDTV DVB-T Dual USB",
			{ &cxusb_table[3], NULL },
			{ &cxusb_table[4], NULL },
		},
		{   "DigitalNow DVB-T Dual USB",
			{ &cxusb_table[9],  NULL },
			{ &cxusb_table[10], NULL },
		},
		{   "DViCO FusionHDTV DVB-T Dual Digital 2",
			{ &cxusb_table[11], NULL },
			{ &cxusb_table[12], NULL },
		},
	}
};

static struct dvb_usb_device_properties cxusb_bluebird_lgz201_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl          = DEVICE_SPECIFIC,
	.firmware          = "dvb-usb-bluebird-01.fw",
	.download_firmware = bluebird_patch_dvico_firmware_download,
	/* use usb alt setting 0 for EP4 transfer (dvb-t),
	   use usb alt setting 7 for EP2 transfer (atsc) */

	.size_of_priv     = sizeof(struct cxusb_state),

	.num_adapters = 2,
	.adapter = {
		{
			.streaming_ctrl   = cxusb_streaming_ctrl,
			.frontend_attach  = cxusb_mt352_frontend_attach,
			.tuner_attach     = cxusb_lgz201_tuner_attach,

			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 5,
				.endpoint = 0x04,
				.u = {
					.bulk = {
						.buffersize = 8192,
					}
				}
			},
		},
	},
	.power_ctrl       = cxusb_bluebird_power_ctrl,

	.i2c_algo         = &cxusb_i2c_algo,

	.rc_interval      = 100,
	.rc_key_map       = dvico_portable_rc_keys,
	.rc_key_map_size  = ARRAY_SIZE(dvico_portable_rc_keys),
	.rc_query         = cxusb_rc_query,

	.generic_bulk_ctrl_endpoint = 0x01,
	.num_device_descs = 1,
	.devices = {
		{   "DViCO FusionHDTV DVB-T USB (LGZ201)",
			{ &cxusb_table[5], NULL },
			{ &cxusb_table[6], NULL },
		},
	}
};

static struct dvb_usb_device_properties cxusb_bluebird_dtt7579_properties = {
	.caps = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl          = DEVICE_SPECIFIC,
	.firmware          = "dvb-usb-bluebird-01.fw",
	.download_firmware = bluebird_patch_dvico_firmware_download,
	/* use usb alt setting 0 for EP4 transfer (dvb-t),
	   use usb alt setting 7 for EP2 transfer (atsc) */

	.size_of_priv     = sizeof(struct cxusb_state),

	.num_adapters = 1,
	.adapter = {
		{
			.streaming_ctrl   = cxusb_streaming_ctrl,
			.frontend_attach  = cxusb_mt352_frontend_attach,
			.tuner_attach     = cxusb_dtt7579_tuner_attach,

			/* parameter for the MPEG2-data transfer */
			.stream = {
				.type = USB_BULK,
				.count = 5,
				.endpoint = 0x04,
				.u = {
					.bulk = {
						.buffersize = 8192,
					}
				}
			},
		},
	},
	.power_ctrl       = cxusb_bluebird_power_ctrl,

	.i2c_algo         = &cxusb_i2c_algo,

	.rc_interval      = 100,
	.rc_key_map       = dvico_portable_rc_keys,
	.rc_key_map_size  = ARRAY_SIZE(dvico_portable_rc_keys),
	.rc_query         = cxusb_rc_query,

	.generic_bulk_ctrl_endpoint = 0x01,

	.num_device_descs = 1,
	.devices = {
		{   "DViCO FusionHDTV DVB-T USB (TH7579)",
			{ &cxusb_table[7], NULL },
			{ &cxusb_table[8], NULL },
		},
	}
};

static struct usb_driver cxusb_driver = {
	.name		= "dvb_usb_cxusb",
	.probe		= cxusb_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table	= cxusb_table,
};

/* module stuff */
static int __init cxusb_module_init(void)
{
	int result;
	if ((result = usb_register(&cxusb_driver))) {
		err("usb_register failed. Error number %d",result);
		return result;
	}

	return 0;
}

static void __exit cxusb_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&cxusb_driver);
}

module_init (cxusb_module_init);
module_exit (cxusb_module_exit);

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@desy.de>");
MODULE_AUTHOR("Michael Krufky <mkrufky@m1k.net>");
MODULE_AUTHOR("Chris Pascoe <c.pascoe@itee.uq.edu.au>");
MODULE_DESCRIPTION("Driver for Conexant USB2.0 hybrid reference design");
MODULE_VERSION("1.0-alpha");
MODULE_LICENSE("GPL");

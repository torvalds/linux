/*
 * DVB USB Linux driver for Anysee E30 DVB-C & DVB-T USB2.0 receiver
 *
 * Copyright (C) 2007 Antti Palosaari <crope@iki.fi>
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
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * TODO:
 * - add smart card reader support for Conditional Access (CA)
 *
 * Card reader in Anysee is nothing more than ISO 7816 card reader.
 * There is no hardware CAM in any Anysee device sold.
 * In my understanding it should be implemented by making own module
 * for ISO 7816 card reader, like dvb_ca_en50221 is implemented. This
 * module registers serial interface that can be used to communicate
 * with any ISO 7816 smart card.
 *
 * Any help according to implement serial smart card reader support
 * is highly welcome!
 */

#include "anysee.h"
#include "tda1002x.h"
#include "mt352.h"
#include "mt352_priv.h"
#include "zl10353.h"

/* debug */
static int dvb_usb_anysee_debug;
module_param_named(debug, dvb_usb_anysee_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level" DVB_USB_DEBUG_STATUS);
static int dvb_usb_anysee_delsys;
module_param_named(delsys, dvb_usb_anysee_delsys, int, 0644);
MODULE_PARM_DESC(delsys, "select delivery mode (0=DVB-C, 1=DVB-T)");
DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static DEFINE_MUTEX(anysee_usb_mutex);

static int anysee_ctrl_msg(struct dvb_usb_device *d, u8 *sbuf, u8 slen,
	u8 *rbuf, u8 rlen)
{
	struct anysee_state *state = d->priv;
	int act_len, ret;
	u8 buf[64];

	if (slen > sizeof(buf))
		slen = sizeof(buf);
	memcpy(&buf[0], sbuf, slen);
	buf[60] = state->seq++;

	if (mutex_lock_interruptible(&anysee_usb_mutex) < 0)
		return -EAGAIN;

	/* We need receive one message more after dvb_usb_generic_rw due
	   to weird transaction flow, which is 1 x send + 2 x receive. */
	ret = dvb_usb_generic_rw(d, buf, sizeof(buf), buf, sizeof(buf), 0);

	if (!ret) {
		/* receive 2nd answer */
		ret = usb_bulk_msg(d->udev, usb_rcvbulkpipe(d->udev,
			d->props.generic_bulk_ctrl_endpoint), buf, sizeof(buf),
			&act_len, 2000);
		if (ret)
			err("%s: recv bulk message failed: %d", __func__, ret);
		else {
			deb_xfer("<<< ");
			debug_dump(buf, act_len, deb_xfer);
		}
	}

	/* read request, copy returned data to return buf */
	if (!ret && rbuf && rlen)
		memcpy(rbuf, buf, rlen);

	mutex_unlock(&anysee_usb_mutex);

	return ret;
}

static int anysee_read_reg(struct dvb_usb_device *d, u16 reg, u8 *val)
{
	u8 buf[] = {CMD_REG_READ, reg >> 8, reg & 0xff, 0x01};
	int ret;
	ret = anysee_ctrl_msg(d, buf, sizeof(buf), val, 1);
	deb_info("%s: reg:%04x val:%02x\n", __func__, reg, *val);
	return ret;
}

static int anysee_write_reg(struct dvb_usb_device *d, u16 reg, u8 val)
{
	u8 buf[] = {CMD_REG_WRITE, reg >> 8, reg & 0xff, 0x01, val};
	deb_info("%s: reg:%04x val:%02x\n", __func__, reg, val);
	return anysee_ctrl_msg(d, buf, sizeof(buf), NULL, 0);
}

static int anysee_get_hw_info(struct dvb_usb_device *d, u8 *id)
{
	u8 buf[] = {CMD_GET_HW_INFO};
	return anysee_ctrl_msg(d, buf, sizeof(buf), id, 3);
}

static int anysee_streaming_ctrl(struct dvb_usb_adapter *adap, int onoff)
{
	u8 buf[] = {CMD_STREAMING_CTRL, (u8)onoff, 0x00};
	deb_info("%s: onoff:%02x\n", __func__, onoff);
	return anysee_ctrl_msg(adap->dev, buf, sizeof(buf), NULL, 0);
}

static int anysee_led_ctrl(struct dvb_usb_device *d, u8 mode, u8 interval)
{
	u8 buf[] = {CMD_LED_AND_IR_CTRL, 0x01, mode, interval};
	deb_info("%s: state:%02x interval:%02x\n", __func__, mode, interval);
	return anysee_ctrl_msg(d, buf, sizeof(buf), NULL, 0);
}

static int anysee_ir_ctrl(struct dvb_usb_device *d, u8 onoff)
{
	u8 buf[] = {CMD_LED_AND_IR_CTRL, 0x02, onoff};
	deb_info("%s: onoff:%02x\n", __func__, onoff);
	return anysee_ctrl_msg(d, buf, sizeof(buf), NULL, 0);
}

static int anysee_init(struct dvb_usb_device *d)
{
	int ret;
	/* LED light */
	ret = anysee_led_ctrl(d, 0x01, 0x03);
	if (ret)
		return ret;

	/* enable IR */
	ret = anysee_ir_ctrl(d, 1);
	if (ret)
		return ret;

	return 0;
}

/* I2C */
static int anysee_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msg,
	int num)
{
	struct dvb_usb_device *d = i2c_get_adapdata(adap);
	int ret = 0, inc, i = 0;

	if (mutex_lock_interruptible(&d->i2c_mutex) < 0)
		return -EAGAIN;

	while (i < num) {
		if (num > i + 1 && (msg[i+1].flags & I2C_M_RD)) {
			u8 buf[6];
			buf[0] = CMD_I2C_READ;
			buf[1] = msg[i].addr + 1;
			buf[2] = msg[i].buf[0];
			buf[3] = 0x00;
			buf[4] = 0x00;
			buf[5] = 0x01;
			ret = anysee_ctrl_msg(d, buf, sizeof(buf), msg[i+1].buf,
				msg[i+1].len);
			inc = 2;
		} else {
			u8 buf[4+msg[i].len];
			buf[0] = CMD_I2C_WRITE;
			buf[1] = msg[i].addr;
			buf[2] = msg[i].len;
			buf[3] = 0x01;
			memcpy(&buf[4], msg[i].buf, msg[i].len);
			ret = anysee_ctrl_msg(d, buf, sizeof(buf), NULL, 0);
			inc = 1;
		}
		if (ret)
			break;

		i += inc;
	}

	mutex_unlock(&d->i2c_mutex);

	return ret ? ret : i;
}

static u32 anysee_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm anysee_i2c_algo = {
	.master_xfer   = anysee_master_xfer,
	.functionality = anysee_i2c_func,
};

static int anysee_mt352_demod_init(struct dvb_frontend *fe)
{
	static u8 clock_config[]   = { CLOCK_CTL,  0x38, 0x28 };
	static u8 reset[]          = { RESET,      0x80 };
	static u8 adc_ctl_1_cfg[]  = { ADC_CTL_1,  0x40 };
	static u8 agc_cfg[]        = { AGC_TARGET, 0x28, 0x20 };
	static u8 gpp_ctl_cfg[]    = { GPP_CTL,    0x33 };
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

/* Callbacks for DVB USB */
static struct tda10023_config anysee_tda10023_config = {
	.demod_address = 0x1a,
	.invert = 0,
	.xtal   = 16000000,
	.pll_m  = 11,
	.pll_p  = 3,
	.pll_n  = 1,
	.output_mode = TDA10023_OUTPUT_MODE_PARALLEL_C,
	.deltaf = 0xfeeb,
};

static struct mt352_config anysee_mt352_config = {
	.demod_address = 0x1e,
	.demod_init    = anysee_mt352_demod_init,
};

static struct zl10353_config anysee_zl10353_config = {
	.demod_address = 0x1e,
	.parallel_ts = 1,
};

static int anysee_frontend_attach(struct dvb_usb_adapter *adap)
{
	int ret;
	struct anysee_state *state = adap->dev->priv;
	u8 hw_info[3];
	u8 io_d; /* IO port D */

	/* check which hardware we have
	   We must do this call two times to get reliable values (hw bug). */
	ret = anysee_get_hw_info(adap->dev, hw_info);
	if (ret)
		return ret;
	ret = anysee_get_hw_info(adap->dev, hw_info);
	if (ret)
		return ret;

	/* Meaning of these info bytes are guessed. */
	info("firmware version:%d.%d.%d hardware id:%d",
		0, hw_info[1], hw_info[2], hw_info[0]);

	ret = anysee_read_reg(adap->dev, 0xb0, &io_d); /* IO port D */
	if (ret)
		return ret;
	deb_info("%s: IO port D:%02x\n", __func__, io_d);

	/* Select demod using trial and error method. */

	/* Try to attach demodulator in following order:
	      model      demod     hw  firmware
	   1. E30        MT352     02  0.2.1
	   2. E30        ZL10353   02  0.2.1
	   3. E30 Combo  ZL10353   0f  0.1.2    DVB-T/C combo
	   4. E30 Plus   ZL10353   06  0.1.0
	   5. E30C Plus  TDA10023  0a  0.1.0    rev 0.2
	      E30C Plus  TDA10023  0f  0.1.2    rev 0.4
	      E30 Combo  TDA10023  0f  0.1.2    DVB-T/C combo
	*/

	/* Zarlink MT352 DVB-T demod inside of Samsung DNOS404ZH102A NIM */
	adap->fe = dvb_attach(mt352_attach, &anysee_mt352_config,
			      &adap->dev->i2c_adap);
	if (adap->fe != NULL) {
		state->tuner = DVB_PLL_THOMSON_DTT7579;
		return 0;
	}

	/* Zarlink ZL10353 DVB-T demod inside of Samsung DNOS404ZH103A NIM */
	adap->fe = dvb_attach(zl10353_attach, &anysee_zl10353_config,
			      &adap->dev->i2c_adap);
	if (adap->fe != NULL) {
		state->tuner = DVB_PLL_THOMSON_DTT7579;
		return 0;
	}

	/* for E30 Combo Plus DVB-T demodulator */
	if (dvb_usb_anysee_delsys) {
		ret = anysee_write_reg(adap->dev, 0xb0, 0x01);
		if (ret)
			return ret;

		/* Zarlink ZL10353 DVB-T demod */
		adap->fe = dvb_attach(zl10353_attach, &anysee_zl10353_config,
				      &adap->dev->i2c_adap);
		if (adap->fe != NULL) {
			state->tuner = DVB_PLL_SAMSUNG_DTOS403IH102A;
			return 0;
		}
	}

	/* connect demod on IO port D for TDA10023 & ZL10353 */
	ret = anysee_write_reg(adap->dev, 0xb0, 0x25);
	if (ret)
		return ret;

	/* Zarlink ZL10353 DVB-T demod inside of Samsung DNOS404ZH103A NIM */
	adap->fe = dvb_attach(zl10353_attach, &anysee_zl10353_config,
			      &adap->dev->i2c_adap);
	if (adap->fe != NULL) {
		state->tuner = DVB_PLL_THOMSON_DTT7579;
		return 0;
	}

	/* IO port E - E30C rev 0.4 board requires this */
	ret = anysee_write_reg(adap->dev, 0xb1, 0xa7);
	if (ret)
		return ret;

	/* Philips TDA10023 DVB-C demod */
	adap->fe = dvb_attach(tda10023_attach, &anysee_tda10023_config,
			      &adap->dev->i2c_adap, 0x48);
	if (adap->fe != NULL) {
		state->tuner = DVB_PLL_SAMSUNG_DTOS403IH102A;
		return 0;
	}

	/* return IO port D to init value for safe */
	ret = anysee_write_reg(adap->dev, 0xb0, io_d);
	if (ret)
		return ret;

	err("Unknown Anysee version: %02x %02x %02x. "\
	    "Please report the <linux-dvb@linuxtv.org>.",
	    hw_info[0], hw_info[1], hw_info[2]);

	return -ENODEV;
}

static int anysee_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct anysee_state *state = adap->dev->priv;
	deb_info("%s:\n", __func__);

	switch (state->tuner) {
	case DVB_PLL_THOMSON_DTT7579:
		/* Thomson dtt7579 (not sure) PLL inside of:
		   Samsung DNOS404ZH102A NIM
		   Samsung DNOS404ZH103A NIM */
		dvb_attach(dvb_pll_attach, adap->fe, 0x61,
			   NULL, DVB_PLL_THOMSON_DTT7579);
		break;
	case DVB_PLL_SAMSUNG_DTOS403IH102A:
		/* Unknown PLL inside of Samsung DTOS403IH102A tuner module */
		dvb_attach(dvb_pll_attach, adap->fe, 0xc0,
			   &adap->dev->i2c_adap, DVB_PLL_SAMSUNG_DTOS403IH102A);
		break;
	}

	return 0;
}

static int anysee_rc_query(struct dvb_usb_device *d)
{
	u8 buf[] = {CMD_GET_IR_CODE};
	u8 ircode[2];
	int ret;

	/* Remote controller is basic NEC using address byte 0x08.
	   Anysee device RC query returns only two bytes, status and code,
	   address byte is dropped. Also it does not return any value for
	   NEC RCs having address byte other than 0x08. Due to that, we
	   cannot use that device as standard NEC receiver.
	   It could be possible make hack which reads whole code directly
	   from device memory... */

	ret = anysee_ctrl_msg(d, buf, sizeof(buf), ircode, sizeof(ircode));
	if (ret)
		return ret;

	if (ircode[0]) {
		deb_rc("%s: key pressed %02x\n", __func__, ircode[1]);
		ir_keydown(d->rc_input_dev, 0x08 << 8 | ircode[1], 0);
	}

	return 0;
}

/* DVB USB Driver stuff */
static struct dvb_usb_device_properties anysee_properties;

static int anysee_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	struct dvb_usb_device *d;
	struct usb_host_interface *alt;
	int ret;

	/* There is one interface with two alternate settings.
	   Alternate setting 0 is for bulk transfer.
	   Alternate setting 1 is for isochronous transfer.
	   We use bulk transfer (alternate setting 0). */
	if (intf->num_altsetting < 1)
		return -ENODEV;

	/*
	 * Anysee is always warm (its USB-bridge, Cypress FX2, uploads
	 * firmware from eeprom).  If dvb_usb_device_init() succeeds that
	 * means d is a valid pointer.
	 */
	ret = dvb_usb_device_init(intf, &anysee_properties, THIS_MODULE, &d,
		adapter_nr);
	if (ret)
		return ret;

	alt = usb_altnum_to_altsetting(intf, 0);
	if (alt == NULL) {
		deb_info("%s: no alt found!\n", __func__);
		return -ENODEV;
	}

	ret = usb_set_interface(d->udev, alt->desc.bInterfaceNumber,
		alt->desc.bAlternateSetting);
	if (ret)
		return ret;

	return anysee_init(d);
}

static struct usb_device_id anysee_table[] = {
	{ USB_DEVICE(USB_VID_CYPRESS, USB_PID_ANYSEE) },
	{ USB_DEVICE(USB_VID_AMT,     USB_PID_ANYSEE) },
	{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, anysee_table);

static struct dvb_usb_device_properties anysee_properties = {
	.caps             = DVB_USB_IS_AN_I2C_ADAPTER,

	.usb_ctrl         = DEVICE_SPECIFIC,

	.size_of_priv     = sizeof(struct anysee_state),

	.num_adapters = 1,
	.adapter = {
		{
			.streaming_ctrl   = anysee_streaming_ctrl,
			.frontend_attach  = anysee_frontend_attach,
			.tuner_attach     = anysee_tuner_attach,
			.stream = {
				.type = USB_BULK,
				.count = 8,
				.endpoint = 0x82,
				.u = {
					.bulk = {
						.buffersize = (16*512),
					}
				}
			},
		}
	},

	.rc.core = {
		.rc_codes         = RC_MAP_ANYSEE,
		.protocol         = IR_TYPE_OTHER,
		.module_name      = "anysee",
		.rc_query         = anysee_rc_query,
		.rc_interval      = 250,  /* windows driver uses 500ms */
	},

	.i2c_algo         = &anysee_i2c_algo,

	.generic_bulk_ctrl_endpoint = 1,

	.num_device_descs = 1,
	.devices = {
		{
			.name = "Anysee DVB USB2.0",
			.cold_ids = {NULL},
			.warm_ids = {&anysee_table[0],
				     &anysee_table[1], NULL},
		},
	}
};

static struct usb_driver anysee_driver = {
	.name       = "dvb_usb_anysee",
	.probe      = anysee_probe,
	.disconnect = dvb_usb_device_exit,
	.id_table   = anysee_table,
};

/* module stuff */
static int __init anysee_module_init(void)
{
	int ret;

	ret = usb_register(&anysee_driver);
	if (ret)
		err("%s: usb_register failed. Error number %d", __func__, ret);

	return ret;
}

static void __exit anysee_module_exit(void)
{
	/* deregister this driver from the USB subsystem */
	usb_deregister(&anysee_driver);
}

module_init(anysee_module_init);
module_exit(anysee_module_exit);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Driver Anysee E30 DVB-C & DVB-T USB2.0");
MODULE_LICENSE("GPL");

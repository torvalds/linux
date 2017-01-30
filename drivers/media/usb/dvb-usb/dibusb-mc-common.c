/* Common methods for dibusb-based-receivers.
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */

#include "dibusb.h"

/* 3000MC/P stuff */
// Config Adjacent channels  Perf -cal22
static struct dibx000_agc_config dib3000p_mt2060_agc_config = {
	.band_caps = BAND_VHF | BAND_UHF,
	.setup     = (1 << 8) | (5 << 5) | (1 << 4) | (1 << 3) | (0 << 2) | (2 << 0),

	.agc1_max = 48497,
	.agc1_min = 23593,
	.agc2_max = 46531,
	.agc2_min = 24904,

	.agc1_pt1 = 0x65,
	.agc1_pt2 = 0x69,

	.agc1_slope1 = 0x51,
	.agc1_slope2 = 0x27,

	.agc2_pt1 = 0,
	.agc2_pt2 = 0x33,

	.agc2_slope1 = 0x35,
	.agc2_slope2 = 0x37,
};

static struct dib3000mc_config stk3000p_dib3000p_config = {
	&dib3000p_mt2060_agc_config,

	.max_time     = 0x196,
	.ln_adc_level = 0x1cc7,

	.output_mpeg2_in_188_bytes = 1,

	.agc_command1 = 1,
	.agc_command2 = 1,
};

static struct dibx000_agc_config dib3000p_panasonic_agc_config = {
	.band_caps = BAND_VHF | BAND_UHF,
	.setup     = (1 << 8) | (5 << 5) | (1 << 4) | (1 << 3) | (0 << 2) | (2 << 0),

	.agc1_max = 56361,
	.agc1_min = 22282,
	.agc2_max = 47841,
	.agc2_min = 36045,

	.agc1_pt1 = 0x3b,
	.agc1_pt2 = 0x6b,

	.agc1_slope1 = 0x55,
	.agc1_slope2 = 0x1d,

	.agc2_pt1 = 0,
	.agc2_pt2 = 0x0a,

	.agc2_slope1 = 0x95,
	.agc2_slope2 = 0x1e,
};

static struct dib3000mc_config mod3000p_dib3000p_config = {
	&dib3000p_panasonic_agc_config,

	.max_time     = 0x51,
	.ln_adc_level = 0x1cc7,

	.output_mpeg2_in_188_bytes = 1,

	.agc_command1 = 1,
	.agc_command2 = 1,
};

int dibusb_dib3000mc_frontend_attach(struct dvb_usb_adapter *adap)
{
	if (le16_to_cpu(adap->dev->udev->descriptor.idVendor) == USB_VID_LITEON &&
	    le16_to_cpu(adap->dev->udev->descriptor.idProduct) ==
			USB_PID_LITEON_DVB_T_WARM) {
		msleep(1000);
	}

	adap->fe_adap[0].fe = dvb_attach(dib3000mc_attach,
					 &adap->dev->i2c_adap,
					 DEFAULT_DIB3000P_I2C_ADDRESS,
					 &mod3000p_dib3000p_config);
	if ((adap->fe_adap[0].fe) == NULL)
		adap->fe_adap[0].fe = dvb_attach(dib3000mc_attach,
						 &adap->dev->i2c_adap,
						 DEFAULT_DIB3000MC_I2C_ADDRESS,
						 &mod3000p_dib3000p_config);
	if ((adap->fe_adap[0].fe) != NULL) {
		if (adap->priv != NULL) {
			struct dibusb_state *st = adap->priv;
			st->ops.pid_parse = dib3000mc_pid_parse;
			st->ops.pid_ctrl  = dib3000mc_pid_control;
		}
		return 0;
	}
	return -ENODEV;
}
EXPORT_SYMBOL(dibusb_dib3000mc_frontend_attach);

static struct mt2060_config stk3000p_mt2060_config = {
	0x60
};

int dibusb_dib3000mc_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct dibusb_state *st = adap->priv;
	u8 a,b;
	u16 if1 = 1220;
	struct i2c_adapter *tun_i2c;

	// First IF calibration for Liteon Sticks
	if (le16_to_cpu(adap->dev->udev->descriptor.idVendor) == USB_VID_LITEON &&
	    le16_to_cpu(adap->dev->udev->descriptor.idProduct) == USB_PID_LITEON_DVB_T_WARM) {

		dibusb_read_eeprom_byte(adap->dev,0x7E,&a);
		dibusb_read_eeprom_byte(adap->dev,0x7F,&b);

		if (a == 0x00)
			if1 += b;
		else if (a == 0x80)
			if1 -= b;
		else
			warn("LITE-ON DVB-T: Strange IF1 calibration :%2X %2X\n", a, b);

	} else if (le16_to_cpu(adap->dev->udev->descriptor.idVendor) == USB_VID_DIBCOM &&
		   le16_to_cpu(adap->dev->udev->descriptor.idProduct) == USB_PID_DIBCOM_MOD3001_WARM) {
		u8 desc;
		dibusb_read_eeprom_byte(adap->dev, 7, &desc);
		if (desc == 2) {
			a = 127;
			do {
				dibusb_read_eeprom_byte(adap->dev, a, &desc);
				a--;
			} while (a > 7 && (desc == 0xff || desc == 0x00));
			if (desc & 0x80)
				if1 -= (0xff - desc);
			else
				if1 += desc;
		}
	}

	tun_i2c = dib3000mc_get_tuner_i2c_master(adap->fe_adap[0].fe, 1);
	if (dvb_attach(mt2060_attach, adap->fe_adap[0].fe, tun_i2c, &stk3000p_mt2060_config, if1) == NULL) {
		/* not found - use panasonic pll parameters */
		if (dvb_attach(dvb_pll_attach, adap->fe_adap[0].fe, 0x60, tun_i2c, DVB_PLL_ENV57H1XD5) == NULL)
			return -ENOMEM;
	} else {
		st->mt2060_present = 1;
		/* set the correct parameters for the dib3000p */
		dib3000mc_set_config(adap->fe_adap[0].fe, &stk3000p_dib3000p_config);
	}
	return 0;
}
EXPORT_SYMBOL(dibusb_dib3000mc_tuner_attach);

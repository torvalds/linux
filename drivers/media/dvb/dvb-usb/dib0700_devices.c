/* Linux driver for devices based on the DiBcom DiB0700 USB bridge
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 *  Copyright (C) 2005-7 DiBcom, SA
 */
#include "dib0700.h"

#include "dib3000mc.h"
#include "dib7000m.h"
#include "dib7000p.h"
#include "mt2060.h"
#include "mt2266.h"
#include "tuner-xc2028.h"
#include "dib0070.h"

static int force_lna_activation;
module_param(force_lna_activation, int, 0644);
MODULE_PARM_DESC(force_lna_activation, "force the activation of Low-Noise-Amplifyer(s) (LNA), "
		"if applicable for the device (default: 0=automatic/off).");

struct dib0700_adapter_state {
	int (*set_param_save) (struct dvb_frontend *, struct dvb_frontend_parameters *);
};

/* Hauppauge Nova-T 500 (aka Bristol)
 *  has a LNA on GPIO0 which is enabled by setting 1 */
static struct mt2060_config bristol_mt2060_config[2] = {
	{
		.i2c_address = 0x60,
		.clock_out   = 3,
	}, {
		.i2c_address = 0x61,
	}
};

static struct dibx000_agc_config bristol_dib3000p_mt2060_agc_config = {
	.band_caps = BAND_VHF | BAND_UHF,
	.setup     = (1 << 8) | (5 << 5) | (0 << 4) | (0 << 3) | (0 << 2) | (2 << 0),

	.agc1_max = 42598,
	.agc1_min = 17694,
	.agc2_max = 45875,
	.agc2_min = 0,

	.agc1_pt1 = 0,
	.agc1_pt2 = 59,

	.agc1_slope1 = 0,
	.agc1_slope2 = 69,

	.agc2_pt1 = 0,
	.agc2_pt2 = 59,

	.agc2_slope1 = 111,
	.agc2_slope2 = 28,
};

static struct dib3000mc_config bristol_dib3000mc_config[2] = {
	{	.agc          = &bristol_dib3000p_mt2060_agc_config,
		.max_time     = 0x196,
		.ln_adc_level = 0x1cc7,
		.output_mpeg2_in_188_bytes = 1,
	},
	{	.agc          = &bristol_dib3000p_mt2060_agc_config,
		.max_time     = 0x196,
		.ln_adc_level = 0x1cc7,
		.output_mpeg2_in_188_bytes = 1,
	}
};

static int bristol_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct dib0700_state *st = adap->dev->priv;
	if (adap->id == 0) {
		dib0700_set_gpio(adap->dev, GPIO6,  GPIO_OUT, 0); msleep(10);
		dib0700_set_gpio(adap->dev, GPIO6,  GPIO_OUT, 1); msleep(10);
		dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 0); msleep(10);
		dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 1); msleep(10);

		if (force_lna_activation)
			dib0700_set_gpio(adap->dev, GPIO0, GPIO_OUT, 1);
		else
			dib0700_set_gpio(adap->dev, GPIO0, GPIO_OUT, 0);

		if (dib3000mc_i2c_enumeration(&adap->dev->i2c_adap, 2, DEFAULT_DIB3000P_I2C_ADDRESS, bristol_dib3000mc_config) != 0) {
			dib0700_set_gpio(adap->dev, GPIO6, GPIO_OUT, 0); msleep(10);
			return -ENODEV;
		}
	}
	st->mt2060_if1[adap->id] = 1220;
	return (adap->fe = dvb_attach(dib3000mc_attach, &adap->dev->i2c_adap,
		(10 + adap->id) << 1, &bristol_dib3000mc_config[adap->id])) == NULL ? -ENODEV : 0;
}

static int eeprom_read(struct i2c_adapter *adap,u8 adrs,u8 *pval)
{
	struct i2c_msg msg[2] = {
		{ .addr = 0x50, .flags = 0,        .buf = &adrs, .len = 1 },
		{ .addr = 0x50, .flags = I2C_M_RD, .buf = pval,  .len = 1 },
	};
	if (i2c_transfer(adap, msg, 2) != 2) return -EREMOTEIO;
	return 0;
}

static int bristol_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct i2c_adapter *prim_i2c = &adap->dev->i2c_adap;
	struct i2c_adapter *tun_i2c = dib3000mc_get_tuner_i2c_master(adap->fe, 1);
	s8 a;
	int if1=1220;
	if (adap->dev->udev->descriptor.idVendor  == cpu_to_le16(USB_VID_HAUPPAUGE) &&
		adap->dev->udev->descriptor.idProduct == cpu_to_le16(USB_PID_HAUPPAUGE_NOVA_T_500_2)) {
		if (!eeprom_read(prim_i2c,0x59 + adap->id,&a)) if1=1220+a;
	}
	return dvb_attach(mt2060_attach,adap->fe, tun_i2c,&bristol_mt2060_config[adap->id],
		if1) == NULL ? -ENODEV : 0;
}

/* STK7700D: Pinnacle/Terratec/Hauppauge Dual DVB-T Diversity */

/* MT226x */
static struct dibx000_agc_config stk7700d_7000p_mt2266_agc_config[2] = {
	{
		BAND_UHF, // band_caps

		/* P_agc_use_sd_mod1=0, P_agc_use_sd_mod2=0, P_agc_freq_pwm_div=1, P_agc_inv_pwm1=1, P_agc_inv_pwm2=1,
		* P_agc_inh_dc_rv_est=0, P_agc_time_est=3, P_agc_freeze=0, P_agc_nb_est=2, P_agc_write=0 */
		(0 << 15) | (0 << 14) | (1 << 11) | (1 << 10) | (1 << 9) | (0 << 8) | (3 << 5) | (0 << 4) | (5 << 1) | (0 << 0), // setup

		1130,  // inv_gain
		21,  // time_stabiliz

		0,  // alpha_level
		118,  // thlock

		0,     // wbd_inv
		3530,  // wbd_ref
		1,     // wbd_sel
		0,     // wbd_alpha

		65535,  // agc1_max
		33770,  // agc1_min
		65535,  // agc2_max
		23592,  // agc2_min

		0,    // agc1_pt1
		62,   // agc1_pt2
		255,  // agc1_pt3
		64,   // agc1_slope1
		64,   // agc1_slope2
		132,  // agc2_pt1
		192,  // agc2_pt2
		80,   // agc2_slope1
		80,   // agc2_slope2

		17,  // alpha_mant
		27,  // alpha_exp
		23,  // beta_mant
		51,  // beta_exp

		1,  // perform_agc_softsplit
	}, {
		BAND_VHF | BAND_LBAND, // band_caps

		/* P_agc_use_sd_mod1=0, P_agc_use_sd_mod2=0, P_agc_freq_pwm_div=1, P_agc_inv_pwm1=1, P_agc_inv_pwm2=1,
		* P_agc_inh_dc_rv_est=0, P_agc_time_est=3, P_agc_freeze=0, P_agc_nb_est=2, P_agc_write=0 */
		(0 << 15) | (0 << 14) | (1 << 11) | (1 << 10) | (1 << 9) | (0 << 8) | (3 << 5) | (0 << 4) | (2 << 1) | (0 << 0), // setup

		2372, // inv_gain
		21,   // time_stabiliz

		0,    // alpha_level
		118,  // thlock

		0,    // wbd_inv
		3530, // wbd_ref
		1,     // wbd_sel
		0,    // wbd_alpha

		65535, // agc1_max
		0,     // agc1_min
		65535, // agc2_max
		23592, // agc2_min

		0,    // agc1_pt1
		128,  // agc1_pt2
		128,  // agc1_pt3
		128,  // agc1_slope1
		0,    // agc1_slope2
		128,  // agc2_pt1
		253,  // agc2_pt2
		81,   // agc2_slope1
		0,    // agc2_slope2

		17,  // alpha_mant
		27,  // alpha_exp
		23,  // beta_mant
		51,  // beta_exp

		1,  // perform_agc_softsplit
	}
};

static struct dibx000_bandwidth_config stk7700d_mt2266_pll_config = {
	60000, 30000, // internal, sampling
	1, 8, 3, 1, 0, // pll_cfg: prediv, ratio, range, reset, bypass
	0, 0, 1, 1, 2, // misc: refdiv, bypclk_div, IO_CLK_en_core, ADClkSrc, modulo
	(3 << 14) | (1 << 12) | (524 << 0), // sad_cfg: refsel, sel, freq_15k
	0, // ifreq
	20452225, // timf
};

static struct dib7000p_config stk7700d_dib7000p_mt2266_config[] = {
	{	.output_mpeg2_in_188_bytes = 1,
		.hostbus_diversity = 1,
		.tuner_is_baseband = 1,

		.agc_config_count = 2,
		.agc = stk7700d_7000p_mt2266_agc_config,
		.bw  = &stk7700d_mt2266_pll_config,

		.gpio_dir = DIB7000P_GPIO_DEFAULT_DIRECTIONS,
		.gpio_val = DIB7000P_GPIO_DEFAULT_VALUES,
		.gpio_pwm_pos = DIB7000P_GPIO_DEFAULT_PWM_POS,
	},
	{	.output_mpeg2_in_188_bytes = 1,
		.hostbus_diversity = 1,
		.tuner_is_baseband = 1,

		.agc_config_count = 2,
		.agc = stk7700d_7000p_mt2266_agc_config,
		.bw  = &stk7700d_mt2266_pll_config,

		.gpio_dir = DIB7000P_GPIO_DEFAULT_DIRECTIONS,
		.gpio_val = DIB7000P_GPIO_DEFAULT_VALUES,
		.gpio_pwm_pos = DIB7000P_GPIO_DEFAULT_PWM_POS,
	}
};

static struct mt2266_config stk7700d_mt2266_config[2] = {
	{	.i2c_address = 0x60
	},
	{	.i2c_address = 0x60
	}
};

static int stk7700P2_frontend_attach(struct dvb_usb_adapter *adap)
{
	if (adap->id == 0) {
		dib0700_set_gpio(adap->dev, GPIO6, GPIO_OUT, 1);
		msleep(10);
		dib0700_set_gpio(adap->dev, GPIO9, GPIO_OUT, 1);
		dib0700_set_gpio(adap->dev, GPIO4, GPIO_OUT, 1);
		dib0700_set_gpio(adap->dev, GPIO7, GPIO_OUT, 1);
		dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 0);
		msleep(10);
		dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 1);
		msleep(10);
		dib7000p_i2c_enumeration(&adap->dev->i2c_adap,1,18,stk7700d_dib7000p_mt2266_config);
	}

	adap->fe = dvb_attach(dib7000p_attach, &adap->dev->i2c_adap,0x80+(adap->id << 1),
				&stk7700d_dib7000p_mt2266_config[adap->id]);

	return adap->fe == NULL ? -ENODEV : 0;
}

static int stk7700d_frontend_attach(struct dvb_usb_adapter *adap)
{
	if (adap->id == 0) {
		dib0700_set_gpio(adap->dev, GPIO6, GPIO_OUT, 1);
		msleep(10);
		dib0700_set_gpio(adap->dev, GPIO9, GPIO_OUT, 1);
		dib0700_set_gpio(adap->dev, GPIO4, GPIO_OUT, 1);
		dib0700_set_gpio(adap->dev, GPIO7, GPIO_OUT, 1);
		dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 0);
		msleep(10);
		dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 1);
		msleep(10);
		dib0700_set_gpio(adap->dev, GPIO0, GPIO_OUT, 1);
		dib7000p_i2c_enumeration(&adap->dev->i2c_adap,2,18,stk7700d_dib7000p_mt2266_config);
	}

	adap->fe = dvb_attach(dib7000p_attach, &adap->dev->i2c_adap,0x80+(adap->id << 1),
				&stk7700d_dib7000p_mt2266_config[adap->id]);

	return adap->fe == NULL ? -ENODEV : 0;
}

static int stk7700d_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct i2c_adapter *tun_i2c;
	tun_i2c = dib7000p_get_i2c_master(adap->fe, DIBX000_I2C_INTERFACE_TUNER, 1);
	return dvb_attach(mt2266_attach, adap->fe, tun_i2c,
		&stk7700d_mt2266_config[adap->id]) == NULL ? -ENODEV : 0;;
}

/* STK7700-PH: Digital/Analog Hybrid Tuner, e.h. Cinergy HT USB HE */
static struct dibx000_agc_config xc3028_agc_config = {
	BAND_VHF | BAND_UHF,       /* band_caps */

	/* P_agc_use_sd_mod1=0, P_agc_use_sd_mod2=0, P_agc_freq_pwm_div=0,
	 * P_agc_inv_pwm1=0, P_agc_inv_pwm2=0, P_agc_inh_dc_rv_est=0,
	 * P_agc_time_est=3, P_agc_freeze=0, P_agc_nb_est=2, P_agc_write=0 */
	(0 << 15) | (0 << 14) | (0 << 11) | (0 << 10) | (0 << 9) | (0 << 8) |
	(3 << 5) | (0 << 4) | (2 << 1) | (0 << 0), /* setup */

	712,	/* inv_gain */
	21,	/* time_stabiliz */

	0,	/* alpha_level */
	118,	/* thlock */

	0,	/* wbd_inv */
	2867,	/* wbd_ref */
	0,	/* wbd_sel */
	2,	/* wbd_alpha */

	0,	/* agc1_max */
	0,	/* agc1_min */
	39718,	/* agc2_max */
	9930,	/* agc2_min */
	0,	/* agc1_pt1 */
	0,	/* agc1_pt2 */
	0,	/* agc1_pt3 */
	0,	/* agc1_slope1 */
	0,	/* agc1_slope2 */
	0,	/* agc2_pt1 */
	128,	/* agc2_pt2 */
	29,	/* agc2_slope1 */
	29,	/* agc2_slope2 */

	17,	/* alpha_mant */
	27,	/* alpha_exp */
	23,	/* beta_mant */
	51,	/* beta_exp */

	1,	/* perform_agc_softsplit */
};

/* PLL Configuration for COFDM BW_MHz = 8.00 with external clock = 30.00 */
static struct dibx000_bandwidth_config xc3028_bw_config = {
	60000, 30000, /* internal, sampling */
	1, 8, 3, 1, 0, /* pll_cfg: prediv, ratio, range, reset, bypass */
	0, 0, 1, 1, 0, /* misc: refdiv, bypclk_div, IO_CLK_en_core, ADClkSrc,
			  modulo */
	(3 << 14) | (1 << 12) | (524 << 0), /* sad_cfg: refsel, sel, freq_15k */
	(1 << 25) | 5816102, /* ifreq = 5.200000 MHz */
	20452225, /* timf */
	30000000, /* xtal_hz */
};

static struct dib7000p_config stk7700ph_dib7700_xc3028_config = {
	.output_mpeg2_in_188_bytes = 1,
	.tuner_is_baseband = 1,

	.agc_config_count = 1,
	.agc = &xc3028_agc_config,
	.bw  = &xc3028_bw_config,

	.gpio_dir = DIB7000P_GPIO_DEFAULT_DIRECTIONS,
	.gpio_val = DIB7000P_GPIO_DEFAULT_VALUES,
	.gpio_pwm_pos = DIB7000P_GPIO_DEFAULT_PWM_POS,
};

static int stk7700ph_xc3028_callback(void *ptr, int command, int arg)
{
	struct dvb_usb_adapter *adap = ptr;

	switch (command) {
	case XC2028_TUNER_RESET:
		/* Send the tuner in then out of reset */
		dib7000p_set_gpio(adap->fe, 8, 0, 0); msleep(10);
		dib7000p_set_gpio(adap->fe, 8, 0, 1);
		break;
	case XC2028_RESET_CLK:
		break;
	default:
		err("%s: unknown command %d, arg %d\n", __func__,
			command, arg);
		return -EINVAL;
	}
	return 0;
}

static struct xc2028_ctrl stk7700ph_xc3028_ctrl = {
	.fname = XC2028_DEFAULT_FIRMWARE,
	.max_len = 64,
	.demod = XC3028_FE_DIBCOM52,
};

static struct xc2028_config stk7700ph_xc3028_config = {
	.i2c_addr = 0x61,
	.callback = stk7700ph_xc3028_callback,
	.ctrl = &stk7700ph_xc3028_ctrl,
};

static int stk7700ph_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct usb_device_descriptor *desc = &adap->dev->udev->descriptor;

	if (desc->idVendor  == cpu_to_le16(USB_VID_PINNACLE) &&
	    desc->idProduct == cpu_to_le16(USB_PID_PINNACLE_EXPRESSCARD_320CX))
	dib0700_set_gpio(adap->dev, GPIO6, GPIO_OUT, 0);
	else
	dib0700_set_gpio(adap->dev, GPIO6, GPIO_OUT, 1);
	msleep(20);
	dib0700_set_gpio(adap->dev, GPIO9, GPIO_OUT, 1);
	dib0700_set_gpio(adap->dev, GPIO4, GPIO_OUT, 1);
	dib0700_set_gpio(adap->dev, GPIO7, GPIO_OUT, 1);
	dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 0);
	msleep(10);
	dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 1);
	msleep(20);
	dib0700_set_gpio(adap->dev, GPIO0, GPIO_OUT, 1);
	msleep(10);

	dib7000p_i2c_enumeration(&adap->dev->i2c_adap, 1, 18,
		&stk7700ph_dib7700_xc3028_config);

	adap->fe = dvb_attach(dib7000p_attach, &adap->dev->i2c_adap, 0x80,
		&stk7700ph_dib7700_xc3028_config);

	return adap->fe == NULL ? -ENODEV : 0;
}

static int stk7700ph_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct i2c_adapter *tun_i2c;

	tun_i2c = dib7000p_get_i2c_master(adap->fe,
		DIBX000_I2C_INTERFACE_TUNER, 1);

	stk7700ph_xc3028_config.i2c_adap = tun_i2c;
	stk7700ph_xc3028_config.video_dev = adap;

	return dvb_attach(xc2028_attach, adap->fe, &stk7700ph_xc3028_config)
		== NULL ? -ENODEV : 0;
}

#define DEFAULT_RC_INTERVAL 150

static u8 rc_request[] = { REQUEST_POLL_RC, 0 };

/* Number of keypresses to ignore before start repeating */
#define RC_REPEAT_DELAY 2

static int dib0700_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{
	u8 key[4];
	int i;
	struct dvb_usb_rc_key *keymap = d->props.rc_key_map;
	struct dib0700_state *st = d->priv;
	*event = 0;
	*state = REMOTE_NO_KEY_PRESSED;
	i=dib0700_ctrl_rd(d,rc_request,2,key,4);
	if (i<=0) {
		err("RC Query Failed");
		return -1;
	}

	/* losing half of KEY_0 events from Philipps rc5 remotes.. */
	if (key[0]==0 && key[1]==0 && key[2]==0 && key[3]==0) return 0;

	/* info("%d: %2X %2X %2X %2X",dvb_usb_dib0700_ir_proto,(int)key[3-2],(int)key[3-3],(int)key[3-1],(int)key[3]);  */

	dib0700_rc_setup(d); /* reset ir sensor data to prevent false events */

	switch (dvb_usb_dib0700_ir_proto) {
	case 0: {
		/* NEC protocol sends repeat code as 0 0 0 FF */
		if ((key[3-2] == 0x00) && (key[3-3] == 0x00) &&
		    (key[3] == 0xFF)) {
			st->rc_counter++;
			if (st->rc_counter > RC_REPEAT_DELAY) {
				*event = d->last_event;
				*state = REMOTE_KEY_PRESSED;
				st->rc_counter = RC_REPEAT_DELAY;
			}
			return 0;
		}
		for (i=0;i<d->props.rc_key_map_size; i++) {
			if (keymap[i].custom == key[3-2] && keymap[i].data == key[3-3]) {
				st->rc_counter = 0;
				*event = keymap[i].event;
				*state = REMOTE_KEY_PRESSED;
				d->last_event = keymap[i].event;
				return 0;
			}
		}
		break;
	}
	default: {
		/* RC-5 protocol changes toggle bit on new keypress */
		for (i = 0; i < d->props.rc_key_map_size; i++) {
			if (keymap[i].custom == key[3-2] && keymap[i].data == key[3-3]) {
				if (d->last_event == keymap[i].event &&
					key[3-1] == st->rc_toggle) {
					st->rc_counter++;
					/* prevents unwanted double hits */
					if (st->rc_counter > RC_REPEAT_DELAY) {
						*event = d->last_event;
						*state = REMOTE_KEY_PRESSED;
						st->rc_counter = RC_REPEAT_DELAY;
					}

					return 0;
				}
				st->rc_counter = 0;
				*event = keymap[i].event;
				*state = REMOTE_KEY_PRESSED;
				st->rc_toggle = key[3-1];
				d->last_event = keymap[i].event;
				return 0;
			}
		}
		break;
	}
	}
	err("Unknown remote controller key: %2X %2X %2X %2X", (int) key[3-2], (int) key[3-3], (int) key[3-1], (int) key[3]);
	d->last_event = 0;
	return 0;
}

static struct dvb_usb_rc_key dib0700_rc_keys[] = {
	/* Key codes for the tiny Pinnacle remote*/
	{ 0x07, 0x00, KEY_MUTE },
	{ 0x07, 0x01, KEY_MENU }, // Pinnacle logo
	{ 0x07, 0x39, KEY_POWER },
	{ 0x07, 0x03, KEY_VOLUMEUP },
	{ 0x07, 0x09, KEY_VOLUMEDOWN },
	{ 0x07, 0x06, KEY_CHANNELUP },
	{ 0x07, 0x0c, KEY_CHANNELDOWN },
	{ 0x07, 0x0f, KEY_1 },
	{ 0x07, 0x15, KEY_2 },
	{ 0x07, 0x10, KEY_3 },
	{ 0x07, 0x18, KEY_4 },
	{ 0x07, 0x1b, KEY_5 },
	{ 0x07, 0x1e, KEY_6 },
	{ 0x07, 0x11, KEY_7 },
	{ 0x07, 0x21, KEY_8 },
	{ 0x07, 0x12, KEY_9 },
	{ 0x07, 0x27, KEY_0 },
	{ 0x07, 0x24, KEY_SCREEN }, // 'Square' key
	{ 0x07, 0x2a, KEY_TEXT },   // 'T' key
	{ 0x07, 0x2d, KEY_REWIND },
	{ 0x07, 0x30, KEY_PLAY },
	{ 0x07, 0x33, KEY_FASTFORWARD },
	{ 0x07, 0x36, KEY_RECORD },
	{ 0x07, 0x3c, KEY_STOP },
	{ 0x07, 0x3f, KEY_CANCEL }, // '?' key
	/* Key codes for the Terratec Cinergy DT XS Diversity, similar to cinergyT2.c */
	{ 0xeb, 0x01, KEY_POWER },
	{ 0xeb, 0x02, KEY_1 },
	{ 0xeb, 0x03, KEY_2 },
	{ 0xeb, 0x04, KEY_3 },
	{ 0xeb, 0x05, KEY_4 },
	{ 0xeb, 0x06, KEY_5 },
	{ 0xeb, 0x07, KEY_6 },
	{ 0xeb, 0x08, KEY_7 },
	{ 0xeb, 0x09, KEY_8 },
	{ 0xeb, 0x0a, KEY_9 },
	{ 0xeb, 0x0b, KEY_VIDEO },
	{ 0xeb, 0x0c, KEY_0 },
	{ 0xeb, 0x0d, KEY_REFRESH },
	{ 0xeb, 0x0f, KEY_EPG },
	{ 0xeb, 0x10, KEY_UP },
	{ 0xeb, 0x11, KEY_LEFT },
	{ 0xeb, 0x12, KEY_OK },
	{ 0xeb, 0x13, KEY_RIGHT },
	{ 0xeb, 0x14, KEY_DOWN },
	{ 0xeb, 0x16, KEY_INFO },
	{ 0xeb, 0x17, KEY_RED },
	{ 0xeb, 0x18, KEY_GREEN },
	{ 0xeb, 0x19, KEY_YELLOW },
	{ 0xeb, 0x1a, KEY_BLUE },
	{ 0xeb, 0x1b, KEY_CHANNELUP },
	{ 0xeb, 0x1c, KEY_VOLUMEUP },
	{ 0xeb, 0x1d, KEY_MUTE },
	{ 0xeb, 0x1e, KEY_VOLUMEDOWN },
	{ 0xeb, 0x1f, KEY_CHANNELDOWN },
	{ 0xeb, 0x40, KEY_PAUSE },
	{ 0xeb, 0x41, KEY_HOME },
	{ 0xeb, 0x42, KEY_MENU }, /* DVD Menu */
	{ 0xeb, 0x43, KEY_SUBTITLE },
	{ 0xeb, 0x44, KEY_TEXT }, /* Teletext */
	{ 0xeb, 0x45, KEY_DELETE },
	{ 0xeb, 0x46, KEY_TV },
	{ 0xeb, 0x47, KEY_DVD },
	{ 0xeb, 0x48, KEY_STOP },
	{ 0xeb, 0x49, KEY_VIDEO },
	{ 0xeb, 0x4a, KEY_AUDIO }, /* Music */
	{ 0xeb, 0x4b, KEY_SCREEN }, /* Pic */
	{ 0xeb, 0x4c, KEY_PLAY },
	{ 0xeb, 0x4d, KEY_BACK },
	{ 0xeb, 0x4e, KEY_REWIND },
	{ 0xeb, 0x4f, KEY_FASTFORWARD },
	{ 0xeb, 0x54, KEY_PREVIOUS },
	{ 0xeb, 0x58, KEY_RECORD },
	{ 0xeb, 0x5c, KEY_NEXT },

	/* Key codes for the Haupauge WinTV Nova-TD, copied from nova-t-usb2.c (Nova-T USB2) */
	{ 0x1e, 0x00, KEY_0 },
	{ 0x1e, 0x01, KEY_1 },
	{ 0x1e, 0x02, KEY_2 },
	{ 0x1e, 0x03, KEY_3 },
	{ 0x1e, 0x04, KEY_4 },
	{ 0x1e, 0x05, KEY_5 },
	{ 0x1e, 0x06, KEY_6 },
	{ 0x1e, 0x07, KEY_7 },
	{ 0x1e, 0x08, KEY_8 },
	{ 0x1e, 0x09, KEY_9 },
	{ 0x1e, 0x0a, KEY_KPASTERISK },
	{ 0x1e, 0x0b, KEY_RED },
	{ 0x1e, 0x0c, KEY_RADIO },
	{ 0x1e, 0x0d, KEY_MENU },
	{ 0x1e, 0x0e, KEY_GRAVE }, /* # */
	{ 0x1e, 0x0f, KEY_MUTE },
	{ 0x1e, 0x10, KEY_VOLUMEUP },
	{ 0x1e, 0x11, KEY_VOLUMEDOWN },
	{ 0x1e, 0x12, KEY_CHANNEL },
	{ 0x1e, 0x14, KEY_UP },
	{ 0x1e, 0x15, KEY_DOWN },
	{ 0x1e, 0x16, KEY_LEFT },
	{ 0x1e, 0x17, KEY_RIGHT },
	{ 0x1e, 0x18, KEY_VIDEO },
	{ 0x1e, 0x19, KEY_AUDIO },
	{ 0x1e, 0x1a, KEY_MEDIA },
	{ 0x1e, 0x1b, KEY_EPG },
	{ 0x1e, 0x1c, KEY_TV },
	{ 0x1e, 0x1e, KEY_NEXT },
	{ 0x1e, 0x1f, KEY_BACK },
	{ 0x1e, 0x20, KEY_CHANNELUP },
	{ 0x1e, 0x21, KEY_CHANNELDOWN },
	{ 0x1e, 0x24, KEY_LAST }, /* Skip backwards */
	{ 0x1e, 0x25, KEY_OK },
	{ 0x1e, 0x29, KEY_BLUE},
	{ 0x1e, 0x2e, KEY_GREEN },
	{ 0x1e, 0x30, KEY_PAUSE },
	{ 0x1e, 0x32, KEY_REWIND },
	{ 0x1e, 0x34, KEY_FASTFORWARD },
	{ 0x1e, 0x35, KEY_PLAY },
	{ 0x1e, 0x36, KEY_STOP },
	{ 0x1e, 0x37, KEY_RECORD },
	{ 0x1e, 0x38, KEY_YELLOW },
	{ 0x1e, 0x3b, KEY_GOTO },
	{ 0x1e, 0x3d, KEY_POWER },

	/* Key codes for the Leadtek Winfast DTV Dongle */
	{ 0x00, 0x42, KEY_POWER },
	{ 0x07, 0x7c, KEY_TUNER },
	{ 0x0f, 0x4e, KEY_PRINT }, /* PREVIEW */
	{ 0x08, 0x40, KEY_SCREEN }, /* full screen toggle*/
	{ 0x0f, 0x71, KEY_DOT }, /* frequency */
	{ 0x07, 0x43, KEY_0 },
	{ 0x0c, 0x41, KEY_1 },
	{ 0x04, 0x43, KEY_2 },
	{ 0x0b, 0x7f, KEY_3 },
	{ 0x0e, 0x41, KEY_4 },
	{ 0x06, 0x43, KEY_5 },
	{ 0x09, 0x7f, KEY_6 },
	{ 0x0d, 0x7e, KEY_7 },
	{ 0x05, 0x7c, KEY_8 },
	{ 0x0a, 0x40, KEY_9 },
	{ 0x0e, 0x4e, KEY_CLEAR },
	{ 0x04, 0x7c, KEY_CHANNEL }, /* show channel number */
	{ 0x0f, 0x41, KEY_LAST }, /* recall */
	{ 0x03, 0x42, KEY_MUTE },
	{ 0x06, 0x4c, KEY_RESERVED }, /* PIP button*/
	{ 0x01, 0x72, KEY_SHUFFLE }, /* SNAPSHOT */
	{ 0x0c, 0x4e, KEY_PLAYPAUSE }, /* TIMESHIFT */
	{ 0x0b, 0x70, KEY_RECORD },
	{ 0x03, 0x7d, KEY_VOLUMEUP },
	{ 0x01, 0x7d, KEY_VOLUMEDOWN },
	{ 0x02, 0x42, KEY_CHANNELUP },
	{ 0x00, 0x7d, KEY_CHANNELDOWN },
};

/* STK7700P: Hauppauge Nova-T Stick, AVerMedia Volar */
static struct dibx000_agc_config stk7700p_7000m_mt2060_agc_config = {
	BAND_UHF | BAND_VHF,       // band_caps

	/* P_agc_use_sd_mod1=0, P_agc_use_sd_mod2=0, P_agc_freq_pwm_div=5, P_agc_inv_pwm1=0, P_agc_inv_pwm2=0,
	 * P_agc_inh_dc_rv_est=0, P_agc_time_est=3, P_agc_freeze=0, P_agc_nb_est=2, P_agc_write=0 */
	(0 << 15) | (0 << 14) | (5 << 11) | (0 << 10) | (0 << 9) | (0 << 8) | (3 << 5) | (0 << 4) | (2 << 1) | (0 << 0), // setup

	712,  // inv_gain
	41,  // time_stabiliz

	0,  // alpha_level
	118,  // thlock

	0,     // wbd_inv
	4095,  // wbd_ref
	0,     // wbd_sel
	0,     // wbd_alpha

	42598,  // agc1_max
	17694,  // agc1_min
	45875,  // agc2_max
	2621,  // agc2_min
	0,  // agc1_pt1
	76,  // agc1_pt2
	139,  // agc1_pt3
	52,  // agc1_slope1
	59,  // agc1_slope2
	107,  // agc2_pt1
	172,  // agc2_pt2
	57,  // agc2_slope1
	70,  // agc2_slope2

	21,  // alpha_mant
	25,  // alpha_exp
	28,  // beta_mant
	48,  // beta_exp

	1,  // perform_agc_softsplit
	{  0,     // split_min
	   107,   // split_max
	   51800, // global_split_min
	   24700  // global_split_max
	},
};

static struct dibx000_agc_config stk7700p_7000p_mt2060_agc_config = {
	BAND_UHF | BAND_VHF,

	/* P_agc_use_sd_mod1=0, P_agc_use_sd_mod2=0, P_agc_freq_pwm_div=5, P_agc_inv_pwm1=0, P_agc_inv_pwm2=0,
	 * P_agc_inh_dc_rv_est=0, P_agc_time_est=3, P_agc_freeze=0, P_agc_nb_est=2, P_agc_write=0 */
	(0 << 15) | (0 << 14) | (5 << 11) | (0 << 10) | (0 << 9) | (0 << 8) | (3 << 5) | (0 << 4) | (2 << 1) | (0 << 0), // setup

	712, // inv_gain
	41,  // time_stabiliz

	0,   // alpha_level
	118, // thlock

	0,    // wbd_inv
	4095, // wbd_ref
	0,    // wbd_sel
	0,    // wbd_alpha

	42598, // agc1_max
	16384, // agc1_min
	42598, // agc2_max
	    0, // agc2_min

	  0,   // agc1_pt1
	137,   // agc1_pt2
	255,   // agc1_pt3

	  0,   // agc1_slope1
	255,   // agc1_slope2

	0,     // agc2_pt1
	0,     // agc2_pt2

	 0,    // agc2_slope1
	41,    // agc2_slope2

	15, // alpha_mant
	25, // alpha_exp

	28, // beta_mant
	48, // beta_exp

	0, // perform_agc_softsplit
};

static struct dibx000_bandwidth_config stk7700p_pll_config = {
	60000, 30000, // internal, sampling
	1, 8, 3, 1, 0, // pll_cfg: prediv, ratio, range, reset, bypass
	0, 0, 1, 1, 0, // misc: refdiv, bypclk_div, IO_CLK_en_core, ADClkSrc, modulo
	(3 << 14) | (1 << 12) | (524 << 0), // sad_cfg: refsel, sel, freq_15k
	60258167, // ifreq
	20452225, // timf
	30000000, // xtal
};

static struct dib7000m_config stk7700p_dib7000m_config = {
	.dvbt_mode = 1,
	.output_mpeg2_in_188_bytes = 1,
	.quartz_direct = 1,

	.agc_config_count = 1,
	.agc = &stk7700p_7000m_mt2060_agc_config,
	.bw  = &stk7700p_pll_config,

	.gpio_dir = DIB7000M_GPIO_DEFAULT_DIRECTIONS,
	.gpio_val = DIB7000M_GPIO_DEFAULT_VALUES,
	.gpio_pwm_pos = DIB7000M_GPIO_DEFAULT_PWM_POS,
};

static struct dib7000p_config stk7700p_dib7000p_config = {
	.output_mpeg2_in_188_bytes = 1,

	.agc_config_count = 1,
	.agc = &stk7700p_7000p_mt2060_agc_config,
	.bw  = &stk7700p_pll_config,

	.gpio_dir = DIB7000M_GPIO_DEFAULT_DIRECTIONS,
	.gpio_val = DIB7000M_GPIO_DEFAULT_VALUES,
	.gpio_pwm_pos = DIB7000M_GPIO_DEFAULT_PWM_POS,
};

static int stk7700p_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct dib0700_state *st = adap->dev->priv;
	/* unless there is no real power management in DVB - we leave the device on GPIO6 */

	dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 0);
	dib0700_set_gpio(adap->dev, GPIO6,  GPIO_OUT, 0); msleep(50);

	dib0700_set_gpio(adap->dev, GPIO6,  GPIO_OUT, 1); msleep(10);
	dib0700_set_gpio(adap->dev, GPIO9,  GPIO_OUT, 1);

	dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 0); msleep(10);
	dib0700_ctrl_clock(adap->dev, 72, 1);
	dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 1); msleep(100);

	dib0700_set_gpio(adap->dev,  GPIO0, GPIO_OUT, 1);

	st->mt2060_if1[0] = 1220;

	if (dib7000pc_detection(&adap->dev->i2c_adap)) {
		adap->fe = dvb_attach(dib7000p_attach, &adap->dev->i2c_adap, 18, &stk7700p_dib7000p_config);
		st->is_dib7000pc = 1;
	} else
		adap->fe = dvb_attach(dib7000m_attach, &adap->dev->i2c_adap, 18, &stk7700p_dib7000m_config);

	return adap->fe == NULL ? -ENODEV : 0;
}

static struct mt2060_config stk7700p_mt2060_config = {
	0x60
};

static int stk7700p_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct i2c_adapter *prim_i2c = &adap->dev->i2c_adap;
	struct dib0700_state *st = adap->dev->priv;
	struct i2c_adapter *tun_i2c;
	s8 a;
	int if1=1220;
	if (adap->dev->udev->descriptor.idVendor  == cpu_to_le16(USB_VID_HAUPPAUGE) &&
		adap->dev->udev->descriptor.idProduct == cpu_to_le16(USB_PID_HAUPPAUGE_NOVA_T_STICK)) {
		if (!eeprom_read(prim_i2c,0x58,&a)) if1=1220+a;
	}
	if (st->is_dib7000pc)
		tun_i2c = dib7000p_get_i2c_master(adap->fe, DIBX000_I2C_INTERFACE_TUNER, 1);
	else
		tun_i2c = dib7000m_get_i2c_master(adap->fe, DIBX000_I2C_INTERFACE_TUNER, 1);

	return dvb_attach(mt2060_attach, adap->fe, tun_i2c, &stk7700p_mt2060_config,
		if1) == NULL ? -ENODEV : 0;
}

/* DIB7070 generic */
static struct dibx000_agc_config dib7070_agc_config = {
	BAND_UHF | BAND_VHF | BAND_LBAND | BAND_SBAND,
	/* P_agc_use_sd_mod1=0, P_agc_use_sd_mod2=0, P_agc_freq_pwm_div=5, P_agc_inv_pwm1=0, P_agc_inv_pwm2=0,
	 * P_agc_inh_dc_rv_est=0, P_agc_time_est=3, P_agc_freeze=0, P_agc_nb_est=5, P_agc_write=0 */
	(0 << 15) | (0 << 14) | (5 << 11) | (0 << 10) | (0 << 9) | (0 << 8) | (3 << 5) | (0 << 4) | (5 << 1) | (0 << 0), // setup

	600, // inv_gain
	10,  // time_stabiliz

	0,  // alpha_level
	118,  // thlock

	0,     // wbd_inv
	3530,  // wbd_ref
	1,     // wbd_sel
	5,     // wbd_alpha

	65535,  // agc1_max
		0,  // agc1_min

	65535,  // agc2_max
	0,      // agc2_min

	0,      // agc1_pt1
	40,     // agc1_pt2
	183,    // agc1_pt3
	206,    // agc1_slope1
	255,    // agc1_slope2
	72,     // agc2_pt1
	152,    // agc2_pt2
	88,     // agc2_slope1
	90,     // agc2_slope2

	17,  // alpha_mant
	27,  // alpha_exp
	23,  // beta_mant
	51,  // beta_exp

	0,  // perform_agc_softsplit
};

static int dib7070_tuner_reset(struct dvb_frontend *fe, int onoff)
{
	return dib7000p_set_gpio(fe, 8, 0, !onoff);
}

static int dib7070_tuner_sleep(struct dvb_frontend *fe, int onoff)
{
	return dib7000p_set_gpio(fe, 9, 0, onoff);
}

static struct dib0070_config dib7070p_dib0070_config[2] = {
	{
		.i2c_address = DEFAULT_DIB0070_I2C_ADDRESS,
		.reset = dib7070_tuner_reset,
		.sleep = dib7070_tuner_sleep,
		.clock_khz = 12000,
		.clock_pad_drive = 4
	}, {
		.i2c_address = DEFAULT_DIB0070_I2C_ADDRESS,
		.reset = dib7070_tuner_reset,
		.sleep = dib7070_tuner_sleep,
		.clock_khz = 12000,

	}
};

static int dib7070_set_param_override(struct dvb_frontend *fe, struct dvb_frontend_parameters *fep)
{
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	struct dib0700_adapter_state *state = adap->priv;

	u16 offset;
	u8 band = BAND_OF_FREQUENCY(fep->frequency/1000);
	switch (band) {
		case BAND_VHF: offset = 950; break;
		case BAND_UHF:
		default: offset = 550; break;
	}
	deb_info("WBD for DiB7000P: %d\n", offset + dib0070_wbd_offset(fe));
	dib7000p_set_wbd_ref(fe, offset + dib0070_wbd_offset(fe));
	return state->set_param_save(fe, fep);
}

static int dib7070p_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct dib0700_adapter_state *st = adap->priv;
	struct i2c_adapter *tun_i2c = dib7000p_get_i2c_master(adap->fe, DIBX000_I2C_INTERFACE_TUNER, 1);

	if (adap->id == 0) {
		if (dvb_attach(dib0070_attach, adap->fe, tun_i2c, &dib7070p_dib0070_config[0]) == NULL)
			return -ENODEV;
	} else {
		if (dvb_attach(dib0070_attach, adap->fe, tun_i2c, &dib7070p_dib0070_config[1]) == NULL)
			return -ENODEV;
	}

	st->set_param_save = adap->fe->ops.tuner_ops.set_params;
	adap->fe->ops.tuner_ops.set_params = dib7070_set_param_override;
	return 0;
}

static struct dibx000_bandwidth_config dib7070_bw_config_12_mhz = {
	60000, 15000, // internal, sampling
	1, 20, 3, 1, 0, // pll_cfg: prediv, ratio, range, reset, bypass
	0, 0, 1, 1, 2, // misc: refdiv, bypclk_div, IO_CLK_en_core, ADClkSrc, modulo
	(3 << 14) | (1 << 12) | (524 << 0), // sad_cfg: refsel, sel, freq_15k
	(0 << 25) | 0, // ifreq = 0.000000 MHz
	20452225, // timf
	12000000, // xtal_hz
};

static struct dib7000p_config dib7070p_dib7000p_config = {
	.output_mpeg2_in_188_bytes = 1,

	.agc_config_count = 1,
	.agc = &dib7070_agc_config,
	.bw  = &dib7070_bw_config_12_mhz,
	.tuner_is_baseband = 1,
	.spur_protect = 1,

	.gpio_dir = DIB7000P_GPIO_DEFAULT_DIRECTIONS,
	.gpio_val = DIB7000P_GPIO_DEFAULT_VALUES,
	.gpio_pwm_pos = DIB7000P_GPIO_DEFAULT_PWM_POS,

	.hostbus_diversity = 1,
};

/* STK7070P */
static int stk7070p_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct usb_device_descriptor *p = &adap->dev->udev->descriptor;
	if (p->idVendor  == cpu_to_le16(USB_VID_PINNACLE) &&
	    p->idProduct == cpu_to_le16(USB_PID_PINNACLE_PCTV72E))
		dib0700_set_gpio(adap->dev, GPIO6, GPIO_OUT, 0);
	else
		dib0700_set_gpio(adap->dev, GPIO6, GPIO_OUT, 1);
	msleep(10);
	dib0700_set_gpio(adap->dev, GPIO9, GPIO_OUT, 1);
	dib0700_set_gpio(adap->dev, GPIO4, GPIO_OUT, 1);
	dib0700_set_gpio(adap->dev, GPIO7, GPIO_OUT, 1);
	dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 0);

	dib0700_ctrl_clock(adap->dev, 72, 1);

	msleep(10);
	dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 1);
	msleep(10);
	dib0700_set_gpio(adap->dev, GPIO0, GPIO_OUT, 1);

	dib7000p_i2c_enumeration(&adap->dev->i2c_adap, 1, 18,
		&dib7070p_dib7000p_config);

	adap->fe = dvb_attach(dib7000p_attach, &adap->dev->i2c_adap, 0x80,
		&dib7070p_dib7000p_config);
	return adap->fe == NULL ? -ENODEV : 0;
}

/* STK7070PD */
static struct dib7000p_config stk7070pd_dib7000p_config[2] = {
	{
		.output_mpeg2_in_188_bytes = 1,

		.agc_config_count = 1,
		.agc = &dib7070_agc_config,
		.bw  = &dib7070_bw_config_12_mhz,
		.tuner_is_baseband = 1,
		.spur_protect = 1,

		.gpio_dir = DIB7000P_GPIO_DEFAULT_DIRECTIONS,
		.gpio_val = DIB7000P_GPIO_DEFAULT_VALUES,
		.gpio_pwm_pos = DIB7000P_GPIO_DEFAULT_PWM_POS,

		.hostbus_diversity = 1,
	}, {
		.output_mpeg2_in_188_bytes = 1,

		.agc_config_count = 1,
		.agc = &dib7070_agc_config,
		.bw  = &dib7070_bw_config_12_mhz,
		.tuner_is_baseband = 1,
		.spur_protect = 1,

		.gpio_dir = DIB7000P_GPIO_DEFAULT_DIRECTIONS,
		.gpio_val = DIB7000P_GPIO_DEFAULT_VALUES,
		.gpio_pwm_pos = DIB7000P_GPIO_DEFAULT_PWM_POS,

		.hostbus_diversity = 1,
	}
};

static int stk7070pd_frontend_attach0(struct dvb_usb_adapter *adap)
{
	dib0700_set_gpio(adap->dev, GPIO6, GPIO_OUT, 1);
	msleep(10);
	dib0700_set_gpio(adap->dev, GPIO9, GPIO_OUT, 1);
	dib0700_set_gpio(adap->dev, GPIO4, GPIO_OUT, 1);
	dib0700_set_gpio(adap->dev, GPIO7, GPIO_OUT, 1);
	dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 0);

	dib0700_ctrl_clock(adap->dev, 72, 1);

	msleep(10);
	dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 1);
	msleep(10);
	dib0700_set_gpio(adap->dev, GPIO0, GPIO_OUT, 1);

	dib7000p_i2c_enumeration(&adap->dev->i2c_adap, 2, 18, stk7070pd_dib7000p_config);

	adap->fe = dvb_attach(dib7000p_attach, &adap->dev->i2c_adap, 0x80, &stk7070pd_dib7000p_config[0]);
	return adap->fe == NULL ? -ENODEV : 0;
}

static int stk7070pd_frontend_attach1(struct dvb_usb_adapter *adap)
{
	adap->fe = dvb_attach(dib7000p_attach, &adap->dev->i2c_adap, 0x82, &stk7070pd_dib7000p_config[1]);
	return adap->fe == NULL ? -ENODEV : 0;
}

/* DVB-USB and USB stuff follows */
struct usb_device_id dib0700_usb_id_table[] = {
/* 0 */	{ USB_DEVICE(USB_VID_DIBCOM,    USB_PID_DIBCOM_STK7700P) },
	{ USB_DEVICE(USB_VID_DIBCOM,    USB_PID_DIBCOM_STK7700P_PC) },
	{ USB_DEVICE(USB_VID_HAUPPAUGE, USB_PID_HAUPPAUGE_NOVA_T_500) },
	{ USB_DEVICE(USB_VID_HAUPPAUGE, USB_PID_HAUPPAUGE_NOVA_T_500_2) },
	{ USB_DEVICE(USB_VID_HAUPPAUGE, USB_PID_HAUPPAUGE_NOVA_T_STICK) },
/* 5 */	{ USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_VOLAR) },
	{ USB_DEVICE(USB_VID_COMPRO,    USB_PID_COMPRO_VIDEOMATE_U500) },
	{ USB_DEVICE(USB_VID_UNIWILL,   USB_PID_UNIWILL_STK7700P) },
	{ USB_DEVICE(USB_VID_LEADTEK,   USB_PID_WINFAST_DTV_DONGLE_STK7700P) },
	{ USB_DEVICE(USB_VID_HAUPPAUGE, USB_PID_HAUPPAUGE_NOVA_T_STICK_2) },
/* 10 */{ USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_VOLAR_2) },
	{ USB_DEVICE(USB_VID_PINNACLE,  USB_PID_PINNACLE_PCTV2000E) },
	{ USB_DEVICE(USB_VID_TERRATEC,
			USB_PID_TERRATEC_CINERGY_DT_XS_DIVERSITY) },
	{ USB_DEVICE(USB_VID_HAUPPAUGE, USB_PID_HAUPPAUGE_NOVA_TD_STICK) },
	{ USB_DEVICE(USB_VID_DIBCOM,    USB_PID_DIBCOM_STK7700D) },
/* 15 */{ USB_DEVICE(USB_VID_DIBCOM,    USB_PID_DIBCOM_STK7070P) },
	{ USB_DEVICE(USB_VID_PINNACLE,  USB_PID_PINNACLE_PCTV_DVB_T_FLASH) },
	{ USB_DEVICE(USB_VID_DIBCOM,    USB_PID_DIBCOM_STK7070PD) },
	{ USB_DEVICE(USB_VID_PINNACLE,
			USB_PID_PINNACLE_PCTV_DUAL_DIVERSITY_DVB_T) },
	{ USB_DEVICE(USB_VID_COMPRO,    USB_PID_COMPRO_VIDEOMATE_U500_PC) },
/* 20 */{ USB_DEVICE(USB_VID_AVERMEDIA, USB_PID_AVERMEDIA_EXPRESS) },
	{ USB_DEVICE(USB_VID_GIGABYTE,  USB_PID_GIGABYTE_U7000) },
	{ USB_DEVICE(USB_VID_ULTIMA_ELECTRONIC, USB_PID_ARTEC_T14BR) },
	{ USB_DEVICE(USB_VID_ASUS,      USB_PID_ASUS_U3000) },
	{ USB_DEVICE(USB_VID_ASUS,      USB_PID_ASUS_U3100) },
/* 25 */{ USB_DEVICE(USB_VID_HAUPPAUGE, USB_PID_HAUPPAUGE_NOVA_T_STICK_3) },
	{ USB_DEVICE(USB_VID_HAUPPAUGE, USB_PID_HAUPPAUGE_MYTV_T) },
	{ USB_DEVICE(USB_VID_TERRATEC,  USB_PID_TERRATEC_CINERGY_HT_USB_XE) },
	{ USB_DEVICE(USB_VID_PINNACLE,	USB_PID_PINNACLE_EXPRESSCARD_320CX) },
	{ USB_DEVICE(USB_VID_PINNACLE,	USB_PID_PINNACLE_PCTV72E) },
/* 30 */{ USB_DEVICE(USB_VID_PINNACLE,	USB_PID_PINNACLE_PCTV73E) },
	{ USB_DEVICE(USB_VID_YUAN,	USB_PID_YUAN_EC372S) },
	{ USB_DEVICE(USB_VID_TERRATEC,	USB_PID_TERRATEC_CINERGY_HT_EXPRESS) },
	{ USB_DEVICE(USB_VID_TERRATEC,	USB_PID_TERRATEC_CINERGY_T_XXS) },
	{ USB_DEVICE(USB_VID_LEADTEK,   USB_PID_WINFAST_DTV_DONGLE_STK7700P_2) },
/* 35 */{ USB_DEVICE(USB_VID_HAUPPAUGE, USB_PID_HAUPPAUGE_NOVA_TD_STICK_52009) },
	{ 0 }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, dib0700_usb_id_table);

#define DIB0700_DEFAULT_DEVICE_PROPERTIES \
	.caps              = DVB_USB_IS_AN_I2C_ADAPTER, \
	.usb_ctrl          = DEVICE_SPECIFIC, \
	.firmware          = "dvb-usb-dib0700-1.10.fw", \
	.download_firmware = dib0700_download_firmware, \
	.no_reconnect      = 1, \
	.size_of_priv      = sizeof(struct dib0700_state), \
	.i2c_algo          = &dib0700_i2c_algo, \
	.identify_state    = dib0700_identify_state

#define DIB0700_DEFAULT_STREAMING_CONFIG(ep) \
	.streaming_ctrl   = dib0700_streaming_ctrl, \
	.stream = { \
		.type = USB_BULK, \
		.count = 4, \
		.endpoint = ep, \
		.u = { \
			.bulk = { \
				.buffersize = 39480, \
			} \
		} \
	}

struct dvb_usb_device_properties dib0700_devices[] = {
	{
		DIB0700_DEFAULT_DEVICE_PROPERTIES,

		.num_adapters = 1,
		.adapter = {
			{
				.frontend_attach  = stk7700p_frontend_attach,
				.tuner_attach     = stk7700p_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),
			},
		},

		.num_device_descs = 8,
		.devices = {
			{   "DiBcom STK7700P reference design",
				{ &dib0700_usb_id_table[0], &dib0700_usb_id_table[1] },
				{ NULL },
			},
			{   "Hauppauge Nova-T Stick",
				{ &dib0700_usb_id_table[4], &dib0700_usb_id_table[9], NULL },
				{ NULL },
			},
			{   "AVerMedia AVerTV DVB-T Volar",
				{ &dib0700_usb_id_table[5], &dib0700_usb_id_table[10] },
				{ NULL },
			},
			{   "Compro Videomate U500",
				{ &dib0700_usb_id_table[6], &dib0700_usb_id_table[19] },
				{ NULL },
			},
			{   "Uniwill STK7700P based (Hama and others)",
				{ &dib0700_usb_id_table[7], NULL },
				{ NULL },
			},
			{   "Leadtek Winfast DTV Dongle (STK7700P based)",
				{ &dib0700_usb_id_table[8], &dib0700_usb_id_table[34] },
				{ NULL },
			},
			{   "AVerMedia AVerTV DVB-T Express",
				{ &dib0700_usb_id_table[20] },
				{ NULL },
			},
			{   "Gigabyte U7000",
				{ &dib0700_usb_id_table[21], NULL },
				{ NULL },
			}
		},

		.rc_interval      = DEFAULT_RC_INTERVAL,
		.rc_key_map       = dib0700_rc_keys,
		.rc_key_map_size  = ARRAY_SIZE(dib0700_rc_keys),
		.rc_query         = dib0700_rc_query
	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,

		.num_adapters = 2,
		.adapter = {
			{
				.frontend_attach  = bristol_frontend_attach,
				.tuner_attach     = bristol_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),
			}, {
				.frontend_attach  = bristol_frontend_attach,
				.tuner_attach     = bristol_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x03),
			}
		},

		.num_device_descs = 1,
		.devices = {
			{   "Hauppauge Nova-T 500 Dual DVB-T",
				{ &dib0700_usb_id_table[2], &dib0700_usb_id_table[3], NULL },
				{ NULL },
			},
		},

		.rc_interval      = DEFAULT_RC_INTERVAL,
		.rc_key_map       = dib0700_rc_keys,
		.rc_key_map_size  = ARRAY_SIZE(dib0700_rc_keys),
		.rc_query         = dib0700_rc_query
	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,

		.num_adapters = 2,
		.adapter = {
			{
				.frontend_attach  = stk7700d_frontend_attach,
				.tuner_attach     = stk7700d_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),
			}, {
				.frontend_attach  = stk7700d_frontend_attach,
				.tuner_attach     = stk7700d_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x03),
			}
		},

		.num_device_descs = 4,
		.devices = {
			{   "Pinnacle PCTV 2000e",
				{ &dib0700_usb_id_table[11], NULL },
				{ NULL },
			},
			{   "Terratec Cinergy DT XS Diversity",
				{ &dib0700_usb_id_table[12], NULL },
				{ NULL },
			},
			{   "Hauppauge Nova-TD Stick/Elgato Eye-TV Diversity",
				{ &dib0700_usb_id_table[13], NULL },
				{ NULL },
			},
			{   "DiBcom STK7700D reference design",
				{ &dib0700_usb_id_table[14], NULL },
				{ NULL },
			}
		},

		.rc_interval      = DEFAULT_RC_INTERVAL,
		.rc_key_map       = dib0700_rc_keys,
		.rc_key_map_size  = ARRAY_SIZE(dib0700_rc_keys),
		.rc_query         = dib0700_rc_query

	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,

		.num_adapters = 1,
		.adapter = {
			{
				.frontend_attach  = stk7700P2_frontend_attach,
				.tuner_attach     = stk7700d_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),
			},
		},

		.num_device_descs = 2,
		.devices = {
			{   "ASUS My Cinema U3000 Mini DVBT Tuner",
				{ &dib0700_usb_id_table[23], NULL },
				{ NULL },
			},
			{   "Yuan EC372S",
				{ &dib0700_usb_id_table[31], NULL },
				{ NULL },
			}
		}
	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,

		.num_adapters = 1,
		.adapter = {
			{
				.frontend_attach  = stk7070p_frontend_attach,
				.tuner_attach     = dib7070p_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),

				.size_of_priv     = sizeof(struct dib0700_adapter_state),
			},
		},

		.num_device_descs = 9,
		.devices = {
			{   "DiBcom STK7070P reference design",
				{ &dib0700_usb_id_table[15], NULL },
				{ NULL },
			},
			{   "Pinnacle PCTV DVB-T Flash Stick",
				{ &dib0700_usb_id_table[16], NULL },
				{ NULL },
			},
			{   "Artec T14BR DVB-T",
				{ &dib0700_usb_id_table[22], NULL },
				{ NULL },
			},
			{   "ASUS My Cinema U3100 Mini DVBT Tuner",
				{ &dib0700_usb_id_table[24], NULL },
				{ NULL },
			},
			{   "Hauppauge Nova-T Stick",
				{ &dib0700_usb_id_table[25], NULL },
				{ NULL },
			},
			{   "Hauppauge Nova-T MyTV.t",
				{ &dib0700_usb_id_table[26], NULL },
				{ NULL },
			},
			{   "Pinnacle PCTV 72e",
				{ &dib0700_usb_id_table[29], NULL },
				{ NULL },
			},
			{   "Pinnacle PCTV 73e",
				{ &dib0700_usb_id_table[30], NULL },
				{ NULL },
			},
			{   "Terratec Cinergy T USB XXS",
				{ &dib0700_usb_id_table[33], NULL },
				{ NULL },
			},
		},

		.rc_interval      = DEFAULT_RC_INTERVAL,
		.rc_key_map       = dib0700_rc_keys,
		.rc_key_map_size  = ARRAY_SIZE(dib0700_rc_keys),
		.rc_query         = dib0700_rc_query

	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,

		.num_adapters = 2,
		.adapter = {
			{
				.frontend_attach  = stk7070pd_frontend_attach0,
				.tuner_attach     = dib7070p_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),

				.size_of_priv     = sizeof(struct dib0700_adapter_state),
			}, {
				.frontend_attach  = stk7070pd_frontend_attach1,
				.tuner_attach     = dib7070p_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x03),

				.size_of_priv     = sizeof(struct dib0700_adapter_state),
			}
		},

		.num_device_descs = 3,
		.devices = {
			{   "DiBcom STK7070PD reference design",
				{ &dib0700_usb_id_table[17], NULL },
				{ NULL },
			},
			{   "Pinnacle PCTV Dual DVB-T Diversity Stick",
				{ &dib0700_usb_id_table[18], NULL },
				{ NULL },
			},
			{   "Hauppauge Nova-TD Stick (52009)",
				{ &dib0700_usb_id_table[35], NULL },
				{ NULL },
			}
		}
	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,

		.num_adapters = 1,
		.adapter = {
			{
				.frontend_attach  = stk7700ph_frontend_attach,
				.tuner_attach     = stk7700ph_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),

				.size_of_priv = sizeof(struct
						dib0700_adapter_state),
			},
		},

		.num_device_descs = 3,
		.devices = {
			{   "Terratec Cinergy HT USB XE",
				{ &dib0700_usb_id_table[27], NULL },
				{ NULL },
			},
			{   "Pinnacle Expresscard 320cx",
				{ &dib0700_usb_id_table[28], NULL },
				{ NULL },
			},
			{   "Terratec Cinergy HT Express",
				{ &dib0700_usb_id_table[32], NULL },
				{ NULL },
			},
		},
		.rc_interval      = DEFAULT_RC_INTERVAL,
		.rc_key_map       = dib0700_rc_keys,
		.rc_key_map_size  = ARRAY_SIZE(dib0700_rc_keys),
		.rc_query         = dib0700_rc_query
	},
};

int dib0700_device_count = ARRAY_SIZE(dib0700_devices);

/* Linux driver for devices based on the DiBcom DiB0700 USB bridge
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 *  Copyright (C) 2005-9 DiBcom, SA et al
 */
#include "dib0700.h"

#include "dib3000mc.h"
#include "dib7000m.h"
#include "dib7000p.h"
#include "dib8000.h"
#include "mt2060.h"
#include "mt2266.h"
#include "tuner-xc2028.h"
#include "xc5000.h"
#include "s5h1411.h"
#include "dib0070.h"
#include "lgdt3305.h"
#include "mxl5007t.h"

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
		if (dib7000p_i2c_enumeration(&adap->dev->i2c_adap, 1, 18,
					     stk7700d_dib7000p_mt2266_config)
		    != 0) {
			err("%s: dib7000p_i2c_enumeration failed.  Cannot continue\n", __func__);
			return -ENODEV;
		}
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
		if (dib7000p_i2c_enumeration(&adap->dev->i2c_adap, 2, 18,
					     stk7700d_dib7000p_mt2266_config)
		    != 0) {
			err("%s: dib7000p_i2c_enumeration failed.  Cannot continue\n", __func__);
			return -ENODEV;
		}
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
		&stk7700d_mt2266_config[adap->id]) == NULL ? -ENODEV : 0;
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

static int stk7700ph_xc3028_callback(void *ptr, int component,
				     int command, int arg)
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

	if (dib7000p_i2c_enumeration(&adap->dev->i2c_adap, 1, 18,
				     &stk7700ph_dib7700_xc3028_config) != 0) {
		err("%s: dib7000p_i2c_enumeration failed.  Cannot continue\n",
		    __func__);
		return -ENODEV;
	}

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

	/* FIXME: generalize & move to common area */
	adap->fe->callback = stk7700ph_xc3028_callback;

	return dvb_attach(xc2028_attach, adap->fe, &stk7700ph_xc3028_config)
		== NULL ? -ENODEV : 0;
}

#define DEFAULT_RC_INTERVAL 50

static u8 rc_request[] = { REQUEST_POLL_RC, 0 };

/* Number of keypresses to ignore before start repeating */
#define RC_REPEAT_DELAY 6
#define RC_REPEAT_DELAY_V1_20 10



/* Used by firmware versions < 1.20 (deprecated) */
static int dib0700_rc_query_legacy(struct dvb_usb_device *d, u32 *event,
				   int *state)
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
			if (rc5_custom(&keymap[i]) == key[3-2] &&
			    rc5_data(&keymap[i]) == key[3-3]) {
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
			if (rc5_custom(&keymap[i]) == key[3-2] &&
			    rc5_data(&keymap[i]) == key[3-3]) {
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

/* This is the structure of the RC response packet starting in firmware 1.20 */
struct dib0700_rc_response {
	u8 report_id;
	u8 data_state;
	u8 system_msb;
	u8 system_lsb;
	u8 data;
	u8 not_data;
};

/* This supports the new IR response format for firmware v1.20 */
static int dib0700_rc_query_v1_20(struct dvb_usb_device *d, u32 *event,
				  int *state)
{
	struct dvb_usb_rc_key *keymap = d->props.rc_key_map;
	struct dib0700_state *st = d->priv;
	struct dib0700_rc_response poll_reply;
	u8 buf[6];
	int i;
	int status;
	int actlen;
	int found = 0;

	/* Set initial results in case we exit the function early */
	*event = 0;
	*state = REMOTE_NO_KEY_PRESSED;

	/* Firmware v1.20 provides RC data via bulk endpoint 1 */
	status = usb_bulk_msg(d->udev, usb_rcvbulkpipe(d->udev, 1), buf,
			      sizeof(buf), &actlen, 50);
	if (status < 0) {
		/* No data available (meaning no key press) */
		return 0;
	}

	if (actlen != sizeof(buf)) {
		/* We didn't get back the 6 byte message we expected */
		err("Unexpected RC response size [%d]", actlen);
		return -1;
	}

	poll_reply.report_id  = buf[0];
	poll_reply.data_state = buf[1];
	poll_reply.system_msb = buf[2];
	poll_reply.system_lsb = buf[3];
	poll_reply.data       = buf[4];
	poll_reply.not_data   = buf[5];

	/*
	info("rid=%02x ds=%02x sm=%02x sl=%02x d=%02x nd=%02x\n",
	     poll_reply.report_id, poll_reply.data_state,
	     poll_reply.system_msb, poll_reply.system_lsb,
	     poll_reply.data, poll_reply.not_data);
	*/

	if ((poll_reply.data + poll_reply.not_data) != 0xff) {
		/* Key failed integrity check */
		err("key failed integrity check: %02x %02x %02x %02x",
		    poll_reply.system_msb, poll_reply.system_lsb,
		    poll_reply.data, poll_reply.not_data);
		return -1;
	}

	/* Find the key in the map */
	for (i = 0; i < d->props.rc_key_map_size; i++) {
		if (rc5_custom(&keymap[i]) == poll_reply.system_lsb &&
		    rc5_data(&keymap[i]) == poll_reply.data) {
			*event = keymap[i].event;
			found = 1;
			break;
		}
	}

	if (found == 0) {
		err("Unknown remote controller key: %02x %02x %02x %02x",
		    poll_reply.system_msb, poll_reply.system_lsb,
		    poll_reply.data, poll_reply.not_data);
		d->last_event = 0;
		return 0;
	}

	if (poll_reply.data_state == 1) {
		/* New key hit */
		st->rc_counter = 0;
		*event = keymap[i].event;
		*state = REMOTE_KEY_PRESSED;
		d->last_event = keymap[i].event;
	} else if (poll_reply.data_state == 2) {
		/* Key repeated */
		st->rc_counter++;

		/* prevents unwanted double hits */
		if (st->rc_counter > RC_REPEAT_DELAY_V1_20) {
			*event = d->last_event;
			*state = REMOTE_KEY_PRESSED;
			st->rc_counter = RC_REPEAT_DELAY_V1_20;
		}
	} else {
		err("Unknown data state [%d]", poll_reply.data_state);
	}

	return 0;
}

static int dib0700_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{
	struct dib0700_state *st = d->priv;

	/* Because some people may have improperly named firmware files,
	   let's figure out whether to use the new firmware call or the legacy
	   call based on the firmware version embedded in the file */
	if (st->rc_func_version == 0) {
		u32 hwver, romver, ramver, fwtype;
		int ret = dib0700_get_version(d, &hwver, &romver, &ramver,
					      &fwtype);
		if (ret < 0) {
			err("Could not determine version info");
			return -1;
		}
		if (ramver < 0x10200)
			st->rc_func_version = 1;
		else
			st->rc_func_version = 2;
	}

	if (st->rc_func_version == 2)
		return dib0700_rc_query_v1_20(d, event, state);
	else
		return dib0700_rc_query_legacy(d, event, state);
}

static struct dvb_usb_rc_key dib0700_rc_keys[] = {
	/* Key codes for the tiny Pinnacle remote*/
	{ 0x0700, KEY_MUTE },
	{ 0x0701, KEY_MENU }, /* Pinnacle logo */
	{ 0x0739, KEY_POWER },
	{ 0x0703, KEY_VOLUMEUP },
	{ 0x0709, KEY_VOLUMEDOWN },
	{ 0x0706, KEY_CHANNELUP },
	{ 0x070c, KEY_CHANNELDOWN },
	{ 0x070f, KEY_1 },
	{ 0x0715, KEY_2 },
	{ 0x0710, KEY_3 },
	{ 0x0718, KEY_4 },
	{ 0x071b, KEY_5 },
	{ 0x071e, KEY_6 },
	{ 0x0711, KEY_7 },
	{ 0x0721, KEY_8 },
	{ 0x0712, KEY_9 },
	{ 0x0727, KEY_0 },
	{ 0x0724, KEY_SCREEN }, /* 'Square' key */
	{ 0x072a, KEY_TEXT },   /* 'T' key */
	{ 0x072d, KEY_REWIND },
	{ 0x0730, KEY_PLAY },
	{ 0x0733, KEY_FASTFORWARD },
	{ 0x0736, KEY_RECORD },
	{ 0x073c, KEY_STOP },
	{ 0x073f, KEY_CANCEL }, /* '?' key */
	/* Key codes for the Terratec Cinergy DT XS Diversity, similar to cinergyT2.c */
	{ 0xeb01, KEY_POWER },
	{ 0xeb02, KEY_1 },
	{ 0xeb03, KEY_2 },
	{ 0xeb04, KEY_3 },
	{ 0xeb05, KEY_4 },
	{ 0xeb06, KEY_5 },
	{ 0xeb07, KEY_6 },
	{ 0xeb08, KEY_7 },
	{ 0xeb09, KEY_8 },
	{ 0xeb0a, KEY_9 },
	{ 0xeb0b, KEY_VIDEO },
	{ 0xeb0c, KEY_0 },
	{ 0xeb0d, KEY_REFRESH },
	{ 0xeb0f, KEY_EPG },
	{ 0xeb10, KEY_UP },
	{ 0xeb11, KEY_LEFT },
	{ 0xeb12, KEY_OK },
	{ 0xeb13, KEY_RIGHT },
	{ 0xeb14, KEY_DOWN },
	{ 0xeb16, KEY_INFO },
	{ 0xeb17, KEY_RED },
	{ 0xeb18, KEY_GREEN },
	{ 0xeb19, KEY_YELLOW },
	{ 0xeb1a, KEY_BLUE },
	{ 0xeb1b, KEY_CHANNELUP },
	{ 0xeb1c, KEY_VOLUMEUP },
	{ 0xeb1d, KEY_MUTE },
	{ 0xeb1e, KEY_VOLUMEDOWN },
	{ 0xeb1f, KEY_CHANNELDOWN },
	{ 0xeb40, KEY_PAUSE },
	{ 0xeb41, KEY_HOME },
	{ 0xeb42, KEY_MENU }, /* DVD Menu */
	{ 0xeb43, KEY_SUBTITLE },
	{ 0xeb44, KEY_TEXT }, /* Teletext */
	{ 0xeb45, KEY_DELETE },
	{ 0xeb46, KEY_TV },
	{ 0xeb47, KEY_DVD },
	{ 0xeb48, KEY_STOP },
	{ 0xeb49, KEY_VIDEO },
	{ 0xeb4a, KEY_AUDIO }, /* Music */
	{ 0xeb4b, KEY_SCREEN }, /* Pic */
	{ 0xeb4c, KEY_PLAY },
	{ 0xeb4d, KEY_BACK },
	{ 0xeb4e, KEY_REWIND },
	{ 0xeb4f, KEY_FASTFORWARD },
	{ 0xeb54, KEY_PREVIOUS },
	{ 0xeb58, KEY_RECORD },
	{ 0xeb5c, KEY_NEXT },

	/* Key codes for the Haupauge WinTV Nova-TD, copied from nova-t-usb2.c (Nova-T USB2) */
	{ 0x1e00, KEY_0 },
	{ 0x1e01, KEY_1 },
	{ 0x1e02, KEY_2 },
	{ 0x1e03, KEY_3 },
	{ 0x1e04, KEY_4 },
	{ 0x1e05, KEY_5 },
	{ 0x1e06, KEY_6 },
	{ 0x1e07, KEY_7 },
	{ 0x1e08, KEY_8 },
	{ 0x1e09, KEY_9 },
	{ 0x1e0a, KEY_KPASTERISK },
	{ 0x1e0b, KEY_RED },
	{ 0x1e0c, KEY_RADIO },
	{ 0x1e0d, KEY_MENU },
	{ 0x1e0e, KEY_GRAVE }, /* # */
	{ 0x1e0f, KEY_MUTE },
	{ 0x1e10, KEY_VOLUMEUP },
	{ 0x1e11, KEY_VOLUMEDOWN },
	{ 0x1e12, KEY_CHANNEL },
	{ 0x1e14, KEY_UP },
	{ 0x1e15, KEY_DOWN },
	{ 0x1e16, KEY_LEFT },
	{ 0x1e17, KEY_RIGHT },
	{ 0x1e18, KEY_VIDEO },
	{ 0x1e19, KEY_AUDIO },
	{ 0x1e1a, KEY_MEDIA },
	{ 0x1e1b, KEY_EPG },
	{ 0x1e1c, KEY_TV },
	{ 0x1e1e, KEY_NEXT },
	{ 0x1e1f, KEY_BACK },
	{ 0x1e20, KEY_CHANNELUP },
	{ 0x1e21, KEY_CHANNELDOWN },
	{ 0x1e24, KEY_LAST }, /* Skip backwards */
	{ 0x1e25, KEY_OK },
	{ 0x1e29, KEY_BLUE},
	{ 0x1e2e, KEY_GREEN },
	{ 0x1e30, KEY_PAUSE },
	{ 0x1e32, KEY_REWIND },
	{ 0x1e34, KEY_FASTFORWARD },
	{ 0x1e35, KEY_PLAY },
	{ 0x1e36, KEY_STOP },
	{ 0x1e37, KEY_RECORD },
	{ 0x1e38, KEY_YELLOW },
	{ 0x1e3b, KEY_GOTO },
	{ 0x1e3d, KEY_POWER },

	/* Key codes for the Leadtek Winfast DTV Dongle */
	{ 0x0042, KEY_POWER },
	{ 0x077c, KEY_TUNER },
	{ 0x0f4e, KEY_PRINT }, /* PREVIEW */
	{ 0x0840, KEY_SCREEN }, /* full screen toggle*/
	{ 0x0f71, KEY_DOT }, /* frequency */
	{ 0x0743, KEY_0 },
	{ 0x0c41, KEY_1 },
	{ 0x0443, KEY_2 },
	{ 0x0b7f, KEY_3 },
	{ 0x0e41, KEY_4 },
	{ 0x0643, KEY_5 },
	{ 0x097f, KEY_6 },
	{ 0x0d7e, KEY_7 },
	{ 0x057c, KEY_8 },
	{ 0x0a40, KEY_9 },
	{ 0x0e4e, KEY_CLEAR },
	{ 0x047c, KEY_CHANNEL }, /* show channel number */
	{ 0x0f41, KEY_LAST }, /* recall */
	{ 0x0342, KEY_MUTE },
	{ 0x064c, KEY_RESERVED }, /* PIP button*/
	{ 0x0172, KEY_SHUFFLE }, /* SNAPSHOT */
	{ 0x0c4e, KEY_PLAYPAUSE }, /* TIMESHIFT */
	{ 0x0b70, KEY_RECORD },
	{ 0x037d, KEY_VOLUMEUP },
	{ 0x017d, KEY_VOLUMEDOWN },
	{ 0x0242, KEY_CHANNELUP },
	{ 0x007d, KEY_CHANNELDOWN },

	/* Key codes for Nova-TD "credit card" remote control. */
	{ 0x1d00, KEY_0 },
	{ 0x1d01, KEY_1 },
	{ 0x1d02, KEY_2 },
	{ 0x1d03, KEY_3 },
	{ 0x1d04, KEY_4 },
	{ 0x1d05, KEY_5 },
	{ 0x1d06, KEY_6 },
	{ 0x1d07, KEY_7 },
	{ 0x1d08, KEY_8 },
	{ 0x1d09, KEY_9 },
	{ 0x1d0a, KEY_TEXT },
	{ 0x1d0d, KEY_MENU },
	{ 0x1d0f, KEY_MUTE },
	{ 0x1d10, KEY_VOLUMEUP },
	{ 0x1d11, KEY_VOLUMEDOWN },
	{ 0x1d12, KEY_CHANNEL },
	{ 0x1d14, KEY_UP },
	{ 0x1d15, KEY_DOWN },
	{ 0x1d16, KEY_LEFT },
	{ 0x1d17, KEY_RIGHT },
	{ 0x1d1c, KEY_TV },
	{ 0x1d1e, KEY_NEXT },
	{ 0x1d1f, KEY_BACK },
	{ 0x1d20, KEY_CHANNELUP },
	{ 0x1d21, KEY_CHANNELDOWN },
	{ 0x1d24, KEY_LAST },
	{ 0x1d25, KEY_OK },
	{ 0x1d30, KEY_PAUSE },
	{ 0x1d32, KEY_REWIND },
	{ 0x1d34, KEY_FASTFORWARD },
	{ 0x1d35, KEY_PLAY },
	{ 0x1d36, KEY_STOP },
	{ 0x1d37, KEY_RECORD },
	{ 0x1d3b, KEY_GOTO },
	{ 0x1d3d, KEY_POWER },

	/* Key codes for the Elgato EyeTV Diversity silver remote,
	   set dvb_usb_dib0700_ir_proto=0 */
	{ 0x4501, KEY_POWER },
	{ 0x4502, KEY_MUTE },
	{ 0x4503, KEY_1 },
	{ 0x4504, KEY_2 },
	{ 0x4505, KEY_3 },
	{ 0x4506, KEY_4 },
	{ 0x4507, KEY_5 },
	{ 0x4508, KEY_6 },
	{ 0x4509, KEY_7 },
	{ 0x450a, KEY_8 },
	{ 0x450b, KEY_9 },
	{ 0x450c, KEY_LAST },
	{ 0x450d, KEY_0 },
	{ 0x450e, KEY_ENTER },
	{ 0x450f, KEY_RED },
	{ 0x4510, KEY_CHANNELUP },
	{ 0x4511, KEY_GREEN },
	{ 0x4512, KEY_VOLUMEDOWN },
	{ 0x4513, KEY_OK },
	{ 0x4514, KEY_VOLUMEUP },
	{ 0x4515, KEY_YELLOW },
	{ 0x4516, KEY_CHANNELDOWN },
	{ 0x4517, KEY_BLUE },
	{ 0x4518, KEY_LEFT }, /* Skip backwards */
	{ 0x4519, KEY_PLAYPAUSE },
	{ 0x451a, KEY_RIGHT }, /* Skip forward */
	{ 0x451b, KEY_REWIND },
	{ 0x451c, KEY_L }, /* Live */
	{ 0x451d, KEY_FASTFORWARD },
	{ 0x451e, KEY_STOP }, /* 'Reveal' for Teletext */
	{ 0x451f, KEY_MENU }, /* KEY_TEXT for Teletext */
	{ 0x4540, KEY_RECORD }, /* Font 'Size' for Teletext */
	{ 0x4541, KEY_SCREEN }, /*  Full screen toggle, 'Hold' for Teletext */
	{ 0x4542, KEY_SELECT }, /* Select video input, 'Select' for Teletext */
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
	deb_info("reset: %d", onoff);
	return dib7000p_set_gpio(fe, 8, 0, !onoff);
}

static int dib7070_tuner_sleep(struct dvb_frontend *fe, int onoff)
{
	deb_info("sleep: %d", onoff);
	return dib7000p_set_gpio(fe, 9, 0, onoff);
}

static struct dib0070_config dib7070p_dib0070_config[2] = {
	{
		.i2c_address = DEFAULT_DIB0070_I2C_ADDRESS,
		.reset = dib7070_tuner_reset,
		.sleep = dib7070_tuner_sleep,
		.clock_khz = 12000,
		.clock_pad_drive = 4,
		.charge_pump = 2,
	}, {
		.i2c_address = DEFAULT_DIB0070_I2C_ADDRESS,
		.reset = dib7070_tuner_reset,
		.sleep = dib7070_tuner_sleep,
		.clock_khz = 12000,
		.charge_pump = 2,
	}
};

static struct dib0070_config dib7770p_dib0070_config = {
	 .i2c_address = DEFAULT_DIB0070_I2C_ADDRESS,
	 .reset = dib7070_tuner_reset,
	 .sleep = dib7070_tuner_sleep,
	 .clock_khz = 12000,
	 .clock_pad_drive = 0,
	 .flip_chip = 1,
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

static int dib7770_set_param_override(struct dvb_frontend *fe,
		struct dvb_frontend_parameters *fep)
{
	 struct dvb_usb_adapter *adap = fe->dvb->priv;
	 struct dib0700_adapter_state *state = adap->priv;

	 u16 offset;
	 u8 band = BAND_OF_FREQUENCY(fep->frequency/1000);
	 switch (band) {
	 case BAND_VHF:
		  dib7000p_set_gpio(fe, 0, 0, 1);
		  offset = 850;
		  break;
	 case BAND_UHF:
	 default:
		  dib7000p_set_gpio(fe, 0, 0, 0);
		  offset = 250;
		  break;
	 }
	 deb_info("WBD for DiB7000P: %d\n", offset + dib0070_wbd_offset(fe));
	 dib7000p_set_wbd_ref(fe, offset + dib0070_wbd_offset(fe));
	 return state->set_param_save(fe, fep);
}

static int dib7770p_tuner_attach(struct dvb_usb_adapter *adap)
{
	 struct dib0700_adapter_state *st = adap->priv;
	 struct i2c_adapter *tun_i2c = dib7000p_get_i2c_master(adap->fe,
			 DIBX000_I2C_INTERFACE_TUNER, 1);

	 if (dvb_attach(dib0070_attach, adap->fe, tun_i2c,
				 &dib7770p_dib0070_config) == NULL)
		 return -ENODEV;

	 st->set_param_save = adap->fe->ops.tuner_ops.set_params;
	 adap->fe->ops.tuner_ops.set_params = dib7770_set_param_override;
	 return 0;
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

	if (dib7000p_i2c_enumeration(&adap->dev->i2c_adap, 1, 18,
				     &dib7070p_dib7000p_config) != 0) {
		err("%s: dib7000p_i2c_enumeration failed.  Cannot continue\n",
		    __func__);
		return -ENODEV;
	}

	adap->fe = dvb_attach(dib7000p_attach, &adap->dev->i2c_adap, 0x80,
		&dib7070p_dib7000p_config);
	return adap->fe == NULL ? -ENODEV : 0;
}

/* DIB807x generic */
static struct dibx000_agc_config dib807x_agc_config[2] = {
	{
		BAND_VHF,
		/* P_agc_use_sd_mod1=0, P_agc_use_sd_mod2=0,
		 * P_agc_freq_pwm_div=1, P_agc_inv_pwm1=0,
		 * P_agc_inv_pwm2=0,P_agc_inh_dc_rv_est=0,
		 * P_agc_time_est=3, P_agc_freeze=0, P_agc_nb_est=5,
		 * P_agc_write=0 */
		(0 << 15) | (0 << 14) | (7 << 11) | (0 << 10) | (0 << 9) |
			(0 << 8) | (3 << 5) | (0 << 4) | (5 << 1) |
			(0 << 0), /* setup*/

		600, /* inv_gain*/
		10,  /* time_stabiliz*/

		0,  /* alpha_level*/
		118,  /* thlock*/

		0,     /* wbd_inv*/
		3530,  /* wbd_ref*/
		1,     /* wbd_sel*/
		5,     /* wbd_alpha*/

		65535,  /* agc1_max*/
		0,  /* agc1_min*/

		65535,  /* agc2_max*/
		0,      /* agc2_min*/

		0,      /* agc1_pt1*/
		40,     /* agc1_pt2*/
		183,    /* agc1_pt3*/
		206,    /* agc1_slope1*/
		255,    /* agc1_slope2*/
		72,     /* agc2_pt1*/
		152,    /* agc2_pt2*/
		88,     /* agc2_slope1*/
		90,     /* agc2_slope2*/

		17,  /* alpha_mant*/
		27,  /* alpha_exp*/
		23,  /* beta_mant*/
		51,  /* beta_exp*/

		0,  /* perform_agc_softsplit*/
	}, {
		BAND_UHF,
		/* P_agc_use_sd_mod1=0, P_agc_use_sd_mod2=0,
		 * P_agc_freq_pwm_div=1, P_agc_inv_pwm1=0,
		 * P_agc_inv_pwm2=0, P_agc_inh_dc_rv_est=0,
		 * P_agc_time_est=3, P_agc_freeze=0, P_agc_nb_est=5,
		 * P_agc_write=0 */
		(0 << 15) | (0 << 14) | (1 << 11) | (0 << 10) | (0 << 9) |
			(0 << 8) | (3 << 5) | (0 << 4) | (5 << 1) |
			(0 << 0), /* setup */

		600, /* inv_gain*/
		10,  /* time_stabiliz*/

		0,  /* alpha_level*/
		118,  /* thlock*/

		0,     /* wbd_inv*/
		3530,  /* wbd_ref*/
		1,     /* wbd_sel*/
		5,     /* wbd_alpha*/

		65535,  /* agc1_max*/
		0,  /* agc1_min*/

		65535,  /* agc2_max*/
		0,      /* agc2_min*/

		0,      /* agc1_pt1*/
		40,     /* agc1_pt2*/
		183,    /* agc1_pt3*/
		206,    /* agc1_slope1*/
		255,    /* agc1_slope2*/
		72,     /* agc2_pt1*/
		152,    /* agc2_pt2*/
		88,     /* agc2_slope1*/
		90,     /* agc2_slope2*/

		17,  /* alpha_mant*/
		27,  /* alpha_exp*/
		23,  /* beta_mant*/
		51,  /* beta_exp*/

		0,  /* perform_agc_softsplit*/
	}
};

static struct dibx000_bandwidth_config dib807x_bw_config_12_mhz = {
	60000, 15000, /* internal, sampling*/
	1, 20, 3, 1, 0, /* pll_cfg: prediv, ratio, range, reset, bypass*/
	0, 0, 1, 1, 2, /* misc: refdiv, bypclk_div, IO_CLK_en_core,
			  ADClkSrc, modulo */
	(3 << 14) | (1 << 12) | (599 << 0), /* sad_cfg: refsel, sel, freq_15k*/
	(0 << 25) | 0, /* ifreq = 0.000000 MHz*/
	18179755, /* timf*/
	12000000, /* xtal_hz*/
};

static struct dib8000_config dib807x_dib8000_config[2] = {
	{
		.output_mpeg2_in_188_bytes = 1,

		.agc_config_count = 2,
		.agc = dib807x_agc_config,
		.pll = &dib807x_bw_config_12_mhz,
		.tuner_is_baseband = 1,

		.gpio_dir = DIB8000_GPIO_DEFAULT_DIRECTIONS,
		.gpio_val = DIB8000_GPIO_DEFAULT_VALUES,
		.gpio_pwm_pos = DIB8000_GPIO_DEFAULT_PWM_POS,

		.hostbus_diversity = 1,
		.div_cfg = 1,
		.agc_control = &dib0070_ctrl_agc_filter,
		.output_mode = OUTMODE_MPEG2_FIFO,
		.drives = 0x2d98,
	}, {
		.output_mpeg2_in_188_bytes = 1,

		.agc_config_count = 2,
		.agc = dib807x_agc_config,
		.pll = &dib807x_bw_config_12_mhz,
		.tuner_is_baseband = 1,

		.gpio_dir = DIB8000_GPIO_DEFAULT_DIRECTIONS,
		.gpio_val = DIB8000_GPIO_DEFAULT_VALUES,
		.gpio_pwm_pos = DIB8000_GPIO_DEFAULT_PWM_POS,

		.hostbus_diversity = 1,
		.agc_control = &dib0070_ctrl_agc_filter,
		.output_mode = OUTMODE_MPEG2_FIFO,
		.drives = 0x2d98,
	}
};

static int dib807x_tuner_reset(struct dvb_frontend *fe, int onoff)
{
	return dib8000_set_gpio(fe, 5, 0, !onoff);
}

static int dib807x_tuner_sleep(struct dvb_frontend *fe, int onoff)
{
	return dib8000_set_gpio(fe, 0, 0, onoff);
}

static const struct dib0070_wbd_gain_cfg dib8070_wbd_gain_cfg[] = {
    { 240,      7},
    { 0xffff,   6},
};

static struct dib0070_config dib807x_dib0070_config[2] = {
	{
		.i2c_address = DEFAULT_DIB0070_I2C_ADDRESS,
		.reset = dib807x_tuner_reset,
		.sleep = dib807x_tuner_sleep,
		.clock_khz = 12000,
		.clock_pad_drive = 4,
		.vga_filter = 1,
		.force_crystal_mode = 1,
		.enable_third_order_filter = 1,
		.charge_pump = 0,
		.wbd_gain = dib8070_wbd_gain_cfg,
		.osc_buffer_state = 0,
		.freq_offset_khz_uhf = -100,
		.freq_offset_khz_vhf = -100,
	}, {
		.i2c_address = DEFAULT_DIB0070_I2C_ADDRESS,
		.reset = dib807x_tuner_reset,
		.sleep = dib807x_tuner_sleep,
		.clock_khz = 12000,
		.clock_pad_drive = 2,
		.vga_filter = 1,
		.force_crystal_mode = 1,
		.enable_third_order_filter = 1,
		.charge_pump = 0,
		.wbd_gain = dib8070_wbd_gain_cfg,
		.osc_buffer_state = 0,
		.freq_offset_khz_uhf = -25,
		.freq_offset_khz_vhf = -25,
	}
};

static int dib807x_set_param_override(struct dvb_frontend *fe,
		struct dvb_frontend_parameters *fep)
{
	struct dvb_usb_adapter *adap = fe->dvb->priv;
	struct dib0700_adapter_state *state = adap->priv;

	u16 offset = dib0070_wbd_offset(fe);
	u8 band = BAND_OF_FREQUENCY(fep->frequency/1000);
	switch (band) {
	case BAND_VHF:
		offset += 750;
		break;
	case BAND_UHF:  /* fall-thru wanted */
	default:
		offset += 250; break;
	}
	deb_info("WBD for DiB8000: %d\n", offset);
	dib8000_set_wbd_ref(fe, offset);

	return state->set_param_save(fe, fep);
}

static int dib807x_tuner_attach(struct dvb_usb_adapter *adap)
{
	struct dib0700_adapter_state *st = adap->priv;
	struct i2c_adapter *tun_i2c = dib8000_get_i2c_master(adap->fe,
			DIBX000_I2C_INTERFACE_TUNER, 1);

	if (adap->id == 0) {
		if (dvb_attach(dib0070_attach, adap->fe, tun_i2c,
				&dib807x_dib0070_config[0]) == NULL)
			return -ENODEV;
	} else {
		if (dvb_attach(dib0070_attach, adap->fe, tun_i2c,
				&dib807x_dib0070_config[1]) == NULL)
			return -ENODEV;
	}

	st->set_param_save = adap->fe->ops.tuner_ops.set_params;
	adap->fe->ops.tuner_ops.set_params = dib807x_set_param_override;
	return 0;
}


/* STK807x */
static int stk807x_frontend_attach(struct dvb_usb_adapter *adap)
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

	dib8000_i2c_enumeration(&adap->dev->i2c_adap, 1, 18,
				0x80);

	adap->fe = dvb_attach(dib8000_attach, &adap->dev->i2c_adap, 0x80,
			      &dib807x_dib8000_config[0]);

	return adap->fe == NULL ?  -ENODEV : 0;
}

/* STK807xPVR */
static int stk807xpvr_frontend_attach0(struct dvb_usb_adapter *adap)
{
	dib0700_set_gpio(adap->dev, GPIO6, GPIO_OUT, 0);
	msleep(30);
	dib0700_set_gpio(adap->dev, GPIO6, GPIO_OUT, 1);
	msleep(500);
	dib0700_set_gpio(adap->dev, GPIO9, GPIO_OUT, 1);
	dib0700_set_gpio(adap->dev, GPIO4, GPIO_OUT, 1);
	dib0700_set_gpio(adap->dev, GPIO7, GPIO_OUT, 1);

	dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 0);

	dib0700_ctrl_clock(adap->dev, 72, 1);

	msleep(10);
	dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 1);
	msleep(10);
	dib0700_set_gpio(adap->dev, GPIO0, GPIO_OUT, 1);

	/* initialize IC 0 */
	dib8000_i2c_enumeration(&adap->dev->i2c_adap, 1, 0x12, 0x80);

	adap->fe = dvb_attach(dib8000_attach, &adap->dev->i2c_adap, 0x80,
			      &dib807x_dib8000_config[0]);

	return adap->fe == NULL ? -ENODEV : 0;
}

static int stk807xpvr_frontend_attach1(struct dvb_usb_adapter *adap)
{
	/* initialize IC 1 */
	dib8000_i2c_enumeration(&adap->dev->i2c_adap, 1, 0x22, 0x82);

	adap->fe = dvb_attach(dib8000_attach, &adap->dev->i2c_adap, 0x82,
			      &dib807x_dib8000_config[1]);

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

	if (dib7000p_i2c_enumeration(&adap->dev->i2c_adap, 2, 18,
				     stk7070pd_dib7000p_config) != 0) {
		err("%s: dib7000p_i2c_enumeration failed.  Cannot continue\n",
		    __func__);
		return -ENODEV;
	}

	adap->fe = dvb_attach(dib7000p_attach, &adap->dev->i2c_adap, 0x80, &stk7070pd_dib7000p_config[0]);
	return adap->fe == NULL ? -ENODEV : 0;
}

static int stk7070pd_frontend_attach1(struct dvb_usb_adapter *adap)
{
	adap->fe = dvb_attach(dib7000p_attach, &adap->dev->i2c_adap, 0x82, &stk7070pd_dib7000p_config[1]);
	return adap->fe == NULL ? -ENODEV : 0;
}

/* S5H1411 */
static struct s5h1411_config pinnacle_801e_config = {
	.output_mode   = S5H1411_PARALLEL_OUTPUT,
	.gpio          = S5H1411_GPIO_OFF,
	.mpeg_timing   = S5H1411_MPEGTIMING_NONCONTINOUS_NONINVERTING_CLOCK,
	.qam_if        = S5H1411_IF_44000,
	.vsb_if        = S5H1411_IF_44000,
	.inversion     = S5H1411_INVERSION_OFF,
	.status_mode   = S5H1411_DEMODLOCKING
};

/* Pinnacle PCTV HD Pro 801e GPIOs map:
   GPIO0  - currently unknown
   GPIO1  - xc5000 tuner reset
   GPIO2  - CX25843 sleep
   GPIO3  - currently unknown
   GPIO4  - currently unknown
   GPIO6  - currently unknown
   GPIO7  - currently unknown
   GPIO9  - currently unknown
   GPIO10 - CX25843 reset
 */
static int s5h1411_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct dib0700_state *st = adap->dev->priv;

	/* Make use of the new i2c functions from FW 1.20 */
	st->fw_use_new_i2c_api = 1;

	/* The s5h1411 requires the dib0700 to not be in master mode */
	st->disable_streaming_master_mode = 1;

	/* All msleep values taken from Windows USB trace */
	dib0700_set_gpio(adap->dev, GPIO0, GPIO_OUT, 0);
	dib0700_set_gpio(adap->dev, GPIO3, GPIO_OUT, 0);
	dib0700_set_gpio(adap->dev, GPIO6, GPIO_OUT, 1);
	msleep(400);
	dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 0);
	msleep(60);
	dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 1);
	msleep(30);
	dib0700_set_gpio(adap->dev, GPIO0, GPIO_OUT, 1);
	dib0700_set_gpio(adap->dev, GPIO9, GPIO_OUT, 1);
	dib0700_set_gpio(adap->dev, GPIO4, GPIO_OUT, 1);
	dib0700_set_gpio(adap->dev, GPIO7, GPIO_OUT, 1);
	dib0700_set_gpio(adap->dev, GPIO2, GPIO_OUT, 0);
	msleep(30);

	/* Put the CX25843 to sleep for now since we're in digital mode */
	dib0700_set_gpio(adap->dev, GPIO2, GPIO_OUT, 1);

	/* GPIOs are initialized, do the attach */
	adap->fe = dvb_attach(s5h1411_attach, &pinnacle_801e_config,
			      &adap->dev->i2c_adap);
	return adap->fe == NULL ? -ENODEV : 0;
}

static int dib0700_xc5000_tuner_callback(void *priv, int component,
					 int command, int arg)
{
	struct dvb_usb_adapter *adap = priv;

	if (command == XC5000_TUNER_RESET) {
		/* Reset the tuner */
		dib0700_set_gpio(adap->dev, GPIO1, GPIO_OUT, 0);
		msleep(10);
		dib0700_set_gpio(adap->dev, GPIO1, GPIO_OUT, 1);
		msleep(10);
	} else {
		err("xc5000: unknown tuner callback command: %d\n", command);
		return -EINVAL;
	}

	return 0;
}

static struct xc5000_config s5h1411_xc5000_tunerconfig = {
	.i2c_address      = 0x64,
	.if_khz           = 5380,
};

static int xc5000_tuner_attach(struct dvb_usb_adapter *adap)
{
	/* FIXME: generalize & move to common area */
	adap->fe->callback = dib0700_xc5000_tuner_callback;

	return dvb_attach(xc5000_attach, adap->fe, &adap->dev->i2c_adap,
			  &s5h1411_xc5000_tunerconfig)
		== NULL ? -ENODEV : 0;
}

static struct lgdt3305_config hcw_lgdt3305_config = {
	.i2c_addr           = 0x0e,
	.mpeg_mode          = LGDT3305_MPEG_PARALLEL,
	.tpclk_edge         = LGDT3305_TPCLK_FALLING_EDGE,
	.tpvalid_polarity   = LGDT3305_TP_VALID_LOW,
	.deny_i2c_rptr      = 0,
	.spectral_inversion = 1,
	.qam_if_khz         = 6000,
	.vsb_if_khz         = 6000,
	.usref_8vsb         = 0x0500,
};

static struct mxl5007t_config hcw_mxl5007t_config = {
	.xtal_freq_hz = MxL_XTAL_25_MHZ,
	.if_freq_hz = MxL_IF_6_MHZ,
	.invert_if = 1,
};

/* TIGER-ATSC map:
   GPIO0  - LNA_CTR  (H: LNA power enabled, L: LNA power disabled)
   GPIO1  - ANT_SEL  (H: VPA, L: MCX)
   GPIO4  - SCL2
   GPIO6  - EN_TUNER
   GPIO7  - SDA2
   GPIO10 - DEM_RST

   MXL is behind LG's i2c repeater.  LG is on SCL2/SDA2 gpios on the DIB
 */
static int lgdt3305_frontend_attach(struct dvb_usb_adapter *adap)
{
	struct dib0700_state *st = adap->dev->priv;

	/* Make use of the new i2c functions from FW 1.20 */
	st->fw_use_new_i2c_api = 1;

	st->disable_streaming_master_mode = 1;

	/* fe power enable */
	dib0700_set_gpio(adap->dev, GPIO6, GPIO_OUT, 0);
	msleep(30);
	dib0700_set_gpio(adap->dev, GPIO6, GPIO_OUT, 1);
	msleep(30);

	/* demod reset */
	dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 1);
	msleep(30);
	dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 0);
	msleep(30);
	dib0700_set_gpio(adap->dev, GPIO10, GPIO_OUT, 1);
	msleep(30);

	adap->fe = dvb_attach(lgdt3305_attach,
			      &hcw_lgdt3305_config,
			      &adap->dev->i2c_adap);

	return adap->fe == NULL ? -ENODEV : 0;
}

static int mxl5007t_tuner_attach(struct dvb_usb_adapter *adap)
{
	return dvb_attach(mxl5007t_attach, adap->fe,
			  &adap->dev->i2c_adap, 0x60,
			  &hcw_mxl5007t_config) == NULL ? -ENODEV : 0;
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
	{ USB_DEVICE(USB_VID_HAUPPAUGE, USB_PID_HAUPPAUGE_NOVA_T_500_3) },
	{ USB_DEVICE(USB_VID_GIGABYTE,  USB_PID_GIGABYTE_U8000) },
	{ USB_DEVICE(USB_VID_YUAN,      USB_PID_YUAN_STK7700PH) },
	{ USB_DEVICE(USB_VID_ASUS,	USB_PID_ASUS_U3000H) },
/* 40 */{ USB_DEVICE(USB_VID_PINNACLE,  USB_PID_PINNACLE_PCTV801E) },
	{ USB_DEVICE(USB_VID_PINNACLE,  USB_PID_PINNACLE_PCTV801E_SE) },
	{ USB_DEVICE(USB_VID_TERRATEC,	USB_PID_TERRATEC_CINERGY_T_EXPRESS) },
	{ USB_DEVICE(USB_VID_TERRATEC,
			USB_PID_TERRATEC_CINERGY_DT_XS_DIVERSITY_2) },
	{ USB_DEVICE(USB_VID_SONY,	USB_PID_SONY_PLAYTV) },
/* 45 */{ USB_DEVICE(USB_VID_YUAN,      USB_PID_YUAN_PD378S) },
	{ USB_DEVICE(USB_VID_HAUPPAUGE, USB_PID_HAUPPAUGE_TIGER_ATSC) },
	{ USB_DEVICE(USB_VID_HAUPPAUGE, USB_PID_HAUPPAUGE_TIGER_ATSC_B210) },
	{ USB_DEVICE(USB_VID_YUAN,	USB_PID_YUAN_MC770) },
	{ USB_DEVICE(USB_VID_ELGATO,	USB_PID_ELGATO_EYETV_DTT) },
/* 50 */{ USB_DEVICE(USB_VID_ELGATO,	USB_PID_ELGATO_EYETV_DTT_Dlx) },
	{ USB_DEVICE(USB_VID_LEADTEK,   USB_PID_WINFAST_DTV_DONGLE_H) },
	{ USB_DEVICE(USB_VID_TERRATEC,	USB_PID_TERRATEC_T3) },
	{ USB_DEVICE(USB_VID_TERRATEC,	USB_PID_TERRATEC_T5) },
	{ USB_DEVICE(USB_VID_YUAN,      USB_PID_YUAN_STK7700D) },
/* 55 */{ USB_DEVICE(USB_VID_YUAN,	USB_PID_YUAN_STK7700D_2) },
	{ USB_DEVICE(USB_VID_PINNACLE,	USB_PID_PINNACLE_PCTV73A) },
	{ USB_DEVICE(USB_VID_PINNACLE,	USB_PID_PINNACLE_PCTV73ESE) },
	{ USB_DEVICE(USB_VID_PINNACLE,	USB_PID_PINNACLE_PCTV282E) },
	{ USB_DEVICE(USB_VID_DIBCOM,	USB_PID_DIBCOM_STK7770P) },
/* 60 */{ USB_DEVICE(USB_VID_TERRATEC,	USB_PID_TERRATEC_CINERGY_T_XXS_2) },
	{ USB_DEVICE(USB_VID_DIBCOM,    USB_PID_DIBCOM_STK807XPVR) },
	{ USB_DEVICE(USB_VID_DIBCOM,    USB_PID_DIBCOM_STK807XP) },
	{ USB_DEVICE(USB_VID_PIXELVIEW, USB_PID_PIXELVIEW_SBTVD) },
	{ USB_DEVICE(USB_VID_ELGATO,    USB_PID_ELGATO_EYETV_DIVERSITY) },
	{ 0 }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, dib0700_usb_id_table);

#define DIB0700_DEFAULT_DEVICE_PROPERTIES \
	.caps              = DVB_USB_IS_AN_I2C_ADAPTER, \
	.usb_ctrl          = DEVICE_SPECIFIC, \
	.firmware          = "dvb-usb-dib0700-1.20.fw", \
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

		.num_device_descs = 5,
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
			},
			{   "YUAN High-Tech DiBcom STK7700D",
				{ &dib0700_usb_id_table[55], NULL },
				{ NULL },
			},

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

		.num_device_descs = 3,
		.devices = {
			{   "ASUS My Cinema U3000 Mini DVBT Tuner",
				{ &dib0700_usb_id_table[23], NULL },
				{ NULL },
			},
			{   "Yuan EC372S",
				{ &dib0700_usb_id_table[31], NULL },
				{ NULL },
			},
			{   "Terratec Cinergy T Express",
				{ &dib0700_usb_id_table[42], NULL },
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
				.frontend_attach  = stk7070p_frontend_attach,
				.tuner_attach     = dib7070p_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),

				.size_of_priv     = sizeof(struct dib0700_adapter_state),
			},
		},

		.num_device_descs = 11,
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
			{   "Elgato EyeTV DTT",
				{ &dib0700_usb_id_table[49], NULL },
				{ NULL },
			},
			{   "Yuan PD378S",
				{ &dib0700_usb_id_table[45], NULL },
				{ NULL },
			},
			{   "Elgato EyeTV Dtt Dlx PD378S",
				{ &dib0700_usb_id_table[50], NULL },
				{ NULL },
			},
		},

		.rc_interval      = DEFAULT_RC_INTERVAL,
		.rc_key_map       = dib0700_rc_keys,
		.rc_key_map_size  = ARRAY_SIZE(dib0700_rc_keys),
		.rc_query         = dib0700_rc_query

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

		.num_device_descs = 3,
		.devices = {
			{   "Pinnacle PCTV 73A",
				{ &dib0700_usb_id_table[56], NULL },
				{ NULL },
			},
			{   "Pinnacle PCTV 73e SE",
				{ &dib0700_usb_id_table[57], NULL },
				{ NULL },
			},
			{   "Pinnacle PCTV 282e",
				{ &dib0700_usb_id_table[58], NULL },
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

		.num_device_descs = 7,
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
			},
			{   "Hauppauge Nova-TD-500 (84xxx)",
				{ &dib0700_usb_id_table[36], NULL },
				{ NULL },
			},
			{  "Terratec Cinergy DT USB XS Diversity/ T5",
				{ &dib0700_usb_id_table[43],
					&dib0700_usb_id_table[53], NULL},
				{ NULL },
			},
			{  "Sony PlayTV",
				{ &dib0700_usb_id_table[44], NULL },
				{ NULL },
			},
			{   "Elgato EyeTV Diversity",
				{ &dib0700_usb_id_table[64], NULL },
				{ NULL },
			},
		},
		.rc_interval      = DEFAULT_RC_INTERVAL,
		.rc_key_map       = dib0700_rc_keys,
		.rc_key_map_size  = ARRAY_SIZE(dib0700_rc_keys),
		.rc_query         = dib0700_rc_query
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

		.num_device_descs = 9,
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
			{   "Gigabyte U8000-RH",
				{ &dib0700_usb_id_table[37], NULL },
				{ NULL },
			},
			{   "YUAN High-Tech STK7700PH",
				{ &dib0700_usb_id_table[38], NULL },
				{ NULL },
			},
			{   "Asus My Cinema-U3000Hybrid",
				{ &dib0700_usb_id_table[39], NULL },
				{ NULL },
			},
			{   "YUAN High-Tech MC770",
				{ &dib0700_usb_id_table[48], NULL },
				{ NULL },
			},
			{   "Leadtek WinFast DTV Dongle H",
				{ &dib0700_usb_id_table[51], NULL },
				{ NULL },
			},
			{   "YUAN High-Tech STK7700D",
				{ &dib0700_usb_id_table[54], NULL },
				{ NULL },
			},
		},
		.rc_interval      = DEFAULT_RC_INTERVAL,
		.rc_key_map       = dib0700_rc_keys,
		.rc_key_map_size  = ARRAY_SIZE(dib0700_rc_keys),
		.rc_query         = dib0700_rc_query
	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,
		.num_adapters = 1,
		.adapter = {
			{
				.frontend_attach  = s5h1411_frontend_attach,
				.tuner_attach     = xc5000_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),

				.size_of_priv = sizeof(struct
						dib0700_adapter_state),
			},
		},

		.num_device_descs = 2,
		.devices = {
			{   "Pinnacle PCTV HD Pro USB Stick",
				{ &dib0700_usb_id_table[40], NULL },
				{ NULL },
			},
			{   "Pinnacle PCTV HD USB Stick",
				{ &dib0700_usb_id_table[41], NULL },
				{ NULL },
			},
		},
		.rc_interval      = DEFAULT_RC_INTERVAL,
		.rc_key_map       = dib0700_rc_keys,
		.rc_key_map_size  = ARRAY_SIZE(dib0700_rc_keys),
		.rc_query         = dib0700_rc_query
	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,
		.num_adapters = 1,
		.adapter = {
			{
				.frontend_attach  = lgdt3305_frontend_attach,
				.tuner_attach     = mxl5007t_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),

				.size_of_priv = sizeof(struct
						dib0700_adapter_state),
			},
		},

		.num_device_descs = 2,
		.devices = {
			{   "Hauppauge ATSC MiniCard (B200)",
				{ &dib0700_usb_id_table[46], NULL },
				{ NULL },
			},
			{   "Hauppauge ATSC MiniCard (B210)",
				{ &dib0700_usb_id_table[47], NULL },
				{ NULL },
			},
		},
	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,

		.num_adapters = 1,
		.adapter = {
			{
				.frontend_attach  = stk7070p_frontend_attach,
				.tuner_attach     = dib7770p_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),

				.size_of_priv =
					sizeof(struct dib0700_adapter_state),
			},
		},

		.num_device_descs = 2,
		.devices = {
			{   "DiBcom STK7770P reference design",
				{ &dib0700_usb_id_table[59], NULL },
				{ NULL },
			},
			{   "Terratec Cinergy T USB XXS (HD)/ T3",
				{ &dib0700_usb_id_table[33],
					&dib0700_usb_id_table[52],
					&dib0700_usb_id_table[60], NULL},
				{ NULL },
			},
		},
		.rc_interval      = DEFAULT_RC_INTERVAL,
		.rc_key_map       = dib0700_rc_keys,
		.rc_key_map_size  = ARRAY_SIZE(dib0700_rc_keys),
		.rc_query         = dib0700_rc_query
	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,
		.num_adapters = 1,
		.adapter = {
			{
				.frontend_attach  = stk807x_frontend_attach,
				.tuner_attach     = dib807x_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),

				.size_of_priv =
					sizeof(struct dib0700_adapter_state),
			},
		},

		.num_device_descs = 2,
		.devices = {
			{   "DiBcom STK807xP reference design",
				{ &dib0700_usb_id_table[62], NULL },
				{ NULL },
			},
			{   "Prolink Pixelview SBTVD",
				{ &dib0700_usb_id_table[63], NULL },
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
				.frontend_attach  = stk807xpvr_frontend_attach0,
				.tuner_attach     = dib807x_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),

				.size_of_priv =
					sizeof(struct dib0700_adapter_state),
			},
			{
				.frontend_attach  = stk807xpvr_frontend_attach1,
				.tuner_attach     = dib807x_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x03),

				.size_of_priv =
					sizeof(struct dib0700_adapter_state),
			},
		},

		.num_device_descs = 1,
		.devices = {
			{   "DiBcom STK807xPVR reference design",
				{ &dib0700_usb_id_table[61], NULL },
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

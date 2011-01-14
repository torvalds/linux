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
#include "dib0090.h"
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
		BAND_UHF,

		/* P_agc_use_sd_mod1=0, P_agc_use_sd_mod2=0, P_agc_freq_pwm_div=1, P_agc_inv_pwm1=1, P_agc_inv_pwm2=1,
		* P_agc_inh_dc_rv_est=0, P_agc_time_est=3, P_agc_freeze=0, P_agc_nb_est=2, P_agc_write=0 */
		(0 << 15) | (0 << 14) | (1 << 11) | (1 << 10) | (1 << 9) | (0 << 8)
	    | (3 << 5) | (0 << 4) | (5 << 1) | (0 << 0),

		1130,
		21,

		0,
		118,

		0,
		3530,
		1,
		0,

		65535,
		33770,
		65535,
		23592,

		0,
		62,
		255,
		64,
		64,
		132,
		192,
		80,
		80,

		17,
		27,
		23,
		51,

		1,
	}, {
		BAND_VHF | BAND_LBAND,

		/* P_agc_use_sd_mod1=0, P_agc_use_sd_mod2=0, P_agc_freq_pwm_div=1, P_agc_inv_pwm1=1, P_agc_inv_pwm2=1,
		* P_agc_inh_dc_rv_est=0, P_agc_time_est=3, P_agc_freeze=0, P_agc_nb_est=2, P_agc_write=0 */
		(0 << 15) | (0 << 14) | (1 << 11) | (1 << 10) | (1 << 9) | (0 << 8)
	    | (3 << 5) | (0 << 4) | (2 << 1) | (0 << 0),

		2372,
		21,

		0,
		118,

		0,
		3530,
		1,
		0,

		65535,
		0,
		65535,
		23592,

		0,
		128,
		128,
		128,
		0,
		128,
		253,
		81,
		0,

		17,
		27,
		23,
		51,

		1,
	}
};

static struct dibx000_bandwidth_config stk7700d_mt2266_pll_config = {
	60000, 30000,
	1, 8, 3, 1, 0,
	0, 0, 1, 1, 2,
	(3 << 14) | (1 << 12) | (524 << 0),
	0,
	20452225,
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

/*
 * This function is used only when firmware is < 1.20 version. Newer
 * firmwares use bulk mode, with functions implemented at dib0700_core,
 * at dib0700_rc_urb_completion()
 */
static int dib0700_rc_query_old_firmware(struct dvb_usb_device *d)
{
	u8 key[4];
	u32 keycode;
	u8 toggle;
	int i;
	struct dib0700_state *st = d->priv;

	if (st->fw_version >= 0x10200) {
		/* For 1.20 firmware , We need to keep the RC polling
		   callback so we can reuse the input device setup in
		   dvb-usb-remote.c.  However, the actual work is being done
		   in the bulk URB completion handler. */
		return 0;
	}

	i = dib0700_ctrl_rd(d, rc_request, 2, key, 4);
	if (i <= 0) {
		err("RC Query Failed");
		return -1;
	}

	/* losing half of KEY_0 events from Philipps rc5 remotes.. */
	if (key[0] == 0 && key[1] == 0 && key[2] == 0 && key[3] == 0)
		return 0;

	/* info("%d: %2X %2X %2X %2X",dvb_usb_dib0700_ir_proto,(int)key[3-2],(int)key[3-3],(int)key[3-1],(int)key[3]);  */

	dib0700_rc_setup(d); /* reset ir sensor data to prevent false events */

	d->last_event = 0;
	switch (d->props.rc.core.protocol) {
	case RC_TYPE_NEC:
		/* NEC protocol sends repeat code as 0 0 0 FF */
		if ((key[3-2] == 0x00) && (key[3-3] == 0x00) &&
		    (key[3] == 0xff))
			keycode = d->last_event;
		else {
			keycode = key[3-2] << 8 | key[3-3];
			d->last_event = keycode;
		}

		rc_keydown(d->rc_dev, keycode, 0);
		break;
	default:
		/* RC-5 protocol changes toggle bit on new keypress */
		keycode = key[3-2] << 8 | key[3-3];
		toggle = key[3-1];
		rc_keydown(d->rc_dev, keycode, toggle);

		break;
	}
	return 0;
}

/* STK7700P: Hauppauge Nova-T Stick, AVerMedia Volar */
static struct dibx000_agc_config stk7700p_7000m_mt2060_agc_config = {
	BAND_UHF | BAND_VHF,

	/* P_agc_use_sd_mod1=0, P_agc_use_sd_mod2=0, P_agc_freq_pwm_div=5, P_agc_inv_pwm1=0, P_agc_inv_pwm2=0,
	 * P_agc_inh_dc_rv_est=0, P_agc_time_est=3, P_agc_freeze=0, P_agc_nb_est=2, P_agc_write=0 */
	(0 << 15) | (0 << 14) | (5 << 11) | (0 << 10) | (0 << 9) | (0 << 8)
	| (3 << 5) | (0 << 4) | (2 << 1) | (0 << 0),

	712,
	41,

	0,
	118,

	0,
	4095,
	0,
	0,

	42598,
	17694,
	45875,
	2621,
	0,
	76,
	139,
	52,
	59,
	107,
	172,
	57,
	70,

	21,
	25,
	28,
	48,

	1,
	{  0,
	   107,
	   51800,
	   24700
	},
};

static struct dibx000_agc_config stk7700p_7000p_mt2060_agc_config = {
	BAND_UHF | BAND_VHF,

	/* P_agc_use_sd_mod1=0, P_agc_use_sd_mod2=0, P_agc_freq_pwm_div=5, P_agc_inv_pwm1=0, P_agc_inv_pwm2=0,
	 * P_agc_inh_dc_rv_est=0, P_agc_time_est=3, P_agc_freeze=0, P_agc_nb_est=2, P_agc_write=0 */
	(0 << 15) | (0 << 14) | (5 << 11) | (0 << 10) | (0 << 9) | (0 << 8)
	| (3 << 5) | (0 << 4) | (2 << 1) | (0 << 0),

	712,
	41,

	0,
	118,

	0,
	4095,
	0,
	0,

	42598,
	16384,
	42598,
	    0,

	  0,
	137,
	255,

	  0,
	255,

	0,
	0,

	 0,
	41,

	15,
	25,

	28,
	48,

	0,
};

static struct dibx000_bandwidth_config stk7700p_pll_config = {
	60000, 30000,
	1, 8, 3, 1, 0,
	0, 0, 1, 1, 0,
	(3 << 14) | (1 << 12) | (524 << 0),
	60258167,
	20452225,
	30000000,
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
	(0 << 15) | (0 << 14) | (5 << 11) | (0 << 10) | (0 << 9) | (0 << 8)
	| (3 << 5) | (0 << 4) | (5 << 1) | (0 << 0),

	600,
	10,

	0,
	118,

	0,
	3530,
	1,
	5,

	65535,
		0,

	65535,
	0,

	0,
	40,
	183,
	206,
	255,
	72,
	152,
	88,
	90,

	17,
	27,
	23,
	51,

	0,
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
	 .charge_pump = 2,
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

static int stk7700p_pid_filter(struct dvb_usb_adapter *adapter, int index,
		u16 pid, int onoff)
{
	struct dib0700_state *st = adapter->dev->priv;
	if (st->is_dib7000pc)
		return dib7000p_pid_filter(adapter->fe, index, pid, onoff);
	return dib7000m_pid_filter(adapter->fe, index, pid, onoff);
}

static int stk7700p_pid_filter_ctrl(struct dvb_usb_adapter *adapter, int onoff)
{
	struct dib0700_state *st = adapter->dev->priv;
	if (st->is_dib7000pc)
		return dib7000p_pid_filter_ctrl(adapter->fe, onoff);
	return dib7000m_pid_filter_ctrl(adapter->fe, onoff);
}

static int stk70x0p_pid_filter(struct dvb_usb_adapter *adapter, int index, u16 pid, int onoff)
{
    return dib7000p_pid_filter(adapter->fe, index, pid, onoff);
}

static int stk70x0p_pid_filter_ctrl(struct dvb_usb_adapter *adapter, int onoff)
{
    return dib7000p_pid_filter_ctrl(adapter->fe, onoff);
}

static struct dibx000_bandwidth_config dib7070_bw_config_12_mhz = {
	60000, 15000,
	1, 20, 3, 1, 0,
	0, 0, 1, 1, 2,
	(3 << 14) | (1 << 12) | (524 << 0),
	(0 << 25) | 0,
	20452225,
	12000000,
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

/* STK7770P */
static struct dib7000p_config dib7770p_dib7000p_config = {
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
	.enable_current_mirror = 1,
	.disable_sample_and_hold = 0,
};

static int stk7770p_frontend_attach(struct dvb_usb_adapter *adap)
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
				     &dib7770p_dib7000p_config) != 0) {
		err("%s: dib7000p_i2c_enumeration failed.  Cannot continue\n",
		    __func__);
		return -ENODEV;
	}

	adap->fe = dvb_attach(dib7000p_attach, &adap->dev->i2c_adap, 0x80,
		&dib7770p_dib7000p_config);
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

static int dib80xx_tuner_reset(struct dvb_frontend *fe, int onoff)
{
	return dib8000_set_gpio(fe, 5, 0, !onoff);
}

static int dib80xx_tuner_sleep(struct dvb_frontend *fe, int onoff)
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
		.reset = dib80xx_tuner_reset,
		.sleep = dib80xx_tuner_sleep,
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
		.reset = dib80xx_tuner_reset,
		.sleep = dib80xx_tuner_sleep,
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

static int stk80xx_pid_filter(struct dvb_usb_adapter *adapter, int index,
	u16 pid, int onoff)
{
    return dib8000_pid_filter(adapter->fe, index, pid, onoff);
}

static int stk80xx_pid_filter_ctrl(struct dvb_usb_adapter *adapter,
	int onoff)
{
    return dib8000_pid_filter_ctrl(adapter->fe, onoff);
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
	dib8000_i2c_enumeration(&adap->dev->i2c_adap, 1, 0x22, 0x80);

	adap->fe = dvb_attach(dib8000_attach, &adap->dev->i2c_adap, 0x80,
			      &dib807x_dib8000_config[0]);

	return adap->fe == NULL ? -ENODEV : 0;
}

static int stk807xpvr_frontend_attach1(struct dvb_usb_adapter *adap)
{
	/* initialize IC 1 */
	dib8000_i2c_enumeration(&adap->dev->i2c_adap, 1, 0x12, 0x82);

	adap->fe = dvb_attach(dib8000_attach, &adap->dev->i2c_adap, 0x82,
			      &dib807x_dib8000_config[1]);

	return adap->fe == NULL ? -ENODEV : 0;
}

/* STK8096GP */
struct dibx000_agc_config dib8090_agc_config[2] = {
    {
	BAND_UHF | BAND_VHF | BAND_LBAND | BAND_SBAND,
	/* P_agc_use_sd_mod1=0, P_agc_use_sd_mod2=0, P_agc_freq_pwm_div=1,
     * P_agc_inv_pwm1=0, P_agc_inv_pwm2=0, P_agc_inh_dc_rv_est=0,
     * P_agc_time_est=3, P_agc_freeze=0, P_agc_nb_est=5, P_agc_write=0 */
	(0 << 15) | (0 << 14) | (5 << 11) | (0 << 10) | (0 << 9) | (0 << 8)
	| (3 << 5) | (0 << 4) | (5 << 1) | (0 << 0),

	787,
	10,

	0,
	118,

	0,
	3530,
	1,
	5,

	65535,
	0,

	65535,
	0,

	0,
	32,
	114,
	143,
	144,
	114,
	227,
	116,
	117,

	28,
	26,
	31,
	51,

	0,
    },
    {
	BAND_CBAND,
	/* P_agc_use_sd_mod1=0, P_agc_use_sd_mod2=0, P_agc_freq_pwm_div=1,
     * P_agc_inv_pwm1=0, P_agc_inv_pwm2=0, P_agc_inh_dc_rv_est=0,
     * P_agc_time_est=3, P_agc_freeze=0, P_agc_nb_est=5, P_agc_write=0 */
	(0 << 15) | (0 << 14) | (5 << 11) | (0 << 10) | (0 << 9) | (0 << 8)
	| (3 << 5) | (0 << 4) | (5 << 1) | (0 << 0),

	787,
	10,

	0,
	118,

	0,
	3530,
	1,
	5,

	0,
	0,

	65535,
	0,

	0,
	32,
	114,
	143,
	144,
	114,
	227,
	116,
	117,

	28,
	26,
	31,
	51,

	0,
    }
};

static struct dibx000_bandwidth_config dib8090_pll_config_12mhz = {
    54000, 13500,
    1, 18, 3, 1, 0,
    0, 0, 1, 1, 2,
    (3 << 14) | (1 << 12) | (599 << 0),
    (0 << 25) | 0,
    20199727,
    12000000,
};

static int dib8090_get_adc_power(struct dvb_frontend *fe)
{
    return dib8000_get_adc_power(fe, 1);
}

static struct dib8000_config dib809x_dib8000_config = {
    .output_mpeg2_in_188_bytes = 1,

    .agc_config_count = 2,
    .agc = dib8090_agc_config,
    .agc_control = dib0090_dcc_freq,
    .pll = &dib8090_pll_config_12mhz,
    .tuner_is_baseband = 1,

    .gpio_dir = DIB8000_GPIO_DEFAULT_DIRECTIONS,
    .gpio_val = DIB8000_GPIO_DEFAULT_VALUES,
    .gpio_pwm_pos = DIB8000_GPIO_DEFAULT_PWM_POS,

    .hostbus_diversity = 1,
    .div_cfg = 0x31,
    .output_mode = OUTMODE_MPEG2_FIFO,
    .drives = 0x2d98,
    .diversity_delay = 144,
    .refclksel = 3,
};

static struct dib0090_config dib809x_dib0090_config = {
    .io.pll_bypass = 1,
    .io.pll_range = 1,
    .io.pll_prediv = 1,
    .io.pll_loopdiv = 20,
    .io.adc_clock_ratio = 8,
    .io.pll_int_loop_filt = 0,
    .io.clock_khz = 12000,
    .reset = dib80xx_tuner_reset,
    .sleep = dib80xx_tuner_sleep,
    .clkouttobamse = 1,
    .analog_output = 1,
    .i2c_address = DEFAULT_DIB0090_I2C_ADDRESS,
    .wbd_vhf_offset = 100,
    .wbd_cband_offset = 450,
    .use_pwm_agc = 1,
    .clkoutdrive = 1,
    .get_adc_power = dib8090_get_adc_power,
	.freq_offset_khz_uhf = 0,
	.freq_offset_khz_vhf = -143,
};

static int dib8096_set_param_override(struct dvb_frontend *fe,
		struct dvb_frontend_parameters *fep)
{
    struct dvb_usb_adapter *adap = fe->dvb->priv;
    struct dib0700_adapter_state *state = adap->priv;
    u8 band = BAND_OF_FREQUENCY(fep->frequency/1000);
    u16 offset;
    int ret = 0;
    enum frontend_tune_state tune_state = CT_SHUTDOWN;
    u16 ltgain, rf_gain_limit;

    ret = state->set_param_save(fe, fep);
    if (ret < 0)
	return ret;

    switch (band) {
    case BAND_VHF:
	    offset = 100;
	    break;
    case BAND_UHF:
	    offset = 550;
	    break;
    default:
	    offset = 0;
	    break;
    }
    offset += (dib0090_get_wbd_offset(fe) * 8 * 18 / 33 + 1) / 2;
    dib8000_set_wbd_ref(fe, offset);


    if (band == BAND_CBAND) {
	deb_info("tuning in CBAND - soft-AGC startup\n");
	/* TODO specific wbd target for dib0090 - needed for startup ? */
	dib0090_set_tune_state(fe, CT_AGC_START);
	do {
		ret = dib0090_gain_control(fe);
		msleep(ret);
		tune_state = dib0090_get_tune_state(fe);
		if (tune_state == CT_AGC_STEP_0)
			dib8000_set_gpio(fe, 6, 0, 1);
		else if (tune_state == CT_AGC_STEP_1) {
			dib0090_get_current_gain(fe, NULL, NULL, &rf_gain_limit, &ltgain);
			if (rf_gain_limit == 0)
				dib8000_set_gpio(fe, 6, 0, 0);
		}
	} while (tune_state < CT_AGC_STOP);
	dib0090_pwm_gain_reset(fe);
	dib8000_pwm_agc_reset(fe);
	dib8000_set_tune_state(fe, CT_DEMOD_START);
    } else {
	deb_info("not tuning in CBAND - standard AGC startup\n");
	dib0090_pwm_gain_reset(fe);
    }

    return 0;
}

static int dib809x_tuner_attach(struct dvb_usb_adapter *adap)
{
    struct dib0700_adapter_state *st = adap->priv;
    struct i2c_adapter *tun_i2c = dib8000_get_i2c_master(adap->fe, DIBX000_I2C_INTERFACE_TUNER, 1);

    if (dvb_attach(dib0090_register, adap->fe, tun_i2c, &dib809x_dib0090_config) == NULL)
	return -ENODEV;

    st->set_param_save = adap->fe->ops.tuner_ops.set_params;
    adap->fe->ops.tuner_ops.set_params = dib8096_set_param_override;
    return 0;
}

static int stk809x_frontend_attach(struct dvb_usb_adapter *adap)
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

	dib8000_i2c_enumeration(&adap->dev->i2c_adap, 1, 18, 0x80);

	adap->fe = dvb_attach(dib8000_attach, &adap->dev->i2c_adap, 0x80, &dib809x_dib8000_config);

	return adap->fe == NULL ?  -ENODEV : 0;
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
	{ USB_DEVICE(USB_VID_PCTV,	USB_PID_PINNACLE_PCTV73ESE) },
	{ USB_DEVICE(USB_VID_PCTV,	USB_PID_PINNACLE_PCTV282E) },
	{ USB_DEVICE(USB_VID_DIBCOM,	USB_PID_DIBCOM_STK7770P) },
/* 60 */{ USB_DEVICE(USB_VID_TERRATEC,	USB_PID_TERRATEC_CINERGY_T_XXS_2) },
	{ USB_DEVICE(USB_VID_DIBCOM,    USB_PID_DIBCOM_STK807XPVR) },
	{ USB_DEVICE(USB_VID_DIBCOM,    USB_PID_DIBCOM_STK807XP) },
	{ USB_DEVICE_VER(USB_VID_PIXELVIEW, USB_PID_PIXELVIEW_SBTVD, 0x000, 0x3f00) },
	{ USB_DEVICE(USB_VID_EVOLUTEPC, USB_PID_TVWAY_PLUS) },
/* 65 */{ USB_DEVICE(USB_VID_PINNACLE,	USB_PID_PINNACLE_PCTV73ESE) },
	{ USB_DEVICE(USB_VID_PINNACLE,	USB_PID_PINNACLE_PCTV282E) },
	{ USB_DEVICE(USB_VID_DIBCOM,    USB_PID_DIBCOM_STK8096GP) },
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
				.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
				.pid_filter_count = 32,
				.pid_filter       = stk7700p_pid_filter,
				.pid_filter_ctrl  = stk7700p_pid_filter_ctrl,
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

		.rc.core = {
			.rc_interval      = DEFAULT_RC_INTERVAL,
			.rc_codes         = RC_MAP_DIB0700_RC5_TABLE,
			.rc_query         = dib0700_rc_query_old_firmware,
			.allowed_protos   = RC_TYPE_RC5 |
					    RC_TYPE_RC6 |
					    RC_TYPE_NEC,
			.change_protocol  = dib0700_change_protocol,
		},
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

		.rc.core = {
			.rc_interval      = DEFAULT_RC_INTERVAL,
			.rc_codes         = RC_MAP_DIB0700_RC5_TABLE,
			.rc_query         = dib0700_rc_query_old_firmware,
			.allowed_protos   = RC_TYPE_RC5 |
					    RC_TYPE_RC6 |
					    RC_TYPE_NEC,
			.change_protocol = dib0700_change_protocol,
		},
	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,

		.num_adapters = 2,
		.adapter = {
			{
				.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
				.pid_filter_count = 32,
				.pid_filter       = stk70x0p_pid_filter,
				.pid_filter_ctrl  = stk70x0p_pid_filter_ctrl,
				.frontend_attach  = stk7700d_frontend_attach,
				.tuner_attach     = stk7700d_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),
			}, {
				.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
				.pid_filter_count = 32,
				.pid_filter       = stk70x0p_pid_filter,
				.pid_filter_ctrl  = stk70x0p_pid_filter_ctrl,
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

		.rc.core = {
			.rc_interval      = DEFAULT_RC_INTERVAL,
			.rc_codes         = RC_MAP_DIB0700_RC5_TABLE,
			.rc_query         = dib0700_rc_query_old_firmware,
			.allowed_protos   = RC_TYPE_RC5 |
					    RC_TYPE_RC6 |
					    RC_TYPE_NEC,
			.change_protocol = dib0700_change_protocol,
		},
	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,

		.num_adapters = 1,
		.adapter = {
			{
				.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
				.pid_filter_count = 32,
				.pid_filter       = stk70x0p_pid_filter,
				.pid_filter_ctrl  = stk70x0p_pid_filter_ctrl,
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

		.rc.core = {
			.rc_interval      = DEFAULT_RC_INTERVAL,
			.rc_codes         = RC_MAP_DIB0700_RC5_TABLE,
			.module_name	  = "dib0700",
			.rc_query         = dib0700_rc_query_old_firmware,
			.allowed_protos   = RC_TYPE_RC5 |
					    RC_TYPE_RC6 |
					    RC_TYPE_NEC,
			.change_protocol = dib0700_change_protocol,
		},
	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,

		.num_adapters = 1,
		.adapter = {
			{
				.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
				.pid_filter_count = 32,
				.pid_filter       = stk70x0p_pid_filter,
				.pid_filter_ctrl  = stk70x0p_pid_filter_ctrl,
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

		.rc.core = {
			.rc_interval      = DEFAULT_RC_INTERVAL,
			.rc_codes         = RC_MAP_DIB0700_RC5_TABLE,
			.module_name	  = "dib0700",
			.rc_query         = dib0700_rc_query_old_firmware,
			.allowed_protos   = RC_TYPE_RC5 |
					    RC_TYPE_RC6 |
					    RC_TYPE_NEC,
			.change_protocol  = dib0700_change_protocol,
		},
	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,

		.num_adapters = 1,
		.adapter = {
			{
				.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
				.pid_filter_count = 32,
				.pid_filter       = stk70x0p_pid_filter,
				.pid_filter_ctrl  = stk70x0p_pid_filter_ctrl,
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
				{ &dib0700_usb_id_table[57], &dib0700_usb_id_table[65], NULL },
				{ NULL },
			},
			{   "Pinnacle PCTV 282e",
				{ &dib0700_usb_id_table[58], &dib0700_usb_id_table[66], NULL },
				{ NULL },
			},
		},

		.rc.core = {
			.rc_interval      = DEFAULT_RC_INTERVAL,
			.rc_codes         = RC_MAP_DIB0700_RC5_TABLE,
			.module_name	  = "dib0700",
			.rc_query         = dib0700_rc_query_old_firmware,
			.allowed_protos   = RC_TYPE_RC5 |
					    RC_TYPE_RC6 |
					    RC_TYPE_NEC,
			.change_protocol  = dib0700_change_protocol,
		},
	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,

		.num_adapters = 2,
		.adapter = {
			{
				.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
				.pid_filter_count = 32,
				.pid_filter       = stk70x0p_pid_filter,
				.pid_filter_ctrl  = stk70x0p_pid_filter_ctrl,
				.frontend_attach  = stk7070pd_frontend_attach0,
				.tuner_attach     = dib7070p_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),

				.size_of_priv     = sizeof(struct dib0700_adapter_state),
			}, {
				.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
				.pid_filter_count = 32,
				.pid_filter       = stk70x0p_pid_filter,
				.pid_filter_ctrl  = stk70x0p_pid_filter_ctrl,
				.frontend_attach  = stk7070pd_frontend_attach1,
				.tuner_attach     = dib7070p_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x03),

				.size_of_priv     = sizeof(struct dib0700_adapter_state),
			}
		},

		.num_device_descs = 6,
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
		},

		.rc.core = {
			.rc_interval      = DEFAULT_RC_INTERVAL,
			.rc_codes         = RC_MAP_DIB0700_RC5_TABLE,
			.module_name	  = "dib0700",
			.rc_query         = dib0700_rc_query_old_firmware,
			.allowed_protos   = RC_TYPE_RC5 |
					    RC_TYPE_RC6 |
					    RC_TYPE_NEC,
			.change_protocol = dib0700_change_protocol,
		},
	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,

		.num_adapters = 2,
		.adapter = {
			{
				.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
				.pid_filter_count = 32,
				.pid_filter       = stk70x0p_pid_filter,
				.pid_filter_ctrl  = stk70x0p_pid_filter_ctrl,
				.frontend_attach  = stk7070pd_frontend_attach0,
				.tuner_attach     = dib7070p_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),

				.size_of_priv     = sizeof(struct dib0700_adapter_state),
			}, {
				.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
				.pid_filter_count = 32,
				.pid_filter       = stk70x0p_pid_filter,
				.pid_filter_ctrl  = stk70x0p_pid_filter_ctrl,
				.frontend_attach  = stk7070pd_frontend_attach1,
				.tuner_attach     = dib7070p_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x03),

				.size_of_priv     = sizeof(struct dib0700_adapter_state),
			}
		},

		.num_device_descs = 1,
		.devices = {
			{   "Elgato EyeTV Diversity",
				{ &dib0700_usb_id_table[68], NULL },
				{ NULL },
			},
		},

		.rc.core = {
			.rc_interval      = DEFAULT_RC_INTERVAL,
			.rc_codes         = RC_MAP_DIB0700_NEC_TABLE,
			.module_name	  = "dib0700",
			.rc_query         = dib0700_rc_query_old_firmware,
			.allowed_protos   = RC_TYPE_RC5 |
					    RC_TYPE_RC6 |
					    RC_TYPE_NEC,
			.change_protocol  = dib0700_change_protocol,
		},
	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,

		.num_adapters = 1,
		.adapter = {
			{
				.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
				.pid_filter_count = 32,
				.pid_filter       = stk70x0p_pid_filter,
				.pid_filter_ctrl  = stk70x0p_pid_filter_ctrl,
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

		.rc.core = {
			.rc_interval      = DEFAULT_RC_INTERVAL,
			.rc_codes         = RC_MAP_DIB0700_RC5_TABLE,
			.module_name	  = "dib0700",
			.rc_query         = dib0700_rc_query_old_firmware,
			.allowed_protos   = RC_TYPE_RC5 |
					    RC_TYPE_RC6 |
					    RC_TYPE_NEC,
			.change_protocol  = dib0700_change_protocol,
		},
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

		.rc.core = {
			.rc_interval      = DEFAULT_RC_INTERVAL,
			.rc_codes         = RC_MAP_DIB0700_RC5_TABLE,
			.module_name	  = "dib0700",
			.rc_query         = dib0700_rc_query_old_firmware,
			.allowed_protos   = RC_TYPE_RC5 |
					    RC_TYPE_RC6 |
					    RC_TYPE_NEC,
			.change_protocol  = dib0700_change_protocol,
		},
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
				.caps = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
				.pid_filter_count = 32,
				.pid_filter       = stk70x0p_pid_filter,
				.pid_filter_ctrl  = stk70x0p_pid_filter_ctrl,
				.frontend_attach  = stk7770p_frontend_attach,
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

		.rc.core = {
			.rc_interval      = DEFAULT_RC_INTERVAL,
			.rc_codes         = RC_MAP_DIB0700_RC5_TABLE,
			.module_name	  = "dib0700",
			.rc_query         = dib0700_rc_query_old_firmware,
			.allowed_protos   = RC_TYPE_RC5 |
					    RC_TYPE_RC6 |
					    RC_TYPE_NEC,
			.change_protocol  = dib0700_change_protocol,
		},
	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,
		.num_adapters = 1,
		.adapter = {
			{
				.caps  = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
				.pid_filter_count = 32,
				.pid_filter = stk80xx_pid_filter,
				.pid_filter_ctrl = stk80xx_pid_filter_ctrl,
				.frontend_attach  = stk807x_frontend_attach,
				.tuner_attach     = dib807x_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),

				.size_of_priv =
					sizeof(struct dib0700_adapter_state),
			},
		},

		.num_device_descs = 3,
		.devices = {
			{   "DiBcom STK807xP reference design",
				{ &dib0700_usb_id_table[62], NULL },
				{ NULL },
			},
			{   "Prolink Pixelview SBTVD",
				{ &dib0700_usb_id_table[63], NULL },
				{ NULL },
			},
			{   "EvolutePC TVWay+",
				{ &dib0700_usb_id_table[64], NULL },
				{ NULL },
			},
		},

		.rc.core = {
			.rc_interval      = DEFAULT_RC_INTERVAL,
			.rc_codes         = RC_MAP_DIB0700_NEC_TABLE,
			.module_name	  = "dib0700",
			.rc_query         = dib0700_rc_query_old_firmware,
			.allowed_protos   = RC_TYPE_RC5 |
					    RC_TYPE_RC6 |
					    RC_TYPE_NEC,
			.change_protocol  = dib0700_change_protocol,
		},
	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,
		.num_adapters = 2,
		.adapter = {
			{
				.caps  = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
				.pid_filter_count = 32,
				.pid_filter = stk80xx_pid_filter,
				.pid_filter_ctrl = stk80xx_pid_filter_ctrl,
				.frontend_attach  = stk807xpvr_frontend_attach0,
				.tuner_attach     = dib807x_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),

				.size_of_priv =
					sizeof(struct dib0700_adapter_state),
			},
			{
				.caps  = DVB_USB_ADAP_HAS_PID_FILTER | DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
				.pid_filter_count = 32,
				.pid_filter = stk80xx_pid_filter,
				.pid_filter_ctrl = stk80xx_pid_filter_ctrl,
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

		.rc.core = {
			.rc_interval      = DEFAULT_RC_INTERVAL,
			.rc_codes         = RC_MAP_DIB0700_RC5_TABLE,
			.module_name	  = "dib0700",
			.rc_query         = dib0700_rc_query_old_firmware,
			.allowed_protos   = RC_TYPE_RC5 |
					    RC_TYPE_RC6 |
					    RC_TYPE_NEC,
			.change_protocol  = dib0700_change_protocol,
		},
	}, { DIB0700_DEFAULT_DEVICE_PROPERTIES,
		.num_adapters = 1,
		.adapter = {
			{
				.caps  = DVB_USB_ADAP_HAS_PID_FILTER |
					DVB_USB_ADAP_PID_FILTER_CAN_BE_TURNED_OFF,
				.pid_filter_count = 32,
				.pid_filter = stk80xx_pid_filter,
				.pid_filter_ctrl = stk80xx_pid_filter_ctrl,
				.frontend_attach  = stk809x_frontend_attach,
				.tuner_attach     = dib809x_tuner_attach,

				DIB0700_DEFAULT_STREAMING_CONFIG(0x02),

				.size_of_priv =
					sizeof(struct dib0700_adapter_state),
			},
		},

		.num_device_descs = 1,
		.devices = {
			{   "DiBcom STK8096GP reference design",
				{ &dib0700_usb_id_table[67], NULL },
				{ NULL },
			},
		},

		.rc.core = {
			.rc_interval      = DEFAULT_RC_INTERVAL,
			.rc_codes         = RC_MAP_DIB0700_RC5_TABLE,
			.module_name	  = "dib0700",
			.rc_query         = dib0700_rc_query_old_firmware,
			.allowed_protos   = RC_TYPE_RC5 |
					    RC_TYPE_RC6 |
					    RC_TYPE_NEC,
			.change_protocol  = dib0700_change_protocol,
		},
	},
};

int dib0700_device_count = ARRAY_SIZE(dib0700_devices);

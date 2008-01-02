/*
    tda18271-fe.c - driver for the Philips / NXP TDA18271 silicon tuner

    Copyright (C) 2007, 2008 Michael Krufky <mkrufky@linuxtv.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/delay.h>
#include <linux/videodev2.h>
#include "tda18271-priv.h"

int tda18271_debug;
module_param_named(debug, tda18271_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debug level (info=1, map=2, reg=4 (or-able))");

/*---------------------------------------------------------------------*/

static int tda18271_init(struct dvb_frontend *fe)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;

	tda18271_read_regs(fe);

	/* test IR_CAL_OK to see if we need init */
	if ((regs[R_EP1] & 0x08) == 0)
		tda18271_init_regs(fe);

	return 0;
}

/* ------------------------------------------------------------------ */

static int tda18271_channel_configuration(struct dvb_frontend *fe,
					  u32 ifc, u32 freq, u32 bw, u8 std)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	u32 N;

	/* update TV broadcast parameters */

	/* set standard */
	regs[R_EP3]  &= ~0x1f; /* clear std bits */
	regs[R_EP3]  |= std;

	/* set cal mode to normal */
	regs[R_EP4]  &= ~0x03;

	/* update IF output level & IF notch frequency */
	regs[R_EP4]  &= ~0x1c; /* clear if level bits */

	switch (priv->mode) {
	case TDA18271_ANALOG:
		regs[R_MPD]  &= ~0x80; /* IF notch = 0 */
		break;
	case TDA18271_DIGITAL:
		regs[R_EP4]  |= 0x04; /* IF level = 1 */
		regs[R_MPD]  |= 0x80; /* IF notch = 1 */
		break;
	}
	regs[R_EP4]  &= ~0x80; /* FM_RFn: turn this bit on only for fm radio */

	/* update RF_TOP / IF_TOP */
	switch (priv->mode) {
	case TDA18271_ANALOG:
		regs[R_EB22]  = 0x2c;
		break;
	case TDA18271_DIGITAL:
		regs[R_EB22]  = 0x37;
		break;
	}
	tda18271_write_regs(fe, R_EB22, 1);

	/* --------------------------------------------------------------- */

	/* disable Power Level Indicator */
	regs[R_EP1]  |= 0x40;

	/* frequency dependent parameters */

	tda18271_calc_ir_measure(fe, &freq);

	tda18271_calc_bp_filter(fe, &freq);

	tda18271_calc_rf_band(fe, &freq);

	tda18271_calc_gain_taper(fe, &freq);

	/* --------------------------------------------------------------- */

	/* dual tuner and agc1 extra configuration */

	/* main vco when Master, cal vco when slave */
	regs[R_EB1]  |= 0x04; /* FIXME: assumes master */

	/* agc1 always active */
	regs[R_EB1]  &= ~0x02;

	/* agc1 has priority on agc2 */
	regs[R_EB1]  &= ~0x01;

	tda18271_write_regs(fe, R_EB1, 1);

	/* --------------------------------------------------------------- */

	N = freq + ifc;

	/* FIXME: assumes master */
	tda18271_calc_main_pll(fe, N);
	tda18271_write_regs(fe, R_MPD, 4);

	tda18271_write_regs(fe, R_TM, 7);

	/* main pll charge pump source */
	regs[R_EB4] |= 0x20;
	tda18271_write_regs(fe, R_EB4, 1);

	msleep(1);

	/* normal operation for the main pll */
	regs[R_EB4] &= ~0x20;
	tda18271_write_regs(fe, R_EB4, 1);

	msleep(5);

	return 0;
}

static int tda18271_read_thermometer(struct dvb_frontend *fe)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	int tm;

	/* switch thermometer on */
	regs[R_TM]   |= 0x10;
	tda18271_write_regs(fe, R_TM, 1);

	/* read thermometer info */
	tda18271_read_regs(fe);

	if ((((regs[R_TM] & 0x0f) == 0x00) && ((regs[R_TM] & 0x20) == 0x20)) ||
	    (((regs[R_TM] & 0x0f) == 0x08) && ((regs[R_TM] & 0x20) == 0x00))) {

		if ((regs[R_TM] & 0x20) == 0x20)
			regs[R_TM] &= ~0x20;
		else
			regs[R_TM] |= 0x20;

		tda18271_write_regs(fe, R_TM, 1);

		msleep(10); /* temperature sensing */

		/* read thermometer info */
		tda18271_read_regs(fe);
	}

	tm = tda18271_lookup_thermometer(fe);

	/* switch thermometer off */
	regs[R_TM]   &= ~0x10;
	tda18271_write_regs(fe, R_TM, 1);

	/* set CAL mode to normal */
	regs[R_EP4]  &= ~0x03;
	tda18271_write_regs(fe, R_EP4, 1);

	return tm;
}

static int tda18271_rf_tracking_filters_correction(struct dvb_frontend *fe,
						   u32 freq, int tm_rfcal)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	struct tda18271_rf_tracking_filter_cal *map = priv->rf_cal_state;
	unsigned char *regs = priv->tda18271_regs;
	int tm_current, rfcal_comp, approx, i;
	u8 dc_over_dt, rf_tab;

	/* power up */
	regs[R_EP3]  &= ~0xe0; /* sm = 0, sm_lt = 0, sm_xt = 0 */
	tda18271_write_regs(fe, R_EP3, 1);

	/* read die current temperature */
	tm_current = tda18271_read_thermometer(fe);

	/* frequency dependent parameters */

	tda18271_calc_rf_cal(fe, &freq);
	rf_tab = regs[R_EB14];

	i = tda18271_lookup_rf_band(fe, &freq, NULL);
	if (i < 0)
		return -EINVAL;

	if ((0 == map[i].rf3) || (freq / 1000 < map[i].rf2)) {
		approx = map[i].rf_a1 *
			(freq / 1000 - map[i].rf1) + map[i].rf_b1 + rf_tab;
	} else {
		approx = map[i].rf_a2 *
			(freq / 1000 - map[i].rf2) + map[i].rf_b2 + rf_tab;
	}

	if (approx < 0)
		approx = 0;
	if (approx > 255)
		approx = 255;

	tda18271_lookup_map(fe, RF_CAL_DC_OVER_DT, &freq, &dc_over_dt);

	/* calculate temperature compensation */
	rfcal_comp = dc_over_dt * (tm_current - tm_rfcal);

	regs[R_EB14] = approx + rfcal_comp;
	tda18271_write_regs(fe, R_EB14, 1);

	return 0;
}

static int tda18271_por(struct dvb_frontend *fe)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;

	/* power up detector 1 */
	regs[R_EB12] &= ~0x20;
	tda18271_write_regs(fe, R_EB12, 1);

	regs[R_EB18] &= ~0x80; /* turn agc1 loop on */
	regs[R_EB18] &= ~0x03; /* set agc1_gain to  6 dB */
	tda18271_write_regs(fe, R_EB18, 1);

	regs[R_EB21] |= 0x03; /* set agc2_gain to -6 dB */

	/* POR mode */
	regs[R_EP3]  &= ~0xe0; /* clear sm, sm_lt, sm_xt */
	regs[R_EP3]  |= 0x80; /* sm = 1, sm_lt = 0, sm_xt = 0 */
	tda18271_write_regs(fe, R_EP3, 1);

	/* disable 1.5 MHz low pass filter */
	regs[R_EB23] &= ~0x04; /* forcelp_fc2_en = 0 */
	regs[R_EB23] &= ~0x02; /* XXX: lp_fc[2] = 0 */
	tda18271_write_regs(fe, R_EB21, 3);

	return 0;
}

static int tda18271_calibrate_rf(struct dvb_frontend *fe, u32 freq)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	u32 N;

	/* set CAL mode to normal */
	regs[R_EP4]  &= ~0x03;
	tda18271_write_regs(fe, R_EP4, 1);

	/* switch off agc1 */
	regs[R_EP3]  |= 0x40; /* sm_lt = 1 */

	regs[R_EB18] |= 0x03; /* set agc1_gain to 15 dB */
	tda18271_write_regs(fe, R_EB18, 1);

	/* frequency dependent parameters */

	tda18271_calc_bp_filter(fe, &freq);
	tda18271_calc_gain_taper(fe, &freq);
	tda18271_calc_rf_band(fe, &freq);
	tda18271_calc_km(fe, &freq);

	tda18271_write_regs(fe, R_EP1, 3);
	tda18271_write_regs(fe, R_EB13, 1);

	/* main pll charge pump source */
	regs[R_EB4]  |= 0x20;
	tda18271_write_regs(fe, R_EB4, 1);

	/* cal pll charge pump source */
	regs[R_EB7]  |= 0x20;
	tda18271_write_regs(fe, R_EB7, 1);

	/* force dcdc converter to 0 V */
	regs[R_EB14] = 0x00;
	tda18271_write_regs(fe, R_EB14, 1);

	/* disable plls lock */
	regs[R_EB20] &= ~0x20;
	tda18271_write_regs(fe, R_EB20, 1);

	/* set CAL mode to RF tracking filter calibration */
	regs[R_EP4]  |= 0x03;
	tda18271_write_regs(fe, R_EP4, 2);

	/* --------------------------------------------------------------- */

	/* set the internal calibration signal */
	N = freq;

	tda18271_calc_main_pll(fe, N);
	tda18271_write_regs(fe, R_MPD, 4);

	/* downconvert internal calibration */
	N += 1000000;

	tda18271_calc_main_pll(fe, N);
	tda18271_write_regs(fe, R_MPD, 4);

	msleep(5);

	tda18271_write_regs(fe, R_EP2, 1);
	tda18271_write_regs(fe, R_EP1, 1);
	tda18271_write_regs(fe, R_EP2, 1);
	tda18271_write_regs(fe, R_EP1, 1);

	/* --------------------------------------------------------------- */

	/* normal operation for the main pll */
	regs[R_EB4] &= ~0x20;
	tda18271_write_regs(fe, R_EB4, 1);

	/* normal operation for the cal pll  */
	regs[R_EB7] &= ~0x20;
	tda18271_write_regs(fe, R_EB7, 1);

	msleep(5); /* plls locking */

	/* launch the rf tracking filters calibration */
	regs[R_EB20]  |= 0x20;
	tda18271_write_regs(fe, R_EB20, 1);

	msleep(60); /* calibration */

	/* --------------------------------------------------------------- */

	/* set CAL mode to normal */
	regs[R_EP4]  &= ~0x03;

	/* switch on agc1 */
	regs[R_EP3]  &= ~0x40; /* sm_lt = 0 */

	regs[R_EB18] &= ~0x03; /* set agc1_gain to  6 dB */
	tda18271_write_regs(fe, R_EB18, 1);

	tda18271_write_regs(fe, R_EP3, 2);

	/* synchronization */
	tda18271_write_regs(fe, R_EP1, 1);

	/* get calibration result */
	tda18271_read_extended(fe);

	return regs[R_EB14];
}

static int tda18271_powerscan(struct dvb_frontend *fe,
			      u32 *freq_in, u32 *freq_out)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	int sgn, bcal, count, wait;
	u8 cid_target;
	u16 count_limit;
	u32 freq;

	freq = *freq_in;

	tda18271_calc_rf_band(fe, &freq);
	tda18271_calc_rf_cal(fe, &freq);
	tda18271_calc_gain_taper(fe, &freq);
	tda18271_lookup_cid_target(fe, &freq, &cid_target, &count_limit);

	tda18271_write_regs(fe, R_EP2, 1);
	tda18271_write_regs(fe, R_EB14, 1);

	/* downconvert frequency */
	freq += 1000000;

	tda18271_calc_main_pll(fe, freq);
	tda18271_write_regs(fe, R_MPD, 4);

	msleep(5); /* pll locking */

	/* detection mode */
	regs[R_EP4]  &= ~0x03;
	regs[R_EP4]  |= 0x01;
	tda18271_write_regs(fe, R_EP4, 1);

	/* launch power detection measurement */
	tda18271_write_regs(fe, R_EP2, 1);

	/* read power detection info, stored in EB10 */
	tda18271_read_extended(fe);

	/* algorithm initialization */
	sgn = 1;
	*freq_out = *freq_in;
	bcal = 0;
	count = 0;
	wait = false;

	while ((regs[R_EB10] & 0x3f) < cid_target) {
		/* downconvert updated freq to 1 MHz */
		freq = *freq_in + (sgn * count) + 1000000;

		tda18271_calc_main_pll(fe, freq);
		tda18271_write_regs(fe, R_MPD, 4);

		if (wait) {
			msleep(5); /* pll locking */
			wait = false;
		} else
			udelay(100); /* pll locking */

		/* launch power detection measurement */
		tda18271_write_regs(fe, R_EP2, 1);

		/* read power detection info, stored in EB10 */
		tda18271_read_extended(fe);

		count += 200;

		if (count < count_limit)
			continue;

		if (sgn <= 0)
			break;

		sgn = -1 * sgn;
		count = 200;
		wait = true;
	}

	if ((regs[R_EB10] & 0x3f) >= cid_target) {
		bcal = 1;
		*freq_out = freq - 1000000;
	} else
		bcal = 0;

	tda_dbg("bcal = %d, freq_in = %d, freq_out = %d (freq = %d)\n",
		bcal, *freq_in, *freq_out, freq);

	return bcal;
}

static int tda18271_powerscan_init(struct dvb_frontend *fe)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;

	/* set standard to digital */
	regs[R_EP3]  &= ~0x1f; /* clear std bits */
	regs[R_EP3]  |= 0x12;

	/* set cal mode to normal */
	regs[R_EP4]  &= ~0x03;

	/* update IF output level & IF notch frequency */
	regs[R_EP4]  &= ~0x1c; /* clear if level bits */

	tda18271_write_regs(fe, R_EP3, 2);

	regs[R_EB18] &= ~0x03; /* set agc1_gain to   6 dB */
	tda18271_write_regs(fe, R_EB18, 1);

	regs[R_EB21] &= ~0x03; /* set agc2_gain to -15 dB */

	/* 1.5 MHz low pass filter */
	regs[R_EB23] |= 0x04; /* forcelp_fc2_en = 1 */
	regs[R_EB23] |= 0x02; /* lp_fc[2] = 1 */

	tda18271_write_regs(fe, R_EB21, 3);

	return 0;
}

static int tda18271_rf_tracking_filters_init(struct dvb_frontend *fe, u32 freq)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	struct tda18271_rf_tracking_filter_cal *map = priv->rf_cal_state;
	unsigned char *regs = priv->tda18271_regs;
	int bcal, rf, i;
#define RF1 0
#define RF2 1
#define RF3 2
	u32 rf_default[3];
	u32 rf_freq[3];
	u8 prog_cal[3];
	u8 prog_tab[3];

	i = tda18271_lookup_rf_band(fe, &freq, NULL);

	if (i < 0)
		return i;

	rf_default[RF1] = 1000 * map[i].rf1_def;
	rf_default[RF2] = 1000 * map[i].rf2_def;
	rf_default[RF3] = 1000 * map[i].rf3_def;

	for (rf = RF1; rf <= RF3; rf++) {
		if (0 == rf_default[rf])
			return 0;
		tda_dbg("freq = %d, rf = %d\n", freq, rf);

		/* look for optimized calibration frequency */
		bcal = tda18271_powerscan(fe, &rf_default[rf], &rf_freq[rf]);

		tda18271_calc_rf_cal(fe, &rf_freq[rf]);
		prog_tab[rf] = regs[R_EB14];

		if (1 == bcal)
			prog_cal[rf] = tda18271_calibrate_rf(fe, rf_freq[rf]);
		else
			prog_cal[rf] = prog_tab[rf];

		switch (rf) {
		case RF1:
			map[i].rf_a1 = 0;
			map[i].rf_b1 = prog_cal[RF1] - prog_tab[RF1];
			map[i].rf1   = rf_freq[RF1] / 1000;
			break;
		case RF2:
			map[i].rf_a1 = (prog_cal[RF2] - prog_tab[RF2] -
					prog_cal[RF1] + prog_tab[RF1]) /
				((rf_freq[RF2] - rf_freq[RF1]) / 1000);
			map[i].rf2   = rf_freq[RF2] / 1000;
			break;
		case RF3:
			map[i].rf_a2 = (prog_cal[RF3] - prog_tab[RF3] -
					prog_cal[RF2] + prog_tab[RF2]) /
				((rf_freq[RF3] - rf_freq[RF2]) / 1000);
			map[i].rf_b2 = prog_cal[RF2] - prog_tab[RF2];
			map[i].rf3   = rf_freq[RF3] / 1000;
			break;
		default:
			BUG();
		}
	}

	return 0;
}

static int tda18271_calc_rf_filter_curve(struct dvb_frontend *fe,
					 int *tm_rfcal)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned int i;

	tda_info("tda18271: performing RF tracking filter calibration\n");

	/* wait for die temperature stabilization */
	msleep(200);

	tda18271_powerscan_init(fe);

	/* rf band calibration */
	for (i = 0; priv->rf_cal_state[i].rfmax != 0; i++)
		tda18271_rf_tracking_filters_init(fe, 1000 *
						  priv->rf_cal_state[i].rfmax);

	*tm_rfcal = tda18271_read_thermometer(fe);

	return 0;
}

/* ------------------------------------------------------------------ */

static int tda18271_init_cal(struct dvb_frontend *fe, int *tm)
{
	struct tda18271_priv *priv = fe->tuner_priv;

	if (priv->cal_initialized)
		return 0;

	/* initialization */
	tda18271_init(fe);

	tda18271_calc_rf_filter_curve(fe, tm);

	tda18271_por(fe);

	priv->cal_initialized = true;

	return 0;
}

static int tda18271c2_tune(struct dvb_frontend *fe,
			   u32 ifc, u32 freq, u32 bw, u8 std)
{
	int tm = 0;

	tda_dbg("freq = %d, ifc = %d\n", freq, ifc);

	tda18271_init_cal(fe, &tm);

	tda18271_rf_tracking_filters_correction(fe, freq, tm);

	tda18271_channel_configuration(fe, ifc, freq, bw, std);

	return 0;
}

/* ------------------------------------------------------------------ */

static int tda18271c1_tune(struct dvb_frontend *fe,
			   u32 ifc, u32 freq, u32 bw, u8 std)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	u32 N = 0;

	tda18271_init(fe);

	tda_dbg("freq = %d, ifc = %d\n", freq, ifc);

	/* RF tracking filter calibration */

	/* calculate bp filter */
	tda18271_calc_bp_filter(fe, &freq);
	tda18271_write_regs(fe, R_EP1, 1);

	regs[R_EB4]  &= 0x07;
	regs[R_EB4]  |= 0x60;
	tda18271_write_regs(fe, R_EB4, 1);

	regs[R_EB7]   = 0x60;
	tda18271_write_regs(fe, R_EB7, 1);

	regs[R_EB14]  = 0x00;
	tda18271_write_regs(fe, R_EB14, 1);

	regs[R_EB20]  = 0xcc;
	tda18271_write_regs(fe, R_EB20, 1);

	/* set cal mode to RF tracking filter calibration */
	regs[R_EP4]  |= 0x03;

	/* calculate cal pll */

	switch (priv->mode) {
	case TDA18271_ANALOG:
		N = freq - 1250000;
		break;
	case TDA18271_DIGITAL:
		N = freq + bw / 2;
		break;
	}

	tda18271_calc_cal_pll(fe, N);

	/* calculate main pll */

	switch (priv->mode) {
	case TDA18271_ANALOG:
		N = freq - 250000;
		break;
	case TDA18271_DIGITAL:
		N = freq + bw / 2 + 1000000;
		break;
	}

	tda18271_calc_main_pll(fe, N);

	tda18271_write_regs(fe, R_EP3, 11);
	msleep(5); /* RF tracking filter calibration initialization */

	/* search for K,M,CO for RF calibration */
	tda18271_calc_km(fe, &freq);
	tda18271_write_regs(fe, R_EB13, 1);

	/* search for rf band */
	tda18271_calc_rf_band(fe, &freq);

	/* search for gain taper */
	tda18271_calc_gain_taper(fe, &freq);

	tda18271_write_regs(fe, R_EP2, 1);
	tda18271_write_regs(fe, R_EP1, 1);
	tda18271_write_regs(fe, R_EP2, 1);
	tda18271_write_regs(fe, R_EP1, 1);

	regs[R_EB4]  &= 0x07;
	regs[R_EB4]  |= 0x40;
	tda18271_write_regs(fe, R_EB4, 1);

	regs[R_EB7]   = 0x40;
	tda18271_write_regs(fe, R_EB7, 1);
	msleep(10);

	regs[R_EB20]  = 0xec;
	tda18271_write_regs(fe, R_EB20, 1);
	msleep(60); /* RF tracking filter calibration completion */

	regs[R_EP4]  &= ~0x03; /* set cal mode to normal */
	tda18271_write_regs(fe, R_EP4, 1);

	tda18271_write_regs(fe, R_EP1, 1);

	/* RF tracking filter correction for VHF_Low band */
	if (0 == tda18271_calc_rf_cal(fe, &freq))
		tda18271_write_regs(fe, R_EB14, 1);

	/* Channel Configuration */

	switch (priv->mode) {
	case TDA18271_ANALOG:
		regs[R_EB22]  = 0x2c;
		break;
	case TDA18271_DIGITAL:
		regs[R_EB22]  = 0x37;
		break;
	}
	tda18271_write_regs(fe, R_EB22, 1);

	regs[R_EP1]  |= 0x40; /* set dis power level on */

	/* set standard */
	regs[R_EP3]  &= ~0x1f; /* clear std bits */

	/* see table 22 */
	regs[R_EP3]  |= std;

	regs[R_EP4]  &= ~0x03; /* set cal mode to normal */

	regs[R_EP4]  &= ~0x1c; /* clear if level bits */
	switch (priv->mode) {
	case TDA18271_ANALOG:
		regs[R_MPD]  &= ~0x80; /* IF notch = 0 */
		break;
	case TDA18271_DIGITAL:
		regs[R_EP4]  |= 0x04;
		regs[R_MPD]  |= 0x80;
		break;
	}

	regs[R_EP4]  &= ~0x80; /* turn this bit on only for fm */

	/* image rejection validity */
	tda18271_calc_ir_measure(fe, &freq);

	/* calculate MAIN PLL */
	N = freq + ifc;

	tda18271_calc_main_pll(fe, N);

	tda18271_write_regs(fe, R_TM, 15);
	msleep(5);

	return 0;
}

/* ------------------------------------------------------------------ */

static int tda18271_set_params(struct dvb_frontend *fe,
			       struct dvb_frontend_parameters *params)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	struct tda18271_std_map *std_map = priv->std;
	u8 std;
	u32 bw, sgIF = 0;
	u32 freq = params->frequency;

	BUG_ON(!priv->tune || !priv->std);

	priv->mode = TDA18271_DIGITAL;

	/* see table 22 */
	if (fe->ops.info.type == FE_ATSC) {
		switch (params->u.vsb.modulation) {
		case VSB_8:
		case VSB_16:
			std  = std_map->atsc_6.std_bits;
			sgIF = std_map->atsc_6.if_freq;
			break;
		case QAM_64:
		case QAM_256:
			std  = std_map->qam_6.std_bits;
			sgIF = std_map->qam_6.if_freq;
			break;
		default:
			tda_warn("modulation not set!\n");
			return -EINVAL;
		}
#if 0
		/* userspace request is already center adjusted */
		freq += 1750000; /* Adjust to center (+1.75MHZ) */
#endif
		bw = 6000000;
	} else if (fe->ops.info.type == FE_OFDM) {
		switch (params->u.ofdm.bandwidth) {
		case BANDWIDTH_6_MHZ:
			bw = 6000000;
			std  = std_map->dvbt_6.std_bits;
			sgIF = std_map->dvbt_6.if_freq;
			break;
		case BANDWIDTH_7_MHZ:
			bw = 7000000;
			std  = std_map->dvbt_7.std_bits;
			sgIF = std_map->dvbt_7.if_freq;
			break;
		case BANDWIDTH_8_MHZ:
			bw = 8000000;
			std  = std_map->dvbt_8.std_bits;
			sgIF = std_map->dvbt_8.if_freq;
			break;
		default:
			tda_warn("bandwidth not set!\n");
			return -EINVAL;
		}
	} else {
		tda_warn("modulation type not supported!\n");
		return -EINVAL;
	}

	return priv->tune(fe, sgIF, freq, bw, std);
}

static int tda18271_set_analog_params(struct dvb_frontend *fe,
				      struct analog_parameters *params)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	struct tda18271_std_map *std_map = priv->std;
	char *mode;
	u8 std;
	u32 sgIF, freq = params->frequency * 62500;

	BUG_ON(!priv->tune || !priv->std);

	priv->mode = TDA18271_ANALOG;

	if (params->std & V4L2_STD_MN) {
		std  = std_map->atv_mn.std_bits;
		sgIF = std_map->atv_mn.if_freq;
		mode = "MN";
	} else if (params->std & V4L2_STD_B) {
		std  = std_map->atv_b.std_bits;
		sgIF = std_map->atv_b.if_freq;
		mode = "B";
	} else if (params->std & V4L2_STD_GH) {
		std  = std_map->atv_gh.std_bits;
		sgIF = std_map->atv_gh.if_freq;
		mode = "GH";
	} else if (params->std & V4L2_STD_PAL_I) {
		std  = std_map->atv_i.std_bits;
		sgIF = std_map->atv_i.if_freq;
		mode = "I";
	} else if (params->std & V4L2_STD_DK) {
		std  = std_map->atv_dk.std_bits;
		sgIF = std_map->atv_dk.if_freq;
		mode = "DK";
	} else if (params->std & V4L2_STD_SECAM_L) {
		std  = std_map->atv_l.std_bits;
		sgIF = std_map->atv_l.if_freq;
		mode = "L";
	} else if (params->std & V4L2_STD_SECAM_LC) {
		std  = std_map->atv_lc.std_bits;
		sgIF = std_map->atv_lc.if_freq;
		mode = "L'";
	} else {
		std  = std_map->atv_i.std_bits;
		sgIF = std_map->atv_i.if_freq;
		mode = "xx";
	}

	tda_dbg("setting tda18271 to system %s\n", mode);

	return priv->tune(fe, sgIF, freq, 0, std);
}

static int tda18271_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
	return 0;
}

static int tda18271_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	*frequency = priv->frequency;
	return 0;
}

static int tda18271_get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	*bandwidth = priv->bandwidth;
	return 0;
}

static int tda18271_get_id(struct dvb_frontend *fe)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	char *name;
	int ret = 0;

	tda18271_read_regs(fe);

	switch (regs[R_ID] & 0x7f) {
	case 3:
		name = "TDA18271HD/C1";
		priv->id = TDA18271HDC1;
		priv->tune = tda18271c1_tune;
		break;
	case 4:
		name = "TDA18271HD/C2";
		priv->id = TDA18271HDC2;
		priv->tune = tda18271c2_tune;
		break;
	default:
		name = "Unknown device";
		ret = -EINVAL;
		break;
	}

	tda_info("%s detected @ %d-%04x%s\n", name,
		 i2c_adapter_id(priv->i2c_adap), priv->i2c_addr,
		 (0 == ret) ? "" : ", device not supported.");

	return ret;
}

static struct dvb_tuner_ops tda18271_tuner_ops = {
	.info = {
		.name = "NXP TDA18271HD",
		.frequency_min  =  45000000,
		.frequency_max  = 864000000,
		.frequency_step =     62500
	},
	.init              = tda18271_init,
	.set_params        = tda18271_set_params,
	.set_analog_params = tda18271_set_analog_params,
	.release           = tda18271_release,
	.get_frequency     = tda18271_get_frequency,
	.get_bandwidth     = tda18271_get_bandwidth,
};

struct dvb_frontend *tda18271_attach(struct dvb_frontend *fe, u8 addr,
				     struct i2c_adapter *i2c,
				     enum tda18271_i2c_gate gate)
{
	struct tda18271_priv *priv = NULL;

	priv = kzalloc(sizeof(struct tda18271_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;

	priv->i2c_addr = addr;
	priv->i2c_adap = i2c;
	priv->gate = gate;
	priv->cal_initialized = false;

	fe->tuner_priv = priv;

	if (tda18271_get_id(fe) < 0)
		goto fail;

	if (tda18271_assign_map_layout(fe) < 0)
		goto fail;

	memcpy(&fe->ops.tuner_ops, &tda18271_tuner_ops,
	       sizeof(struct dvb_tuner_ops));

	tda18271_init_regs(fe);

	return fe;
fail:
	tda18271_release(fe);
	return NULL;
}
EXPORT_SYMBOL_GPL(tda18271_attach);
MODULE_DESCRIPTION("NXP TDA18271HD analog / digital tuner driver");
MODULE_AUTHOR("Michael Krufky <mkrufky@linuxtv.org>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.2");

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

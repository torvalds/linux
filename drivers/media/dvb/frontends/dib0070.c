/*
 * Linux-DVB Driver for DiBcom's DiB0070 base-band RF Tuner.
 *
 * Copyright (C) 2005-7 DiBcom (http://www.dibcom.fr/)
 *
 * This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 */
#include <linux/kernel.h>
#include <linux/i2c.h>

#include "dvb_frontend.h"

#include "dib0070.h"
#include "dibx000_common.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "turn on debugging (default: 0)");

#define dprintk(args...) do { if (debug) { printk(KERN_DEBUG "DiB0070: "); printk(args); printk("\n"); } } while (0)

#define DIB0070_P1D  0x00
#define DIB0070_P1F  0x01
#define DIB0070_P1G  0x03
#define DIB0070S_P1A 0x02

struct dib0070_state {
	struct i2c_adapter *i2c;
	struct dvb_frontend *fe;
	const struct dib0070_config *cfg;
	u16 wbd_ff_offset;
	u8 revision;
};

static uint16_t dib0070_read_reg(struct dib0070_state *state, u8 reg)
{
	u8 b[2];
	struct i2c_msg msg[2] = {
		{ .addr = state->cfg->i2c_address, .flags = 0,        .buf = &reg, .len = 1 },
		{ .addr = state->cfg->i2c_address, .flags = I2C_M_RD, .buf = b,  .len = 2 },
	};
	if (i2c_transfer(state->i2c, msg, 2) != 2) {
		printk(KERN_WARNING "DiB0070 I2C read failed\n");
		return 0;
	}
	return (b[0] << 8) | b[1];
}

static int dib0070_write_reg(struct dib0070_state *state, u8 reg, u16 val)
{
	u8 b[3] = { reg, val >> 8, val & 0xff };
	struct i2c_msg msg = { .addr = state->cfg->i2c_address, .flags = 0, .buf = b, .len = 3 };
	if (i2c_transfer(state->i2c, &msg, 1) != 1) {
		printk(KERN_WARNING "DiB0070 I2C write failed\n");
		return -EREMOTEIO;
	}
	return 0;
}

#define HARD_RESET(state) do { if (state->cfg->reset) { state->cfg->reset(state->fe,1); msleep(10); state->cfg->reset(state->fe,0); msleep(10); } } while (0)

static int dib0070_set_bandwidth(struct dvb_frontend *fe, struct dvb_frontend_parameters *ch)
{
	struct dib0070_state *st = fe->tuner_priv;
	u16 tmp = 0;
	tmp = dib0070_read_reg(st, 0x02) & 0x3fff;

    switch(BANDWIDTH_TO_KHZ(ch->u.ofdm.bandwidth)) {
		case  8000:
			tmp |= (0 << 14);
			break;
		case  7000:
			tmp |= (1 << 14);
			break;
	case  6000:
			tmp |= (2 << 14);
			break;
	case 5000:
		default:
			tmp |= (3 << 14);
			break;
	}
	dib0070_write_reg(st, 0x02, tmp);
	return 0;
}

static void dib0070_captrim(struct dib0070_state *st, u16 LO4)
{
	int8_t captrim, fcaptrim, step_sign, step;
	u16 adc, adc_diff = 3000;



	dib0070_write_reg(st, 0x0f, 0xed10);
	dib0070_write_reg(st, 0x17,    0x0034);

	dib0070_write_reg(st, 0x18, 0x0032);
	msleep(2);

	step = captrim = fcaptrim = 64;

	do {
		step /= 2;
		dib0070_write_reg(st, 0x14, LO4 | captrim);
		msleep(1);
		adc = dib0070_read_reg(st, 0x19);

		dprintk( "CAPTRIM=%hd; ADC = %hd (ADC) & %dmV", captrim, adc, (u32) adc*(u32)1800/(u32)1024);

		if (adc >= 400) {
			adc -= 400;
			step_sign = -1;
		} else {
			adc = 400 - adc;
			step_sign = 1;
		}

		if (adc < adc_diff) {
			dprintk( "CAPTRIM=%hd is closer to target (%hd/%hd)", captrim, adc, adc_diff);
			adc_diff = adc;
			fcaptrim = captrim;



		}
		captrim += (step_sign * step);
	} while (step >= 1);

	dib0070_write_reg(st, 0x14, LO4 | fcaptrim);
	dib0070_write_reg(st, 0x18, 0x07ff);
}

#define LPF	100                       // define for the loop filter 100kHz by default 16-07-06
#define LO4_SET_VCO_HFDIV(l, v, h)   l |= ((v) << 11) | ((h) << 7)
#define LO4_SET_SD(l, s)             l |= ((s) << 14) | ((s) << 12)
#define LO4_SET_CTRIM(l, c)          l |=  (c) << 10
static int dib0070_tune_digital(struct dvb_frontend *fe, struct dvb_frontend_parameters *ch)
{
	struct dib0070_state *st = fe->tuner_priv;
	u32 freq = ch->frequency/1000 + (BAND_OF_FREQUENCY(ch->frequency/1000) == BAND_VHF ? st->cfg->freq_offset_khz_vhf : st->cfg->freq_offset_khz_uhf);

	u8 band = BAND_OF_FREQUENCY(freq), c;

	/*******************VCO***********************************/
	u16 lo4 = 0;

	u8 REFDIV, PRESC = 2;
	u32 FBDiv, Rest, FREF, VCOF_kHz;
	u16 Num, Den;
	/*******************FrontEnd******************************/
	u16 value = 0;

	dprintk( "Tuning for Band: %hd (%d kHz)", band, freq);


	dib0070_write_reg(st, 0x17, 0x30);

	dib0070_set_bandwidth(fe, ch);	/* c is used as HF */
	switch (st->revision) {
		case DIB0070S_P1A:
			switch (band) {
				case BAND_LBAND:
					LO4_SET_VCO_HFDIV(lo4, 1, 1);
					c = 2;
					break;
				case BAND_SBAND:
					LO4_SET_VCO_HFDIV(lo4, 0, 0);
					LO4_SET_CTRIM(lo4, 1);;
					c = 1;
					break;
				case BAND_UHF:
				default:
					if (freq < 570000) {
						LO4_SET_VCO_HFDIV(lo4, 1, 3);
						PRESC = 6; c = 6;
					} else if (freq < 680000) {
						LO4_SET_VCO_HFDIV(lo4, 0, 2);
						c = 4;
					} else {
						LO4_SET_VCO_HFDIV(lo4, 1, 2);
						c = 4;
					}
					break;
			} break;

		case DIB0070_P1G:
		case DIB0070_P1F:
		default:
			switch (band) {
				case BAND_FM:
						LO4_SET_VCO_HFDIV(lo4, 0, 7);
						c = 24;
					break;
				case BAND_LBAND:
						LO4_SET_VCO_HFDIV(lo4, 1, 0);
						c = 2;
					break;
				case BAND_VHF:
					if (freq < 180000) {
						LO4_SET_VCO_HFDIV(lo4, 0, 3);
						c = 16;
					} else if (freq < 190000) {
						LO4_SET_VCO_HFDIV(lo4, 1, 3);
						c = 16;
					} else {
						LO4_SET_VCO_HFDIV(lo4, 0, 6);
						c = 12;
					}
					break;

				case BAND_UHF:
				default:
					if (freq < 570000) {
						LO4_SET_VCO_HFDIV(lo4, 1, 5);
						c = 6;
					} else if (freq < 700000) {
						LO4_SET_VCO_HFDIV(lo4, 0, 1);
						c = 4;
					} else {
						LO4_SET_VCO_HFDIV(lo4, 1, 1);
						c = 4;
					}
					break;
			}
		break;
	}

	dprintk( "HFDIV code: %hd", (lo4 >> 7) & 0xf);
	dprintk( "VCO = %hd", (lo4 >> 11) & 0x3);


	VCOF_kHz = (c * freq) * 2;
	dprintk( "VCOF in kHz: %d ((%hd*%d) << 1))",VCOF_kHz, c, freq);

	switch (band) {
		case BAND_VHF:
			REFDIV = (u8) ((st->cfg->clock_khz + 9999) / 10000);
			break;
		case BAND_FM:
			REFDIV = (u8) ((st->cfg->clock_khz) / 1000);
			break;
		default:
			REFDIV = (u8) ( st->cfg->clock_khz  / 10000);
			break;
	}
	FREF = st->cfg->clock_khz / REFDIV;

	dprintk( "REFDIV: %hd, FREF: %d", REFDIV, FREF);



	switch (st->revision) {
		case DIB0070S_P1A:
			FBDiv = (VCOF_kHz / PRESC / FREF);
			Rest  = (VCOF_kHz / PRESC) - FBDiv * FREF;
			break;

		case DIB0070_P1G:
		case DIB0070_P1F:
		default:
			FBDiv = (freq / (FREF / 2));
			Rest  = 2 * freq - FBDiv * FREF;
			break;
	}


	     if (Rest < LPF)              Rest = 0;
	else if (Rest < 2 * LPF)          Rest = 2 * LPF;
	else if (Rest > (FREF - LPF))   { Rest = 0 ; FBDiv += 1; }
	else if (Rest > (FREF - 2 * LPF)) Rest = FREF - 2 * LPF;
	Rest = (Rest * 6528) / (FREF / 10);
	dprintk( "FBDIV: %d, Rest: %d", FBDiv, Rest);

	Num = 0;
	Den = 1;

	if (Rest > 0) {
		LO4_SET_SD(lo4, 1);
		Den = 255;
		Num = (u16)Rest;
	}
	dprintk( "Num: %hd, Den: %hd, SD: %hd",Num, Den, (lo4 >> 12) & 0x1);



	dib0070_write_reg(st, 0x11, (u16)FBDiv);


	dib0070_write_reg(st, 0x12, (Den << 8) | REFDIV);


	dib0070_write_reg(st, 0x13, Num);


	value = 0x0040 | 0x0020 | 0x0010 | 0x0008 | 0x0002 | 0x0001;

	switch (band) {
		case BAND_UHF:   value |= 0x4000 | 0x0800; break;
		case BAND_LBAND: value |= 0x2000 | 0x0400; break;
		default:         value |= 0x8000 | 0x1000; break;
	}
	dib0070_write_reg(st, 0x20, value);

	dib0070_captrim(st, lo4);
	if (st->revision == DIB0070S_P1A) {
		if (band == BAND_SBAND)
			dib0070_write_reg(st, 0x15, 0x16e2);
		else
			dib0070_write_reg(st, 0x15, 0x56e5);
	}



	switch (band) {
		case BAND_UHF:   value = 0x7c82; break;
		case BAND_LBAND: value = 0x7c84; break;
		default:         value = 0x7c81; break;
	}
	dib0070_write_reg(st, 0x0f, value);
	dib0070_write_reg(st, 0x06, 0x3fff);

	/* Front End */
	/* c == TUNE, value = SWITCH */
	c = 0;
	value = 0;
	switch (band) {
		case BAND_FM:
			c = 0; value = 1;
		break;

		case BAND_VHF:
			if (freq <= 180000) c = 0;
			else if (freq <= 188200) c = 1;
			else if (freq <= 196400) c = 2;
			else c = 3;
			value = 1;
		break;

		case BAND_LBAND:
			if (freq <= 1500000) c = 0;
			else if (freq <= 1600000) c = 1;
			else c = 3;
		break;

		case BAND_SBAND:
			c = 7;
			dib0070_write_reg(st, 0x1d,0xFFFF);
		break;

		case BAND_UHF:
		default:
			if (st->cfg->flip_chip) {
				if (freq <= 550000) c = 0;
				else if (freq <= 590000) c = 1;
				else if (freq <= 666000) c = 3;
				else c = 5;
			} else {
				if (freq <= 550000) c = 2;
				else if (freq <= 650000) c = 3;
				else if (freq <= 750000) c = 5;
				else if (freq <= 850000) c = 6;
				else c = 7;
			}
			value = 2;
		break;
	}

	/* default: LNA_MATCH=7, BIAS=3 */
	dib0070_write_reg(st, 0x07, (value << 11) | (7 << 8) | (c << 3) | (3 << 0));
	dib0070_write_reg(st, 0x08, (c << 10) | (3 << 7) | (127));
	dib0070_write_reg(st, 0x0d, 0x0d80);


	dib0070_write_reg(st, 0x18,   0x07ff);
	dib0070_write_reg(st, 0x17, 0x0033);

	return 0;
}

static int dib0070_wakeup(struct dvb_frontend *fe)
{
	struct dib0070_state *st = fe->tuner_priv;
	if (st->cfg->sleep)
		st->cfg->sleep(fe, 0);
	return 0;
}

static int dib0070_sleep(struct dvb_frontend *fe)
{
	struct dib0070_state *st = fe->tuner_priv;
	if (st->cfg->sleep)
		st->cfg->sleep(fe, 1);
	return 0;
}

static u16 dib0070_p1f_defaults[] =

{
	7, 0x02,
		0x0008,
		0x0000,
		0x0000,
		0x0000,
		0x0000,
		0x0002,
		0x0100,

	3, 0x0d,
		0x0d80,
		0x0001,
		0x0000,

	4, 0x11,
		0x0000,
		0x0103,
		0x0000,
		0x0000,

	3, 0x16,
		0x0004 | 0x0040,
		0x0030,
		0x07ff,

	6, 0x1b,
		0x4112,
		0xff00,
		0xc07f,
		0x0000,
		0x0180,
		0x4000 | 0x0800 | 0x0040 | 0x0020 | 0x0010 | 0x0008 | 0x0002 | 0x0001,

	0,
};

static void dib0070_wbd_calibration(struct dvb_frontend *fe)
{
	u16 wbd_offs;
	struct dib0070_state *state = fe->tuner_priv;

	if (state->cfg->sleep)
		state->cfg->sleep(fe, 0);

	dib0070_write_reg(state, 0x0f, 0x6d81);
	dib0070_write_reg(state, 0x20, 0x0040 | 0x0020 | 0x0010 | 0x0008 | 0x0002 | 0x0001);
	msleep(9);
	wbd_offs = dib0070_read_reg(state, 0x19);
	dib0070_write_reg(state, 0x20, 0);
	state->wbd_ff_offset = ((wbd_offs * 8 * 18 / 33 + 1) / 2);
	dprintk( "WBDStart = %d (Vargen) - FF = %hd", (u32) wbd_offs * 1800/1024, state->wbd_ff_offset);

	if (state->cfg->sleep)
		state->cfg->sleep(fe, 1);

}

u16 dib0070_wbd_offset(struct dvb_frontend *fe)
{
	struct dib0070_state *st = fe->tuner_priv;
	return st->wbd_ff_offset;
}

EXPORT_SYMBOL(dib0070_wbd_offset);
static int dib0070_set_ctrl_lo5(struct dvb_frontend *fe, u8 vco_bias_trim, u8 hf_div_trim, u8 cp_current, u8 third_order_filt)
{
	struct dib0070_state *state = fe->tuner_priv;
    u16 lo5 = (third_order_filt << 14) | (0 << 13) | (1 << 12) | (3 << 9) | (cp_current << 6) | (hf_div_trim << 3) | (vco_bias_trim << 0);
	dprintk( "CTRL_LO5: 0x%x", lo5);
	return dib0070_write_reg(state, 0x15, lo5);
}

#define pgm_read_word(w) (*w)
static int dib0070_reset(struct dib0070_state *state)
{
	u16 l, r, *n;

	HARD_RESET(state);


#ifndef FORCE_SBAND_TUNER
	if ((dib0070_read_reg(state, 0x22) >> 9) & 0x1)
		state->revision = (dib0070_read_reg(state, 0x1f) >> 8) & 0xff;
	else
#endif
		state->revision = DIB0070S_P1A;

	/* P1F or not */
	dprintk( "Revision: %x", state->revision);

	if (state->revision == DIB0070_P1D) {
		dprintk( "Error: this driver is not to be used meant for P1D or earlier");
		return -EINVAL;
	}

	n = (u16 *) dib0070_p1f_defaults;
	l = pgm_read_word(n++);
	while (l) {
		r = pgm_read_word(n++);
		do {
			dib0070_write_reg(state, (u8)r, pgm_read_word(n++));
			r++;
		} while (--l);
		l = pgm_read_word(n++);
	}

	if (state->cfg->force_crystal_mode != 0)
		r = state->cfg->force_crystal_mode;
	else if (state->cfg->clock_khz >= 24000)
		r = 1;
	else
		r = 2;

	r |= state->cfg->osc_buffer_state << 3;

	dib0070_write_reg(state, 0x10, r);
	dib0070_write_reg(state, 0x1f, (1 << 8) | ((state->cfg->clock_pad_drive & 0xf) << 4));

	if (state->cfg->invert_iq) {
		r = dib0070_read_reg(state, 0x02) & 0xffdf;
		dib0070_write_reg(state, 0x02, r | (1 << 5));
	}


	if (state->revision == DIB0070S_P1A)
		dib0070_set_ctrl_lo5(state->fe, 4, 7, 3, 1);
	else
		dib0070_set_ctrl_lo5(state->fe, 4, 4, 2, 0);

	dib0070_write_reg(state, 0x01, (54 << 9) | 0xc8);
	return 0;
}


static int dib0070_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
	return 0;
}

static struct dvb_tuner_ops dib0070_ops = {
	.info = {
		.name           = "DiBcom DiB0070",
		.frequency_min  =  45000000,
		.frequency_max  = 860000000,
		.frequency_step =      1000,
	},
	.release       = dib0070_release,

	.init          = dib0070_wakeup,
	.sleep         = dib0070_sleep,
	.set_params    = dib0070_tune_digital,
//	.get_frequency = dib0070_get_frequency,
//	.get_bandwidth = dib0070_get_bandwidth
};

struct dvb_frontend * dib0070_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c, struct dib0070_config *cfg)
{
	struct dib0070_state *state = kzalloc(sizeof(struct dib0070_state), GFP_KERNEL);
	if (state == NULL)
		return NULL;

	state->cfg = cfg;
	state->i2c = i2c;
	state->fe  = fe;
	fe->tuner_priv = state;

	if (dib0070_reset(state) != 0)
		goto free_mem;

	dib0070_wbd_calibration(fe);

	printk(KERN_INFO "DiB0070: successfully identified\n");
	memcpy(&fe->ops.tuner_ops, &dib0070_ops, sizeof(struct dvb_tuner_ops));

	fe->tuner_priv = state;
	return fe;

free_mem:
	kfree(state);
	fe->tuner_priv = NULL;
	return NULL;
}
EXPORT_SYMBOL(dib0070_attach);

MODULE_AUTHOR("Patrick Boettcher <pboettcher@dibcom.fr>");
MODULE_DESCRIPTION("Driver for the DiBcom 0070 base-band RF Tuner");
MODULE_LICENSE("GPL");

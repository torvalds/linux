/*
 * Linux-DVB Driver for DiBcom's DiB0070 base-band RF Tuner.
 *
 * Copyright (C) 2005-9 DiBcom (http://www.dibcom.fr/)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 * GNU General Public License for more details.
 *
 *
 * This code is more or less generated from another driver, please
 * excuse some codingstyle oddities.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>

#include <media/dvb_frontend.h>

#include "dib0070.h"
#include "dibx000_common.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "turn on debugging (default: 0)");

#define dprintk(fmt, arg...) do {					\
	if (debug)							\
		printk(KERN_DEBUG pr_fmt("%s: " fmt),			\
		       __func__, ##arg);				\
} while (0)

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

	enum frontend_tune_state tune_state;
	u32 current_rf;

	/* for the captrim binary search */
	s8 step;
	u16 adc_diff;

	s8 captrim;
	s8 fcaptrim;
	u16 lo4;

	const struct dib0070_tuning *current_tune_table_index;
	const struct dib0070_lna_match *lna_match;

	u8  wbd_gain_current;
	u16 wbd_offset_3_3[2];

	/* for the I2C transfer */
	struct i2c_msg msg[2];
	u8 i2c_write_buffer[3];
	u8 i2c_read_buffer[2];
	struct mutex i2c_buffer_lock;
};

static u16 dib0070_read_reg(struct dib0070_state *state, u8 reg)
{
	u16 ret;

	if (mutex_lock_interruptible(&state->i2c_buffer_lock) < 0) {
		dprintk("could not acquire lock\n");
		return 0;
	}

	state->i2c_write_buffer[0] = reg;

	memset(state->msg, 0, 2 * sizeof(struct i2c_msg));
	state->msg[0].addr = state->cfg->i2c_address;
	state->msg[0].flags = 0;
	state->msg[0].buf = state->i2c_write_buffer;
	state->msg[0].len = 1;
	state->msg[1].addr = state->cfg->i2c_address;
	state->msg[1].flags = I2C_M_RD;
	state->msg[1].buf = state->i2c_read_buffer;
	state->msg[1].len = 2;

	if (i2c_transfer(state->i2c, state->msg, 2) != 2) {
		pr_warn("DiB0070 I2C read failed\n");
		ret = 0;
	} else
		ret = (state->i2c_read_buffer[0] << 8)
			| state->i2c_read_buffer[1];

	mutex_unlock(&state->i2c_buffer_lock);
	return ret;
}

static int dib0070_write_reg(struct dib0070_state *state, u8 reg, u16 val)
{
	int ret;

	if (mutex_lock_interruptible(&state->i2c_buffer_lock) < 0) {
		dprintk("could not acquire lock\n");
		return -EINVAL;
	}
	state->i2c_write_buffer[0] = reg;
	state->i2c_write_buffer[1] = val >> 8;
	state->i2c_write_buffer[2] = val & 0xff;

	memset(state->msg, 0, sizeof(struct i2c_msg));
	state->msg[0].addr = state->cfg->i2c_address;
	state->msg[0].flags = 0;
	state->msg[0].buf = state->i2c_write_buffer;
	state->msg[0].len = 3;

	if (i2c_transfer(state->i2c, state->msg, 1) != 1) {
		pr_warn("DiB0070 I2C write failed\n");
		ret = -EREMOTEIO;
	} else
		ret = 0;

	mutex_unlock(&state->i2c_buffer_lock);
	return ret;
}

#define HARD_RESET(state) do { \
    state->cfg->sleep(state->fe, 0); \
    if (state->cfg->reset) { \
	state->cfg->reset(state->fe,1); msleep(10); \
	state->cfg->reset(state->fe,0); msleep(10); \
    } \
} while (0)

static int dib0070_set_bandwidth(struct dvb_frontend *fe)
	{
	struct dib0070_state *state = fe->tuner_priv;
	u16 tmp = dib0070_read_reg(state, 0x02) & 0x3fff;

	if (state->fe->dtv_property_cache.bandwidth_hz/1000 > 7000)
		tmp |= (0 << 14);
	else if (state->fe->dtv_property_cache.bandwidth_hz/1000 > 6000)
		tmp |= (1 << 14);
	else if (state->fe->dtv_property_cache.bandwidth_hz/1000 > 5000)
		tmp |= (2 << 14);
	else
		tmp |= (3 << 14);

	dib0070_write_reg(state, 0x02, tmp);

	/* sharpen the BB filter in ISDB-T to have higher immunity to adjacent channels */
	if (state->fe->dtv_property_cache.delivery_system == SYS_ISDBT) {
		u16 value = dib0070_read_reg(state, 0x17);

		dib0070_write_reg(state, 0x17, value & 0xfffc);
		tmp = dib0070_read_reg(state, 0x01) & 0x01ff;
		dib0070_write_reg(state, 0x01, tmp | (60 << 9));

		dib0070_write_reg(state, 0x17, value);
	}
	return 0;
}

static int dib0070_captrim(struct dib0070_state *state, enum frontend_tune_state *tune_state)
{
	int8_t step_sign;
	u16 adc;
	int ret = 0;

	if (*tune_state == CT_TUNER_STEP_0) {
		dib0070_write_reg(state, 0x0f, 0xed10);
		dib0070_write_reg(state, 0x17,    0x0034);

		dib0070_write_reg(state, 0x18, 0x0032);
		state->step = state->captrim = state->fcaptrim = 64;
		state->adc_diff = 3000;
		ret = 20;

		*tune_state = CT_TUNER_STEP_1;
	} else if (*tune_state == CT_TUNER_STEP_1) {
		state->step /= 2;
		dib0070_write_reg(state, 0x14, state->lo4 | state->captrim);
		ret = 15;

		*tune_state = CT_TUNER_STEP_2;
	} else if (*tune_state == CT_TUNER_STEP_2) {

		adc = dib0070_read_reg(state, 0x19);

		dprintk("CAPTRIM=%hd; ADC = %hd (ADC) & %dmV\n", state->captrim, adc, (u32) adc*(u32)1800/(u32)1024);

		if (adc >= 400) {
			adc -= 400;
			step_sign = -1;
		} else {
			adc = 400 - adc;
			step_sign = 1;
		}

		if (adc < state->adc_diff) {
			dprintk("CAPTRIM=%hd is closer to target (%hd/%hd)\n", state->captrim, adc, state->adc_diff);
			state->adc_diff = adc;
			state->fcaptrim = state->captrim;
		}
		state->captrim += (step_sign * state->step);

		if (state->step >= 1)
			*tune_state = CT_TUNER_STEP_1;
		else
			*tune_state = CT_TUNER_STEP_3;

	} else if (*tune_state == CT_TUNER_STEP_3) {
		dib0070_write_reg(state, 0x14, state->lo4 | state->fcaptrim);
		dib0070_write_reg(state, 0x18, 0x07ff);
		*tune_state = CT_TUNER_STEP_4;
	}

	return ret;
}

static int dib0070_set_ctrl_lo5(struct dvb_frontend *fe, u8 vco_bias_trim, u8 hf_div_trim, u8 cp_current, u8 third_order_filt)
{
	struct dib0070_state *state = fe->tuner_priv;
	u16 lo5 = (third_order_filt << 14) | (0 << 13) | (1 << 12) | (3 << 9) | (cp_current << 6) | (hf_div_trim << 3) | (vco_bias_trim << 0);

	dprintk("CTRL_LO5: 0x%x\n", lo5);
	return dib0070_write_reg(state, 0x15, lo5);
}

void dib0070_ctrl_agc_filter(struct dvb_frontend *fe, u8 open)
{
	struct dib0070_state *state = fe->tuner_priv;

	if (open) {
		dib0070_write_reg(state, 0x1b, 0xff00);
		dib0070_write_reg(state, 0x1a, 0x0000);
	} else {
		dib0070_write_reg(state, 0x1b, 0x4112);
		if (state->cfg->vga_filter != 0) {
			dib0070_write_reg(state, 0x1a, state->cfg->vga_filter);
			dprintk("vga filter register is set to %x\n", state->cfg->vga_filter);
		} else
			dib0070_write_reg(state, 0x1a, 0x0009);
	}
}

EXPORT_SYMBOL(dib0070_ctrl_agc_filter);
struct dib0070_tuning {
	u32 max_freq; /* for every frequency less than or equal to that field: this information is correct */
	u8 switch_trim;
	u8 vco_band;
	u8 hfdiv;
	u8 vco_multi;
	u8 presc;
	u8 wbdmux;
	u16 tuner_enable;
};

struct dib0070_lna_match {
	u32 max_freq; /* for every frequency less than or equal to that field: this information is correct */
	u8 lna_band;
};

static const struct dib0070_tuning dib0070s_tuning_table[] = {
	{     570000, 2, 1, 3, 6, 6, 2, 0x4000 | 0x0800 }, /* UHF */
	{     700000, 2, 0, 2, 4, 2, 2, 0x4000 | 0x0800 },
	{     863999, 2, 1, 2, 4, 2, 2, 0x4000 | 0x0800 },
	{    1500000, 0, 1, 1, 2, 2, 4, 0x2000 | 0x0400 }, /* LBAND */
	{    1600000, 0, 1, 1, 2, 2, 4, 0x2000 | 0x0400 },
	{    2000000, 0, 1, 1, 2, 2, 4, 0x2000 | 0x0400 },
	{ 0xffffffff, 0, 0, 8, 1, 2, 1, 0x8000 | 0x1000 }, /* SBAND */
};

static const struct dib0070_tuning dib0070_tuning_table[] = {
	{     115000, 1, 0, 7, 24, 2, 1, 0x8000 | 0x1000 }, /* FM below 92MHz cannot be tuned */
	{     179500, 1, 0, 3, 16, 2, 1, 0x8000 | 0x1000 }, /* VHF */
	{     189999, 1, 1, 3, 16, 2, 1, 0x8000 | 0x1000 },
	{     250000, 1, 0, 6, 12, 2, 1, 0x8000 | 0x1000 },
	{     569999, 2, 1, 5,  6, 2, 2, 0x4000 | 0x0800 }, /* UHF */
	{     699999, 2, 0, 1,  4, 2, 2, 0x4000 | 0x0800 },
	{     863999, 2, 1, 1,  4, 2, 2, 0x4000 | 0x0800 },
	{ 0xffffffff, 0, 1, 0,  2, 2, 4, 0x2000 | 0x0400 }, /* LBAND or everything higher than UHF */
};

static const struct dib0070_lna_match dib0070_lna_flip_chip[] = {
	{     180000, 0 }, /* VHF */
	{     188000, 1 },
	{     196400, 2 },
	{     250000, 3 },
	{     550000, 0 }, /* UHF */
	{     590000, 1 },
	{     666000, 3 },
	{     864000, 5 },
	{    1500000, 0 }, /* LBAND or everything higher than UHF */
	{    1600000, 1 },
	{    2000000, 3 },
	{ 0xffffffff, 7 },
};

static const struct dib0070_lna_match dib0070_lna[] = {
	{     180000, 0 }, /* VHF */
	{     188000, 1 },
	{     196400, 2 },
	{     250000, 3 },
	{     550000, 2 }, /* UHF */
	{     650000, 3 },
	{     750000, 5 },
	{     850000, 6 },
	{     864000, 7 },
	{    1500000, 0 }, /* LBAND or everything higher than UHF */
	{    1600000, 1 },
	{    2000000, 3 },
	{ 0xffffffff, 7 },
};

#define LPF	100
static int dib0070_tune_digital(struct dvb_frontend *fe)
{
	struct dib0070_state *state = fe->tuner_priv;

	const struct dib0070_tuning *tune;
	const struct dib0070_lna_match *lna_match;

	enum frontend_tune_state *tune_state = &state->tune_state;
	int ret = 10; /* 1ms is the default delay most of the time */

	u8  band = (u8)BAND_OF_FREQUENCY(fe->dtv_property_cache.frequency/1000);
	u32 freq = fe->dtv_property_cache.frequency/1000 + (band == BAND_VHF ? state->cfg->freq_offset_khz_vhf : state->cfg->freq_offset_khz_uhf);

#ifdef CONFIG_SYS_ISDBT
	if (state->fe->dtv_property_cache.delivery_system == SYS_ISDBT && state->fe->dtv_property_cache.isdbt_sb_mode == 1)
			if (((state->fe->dtv_property_cache.isdbt_sb_segment_count % 2)
			&& (state->fe->dtv_property_cache.isdbt_sb_segment_idx == ((state->fe->dtv_property_cache.isdbt_sb_segment_count / 2) + 1)))
			|| (((state->fe->dtv_property_cache.isdbt_sb_segment_count % 2) == 0)
				&& (state->fe->dtv_property_cache.isdbt_sb_segment_idx == (state->fe->dtv_property_cache.isdbt_sb_segment_count / 2)))
			|| (((state->fe->dtv_property_cache.isdbt_sb_segment_count % 2) == 0)
				&& (state->fe->dtv_property_cache.isdbt_sb_segment_idx == ((state->fe->dtv_property_cache.isdbt_sb_segment_count / 2) + 1))))
				freq += 850;
#endif
	if (state->current_rf != freq) {

		switch (state->revision) {
		case DIB0070S_P1A:
		tune = dib0070s_tuning_table;
		lna_match = dib0070_lna;
		break;
		default:
		tune = dib0070_tuning_table;
		if (state->cfg->flip_chip)
			lna_match = dib0070_lna_flip_chip;
		else
			lna_match = dib0070_lna;
		break;
		}
		while (freq > tune->max_freq) /* find the right one */
			tune++;
		while (freq > lna_match->max_freq) /* find the right one */
			lna_match++;

		state->current_tune_table_index = tune;
		state->lna_match = lna_match;
	}

	if (*tune_state == CT_TUNER_START) {
		dprintk("Tuning for Band: %hd (%d kHz)\n", band, freq);
		if (state->current_rf != freq) {
			u8 REFDIV;
			u32 FBDiv, Rest, FREF, VCOF_kHz;
			u8 Den;

			state->current_rf = freq;
			state->lo4 = (state->current_tune_table_index->vco_band << 11) | (state->current_tune_table_index->hfdiv << 7);


			dib0070_write_reg(state, 0x17, 0x30);


			VCOF_kHz = state->current_tune_table_index->vco_multi * freq * 2;

			switch (band) {
			case BAND_VHF:
				REFDIV = (u8) ((state->cfg->clock_khz + 9999) / 10000);
				break;
			case BAND_FM:
				REFDIV = (u8) ((state->cfg->clock_khz) / 1000);
				break;
			default:
				REFDIV = (u8) (state->cfg->clock_khz  / 10000);
				break;
			}
			FREF = state->cfg->clock_khz / REFDIV;



			switch (state->revision) {
			case DIB0070S_P1A:
				FBDiv = (VCOF_kHz / state->current_tune_table_index->presc / FREF);
				Rest  = (VCOF_kHz / state->current_tune_table_index->presc) - FBDiv * FREF;
				break;

			case DIB0070_P1G:
			case DIB0070_P1F:
			default:
				FBDiv = (freq / (FREF / 2));
				Rest  = 2 * freq - FBDiv * FREF;
				break;
			}

			if (Rest < LPF)
				Rest = 0;
			else if (Rest < 2 * LPF)
				Rest = 2 * LPF;
			else if (Rest > (FREF - LPF)) {
				Rest = 0;
				FBDiv += 1;
			} else if (Rest > (FREF - 2 * LPF))
				Rest = FREF - 2 * LPF;
			Rest = (Rest * 6528) / (FREF / 10);

			Den = 1;
			if (Rest > 0) {
				state->lo4 |= (1 << 14) | (1 << 12);
				Den = 255;
			}


			dib0070_write_reg(state, 0x11, (u16)FBDiv);
			dib0070_write_reg(state, 0x12, (Den << 8) | REFDIV);
			dib0070_write_reg(state, 0x13, (u16) Rest);

			if (state->revision == DIB0070S_P1A) {

				if (band == BAND_SBAND) {
					dib0070_set_ctrl_lo5(fe, 2, 4, 3, 0);
					dib0070_write_reg(state, 0x1d, 0xFFFF);
				} else
					dib0070_set_ctrl_lo5(fe, 5, 4, 3, 1);
			}

			dib0070_write_reg(state, 0x20,
				0x0040 | 0x0020 | 0x0010 | 0x0008 | 0x0002 | 0x0001 | state->current_tune_table_index->tuner_enable);

			dprintk("REFDIV: %hd, FREF: %d\n", REFDIV, FREF);
			dprintk("FBDIV: %d, Rest: %d\n", FBDiv, Rest);
			dprintk("Num: %hd, Den: %hd, SD: %hd\n", (u16) Rest, Den, (state->lo4 >> 12) & 0x1);
			dprintk("HFDIV code: %hd\n", state->current_tune_table_index->hfdiv);
			dprintk("VCO = %hd\n", state->current_tune_table_index->vco_band);
			dprintk("VCOF: ((%hd*%d) << 1))\n", state->current_tune_table_index->vco_multi, freq);

			*tune_state = CT_TUNER_STEP_0;
		} else { /* we are already tuned to this frequency - the configuration is correct  */
			ret = 50; /* wakeup time */
			*tune_state = CT_TUNER_STEP_5;
		}
	} else if ((*tune_state > CT_TUNER_START) && (*tune_state < CT_TUNER_STEP_4)) {

		ret = dib0070_captrim(state, tune_state);

	} else if (*tune_state == CT_TUNER_STEP_4) {
		const struct dib0070_wbd_gain_cfg *tmp = state->cfg->wbd_gain;
		if (tmp != NULL) {
			while (freq/1000 > tmp->freq) /* find the right one */
				tmp++;
			dib0070_write_reg(state, 0x0f,
				(0 << 15) | (1 << 14) | (3 << 12)
				| (tmp->wbd_gain_val << 9) | (0 << 8) | (1 << 7)
				| (state->current_tune_table_index->wbdmux << 0));
			state->wbd_gain_current = tmp->wbd_gain_val;
		} else {
			dib0070_write_reg(state, 0x0f,
					  (0 << 15) | (1 << 14) | (3 << 12)
					  | (6 << 9) | (0 << 8) | (1 << 7)
					  | (state->current_tune_table_index->wbdmux << 0));
			state->wbd_gain_current = 6;
		}

		dib0070_write_reg(state, 0x06, 0x3fff);
		dib0070_write_reg(state, 0x07,
				  (state->current_tune_table_index->switch_trim << 11) | (7 << 8) | (state->lna_match->lna_band << 3) | (3 << 0));
		dib0070_write_reg(state, 0x08, (state->lna_match->lna_band << 10) | (3 << 7) | (127));
		dib0070_write_reg(state, 0x0d, 0x0d80);


		dib0070_write_reg(state, 0x18,   0x07ff);
		dib0070_write_reg(state, 0x17, 0x0033);


		*tune_state = CT_TUNER_STEP_5;
	} else if (*tune_state == CT_TUNER_STEP_5) {
		dib0070_set_bandwidth(fe);
		*tune_state = CT_TUNER_STOP;
	} else {
		ret = FE_CALLBACK_TIME_NEVER; /* tuner finished, time to call again infinite */
	}
	return ret;
}


static int dib0070_tune(struct dvb_frontend *fe)
{
	struct dib0070_state *state = fe->tuner_priv;
	uint32_t ret;

	state->tune_state = CT_TUNER_START;

	do {
		ret = dib0070_tune_digital(fe);
		if (ret != FE_CALLBACK_TIME_NEVER)
			msleep(ret/10);
		else
		break;
	} while (state->tune_state != CT_TUNER_STOP);

	return 0;
}

static int dib0070_wakeup(struct dvb_frontend *fe)
{
	struct dib0070_state *state = fe->tuner_priv;
	if (state->cfg->sleep)
		state->cfg->sleep(fe, 0);
	return 0;
}

static int dib0070_sleep(struct dvb_frontend *fe)
{
	struct dib0070_state *state = fe->tuner_priv;
	if (state->cfg->sleep)
		state->cfg->sleep(fe, 1);
	return 0;
}

u8 dib0070_get_rf_output(struct dvb_frontend *fe)
{
	struct dib0070_state *state = fe->tuner_priv;
	return (dib0070_read_reg(state, 0x07) >> 11) & 0x3;
}
EXPORT_SYMBOL(dib0070_get_rf_output);

int dib0070_set_rf_output(struct dvb_frontend *fe, u8 no)
{
	struct dib0070_state *state = fe->tuner_priv;
	u16 rxrf2 = dib0070_read_reg(state, 0x07) & 0xfe7ff;
	if (no > 3)
		no = 3;
	if (no < 1)
		no = 1;
	return dib0070_write_reg(state, 0x07, rxrf2 | (no << 11));
}
EXPORT_SYMBOL(dib0070_set_rf_output);

static const u16 dib0070_p1f_defaults[] =

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

static u16 dib0070_read_wbd_offset(struct dib0070_state *state, u8 gain)
{
	u16 tuner_en = dib0070_read_reg(state, 0x20);
	u16 offset;

	dib0070_write_reg(state, 0x18, 0x07ff);
	dib0070_write_reg(state, 0x20, 0x0800 | 0x4000 | 0x0040 | 0x0020 | 0x0010 | 0x0008 | 0x0002 | 0x0001);
	dib0070_write_reg(state, 0x0f, (1 << 14) | (2 << 12) | (gain << 9) | (1 << 8) | (1 << 7) | (0 << 0));
	msleep(9);
	offset = dib0070_read_reg(state, 0x19);
	dib0070_write_reg(state, 0x20, tuner_en);
	return offset;
}

static void dib0070_wbd_offset_calibration(struct dib0070_state *state)
{
	u8 gain;
	for (gain = 6; gain < 8; gain++) {
		state->wbd_offset_3_3[gain - 6] = ((dib0070_read_wbd_offset(state, gain) * 8 * 18 / 33 + 1) / 2);
		dprintk("Gain: %d, WBDOffset (3.3V) = %hd\n", gain, state->wbd_offset_3_3[gain-6]);
	}
}

u16 dib0070_wbd_offset(struct dvb_frontend *fe)
{
	struct dib0070_state *state = fe->tuner_priv;
	const struct dib0070_wbd_gain_cfg *tmp = state->cfg->wbd_gain;
	u32 freq = fe->dtv_property_cache.frequency/1000;

	if (tmp != NULL) {
		while (freq/1000 > tmp->freq) /* find the right one */
			tmp++;
		state->wbd_gain_current = tmp->wbd_gain_val;
	} else
		state->wbd_gain_current = 6;

	return state->wbd_offset_3_3[state->wbd_gain_current - 6];
}
EXPORT_SYMBOL(dib0070_wbd_offset);

#define pgm_read_word(w) (*w)
static int dib0070_reset(struct dvb_frontend *fe)
{
	struct dib0070_state *state = fe->tuner_priv;
	u16 l, r, *n;

	HARD_RESET(state);


#ifndef FORCE_SBAND_TUNER
	if ((dib0070_read_reg(state, 0x22) >> 9) & 0x1)
		state->revision = (dib0070_read_reg(state, 0x1f) >> 8) & 0xff;
	else
#else
#warning forcing SBAND
#endif
	state->revision = DIB0070S_P1A;

	/* P1F or not */
	dprintk("Revision: %x\n", state->revision);

	if (state->revision == DIB0070_P1D) {
		dprintk("Error: this driver is not to be used meant for P1D or earlier\n");
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
	dib0070_write_reg(state, 0x1f, (1 << 8) | ((state->cfg->clock_pad_drive & 0xf) << 5));

	if (state->cfg->invert_iq) {
		r = dib0070_read_reg(state, 0x02) & 0xffdf;
		dib0070_write_reg(state, 0x02, r | (1 << 5));
	}

	if (state->revision == DIB0070S_P1A)
		dib0070_set_ctrl_lo5(fe, 2, 4, 3, 0);
	else
		dib0070_set_ctrl_lo5(fe, 5, 4, state->cfg->charge_pump,
				     state->cfg->enable_third_order_filter);

	dib0070_write_reg(state, 0x01, (54 << 9) | 0xc8);

	dib0070_wbd_offset_calibration(state);

	return 0;
}

static int dib0070_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct dib0070_state *state = fe->tuner_priv;

	*frequency = 1000 * state->current_rf;
	return 0;
}

static void dib0070_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
}

static const struct dvb_tuner_ops dib0070_ops = {
	.info = {
		.name           = "DiBcom DiB0070",
		.frequency_min  =  45000000,
		.frequency_max  = 860000000,
		.frequency_step =      1000,
	},
	.release       = dib0070_release,

	.init          = dib0070_wakeup,
	.sleep         = dib0070_sleep,
	.set_params    = dib0070_tune,

	.get_frequency = dib0070_get_frequency,
//      .get_bandwidth = dib0070_get_bandwidth
};

struct dvb_frontend *dib0070_attach(struct dvb_frontend *fe, struct i2c_adapter *i2c, struct dib0070_config *cfg)
{
	struct dib0070_state *state = kzalloc(sizeof(struct dib0070_state), GFP_KERNEL);
	if (state == NULL)
		return NULL;

	state->cfg = cfg;
	state->i2c = i2c;
	state->fe  = fe;
	mutex_init(&state->i2c_buffer_lock);
	fe->tuner_priv = state;

	if (dib0070_reset(fe) != 0)
		goto free_mem;

	pr_info("DiB0070: successfully identified\n");
	memcpy(&fe->ops.tuner_ops, &dib0070_ops, sizeof(struct dvb_tuner_ops));

	fe->tuner_priv = state;
	return fe;

free_mem:
	kfree(state);
	fe->tuner_priv = NULL;
	return NULL;
}
EXPORT_SYMBOL(dib0070_attach);

MODULE_AUTHOR("Patrick Boettcher <patrick.boettcher@posteo.de>");
MODULE_DESCRIPTION("Driver for the DiBcom 0070 base-band RF Tuner");
MODULE_LICENSE("GPL");

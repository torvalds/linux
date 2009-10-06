/* DVB USB compliant Linux driver for the Friio USB2.0 ISDB-T receiver.
 *
 * Copyright (C) 2009 Akihiro Tsukada <tskd2@yahoo.co.jp>
 *
 * This module is based off the the gl861 and vp702x modules.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>

#include "friio.h"

struct jdvbt90502_state {
	struct i2c_adapter *i2c;
	struct dvb_frontend frontend;
	struct jdvbt90502_config config;
};

/* NOTE: TC90502 has 16bit register-address? */
/* register 0x0100 is used for reading PLL status, so reg is u16 here */
static int jdvbt90502_reg_read(struct jdvbt90502_state *state,
			       const u16 reg, u8 *buf, const size_t count)
{
	int ret;
	u8 wbuf[3];
	struct i2c_msg msg[2];

	wbuf[0] = reg & 0xFF;
	wbuf[1] = 0;
	wbuf[2] = reg >> 8;

	msg[0].addr = state->config.demod_address;
	msg[0].flags = 0;
	msg[0].buf = wbuf;
	msg[0].len = sizeof(wbuf);

	msg[1].addr = msg[0].addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = count;

	ret = i2c_transfer(state->i2c, msg, 2);
	if (ret != 2) {
		deb_fe(" reg read failed.\n");
		return -EREMOTEIO;
	}
	return 0;
}

/* currently 16bit register-address is not used, so reg is u8 here */
static int jdvbt90502_single_reg_write(struct jdvbt90502_state *state,
				       const u8 reg, const u8 val)
{
	struct i2c_msg msg;
	u8 wbuf[2];

	wbuf[0] = reg;
	wbuf[1] = val;

	msg.addr = state->config.demod_address;
	msg.flags = 0;
	msg.buf = wbuf;
	msg.len = sizeof(wbuf);

	if (i2c_transfer(state->i2c, &msg, 1) != 1) {
		deb_fe(" reg write failed.");
		return -EREMOTEIO;
	}
	return 0;
}

static int _jdvbt90502_write(struct dvb_frontend *fe, u8 *buf, int len)
{
	struct jdvbt90502_state *state = fe->demodulator_priv;
	int err, i;
	for (i = 0; i < len - 1; i++) {
		err = jdvbt90502_single_reg_write(state,
						  buf[0] + i, buf[i + 1]);
		if (err)
			return err;
	}

	return 0;
}

/* read pll status byte via the demodulator's I2C register */
/* note: Win box reads it by 8B block at the I2C addr 0x30 from reg:0x80 */
static int jdvbt90502_pll_read(struct jdvbt90502_state *state, u8 *result)
{
	int ret;

	/* +1 for reading */
	u8 pll_addr_byte = (state->config.pll_address << 1) + 1;

	*result = 0;

	ret = jdvbt90502_single_reg_write(state, JDVBT90502_2ND_I2C_REG,
					  pll_addr_byte);
	if (ret)
		goto error;

	ret = jdvbt90502_reg_read(state, 0x0100, result, 1);
	if (ret)
		goto error;

	deb_fe("PLL read val:%02x\n", *result);
	return 0;

error:
	deb_fe("%s:ret == %d\n", __func__, ret);
	return -EREMOTEIO;
}


/* set pll frequency via the demodulator's I2C register */
static int jdvbt90502_pll_set_freq(struct jdvbt90502_state *state, u32 freq)
{
	int ret;
	int retry;
	u8 res1;
	u8 res2[9];

	u8 pll_freq_cmd[PLL_CMD_LEN];
	u8 pll_agc_cmd[PLL_CMD_LEN];
	struct i2c_msg msg[2];
	u32 f;

	deb_fe("%s: freq=%d, step=%d\n", __func__, freq,
	       state->frontend.ops.info.frequency_stepsize);
	/* freq -> oscilator frequency conversion. */
	/* freq: 473,000,000 + n*6,000,000 (no 1/7MHz shift to center freq) */
	/* add 400[1/7 MHZ] = 57.142857MHz.   57MHz for the IF,  */
	/*                                   1/7MHz for center freq shift */
	f = freq / state->frontend.ops.info.frequency_stepsize;
	f += 400;
	pll_freq_cmd[DEMOD_REDIRECT_REG] = JDVBT90502_2ND_I2C_REG; /* 0xFE */
	pll_freq_cmd[ADDRESS_BYTE] = state->config.pll_address << 1;
	pll_freq_cmd[DIVIDER_BYTE1] = (f >> 8) & 0x7F;
	pll_freq_cmd[DIVIDER_BYTE2] = f & 0xFF;
	pll_freq_cmd[CONTROL_BYTE] = 0xB2; /* ref.divider:28, 4MHz/28=1/7MHz */
	pll_freq_cmd[BANDSWITCH_BYTE] = 0x08;	/* UHF band */

	msg[0].addr = state->config.demod_address;
	msg[0].flags = 0;
	msg[0].buf = pll_freq_cmd;
	msg[0].len = sizeof(pll_freq_cmd);

	ret = i2c_transfer(state->i2c, &msg[0], 1);
	if (ret != 1)
		goto error;

	udelay(50);

	pll_agc_cmd[DEMOD_REDIRECT_REG] = pll_freq_cmd[DEMOD_REDIRECT_REG];
	pll_agc_cmd[ADDRESS_BYTE] = pll_freq_cmd[ADDRESS_BYTE];
	pll_agc_cmd[DIVIDER_BYTE1] = pll_freq_cmd[DIVIDER_BYTE1];
	pll_agc_cmd[DIVIDER_BYTE2] = pll_freq_cmd[DIVIDER_BYTE2];
	pll_agc_cmd[CONTROL_BYTE] = 0x9A; /*  AGC_CTRL instead of BANDSWITCH */
	pll_agc_cmd[AGC_CTRL_BYTE] = 0x50;
	/* AGC Time Constant 2s, AGC take-over point:103dBuV(lowest) */

	msg[1].addr = msg[0].addr;
	msg[1].flags = 0;
	msg[1].buf = pll_agc_cmd;
	msg[1].len = sizeof(pll_agc_cmd);

	ret = i2c_transfer(state->i2c, &msg[1], 1);
	if (ret != 1)
		goto error;

	/* I don't know what these cmds are for,  */
	/* but the USB log on a windows box contains them */
	ret = jdvbt90502_single_reg_write(state, 0x01, 0x40);
	ret |= jdvbt90502_single_reg_write(state, 0x01, 0x00);
	if (ret)
		goto error;
	udelay(100);

	/* wait for the demod to be ready? */
#define RETRY_COUNT 5
	for (retry = 0; retry < RETRY_COUNT; retry++) {
		ret = jdvbt90502_reg_read(state, 0x0096, &res1, 1);
		if (ret)
			goto error;
		/* if (res1 != 0x00) goto error; */
		ret = jdvbt90502_reg_read(state, 0x00B0, res2, sizeof(res2));
		if (ret)
			goto error;
		if (res2[0] >= 0xA7)
			break;
		msleep(100);
	}
	if (retry >= RETRY_COUNT) {
		deb_fe("%s: FE does not get ready after freq setting.\n",
		       __func__);
		return -EREMOTEIO;
	}

	return 0;
error:
	deb_fe("%s:ret == %d\n", __func__, ret);
	return -EREMOTEIO;
}

static int jdvbt90502_read_status(struct dvb_frontend *fe, fe_status_t *state)
{
	u8 result;
	int ret;

	*state = FE_HAS_SIGNAL;

	ret = jdvbt90502_pll_read(fe->demodulator_priv, &result);
	if (ret) {
		deb_fe("%s:ret == %d\n", __func__, ret);
		return -EREMOTEIO;
	}

	*state = FE_HAS_SIGNAL
		| FE_HAS_CARRIER
		| FE_HAS_VITERBI
		| FE_HAS_SYNC;

	if (result & PLL_STATUS_LOCKED)
		*state |= FE_HAS_LOCK;

	return 0;
}

static int jdvbt90502_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	*ber = 0;
	return 0;
}

static int jdvbt90502_read_signal_strength(struct dvb_frontend *fe,
					   u16 *strength)
{
	int ret;
	u8 rbuf[37];

	*strength = 0;

	/* status register (incl. signal strength) : 0x89  */
	/* TODO: read just the necessary registers [0x8B..0x8D]? */
	ret = jdvbt90502_reg_read(fe->demodulator_priv, 0x0089,
				  rbuf, sizeof(rbuf));

	if (ret) {
		deb_fe("%s:ret == %d\n", __func__, ret);
		return -EREMOTEIO;
	}

	/* signal_strength: rbuf[2-4] (24bit BE), use lower 16bit for now. */
	*strength = (rbuf[3] << 8) + rbuf[4];
	if (rbuf[2])
		*strength = 0xffff;

	return 0;
}

static int jdvbt90502_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	*snr = 0x0101;
	return 0;
}

static int jdvbt90502_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	*ucblocks = 0;
	return 0;
}

static int jdvbt90502_get_tune_settings(struct dvb_frontend *fe,
					struct dvb_frontend_tune_settings *fs)
{
	fs->min_delay_ms = 500;
	fs->step_size = 0;
	fs->max_drift = 0;

	return 0;
}

static int jdvbt90502_get_frontend(struct dvb_frontend *fe,
				   struct dvb_frontend_parameters *p)
{
	p->inversion = INVERSION_AUTO;
	p->u.ofdm.bandwidth = BANDWIDTH_6_MHZ;
	p->u.ofdm.code_rate_HP = FEC_AUTO;
	p->u.ofdm.code_rate_LP = FEC_AUTO;
	p->u.ofdm.constellation = QAM_64;
	p->u.ofdm.transmission_mode = TRANSMISSION_MODE_AUTO;
	p->u.ofdm.guard_interval = GUARD_INTERVAL_AUTO;
	p->u.ofdm.hierarchy_information = HIERARCHY_AUTO;
	return 0;
}

static int jdvbt90502_set_frontend(struct dvb_frontend *fe,
				   struct dvb_frontend_parameters *p)
{
	/**
	 * NOTE: ignore all the paramters except frequency.
	 *       others should be fixed to the proper value for ISDB-T,
	 *       but don't check here.
	 */

	struct jdvbt90502_state *state = fe->demodulator_priv;
	int ret;

	deb_fe("%s: Freq:%d\n", __func__, p->frequency);

	ret = jdvbt90502_pll_set_freq(state, p->frequency);
	if (ret) {
		deb_fe("%s:ret == %d\n", __func__, ret);
		return -EREMOTEIO;
	}

	return 0;
}

static int jdvbt90502_sleep(struct dvb_frontend *fe)
{
	deb_fe("%s called.\n", __func__);
	return 0;
}


/**
 * (reg, val) commad list to initialize this module.
 *  captured on a Windows box.
 */
static u8 init_code[][2] = {
	{0x01, 0x40},
	{0x04, 0x38},
	{0x05, 0x40},
	{0x07, 0x40},
	{0x0F, 0x4F},
	{0x11, 0x21},
	{0x12, 0x0B},
	{0x13, 0x2F},
	{0x14, 0x31},
	{0x16, 0x02},
	{0x21, 0xC4},
	{0x22, 0x20},
	{0x2C, 0x79},
	{0x2D, 0x34},
	{0x2F, 0x00},
	{0x30, 0x28},
	{0x31, 0x31},
	{0x32, 0xDF},
	{0x38, 0x01},
	{0x39, 0x78},
	{0x3B, 0x33},
	{0x3C, 0x33},
	{0x48, 0x90},
	{0x51, 0x68},
	{0x5E, 0x38},
	{0x71, 0x00},
	{0x72, 0x08},
	{0x77, 0x00},
	{0xC0, 0x21},
	{0xC1, 0x10},
	{0xE4, 0x1A},
	{0xEA, 0x1F},
	{0x77, 0x00},
	{0x71, 0x00},
	{0x71, 0x00},
	{0x76, 0x0C},
};

const static int init_code_len = sizeof(init_code) / sizeof(u8[2]);

static int jdvbt90502_init(struct dvb_frontend *fe)
{
	int i = -1;
	int ret;
	struct i2c_msg msg;

	struct jdvbt90502_state *state = fe->demodulator_priv;

	deb_fe("%s called.\n", __func__);

	msg.addr = state->config.demod_address;
	msg.flags = 0;
	msg.len = 2;
	for (i = 0; i < init_code_len; i++) {
		msg.buf = init_code[i];
		ret = i2c_transfer(state->i2c, &msg, 1);
		if (ret != 1)
			goto error;
	}
	msleep(100);

	return 0;

error:
	deb_fe("%s: init_code[%d] failed. ret==%d\n", __func__, i, ret);
	return -EREMOTEIO;
}


static void jdvbt90502_release(struct dvb_frontend *fe)
{
	struct jdvbt90502_state *state = fe->demodulator_priv;
	kfree(state);
}


static struct dvb_frontend_ops jdvbt90502_ops;

struct dvb_frontend *jdvbt90502_attach(struct dvb_usb_device *d)
{
	struct jdvbt90502_state *state = NULL;

	deb_info("%s called.\n", __func__);

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct jdvbt90502_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* setup the state */
	state->i2c = &d->i2c_adap;
	memcpy(&state->config, &friio_fe_config, sizeof(friio_fe_config));

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &jdvbt90502_ops,
	       sizeof(jdvbt90502_ops));
	state->frontend.demodulator_priv = state;

	if (jdvbt90502_init(&state->frontend) < 0)
		goto error;

	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops jdvbt90502_ops = {

	.info = {
		.name			= "Comtech JDVBT90502 ISDB-T",
		.type			= FE_OFDM,
		.frequency_min		= 473000000, /* UHF 13ch, center */
		.frequency_max		= 767142857, /* UHF 62ch, center */
		.frequency_stepsize	= JDVBT90502_PLL_CLK /
							JDVBT90502_PLL_DIVIDER,
		.frequency_tolerance	= 0,

		/* NOTE: this driver ignores all parameters but frequency. */
		.caps = FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_4_5 | FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 |
			FE_CAN_FEC_7_8 | FE_CAN_FEC_8_9 | FE_CAN_FEC_AUTO |
			FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO,
	},

	.release = jdvbt90502_release,

	.init = jdvbt90502_init,
	.sleep = jdvbt90502_sleep,
	.write = _jdvbt90502_write,

	.set_frontend = jdvbt90502_set_frontend,
	.get_frontend = jdvbt90502_get_frontend,
	.get_tune_settings = jdvbt90502_get_tune_settings,

	.read_status = jdvbt90502_read_status,
	.read_ber = jdvbt90502_read_ber,
	.read_signal_strength = jdvbt90502_read_signal_strength,
	.read_snr = jdvbt90502_read_snr,
	.read_ucblocks = jdvbt90502_read_ucblocks,
};

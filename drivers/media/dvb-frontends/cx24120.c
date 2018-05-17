/*
    Conexant cx24120/cx24118 - DVBS/S2 Satellite demod/tuner driver

    Copyright (C) 2008 Patrick Boettcher <pb@linuxtv.org>
    Copyright (C) 2009 Sergey Tyurin <forum.free-x.de>
    Updated 2012 by Jannis Achstetter <jannis_achstetter@web.de>
    Copyright (C) 2015 Jemma Denson <jdenson@gmail.com>
	April 2015
	    Refactored & simplified driver
	    Updated to work with delivery system supplied by DVBv5
	    Add frequency, fec & pilot to get_frontend

	Cards supported: Technisat Skystar S2

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*/

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/firmware.h>
#include <media/dvb_frontend.h>
#include "cx24120.h"

#define CX24120_SEARCH_RANGE_KHZ 5000
#define CX24120_FIRMWARE "dvb-fe-cx24120-1.20.58.2.fw"

/* cx24120 i2c registers  */
#define CX24120_REG_CMD_START	0x00		/* write cmd_id */
#define CX24120_REG_CMD_ARGS	0x01		/* write command arguments */
#define CX24120_REG_CMD_END	0x1f		/* write 0x01 for end */

#define CX24120_REG_MAILBOX	0x33
#define CX24120_REG_FREQ3	0x34		/* frequency */
#define CX24120_REG_FREQ2	0x35
#define CX24120_REG_FREQ1	0x36

#define CX24120_REG_FECMODE	0x39		/* FEC status */
#define CX24120_REG_STATUS	0x3a		/* Tuner status */
#define CX24120_REG_SIGSTR_H	0x3a		/* Signal strength high */
#define CX24120_REG_SIGSTR_L	0x3b		/* Signal strength low byte */
#define CX24120_REG_QUALITY_H	0x40		/* SNR high byte */
#define CX24120_REG_QUALITY_L	0x41		/* SNR low byte */

#define CX24120_REG_BER_HH	0x47		/* BER high byte of high word */
#define CX24120_REG_BER_HL	0x48		/* BER low byte of high word */
#define CX24120_REG_BER_LH	0x49		/* BER high byte of low word */
#define CX24120_REG_BER_LL	0x4a		/* BER low byte of low word */

#define CX24120_REG_UCB_H	0x50		/* UCB high byte */
#define CX24120_REG_UCB_L	0x51		/* UCB low byte  */

#define CX24120_REG_CLKDIV	0xe6
#define CX24120_REG_RATEDIV	0xf0

#define CX24120_REG_REVISION	0xff		/* Chip revision (ro) */

/* Command messages */
enum command_message_id {
	CMD_VCO_SET		= 0x10,		/* cmd.len = 12; */
	CMD_TUNEREQUEST		= 0x11,		/* cmd.len = 15; */

	CMD_MPEG_ONOFF		= 0x13,		/* cmd.len = 4; */
	CMD_MPEG_INIT		= 0x14,		/* cmd.len = 7; */
	CMD_BANDWIDTH		= 0x15,		/* cmd.len = 12; */
	CMD_CLOCK_READ		= 0x16,		/* read clock */
	CMD_CLOCK_SET		= 0x17,		/* cmd.len = 10; */

	CMD_DISEQC_MSG1		= 0x20,		/* cmd.len = 11; */
	CMD_DISEQC_MSG2		= 0x21,		/* cmd.len = d->msg_len + 6; */
	CMD_SETVOLTAGE		= 0x22,		/* cmd.len = 2; */
	CMD_SETTONE		= 0x23,		/* cmd.len = 4; */
	CMD_DISEQC_BURST	= 0x24,		/* cmd.len not used !!! */

	CMD_READ_SNR		= 0x1a,		/* Read signal strength */
	CMD_START_TUNER		= 0x1b,		/* ??? */

	CMD_FWVERSION		= 0x35,

	CMD_BER_CTRL		= 0x3c,		/* cmd.len = 0x03; */
};

#define CX24120_MAX_CMD_LEN	30

/* pilot mask */
#define CX24120_PILOT_OFF	0x00
#define CX24120_PILOT_ON	0x40
#define CX24120_PILOT_AUTO	0x80

/* signal status */
#define CX24120_HAS_SIGNAL	0x01
#define CX24120_HAS_CARRIER	0x02
#define CX24120_HAS_VITERBI	0x04
#define CX24120_HAS_LOCK	0x08
#define CX24120_HAS_UNK1	0x10
#define CX24120_HAS_UNK2	0x20
#define CX24120_STATUS_MASK	0x0f
#define CX24120_SIGNAL_MASK	0xc0

/* ber window */
#define CX24120_BER_WINDOW	16
#define CX24120_BER_WSIZE	((1 << CX24120_BER_WINDOW) * 208 * 8)

#define info(args...) pr_info("cx24120: " args)
#define err(args...)  pr_err("cx24120: ### ERROR: " args)

/* The Demod/Tuner can't easily provide these, we cache them */
struct cx24120_tuning {
	u32 frequency;
	u32 symbol_rate;
	enum fe_spectral_inversion inversion;
	enum fe_code_rate fec;

	enum fe_delivery_system delsys;
	enum fe_modulation modulation;
	enum fe_pilot pilot;

	/* Demod values */
	u8 fec_val;
	u8 fec_mask;
	u8 clkdiv;
	u8 ratediv;
	u8 inversion_val;
	u8 pilot_val;
};

/* Private state */
struct cx24120_state {
	struct i2c_adapter *i2c;
	const struct cx24120_config *config;
	struct dvb_frontend frontend;

	u8 cold_init;
	u8 mpeg_enabled;
	u8 need_clock_set;

	/* current and next tuning parameters */
	struct cx24120_tuning dcur;
	struct cx24120_tuning dnxt;

	enum fe_status fe_status;

	/* dvbv5 stats calculations */
	u32 bitrate;
	u32 berw_usecs;
	u32 ber_prev;
	u32 ucb_offset;
	unsigned long ber_jiffies_stats;
	unsigned long per_jiffies_stats;
};

/* Command message to firmware */
struct cx24120_cmd {
	u8 id;
	u8 len;
	u8 arg[CX24120_MAX_CMD_LEN];
};

/* Read single register */
static int cx24120_readreg(struct cx24120_state *state, u8 reg)
{
	int ret;
	u8 buf = 0;
	struct i2c_msg msg[] = {
		{
			.addr = state->config->i2c_addr,
			.flags = 0,
			.len = 1,
			.buf = &reg
		}, {
			.addr = state->config->i2c_addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = &buf
		}
	};

	ret = i2c_transfer(state->i2c, msg, 2);
	if (ret != 2) {
		err("Read error: reg=0x%02x, ret=%i)\n", reg, ret);
		return ret;
	}

	dev_dbg(&state->i2c->dev, "reg=0x%02x; data=0x%02x\n", reg, buf);

	return buf;
}

/* Write single register */
static int cx24120_writereg(struct cx24120_state *state, u8 reg, u8 data)
{
	u8 buf[] = { reg, data };
	struct i2c_msg msg = {
		.addr = state->config->i2c_addr,
		.flags = 0,
		.buf = buf,
		.len = 2
	};
	int ret;

	ret = i2c_transfer(state->i2c, &msg, 1);
	if (ret != 1) {
		err("Write error: i2c_write error(err == %i, 0x%02x: 0x%02x)\n",
		    ret, reg, data);
		return ret;
	}

	dev_dbg(&state->i2c->dev, "reg=0x%02x; data=0x%02x\n", reg, data);

	return 0;
}

/* Write multiple registers in chunks of i2c_wr_max-sized buffers */
static int cx24120_writeregs(struct cx24120_state *state,
			     u8 reg, const u8 *values, u16 len, u8 incr)
{
	int ret;
	u16 max = state->config->i2c_wr_max > 0 ?
				state->config->i2c_wr_max :
				len;

	struct i2c_msg msg = {
		.addr = state->config->i2c_addr,
		.flags = 0,
	};

	msg.buf = kmalloc(max + 1, GFP_KERNEL);
	if (!msg.buf)
		return -ENOMEM;

	while (len) {
		msg.buf[0] = reg;
		msg.len = len > max ? max : len;
		memcpy(&msg.buf[1], values, msg.len);

		len    -= msg.len;      /* data length revers counter */
		values += msg.len;      /* incr data pointer */

		if (incr)
			reg += msg.len;
		msg.len++;              /* don't forget the addr byte */

		ret = i2c_transfer(state->i2c, &msg, 1);
		if (ret != 1) {
			err("i2c_write error(err == %i, 0x%02x)\n", ret, reg);
			goto out;
		}

		dev_dbg(&state->i2c->dev, "reg=0x%02x; data=%*ph\n",
			reg, msg.len - 1, msg.buf + 1);
	}

	ret = 0;

out:
	kfree(msg.buf);
	return ret;
}

static const struct dvb_frontend_ops cx24120_ops;

struct dvb_frontend *cx24120_attach(const struct cx24120_config *config,
				    struct i2c_adapter *i2c)
{
	struct cx24120_state *state;
	int demod_rev;

	info("Conexant cx24120/cx24118 - DVBS/S2 Satellite demod/tuner\n");
	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state) {
		err("Unable to allocate memory for cx24120_state\n");
		goto error;
	}

	/* setup the state */
	state->config = config;
	state->i2c = i2c;

	/* check if the demod is present and has proper type */
	demod_rev = cx24120_readreg(state, CX24120_REG_REVISION);
	switch (demod_rev) {
	case 0x07:
		info("Demod cx24120 rev. 0x07 detected.\n");
		break;
	case 0x05:
		info("Demod cx24120 rev. 0x05 detected.\n");
		break;
	default:
		err("Unsupported demod revision: 0x%x detected.\n", demod_rev);
		goto error;
	}

	/* create dvb_frontend */
	state->cold_init = 0;
	memcpy(&state->frontend.ops, &cx24120_ops,
	       sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;

	info("Conexant cx24120/cx24118 attached.\n");
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(cx24120_attach);

static int cx24120_test_rom(struct cx24120_state *state)
{
	int err, ret;

	err = cx24120_readreg(state, 0xfd);
	if (err & 4) {
		ret = cx24120_readreg(state, 0xdf) & 0xfe;
		err = cx24120_writereg(state, 0xdf, ret);
	}
	return err;
}

static int cx24120_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	if (c->cnr.stat[0].scale != FE_SCALE_DECIBEL)
		*snr = 0;
	else
		*snr = div_s64(c->cnr.stat[0].svalue, 100);

	return 0;
}

static int cx24120_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct cx24120_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	if (c->post_bit_error.stat[0].scale != FE_SCALE_COUNTER) {
		*ber = 0;
		return 0;
	}

	*ber = c->post_bit_error.stat[0].uvalue - state->ber_prev;
	state->ber_prev = c->post_bit_error.stat[0].uvalue;

	return 0;
}

static int cx24120_msg_mpeg_output_global_config(struct cx24120_state *state,
						 u8 flag);

/* Check if we're running a command that needs to disable mpeg out */
static void cx24120_check_cmd(struct cx24120_state *state, u8 id)
{
	switch (id) {
	case CMD_TUNEREQUEST:
	case CMD_CLOCK_READ:
	case CMD_DISEQC_MSG1:
	case CMD_DISEQC_MSG2:
	case CMD_SETVOLTAGE:
	case CMD_SETTONE:
	case CMD_DISEQC_BURST:
		cx24120_msg_mpeg_output_global_config(state, 0);
		/* Old driver would do a msleep(100) here */
	default:
		return;
	}
}

/* Send a message to the firmware */
static int cx24120_message_send(struct cx24120_state *state,
				struct cx24120_cmd *cmd)
{
	int ficus;

	if (state->mpeg_enabled) {
		/* Disable mpeg out on certain commands */
		cx24120_check_cmd(state, cmd->id);
	}

	cx24120_writereg(state, CX24120_REG_CMD_START, cmd->id);
	cx24120_writeregs(state, CX24120_REG_CMD_ARGS, &cmd->arg[0],
			  cmd->len, 1);
	cx24120_writereg(state, CX24120_REG_CMD_END, 0x01);

	ficus = 1000;
	while (cx24120_readreg(state, CX24120_REG_CMD_END)) {
		msleep(20);
		ficus -= 20;
		if (ficus == 0) {
			err("Error sending message to firmware\n");
			return -EREMOTEIO;
		}
	}
	dev_dbg(&state->i2c->dev, "sent message 0x%02x\n", cmd->id);

	return 0;
}

/* Send a message and fill arg[] with the results */
static int cx24120_message_sendrcv(struct cx24120_state *state,
				   struct cx24120_cmd *cmd, u8 numreg)
{
	int ret, i;

	if (numreg > CX24120_MAX_CMD_LEN) {
		err("Too many registers to read. cmd->reg = %d", numreg);
		return -EREMOTEIO;
	}

	ret = cx24120_message_send(state, cmd);
	if (ret != 0)
		return ret;

	if (!numreg)
		return 0;

	/* Read numreg registers starting from register cmd->len */
	for (i = 0; i < numreg; i++)
		cmd->arg[i] = cx24120_readreg(state, (cmd->len + i + 1));

	return 0;
}

static int cx24120_read_signal_strength(struct dvb_frontend *fe,
					u16 *signal_strength)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	if (c->strength.stat[0].scale != FE_SCALE_RELATIVE)
		*signal_strength = 0;
	else
		*signal_strength = c->strength.stat[0].uvalue;

	return 0;
}

static int cx24120_msg_mpeg_output_global_config(struct cx24120_state *state,
						 u8 enable)
{
	struct cx24120_cmd cmd;
	int ret;

	cmd.id = CMD_MPEG_ONOFF;
	cmd.len = 4;
	cmd.arg[0] = 0x01;
	cmd.arg[1] = 0x00;
	cmd.arg[2] = enable ? 0 : (u8)(-1);
	cmd.arg[3] = 0x01;

	ret = cx24120_message_send(state, &cmd);
	if (ret != 0) {
		dev_dbg(&state->i2c->dev, "failed to %s MPEG output\n",
			enable ? "enable" : "disable");
		return ret;
	}

	state->mpeg_enabled = enable;
	dev_dbg(&state->i2c->dev, "MPEG output %s\n",
		enable ? "enabled" : "disabled");

	return 0;
}

static int cx24120_msg_mpeg_output_config(struct cx24120_state *state, u8 seq)
{
	struct cx24120_cmd cmd;
	struct cx24120_initial_mpeg_config i =
			state->config->initial_mpeg_config;

	cmd.id = CMD_MPEG_INIT;
	cmd.len = 7;
	cmd.arg[0] = seq; /* sequental number - can be 0,1,2 */
	cmd.arg[1] = ((i.x1 & 0x01) << 1) | ((i.x1 >> 1) & 0x01);
	cmd.arg[2] = 0x05;
	cmd.arg[3] = 0x02;
	cmd.arg[4] = ((i.x2 >> 1) & 0x01);
	cmd.arg[5] = (i.x2 & 0xf0) | (i.x3 & 0x0f);
	cmd.arg[6] = 0x10;

	return cx24120_message_send(state, &cmd);
}

static int cx24120_diseqc_send_burst(struct dvb_frontend *fe,
				     enum fe_sec_mini_cmd burst)
{
	struct cx24120_state *state = fe->demodulator_priv;
	struct cx24120_cmd cmd;

	dev_dbg(&state->i2c->dev, "\n");

	/*
	 * Yes, cmd.len is set to zero. The old driver
	 * didn't specify any len, but also had a
	 * memset 0 before every use of the cmd struct
	 * which would have set it to zero.
	 * This quite probably needs looking into.
	 */
	cmd.id = CMD_DISEQC_BURST;
	cmd.len = 0;
	cmd.arg[0] = 0x00;
	cmd.arg[1] = (burst == SEC_MINI_B) ? 0x01 : 0x00;

	return cx24120_message_send(state, &cmd);
}

static int cx24120_set_tone(struct dvb_frontend *fe, enum fe_sec_tone_mode tone)
{
	struct cx24120_state *state = fe->demodulator_priv;
	struct cx24120_cmd cmd;

	dev_dbg(&state->i2c->dev, "(%d)\n", tone);

	if ((tone != SEC_TONE_ON) && (tone != SEC_TONE_OFF)) {
		err("Invalid tone=%d\n", tone);
		return -EINVAL;
	}

	cmd.id = CMD_SETTONE;
	cmd.len = 4;
	cmd.arg[0] = 0x00;
	cmd.arg[1] = 0x00;
	cmd.arg[2] = 0x00;
	cmd.arg[3] = (tone == SEC_TONE_ON) ? 0x01 : 0x00;

	return cx24120_message_send(state, &cmd);
}

static int cx24120_set_voltage(struct dvb_frontend *fe,
			       enum fe_sec_voltage voltage)
{
	struct cx24120_state *state = fe->demodulator_priv;
	struct cx24120_cmd cmd;

	dev_dbg(&state->i2c->dev, "(%d)\n", voltage);

	cmd.id = CMD_SETVOLTAGE;
	cmd.len = 2;
	cmd.arg[0] = 0x00;
	cmd.arg[1] = (voltage == SEC_VOLTAGE_18) ? 0x01 : 0x00;

	return cx24120_message_send(state, &cmd);
}

static int cx24120_send_diseqc_msg(struct dvb_frontend *fe,
				   struct dvb_diseqc_master_cmd *d)
{
	struct cx24120_state *state = fe->demodulator_priv;
	struct cx24120_cmd cmd;
	int back_count;

	dev_dbg(&state->i2c->dev, "\n");

	cmd.id = CMD_DISEQC_MSG1;
	cmd.len = 11;
	cmd.arg[0] = 0x00;
	cmd.arg[1] = 0x00;
	cmd.arg[2] = 0x03;
	cmd.arg[3] = 0x16;
	cmd.arg[4] = 0x28;
	cmd.arg[5] = 0x01;
	cmd.arg[6] = 0x01;
	cmd.arg[7] = 0x14;
	cmd.arg[8] = 0x19;
	cmd.arg[9] = 0x14;
	cmd.arg[10] = 0x1e;

	if (cx24120_message_send(state, &cmd)) {
		err("send 1st message(0x%x) failed\n", cmd.id);
		return -EREMOTEIO;
	}

	cmd.id = CMD_DISEQC_MSG2;
	cmd.len = d->msg_len + 6;
	cmd.arg[0] = 0x00;
	cmd.arg[1] = 0x01;
	cmd.arg[2] = 0x02;
	cmd.arg[3] = 0x00;
	cmd.arg[4] = 0x00;
	cmd.arg[5] = d->msg_len;

	memcpy(&cmd.arg[6], &d->msg, d->msg_len);

	if (cx24120_message_send(state, &cmd)) {
		err("send 2nd message(0x%x) failed\n", cmd.id);
		return -EREMOTEIO;
	}

	back_count = 500;
	do {
		if (!(cx24120_readreg(state, 0x93) & 0x01)) {
			dev_dbg(&state->i2c->dev, "diseqc sequence sent\n");
			return 0;
		}
		msleep(20);
		back_count -= 20;
	} while (back_count);

	err("Too long waiting for diseqc.\n");
	return -ETIMEDOUT;
}

static void cx24120_get_stats(struct cx24120_state *state)
{
	struct dvb_frontend *fe = &state->frontend;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct cx24120_cmd cmd;
	int ret, cnr, msecs;
	u16 sig, ucb;
	u32 ber;

	dev_dbg(&state->i2c->dev, "\n");

	/* signal strength */
	if (state->fe_status & FE_HAS_SIGNAL) {
		cmd.id = CMD_READ_SNR;
		cmd.len = 1;
		cmd.arg[0] = 0x00;

		ret = cx24120_message_send(state, &cmd);
		if (ret != 0) {
			err("error reading signal strength\n");
			return;
		}

		/* raw */
		sig = cx24120_readreg(state, CX24120_REG_SIGSTR_H) >> 6;
		sig = sig << 8;
		sig |= cx24120_readreg(state, CX24120_REG_SIGSTR_L);
		dev_dbg(&state->i2c->dev,
			"signal strength from firmware = 0x%x\n", sig);

		/* cooked */
		sig = -100 * sig + 94324;

		c->strength.stat[0].scale = FE_SCALE_RELATIVE;
		c->strength.stat[0].uvalue = sig;
	} else {
		c->strength.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}

	/* CNR */
	if (state->fe_status & FE_HAS_VITERBI) {
		cnr = cx24120_readreg(state, CX24120_REG_QUALITY_H) << 8;
		cnr |= cx24120_readreg(state, CX24120_REG_QUALITY_L);
		dev_dbg(&state->i2c->dev, "read SNR index = %d\n", cnr);

		/* guessed - seems about right */
		cnr = cnr * 100;

		c->cnr.stat[0].scale = FE_SCALE_DECIBEL;
		c->cnr.stat[0].svalue = cnr;
	} else {
		c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}

	/* BER & UCB require lock */
	if (!(state->fe_status & FE_HAS_LOCK)) {
		c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->block_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		return;
	}

	/* BER */
	if (time_after(jiffies, state->ber_jiffies_stats)) {
		msecs = (state->berw_usecs + 500) / 1000;
		state->ber_jiffies_stats = jiffies + msecs_to_jiffies(msecs);

		ber = cx24120_readreg(state, CX24120_REG_BER_HH) << 24;
		ber |= cx24120_readreg(state, CX24120_REG_BER_HL) << 16;
		ber |= cx24120_readreg(state, CX24120_REG_BER_LH) << 8;
		ber |= cx24120_readreg(state, CX24120_REG_BER_LL);
		dev_dbg(&state->i2c->dev, "read BER index = %d\n", ber);

		c->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
		c->post_bit_error.stat[0].uvalue += ber;

		c->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
		c->post_bit_count.stat[0].uvalue += CX24120_BER_WSIZE;
	}

	/* UCB */
	if (time_after(jiffies, state->per_jiffies_stats)) {
		state->per_jiffies_stats = jiffies + msecs_to_jiffies(1000);

		ucb = cx24120_readreg(state, CX24120_REG_UCB_H) << 8;
		ucb |= cx24120_readreg(state, CX24120_REG_UCB_L);
		dev_dbg(&state->i2c->dev, "ucblocks = %d\n", ucb);

		/* handle reset */
		if (ucb < state->ucb_offset)
			state->ucb_offset = c->block_error.stat[0].uvalue;

		c->block_error.stat[0].scale = FE_SCALE_COUNTER;
		c->block_error.stat[0].uvalue = ucb + state->ucb_offset;

		c->block_count.stat[0].scale = FE_SCALE_COUNTER;
		c->block_count.stat[0].uvalue += state->bitrate / 8 / 208;
	}
}

static void cx24120_set_clock_ratios(struct dvb_frontend *fe);

/* Read current tuning status */
static int cx24120_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct cx24120_state *state = fe->demodulator_priv;
	int lock;

	lock = cx24120_readreg(state, CX24120_REG_STATUS);

	dev_dbg(&state->i2c->dev, "status = 0x%02x\n", lock);

	*status = 0;

	if (lock & CX24120_HAS_SIGNAL)
		*status = FE_HAS_SIGNAL;
	if (lock & CX24120_HAS_CARRIER)
		*status |= FE_HAS_CARRIER;
	if (lock & CX24120_HAS_VITERBI)
		*status |= FE_HAS_VITERBI | FE_HAS_SYNC;
	if (lock & CX24120_HAS_LOCK)
		*status |= FE_HAS_LOCK;

	/*
	 * TODO: is FE_HAS_SYNC in the right place?
	 * Other cx241xx drivers have this slightly
	 * different
	 */

	state->fe_status = *status;
	cx24120_get_stats(state);

	/* Set the clock once tuned in */
	if (state->need_clock_set && *status & FE_HAS_LOCK) {
		/* Set clock ratios */
		cx24120_set_clock_ratios(fe);

		/* Old driver would do a msleep(200) here */

		/* Renable mpeg output */
		if (!state->mpeg_enabled)
			cx24120_msg_mpeg_output_global_config(state, 1);

		state->need_clock_set = 0;
	}

	return 0;
}

/*
 * FEC & modulation lookup table
 * Used for decoding the REG_FECMODE register
 * once tuned in.
 */
struct cx24120_modfec {
	enum fe_delivery_system delsys;
	enum fe_modulation mod;
	enum fe_code_rate fec;
	u8 val;
};

static const struct cx24120_modfec modfec_lookup_table[] = {
	/*delsys     mod    fec       val */
	{ SYS_DVBS,  QPSK,  FEC_1_2,  0x01 },
	{ SYS_DVBS,  QPSK,  FEC_2_3,  0x02 },
	{ SYS_DVBS,  QPSK,  FEC_3_4,  0x03 },
	{ SYS_DVBS,  QPSK,  FEC_4_5,  0x04 },
	{ SYS_DVBS,  QPSK,  FEC_5_6,  0x05 },
	{ SYS_DVBS,  QPSK,  FEC_6_7,  0x06 },
	{ SYS_DVBS,  QPSK,  FEC_7_8,  0x07 },

	{ SYS_DVBS2, QPSK,  FEC_1_2,  0x04 },
	{ SYS_DVBS2, QPSK,  FEC_3_5,  0x05 },
	{ SYS_DVBS2, QPSK,  FEC_2_3,  0x06 },
	{ SYS_DVBS2, QPSK,  FEC_3_4,  0x07 },
	{ SYS_DVBS2, QPSK,  FEC_4_5,  0x08 },
	{ SYS_DVBS2, QPSK,  FEC_5_6,  0x09 },
	{ SYS_DVBS2, QPSK,  FEC_8_9,  0x0a },
	{ SYS_DVBS2, QPSK,  FEC_9_10, 0x0b },

	{ SYS_DVBS2, PSK_8, FEC_3_5,  0x0c },
	{ SYS_DVBS2, PSK_8, FEC_2_3,  0x0d },
	{ SYS_DVBS2, PSK_8, FEC_3_4,  0x0e },
	{ SYS_DVBS2, PSK_8, FEC_5_6,  0x0f },
	{ SYS_DVBS2, PSK_8, FEC_8_9,  0x10 },
	{ SYS_DVBS2, PSK_8, FEC_9_10, 0x11 },
};

/* Retrieve current fec, modulation & pilot values */
static int cx24120_get_fec(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct cx24120_state *state = fe->demodulator_priv;
	int idx;
	int ret;
	int fec;

	ret = cx24120_readreg(state, CX24120_REG_FECMODE);
	fec = ret & 0x3f; /* Lower 6 bits */

	dev_dbg(&state->i2c->dev, "raw fec = %d\n", fec);

	for (idx = 0; idx < ARRAY_SIZE(modfec_lookup_table); idx++) {
		if (modfec_lookup_table[idx].delsys != state->dcur.delsys)
			continue;
		if (modfec_lookup_table[idx].val != fec)
			continue;

		break; /* found */
	}

	if (idx >= ARRAY_SIZE(modfec_lookup_table)) {
		dev_dbg(&state->i2c->dev, "couldn't find fec!\n");
		return -EINVAL;
	}

	/* save values back to cache */
	c->modulation = modfec_lookup_table[idx].mod;
	c->fec_inner = modfec_lookup_table[idx].fec;
	c->pilot = (ret & 0x80) ? PILOT_ON : PILOT_OFF;

	dev_dbg(&state->i2c->dev, "mod(%d), fec(%d), pilot(%d)\n",
		c->modulation, c->fec_inner, c->pilot);

	return 0;
}

/* Calculate ber window time */
static void cx24120_calculate_ber_window(struct cx24120_state *state, u32 rate)
{
	struct dvb_frontend *fe = &state->frontend;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u64 tmp;

	/*
	 * Calculate bitrate from rate in the clock ratios table.
	 * This isn't *exactly* right but close enough.
	 */
	tmp = (u64)c->symbol_rate * rate;
	do_div(tmp, 256);
	state->bitrate = tmp;

	/* usecs per ber window */
	tmp = 1000000ULL * CX24120_BER_WSIZE;
	do_div(tmp, state->bitrate);
	state->berw_usecs = tmp;

	dev_dbg(&state->i2c->dev, "bitrate: %u, berw_usecs: %u\n",
		state->bitrate, state->berw_usecs);
}

/*
 * Clock ratios lookup table
 *
 * Values obtained from much larger table in old driver
 * which had numerous entries which would never match.
 *
 * There's probably some way of calculating these but I
 * can't determine the pattern
 */
struct cx24120_clock_ratios_table {
	enum fe_delivery_system delsys;
	enum fe_pilot pilot;
	enum fe_modulation mod;
	enum fe_code_rate fec;
	u32 m_rat;
	u32 n_rat;
	u32 rate;
};

static const struct cx24120_clock_ratios_table clock_ratios_table[] = {
	/*delsys     pilot      mod    fec       m_rat    n_rat   rate */
	{ SYS_DVBS2, PILOT_OFF, QPSK,  FEC_1_2,  273088,  254505, 274 },
	{ SYS_DVBS2, PILOT_OFF, QPSK,  FEC_3_5,  17272,   13395,  330 },
	{ SYS_DVBS2, PILOT_OFF, QPSK,  FEC_2_3,  24344,   16967,  367 },
	{ SYS_DVBS2, PILOT_OFF, QPSK,  FEC_3_4,  410788,  254505, 413 },
	{ SYS_DVBS2, PILOT_OFF, QPSK,  FEC_4_5,  438328,  254505, 440 },
	{ SYS_DVBS2, PILOT_OFF, QPSK,  FEC_5_6,  30464,   16967,  459 },
	{ SYS_DVBS2, PILOT_OFF, QPSK,  FEC_8_9,  487832,  254505, 490 },
	{ SYS_DVBS2, PILOT_OFF, QPSK,  FEC_9_10, 493952,  254505, 496 },
	{ SYS_DVBS2, PILOT_OFF, PSK_8, FEC_3_5,  328168,  169905, 494 },
	{ SYS_DVBS2, PILOT_OFF, PSK_8, FEC_2_3,  24344,   11327,  550 },
	{ SYS_DVBS2, PILOT_OFF, PSK_8, FEC_3_4,  410788,  169905, 618 },
	{ SYS_DVBS2, PILOT_OFF, PSK_8, FEC_5_6,  30464,   11327,  688 },
	{ SYS_DVBS2, PILOT_OFF, PSK_8, FEC_8_9,  487832,  169905, 735 },
	{ SYS_DVBS2, PILOT_OFF, PSK_8, FEC_9_10, 493952,  169905, 744 },
	{ SYS_DVBS2, PILOT_ON,  QPSK,  FEC_1_2,  273088,  260709, 268 },
	{ SYS_DVBS2, PILOT_ON,  QPSK,  FEC_3_5,  328168,  260709, 322 },
	{ SYS_DVBS2, PILOT_ON,  QPSK,  FEC_2_3,  121720,  86903,  358 },
	{ SYS_DVBS2, PILOT_ON,  QPSK,  FEC_3_4,  410788,  260709, 403 },
	{ SYS_DVBS2, PILOT_ON,  QPSK,  FEC_4_5,  438328,  260709, 430 },
	{ SYS_DVBS2, PILOT_ON,  QPSK,  FEC_5_6,  152320,  86903,  448 },
	{ SYS_DVBS2, PILOT_ON,  QPSK,  FEC_8_9,  487832,  260709, 479 },
	{ SYS_DVBS2, PILOT_ON,  QPSK,  FEC_9_10, 493952,  260709, 485 },
	{ SYS_DVBS2, PILOT_ON,  PSK_8, FEC_3_5,  328168,  173853, 483 },
	{ SYS_DVBS2, PILOT_ON,  PSK_8, FEC_2_3,  121720,  57951,  537 },
	{ SYS_DVBS2, PILOT_ON,  PSK_8, FEC_3_4,  410788,  173853, 604 },
	{ SYS_DVBS2, PILOT_ON,  PSK_8, FEC_5_6,  152320,  57951,  672 },
	{ SYS_DVBS2, PILOT_ON,  PSK_8, FEC_8_9,  487832,  173853, 718 },
	{ SYS_DVBS2, PILOT_ON,  PSK_8, FEC_9_10, 493952,  173853, 727 },
	{ SYS_DVBS,  PILOT_OFF, QPSK,  FEC_1_2,  152592,  152592, 256 },
	{ SYS_DVBS,  PILOT_OFF, QPSK,  FEC_2_3,  305184,  228888, 341 },
	{ SYS_DVBS,  PILOT_OFF, QPSK,  FEC_3_4,  457776,  305184, 384 },
	{ SYS_DVBS,  PILOT_OFF, QPSK,  FEC_5_6,  762960,  457776, 427 },
	{ SYS_DVBS,  PILOT_OFF, QPSK,  FEC_7_8,  1068144, 610368, 448 },
};

/* Set clock ratio from lookup table */
static void cx24120_set_clock_ratios(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct cx24120_state *state = fe->demodulator_priv;
	struct cx24120_cmd cmd;
	int ret, idx;

	/* Find fec, modulation, pilot */
	ret = cx24120_get_fec(fe);
	if (ret != 0)
		return;

	/* Find the clock ratios in the lookup table */
	for (idx = 0; idx < ARRAY_SIZE(clock_ratios_table); idx++) {
		if (clock_ratios_table[idx].delsys != state->dcur.delsys)
			continue;
		if (clock_ratios_table[idx].mod != c->modulation)
			continue;
		if (clock_ratios_table[idx].fec != c->fec_inner)
			continue;
		if (clock_ratios_table[idx].pilot != c->pilot)
			continue;

		break;		/* found */
	}

	if (idx >= ARRAY_SIZE(clock_ratios_table)) {
		info("Clock ratio not found - data reception in danger\n");
		return;
	}

	/* Read current values? */
	cmd.id = CMD_CLOCK_READ;
	cmd.len = 1;
	cmd.arg[0] = 0x00;
	ret = cx24120_message_sendrcv(state, &cmd, 6);
	if (ret != 0)
		return;
	/* in cmd[0]-[5] - result */

	dev_dbg(&state->i2c->dev, "m=%d, n=%d; idx: %d m=%d, n=%d, rate=%d\n",
		cmd.arg[2] | (cmd.arg[1] << 8) | (cmd.arg[0] << 16),
		cmd.arg[5] | (cmd.arg[4] << 8) | (cmd.arg[3] << 16),
		idx,
		clock_ratios_table[idx].m_rat,
		clock_ratios_table[idx].n_rat,
		clock_ratios_table[idx].rate);

	/* Set the clock */
	cmd.id = CMD_CLOCK_SET;
	cmd.len = 10;
	cmd.arg[0] = 0;
	cmd.arg[1] = 0x10;
	cmd.arg[2] = (clock_ratios_table[idx].m_rat >> 16) & 0xff;
	cmd.arg[3] = (clock_ratios_table[idx].m_rat >>  8) & 0xff;
	cmd.arg[4] = (clock_ratios_table[idx].m_rat >>  0) & 0xff;
	cmd.arg[5] = (clock_ratios_table[idx].n_rat >> 16) & 0xff;
	cmd.arg[6] = (clock_ratios_table[idx].n_rat >>  8) & 0xff;
	cmd.arg[7] = (clock_ratios_table[idx].n_rat >>  0) & 0xff;
	cmd.arg[8] = (clock_ratios_table[idx].rate >> 8) & 0xff;
	cmd.arg[9] = (clock_ratios_table[idx].rate >> 0) & 0xff;

	cx24120_message_send(state, &cmd);

	/* Calculate ber window rates for stat work */
	cx24120_calculate_ber_window(state, clock_ratios_table[idx].rate);
}

/* Set inversion value */
static int cx24120_set_inversion(struct cx24120_state *state,
				 enum fe_spectral_inversion inversion)
{
	dev_dbg(&state->i2c->dev, "(%d)\n", inversion);

	switch (inversion) {
	case INVERSION_OFF:
		state->dnxt.inversion_val = 0x00;
		break;
	case INVERSION_ON:
		state->dnxt.inversion_val = 0x04;
		break;
	case INVERSION_AUTO:
		state->dnxt.inversion_val = 0x0c;
		break;
	default:
		return -EINVAL;
	}

	state->dnxt.inversion = inversion;

	return 0;
}

/* FEC lookup table for tuning */
struct cx24120_modfec_table {
	enum fe_delivery_system delsys;
	enum fe_modulation mod;
	enum fe_code_rate fec;
	u8 val;
};

static const struct cx24120_modfec_table modfec_table[] = {
	/*delsys     mod    fec       val */
	{ SYS_DVBS,  QPSK,  FEC_1_2,  0x2e },
	{ SYS_DVBS,  QPSK,  FEC_2_3,  0x2f },
	{ SYS_DVBS,  QPSK,  FEC_3_4,  0x30 },
	{ SYS_DVBS,  QPSK,  FEC_5_6,  0x31 },
	{ SYS_DVBS,  QPSK,  FEC_6_7,  0x32 },
	{ SYS_DVBS,  QPSK,  FEC_7_8,  0x33 },

	{ SYS_DVBS2, QPSK,  FEC_1_2,  0x04 },
	{ SYS_DVBS2, QPSK,  FEC_3_5,  0x05 },
	{ SYS_DVBS2, QPSK,  FEC_2_3,  0x06 },
	{ SYS_DVBS2, QPSK,  FEC_3_4,  0x07 },
	{ SYS_DVBS2, QPSK,  FEC_4_5,  0x08 },
	{ SYS_DVBS2, QPSK,  FEC_5_6,  0x09 },
	{ SYS_DVBS2, QPSK,  FEC_8_9,  0x0a },
	{ SYS_DVBS2, QPSK,  FEC_9_10, 0x0b },

	{ SYS_DVBS2, PSK_8, FEC_3_5,  0x0c },
	{ SYS_DVBS2, PSK_8, FEC_2_3,  0x0d },
	{ SYS_DVBS2, PSK_8, FEC_3_4,  0x0e },
	{ SYS_DVBS2, PSK_8, FEC_5_6,  0x0f },
	{ SYS_DVBS2, PSK_8, FEC_8_9,  0x10 },
	{ SYS_DVBS2, PSK_8, FEC_9_10, 0x11 },
};

/* Set fec_val & fec_mask values from delsys, modulation & fec */
static int cx24120_set_fec(struct cx24120_state *state, enum fe_modulation mod,
			   enum fe_code_rate fec)
{
	int idx;

	dev_dbg(&state->i2c->dev, "(0x%02x,0x%02x)\n", mod, fec);

	state->dnxt.fec = fec;

	/* Lookup fec_val from modfec table */
	for (idx = 0; idx < ARRAY_SIZE(modfec_table); idx++) {
		if (modfec_table[idx].delsys != state->dnxt.delsys)
			continue;
		if (modfec_table[idx].mod != mod)
			continue;
		if (modfec_table[idx].fec != fec)
			continue;

		/* found */
		state->dnxt.fec_mask = 0x00;
		state->dnxt.fec_val = modfec_table[idx].val;
		return 0;
	}

	if (state->dnxt.delsys == SYS_DVBS2) {
		/* DVBS2 auto is 0x00/0x00 */
		state->dnxt.fec_mask = 0x00;
		state->dnxt.fec_val  = 0x00;
	} else {
		/* Set DVB-S to auto */
		state->dnxt.fec_val  = 0x2e;
		state->dnxt.fec_mask = 0xac;
	}

	return 0;
}

/* Set pilot */
static int cx24120_set_pilot(struct cx24120_state *state, enum fe_pilot pilot)
{
	dev_dbg(&state->i2c->dev, "(%d)\n", pilot);

	/* Pilot only valid in DVBS2 */
	if (state->dnxt.delsys != SYS_DVBS2) {
		state->dnxt.pilot_val = CX24120_PILOT_OFF;
		return 0;
	}

	switch (pilot) {
	case PILOT_OFF:
		state->dnxt.pilot_val = CX24120_PILOT_OFF;
		break;
	case PILOT_ON:
		state->dnxt.pilot_val = CX24120_PILOT_ON;
		break;
	case PILOT_AUTO:
	default:
		state->dnxt.pilot_val = CX24120_PILOT_AUTO;
	}

	return 0;
}

/* Set symbol rate */
static int cx24120_set_symbolrate(struct cx24120_state *state, u32 rate)
{
	dev_dbg(&state->i2c->dev, "(%d)\n", rate);

	state->dnxt.symbol_rate = rate;

	/* Check symbol rate */
	if (rate  > 31000000) {
		state->dnxt.clkdiv  = (-(rate < 31000001) & 3) + 2;
		state->dnxt.ratediv = (-(rate < 31000001) & 6) + 4;
	} else {
		state->dnxt.clkdiv  = 3;
		state->dnxt.ratediv = 6;
	}

	return 0;
}

/* Overwrite the current tuning params, we are about to tune */
static void cx24120_clone_params(struct dvb_frontend *fe)
{
	struct cx24120_state *state = fe->demodulator_priv;

	state->dcur = state->dnxt;
}

static int cx24120_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct cx24120_state *state = fe->demodulator_priv;
	struct cx24120_cmd cmd;
	int ret;

	switch (c->delivery_system) {
	case SYS_DVBS2:
		dev_dbg(&state->i2c->dev, "DVB-S2\n");
		break;
	case SYS_DVBS:
		dev_dbg(&state->i2c->dev, "DVB-S\n");
		break;
	default:
		dev_dbg(&state->i2c->dev,
			"delivery system(%d) not supported\n",
			c->delivery_system);
		return -EINVAL;
	}

	state->dnxt.delsys = c->delivery_system;
	state->dnxt.modulation = c->modulation;
	state->dnxt.frequency = c->frequency;
	state->dnxt.pilot = c->pilot;

	ret = cx24120_set_inversion(state, c->inversion);
	if (ret !=  0)
		return ret;

	ret = cx24120_set_fec(state, c->modulation, c->fec_inner);
	if (ret !=  0)
		return ret;

	ret = cx24120_set_pilot(state, c->pilot);
	if (ret != 0)
		return ret;

	ret = cx24120_set_symbolrate(state, c->symbol_rate);
	if (ret !=  0)
		return ret;

	/* discard the 'current' tuning parameters and prepare to tune */
	cx24120_clone_params(fe);

	dev_dbg(&state->i2c->dev,
		"delsys      = %d\n", state->dcur.delsys);
	dev_dbg(&state->i2c->dev,
		"modulation  = %d\n", state->dcur.modulation);
	dev_dbg(&state->i2c->dev,
		"frequency   = %d\n", state->dcur.frequency);
	dev_dbg(&state->i2c->dev,
		"pilot       = %d (val = 0x%02x)\n",
		state->dcur.pilot, state->dcur.pilot_val);
	dev_dbg(&state->i2c->dev,
		"symbol_rate = %d (clkdiv/ratediv = 0x%02x/0x%02x)\n",
		 state->dcur.symbol_rate,
		 state->dcur.clkdiv, state->dcur.ratediv);
	dev_dbg(&state->i2c->dev,
		"FEC         = %d (mask/val = 0x%02x/0x%02x)\n",
		state->dcur.fec, state->dcur.fec_mask, state->dcur.fec_val);
	dev_dbg(&state->i2c->dev,
		"Inversion   = %d (val = 0x%02x)\n",
		state->dcur.inversion, state->dcur.inversion_val);

	/* Flag that clock needs to be set after tune */
	state->need_clock_set = 1;

	/* Tune in */
	cmd.id = CMD_TUNEREQUEST;
	cmd.len = 15;
	cmd.arg[0] = 0;
	cmd.arg[1]  = (state->dcur.frequency & 0xff0000) >> 16;
	cmd.arg[2]  = (state->dcur.frequency & 0x00ff00) >> 8;
	cmd.arg[3]  = (state->dcur.frequency & 0x0000ff);
	cmd.arg[4]  = ((state->dcur.symbol_rate / 1000) & 0xff00) >> 8;
	cmd.arg[5]  = ((state->dcur.symbol_rate / 1000) & 0x00ff);
	cmd.arg[6]  = state->dcur.inversion;
	cmd.arg[7]  = state->dcur.fec_val | state->dcur.pilot_val;
	cmd.arg[8]  = CX24120_SEARCH_RANGE_KHZ >> 8;
	cmd.arg[9]  = CX24120_SEARCH_RANGE_KHZ & 0xff;
	cmd.arg[10] = 0;		/* maybe rolloff? */
	cmd.arg[11] = state->dcur.fec_mask;
	cmd.arg[12] = state->dcur.ratediv;
	cmd.arg[13] = state->dcur.clkdiv;
	cmd.arg[14] = 0;

	/* Send tune command */
	ret = cx24120_message_send(state, &cmd);
	if (ret != 0)
		return ret;

	/* Write symbol rate values */
	ret = cx24120_writereg(state, CX24120_REG_CLKDIV, state->dcur.clkdiv);
	ret = cx24120_readreg(state, CX24120_REG_RATEDIV);
	ret &= 0xfffffff0;
	ret |= state->dcur.ratediv;
	ret = cx24120_writereg(state, CX24120_REG_RATEDIV, ret);

	return 0;
}

/* Set vco from config */
static int cx24120_set_vco(struct cx24120_state *state)
{
	struct cx24120_cmd cmd;
	u32 nxtal_khz, vco;
	u64 inv_vco;
	u32 xtal_khz = state->config->xtal_khz;

	nxtal_khz = xtal_khz * 4;
	vco = nxtal_khz * 10;
	inv_vco = DIV_ROUND_CLOSEST_ULL(0x400000000ULL, vco);

	dev_dbg(&state->i2c->dev, "xtal=%d, vco=%d, inv_vco=%lld\n",
		xtal_khz, vco, inv_vco);

	cmd.id = CMD_VCO_SET;
	cmd.len = 12;
	cmd.arg[0] = (vco >> 16) & 0xff;
	cmd.arg[1] = (vco >> 8) & 0xff;
	cmd.arg[2] = vco & 0xff;
	cmd.arg[3] = (inv_vco >> 8) & 0xff;
	cmd.arg[4] = (inv_vco) & 0xff;
	cmd.arg[5] = 0x03;
	cmd.arg[6] = (nxtal_khz >> 8) & 0xff;
	cmd.arg[7] = nxtal_khz & 0xff;
	cmd.arg[8] = 0x06;
	cmd.arg[9] = 0x03;
	cmd.arg[10] = (xtal_khz >> 16) & 0xff;
	cmd.arg[11] = xtal_khz & 0xff;

	return cx24120_message_send(state, &cmd);
}

static int cx24120_init(struct dvb_frontend *fe)
{
	const struct firmware *fw;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct cx24120_state *state = fe->demodulator_priv;
	struct cx24120_cmd cmd;
	u8 reg;
	int ret, i;
	unsigned char vers[4];

	if (state->cold_init)
		return 0;

	/* ???? */
	cx24120_writereg(state, 0xea, 0x00);
	cx24120_test_rom(state);
	reg = cx24120_readreg(state, 0xfb) & 0xfe;
	cx24120_writereg(state, 0xfb, reg);
	reg = cx24120_readreg(state, 0xfc) & 0xfe;
	cx24120_writereg(state, 0xfc, reg);
	cx24120_writereg(state, 0xc3, 0x04);
	cx24120_writereg(state, 0xc4, 0x04);
	cx24120_writereg(state, 0xce, 0x00);
	cx24120_writereg(state, 0xcf, 0x00);
	reg = cx24120_readreg(state, 0xea) & 0xfe;
	cx24120_writereg(state, 0xea, reg);
	cx24120_writereg(state, 0xeb, 0x0c);
	cx24120_writereg(state, 0xec, 0x06);
	cx24120_writereg(state, 0xed, 0x05);
	cx24120_writereg(state, 0xee, 0x03);
	cx24120_writereg(state, 0xef, 0x05);
	cx24120_writereg(state, 0xf3, 0x03);
	cx24120_writereg(state, 0xf4, 0x44);

	for (i = 0; i < 3; i++) {
		cx24120_writereg(state, 0xf0 + i, 0x04);
		cx24120_writereg(state, 0xe6 + i, 0x02);
	}

	cx24120_writereg(state, 0xea, (reg | 0x01));
	for (i = 0; i < 6; i += 2) {
		cx24120_writereg(state, 0xc5 + i, 0x00);
		cx24120_writereg(state, 0xc6 + i, 0x00);
	}

	cx24120_writereg(state, 0xe4, 0x03);
	cx24120_writereg(state, 0xeb, 0x0a);

	dev_dbg(&state->i2c->dev, "requesting firmware (%s) to download...\n",
		CX24120_FIRMWARE);

	ret = state->config->request_firmware(fe, &fw, CX24120_FIRMWARE);
	if (ret) {
		err("Could not load firmware (%s): %d\n", CX24120_FIRMWARE,
		    ret);
		return ret;
	}

	dev_dbg(&state->i2c->dev,
		"Firmware found, size %d bytes (%02x %02x .. %02x %02x)\n",
		(int)fw->size,			/* firmware_size in bytes */
		fw->data[0],			/* fw 1st byte */
		fw->data[1],			/* fw 2d byte */
		fw->data[fw->size - 2],		/* fw before last byte */
		fw->data[fw->size - 1]);	/* fw last byte */

	cx24120_test_rom(state);
	reg = cx24120_readreg(state, 0xfb) & 0xfe;
	cx24120_writereg(state, 0xfb, reg);
	cx24120_writereg(state, 0xe0, 0x76);
	cx24120_writereg(state, 0xf7, 0x81);
	cx24120_writereg(state, 0xf8, 0x00);
	cx24120_writereg(state, 0xf9, 0x00);
	cx24120_writeregs(state, 0xfa, fw->data, (fw->size - 1), 0x00);
	cx24120_writereg(state, 0xf7, 0xc0);
	cx24120_writereg(state, 0xe0, 0x00);
	reg = (fw->size - 2) & 0x00ff;
	cx24120_writereg(state, 0xf8, reg);
	reg = ((fw->size - 2) >> 8) & 0x00ff;
	cx24120_writereg(state, 0xf9, reg);
	cx24120_writereg(state, 0xf7, 0x00);
	cx24120_writereg(state, 0xdc, 0x00);
	cx24120_writereg(state, 0xdc, 0x07);
	msleep(500);

	/* Check final byte matches final byte of firmware */
	reg = cx24120_readreg(state, 0xe1);
	if (reg == fw->data[fw->size - 1]) {
		dev_dbg(&state->i2c->dev, "Firmware uploaded successfully\n");
		ret = 0;
	} else {
		err("Firmware upload failed. Last byte returned=0x%x\n", ret);
		ret = -EREMOTEIO;
	}
	cx24120_writereg(state, 0xdc, 0x00);
	release_firmware(fw);
	if (ret != 0)
		return ret;

	/* Start tuner */
	cmd.id = CMD_START_TUNER;
	cmd.len = 3;
	cmd.arg[0] = 0x00;
	cmd.arg[1] = 0x00;
	cmd.arg[2] = 0x00;

	if (cx24120_message_send(state, &cmd) != 0) {
		err("Error tuner start! :(\n");
		return -EREMOTEIO;
	}

	/* Set VCO */
	ret = cx24120_set_vco(state);
	if (ret != 0) {
		err("Error set VCO! :(\n");
		return ret;
	}

	/* set bandwidth */
	cmd.id = CMD_BANDWIDTH;
	cmd.len = 12;
	cmd.arg[0] = 0x00;
	cmd.arg[1] = 0x00;
	cmd.arg[2] = 0x00;
	cmd.arg[3] = 0x00;
	cmd.arg[4] = 0x05;
	cmd.arg[5] = 0x02;
	cmd.arg[6] = 0x02;
	cmd.arg[7] = 0x00;
	cmd.arg[8] = 0x05;
	cmd.arg[9] = 0x02;
	cmd.arg[10] = 0x02;
	cmd.arg[11] = 0x00;

	if (cx24120_message_send(state, &cmd)) {
		err("Error set bandwidth!\n");
		return -EREMOTEIO;
	}

	reg = cx24120_readreg(state, 0xba);
	if (reg > 3) {
		dev_dbg(&state->i2c->dev, "Reset-readreg 0xba: %x\n", ret);
		err("Error initialising tuner!\n");
		return -EREMOTEIO;
	}

	dev_dbg(&state->i2c->dev, "Tuner initialised correctly.\n");

	/* Initialise mpeg outputs */
	cx24120_writereg(state, 0xeb, 0x0a);
	if (cx24120_msg_mpeg_output_global_config(state, 0) ||
	    cx24120_msg_mpeg_output_config(state, 0) ||
	    cx24120_msg_mpeg_output_config(state, 1) ||
	    cx24120_msg_mpeg_output_config(state, 2)) {
		err("Error initialising mpeg output. :(\n");
		return -EREMOTEIO;
	}

	/* Set size of BER window */
	cmd.id = CMD_BER_CTRL;
	cmd.len = 3;
	cmd.arg[0] = 0x00;
	cmd.arg[1] = CX24120_BER_WINDOW;
	cmd.arg[2] = CX24120_BER_WINDOW;
	if (cx24120_message_send(state, &cmd)) {
		err("Error setting ber window\n");
		return -EREMOTEIO;
	}

	/* Firmware CMD 35: Get firmware version */
	cmd.id = CMD_FWVERSION;
	cmd.len = 1;
	for (i = 0; i < 4; i++) {
		cmd.arg[0] = i;
		ret = cx24120_message_send(state, &cmd);
		if (ret != 0)
			return ret;
		vers[i] = cx24120_readreg(state, CX24120_REG_MAILBOX);
	}
	info("FW version %i.%i.%i.%i\n", vers[0], vers[1], vers[2], vers[3]);

	/* init stats here in order signal app which stats are supported */
	c->strength.len = 1;
	c->strength.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->cnr.len = 1;
	c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->post_bit_error.len = 1;
	c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->post_bit_count.len = 1;
	c->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->block_error.len = 1;
	c->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->block_count.len = 1;
	c->block_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	state->cold_init = 1;

	return 0;
}

static int cx24120_tune(struct dvb_frontend *fe, bool re_tune,
			unsigned int mode_flags, unsigned int *delay,
			enum fe_status *status)
{
	struct cx24120_state *state = fe->demodulator_priv;
	int ret;

	dev_dbg(&state->i2c->dev, "(%d)\n", re_tune);

	/* TODO: Do we need to set delay? */

	if (re_tune) {
		ret = cx24120_set_frontend(fe);
		if (ret)
			return ret;
	}

	return cx24120_read_status(fe, status);
}

static int cx24120_get_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_HW;
}

static int cx24120_sleep(struct dvb_frontend *fe)
{
	return 0;
}

static int cx24120_get_frontend(struct dvb_frontend *fe,
				struct dtv_frontend_properties *c)
{
	struct cx24120_state *state = fe->demodulator_priv;
	u8 freq1, freq2, freq3;
	int status;

	dev_dbg(&state->i2c->dev, "\n");

	/* don't return empty data if we're not tuned in */
	status = cx24120_readreg(state, CX24120_REG_STATUS);
	if (!(status & CX24120_HAS_LOCK))
		return 0;

	/* Get frequency */
	freq1 = cx24120_readreg(state, CX24120_REG_FREQ1);
	freq2 = cx24120_readreg(state, CX24120_REG_FREQ2);
	freq3 = cx24120_readreg(state, CX24120_REG_FREQ3);
	c->frequency = (freq3 << 16) | (freq2 << 8) | freq1;
	dev_dbg(&state->i2c->dev, "frequency = %d\n", c->frequency);

	/* Get modulation, fec, pilot */
	cx24120_get_fec(fe);

	return 0;
}

static void cx24120_release(struct dvb_frontend *fe)
{
	struct cx24120_state *state = fe->demodulator_priv;

	dev_dbg(&state->i2c->dev, "Clear state structure\n");
	kfree(state);
}

static int cx24120_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct cx24120_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	if (c->block_error.stat[0].scale != FE_SCALE_COUNTER) {
		*ucblocks = 0;
		return 0;
	}

	*ucblocks = c->block_error.stat[0].uvalue - state->ucb_offset;

	return 0;
}

static const struct dvb_frontend_ops cx24120_ops = {
	.delsys = { SYS_DVBS, SYS_DVBS2 },
	.info = {
		.name = "Conexant CX24120/CX24118",
		.frequency_min = 950000,
		.frequency_max = 2150000,
		.frequency_stepsize = 1011, /* kHz for QPSK frontends */
		.frequency_tolerance = 5000,
		.symbol_rate_min = 1000000,
		.symbol_rate_max = 45000000,
		.caps =	FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_4_5 | FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 |
			FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_2G_MODULATION |
			FE_CAN_QPSK | FE_CAN_RECOVER
	},
	.release =			cx24120_release,

	.init =				cx24120_init,
	.sleep =			cx24120_sleep,

	.tune =				cx24120_tune,
	.get_frontend_algo =		cx24120_get_algo,
	.set_frontend =			cx24120_set_frontend,

	.get_frontend =			cx24120_get_frontend,
	.read_status =			cx24120_read_status,
	.read_ber =			cx24120_read_ber,
	.read_signal_strength =		cx24120_read_signal_strength,
	.read_snr =			cx24120_read_snr,
	.read_ucblocks =		cx24120_read_ucblocks,

	.diseqc_send_master_cmd =	cx24120_send_diseqc_msg,

	.diseqc_send_burst =		cx24120_diseqc_send_burst,
	.set_tone =			cx24120_set_tone,
	.set_voltage =			cx24120_set_voltage,
};

MODULE_DESCRIPTION("DVB Frontend module for Conexant CX24120/CX24118 hardware");
MODULE_AUTHOR("Jemma Denson");
MODULE_LICENSE("GPL");

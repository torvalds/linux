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
#include "dvb_frontend.h"
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

	CMD_TUNER_INIT		= 0x3c,		/* cmd.len = 0x03; */
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

#define info(args...) pr_info("cx24120: " args)
#define err(args...)  pr_err("cx24120: ### ERROR: " args)

/* The Demod/Tuner can't easily provide these, we cache them */
struct cx24120_tuning {
	u32 frequency;
	u32 symbol_rate;
	fe_spectral_inversion_t inversion;
	fe_code_rate_t fec;

	fe_delivery_system_t delsys;
	fe_modulation_t modulation;
	fe_pilot_t pilot;

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

	/* current and next tuning parameters */
	struct cx24120_tuning dcur;
	struct cx24120_tuning dnxt;
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
		{	.addr = state->config->i2c_addr,
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

	dev_dbg(&state->i2c->dev, "%s: reg=0x%02x; data=0x%02x\n",
		__func__, reg, buf);

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

	dev_dbg(&state->i2c->dev, "%s: reg=0x%02x; data=0x%02x\n",
		__func__, reg, data);

	return 0;
}


/* Write multiple registers in chunks of i2c_wr_max-sized buffers */
static int cx24120_writeregN(struct cx24120_state *state,
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
	if (msg.buf == NULL)
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

		dev_dbg(&state->i2c->dev,
			"%s: reg=0x%02x; data=%*ph\n",
			__func__, reg, msg.len, msg.buf+1);
	}

	ret = 0;

out:
	kfree(msg.buf);
	return ret;
}


static struct dvb_frontend_ops cx24120_ops;

struct dvb_frontend *cx24120_attach(const struct cx24120_config *config,
			struct i2c_adapter *i2c)
{
	struct cx24120_state *state = NULL;
	int demod_rev;

	info("Conexant cx24120/cx24118 - DVBS/S2 Satellite demod/tuner\n");
	state = kzalloc(sizeof(struct cx24120_state), GFP_KERNEL);
	if (state == NULL) {
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
		err("Unsupported demod revision: 0x%x detected.\n",
			demod_rev);
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
	struct cx24120_state *state = fe->demodulator_priv;

	*snr =  (cx24120_readreg(state, CX24120_REG_QUALITY_H)<<8) |
		(cx24120_readreg(state, CX24120_REG_QUALITY_L));
	dev_dbg(&state->i2c->dev, "%s: read SNR index = %d\n",
			__func__, *snr);

	return 0;
}


static int cx24120_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct cx24120_state *state = fe->demodulator_priv;

	*ber =  (cx24120_readreg(state, CX24120_REG_BER_HH) << 24)	|
		(cx24120_readreg(state, CX24120_REG_BER_HL) << 16)	|
		(cx24120_readreg(state, CX24120_REG_BER_LH)  << 8)	|
		 cx24120_readreg(state, CX24120_REG_BER_LL);
	dev_dbg(&state->i2c->dev, "%s: read BER index = %d\n",
			__func__, *ber);

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
	int ret, ficus;

	if (state->mpeg_enabled) {
		/* Disable mpeg out on certain commands */
		cx24120_check_cmd(state, cmd->id);
	}

	ret = cx24120_writereg(state, CX24120_REG_CMD_START, cmd->id);
	ret = cx24120_writeregN(state, CX24120_REG_CMD_ARGS, &cmd->arg[0],
				cmd->len, 1);
	ret = cx24120_writereg(state, CX24120_REG_CMD_END, 0x01);

	ficus = 1000;
	while (cx24120_readreg(state, CX24120_REG_CMD_END)) {
		msleep(20);
		ficus -= 20;
		if (ficus == 0) {
			err("Error sending message to firmware\n");
			return -EREMOTEIO;
		}
	}
	dev_dbg(&state->i2c->dev, "%s: Successfully send message 0x%02x\n",
		__func__, cmd->id);

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
		cmd->arg[i] = cx24120_readreg(state, (cmd->len+i+1));

	return 0;
}



static int cx24120_read_signal_strength(struct dvb_frontend *fe,
			u16 *signal_strength)
{
	struct cx24120_state *state = fe->demodulator_priv;
	struct cx24120_cmd cmd;
	int ret, sigstr_h, sigstr_l;

	cmd.id = CMD_READ_SNR;
	cmd.len = 1;
	cmd.arg[0] = 0x00;

	ret = cx24120_message_send(state, &cmd);
	if (ret != 0) {
		err("error reading signal strength\n");
		return -EREMOTEIO;
	}

	/* raw */
	sigstr_h = (cx24120_readreg(state, CX24120_REG_SIGSTR_H) >> 6) << 8;
	sigstr_l = cx24120_readreg(state, CX24120_REG_SIGSTR_L);
	dev_dbg(&state->i2c->dev, "%s: Signal strength from firmware= 0x%x\n",
			__func__, (sigstr_h | sigstr_l));

	/* cooked */
	*signal_strength = ((sigstr_h | sigstr_l)  << 5) & 0x0000ffff;
	dev_dbg(&state->i2c->dev, "%s: Signal strength= 0x%x\n",
			__func__, *signal_strength);

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
		dev_dbg(&state->i2c->dev,
			"%s: Failed to set MPEG output to %s\n",
			__func__,
			(enable)?"enabled":"disabled");
		return ret;
	}

	state->mpeg_enabled = enable;
	dev_dbg(&state->i2c->dev, "%s: MPEG output %s\n",
		__func__,
		(enable)?"enabled":"disabled");

	return 0;
}


static int cx24120_msg_mpeg_output_config(struct cx24120_state *state, u8 seq)
{
	struct cx24120_cmd cmd;
	struct cx24120_initial_mpeg_config i =
			state->config->initial_mpeg_config;

	cmd.id = CMD_MPEG_INIT;
	cmd.len = 7;
	cmd.arg[0] = seq;		/* sequental number - can be 0,1,2 */
	cmd.arg[1] = ((i.x1 & 0x01) << 1) | ((i.x1 >> 1) & 0x01);
	cmd.arg[2] = 0x05;
	cmd.arg[3] = 0x02;
	cmd.arg[4] = ((i.x2 >> 1) & 0x01);
	cmd.arg[5] = (i.x2 & 0xf0) | (i.x3 & 0x0f);
	cmd.arg[6] = 0x10;

	return cx24120_message_send(state, &cmd);
}


static int cx24120_diseqc_send_burst(struct dvb_frontend *fe,
			fe_sec_mini_cmd_t burst)
{
	struct cx24120_state *state = fe->demodulator_priv;
	struct cx24120_cmd cmd;

	/* Yes, cmd.len is set to zero. The old driver
	 * didn't specify any len, but also had a
	 * memset 0 before every use of the cmd struct
	 * which would have set it to zero.
	 * This quite probably needs looking into.
	 */
	cmd.id = CMD_DISEQC_BURST;
	cmd.len = 0;
	cmd.arg[0] = 0x00;
	if (burst)
		cmd.arg[1] = 0x01;
	dev_dbg(&state->i2c->dev, "%s: burst sent.\n", __func__);

	return cx24120_message_send(state, &cmd);
}


static int cx24120_set_tone(struct dvb_frontend *fe, fe_sec_tone_mode_t tone)
{
	struct cx24120_state *state = fe->demodulator_priv;
	struct cx24120_cmd cmd;

	dev_dbg(&state->i2c->dev, "%s(%d)\n",
			__func__, tone);

	if ((tone != SEC_TONE_ON) && (tone != SEC_TONE_OFF)) {
		err("Invalid tone=%d\n", tone);
		return -EINVAL;
	}

	cmd.id = CMD_SETTONE;
	cmd.len = 4;
	cmd.arg[0] = 0x00;
	cmd.arg[1] = 0x00;
	cmd.arg[2] = 0x00;
	cmd.arg[3] = (tone == SEC_TONE_ON)?0x01:0x00;

	return cx24120_message_send(state, &cmd);
}


static int cx24120_set_voltage(struct dvb_frontend *fe,
			fe_sec_voltage_t voltage)
{
	struct cx24120_state *state = fe->demodulator_priv;
	struct cx24120_cmd cmd;

	dev_dbg(&state->i2c->dev, "%s(%d)\n",
			__func__, voltage);

	cmd.id = CMD_SETVOLTAGE;
	cmd.len = 2;
	cmd.arg[0] = 0x00;
	cmd.arg[1] = (voltage == SEC_VOLTAGE_18)?0x01:0x00;

	return cx24120_message_send(state, &cmd);
}


static int cx24120_send_diseqc_msg(struct dvb_frontend *fe,
			struct dvb_diseqc_master_cmd *d)
{
	struct cx24120_state *state = fe->demodulator_priv;
	struct cx24120_cmd cmd;
	int back_count;

	dev_dbg(&state->i2c->dev, "%s()\n", __func__);

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
			dev_dbg(&state->i2c->dev,
				"%s: diseqc sequence sent success\n",
				__func__);
			return 0;
		}
		msleep(20);
		back_count -= 20;
	} while (back_count);

	err("Too long waiting for diseqc.\n");
	return -ETIMEDOUT;
}


/* Read current tuning status */
static int cx24120_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct cx24120_state *state = fe->demodulator_priv;
	int lock;

	lock = cx24120_readreg(state, CX24120_REG_STATUS);

	dev_dbg(&state->i2c->dev, "%s() status = 0x%02x\n",
		__func__, lock);

	*status = 0;

	if (lock & CX24120_HAS_SIGNAL)
		*status = FE_HAS_SIGNAL;
	if (lock & CX24120_HAS_CARRIER)
		*status |= FE_HAS_CARRIER;
	if (lock & CX24120_HAS_VITERBI)
		*status |= FE_HAS_VITERBI | FE_HAS_SYNC;
	if (lock & CX24120_HAS_LOCK)
		*status |= FE_HAS_LOCK;

	/* TODO: is FE_HAS_SYNC in the right place?
	 * Other cx241xx drivers have this slightly
	 * different */

	return 0;
}


/* FEC & modulation lookup table
 * Used for decoding the REG_FECMODE register
 * once tuned in.
 */
static struct cx24120_modfec {
	fe_delivery_system_t delsys;
	fe_modulation_t mod;
	fe_code_rate_t fec;
	u8 val;
} modfec_lookup_table[] = {
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
	int GettedFEC;

	dev_dbg(&state->i2c->dev, "%s()\n", __func__);

	ret = cx24120_readreg(state, CX24120_REG_FECMODE);
	GettedFEC = ret & 0x3f; /* Lower 6 bits */

	dev_dbg(&state->i2c->dev, "%s: Get FEC: %d\n", __func__, GettedFEC);

	for (idx = 0; idx < ARRAY_SIZE(modfec_lookup_table); idx++) {
		if (modfec_lookup_table[idx].delsys != state->dcur.delsys)
			continue;
		if (modfec_lookup_table[idx].val != GettedFEC)
			continue;

		break; /* found */
	}

	if (idx >= ARRAY_SIZE(modfec_lookup_table)) {
		dev_dbg(&state->i2c->dev, "%s: Couldn't find fec!\n",
			__func__);
		return -EINVAL;
	}

	/* save values back to cache */
	c->modulation = modfec_lookup_table[idx].mod;
	c->fec_inner = modfec_lookup_table[idx].fec;
	c->pilot = (ret & 0x80) ? PILOT_ON : PILOT_OFF;

	dev_dbg(&state->i2c->dev,
		"%s: mod(%d), fec(%d), pilot(%d)\n",
		__func__,
		c->modulation, c->fec_inner, c->pilot);

	return 0;
}


/* Clock ratios lookup table
 *
 * Values obtained from much larger table in old driver
 * which had numerous entries which would never match.
 *
 * There's probably some way of calculating these but I
 * can't determine the pattern
*/
static struct cx24120_clock_ratios_table {
	fe_delivery_system_t delsys;
	fe_pilot_t pilot;
	fe_modulation_t mod;
	fe_code_rate_t fec;
	u32 m_rat;
	u32 n_rat;
	u32 rate;
} clock_ratios_table[] = {
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

	dev_dbg(&state->i2c->dev,
		"%s: m=%d, n=%d; idx: %d m=%d, n=%d, rate=%d\n",
		__func__,
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
}


/* Set inversion value */
static int cx24120_set_inversion(struct cx24120_state *state,
	fe_spectral_inversion_t inversion)
{
	dev_dbg(&state->i2c->dev, "%s(%d)\n",
		__func__, inversion);

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

/*
 * FEC lookup table for tuning Some DVB-S2 val's have been found by
 * trial and error. Sofar it seems to match up with the contents of
 * the REG_FECMODE after tuning The rest will probably be the same but
 * would need testing.  Anything not in the table will run with
 * FEC_AUTO and take a while longer to tune in ( c.500ms instead of
 * 30ms )
 */
static struct cx24120_modfec_table {
	fe_delivery_system_t delsys;
	fe_modulation_t mod;
	fe_code_rate_t fec;
	u8 val;
} modfec_table[] = {
/*delsys	mod	fec	 val */
	{ SYS_DVBS,  QPSK,  FEC_1_2, 0x2e },
	{ SYS_DVBS,  QPSK,  FEC_2_3, 0x2f },
	{ SYS_DVBS,  QPSK,  FEC_3_4, 0x30 },
	{ SYS_DVBS,  QPSK,  FEC_5_6, 0x31 },
	{ SYS_DVBS,  QPSK,  FEC_6_7, 0x32 },
	{ SYS_DVBS,  QPSK,  FEC_7_8, 0x33 },

	{ SYS_DVBS2, QPSK,  FEC_3_4, 0x07 },

	{ SYS_DVBS2, PSK_8, FEC_2_3, 0x0d },
	{ SYS_DVBS2, PSK_8, FEC_3_4, 0x0e },
};

/* Set fec_val & fec_mask values from delsys, modulation & fec */
static int cx24120_set_fec(struct cx24120_state *state,
	fe_modulation_t mod, fe_code_rate_t fec)
{
	int idx;

	dev_dbg(&state->i2c->dev,
		"%s(0x%02x,0x%02x)\n", __func__, mod, fec);

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
static int cx24120_set_pilot(struct cx24120_state *state,
		fe_pilot_t pilot) {

	dev_dbg(&state->i2c->dev, "%s(%d)\n", __func__, pilot);

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
	dev_dbg(&state->i2c->dev, "%s(%d)\n",
		__func__, rate);

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


/* Table of time to tune for different symrates */
static struct cx24120_symrate_delay {
	fe_delivery_system_t delsys;
	u32 symrate;		/* Check for >= this symrate */
	u32 delay;		/* Timeout in ms */
} symrates_delay_table[] = {
	{ SYS_DVBS,  10000000,   400 },
	{ SYS_DVBS,   8000000,  2000 },
	{ SYS_DVBS,   6000000,  5000 },
	{ SYS_DVBS,   3000000, 10000 },
	{ SYS_DVBS,         0, 15000 },
	{ SYS_DVBS2, 10000000,   600 }, /* DVBS2 needs a little longer */
	{ SYS_DVBS2,  8000000,  2000 }, /* (so these might need bumping too) */
	{ SYS_DVBS2,  6000000,  5000 },
	{ SYS_DVBS2,  3000000, 10000 },
	{ SYS_DVBS2,        0, 15000 },
};


static int cx24120_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct cx24120_state *state = fe->demodulator_priv;
	struct cx24120_cmd cmd;
	int ret;
	int delay_cnt, sd_idx = 0;
	fe_status_t status;

	switch (c->delivery_system) {
	case SYS_DVBS2:
		dev_dbg(&state->i2c->dev, "%s() DVB-S2\n",
			__func__);
		break;
	case SYS_DVBS:
		dev_dbg(&state->i2c->dev, "%s() DVB-S\n",
			__func__);
		break;
	default:
		dev_dbg(&state->i2c->dev,
			"%s() Delivery system(%d) not supported\n",
			__func__, c->delivery_system);
		ret = -EINVAL;
		break;
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
		"%s: delsys      = %d\n", __func__, state->dcur.delsys);
	dev_dbg(&state->i2c->dev,
		"%s: modulation  = %d\n", __func__, state->dcur.modulation);
	dev_dbg(&state->i2c->dev,
		"%s: frequency   = %d\n", __func__, state->dcur.frequency);
	dev_dbg(&state->i2c->dev,
		"%s: pilot       = %d (val = 0x%02x)\n", __func__,
		state->dcur.pilot, state->dcur.pilot_val);
	dev_dbg(&state->i2c->dev,
		"%s: symbol_rate = %d (clkdiv/ratediv = 0x%02x/0x%02x)\n",
		 __func__, state->dcur.symbol_rate,
		 state->dcur.clkdiv, state->dcur.ratediv);
	dev_dbg(&state->i2c->dev,
		"%s: FEC         = %d (mask/val = 0x%02x/0x%02x)\n", __func__,
		state->dcur.fec, state->dcur.fec_mask, state->dcur.fec_val);
	dev_dbg(&state->i2c->dev,
		"%s: Inversion   = %d (val = 0x%02x)\n", __func__,
		state->dcur.inversion, state->dcur.inversion_val);

	/* Tune in */
	cmd.id = CMD_TUNEREQUEST;
	cmd.len = 15;
	cmd.arg[0] = 0;
	cmd.arg[1]  = (state->dcur.frequency & 0xff0000) >> 16;
	cmd.arg[2]  = (state->dcur.frequency & 0x00ff00) >> 8;
	cmd.arg[3]  = (state->dcur.frequency & 0x0000ff);
	cmd.arg[4]  = ((state->dcur.symbol_rate/1000) & 0xff00) >> 8;
	cmd.arg[5]  = ((state->dcur.symbol_rate/1000) & 0x00ff);
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

	/* Default time to tune */
	delay_cnt = 500;

	/* Establish time to tune from symrates_delay_table */
	for (sd_idx = 0; sd_idx < ARRAY_SIZE(symrates_delay_table); sd_idx++) {
		if (state->dcur.delsys != symrates_delay_table[sd_idx].delsys)
			continue;
		if (c->symbol_rate < symrates_delay_table[sd_idx].symrate)
			continue;

		/* found */
		delay_cnt = symrates_delay_table[sd_idx].delay;
		dev_dbg(&state->i2c->dev, "%s: Found symrate delay = %d\n",
			__func__, delay_cnt);
		break;
	}

	/* Wait for tuning */
	while (delay_cnt >= 0) {
		cx24120_read_status(fe, &status);
		if (status & FE_HAS_LOCK)
			goto tuned;
		msleep(20);
		delay_cnt -= 20;
	}

	/* Fail to tune */
	dev_dbg(&state->i2c->dev, "%s: Tuning failed\n", __func__);

	return -EINVAL;

tuned:
	dev_dbg(&state->i2c->dev, "%s: Tuning successful\n", __func__);

	/* Set clock ratios */
	cx24120_set_clock_ratios(fe);

	/* Old driver would do a msleep(200) here */

	/* Renable mpeg output */
	if (!state->mpeg_enabled)
		cx24120_msg_mpeg_output_global_config(state, 1);

	return 0;
}


/* Calculate vco from config */
static u64 cx24120_calculate_vco(struct cx24120_state *state)
{
	u32 vco;
	u64 inv_vco, res, xxyyzz;
	u32 xtal_khz = state->config->xtal_khz;

	xxyyzz = 0x400000000ULL;
	vco = xtal_khz * 10 * 4;
	inv_vco = xxyyzz / vco;
	res = xxyyzz % vco;

	if (inv_vco > xtal_khz * 10 * 2)
		++inv_vco;

	dev_dbg(&state->i2c->dev,
		"%s: xtal=%d, vco=%d, inv_vco=%lld, res=%lld\n",
		__func__, xtal_khz, vco, inv_vco, res);

	return inv_vco;
}


int cx24120_init(struct dvb_frontend *fe)
{
	const struct firmware *fw;
	struct cx24120_state *state = fe->demodulator_priv;
	struct cx24120_cmd cmd;
	u8 ret, ret_EA, reg1;
	u64 inv_vco;
	int reset_result;

	int i;
	unsigned char vers[4];

	if (state->cold_init)
		return 0;

	/* ???? */
	ret = cx24120_writereg(state, 0xea, 0x00);
	ret = cx24120_test_rom(state);
	ret = cx24120_readreg(state, 0xfb) & 0xfe;
	ret = cx24120_writereg(state, 0xfb, ret);
	ret = cx24120_readreg(state, 0xfc) & 0xfe;
	ret = cx24120_writereg(state, 0xfc, ret);
	ret = cx24120_writereg(state, 0xc3, 0x04);
	ret = cx24120_writereg(state, 0xc4, 0x04);
	ret = cx24120_writereg(state, 0xce, 0x00);
	ret = cx24120_writereg(state, 0xcf, 0x00);
	ret_EA = cx24120_readreg(state, 0xea) & 0xfe;
	ret = cx24120_writereg(state, 0xea, ret_EA);
	ret = cx24120_writereg(state, 0xeb, 0x0c);
	ret = cx24120_writereg(state, 0xec, 0x06);
	ret = cx24120_writereg(state, 0xed, 0x05);
	ret = cx24120_writereg(state, 0xee, 0x03);
	ret = cx24120_writereg(state, 0xef, 0x05);
	ret = cx24120_writereg(state, 0xf3, 0x03);
	ret = cx24120_writereg(state, 0xf4, 0x44);

	for (reg1 = 0xf0; reg1 < 0xf3; reg1++) {
		cx24120_writereg(state, reg1, 0x04);
		cx24120_writereg(state, reg1 - 10, 0x02);
	}

	ret = cx24120_writereg(state, 0xea, (ret_EA | 0x01));
	for (reg1 = 0xc5; reg1 < 0xcb; reg1 += 2) {
		ret = cx24120_writereg(state, reg1, 0x00);
		ret = cx24120_writereg(state, reg1 + 1, 0x00);
	}

	ret = cx24120_writereg(state, 0xe4, 0x03);
	ret = cx24120_writereg(state, 0xeb, 0x0a);

	dev_dbg(&state->i2c->dev,
		"%s: Requesting firmware (%s) to download...\n",
		__func__, CX24120_FIRMWARE);

	ret = state->config->request_firmware(fe, &fw, CX24120_FIRMWARE);
	if (ret) {
		err("Could not load firmware (%s): %d\n",
			CX24120_FIRMWARE, ret);
		return ret;
	}

	dev_dbg(&state->i2c->dev,
		"%s: Firmware found, size %d bytes (%02x %02x .. %02x %02x)\n",
		__func__,
		(int)fw->size,			/* firmware_size in bytes */
		fw->data[0],			/* fw 1st byte */
		fw->data[1],			/* fw 2d byte */
		fw->data[fw->size - 2],		/* fw before last byte */
		fw->data[fw->size - 1]);	/* fw last byte */

	ret = cx24120_test_rom(state);
	ret = cx24120_readreg(state, 0xfb) & 0xfe;
	ret = cx24120_writereg(state, 0xfb, ret);
	ret = cx24120_writereg(state, 0xe0, 0x76);
	ret = cx24120_writereg(state, 0xf7, 0x81);
	ret = cx24120_writereg(state, 0xf8, 0x00);
	ret = cx24120_writereg(state, 0xf9, 0x00);
	ret = cx24120_writeregN(state, 0xfa, fw->data, (fw->size - 1), 0x00);
	ret = cx24120_writereg(state, 0xf7, 0xc0);
	ret = cx24120_writereg(state, 0xe0, 0x00);
	ret = (fw->size - 2) & 0x00ff;
	ret = cx24120_writereg(state, 0xf8, ret);
	ret = ((fw->size - 2) >> 8) & 0x00ff;
	ret = cx24120_writereg(state, 0xf9, ret);
	ret = cx24120_writereg(state, 0xf7, 0x00);
	ret = cx24120_writereg(state, 0xdc, 0x00);
	ret = cx24120_writereg(state, 0xdc, 0x07);
	msleep(500);

	/* Check final byte matches final byte of firmware */
	ret = cx24120_readreg(state, 0xe1);
	if (ret == fw->data[fw->size - 1]) {
		dev_dbg(&state->i2c->dev,
			"%s: Firmware uploaded successfully\n",
			__func__);
		reset_result = 0;
	} else {
		err("Firmware upload failed. Last byte returned=0x%x\n", ret);
		reset_result = -EREMOTEIO;
	}
	ret = cx24120_writereg(state, 0xdc, 0x00);
	release_firmware(fw);
	if (reset_result != 0)
		return reset_result;


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
	inv_vco = cx24120_calculate_vco(state);

	cmd.id = CMD_VCO_SET;
	cmd.len = 12;
	cmd.arg[0] = 0x06;
	cmd.arg[1] = 0x2b;
	cmd.arg[2] = 0xd8;
	cmd.arg[3] = (inv_vco >> 8) & 0xff;
	cmd.arg[4] = (inv_vco) & 0xff;
	cmd.arg[5] = 0x03;
	cmd.arg[6] = 0x9d;
	cmd.arg[7] = 0xfc;
	cmd.arg[8] = 0x06;
	cmd.arg[9] = 0x03;
	cmd.arg[10] = 0x27;
	cmd.arg[11] = 0x7f;

	if (cx24120_message_send(state, &cmd)) {
		err("Error set VCO! :(\n");
		return -EREMOTEIO;
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

	ret = cx24120_readreg(state, 0xba);
	if (ret > 3) {
		dev_dbg(&state->i2c->dev, "%s: Reset-readreg 0xba: %x\n",
			__func__, ret);
		err("Error initialising tuner!\n");
		return -EREMOTEIO;
	}

	dev_dbg(&state->i2c->dev, "%s: Tuner initialised correctly.\n",
			__func__);


	/* Initialise mpeg outputs */
	ret = cx24120_writereg(state, 0xeb, 0x0a);
	if (cx24120_msg_mpeg_output_global_config(state, 0) ||
	    cx24120_msg_mpeg_output_config(state, 0) ||
	    cx24120_msg_mpeg_output_config(state, 1) ||
	    cx24120_msg_mpeg_output_config(state, 2)) {
		err("Error initialising mpeg output. :(\n");
		return -EREMOTEIO;
	}


	/* ???? */
	cmd.id = CMD_TUNER_INIT;
	cmd.len = 3;
	cmd.arg[0] = 0x00;
	cmd.arg[1] = 0x10;
	cmd.arg[2] = 0x10;
	if (cx24120_message_send(state, &cmd)) {
		err("Error sending final init message. :(\n");
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

	state->cold_init = 1;
	return 0;
}


static int cx24120_tune(struct dvb_frontend *fe, bool re_tune,
	unsigned int mode_flags, unsigned int *delay, fe_status_t *status)
{
	struct cx24120_state *state = fe->demodulator_priv;
	int ret;

	dev_dbg(&state->i2c->dev, "%s(%d)\n", __func__, re_tune);

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


/*static int cx24120_wakeup(struct dvb_frontend *fe)
 * {
 *   return 0;
 * }
*/


static int cx24120_get_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct cx24120_state *state = fe->demodulator_priv;
	u8 freq1, freq2, freq3;

	dev_dbg(&state->i2c->dev, "%s()", __func__);

	/* don't return empty data if we're not tuned in */
	if (state->mpeg_enabled)
		return 0;

	/* Get frequency */
	freq1 = cx24120_readreg(state, CX24120_REG_FREQ1);
	freq2 = cx24120_readreg(state, CX24120_REG_FREQ2);
	freq3 = cx24120_readreg(state, CX24120_REG_FREQ3);
	c->frequency = (freq3 << 16) | (freq2 << 8) | freq1;
	dev_dbg(&state->i2c->dev, "%s frequency = %d\n", __func__,
		c->frequency);

	/* Get modulation, fec, pilot */
	cx24120_get_fec(fe);

	return 0;
}


static void cx24120_release(struct dvb_frontend *fe)
{
	struct cx24120_state *state = fe->demodulator_priv;

	dev_dbg(&state->i2c->dev, "%s: Clear state structure\n", __func__);
	kfree(state);
}


static int cx24120_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct cx24120_state *state = fe->demodulator_priv;

	*ucblocks = (cx24120_readreg(state, CX24120_REG_UCB_H) << 8) |
		     cx24120_readreg(state, CX24120_REG_UCB_L);

	dev_dbg(&state->i2c->dev, "%s: Blocks = %d\n",
			__func__, *ucblocks);
	return 0;
}


static struct dvb_frontend_ops cx24120_ops = {
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

/*
    Conexant cx24116/cx24118 - DVBS/S2 Satellite demod/tuner driver

    Copyright (C) 2006-2008 Steven Toth <stoth@hauppauge.com>

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

/*
 * Updates by Darron Broad 2007.
 *
 * March
 *      Fixed some bugs.
 *      Added diseqc support.
 *      Added corrected signal strength support.
 *
 * August
 *	Sync with legacy version.
 *	Some clean ups.
 */
/* Updates by Igor Liplianin
 *
 * September, 9th 2008
 *	Fixed locking on high symbol rates (>30000).
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/firmware.h>

#include "dvb_frontend.h"
#include "cx24116.h"

/*
 * Fetch firmware in the following manner.
 *
 * #!/bin/sh
 * wget ftp://167.206.143.11/outgoing/Oxford/88x_2_117_24275_1_INF.zip
 * unzip 88x_2_117_24275_1_INF.zip
 * dd if=Driver88/hcw88bda.sys of=dvb-fe-cx24116.fw skip=81768 bs=1 count=32522
 */
#define CX24116_DEFAULT_FIRMWARE "dvb-fe-cx24116.fw"
#define CX24116_SEARCH_RANGE_KHZ 5000

/* registers (TO BE COMPLETED) */
#define CX24116_REG_SIGNAL (0xd5)

/* arg buffer size */
#define CX24116_ARGLEN (0x1e)

/* arg offset for DiSEqC */
#define CX24116_DISEQC_BURST  (1)
#define CX24116_DISEQC_ARG2_2 (2)   /* unknown value=2 */
#define CX24116_DISEQC_ARG3_0 (3)   /* unknown value=0 */
#define CX24116_DISEQC_ARG4_0 (4)   /* unknown value=0 */
#define CX24116_DISEQC_MSGLEN (5)
#define CX24116_DISEQC_MSGOFS (6)

/* DiSEqC burst */
#define CX24116_DISEQC_MINI_A (0)
#define CX24116_DISEQC_MINI_B (1)

static int debug = 0;
#define dprintk(args...) \
	do { \
		if (debug) printk ("cx24116: " args); \
	} while (0)

enum cmds
{
	CMD_INIT_CMD10  = 0x10,
	CMD_TUNEREQUEST = 0x11,
	CMD_INIT_CMD13  = 0x13,
	CMD_INIT_CMD14  = 0x14,
	CMD_SEND_DISEQC = 0x21,
	CMD_SET_TONEPRE = 0x22,
	CMD_SET_TONE    = 0x23,
};

/* The Demod/Tuner can't easily provide these, we cache them */
struct cx24116_tuning
{
	u32 frequency;
	u32 symbol_rate;
	fe_spectral_inversion_t inversion;
	fe_code_rate_t fec;

	fe_modulation_t modulation;

	/* Demod values */
	u8 fec_val;
	u8 fec_mask;
	u8 inversion_val;
};

/* Basic commands that are sent to the firmware */
struct cx24116_cmd
{
	u8 len;
	u8 args[CX24116_ARGLEN];
};

struct cx24116_state
{
	struct i2c_adapter* i2c;
	const struct cx24116_config* config;

	struct dvb_frontend frontend;

	struct cx24116_tuning dcur;
	struct cx24116_tuning dnxt;

	u8 skip_fw_load;
	u8 burst;
};

static int cx24116_writereg(struct cx24116_state* state, int reg, int data)
{
	u8 buf[] = { reg, data };
	struct i2c_msg msg = { .addr = state->config->demod_address,
		.flags = 0, .buf = buf, .len = 2 };
	int err;

	if (debug>1)
		printk("cx24116: %s: write reg 0x%02x, value 0x%02x\n",
						__func__,reg, data);

	if ((err = i2c_transfer(state->i2c, &msg, 1)) != 1) {
		printk("%s: writereg error(err == %i, reg == 0x%02x,"
			 " value == 0x%02x)\n", __func__, err, reg, data);
		return -EREMOTEIO;
	}

	return 0;
}

/* Bulk byte writes to a single I2C address, for 32k firmware load */
static int cx24116_writeregN(struct cx24116_state* state, int reg, u8 *data, u16 len)
{
	int ret = -EREMOTEIO;
	struct i2c_msg msg;
	u8 *buf;

	buf = kmalloc(len + 1, GFP_KERNEL);
	if (buf == NULL) {
		printk("Unable to kmalloc\n");
		ret = -ENOMEM;
		goto error;
	}

	*(buf) = reg;
	memcpy(buf + 1, data, len);

	msg.addr = state->config->demod_address;
	msg.flags = 0;
	msg.buf = buf;
	msg.len = len + 1;

	if (debug>1)
		printk("cx24116: %s:  write regN 0x%02x, len = %d\n",
						__func__,reg, len);

	if ((ret = i2c_transfer(state->i2c, &msg, 1)) != 1) {
		printk("%s: writereg error(err == %i, reg == 0x%02x\n",
			 __func__, ret, reg);
		ret = -EREMOTEIO;
	}

error:
	kfree(buf);

	return ret;
}

static int cx24116_readreg(struct cx24116_state* state, u8 reg)
{
	int ret;
	u8 b0[] = { reg };
	u8 b1[] = { 0 };
	struct i2c_msg msg[] = {
		{ .addr = state->config->demod_address, .flags = 0, .buf = b0, .len = 1 },
		{ .addr = state->config->demod_address, .flags = I2C_M_RD, .buf = b1, .len = 1 }
	};

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2) {
		printk("%s: reg=0x%x (error=%d)\n", __func__, reg, ret);
		return ret;
	}

	if (debug>1)
		printk("cx24116: read reg 0x%02x, value 0x%02x\n",reg, b1[0]);

	return b1[0];
}

static int cx24116_set_inversion(struct cx24116_state* state, fe_spectral_inversion_t inversion)
{
	dprintk("%s(%d)\n", __func__, inversion);

	switch (inversion) {
	case INVERSION_OFF:
		state->dnxt.inversion_val = 0x00;
		break;
	case INVERSION_ON:
		state->dnxt.inversion_val = 0x04;
		break;
	case INVERSION_AUTO:
		state->dnxt.inversion_val = 0x0C;
		break;
	default:
		return -EINVAL;
	}

	state->dnxt.inversion = inversion;

	return 0;
}

/* A table of modulation, fec and configuration bytes for the demod.
 * Not all S2 mmodulation schemes are support and not all rates with
 * a scheme are support. Especially, no auto detect when in S2 mode.
 */
struct cx24116_modfec {
	fe_modulation_t modulation;
	fe_code_rate_t fec;
	u8 mask;	/* In DVBS mode this is used to autodetect */
	u8 val;		/* Passed to the firmware to indicate mode selection */
} CX24116_MODFEC_MODES[] = {
 /* QPSK. For unknown rates we set hardware to auto detect 0xfe 0x30 */
 { QPSK, FEC_NONE, 0xfe, 0x30 },
 { QPSK, FEC_1_2,  0x02, 0x2e },
 { QPSK, FEC_2_3,  0x04, 0x2f },
 { QPSK, FEC_3_4,  0x08, 0x30 },
 { QPSK, FEC_4_5,  0xfe, 0x30 },
 { QPSK, FEC_5_6,  0x20, 0x31 },
 { QPSK, FEC_6_7,  0xfe, 0x30 },
 { QPSK, FEC_7_8,  0x80, 0x32 },
 { QPSK, FEC_8_9,  0xfe, 0x30 },
 { QPSK, FEC_AUTO, 0xfe, 0x30 },
 /* NBC-QPSK */
 { NBC_QPSK, FEC_1_2,  0x00, 0x04 },
 { NBC_QPSK, FEC_3_5,  0x00, 0x05 },
 { NBC_QPSK, FEC_2_3,  0x00, 0x06 },
 { NBC_QPSK, FEC_3_4,  0x00, 0x07 },
 { NBC_QPSK, FEC_4_5,  0x00, 0x08 },
 { NBC_QPSK, FEC_5_6,  0x00, 0x09 },
 { NBC_QPSK, FEC_8_9,  0x00, 0x0a },
 { NBC_QPSK, FEC_9_10, 0x00, 0x0b },
 /* 8PSK */
 { _8PSK, FEC_3_5,  0x00, 0x0c },
 { _8PSK, FEC_2_3,  0x00, 0x0d },
 { _8PSK, FEC_3_4,  0x00, 0x0e },
 { _8PSK, FEC_5_6,  0x00, 0x0f },
 { _8PSK, FEC_9_10, 0x00, 0x11 },
};

static int cx24116_lookup_fecmod(struct cx24116_state* state,
	fe_modulation_t m, fe_code_rate_t f)
{
	int i, ret = -EOPNOTSUPP;

	for(i=0 ; i < sizeof(CX24116_MODFEC_MODES) / sizeof(struct cx24116_modfec) ; i++)
	{
		if( (m == CX24116_MODFEC_MODES[i].modulation) &&
			(f == CX24116_MODFEC_MODES[i].fec) )
			{
				ret = i;
				break;
			}
	}

	return ret;
}

static int cx24116_set_fec(struct cx24116_state* state, fe_modulation_t mod, fe_code_rate_t fec)
{
	int ret = 0;
	dprintk("%s()\n", __func__);

	ret = cx24116_lookup_fecmod(state, mod, fec);

	if(ret < 0)
		return ret;

	state->dnxt.fec_val = CX24116_MODFEC_MODES[ret].val;
	state->dnxt.fec_mask = CX24116_MODFEC_MODES[ret].mask;
	dprintk("%s() fec_val/mask = 0x%02x/0x%02x\n", __func__,
		state->dnxt.fec_val, state->dnxt.fec_mask);

	return 0;
}

static int cx24116_set_symbolrate(struct cx24116_state* state, u32 rate)
{
	int ret = 0;

	dprintk("%s()\n", __func__);

	state->dnxt.symbol_rate = rate;

	dprintk("%s() symbol_rate = %d\n", __func__, state->dnxt.symbol_rate);

	/*  check if symbol rate is within limits */
	if ((state->dnxt.symbol_rate > state->frontend.ops.info.symbol_rate_max) ||
	    (state->dnxt.symbol_rate < state->frontend.ops.info.symbol_rate_min))
		ret = -EOPNOTSUPP;

	return ret;
}

static int cx24116_load_firmware (struct dvb_frontend* fe, const struct firmware *fw);

static int cx24116_firmware_ondemand(struct dvb_frontend* fe)
{
	struct cx24116_state *state = fe->demodulator_priv;
	const struct firmware *fw;
	int ret = 0;

	dprintk("%s()\n",__func__);

	if (cx24116_readreg(state, 0x20) > 0)
	{

		if (state->skip_fw_load)
			return 0;

		/* Load firmware */
		/* request the firmware, this will block until someone uploads it */
		printk("%s: Waiting for firmware upload (%s)...\n", __func__, CX24116_DEFAULT_FIRMWARE);
		ret = request_firmware(&fw, CX24116_DEFAULT_FIRMWARE, &state->i2c->dev);
		printk("%s: Waiting for firmware upload(2)...\n", __func__);
		if (ret) {
			printk("%s: No firmware uploaded (timeout or file not found?)\n", __func__);
			return ret;
		}

		/* Make sure we don't recurse back through here during loading */
		state->skip_fw_load = 1;

		ret = cx24116_load_firmware(fe, fw);
		if (ret)
			printk("%s: Writing firmware to device failed\n", __func__);

		release_firmware(fw);

		printk("%s: Firmware upload %s\n", __func__, ret == 0 ? "complete" : "failed");

		/* Ensure firmware is always loaded if required */
		state->skip_fw_load = 0;
	}

	return ret;
}

/* Take a basic firmware command structure, format it and forward it for processing */
static int cx24116_cmd_execute(struct dvb_frontend* fe, struct cx24116_cmd *cmd)
{
	struct cx24116_state *state = fe->demodulator_priv;
	int i, ret;

	dprintk("%s()\n", __func__);

	/* Load the firmware if required */
	if ( (ret = cx24116_firmware_ondemand(fe)) != 0)
	{
		printk("%s(): Unable initialise the firmware\n", __func__);
		return ret;
	}

	/* Write the command */
	for(i = 0; i < cmd->len ; i++)
	{
		dprintk("%s: 0x%02x == 0x%02x\n", __func__, i, cmd->args[i]);
		cx24116_writereg(state, i, cmd->args[i]);
	}

	/* Start execution and wait for cmd to terminate */
	cx24116_writereg(state, 0x1f, 0x01);
	while( cx24116_readreg(state, 0x1f) )
	{
		msleep(10);
		if(i++ > 64)
		{
			/* Avoid looping forever if the firmware does no respond */
			printk("%s() Firmware not responding\n", __func__);
			return -EREMOTEIO;
		}
	}
	return 0;
}

static int cx24116_load_firmware (struct dvb_frontend* fe, const struct firmware *fw)
{
	struct cx24116_state* state = fe->demodulator_priv;
	struct cx24116_cmd cmd;
	int ret;

	dprintk("%s\n", __func__);
	dprintk("Firmware is %zu bytes (%02x %02x .. %02x %02x)\n"
			,fw->size
			,fw->data[0]
			,fw->data[1]
			,fw->data[ fw->size-2 ]
			,fw->data[ fw->size-1 ]
			);

	/* Toggle 88x SRST pin to reset demod */
	if (state->config->reset_device)
		state->config->reset_device(fe);

	/* Begin the firmware load process */
	/* Prepare the demod, load the firmware, cleanup after load */
	cx24116_writereg(state, 0xF1, 0x08);
	cx24116_writereg(state, 0xF2, cx24116_readreg(state, 0xF2) | 0x03);
	cx24116_writereg(state, 0xF3, 0x46);
	cx24116_writereg(state, 0xF9, 0x00);

	cx24116_writereg(state, 0xF0, 0x03);
	cx24116_writereg(state, 0xF4, 0x81);
	cx24116_writereg(state, 0xF5, 0x00);
	cx24116_writereg(state, 0xF6, 0x00);

	/* write the entire firmware as one transaction */
	cx24116_writeregN(state, 0xF7, fw->data, fw->size);

	cx24116_writereg(state, 0xF4, 0x10);
	cx24116_writereg(state, 0xF0, 0x00);
	cx24116_writereg(state, 0xF8, 0x06);

	/* Firmware CMD 10: Chip config? */
	cmd.args[0x00] = CMD_INIT_CMD10;
	cmd.args[0x01] = 0x05;
	cmd.args[0x02] = 0xdc;
	cmd.args[0x03] = 0xda;
	cmd.args[0x04] = 0xae;
	cmd.args[0x05] = 0xaa;
	cmd.args[0x06] = 0x04;
	cmd.args[0x07] = 0x9d;
	cmd.args[0x08] = 0xfc;
	cmd.args[0x09] = 0x06;
	cmd.len= 0x0a;
	ret = cx24116_cmd_execute(fe, &cmd);
	if (ret != 0)
		return ret;

	cx24116_writereg(state, 0x9d, 0x00);

	/* Firmware CMD 14: Unknown */
	cmd.args[0x00] = CMD_INIT_CMD14;
	cmd.args[0x01] = 0x00;
	cmd.args[0x02] = 0x00;
	cmd.len= 0x03;
	ret = cx24116_cmd_execute(fe, &cmd);
	if (ret != 0)
		return ret;

	cx24116_writereg(state, 0xe5, 0x00);

	/* Firmware CMD 13: Unknown - Firmware config? */
	cmd.args[0x00] = CMD_INIT_CMD13;
	cmd.args[0x01] = 0x01;
	cmd.args[0x02] = 0x75;
	cmd.args[0x03] = 0x00;
	cmd.args[0x04] = 0x02;
	cmd.args[0x05] = 0x00;
	cmd.len= 0x06;
	ret = cx24116_cmd_execute(fe, &cmd);
	if (ret != 0)
		return ret;

	return 0;
}

static int cx24116_set_voltage(struct dvb_frontend* fe, fe_sec_voltage_t voltage)
{
	/* The isl6421 module will override this function in the fops. */
	dprintk("%s() This should never appear if the isl6421 module is loaded correctly\n",__func__);

	return -EOPNOTSUPP;
}

static int cx24116_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct cx24116_state *state = fe->demodulator_priv;

	int lock = cx24116_readreg(state, 0x9d);

	dprintk("%s: status = 0x%02x\n", __func__, lock);

	*status = 0;

	if (lock & 0x01)
		*status |= FE_HAS_SIGNAL;
	if (lock & 0x02)
		*status |= FE_HAS_CARRIER;
	if (lock & 0x04)
		*status |= FE_HAS_VITERBI;
	if (lock & 0x08)
		*status |= FE_HAS_SYNC | FE_HAS_LOCK;

	return 0;
}

/* TODO: Not clear how we do this */
static int cx24116_read_ber(struct dvb_frontend* fe, u32* ber)
{
	//struct cx24116_state *state = fe->demodulator_priv;
	dprintk("%s()\n", __func__);
	*ber = 0;

	return 0;
}

/* Signal strength (0..100)% = (sig & 0xf0) * 10 + (sig & 0x0f) * 10 / 16 */
static int cx24116_read_signal_strength(struct dvb_frontend* fe, u16* signal_strength)
{
	struct cx24116_state *state = fe->demodulator_priv;
	u8 strength_reg;
	static const u32 strength_tab[] = { /* 10 x Table (rounded up) */
		0x00000,0x0199A,0x03333,0x04ccD,0x06667,0x08000,0x0999A,0x0b333,0x0cccD,0x0e667,
		0x10000,0x1199A,0x13333,0x14ccD,0x16667,0x18000 };

	dprintk("%s()\n", __func__);

	strength_reg = cx24116_readreg(state, CX24116_REG_SIGNAL);

	if(strength_reg < 0xa0)
		*signal_strength = strength_tab [ ( strength_reg & 0xf0 )   >> 4 ] +
				( strength_tab [ ( strength_reg & 0x0f ) ] >> 4 );
	else
		*signal_strength = 0xffff;

	dprintk("%s: Signal strength (raw / cooked) = (0x%02x / 0x%04x)\n",
		__func__,strength_reg,*signal_strength);

	return 0;
}

/* TODO: Not clear how we do this */
static int cx24116_read_snr(struct dvb_frontend* fe, u16* snr)
{
	//struct cx24116_state *state = fe->demodulator_priv;
	dprintk("%s()\n", __func__);
	*snr = 0;

	return 0;
}

/* TODO: Not clear how we do this */
static int cx24116_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	//struct cx24116_state *state = fe->demodulator_priv;
	dprintk("%s()\n", __func__);
	*ucblocks = 0;

	return 0;
}

/* Overwrite the current tuning params, we are about to tune */
static void cx24116_clone_params(struct dvb_frontend* fe)
{
	struct cx24116_state *state = fe->demodulator_priv;
	memcpy(&state->dcur, &state->dnxt, sizeof(state->dcur));
}

static int cx24116_set_tone(struct dvb_frontend* fe, fe_sec_tone_mode_t tone)
{
	struct cx24116_cmd cmd;
	int ret;

	dprintk("%s(%d)\n", __func__, tone);
	if ( (tone != SEC_TONE_ON) && (tone != SEC_TONE_OFF) ) {
		printk("%s: Invalid, tone=%d\n", __func__, tone);
		return -EINVAL;
	}

	/* This is always done before the tone is set */
	cmd.args[0x00] = CMD_SET_TONEPRE;
	cmd.args[0x01] = 0x00;
	cmd.len= 0x02;
	ret = cx24116_cmd_execute(fe, &cmd);
	if (ret != 0)
		return ret;

	/* Now we set the tone */
	cmd.args[0x00] = CMD_SET_TONE;
	cmd.args[0x01] = 0x00;
	cmd.args[0x02] = 0x00;

	switch (tone) {
	case SEC_TONE_ON:
		dprintk("%s: setting tone on\n", __func__);
		cmd.args[0x03] = 0x01;
		break;
	case SEC_TONE_OFF:
		dprintk("%s: setting tone off\n",__func__);
		cmd.args[0x03] = 0x00;
		break;
	}
	cmd.len= 0x04;

	return cx24116_cmd_execute(fe, &cmd);
}

/* Initialise DiSEqC */
static int cx24116_diseqc_init(struct dvb_frontend* fe)
{
	struct cx24116_state *state = fe->demodulator_priv;

	/* Default DiSEqC burst state */
	state->burst = CX24116_DISEQC_MINI_A;

	return 0;
}

/* Send DiSEqC message with derived burst (hack) || previous burst */
static int cx24116_send_diseqc_msg(struct dvb_frontend* fe, struct dvb_diseqc_master_cmd *d)
{
	struct cx24116_state *state = fe->demodulator_priv;
	struct cx24116_cmd cmd;
	int i, ret;

	/* Dump DiSEqC message */
	if (debug) {
		printk("cx24116: %s(", __func__);
		for(i = 0 ; i < d->msg_len ;) {
			printk("0x%02x", d->msg[i]);
			if(++i < d->msg_len)
				printk(", ");
		}
		printk(")\n");
	}

	if(d->msg_len > (CX24116_ARGLEN - CX24116_DISEQC_MSGOFS))
		return -EINVAL;

	cmd.args[0x00] = CMD_SEND_DISEQC;
	cmd.args[CX24116_DISEQC_ARG2_2] = 0x02;
	cmd.args[CX24116_DISEQC_ARG3_0] = 0x00;
	cmd.args[CX24116_DISEQC_ARG4_0] = 0x00;

	/* DiSEqC message */
	for (i = 0; i < d->msg_len; i++)
		cmd.args[CX24116_DISEQC_MSGOFS + i] = d->msg[i];

	/* Hack: Derive burst from command else use previous burst */
	if(d->msg_len >= 4 && d->msg[2] == 0x38)
		cmd.args[CX24116_DISEQC_BURST] = (d->msg[3] >> 2) & 1;
	else
		cmd.args[CX24116_DISEQC_BURST] = state->burst;

	cmd.args[CX24116_DISEQC_MSGLEN] = d->msg_len;
	cmd.len = CX24116_DISEQC_MSGOFS + d->msg_len;

	ret = cx24116_cmd_execute(fe, &cmd);

	/* Firmware command duration is unknown, so guess...
	 *
	 * Eutelsat spec:
	 * >15ms delay		+
	 *  13.5ms per byte	+
	 * >15ms delay		+
	 *  12.5ms burst	+
	 * >15ms delay
	 */
	if(ret == 0)
		msleep( (cmd.args[CX24116_DISEQC_MSGLEN] << 4) + 60 );

	return ret;
}

/* Send DiSEqC burst */
static int cx24116_diseqc_send_burst(struct dvb_frontend* fe, fe_sec_mini_cmd_t burst)
{
	struct cx24116_state *state = fe->demodulator_priv;
	struct cx24116_cmd cmd;
	int ret;

	dprintk("%s(%d)\n",__func__,(int)burst);

	cmd.args[0x00] = CMD_SEND_DISEQC;
	cmd.args[CX24116_DISEQC_ARG2_2] = 0x02;
	cmd.args[CX24116_DISEQC_ARG3_0] = 0x00;
	cmd.args[CX24116_DISEQC_ARG4_0] = 0x00;

	if (burst == SEC_MINI_A)
		cmd.args[CX24116_DISEQC_BURST] = CX24116_DISEQC_MINI_A;
	else if(burst == SEC_MINI_B)
		cmd.args[CX24116_DISEQC_BURST] = CX24116_DISEQC_MINI_B;
	else
		return -EINVAL;

	/* Cache as previous burst state */
	state->burst= cmd.args[CX24116_DISEQC_BURST];

	cmd.args[CX24116_DISEQC_MSGLEN] = 0x00;
	cmd.len= CX24116_DISEQC_MSGOFS;

	ret= cx24116_cmd_execute(fe, &cmd);

	/* Firmware command duration is unknown, so guess... */
	if(ret == 0)
		msleep(60);

	return ret;
}

static void cx24116_release(struct dvb_frontend* fe)
{
	struct cx24116_state* state = fe->demodulator_priv;
	dprintk("%s\n",__func__);
	kfree(state);
}

static struct dvb_frontend_ops cx24116_ops;

struct dvb_frontend* cx24116_attach(const struct cx24116_config* config,
				    struct i2c_adapter* i2c)
{
	struct cx24116_state* state = NULL;
	int ret;

	dprintk("%s\n",__func__);

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct cx24116_state), GFP_KERNEL);
	if (state == NULL) {
		printk("Unable to kmalloc\n");
		goto error;
	}

	/* setup the state */
	memset(state, 0, sizeof(struct cx24116_state));

	state->config = config;
	state->i2c = i2c;

	/* check if the demod is present */
	ret = (cx24116_readreg(state, 0xFF) << 8) | cx24116_readreg(state, 0xFE);
	if (ret != 0x0501) {
		printk("Invalid probe, probably not a CX24116 device\n");
		goto error;
	}

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &cx24116_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);

	return NULL;
}

static int cx24116_get_params(struct dvb_frontend* fe)
{
	struct cx24116_state *state = fe->demodulator_priv;
	struct tv_frontend_properties *cache = &fe->tv_property_cache;

	dprintk("%s()\n",__func__);

	cache->frequency = state->dcur.frequency;
	cache->inversion = state->dcur.inversion;
	cache->modulation = state->dcur.modulation;
	cache->fec_inner = state->dcur.fec;
	cache->symbol_rate = state->dcur.symbol_rate;

	return 0;
}

static int cx24116_initfe(struct dvb_frontend* fe)
{
	dprintk("%s()\n",__func__);

	return cx24116_diseqc_init(fe);
}

static int cx24116_set_property(struct dvb_frontend *fe, tv_property_t* tvp)
{
	dprintk("%s(..)\n", __func__);
	return 0;
}

static int cx24116_set_params(struct dvb_frontend *fe)
{
	dprintk("%s(..) We were notified that a tune request may occur\n", __func__);
	return 0;
}

/* dvb-core told us to tune, the tv property cache will be complete,
 * it's safe for is to pull values and use them for tuning purposes.
 */
static int cx24116_set_frontend(struct dvb_frontend* fe, struct dvb_frontend_parameters *p)
{
	struct cx24116_state *state = fe->demodulator_priv;
	struct tv_frontend_properties *c = &fe->tv_property_cache;
	struct cx24116_cmd cmd;
	fe_status_t tunerstat;
	int ret, above30msps;
	u8 retune=4;

	dprintk("%s()\n",__func__);

	state->dnxt.modulation = c->modulation;
	state->dnxt.frequency = c->frequency;

	if ((ret = cx24116_set_inversion(state, c->inversion)) !=  0)
		return ret;

	if ((ret = cx24116_set_fec(state, c->modulation, c->fec_inner)) !=  0)
		return ret;

	if ((ret = cx24116_set_symbolrate(state, c->symbol_rate)) !=  0)
		return ret;

	/* discard the 'current' tuning parameters and prepare to tune */
	cx24116_clone_params(fe);

	dprintk("%s:   frequency   = %d\n", __func__, state->dcur.frequency);
	dprintk("%s:   symbol_rate = %d\n", __func__, state->dcur.symbol_rate);
	dprintk("%s:   FEC         = %d (mask/val = 0x%02x/0x%02x)\n", __func__,
		state->dcur.fec, state->dcur.fec_mask, state->dcur.fec_val);
	dprintk("%s:   Inversion   = %d (val = 0x%02x)\n", __func__,
		state->dcur.inversion, state->dcur.inversion_val);

	if (state->config->set_ts_params)
		state->config->set_ts_params(fe, 0);

	above30msps = (state->dcur.symbol_rate > 30000000);

	if (above30msps){
		cx24116_writereg(state, 0xF9, 0x01);
		cx24116_writereg(state, 0xF3, 0x44);
	} else {
		cx24116_writereg(state, 0xF9, 0x00);
		cx24116_writereg(state, 0xF3, 0x46);
	}

	/* Prepare a tune request */
	cmd.args[0x00] = CMD_TUNEREQUEST;

	/* Frequency */
	cmd.args[0x01] = (state->dcur.frequency & 0xff0000) >> 16;
	cmd.args[0x02] = (state->dcur.frequency & 0x00ff00) >> 8;
	cmd.args[0x03] = (state->dcur.frequency & 0x0000ff);

	/* Symbol Rate */
	cmd.args[0x04] = ((state->dcur.symbol_rate / 1000) & 0xff00) >> 8;
	cmd.args[0x05] = ((state->dcur.symbol_rate / 1000) & 0x00ff);

	/* Automatic Inversion */
	cmd.args[0x06] = state->dcur.inversion_val;

	/* Modulation / FEC & Pilot Off */
	cmd.args[0x07] = state->dcur.fec_val;

	if (c->pilot == PILOT_ON)
		cmd.args[0x07] |= 0x40;

	cmd.args[0x08] = CX24116_SEARCH_RANGE_KHZ >> 8;
	cmd.args[0x09] = CX24116_SEARCH_RANGE_KHZ & 0xff;
	cmd.args[0x0a] = 0x00;
	cmd.args[0x0b] = 0x00;
	cmd.args[0x0c] = 0x02;
	cmd.args[0x0d] = state->dcur.fec_mask;

	if (above30msps){
		cmd.args[0x0e] = 0x04;
		cmd.args[0x0f] = 0x00;
		cmd.args[0x10] = 0x01;
		cmd.args[0x11] = 0x77;
		cmd.args[0x12] = 0x36;
	} else {
		cmd.args[0x0e] = 0x06;
		cmd.args[0x0f] = 0x00;
		cmd.args[0x10] = 0x00;
		cmd.args[0x11] = 0xFA;
		cmd.args[0x12] = 0x24;
	}

	cmd.len= 0x13;

	/* We need to support pilot and non-pilot tuning in the
	 * driver automatically. This is a workaround for because
	 * the demod does not support autodetect.
	 */
	do {
		/* Reset status register? */
		cx24116_writereg(state, 0x9d, 0xc1);

		/* Tune */
		ret = cx24116_cmd_execute(fe, &cmd);
		if( ret != 0 )
			break;

		/* The hardware can take time to lock, wait a while */
		msleep(500);

		cx24116_read_status(fe, &tunerstat);
		if(tunerstat & FE_HAS_SIGNAL) {
			if(tunerstat & FE_HAS_SYNC)
				/* Tuned */
				break;
			else if(c->pilot == PILOT_AUTO)
				/* Toggle pilot bit */
				cmd.args[0x07] ^= 0x40;
		}
	}
	while(--retune);

	return ret;
}

static struct dvb_frontend_ops cx24116_ops = {

	.info = {
		.name = "Conexant CX24116/CX24118",
		.type = FE_QPSK,
		.frequency_min = 950000,
		.frequency_max = 2150000,
		.frequency_stepsize = 1011, /* kHz for QPSK frontends */
		.frequency_tolerance = 5000,
		.symbol_rate_min = 1000000,
		.symbol_rate_max = 45000000,
		.caps = FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_4_5 | FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 |
			FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK | FE_CAN_RECOVER
	},

	.release = cx24116_release,

	.init = cx24116_initfe,
	.read_status = cx24116_read_status,
	.read_ber = cx24116_read_ber,
	.read_signal_strength = cx24116_read_signal_strength,
	.read_snr = cx24116_read_snr,
	.read_ucblocks = cx24116_read_ucblocks,
	.set_tone = cx24116_set_tone,
	.set_voltage = cx24116_set_voltage,
	.diseqc_send_master_cmd = cx24116_send_diseqc_msg,
	.diseqc_send_burst = cx24116_diseqc_send_burst,

	.set_property = cx24116_set_property,
	.set_params = cx24116_set_params,
	.set_frontend = cx24116_set_frontend,
};

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Activates frontend debugging (default:0)");

MODULE_DESCRIPTION("DVB Frontend module for Conexant cx24116/cx24118 hardware");
MODULE_AUTHOR("Steven Toth");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(cx24116_attach);

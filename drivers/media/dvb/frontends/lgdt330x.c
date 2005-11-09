/*
 *    Support for LGDT3302 and LGDT3303 - VSB/QAM
 *
 *    Copyright (C) 2005 Wilson Michaels <wilsonmichaels@earthlink.net>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*
 *                      NOTES ABOUT THIS DRIVER
 *
 * This Linux driver supports:
 *   DViCO FusionHDTV 3 Gold-Q
 *   DViCO FusionHDTV 3 Gold-T
 *   DViCO FusionHDTV 5 Gold
 *
 * TODO:
 * signal strength always returns 0.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <asm/byteorder.h>

#include "dvb_frontend.h"
#include "lgdt330x_priv.h"
#include "lgdt330x.h"

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug,"Turn on/off lgdt330x frontend debugging (default:off).");
#define dprintk(args...) \
do { \
if (debug) printk(KERN_DEBUG "lgdt330x: " args); \
} while (0)

struct lgdt330x_state
{
	struct i2c_adapter* i2c;
	struct dvb_frontend_ops ops;

	/* Configuration settings */
	const struct lgdt330x_config* config;

	struct dvb_frontend frontend;

	/* Demodulator private data */
	fe_modulation_t current_modulation;

	/* Tuner private data */
	u32 current_frequency;
};

static int i2c_write_demod_bytes (struct lgdt330x_state* state,
				  u8 *buf, /* data bytes to send */
				  int len  /* number of bytes to send */ )
{
	struct i2c_msg msg =
		{ .addr = state->config->demod_address,
		  .flags = 0,
		  .buf = buf,
		  .len = 2 };
	int i;
	int err;

	for (i=0; i<len-1; i+=2){
		if ((err = i2c_transfer(state->i2c, &msg, 1)) != 1) {
			printk(KERN_WARNING "lgdt330x: %s error (addr %02x <- %02x, err = %i)\n", __FUNCTION__, msg.buf[0], msg.buf[1], err);
			if (err < 0)
				return err;
			else
				return -EREMOTEIO;
		}
		msg.buf += 2;
	}
	return 0;
}

/*
 * This routine writes the register (reg) to the demod bus
 * then reads the data returned for (len) bytes.
 */

static u8 i2c_read_demod_bytes (struct lgdt330x_state* state,
			       enum I2C_REG reg, u8* buf, int len)
{
	u8 wr [] = { reg };
	struct i2c_msg msg [] = {
		{ .addr = state->config->demod_address,
		  .flags = 0, .buf = wr,  .len = 1 },
		{ .addr = state->config->demod_address,
		  .flags = I2C_M_RD, .buf = buf, .len = len },
	};
	int ret;
	ret = i2c_transfer(state->i2c, msg, 2);
	if (ret != 2) {
		printk(KERN_WARNING "lgdt330x: %s: addr 0x%02x select 0x%02x error (ret == %i)\n", __FUNCTION__, state->config->demod_address, reg, ret);
	} else {
		ret = 0;
	}
	return ret;
}

/* Software reset */
static int lgdt3302_SwReset(struct lgdt330x_state* state)
{
	u8 ret;
	u8 reset[] = {
		IRQ_MASK,
		0x00 /* bit 6 is active low software reset
		      *	bits 5-0 are 1 to mask interrupts */
	};

	ret = i2c_write_demod_bytes(state,
				    reset, sizeof(reset));
	if (ret == 0) {

		/* force reset high (inactive) and unmask interrupts */
		reset[1] = 0x7f;
		ret = i2c_write_demod_bytes(state,
					    reset, sizeof(reset));
	}
	return ret;
}

static int lgdt3303_SwReset(struct lgdt330x_state* state)
{
	u8 ret;
	u8 reset[] = {
		0x02,
		0x00 /* bit 0 is active low software reset */
	};

	ret = i2c_write_demod_bytes(state,
				    reset, sizeof(reset));
	if (ret == 0) {

		/* force reset high (inactive) */
		reset[1] = 0x01;
		ret = i2c_write_demod_bytes(state,
					    reset, sizeof(reset));
	}
	return ret;
}

static int lgdt330x_SwReset(struct lgdt330x_state* state)
{
	switch (state->config->demod_chip) {
	case LGDT3302:
		return lgdt3302_SwReset(state);
	case LGDT3303:
		return lgdt3303_SwReset(state);
	default:
		return -ENODEV;
	}
}

static int lgdt330x_init(struct dvb_frontend* fe)
{
	/* Hardware reset is done using gpio[0] of cx23880x chip.
	 * I'd like to do it here, but don't know how to find chip address.
	 * cx88-cards.c arranges for the reset bit to be inactive (high).
	 * Maybe there needs to be a callable function in cx88-core or
	 * the caller of this function needs to do it. */

	/*
	 * Array of byte pairs <address, value>
	 * to initialize each different chip
	 */
	static u8 lgdt3302_init_data[] = {
		/* Use 50MHz parameter values from spec sheet since xtal is 50 */
		/* Change the value of NCOCTFV[25:0] of carrier
		   recovery center frequency register */
		VSB_CARRIER_FREQ0, 0x00,
		VSB_CARRIER_FREQ1, 0x87,
		VSB_CARRIER_FREQ2, 0x8e,
		VSB_CARRIER_FREQ3, 0x01,
		/* Change the TPCLK pin polarity
		   data is valid on falling clock */
		DEMUX_CONTROL, 0xfb,
		/* Change the value of IFBW[11:0] of
		   AGC IF/RF loop filter bandwidth register */
		AGC_RF_BANDWIDTH0, 0x40,
		AGC_RF_BANDWIDTH1, 0x93,
		AGC_RF_BANDWIDTH2, 0x00,
		/* Change the value of bit 6, 'nINAGCBY' and
		   'NSSEL[1:0] of ACG function control register 2 */
		AGC_FUNC_CTRL2, 0xc6,
		/* Change the value of bit 6 'RFFIX'
		   of AGC function control register 3 */
		AGC_FUNC_CTRL3, 0x40,
		/* Set the value of 'INLVTHD' register 0x2a/0x2c
		   to 0x7fe */
		AGC_DELAY0, 0x07,
		AGC_DELAY2, 0xfe,
		/* Change the value of IAGCBW[15:8]
		   of inner AGC loop filter bandwith */
		AGC_LOOP_BANDWIDTH0, 0x08,
		AGC_LOOP_BANDWIDTH1, 0x9a
	};

	static u8 lgdt3303_init_data[] = {
		0x4c, 0x14
	};

	struct lgdt330x_state* state = fe->demodulator_priv;
	char  *chip_name;
	int    err;

	switch (state->config->demod_chip) {
	case LGDT3302:
		chip_name = "LGDT3302";
		err = i2c_write_demod_bytes(state, lgdt3302_init_data,
					    sizeof(lgdt3302_init_data));
		break;
	case LGDT3303:
		chip_name = "LGDT3303";
		err = i2c_write_demod_bytes(state, lgdt3303_init_data,
					    sizeof(lgdt3303_init_data));
		break;
	default:
		chip_name = "undefined";
		printk (KERN_WARNING "Only LGDT3302 and LGDT3303 are supported chips.\n");
		err = -ENODEV;
	}
	dprintk("%s entered as %s\n", __FUNCTION__, chip_name);
	if (err < 0)
		return err;
	return lgdt330x_SwReset(state);
}

static int lgdt330x_read_ber(struct dvb_frontend* fe, u32* ber)
{
	*ber = 0; /* Not supplied by the demod chips */
	return 0;
}

static int lgdt330x_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct lgdt330x_state* state = fe->demodulator_priv;
	int err;
	u8 buf[2];

	switch (state->config->demod_chip) {
	case LGDT3302:
		err = i2c_read_demod_bytes(state, LGDT3302_PACKET_ERR_COUNTER1,
					   buf, sizeof(buf));
		break;
	case LGDT3303:
		err = i2c_read_demod_bytes(state, LGDT3303_PACKET_ERR_COUNTER1,
					   buf, sizeof(buf));
		break;
	default:
		printk(KERN_WARNING
		       "Only LGDT3302 and LGDT3303 are supported chips.\n");
		err = -ENODEV;
	}

	*ucblocks = (buf[0] << 8) | buf[1];
	return 0;
}

static int lgdt330x_set_parameters(struct dvb_frontend* fe,
				   struct dvb_frontend_parameters *param)
{
	/*
	 * Array of byte pairs <address, value>
	 * to initialize 8VSB for lgdt3303 chip 50 MHz IF
	 */
	static u8 lgdt3303_8vsb_44_data[] = {
		0x04, 0x00,
		0x0d, 0x40,
        0x0e, 0x87,
        0x0f, 0x8e,
        0x10, 0x01,
        0x47, 0x8b };

	/*
	 * Array of byte pairs <address, value>
	 * to initialize QAM for lgdt3303 chip
	 */
	static u8 lgdt3303_qam_data[] = {
		0x04, 0x00,
		0x0d, 0x00,
		0x0e, 0x00,
		0x0f, 0x00,
		0x10, 0x00,
		0x51, 0x63,
		0x47, 0x66,
		0x48, 0x66,
		0x4d, 0x1a,
		0x49, 0x08,
		0x4a, 0x9b };

	struct lgdt330x_state* state = fe->demodulator_priv;

	static u8 top_ctrl_cfg[]   = { TOP_CONTROL, 0x03 };

	int err;
	/* Change only if we are actually changing the modulation */
	if (state->current_modulation != param->u.vsb.modulation) {
		switch(param->u.vsb.modulation) {
		case VSB_8:
			dprintk("%s: VSB_8 MODE\n", __FUNCTION__);

			/* Select VSB mode */
			top_ctrl_cfg[1] = 0x03;

			/* Select ANT connector if supported by card */
			if (state->config->pll_rf_set)
				state->config->pll_rf_set(fe, 1);

			if (state->config->demod_chip == LGDT3303) {
				err = i2c_write_demod_bytes(state, lgdt3303_8vsb_44_data,
							    sizeof(lgdt3303_8vsb_44_data));
			}
			break;

		case QAM_64:
			dprintk("%s: QAM_64 MODE\n", __FUNCTION__);

			/* Select QAM_64 mode */
			top_ctrl_cfg[1] = 0x00;

			/* Select CABLE connector if supported by card */
			if (state->config->pll_rf_set)
				state->config->pll_rf_set(fe, 0);

			if (state->config->demod_chip == LGDT3303) {
				err = i2c_write_demod_bytes(state, lgdt3303_qam_data,
											sizeof(lgdt3303_qam_data));
			}
			break;

		case QAM_256:
			dprintk("%s: QAM_256 MODE\n", __FUNCTION__);

			/* Select QAM_256 mode */
			top_ctrl_cfg[1] = 0x01;

			/* Select CABLE connector if supported by card */
			if (state->config->pll_rf_set)
				state->config->pll_rf_set(fe, 0);

			if (state->config->demod_chip == LGDT3303) {
				err = i2c_write_demod_bytes(state, lgdt3303_qam_data,
											sizeof(lgdt3303_qam_data));
			}
			break;
		default:
			printk(KERN_WARNING "lgdt330x: %s: Modulation type(%d) UNSUPPORTED\n", __FUNCTION__, param->u.vsb.modulation);
			return -1;
		}
		/*
		 * select serial or parallel MPEG harware interface
		 * Serial:   0x04 for LGDT3302 or 0x40 for LGDT3303
		 * Parallel: 0x00
		 */
		top_ctrl_cfg[1] |= state->config->serial_mpeg;

		/* Select the requested mode */
		i2c_write_demod_bytes(state, top_ctrl_cfg,
				      sizeof(top_ctrl_cfg));
		if (state->config->set_ts_params)
			state->config->set_ts_params(fe, 0);
		state->current_modulation = param->u.vsb.modulation;
	}

	/* Tune to the specified frequency */
	if (state->config->pll_set)
		state->config->pll_set(fe, param);

	/* Keep track of the new frequency */
	state->current_frequency = param->frequency;

	lgdt330x_SwReset(state);
	return 0;
}

static int lgdt330x_get_frontend(struct dvb_frontend* fe,
				 struct dvb_frontend_parameters* param)
{
	struct lgdt330x_state *state = fe->demodulator_priv;
	param->frequency = state->current_frequency;
	return 0;
}

static int lgdt3302_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct lgdt330x_state* state = fe->demodulator_priv;
	u8 buf[3];

	*status = 0; /* Reset status result */

	/* AGC status register */
	i2c_read_demod_bytes(state, AGC_STATUS, buf, 1);
	dprintk("%s: AGC_STATUS = 0x%02x\n", __FUNCTION__, buf[0]);
	if ((buf[0] & 0x0c) == 0x8){
		/* Test signal does not exist flag */
		/* as well as the AGC lock flag.   */
		*status |= FE_HAS_SIGNAL;
	} else {
		/* Without a signal all other status bits are meaningless */
		return 0;
	}

	/*
	 * You must set the Mask bits to 1 in the IRQ_MASK in order
	 * to see that status bit in the IRQ_STATUS register.
	 * This is done in SwReset();
	 */
	/* signal status */
	i2c_read_demod_bytes(state, TOP_CONTROL, buf, sizeof(buf));
	dprintk("%s: TOP_CONTROL = 0x%02x, IRO_MASK = 0x%02x, IRQ_STATUS = 0x%02x\n", __FUNCTION__, buf[0], buf[1], buf[2]);


	/* sync status */
	if ((buf[2] & 0x03) == 0x01) {
		*status |= FE_HAS_SYNC;
	}

	/* FEC error status */
	if ((buf[2] & 0x0c) == 0x08) {
		*status |= FE_HAS_LOCK;
		*status |= FE_HAS_VITERBI;
	}

	/* Carrier Recovery Lock Status Register */
	i2c_read_demod_bytes(state, CARRIER_LOCK, buf, 1);
	dprintk("%s: CARRIER_LOCK = 0x%02x\n", __FUNCTION__, buf[0]);
	switch (state->current_modulation) {
	case QAM_256:
	case QAM_64:
		/* Need to undestand why there are 3 lock levels here */
		if ((buf[0] & 0x07) == 0x07)
			*status |= FE_HAS_CARRIER;
		break;
	case VSB_8:
		if ((buf[0] & 0x80) == 0x80)
			*status |= FE_HAS_CARRIER;
		break;
	default:
		printk("KERN_WARNING lgdt330x: %s: Modulation set to unsupported value\n", __FUNCTION__);
	}

	return 0;
}

static int lgdt3303_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct lgdt330x_state* state = fe->demodulator_priv;
	int err;
	u8 buf[3];

	*status = 0; /* Reset status result */

	/* lgdt3303 AGC status register */
	err = i2c_read_demod_bytes(state, 0x58, buf, 1);
	if (err < 0)
		return err;

	dprintk("%s: AGC_STATUS = 0x%02x\n", __FUNCTION__, buf[0]);
	if ((buf[0] & 0x21) == 0x01){
		/* Test input signal does not exist flag */
		/* as well as the AGC lock flag.   */
		*status |= FE_HAS_SIGNAL;
	} else {
		/* Without a signal all other status bits are meaningless */
		return 0;
	}

	/* Carrier Recovery Lock Status Register */
	i2c_read_demod_bytes(state, CARRIER_LOCK, buf, 1);
	dprintk("%s: CARRIER_LOCK = 0x%02x\n", __FUNCTION__, buf[0]);
	switch (state->current_modulation) {
	case QAM_256:
	case QAM_64:
		/* Need to undestand why there are 3 lock levels here */
		if ((buf[0] & 0x07) == 0x07)
			*status |= FE_HAS_CARRIER;
		else
			break;
		i2c_read_demod_bytes(state, 0x8a, buf, 1);
		if ((buf[0] & 0x04) == 0x04)
			*status |= FE_HAS_SYNC;
		if ((buf[0] & 0x01) == 0x01)
			*status |= FE_HAS_LOCK;
		if ((buf[0] & 0x08) == 0x08)
			*status |= FE_HAS_VITERBI;
		break;
	case VSB_8:
		if ((buf[0] & 0x80) == 0x80)
			*status |= FE_HAS_CARRIER;
		else
			break;
		i2c_read_demod_bytes(state, 0x38, buf, 1);
		if ((buf[0] & 0x02) == 0x00)
			*status |= FE_HAS_SYNC;
		if ((buf[0] & 0x01) == 0x01) {
			*status |= FE_HAS_LOCK;
			*status |= FE_HAS_VITERBI;
		}
		break;
	default:
		printk("KERN_WARNING lgdt330x: %s: Modulation set to unsupported value\n", __FUNCTION__);
	}
	return 0;
}

static int lgdt330x_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	/* not directly available. */
	*strength = 0;
	return 0;
}

static int lgdt3302_read_snr(struct dvb_frontend* fe, u16* snr)
{
#ifdef SNR_IN_DB
	/*
	 * Spec sheet shows formula for SNR_EQ = 10 log10(25 * 24**2 / noise)
	 * and SNR_PH = 10 log10(25 * 32**2 / noise) for equalizer and phase tracker
	 * respectively. The following tables are built on these formulas.
	 * The usual definition is SNR = 20 log10(signal/noise)
	 * If the specification is wrong the value retuned is 1/2 the actual SNR in db.
	 *
	 * This table is a an ordered list of noise values computed by the
	 * formula from the spec sheet such that the index into the table
	 * starting at 43 or 45 is the SNR value in db. There are duplicate noise
	 * value entries at the beginning because the SNR varies more than
	 * 1 db for a change of 1 digit in noise at very small values of noise.
	 *
	 * Examples from SNR_EQ table:
	 * noise SNR
	 *   0    43
	 *   1    42
	 *   2    39
	 *   3    37
	 *   4    36
	 *   5    35
	 *   6    34
	 *   7    33
	 *   8    33
	 *   9    32
	 *   10   32
	 *   11   31
	 *   12   31
	 *   13   30
	 */

	static const u32 SNR_EQ[] =
		{ 1,     2,      2,      2, 3,      3,      4,     4,     5,     7,
		  9,     11,     13,     17, 21,     26,     33,    41,    52,    65,
		  81,    102,    129,    162, 204,    257,    323,   406,   511,   644,
		  810,   1020,   1284,   1616, 2035,   2561,   3224,  4059,  5110,  6433,
		  8098,  10195,  12835,  16158, 20341,  25608,  32238, 40585, 51094, 64323,
		  80978, 101945, 128341, 161571, 203406, 256073, 0x40000
		};

	static const u32 SNR_PH[] =
		{ 1,     2,      2,      2,      3,      3,     4,     5,     6,     8,
		  10,    12,     15,     19,     23,     29, 37,    46,    58,    73,
		  91,    115,    144,    182,    229,    288, 362,   456,   574,   722,
		  909,   1144,   1440,   1813,   2282,   2873, 3617,  4553,  5732,  7216,
		  9084,  11436,  14396,  18124,  22817,  28724,  36161, 45524, 57312, 72151,
		  90833, 114351, 143960, 181235, 228161, 0x080000
		};

	static u8 buf[5];/* read data buffer */
	static u32 noise;   /* noise value */
	static u32 snr_db;  /* index into SNR_EQ[] */
	struct lgdt330x_state* state = (struct lgdt330x_state*) fe->demodulator_priv;

	/* read both equalizer and phase tracker noise data */
	i2c_read_demod_bytes(state, EQPH_ERR0, buf, sizeof(buf));

	if (state->current_modulation == VSB_8) {
		/* Equalizer Mean-Square Error Register for VSB */
		noise = ((buf[0] & 7) << 16) | (buf[1] << 8) | buf[2];

		/*
		 * Look up noise value in table.
		 * A better search algorithm could be used...
		 * watch out there are duplicate entries.
		 */
		for (snr_db = 0; snr_db < sizeof(SNR_EQ); snr_db++) {
			if (noise < SNR_EQ[snr_db]) {
				*snr = 43 - snr_db;
				break;
			}
		}
	} else {
		/* Phase Tracker Mean-Square Error Register for QAM */
		noise = ((buf[0] & 7<<3) << 13) | (buf[3] << 8) | buf[4];

		/* Look up noise value in table. */
		for (snr_db = 0; snr_db < sizeof(SNR_PH); snr_db++) {
			if (noise < SNR_PH[snr_db]) {
				*snr = 45 - snr_db;
				break;
			}
		}
	}
#else
	/* Return the raw noise value */
	static u8 buf[5];/* read data buffer */
	static u32 noise;   /* noise value */
	struct lgdt330x_state* state = (struct lgdt330x_state*) fe->demodulator_priv;

	/* read both equalizer and pase tracker noise data */
	i2c_read_demod_bytes(state, EQPH_ERR0, buf, sizeof(buf));

	if (state->current_modulation == VSB_8) {
		/* Phase Tracker Mean-Square Error Register for VSB */
		noise = ((buf[0] & 7<<3) << 13) | (buf[3] << 8) | buf[4];
	} else {

		/* Carrier Recovery Mean-Square Error for QAM */
		i2c_read_demod_bytes(state, 0x1a, buf, 2);
		noise = ((buf[0] & 3) << 8) | buf[1];
	}

	/* Small values for noise mean signal is better so invert noise */
	*snr = ~noise;
#endif

	dprintk("%s: noise = 0x%05x, snr = %idb\n",__FUNCTION__, noise, *snr);

	return 0;
}

static int lgdt3303_read_snr(struct dvb_frontend* fe, u16* snr)
{
	/* Return the raw noise value */
	static u8 buf[5];/* read data buffer */
	static u32 noise;   /* noise value */
	struct lgdt330x_state* state = (struct lgdt330x_state*) fe->demodulator_priv;

	if (state->current_modulation == VSB_8) {

		/* Phase Tracker Mean-Square Error Register for VSB */
		noise = ((buf[0] & 7) << 16) | (buf[3] << 8) | buf[4];
	} else {

		/* Carrier Recovery Mean-Square Error for QAM */
		i2c_read_demod_bytes(state, 0x1a, buf, 2);
		noise = (buf[0] << 8) | buf[1];
	}

	/* Small values for noise mean signal is better so invert noise */
	*snr = ~noise;

	dprintk("%s: noise = 0x%05x, snr = %idb\n",__FUNCTION__, noise, *snr);

	return 0;
}

static int lgdt330x_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings* fe_tune_settings)
{
	/* I have no idea about this - it may not be needed */
	fe_tune_settings->min_delay_ms = 500;
	fe_tune_settings->step_size = 0;
	fe_tune_settings->max_drift = 0;
	return 0;
}

static void lgdt330x_release(struct dvb_frontend* fe)
{
	struct lgdt330x_state* state = (struct lgdt330x_state*) fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops lgdt3302_ops;
static struct dvb_frontend_ops lgdt3303_ops;

struct dvb_frontend* lgdt330x_attach(const struct lgdt330x_config* config,
				     struct i2c_adapter* i2c)
{
	struct lgdt330x_state* state = NULL;
	u8 buf[1];

	/* Allocate memory for the internal state */
	state = (struct lgdt330x_state*) kmalloc(sizeof(struct lgdt330x_state), GFP_KERNEL);
	if (state == NULL)
		goto error;
	memset(state,0,sizeof(*state));

	/* Setup the state */
	state->config = config;
	state->i2c = i2c;
	switch (config->demod_chip) {
	case LGDT3302:
		memcpy(&state->ops, &lgdt3302_ops, sizeof(struct dvb_frontend_ops));
		break;
	case LGDT3303:
		memcpy(&state->ops, &lgdt3303_ops, sizeof(struct dvb_frontend_ops));
		break;
	default:
		goto error;
	}

	/* Verify communication with demod chip */
	if (i2c_read_demod_bytes(state, 2, buf, 1))
		goto error;

	state->current_frequency = -1;
	state->current_modulation = -1;

	/* Create dvb_frontend */
	state->frontend.ops = &state->ops;
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	dprintk("%s: ERROR\n",__FUNCTION__);
	return NULL;
}

static struct dvb_frontend_ops lgdt3302_ops = {
	.info = {
		.name= "LG Electronics LGDT3302 VSB/QAM Frontend",
		.type = FE_ATSC,
		.frequency_min= 54000000,
		.frequency_max= 858000000,
		.frequency_stepsize= 62500,
		/* Symbol rate is for all VSB modes need to check QAM */
		.symbol_rate_min    = 10762000,
		.symbol_rate_max    = 10762000,
		.caps = FE_CAN_QAM_64 | FE_CAN_QAM_256 | FE_CAN_8VSB
	},
	.init                 = lgdt330x_init,
	.set_frontend         = lgdt330x_set_parameters,
	.get_frontend         = lgdt330x_get_frontend,
	.get_tune_settings    = lgdt330x_get_tune_settings,
	.read_status          = lgdt3302_read_status,
	.read_ber             = lgdt330x_read_ber,
	.read_signal_strength = lgdt330x_read_signal_strength,
	.read_snr             = lgdt3302_read_snr,
	.read_ucblocks        = lgdt330x_read_ucblocks,
	.release              = lgdt330x_release,
};

static struct dvb_frontend_ops lgdt3303_ops = {
	.info = {
		.name= "LG Electronics LGDT3303 VSB/QAM Frontend",
		.type = FE_ATSC,
		.frequency_min= 54000000,
		.frequency_max= 858000000,
		.frequency_stepsize= 62500,
		/* Symbol rate is for all VSB modes need to check QAM */
		.symbol_rate_min    = 10762000,
		.symbol_rate_max    = 10762000,
		.caps = FE_CAN_QAM_64 | FE_CAN_QAM_256 | FE_CAN_8VSB
	},
	.init                 = lgdt330x_init,
	.set_frontend         = lgdt330x_set_parameters,
	.get_frontend         = lgdt330x_get_frontend,
	.get_tune_settings    = lgdt330x_get_tune_settings,
	.read_status          = lgdt3303_read_status,
	.read_ber             = lgdt330x_read_ber,
	.read_signal_strength = lgdt330x_read_signal_strength,
	.read_snr             = lgdt3303_read_snr,
	.read_ucblocks        = lgdt330x_read_ucblocks,
	.release              = lgdt330x_release,
};

MODULE_DESCRIPTION("LGDT330X (ATSC 8VSB & ITU-T J.83 AnnexB 64/256 QAM) Demodulator Driver");
MODULE_AUTHOR("Wilson Michaels");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(lgdt330x_attach);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

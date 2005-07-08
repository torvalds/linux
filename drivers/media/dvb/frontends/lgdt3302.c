/*
 * $Id: lgdt3302.c,v 1.5 2005/07/07 03:47:15 mkrufky Exp $
 *
 *    Support for LGDT3302 (DViCO FustionHDTV 3 Gold) - VSB/QAM
 *
 *    Copyright (C) 2005 Wilson Michaels <wilsonmichaels@earthlink.net>
 *
 *    Based on code from  Kirk Lapray <kirk_lapray@bigfoot.com>
 *                           Copyright (C) 2005
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
 * This driver supports DViCO FusionHDTV 3 Gold under Linux.
 *
 * TODO:
 * BER and signal strength always return 0.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/byteorder.h>

#include "dvb_frontend.h"
#include "dvb-pll.h"
#include "lgdt3302_priv.h"
#include "lgdt3302.h"

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug,"Turn on/off lgdt3302 frontend debugging (default:off).");
#define dprintk(args...) \
do { \
if (debug) printk(KERN_DEBUG "lgdt3302: " args); \
} while (0)

struct lgdt3302_state
{
	struct i2c_adapter* i2c;
	struct dvb_frontend_ops ops;

	/* Configuration settings */
	const struct lgdt3302_config* config;

	struct dvb_frontend frontend;

	/* Demodulator private data */
	fe_modulation_t current_modulation;

	/* Tuner private data */
	u32 current_frequency;
};

static int i2c_writebytes (struct lgdt3302_state* state,
			   u8 addr, /* demod_address or pll_address */
			   u8 *buf, /* data bytes to send */
			   int len  /* number of bytes to send */ )
{
	if (addr == state->config->pll_address) {
		struct i2c_msg msg =
			{ .addr = addr, .flags = 0, .buf = buf,  .len = len };
		int err;

		if ((err = i2c_transfer(state->i2c, &msg, 1)) != 1) {
			printk(KERN_WARNING "lgdt3302: %s error (addr %02x <- %02x, err == %i)\n", __FUNCTION__, addr, buf[0], err);
			return -EREMOTEIO;
		}
	} else {
		u8 tmp[] = { buf[0], buf[1] };
		struct i2c_msg msg =
			{ .addr = addr, .flags = 0, .buf = tmp,  .len = 2 };
		int err;
		int i;

		for (i=1; i<len; i++) {
			tmp[1] = buf[i];
			if ((err = i2c_transfer(state->i2c, &msg, 1)) != 1) {
				printk(KERN_WARNING "lgdt3302: %s error (addr %02x <- %02x, err == %i)\n", __FUNCTION__, addr, buf[0], err);
				return -EREMOTEIO;
			}
			tmp[0]++;
		}
	}
	return 0;
}
static int i2c_readbytes (struct lgdt3302_state* state,
			  u8 addr, /* demod_address or pll_address */
			  u8 *buf, /* holds data bytes read */
			  int len  /* number of bytes to read */ )
{
	struct i2c_msg msg =
		{ .addr = addr, .flags = I2C_M_RD, .buf = buf,  .len = len };
	int err;

	if ((err = i2c_transfer(state->i2c, &msg, 1)) != 1) {
		printk(KERN_WARNING "lgdt3302: %s error (addr %02x, err == %i)\n", __FUNCTION__, addr, err);
		return -EREMOTEIO;
	}
	return 0;
}

/*
 * This routine writes the register (reg) to the demod bus
 * then reads the data returned for (len) bytes.
 */

static u8 i2c_selectreadbytes (struct lgdt3302_state* state,
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
		printk(KERN_WARNING "lgdt3302: %s: addr 0x%02x select 0x%02x error (ret == %i)\n", __FUNCTION__, state->config->demod_address, reg, ret);
	} else {
		ret = 0;
	}
	return ret;
}

/* Software reset */
int lgdt3302_SwReset(struct lgdt3302_state* state)
{
	u8 ret;
	u8 reset[] = {
		IRQ_MASK,
		0x00 /* bit 6 is active low software reset
		      *	bits 5-0 are 1 to mask interrupts */
	};

	ret = i2c_writebytes(state,
			     state->config->demod_address,
			     reset, sizeof(reset));
	if (ret == 0) {
		/* spec says reset takes 100 ns why wait */
		/* mdelay(100);    */ /* keep low for 100mS */
		reset[1] = 0x7f;      /* force reset high (inactive)
				       * and unmask interrupts */
		ret = i2c_writebytes(state,
				     state->config->demod_address,
				     reset, sizeof(reset));
	}
	/* Spec does not indicate a need for this either */
	/*mdelay(5); */               /* wait 5 msec before doing more */
	return ret;
}

static int lgdt3302_init(struct dvb_frontend* fe)
{
	/* Hardware reset is done using gpio[0] of cx23880x chip.
	 * I'd like to do it here, but don't know how to find chip address.
	 * cx88-cards.c arranges for the reset bit to be inactive (high).
	 * Maybe there needs to be a callable function in cx88-core or
	 * the caller of this function needs to do it. */

	dprintk("%s entered\n", __FUNCTION__);
	return lgdt3302_SwReset((struct lgdt3302_state*) fe->demodulator_priv);
}

static int lgdt3302_read_ber(struct dvb_frontend* fe, u32* ber)
{
	*ber = 0; /* Dummy out for now */
	return 0;
}

static int lgdt3302_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct lgdt3302_state* state = (struct lgdt3302_state*) fe->demodulator_priv;
	u8 buf[2];

	i2c_selectreadbytes(state, PACKET_ERR_COUNTER1, buf, sizeof(buf));

	*ucblocks = (buf[0] << 8) | buf[1];
	return 0;
}

static int lgdt3302_set_parameters(struct dvb_frontend* fe,
				   struct dvb_frontend_parameters *param)
{
	u8 buf[4];
	struct lgdt3302_state* state =
		(struct lgdt3302_state*) fe->demodulator_priv;

	/* Use 50MHz parameter values from spec sheet since xtal is 50 */
	static u8 top_ctrl_cfg[]   = { TOP_CONTROL, 0x03 };
	static u8 vsb_freq_cfg[]   = { VSB_CARRIER_FREQ0, 0x00, 0x87, 0x8e, 0x01 };
	static u8 demux_ctrl_cfg[] = { DEMUX_CONTROL, 0xfb };
	static u8 agc_rf_cfg[]     = { AGC_RF_BANDWIDTH0, 0x40, 0x93, 0x00 };
	static u8 agc_ctrl_cfg[]   = { AGC_FUNC_CTRL2, 0xc6, 0x40 };
	static u8 agc_delay_cfg[]  = { AGC_DELAY0, 0x00, 0x00, 0x00 };
	static u8 agc_loop_cfg[]   = { AGC_LOOP_BANDWIDTH0, 0x08, 0x9a };

	/* Change only if we are actually changing the modulation */
	if (state->current_modulation != param->u.vsb.modulation) {
		switch(param->u.vsb.modulation) {
		case VSB_8:
			dprintk("%s: VSB_8 MODE\n", __FUNCTION__);

			/* Select VSB mode and serial MPEG interface */
			top_ctrl_cfg[1] = 0x07;
			break;

		case QAM_64:
			dprintk("%s: QAM_64 MODE\n", __FUNCTION__);

			/* Select QAM_64 mode and serial MPEG interface */
			top_ctrl_cfg[1] = 0x04;
			break;

		case QAM_256:
			dprintk("%s: QAM_256 MODE\n", __FUNCTION__);

			/* Select QAM_256 mode and serial MPEG interface */
			top_ctrl_cfg[1] = 0x05;
			break;
		default:
			printk(KERN_WARNING "lgdt3302: %s: Modulation type(%d) UNSUPPORTED\n", __FUNCTION__, param->u.vsb.modulation);
			return -1;
		}
		/* Initializations common to all modes */

		/* Select the requested mode */
		i2c_writebytes(state, state->config->demod_address,
			       top_ctrl_cfg, sizeof(top_ctrl_cfg));

		/* Change the value of IFBW[11:0]
		   of AGC IF/RF loop filter bandwidth register */
		i2c_writebytes(state, state->config->demod_address,
			       agc_rf_cfg, sizeof(agc_rf_cfg));

		/* Change the value of bit 6, 'nINAGCBY' and
		   'NSSEL[1:0] of ACG function control register 2 */
		/* Change the value of bit 6 'RFFIX'
		   of AGC function control register 3 */
		i2c_writebytes(state, state->config->demod_address,
			       agc_ctrl_cfg, sizeof(agc_ctrl_cfg));

		/* Change the TPCLK pin polarity
		   data is valid on falling clock */
		i2c_writebytes(state, state->config->demod_address,
			       demux_ctrl_cfg, sizeof(demux_ctrl_cfg));

		if (param->u.vsb.modulation == VSB_8) {
			/* Initialization for VSB modes only */
			/* Change the value of NCOCTFV[25:0]of carrier
			   recovery center frequency register for VSB */
			i2c_writebytes(state, state->config->demod_address,
				       vsb_freq_cfg, sizeof(vsb_freq_cfg));
		} else {
			/* Initialization for QAM modes only */
			/* Set the value of 'INLVTHD' register 0x2a/0x2c
			   to value from 'IFACC' register 0x39/0x3b -1 */
			int value;
			i2c_selectreadbytes(state, AGC_RFIF_ACC0,
					    &agc_delay_cfg[1], 3);
			value = ((agc_delay_cfg[1] & 0x0f) << 8) | agc_delay_cfg[3];
			value = value -1;
			dprintk("%s IFACC -1 = 0x%03x\n", __FUNCTION__, value);
			agc_delay_cfg[1] = (value >> 8) & 0x0f;
			agc_delay_cfg[2] = 0x00;
			agc_delay_cfg[3] = value & 0xff;
			i2c_writebytes(state, state->config->demod_address,
				       agc_delay_cfg, sizeof(agc_delay_cfg));

			/* Change the value of IAGCBW[15:8]
			   of inner AGC loop filter bandwith */
			i2c_writebytes(state, state->config->demod_address,
				       agc_loop_cfg, sizeof(agc_loop_cfg));
		}

		state->config->set_ts_params(fe, 0);
		lgdt3302_SwReset(state);
		state->current_modulation = param->u.vsb.modulation;
	}

	/* Change only if we are actually changing the channel */
	if (state->current_frequency != param->frequency) {
		dvb_pll_configure(state->config->pll_desc, buf,
				  param->frequency, 0);
		dprintk("%s: tuner bytes: 0x%02x 0x%02x "
			"0x%02x 0x%02x\n", __FUNCTION__, buf[0],buf[1],buf[2],buf[3]);
		i2c_writebytes(state, state->config->pll_address ,buf, 4);

		/* Check the status of the tuner pll */
		i2c_readbytes(state, state->config->pll_address, buf, 1);
		dprintk("%s: tuner status byte = 0x%02x\n", __FUNCTION__, buf[0]);

		lgdt3302_SwReset(state);

		/* Update current frequency */
		state->current_frequency = param->frequency;
	}
	return 0;
}

static int lgdt3302_get_frontend(struct dvb_frontend* fe,
				 struct dvb_frontend_parameters* param)
{
	struct lgdt3302_state *state = fe->demodulator_priv;
	param->frequency = state->current_frequency;
	return 0;
}

static int lgdt3302_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct lgdt3302_state* state = (struct lgdt3302_state*) fe->demodulator_priv;
	u8 buf[3];

	*status = 0; /* Reset status result */

	/* Check the status of the tuner pll */
	i2c_readbytes(state, state->config->pll_address, buf, 1);
	dprintk("%s: tuner status byte = 0x%02x\n", __FUNCTION__, buf[0]);
	if ((buf[0] & 0xc0) != 0x40)
		return 0; /* Tuner PLL not locked or not powered on */

	/*
	 * You must set the Mask bits to 1 in the IRQ_MASK in order
	 * to see that status bit in the IRQ_STATUS register.
	 * This is done in SwReset();
	 */

	/* AGC status register */
	i2c_selectreadbytes(state, AGC_STATUS, buf, 1);
	dprintk("%s: AGC_STATUS = 0x%02x\n", __FUNCTION__, buf[0]);
	if ((buf[0] & 0x0c) == 0x8){
		/* Test signal does not exist flag */
		/* as well as the AGC lock flag.   */
		*status |= FE_HAS_SIGNAL;
	} else {
		/* Without a signal all other status bits are meaningless */
		return 0;
	}

	/* signal status */
	i2c_selectreadbytes(state, TOP_CONTROL, buf, sizeof(buf));
	dprintk("%s: TOP_CONTROL = 0x%02x, IRO_MASK = 0x%02x, IRQ_STATUS = 0x%02x\n", __FUNCTION__, buf[0], buf[1], buf[2]);

#if 0
	/* Alternative method to check for a signal */
	/* using the SNR good/bad interrupts.   */
	if ((buf[2] & 0x30) == 0x10)
		*status |= FE_HAS_SIGNAL;
#endif

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
	i2c_selectreadbytes(state, CARRIER_LOCK, buf, 1);
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
		printk("KERN_WARNING lgdt3302: %s: Modulation set to unsupported value\n", __FUNCTION__);
	}

	return 0;
}

static int lgdt3302_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	/* not directly available. */
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
		  90833, 114351, 143960, 181235, 228161, 0x040000
		};

	static u8 buf[5];/* read data buffer */
	static u32 noise;   /* noise value */
	static u32 snr_db;  /* index into SNR_EQ[] */
	struct lgdt3302_state* state = (struct lgdt3302_state*) fe->demodulator_priv;

	/* read both equalizer and pase tracker noise data */
	i2c_selectreadbytes(state, EQPH_ERR0, buf, sizeof(buf));

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
	struct lgdt3302_state* state = (struct lgdt3302_state*) fe->demodulator_priv;

	/* read both equalizer and pase tracker noise data */
	i2c_selectreadbytes(state, EQPH_ERR0, buf, sizeof(buf));

	if (state->current_modulation == VSB_8) {
		/* Equalizer Mean-Square Error Register for VSB */
		noise = ((buf[0] & 7) << 16) | (buf[1] << 8) | buf[2];
	} else {
		/* Phase Tracker Mean-Square Error Register for QAM */
		noise = ((buf[0] & 7<<3) << 13) | (buf[3] << 8) | buf[4];
	}

	/* Small values for noise mean signal is better so invert noise */
	/* Noise is 19 bit value so discard 3 LSB*/
	*snr = ~noise>>3;
#endif

	dprintk("%s: noise = 0x%05x, snr = %idb\n",__FUNCTION__, noise, *snr);

	return 0;
}

static int lgdt3302_get_tune_settings(struct dvb_frontend* fe, struct dvb_frontend_tune_settings* fe_tune_settings)
{
	/* I have no idea about this - it may not be needed */
	fe_tune_settings->min_delay_ms = 500;
	fe_tune_settings->step_size = 0;
	fe_tune_settings->max_drift = 0;
	return 0;
}

static void lgdt3302_release(struct dvb_frontend* fe)
{
	struct lgdt3302_state* state = (struct lgdt3302_state*) fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops lgdt3302_ops;

struct dvb_frontend* lgdt3302_attach(const struct lgdt3302_config* config,
				     struct i2c_adapter* i2c)
{
	struct lgdt3302_state* state = NULL;
	u8 buf[1];

	/* Allocate memory for the internal state */
	state = (struct lgdt3302_state*) kmalloc(sizeof(struct lgdt3302_state), GFP_KERNEL);
	if (state == NULL)
		goto error;
	memset(state,0,sizeof(*state));

	/* Setup the state */
	state->config = config;
	state->i2c = i2c;
	memcpy(&state->ops, &lgdt3302_ops, sizeof(struct dvb_frontend_ops));
	/* Verify communication with demod chip */
	if (i2c_selectreadbytes(state, 2, buf, 1))
		goto error;

	state->current_frequency = -1;
	state->current_modulation = -1;

	/* Create dvb_frontend */
	state->frontend.ops = &state->ops;
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	if (state)
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
	.init                 = lgdt3302_init,
	.set_frontend         = lgdt3302_set_parameters,
	.get_frontend         = lgdt3302_get_frontend,
	.get_tune_settings    = lgdt3302_get_tune_settings,
	.read_status          = lgdt3302_read_status,
	.read_ber             = lgdt3302_read_ber,
	.read_signal_strength = lgdt3302_read_signal_strength,
	.read_snr             = lgdt3302_read_snr,
	.read_ucblocks        = lgdt3302_read_ucblocks,
	.release              = lgdt3302_release,
};

MODULE_DESCRIPTION("LGDT3302 [DViCO FusionHDTV 3 Gold] (ATSC 8VSB & ITU-T J.83 AnnexB 64/256 QAM) Demodulator Driver");
MODULE_AUTHOR("Wilson Michaels");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(lgdt3302_attach);

/*
 * Local variables:
 * c-basic-offset: 8
 * compile-command: "make DVB=1"
 * End:
 */

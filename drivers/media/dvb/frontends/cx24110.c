	/*
    cx24110 - Single Chip Satellite Channel Receiver driver module

    Copyright (C) 2002 Peter Hettkamp <peter.hettkamp@htp-tel.de> based on
    work
    Copyright (C) 1999 Convergence Integrated Media GmbH <ralph@convergence.de>

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

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include "dvb_frontend.h"
#include "cx24110.h"


struct cx24110_state {

	struct i2c_adapter* i2c;

	const struct cx24110_config* config;

	struct dvb_frontend frontend;

	u32 lastber;
	u32 lastbler;
	u32 lastesn0;
};

static int debug;
#define dprintk(args...) \
	do { \
		if (debug) printk(KERN_DEBUG "cx24110: " args); \
	} while (0)

static struct {u8 reg; u8 data;} cx24110_regdata[]=
		      /* Comments beginning with @ denote this value should
			 be the default */
	{{0x09,0x01}, /* SoftResetAll */
	 {0x09,0x00}, /* release reset */
	 {0x01,0xe8}, /* MSB of code rate 27.5MS/s */
	 {0x02,0x17}, /* middle byte " */
	 {0x03,0x29}, /* LSB         " */
	 {0x05,0x03}, /* @ DVB mode, standard code rate 3/4 */
	 {0x06,0xa5}, /* @ PLL 60MHz */
	 {0x07,0x01}, /* @ Fclk, i.e. sampling clock, 60MHz */
	 {0x0a,0x00}, /* @ partial chip disables, do not set */
	 {0x0b,0x01}, /* set output clock in gapped mode, start signal low
			 active for first byte */
	 {0x0c,0x11}, /* no parity bytes, large hold time, serial data out */
	 {0x0d,0x6f}, /* @ RS Sync/Unsync thresholds */
	 {0x10,0x40}, /* chip doc is misleading here: write bit 6 as 1
			 to avoid starting the BER counter. Reset the
			 CRC test bit. Finite counting selected */
	 {0x15,0xff}, /* @ size of the limited time window for RS BER
			 estimation. It is <value>*256 RS blocks, this
			 gives approx. 2.6 sec at 27.5MS/s, rate 3/4 */
	 {0x16,0x00}, /* @ enable all RS output ports */
	 {0x17,0x04}, /* @ time window allowed for the RS to sync */
	 {0x18,0xae}, /* @ allow all standard DVB code rates to be scanned
			 for automatically */
		      /* leave the current code rate and normalization
			 registers as they are after reset... */
	 {0x21,0x10}, /* @ during AutoAcq, search each viterbi setting
			 only once */
	 {0x23,0x18}, /* @ size of the limited time window for Viterbi BER
			 estimation. It is <value>*65536 channel bits, i.e.
			 approx. 38ms at 27.5MS/s, rate 3/4 */
	 {0x24,0x24}, /* do not trigger Viterbi CRC test. Finite count window */
		      /* leave front-end AGC parameters at default values */
		      /* leave decimation AGC parameters at default values */
	 {0x35,0x40}, /* disable all interrupts. They are not connected anyway */
	 {0x36,0xff}, /* clear all interrupt pending flags */
	 {0x37,0x00}, /* @ fully enable AutoAcqq state machine */
	 {0x38,0x07}, /* @ enable fade recovery, but not autostart AutoAcq */
		      /* leave the equalizer parameters on their default values */
		      /* leave the final AGC parameters on their default values */
	 {0x41,0x00}, /* @ MSB of front-end derotator frequency */
	 {0x42,0x00}, /* @ middle bytes " */
	 {0x43,0x00}, /* @ LSB          " */
		      /* leave the carrier tracking loop parameters on default */
		      /* leave the bit timing loop parameters at gefault */
	 {0x56,0x4d}, /* set the filtune voltage to 2.7V, as recommended by */
		      /* the cx24108 data sheet for symbol rates above 15MS/s */
	 {0x57,0x00}, /* @ Filter sigma delta enabled, positive */
	 {0x61,0x95}, /* GPIO pins 1-4 have special function */
	 {0x62,0x05}, /* GPIO pin 5 has special function, pin 6 is GPIO */
	 {0x63,0x00}, /* All GPIO pins use CMOS output characteristics */
	 {0x64,0x20}, /* GPIO 6 is input, all others are outputs */
	 {0x6d,0x30}, /* tuner auto mode clock freq 62kHz */
	 {0x70,0x15}, /* use auto mode, tuner word is 21 bits long */
	 {0x73,0x00}, /* @ disable several demod bypasses */
	 {0x74,0x00}, /* @  " */
	 {0x75,0x00}  /* @  " */
		      /* the remaining registers are for SEC */
	};


static int cx24110_writereg (struct cx24110_state* state, int reg, int data)
{
	u8 buf [] = { reg, data };
	struct i2c_msg msg = { .addr = state->config->demod_address, .flags = 0, .buf = buf, .len = 2 };
	int err;

	if ((err = i2c_transfer(state->i2c, &msg, 1)) != 1) {
		dprintk ("%s: writereg error (err == %i, reg == 0x%02x,"
			 " data == 0x%02x)\n", __FUNCTION__, err, reg, data);
		return -EREMOTEIO;
	}

	return 0;
}

static int cx24110_readreg (struct cx24110_state* state, u8 reg)
{
	int ret;
	u8 b0 [] = { reg };
	u8 b1 [] = { 0 };
	struct i2c_msg msg [] = { { .addr = state->config->demod_address, .flags = 0, .buf = b0, .len = 1 },
			   { .addr = state->config->demod_address, .flags = I2C_M_RD, .buf = b1, .len = 1 } };

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2) return ret;

	return b1[0];
}

static int cx24110_set_inversion (struct cx24110_state* state, fe_spectral_inversion_t inversion)
{
/* fixme (low): error handling */

	switch (inversion) {
	case INVERSION_OFF:
		cx24110_writereg(state,0x37,cx24110_readreg(state,0x37)|0x1);
		/* AcqSpectrInvDis on. No idea why someone should want this */
		cx24110_writereg(state,0x5,cx24110_readreg(state,0x5)&0xf7);
		/* Initial value 0 at start of acq */
		cx24110_writereg(state,0x22,cx24110_readreg(state,0x22)&0xef);
		/* current value 0 */
		/* The cx24110 manual tells us this reg is read-only.
		   But what the heck... set it ayways */
		break;
	case INVERSION_ON:
		cx24110_writereg(state,0x37,cx24110_readreg(state,0x37)|0x1);
		/* AcqSpectrInvDis on. No idea why someone should want this */
		cx24110_writereg(state,0x5,cx24110_readreg(state,0x5)|0x08);
		/* Initial value 1 at start of acq */
		cx24110_writereg(state,0x22,cx24110_readreg(state,0x22)|0x10);
		/* current value 1 */
		break;
	case INVERSION_AUTO:
		cx24110_writereg(state,0x37,cx24110_readreg(state,0x37)&0xfe);
		/* AcqSpectrInvDis off. Leave initial & current states as is */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cx24110_set_fec (struct cx24110_state* state, fe_code_rate_t fec)
{
/* fixme (low): error handling */

	static const int rate[]={-1,1,2,3,5,7,-1};
	static const int g1[]={-1,0x01,0x02,0x05,0x15,0x45,-1};
	static const int g2[]={-1,0x01,0x03,0x06,0x1a,0x7a,-1};

	/* Well, the AutoAcq engine of the cx24106 and 24110 automatically
	   searches all enabled viterbi rates, and can handle non-standard
	   rates as well. */

	if (fec>FEC_AUTO)
		fec=FEC_AUTO;

	if (fec==FEC_AUTO) { /* (re-)establish AutoAcq behaviour */
		cx24110_writereg(state,0x37,cx24110_readreg(state,0x37)&0xdf);
		/* clear AcqVitDis bit */
		cx24110_writereg(state,0x18,0xae);
		/* allow all DVB standard code rates */
		cx24110_writereg(state,0x05,(cx24110_readreg(state,0x05)&0xf0)|0x3);
		/* set nominal Viterbi rate 3/4 */
		cx24110_writereg(state,0x22,(cx24110_readreg(state,0x22)&0xf0)|0x3);
		/* set current Viterbi rate 3/4 */
		cx24110_writereg(state,0x1a,0x05); cx24110_writereg(state,0x1b,0x06);
		/* set the puncture registers for code rate 3/4 */
		return 0;
	} else {
		cx24110_writereg(state,0x37,cx24110_readreg(state,0x37)|0x20);
		/* set AcqVitDis bit */
		if(rate[fec]>0) {
			cx24110_writereg(state,0x05,(cx24110_readreg(state,0x05)&0xf0)|rate[fec]);
			/* set nominal Viterbi rate */
			cx24110_writereg(state,0x22,(cx24110_readreg(state,0x22)&0xf0)|rate[fec]);
			/* set current Viterbi rate */
			cx24110_writereg(state,0x1a,g1[fec]);
			cx24110_writereg(state,0x1b,g2[fec]);
			/* not sure if this is the right way: I always used AutoAcq mode */
	   } else
		   return -EOPNOTSUPP;
/* fixme (low): which is the correct return code? */
	};
	return 0;
}

static fe_code_rate_t cx24110_get_fec (struct cx24110_state* state)
{
	int i;

	i=cx24110_readreg(state,0x22)&0x0f;
	if(!(i&0x08)) {
		return FEC_1_2 + i - 1;
	} else {
/* fixme (low): a special code rate has been selected. In theory, we need to
   return a denominator value, a numerator value, and a pair of puncture
   maps to correctly describe this mode. But this should never happen in
   practice, because it cannot be set by cx24110_get_fec. */
	   return FEC_NONE;
	}
}

static int cx24110_set_symbolrate (struct cx24110_state* state, u32 srate)
{
/* fixme (low): add error handling */
	u32 ratio;
	u32 tmp, fclk, BDRI;

	static const u32 bands[]={5000000UL,15000000UL,90999000UL/2};
	int i;

	dprintk("cx24110 debug: entering %s(%d)\n",__FUNCTION__,srate);
	if (srate>90999000UL/2)
		srate=90999000UL/2;
	if (srate<500000)
		srate=500000;

	for(i=0;(i<sizeof(bands)/sizeof(bands[0]))&&(srate>bands[i]);i++)
		;
	/* first, check which sample rate is appropriate: 45, 60 80 or 90 MHz,
	   and set the PLL accordingly (R07[1:0] Fclk, R06[7:4] PLLmult,
	   R06[3:0] PLLphaseDetGain */
	tmp=cx24110_readreg(state,0x07)&0xfc;
	if(srate<90999000UL/4) { /* sample rate 45MHz*/
		cx24110_writereg(state,0x07,tmp);
		cx24110_writereg(state,0x06,0x78);
		fclk=90999000UL/2;
	} else if(srate<60666000UL/2) { /* sample rate 60MHz */
		cx24110_writereg(state,0x07,tmp|0x1);
		cx24110_writereg(state,0x06,0xa5);
		fclk=60666000UL;
	} else if(srate<80888000UL/2) { /* sample rate 80MHz */
		cx24110_writereg(state,0x07,tmp|0x2);
		cx24110_writereg(state,0x06,0x87);
		fclk=80888000UL;
	} else { /* sample rate 90MHz */
		cx24110_writereg(state,0x07,tmp|0x3);
		cx24110_writereg(state,0x06,0x78);
		fclk=90999000UL;
	};
	dprintk("cx24110 debug: fclk %d Hz\n",fclk);
	/* we need to divide two integers with approx. 27 bits in 32 bit
	   arithmetic giving a 25 bit result */
	/* the maximum dividend is 90999000/2, 0x02b6446c, this number is
	   also the most complex divisor. Hence, the dividend has,
	   assuming 32bit unsigned arithmetic, 6 clear bits on top, the
	   divisor 2 unused bits at the bottom. Also, the quotient is
	   always less than 1/2. Borrowed from VES1893.c, of course */

	tmp=srate<<6;
	BDRI=fclk>>2;
	ratio=(tmp/BDRI);

	tmp=(tmp%BDRI)<<8;
	ratio=(ratio<<8)+(tmp/BDRI);

	tmp=(tmp%BDRI)<<8;
	ratio=(ratio<<8)+(tmp/BDRI);

	tmp=(tmp%BDRI)<<1;
	ratio=(ratio<<1)+(tmp/BDRI);

	dprintk("srate= %d (range %d, up to %d)\n", srate,i,bands[i]);
	dprintk("fclk = %d\n", fclk);
	dprintk("ratio= %08x\n", ratio);

	cx24110_writereg(state, 0x1, (ratio>>16)&0xff);
	cx24110_writereg(state, 0x2, (ratio>>8)&0xff);
	cx24110_writereg(state, 0x3, (ratio)&0xff);

	return 0;

}

static int _cx24110_pll_write (struct dvb_frontend* fe, u8 *buf, int len)
{
	struct cx24110_state *state = fe->demodulator_priv;

	if (len != 3)
		return -EINVAL;

/* tuner data is 21 bits long, must be left-aligned in data */
/* tuner cx24108 is written through a dedicated 3wire interface on the demod chip */
/* FIXME (low): add error handling, avoid infinite loops if HW fails... */

	cx24110_writereg(state,0x6d,0x30); /* auto mode at 62kHz */
	cx24110_writereg(state,0x70,0x15); /* auto mode 21 bits */

	/* if the auto tuner writer is still busy, clear it out */
	while (cx24110_readreg(state,0x6d)&0x80)
		cx24110_writereg(state,0x72,0);

	/* write the topmost 8 bits */
	cx24110_writereg(state,0x72,buf[0]);

	/* wait for the send to be completed */
	while ((cx24110_readreg(state,0x6d)&0xc0)==0x80)
		;

	/* send another 8 bytes */
	cx24110_writereg(state,0x72,buf[1]);
	while ((cx24110_readreg(state,0x6d)&0xc0)==0x80)
		;

	/* and the topmost 5 bits of this byte */
	cx24110_writereg(state,0x72,buf[2]);
	while ((cx24110_readreg(state,0x6d)&0xc0)==0x80)
		;

	/* now strobe the enable line once */
	cx24110_writereg(state,0x6d,0x32);
	cx24110_writereg(state,0x6d,0x30);

	return 0;
}

static int cx24110_initfe(struct dvb_frontend* fe)
{
	struct cx24110_state *state = fe->demodulator_priv;
/* fixme (low): error handling */
	int i;

	dprintk("%s: init chip\n", __FUNCTION__);

	for(i=0;i<sizeof(cx24110_regdata)/sizeof(cx24110_regdata[0]);i++) {
		cx24110_writereg(state, cx24110_regdata[i].reg, cx24110_regdata[i].data);
	};

	return 0;
}

static int cx24110_set_voltage (struct dvb_frontend* fe, fe_sec_voltage_t voltage)
{
	struct cx24110_state *state = fe->demodulator_priv;

	switch (voltage) {
	case SEC_VOLTAGE_13:
		return cx24110_writereg(state,0x76,(cx24110_readreg(state,0x76)&0x3b)|0xc0);
	case SEC_VOLTAGE_18:
		return cx24110_writereg(state,0x76,(cx24110_readreg(state,0x76)&0x3b)|0x40);
	default:
		return -EINVAL;
	};
}

static int cx24110_diseqc_send_burst(struct dvb_frontend* fe, fe_sec_mini_cmd_t burst)
{
	int rv, bit;
	struct cx24110_state *state = fe->demodulator_priv;
	unsigned long timeout;

	if (burst == SEC_MINI_A)
		bit = 0x00;
	else if (burst == SEC_MINI_B)
		bit = 0x08;
	else
		return -EINVAL;

	rv = cx24110_readreg(state, 0x77);
	if (!(rv & 0x04))
		cx24110_writereg(state, 0x77, rv | 0x04);

	rv = cx24110_readreg(state, 0x76);
	cx24110_writereg(state, 0x76, ((rv & 0x90) | 0x40 | bit));
	timeout = jiffies + msecs_to_jiffies(100);
	while (!time_after(jiffies, timeout) && !(cx24110_readreg(state, 0x76) & 0x40))
		; /* wait for LNB ready */

	return 0;
}

static int cx24110_send_diseqc_msg(struct dvb_frontend* fe,
				   struct dvb_diseqc_master_cmd *cmd)
{
	int i, rv;
	struct cx24110_state *state = fe->demodulator_priv;
	unsigned long timeout;

	if (cmd->msg_len < 3 || cmd->msg_len > 6)
		return -EINVAL;  /* not implemented */

	for (i = 0; i < cmd->msg_len; i++)
		cx24110_writereg(state, 0x79 + i, cmd->msg[i]);

	rv = cx24110_readreg(state, 0x77);
	if (rv & 0x04) {
		cx24110_writereg(state, 0x77, rv & ~0x04);
		msleep(30); /* reportedly fixes switching problems */
	}

	rv = cx24110_readreg(state, 0x76);

	cx24110_writereg(state, 0x76, ((rv & 0x90) | 0x40) | ((cmd->msg_len-3) & 3));
	timeout = jiffies + msecs_to_jiffies(100);
	while (!time_after(jiffies, timeout) && !(cx24110_readreg(state, 0x76) & 0x40))
		; /* wait for LNB ready */

	return 0;
}

static int cx24110_read_status(struct dvb_frontend* fe, fe_status_t* status)
{
	struct cx24110_state *state = fe->demodulator_priv;

	int sync = cx24110_readreg (state, 0x55);

	*status = 0;

	if (sync & 0x10)
		*status |= FE_HAS_SIGNAL;

	if (sync & 0x08)
		*status |= FE_HAS_CARRIER;

	sync = cx24110_readreg (state, 0x08);

	if (sync & 0x40)
		*status |= FE_HAS_VITERBI;

	if (sync & 0x20)
		*status |= FE_HAS_SYNC;

	if ((sync & 0x60) == 0x60)
		*status |= FE_HAS_LOCK;

	return 0;
}

static int cx24110_read_ber(struct dvb_frontend* fe, u32* ber)
{
	struct cx24110_state *state = fe->demodulator_priv;

	/* fixme (maybe): value range is 16 bit. Scale? */
	if(cx24110_readreg(state,0x24)&0x10) {
		/* the Viterbi error counter has finished one counting window */
		cx24110_writereg(state,0x24,0x04); /* select the ber reg */
		state->lastber=cx24110_readreg(state,0x25)|
			(cx24110_readreg(state,0x26)<<8);
		cx24110_writereg(state,0x24,0x04); /* start new count window */
		cx24110_writereg(state,0x24,0x14);
	}
	*ber = state->lastber;

	return 0;
}

static int cx24110_read_signal_strength(struct dvb_frontend* fe, u16* signal_strength)
{
	struct cx24110_state *state = fe->demodulator_priv;

/* no provision in hardware. Read the frontend AGC accumulator. No idea how to scale this, but I know it is 2s complement */
	u8 signal = cx24110_readreg (state, 0x27)+128;
	*signal_strength = (signal << 8) | signal;

	return 0;
}

static int cx24110_read_snr(struct dvb_frontend* fe, u16* snr)
{
	struct cx24110_state *state = fe->demodulator_priv;

	/* no provision in hardware. Can be computed from the Es/N0 estimator, but I don't know how. */
	if(cx24110_readreg(state,0x6a)&0x80) {
		/* the Es/N0 error counter has finished one counting window */
		state->lastesn0=cx24110_readreg(state,0x69)|
			(cx24110_readreg(state,0x68)<<8);
		cx24110_writereg(state,0x6a,0x84); /* start new count window */
	}
	*snr = state->lastesn0;

	return 0;
}

static int cx24110_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct cx24110_state *state = fe->demodulator_priv;
	u32 lastbyer;

	if(cx24110_readreg(state,0x10)&0x40) {
		/* the RS error counter has finished one counting window */
		cx24110_writereg(state,0x10,0x60); /* select the byer reg */
		lastbyer=cx24110_readreg(state,0x12)|
			(cx24110_readreg(state,0x13)<<8)|
			(cx24110_readreg(state,0x14)<<16);
		cx24110_writereg(state,0x10,0x70); /* select the bler reg */
		state->lastbler=cx24110_readreg(state,0x12)|
			(cx24110_readreg(state,0x13)<<8)|
			(cx24110_readreg(state,0x14)<<16);
		cx24110_writereg(state,0x10,0x20); /* start new count window */
	}
	*ucblocks = state->lastbler;

	return 0;
}

static int cx24110_set_frontend(struct dvb_frontend* fe, struct dvb_frontend_parameters *p)
{
	struct cx24110_state *state = fe->demodulator_priv;


	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe, p);
		if (fe->ops.i2c_gate_ctrl) fe->ops.i2c_gate_ctrl(fe, 0);
	}

	cx24110_set_inversion (state, p->inversion);
	cx24110_set_fec (state, p->u.qpsk.fec_inner);
	cx24110_set_symbolrate (state, p->u.qpsk.symbol_rate);
	cx24110_writereg(state,0x04,0x05); /* start aquisition */

	return 0;
}

static int cx24110_get_frontend(struct dvb_frontend* fe, struct dvb_frontend_parameters *p)
{
	struct cx24110_state *state = fe->demodulator_priv;
	s32 afc; unsigned sclk;

/* cannot read back tuner settings (freq). Need to have some private storage */

	sclk = cx24110_readreg (state, 0x07) & 0x03;
/* ok, real AFC (FEDR) freq. is afc/2^24*fsamp, fsamp=45/60/80/90MHz.
 * Need 64 bit arithmetic. Is thiss possible in the kernel? */
	if (sclk==0) sclk=90999000L/2L;
	else if (sclk==1) sclk=60666000L;
	else if (sclk==2) sclk=80888000L;
	else sclk=90999000L;
	sclk>>=8;
	afc = sclk*(cx24110_readreg (state, 0x44)&0x1f)+
	      ((sclk*cx24110_readreg (state, 0x45))>>8)+
	      ((sclk*cx24110_readreg (state, 0x46))>>16);

	p->frequency += afc;
	p->inversion = (cx24110_readreg (state, 0x22) & 0x10) ?
				INVERSION_ON : INVERSION_OFF;
	p->u.qpsk.fec_inner = cx24110_get_fec (state);

	return 0;
}

static int cx24110_set_tone(struct dvb_frontend* fe, fe_sec_tone_mode_t tone)
{
	struct cx24110_state *state = fe->demodulator_priv;

	return cx24110_writereg(state,0x76,(cx24110_readreg(state,0x76)&~0x10)|(((tone==SEC_TONE_ON))?0x10:0));
}

static void cx24110_release(struct dvb_frontend* fe)
{
	struct cx24110_state* state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops cx24110_ops;

struct dvb_frontend* cx24110_attach(const struct cx24110_config* config,
				    struct i2c_adapter* i2c)
{
	struct cx24110_state* state = NULL;
	int ret;

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct cx24110_state), GFP_KERNEL);
	if (state == NULL) goto error;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;
	state->lastber = 0;
	state->lastbler = 0;
	state->lastesn0 = 0;

	/* check if the demod is there */
	ret = cx24110_readreg(state, 0x00);
	if ((ret != 0x5a) && (ret != 0x69)) goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &cx24110_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops cx24110_ops = {

	.info = {
		.name = "Conexant CX24110 DVB-S",
		.type = FE_QPSK,
		.frequency_min = 950000,
		.frequency_max = 2150000,
		.frequency_stepsize = 1011,  /* kHz for QPSK frontends */
		.frequency_tolerance = 29500,
		.symbol_rate_min = 1000000,
		.symbol_rate_max = 45000000,
		.caps = FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK | FE_CAN_RECOVER
	},

	.release = cx24110_release,

	.init = cx24110_initfe,
	.write = _cx24110_pll_write,
	.set_frontend = cx24110_set_frontend,
	.get_frontend = cx24110_get_frontend,
	.read_status = cx24110_read_status,
	.read_ber = cx24110_read_ber,
	.read_signal_strength = cx24110_read_signal_strength,
	.read_snr = cx24110_read_snr,
	.read_ucblocks = cx24110_read_ucblocks,

	.diseqc_send_master_cmd = cx24110_send_diseqc_msg,
	.set_tone = cx24110_set_tone,
	.set_voltage = cx24110_set_voltage,
	.diseqc_send_burst = cx24110_diseqc_send_burst,
};

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("Conexant CX24110 DVB-S Demodulator driver");
MODULE_AUTHOR("Peter Hettkamp");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(cx24110_attach);

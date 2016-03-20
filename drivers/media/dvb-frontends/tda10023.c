/*
    TDA10023  - DVB-C decoder
    (as used in Philips CU1216-3 NIM and the Reelbox DVB-C tuner card)

    Copyright (C) 2005 Georg Acher, BayCom GmbH (acher at baycom dot de)
    Copyright (c) 2006 Hartmut Birr (e9hack at gmail dot com)

    Remotely based on tda10021.c
    Copyright (C) 1999 Convergence Integrated Media GmbH <ralph@convergence.de>
    Copyright (C) 2004 Markus Schulz <msc@antzsystem.de>
		   Support for TDA10021

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
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>

#include <asm/div64.h>

#include "dvb_frontend.h"
#include "tda1002x.h"

#define REG0_INIT_VAL 0x23

struct tda10023_state {
	struct i2c_adapter* i2c;
	/* configuration settings */
	const struct tda10023_config *config;
	struct dvb_frontend frontend;

	u8 pwm;
	u8 reg0;

	/* clock settings */
	u32 xtal;
	u8 pll_m;
	u8 pll_p;
	u8 pll_n;
	u32 sysclk;
};

#define dprintk(x...)

static int verbose;

static u8 tda10023_readreg (struct tda10023_state* state, u8 reg)
{
	u8 b0 [] = { reg };
	u8 b1 [] = { 0 };
	struct i2c_msg msg [] = { { .addr = state->config->demod_address, .flags = 0, .buf = b0, .len = 1 },
				  { .addr = state->config->demod_address, .flags = I2C_M_RD, .buf = b1, .len = 1 } };
	int ret;

	ret = i2c_transfer (state->i2c, msg, 2);
	if (ret != 2) {
		int num = state->frontend.dvb ? state->frontend.dvb->num : -1;
		printk(KERN_ERR "DVB: TDA10023(%d): %s: readreg error "
			"(reg == 0x%02x, ret == %i)\n",
			num, __func__, reg, ret);
	}
	return b1[0];
}

static int tda10023_writereg (struct tda10023_state* state, u8 reg, u8 data)
{
	u8 buf[] = { reg, data };
	struct i2c_msg msg = { .addr = state->config->demod_address, .flags = 0, .buf = buf, .len = 2 };
	int ret;

	ret = i2c_transfer (state->i2c, &msg, 1);
	if (ret != 1) {
		int num = state->frontend.dvb ? state->frontend.dvb->num : -1;
		printk(KERN_ERR "DVB: TDA10023(%d): %s, writereg error "
			"(reg == 0x%02x, val == 0x%02x, ret == %i)\n",
			num, __func__, reg, data, ret);
	}
	return (ret != 1) ? -EREMOTEIO : 0;
}


static int tda10023_writebit (struct tda10023_state* state, u8 reg, u8 mask,u8 data)
{
	if (mask==0xff)
		return tda10023_writereg(state, reg, data);
	else {
		u8 val;
		val=tda10023_readreg(state,reg);
		val&=~mask;
		val|=(data&mask);
		return tda10023_writereg(state, reg, val);
	}
}

static void tda10023_writetab(struct tda10023_state* state, u8* tab)
{
	u8 r,m,v;
	while (1) {
		r=*tab++;
		m=*tab++;
		v=*tab++;
		if (r==0xff) {
			if (m==0xff)
				break;
			else
				msleep(m);
		}
		else
			tda10023_writebit(state,r,m,v);
	}
}

//get access to tuner
static int lock_tuner(struct tda10023_state* state)
{
	u8 buf[2] = { 0x0f, 0xc0 };
	struct i2c_msg msg = {.addr=state->config->demod_address, .flags=0, .buf=buf, .len=2};

	if(i2c_transfer(state->i2c, &msg, 1) != 1)
	{
		printk("tda10023: lock tuner fails\n");
		return -EREMOTEIO;
	}
	return 0;
}

//release access from tuner
static int unlock_tuner(struct tda10023_state* state)
{
	u8 buf[2] = { 0x0f, 0x40 };
	struct i2c_msg msg_post={.addr=state->config->demod_address, .flags=0, .buf=buf, .len=2};

	if(i2c_transfer(state->i2c, &msg_post, 1) != 1)
	{
		printk("tda10023: unlock tuner fails\n");
		return -EREMOTEIO;
	}
	return 0;
}

static int tda10023_setup_reg0 (struct tda10023_state* state, u8 reg0)
{
	reg0 |= state->reg0 & 0x63;

	tda10023_writereg (state, 0x00, reg0 & 0xfe);
	tda10023_writereg (state, 0x00, reg0 | 0x01);

	state->reg0 = reg0;
	return 0;
}

static int tda10023_set_symbolrate (struct tda10023_state* state, u32 sr)
{
	s32 BDR;
	s32 BDRI;
	s16 SFIL=0;
	u16 NDEC = 0;

	/* avoid floating point operations multiplying syscloc and divider
	   by 10 */
	u32 sysclk_x_10 = state->sysclk * 10;

	if (sr < (u32)(sysclk_x_10/984)) {
		NDEC=3;
		SFIL=1;
	} else if (sr < (u32)(sysclk_x_10/640)) {
		NDEC=3;
		SFIL=0;
	} else if (sr < (u32)(sysclk_x_10/492)) {
		NDEC=2;
		SFIL=1;
	} else if (sr < (u32)(sysclk_x_10/320)) {
		NDEC=2;
		SFIL=0;
	} else if (sr < (u32)(sysclk_x_10/246)) {
		NDEC=1;
		SFIL=1;
	} else if (sr < (u32)(sysclk_x_10/160)) {
		NDEC=1;
		SFIL=0;
	} else if (sr < (u32)(sysclk_x_10/123)) {
		NDEC=0;
		SFIL=1;
	}

	BDRI = (state->sysclk)*16;
	BDRI>>=NDEC;
	BDRI +=sr/2;
	BDRI /=sr;

	if (BDRI>255)
		BDRI=255;

	{
		u64 BDRX;

		BDRX=1<<(24+NDEC);
		BDRX*=sr;
		do_div(BDRX, state->sysclk); 	/* BDRX/=SYSCLK; */

		BDR=(s32)BDRX;
	}
	dprintk("Symbolrate %i, BDR %i BDRI %i, NDEC %i\n",
		sr, BDR, BDRI, NDEC);
	tda10023_writebit (state, 0x03, 0xc0, NDEC<<6);
	tda10023_writereg (state, 0x0a, BDR&255);
	tda10023_writereg (state, 0x0b, (BDR>>8)&255);
	tda10023_writereg (state, 0x0c, (BDR>>16)&31);
	tda10023_writereg (state, 0x0d, BDRI);
	tda10023_writereg (state, 0x3d, (SFIL<<7));
	return 0;
}

static int tda10023_init (struct dvb_frontend *fe)
{
	struct tda10023_state* state = fe->demodulator_priv;
	u8 tda10023_inittab[] = {
/*        reg  mask val */
/* 000 */ 0x2a, 0xff, 0x02,  /* PLL3, Bypass, Power Down */
/* 003 */ 0xff, 0x64, 0x00,  /* Sleep 100ms */
/* 006 */ 0x2a, 0xff, 0x03,  /* PLL3, Bypass, Power Down */
/* 009 */ 0xff, 0x64, 0x00,  /* Sleep 100ms */
			   /* PLL1 */
/* 012 */ 0x28, 0xff, (state->pll_m-1),
			   /* PLL2 */
/* 015 */ 0x29, 0xff, ((state->pll_p-1)<<6)|(state->pll_n-1),
			   /* GPR FSAMPLING=1 */
/* 018 */ 0x00, 0xff, REG0_INIT_VAL,
/* 021 */ 0x2a, 0xff, 0x08,  /* PLL3 PSACLK=1 */
/* 024 */ 0xff, 0x64, 0x00,  /* Sleep 100ms */
/* 027 */ 0x1f, 0xff, 0x00,  /* RESET */
/* 030 */ 0xff, 0x64, 0x00,  /* Sleep 100ms */
/* 033 */ 0xe6, 0x0c, 0x04,  /* RSCFG_IND */
/* 036 */ 0x10, 0xc0, 0x80,  /* DECDVBCFG1 PBER=1 */

/* 039 */ 0x0e, 0xff, 0x82,  /* GAIN1 */
/* 042 */ 0x03, 0x08, 0x08,  /* CLKCONF DYN=1 */
/* 045 */ 0x2e, 0xbf, 0x30,  /* AGCCONF2 TRIAGC=0,POSAGC=ENAGCIF=1
				       PPWMTUN=0 PPWMIF=0 */
/* 048 */ 0x01, 0xff, 0x30,  /* AGCREF */
/* 051 */ 0x1e, 0x84, 0x84,  /* CONTROL SACLK_ON=1 */
/* 054 */ 0x1b, 0xff, 0xc8,  /* ADC TWOS=1 */
/* 057 */ 0x3b, 0xff, 0xff,  /* IFMAX */
/* 060 */ 0x3c, 0xff, 0x00,  /* IFMIN */
/* 063 */ 0x34, 0xff, 0x00,  /* PWMREF */
/* 066 */ 0x35, 0xff, 0xff,  /* TUNMAX */
/* 069 */ 0x36, 0xff, 0x00,  /* TUNMIN */
/* 072 */ 0x06, 0xff, 0x7f,  /* EQCONF1 POSI=7 ENADAPT=ENEQUAL=DFE=1 */
/* 075 */ 0x1c, 0x30, 0x30,  /* EQCONF2 STEPALGO=SGNALGO=1 */
/* 078 */ 0x37, 0xff, 0xf6,  /* DELTAF_LSB */
/* 081 */ 0x38, 0xff, 0xff,  /* DELTAF_MSB */
/* 084 */ 0x02, 0xff, 0x93,  /* AGCCONF1  IFS=1 KAGCIF=2 KAGCTUN=3 */
/* 087 */ 0x2d, 0xff, 0xf6,  /* SWEEP SWPOS=1 SWDYN=7 SWSTEP=1 SWLEN=2 */
/* 090 */ 0x04, 0x10, 0x00,  /* SWRAMP=1 */
/* 093 */ 0x12, 0xff, TDA10023_OUTPUT_MODE_PARALLEL_B, /*
				INTP1 POCLKP=1 FEL=1 MFS=0 */
/* 096 */ 0x2b, 0x01, 0xa1,  /* INTS1 */
/* 099 */ 0x20, 0xff, 0x04,  /* INTP2 SWAPP=? MSBFIRSTP=? INTPSEL=? */
/* 102 */ 0x2c, 0xff, 0x0d,  /* INTP/S TRIP=0 TRIS=0 */
/* 105 */ 0xc4, 0xff, 0x00,
/* 108 */ 0xc3, 0x30, 0x00,
/* 111 */ 0xb5, 0xff, 0x19,  /* ERAGC_THD */
/* 114 */ 0x00, 0x03, 0x01,  /* GPR, CLBS soft reset */
/* 117 */ 0x00, 0x03, 0x03,  /* GPR, CLBS soft reset */
/* 120 */ 0xff, 0x64, 0x00,  /* Sleep 100ms */
/* 123 */ 0xff, 0xff, 0xff
};
	dprintk("DVB: TDA10023(%d): init chip\n", fe->dvb->num);

	/* override default values if set in config */
	if (state->config->deltaf) {
		tda10023_inittab[80] = (state->config->deltaf & 0xff);
		tda10023_inittab[83] = (state->config->deltaf >> 8);
	}

	if (state->config->output_mode)
		tda10023_inittab[95] = state->config->output_mode;

	tda10023_writetab(state, tda10023_inittab);

	return 0;
}

struct qam_params {
	u8 qam, lockthr, mseth, aref, agcrefnyq, eragnyq_thd;
};

static int tda10023_set_parameters(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u32 delsys  = c->delivery_system;
	unsigned qam = c->modulation;
	bool is_annex_c;
	struct tda10023_state* state = fe->demodulator_priv;
	static const struct qam_params qam_params[] = {
		/* Modulation  QAM    LOCKTHR   MSETH   AREF AGCREFNYQ ERAGCNYQ_THD */
		[QPSK]    = { (5<<2),  0x78,    0x8c,   0x96,   0x78,   0x4c  },
		[QAM_16]  = { (0<<2),  0x87,    0xa2,   0x91,   0x8c,   0x57  },
		[QAM_32]  = { (1<<2),  0x64,    0x74,   0x96,   0x8c,   0x57  },
		[QAM_64]  = { (2<<2),  0x46,    0x43,   0x6a,   0x6a,   0x44  },
		[QAM_128] = { (3<<2),  0x36,    0x34,   0x7e,   0x78,   0x4c  },
		[QAM_256] = { (4<<2),  0x26,    0x23,   0x6c,   0x5c,   0x3c  },
	};

	switch (delsys) {
	case SYS_DVBC_ANNEX_A:
		is_annex_c = false;
		break;
	case SYS_DVBC_ANNEX_C:
		is_annex_c = true;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * gcc optimizes the code below the same way as it would code:
	 *		 "if (qam > 5) return -EINVAL;"
	 * Yet, the code is clearer, as it shows what QAM standards are
	 * supported by the driver, and avoids the usage of magic numbers on
	 * it.
	 */
	switch (qam) {
	case QPSK:
	case QAM_16:
	case QAM_32:
	case QAM_64:
	case QAM_128:
	case QAM_256:
		break;
	default:
		return -EINVAL;
	}

	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe);
		if (fe->ops.i2c_gate_ctrl) fe->ops.i2c_gate_ctrl(fe, 0);
	}

	tda10023_set_symbolrate(state, c->symbol_rate);
	tda10023_writereg(state, 0x05, qam_params[qam].lockthr);
	tda10023_writereg(state, 0x08, qam_params[qam].mseth);
	tda10023_writereg(state, 0x09, qam_params[qam].aref);
	tda10023_writereg(state, 0xb4, qam_params[qam].agcrefnyq);
	tda10023_writereg(state, 0xb6, qam_params[qam].eragnyq_thd);
#if 0
	tda10023_writereg(state, 0x04, (c->inversion ? 0x12 : 0x32));
	tda10023_writebit(state, 0x04, 0x60, (c->inversion ? 0 : 0x20));
#endif
	tda10023_writebit(state, 0x04, 0x40, 0x40);

	if (is_annex_c)
		tda10023_writebit(state, 0x3d, 0xfc, 0x03);
	else
		tda10023_writebit(state, 0x3d, 0xfc, 0x02);

	tda10023_setup_reg0(state, qam_params[qam].qam);

	return 0;
}

static int tda10023_read_status(struct dvb_frontend *fe,
				enum fe_status *status)
{
	struct tda10023_state* state = fe->demodulator_priv;
	int sync;

	*status = 0;

	//0x11[1] == CARLOCK -> Carrier locked
	//0x11[2] == FSYNC -> Frame synchronisation
	//0x11[3] == FEL -> Front End locked
	//0x11[6] == NODVB -> DVB Mode Information
	sync = tda10023_readreg (state, 0x11);

	if (sync & 2)
		*status |= FE_HAS_SIGNAL|FE_HAS_CARRIER;

	if (sync & 4)
		*status |= FE_HAS_SYNC|FE_HAS_VITERBI;

	if (sync & 8)
		*status |= FE_HAS_LOCK;

	return 0;
}

static int tda10023_read_ber(struct dvb_frontend* fe, u32* ber)
{
	struct tda10023_state* state = fe->demodulator_priv;
	u8 a,b,c;
	a=tda10023_readreg(state, 0x14);
	b=tda10023_readreg(state, 0x15);
	c=tda10023_readreg(state, 0x16)&0xf;
	tda10023_writebit (state, 0x10, 0xc0, 0x00);

	*ber = a | (b<<8)| (c<<16);
	return 0;
}

static int tda10023_read_signal_strength(struct dvb_frontend* fe, u16* strength)
{
	struct tda10023_state* state = fe->demodulator_priv;
	u8 ifgain=tda10023_readreg(state, 0x2f);

	u16 gain = ((255-tda10023_readreg(state, 0x17))) + (255-ifgain)/16;
	// Max raw value is about 0xb0 -> Normalize to >0xf0 after 0x90
	if (gain>0x90)
		gain=gain+2*(gain-0x90);
	if (gain>255)
		gain=255;

	*strength = (gain<<8)|gain;
	return 0;
}

static int tda10023_read_snr(struct dvb_frontend* fe, u16* snr)
{
	struct tda10023_state* state = fe->demodulator_priv;

	u8 quality = ~tda10023_readreg(state, 0x18);
	*snr = (quality << 8) | quality;
	return 0;
}

static int tda10023_read_ucblocks(struct dvb_frontend* fe, u32* ucblocks)
{
	struct tda10023_state* state = fe->demodulator_priv;
	u8 a,b,c,d;
	a= tda10023_readreg (state, 0x74);
	b= tda10023_readreg (state, 0x75);
	c= tda10023_readreg (state, 0x76);
	d= tda10023_readreg (state, 0x77);
	*ucblocks = a | (b<<8)|(c<<16)|(d<<24);

	tda10023_writebit (state, 0x10, 0x20,0x00);
	tda10023_writebit (state, 0x10, 0x20,0x20);
	tda10023_writebit (state, 0x13, 0x01, 0x00);

	return 0;
}

static int tda10023_get_frontend(struct dvb_frontend *fe,
				 struct dtv_frontend_properties *p)
{
	struct tda10023_state* state = fe->demodulator_priv;
	int sync,inv;
	s8 afc = 0;

	sync = tda10023_readreg(state, 0x11);
	afc = tda10023_readreg(state, 0x19);
	inv = tda10023_readreg(state, 0x04);

	if (verbose) {
		/* AFC only valid when carrier has been recovered */
		printk(sync & 2 ? "DVB: TDA10023(%d): AFC (%d) %dHz\n" :
				  "DVB: TDA10023(%d): [AFC (%d) %dHz]\n",
			state->frontend.dvb->num, afc,
		       -((s32)p->symbol_rate * afc) >> 10);
	}

	p->inversion = (inv&0x20?0:1);
	p->modulation = ((state->reg0 >> 2) & 7) + QAM_16;

	p->fec_inner = FEC_NONE;
	p->frequency = ((p->frequency + 31250) / 62500) * 62500;

	if (sync & 2)
		p->frequency -= ((s32)p->symbol_rate * afc) >> 10;

	return 0;
}

static int tda10023_sleep(struct dvb_frontend* fe)
{
	struct tda10023_state* state = fe->demodulator_priv;

	tda10023_writereg (state, 0x1b, 0x02);  /* pdown ADC */
	tda10023_writereg (state, 0x00, 0x80);  /* standby */

	return 0;
}

static int tda10023_i2c_gate_ctrl(struct dvb_frontend* fe, int enable)
{
	struct tda10023_state* state = fe->demodulator_priv;

	if (enable) {
		lock_tuner(state);
	} else {
		unlock_tuner(state);
	}
	return 0;
}

static void tda10023_release(struct dvb_frontend* fe)
{
	struct tda10023_state* state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops tda10023_ops;

struct dvb_frontend *tda10023_attach(const struct tda10023_config *config,
				     struct i2c_adapter *i2c,
				     u8 pwm)
{
	struct tda10023_state* state = NULL;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct tda10023_state), GFP_KERNEL);
	if (state == NULL) goto error;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;

	/* wakeup if in standby */
	tda10023_writereg (state, 0x00, 0x33);
	/* check if the demod is there */
	if ((tda10023_readreg(state, 0x1a) & 0xf0) != 0x70) goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &tda10023_ops, sizeof(struct dvb_frontend_ops));
	state->pwm = pwm;
	state->reg0 = REG0_INIT_VAL;
	if (state->config->xtal) {
		state->xtal  = state->config->xtal;
		state->pll_m = state->config->pll_m;
		state->pll_p = state->config->pll_p;
		state->pll_n = state->config->pll_n;
	} else {
		/* set default values if not defined in config */
		state->xtal  = 28920000;
		state->pll_m = 8;
		state->pll_p = 4;
		state->pll_n = 1;
	}

	/* calc sysclk */
	state->sysclk = (state->xtal * state->pll_m / \
			(state->pll_n * state->pll_p));

	state->frontend.ops.info.symbol_rate_min = (state->sysclk/2)/64;
	state->frontend.ops.info.symbol_rate_max = (state->sysclk/2)/4;

	dprintk("DVB: TDA10023 %s: xtal:%d pll_m:%d pll_p:%d pll_n:%d\n",
		__func__, state->xtal, state->pll_m, state->pll_p,
		state->pll_n);

	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}

static struct dvb_frontend_ops tda10023_ops = {
	.delsys = { SYS_DVBC_ANNEX_A, SYS_DVBC_ANNEX_C },
	.info = {
		.name = "Philips TDA10023 DVB-C",
		.frequency_stepsize = 62500,
		.frequency_min =  47000000,
		.frequency_max = 862000000,
		.symbol_rate_min = 0,  /* set in tda10023_attach */
		.symbol_rate_max = 0,  /* set in tda10023_attach */
		.caps = 0x400 | //FE_CAN_QAM_4
			FE_CAN_QAM_16 | FE_CAN_QAM_32 | FE_CAN_QAM_64 |
			FE_CAN_QAM_128 | FE_CAN_QAM_256 |
			FE_CAN_FEC_AUTO
	},

	.release = tda10023_release,

	.init = tda10023_init,
	.sleep = tda10023_sleep,
	.i2c_gate_ctrl = tda10023_i2c_gate_ctrl,

	.set_frontend = tda10023_set_parameters,
	.get_frontend = tda10023_get_frontend,
	.read_status = tda10023_read_status,
	.read_ber = tda10023_read_ber,
	.read_signal_strength = tda10023_read_signal_strength,
	.read_snr = tda10023_read_snr,
	.read_ucblocks = tda10023_read_ucblocks,
};


MODULE_DESCRIPTION("Philips TDA10023 DVB-C demodulator driver");
MODULE_AUTHOR("Georg Acher, Hartmut Birr");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(tda10023_attach);

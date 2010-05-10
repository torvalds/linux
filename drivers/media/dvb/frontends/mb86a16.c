/*
	Fujitsu MB86A16 DVB-S/DSS DC Receiver driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

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

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>

#include "dvb_frontend.h"
#include "mb86a16.h"
#include "mb86a16_priv.h"

unsigned int verbose = 5;
module_param(verbose, int, 0644);

#define ABS(x)		((x) < 0 ? (-x) : (x))

struct mb86a16_state {
	struct i2c_adapter		*i2c_adap;
	const struct mb86a16_config	*config;
	struct dvb_frontend		frontend;

	/* tuning parameters */
	int				frequency;
	int				srate;

	/* Internal stuff */
	int				master_clk;
	int				deci;
	int				csel;
	int				rsel;
};

#define MB86A16_ERROR		0
#define MB86A16_NOTICE		1
#define MB86A16_INFO		2
#define MB86A16_DEBUG		3

#define dprintk(x, y, z, format, arg...) do {						\
	if (z) {									\
		if	((x > MB86A16_ERROR) && (x > y))				\
			printk(KERN_ERR "%s: " format "\n", __func__, ##arg);		\
		else if ((x > MB86A16_NOTICE) && (x > y))				\
			printk(KERN_NOTICE "%s: " format "\n", __func__, ##arg);	\
		else if ((x > MB86A16_INFO) && (x > y))					\
			printk(KERN_INFO "%s: " format "\n", __func__, ##arg);		\
		else if ((x > MB86A16_DEBUG) && (x > y))				\
			printk(KERN_DEBUG "%s: " format "\n", __func__, ##arg);		\
	} else {									\
		if (x > y)								\
			printk(format, ##arg);						\
	}										\
} while (0)

#define TRACE_IN	dprintk(verbose, MB86A16_DEBUG, 1, "-->()")
#define TRACE_OUT	dprintk(verbose, MB86A16_DEBUG, 1, "()-->")

static int mb86a16_write(struct mb86a16_state *state, u8 reg, u8 val)
{
	int ret;
	u8 buf[] = { reg, val };

	struct i2c_msg msg = {
		.addr = state->config->demod_address,
		.flags = 0,
		.buf = buf,
		.len = 2
	};

	dprintk(verbose, MB86A16_DEBUG, 1,
		"writing to [0x%02x],Reg[0x%02x],Data[0x%02x]",
		state->config->demod_address, buf[0], buf[1]);

	ret = i2c_transfer(state->i2c_adap, &msg, 1);

	return (ret != 1) ? -EREMOTEIO : 0;
}

static int mb86a16_read(struct mb86a16_state *state, u8 reg, u8 *val)
{
	int ret;
	u8 b0[] = { reg };
	u8 b1[] = { 0 };

	struct i2c_msg msg[] = {
		{
			.addr = state->config->demod_address,
			.flags = 0,
			.buf = b0,
			.len = 1
		}, {
			.addr = state->config->demod_address,
			.flags = I2C_M_RD,
			.buf = b1,
			.len = 1
		}
	};
	ret = i2c_transfer(state->i2c_adap, msg, 2);
	if (ret != 2) {
		dprintk(verbose, MB86A16_ERROR, 1, "read error(reg=0x%02x, ret=0x%i)",
			reg, ret);

		return -EREMOTEIO;
	}
	*val = b1[0];

	return ret;
}

static int CNTM_set(struct mb86a16_state *state,
		    unsigned char timint1,
		    unsigned char timint2,
		    unsigned char cnext)
{
	unsigned char val;

	val = (timint1 << 4) | (timint2 << 2) | cnext;
	if (mb86a16_write(state, MB86A16_CNTMR, val) < 0)
		goto err;

	return 0;

err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
	return -EREMOTEIO;
}

static int smrt_set(struct mb86a16_state *state, int rate)
{
	int tmp ;
	int m ;
	unsigned char STOFS0, STOFS1;

	m = 1 << state->deci;
	tmp = (8192 * state->master_clk - 2 * m * rate * 8192 + state->master_clk / 2) / state->master_clk;

	STOFS0 = tmp & 0x0ff;
	STOFS1 = (tmp & 0xf00) >> 8;

	if (mb86a16_write(state, MB86A16_SRATE1, (state->deci << 2) |
				       (state->csel << 1) |
					state->rsel) < 0)
		goto err;
	if (mb86a16_write(state, MB86A16_SRATE2, STOFS0) < 0)
		goto err;
	if (mb86a16_write(state, MB86A16_SRATE3, STOFS1) < 0)
		goto err;

	return 0;
err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
	return -1;
}

static int srst(struct mb86a16_state *state)
{
	if (mb86a16_write(state, MB86A16_RESET, 0x04) < 0)
		goto err;

	return 0;
err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
	return -EREMOTEIO;

}

static int afcex_data_set(struct mb86a16_state *state,
			  unsigned char AFCEX_L,
			  unsigned char AFCEX_H)
{
	if (mb86a16_write(state, MB86A16_AFCEXL, AFCEX_L) < 0)
		goto err;
	if (mb86a16_write(state, MB86A16_AFCEXH, AFCEX_H) < 0)
		goto err;

	return 0;
err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");

	return -1;
}

static int afcofs_data_set(struct mb86a16_state *state,
			   unsigned char AFCEX_L,
			   unsigned char AFCEX_H)
{
	if (mb86a16_write(state, 0x58, AFCEX_L) < 0)
		goto err;
	if (mb86a16_write(state, 0x59, AFCEX_H) < 0)
		goto err;

	return 0;
err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
	return -EREMOTEIO;
}

static int stlp_set(struct mb86a16_state *state,
		    unsigned char STRAS,
		    unsigned char STRBS)
{
	if (mb86a16_write(state, MB86A16_STRFILTCOEF1, (STRBS << 3) | (STRAS)) < 0)
		goto err;

	return 0;
err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
	return -EREMOTEIO;
}

static int Vi_set(struct mb86a16_state *state, unsigned char ETH, unsigned char VIA)
{
	if (mb86a16_write(state, MB86A16_VISET2, 0x04) < 0)
		goto err;
	if (mb86a16_write(state, MB86A16_VISET3, 0xf5) < 0)
		goto err;

	return 0;
err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
	return -EREMOTEIO;
}

static int initial_set(struct mb86a16_state *state)
{
	if (stlp_set(state, 5, 7))
		goto err;

	udelay(100);
	if (afcex_data_set(state, 0, 0))
		goto err;

	udelay(100);
	if (afcofs_data_set(state, 0, 0))
		goto err;

	udelay(100);
	if (mb86a16_write(state, MB86A16_CRLFILTCOEF1, 0x16) < 0)
		goto err;
	if (mb86a16_write(state, 0x2f, 0x21) < 0)
		goto err;
	if (mb86a16_write(state, MB86A16_VIMAG, 0x38) < 0)
		goto err;
	if (mb86a16_write(state, MB86A16_FAGCS1, 0x00) < 0)
		goto err;
	if (mb86a16_write(state, MB86A16_FAGCS2, 0x1c) < 0)
		goto err;
	if (mb86a16_write(state, MB86A16_FAGCS3, 0x20) < 0)
		goto err;
	if (mb86a16_write(state, MB86A16_FAGCS4, 0x1e) < 0)
		goto err;
	if (mb86a16_write(state, MB86A16_FAGCS5, 0x23) < 0)
		goto err;
	if (mb86a16_write(state, 0x54, 0xff) < 0)
		goto err;
	if (mb86a16_write(state, MB86A16_TSOUT, 0x00) < 0)
		goto err;

	return 0;

err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
	return -EREMOTEIO;
}

static int S01T_set(struct mb86a16_state *state,
		    unsigned char s1t,
		    unsigned s0t)
{
	if (mb86a16_write(state, 0x33, (s1t << 3) | s0t) < 0)
		goto err;

	return 0;
err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
	return -EREMOTEIO;
}


static int EN_set(struct mb86a16_state *state,
		  int cren,
		  int afcen)
{
	unsigned char val;

	val = 0x7a | (cren << 7) | (afcen << 2);
	if (mb86a16_write(state, 0x49, val) < 0)
		goto err;

	return 0;
err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
	return -EREMOTEIO;
}

static int AFCEXEN_set(struct mb86a16_state *state,
		       int afcexen,
		       int smrt)
{
	unsigned char AFCA ;

	if (smrt > 18875)
		AFCA = 4;
	else if (smrt > 9375)
		AFCA = 3;
	else if (smrt > 2250)
		AFCA = 2;
	else
		AFCA = 1;

	if (mb86a16_write(state, 0x2a, 0x02 | (afcexen << 5) | (AFCA << 2)) < 0)
		goto err;

	return 0;

err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
	return -EREMOTEIO;
}

static int DAGC_data_set(struct mb86a16_state *state,
			 unsigned char DAGCA,
			 unsigned char DAGCW)
{
	if (mb86a16_write(state, 0x2d, (DAGCA << 3) | DAGCW) < 0)
		goto err;

	return 0;

err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
	return -EREMOTEIO;
}

static void smrt_info_get(struct mb86a16_state *state, int rate)
{
	if (rate >= 37501) {
		state->deci = 0; state->csel = 0; state->rsel = 0;
	} else if (rate >= 30001) {
		state->deci = 0; state->csel = 0; state->rsel = 1;
	} else if (rate >= 26251) {
		state->deci = 0; state->csel = 1; state->rsel = 0;
	} else if (rate >= 22501) {
		state->deci = 0; state->csel = 1; state->rsel = 1;
	} else if (rate >= 18751) {
		state->deci = 1; state->csel = 0; state->rsel = 0;
	} else if (rate >= 15001) {
		state->deci = 1; state->csel = 0; state->rsel = 1;
	} else if (rate >= 13126) {
		state->deci = 1; state->csel = 1; state->rsel = 0;
	} else if (rate >= 11251) {
		state->deci = 1; state->csel = 1; state->rsel = 1;
	} else if (rate >= 9376) {
		state->deci = 2; state->csel = 0; state->rsel = 0;
	} else if (rate >= 7501) {
		state->deci = 2; state->csel = 0; state->rsel = 1;
	} else if (rate >= 6563) {
		state->deci = 2; state->csel = 1; state->rsel = 0;
	} else if (rate >= 5626) {
		state->deci = 2; state->csel = 1; state->rsel = 1;
	} else if (rate >= 4688) {
		state->deci = 3; state->csel = 0; state->rsel = 0;
	} else if (rate >= 3751) {
		state->deci = 3; state->csel = 0; state->rsel = 1;
	} else if (rate >= 3282) {
		state->deci = 3; state->csel = 1; state->rsel = 0;
	} else if (rate >= 2814) {
		state->deci = 3; state->csel = 1; state->rsel = 1;
	} else if (rate >= 2344) {
		state->deci = 4; state->csel = 0; state->rsel = 0;
	} else if (rate >= 1876) {
		state->deci = 4; state->csel = 0; state->rsel = 1;
	} else if (rate >= 1641) {
		state->deci = 4; state->csel = 1; state->rsel = 0;
	} else if (rate >= 1407) {
		state->deci = 4; state->csel = 1; state->rsel = 1;
	} else if (rate >= 1172) {
		state->deci = 5; state->csel = 0; state->rsel = 0;
	} else if (rate >=  939) {
		state->deci = 5; state->csel = 0; state->rsel = 1;
	} else if (rate >=  821) {
		state->deci = 5; state->csel = 1; state->rsel = 0;
	} else {
		state->deci = 5; state->csel = 1; state->rsel = 1;
	}

	if (state->csel == 0)
		state->master_clk = 92000;
	else
		state->master_clk = 61333;

}

static int signal_det(struct mb86a16_state *state,
		      int smrt,
		      unsigned char *SIG)
{

	int ret ;
	int smrtd ;
	int wait_sym ;

	u32 wait_t;
	unsigned char S[3] ;
	int i ;

	if (*SIG > 45) {
		if (CNTM_set(state, 2, 1, 2) < 0) {
			dprintk(verbose, MB86A16_ERROR, 1, "CNTM set Error");
			return -1;
		}
		wait_sym = 40000;
	} else {
		if (CNTM_set(state, 3, 1, 2) < 0) {
			dprintk(verbose, MB86A16_ERROR, 1, "CNTM set Error");
			return -1;
		}
		wait_sym = 80000;
	}
	for (i = 0; i < 3; i++) {
		if (i == 0)
			smrtd = smrt * 98 / 100;
		else if (i == 1)
			smrtd = smrt;
		else
			smrtd = smrt * 102 / 100;
		smrt_info_get(state, smrtd);
		smrt_set(state, smrtd);
		srst(state);
		wait_t = (wait_sym + 99 * smrtd / 100) / smrtd;
		if (wait_t == 0)
			wait_t = 1;
		msleep_interruptible(10);
		if (mb86a16_read(state, 0x37, &(S[i])) != 2) {
			dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
			return -EREMOTEIO;
		}
	}
	if ((S[1] > S[0] * 112 / 100) &&
	    (S[1] > S[2] * 112 / 100)) {

		ret = 1;
	} else {
		ret = 0;
	}
	*SIG = S[1];

	if (CNTM_set(state, 0, 1, 2) < 0) {
		dprintk(verbose, MB86A16_ERROR, 1, "CNTM set Error");
		return -1;
	}

	return ret;
}

static int rf_val_set(struct mb86a16_state *state,
		      int f,
		      int smrt,
		      unsigned char R)
{
	unsigned char C, F, B;
	int M;
	unsigned char rf_val[5];
	int ack = -1;

	if (smrt > 37750)
		C = 1;
	else if (smrt > 18875)
		C = 2;
	else if (smrt > 5500)
		C = 3;
	else
		C = 4;

	if (smrt > 30500)
		F = 3;
	else if (smrt > 9375)
		F = 1;
	else if (smrt > 4625)
		F = 0;
	else
		F = 2;

	if (f < 1060)
		B = 0;
	else if (f < 1175)
		B = 1;
	else if (f < 1305)
		B = 2;
	else if (f < 1435)
		B = 3;
	else if (f < 1570)
		B = 4;
	else if (f < 1715)
		B = 5;
	else if (f < 1845)
		B = 6;
	else if (f < 1980)
		B = 7;
	else if (f < 2080)
		B = 8;
	else
		B = 9;

	M = f * (1 << R) / 2;

	rf_val[0] = 0x01 | (C << 3) | (F << 1);
	rf_val[1] = (R << 5) | ((M & 0x1f000) >> 12);
	rf_val[2] = (M & 0x00ff0) >> 4;
	rf_val[3] = ((M & 0x0000f) << 4) | B;

	/* Frequency Set */
	if (mb86a16_write(state, 0x21, rf_val[0]) < 0)
		ack = 0;
	if (mb86a16_write(state, 0x22, rf_val[1]) < 0)
		ack = 0;
	if (mb86a16_write(state, 0x23, rf_val[2]) < 0)
		ack = 0;
	if (mb86a16_write(state, 0x24, rf_val[3]) < 0)
		ack = 0;
	if (mb86a16_write(state, 0x25, 0x01) < 0)
		ack = 0;
	if (ack == 0) {
		dprintk(verbose, MB86A16_ERROR, 1, "RF Setup - I2C transfer error");
		return -EREMOTEIO;
	}

	return 0;
}

static int afcerr_chk(struct mb86a16_state *state)
{
	unsigned char AFCM_L, AFCM_H ;
	int AFCM ;
	int afcm, afcerr ;

	if (mb86a16_read(state, 0x0e, &AFCM_L) != 2)
		goto err;
	if (mb86a16_read(state, 0x0f, &AFCM_H) != 2)
		goto err;

	AFCM = (AFCM_H << 8) + AFCM_L;

	if (AFCM > 2048)
		afcm = AFCM - 4096;
	else
		afcm = AFCM;
	afcerr = afcm * state->master_clk / 8192;

	return afcerr;

err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
	return -EREMOTEIO;
}

static int dagcm_val_get(struct mb86a16_state *state)
{
	int DAGCM;
	unsigned char DAGCM_H, DAGCM_L;

	if (mb86a16_read(state, 0x45, &DAGCM_L) != 2)
		goto err;
	if (mb86a16_read(state, 0x46, &DAGCM_H) != 2)
		goto err;

	DAGCM = (DAGCM_H << 8) + DAGCM_L;

	return DAGCM;

err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
	return -EREMOTEIO;
}

static int mb86a16_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	u8 stat, stat2;
	struct mb86a16_state *state = fe->demodulator_priv;

	*status = 0;

	if (mb86a16_read(state, MB86A16_SIG1, &stat) != 2)
		goto err;
	if (mb86a16_read(state, MB86A16_SIG2, &stat2) != 2)
		goto err;
	if ((stat > 25) && (stat2 > 25))
		*status |= FE_HAS_SIGNAL;
	if ((stat > 45) && (stat2 > 45))
		*status |= FE_HAS_CARRIER;

	if (mb86a16_read(state, MB86A16_STATUS, &stat) != 2)
		goto err;

	if (stat & 0x01)
		*status |= FE_HAS_SYNC;
	if (stat & 0x01)
		*status |= FE_HAS_VITERBI;

	if (mb86a16_read(state, MB86A16_FRAMESYNC, &stat) != 2)
		goto err;

	if ((stat & 0x0f) && (*status & FE_HAS_VITERBI))
		*status |= FE_HAS_LOCK;

	return 0;

err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
	return -EREMOTEIO;
}

static int sync_chk(struct mb86a16_state *state,
		    unsigned char *VIRM)
{
	unsigned char val;
	int sync;

	if (mb86a16_read(state, 0x0d, &val) != 2)
		goto err;

	dprintk(verbose, MB86A16_INFO, 1, "Status = %02x,", val);
	sync = val & 0x01;
	*VIRM = (val & 0x1c) >> 2;

	return sync;
err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
	return -EREMOTEIO;

}

static int freqerr_chk(struct mb86a16_state *state,
		       int fTP,
		       int smrt,
		       int unit)
{
	unsigned char CRM, AFCML, AFCMH;
	unsigned char temp1, temp2, temp3;
	int crm, afcm, AFCM;
	int crrerr, afcerr;		/* kHz */
	int frqerr;			/* MHz */
	int afcen, afcexen = 0;
	int R, M, fOSC, fOSC_OFS;

	if (mb86a16_read(state, 0x43, &CRM) != 2)
		goto err;

	if (CRM > 127)
		crm = CRM - 256;
	else
		crm = CRM;

	crrerr = smrt * crm / 256;
	if (mb86a16_read(state, 0x49, &temp1) != 2)
		goto err;

	afcen = (temp1 & 0x04) >> 2;
	if (afcen == 0) {
		if (mb86a16_read(state, 0x2a, &temp1) != 2)
			goto err;
		afcexen = (temp1 & 0x20) >> 5;
	}

	if (afcen == 1) {
		if (mb86a16_read(state, 0x0e, &AFCML) != 2)
			goto err;
		if (mb86a16_read(state, 0x0f, &AFCMH) != 2)
			goto err;
	} else if (afcexen == 1) {
		if (mb86a16_read(state, 0x2b, &AFCML) != 2)
			goto err;
		if (mb86a16_read(state, 0x2c, &AFCMH) != 2)
			goto err;
	}
	if ((afcen == 1) || (afcexen == 1)) {
		smrt_info_get(state, smrt);
		AFCM = ((AFCMH & 0x01) << 8) + AFCML;
		if (AFCM > 255)
			afcm = AFCM - 512;
		else
			afcm = AFCM;

		afcerr = afcm * state->master_clk / 8192;
	} else
		afcerr = 0;

	if (mb86a16_read(state, 0x22, &temp1) != 2)
		goto err;
	if (mb86a16_read(state, 0x23, &temp2) != 2)
		goto err;
	if (mb86a16_read(state, 0x24, &temp3) != 2)
		goto err;

	R = (temp1 & 0xe0) >> 5;
	M = ((temp1 & 0x1f) << 12) + (temp2 << 4) + (temp3 >> 4);
	if (R == 0)
		fOSC = 2 * M;
	else
		fOSC = M;

	fOSC_OFS = fOSC - fTP;

	if (unit == 0) {	/* MHz */
		if (crrerr + afcerr + fOSC_OFS * 1000 >= 0)
			frqerr = (crrerr + afcerr + fOSC_OFS * 1000 + 500) / 1000;
		else
			frqerr = (crrerr + afcerr + fOSC_OFS * 1000 - 500) / 1000;
	} else {	/* kHz */
		frqerr = crrerr + afcerr + fOSC_OFS * 1000;
	}

	return frqerr;
err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
	return -EREMOTEIO;
}

static unsigned char vco_dev_get(struct mb86a16_state *state, int smrt)
{
	unsigned char R;

	if (smrt > 9375)
		R = 0;
	else
		R = 1;

	return R;
}

static void swp_info_get(struct mb86a16_state *state,
			 int fOSC_start,
			 int smrt,
			 int v, int R,
			 int swp_ofs,
			 int *fOSC,
			 int *afcex_freq,
			 unsigned char *AFCEX_L,
			 unsigned char *AFCEX_H)
{
	int AFCEX ;
	int crnt_swp_freq ;

	crnt_swp_freq = fOSC_start * 1000 + v * swp_ofs;

	if (R == 0)
		*fOSC = (crnt_swp_freq + 1000) / 2000 * 2;
	else
		*fOSC = (crnt_swp_freq + 500) / 1000;

	if (*fOSC >= crnt_swp_freq)
		*afcex_freq = *fOSC * 1000 - crnt_swp_freq;
	else
		*afcex_freq = crnt_swp_freq - *fOSC * 1000;

	AFCEX = *afcex_freq * 8192 / state->master_clk;
	*AFCEX_L =  AFCEX & 0x00ff;
	*AFCEX_H = (AFCEX & 0x0f00) >> 8;
}


static int swp_freq_calcuation(struct mb86a16_state *state, int i, int v, int *V,  int vmax, int vmin,
			       int SIGMIN, int fOSC, int afcex_freq, int swp_ofs, unsigned char *SIG1)
{
	int swp_freq ;

	if ((i % 2 == 1) && (v <= vmax)) {
		/* positive v (case 1) */
		if ((v - 1 == vmin)				&&
		    (*(V + 30 + v) >= 0)			&&
		    (*(V + 30 + v - 1) >= 0)			&&
		    (*(V + 30 + v - 1) > *(V + 30 + v))		&&
		    (*(V + 30 + v - 1) > SIGMIN)) {

			swp_freq = fOSC * 1000 + afcex_freq - swp_ofs;
			*SIG1 = *(V + 30 + v - 1);
		} else if ((v == vmax)				&&
			   (*(V + 30 + v) >= 0)			&&
			   (*(V + 30 + v - 1) >= 0)		&&
			   (*(V + 30 + v) > *(V + 30 + v - 1))	&&
			   (*(V + 30 + v) > SIGMIN)) {
			/* (case 2) */
			swp_freq = fOSC * 1000 + afcex_freq;
			*SIG1 = *(V + 30 + v);
		} else if ((*(V + 30 + v) > 0)			&&
			   (*(V + 30 + v - 1) > 0)		&&
			   (*(V + 30 + v - 2) > 0)		&&
			   (*(V + 30 + v - 3) > 0)		&&
			   (*(V + 30 + v - 1) > *(V + 30 + v))	&&
			   (*(V + 30 + v - 2) > *(V + 30 + v - 3)) &&
			   ((*(V + 30 + v - 1) > SIGMIN)	||
			   (*(V + 30 + v - 2) > SIGMIN))) {
			/* (case 3) */
			if (*(V + 30 + v - 1) >= *(V + 30 + v - 2)) {
				swp_freq = fOSC * 1000 + afcex_freq - swp_ofs;
				*SIG1 = *(V + 30 + v - 1);
			} else {
				swp_freq = fOSC * 1000 + afcex_freq - swp_ofs * 2;
				*SIG1 = *(V + 30 + v - 2);
			}
		} else if ((v == vmax)				&&
			   (*(V + 30 + v) >= 0)			&&
			   (*(V + 30 + v - 1) >= 0)		&&
			   (*(V + 30 + v - 2) >= 0)		&&
			   (*(V + 30 + v) > *(V + 30 + v - 2))	&&
			   (*(V + 30 + v - 1) > *(V + 30 + v - 2)) &&
			   ((*(V + 30 + v) > SIGMIN)		||
			   (*(V + 30 + v - 1) > SIGMIN))) {
			/* (case 4) */
			if (*(V + 30 + v) >= *(V + 30 + v - 1)) {
				swp_freq = fOSC * 1000 + afcex_freq;
				*SIG1 = *(V + 30 + v);
			} else {
				swp_freq = fOSC * 1000 + afcex_freq - swp_ofs;
				*SIG1 = *(V + 30 + v - 1);
			}
		} else  {
			swp_freq = -1 ;
		}
	} else if ((i % 2 == 0) && (v >= vmin)) {
		/* Negative v (case 1) */
		if ((*(V + 30 + v) > 0)				&&
		    (*(V + 30 + v + 1) > 0)			&&
		    (*(V + 30 + v + 2) > 0)			&&
		    (*(V + 30 + v + 1) > *(V + 30 + v))		&&
		    (*(V + 30 + v + 1) > *(V + 30 + v + 2))	&&
		    (*(V + 30 + v + 1) > SIGMIN)) {

			swp_freq = fOSC * 1000 + afcex_freq + swp_ofs;
			*SIG1 = *(V + 30 + v + 1);
		} else if ((v + 1 == vmax)			&&
			   (*(V + 30 + v) >= 0)			&&
			   (*(V + 30 + v + 1) >= 0)		&&
			   (*(V + 30 + v + 1) > *(V + 30 + v))	&&
			   (*(V + 30 + v + 1) > SIGMIN)) {
			/* (case 2) */
			swp_freq = fOSC * 1000 + afcex_freq + swp_ofs;
			*SIG1 = *(V + 30 + v);
		} else if ((v == vmin)				&&
			   (*(V + 30 + v) > 0)			&&
			   (*(V + 30 + v + 1) > 0)		&&
			   (*(V + 30 + v + 2) > 0)		&&
			   (*(V + 30 + v) > *(V + 30 + v + 1))	&&
			   (*(V + 30 + v) > *(V + 30 + v + 2))	&&
			   (*(V + 30 + v) > SIGMIN)) {
			/* (case 3) */
			swp_freq = fOSC * 1000 + afcex_freq;
			*SIG1 = *(V + 30 + v);
		} else if ((*(V + 30 + v) >= 0)			&&
			   (*(V + 30 + v + 1) >= 0)		&&
			   (*(V + 30 + v + 2) >= 0)		&&
			   (*(V + 30 + v + 3) >= 0)		&&
			   (*(V + 30 + v + 1) > *(V + 30 + v))	&&
			   (*(V + 30 + v + 2) > *(V + 30 + v + 3)) &&
			   ((*(V + 30 + v + 1) > SIGMIN)	||
			    (*(V + 30 + v + 2) > SIGMIN))) {
			/* (case 4) */
			if (*(V + 30 + v + 1) >= *(V + 30 + v + 2)) {
				swp_freq = fOSC * 1000 + afcex_freq + swp_ofs;
				*SIG1 = *(V + 30 + v + 1);
			} else {
				swp_freq = fOSC * 1000 + afcex_freq + swp_ofs * 2;
				*SIG1 = *(V + 30 + v + 2);
			}
		} else if ((*(V + 30 + v) >= 0)			&&
			   (*(V + 30 + v + 1) >= 0)		&&
			   (*(V + 30 + v + 2) >= 0)		&&
			   (*(V + 30 + v + 3) >= 0)		&&
			   (*(V + 30 + v) > *(V + 30 + v + 2))	&&
			   (*(V + 30 + v + 1) > *(V + 30 + v + 2)) &&
			   (*(V + 30 + v) > *(V + 30 + v + 3))	&&
			   (*(V + 30 + v + 1) > *(V + 30 + v + 3)) &&
			   ((*(V + 30 + v) > SIGMIN)		||
			    (*(V + 30 + v + 1) > SIGMIN))) {
			/* (case 5) */
			if (*(V + 30 + v) >= *(V + 30 + v + 1)) {
				swp_freq = fOSC * 1000 + afcex_freq;
				*SIG1 = *(V + 30 + v);
			} else {
				swp_freq = fOSC * 1000 + afcex_freq + swp_ofs;
				*SIG1 = *(V + 30 + v + 1);
			}
		} else if ((v + 2 == vmin)			&&
			   (*(V + 30 + v) >= 0)			&&
			   (*(V + 30 + v + 1) >= 0)		&&
			   (*(V + 30 + v + 2) >= 0)		&&
			   (*(V + 30 + v + 1) > *(V + 30 + v))	&&
			   (*(V + 30 + v + 2) > *(V + 30 + v))	&&
			   ((*(V + 30 + v + 1) > SIGMIN)	||
			    (*(V + 30 + v + 2) > SIGMIN))) {
			/* (case 6) */
			if (*(V + 30 + v + 1) >= *(V + 30 + v + 2)) {
				swp_freq = fOSC * 1000 + afcex_freq + swp_ofs;
				*SIG1 = *(V + 30 + v + 1);
			} else {
				swp_freq = fOSC * 1000 + afcex_freq + swp_ofs * 2;
				*SIG1 = *(V + 30 + v + 2);
			}
		} else if ((vmax == 0) && (vmin == 0) && (*(V + 30 + v) > SIGMIN)) {
			swp_freq = fOSC * 1000;
			*SIG1 = *(V + 30 + v);
		} else
			swp_freq = -1;
	} else
		swp_freq = -1;

	return swp_freq;
}

static void swp_info_get2(struct mb86a16_state *state,
			  int smrt,
			  int R,
			  int swp_freq,
			  int *afcex_freq,
			  int *fOSC,
			  unsigned char *AFCEX_L,
			  unsigned char *AFCEX_H)
{
	int AFCEX ;

	if (R == 0)
		*fOSC = (swp_freq + 1000) / 2000 * 2;
	else
		*fOSC = (swp_freq + 500) / 1000;

	if (*fOSC >= swp_freq)
		*afcex_freq = *fOSC * 1000 - swp_freq;
	else
		*afcex_freq = swp_freq - *fOSC * 1000;

	AFCEX = *afcex_freq * 8192 / state->master_clk;
	*AFCEX_L =  AFCEX & 0x00ff;
	*AFCEX_H = (AFCEX & 0x0f00) >> 8;
}

static void afcex_info_get(struct mb86a16_state *state,
			   int afcex_freq,
			   unsigned char *AFCEX_L,
			   unsigned char *AFCEX_H)
{
	int AFCEX ;

	AFCEX = afcex_freq * 8192 / state->master_clk;
	*AFCEX_L =  AFCEX & 0x00ff;
	*AFCEX_H = (AFCEX & 0x0f00) >> 8;
}

static int SEQ_set(struct mb86a16_state *state, unsigned char loop)
{
	/* SLOCK0 = 0 */
	if (mb86a16_write(state, 0x32, 0x02 | (loop << 2)) < 0) {
		dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
		return -EREMOTEIO;
	}

	return 0;
}

static int iq_vt_set(struct mb86a16_state *state, unsigned char IQINV)
{
	/* Viterbi Rate, IQ Settings */
	if (mb86a16_write(state, 0x06, 0xdf | (IQINV << 5)) < 0) {
		dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
		return -EREMOTEIO;
	}

	return 0;
}

static int FEC_srst(struct mb86a16_state *state)
{
	if (mb86a16_write(state, MB86A16_RESET, 0x02) < 0) {
		dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
		return -EREMOTEIO;
	}

	return 0;
}

static int S2T_set(struct mb86a16_state *state, unsigned char S2T)
{
	if (mb86a16_write(state, 0x34, 0x70 | S2T) < 0) {
		dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
		return -EREMOTEIO;
	}

	return 0;
}

static int S45T_set(struct mb86a16_state *state, unsigned char S4T, unsigned char S5T)
{
	if (mb86a16_write(state, 0x35, 0x00 | (S5T << 4) | S4T) < 0) {
		dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
		return -EREMOTEIO;
	}

	return 0;
}


static int mb86a16_set_fe(struct mb86a16_state *state)
{
	u8 agcval, cnmval;

	int i, j;
	int fOSC = 0;
	int fOSC_start = 0;
	int wait_t;
	int fcp;
	int swp_ofs;
	int V[60];
	u8 SIG1MIN;

	unsigned char CREN, AFCEN, AFCEXEN;
	unsigned char SIG1;
	unsigned char TIMINT1, TIMINT2, TIMEXT;
	unsigned char S0T, S1T;
	unsigned char S2T;
/*	unsigned char S2T, S3T; */
	unsigned char S4T, S5T;
	unsigned char AFCEX_L, AFCEX_H;
	unsigned char R;
	unsigned char VIRM;
	unsigned char ETH, VIA;
	unsigned char junk;

	int loop;
	int ftemp;
	int v, vmax, vmin;
	int vmax_his, vmin_his;
	int swp_freq, prev_swp_freq[20];
	int prev_freq_num;
	int signal_dupl;
	int afcex_freq;
	int signal;
	int afcerr;
	int temp_freq, delta_freq;
	int dagcm[4];
	int smrt_d;
/*	int freq_err; */
	int n;
	int ret = -1;
	int sync;

	dprintk(verbose, MB86A16_INFO, 1, "freq=%d Mhz, symbrt=%d Ksps", state->frequency, state->srate);

	fcp = 3000;
	swp_ofs = state->srate / 4;

	for (i = 0; i < 60; i++)
		V[i] = -1;

	for (i = 0; i < 20; i++)
		prev_swp_freq[i] = 0;

	SIG1MIN = 25;

	for (n = 0; ((n < 3) && (ret == -1)); n++) {
		SEQ_set(state, 0);
		iq_vt_set(state, 0);

		CREN = 0;
		AFCEN = 0;
		AFCEXEN = 1;
		TIMINT1 = 0;
		TIMINT2 = 1;
		TIMEXT = 2;
		S1T = 0;
		S0T = 0;

		if (initial_set(state) < 0) {
			dprintk(verbose, MB86A16_ERROR, 1, "initial set failed");
			return -1;
		}
		if (DAGC_data_set(state, 3, 2) < 0) {
			dprintk(verbose, MB86A16_ERROR, 1, "DAGC data set error");
			return -1;
		}
		if (EN_set(state, CREN, AFCEN) < 0) {
			dprintk(verbose, MB86A16_ERROR, 1, "EN set error");
			return -1; /* (0, 0) */
		}
		if (AFCEXEN_set(state, AFCEXEN, state->srate) < 0) {
			dprintk(verbose, MB86A16_ERROR, 1, "AFCEXEN set error");
			return -1; /* (1, smrt) = (1, symbolrate) */
		}
		if (CNTM_set(state, TIMINT1, TIMINT2, TIMEXT) < 0) {
			dprintk(verbose, MB86A16_ERROR, 1, "CNTM set error");
			return -1; /* (0, 1, 2) */
		}
		if (S01T_set(state, S1T, S0T) < 0) {
			dprintk(verbose, MB86A16_ERROR, 1, "S01T set error");
			return -1; /* (0, 0) */
		}
		smrt_info_get(state, state->srate);
		if (smrt_set(state, state->srate) < 0) {
			dprintk(verbose, MB86A16_ERROR, 1, "smrt info get error");
			return -1;
		}

		R = vco_dev_get(state, state->srate);
		if (R == 1)
			fOSC_start = state->frequency;

		else if (R == 0) {
			if (state->frequency % 2 == 0) {
				fOSC_start = state->frequency;
			} else {
				fOSC_start = state->frequency + 1;
				if (fOSC_start > 2150)
					fOSC_start = state->frequency - 1;
			}
		}
		loop = 1;
		ftemp = fOSC_start * 1000;
		vmax = 0 ;
		while (loop == 1) {
			ftemp = ftemp + swp_ofs;
			vmax++;

			/* Upper bound */
			if (ftemp > 2150000) {
				loop = 0;
				vmax--;
			} else {
				if ((ftemp == 2150000) ||
				    (ftemp - state->frequency * 1000 >= fcp + state->srate / 4))
					loop = 0;
			}
		}

		loop = 1;
		ftemp = fOSC_start * 1000;
		vmin = 0 ;
		while (loop == 1) {
			ftemp = ftemp - swp_ofs;
			vmin--;

			/* Lower bound */
			if (ftemp < 950000) {
				loop = 0;
				vmin++;
			} else {
				if ((ftemp == 950000) ||
				    (state->frequency * 1000 - ftemp >= fcp + state->srate / 4))
					loop = 0;
			}
		}

		wait_t = (8000 + state->srate / 2) / state->srate;
		if (wait_t == 0)
			wait_t = 1;

		i = 0;
		j = 0;
		prev_freq_num = 0;
		loop = 1;
		signal = 0;
		vmax_his = 0;
		vmin_his = 0;
		v = 0;

		while (loop == 1) {
			swp_info_get(state, fOSC_start, state->srate,
				     v, R, swp_ofs, &fOSC,
				     &afcex_freq, &AFCEX_L, &AFCEX_H);

			udelay(100);
			if (rf_val_set(state, fOSC, state->srate, R) < 0) {
				dprintk(verbose, MB86A16_ERROR, 1, "rf val set error");
				return -1;
			}
			udelay(100);
			if (afcex_data_set(state, AFCEX_L, AFCEX_H) < 0) {
				dprintk(verbose, MB86A16_ERROR, 1, "afcex data set error");
				return -1;
			}
			if (srst(state) < 0) {
				dprintk(verbose, MB86A16_ERROR, 1, "srst error");
				return -1;
			}
			msleep_interruptible(wait_t);

			if (mb86a16_read(state, 0x37, &SIG1) != 2) {
				dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
				return -1;
			}
			V[30 + v] = SIG1 ;
			swp_freq = swp_freq_calcuation(state, i, v, V, vmax, vmin,
						      SIG1MIN, fOSC, afcex_freq,
						      swp_ofs, &SIG1);	/* changed */

			signal_dupl = 0;
			for (j = 0; j < prev_freq_num; j++) {
				if ((ABS(prev_swp_freq[j] - swp_freq)) < (swp_ofs * 3 / 2)) {
					signal_dupl = 1;
					dprintk(verbose, MB86A16_INFO, 1, "Probably Duplicate Signal, j = %d", j);
				}
			}
			if ((signal_dupl == 0) && (swp_freq > 0) && (ABS(swp_freq - state->frequency * 1000) < fcp + state->srate / 6)) {
				dprintk(verbose, MB86A16_DEBUG, 1, "------ Signal detect ------ [swp_freq=[%07d, srate=%05d]]", swp_freq, state->srate);
				prev_swp_freq[prev_freq_num] = swp_freq;
				prev_freq_num++;
				swp_info_get2(state, state->srate, R, swp_freq,
					      &afcex_freq, &fOSC,
					      &AFCEX_L, &AFCEX_H);

				if (rf_val_set(state, fOSC, state->srate, R) < 0) {
					dprintk(verbose, MB86A16_ERROR, 1, "rf val set error");
					return -1;
				}
				if (afcex_data_set(state, AFCEX_L, AFCEX_H) < 0) {
					dprintk(verbose, MB86A16_ERROR, 1, "afcex data set error");
					return -1;
				}
				signal = signal_det(state, state->srate, &SIG1);
				if (signal == 1) {
					dprintk(verbose, MB86A16_ERROR, 1, "***** Signal Found *****");
					loop = 0;
				} else {
					dprintk(verbose, MB86A16_ERROR, 1, "!!!!! No signal !!!!!, try again...");
					smrt_info_get(state, state->srate);
					if (smrt_set(state, state->srate) < 0) {
						dprintk(verbose, MB86A16_ERROR, 1, "smrt set error");
						return -1;
					}
				}
			}
			if (v > vmax)
				vmax_his = 1 ;
			if (v < vmin)
				vmin_his = 1 ;
			i++;

			if ((i % 2 == 1) && (vmax_his == 1))
				i++;
			if ((i % 2 == 0) && (vmin_his == 1))
				i++;

			if (i % 2 == 1)
				v = (i + 1) / 2;
			else
				v = -i / 2;

			if ((vmax_his == 1) && (vmin_his == 1))
				loop = 0 ;
		}

		if (signal == 1) {
			dprintk(verbose, MB86A16_INFO, 1, " Start Freq Error Check");
			S1T = 7 ;
			S0T = 1 ;
			CREN = 0 ;
			AFCEN = 1 ;
			AFCEXEN = 0 ;

			if (S01T_set(state, S1T, S0T) < 0) {
				dprintk(verbose, MB86A16_ERROR, 1, "S01T set error");
				return -1;
			}
			smrt_info_get(state, state->srate);
			if (smrt_set(state, state->srate) < 0) {
				dprintk(verbose, MB86A16_ERROR, 1, "smrt set error");
				return -1;
			}
			if (EN_set(state, CREN, AFCEN) < 0) {
				dprintk(verbose, MB86A16_ERROR, 1, "EN set error");
				return -1;
			}
			if (AFCEXEN_set(state, AFCEXEN, state->srate) < 0) {
				dprintk(verbose, MB86A16_ERROR, 1, "AFCEXEN set error");
				return -1;
			}
			afcex_info_get(state, afcex_freq, &AFCEX_L, &AFCEX_H);
			if (afcofs_data_set(state, AFCEX_L, AFCEX_H) < 0) {
				dprintk(verbose, MB86A16_ERROR, 1, "AFCOFS data set error");
				return -1;
			}
			if (srst(state) < 0) {
				dprintk(verbose, MB86A16_ERROR, 1, "srst error");
				return -1;
			}
			/* delay 4~200 */
			wait_t = 200000 / state->master_clk + 200000 / state->srate;
			msleep(wait_t);
			afcerr = afcerr_chk(state);
			if (afcerr == -1)
				return -1;

			swp_freq = fOSC * 1000 + afcerr ;
			AFCEXEN = 1 ;
			if (state->srate >= 1500)
				smrt_d = state->srate / 3;
			else
				smrt_d = state->srate / 2;
			smrt_info_get(state, smrt_d);
			if (smrt_set(state, smrt_d) < 0) {
				dprintk(verbose, MB86A16_ERROR, 1, "smrt set error");
				return -1;
			}
			if (AFCEXEN_set(state, AFCEXEN, smrt_d) < 0) {
				dprintk(verbose, MB86A16_ERROR, 1, "AFCEXEN set error");
				return -1;
			}
			R = vco_dev_get(state, smrt_d);
			if (DAGC_data_set(state, 2, 0) < 0) {
				dprintk(verbose, MB86A16_ERROR, 1, "DAGC data set error");
				return -1;
			}
			for (i = 0; i < 3; i++) {
				temp_freq = swp_freq + (i - 1) * state->srate / 8;
				swp_info_get2(state, smrt_d, R, temp_freq, &afcex_freq, &fOSC, &AFCEX_L, &AFCEX_H);
				if (rf_val_set(state, fOSC, smrt_d, R) < 0) {
					dprintk(verbose, MB86A16_ERROR, 1, "rf val set error");
					return -1;
				}
				if (afcex_data_set(state, AFCEX_L, AFCEX_H) < 0) {
					dprintk(verbose, MB86A16_ERROR, 1, "afcex data set error");
					return -1;
				}
				wait_t = 200000 / state->master_clk + 40000 / smrt_d;
				msleep(wait_t);
				dagcm[i] = dagcm_val_get(state);
			}
			if ((dagcm[0] > dagcm[1]) &&
			    (dagcm[0] > dagcm[2]) &&
			    (dagcm[0] - dagcm[1] > 2 * (dagcm[2] - dagcm[1]))) {

				temp_freq = swp_freq - 2 * state->srate / 8;
				swp_info_get2(state, smrt_d, R, temp_freq, &afcex_freq, &fOSC, &AFCEX_L, &AFCEX_H);
				if (rf_val_set(state, fOSC, smrt_d, R) < 0) {
					dprintk(verbose, MB86A16_ERROR, 1, "rf val set error");
					return -1;
				}
				if (afcex_data_set(state, AFCEX_L, AFCEX_H) < 0) {
					dprintk(verbose, MB86A16_ERROR, 1, "afcex data set");
					return -1;
				}
				wait_t = 200000 / state->master_clk + 40000 / smrt_d;
				msleep(wait_t);
				dagcm[3] = dagcm_val_get(state);
				if (dagcm[3] > dagcm[1])
					delta_freq = (dagcm[2] - dagcm[0] + dagcm[1] - dagcm[3]) * state->srate / 300;
				else
					delta_freq = 0;
			} else if ((dagcm[2] > dagcm[1]) &&
				   (dagcm[2] > dagcm[0]) &&
				   (dagcm[2] - dagcm[1] > 2 * (dagcm[0] - dagcm[1]))) {

				temp_freq = swp_freq + 2 * state->srate / 8;
				swp_info_get2(state, smrt_d, R, temp_freq, &afcex_freq, &fOSC, &AFCEX_L, &AFCEX_H);
				if (rf_val_set(state, fOSC, smrt_d, R) < 0) {
					dprintk(verbose, MB86A16_ERROR, 1, "rf val set");
					return -1;
				}
				if (afcex_data_set(state, AFCEX_L, AFCEX_H) < 0) {
					dprintk(verbose, MB86A16_ERROR, 1, "afcex data set");
					return -1;
				}
				wait_t = 200000 / state->master_clk + 40000 / smrt_d;
				msleep(wait_t);
				dagcm[3] = dagcm_val_get(state);
				if (dagcm[3] > dagcm[1])
					delta_freq = (dagcm[2] - dagcm[0] + dagcm[3] - dagcm[1]) * state->srate / 300;
				else
					delta_freq = 0 ;

			} else {
				delta_freq = 0 ;
			}
			dprintk(verbose, MB86A16_INFO, 1, "SWEEP Frequency = %d", swp_freq);
			swp_freq += delta_freq;
			dprintk(verbose, MB86A16_INFO, 1, "Adjusting .., DELTA Freq = %d, SWEEP Freq=%d", delta_freq, swp_freq);
			if (ABS(state->frequency * 1000 - swp_freq) > 3800) {
				dprintk(verbose, MB86A16_INFO, 1, "NO  --  SIGNAL !");
			} else {

				S1T = 0;
				S0T = 3;
				CREN = 1;
				AFCEN = 0;
				AFCEXEN = 1;

				if (S01T_set(state, S1T, S0T) < 0) {
					dprintk(verbose, MB86A16_ERROR, 1, "S01T set error");
					return -1;
				}
				if (DAGC_data_set(state, 0, 0) < 0) {
					dprintk(verbose, MB86A16_ERROR, 1, "DAGC data set error");
					return -1;
				}
				R = vco_dev_get(state, state->srate);
				smrt_info_get(state, state->srate);
				if (smrt_set(state, state->srate) < 0) {
					dprintk(verbose, MB86A16_ERROR, 1, "smrt set error");
					return -1;
				}
				if (EN_set(state, CREN, AFCEN) < 0) {
					dprintk(verbose, MB86A16_ERROR, 1, "EN set error");
					return -1;
				}
				if (AFCEXEN_set(state, AFCEXEN, state->srate) < 0) {
					dprintk(verbose, MB86A16_ERROR, 1, "AFCEXEN set error");
					return -1;
				}
				swp_info_get2(state, state->srate, R, swp_freq, &afcex_freq, &fOSC, &AFCEX_L, &AFCEX_H);
				if (rf_val_set(state, fOSC, state->srate, R) < 0) {
					dprintk(verbose, MB86A16_ERROR, 1, "rf val set error");
					return -1;
				}
				if (afcex_data_set(state, AFCEX_L, AFCEX_H) < 0) {
					dprintk(verbose, MB86A16_ERROR, 1, "afcex data set error");
					return -1;
				}
				if (srst(state) < 0) {
					dprintk(verbose, MB86A16_ERROR, 1, "srst error");
					return -1;
				}
				wait_t = 7 + (10000 + state->srate / 2) / state->srate;
				if (wait_t == 0)
					wait_t = 1;
				msleep_interruptible(wait_t);
				if (mb86a16_read(state, 0x37, &SIG1) != 2) {
					dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
					return -EREMOTEIO;
				}

				if (SIG1 > 110) {
					S2T = 4; S4T = 1; S5T = 6; ETH = 4; VIA = 6;
					wait_t = 7 + (917504 + state->srate / 2) / state->srate;
				} else if (SIG1 > 105) {
					S2T = 4; S4T = 2; S5T = 8; ETH = 7; VIA = 2;
					wait_t = 7 + (1048576 + state->srate / 2) / state->srate;
				} else if (SIG1 > 85) {
					S2T = 5; S4T = 2; S5T = 8; ETH = 7; VIA = 2;
					wait_t = 7 + (1310720 + state->srate / 2) / state->srate;
				} else if (SIG1 > 65) {
					S2T = 6; S4T = 2; S5T = 8; ETH = 7; VIA = 2;
					wait_t = 7 + (1572864 + state->srate / 2) / state->srate;
				} else {
					S2T = 7; S4T = 2; S5T = 8; ETH = 7; VIA = 2;
					wait_t = 7 + (2097152 + state->srate / 2) / state->srate;
				}
				wait_t *= 2; /* FOS */
				S2T_set(state, S2T);
				S45T_set(state, S4T, S5T);
				Vi_set(state, ETH, VIA);
				srst(state);
				msleep_interruptible(wait_t);
				sync = sync_chk(state, &VIRM);
				dprintk(verbose, MB86A16_INFO, 1, "-------- Viterbi=[%d] SYNC=[%d] ---------", VIRM, sync);
				if (VIRM) {
					if (VIRM == 4) {
						/* 5/6 */
						if (SIG1 > 110)
							wait_t = (786432 + state->srate / 2) / state->srate;
						else
							wait_t = (1572864 + state->srate / 2) / state->srate;
						if (state->srate < 5000)
							/* FIXME ! , should be a long wait ! */
							msleep_interruptible(wait_t);
						else
							msleep_interruptible(wait_t);

						if (sync_chk(state, &junk) == 0) {
							iq_vt_set(state, 1);
							FEC_srst(state);
						}
					}
					/* 1/2, 2/3, 3/4, 7/8 */
					if (SIG1 > 110)
						wait_t = (786432 + state->srate / 2) / state->srate;
					else
						wait_t = (1572864 + state->srate / 2) / state->srate;
					msleep_interruptible(wait_t);
					SEQ_set(state, 1);
				} else {
					dprintk(verbose, MB86A16_INFO, 1, "NO  -- SYNC");
					SEQ_set(state, 1);
					ret = -1;
				}
			}
		} else {
			dprintk(verbose, MB86A16_INFO, 1, "NO  -- SIGNAL");
			ret = -1;
		}

		sync = sync_chk(state, &junk);
		if (sync) {
			dprintk(verbose, MB86A16_INFO, 1, "******* SYNC *******");
			freqerr_chk(state, state->frequency, state->srate, 1);
			ret = 0;
			break;
		}
	}

	mb86a16_read(state, 0x15, &agcval);
	mb86a16_read(state, 0x26, &cnmval);
	dprintk(verbose, MB86A16_INFO, 1, "AGC = %02x CNM = %02x", agcval, cnmval);

	return ret;
}

static int mb86a16_send_diseqc_msg(struct dvb_frontend *fe,
				   struct dvb_diseqc_master_cmd *cmd)
{
	struct mb86a16_state *state = fe->demodulator_priv;
	int i;
	u8 regs;

	if (mb86a16_write(state, MB86A16_DCC1, MB86A16_DCC1_DISTA) < 0)
		goto err;
	if (mb86a16_write(state, MB86A16_DCCOUT, 0x00) < 0)
		goto err;
	if (mb86a16_write(state, MB86A16_TONEOUT2, 0x04) < 0)
		goto err;

	regs = 0x18;

	if (cmd->msg_len > 5 || cmd->msg_len < 4)
		return -EINVAL;

	for (i = 0; i < cmd->msg_len; i++) {
		if (mb86a16_write(state, regs, cmd->msg[i]) < 0)
			goto err;

		regs++;
	}
	i += 0x90;

	msleep_interruptible(10);

	if (mb86a16_write(state, MB86A16_DCC1, i) < 0)
		goto err;
	if (mb86a16_write(state, MB86A16_DCCOUT, MB86A16_DCCOUT_DISEN) < 0)
		goto err;

	return 0;

err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
	return -EREMOTEIO;
}

static int mb86a16_send_diseqc_burst(struct dvb_frontend *fe, fe_sec_mini_cmd_t burst)
{
	struct mb86a16_state *state = fe->demodulator_priv;

	switch (burst) {
	case SEC_MINI_A:
		if (mb86a16_write(state, MB86A16_DCC1, MB86A16_DCC1_DISTA |
						       MB86A16_DCC1_TBEN  |
						       MB86A16_DCC1_TBO) < 0)
			goto err;
		if (mb86a16_write(state, MB86A16_DCCOUT, MB86A16_DCCOUT_DISEN) < 0)
			goto err;
		break;
	case SEC_MINI_B:
		if (mb86a16_write(state, MB86A16_DCC1, MB86A16_DCC1_DISTA |
						       MB86A16_DCC1_TBEN) < 0)
			goto err;
		if (mb86a16_write(state, MB86A16_DCCOUT, MB86A16_DCCOUT_DISEN) < 0)
			goto err;
		break;
	}

	return 0;
err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
	return -EREMOTEIO;
}

static int mb86a16_set_tone(struct dvb_frontend *fe, fe_sec_tone_mode_t tone)
{
	struct mb86a16_state *state = fe->demodulator_priv;

	switch (tone) {
	case SEC_TONE_ON:
		if (mb86a16_write(state, MB86A16_TONEOUT2, 0x00) < 0)
			goto err;
		if (mb86a16_write(state, MB86A16_DCC1, MB86A16_DCC1_DISTA |
						       MB86A16_DCC1_CTOE) < 0)

			goto err;
		if (mb86a16_write(state, MB86A16_DCCOUT, MB86A16_DCCOUT_DISEN) < 0)
			goto err;
		break;
	case SEC_TONE_OFF:
		if (mb86a16_write(state, MB86A16_TONEOUT2, 0x04) < 0)
			goto err;
		if (mb86a16_write(state, MB86A16_DCC1, MB86A16_DCC1_DISTA) < 0)
			goto err;
		if (mb86a16_write(state, MB86A16_DCCOUT, 0x00) < 0)
			goto err;
		break;
	default:
		return -EINVAL;
	}
	return 0;

err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
	return -EREMOTEIO;
}

static enum dvbfe_search mb86a16_search(struct dvb_frontend *fe,
					struct dvb_frontend_parameters *p)
{
	struct mb86a16_state *state = fe->demodulator_priv;

	state->frequency = p->frequency / 1000;
	state->srate = p->u.qpsk.symbol_rate / 1000;

	if (!mb86a16_set_fe(state)) {
		dprintk(verbose, MB86A16_ERROR, 1, "Succesfully acquired LOCK");
		return DVBFE_ALGO_SEARCH_SUCCESS;
	}

	dprintk(verbose, MB86A16_ERROR, 1, "Lock acquisition failed!");
	return DVBFE_ALGO_SEARCH_FAILED;
}

static void mb86a16_release(struct dvb_frontend *fe)
{
	struct mb86a16_state *state = fe->demodulator_priv;
	kfree(state);
}

static int mb86a16_init(struct dvb_frontend *fe)
{
	return 0;
}

static int mb86a16_sleep(struct dvb_frontend *fe)
{
	return 0;
}

static int mb86a16_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	u8 ber_mon, ber_tab, ber_lsb, ber_mid, ber_msb, ber_tim, ber_rst;
	u32 timer;

	struct mb86a16_state *state = fe->demodulator_priv;

	*ber = 0;
	if (mb86a16_read(state, MB86A16_BERMON, &ber_mon) != 2)
		goto err;
	if (mb86a16_read(state, MB86A16_BERTAB, &ber_tab) != 2)
		goto err;
	if (mb86a16_read(state, MB86A16_BERLSB, &ber_lsb) != 2)
		goto err;
	if (mb86a16_read(state, MB86A16_BERMID, &ber_mid) != 2)
		goto err;
	if (mb86a16_read(state, MB86A16_BERMSB, &ber_msb) != 2)
		goto err;
	/* BER monitor invalid when BER_EN = 0	*/
	if (ber_mon & 0x04) {
		/* coarse, fast calculation	*/
		*ber = ber_tab & 0x1f;
		dprintk(verbose, MB86A16_DEBUG, 1, "BER coarse=[0x%02x]", *ber);
		if (ber_mon & 0x01) {
			/*
			 * BER_SEL = 1, The monitored BER is the estimated
			 * value with a Reed-Solomon decoder error amount at
			 * the deinterleaver output.
			 * monitored BER is expressed as a 20 bit output in total
			 */
			ber_rst = ber_mon >> 3;
			*ber = (((ber_msb << 8) | ber_mid) << 8) | ber_lsb;
			if (ber_rst == 0)
				timer =  12500000;
			if (ber_rst == 1)
				timer =  25000000;
			if (ber_rst == 2)
				timer =  50000000;
			if (ber_rst == 3)
				timer = 100000000;

			*ber /= timer;
			dprintk(verbose, MB86A16_DEBUG, 1, "BER fine=[0x%02x]", *ber);
		} else {
			/*
			 * BER_SEL = 0, The monitored BER is the estimated
			 * value with a Viterbi decoder error amount at the
			 * QPSK demodulator output.
			 * monitored BER is expressed as a 24 bit output in total
			 */
			ber_tim = ber_mon >> 1;
			*ber = (((ber_msb << 8) | ber_mid) << 8) | ber_lsb;
			if (ber_tim == 0)
				timer = 16;
			if (ber_tim == 1)
				timer = 24;

			*ber /= 2 ^ timer;
			dprintk(verbose, MB86A16_DEBUG, 1, "BER fine=[0x%02x]", *ber);
		}
	}
	return 0;
err:
	dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
	return -EREMOTEIO;
}

static int mb86a16_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	u8 agcm = 0;
	struct mb86a16_state *state = fe->demodulator_priv;

	*strength = 0;
	if (mb86a16_read(state, MB86A16_AGCM, &agcm) != 2) {
		dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
		return -EREMOTEIO;
	}

	*strength = ((0xff - agcm) * 100) / 256;
	dprintk(verbose, MB86A16_DEBUG, 1, "Signal strength=[%d %%]", (u8) *strength);
	*strength = (0xffff - 0xff) + agcm;

	return 0;
}

struct cnr {
	u8 cn_reg;
	u8 cn_val;
};

static const struct cnr cnr_tab[] = {
	{  35,  2 },
	{  40,  3 },
	{  50,  4 },
	{  60,  5 },
	{  70,  6 },
	{  80,  7 },
	{  92,  8 },
	{ 103,  9 },
	{ 115, 10 },
	{ 138, 12 },
	{ 162, 15 },
	{ 180, 18 },
	{ 185, 19 },
	{ 189, 20 },
	{ 195, 22 },
	{ 199, 24 },
	{ 201, 25 },
	{ 202, 26 },
	{ 203, 27 },
	{ 205, 28 },
	{ 208, 30 }
};

static int mb86a16_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct mb86a16_state *state = fe->demodulator_priv;
	int i = 0;
	int low_tide = 2, high_tide = 30, q_level;
	u8  cn;

	*snr = 0;
	if (mb86a16_read(state, 0x26, &cn) != 2) {
		dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
		return -EREMOTEIO;
	}

	for (i = 0; i < ARRAY_SIZE(cnr_tab); i++) {
		if (cn < cnr_tab[i].cn_reg) {
			*snr = cnr_tab[i].cn_val;
			break;
		}
	}
	q_level = (*snr * 100) / (high_tide - low_tide);
	dprintk(verbose, MB86A16_ERROR, 1, "SNR (Quality) = [%d dB], Level=%d %%", *snr, q_level);
	*snr = (0xffff - 0xff) + *snr;

	return 0;
}

static int mb86a16_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	u8 dist;
	struct mb86a16_state *state = fe->demodulator_priv;

	if (mb86a16_read(state, MB86A16_DISTMON, &dist) != 2) {
		dprintk(verbose, MB86A16_ERROR, 1, "I2C transfer error");
		return -EREMOTEIO;
	}
	*ucblocks = dist;

	return 0;
}

static enum dvbfe_algo mb86a16_frontend_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_CUSTOM;
}

static struct dvb_frontend_ops mb86a16_ops = {
	.info = {
		.name			= "Fujitsu MB86A16 DVB-S",
		.type			= FE_QPSK,
		.frequency_min		= 950000,
		.frequency_max		= 2150000,
		.frequency_stepsize	= 3000,
		.frequency_tolerance	= 0,
		.symbol_rate_min	= 1000000,
		.symbol_rate_max	= 45000000,
		.symbol_rate_tolerance	= 500,
		.caps			= FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 |
					  FE_CAN_FEC_3_4 | FE_CAN_FEC_5_6 |
					  FE_CAN_FEC_7_8 | FE_CAN_QPSK    |
					  FE_CAN_FEC_AUTO
	},
	.release			= mb86a16_release,

	.get_frontend_algo		= mb86a16_frontend_algo,
	.search				= mb86a16_search,
	.read_status			= mb86a16_read_status,
	.init				= mb86a16_init,
	.sleep				= mb86a16_sleep,
	.read_status			= mb86a16_read_status,

	.read_ber			= mb86a16_read_ber,
	.read_signal_strength		= mb86a16_read_signal_strength,
	.read_snr			= mb86a16_read_snr,
	.read_ucblocks			= mb86a16_read_ucblocks,

	.diseqc_send_master_cmd		= mb86a16_send_diseqc_msg,
	.diseqc_send_burst		= mb86a16_send_diseqc_burst,
	.set_tone			= mb86a16_set_tone,
};

struct dvb_frontend *mb86a16_attach(const struct mb86a16_config *config,
				    struct i2c_adapter *i2c_adap)
{
	u8 dev_id = 0;
	struct mb86a16_state *state = NULL;

	state = kmalloc(sizeof(struct mb86a16_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	state->config = config;
	state->i2c_adap = i2c_adap;

	mb86a16_read(state, 0x7f, &dev_id);
	if (dev_id != 0xfe)
		goto error;

	memcpy(&state->frontend.ops, &mb86a16_ops, sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	state->frontend.ops.set_voltage = state->config->set_voltage;

	return &state->frontend;
error:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(mb86a16_attach);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Manu Abraham");

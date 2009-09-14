/* DVB compliant Linux driver for the DVB-S si2109/2110 demodulator
*
* Copyright (C) 2008 Igor M. Liplianin (liplianin@me.by)
*
*	This program is free software; you can redistribute it and/or modify
*	it under the terms of the GNU General Public License as published by
*	the Free Software Foundation; either version 2 of the License, or
*	(at your option) any later version.
*
*/
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <asm/div64.h>

#include "dvb_frontend.h"
#include "si21xx.h"

#define	REVISION_REG			0x00
#define	SYSTEM_MODE_REG			0x01
#define	TS_CTRL_REG_1			0x02
#define	TS_CTRL_REG_2			0x03
#define	PIN_CTRL_REG_1			0x04
#define	PIN_CTRL_REG_2			0x05
#define	LOCK_STATUS_REG_1		0x0f
#define	LOCK_STATUS_REG_2		0x10
#define	ACQ_STATUS_REG			0x11
#define	ACQ_CTRL_REG_1			0x13
#define	ACQ_CTRL_REG_2			0x14
#define	PLL_DIVISOR_REG			0x15
#define	COARSE_TUNE_REG			0x16
#define	FINE_TUNE_REG_L			0x17
#define	FINE_TUNE_REG_H			0x18

#define	ANALOG_AGC_POWER_LEVEL_REG	0x28
#define	CFO_ESTIMATOR_CTRL_REG_1	0x29
#define	CFO_ESTIMATOR_CTRL_REG_2	0x2a
#define	CFO_ESTIMATOR_CTRL_REG_3	0x2b

#define	SYM_RATE_ESTIMATE_REG_L		0x31
#define	SYM_RATE_ESTIMATE_REG_M		0x32
#define	SYM_RATE_ESTIMATE_REG_H		0x33

#define	CFO_ESTIMATOR_OFFSET_REG_L	0x36
#define	CFO_ESTIMATOR_OFFSET_REG_H	0x37
#define	CFO_ERROR_REG_L			0x38
#define	CFO_ERROR_REG_H			0x39
#define	SYM_RATE_ESTIMATOR_CTRL_REG	0x3a

#define	SYM_RATE_REG_L			0x3f
#define	SYM_RATE_REG_M			0x40
#define	SYM_RATE_REG_H			0x41
#define	SYM_RATE_ESTIMATOR_MAXIMUM_REG	0x42
#define	SYM_RATE_ESTIMATOR_MINIMUM_REG	0x43

#define	C_N_ESTIMATOR_CTRL_REG		0x7c
#define	C_N_ESTIMATOR_THRSHLD_REG	0x7d
#define	C_N_ESTIMATOR_LEVEL_REG_L	0x7e
#define	C_N_ESTIMATOR_LEVEL_REG_H	0x7f

#define	BLIND_SCAN_CTRL_REG		0x80

#define	LSA_CTRL_REG_1			0x8D
#define	SPCTRM_TILT_CORR_THRSHLD_REG	0x8f
#define	ONE_DB_BNDWDTH_THRSHLD_REG	0x90
#define	TWO_DB_BNDWDTH_THRSHLD_REG	0x91
#define	THREE_DB_BNDWDTH_THRSHLD_REG	0x92
#define	INBAND_POWER_THRSHLD_REG	0x93
#define	REF_NOISE_LVL_MRGN_THRSHLD_REG	0x94

#define	VIT_SRCH_CTRL_REG_1		0xa0
#define	VIT_SRCH_CTRL_REG_2		0xa1
#define	VIT_SRCH_CTRL_REG_3		0xa2
#define	VIT_SRCH_STATUS_REG		0xa3
#define	VITERBI_BER_COUNT_REG_L		0xab
#define	REED_SOLOMON_CTRL_REG		0xb0
#define	REED_SOLOMON_ERROR_COUNT_REG_L	0xb1
#define	PRBS_CTRL_REG			0xb5

#define	LNB_CTRL_REG_1			0xc0
#define	LNB_CTRL_REG_2			0xc1
#define	LNB_CTRL_REG_3			0xc2
#define	LNB_CTRL_REG_4			0xc3
#define	LNB_CTRL_STATUS_REG		0xc4
#define	LNB_FIFO_REGS_0			0xc5
#define	LNB_FIFO_REGS_1			0xc6
#define	LNB_FIFO_REGS_2			0xc7
#define	LNB_FIFO_REGS_3			0xc8
#define	LNB_FIFO_REGS_4			0xc9
#define	LNB_FIFO_REGS_5			0xca
#define	LNB_SUPPLY_CTRL_REG_1		0xcb
#define	LNB_SUPPLY_CTRL_REG_2		0xcc
#define	LNB_SUPPLY_CTRL_REG_3		0xcd
#define	LNB_SUPPLY_CTRL_REG_4		0xce
#define	LNB_SUPPLY_STATUS_REG		0xcf

#define FALSE	0
#define TRUE	1
#define FAIL	-1
#define PASS	0

#define ALLOWABLE_FS_COUNT	10
#define STATUS_BER		0
#define STATUS_UCBLOCKS		1

static int debug;
#define dprintk(args...) \
	do { \
		if (debug) \
			printk(KERN_DEBUG "si21xx: " args); \
	} while (0)

enum {
	ACTIVE_HIGH,
	ACTIVE_LOW
};
enum {
	BYTE_WIDE,
	BIT_WIDE
};
enum {
	CLK_GAPPED_MODE,
	CLK_CONTINUOUS_MODE
};
enum {
	RISING_EDGE,
	FALLING_EDGE
};
enum {
	MSB_FIRST,
	LSB_FIRST
};
enum {
	SERIAL,
	PARALLEL
};

struct si21xx_state {
	struct i2c_adapter *i2c;
	const struct si21xx_config *config;
	struct dvb_frontend frontend;
	u8 initialised:1;
	int errmode;
	int fs;			/*Sampling rate of the ADC in MHz*/
};

/*	register default initialization */
static u8 serit_sp1511lhb_inittab[] = {
	0x01, 0x28,	/* set i2c_inc_disable */
	0x20, 0x03,
	0x27, 0x20,
	0xe0, 0x45,
	0xe1, 0x08,
	0xfe, 0x01,
	0x01, 0x28,
	0x89, 0x09,
	0x04, 0x80,
	0x05, 0x01,
	0x06, 0x00,
	0x20, 0x03,
	0x24, 0x88,
	0x29, 0x09,
	0x2a, 0x0f,
	0x2c, 0x10,
	0x2d, 0x19,
	0x2e, 0x08,
	0x2f, 0x10,
	0x30, 0x19,
	0x34, 0x20,
	0x35, 0x03,
	0x45, 0x02,
	0x46, 0x45,
	0x47, 0xd0,
	0x48, 0x00,
	0x49, 0x40,
	0x4a, 0x03,
	0x4c, 0xfd,
	0x4f, 0x2e,
	0x50, 0x2e,
	0x51, 0x10,
	0x52, 0x10,
	0x56, 0x92,
	0x59, 0x00,
	0x5a, 0x2d,
	0x5b, 0x33,
	0x5c, 0x1f,
	0x5f, 0x76,
	0x62, 0xc0,
	0x63, 0xc0,
	0x64, 0xf3,
	0x65, 0xf3,
	0x79, 0x40,
	0x6a, 0x40,
	0x6b, 0x0a,
	0x6c, 0x80,
	0x6d, 0x27,
	0x71, 0x06,
	0x75, 0x60,
	0x78, 0x00,
	0x79, 0xb5,
	0x7c, 0x05,
	0x7d, 0x1a,
	0x87, 0x55,
	0x88, 0x72,
	0x8f, 0x08,
	0x90, 0xe0,
	0x94, 0x40,
	0xa0, 0x3f,
	0xa1, 0xc0,
	0xa4, 0xcc,
	0xa5, 0x66,
	0xa6, 0x66,
	0xa7, 0x7b,
	0xa8, 0x7b,
	0xa9, 0x7b,
	0xaa, 0x9a,
	0xed, 0x04,
	0xad, 0x00,
	0xae, 0x03,
	0xcc, 0xab,
	0x01, 0x08,
	0xff, 0xff
};

/*	low level read/writes */
static int si21_writeregs(struct si21xx_state *state, u8 reg1,
							u8 *data, int len)
{
	int ret;
	u8 buf[60];/* = { reg1, data };*/
	struct i2c_msg msg = {
				.addr = state->config->demod_address,
				.flags = 0,
				.buf = buf,
				.len = len + 1
	};

	msg.buf[0] =  reg1;
	memcpy(msg.buf + 1, data, len);

	ret = i2c_transfer(state->i2c, &msg, 1);

	if (ret != 1)
		dprintk("%s: writereg error (reg1 == 0x%02x, data == 0x%02x, "
			"ret == %i)\n", __func__, reg1, data[0], ret);

	return (ret != 1) ? -EREMOTEIO : 0;
}

static int si21_writereg(struct si21xx_state *state, u8 reg, u8 data)
{
	int ret;
	u8 buf[] = { reg, data };
	struct i2c_msg msg = {
				.addr = state->config->demod_address,
				.flags = 0,
				.buf = buf,
				.len = 2
	};

	ret = i2c_transfer(state->i2c, &msg, 1);

	if (ret != 1)
		dprintk("%s: writereg error (reg == 0x%02x, data == 0x%02x, "
			"ret == %i)\n", __func__, reg, data, ret);

	return (ret != 1) ? -EREMOTEIO : 0;
}

static int si21_write(struct dvb_frontend *fe, u8 *buf, int len)
{
	struct si21xx_state *state = fe->demodulator_priv;

	if (len != 2)
		return -EINVAL;

	return si21_writereg(state, buf[0], buf[1]);
}

static u8 si21_readreg(struct si21xx_state *state, u8 reg)
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

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2)
		dprintk("%s: readreg error (reg == 0x%02x, ret == %i)\n",
			__func__, reg, ret);

	return b1[0];
}

static int si21_readregs(struct si21xx_state *state, u8 reg1, u8 *b, u8 len)
{
	int ret;
	struct i2c_msg msg[] = {
		{
			.addr = state->config->demod_address,
			.flags = 0,
			.buf = &reg1,
			.len = 1
		}, {
			.addr = state->config->demod_address,
			.flags = I2C_M_RD,
			.buf = b,
			.len = len
		}
	};

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2)
		dprintk("%s: readreg error (ret == %i)\n", __func__, ret);

	return ret == 2 ? 0 : -1;
}

static int si21xx_wait_diseqc_idle(struct si21xx_state *state, int timeout)
{
	unsigned long start = jiffies;

	dprintk("%s\n", __func__);

	while ((si21_readreg(state, LNB_CTRL_REG_1) & 0x8) == 8) {
		if (jiffies - start > timeout) {
			dprintk("%s: timeout!!\n", __func__);
			return -ETIMEDOUT;
		}
		msleep(10);
	};

	return 0;
}

static int si21xx_set_symbolrate(struct dvb_frontend *fe, u32 srate)
{
	struct si21xx_state *state = fe->demodulator_priv;
	u32 sym_rate, data_rate;
	int i;
	u8 sym_rate_bytes[3];

	dprintk("%s : srate = %i\n", __func__ , srate);

	if ((srate < 1000000) || (srate > 45000000))
		return -EINVAL;

	data_rate = srate;
	sym_rate = 0;

	for (i = 0; i < 4; ++i) {
		sym_rate /= 100;
		sym_rate = sym_rate + ((data_rate % 100) * 0x800000) /
								state->fs;
		data_rate /= 100;
	}
	for (i = 0; i < 3; ++i)
		sym_rate_bytes[i] = (u8)((sym_rate >> (i * 8)) & 0xff);

	si21_writeregs(state, SYM_RATE_REG_L, sym_rate_bytes, 0x03);

	return 0;
}

static int si21xx_send_diseqc_msg(struct dvb_frontend *fe,
					struct dvb_diseqc_master_cmd *m)
{
	struct si21xx_state *state = fe->demodulator_priv;
	u8 lnb_status;
	u8 LNB_CTRL_1;
	int status;

	dprintk("%s\n", __func__);

	status = PASS;
	LNB_CTRL_1 = 0;

	status |= si21_readregs(state, LNB_CTRL_STATUS_REG, &lnb_status, 0x01);
	status |= si21_readregs(state, LNB_CTRL_REG_1, &lnb_status, 0x01);

	/*fill the FIFO*/
	status |= si21_writeregs(state, LNB_FIFO_REGS_0, m->msg, m->msg_len);

	LNB_CTRL_1 = (lnb_status & 0x70);
	LNB_CTRL_1 |= m->msg_len;

	LNB_CTRL_1 |= 0x80;	/* begin LNB signaling */

	status |= si21_writeregs(state, LNB_CTRL_REG_1, &LNB_CTRL_1, 0x01);

	return status;
}

static int si21xx_send_diseqc_burst(struct dvb_frontend *fe,
						fe_sec_mini_cmd_t burst)
{
	struct si21xx_state *state = fe->demodulator_priv;
	u8 val;

	dprintk("%s\n", __func__);

	if (si21xx_wait_diseqc_idle(state, 100) < 0)
		return -ETIMEDOUT;

	val = (0x80 | si21_readreg(state, 0xc1));
	if (si21_writereg(state, LNB_CTRL_REG_1,
			burst == SEC_MINI_A ? (val & ~0x10) : (val | 0x10)))
		return -EREMOTEIO;

	if (si21xx_wait_diseqc_idle(state, 100) < 0)
		return -ETIMEDOUT;

	if (si21_writereg(state, LNB_CTRL_REG_1, val))
		return -EREMOTEIO;

	return 0;
}
/*	30.06.2008 */
static int si21xx_set_tone(struct dvb_frontend *fe, fe_sec_tone_mode_t tone)
{
	struct si21xx_state *state = fe->demodulator_priv;
	u8 val;

	dprintk("%s\n", __func__);
	val = (0x80 | si21_readreg(state, LNB_CTRL_REG_1));

	switch (tone) {
	case SEC_TONE_ON:
		return si21_writereg(state, LNB_CTRL_REG_1, val | 0x20);

	case SEC_TONE_OFF:
		return si21_writereg(state, LNB_CTRL_REG_1, (val & ~0x20));

	default:
		return -EINVAL;
	}
}

static int si21xx_set_voltage(struct dvb_frontend *fe, fe_sec_voltage_t volt)
{
	struct si21xx_state *state = fe->demodulator_priv;

	u8 val;
	dprintk("%s: %s\n", __func__,
		volt == SEC_VOLTAGE_13 ? "SEC_VOLTAGE_13" :
		volt == SEC_VOLTAGE_18 ? "SEC_VOLTAGE_18" : "??");


	val = (0x80 | si21_readreg(state, LNB_CTRL_REG_1));

	switch (volt) {
	case SEC_VOLTAGE_18:
		return si21_writereg(state, LNB_CTRL_REG_1, val | 0x40);
		break;
	case SEC_VOLTAGE_13:
		return si21_writereg(state, LNB_CTRL_REG_1, (val & ~0x40));
		break;
	default:
		return -EINVAL;
	};
}

static int si21xx_init(struct dvb_frontend *fe)
{
	struct si21xx_state *state = fe->demodulator_priv;
	int i;
	int status = 0;
	u8 reg1;
	u8 val;
	u8 reg2[2];

	dprintk("%s\n", __func__);

	for (i = 0; ; i += 2) {
		reg1 = serit_sp1511lhb_inittab[i];
		val = serit_sp1511lhb_inittab[i+1];
		if (reg1 == 0xff && val == 0xff)
			break;
		si21_writeregs(state, reg1, &val, 1);
	}

	/*DVB QPSK SYSTEM MODE REG*/
	reg1 = 0x08;
	si21_writeregs(state, SYSTEM_MODE_REG, &reg1, 0x01);

	/*transport stream config*/
	/*
	mode = PARALLEL;
	sdata_form = LSB_FIRST;
	clk_edge = FALLING_EDGE;
	clk_mode = CLK_GAPPED_MODE;
	strt_len = BYTE_WIDE;
	sync_pol = ACTIVE_HIGH;
	val_pol = ACTIVE_HIGH;
	err_pol = ACTIVE_HIGH;
	sclk_rate = 0x00;
	parity = 0x00 ;
	data_delay = 0x00;
	clk_delay = 0x00;
	pclk_smooth = 0x00;
	*/
	reg2[0] =
		PARALLEL + (LSB_FIRST << 1)
		+ (FALLING_EDGE << 2) + (CLK_GAPPED_MODE << 3)
		+ (BYTE_WIDE << 4) + (ACTIVE_HIGH << 5)
		+ (ACTIVE_HIGH << 6) + (ACTIVE_HIGH << 7);

	reg2[1] = 0;
	/*	sclk_rate + (parity << 2)
		+ (data_delay << 3) + (clk_delay << 4)
		+ (pclk_smooth << 5);
	*/
	status |= si21_writeregs(state, TS_CTRL_REG_1, reg2, 0x02);
	if (status != 0)
		dprintk(" %s : TS Set Error\n", __func__);

	return 0;

}

static int si21_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct si21xx_state *state = fe->demodulator_priv;
	u8 regs_read[2];
	u8 reg_read;
	u8 i;
	u8 lock;
	u8 signal = si21_readreg(state, ANALOG_AGC_POWER_LEVEL_REG);

	si21_readregs(state, LOCK_STATUS_REG_1, regs_read, 0x02);
	reg_read = 0;

	for (i = 0; i < 7; ++i)
		reg_read |= ((regs_read[0] >> i) & 0x01) << (6 - i);

	lock = ((reg_read & 0x7f) | (regs_read[1] & 0x80));

	dprintk("%s : FE_READ_STATUS : VSTATUS: 0x%02x\n", __func__, lock);
	*status = 0;

	if (signal > 10)
		*status |= FE_HAS_SIGNAL;

	if (lock & 0x2)
		*status |= FE_HAS_CARRIER;

	if (lock & 0x20)
		*status |= FE_HAS_VITERBI;

	if (lock & 0x40)
		*status |= FE_HAS_SYNC;

	if ((lock & 0x7b) == 0x7b)
		*status |= FE_HAS_LOCK;

	return 0;
}

static int si21_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct si21xx_state *state = fe->demodulator_priv;

	/*status = si21_readreg(state, ANALOG_AGC_POWER_LEVEL_REG,
						(u8*)agclevel, 0x01);*/

	u16 signal = (3 * si21_readreg(state, 0x27) *
					si21_readreg(state, 0x28));

	dprintk("%s : AGCPWR: 0x%02x%02x, signal=0x%04x\n", __func__,
		si21_readreg(state, 0x27),
		si21_readreg(state, 0x28), (int) signal);

	signal  <<= 4;
	*strength = signal;

	return 0;
}

static int si21_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct si21xx_state *state = fe->demodulator_priv;

	dprintk("%s\n", __func__);

	if (state->errmode != STATUS_BER)
		return 0;

	*ber = (si21_readreg(state, 0x1d) << 8) |
				si21_readreg(state, 0x1e);

	return 0;
}

static int si21_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct si21xx_state *state = fe->demodulator_priv;

	s32 xsnr = 0xffff - ((si21_readreg(state, 0x24) << 8) |
					si21_readreg(state, 0x25));
	xsnr = 3 * (xsnr - 0xa100);
	*snr = (xsnr > 0xffff) ? 0xffff : (xsnr < 0) ? 0 : xsnr;

	dprintk("%s\n", __func__);

	return 0;
}

static int si21_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct si21xx_state *state = fe->demodulator_priv;

	dprintk("%s\n", __func__);

	if (state->errmode != STATUS_UCBLOCKS)
		*ucblocks = 0;
	else
		*ucblocks = (si21_readreg(state, 0x1d) << 8) |
					si21_readreg(state, 0x1e);

	return 0;
}

/*	initiates a channel acquisition sequence
	using the specified symbol rate and code rate */
static int si21xx_setacquire(struct dvb_frontend *fe, int symbrate,
						fe_code_rate_t crate)
{

	struct si21xx_state *state = fe->demodulator_priv;
	u8 coderates[] = {
				0x0, 0x01, 0x02, 0x04, 0x00,
				0x8, 0x10, 0x20, 0x00, 0x3f
	};

	u8 coderate_ptr;
	int status;
	u8 start_acq = 0x80;
	u8 reg, regs[3];

	dprintk("%s\n", __func__);

	status = PASS;
	coderate_ptr = coderates[crate];

	si21xx_set_symbolrate(fe, symbrate);

	/* write code rates to use in the Viterbi search */
	status |= si21_writeregs(state,
				VIT_SRCH_CTRL_REG_1,
				&coderate_ptr, 0x01);

	/* clear acq_start bit */
	status |= si21_readregs(state, ACQ_CTRL_REG_2, &reg, 0x01);
	reg &= ~start_acq;
	status |= si21_writeregs(state, ACQ_CTRL_REG_2, &reg, 0x01);

	/* use new Carrier Frequency Offset Estimator (QuickLock) */
	regs[0] = 0xCB;
	regs[1] = 0x40;
	regs[2] = 0xCB;

	status |= si21_writeregs(state,
				TWO_DB_BNDWDTH_THRSHLD_REG,
				&regs[0], 0x03);
	reg = 0x56;
	status |= si21_writeregs(state,
				LSA_CTRL_REG_1, &reg, 1);
	reg = 0x05;
	status |= si21_writeregs(state,
				BLIND_SCAN_CTRL_REG, &reg, 1);
	/* start automatic acq */
	status |= si21_writeregs(state,
				ACQ_CTRL_REG_2, &start_acq, 0x01);

	return status;
}

static int si21xx_set_property(struct dvb_frontend *fe, struct dtv_property *p)
{
	dprintk("%s(..)\n", __func__);
	return 0;
}

static int si21xx_get_property(struct dvb_frontend *fe, struct dtv_property *p)
{
	dprintk("%s(..)\n", __func__);
	return 0;
}

static int si21xx_set_frontend(struct dvb_frontend *fe,
					struct dvb_frontend_parameters *dfp)
{
	struct si21xx_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	/* freq		Channel carrier frequency in KHz (i.e. 1550000 KHz)
	 datarate	Channel symbol rate in Sps (i.e. 22500000 Sps)*/

	/* in MHz */
	unsigned char coarse_tune_freq;
	int fine_tune_freq;
	unsigned char sample_rate = 0;
	/* boolean */
	unsigned int inband_interferer_ind;

	/* INTERMEDIATE VALUES */
	int icoarse_tune_freq; /* MHz */
	int ifine_tune_freq; /* MHz */
	unsigned int band_high;
	unsigned int band_low;
	unsigned int x1;
	unsigned int x2;
	int i;
	unsigned int inband_interferer_div2[ALLOWABLE_FS_COUNT] = {
			FALSE, FALSE, FALSE, FALSE, FALSE,
			FALSE, FALSE, FALSE, FALSE, FALSE
	};
	unsigned int inband_interferer_div4[ALLOWABLE_FS_COUNT] = {
			FALSE, FALSE, FALSE, FALSE, FALSE,
			FALSE, FALSE, FALSE, FALSE, FALSE
	};

	int status;

	/* allowable sample rates for ADC in MHz */
	int afs[ALLOWABLE_FS_COUNT] = { 200, 192, 193, 194, 195,
					196, 204, 205, 206, 207
	};
	/* in MHz */
	int if_limit_high;
	int if_limit_low;
	int lnb_lo;
	int lnb_uncertanity;

	int rf_freq;
	int data_rate;
	unsigned char regs[4];

	dprintk("%s : FE_SET_FRONTEND\n", __func__);

	if (c->delivery_system != SYS_DVBS) {
			dprintk("%s: unsupported delivery system selected (%d)\n",
				__func__, c->delivery_system);
			return -EOPNOTSUPP;
	}

	for (i = 0; i < ALLOWABLE_FS_COUNT; ++i)
		inband_interferer_div2[i] = inband_interferer_div4[i] = FALSE;

	if_limit_high = -700000;
	if_limit_low = -100000;
	/* in MHz */
	lnb_lo = 0;
	lnb_uncertanity = 0;

	rf_freq = 10 * c->frequency ;
	data_rate = c->symbol_rate / 100;

	status = PASS;

	band_low = (rf_freq - lnb_lo) - ((lnb_uncertanity * 200)
					+ (data_rate * 135)) / 200;

	band_high = (rf_freq - lnb_lo) + ((lnb_uncertanity * 200)
					+ (data_rate * 135)) / 200;


	icoarse_tune_freq = 100000 *
				(((rf_freq - lnb_lo) -
					(if_limit_low + if_limit_high) / 2)
								/ 100000);

	ifine_tune_freq = (rf_freq - lnb_lo) - icoarse_tune_freq ;

	for (i = 0; i < ALLOWABLE_FS_COUNT; ++i) {
		x1 = ((rf_freq - lnb_lo) / (afs[i] * 2500)) *
					(afs[i] * 2500) + afs[i] * 2500;

		x2 = ((rf_freq - lnb_lo) / (afs[i] * 2500)) *
							(afs[i] * 2500);

		if (((band_low < x1) && (x1 < band_high)) ||
					((band_low < x2) && (x2 < band_high)))
					inband_interferer_div4[i] = TRUE;

	}

	for (i = 0; i < ALLOWABLE_FS_COUNT; ++i) {
		x1 = ((rf_freq - lnb_lo) / (afs[i] * 5000)) *
					(afs[i] * 5000) + afs[i] * 5000;

		x2 = ((rf_freq - lnb_lo) / (afs[i] * 5000)) *
					(afs[i] * 5000);

		if (((band_low < x1) && (x1 < band_high)) ||
					((band_low < x2) && (x2 < band_high)))
					inband_interferer_div2[i] = TRUE;
	}

	inband_interferer_ind = TRUE;
	for (i = 0; i < ALLOWABLE_FS_COUNT; ++i)
		inband_interferer_ind &= inband_interferer_div2[i] |
						inband_interferer_div4[i];

	if (inband_interferer_ind) {
		for (i = 0; i < ALLOWABLE_FS_COUNT; ++i) {
			if (inband_interferer_div2[i] == FALSE) {
				sample_rate = (u8) afs[i];
				break;
			}
		}
	} else {
		for (i = 0; i < ALLOWABLE_FS_COUNT; ++i) {
			if ((inband_interferer_div2[i] |
					inband_interferer_div4[i]) == FALSE) {
				sample_rate = (u8) afs[i];
				break;
			}
		}

	}

	if (sample_rate > 207 || sample_rate < 192)
		sample_rate = 200;

	fine_tune_freq = ((0x4000 * (ifine_tune_freq / 10)) /
					((sample_rate) * 1000));

	coarse_tune_freq = (u8)(icoarse_tune_freq / 100000);

	regs[0] = sample_rate;
	regs[1] = coarse_tune_freq;
	regs[2] = fine_tune_freq & 0xFF;
	regs[3] = fine_tune_freq >> 8 & 0xFF;

	status |= si21_writeregs(state, PLL_DIVISOR_REG, &regs[0], 0x04);

	state->fs = sample_rate;/*ADC MHz*/
	si21xx_setacquire(fe, c->symbol_rate, c->fec_inner);

	return 0;
}

static int si21xx_sleep(struct dvb_frontend *fe)
{
	struct si21xx_state *state = fe->demodulator_priv;
	u8 regdata;

	dprintk("%s\n", __func__);

	si21_readregs(state, SYSTEM_MODE_REG, &regdata, 0x01);
	regdata |= 1 << 6;
	si21_writeregs(state, SYSTEM_MODE_REG, &regdata, 0x01);
	state->initialised = 0;

	return 0;
}

static void si21xx_release(struct dvb_frontend *fe)
{
	struct si21xx_state *state = fe->demodulator_priv;

	dprintk("%s\n", __func__);

	kfree(state);
}

static struct dvb_frontend_ops si21xx_ops = {

	.info = {
		.name			= "SL SI21XX DVB-S",
		.type			= FE_QPSK,
		.frequency_min		= 950000,
		.frequency_max		= 2150000,
		.frequency_stepsize	= 125,	 /* kHz for QPSK frontends */
		.frequency_tolerance	= 0,
		.symbol_rate_min	= 1000000,
		.symbol_rate_max	= 45000000,
		.symbol_rate_tolerance	= 500,	/* ppm */
		.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 |
		FE_CAN_QPSK |
		FE_CAN_FEC_AUTO
	},

	.release = si21xx_release,
	.init = si21xx_init,
	.sleep = si21xx_sleep,
	.write = si21_write,
	.read_status = si21_read_status,
	.read_ber = si21_read_ber,
	.read_signal_strength = si21_read_signal_strength,
	.read_snr = si21_read_snr,
	.read_ucblocks = si21_read_ucblocks,
	.diseqc_send_master_cmd = si21xx_send_diseqc_msg,
	.diseqc_send_burst = si21xx_send_diseqc_burst,
	.set_tone = si21xx_set_tone,
	.set_voltage = si21xx_set_voltage,

	.set_property = si21xx_set_property,
	.get_property = si21xx_get_property,
	.set_frontend = si21xx_set_frontend,
};

struct dvb_frontend *si21xx_attach(const struct si21xx_config *config,
						struct i2c_adapter *i2c)
{
	struct si21xx_state *state = NULL;
	int id;

	dprintk("%s\n", __func__);

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct si21xx_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;
	state->initialised = 0;
	state->errmode = STATUS_BER;

	/* check if the demod is there */
	id = si21_readreg(state, SYSTEM_MODE_REG);
	si21_writereg(state, SYSTEM_MODE_REG, id | 0x40); /* standby off */
	msleep(200);
	id = si21_readreg(state, 0x00);

	/* register 0x00 contains:
		0x34 for SI2107
		0x24 for SI2108
		0x14 for SI2109
		0x04 for SI2110
	*/
	if (id != 0x04 && id != 0x14)
		goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &si21xx_ops,
					sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(si21xx_attach);

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("SL SI21XX DVB Demodulator driver");
MODULE_AUTHOR("Igor M. Liplianin");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * stv0367.c
 *
 * Driver for ST STV0367 DVB-T & DVB-C demodulator IC.
 *
 * Copyright (C) ST Microelectronics.
 * Copyright (C) 2010,2011 NetUP Inc.
 * Copyright (C) 2010,2011 Igor M. Liplianin <liplianin@netup.ru>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/i2c.h>

#include <linux/int_log.h>

#include "stv0367.h"
#include "stv0367_defs.h"
#include "stv0367_regs.h"
#include "stv0367_priv.h"

/* Max transfer size done by I2C transfer functions */
#define MAX_XFER_SIZE  64

static int stvdebug;
module_param_named(debug, stvdebug, int, 0644);

static int i2cdebug;
module_param_named(i2c_debug, i2cdebug, int, 0644);

#define dprintk(args...) \
	do { \
		if (stvdebug) \
			printk(KERN_DEBUG args); \
	} while (0)
	/* DVB-C */

enum active_demod_state { demod_none, demod_ter, demod_cab };

struct stv0367cab_state {
	enum stv0367_cab_signal_type	state;
	u32	mclk;
	u32	adc_clk;
	s32	search_range;
	s32	derot_offset;
	/* results */
	int locked;			/* channel found		*/
	u32 freq_khz;			/* found frequency (in kHz)	*/
	u32 symbol_rate;		/* found symbol rate (in Bds)	*/
	enum fe_spectral_inversion spect_inv; /* Spectrum Inversion	*/
	u32 qamfec_status_reg;          /* status reg to poll for FEC Lock */
};

struct stv0367ter_state {
	/* DVB-T */
	enum stv0367_ter_signal_type state;
	enum stv0367_ter_if_iq_mode if_iq_mode;
	enum stv0367_ter_mode mode;/* mode 2K or 8K */
	enum fe_guard_interval guard;
	enum stv0367_ter_hierarchy hierarchy;
	u32 frequency;
	enum fe_spectral_inversion sense; /*  current search spectrum */
	u8  force; /* force mode/guard */
	u8  bw; /* channel width 6, 7 or 8 in MHz */
	u8  pBW; /* channel width used during previous lock */
	u32 pBER;
	u32 pPER;
	u32 ucblocks;
	s8  echo_pos; /* echo position */
	u8  first_lock;
	u8  unlock_counter;
	u32 agc_val;
};

struct stv0367_state {
	struct dvb_frontend fe;
	struct i2c_adapter *i2c;
	/* config settings */
	const struct stv0367_config *config;
	u8 chip_id;
	/* DVB-C */
	struct stv0367cab_state *cab_state;
	/* DVB-T */
	struct stv0367ter_state *ter_state;
	/* flags for operation control */
	u8 use_i2c_gatectrl;
	u8 deftabs;
	u8 reinit_on_setfrontend;
	u8 auto_if_khz;
	enum active_demod_state activedemod;
};

#define RF_LOOKUP_TABLE_SIZE  31
#define RF_LOOKUP_TABLE2_SIZE 16
/* RF Level (for RF AGC->AGC1) Lookup Table, depends on the board and tuner.*/
static const s32 stv0367cab_RF_LookUp1[RF_LOOKUP_TABLE_SIZE][RF_LOOKUP_TABLE_SIZE] = {
	{/*AGC1*/
		48, 50, 51, 53, 54, 56, 57, 58, 60, 61, 62, 63,
		64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75,
		76, 77, 78, 80, 83, 85, 88,
	}, {/*RF(dbm)*/
		22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
		34, 35, 36, 37, 38, 39, 41, 42, 43, 44, 46, 47,
		49, 50, 52, 53, 54, 55, 56,
	}
};
/* RF Level (for IF AGC->AGC2) Lookup Table, depends on the board and tuner.*/
static const s32 stv0367cab_RF_LookUp2[RF_LOOKUP_TABLE2_SIZE][RF_LOOKUP_TABLE2_SIZE] = {
	{/*AGC2*/
		28, 29, 31, 32, 34, 35, 36, 37,
		38, 39, 40, 41, 42, 43, 44, 45,
	}, {/*RF(dbm)*/
		57, 58, 59, 60, 61, 62, 63, 64,
		65, 66, 67, 68, 69, 70, 71, 72,
	}
};

static
int stv0367_writeregs(struct stv0367_state *state, u16 reg, u8 *data, int len)
{
	u8 buf[MAX_XFER_SIZE];
	struct i2c_msg msg = {
		.addr = state->config->demod_address,
		.flags = 0,
		.buf = buf,
		.len = len + 2
	};
	int ret;

	if (2 + len > sizeof(buf)) {
		printk(KERN_WARNING
		       "%s: i2c wr reg=%04x: len=%d is too big!\n",
		       KBUILD_MODNAME, reg, len);
		return -EINVAL;
	}


	buf[0] = MSB(reg);
	buf[1] = LSB(reg);
	memcpy(buf + 2, data, len);

	if (i2cdebug)
		printk(KERN_DEBUG "%s: [%02x] %02x: %02x\n", __func__,
			state->config->demod_address, reg, buf[2]);

	ret = i2c_transfer(state->i2c, &msg, 1);
	if (ret != 1)
		printk(KERN_ERR "%s: i2c write error! ([%02x] %02x: %02x)\n",
			__func__, state->config->demod_address, reg, buf[2]);

	return (ret != 1) ? -EREMOTEIO : 0;
}

static int stv0367_writereg(struct stv0367_state *state, u16 reg, u8 data)
{
	u8 tmp = data; /* see gcc.gnu.org/bugzilla/show_bug.cgi?id=81715 */

	return stv0367_writeregs(state, reg, &tmp, 1);
}

static u8 stv0367_readreg(struct stv0367_state *state, u16 reg)
{
	u8 b0[] = { 0, 0 };
	u8 b1[] = { 0 };
	struct i2c_msg msg[] = {
		{
			.addr = state->config->demod_address,
			.flags = 0,
			.buf = b0,
			.len = 2
		}, {
			.addr = state->config->demod_address,
			.flags = I2C_M_RD,
			.buf = b1,
			.len = 1
		}
	};
	int ret;

	b0[0] = MSB(reg);
	b0[1] = LSB(reg);

	ret = i2c_transfer(state->i2c, msg, 2);
	if (ret != 2)
		printk(KERN_ERR "%s: i2c read error ([%02x] %02x: %02x)\n",
			__func__, state->config->demod_address, reg, b1[0]);

	if (i2cdebug)
		printk(KERN_DEBUG "%s: [%02x] %02x: %02x\n", __func__,
			state->config->demod_address, reg, b1[0]);

	return b1[0];
}

static void extract_mask_pos(u32 label, u8 *mask, u8 *pos)
{
	u8 position = 0, i = 0;

	(*mask) = label & 0xff;

	while ((position == 0) && (i < 8)) {
		position = ((*mask) >> i) & 0x01;
		i++;
	}

	(*pos) = (i - 1);
}

static void stv0367_writebits(struct stv0367_state *state, u32 label, u8 val)
{
	u8 reg, mask, pos;

	reg = stv0367_readreg(state, (label >> 16) & 0xffff);
	extract_mask_pos(label, &mask, &pos);

	val = mask & (val << pos);

	reg = (reg & (~mask)) | val;
	stv0367_writereg(state, (label >> 16) & 0xffff, reg);

}

static void stv0367_setbits(u8 *reg, u32 label, u8 val)
{
	u8 mask, pos;

	extract_mask_pos(label, &mask, &pos);

	val = mask & (val << pos);

	(*reg) = ((*reg) & (~mask)) | val;
}

static u8 stv0367_readbits(struct stv0367_state *state, u32 label)
{
	u8 val = 0xff;
	u8 mask, pos;

	extract_mask_pos(label, &mask, &pos);

	val = stv0367_readreg(state, label >> 16);
	val = (val & mask) >> pos;

	return val;
}

#if 0 /* Currently, unused */
static u8 stv0367_getbits(u8 reg, u32 label)
{
	u8 mask, pos;

	extract_mask_pos(label, &mask, &pos);

	return (reg & mask) >> pos;
}
#endif

static void stv0367_write_table(struct stv0367_state *state,
				const struct st_register *deftab)
{
	int i = 0;

	while (1) {
		if (!deftab[i].addr)
			break;
		stv0367_writereg(state, deftab[i].addr, deftab[i].value);
		i++;
	}
}

static void stv0367_pll_setup(struct stv0367_state *state,
				u32 icspeed, u32 xtal)
{
	/* note on regs: R367TER_* and R367CAB_* defines each point to
	 * 0xf0d8, so just use R367TER_ for both cases
	 */

	switch (icspeed) {
	case STV0367_ICSPEED_58000:
		switch (xtal) {
		default:
		case 27000000:
			dprintk("STV0367 SetCLKgen for 58MHz IC and 27Mhz crystal\n");
			/* PLLMDIV: 27, PLLNDIV: 232 */
			stv0367_writereg(state, R367TER_PLLMDIV, 0x1b);
			stv0367_writereg(state, R367TER_PLLNDIV, 0xe8);
			break;
		}
		break;
	default:
	case STV0367_ICSPEED_53125:
		switch (xtal) {
			/* set internal freq to 53.125MHz */
		case 16000000:
			stv0367_writereg(state, R367TER_PLLMDIV, 0x2);
			stv0367_writereg(state, R367TER_PLLNDIV, 0x1b);
			break;
		case 25000000:
			stv0367_writereg(state, R367TER_PLLMDIV, 0xa);
			stv0367_writereg(state, R367TER_PLLNDIV, 0x55);
			break;
		default:
		case 27000000:
			dprintk("FE_STV0367TER_SetCLKgen for 27Mhz\n");
			stv0367_writereg(state, R367TER_PLLMDIV, 0x1);
			stv0367_writereg(state, R367TER_PLLNDIV, 0x8);
			break;
		case 30000000:
			stv0367_writereg(state, R367TER_PLLMDIV, 0xc);
			stv0367_writereg(state, R367TER_PLLNDIV, 0x55);
			break;
		}
	}

	stv0367_writereg(state, R367TER_PLLSETUP, 0x18);
}

static int stv0367_get_if_khz(struct stv0367_state *state, u32 *ifkhz)
{
	if (state->auto_if_khz && state->fe.ops.tuner_ops.get_if_frequency) {
		state->fe.ops.tuner_ops.get_if_frequency(&state->fe, ifkhz);
		*ifkhz = *ifkhz / 1000; /* hz -> khz */
	} else
		*ifkhz = state->config->if_khz;

	return 0;
}

static int stv0367ter_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct stv0367_state *state = fe->demodulator_priv;
	u8 tmp = stv0367_readreg(state, R367TER_I2CRPT);

	dprintk("%s:\n", __func__);

	if (enable) {
		stv0367_setbits(&tmp, F367TER_STOP_ENABLE, 0);
		stv0367_setbits(&tmp, F367TER_I2CT_ON, 1);
	} else {
		stv0367_setbits(&tmp, F367TER_STOP_ENABLE, 1);
		stv0367_setbits(&tmp, F367TER_I2CT_ON, 0);
	}

	stv0367_writereg(state, R367TER_I2CRPT, tmp);

	return 0;
}

static u32 stv0367_get_tuner_freq(struct dvb_frontend *fe)
{
	struct dvb_frontend_ops	*frontend_ops = &fe->ops;
	struct dvb_tuner_ops	*tuner_ops = &frontend_ops->tuner_ops;
	u32 freq = 0;
	int err = 0;

	dprintk("%s:\n", __func__);

	if (tuner_ops->get_frequency) {
		err = tuner_ops->get_frequency(fe, &freq);
		if (err < 0) {
			printk(KERN_ERR "%s: Invalid parameter\n", __func__);
			return err;
		}

		dprintk("%s: frequency=%d\n", __func__, freq);

	} else
		return -1;

	return freq;
}

static u16 CellsCoeffs_8MHz_367cofdm[3][6][5] = {
	{
		{0x10EF, 0xE205, 0x10EF, 0xCE49, 0x6DA7}, /* CELL 1 COEFFS 27M*/
		{0x2151, 0xc557, 0x2151, 0xc705, 0x6f93}, /* CELL 2 COEFFS */
		{0x2503, 0xc000, 0x2503, 0xc375, 0x7194}, /* CELL 3 COEFFS */
		{0x20E9, 0xca94, 0x20e9, 0xc153, 0x7194}, /* CELL 4 COEFFS */
		{0x06EF, 0xF852, 0x06EF, 0xC057, 0x7207}, /* CELL 5 COEFFS */
		{0x0000, 0x0ECC, 0x0ECC, 0x0000, 0x3647} /* CELL 6 COEFFS */
	}, {
		{0x10A0, 0xE2AF, 0x10A1, 0xCE76, 0x6D6D}, /* CELL 1 COEFFS 25M*/
		{0x20DC, 0xC676, 0x20D9, 0xC80A, 0x6F29},
		{0x2532, 0xC000, 0x251D, 0xC391, 0x706F},
		{0x1F7A, 0xCD2B, 0x2032, 0xC15E, 0x711F},
		{0x0698, 0xFA5E, 0x0568, 0xC059, 0x7193},
		{0x0000, 0x0918, 0x149C, 0x0000, 0x3642} /* CELL 6 COEFFS */
	}, {
		{0x0000, 0x0000, 0x0000, 0x0000, 0x0000}, /* 30M */
		{0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
		{0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
		{0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
		{0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
		{0x0000, 0x0000, 0x0000, 0x0000, 0x0000}
	}
};

static u16 CellsCoeffs_7MHz_367cofdm[3][6][5] = {
	{
		{0x12CA, 0xDDAF, 0x12CA, 0xCCEB, 0x6FB1}, /* CELL 1 COEFFS 27M*/
		{0x2329, 0xC000, 0x2329, 0xC6B0, 0x725F}, /* CELL 2 COEFFS */
		{0x2394, 0xC000, 0x2394, 0xC2C7, 0x7410}, /* CELL 3 COEFFS */
		{0x251C, 0xC000, 0x251C, 0xC103, 0x74D9}, /* CELL 4 COEFFS */
		{0x0804, 0xF546, 0x0804, 0xC040, 0x7544}, /* CELL 5 COEFFS */
		{0x0000, 0x0CD9, 0x0CD9, 0x0000, 0x370A} /* CELL 6 COEFFS */
	}, {
		{0x1285, 0xDE47, 0x1285, 0xCD17, 0x6F76}, /*25M*/
		{0x234C, 0xC000, 0x2348, 0xC6DA, 0x7206},
		{0x23B4, 0xC000, 0x23AC, 0xC2DB, 0x73B3},
		{0x253D, 0xC000, 0x25B6, 0xC10B, 0x747F},
		{0x0721, 0xF79C, 0x065F, 0xC041, 0x74EB},
		{0x0000, 0x08FA, 0x1162, 0x0000, 0x36FF}
	}, {
		{0x0000, 0x0000, 0x0000, 0x0000, 0x0000}, /* 30M */
		{0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
		{0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
		{0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
		{0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
		{0x0000, 0x0000, 0x0000, 0x0000, 0x0000}
	}
};

static u16 CellsCoeffs_6MHz_367cofdm[3][6][5] = {
	{
		{0x1699, 0xD5B8, 0x1699, 0xCBC3, 0x713B}, /* CELL 1 COEFFS 27M*/
		{0x2245, 0xC000, 0x2245, 0xC568, 0x74D5}, /* CELL 2 COEFFS */
		{0x227F, 0xC000, 0x227F, 0xC1FC, 0x76C6}, /* CELL 3 COEFFS */
		{0x235E, 0xC000, 0x235E, 0xC0A7, 0x778A}, /* CELL 4 COEFFS */
		{0x0ECB, 0xEA0B, 0x0ECB, 0xC027, 0x77DD}, /* CELL 5 COEFFS */
		{0x0000, 0x0B68, 0x0B68, 0x0000, 0xC89A}, /* CELL 6 COEFFS */
	}, {
		{0x1655, 0xD64E, 0x1658, 0xCBEF, 0x70FE}, /*25M*/
		{0x225E, 0xC000, 0x2256, 0xC589, 0x7489},
		{0x2293, 0xC000, 0x2295, 0xC209, 0x767E},
		{0x2377, 0xC000, 0x23AA, 0xC0AB, 0x7746},
		{0x0DC7, 0xEBC8, 0x0D07, 0xC027, 0x7799},
		{0x0000, 0x0888, 0x0E9C, 0x0000, 0x3757}

	}, {
		{0x0000, 0x0000, 0x0000, 0x0000, 0x0000}, /* 30M */
		{0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
		{0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
		{0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
		{0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
		{0x0000, 0x0000, 0x0000, 0x0000, 0x0000}
	}
};

static u32 stv0367ter_get_mclk(struct stv0367_state *state, u32 ExtClk_Hz)
{
	u32 mclk_Hz = 0; /* master clock frequency (Hz) */
	u32 m, n, p;

	dprintk("%s:\n", __func__);

	if (stv0367_readbits(state, F367TER_BYPASS_PLLXN) == 0) {
		n = (u32)stv0367_readbits(state, F367TER_PLL_NDIV);
		if (n == 0)
			n = n + 1;

		m = (u32)stv0367_readbits(state, F367TER_PLL_MDIV);
		if (m == 0)
			m = m + 1;

		p = (u32)stv0367_readbits(state, F367TER_PLL_PDIV);
		if (p > 5)
			p = 5;

		mclk_Hz = ((ExtClk_Hz / 2) * n) / (m * (1 << p));

		dprintk("N=%d M=%d P=%d mclk_Hz=%d ExtClk_Hz=%d\n",
				n, m, p, mclk_Hz, ExtClk_Hz);
	} else
		mclk_Hz = ExtClk_Hz;

	dprintk("%s: mclk_Hz=%d\n", __func__, mclk_Hz);

	return mclk_Hz;
}

static int stv0367ter_filt_coeff_init(struct stv0367_state *state,
				u16 CellsCoeffs[3][6][5], u32 DemodXtal)
{
	int i, j, k, freq;

	dprintk("%s:\n", __func__);

	freq = stv0367ter_get_mclk(state, DemodXtal);

	if (freq == 53125000)
		k = 1; /* equivalent to Xtal 25M on 362*/
	else if (freq == 54000000)
		k = 0; /* equivalent to Xtal 27M on 362*/
	else if (freq == 52500000)
		k = 2; /* equivalent to Xtal 30M on 362*/
	else
		return 0;

	for (i = 1; i <= 6; i++) {
		stv0367_writebits(state, F367TER_IIR_CELL_NB, i - 1);

		for (j = 1; j <= 5; j++) {
			stv0367_writereg(state,
				(R367TER_IIRCX_COEFF1_MSB + 2 * (j - 1)),
				MSB(CellsCoeffs[k][i-1][j-1]));
			stv0367_writereg(state,
				(R367TER_IIRCX_COEFF1_LSB + 2 * (j - 1)),
				LSB(CellsCoeffs[k][i-1][j-1]));
		}
	}

	return 1;

}

static void stv0367ter_agc_iir_lock_detect_set(struct stv0367_state *state)
{
	dprintk("%s:\n", __func__);

	stv0367_writebits(state, F367TER_LOCK_DETECT_LSB, 0x00);

	/* Lock detect 1 */
	stv0367_writebits(state, F367TER_LOCK_DETECT_CHOICE, 0x00);
	stv0367_writebits(state, F367TER_LOCK_DETECT_MSB, 0x06);
	stv0367_writebits(state, F367TER_AUT_AGC_TARGET_LSB, 0x04);

	/* Lock detect 2 */
	stv0367_writebits(state, F367TER_LOCK_DETECT_CHOICE, 0x01);
	stv0367_writebits(state, F367TER_LOCK_DETECT_MSB, 0x06);
	stv0367_writebits(state, F367TER_AUT_AGC_TARGET_LSB, 0x04);

	/* Lock detect 3 */
	stv0367_writebits(state, F367TER_LOCK_DETECT_CHOICE, 0x02);
	stv0367_writebits(state, F367TER_LOCK_DETECT_MSB, 0x01);
	stv0367_writebits(state, F367TER_AUT_AGC_TARGET_LSB, 0x00);

	/* Lock detect 4 */
	stv0367_writebits(state, F367TER_LOCK_DETECT_CHOICE, 0x03);
	stv0367_writebits(state, F367TER_LOCK_DETECT_MSB, 0x01);
	stv0367_writebits(state, F367TER_AUT_AGC_TARGET_LSB, 0x00);

}

static int stv0367_iir_filt_init(struct stv0367_state *state, u8 Bandwidth,
							u32 DemodXtalValue)
{
	dprintk("%s:\n", __func__);

	stv0367_writebits(state, F367TER_NRST_IIR, 0);

	switch (Bandwidth) {
	case 6:
		if (!stv0367ter_filt_coeff_init(state,
				CellsCoeffs_6MHz_367cofdm,
				DemodXtalValue))
			return 0;
		break;
	case 7:
		if (!stv0367ter_filt_coeff_init(state,
				CellsCoeffs_7MHz_367cofdm,
				DemodXtalValue))
			return 0;
		break;
	case 8:
		if (!stv0367ter_filt_coeff_init(state,
				CellsCoeffs_8MHz_367cofdm,
				DemodXtalValue))
			return 0;
		break;
	default:
		return 0;
	}

	stv0367_writebits(state, F367TER_NRST_IIR, 1);

	return 1;
}

static void stv0367ter_agc_iir_rst(struct stv0367_state *state)
{

	u8 com_n;

	dprintk("%s:\n", __func__);

	com_n = stv0367_readbits(state, F367TER_COM_N);

	stv0367_writebits(state, F367TER_COM_N, 0x07);

	stv0367_writebits(state, F367TER_COM_SOFT_RSTN, 0x00);
	stv0367_writebits(state, F367TER_COM_AGC_ON, 0x00);

	stv0367_writebits(state, F367TER_COM_SOFT_RSTN, 0x01);
	stv0367_writebits(state, F367TER_COM_AGC_ON, 0x01);

	stv0367_writebits(state, F367TER_COM_N, com_n);

}

static int stv0367ter_duration(s32 mode, int tempo1, int tempo2, int tempo3)
{
	int local_tempo = 0;
	switch (mode) {
	case 0:
		local_tempo = tempo1;
		break;
	case 1:
		local_tempo = tempo2;
		break ;

	case 2:
		local_tempo = tempo3;
		break;

	default:
		break;
	}
	/*	msleep(local_tempo);  */
	return local_tempo;
}

static enum
stv0367_ter_signal_type stv0367ter_check_syr(struct stv0367_state *state)
{
	int wd = 100;
	unsigned short int SYR_var;
	s32 SYRStatus;

	dprintk("%s:\n", __func__);

	SYR_var = stv0367_readbits(state, F367TER_SYR_LOCK);

	while ((!SYR_var) && (wd > 0)) {
		usleep_range(2000, 3000);
		wd -= 2;
		SYR_var = stv0367_readbits(state, F367TER_SYR_LOCK);
	}

	if (!SYR_var)
		SYRStatus = FE_TER_NOSYMBOL;
	else
		SYRStatus =  FE_TER_SYMBOLOK;

	dprintk("stv0367ter_check_syr SYRStatus %s\n",
				SYR_var == 0 ? "No Symbol" : "OK");

	return SYRStatus;
}

static enum
stv0367_ter_signal_type stv0367ter_check_cpamp(struct stv0367_state *state,
								s32 FFTmode)
{

	s32  CPAMPvalue = 0, CPAMPStatus, CPAMPMin;
	int wd = 0;

	dprintk("%s:\n", __func__);

	switch (FFTmode) {
	case 0: /*2k mode*/
		CPAMPMin = 20;
		wd = 10;
		break;
	case 1: /*8k mode*/
		CPAMPMin = 80;
		wd = 55;
		break;
	case 2: /*4k mode*/
		CPAMPMin = 40;
		wd = 30;
		break;
	default:
		CPAMPMin = 0xffff;  /*drives to NOCPAMP	*/
		break;
	}

	dprintk("%s: CPAMPMin=%d wd=%d\n", __func__, CPAMPMin, wd);

	CPAMPvalue = stv0367_readbits(state, F367TER_PPM_CPAMP_DIRECT);
	while ((CPAMPvalue < CPAMPMin) && (wd > 0)) {
		usleep_range(1000, 2000);
		wd -= 1;
		CPAMPvalue = stv0367_readbits(state, F367TER_PPM_CPAMP_DIRECT);
		/*dprintk("CPAMPvalue= %d at wd=%d\n",CPAMPvalue,wd); */
	}
	dprintk("******last CPAMPvalue= %d at wd=%d\n", CPAMPvalue, wd);
	if (CPAMPvalue < CPAMPMin) {
		CPAMPStatus = FE_TER_NOCPAMP;
		dprintk("%s: CPAMP failed\n", __func__);
	} else {
		dprintk("%s: CPAMP OK !\n", __func__);
		CPAMPStatus = FE_TER_CPAMPOK;
	}

	return CPAMPStatus;
}

static enum stv0367_ter_signal_type
stv0367ter_lock_algo(struct stv0367_state *state)
{
	enum stv0367_ter_signal_type ret_flag;
	short int wd, tempo;
	u8 try, u_var1 = 0, u_var2 = 0, u_var3 = 0, u_var4 = 0, mode, guard;
	u8 tmp, tmp2;

	dprintk("%s:\n", __func__);

	if (state == NULL)
		return FE_TER_SWNOK;

	try = 0;
	do {
		ret_flag = FE_TER_LOCKOK;

		stv0367_writebits(state, F367TER_CORE_ACTIVE, 0);

		if (state->config->if_iq_mode != 0)
			stv0367_writebits(state, F367TER_COM_N, 0x07);

		stv0367_writebits(state, F367TER_GUARD, 3);/* suggest 2k 1/4 */
		stv0367_writebits(state, F367TER_MODE, 0);
		stv0367_writebits(state, F367TER_SYR_TR_DIS, 0);
		usleep_range(5000, 10000);

		stv0367_writebits(state, F367TER_CORE_ACTIVE, 1);


		if (stv0367ter_check_syr(state) == FE_TER_NOSYMBOL)
			return FE_TER_NOSYMBOL;
		else { /*
			if chip locked on wrong mode first try,
			it must lock correctly second try */
			mode = stv0367_readbits(state, F367TER_SYR_MODE);
			if (stv0367ter_check_cpamp(state, mode) ==
							FE_TER_NOCPAMP) {
				if (try == 0)
					ret_flag = FE_TER_NOCPAMP;

			}
		}

		try++;
	} while ((try < 10) && (ret_flag != FE_TER_LOCKOK));

	tmp  = stv0367_readreg(state, R367TER_SYR_STAT);
	tmp2 = stv0367_readreg(state, R367TER_STATUS);
	dprintk("state=%p\n", state);
	dprintk("LOCK OK! mode=%d SYR_STAT=0x%x R367TER_STATUS=0x%x\n",
							mode, tmp, tmp2);

	tmp  = stv0367_readreg(state, R367TER_PRVIT);
	tmp2 = stv0367_readreg(state, R367TER_I2CRPT);
	dprintk("PRVIT=0x%x I2CRPT=0x%x\n", tmp, tmp2);

	tmp  = stv0367_readreg(state, R367TER_GAIN_SRC1);
	dprintk("GAIN_SRC1=0x%x\n", tmp);

	if ((mode != 0) && (mode != 1) && (mode != 2))
		return FE_TER_SWNOK;

	/*guard=stv0367_readbits(state,F367TER_SYR_GUARD); */

	/*suppress EPQ auto for SYR_GARD 1/16 or 1/32
	and set channel predictor in automatic */
#if 0
	switch (guard) {

	case 0:
	case 1:
		stv0367_writebits(state, F367TER_AUTO_LE_EN, 0);
		stv0367_writereg(state, R367TER_CHC_CTL, 0x01);
		break;
	case 2:
	case 3:
		stv0367_writebits(state, F367TER_AUTO_LE_EN, 1);
		stv0367_writereg(state, R367TER_CHC_CTL, 0x11);
		break;

	default:
		return FE_TER_SWNOK;
	}
#endif

	/*reset fec an reedsolo FOR 367 only*/
	stv0367_writebits(state, F367TER_RST_SFEC, 1);
	stv0367_writebits(state, F367TER_RST_REEDSOLO, 1);
	usleep_range(1000, 2000);
	stv0367_writebits(state, F367TER_RST_SFEC, 0);
	stv0367_writebits(state, F367TER_RST_REEDSOLO, 0);

	u_var1 = stv0367_readbits(state, F367TER_LK);
	u_var2 = stv0367_readbits(state, F367TER_PRF);
	u_var3 = stv0367_readbits(state, F367TER_TPS_LOCK);
	/*	u_var4=stv0367_readbits(state,F367TER_TSFIFO_LINEOK); */

	wd = stv0367ter_duration(mode, 125, 500, 250);
	tempo = stv0367ter_duration(mode, 4, 16, 8);

	/*while ( ((!u_var1)||(!u_var2)||(!u_var3)||(!u_var4))  && (wd>=0)) */
	while (((!u_var1) || (!u_var2) || (!u_var3)) && (wd >= 0)) {
		usleep_range(1000 * tempo, 1000 * (tempo + 1));
		wd -= tempo;
		u_var1 = stv0367_readbits(state, F367TER_LK);
		u_var2 = stv0367_readbits(state, F367TER_PRF);
		u_var3 = stv0367_readbits(state, F367TER_TPS_LOCK);
		/*u_var4=stv0367_readbits(state, F367TER_TSFIFO_LINEOK); */
	}

	if (!u_var1)
		return FE_TER_NOLOCK;


	if (!u_var2)
		return FE_TER_NOPRFOUND;

	if (!u_var3)
		return FE_TER_NOTPS;

	guard = stv0367_readbits(state, F367TER_SYR_GUARD);
	stv0367_writereg(state, R367TER_CHC_CTL, 0x11);
	switch (guard) {
	case 0:
	case 1:
		stv0367_writebits(state, F367TER_AUTO_LE_EN, 0);
		/*stv0367_writereg(state,R367TER_CHC_CTL, 0x1);*/
		stv0367_writebits(state, F367TER_SYR_FILTER, 0);
		break;
	case 2:
	case 3:
		stv0367_writebits(state, F367TER_AUTO_LE_EN, 1);
		/*stv0367_writereg(state,R367TER_CHC_CTL, 0x11);*/
		stv0367_writebits(state, F367TER_SYR_FILTER, 1);
		break;

	default:
		return FE_TER_SWNOK;
	}

	/* apply Sfec workaround if 8K 64QAM CR!=1/2*/
	if ((stv0367_readbits(state, F367TER_TPS_CONST) == 2) &&
			(mode == 1) &&
			(stv0367_readbits(state, F367TER_TPS_HPCODE) != 0)) {
		stv0367_writereg(state, R367TER_SFDLYSETH, 0xc0);
		stv0367_writereg(state, R367TER_SFDLYSETM, 0x60);
		stv0367_writereg(state, R367TER_SFDLYSETL, 0x0);
	} else
		stv0367_writereg(state, R367TER_SFDLYSETH, 0x0);

	wd = stv0367ter_duration(mode, 125, 500, 250);
	u_var4 = stv0367_readbits(state, F367TER_TSFIFO_LINEOK);

	while ((!u_var4) && (wd >= 0)) {
		usleep_range(1000 * tempo, 1000 * (tempo + 1));
		wd -= tempo;
		u_var4 = stv0367_readbits(state, F367TER_TSFIFO_LINEOK);
	}

	if (!u_var4)
		return FE_TER_NOLOCK;

	/* for 367 leave COM_N at 0x7 for IQ_mode*/
	/*if(ter_state->if_iq_mode!=FE_TER_NORMAL_IF_TUNER) {
		tempo=0;
		while ((stv0367_readbits(state,F367TER_COM_USEGAINTRK)!=1) &&
		(stv0367_readbits(state,F367TER_COM_AGCLOCK)!=1)&&(tempo<100)) {
			ChipWaitOrAbort(state,1);
			tempo+=1;
		}

		stv0367_writebits(state,F367TER_COM_N,0x17);
	} */

	stv0367_writebits(state, F367TER_SYR_TR_DIS, 1);

	dprintk("FE_TER_LOCKOK !!!\n");

	return	FE_TER_LOCKOK;

}

static void stv0367ter_set_ts_mode(struct stv0367_state *state,
					enum stv0367_ts_mode PathTS)
{

	dprintk("%s:\n", __func__);

	if (state == NULL)
		return;

	stv0367_writebits(state, F367TER_TS_DIS, 0);
	switch (PathTS) {
	default:
		/*for removing warning :default we can assume in parallel mode*/
	case STV0367_PARALLEL_PUNCT_CLOCK:
		stv0367_writebits(state, F367TER_TSFIFO_SERIAL, 0);
		stv0367_writebits(state, F367TER_TSFIFO_DVBCI, 0);
		break;
	case STV0367_SERIAL_PUNCT_CLOCK:
		stv0367_writebits(state, F367TER_TSFIFO_SERIAL, 1);
		stv0367_writebits(state, F367TER_TSFIFO_DVBCI, 1);
		break;
	}
}

static void stv0367ter_set_clk_pol(struct stv0367_state *state,
					enum stv0367_clk_pol clock)
{

	dprintk("%s:\n", __func__);

	if (state == NULL)
		return;

	switch (clock) {
	case STV0367_RISINGEDGE_CLOCK:
		stv0367_writebits(state, F367TER_TS_BYTE_CLK_INV, 1);
		break;
	case STV0367_FALLINGEDGE_CLOCK:
		stv0367_writebits(state, F367TER_TS_BYTE_CLK_INV, 0);
		break;
		/*case FE_TER_CLOCK_POLARITY_DEFAULT:*/
	default:
		stv0367_writebits(state, F367TER_TS_BYTE_CLK_INV, 0);
		break;
	}
}

#if 0
static void stv0367ter_core_sw(struct stv0367_state *state)
{

	dprintk("%s:\n", __func__);

	stv0367_writebits(state, F367TER_CORE_ACTIVE, 0);
	stv0367_writebits(state, F367TER_CORE_ACTIVE, 1);
	msleep(350);
}
#endif
static int stv0367ter_standby(struct dvb_frontend *fe, u8 standby_on)
{
	struct stv0367_state *state = fe->demodulator_priv;

	dprintk("%s:\n", __func__);

	if (standby_on) {
		stv0367_writebits(state, F367TER_STDBY, 1);
		stv0367_writebits(state, F367TER_STDBY_FEC, 1);
		stv0367_writebits(state, F367TER_STDBY_CORE, 1);
	} else {
		stv0367_writebits(state, F367TER_STDBY, 0);
		stv0367_writebits(state, F367TER_STDBY_FEC, 0);
		stv0367_writebits(state, F367TER_STDBY_CORE, 0);
	}

	return 0;
}

static int stv0367ter_sleep(struct dvb_frontend *fe)
{
	return stv0367ter_standby(fe, 1);
}

static int stv0367ter_init(struct dvb_frontend *fe)
{
	struct stv0367_state *state = fe->demodulator_priv;
	struct stv0367ter_state *ter_state = state->ter_state;

	dprintk("%s:\n", __func__);

	ter_state->pBER = 0;

	stv0367_write_table(state,
		stv0367_deftabs[state->deftabs][STV0367_TAB_TER]);

	stv0367_pll_setup(state, STV0367_ICSPEED_53125, state->config->xtal);

	stv0367_writereg(state, R367TER_I2CRPT, 0xa0);
	stv0367_writereg(state, R367TER_ANACTRL, 0x00);

	/*Set TS1 and TS2 to serial or parallel mode */
	stv0367ter_set_ts_mode(state, state->config->ts_mode);
	stv0367ter_set_clk_pol(state, state->config->clk_pol);

	state->chip_id = stv0367_readreg(state, R367TER_ID);
	ter_state->first_lock = 0;
	ter_state->unlock_counter = 2;

	return 0;
}

static int stv0367ter_algo(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct stv0367_state *state = fe->demodulator_priv;
	struct stv0367ter_state *ter_state = state->ter_state;
	int offset = 0, tempo = 0;
	u8 u_var;
	u8 /*constell,*/ counter;
	s8 step;
	s32 timing_offset = 0;
	u32 trl_nomrate = 0, InternalFreq = 0, temp = 0, ifkhz = 0;

	dprintk("%s:\n", __func__);

	stv0367_get_if_khz(state, &ifkhz);

	ter_state->frequency = p->frequency;
	ter_state->force = FE_TER_FORCENONE
			+ stv0367_readbits(state, F367TER_FORCE) * 2;
	ter_state->if_iq_mode = state->config->if_iq_mode;
	switch (state->config->if_iq_mode) {
	case FE_TER_NORMAL_IF_TUNER:  /* Normal IF mode */
		dprintk("ALGO: FE_TER_NORMAL_IF_TUNER selected\n");
		stv0367_writebits(state, F367TER_TUNER_BB, 0);
		stv0367_writebits(state, F367TER_LONGPATH_IF, 0);
		stv0367_writebits(state, F367TER_DEMUX_SWAP, 0);
		break;
	case FE_TER_LONGPATH_IF_TUNER:  /* Long IF mode */
		dprintk("ALGO: FE_TER_LONGPATH_IF_TUNER selected\n");
		stv0367_writebits(state, F367TER_TUNER_BB, 0);
		stv0367_writebits(state, F367TER_LONGPATH_IF, 1);
		stv0367_writebits(state, F367TER_DEMUX_SWAP, 1);
		break;
	case FE_TER_IQ_TUNER:  /* IQ mode */
		dprintk("ALGO: FE_TER_IQ_TUNER selected\n");
		stv0367_writebits(state, F367TER_TUNER_BB, 1);
		stv0367_writebits(state, F367TER_PPM_INVSEL, 0);
		break;
	default:
		printk(KERN_ERR "ALGO: wrong TUNER type selected\n");
		return -EINVAL;
	}

	usleep_range(5000, 7000);

	switch (p->inversion) {
	case INVERSION_AUTO:
	default:
		dprintk("%s: inversion AUTO\n", __func__);
		if (ter_state->if_iq_mode == FE_TER_IQ_TUNER)
			stv0367_writebits(state, F367TER_IQ_INVERT,
						ter_state->sense);
		else
			stv0367_writebits(state, F367TER_INV_SPECTR,
						ter_state->sense);

		break;
	case INVERSION_ON:
	case INVERSION_OFF:
		if (ter_state->if_iq_mode == FE_TER_IQ_TUNER)
			stv0367_writebits(state, F367TER_IQ_INVERT,
						p->inversion);
		else
			stv0367_writebits(state, F367TER_INV_SPECTR,
						p->inversion);

		break;
	}

	if ((ter_state->if_iq_mode != FE_TER_NORMAL_IF_TUNER) &&
				(ter_state->pBW != ter_state->bw)) {
		stv0367ter_agc_iir_lock_detect_set(state);

		/*set fine agc target to 180 for LPIF or IQ mode*/
		/* set Q_AGCTarget */
		stv0367_writebits(state, F367TER_SEL_IQNTAR, 1);
		stv0367_writebits(state, F367TER_AUT_AGC_TARGET_MSB, 0xB);
		/*stv0367_writebits(state,AUT_AGC_TARGET_LSB,0x04); */

		/* set Q_AGCTarget */
		stv0367_writebits(state, F367TER_SEL_IQNTAR, 0);
		stv0367_writebits(state, F367TER_AUT_AGC_TARGET_MSB, 0xB);
		/*stv0367_writebits(state,AUT_AGC_TARGET_LSB,0x04); */

		if (!stv0367_iir_filt_init(state, ter_state->bw,
						state->config->xtal))
			return -EINVAL;
		/*set IIR filter once for 6,7 or 8MHz BW*/
		ter_state->pBW = ter_state->bw;

		stv0367ter_agc_iir_rst(state);
	}

	if (ter_state->hierarchy == FE_TER_HIER_LOW_PRIO)
		stv0367_writebits(state, F367TER_BDI_LPSEL, 0x01);
	else
		stv0367_writebits(state, F367TER_BDI_LPSEL, 0x00);

	InternalFreq = stv0367ter_get_mclk(state, state->config->xtal) / 1000;
	temp = (int)
		((((ter_state->bw * 64 * (1 << 15) * 100)
						/ (InternalFreq)) * 10) / 7);

	stv0367_writebits(state, F367TER_TRL_NOMRATE_LSB, temp % 2);
	temp = temp / 2;
	stv0367_writebits(state, F367TER_TRL_NOMRATE_HI, temp / 256);
	stv0367_writebits(state, F367TER_TRL_NOMRATE_LO, temp % 256);

	temp = stv0367_readbits(state, F367TER_TRL_NOMRATE_HI) * 512 +
			stv0367_readbits(state, F367TER_TRL_NOMRATE_LO) * 2 +
			stv0367_readbits(state, F367TER_TRL_NOMRATE_LSB);
	temp = (int)(((1 << 17) * ter_state->bw * 1000) / (7 * (InternalFreq)));
	stv0367_writebits(state, F367TER_GAIN_SRC_HI, temp / 256);
	stv0367_writebits(state, F367TER_GAIN_SRC_LO, temp % 256);
	temp = stv0367_readbits(state, F367TER_GAIN_SRC_HI) * 256 +
			stv0367_readbits(state, F367TER_GAIN_SRC_LO);

	temp = (int)
		((InternalFreq - ifkhz) * (1 << 16) / (InternalFreq));

	dprintk("DEROT temp=0x%x\n", temp);
	stv0367_writebits(state, F367TER_INC_DEROT_HI, temp / 256);
	stv0367_writebits(state, F367TER_INC_DEROT_LO, temp % 256);

	ter_state->echo_pos = 0;
	ter_state->ucblocks = 0; /* liplianin */
	ter_state->pBER = 0; /* liplianin */
	stv0367_writebits(state, F367TER_LONG_ECHO, ter_state->echo_pos);

	if (stv0367ter_lock_algo(state) != FE_TER_LOCKOK)
		return 0;

	ter_state->state = FE_TER_LOCKOK;

	ter_state->mode = stv0367_readbits(state, F367TER_SYR_MODE);
	ter_state->guard = stv0367_readbits(state, F367TER_SYR_GUARD);

	ter_state->first_lock = 1; /* we know sense now :) */

	ter_state->agc_val =
			(stv0367_readbits(state, F367TER_AGC1_VAL_LO) << 16) +
			(stv0367_readbits(state, F367TER_AGC1_VAL_HI) << 24) +
			stv0367_readbits(state, F367TER_AGC2_VAL_LO) +
			(stv0367_readbits(state, F367TER_AGC2_VAL_HI) << 8);

	/* Carrier offset calculation */
	stv0367_writebits(state, F367TER_FREEZE, 1);
	offset = (stv0367_readbits(state, F367TER_CRL_FOFFSET_VHI) << 16) ;
	offset += (stv0367_readbits(state, F367TER_CRL_FOFFSET_HI) << 8);
	offset += (stv0367_readbits(state, F367TER_CRL_FOFFSET_LO));
	stv0367_writebits(state, F367TER_FREEZE, 0);
	if (offset > 8388607)
		offset -= 16777216;

	offset = offset * 2 / 16384;

	if (ter_state->mode == FE_TER_MODE_2K)
		offset = (offset * 4464) / 1000;/*** 1 FFT BIN=4.464khz***/
	else if (ter_state->mode == FE_TER_MODE_4K)
		offset = (offset * 223) / 100;/*** 1 FFT BIN=2.23khz***/
	else  if (ter_state->mode == FE_TER_MODE_8K)
		offset = (offset * 111) / 100;/*** 1 FFT BIN=1.1khz***/

	if (stv0367_readbits(state, F367TER_PPM_INVSEL) == 1) {
		if ((stv0367_readbits(state, F367TER_INV_SPECTR) ==
				(stv0367_readbits(state,
					F367TER_STATUS_INV_SPECRUM) == 1)))
			offset = offset * -1;
	}

	if (ter_state->bw == 6)
		offset = (offset * 6) / 8;
	else if (ter_state->bw == 7)
		offset = (offset * 7) / 8;

	ter_state->frequency += offset;

	tempo = 10;  /* exit even if timing_offset stays null */
	while ((timing_offset == 0) && (tempo > 0)) {
		usleep_range(10000, 20000);	/*was 20ms  */
		/* fine tuning of timing offset if required */
		timing_offset = stv0367_readbits(state, F367TER_TRL_TOFFSET_LO)
				+ 256 * stv0367_readbits(state,
							F367TER_TRL_TOFFSET_HI);
		if (timing_offset >= 32768)
			timing_offset -= 65536;
		trl_nomrate = (512 * stv0367_readbits(state,
							F367TER_TRL_NOMRATE_HI)
			+ stv0367_readbits(state, F367TER_TRL_NOMRATE_LO) * 2
			+ stv0367_readbits(state, F367TER_TRL_NOMRATE_LSB));

		timing_offset = ((signed)(1000000 / trl_nomrate) *
							timing_offset) / 2048;
		tempo--;
	}

	if (timing_offset <= 0) {
		timing_offset = (timing_offset - 11) / 22;
		step = -1;
	} else {
		timing_offset = (timing_offset + 11) / 22;
		step = 1;
	}

	for (counter = 0; counter < abs(timing_offset); counter++) {
		trl_nomrate += step;
		stv0367_writebits(state, F367TER_TRL_NOMRATE_LSB,
						trl_nomrate % 2);
		stv0367_writebits(state, F367TER_TRL_NOMRATE_LO,
						trl_nomrate / 2);
		usleep_range(1000, 2000);
	}

	usleep_range(5000, 6000);
	/* unlocks could happen in case of trl centring big step,
	then a core off/on restarts demod */
	u_var = stv0367_readbits(state, F367TER_LK);

	if (!u_var) {
		stv0367_writebits(state, F367TER_CORE_ACTIVE, 0);
		msleep(20);
		stv0367_writebits(state, F367TER_CORE_ACTIVE, 1);
	}

	return 0;
}

static int stv0367ter_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct stv0367_state *state = fe->demodulator_priv;
	struct stv0367ter_state *ter_state = state->ter_state;

	/*u8 trials[2]; */
	s8 num_trials, index;
	u8 SenseTrials[] = { INVERSION_ON, INVERSION_OFF };

	if (state->reinit_on_setfrontend)
		stv0367ter_init(fe);

	if (fe->ops.tuner_ops.set_params) {
		if (state->use_i2c_gatectrl && fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);
		fe->ops.tuner_ops.set_params(fe);
		if (state->use_i2c_gatectrl && fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	switch (p->transmission_mode) {
	default:
	case TRANSMISSION_MODE_AUTO:
	case TRANSMISSION_MODE_2K:
		ter_state->mode = FE_TER_MODE_2K;
		break;
/*	case TRANSMISSION_MODE_4K:
		pLook.mode = FE_TER_MODE_4K;
		break;*/
	case TRANSMISSION_MODE_8K:
		ter_state->mode = FE_TER_MODE_8K;
		break;
	}

	switch (p->guard_interval) {
	default:
	case GUARD_INTERVAL_1_32:
	case GUARD_INTERVAL_1_16:
	case GUARD_INTERVAL_1_8:
	case GUARD_INTERVAL_1_4:
		ter_state->guard = p->guard_interval;
		break;
	case GUARD_INTERVAL_AUTO:
		ter_state->guard = GUARD_INTERVAL_1_32;
		break;
	}

	switch (p->bandwidth_hz) {
	case 6000000:
		ter_state->bw = FE_TER_CHAN_BW_6M;
		break;
	case 7000000:
		ter_state->bw = FE_TER_CHAN_BW_7M;
		break;
	case 8000000:
	default:
		ter_state->bw = FE_TER_CHAN_BW_8M;
	}

	ter_state->hierarchy = FE_TER_HIER_NONE;

	switch (p->inversion) {
	case INVERSION_OFF:
	case INVERSION_ON:
		num_trials = 1;
		break;
	default:
		num_trials = 2;
		if (ter_state->first_lock)
			num_trials = 1;
		break;
	}

	ter_state->state = FE_TER_NOLOCK;
	index = 0;

	while (((index) < num_trials) && (ter_state->state != FE_TER_LOCKOK)) {
		if (!ter_state->first_lock) {
			if (p->inversion == INVERSION_AUTO)
				ter_state->sense = SenseTrials[index];

		}
		stv0367ter_algo(fe);

		if ((ter_state->state == FE_TER_LOCKOK) &&
				(p->inversion == INVERSION_AUTO) &&
								(index == 1)) {
			/* invert spectrum sense */
			SenseTrials[index] = SenseTrials[0];
			SenseTrials[(index + 1) % 2] = (SenseTrials[1] + 1) % 2;
		}

		index++;
	}

	return 0;
}

static int stv0367ter_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct stv0367_state *state = fe->demodulator_priv;
	struct stv0367ter_state *ter_state = state->ter_state;
	u32 errs = 0;

	/*wait for counting completion*/
	if (stv0367_readbits(state, F367TER_SFERRC_OLDVALUE) == 0) {
		errs =
			((u32)stv0367_readbits(state, F367TER_ERR_CNT1)
			* (1 << 16))
			+ ((u32)stv0367_readbits(state, F367TER_ERR_CNT1_HI)
			* (1 << 8))
			+ ((u32)stv0367_readbits(state, F367TER_ERR_CNT1_LO));
		ter_state->ucblocks = errs;
	}

	(*ucblocks) = ter_state->ucblocks;

	return 0;
}

static int stv0367ter_get_frontend(struct dvb_frontend *fe,
				   struct dtv_frontend_properties *p)
{
	struct stv0367_state *state = fe->demodulator_priv;
	struct stv0367ter_state *ter_state = state->ter_state;
	enum stv0367_ter_mode mode;
	int constell = 0,/* snr = 0,*/ Data = 0;

	p->frequency = stv0367_get_tuner_freq(fe);
	if ((int)p->frequency < 0)
		p->frequency = -p->frequency;

	constell = stv0367_readbits(state, F367TER_TPS_CONST);
	if (constell == 0)
		p->modulation = QPSK;
	else if (constell == 1)
		p->modulation = QAM_16;
	else
		p->modulation = QAM_64;

	p->inversion = stv0367_readbits(state, F367TER_INV_SPECTR);

	/* Get the Hierarchical mode */
	Data = stv0367_readbits(state, F367TER_TPS_HIERMODE);

	switch (Data) {
	case 0:
		p->hierarchy = HIERARCHY_NONE;
		break;
	case 1:
		p->hierarchy = HIERARCHY_1;
		break;
	case 2:
		p->hierarchy = HIERARCHY_2;
		break;
	case 3:
		p->hierarchy = HIERARCHY_4;
		break;
	default:
		p->hierarchy = HIERARCHY_AUTO;
		break; /* error */
	}

	/* Get the FEC Rate */
	if (ter_state->hierarchy == FE_TER_HIER_LOW_PRIO)
		Data = stv0367_readbits(state, F367TER_TPS_LPCODE);
	else
		Data = stv0367_readbits(state, F367TER_TPS_HPCODE);

	switch (Data) {
	case 0:
		p->code_rate_HP = FEC_1_2;
		break;
	case 1:
		p->code_rate_HP = FEC_2_3;
		break;
	case 2:
		p->code_rate_HP = FEC_3_4;
		break;
	case 3:
		p->code_rate_HP = FEC_5_6;
		break;
	case 4:
		p->code_rate_HP = FEC_7_8;
		break;
	default:
		p->code_rate_HP = FEC_AUTO;
		break; /* error */
	}

	mode = stv0367_readbits(state, F367TER_SYR_MODE);

	switch (mode) {
	case FE_TER_MODE_2K:
		p->transmission_mode = TRANSMISSION_MODE_2K;
		break;
/*	case FE_TER_MODE_4K:
		p->transmission_mode = TRANSMISSION_MODE_4K;
		break;*/
	case FE_TER_MODE_8K:
		p->transmission_mode = TRANSMISSION_MODE_8K;
		break;
	default:
		p->transmission_mode = TRANSMISSION_MODE_AUTO;
	}

	p->guard_interval = stv0367_readbits(state, F367TER_SYR_GUARD);

	return 0;
}

static u32 stv0367ter_snr_readreg(struct dvb_frontend *fe)
{
	struct stv0367_state *state = fe->demodulator_priv;
	u32 snru32 = 0;
	int cpt = 0;
	u8 cut = stv0367_readbits(state, F367TER_IDENTIFICATIONREG);

	while (cpt < 10) {
		usleep_range(2000, 3000);
		if (cut == 0x50) /*cut 1.0 cut 1.1*/
			snru32 += stv0367_readbits(state, F367TER_CHCSNR) / 4;
		else /*cu2.0*/
			snru32 += 125 * stv0367_readbits(state, F367TER_CHCSNR);

		cpt++;
	}
	snru32 /= 10;/*average on 10 values*/

	return snru32;
}

static int stv0367ter_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	u32 snrval = stv0367ter_snr_readreg(fe);

	*snr = snrval / 1000;

	return 0;
}

#if 0
static int stv0367ter_status(struct dvb_frontend *fe)
{

	struct stv0367_state *state = fe->demodulator_priv;
	struct stv0367ter_state *ter_state = state->ter_state;
	int locked = FALSE;

	locked = (stv0367_readbits(state, F367TER_LK));
	if (!locked)
		ter_state->unlock_counter += 1;
	else
		ter_state->unlock_counter = 0;

	if (ter_state->unlock_counter > 2) {
		if (!stv0367_readbits(state, F367TER_TPS_LOCK) ||
				(!stv0367_readbits(state, F367TER_LK))) {
			stv0367_writebits(state, F367TER_CORE_ACTIVE, 0);
			usleep_range(2000, 3000);
			stv0367_writebits(state, F367TER_CORE_ACTIVE, 1);
			msleep(350);
			locked = (stv0367_readbits(state, F367TER_TPS_LOCK)) &&
					(stv0367_readbits(state, F367TER_LK));
		}

	}

	return locked;
}
#endif
static int stv0367ter_read_status(struct dvb_frontend *fe,
				  enum fe_status *status)
{
	struct stv0367_state *state = fe->demodulator_priv;

	dprintk("%s:\n", __func__);

	*status = 0;

	if (stv0367_readbits(state, F367TER_LK)) {
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI
			  | FE_HAS_SYNC | FE_HAS_LOCK;
		dprintk("%s: stv0367 has locked\n", __func__);
	}

	return 0;
}

static int stv0367ter_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct stv0367_state *state = fe->demodulator_priv;
	struct stv0367ter_state *ter_state = state->ter_state;
	u32 Errors = 0, tber = 0, temporary = 0;
	int abc = 0, def = 0;


	/*wait for counting completion*/
	if (stv0367_readbits(state, F367TER_SFERRC_OLDVALUE) == 0)
		Errors = ((u32)stv0367_readbits(state, F367TER_SFEC_ERR_CNT)
			* (1 << 16))
			+ ((u32)stv0367_readbits(state, F367TER_SFEC_ERR_CNT_HI)
			* (1 << 8))
			+ ((u32)stv0367_readbits(state,
						F367TER_SFEC_ERR_CNT_LO));
	/*measurement not completed, load previous value*/
	else {
		tber = ter_state->pBER;
		return 0;
	}

	abc = stv0367_readbits(state, F367TER_SFEC_ERR_SOURCE);
	def = stv0367_readbits(state, F367TER_SFEC_NUM_EVENT);

	if (Errors == 0) {
		tber = 0;
	} else if (abc == 0x7) {
		if (Errors <= 4) {
			temporary = (Errors * 1000000000) / (8 * (1 << 14));
		} else if (Errors <= 42) {
			temporary = (Errors * 100000000) / (8 * (1 << 14));
			temporary = temporary * 10;
		} else if (Errors <= 429) {
			temporary = (Errors * 10000000) / (8 * (1 << 14));
			temporary = temporary * 100;
		} else if (Errors <= 4294) {
			temporary = (Errors * 1000000) / (8 * (1 << 14));
			temporary = temporary * 1000;
		} else if (Errors <= 42949) {
			temporary = (Errors * 100000) / (8 * (1 << 14));
			temporary = temporary * 10000;
		} else if (Errors <= 429496) {
			temporary = (Errors * 10000) / (8 * (1 << 14));
			temporary = temporary * 100000;
		} else { /*if (Errors<4294967) 2^22 max error*/
			temporary = (Errors * 1000) / (8 * (1 << 14));
			temporary = temporary * 100000;	/* still to *10 */
		}

		/* Byte error*/
		if (def == 2)
			/*tber=Errors/(8*(1 <<14));*/
			tber = temporary;
		else if (def == 3)
			/*tber=Errors/(8*(1 <<16));*/
			tber = temporary / 4;
		else if (def == 4)
			/*tber=Errors/(8*(1 <<18));*/
			tber = temporary / 16;
		else if (def == 5)
			/*tber=Errors/(8*(1 <<20));*/
			tber = temporary / 64;
		else if (def == 6)
			/*tber=Errors/(8*(1 <<22));*/
			tber = temporary / 256;
		else
			/* should not pass here*/
			tber = 0;

		if ((Errors < 4294967) && (Errors > 429496))
			tber *= 10;

	}

	/* save actual value */
	ter_state->pBER = tber;

	(*ber) = tber;

	return 0;
}
#if 0
static u32 stv0367ter_get_per(struct stv0367_state *state)
{
	struct stv0367ter_state *ter_state = state->ter_state;
	u32 Errors = 0, Per = 0, temporary = 0;
	int abc = 0, def = 0, cpt = 0;

	while (((stv0367_readbits(state, F367TER_SFERRC_OLDVALUE) == 1) &&
			(cpt < 400)) || ((Errors == 0) && (cpt < 400))) {
		usleep_range(1000, 2000);
		Errors = ((u32)stv0367_readbits(state, F367TER_ERR_CNT1)
			* (1 << 16))
			+ ((u32)stv0367_readbits(state, F367TER_ERR_CNT1_HI)
			* (1 << 8))
			+ ((u32)stv0367_readbits(state, F367TER_ERR_CNT1_LO));
		cpt++;
	}
	abc = stv0367_readbits(state, F367TER_ERR_SRC1);
	def = stv0367_readbits(state, F367TER_NUM_EVT1);

	if (Errors == 0)
		Per = 0;
	else if (abc == 0x9) {
		if (Errors <= 4) {
			temporary = (Errors * 1000000000) / (8 * (1 << 8));
		} else if (Errors <= 42) {
			temporary = (Errors * 100000000) / (8 * (1 << 8));
			temporary = temporary * 10;
		} else if (Errors <= 429) {
			temporary = (Errors * 10000000) / (8 * (1 << 8));
			temporary = temporary * 100;
		} else if (Errors <= 4294) {
			temporary = (Errors * 1000000) / (8 * (1 << 8));
			temporary = temporary * 1000;
		} else if (Errors <= 42949) {
			temporary = (Errors * 100000) / (8 * (1 << 8));
			temporary = temporary * 10000;
		} else { /*if(Errors<=429496)  2^16 errors max*/
			temporary = (Errors * 10000) / (8 * (1 << 8));
			temporary = temporary * 100000;
		}

		/* pkt error*/
		if (def == 2)
			/*Per=Errors/(1 << 8);*/
			Per = temporary;
		else if (def == 3)
			/*Per=Errors/(1 << 10);*/
			Per = temporary / 4;
		else if (def == 4)
			/*Per=Errors/(1 << 12);*/
			Per = temporary / 16;
		else if (def == 5)
			/*Per=Errors/(1 << 14);*/
			Per = temporary / 64;
		else if (def == 6)
			/*Per=Errors/(1 << 16);*/
			Per = temporary / 256;
		else
			Per = 0;

	}
	/* save actual value */
	ter_state->pPER = Per;

	return Per;
}
#endif
static int stv0367_get_tune_settings(struct dvb_frontend *fe,
					struct dvb_frontend_tune_settings
					*fe_tune_settings)
{
	fe_tune_settings->min_delay_ms = 1000;
	fe_tune_settings->step_size = 0;
	fe_tune_settings->max_drift = 0;

	return 0;
}

static void stv0367_release(struct dvb_frontend *fe)
{
	struct stv0367_state *state = fe->demodulator_priv;

	kfree(state->ter_state);
	kfree(state->cab_state);
	kfree(state);
}

static const struct dvb_frontend_ops stv0367ter_ops = {
	.delsys = { SYS_DVBT },
	.info = {
		.name			= "ST STV0367 DVB-T",
		.frequency_min_hz	=  47 * MHz,
		.frequency_max_hz	= 862 * MHz,
		.frequency_stepsize_hz	= 15625,
		.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 |
			FE_CAN_FEC_3_4 | FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 |
			FE_CAN_FEC_AUTO |
			FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 |
			FE_CAN_QAM_128 | FE_CAN_QAM_256 | FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_RECOVER |
			FE_CAN_INVERSION_AUTO |
			FE_CAN_MUTE_TS
	},
	.release = stv0367_release,
	.init = stv0367ter_init,
	.sleep = stv0367ter_sleep,
	.i2c_gate_ctrl = stv0367ter_gate_ctrl,
	.set_frontend = stv0367ter_set_frontend,
	.get_frontend = stv0367ter_get_frontend,
	.get_tune_settings = stv0367_get_tune_settings,
	.read_status = stv0367ter_read_status,
	.read_ber = stv0367ter_read_ber,/* too slow */
/*	.read_signal_strength = stv0367_read_signal_strength,*/
	.read_snr = stv0367ter_read_snr,
	.read_ucblocks = stv0367ter_read_ucblocks,
};

struct dvb_frontend *stv0367ter_attach(const struct stv0367_config *config,
				   struct i2c_adapter *i2c)
{
	struct stv0367_state *state = NULL;
	struct stv0367ter_state *ter_state = NULL;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct stv0367_state), GFP_KERNEL);
	if (state == NULL)
		goto error;
	ter_state = kzalloc(sizeof(struct stv0367ter_state), GFP_KERNEL);
	if (ter_state == NULL)
		goto error;

	/* setup the state */
	state->i2c = i2c;
	state->config = config;
	state->ter_state = ter_state;
	state->fe.ops = stv0367ter_ops;
	state->fe.demodulator_priv = state;
	state->chip_id = stv0367_readreg(state, 0xf000);

	/* demod operation options */
	state->use_i2c_gatectrl = 1;
	state->deftabs = STV0367_DEFTAB_GENERIC;
	state->reinit_on_setfrontend = 1;
	state->auto_if_khz = 0;

	dprintk("%s: chip_id = 0x%x\n", __func__, state->chip_id);

	/* check if the demod is there */
	if ((state->chip_id != 0x50) && (state->chip_id != 0x60))
		goto error;

	return &state->fe;

error:
	kfree(ter_state);
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL_GPL(stv0367ter_attach);

static int stv0367cab_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct stv0367_state *state = fe->demodulator_priv;

	dprintk("%s:\n", __func__);

	stv0367_writebits(state, F367CAB_I2CT_ON, (enable > 0) ? 1 : 0);

	return 0;
}

static u32 stv0367cab_get_mclk(struct dvb_frontend *fe, u32 ExtClk_Hz)
{
	struct stv0367_state *state = fe->demodulator_priv;
	u32 mclk_Hz = 0;/* master clock frequency (Hz) */
	u32 M, N, P;


	if (stv0367_readbits(state, F367CAB_BYPASS_PLLXN) == 0) {
		N = (u32)stv0367_readbits(state, F367CAB_PLL_NDIV);
		if (N == 0)
			N = N + 1;

		M = (u32)stv0367_readbits(state, F367CAB_PLL_MDIV);
		if (M == 0)
			M = M + 1;

		P = (u32)stv0367_readbits(state, F367CAB_PLL_PDIV);

		if (P > 5)
			P = 5;

		mclk_Hz = ((ExtClk_Hz / 2) * N) / (M * (1 << P));
		dprintk("stv0367cab_get_mclk BYPASS_PLLXN mclk_Hz=%d\n",
								mclk_Hz);
	} else
		mclk_Hz = ExtClk_Hz;

	dprintk("stv0367cab_get_mclk final mclk_Hz=%d\n", mclk_Hz);

	return mclk_Hz;
}

static u32 stv0367cab_get_adc_freq(struct dvb_frontend *fe, u32 ExtClk_Hz)
{
	return stv0367cab_get_mclk(fe, ExtClk_Hz);
}

static enum stv0367cab_mod stv0367cab_SetQamSize(struct stv0367_state *state,
						 u32 SymbolRate,
						 enum stv0367cab_mod QAMSize)
{
	/* Set QAM size */
	stv0367_writebits(state, F367CAB_QAM_MODE, QAMSize);

	/* Set Registers settings specific to the QAM size */
	switch (QAMSize) {
	case FE_CAB_MOD_QAM4:
		stv0367_writereg(state, R367CAB_IQDEM_ADJ_AGC_REF, 0x00);
		break;
	case FE_CAB_MOD_QAM16:
		stv0367_writereg(state, R367CAB_AGC_PWR_REF_L, 0x64);
		stv0367_writereg(state, R367CAB_IQDEM_ADJ_AGC_REF, 0x00);
		stv0367_writereg(state, R367CAB_FSM_STATE, 0x90);
		stv0367_writereg(state, R367CAB_EQU_CTR_LPF_GAIN, 0xc1);
		stv0367_writereg(state, R367CAB_EQU_CRL_LPF_GAIN, 0xa7);
		stv0367_writereg(state, R367CAB_EQU_CRL_LD_SEN, 0x95);
		stv0367_writereg(state, R367CAB_EQU_CRL_LIMITER, 0x40);
		stv0367_writereg(state, R367CAB_EQU_PNT_GAIN, 0x8a);
		break;
	case FE_CAB_MOD_QAM32:
		stv0367_writereg(state, R367CAB_IQDEM_ADJ_AGC_REF, 0x00);
		stv0367_writereg(state, R367CAB_AGC_PWR_REF_L, 0x6e);
		stv0367_writereg(state, R367CAB_FSM_STATE, 0xb0);
		stv0367_writereg(state, R367CAB_EQU_CTR_LPF_GAIN, 0xc1);
		stv0367_writereg(state, R367CAB_EQU_CRL_LPF_GAIN, 0xb7);
		stv0367_writereg(state, R367CAB_EQU_CRL_LD_SEN, 0x9d);
		stv0367_writereg(state, R367CAB_EQU_CRL_LIMITER, 0x7f);
		stv0367_writereg(state, R367CAB_EQU_PNT_GAIN, 0xa7);
		break;
	case FE_CAB_MOD_QAM64:
		stv0367_writereg(state, R367CAB_IQDEM_ADJ_AGC_REF, 0x82);
		stv0367_writereg(state, R367CAB_AGC_PWR_REF_L, 0x5a);
		if (SymbolRate > 4500000) {
			stv0367_writereg(state, R367CAB_FSM_STATE, 0xb0);
			stv0367_writereg(state, R367CAB_EQU_CTR_LPF_GAIN, 0xc1);
			stv0367_writereg(state, R367CAB_EQU_CRL_LPF_GAIN, 0xa5);
		} else if (SymbolRate > 2500000) {
			stv0367_writereg(state, R367CAB_FSM_STATE, 0xa0);
			stv0367_writereg(state, R367CAB_EQU_CTR_LPF_GAIN, 0xc1);
			stv0367_writereg(state, R367CAB_EQU_CRL_LPF_GAIN, 0xa6);
		} else {
			stv0367_writereg(state, R367CAB_FSM_STATE, 0xa0);
			stv0367_writereg(state, R367CAB_EQU_CTR_LPF_GAIN, 0xd1);
			stv0367_writereg(state, R367CAB_EQU_CRL_LPF_GAIN, 0xa7);
		}
		stv0367_writereg(state, R367CAB_EQU_CRL_LD_SEN, 0x95);
		stv0367_writereg(state, R367CAB_EQU_CRL_LIMITER, 0x40);
		stv0367_writereg(state, R367CAB_EQU_PNT_GAIN, 0x99);
		break;
	case FE_CAB_MOD_QAM128:
		stv0367_writereg(state, R367CAB_IQDEM_ADJ_AGC_REF, 0x00);
		stv0367_writereg(state, R367CAB_AGC_PWR_REF_L, 0x76);
		stv0367_writereg(state, R367CAB_FSM_STATE, 0x90);
		stv0367_writereg(state, R367CAB_EQU_CTR_LPF_GAIN, 0xb1);
		if (SymbolRate > 4500000)
			stv0367_writereg(state, R367CAB_EQU_CRL_LPF_GAIN, 0xa7);
		else if (SymbolRate > 2500000)
			stv0367_writereg(state, R367CAB_EQU_CRL_LPF_GAIN, 0xa6);
		else
			stv0367_writereg(state, R367CAB_EQU_CRL_LPF_GAIN, 0x97);

		stv0367_writereg(state, R367CAB_EQU_CRL_LD_SEN, 0x8e);
		stv0367_writereg(state, R367CAB_EQU_CRL_LIMITER, 0x7f);
		stv0367_writereg(state, R367CAB_EQU_PNT_GAIN, 0xa7);
		break;
	case FE_CAB_MOD_QAM256:
		stv0367_writereg(state, R367CAB_IQDEM_ADJ_AGC_REF, 0x94);
		stv0367_writereg(state, R367CAB_AGC_PWR_REF_L, 0x5a);
		stv0367_writereg(state, R367CAB_FSM_STATE, 0xa0);
		if (SymbolRate > 4500000)
			stv0367_writereg(state, R367CAB_EQU_CTR_LPF_GAIN, 0xc1);
		else if (SymbolRate > 2500000)
			stv0367_writereg(state, R367CAB_EQU_CTR_LPF_GAIN, 0xc1);
		else
			stv0367_writereg(state, R367CAB_EQU_CTR_LPF_GAIN, 0xd1);

		stv0367_writereg(state, R367CAB_EQU_CRL_LPF_GAIN, 0xa7);
		stv0367_writereg(state, R367CAB_EQU_CRL_LD_SEN, 0x85);
		stv0367_writereg(state, R367CAB_EQU_CRL_LIMITER, 0x40);
		stv0367_writereg(state, R367CAB_EQU_PNT_GAIN, 0xa7);
		break;
	case FE_CAB_MOD_QAM512:
		stv0367_writereg(state, R367CAB_IQDEM_ADJ_AGC_REF, 0x00);
		break;
	case FE_CAB_MOD_QAM1024:
		stv0367_writereg(state, R367CAB_IQDEM_ADJ_AGC_REF, 0x00);
		break;
	default:
		break;
	}

	return QAMSize;
}

static u32 stv0367cab_set_derot_freq(struct stv0367_state *state,
					u32 adc_hz, s32 derot_hz)
{
	u32 sampled_if = 0;
	u32 adc_khz;

	adc_khz = adc_hz / 1000;

	dprintk("%s: adc_hz=%d derot_hz=%d\n", __func__, adc_hz, derot_hz);

	if (adc_khz != 0) {
		if (derot_hz < 1000000)
			derot_hz = adc_hz / 4; /* ZIF operation */
		if (derot_hz > adc_hz)
			derot_hz = derot_hz - adc_hz;
		sampled_if = (u32)derot_hz / 1000;
		sampled_if *= 32768;
		sampled_if /= adc_khz;
		sampled_if *= 256;
	}

	if (sampled_if > 8388607)
		sampled_if = 8388607;

	dprintk("%s: sampled_if=0x%x\n", __func__, sampled_if);

	stv0367_writereg(state, R367CAB_MIX_NCO_LL, sampled_if);
	stv0367_writereg(state, R367CAB_MIX_NCO_HL, (sampled_if >> 8));
	stv0367_writebits(state, F367CAB_MIX_NCO_INC_HH, (sampled_if >> 16));

	return derot_hz;
}

static u32 stv0367cab_get_derot_freq(struct stv0367_state *state, u32 adc_hz)
{
	u32 sampled_if;

	sampled_if = stv0367_readbits(state, F367CAB_MIX_NCO_INC_LL) +
			(stv0367_readbits(state, F367CAB_MIX_NCO_INC_HL) << 8) +
			(stv0367_readbits(state, F367CAB_MIX_NCO_INC_HH) << 16);

	sampled_if /= 256;
	sampled_if *= (adc_hz / 1000);
	sampled_if += 1;
	sampled_if /= 32768;

	return sampled_if;
}

static u32 stv0367cab_set_srate(struct stv0367_state *state, u32 adc_hz,
			u32 mclk_hz, u32 SymbolRate,
			enum stv0367cab_mod QAMSize)
{
	u32 QamSizeCorr = 0;
	u32 u32_tmp = 0, u32_tmp1 = 0;
	u32 adp_khz;

	dprintk("%s:\n", __func__);

	/* Set Correction factor of SRC gain */
	switch (QAMSize) {
	case FE_CAB_MOD_QAM4:
		QamSizeCorr = 1110;
		break;
	case FE_CAB_MOD_QAM16:
		QamSizeCorr = 1032;
		break;
	case FE_CAB_MOD_QAM32:
		QamSizeCorr =  954;
		break;
	case FE_CAB_MOD_QAM64:
		QamSizeCorr =  983;
		break;
	case FE_CAB_MOD_QAM128:
		QamSizeCorr =  957;
		break;
	case FE_CAB_MOD_QAM256:
		QamSizeCorr =  948;
		break;
	case FE_CAB_MOD_QAM512:
		QamSizeCorr =    0;
		break;
	case FE_CAB_MOD_QAM1024:
		QamSizeCorr =  944;
		break;
	default:
		break;
	}

	/* Transfer ratio calculation */
	if (adc_hz != 0) {
		u32_tmp = 256 * SymbolRate;
		u32_tmp = u32_tmp / adc_hz;
	}
	stv0367_writereg(state, R367CAB_EQU_CRL_TFR, (u8)u32_tmp);

	/* Symbol rate and SRC gain calculation */
	adp_khz = (mclk_hz >> 1) / 1000;/* TRL works at half the system clock */
	if (adp_khz != 0) {
		u32_tmp = SymbolRate;
		u32_tmp1 = SymbolRate;

		if (u32_tmp < 2097152) { /* 2097152 = 2^21 */
			/* Symbol rate calculation */
			u32_tmp *= 2048; /* 2048 = 2^11 */
			u32_tmp = u32_tmp / adp_khz;
			u32_tmp = u32_tmp * 16384; /* 16384 = 2^14 */
			u32_tmp /= 125 ; /* 125 = 1000/2^3 */
			u32_tmp = u32_tmp * 8; /* 8 = 2^3 */

			/* SRC Gain Calculation */
			u32_tmp1 *= 2048; /* *2*2^10 */
			u32_tmp1 /= 439; /* *2/878 */
			u32_tmp1 *= 256; /* *2^8 */
			u32_tmp1 = u32_tmp1 / adp_khz; /* /(AdpClk in kHz) */
			u32_tmp1 *= QamSizeCorr * 9; /* *1000*corr factor */
			u32_tmp1 = u32_tmp1 / 10000000;

		} else if (u32_tmp < 4194304) { /* 4194304 = 2**22 */
			/* Symbol rate calculation */
			u32_tmp *= 1024 ; /* 1024 = 2**10 */
			u32_tmp = u32_tmp / adp_khz;
			u32_tmp = u32_tmp * 16384; /* 16384 = 2**14 */
			u32_tmp /= 125 ; /* 125 = 1000/2**3 */
			u32_tmp = u32_tmp * 16; /* 16 = 2**4 */

			/* SRC Gain Calculation */
			u32_tmp1 *= 1024; /* *2*2^9 */
			u32_tmp1 /= 439; /* *2/878 */
			u32_tmp1 *= 256; /* *2^8 */
			u32_tmp1 = u32_tmp1 / adp_khz; /* /(AdpClk in kHz)*/
			u32_tmp1 *= QamSizeCorr * 9; /* *1000*corr factor */
			u32_tmp1 = u32_tmp1 / 5000000;
		} else if (u32_tmp < 8388607) { /* 8388607 = 2**23 */
			/* Symbol rate calculation */
			u32_tmp *= 512 ; /* 512 = 2**9 */
			u32_tmp = u32_tmp / adp_khz;
			u32_tmp = u32_tmp * 16384; /* 16384 = 2**14 */
			u32_tmp /= 125 ; /* 125 = 1000/2**3 */
			u32_tmp = u32_tmp * 32; /* 32 = 2**5 */

			/* SRC Gain Calculation */
			u32_tmp1 *= 512; /* *2*2^8 */
			u32_tmp1 /= 439; /* *2/878 */
			u32_tmp1 *= 256; /* *2^8 */
			u32_tmp1 = u32_tmp1 / adp_khz; /* /(AdpClk in kHz) */
			u32_tmp1 *= QamSizeCorr * 9; /* *1000*corr factor */
			u32_tmp1 = u32_tmp1 / 2500000;
		} else {
			/* Symbol rate calculation */
			u32_tmp *= 256 ; /* 256 = 2**8 */
			u32_tmp = u32_tmp / adp_khz;
			u32_tmp = u32_tmp * 16384; /* 16384 = 2**13 */
			u32_tmp /= 125 ; /* 125 = 1000/2**3 */
			u32_tmp = u32_tmp * 64; /* 64 = 2**6 */

			/* SRC Gain Calculation */
			u32_tmp1 *= 256; /* 2*2^7 */
			u32_tmp1 /= 439; /* *2/878 */
			u32_tmp1 *= 256; /* *2^8 */
			u32_tmp1 = u32_tmp1 / adp_khz; /* /(AdpClk in kHz) */
			u32_tmp1 *= QamSizeCorr * 9; /* *1000*corr factor */
			u32_tmp1 = u32_tmp1 / 1250000;
		}
	}
#if 0
	/* Filters' coefficients are calculated and written
	into registers only if the filters are enabled */
	if (stv0367_readbits(state, F367CAB_ADJ_EN)) {
		stv0367cab_SetIirAdjacentcoefficient(state, mclk_hz,
								SymbolRate);
		/* AllPass filter must be enabled
		when the adjacents filter is used */
		stv0367_writebits(state, F367CAB_ALLPASSFILT_EN, 1);
		stv0367cab_SetAllPasscoefficient(state, mclk_hz, SymbolRate);
	} else
		/* AllPass filter must be disabled
		when the adjacents filter is not used */
#endif
	stv0367_writebits(state, F367CAB_ALLPASSFILT_EN, 0);

	stv0367_writereg(state, R367CAB_SRC_NCO_LL, u32_tmp);
	stv0367_writereg(state, R367CAB_SRC_NCO_LH, (u32_tmp >> 8));
	stv0367_writereg(state, R367CAB_SRC_NCO_HL, (u32_tmp >> 16));
	stv0367_writereg(state, R367CAB_SRC_NCO_HH, (u32_tmp >> 24));

	stv0367_writereg(state, R367CAB_IQDEM_GAIN_SRC_L, u32_tmp1 & 0x00ff);
	stv0367_writebits(state, F367CAB_GAIN_SRC_HI, (u32_tmp1 >> 8) & 0x00ff);

	return SymbolRate ;
}

static u32 stv0367cab_GetSymbolRate(struct stv0367_state *state, u32 mclk_hz)
{
	u32 regsym;
	u32 adp_khz;

	regsym = stv0367_readreg(state, R367CAB_SRC_NCO_LL) +
		(stv0367_readreg(state, R367CAB_SRC_NCO_LH) << 8) +
		(stv0367_readreg(state, R367CAB_SRC_NCO_HL) << 16) +
		(stv0367_readreg(state, R367CAB_SRC_NCO_HH) << 24);

	adp_khz = (mclk_hz >> 1) / 1000;/* TRL works at half the system clock */

	if (regsym < 134217728) {		/* 134217728L = 2**27*/
		regsym = regsym * 32;		/* 32 = 2**5 */
		regsym = regsym / 32768;	/* 32768L = 2**15 */
		regsym = adp_khz * regsym;	/* AdpClk in kHz */
		regsym = regsym / 128;		/* 128 = 2**7 */
		regsym *= 125 ;			/* 125 = 1000/2**3 */
		regsym /= 2048 ;		/* 2048 = 2**11	*/
	} else if (regsym < 268435456) {	/* 268435456L = 2**28 */
		regsym = regsym * 16;		/* 16 = 2**4 */
		regsym = regsym / 32768;	/* 32768L = 2**15 */
		regsym = adp_khz * regsym;	/* AdpClk in kHz */
		regsym = regsym / 128;		/* 128 = 2**7 */
		regsym *= 125 ;			/* 125 = 1000/2**3*/
		regsym /= 1024 ;		/* 256 = 2**10*/
	} else if (regsym < 536870912) {	/* 536870912L = 2**29*/
		regsym = regsym * 8;		/* 8 = 2**3 */
		regsym = regsym / 32768;	/* 32768L = 2**15 */
		regsym = adp_khz * regsym;	/* AdpClk in kHz */
		regsym = regsym / 128;		/* 128 = 2**7 */
		regsym *= 125 ;			/* 125 = 1000/2**3 */
		regsym /= 512 ;			/* 128 = 2**9 */
	} else {
		regsym = regsym * 4;		/* 4 = 2**2 */
		regsym = regsym / 32768;	/* 32768L = 2**15 */
		regsym = adp_khz * regsym;	/* AdpClk in kHz */
		regsym = regsym / 128;		/* 128 = 2**7 */
		regsym *= 125 ;			/* 125 = 1000/2**3 */
		regsym /= 256 ;			/* 64 = 2**8 */
	}

	return regsym;
}

static u32 stv0367cab_fsm_status(struct stv0367_state *state)
{
	return stv0367_readbits(state, F367CAB_FSM_STATUS);
}

static u32 stv0367cab_qamfec_lock(struct stv0367_state *state)
{
	return stv0367_readbits(state,
		(state->cab_state->qamfec_status_reg ?
		 state->cab_state->qamfec_status_reg :
		 F367CAB_QAMFEC_LOCK));
}

static
enum stv0367_cab_signal_type stv0367cab_fsm_signaltype(u32 qam_fsm_status)
{
	enum stv0367_cab_signal_type signaltype = FE_CAB_NOAGC;

	switch (qam_fsm_status) {
	case 1:
		signaltype = FE_CAB_NOAGC;
		break;
	case 2:
		signaltype = FE_CAB_NOTIMING;
		break;
	case 3:
		signaltype = FE_CAB_TIMINGOK;
		break;
	case 4:
		signaltype = FE_CAB_NOCARRIER;
		break;
	case 5:
		signaltype = FE_CAB_CARRIEROK;
		break;
	case 7:
		signaltype = FE_CAB_NOBLIND;
		break;
	case 8:
		signaltype = FE_CAB_BLINDOK;
		break;
	case 10:
		signaltype = FE_CAB_NODEMOD;
		break;
	case 11:
		signaltype = FE_CAB_DEMODOK;
		break;
	case 12:
		signaltype = FE_CAB_DEMODOK;
		break;
	case 13:
		signaltype = FE_CAB_NODEMOD;
		break;
	case 14:
		signaltype = FE_CAB_NOBLIND;
		break;
	case 15:
		signaltype = FE_CAB_NOSIGNAL;
		break;
	default:
		break;
	}

	return signaltype;
}

static int stv0367cab_read_status(struct dvb_frontend *fe,
				  enum fe_status *status)
{
	struct stv0367_state *state = fe->demodulator_priv;

	dprintk("%s:\n", __func__);

	*status = 0;

	/* update cab_state->state from QAM_FSM_STATUS */
	state->cab_state->state = stv0367cab_fsm_signaltype(
		stv0367cab_fsm_status(state));

	if (stv0367cab_qamfec_lock(state)) {
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI
			  | FE_HAS_SYNC | FE_HAS_LOCK;
		dprintk("%s: stv0367 has locked\n", __func__);
	} else {
		if (state->cab_state->state > FE_CAB_NOSIGNAL)
			*status |= FE_HAS_SIGNAL;

		if (state->cab_state->state > FE_CAB_NOCARRIER)
			*status |= FE_HAS_CARRIER;

		if (state->cab_state->state >= FE_CAB_DEMODOK)
			*status |= FE_HAS_VITERBI;

		if (state->cab_state->state >= FE_CAB_DATAOK)
			*status |= FE_HAS_SYNC;
	}

	return 0;
}

static int stv0367cab_standby(struct dvb_frontend *fe, u8 standby_on)
{
	struct stv0367_state *state = fe->demodulator_priv;

	dprintk("%s:\n", __func__);

	if (standby_on) {
		stv0367_writebits(state, F367CAB_BYPASS_PLLXN, 0x03);
		stv0367_writebits(state, F367CAB_STDBY_PLLXN, 0x01);
		stv0367_writebits(state, F367CAB_STDBY, 1);
		stv0367_writebits(state, F367CAB_STDBY_CORE, 1);
		stv0367_writebits(state, F367CAB_EN_BUFFER_I, 0);
		stv0367_writebits(state, F367CAB_EN_BUFFER_Q, 0);
		stv0367_writebits(state, F367CAB_POFFQ, 1);
		stv0367_writebits(state, F367CAB_POFFI, 1);
	} else {
		stv0367_writebits(state, F367CAB_STDBY_PLLXN, 0x00);
		stv0367_writebits(state, F367CAB_BYPASS_PLLXN, 0x00);
		stv0367_writebits(state, F367CAB_STDBY, 0);
		stv0367_writebits(state, F367CAB_STDBY_CORE, 0);
		stv0367_writebits(state, F367CAB_EN_BUFFER_I, 1);
		stv0367_writebits(state, F367CAB_EN_BUFFER_Q, 1);
		stv0367_writebits(state, F367CAB_POFFQ, 0);
		stv0367_writebits(state, F367CAB_POFFI, 0);
	}

	return 0;
}

static int stv0367cab_sleep(struct dvb_frontend *fe)
{
	return stv0367cab_standby(fe, 1);
}

static int stv0367cab_init(struct dvb_frontend *fe)
{
	struct stv0367_state *state = fe->demodulator_priv;
	struct stv0367cab_state *cab_state = state->cab_state;

	dprintk("%s:\n", __func__);

	stv0367_write_table(state,
		stv0367_deftabs[state->deftabs][STV0367_TAB_CAB]);

	switch (state->config->ts_mode) {
	case STV0367_DVBCI_CLOCK:
		dprintk("Setting TSMode = STV0367_DVBCI_CLOCK\n");
		stv0367_writebits(state, F367CAB_OUTFORMAT, 0x03);
		break;
	case STV0367_SERIAL_PUNCT_CLOCK:
	case STV0367_SERIAL_CONT_CLOCK:
		stv0367_writebits(state, F367CAB_OUTFORMAT, 0x01);
		break;
	case STV0367_PARALLEL_PUNCT_CLOCK:
	case STV0367_OUTPUTMODE_DEFAULT:
		stv0367_writebits(state, F367CAB_OUTFORMAT, 0x00);
		break;
	}

	switch (state->config->clk_pol) {
	case STV0367_RISINGEDGE_CLOCK:
		stv0367_writebits(state, F367CAB_CLK_POLARITY, 0x00);
		break;
	case STV0367_FALLINGEDGE_CLOCK:
	case STV0367_CLOCKPOLARITY_DEFAULT:
		stv0367_writebits(state, F367CAB_CLK_POLARITY, 0x01);
		break;
	}

	stv0367_writebits(state, F367CAB_SYNC_STRIP, 0x00);

	stv0367_writebits(state, F367CAB_CT_NBST, 0x01);

	stv0367_writebits(state, F367CAB_TS_SWAP, 0x01);

	stv0367_writebits(state, F367CAB_FIFO_BYPASS, 0x00);

	stv0367_writereg(state, R367CAB_ANACTRL, 0x00);/*PLL enabled and used */

	cab_state->mclk = stv0367cab_get_mclk(fe, state->config->xtal);
	cab_state->adc_clk = stv0367cab_get_adc_freq(fe, state->config->xtal);

	return 0;
}
static
enum stv0367_cab_signal_type stv0367cab_algo(struct stv0367_state *state,
					     struct dtv_frontend_properties *p)
{
	struct stv0367cab_state *cab_state = state->cab_state;
	enum stv0367_cab_signal_type signalType = FE_CAB_NOAGC;
	u32	QAMFEC_Lock, QAM_Lock, u32_tmp, ifkhz,
		LockTime, TRLTimeOut, AGCTimeOut, CRLSymbols,
		CRLTimeOut, EQLTimeOut, DemodTimeOut, FECTimeOut;
	u8	TrackAGCAccum;
	s32	tmp;

	dprintk("%s:\n", __func__);

	stv0367_get_if_khz(state, &ifkhz);

	/* Timeouts calculation */
	/* A max lock time of 25 ms is allowed for delayed AGC */
	AGCTimeOut = 25;
	/* 100000 symbols needed by the TRL as a maximum value */
	TRLTimeOut = 100000000 / p->symbol_rate;
	/* CRLSymbols is the needed number of symbols to achieve a lock
	   within [-4%, +4%] of the symbol rate.
	   CRL timeout is calculated
	   for a lock within [-search_range, +search_range].
	   EQL timeout can be changed depending on
	   the micro-reflections we want to handle.
	   A characterization must be performed
	   with these echoes to get new timeout values.
	*/
	switch (p->modulation) {
	case QAM_16:
		CRLSymbols = 150000;
		EQLTimeOut = 100;
		break;
	case QAM_32:
		CRLSymbols = 250000;
		EQLTimeOut = 100;
		break;
	case QAM_64:
		CRLSymbols = 200000;
		EQLTimeOut = 100;
		break;
	case QAM_128:
		CRLSymbols = 250000;
		EQLTimeOut = 100;
		break;
	case QAM_256:
		CRLSymbols = 250000;
		EQLTimeOut = 100;
		break;
	default:
		CRLSymbols = 200000;
		EQLTimeOut = 100;
		break;
	}
#if 0
	if (pIntParams->search_range < 0) {
		CRLTimeOut = (25 * CRLSymbols *
				(-pIntParams->search_range / 1000)) /
					(pIntParams->symbol_rate / 1000);
	} else
#endif
	CRLTimeOut = (25 * CRLSymbols * (cab_state->search_range / 1000)) /
					(p->symbol_rate / 1000);

	CRLTimeOut = (1000 * CRLTimeOut) / p->symbol_rate;
	/* Timeouts below 50ms are coerced */
	if (CRLTimeOut < 50)
		CRLTimeOut = 50;
	/* A maximum of 100 TS packets is needed to get FEC lock even in case
	the spectrum inversion needs to be changed.
	   This is equal to 20 ms in case of the lowest symbol rate of 0.87Msps
	*/
	FECTimeOut = 20;
	DemodTimeOut = AGCTimeOut + TRLTimeOut + CRLTimeOut + EQLTimeOut;

	dprintk("%s: DemodTimeOut=%d\n", __func__, DemodTimeOut);

	/* Reset the TRL to ensure nothing starts until the
	   AGC is stable which ensures a better lock time
	*/
	stv0367_writereg(state, R367CAB_CTRL_1, 0x04);
	/* Set AGC accumulation time to minimum and lock threshold to maximum
	in order to speed up the AGC lock */
	TrackAGCAccum = stv0367_readbits(state, F367CAB_AGC_ACCUMRSTSEL);
	stv0367_writebits(state, F367CAB_AGC_ACCUMRSTSEL, 0x0);
	/* Modulus Mapper is disabled */
	stv0367_writebits(state, F367CAB_MODULUSMAP_EN, 0);
	/* Disable the sweep function */
	stv0367_writebits(state, F367CAB_SWEEP_EN, 0);
	/* The sweep function is never used, Sweep rate must be set to 0 */
	/* Set the derotator frequency in Hz */
	stv0367cab_set_derot_freq(state, cab_state->adc_clk,
		(1000 * (s32)ifkhz + cab_state->derot_offset));
	/* Disable the Allpass Filter when the symbol rate is out of range */
	if ((p->symbol_rate > 10800000) | (p->symbol_rate < 1800000)) {
		stv0367_writebits(state, F367CAB_ADJ_EN, 0);
		stv0367_writebits(state, F367CAB_ALLPASSFILT_EN, 0);
	}
#if 0
	/* Check if the tuner is locked */
	tuner_lock = stv0367cab_tuner_get_status(fe);
	if (tuner_lock == 0)
		return FE_367CAB_NOTUNER;
#endif
	/* Release the TRL to start demodulator acquisition */
	/* Wait for QAM lock */
	LockTime = 0;
	stv0367_writereg(state, R367CAB_CTRL_1, 0x00);
	do {
		QAM_Lock = stv0367cab_fsm_status(state);
		if ((LockTime >= (DemodTimeOut - EQLTimeOut)) &&
							(QAM_Lock == 0x04))
			/*
			 * We don't wait longer, the frequency/phase offset
			 * must be too big
			 */
			LockTime = DemodTimeOut;
		else if ((LockTime >= (AGCTimeOut + TRLTimeOut)) &&
							(QAM_Lock == 0x02))
			/*
			 * We don't wait longer, either there is no signal or
			 * it is not the right symbol rate or it is an analog
			 * carrier
			 */
		{
			LockTime = DemodTimeOut;
			u32_tmp = stv0367_readbits(state,
						F367CAB_AGC_PWR_WORD_LO) +
					(stv0367_readbits(state,
						F367CAB_AGC_PWR_WORD_ME) << 8) +
					(stv0367_readbits(state,
						F367CAB_AGC_PWR_WORD_HI) << 16);
			if (u32_tmp >= 131072)
				u32_tmp = 262144 - u32_tmp;
			u32_tmp = u32_tmp / (1 << (11 - stv0367_readbits(state,
							F367CAB_AGC_IF_BWSEL)));

			if (u32_tmp < stv0367_readbits(state,
						F367CAB_AGC_PWRREF_LO) +
					256 * stv0367_readbits(state,
						F367CAB_AGC_PWRREF_HI) - 10)
				QAM_Lock = 0x0f;
		} else {
			usleep_range(10000, 20000);
			LockTime += 10;
		}
		dprintk("QAM_Lock=0x%x LockTime=%d\n", QAM_Lock, LockTime);
		tmp = stv0367_readreg(state, R367CAB_IT_STATUS1);

		dprintk("R367CAB_IT_STATUS1=0x%x\n", tmp);

	} while (((QAM_Lock != 0x0c) && (QAM_Lock != 0x0b)) &&
						(LockTime < DemodTimeOut));

	dprintk("QAM_Lock=0x%x\n", QAM_Lock);

	tmp = stv0367_readreg(state, R367CAB_IT_STATUS1);
	dprintk("R367CAB_IT_STATUS1=0x%x\n", tmp);
	tmp = stv0367_readreg(state, R367CAB_IT_STATUS2);
	dprintk("R367CAB_IT_STATUS2=0x%x\n", tmp);

	tmp  = stv0367cab_get_derot_freq(state, cab_state->adc_clk);
	dprintk("stv0367cab_get_derot_freq=0x%x\n", tmp);

	if ((QAM_Lock == 0x0c) || (QAM_Lock == 0x0b)) {
		/* Wait for FEC lock */
		LockTime = 0;
		do {
			usleep_range(5000, 7000);
			LockTime += 5;
			QAMFEC_Lock = stv0367cab_qamfec_lock(state);
		} while (!QAMFEC_Lock && (LockTime < FECTimeOut));
	} else
		QAMFEC_Lock = 0;

	if (QAMFEC_Lock) {
		signalType = FE_CAB_DATAOK;
		cab_state->spect_inv = stv0367_readbits(state,
							F367CAB_QUAD_INV);
#if 0
/* not clear for me */
		if (ifkhz != 0) {
			if (ifkhz > cab_state->adc_clk / 1000) {
				cab_state->freq_khz =
					FE_Cab_TunerGetFrequency(pIntParams->hTuner)
				- stv0367cab_get_derot_freq(state, cab_state->adc_clk)
				- cab_state->adc_clk / 1000 + ifkhz;
			} else {
				cab_state->freq_khz =
						FE_Cab_TunerGetFrequency(pIntParams->hTuner)
						- stv0367cab_get_derot_freq(state, cab_state->adc_clk)
						+ ifkhz;
			}
		} else {
			cab_state->freq_khz =
				FE_Cab_TunerGetFrequency(pIntParams->hTuner) +
				stv0367cab_get_derot_freq(state,
							cab_state->adc_clk) -
				cab_state->adc_clk / 4000;
		}
#endif
		cab_state->symbol_rate = stv0367cab_GetSymbolRate(state,
							cab_state->mclk);
		cab_state->locked = 1;

		/* stv0367_setbits(state, F367CAB_AGC_ACCUMRSTSEL,7);*/
	} else
		signalType = stv0367cab_fsm_signaltype(QAM_Lock);

	/* Set the AGC control values to tracking values */
	stv0367_writebits(state, F367CAB_AGC_ACCUMRSTSEL, TrackAGCAccum);
	return signalType;
}

static int stv0367cab_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct stv0367_state *state = fe->demodulator_priv;
	struct stv0367cab_state *cab_state = state->cab_state;
	enum stv0367cab_mod QAMSize = 0;

	dprintk("%s: freq = %d, srate = %d\n", __func__,
					p->frequency, p->symbol_rate);

	cab_state->derot_offset = 0;

	switch (p->modulation) {
	case QAM_16:
		QAMSize = FE_CAB_MOD_QAM16;
		break;
	case QAM_32:
		QAMSize = FE_CAB_MOD_QAM32;
		break;
	case QAM_64:
		QAMSize = FE_CAB_MOD_QAM64;
		break;
	case QAM_128:
		QAMSize = FE_CAB_MOD_QAM128;
		break;
	case QAM_256:
		QAMSize = FE_CAB_MOD_QAM256;
		break;
	default:
		break;
	}

	if (state->reinit_on_setfrontend)
		stv0367cab_init(fe);

	/* Tuner Frequency Setting */
	if (fe->ops.tuner_ops.set_params) {
		if (state->use_i2c_gatectrl && fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);
		fe->ops.tuner_ops.set_params(fe);
		if (state->use_i2c_gatectrl && fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	stv0367cab_SetQamSize(
			state,
			p->symbol_rate,
			QAMSize);

	stv0367cab_set_srate(state,
			cab_state->adc_clk,
			cab_state->mclk,
			p->symbol_rate,
			QAMSize);
	/* Search algorithm launch, [-1.1*RangeOffset, +1.1*RangeOffset] scan */
	cab_state->state = stv0367cab_algo(state, p);
	return 0;
}

static int stv0367cab_get_frontend(struct dvb_frontend *fe,
				   struct dtv_frontend_properties *p)
{
	struct stv0367_state *state = fe->demodulator_priv;
	struct stv0367cab_state *cab_state = state->cab_state;
	u32 ifkhz = 0;

	enum stv0367cab_mod QAMSize;

	dprintk("%s:\n", __func__);

	stv0367_get_if_khz(state, &ifkhz);
	p->symbol_rate = stv0367cab_GetSymbolRate(state, cab_state->mclk);

	QAMSize = stv0367_readbits(state, F367CAB_QAM_MODE);
	switch (QAMSize) {
	case FE_CAB_MOD_QAM16:
		p->modulation = QAM_16;
		break;
	case FE_CAB_MOD_QAM32:
		p->modulation = QAM_32;
		break;
	case FE_CAB_MOD_QAM64:
		p->modulation = QAM_64;
		break;
	case FE_CAB_MOD_QAM128:
		p->modulation = QAM_128;
		break;
	case FE_CAB_MOD_QAM256:
		p->modulation = QAM_256;
		break;
	default:
		break;
	}

	p->frequency = stv0367_get_tuner_freq(fe);

	dprintk("%s: tuner frequency = %d\n", __func__, p->frequency);

	if (ifkhz == 0) {
		p->frequency +=
			(stv0367cab_get_derot_freq(state, cab_state->adc_clk) -
			cab_state->adc_clk / 4000);
		return 0;
	}

	if (ifkhz > cab_state->adc_clk / 1000)
		p->frequency += (ifkhz
			- stv0367cab_get_derot_freq(state, cab_state->adc_clk)
			- cab_state->adc_clk / 1000);
	else
		p->frequency += (ifkhz
			- stv0367cab_get_derot_freq(state, cab_state->adc_clk));

	return 0;
}

#if 0
void stv0367cab_GetErrorCount(state, enum stv0367cab_mod QAMSize,
			u32 symbol_rate, FE_367qam_Monitor *Monitor_results)
{
	stv0367cab_OptimiseNByteAndGetBER(state, QAMSize, symbol_rate, Monitor_results);
	stv0367cab_GetPacketsCount(state, Monitor_results);

	return;
}

static int stv0367cab_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct stv0367_state *state = fe->demodulator_priv;

	return 0;
}
#endif
static s32 stv0367cab_get_rf_lvl(struct stv0367_state *state)
{
	s32 rfLevel = 0;
	s32 RfAgcPwm = 0, IfAgcPwm = 0;
	u8 i;

	stv0367_writebits(state, F367CAB_STDBY_ADCGP, 0x0);

	RfAgcPwm =
		(stv0367_readbits(state, F367CAB_RF_AGC1_LEVEL_LO) & 0x03) +
		(stv0367_readbits(state, F367CAB_RF_AGC1_LEVEL_HI) << 2);
	RfAgcPwm = 100 * RfAgcPwm / 1023;

	IfAgcPwm =
		stv0367_readbits(state, F367CAB_AGC_IF_PWMCMD_LO) +
		(stv0367_readbits(state, F367CAB_AGC_IF_PWMCMD_HI) << 8);
	if (IfAgcPwm >= 2048)
		IfAgcPwm -= 2048;
	else
		IfAgcPwm += 2048;

	IfAgcPwm = 100 * IfAgcPwm / 4095;

	/* For DTT75467 on NIM */
	if (RfAgcPwm < 90  && IfAgcPwm < 28) {
		for (i = 0; i < RF_LOOKUP_TABLE_SIZE; i++) {
			if (RfAgcPwm <= stv0367cab_RF_LookUp1[0][i]) {
				rfLevel = (-1) * stv0367cab_RF_LookUp1[1][i];
				break;
			}
		}
		if (i == RF_LOOKUP_TABLE_SIZE)
			rfLevel = -56;
	} else { /*if IF AGC>10*/
		for (i = 0; i < RF_LOOKUP_TABLE2_SIZE; i++) {
			if (IfAgcPwm <= stv0367cab_RF_LookUp2[0][i]) {
				rfLevel = (-1) * stv0367cab_RF_LookUp2[1][i];
				break;
			}
		}
		if (i == RF_LOOKUP_TABLE2_SIZE)
			rfLevel = -72;
	}
	return rfLevel;
}

static int stv0367cab_read_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct stv0367_state *state = fe->demodulator_priv;

	s32 signal =  stv0367cab_get_rf_lvl(state);

	dprintk("%s: signal=%d dBm\n", __func__, signal);

	if (signal <= -72)
		*strength = 65535;
	else
		*strength = (22 + signal) * (-1311);

	dprintk("%s: strength=%d\n", __func__, (*strength));

	return 0;
}

static int stv0367cab_snr_power(struct dvb_frontend *fe)
{
	struct stv0367_state *state = fe->demodulator_priv;
	enum stv0367cab_mod QAMSize;

	QAMSize = stv0367_readbits(state, F367CAB_QAM_MODE);
	switch (QAMSize) {
	case FE_CAB_MOD_QAM4:
		return 21904;
	case FE_CAB_MOD_QAM16:
		return 20480;
	case FE_CAB_MOD_QAM32:
		return 23040;
	case FE_CAB_MOD_QAM64:
		return 21504;
	case FE_CAB_MOD_QAM128:
		return 23616;
	case FE_CAB_MOD_QAM256:
		return 21760;
	case FE_CAB_MOD_QAM1024:
		return 21280;
	default:
		break;
	}

	return 1;
}

static int stv0367cab_snr_readreg(struct dvb_frontend *fe, int avgdiv)
{
	struct stv0367_state *state = fe->demodulator_priv;
	u32 regval = 0;
	int i;

	for (i = 0; i < 10; i++) {
		regval += (stv0367_readbits(state, F367CAB_SNR_LO)
			+ 256 * stv0367_readbits(state, F367CAB_SNR_HI));
	}

	if (avgdiv)
		regval /= 10;

	return regval;
}

static int stv0367cab_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct stv0367_state *state = fe->demodulator_priv;
	u32 noisepercentage;
	u32 regval = 0, temp = 0;
	int power;

	power = stv0367cab_snr_power(fe);
	regval = stv0367cab_snr_readreg(fe, 1);

	if (regval != 0) {
		temp = power
			* (1 << (3 + stv0367_readbits(state, F367CAB_SNR_PER)));
		temp /= regval;
	}

	/* table values, not needed to calculate logarithms */
	if (temp >= 5012)
		noisepercentage = 100;
	else if (temp >= 3981)
		noisepercentage = 93;
	else if (temp >= 3162)
		noisepercentage = 86;
	else if (temp >= 2512)
		noisepercentage = 79;
	else if (temp >= 1995)
		noisepercentage = 72;
	else if (temp >= 1585)
		noisepercentage = 65;
	else if (temp >= 1259)
		noisepercentage = 58;
	else if (temp >= 1000)
		noisepercentage = 50;
	else if (temp >= 794)
		noisepercentage = 43;
	else if (temp >= 501)
		noisepercentage = 36;
	else if (temp >= 316)
		noisepercentage = 29;
	else if (temp >= 200)
		noisepercentage = 22;
	else if (temp >= 158)
		noisepercentage = 14;
	else if (temp >= 126)
		noisepercentage = 7;
	else
		noisepercentage = 0;

	dprintk("%s: noisepercentage=%d\n", __func__, noisepercentage);

	*snr = (noisepercentage * 65535) / 100;

	return 0;
}

static int stv0367cab_read_ucblcks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct stv0367_state *state = fe->demodulator_priv;
	int corrected, tscount;

	*ucblocks = (stv0367_readreg(state, R367CAB_RS_COUNTER_5) << 8)
			| stv0367_readreg(state, R367CAB_RS_COUNTER_4);
	corrected = (stv0367_readreg(state, R367CAB_RS_COUNTER_3) << 8)
			| stv0367_readreg(state, R367CAB_RS_COUNTER_2);
	tscount = (stv0367_readreg(state, R367CAB_RS_COUNTER_2) << 8)
			| stv0367_readreg(state, R367CAB_RS_COUNTER_1);

	dprintk("%s: uncorrected blocks=%d corrected blocks=%d tscount=%d\n",
				__func__, *ucblocks, corrected, tscount);

	return 0;
};

static const struct dvb_frontend_ops stv0367cab_ops = {
	.delsys = { SYS_DVBC_ANNEX_A },
	.info = {
		.name = "ST STV0367 DVB-C",
		.frequency_min_hz =  47 * MHz,
		.frequency_max_hz = 862 * MHz,
		.frequency_stepsize_hz = 62500,
		.symbol_rate_min = 870000,
		.symbol_rate_max = 11700000,
		.caps = 0x400 |/* FE_CAN_QAM_4 */
			FE_CAN_QAM_16 | FE_CAN_QAM_32  |
			FE_CAN_QAM_64 | FE_CAN_QAM_128 |
			FE_CAN_QAM_256 | FE_CAN_FEC_AUTO
	},
	.release				= stv0367_release,
	.init					= stv0367cab_init,
	.sleep					= stv0367cab_sleep,
	.i2c_gate_ctrl				= stv0367cab_gate_ctrl,
	.set_frontend				= stv0367cab_set_frontend,
	.get_frontend				= stv0367cab_get_frontend,
	.read_status				= stv0367cab_read_status,
/*	.read_ber				= stv0367cab_read_ber, */
	.read_signal_strength			= stv0367cab_read_strength,
	.read_snr				= stv0367cab_read_snr,
	.read_ucblocks				= stv0367cab_read_ucblcks,
	.get_tune_settings			= stv0367_get_tune_settings,
};

struct dvb_frontend *stv0367cab_attach(const struct stv0367_config *config,
				   struct i2c_adapter *i2c)
{
	struct stv0367_state *state = NULL;
	struct stv0367cab_state *cab_state = NULL;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct stv0367_state), GFP_KERNEL);
	if (state == NULL)
		goto error;
	cab_state = kzalloc(sizeof(struct stv0367cab_state), GFP_KERNEL);
	if (cab_state == NULL)
		goto error;

	/* setup the state */
	state->i2c = i2c;
	state->config = config;
	cab_state->search_range = 280000;
	cab_state->qamfec_status_reg = F367CAB_QAMFEC_LOCK;
	state->cab_state = cab_state;
	state->fe.ops = stv0367cab_ops;
	state->fe.demodulator_priv = state;
	state->chip_id = stv0367_readreg(state, 0xf000);

	/* demod operation options */
	state->use_i2c_gatectrl = 1;
	state->deftabs = STV0367_DEFTAB_GENERIC;
	state->reinit_on_setfrontend = 1;
	state->auto_if_khz = 0;

	dprintk("%s: chip_id = 0x%x\n", __func__, state->chip_id);

	/* check if the demod is there */
	if ((state->chip_id != 0x50) && (state->chip_id != 0x60))
		goto error;

	return &state->fe;

error:
	kfree(cab_state);
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL_GPL(stv0367cab_attach);

/*
 * Functions for operation on Digital Devices hardware
 */

static void stv0367ddb_setup_ter(struct stv0367_state *state)
{
	stv0367_writereg(state, R367TER_DEBUG_LT4, 0x00);
	stv0367_writereg(state, R367TER_DEBUG_LT5, 0x00);
	stv0367_writereg(state, R367TER_DEBUG_LT6, 0x00); /* R367CAB_CTRL_1 */
	stv0367_writereg(state, R367TER_DEBUG_LT7, 0x00); /* R367CAB_CTRL_2 */
	stv0367_writereg(state, R367TER_DEBUG_LT8, 0x00);
	stv0367_writereg(state, R367TER_DEBUG_LT9, 0x00);

	/* Tuner Setup */
	/* Buffer Q disabled, I Enabled, unsigned ADC */
	stv0367_writereg(state, R367TER_ANADIGCTRL, 0x89);
	stv0367_writereg(state, R367TER_DUAL_AD12, 0x04); /* ADCQ disabled */

	/* Clock setup */
	/* PLL bypassed and disabled */
	stv0367_writereg(state, R367TER_ANACTRL, 0x0D);
	stv0367_writereg(state, R367TER_TOPCTRL, 0x00); /* Set OFDM */

	/* IC runs at 54 MHz with a 27 MHz crystal */
	stv0367_pll_setup(state, STV0367_ICSPEED_53125, state->config->xtal);

	msleep(50);
	/* PLL enabled and used */
	stv0367_writereg(state, R367TER_ANACTRL, 0x00);

	state->activedemod = demod_ter;
}

static void stv0367ddb_setup_cab(struct stv0367_state *state)
{
	stv0367_writereg(state, R367TER_DEBUG_LT4, 0x00);
	stv0367_writereg(state, R367TER_DEBUG_LT5, 0x01);
	stv0367_writereg(state, R367TER_DEBUG_LT6, 0x06); /* R367CAB_CTRL_1 */
	stv0367_writereg(state, R367TER_DEBUG_LT7, 0x03); /* R367CAB_CTRL_2 */
	stv0367_writereg(state, R367TER_DEBUG_LT8, 0x00);
	stv0367_writereg(state, R367TER_DEBUG_LT9, 0x00);

	/* Tuner Setup */
	/* Buffer Q disabled, I Enabled, signed ADC */
	stv0367_writereg(state, R367TER_ANADIGCTRL, 0x8B);
	/* ADCQ disabled */
	stv0367_writereg(state, R367TER_DUAL_AD12, 0x04);

	/* Clock setup */
	/* PLL bypassed and disabled */
	stv0367_writereg(state, R367TER_ANACTRL, 0x0D);
	/* Set QAM */
	stv0367_writereg(state, R367TER_TOPCTRL, 0x10);

	/* IC runs at 58 MHz with a 27 MHz crystal */
	stv0367_pll_setup(state, STV0367_ICSPEED_58000, state->config->xtal);

	msleep(50);
	/* PLL enabled and used */
	stv0367_writereg(state, R367TER_ANACTRL, 0x00);

	state->cab_state->mclk = stv0367cab_get_mclk(&state->fe,
		state->config->xtal);
	state->cab_state->adc_clk = stv0367cab_get_adc_freq(&state->fe,
		state->config->xtal);

	state->activedemod = demod_cab;
}

static int stv0367ddb_set_frontend(struct dvb_frontend *fe)
{
	struct stv0367_state *state = fe->demodulator_priv;

	switch (fe->dtv_property_cache.delivery_system) {
	case SYS_DVBT:
		if (state->activedemod != demod_ter)
			stv0367ddb_setup_ter(state);

		return stv0367ter_set_frontend(fe);
	case SYS_DVBC_ANNEX_A:
		if (state->activedemod != demod_cab)
			stv0367ddb_setup_cab(state);

		/* protect against division error oopses */
		if (fe->dtv_property_cache.symbol_rate == 0) {
			printk(KERN_ERR "Invalid symbol rate\n");
			return -EINVAL;
		}

		return stv0367cab_set_frontend(fe);
	default:
		break;
	}

	return -EINVAL;
}

static void stv0367ddb_read_signal_strength(struct dvb_frontend *fe)
{
	struct stv0367_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	s32 signalstrength;

	switch (state->activedemod) {
	case demod_cab:
		signalstrength = stv0367cab_get_rf_lvl(state) * 1000;
		break;
	default:
		p->strength.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		return;
	}

	p->strength.stat[0].scale = FE_SCALE_DECIBEL;
	p->strength.stat[0].uvalue = signalstrength;
}

static void stv0367ddb_read_snr(struct dvb_frontend *fe)
{
	struct stv0367_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	int cab_pwr;
	u32 regval, tmpval, snrval = 0;

	switch (state->activedemod) {
	case demod_ter:
		snrval = stv0367ter_snr_readreg(fe);
		break;
	case demod_cab:
		cab_pwr = stv0367cab_snr_power(fe);
		regval = stv0367cab_snr_readreg(fe, 0);

		/* prevent division by zero */
		if (!regval) {
			snrval = 0;
			break;
		}

		tmpval = (cab_pwr * 320) / regval;
		snrval = ((tmpval != 0) ? (intlog2(tmpval) / 5581) : 0);
		break;
	default:
		p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		return;
	}

	p->cnr.stat[0].scale = FE_SCALE_DECIBEL;
	p->cnr.stat[0].uvalue = snrval;
}

static void stv0367ddb_read_ucblocks(struct dvb_frontend *fe)
{
	struct stv0367_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	u32 ucblocks = 0;

	switch (state->activedemod) {
	case demod_ter:
		stv0367ter_read_ucblocks(fe, &ucblocks);
		break;
	case demod_cab:
		stv0367cab_read_ucblcks(fe, &ucblocks);
		break;
	default:
		p->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		return;
	}

	p->block_error.stat[0].scale = FE_SCALE_COUNTER;
	p->block_error.stat[0].uvalue = ucblocks;
}

static int stv0367ddb_read_status(struct dvb_frontend *fe,
				  enum fe_status *status)
{
	struct stv0367_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	int ret = 0;

	switch (state->activedemod) {
	case demod_ter:
		ret = stv0367ter_read_status(fe, status);
		break;
	case demod_cab:
		ret = stv0367cab_read_status(fe, status);
		break;
	default:
		break;
	}

	/* stop and report on *_read_status failure */
	if (ret)
		return ret;

	stv0367ddb_read_signal_strength(fe);

	/* read carrier/noise when a carrier is detected */
	if (*status & FE_HAS_CARRIER)
		stv0367ddb_read_snr(fe);
	else
		p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	/* read uncorrected blocks on FE_HAS_LOCK */
	if (*status & FE_HAS_LOCK)
		stv0367ddb_read_ucblocks(fe);
	else
		p->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	return 0;
}

static int stv0367ddb_get_frontend(struct dvb_frontend *fe,
				   struct dtv_frontend_properties *p)
{
	struct stv0367_state *state = fe->demodulator_priv;

	switch (state->activedemod) {
	case demod_ter:
		return stv0367ter_get_frontend(fe, p);
	case demod_cab:
		return stv0367cab_get_frontend(fe, p);
	default:
		break;
	}

	return 0;
}

static int stv0367ddb_sleep(struct dvb_frontend *fe)
{
	struct stv0367_state *state = fe->demodulator_priv;

	switch (state->activedemod) {
	case demod_ter:
		state->activedemod = demod_none;
		return stv0367ter_sleep(fe);
	case demod_cab:
		state->activedemod = demod_none;
		return stv0367cab_sleep(fe);
	default:
		break;
	}

	return -EINVAL;
}

static int stv0367ddb_init(struct stv0367_state *state)
{
	struct stv0367ter_state *ter_state = state->ter_state;
	struct dtv_frontend_properties *p = &state->fe.dtv_property_cache;

	stv0367_writereg(state, R367TER_TOPCTRL, 0x10);

	if (stv0367_deftabs[state->deftabs][STV0367_TAB_BASE])
		stv0367_write_table(state,
			stv0367_deftabs[state->deftabs][STV0367_TAB_BASE]);

	stv0367_write_table(state,
		stv0367_deftabs[state->deftabs][STV0367_TAB_CAB]);

	stv0367_writereg(state, R367TER_TOPCTRL, 0x00);
	stv0367_write_table(state,
		stv0367_deftabs[state->deftabs][STV0367_TAB_TER]);

	stv0367_writereg(state, R367TER_GAIN_SRC1, 0x2A);
	stv0367_writereg(state, R367TER_GAIN_SRC2, 0xD6);
	stv0367_writereg(state, R367TER_INC_DEROT1, 0x55);
	stv0367_writereg(state, R367TER_INC_DEROT2, 0x55);
	stv0367_writereg(state, R367TER_TRL_CTL, 0x14);
	stv0367_writereg(state, R367TER_TRL_NOMRATE1, 0xAE);
	stv0367_writereg(state, R367TER_TRL_NOMRATE2, 0x56);
	stv0367_writereg(state, R367TER_FEPATH_CFG, 0x0);

	/* OFDM TS Setup */

	stv0367_writereg(state, R367TER_TSCFGH, 0x70);
	stv0367_writereg(state, R367TER_TSCFGM, 0xC0);
	stv0367_writereg(state, R367TER_TSCFGL, 0x20);
	stv0367_writereg(state, R367TER_TSSPEED, 0x40); /* Fixed at 54 MHz */

	stv0367_writereg(state, R367TER_TSCFGH, 0x71);
	stv0367_writereg(state, R367TER_TSCFGH, 0x70);

	stv0367_writereg(state, R367TER_TOPCTRL, 0x10);

	/* Also needed for QAM */
	stv0367_writereg(state, R367TER_AGC12C, 0x01); /* AGC Pin setup */

	stv0367_writereg(state, R367TER_AGCCTRL1, 0x8A);

	/* QAM TS setup, note exact format also depends on descrambler */
	/* settings */
	/* Inverted Clock, Swap, serial */
	stv0367_writereg(state, R367CAB_OUTFORMAT_0, 0x85);

	/* Clock setup (PLL bypassed and disabled) */
	stv0367_writereg(state, R367TER_ANACTRL, 0x0D);

	/* IC runs at 58 MHz with a 27 MHz crystal */
	stv0367_pll_setup(state, STV0367_ICSPEED_58000, state->config->xtal);

	/* Tuner setup */
	/* Buffer Q disabled, I Enabled, signed ADC */
	stv0367_writereg(state, R367TER_ANADIGCTRL, 0x8b);
	stv0367_writereg(state, R367TER_DUAL_AD12, 0x04); /* ADCQ disabled */

	/* Improves the C/N lock limit */
	stv0367_writereg(state, R367CAB_FSM_SNR2_HTH, 0x23);
	/* ZIF/IF Automatic mode */
	stv0367_writereg(state, R367CAB_IQ_QAM, 0x01);
	/* Improving burst noise performances */
	stv0367_writereg(state, R367CAB_EQU_FFE_LEAKAGE, 0x83);
	/* Improving ACI performances */
	stv0367_writereg(state, R367CAB_IQDEM_ADJ_EN, 0x05);

	/* PLL enabled and used */
	stv0367_writereg(state, R367TER_ANACTRL, 0x00);

	stv0367_writereg(state, R367TER_I2CRPT, (0x08 | ((5 & 0x07) << 4)));

	ter_state->pBER = 0;
	ter_state->first_lock = 0;
	ter_state->unlock_counter = 2;

	p->strength.len = 1;
	p->strength.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->cnr.len = 1;
	p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->block_error.len = 1;
	p->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	return 0;
}

static const struct dvb_frontend_ops stv0367ddb_ops = {
	.delsys = { SYS_DVBC_ANNEX_A, SYS_DVBT },
	.info = {
		.name			= "ST STV0367 DDB DVB-C/T",
		.frequency_min_hz	=  47 * MHz,
		.frequency_max_hz	= 865 * MHz,
		.frequency_stepsize_hz	= 166667,
		.symbol_rate_min	= 870000,
		.symbol_rate_max	= 11700000,
		.caps = /* DVB-C */
			0x400 |/* FE_CAN_QAM_4 */
			FE_CAN_QAM_16 | FE_CAN_QAM_32  |
			FE_CAN_QAM_64 | FE_CAN_QAM_128 |
			FE_CAN_QAM_256 |
			/* DVB-T */
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK | FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_RECOVER | FE_CAN_INVERSION_AUTO |
			FE_CAN_MUTE_TS
	},
	.release = stv0367_release,
	.sleep = stv0367ddb_sleep,
	.i2c_gate_ctrl = stv0367cab_gate_ctrl, /* valid for TER and CAB */
	.set_frontend = stv0367ddb_set_frontend,
	.get_frontend = stv0367ddb_get_frontend,
	.get_tune_settings = stv0367_get_tune_settings,
	.read_status = stv0367ddb_read_status,
};

struct dvb_frontend *stv0367ddb_attach(const struct stv0367_config *config,
				   struct i2c_adapter *i2c)
{
	struct stv0367_state *state = NULL;
	struct stv0367ter_state *ter_state = NULL;
	struct stv0367cab_state *cab_state = NULL;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct stv0367_state), GFP_KERNEL);
	if (state == NULL)
		goto error;
	ter_state = kzalloc(sizeof(struct stv0367ter_state), GFP_KERNEL);
	if (ter_state == NULL)
		goto error;
	cab_state = kzalloc(sizeof(struct stv0367cab_state), GFP_KERNEL);
	if (cab_state == NULL)
		goto error;

	/* setup the state */
	state->i2c = i2c;
	state->config = config;
	state->ter_state = ter_state;
	cab_state->search_range = 280000;
	cab_state->qamfec_status_reg = F367CAB_DESCR_SYNCSTATE;
	state->cab_state = cab_state;
	state->fe.ops = stv0367ddb_ops;
	state->fe.demodulator_priv = state;
	state->chip_id = stv0367_readreg(state, R367TER_ID);

	/* demod operation options */
	state->use_i2c_gatectrl = 0;
	state->deftabs = STV0367_DEFTAB_DDB;
	state->reinit_on_setfrontend = 0;
	state->auto_if_khz = 1;
	state->activedemod = demod_none;

	dprintk("%s: chip_id = 0x%x\n", __func__, state->chip_id);

	/* check if the demod is there */
	if ((state->chip_id != 0x50) && (state->chip_id != 0x60))
		goto error;

	dev_info(&i2c->dev, "Found %s with ChipID %02X at adr %02X\n",
		state->fe.ops.info.name, state->chip_id,
		config->demod_address);

	stv0367ddb_init(state);

	return &state->fe;

error:
	kfree(cab_state);
	kfree(ter_state);
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL_GPL(stv0367ddb_attach);

MODULE_PARM_DESC(debug, "Set debug");
MODULE_PARM_DESC(i2c_debug, "Set i2c debug");

MODULE_AUTHOR("Igor M. Liplianin");
MODULE_DESCRIPTION("ST STV0367 DVB-C/T demodulator driver");
MODULE_LICENSE("GPL");

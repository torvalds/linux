/*
 * stv0900_core.c
 *
 * Driver for ST STV0900 satellite demodulator IC.
 *
 * Copyright (C) ST Microelectronics.
 * Copyright (C) 2009 NetUP Inc.
 * Copyright (C) 2009 Igor M. Liplianin <liplianin@netup.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/i2c.h>

#include "stv0900.h"
#include "stv0900_reg.h"
#include "stv0900_priv.h"
#include "stv0900_init.h"

int stvdebug = 1;
module_param_named(debug, stvdebug, int, 0644);

/* internal params node */
struct stv0900_inode {
	/* pointer for internal params, one for each pair of demods */
	struct stv0900_internal		*internal;
	struct stv0900_inode		*next_inode;
};

/* first internal params */
static struct stv0900_inode *stv0900_first_inode;

/* find chip by i2c adapter and i2c address */
static struct stv0900_inode *find_inode(struct i2c_adapter *i2c_adap,
							u8 i2c_addr)
{
	struct stv0900_inode *temp_chip = stv0900_first_inode;

	if (temp_chip != NULL) {
		/*
		 Search of the last stv0900 chip or
		 find it by i2c adapter and i2c address */
		while ((temp_chip != NULL) &&
			((temp_chip->internal->i2c_adap != i2c_adap) ||
			(temp_chip->internal->i2c_addr != i2c_addr)))

			temp_chip = temp_chip->next_inode;

	}

	return temp_chip;
}

/* deallocating chip */
static void remove_inode(struct stv0900_internal *internal)
{
	struct stv0900_inode *prev_node = stv0900_first_inode;
	struct stv0900_inode *del_node = find_inode(internal->i2c_adap,
						internal->i2c_addr);

	if (del_node != NULL) {
		if (del_node == stv0900_first_inode) {
			stv0900_first_inode = del_node->next_inode;
		} else {
			while (prev_node->next_inode != del_node)
				prev_node = prev_node->next_inode;

			if (del_node->next_inode == NULL)
				prev_node->next_inode = NULL;
			else
				prev_node->next_inode =
					prev_node->next_inode->next_inode;
		}

		kfree(del_node);
	}
}

/* allocating new chip */
static struct stv0900_inode *append_internal(struct stv0900_internal *internal)
{
	struct stv0900_inode *new_node = stv0900_first_inode;

	if (new_node == NULL) {
		new_node = kmalloc(sizeof(struct stv0900_inode), GFP_KERNEL);
		stv0900_first_inode = new_node;
	} else {
		while (new_node->next_inode != NULL)
			new_node = new_node->next_inode;

		new_node->next_inode = kmalloc(sizeof(struct stv0900_inode),
								GFP_KERNEL);
		if (new_node->next_inode != NULL)
			new_node = new_node->next_inode;
		else
			new_node = NULL;
	}

	if (new_node != NULL) {
		new_node->internal = internal;
		new_node->next_inode = NULL;
	}

	return new_node;
}

s32 ge2comp(s32 a, s32 width)
{
	if (width == 32)
		return a;
	else
		return (a >= (1 << (width - 1))) ? (a - (1 << width)) : a;
}

void stv0900_write_reg(struct stv0900_internal *intp, u16 reg_addr,
								u8 reg_data)
{
	u8 data[3];
	int ret;
	struct i2c_msg i2cmsg = {
		.addr  = intp->i2c_addr,
		.flags = 0,
		.len   = 3,
		.buf   = data,
	};

	data[0] = MSB(reg_addr);
	data[1] = LSB(reg_addr);
	data[2] = reg_data;

	ret = i2c_transfer(intp->i2c_adap, &i2cmsg, 1);
	if (ret != 1)
		dprintk("%s: i2c error %d\n", __func__, ret);
}

u8 stv0900_read_reg(struct stv0900_internal *intp, u16 reg)
{
	int ret;
	u8 b0[] = { MSB(reg), LSB(reg) };
	u8 buf = 0;
	struct i2c_msg msg[] = {
		{
			.addr	= intp->i2c_addr,
			.flags	= 0,
			.buf = b0,
			.len = 2,
		}, {
			.addr	= intp->i2c_addr,
			.flags	= I2C_M_RD,
			.buf = &buf,
			.len = 1,
		},
	};

	ret = i2c_transfer(intp->i2c_adap, msg, 2);
	if (ret != 2)
		dprintk("%s: i2c error %d, reg[0x%02x]\n",
				__func__, ret, reg);

	return buf;
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

void stv0900_write_bits(struct stv0900_internal *intp, u32 label, u8 val)
{
	u8 reg, mask, pos;

	reg = stv0900_read_reg(intp, (label >> 16) & 0xffff);
	extract_mask_pos(label, &mask, &pos);

	val = mask & (val << pos);

	reg = (reg & (~mask)) | val;
	stv0900_write_reg(intp, (label >> 16) & 0xffff, reg);

}

u8 stv0900_get_bits(struct stv0900_internal *intp, u32 label)
{
	u8 val = 0xff;
	u8 mask, pos;

	extract_mask_pos(label, &mask, &pos);

	val = stv0900_read_reg(intp, label >> 16);
	val = (val & mask) >> pos;

	return val;
}

static enum fe_stv0900_error stv0900_initialize(struct stv0900_internal *intp)
{
	s32 i;

	if (intp == NULL)
		return STV0900_INVALID_HANDLE;

	intp->chip_id = stv0900_read_reg(intp, R0900_MID);

	if (intp->errs != STV0900_NO_ERROR)
		return intp->errs;

	/*Startup sequence*/
	stv0900_write_reg(intp, R0900_P1_DMDISTATE, 0x5c);
	stv0900_write_reg(intp, R0900_P2_DMDISTATE, 0x5c);
	msleep(3);
	stv0900_write_reg(intp, R0900_P1_TNRCFG, 0x6c);
	stv0900_write_reg(intp, R0900_P2_TNRCFG, 0x6f);
	stv0900_write_reg(intp, R0900_P1_I2CRPT, 0x20);
	stv0900_write_reg(intp, R0900_P2_I2CRPT, 0x20);
	stv0900_write_reg(intp, R0900_NCOARSE, 0x13);
	msleep(3);
	stv0900_write_reg(intp, R0900_I2CCFG, 0x08);

	switch (intp->clkmode) {
	case 0:
	case 2:
		stv0900_write_reg(intp, R0900_SYNTCTRL, 0x20
				| intp->clkmode);
		break;
	default:
		/* preserve SELOSCI bit */
		i = 0x02 & stv0900_read_reg(intp, R0900_SYNTCTRL);
		stv0900_write_reg(intp, R0900_SYNTCTRL, 0x20 | i);
		break;
	}

	msleep(3);
	for (i = 0; i < 181; i++)
		stv0900_write_reg(intp, STV0900_InitVal[i][0],
				STV0900_InitVal[i][1]);

	if (stv0900_read_reg(intp, R0900_MID) >= 0x20) {
		stv0900_write_reg(intp, R0900_TSGENERAL, 0x0c);
		for (i = 0; i < 32; i++)
			stv0900_write_reg(intp, STV0900_Cut20_AddOnVal[i][0],
					STV0900_Cut20_AddOnVal[i][1]);
	}

	stv0900_write_reg(intp, R0900_P1_FSPYCFG, 0x6c);
	stv0900_write_reg(intp, R0900_P2_FSPYCFG, 0x6c);

	stv0900_write_reg(intp, R0900_P1_PDELCTRL2, 0x01);
	stv0900_write_reg(intp, R0900_P2_PDELCTRL2, 0x21);

	stv0900_write_reg(intp, R0900_P1_PDELCTRL3, 0x20);
	stv0900_write_reg(intp, R0900_P2_PDELCTRL3, 0x20);

	stv0900_write_reg(intp, R0900_TSTRES0, 0x80);
	stv0900_write_reg(intp, R0900_TSTRES0, 0x00);

	return STV0900_NO_ERROR;
}

static u32 stv0900_get_mclk_freq(struct stv0900_internal *intp, u32 ext_clk)
{
	u32 mclk = 90000000, div = 0, ad_div = 0;

	div = stv0900_get_bits(intp, F0900_M_DIV);
	ad_div = ((stv0900_get_bits(intp, F0900_SELX1RATIO) == 1) ? 4 : 6);

	mclk = (div + 1) * ext_clk / ad_div;

	dprintk("%s: Calculated Mclk = %d\n", __func__, mclk);

	return mclk;
}

static enum fe_stv0900_error stv0900_set_mclk(struct stv0900_internal *intp, u32 mclk)
{
	u32 m_div, clk_sel;

	if (intp == NULL)
		return STV0900_INVALID_HANDLE;

	if (intp->errs)
		return STV0900_I2C_ERROR;

	dprintk("%s: Mclk set to %d, Quartz = %d\n", __func__, mclk,
			intp->quartz);

	clk_sel = ((stv0900_get_bits(intp, F0900_SELX1RATIO) == 1) ? 4 : 6);
	m_div = ((clk_sel * mclk) / intp->quartz) - 1;
	stv0900_write_bits(intp, F0900_M_DIV, m_div);
	intp->mclk = stv0900_get_mclk_freq(intp,
					intp->quartz);

	/*Set the DiseqC frequency to 22KHz */
	/*
		Formula:
		DiseqC_TX_Freq= MasterClock/(32*F22TX_Reg)
		DiseqC_RX_Freq= MasterClock/(32*F22RX_Reg)
	*/
	m_div = intp->mclk / 704000;
	stv0900_write_reg(intp, R0900_P1_F22TX, m_div);
	stv0900_write_reg(intp, R0900_P1_F22RX, m_div);

	stv0900_write_reg(intp, R0900_P2_F22TX, m_div);
	stv0900_write_reg(intp, R0900_P2_F22RX, m_div);

	if ((intp->errs))
		return STV0900_I2C_ERROR;

	return STV0900_NO_ERROR;
}

static u32 stv0900_get_err_count(struct stv0900_internal *intp, int cntr,
					enum fe_stv0900_demod_num demod)
{
	u32 lsb, msb, hsb, err_val;

	switch (cntr) {
	case 0:
	default:
		hsb = stv0900_get_bits(intp, ERR_CNT12);
		msb = stv0900_get_bits(intp, ERR_CNT11);
		lsb = stv0900_get_bits(intp, ERR_CNT10);
		break;
	case 1:
		hsb = stv0900_get_bits(intp, ERR_CNT22);
		msb = stv0900_get_bits(intp, ERR_CNT21);
		lsb = stv0900_get_bits(intp, ERR_CNT20);
		break;
	}

	err_val = (hsb << 16) + (msb << 8) + (lsb);

	return err_val;
}

static int stv0900_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *intp = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;

	stv0900_write_bits(intp, I2CT_ON, enable);

	return 0;
}

static void stv0900_set_ts_parallel_serial(struct stv0900_internal *intp,
					enum fe_stv0900_clock_type path1_ts,
					enum fe_stv0900_clock_type path2_ts)
{

	dprintk("%s\n", __func__);

	if (intp->chip_id >= 0x20) {
		switch (path1_ts) {
		case STV0900_PARALLEL_PUNCT_CLOCK:
		case STV0900_DVBCI_CLOCK:
			switch (path2_ts) {
			case STV0900_SERIAL_PUNCT_CLOCK:
			case STV0900_SERIAL_CONT_CLOCK:
			default:
				stv0900_write_reg(intp, R0900_TSGENERAL,
							0x00);
				break;
			case STV0900_PARALLEL_PUNCT_CLOCK:
			case STV0900_DVBCI_CLOCK:
				stv0900_write_reg(intp, R0900_TSGENERAL,
							0x06);
				stv0900_write_bits(intp,
						F0900_P1_TSFIFO_MANSPEED, 3);
				stv0900_write_bits(intp,
						F0900_P2_TSFIFO_MANSPEED, 0);
				stv0900_write_reg(intp,
						R0900_P1_TSSPEED, 0x14);
				stv0900_write_reg(intp,
						R0900_P2_TSSPEED, 0x28);
				break;
			}
			break;
		case STV0900_SERIAL_PUNCT_CLOCK:
		case STV0900_SERIAL_CONT_CLOCK:
		default:
			switch (path2_ts) {
			case STV0900_SERIAL_PUNCT_CLOCK:
			case STV0900_SERIAL_CONT_CLOCK:
			default:
				stv0900_write_reg(intp,
						R0900_TSGENERAL, 0x0C);
				break;
			case STV0900_PARALLEL_PUNCT_CLOCK:
			case STV0900_DVBCI_CLOCK:
				stv0900_write_reg(intp,
						R0900_TSGENERAL, 0x0A);
				dprintk("%s: 0x0a\n", __func__);
				break;
			}
			break;
		}
	} else {
		switch (path1_ts) {
		case STV0900_PARALLEL_PUNCT_CLOCK:
		case STV0900_DVBCI_CLOCK:
			switch (path2_ts) {
			case STV0900_SERIAL_PUNCT_CLOCK:
			case STV0900_SERIAL_CONT_CLOCK:
			default:
				stv0900_write_reg(intp, R0900_TSGENERAL1X,
							0x10);
				break;
			case STV0900_PARALLEL_PUNCT_CLOCK:
			case STV0900_DVBCI_CLOCK:
				stv0900_write_reg(intp, R0900_TSGENERAL1X,
							0x16);
				stv0900_write_bits(intp,
						F0900_P1_TSFIFO_MANSPEED, 3);
				stv0900_write_bits(intp,
						F0900_P2_TSFIFO_MANSPEED, 0);
				stv0900_write_reg(intp, R0900_P1_TSSPEED,
							0x14);
				stv0900_write_reg(intp, R0900_P2_TSSPEED,
							0x28);
				break;
			}

			break;
		case STV0900_SERIAL_PUNCT_CLOCK:
		case STV0900_SERIAL_CONT_CLOCK:
		default:
			switch (path2_ts) {
			case STV0900_SERIAL_PUNCT_CLOCK:
			case STV0900_SERIAL_CONT_CLOCK:
			default:
				stv0900_write_reg(intp, R0900_TSGENERAL1X,
							0x14);
				break;
			case STV0900_PARALLEL_PUNCT_CLOCK:
			case STV0900_DVBCI_CLOCK:
				stv0900_write_reg(intp, R0900_TSGENERAL1X,
							0x12);
				dprintk("%s: 0x12\n", __func__);
				break;
			}

			break;
		}
	}

	switch (path1_ts) {
	case STV0900_PARALLEL_PUNCT_CLOCK:
		stv0900_write_bits(intp, F0900_P1_TSFIFO_SERIAL, 0x00);
		stv0900_write_bits(intp, F0900_P1_TSFIFO_DVBCI, 0x00);
		break;
	case STV0900_DVBCI_CLOCK:
		stv0900_write_bits(intp, F0900_P1_TSFIFO_SERIAL, 0x00);
		stv0900_write_bits(intp, F0900_P1_TSFIFO_DVBCI, 0x01);
		break;
	case STV0900_SERIAL_PUNCT_CLOCK:
		stv0900_write_bits(intp, F0900_P1_TSFIFO_SERIAL, 0x01);
		stv0900_write_bits(intp, F0900_P1_TSFIFO_DVBCI, 0x00);
		break;
	case STV0900_SERIAL_CONT_CLOCK:
		stv0900_write_bits(intp, F0900_P1_TSFIFO_SERIAL, 0x01);
		stv0900_write_bits(intp, F0900_P1_TSFIFO_DVBCI, 0x01);
		break;
	default:
		break;
	}

	switch (path2_ts) {
	case STV0900_PARALLEL_PUNCT_CLOCK:
		stv0900_write_bits(intp, F0900_P2_TSFIFO_SERIAL, 0x00);
		stv0900_write_bits(intp, F0900_P2_TSFIFO_DVBCI, 0x00);
		break;
	case STV0900_DVBCI_CLOCK:
		stv0900_write_bits(intp, F0900_P2_TSFIFO_SERIAL, 0x00);
		stv0900_write_bits(intp, F0900_P2_TSFIFO_DVBCI, 0x01);
		break;
	case STV0900_SERIAL_PUNCT_CLOCK:
		stv0900_write_bits(intp, F0900_P2_TSFIFO_SERIAL, 0x01);
		stv0900_write_bits(intp, F0900_P2_TSFIFO_DVBCI, 0x00);
		break;
	case STV0900_SERIAL_CONT_CLOCK:
		stv0900_write_bits(intp, F0900_P2_TSFIFO_SERIAL, 0x01);
		stv0900_write_bits(intp, F0900_P2_TSFIFO_DVBCI, 0x01);
		break;
	default:
		break;
	}

	stv0900_write_bits(intp, F0900_P2_RST_HWARE, 1);
	stv0900_write_bits(intp, F0900_P2_RST_HWARE, 0);
	stv0900_write_bits(intp, F0900_P1_RST_HWARE, 1);
	stv0900_write_bits(intp, F0900_P1_RST_HWARE, 0);
}

void stv0900_set_tuner(struct dvb_frontend *fe, u32 frequency,
							u32 bandwidth)
{
	struct dvb_frontend_ops *frontend_ops = NULL;
	struct dvb_tuner_ops *tuner_ops = NULL;

	frontend_ops = &fe->ops;
	tuner_ops = &frontend_ops->tuner_ops;

	if (tuner_ops->set_frequency) {
		if ((tuner_ops->set_frequency(fe, frequency)) < 0)
			dprintk("%s: Invalid parameter\n", __func__);
		else
			dprintk("%s: Frequency=%d\n", __func__, frequency);

	}

	if (tuner_ops->set_bandwidth) {
		if ((tuner_ops->set_bandwidth(fe, bandwidth)) < 0)
			dprintk("%s: Invalid parameter\n", __func__);
		else
			dprintk("%s: Bandwidth=%d\n", __func__, bandwidth);

	}
}

void stv0900_set_bandwidth(struct dvb_frontend *fe, u32 bandwidth)
{
	struct dvb_frontend_ops *frontend_ops = NULL;
	struct dvb_tuner_ops *tuner_ops = NULL;

	frontend_ops = &fe->ops;
	tuner_ops = &frontend_ops->tuner_ops;

	if (tuner_ops->set_bandwidth) {
		if ((tuner_ops->set_bandwidth(fe, bandwidth)) < 0)
			dprintk("%s: Invalid parameter\n", __func__);
		else
			dprintk("%s: Bandwidth=%d\n", __func__, bandwidth);

	}
}

u32 stv0900_get_freq_auto(struct stv0900_internal *intp, int demod)
{
	u32 freq, round;
	/*	Formulat :
	Tuner_Frequency(MHz)	= Regs / 64
	Tuner_granularity(MHz)	= Regs / 2048
	real_Tuner_Frequency	= Tuner_Frequency(MHz) - Tuner_granularity(MHz)
	*/
	freq = (stv0900_get_bits(intp, TUN_RFFREQ2) << 10) +
		(stv0900_get_bits(intp, TUN_RFFREQ1) << 2) +
		stv0900_get_bits(intp, TUN_RFFREQ0);

	freq = (freq * 1000) / 64;

	round = (stv0900_get_bits(intp, TUN_RFRESTE1) >> 2) +
		stv0900_get_bits(intp, TUN_RFRESTE0);

	round = (round * 1000) / 2048;

	return freq + round;
}

void stv0900_set_tuner_auto(struct stv0900_internal *intp, u32 Frequency,
						u32 Bandwidth, int demod)
{
	u32 tunerFrequency;
	/* Formulat:
	Tuner_frequency_reg= Frequency(MHz)*64
	*/
	tunerFrequency = (Frequency * 64) / 1000;

	stv0900_write_bits(intp, TUN_RFFREQ2, (tunerFrequency >> 10));
	stv0900_write_bits(intp, TUN_RFFREQ1, (tunerFrequency >> 2) & 0xff);
	stv0900_write_bits(intp, TUN_RFFREQ0, (tunerFrequency & 0x03));
	/* Low Pass Filter = BW /2 (MHz)*/
	stv0900_write_bits(intp, TUN_BW, Bandwidth / 2000000);
	/* Tuner Write trig */
	stv0900_write_reg(intp, TNRLD, 1);
}

static s32 stv0900_get_rf_level(struct stv0900_internal *intp,
				const struct stv0900_table *lookup,
				enum fe_stv0900_demod_num demod)
{
	s32 agc_gain = 0,
		imin,
		imax,
		i,
		rf_lvl = 0;

	dprintk("%s\n", __func__);

	if ((lookup == NULL) || (lookup->size <= 0))
		return 0;

	agc_gain = MAKEWORD(stv0900_get_bits(intp, AGCIQ_VALUE1),
				stv0900_get_bits(intp, AGCIQ_VALUE0));

	imin = 0;
	imax = lookup->size - 1;
	if (INRANGE(lookup->table[imin].regval, agc_gain,
					lookup->table[imax].regval)) {
		while ((imax - imin) > 1) {
			i = (imax + imin) >> 1;

			if (INRANGE(lookup->table[imin].regval,
					agc_gain,
					lookup->table[i].regval))
				imax = i;
			else
				imin = i;
		}

		rf_lvl = (s32)agc_gain - lookup->table[imin].regval;
		rf_lvl *= (lookup->table[imax].realval -
				lookup->table[imin].realval);
		rf_lvl /= (lookup->table[imax].regval -
				lookup->table[imin].regval);
		rf_lvl += lookup->table[imin].realval;
	} else if (agc_gain > lookup->table[0].regval)
		rf_lvl = 5;
	else if (agc_gain < lookup->table[lookup->size-1].regval)
		rf_lvl = -100;

	dprintk("%s: RFLevel = %d\n", __func__, rf_lvl);

	return rf_lvl;
}

static int stv0900_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *internal = state->internal;
	s32 rflevel = stv0900_get_rf_level(internal, &stv0900_rf,
								state->demod);

	rflevel = (rflevel + 100) * (65535 / 70);
	if (rflevel < 0)
		rflevel = 0;

	if (rflevel > 65535)
		rflevel = 65535;

	*strength = rflevel;

	return 0;
}

static s32 stv0900_carr_get_quality(struct dvb_frontend *fe,
					const struct stv0900_table *lookup)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *intp = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;

	s32	c_n = -100,
		regval,
		imin,
		imax,
		i,
		noise_field1,
		noise_field0;

	dprintk("%s\n", __func__);

	if (stv0900_get_standard(fe, demod) == STV0900_DVBS2_STANDARD) {
		noise_field1 = NOSPLHT_NORMED1;
		noise_field0 = NOSPLHT_NORMED0;
	} else {
		noise_field1 = NOSDATAT_NORMED1;
		noise_field0 = NOSDATAT_NORMED0;
	}

	if (stv0900_get_bits(intp, LOCK_DEFINITIF)) {
		if ((lookup != NULL) && lookup->size) {
			regval = 0;
			msleep(5);
			for (i = 0; i < 16; i++) {
				regval += MAKEWORD(stv0900_get_bits(intp,
								noise_field1),
						stv0900_get_bits(intp,
								noise_field0));
				msleep(1);
			}

			regval /= 16;
			imin = 0;
			imax = lookup->size - 1;
			if (INRANGE(lookup->table[imin].regval,
					regval,
					lookup->table[imax].regval)) {
				while ((imax - imin) > 1) {
					i = (imax + imin) >> 1;
					if (INRANGE(lookup->table[imin].regval,
						    regval,
						    lookup->table[i].regval))
						imax = i;
					else
						imin = i;
				}

				c_n = ((regval - lookup->table[imin].regval)
						* (lookup->table[imax].realval
						- lookup->table[imin].realval)
						/ (lookup->table[imax].regval
						- lookup->table[imin].regval))
						+ lookup->table[imin].realval;
			} else if (regval < lookup->table[imin].regval)
				c_n = 1000;
		}
	}

	return c_n;
}

static int stv0900_read_ucblocks(struct dvb_frontend *fe, u32 * ucblocks)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *intp = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;
	u8 err_val1, err_val0;
	u32 header_err_val = 0;

	*ucblocks = 0x0;
	if (stv0900_get_standard(fe, demod) == STV0900_DVBS2_STANDARD) {
		/* DVB-S2 delineator errors count */

		/* retreiving number for errnous headers */
		err_val1 = stv0900_read_reg(intp, BBFCRCKO1);
		err_val0 = stv0900_read_reg(intp, BBFCRCKO0);
		header_err_val = (err_val1 << 8) | err_val0;

		/* retreiving number for errnous packets */
		err_val1 = stv0900_read_reg(intp, UPCRCKO1);
		err_val0 = stv0900_read_reg(intp, UPCRCKO0);
		*ucblocks = (err_val1 << 8) | err_val0;
		*ucblocks += header_err_val;
	}

	return 0;
}

static int stv0900_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	s32 snrlcl = stv0900_carr_get_quality(fe,
			(const struct stv0900_table *)&stv0900_s2_cn);
	snrlcl = (snrlcl + 30) * 384;
	if (snrlcl < 0)
		snrlcl = 0;

	if (snrlcl > 65535)
		snrlcl = 65535;

	*snr = snrlcl;

	return 0;
}

static u32 stv0900_get_ber(struct stv0900_internal *intp,
				enum fe_stv0900_demod_num demod)
{
	u32 ber = 10000000, i;
	s32 demod_state;

	demod_state = stv0900_get_bits(intp, HEADER_MODE);

	switch (demod_state) {
	case STV0900_SEARCH:
	case STV0900_PLH_DETECTED:
	default:
		ber = 10000000;
		break;
	case STV0900_DVBS_FOUND:
		ber = 0;
		for (i = 0; i < 5; i++) {
			msleep(5);
			ber += stv0900_get_err_count(intp, 0, demod);
		}

		ber /= 5;
		if (stv0900_get_bits(intp, PRFVIT)) {
			ber *= 9766;
			ber = ber >> 13;
		}

		break;
	case STV0900_DVBS2_FOUND:
		ber = 0;
		for (i = 0; i < 5; i++) {
			msleep(5);
			ber += stv0900_get_err_count(intp, 0, demod);
		}

		ber /= 5;
		if (stv0900_get_bits(intp, PKTDELIN_LOCK)) {
			ber *= 9766;
			ber = ber >> 13;
		}

		break;
	}

	return ber;
}

static int stv0900_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *internal = state->internal;

	*ber = stv0900_get_ber(internal, state->demod);

	return 0;
}

int stv0900_get_demod_lock(struct stv0900_internal *intp,
			enum fe_stv0900_demod_num demod, s32 time_out)
{
	s32 timer = 0,
		lock = 0;

	enum fe_stv0900_search_state	dmd_state;

	while ((timer < time_out) && (lock == 0)) {
		dmd_state = stv0900_get_bits(intp, HEADER_MODE);
		dprintk("Demod State = %d\n", dmd_state);
		switch (dmd_state) {
		case STV0900_SEARCH:
		case STV0900_PLH_DETECTED:
		default:
			lock = 0;
			break;
		case STV0900_DVBS2_FOUND:
		case STV0900_DVBS_FOUND:
			lock = stv0900_get_bits(intp, LOCK_DEFINITIF);
			break;
		}

		if (lock == 0)
			msleep(10);

		timer += 10;
	}

	if (lock)
		dprintk("DEMOD LOCK OK\n");
	else
		dprintk("DEMOD LOCK FAIL\n");

	return lock;
}

void stv0900_stop_all_s2_modcod(struct stv0900_internal *intp,
				enum fe_stv0900_demod_num demod)
{
	s32 regflist,
	i;

	dprintk("%s\n", __func__);

	regflist = MODCODLST0;

	for (i = 0; i < 16; i++)
		stv0900_write_reg(intp, regflist + i, 0xff);
}

void stv0900_activate_s2_modcod(struct stv0900_internal *intp,
				enum fe_stv0900_demod_num demod)
{
	u32 matype,
		mod_code,
		fmod,
		reg_index,
		field_index;

	dprintk("%s\n", __func__);

	if (intp->chip_id <= 0x11) {
		msleep(5);

		mod_code = stv0900_read_reg(intp, PLHMODCOD);
		matype = mod_code & 0x3;
		mod_code = (mod_code & 0x7f) >> 2;

		reg_index = MODCODLSTF - mod_code / 2;
		field_index = mod_code % 2;

		switch (matype) {
		case 0:
		default:
			fmod = 14;
			break;
		case 1:
			fmod = 13;
			break;
		case 2:
			fmod = 11;
			break;
		case 3:
			fmod = 7;
			break;
		}

		if ((INRANGE(STV0900_QPSK_12, mod_code, STV0900_8PSK_910))
						&& (matype <= 1)) {
			if (field_index == 0)
				stv0900_write_reg(intp, reg_index,
							0xf0 | fmod);
			else
				stv0900_write_reg(intp, reg_index,
							(fmod << 4) | 0xf);
		}

	} else if (intp->chip_id >= 0x12) {
		for (reg_index = 0; reg_index < 7; reg_index++)
			stv0900_write_reg(intp, MODCODLST0 + reg_index, 0xff);

		stv0900_write_reg(intp, MODCODLSTE, 0xff);
		stv0900_write_reg(intp, MODCODLSTF, 0xcf);
		for (reg_index = 0; reg_index < 8; reg_index++)
			stv0900_write_reg(intp, MODCODLST7 + reg_index, 0xcc);


	}
}

void stv0900_activate_s2_modcod_single(struct stv0900_internal *intp,
					enum fe_stv0900_demod_num demod)
{
	u32 reg_index;

	dprintk("%s\n", __func__);

	stv0900_write_reg(intp, MODCODLST0, 0xff);
	stv0900_write_reg(intp, MODCODLST1, 0xf0);
	stv0900_write_reg(intp, MODCODLSTF, 0x0f);
	for (reg_index = 0; reg_index < 13; reg_index++)
		stv0900_write_reg(intp, MODCODLST2 + reg_index, 0);

}

static enum dvbfe_algo stv0900_frontend_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_CUSTOM;
}

void stv0900_start_search(struct stv0900_internal *intp,
				enum fe_stv0900_demod_num demod)
{
	u32 freq;
	s16 freq_s16 ;

	stv0900_write_bits(intp, DEMOD_MODE, 0x1f);
	if (intp->chip_id == 0x10)
		stv0900_write_reg(intp, CORRELEXP, 0xaa);

	if (intp->chip_id < 0x20)
		stv0900_write_reg(intp, CARHDR, 0x55);

	if (intp->chip_id <= 0x20) {
		if (intp->symbol_rate[0] <= 5000000) {
			stv0900_write_reg(intp, CARCFG, 0x44);
			stv0900_write_reg(intp, CFRUP1, 0x0f);
			stv0900_write_reg(intp, CFRUP0, 0xff);
			stv0900_write_reg(intp, CFRLOW1, 0xf0);
			stv0900_write_reg(intp, CFRLOW0, 0x00);
			stv0900_write_reg(intp, RTCS2, 0x68);
		} else {
			stv0900_write_reg(intp, CARCFG, 0xc4);
			stv0900_write_reg(intp, RTCS2, 0x44);
		}

	} else { /*cut 3.0 above*/
		if (intp->symbol_rate[demod] <= 5000000)
			stv0900_write_reg(intp, RTCS2, 0x68);
		else
			stv0900_write_reg(intp, RTCS2, 0x44);

		stv0900_write_reg(intp, CARCFG, 0x46);
		if (intp->srch_algo[demod] == STV0900_WARM_START) {
			freq = 1000 << 16;
			freq /= (intp->mclk / 1000);
			freq_s16 = (s16)freq;
		} else {
			freq = (intp->srch_range[demod] / 2000);
			if (intp->symbol_rate[demod] <= 5000000)
				freq += 80;
			else
				freq += 600;

			freq = freq << 16;
			freq /= (intp->mclk / 1000);
			freq_s16 = (s16)freq;
		}

		stv0900_write_bits(intp, CFR_UP1, MSB(freq_s16));
		stv0900_write_bits(intp, CFR_UP0, LSB(freq_s16));
		freq_s16 *= (-1);
		stv0900_write_bits(intp, CFR_LOW1, MSB(freq_s16));
		stv0900_write_bits(intp, CFR_LOW0, LSB(freq_s16));
	}

	stv0900_write_reg(intp, CFRINIT1, 0);
	stv0900_write_reg(intp, CFRINIT0, 0);

	if (intp->chip_id >= 0x20) {
		stv0900_write_reg(intp, EQUALCFG, 0x41);
		stv0900_write_reg(intp, FFECFG, 0x41);

		if ((intp->srch_standard[demod] == STV0900_SEARCH_DVBS1) ||
			(intp->srch_standard[demod] == STV0900_SEARCH_DSS) ||
			(intp->srch_standard[demod] == STV0900_AUTO_SEARCH)) {
			stv0900_write_reg(intp, VITSCALE,
								0x82);
			stv0900_write_reg(intp, VAVSRVIT, 0x0);
		}
	}

	stv0900_write_reg(intp, SFRSTEP, 0x00);
	stv0900_write_reg(intp, TMGTHRISE, 0xe0);
	stv0900_write_reg(intp, TMGTHFALL, 0xc0);
	stv0900_write_bits(intp, SCAN_ENABLE, 0);
	stv0900_write_bits(intp, CFR_AUTOSCAN, 0);
	stv0900_write_bits(intp, S1S2_SEQUENTIAL, 0);
	stv0900_write_reg(intp, RTC, 0x88);
	if (intp->chip_id >= 0x20) {
		if (intp->symbol_rate[demod] < 2000000) {
			if (intp->chip_id <= 0x20)
				stv0900_write_reg(intp, CARFREQ, 0x39);
			else  /*cut 3.0*/
				stv0900_write_reg(intp, CARFREQ, 0x89);

			stv0900_write_reg(intp, CARHDR, 0x40);
		} else if (intp->symbol_rate[demod] < 10000000) {
			stv0900_write_reg(intp, CARFREQ, 0x4c);
			stv0900_write_reg(intp, CARHDR, 0x20);
		} else {
			stv0900_write_reg(intp, CARFREQ, 0x4b);
			stv0900_write_reg(intp, CARHDR, 0x20);
		}

	} else {
		if (intp->symbol_rate[demod] < 10000000)
			stv0900_write_reg(intp, CARFREQ, 0xef);
		else
			stv0900_write_reg(intp, CARFREQ, 0xed);
	}

	switch (intp->srch_algo[demod]) {
	case STV0900_WARM_START:
		stv0900_write_reg(intp, DMDISTATE, 0x1f);
		stv0900_write_reg(intp, DMDISTATE, 0x18);
		break;
	case STV0900_COLD_START:
		stv0900_write_reg(intp, DMDISTATE, 0x1f);
		stv0900_write_reg(intp, DMDISTATE, 0x15);
		break;
	default:
		break;
	}
}

u8 stv0900_get_optim_carr_loop(s32 srate, enum fe_stv0900_modcode modcode,
							s32 pilot, u8 chip_id)
{
	u8 aclc_value = 0x29;
	s32 i;
	const struct stv0900_car_loop_optim *cls2, *cllqs2, *cllas2;

	dprintk("%s\n", __func__);

	if (chip_id <= 0x12) {
		cls2 = FE_STV0900_S2CarLoop;
		cllqs2 = FE_STV0900_S2LowQPCarLoopCut30;
		cllas2 = FE_STV0900_S2APSKCarLoopCut30;
	} else if (chip_id == 0x20) {
		cls2 = FE_STV0900_S2CarLoopCut20;
		cllqs2 = FE_STV0900_S2LowQPCarLoopCut20;
		cllas2 = FE_STV0900_S2APSKCarLoopCut20;
	} else {
		cls2 = FE_STV0900_S2CarLoopCut30;
		cllqs2 = FE_STV0900_S2LowQPCarLoopCut30;
		cllas2 = FE_STV0900_S2APSKCarLoopCut30;
	}

	if (modcode < STV0900_QPSK_12) {
		i = 0;
		while ((i < 3) && (modcode != cllqs2[i].modcode))
			i++;

		if (i >= 3)
			i = 2;
	} else {
		i = 0;
		while ((i < 14) && (modcode != cls2[i].modcode))
			i++;

		if (i >= 14) {
			i = 0;
			while ((i < 11) && (modcode != cllas2[i].modcode))
				i++;

			if (i >= 11)
				i = 10;
		}
	}

	if (modcode <= STV0900_QPSK_25) {
		if (pilot) {
			if (srate <= 3000000)
				aclc_value = cllqs2[i].car_loop_pilots_on_2;
			else if (srate <= 7000000)
				aclc_value = cllqs2[i].car_loop_pilots_on_5;
			else if (srate <= 15000000)
				aclc_value = cllqs2[i].car_loop_pilots_on_10;
			else if (srate <= 25000000)
				aclc_value = cllqs2[i].car_loop_pilots_on_20;
			else
				aclc_value = cllqs2[i].car_loop_pilots_on_30;
		} else {
			if (srate <= 3000000)
				aclc_value = cllqs2[i].car_loop_pilots_off_2;
			else if (srate <= 7000000)
				aclc_value = cllqs2[i].car_loop_pilots_off_5;
			else if (srate <= 15000000)
				aclc_value = cllqs2[i].car_loop_pilots_off_10;
			else if (srate <= 25000000)
				aclc_value = cllqs2[i].car_loop_pilots_off_20;
			else
				aclc_value = cllqs2[i].car_loop_pilots_off_30;
		}

	} else if (modcode <= STV0900_8PSK_910) {
		if (pilot) {
			if (srate <= 3000000)
				aclc_value = cls2[i].car_loop_pilots_on_2;
			else if (srate <= 7000000)
				aclc_value = cls2[i].car_loop_pilots_on_5;
			else if (srate <= 15000000)
				aclc_value = cls2[i].car_loop_pilots_on_10;
			else if (srate <= 25000000)
				aclc_value = cls2[i].car_loop_pilots_on_20;
			else
				aclc_value = cls2[i].car_loop_pilots_on_30;
		} else {
			if (srate <= 3000000)
				aclc_value = cls2[i].car_loop_pilots_off_2;
			else if (srate <= 7000000)
				aclc_value = cls2[i].car_loop_pilots_off_5;
			else if (srate <= 15000000)
				aclc_value = cls2[i].car_loop_pilots_off_10;
			else if (srate <= 25000000)
				aclc_value = cls2[i].car_loop_pilots_off_20;
			else
				aclc_value = cls2[i].car_loop_pilots_off_30;
		}

	} else {
		if (srate <= 3000000)
			aclc_value = cllas2[i].car_loop_pilots_on_2;
		else if (srate <= 7000000)
			aclc_value = cllas2[i].car_loop_pilots_on_5;
		else if (srate <= 15000000)
			aclc_value = cllas2[i].car_loop_pilots_on_10;
		else if (srate <= 25000000)
			aclc_value = cllas2[i].car_loop_pilots_on_20;
		else
			aclc_value = cllas2[i].car_loop_pilots_on_30;
	}

	return aclc_value;
}

u8 stv0900_get_optim_short_carr_loop(s32 srate,
				enum fe_stv0900_modulation modulation,
				u8 chip_id)
{
	const struct stv0900_short_frames_car_loop_optim *s2scl;
	const struct stv0900_short_frames_car_loop_optim_vs_mod *s2sclc30;
	s32 mod_index = 0;
	u8 aclc_value = 0x0b;

	dprintk("%s\n", __func__);

	s2scl = FE_STV0900_S2ShortCarLoop;
	s2sclc30 = FE_STV0900_S2ShortCarLoopCut30;

	switch (modulation) {
	case STV0900_QPSK:
	default:
		mod_index = 0;
		break;
	case STV0900_8PSK:
		mod_index = 1;
		break;
	case STV0900_16APSK:
		mod_index = 2;
		break;
	case STV0900_32APSK:
		mod_index = 3;
		break;
	}

	if (chip_id >= 0x30) {
		if (srate <= 3000000)
			aclc_value = s2sclc30[mod_index].car_loop_2;
		else if (srate <= 7000000)
			aclc_value = s2sclc30[mod_index].car_loop_5;
		else if (srate <= 15000000)
			aclc_value = s2sclc30[mod_index].car_loop_10;
		else if (srate <= 25000000)
			aclc_value = s2sclc30[mod_index].car_loop_20;
		else
			aclc_value = s2sclc30[mod_index].car_loop_30;

	} else if (chip_id >= 0x20) {
		if (srate <= 3000000)
			aclc_value = s2scl[mod_index].car_loop_cut20_2;
		else if (srate <= 7000000)
			aclc_value = s2scl[mod_index].car_loop_cut20_5;
		else if (srate <= 15000000)
			aclc_value = s2scl[mod_index].car_loop_cut20_10;
		else if (srate <= 25000000)
			aclc_value = s2scl[mod_index].car_loop_cut20_20;
		else
			aclc_value = s2scl[mod_index].car_loop_cut20_30;

	} else {
		if (srate <= 3000000)
			aclc_value = s2scl[mod_index].car_loop_cut12_2;
		else if (srate <= 7000000)
			aclc_value = s2scl[mod_index].car_loop_cut12_5;
		else if (srate <= 15000000)
			aclc_value = s2scl[mod_index].car_loop_cut12_10;
		else if (srate <= 25000000)
			aclc_value = s2scl[mod_index].car_loop_cut12_20;
		else
			aclc_value = s2scl[mod_index].car_loop_cut12_30;

	}

	return aclc_value;
}

static
enum fe_stv0900_error stv0900_st_dvbs2_single(struct stv0900_internal *intp,
					enum fe_stv0900_demod_mode LDPC_Mode,
					enum fe_stv0900_demod_num demod)
{
	enum fe_stv0900_error error = STV0900_NO_ERROR;
	s32 reg_ind;

	dprintk("%s\n", __func__);

	switch (LDPC_Mode) {
	case STV0900_DUAL:
	default:
		if ((intp->demod_mode != STV0900_DUAL)
			|| (stv0900_get_bits(intp, F0900_DDEMOD) != 1)) {
			stv0900_write_reg(intp, R0900_GENCFG, 0x1d);

			intp->demod_mode = STV0900_DUAL;

			stv0900_write_bits(intp, F0900_FRESFEC, 1);
			stv0900_write_bits(intp, F0900_FRESFEC, 0);

			for (reg_ind = 0; reg_ind < 7; reg_ind++)
				stv0900_write_reg(intp,
						R0900_P1_MODCODLST0 + reg_ind,
						0xff);
			for (reg_ind = 0; reg_ind < 8; reg_ind++)
				stv0900_write_reg(intp,
						R0900_P1_MODCODLST7 + reg_ind,
						0xcc);

			stv0900_write_reg(intp, R0900_P1_MODCODLSTE, 0xff);
			stv0900_write_reg(intp, R0900_P1_MODCODLSTF, 0xcf);

			for (reg_ind = 0; reg_ind < 7; reg_ind++)
				stv0900_write_reg(intp,
						R0900_P2_MODCODLST0 + reg_ind,
						0xff);
			for (reg_ind = 0; reg_ind < 8; reg_ind++)
				stv0900_write_reg(intp,
						R0900_P2_MODCODLST7 + reg_ind,
						0xcc);

			stv0900_write_reg(intp, R0900_P2_MODCODLSTE, 0xff);
			stv0900_write_reg(intp, R0900_P2_MODCODLSTF, 0xcf);
		}

		break;
	case STV0900_SINGLE:
		if (demod == STV0900_DEMOD_2) {
			stv0900_stop_all_s2_modcod(intp, STV0900_DEMOD_1);
			stv0900_activate_s2_modcod_single(intp,
							STV0900_DEMOD_2);
			stv0900_write_reg(intp, R0900_GENCFG, 0x06);
		} else {
			stv0900_stop_all_s2_modcod(intp, STV0900_DEMOD_2);
			stv0900_activate_s2_modcod_single(intp,
							STV0900_DEMOD_1);
			stv0900_write_reg(intp, R0900_GENCFG, 0x04);
		}

		intp->demod_mode = STV0900_SINGLE;

		stv0900_write_bits(intp, F0900_FRESFEC, 1);
		stv0900_write_bits(intp, F0900_FRESFEC, 0);
		stv0900_write_bits(intp, F0900_P1_ALGOSWRST, 1);
		stv0900_write_bits(intp, F0900_P1_ALGOSWRST, 0);
		stv0900_write_bits(intp, F0900_P2_ALGOSWRST, 1);
		stv0900_write_bits(intp, F0900_P2_ALGOSWRST, 0);
		break;
	}

	return error;
}

static enum fe_stv0900_error stv0900_init_internal(struct dvb_frontend *fe,
					struct stv0900_init_params *p_init)
{
	struct stv0900_state *state = fe->demodulator_priv;
	enum fe_stv0900_error error = STV0900_NO_ERROR;
	enum fe_stv0900_error demodError = STV0900_NO_ERROR;
	struct stv0900_internal *intp = NULL;
	int selosci, i;

	struct stv0900_inode *temp_int = find_inode(state->i2c_adap,
						state->config->demod_address);

	dprintk("%s\n", __func__);

	if ((temp_int != NULL) && (p_init->demod_mode == STV0900_DUAL)) {
		state->internal = temp_int->internal;
		(state->internal->dmds_used)++;
		dprintk("%s: Find Internal Structure!\n", __func__);
		return STV0900_NO_ERROR;
	} else {
		state->internal = kmalloc(sizeof(struct stv0900_internal),
								GFP_KERNEL);
		if (state->internal == NULL)
			return STV0900_INVALID_HANDLE;
		temp_int = append_internal(state->internal);
		if (temp_int == NULL) {
			kfree(state->internal);
			state->internal = NULL;
			return STV0900_INVALID_HANDLE;
		}
		state->internal->dmds_used = 1;
		state->internal->i2c_adap = state->i2c_adap;
		state->internal->i2c_addr = state->config->demod_address;
		state->internal->clkmode = state->config->clkmode;
		state->internal->errs = STV0900_NO_ERROR;
		dprintk("%s: Create New Internal Structure!\n", __func__);
	}

	if (state->internal == NULL) {
		error = STV0900_INVALID_HANDLE;
		return error;
	}

	demodError = stv0900_initialize(state->internal);
	if (demodError == STV0900_NO_ERROR) {
			error = STV0900_NO_ERROR;
	} else {
		if (demodError == STV0900_INVALID_HANDLE)
			error = STV0900_INVALID_HANDLE;
		else
			error = STV0900_I2C_ERROR;

		return error;
	}

	intp = state->internal;

	intp->demod_mode = p_init->demod_mode;
	stv0900_st_dvbs2_single(intp, intp->demod_mode,	STV0900_DEMOD_1);
	intp->chip_id = stv0900_read_reg(intp, R0900_MID);
	intp->rolloff = p_init->rolloff;
	intp->quartz = p_init->dmd_ref_clk;

	stv0900_write_bits(intp, F0900_P1_ROLLOFF_CONTROL, p_init->rolloff);
	stv0900_write_bits(intp, F0900_P2_ROLLOFF_CONTROL, p_init->rolloff);

	intp->ts_config = p_init->ts_config;
	if (intp->ts_config == NULL)
		stv0900_set_ts_parallel_serial(intp,
				p_init->path1_ts_clock,
				p_init->path2_ts_clock);
	else {
		for (i = 0; intp->ts_config[i].addr != 0xffff; i++)
			stv0900_write_reg(intp,
					intp->ts_config[i].addr,
					intp->ts_config[i].val);

		stv0900_write_bits(intp, F0900_P2_RST_HWARE, 1);
		stv0900_write_bits(intp, F0900_P2_RST_HWARE, 0);
		stv0900_write_bits(intp, F0900_P1_RST_HWARE, 1);
		stv0900_write_bits(intp, F0900_P1_RST_HWARE, 0);
	}

	intp->tuner_type[0] = p_init->tuner1_type;
	intp->tuner_type[1] = p_init->tuner2_type;
	/* tuner init */
	switch (p_init->tuner1_type) {
	case 3: /*FE_AUTO_STB6100:*/
		stv0900_write_reg(intp, R0900_P1_TNRCFG, 0x3c);
		stv0900_write_reg(intp, R0900_P1_TNRCFG2, 0x86);
		stv0900_write_reg(intp, R0900_P1_TNRCFG3, 0x18);
		stv0900_write_reg(intp, R0900_P1_TNRXTAL, 27); /* 27MHz */
		stv0900_write_reg(intp, R0900_P1_TNRSTEPS, 0x05);
		stv0900_write_reg(intp, R0900_P1_TNRGAIN, 0x17);
		stv0900_write_reg(intp, R0900_P1_TNRADJ, 0x1f);
		stv0900_write_reg(intp, R0900_P1_TNRCTL2, 0x0);
		stv0900_write_bits(intp, F0900_P1_TUN_TYPE, 3);
		break;
	/* case FE_SW_TUNER: */
	default:
		stv0900_write_bits(intp, F0900_P1_TUN_TYPE, 6);
		break;
	}

	stv0900_write_bits(intp, F0900_P1_TUN_MADDRESS, p_init->tun1_maddress);
	switch (p_init->tuner1_adc) {
	case 1:
		stv0900_write_reg(intp, R0900_TSTTNR1, 0x26);
		break;
	default:
		break;
	}

	stv0900_write_reg(intp, R0900_P1_TNRLD, 1); /* hw tuner */

	/* tuner init */
	switch (p_init->tuner2_type) {
	case 3: /*FE_AUTO_STB6100:*/
		stv0900_write_reg(intp, R0900_P2_TNRCFG, 0x3c);
		stv0900_write_reg(intp, R0900_P2_TNRCFG2, 0x86);
		stv0900_write_reg(intp, R0900_P2_TNRCFG3, 0x18);
		stv0900_write_reg(intp, R0900_P2_TNRXTAL, 27); /* 27MHz */
		stv0900_write_reg(intp, R0900_P2_TNRSTEPS, 0x05);
		stv0900_write_reg(intp, R0900_P2_TNRGAIN, 0x17);
		stv0900_write_reg(intp, R0900_P2_TNRADJ, 0x1f);
		stv0900_write_reg(intp, R0900_P2_TNRCTL2, 0x0);
		stv0900_write_bits(intp, F0900_P2_TUN_TYPE, 3);
		break;
	/* case FE_SW_TUNER: */
	default:
		stv0900_write_bits(intp, F0900_P2_TUN_TYPE, 6);
		break;
	}

	stv0900_write_bits(intp, F0900_P2_TUN_MADDRESS, p_init->tun2_maddress);
	switch (p_init->tuner2_adc) {
	case 1:
		stv0900_write_reg(intp, R0900_TSTTNR3, 0x26);
		break;
	default:
		break;
	}

	stv0900_write_reg(intp, R0900_P2_TNRLD, 1); /* hw tuner */

	stv0900_write_bits(intp, F0900_P1_TUN_IQSWAP, p_init->tun1_iq_inv);
	stv0900_write_bits(intp, F0900_P2_TUN_IQSWAP, p_init->tun2_iq_inv);
	stv0900_set_mclk(intp, 135000000);
	msleep(3);

	switch (intp->clkmode) {
	case 0:
	case 2:
		stv0900_write_reg(intp, R0900_SYNTCTRL, 0x20 | intp->clkmode);
		break;
	default:
		selosci = 0x02 & stv0900_read_reg(intp, R0900_SYNTCTRL);
		stv0900_write_reg(intp, R0900_SYNTCTRL, 0x20 | selosci);
		break;
	}
	msleep(3);

	intp->mclk = stv0900_get_mclk_freq(intp, intp->quartz);
	if (intp->errs)
		error = STV0900_I2C_ERROR;

	return error;
}

static int stv0900_status(struct stv0900_internal *intp,
					enum fe_stv0900_demod_num demod)
{
	enum fe_stv0900_search_state demod_state;
	int locked = FALSE;
	u8 tsbitrate0_val, tsbitrate1_val;
	s32 bitrate;

	demod_state = stv0900_get_bits(intp, HEADER_MODE);
	switch (demod_state) {
	case STV0900_SEARCH:
	case STV0900_PLH_DETECTED:
	default:
		locked = FALSE;
		break;
	case STV0900_DVBS2_FOUND:
		locked = stv0900_get_bits(intp, LOCK_DEFINITIF) &&
				stv0900_get_bits(intp, PKTDELIN_LOCK) &&
				stv0900_get_bits(intp, TSFIFO_LINEOK);
		break;
	case STV0900_DVBS_FOUND:
		locked = stv0900_get_bits(intp, LOCK_DEFINITIF) &&
				stv0900_get_bits(intp, LOCKEDVIT) &&
				stv0900_get_bits(intp, TSFIFO_LINEOK);
		break;
	}

	dprintk("%s: locked = %d\n", __func__, locked);

	if (stvdebug) {
		/* Print TS bitrate */
		tsbitrate0_val = stv0900_read_reg(intp, TSBITRATE0);
		tsbitrate1_val = stv0900_read_reg(intp, TSBITRATE1);
		/* Formula Bit rate = Mclk * px_tsfifo_bitrate / 16384 */
		bitrate = (stv0900_get_mclk_freq(intp, intp->quartz)/1000000)
			* (tsbitrate1_val << 8 | tsbitrate0_val);
		bitrate /= 16384;
		dprintk("TS bitrate = %d Mbit/sec\n", bitrate);
	}

	return locked;
}

static int stv0900_set_mis(struct stv0900_internal *intp,
				enum fe_stv0900_demod_num demod, int mis)
{
	enum fe_stv0900_error error = STV0900_NO_ERROR;

	dprintk("%s\n", __func__);

	if (mis < 0 || mis > 255) {
		dprintk("Disable MIS filtering\n");
		stv0900_write_bits(intp, FILTER_EN, 0);
	} else {
		dprintk("Enable MIS filtering - %d\n", mis);
		stv0900_write_bits(intp, FILTER_EN, 1);
		stv0900_write_reg(intp, ISIENTRY, mis);
		stv0900_write_reg(intp, ISIBITENA, 0xff);
	}

	return error;
}


static enum dvbfe_search stv0900_search(struct dvb_frontend *fe)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *intp = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	struct stv0900_search_params p_search;
	struct stv0900_signal_info p_result = intp->result[demod];

	enum fe_stv0900_error error = STV0900_NO_ERROR;

	dprintk("%s: ", __func__);

	if (!(INRANGE(100000, c->symbol_rate, 70000000)))
		return DVBFE_ALGO_SEARCH_FAILED;

	if (state->config->set_ts_params)
		state->config->set_ts_params(fe, 0);

	stv0900_set_mis(intp, demod, c->stream_id);

	p_result.locked = FALSE;
	p_search.path = demod;
	p_search.frequency = c->frequency;
	p_search.symbol_rate = c->symbol_rate;
	p_search.search_range = 10000000;
	p_search.fec = STV0900_FEC_UNKNOWN;
	p_search.standard = STV0900_AUTO_SEARCH;
	p_search.iq_inversion = STV0900_IQ_AUTO;
	p_search.search_algo = STV0900_BLIND_SEARCH;
	/* Speeds up DVB-S searching */
	if (c->delivery_system == SYS_DVBS)
		p_search.standard = STV0900_SEARCH_DVBS1;

	intp->srch_standard[demod] = p_search.standard;
	intp->symbol_rate[demod] = p_search.symbol_rate;
	intp->srch_range[demod] = p_search.search_range;
	intp->freq[demod] = p_search.frequency;
	intp->srch_algo[demod] = p_search.search_algo;
	intp->srch_iq_inv[demod] = p_search.iq_inversion;
	intp->fec[demod] = p_search.fec;
	if ((stv0900_algo(fe) == STV0900_RANGEOK) &&
				(intp->errs == STV0900_NO_ERROR)) {
		p_result.locked = intp->result[demod].locked;
		p_result.standard = intp->result[demod].standard;
		p_result.frequency = intp->result[demod].frequency;
		p_result.symbol_rate = intp->result[demod].symbol_rate;
		p_result.fec = intp->result[demod].fec;
		p_result.modcode = intp->result[demod].modcode;
		p_result.pilot = intp->result[demod].pilot;
		p_result.frame_len = intp->result[demod].frame_len;
		p_result.spectrum = intp->result[demod].spectrum;
		p_result.rolloff = intp->result[demod].rolloff;
		p_result.modulation = intp->result[demod].modulation;
	} else {
		p_result.locked = FALSE;
		switch (intp->err[demod]) {
		case STV0900_I2C_ERROR:
			error = STV0900_I2C_ERROR;
			break;
		case STV0900_NO_ERROR:
		default:
			error = STV0900_SEARCH_FAILED;
			break;
		}
	}

	if ((p_result.locked == TRUE) && (error == STV0900_NO_ERROR)) {
		dprintk("Search Success\n");
		return DVBFE_ALGO_SEARCH_SUCCESS;
	} else {
		dprintk("Search Fail\n");
		return DVBFE_ALGO_SEARCH_FAILED;
	}

}

static int stv0900_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct stv0900_state *state = fe->demodulator_priv;

	dprintk("%s: ", __func__);

	if ((stv0900_status(state->internal, state->demod)) == TRUE) {
		dprintk("DEMOD LOCK OK\n");
		*status = FE_HAS_CARRIER
			| FE_HAS_VITERBI
			| FE_HAS_SYNC
			| FE_HAS_LOCK;
		if (state->config->set_lock_led)
			state->config->set_lock_led(fe, 1);
	} else {
		*status = 0;
		if (state->config->set_lock_led)
			state->config->set_lock_led(fe, 0);
		dprintk("DEMOD LOCK FAIL\n");
	}

	return 0;
}

static int stv0900_stop_ts(struct dvb_frontend *fe, int stop_ts)
{

	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *intp = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;

	if (stop_ts == TRUE)
		stv0900_write_bits(intp, RST_HWARE, 1);
	else
		stv0900_write_bits(intp, RST_HWARE, 0);

	return 0;
}

static int stv0900_diseqc_init(struct dvb_frontend *fe)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *intp = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;

	stv0900_write_bits(intp, DISTX_MODE, state->config->diseqc_mode);
	stv0900_write_bits(intp, DISEQC_RESET, 1);
	stv0900_write_bits(intp, DISEQC_RESET, 0);

	return 0;
}

static int stv0900_init(struct dvb_frontend *fe)
{
	dprintk("%s\n", __func__);

	stv0900_stop_ts(fe, 1);
	stv0900_diseqc_init(fe);

	return 0;
}

static int stv0900_diseqc_send(struct stv0900_internal *intp , u8 *data,
				u32 NbData, enum fe_stv0900_demod_num demod)
{
	s32 i = 0;

	stv0900_write_bits(intp, DIS_PRECHARGE, 1);
	while (i < NbData) {
		while (stv0900_get_bits(intp, FIFO_FULL))
			;/* checkpatch complains */
		stv0900_write_reg(intp, DISTXDATA, data[i]);
		i++;
	}

	stv0900_write_bits(intp, DIS_PRECHARGE, 0);
	i = 0;
	while ((stv0900_get_bits(intp, TX_IDLE) != 1) && (i < 10)) {
		msleep(10);
		i++;
	}

	return 0;
}

static int stv0900_send_master_cmd(struct dvb_frontend *fe,
					struct dvb_diseqc_master_cmd *cmd)
{
	struct stv0900_state *state = fe->demodulator_priv;

	return stv0900_diseqc_send(state->internal,
				cmd->msg,
				cmd->msg_len,
				state->demod);
}

static int stv0900_send_burst(struct dvb_frontend *fe, fe_sec_mini_cmd_t burst)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *intp = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;
	u8 data;


	switch (burst) {
	case SEC_MINI_A:
		stv0900_write_bits(intp, DISTX_MODE, 3);/* Unmodulated */
		data = 0x00;
		stv0900_diseqc_send(intp, &data, 1, state->demod);
		break;
	case SEC_MINI_B:
		stv0900_write_bits(intp, DISTX_MODE, 2);/* Modulated */
		data = 0xff;
		stv0900_diseqc_send(intp, &data, 1, state->demod);
		break;
	}

	return 0;
}

static int stv0900_recv_slave_reply(struct dvb_frontend *fe,
				struct dvb_diseqc_slave_reply *reply)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *intp = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;
	s32 i = 0;

	reply->msg_len = 0;

	while ((stv0900_get_bits(intp, RX_END) != 1) && (i < 10)) {
		msleep(10);
		i++;
	}

	if (stv0900_get_bits(intp, RX_END)) {
		reply->msg_len = stv0900_get_bits(intp, FIFO_BYTENBR);

		for (i = 0; i < reply->msg_len; i++)
			reply->msg[i] = stv0900_read_reg(intp, DISRXDATA);
	}

	return 0;
}

static int stv0900_set_tone(struct dvb_frontend *fe, fe_sec_tone_mode_t toneoff)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *intp = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;

	dprintk("%s: %s\n", __func__, ((toneoff == 0) ? "On" : "Off"));

	switch (toneoff) {
	case SEC_TONE_ON:
		/*Set the DiseqC mode to 22Khz _continues_ tone*/
		stv0900_write_bits(intp, DISTX_MODE, 0);
		stv0900_write_bits(intp, DISEQC_RESET, 1);
		/*release DiseqC reset to enable the 22KHz tone*/
		stv0900_write_bits(intp, DISEQC_RESET, 0);
		break;
	case SEC_TONE_OFF:
		/*return diseqc mode to config->diseqc_mode.
		Usually it's without _continues_ tone */
		stv0900_write_bits(intp, DISTX_MODE,
				state->config->diseqc_mode);
		/*maintain the DiseqC reset to disable the 22KHz tone*/
		stv0900_write_bits(intp, DISEQC_RESET, 1);
		stv0900_write_bits(intp, DISEQC_RESET, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void stv0900_release(struct dvb_frontend *fe)
{
	struct stv0900_state *state = fe->demodulator_priv;

	dprintk("%s\n", __func__);

	if (state->config->set_lock_led)
		state->config->set_lock_led(fe, 0);

	if ((--(state->internal->dmds_used)) <= 0) {

		dprintk("%s: Actually removing\n", __func__);

		remove_inode(state->internal);
		kfree(state->internal);
	}

	kfree(state);
}

static int stv0900_sleep(struct dvb_frontend *fe)
{
	struct stv0900_state *state = fe->demodulator_priv;

	dprintk("%s\n", __func__);

	if (state->config->set_lock_led)
		state->config->set_lock_led(fe, 0);

	return 0;
}

static int stv0900_get_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *intp = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;
	struct stv0900_signal_info p_result = intp->result[demod];

	p->frequency = p_result.locked ? p_result.frequency : 0;
	p->symbol_rate = p_result.locked ? p_result.symbol_rate : 0;
	return 0;
}

static struct dvb_frontend_ops stv0900_ops = {
	.delsys = { SYS_DVBS, SYS_DVBS2, SYS_DSS },
	.info = {
		.name			= "STV0900 frontend",
		.frequency_min		= 950000,
		.frequency_max		= 2150000,
		.frequency_stepsize	= 125,
		.frequency_tolerance	= 0,
		.symbol_rate_min	= 1000000,
		.symbol_rate_max	= 45000000,
		.symbol_rate_tolerance	= 500,
		.caps			= FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 |
					  FE_CAN_FEC_3_4 | FE_CAN_FEC_5_6 |
					  FE_CAN_FEC_7_8 | FE_CAN_QPSK    |
					  FE_CAN_2G_MODULATION |
					  FE_CAN_FEC_AUTO
	},
	.release			= stv0900_release,
	.init				= stv0900_init,
	.get_frontend                   = stv0900_get_frontend,
	.sleep				= stv0900_sleep,
	.get_frontend_algo		= stv0900_frontend_algo,
	.i2c_gate_ctrl			= stv0900_i2c_gate_ctrl,
	.diseqc_send_master_cmd		= stv0900_send_master_cmd,
	.diseqc_send_burst		= stv0900_send_burst,
	.diseqc_recv_slave_reply	= stv0900_recv_slave_reply,
	.set_tone			= stv0900_set_tone,
	.search				= stv0900_search,
	.read_status			= stv0900_read_status,
	.read_ber			= stv0900_read_ber,
	.read_signal_strength		= stv0900_read_signal_strength,
	.read_snr			= stv0900_read_snr,
	.read_ucblocks                  = stv0900_read_ucblocks,
};

struct dvb_frontend *stv0900_attach(const struct stv0900_config *config,
					struct i2c_adapter *i2c,
					int demod)
{
	struct stv0900_state *state = NULL;
	struct stv0900_init_params init_params;
	enum fe_stv0900_error err_stv0900;

	state = kzalloc(sizeof(struct stv0900_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	state->demod		= demod;
	state->config		= config;
	state->i2c_adap		= i2c;

	memcpy(&state->frontend.ops, &stv0900_ops,
			sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;

	switch (demod) {
	case 0:
	case 1:
		init_params.dmd_ref_clk  	= config->xtal;
		init_params.demod_mode		= config->demod_mode;
		init_params.rolloff		= STV0900_35;
		init_params.path1_ts_clock	= config->path1_mode;
		init_params.tun1_maddress	= config->tun1_maddress;
		init_params.tun1_iq_inv		= STV0900_IQ_NORMAL;
		init_params.tuner1_adc		= config->tun1_adc;
		init_params.tuner1_type		= config->tun1_type;
		init_params.path2_ts_clock	= config->path2_mode;
		init_params.ts_config		= config->ts_config_regs;
		init_params.tun2_maddress	= config->tun2_maddress;
		init_params.tuner2_adc		= config->tun2_adc;
		init_params.tuner2_type		= config->tun2_type;
		init_params.tun2_iq_inv		= STV0900_IQ_SWAPPED;

		err_stv0900 = stv0900_init_internal(&state->frontend,
							&init_params);

		if (err_stv0900)
			goto error;

		if (state->internal->chip_id >= 0x30)
			state->frontend.ops.info.caps |= FE_CAN_MULTISTREAM;

		break;
	default:
		goto error;
		break;
	}

	dprintk("%s: Attaching STV0900 demodulator(%d) \n", __func__, demod);
	return &state->frontend;

error:
	dprintk("%s: Failed to attach STV0900 demodulator(%d) \n",
		__func__, demod);
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(stv0900_attach);

MODULE_PARM_DESC(debug, "Set debug");

MODULE_AUTHOR("Igor M. Liplianin");
MODULE_DESCRIPTION("ST STV0900 frontend");
MODULE_LICENSE("GPL");

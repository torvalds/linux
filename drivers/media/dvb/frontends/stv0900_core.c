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

static int stvdebug = 1;
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

		new_node->next_inode = kmalloc(sizeof(struct stv0900_inode), GFP_KERNEL);
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

void stv0900_write_reg(struct stv0900_internal *i_params, u16 reg_addr,
								u8 reg_data)
{
	u8 data[3];
	int ret;
	struct i2c_msg i2cmsg = {
		.addr  = i_params->i2c_addr,
		.flags = 0,
		.len   = 3,
		.buf   = data,
	};

	data[0] = MSB(reg_addr);
	data[1] = LSB(reg_addr);
	data[2] = reg_data;

	ret = i2c_transfer(i_params->i2c_adap, &i2cmsg, 1);
	if (ret != 1)
		dprintk(KERN_ERR "%s: i2c error %d\n", __func__, ret);
}

u8 stv0900_read_reg(struct stv0900_internal *i_params, u16 reg)
{
	int ret;
	u8 b0[] = { MSB(reg), LSB(reg) };
	u8 buf = 0;
	struct i2c_msg msg[] = {
		{
			.addr	= i_params->i2c_addr,
			.flags	= 0,
			.buf = b0,
			.len = 2,
		}, {
			.addr	= i_params->i2c_addr,
			.flags	= I2C_M_RD,
			.buf = &buf,
			.len = 1,
		},
	};

	ret = i2c_transfer(i_params->i2c_adap, msg, 2);
	if (ret != 2)
		dprintk(KERN_ERR "%s: i2c error %d, reg[0x%02x]\n",
				__func__, ret, reg);

	return buf;
}

void extract_mask_pos(u32 label, u8 *mask, u8 *pos)
{
	u8 position = 0, i = 0;

	(*mask) = label & 0xff;

	while ((position == 0) && (i < 8)) {
		position = ((*mask) >> i) & 0x01;
		i++;
	}

	(*pos) = (i - 1);
}

void stv0900_write_bits(struct stv0900_internal *i_params, u32 label, u8 val)
{
	u8 reg, mask, pos;

	reg = stv0900_read_reg(i_params, (label >> 16) & 0xffff);
	extract_mask_pos(label, &mask, &pos);

	val = mask & (val << pos);

	reg = (reg & (~mask)) | val;
	stv0900_write_reg(i_params, (label >> 16) & 0xffff, reg);

}

u8 stv0900_get_bits(struct stv0900_internal *i_params, u32 label)
{
	u8 val = 0xff;
	u8 mask, pos;

	extract_mask_pos(label, &mask, &pos);

	val = stv0900_read_reg(i_params, label >> 16);
	val = (val & mask) >> pos;

	return val;
}

enum fe_stv0900_error stv0900_initialize(struct stv0900_internal *i_params)
{
	s32 i;
	enum fe_stv0900_error error;

	if (i_params != NULL) {
		i_params->chip_id = stv0900_read_reg(i_params, R0900_MID);
		if (i_params->errs == STV0900_NO_ERROR) {
			/*Startup sequence*/
			stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x5c);
			stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x5c);
			stv0900_write_reg(i_params, R0900_P1_TNRCFG, 0x6c);
			stv0900_write_reg(i_params, R0900_P2_TNRCFG, 0x6f);
			stv0900_write_reg(i_params, R0900_P1_I2CRPT, 0x20);
			stv0900_write_reg(i_params, R0900_P2_I2CRPT, 0x20);
			stv0900_write_reg(i_params, R0900_NCOARSE, 0x13);
			msleep(3);
			stv0900_write_reg(i_params, R0900_I2CCFG, 0x08);

			switch (i_params->clkmode) {
			case 0:
			case 2:
				stv0900_write_reg(i_params, R0900_SYNTCTRL, 0x20
						| i_params->clkmode);
				break;
			default:
				/* preserve SELOSCI bit */
				i = 0x02 & stv0900_read_reg(i_params, R0900_SYNTCTRL);
				stv0900_write_reg(i_params, R0900_SYNTCTRL, 0x20 | i);
				break;
			}

			msleep(3);
			for (i = 0; i < 182; i++)
				stv0900_write_reg(i_params, STV0900_InitVal[i][0], STV0900_InitVal[i][1]);

			if (stv0900_read_reg(i_params, R0900_MID) >= 0x20) {
				stv0900_write_reg(i_params, R0900_TSGENERAL, 0x0c);
				for (i = 0; i < 32; i++)
					stv0900_write_reg(i_params, STV0900_Cut20_AddOnVal[i][0], STV0900_Cut20_AddOnVal[i][1]);
			}

			stv0900_write_reg(i_params, R0900_P1_FSPYCFG, 0x6c);
			stv0900_write_reg(i_params, R0900_P2_FSPYCFG, 0x6c);
			stv0900_write_reg(i_params, R0900_TSTRES0, 0x80);
			stv0900_write_reg(i_params, R0900_TSTRES0, 0x00);
		}
		error = i_params->errs;
	} else
		error = STV0900_INVALID_HANDLE;

	return error;

}

u32 stv0900_get_mclk_freq(struct stv0900_internal *i_params, u32 ext_clk)
{
	u32 mclk = 90000000, div = 0, ad_div = 0;

	div = stv0900_get_bits(i_params, F0900_M_DIV);
	ad_div = ((stv0900_get_bits(i_params, F0900_SELX1RATIO) == 1) ? 4 : 6);

	mclk = (div + 1) * ext_clk / ad_div;

	dprintk(KERN_INFO "%s: Calculated Mclk = %d\n", __func__, mclk);

	return mclk;
}

enum fe_stv0900_error stv0900_set_mclk(struct stv0900_internal *i_params, u32 mclk)
{
	enum fe_stv0900_error error = STV0900_NO_ERROR;
	u32 m_div, clk_sel;

	dprintk(KERN_INFO "%s: Mclk set to %d, Quartz = %d\n", __func__, mclk,
			i_params->quartz);

	if (i_params == NULL)
		error = STV0900_INVALID_HANDLE;
	else {
		if (i_params->errs)
			error = STV0900_I2C_ERROR;
		else {
			clk_sel = ((stv0900_get_bits(i_params, F0900_SELX1RATIO) == 1) ? 4 : 6);
			m_div = ((clk_sel * mclk) / i_params->quartz) - 1;
			stv0900_write_bits(i_params, F0900_M_DIV, m_div);
			i_params->mclk = stv0900_get_mclk_freq(i_params,
							i_params->quartz);

			/*Set the DiseqC frequency to 22KHz */
			/*
				Formula:
				DiseqC_TX_Freq= MasterClock/(32*F22TX_Reg)
				DiseqC_RX_Freq= MasterClock/(32*F22RX_Reg)
			*/
			m_div = i_params->mclk / 704000;
			stv0900_write_reg(i_params, R0900_P1_F22TX, m_div);
			stv0900_write_reg(i_params, R0900_P1_F22RX, m_div);

			stv0900_write_reg(i_params, R0900_P2_F22TX, m_div);
			stv0900_write_reg(i_params, R0900_P2_F22RX, m_div);

			if ((i_params->errs))
				error = STV0900_I2C_ERROR;
		}
	}

	return error;
}

u32 stv0900_get_err_count(struct stv0900_internal *i_params, int cntr,
					enum fe_stv0900_demod_num demod)
{
	u32 lsb, msb, hsb, err_val;
	s32 err1field_hsb, err1field_msb, err1field_lsb;
	s32 err2field_hsb, err2field_msb, err2field_lsb;

	dmd_reg(err1field_hsb, F0900_P1_ERR_CNT12, F0900_P2_ERR_CNT12);
	dmd_reg(err1field_msb, F0900_P1_ERR_CNT11, F0900_P2_ERR_CNT11);
	dmd_reg(err1field_lsb, F0900_P1_ERR_CNT10, F0900_P2_ERR_CNT10);

	dmd_reg(err2field_hsb, F0900_P1_ERR_CNT22, F0900_P2_ERR_CNT22);
	dmd_reg(err2field_msb, F0900_P1_ERR_CNT21, F0900_P2_ERR_CNT21);
	dmd_reg(err2field_lsb, F0900_P1_ERR_CNT20, F0900_P2_ERR_CNT20);

	switch (cntr) {
	case 0:
	default:
		hsb = stv0900_get_bits(i_params, err1field_hsb);
		msb = stv0900_get_bits(i_params, err1field_msb);
		lsb = stv0900_get_bits(i_params, err1field_lsb);
		break;
	case 1:
		hsb = stv0900_get_bits(i_params, err2field_hsb);
		msb = stv0900_get_bits(i_params, err2field_msb);
		lsb = stv0900_get_bits(i_params, err2field_lsb);
		break;
	}

	err_val = (hsb << 16) + (msb << 8) + (lsb);

	return err_val;
}

static int stv0900_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *i_params = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;

	u32 fi2c;

	dmd_reg(fi2c, F0900_P1_I2CT_ON, F0900_P2_I2CT_ON);

	stv0900_write_bits(i_params, fi2c, enable);

	return 0;
}

static void stv0900_set_ts_parallel_serial(struct stv0900_internal *i_params,
					enum fe_stv0900_clock_type path1_ts,
					enum fe_stv0900_clock_type path2_ts)
{

	dprintk(KERN_INFO "%s\n", __func__);

	if (i_params->chip_id >= 0x20) {
		switch (path1_ts) {
		case STV0900_PARALLEL_PUNCT_CLOCK:
		case STV0900_DVBCI_CLOCK:
			switch (path2_ts) {
			case STV0900_SERIAL_PUNCT_CLOCK:
			case STV0900_SERIAL_CONT_CLOCK:
			default:
				stv0900_write_reg(i_params, R0900_TSGENERAL,
							0x00);
				break;
			case STV0900_PARALLEL_PUNCT_CLOCK:
			case STV0900_DVBCI_CLOCK:
				stv0900_write_reg(i_params, R0900_TSGENERAL,
							0x06);
				stv0900_write_bits(i_params,
						F0900_P1_TSFIFO_MANSPEED, 3);
				stv0900_write_bits(i_params,
						F0900_P2_TSFIFO_MANSPEED, 0);
				stv0900_write_reg(i_params,
						R0900_P1_TSSPEED, 0x14);
				stv0900_write_reg(i_params,
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
				stv0900_write_reg(i_params,
						R0900_TSGENERAL, 0x0C);
				break;
			case STV0900_PARALLEL_PUNCT_CLOCK:
			case STV0900_DVBCI_CLOCK:
				stv0900_write_reg(i_params,
						R0900_TSGENERAL, 0x0A);
				dprintk(KERN_INFO "%s: 0x0a\n", __func__);
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
				stv0900_write_reg(i_params, R0900_TSGENERAL1X,
							0x10);
				break;
			case STV0900_PARALLEL_PUNCT_CLOCK:
			case STV0900_DVBCI_CLOCK:
				stv0900_write_reg(i_params, R0900_TSGENERAL1X,
							0x16);
				stv0900_write_bits(i_params,
						F0900_P1_TSFIFO_MANSPEED, 3);
				stv0900_write_bits(i_params,
						F0900_P2_TSFIFO_MANSPEED, 0);
				stv0900_write_reg(i_params, R0900_P1_TSSPEED,
							0x14);
				stv0900_write_reg(i_params, R0900_P2_TSSPEED,
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
				stv0900_write_reg(i_params, R0900_TSGENERAL1X,
							0x14);
				break;
			case STV0900_PARALLEL_PUNCT_CLOCK:
			case STV0900_DVBCI_CLOCK:
				stv0900_write_reg(i_params, R0900_TSGENERAL1X,
							0x12);
				dprintk(KERN_INFO "%s: 0x12\n", __func__);
				break;
			}

			break;
		}
	}

	switch (path1_ts) {
	case STV0900_PARALLEL_PUNCT_CLOCK:
		stv0900_write_bits(i_params, F0900_P1_TSFIFO_SERIAL, 0x00);
		stv0900_write_bits(i_params, F0900_P1_TSFIFO_DVBCI, 0x00);
		break;
	case STV0900_DVBCI_CLOCK:
		stv0900_write_bits(i_params, F0900_P1_TSFIFO_SERIAL, 0x00);
		stv0900_write_bits(i_params, F0900_P1_TSFIFO_DVBCI, 0x01);
		break;
	case STV0900_SERIAL_PUNCT_CLOCK:
		stv0900_write_bits(i_params, F0900_P1_TSFIFO_SERIAL, 0x01);
		stv0900_write_bits(i_params, F0900_P1_TSFIFO_DVBCI, 0x00);
		break;
	case STV0900_SERIAL_CONT_CLOCK:
		stv0900_write_bits(i_params, F0900_P1_TSFIFO_SERIAL, 0x01);
		stv0900_write_bits(i_params, F0900_P1_TSFIFO_DVBCI, 0x01);
		break;
	default:
		break;
	}

	switch (path2_ts) {
	case STV0900_PARALLEL_PUNCT_CLOCK:
		stv0900_write_bits(i_params, F0900_P2_TSFIFO_SERIAL, 0x00);
		stv0900_write_bits(i_params, F0900_P2_TSFIFO_DVBCI, 0x00);
		break;
	case STV0900_DVBCI_CLOCK:
		stv0900_write_bits(i_params, F0900_P2_TSFIFO_SERIAL, 0x00);
		stv0900_write_bits(i_params, F0900_P2_TSFIFO_DVBCI, 0x01);
		break;
	case STV0900_SERIAL_PUNCT_CLOCK:
		stv0900_write_bits(i_params, F0900_P2_TSFIFO_SERIAL, 0x01);
		stv0900_write_bits(i_params, F0900_P2_TSFIFO_DVBCI, 0x00);
		break;
	case STV0900_SERIAL_CONT_CLOCK:
		stv0900_write_bits(i_params, F0900_P2_TSFIFO_SERIAL, 0x01);
		stv0900_write_bits(i_params, F0900_P2_TSFIFO_DVBCI, 0x01);
		break;
	default:
		break;
	}

	stv0900_write_bits(i_params, F0900_P2_RST_HWARE, 1);
	stv0900_write_bits(i_params, F0900_P2_RST_HWARE, 0);
	stv0900_write_bits(i_params, F0900_P1_RST_HWARE, 1);
	stv0900_write_bits(i_params, F0900_P1_RST_HWARE, 0);
}

void stv0900_set_tuner(struct dvb_frontend *fe, u32 frequency,
							u32 bandwidth)
{
	struct dvb_frontend_ops *frontend_ops = NULL;
	struct dvb_tuner_ops *tuner_ops = NULL;

	if (&fe->ops)
		frontend_ops = &fe->ops;

	if (&frontend_ops->tuner_ops)
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

	if (&fe->ops)
		frontend_ops = &fe->ops;

	if (&frontend_ops->tuner_ops)
		tuner_ops = &frontend_ops->tuner_ops;

	if (tuner_ops->set_bandwidth) {
		if ((tuner_ops->set_bandwidth(fe, bandwidth)) < 0)
			dprintk("%s: Invalid parameter\n", __func__);
		else
			dprintk("%s: Bandwidth=%d\n", __func__, bandwidth);

	}
}

static s32 stv0900_get_rf_level(struct stv0900_internal *i_params,
				const struct stv0900_table *lookup,
				enum fe_stv0900_demod_num demod)
{
	s32 agc_gain = 0,
		imin,
		imax,
		i,
		rf_lvl = 0;

	dprintk(KERN_INFO "%s\n", __func__);

	if ((lookup != NULL) && lookup->size) {
		switch (demod) {
		case STV0900_DEMOD_1:
		default:
			agc_gain = MAKEWORD(stv0900_get_bits(i_params, F0900_P1_AGCIQ_VALUE1),
						stv0900_get_bits(i_params, F0900_P1_AGCIQ_VALUE0));
			break;
		case STV0900_DEMOD_2:
			agc_gain = MAKEWORD(stv0900_get_bits(i_params, F0900_P2_AGCIQ_VALUE1),
						stv0900_get_bits(i_params, F0900_P2_AGCIQ_VALUE0));
			break;
		}

		imin = 0;
		imax = lookup->size - 1;
		if (INRANGE(lookup->table[imin].regval, agc_gain, lookup->table[imax].regval)) {
			while ((imax - imin) > 1) {
				i = (imax + imin) >> 1;

				if (INRANGE(lookup->table[imin].regval, agc_gain, lookup->table[i].regval))
					imax = i;
				else
					imin = i;
			}

			rf_lvl = (((s32)agc_gain - lookup->table[imin].regval)
					* (lookup->table[imax].realval - lookup->table[imin].realval)
					/ (lookup->table[imax].regval - lookup->table[imin].regval))
					+ lookup->table[imin].realval;
		} else if (agc_gain > lookup->table[0].regval)
			rf_lvl = 5;
		else if (agc_gain < lookup->table[lookup->size-1].regval)
			rf_lvl = -100;

	}

	dprintk(KERN_INFO "%s: RFLevel = %d\n", __func__, rf_lvl);

	return rf_lvl;
}

static int stv0900_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *internal = state->internal;
	s32 rflevel = stv0900_get_rf_level(internal, &stv0900_rf,
								state->demod);

	*strength = (rflevel + 100) * (16383 / 105);

	return 0;
}


static s32 stv0900_carr_get_quality(struct dvb_frontend *fe,
					const struct stv0900_table *lookup)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *i_params = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;

	s32 c_n = -100,
		regval, imin, imax,
		i,
		lock_flag_field,
		noise_field1,
		noise_field0;

	dprintk(KERN_INFO "%s\n", __func__);

	dmd_reg(lock_flag_field, F0900_P1_LOCK_DEFINITIF,
					F0900_P2_LOCK_DEFINITIF);
	if (stv0900_get_standard(fe, demod) == STV0900_DVBS2_STANDARD) {
		dmd_reg(noise_field1, F0900_P1_NOSPLHT_NORMED1,
					F0900_P2_NOSPLHT_NORMED1);
		dmd_reg(noise_field0, F0900_P1_NOSPLHT_NORMED0,
					F0900_P2_NOSPLHT_NORMED0);
	} else {
		dmd_reg(noise_field1, F0900_P1_NOSDATAT_NORMED1,
					F0900_P2_NOSDATAT_NORMED1);
		dmd_reg(noise_field0, F0900_P1_NOSDATAT_NORMED0,
					F0900_P2_NOSDATAT_NORMED0);
	}

	if (stv0900_get_bits(i_params, lock_flag_field)) {
		if ((lookup != NULL) && lookup->size) {
			regval = 0;
			msleep(5);
			for (i = 0; i < 16; i++) {
				regval += MAKEWORD(stv0900_get_bits(i_params,
								noise_field1),
						stv0900_get_bits(i_params,
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
	struct stv0900_internal *i_params = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;
	u8 err_val1, err_val0;
	s32 err_field1, err_field0;
	u32 header_err_val = 0;

	*ucblocks = 0x0;
	if (stv0900_get_standard(fe, demod) == STV0900_DVBS2_STANDARD) {
		/* DVB-S2 delineator errors count */

		/* retreiving number for errnous headers */
		dmd_reg(err_field0, R0900_P1_BBFCRCKO0,
					R0900_P2_BBFCRCKO0);
		dmd_reg(err_field1, R0900_P1_BBFCRCKO1,
					R0900_P2_BBFCRCKO1);

		err_val1 = stv0900_read_reg(i_params, err_field1);
		err_val0 = stv0900_read_reg(i_params, err_field0);
		header_err_val = (err_val1<<8) | err_val0;

		/* retreiving number for errnous packets */
		dmd_reg(err_field0, R0900_P1_UPCRCKO0,
					R0900_P2_UPCRCKO0);
		dmd_reg(err_field1, R0900_P1_UPCRCKO1,
					R0900_P2_UPCRCKO1);

		err_val1 = stv0900_read_reg(i_params, err_field1);
		err_val0 = stv0900_read_reg(i_params, err_field0);
		*ucblocks = (err_val1<<8) | err_val0;
		*ucblocks += header_err_val;
	}

	return 0;
}

static int stv0900_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	*snr = stv0900_carr_get_quality(fe,
			(const struct stv0900_table *)&stv0900_s2_cn);
	*snr += 30;
	*snr *= (16383 / 1030);

	return 0;
}

static u32 stv0900_get_ber(struct stv0900_internal *i_params,
				enum fe_stv0900_demod_num demod)
{
	u32 ber = 10000000, i;
	s32 dmd_state_reg;
	s32 demod_state;
	s32 vstatus_reg;
	s32 prvit_field;
	s32 pdel_status_reg;
	s32 pdel_lock_field;

	dmd_reg(dmd_state_reg, F0900_P1_HEADER_MODE, F0900_P2_HEADER_MODE);
	dmd_reg(vstatus_reg, R0900_P1_VSTATUSVIT, R0900_P2_VSTATUSVIT);
	dmd_reg(prvit_field, F0900_P1_PRFVIT, F0900_P2_PRFVIT);
	dmd_reg(pdel_status_reg, R0900_P1_PDELSTATUS1, R0900_P2_PDELSTATUS1);
	dmd_reg(pdel_lock_field, F0900_P1_PKTDELIN_LOCK,
				F0900_P2_PKTDELIN_LOCK);

	demod_state = stv0900_get_bits(i_params, dmd_state_reg);

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
			ber += stv0900_get_err_count(i_params, 0, demod);
		}

		ber /= 5;
		if (stv0900_get_bits(i_params, prvit_field)) {
			ber *= 9766;
			ber = ber >> 13;
		}

		break;
	case STV0900_DVBS2_FOUND:
		ber = 0;
		for (i = 0; i < 5; i++) {
			msleep(5);
			ber += stv0900_get_err_count(i_params, 0, demod);
		}

		ber /= 5;
		if (stv0900_get_bits(i_params, pdel_lock_field)) {
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

int stv0900_get_demod_lock(struct stv0900_internal *i_params,
			enum fe_stv0900_demod_num demod, s32 time_out)
{
	s32 timer = 0,
		lock = 0,
		header_field,
		lock_field;

	enum fe_stv0900_search_state	dmd_state;

	dmd_reg(header_field, F0900_P1_HEADER_MODE, F0900_P2_HEADER_MODE);
	dmd_reg(lock_field, F0900_P1_LOCK_DEFINITIF, F0900_P2_LOCK_DEFINITIF);
	while ((timer < time_out) && (lock == 0)) {
		dmd_state = stv0900_get_bits(i_params, header_field);
		dprintk("Demod State = %d\n", dmd_state);
		switch (dmd_state) {
		case STV0900_SEARCH:
		case STV0900_PLH_DETECTED:
		default:
			lock = 0;
			break;
		case STV0900_DVBS2_FOUND:
		case STV0900_DVBS_FOUND:
			lock = stv0900_get_bits(i_params, lock_field);
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

void stv0900_stop_all_s2_modcod(struct stv0900_internal *i_params,
				enum fe_stv0900_demod_num demod)
{
	s32 regflist,
	i;

	dprintk(KERN_INFO "%s\n", __func__);

	dmd_reg(regflist, R0900_P1_MODCODLST0, R0900_P2_MODCODLST0);

	for (i = 0; i < 16; i++)
		stv0900_write_reg(i_params, regflist + i, 0xff);
}

void stv0900_activate_s2_modcode(struct stv0900_internal *i_params,
				enum fe_stv0900_demod_num demod)
{
	u32 matype,
	mod_code,
	fmod,
	reg_index,
	field_index;

	dprintk(KERN_INFO "%s\n", __func__);

	if (i_params->chip_id <= 0x11) {
		msleep(5);

		switch (demod) {
		case STV0900_DEMOD_1:
		default:
			mod_code = stv0900_read_reg(i_params,
							R0900_P1_PLHMODCOD);
			matype = mod_code & 0x3;
			mod_code = (mod_code & 0x7f) >> 2;

			reg_index = R0900_P1_MODCODLSTF - mod_code / 2;
			field_index = mod_code % 2;
			break;
		case STV0900_DEMOD_2:
			mod_code = stv0900_read_reg(i_params,
							R0900_P2_PLHMODCOD);
			matype = mod_code & 0x3;
			mod_code = (mod_code & 0x7f) >> 2;

			reg_index = R0900_P2_MODCODLSTF - mod_code / 2;
			field_index = mod_code % 2;
			break;
		}


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
				stv0900_write_reg(i_params, reg_index,
							0xf0 | fmod);
			else
				stv0900_write_reg(i_params, reg_index,
							(fmod << 4) | 0xf);
		}
	} else if (i_params->chip_id >= 0x12) {
		switch (demod) {
		case STV0900_DEMOD_1:
		default:
			for (reg_index = 0; reg_index < 7; reg_index++)
				stv0900_write_reg(i_params, R0900_P1_MODCODLST0 + reg_index, 0xff);

			stv0900_write_reg(i_params, R0900_P1_MODCODLSTE, 0xff);
			stv0900_write_reg(i_params, R0900_P1_MODCODLSTF, 0xcf);
			for (reg_index = 0; reg_index < 8; reg_index++)
				stv0900_write_reg(i_params, R0900_P1_MODCODLST7 + reg_index, 0xcc);

			break;
		case STV0900_DEMOD_2:
			for (reg_index = 0; reg_index < 7; reg_index++)
				stv0900_write_reg(i_params, R0900_P2_MODCODLST0 + reg_index, 0xff);

			stv0900_write_reg(i_params, R0900_P2_MODCODLSTE, 0xff);
			stv0900_write_reg(i_params, R0900_P2_MODCODLSTF, 0xcf);
			for (reg_index = 0; reg_index < 8; reg_index++)
				stv0900_write_reg(i_params, R0900_P2_MODCODLST7 + reg_index, 0xcc);

			break;
		}

	}
}

void stv0900_activate_s2_modcode_single(struct stv0900_internal *i_params,
					enum fe_stv0900_demod_num demod)
{
	u32 reg_index;

	dprintk(KERN_INFO "%s\n", __func__);

	switch (demod) {
	case STV0900_DEMOD_1:
	default:
		stv0900_write_reg(i_params, R0900_P1_MODCODLST0, 0xff);
		stv0900_write_reg(i_params, R0900_P1_MODCODLST1, 0xf0);
		stv0900_write_reg(i_params, R0900_P1_MODCODLSTF, 0x0f);
		for (reg_index = 0; reg_index < 13; reg_index++)
			stv0900_write_reg(i_params,
					R0900_P1_MODCODLST2 + reg_index, 0);

		break;
	case STV0900_DEMOD_2:
		stv0900_write_reg(i_params, R0900_P2_MODCODLST0, 0xff);
		stv0900_write_reg(i_params, R0900_P2_MODCODLST1, 0xf0);
		stv0900_write_reg(i_params, R0900_P2_MODCODLSTF, 0x0f);
		for (reg_index = 0; reg_index < 13; reg_index++)
			stv0900_write_reg(i_params,
					R0900_P2_MODCODLST2 + reg_index, 0);

		break;
	}
}

static enum dvbfe_algo stv0900_frontend_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_CUSTOM;
}

static int stb0900_set_property(struct dvb_frontend *fe,
				struct dtv_property *tvp)
{
	dprintk(KERN_INFO "%s(..)\n", __func__);

	return 0;
}

static int stb0900_get_property(struct dvb_frontend *fe,
				struct dtv_property *tvp)
{
	dprintk(KERN_INFO "%s(..)\n", __func__);

	return 0;
}

void stv0900_start_search(struct stv0900_internal *i_params,
				enum fe_stv0900_demod_num demod)
{

	switch (demod) {
	case STV0900_DEMOD_1:
	default:
		stv0900_write_bits(i_params, F0900_P1_I2C_DEMOD_MODE, 0x1f);

		if (i_params->chip_id == 0x10)
			stv0900_write_reg(i_params, R0900_P1_CORRELEXP, 0xaa);

		if (i_params->chip_id < 0x20)
			stv0900_write_reg(i_params, R0900_P1_CARHDR, 0x55);

		if (i_params->dmd1_symbol_rate <= 5000000) {
			stv0900_write_reg(i_params, R0900_P1_CARCFG, 0x44);
			stv0900_write_reg(i_params, R0900_P1_CFRUP1, 0x0f);
			stv0900_write_reg(i_params, R0900_P1_CFRUP0, 0xff);
			stv0900_write_reg(i_params, R0900_P1_CFRLOW1, 0xf0);
			stv0900_write_reg(i_params, R0900_P1_CFRLOW0, 0x00);
			stv0900_write_reg(i_params, R0900_P1_RTCS2, 0x68);
		} else {
			stv0900_write_reg(i_params, R0900_P1_CARCFG, 0xc4);
			stv0900_write_reg(i_params, R0900_P1_RTCS2, 0x44);
		}

		stv0900_write_reg(i_params, R0900_P1_CFRINIT1, 0);
		stv0900_write_reg(i_params, R0900_P1_CFRINIT0, 0);

		if (i_params->chip_id >= 0x20) {
			stv0900_write_reg(i_params, R0900_P1_EQUALCFG, 0x41);
			stv0900_write_reg(i_params, R0900_P1_FFECFG, 0x41);

			if ((i_params->dmd1_srch_standard == STV0900_SEARCH_DVBS1) || (i_params->dmd1_srch_standard == STV0900_SEARCH_DSS) || (i_params->dmd1_srch_standard == STV0900_AUTO_SEARCH)) {
				stv0900_write_reg(i_params, R0900_P1_VITSCALE, 0x82);
				stv0900_write_reg(i_params, R0900_P1_VAVSRVIT, 0x0);
			}
		}

		stv0900_write_reg(i_params, R0900_P1_SFRSTEP, 0x00);
		stv0900_write_reg(i_params, R0900_P1_TMGTHRISE, 0xe0);
		stv0900_write_reg(i_params, R0900_P1_TMGTHFALL, 0xc0);
		stv0900_write_bits(i_params, F0900_P1_SCAN_ENABLE, 0);
		stv0900_write_bits(i_params, F0900_P1_CFR_AUTOSCAN, 0);
		stv0900_write_bits(i_params, F0900_P1_S1S2_SEQUENTIAL, 0);
		stv0900_write_reg(i_params, R0900_P1_RTC, 0x88);
		if (i_params->chip_id >= 0x20) {
			if (i_params->dmd1_symbol_rate < 2000000) {
				stv0900_write_reg(i_params, R0900_P1_CARFREQ, 0x39);
				stv0900_write_reg(i_params, R0900_P1_CARHDR, 0x40);
			}

			if (i_params->dmd1_symbol_rate < 10000000) {
				stv0900_write_reg(i_params, R0900_P1_CARFREQ, 0x4c);
				stv0900_write_reg(i_params, R0900_P1_CARHDR, 0x20);
			} else {
				stv0900_write_reg(i_params, R0900_P1_CARFREQ, 0x4b);
				stv0900_write_reg(i_params, R0900_P1_CARHDR, 0x20);
			}

		} else {
			if (i_params->dmd1_symbol_rate < 10000000)
				stv0900_write_reg(i_params, R0900_P1_CARFREQ, 0xef);
			else
				stv0900_write_reg(i_params, R0900_P1_CARFREQ, 0xed);
		}

		switch (i_params->dmd1_srch_algo) {
		case STV0900_WARM_START:
			stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x1f);
			stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x18);
			break;
		case STV0900_COLD_START:
			stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x1f);
			stv0900_write_reg(i_params, R0900_P1_DMDISTATE, 0x15);
			break;
		default:
			break;
		}

		break;
	case STV0900_DEMOD_2:
		stv0900_write_bits(i_params, F0900_P2_I2C_DEMOD_MODE, 0x1f);
		if (i_params->chip_id == 0x10)
			stv0900_write_reg(i_params, R0900_P2_CORRELEXP, 0xaa);

		if (i_params->chip_id < 0x20)
			stv0900_write_reg(i_params, R0900_P2_CARHDR, 0x55);

		if (i_params->dmd2_symbol_rate <= 5000000) {
			stv0900_write_reg(i_params, R0900_P2_CARCFG, 0x44);
			stv0900_write_reg(i_params, R0900_P2_CFRUP1, 0x0f);
			stv0900_write_reg(i_params, R0900_P2_CFRUP0, 0xff);
			stv0900_write_reg(i_params, R0900_P2_CFRLOW1, 0xf0);
			stv0900_write_reg(i_params, R0900_P2_CFRLOW0, 0x00);
			stv0900_write_reg(i_params, R0900_P2_RTCS2, 0x68);
		} else {
			stv0900_write_reg(i_params, R0900_P2_CARCFG, 0xc4);
			stv0900_write_reg(i_params, R0900_P2_RTCS2, 0x44);
		}

		stv0900_write_reg(i_params, R0900_P2_CFRINIT1, 0);
		stv0900_write_reg(i_params, R0900_P2_CFRINIT0, 0);

		if (i_params->chip_id >= 0x20) {
			stv0900_write_reg(i_params, R0900_P2_EQUALCFG, 0x41);
			stv0900_write_reg(i_params, R0900_P2_FFECFG, 0x41);
			if ((i_params->dmd2_srch_stndrd == STV0900_SEARCH_DVBS1) || (i_params->dmd2_srch_stndrd == STV0900_SEARCH_DSS) || (i_params->dmd2_srch_stndrd == STV0900_AUTO_SEARCH)) {
				stv0900_write_reg(i_params, R0900_P2_VITSCALE, 0x82);
				stv0900_write_reg(i_params, R0900_P2_VAVSRVIT, 0x0);
			}
		}

		stv0900_write_reg(i_params, R0900_P2_SFRSTEP, 0x00);
		stv0900_write_reg(i_params, R0900_P2_TMGTHRISE, 0xe0);
		stv0900_write_reg(i_params, R0900_P2_TMGTHFALL, 0xc0);
		stv0900_write_bits(i_params, F0900_P2_SCAN_ENABLE, 0);
		stv0900_write_bits(i_params, F0900_P2_CFR_AUTOSCAN, 0);
		stv0900_write_bits(i_params, F0900_P2_S1S2_SEQUENTIAL, 0);
		stv0900_write_reg(i_params, R0900_P2_RTC, 0x88);
		if (i_params->chip_id >= 0x20) {
			if (i_params->dmd2_symbol_rate < 2000000) {
				stv0900_write_reg(i_params, R0900_P2_CARFREQ, 0x39);
				stv0900_write_reg(i_params, R0900_P2_CARHDR, 0x40);
			}

			if (i_params->dmd2_symbol_rate < 10000000) {
				stv0900_write_reg(i_params, R0900_P2_CARFREQ, 0x4c);
				stv0900_write_reg(i_params, R0900_P2_CARHDR, 0x20);
			} else {
				stv0900_write_reg(i_params, R0900_P2_CARFREQ, 0x4b);
				stv0900_write_reg(i_params, R0900_P2_CARHDR, 0x20);
			}

		} else {
			if (i_params->dmd2_symbol_rate < 10000000)
				stv0900_write_reg(i_params, R0900_P2_CARFREQ, 0xef);
			else
				stv0900_write_reg(i_params, R0900_P2_CARFREQ, 0xed);
		}

		switch (i_params->dmd2_srch_algo) {
		case STV0900_WARM_START:
			stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x1f);
			stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x18);
			break;
		case STV0900_COLD_START:
			stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x1f);
			stv0900_write_reg(i_params, R0900_P2_DMDISTATE, 0x15);
			break;
		default:
			break;
		}

		break;
	}
}

u8 stv0900_get_optim_carr_loop(s32 srate, enum fe_stv0900_modcode modcode,
							s32 pilot, u8 chip_id)
{
	u8 aclc_value = 0x29;
	s32	i;
	const struct stv0900_car_loop_optim *car_loop_s2;

	dprintk(KERN_INFO "%s\n", __func__);

	if (chip_id <= 0x12)
		car_loop_s2 = FE_STV0900_S2CarLoop;
	else if (chip_id == 0x20)
		car_loop_s2 = FE_STV0900_S2CarLoopCut20;
	else
		car_loop_s2 = FE_STV0900_S2CarLoop;

	if (modcode < STV0900_QPSK_12) {
		i = 0;
		while ((i < 3) && (modcode != FE_STV0900_S2LowQPCarLoopCut20[i].modcode))
			i++;

		if (i >= 3)
			i = 2;
	} else {
		i = 0;
		while ((i < 14) && (modcode != car_loop_s2[i].modcode))
			i++;

		if (i >= 14) {
			i = 0;
			while ((i < 11) && (modcode != FE_STV0900_S2APSKCarLoopCut20[i].modcode))
				i++;

			if (i >= 11)
				i = 10;
		}
	}

	if (modcode <= STV0900_QPSK_25) {
		if (pilot) {
			if (srate <= 3000000)
				aclc_value = FE_STV0900_S2LowQPCarLoopCut20[i].car_loop_pilots_on_2;
			else if (srate <= 7000000)
				aclc_value = FE_STV0900_S2LowQPCarLoopCut20[i].car_loop_pilots_on_5;
			else if (srate <= 15000000)
				aclc_value = FE_STV0900_S2LowQPCarLoopCut20[i].car_loop_pilots_on_10;
			else if (srate <= 25000000)
				aclc_value = FE_STV0900_S2LowQPCarLoopCut20[i].car_loop_pilots_on_20;
			else
				aclc_value = FE_STV0900_S2LowQPCarLoopCut20[i].car_loop_pilots_on_30;
		} else {
			if (srate <= 3000000)
				aclc_value = FE_STV0900_S2LowQPCarLoopCut20[i].car_loop_pilots_off_2;
			else if (srate <= 7000000)
				aclc_value = FE_STV0900_S2LowQPCarLoopCut20[i].car_loop_pilots_off_5;
			else if (srate <= 15000000)
				aclc_value = FE_STV0900_S2LowQPCarLoopCut20[i].car_loop_pilots_off_10;
			else if (srate <= 25000000)
				aclc_value = FE_STV0900_S2LowQPCarLoopCut20[i].car_loop_pilots_off_20;
			else
				aclc_value = FE_STV0900_S2LowQPCarLoopCut20[i].car_loop_pilots_off_30;
		}

	} else if (modcode <= STV0900_8PSK_910) {
		if (pilot) {
			if (srate <= 3000000)
				aclc_value = car_loop_s2[i].car_loop_pilots_on_2;
			else if (srate <= 7000000)
				aclc_value = car_loop_s2[i].car_loop_pilots_on_5;
			else if (srate <= 15000000)
				aclc_value = car_loop_s2[i].car_loop_pilots_on_10;
			else if (srate <= 25000000)
				aclc_value = car_loop_s2[i].car_loop_pilots_on_20;
			else
				aclc_value = car_loop_s2[i].car_loop_pilots_on_30;
		} else {
			if (srate <= 3000000)
				aclc_value = car_loop_s2[i].car_loop_pilots_off_2;
			else if (srate <= 7000000)
				aclc_value = car_loop_s2[i].car_loop_pilots_off_5;
			else if (srate <= 15000000)
				aclc_value = car_loop_s2[i].car_loop_pilots_off_10;
			else if (srate <= 25000000)
				aclc_value = car_loop_s2[i].car_loop_pilots_off_20;
			else
				aclc_value = car_loop_s2[i].car_loop_pilots_off_30;
		}

	} else {
		if (srate <= 3000000)
			aclc_value = FE_STV0900_S2APSKCarLoopCut20[i].car_loop_pilots_on_2;
		else if (srate <= 7000000)
			aclc_value = FE_STV0900_S2APSKCarLoopCut20[i].car_loop_pilots_on_5;
		else if (srate <= 15000000)
			aclc_value = FE_STV0900_S2APSKCarLoopCut20[i].car_loop_pilots_on_10;
		else if (srate <= 25000000)
			aclc_value = FE_STV0900_S2APSKCarLoopCut20[i].car_loop_pilots_on_20;
		else
			aclc_value = FE_STV0900_S2APSKCarLoopCut20[i].car_loop_pilots_on_30;
	}

	return aclc_value;
}

u8 stv0900_get_optim_short_carr_loop(s32 srate, enum fe_stv0900_modulation modulation, u8 chip_id)
{
	s32 mod_index = 0;

	u8 aclc_value = 0x0b;

	dprintk(KERN_INFO "%s\n", __func__);

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

	switch (chip_id) {
	case 0x20:
		if (srate <= 3000000)
			aclc_value = FE_STV0900_S2ShortCarLoop[mod_index].car_loop_cut20_2;
		else if (srate <= 7000000)
			aclc_value = FE_STV0900_S2ShortCarLoop[mod_index].car_loop_cut20_5;
		else if (srate <= 15000000)
			aclc_value = FE_STV0900_S2ShortCarLoop[mod_index].car_loop_cut20_10;
		else if (srate <= 25000000)
			aclc_value = FE_STV0900_S2ShortCarLoop[mod_index].car_loop_cut20_20;
		else
			aclc_value = FE_STV0900_S2ShortCarLoop[mod_index].car_loop_cut20_30;

		break;
	case 0x12:
	default:
		if (srate <= 3000000)
			aclc_value = FE_STV0900_S2ShortCarLoop[mod_index].car_loop_cut12_2;
		else if (srate <= 7000000)
			aclc_value = FE_STV0900_S2ShortCarLoop[mod_index].car_loop_cut12_5;
		else if (srate <= 15000000)
			aclc_value = FE_STV0900_S2ShortCarLoop[mod_index].car_loop_cut12_10;
		else if (srate <= 25000000)
			aclc_value = FE_STV0900_S2ShortCarLoop[mod_index].car_loop_cut12_20;
		else
			aclc_value = FE_STV0900_S2ShortCarLoop[mod_index].car_loop_cut12_30;

		break;
	}

	return aclc_value;
}

static enum fe_stv0900_error stv0900_st_dvbs2_single(struct stv0900_internal *i_params,
					enum fe_stv0900_demod_mode LDPC_Mode,
					enum fe_stv0900_demod_num demod)
{
	enum fe_stv0900_error error = STV0900_NO_ERROR;

	dprintk(KERN_INFO "%s\n", __func__);

	switch (LDPC_Mode) {
	case STV0900_DUAL:
	default:
		if ((i_params->demod_mode != STV0900_DUAL)
			|| (stv0900_get_bits(i_params, F0900_DDEMOD) != 1)) {
			stv0900_write_reg(i_params, R0900_GENCFG, 0x1d);

			i_params->demod_mode = STV0900_DUAL;

			stv0900_write_bits(i_params, F0900_FRESFEC, 1);
			stv0900_write_bits(i_params, F0900_FRESFEC, 0);
		}

		break;
	case STV0900_SINGLE:
		if (demod == STV0900_DEMOD_2)
			stv0900_write_reg(i_params, R0900_GENCFG, 0x06);
		else
			stv0900_write_reg(i_params, R0900_GENCFG, 0x04);

		i_params->demod_mode = STV0900_SINGLE;

		stv0900_write_bits(i_params, F0900_FRESFEC, 1);
		stv0900_write_bits(i_params, F0900_FRESFEC, 0);
		stv0900_write_bits(i_params, F0900_P1_ALGOSWRST, 1);
		stv0900_write_bits(i_params, F0900_P1_ALGOSWRST, 0);
		stv0900_write_bits(i_params, F0900_P2_ALGOSWRST, 1);
		stv0900_write_bits(i_params, F0900_P2_ALGOSWRST, 0);
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
	int selosci, i;

	struct stv0900_inode *temp_int = find_inode(state->i2c_adap,
						state->config->demod_address);

	dprintk(KERN_INFO "%s\n", __func__);

	if (temp_int != NULL) {
		state->internal = temp_int->internal;
		(state->internal->dmds_used)++;
		dprintk(KERN_INFO "%s: Find Internal Structure!\n", __func__);
		return STV0900_NO_ERROR;
	} else {
		state->internal = kmalloc(sizeof(struct stv0900_internal), GFP_KERNEL);
		temp_int = append_internal(state->internal);
		state->internal->dmds_used = 1;
		state->internal->i2c_adap = state->i2c_adap;
		state->internal->i2c_addr = state->config->demod_address;
		state->internal->clkmode = state->config->clkmode;
		state->internal->errs = STV0900_NO_ERROR;
		dprintk(KERN_INFO "%s: Create New Internal Structure!\n", __func__);
	}

	if (state->internal != NULL) {
		demodError = stv0900_initialize(state->internal);
		if (demodError == STV0900_NO_ERROR) {
				error = STV0900_NO_ERROR;
		} else {
			if (demodError == STV0900_INVALID_HANDLE)
				error = STV0900_INVALID_HANDLE;
			else
				error = STV0900_I2C_ERROR;
		}

		if (state->internal != NULL) {
			if (error == STV0900_NO_ERROR) {
				state->internal->demod_mode = p_init->demod_mode;

				stv0900_st_dvbs2_single(state->internal, state->internal->demod_mode, STV0900_DEMOD_1);

				state->internal->chip_id = stv0900_read_reg(state->internal, R0900_MID);
				state->internal->rolloff = p_init->rolloff;
				state->internal->quartz = p_init->dmd_ref_clk;

				stv0900_write_bits(state->internal, F0900_P1_ROLLOFF_CONTROL, p_init->rolloff);
				stv0900_write_bits(state->internal, F0900_P2_ROLLOFF_CONTROL, p_init->rolloff);

				state->internal->ts_config = p_init->ts_config;
				if (state->internal->ts_config == NULL)
					stv0900_set_ts_parallel_serial(state->internal,
							p_init->path1_ts_clock,
							p_init->path2_ts_clock);
				else {
					for (i = 0; state->internal->ts_config[i].addr != 0xffff; i++)
						stv0900_write_reg(state->internal,
								state->internal->ts_config[i].addr,
								state->internal->ts_config[i].val);

					stv0900_write_bits(state->internal, F0900_P2_RST_HWARE, 1);
					stv0900_write_bits(state->internal, F0900_P2_RST_HWARE, 0);
					stv0900_write_bits(state->internal, F0900_P1_RST_HWARE, 1);
					stv0900_write_bits(state->internal, F0900_P1_RST_HWARE, 0);
				}

				stv0900_write_bits(state->internal, F0900_P1_TUN_MADDRESS, p_init->tun1_maddress);
				switch (p_init->tuner1_adc) {
				case 1:
					stv0900_write_reg(state->internal, R0900_TSTTNR1, 0x26);
					break;
				default:
					break;
				}

				stv0900_write_bits(state->internal, F0900_P2_TUN_MADDRESS, p_init->tun2_maddress);
				switch (p_init->tuner2_adc) {
				case 1:
					stv0900_write_reg(state->internal, R0900_TSTTNR3, 0x26);
					break;
				default:
					break;
				}

				stv0900_write_bits(state->internal, F0900_P1_TUN_IQSWAP, p_init->tun1_iq_inversion);
				stv0900_write_bits(state->internal, F0900_P2_TUN_IQSWAP, p_init->tun2_iq_inversion);
				stv0900_set_mclk(state->internal, 135000000);
				msleep(3);

				switch (state->internal->clkmode) {
				case 0:
				case 2:
					stv0900_write_reg(state->internal, R0900_SYNTCTRL, 0x20 | state->internal->clkmode);
					break;
				default:
					selosci = 0x02 & stv0900_read_reg(state->internal, R0900_SYNTCTRL);
					stv0900_write_reg(state->internal, R0900_SYNTCTRL, 0x20 | selosci);
					break;
				}
				msleep(3);

				state->internal->mclk = stv0900_get_mclk_freq(state->internal, state->internal->quartz);
				if (state->internal->errs)
					error = STV0900_I2C_ERROR;
			}
		} else {
			error = STV0900_INVALID_HANDLE;
		}
	}

	return error;
}

static int stv0900_status(struct stv0900_internal *i_params,
					enum fe_stv0900_demod_num demod)
{
	enum fe_stv0900_search_state demod_state;
	s32 mode_field, delin_field, lock_field, fifo_field, lockedvit_field;
	int locked = FALSE;

	dmd_reg(mode_field, F0900_P1_HEADER_MODE, F0900_P2_HEADER_MODE);
	dmd_reg(lock_field, F0900_P1_LOCK_DEFINITIF, F0900_P2_LOCK_DEFINITIF);
	dmd_reg(delin_field, F0900_P1_PKTDELIN_LOCK, F0900_P2_PKTDELIN_LOCK);
	dmd_reg(fifo_field, F0900_P1_TSFIFO_LINEOK, F0900_P2_TSFIFO_LINEOK);
	dmd_reg(lockedvit_field, F0900_P1_LOCKEDVIT, F0900_P2_LOCKEDVIT);

	demod_state = stv0900_get_bits(i_params, mode_field);
	switch (demod_state) {
	case STV0900_SEARCH:
	case STV0900_PLH_DETECTED:
	default:
		locked = FALSE;
		break;
	case STV0900_DVBS2_FOUND:
		locked = stv0900_get_bits(i_params, lock_field) &&
				stv0900_get_bits(i_params, delin_field) &&
				stv0900_get_bits(i_params, fifo_field);
		break;
	case STV0900_DVBS_FOUND:
		locked = stv0900_get_bits(i_params, lock_field) &&
				stv0900_get_bits(i_params, lockedvit_field) &&
				stv0900_get_bits(i_params, fifo_field);
		break;
	}

	return locked;
}

static enum dvbfe_search stv0900_search(struct dvb_frontend *fe,
					struct dvb_frontend_parameters *params)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *i_params = state->internal;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	struct stv0900_search_params p_search;
	struct stv0900_signal_info p_result;

	enum fe_stv0900_error error = STV0900_NO_ERROR;

	dprintk(KERN_INFO "%s: ", __func__);

	p_result.locked = FALSE;
	p_search.path = state->demod;
	p_search.frequency = c->frequency;
	p_search.symbol_rate = c->symbol_rate;
	p_search.search_range = 10000000;
	p_search.fec = STV0900_FEC_UNKNOWN;
	p_search.standard = STV0900_AUTO_SEARCH;
	p_search.iq_inversion = STV0900_IQ_AUTO;
	p_search.search_algo = STV0900_BLIND_SEARCH;

	if ((INRANGE(100000, p_search.symbol_rate, 70000000)) &&
			(INRANGE(100000, p_search.search_range, 50000000))) {
		switch (p_search.path) {
		case STV0900_DEMOD_1:
		default:
			i_params->dmd1_srch_standard = p_search.standard;
			i_params->dmd1_symbol_rate = p_search.symbol_rate;
			i_params->dmd1_srch_range = p_search.search_range;
			i_params->tuner1_freq = p_search.frequency;
			i_params->dmd1_srch_algo = p_search.search_algo;
			i_params->dmd1_srch_iq_inv = p_search.iq_inversion;
			i_params->dmd1_fec = p_search.fec;
			break;

		case STV0900_DEMOD_2:
			i_params->dmd2_srch_stndrd = p_search.standard;
			i_params->dmd2_symbol_rate = p_search.symbol_rate;
			i_params->dmd2_srch_range = p_search.search_range;
			i_params->tuner2_freq = p_search.frequency;
			i_params->dmd2_srch_algo = p_search.search_algo;
			i_params->dmd2_srch_iq_inv = p_search.iq_inversion;
			i_params->dmd2_fec = p_search.fec;
			break;
		}

		if ((stv0900_algo(fe) == STV0900_RANGEOK) &&
					(i_params->errs == STV0900_NO_ERROR)) {
			switch (p_search.path) {
			case STV0900_DEMOD_1:
			default:
				p_result.locked = i_params->dmd1_rslts.locked;
				p_result.standard = i_params->dmd1_rslts.standard;
				p_result.frequency = i_params->dmd1_rslts.frequency;
				p_result.symbol_rate = i_params->dmd1_rslts.symbol_rate;
				p_result.fec = i_params->dmd1_rslts.fec;
				p_result.modcode = i_params->dmd1_rslts.modcode;
				p_result.pilot = i_params->dmd1_rslts.pilot;
				p_result.frame_length = i_params->dmd1_rslts.frame_length;
				p_result.spectrum = i_params->dmd1_rslts.spectrum;
				p_result.rolloff = i_params->dmd1_rslts.rolloff;
				p_result.modulation = i_params->dmd1_rslts.modulation;
				break;
			case STV0900_DEMOD_2:
				p_result.locked = i_params->dmd2_rslts.locked;
				p_result.standard = i_params->dmd2_rslts.standard;
				p_result.frequency = i_params->dmd2_rslts.frequency;
				p_result.symbol_rate = i_params->dmd2_rslts.symbol_rate;
				p_result.fec = i_params->dmd2_rslts.fec;
				p_result.modcode = i_params->dmd2_rslts.modcode;
				p_result.pilot = i_params->dmd2_rslts.pilot;
				p_result.frame_length = i_params->dmd2_rslts.frame_length;
				p_result.spectrum = i_params->dmd2_rslts.spectrum;
				p_result.rolloff = i_params->dmd2_rslts.rolloff;
				p_result.modulation = i_params->dmd2_rslts.modulation;
				break;
			}

		} else {
			p_result.locked = FALSE;
			switch (p_search.path) {
			case STV0900_DEMOD_1:
				switch (i_params->dmd1_err) {
				case STV0900_I2C_ERROR:
					error = STV0900_I2C_ERROR;
					break;
				case STV0900_NO_ERROR:
				default:
					error = STV0900_SEARCH_FAILED;
					break;
				}
				break;
			case STV0900_DEMOD_2:
				switch (i_params->dmd2_err) {
				case STV0900_I2C_ERROR:
					error = STV0900_I2C_ERROR;
					break;
				case STV0900_NO_ERROR:
				default:
					error = STV0900_SEARCH_FAILED;
					break;
				}
				break;
			}
		}

	} else
		error = STV0900_BAD_PARAMETER;

	if ((p_result.locked == TRUE) && (error == STV0900_NO_ERROR)) {
		dprintk(KERN_INFO "Search Success\n");
		return DVBFE_ALGO_SEARCH_SUCCESS;
	} else {
		dprintk(KERN_INFO "Search Fail\n");
		return DVBFE_ALGO_SEARCH_FAILED;
	}

	return DVBFE_ALGO_SEARCH_ERROR;
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
	} else
		dprintk("DEMOD LOCK FAIL\n");

	return 0;
}

static int stv0900_track(struct dvb_frontend *fe,
			struct dvb_frontend_parameters *p)
{
	return 0;
}

static int stv0900_stop_ts(struct dvb_frontend *fe, int stop_ts)
{

	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *i_params = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;
	s32 rst_field;

	dmd_reg(rst_field, F0900_P1_RST_HWARE, F0900_P2_RST_HWARE);

	if (stop_ts == TRUE)
		stv0900_write_bits(i_params, rst_field, 1);
	else
		stv0900_write_bits(i_params, rst_field, 0);

	return 0;
}

static int stv0900_diseqc_init(struct dvb_frontend *fe)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *i_params = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;
	s32 mode_field, reset_field;

	dmd_reg(mode_field, F0900_P1_DISTX_MODE, F0900_P2_DISTX_MODE);
	dmd_reg(reset_field, F0900_P1_DISEQC_RESET, F0900_P2_DISEQC_RESET);

	stv0900_write_bits(i_params, mode_field, state->config->diseqc_mode);
	stv0900_write_bits(i_params, reset_field, 1);
	stv0900_write_bits(i_params, reset_field, 0);

	return 0;
}

static int stv0900_init(struct dvb_frontend *fe)
{
	dprintk(KERN_INFO "%s\n", __func__);

	stv0900_stop_ts(fe, 1);
	stv0900_diseqc_init(fe);

	return 0;
}

static int stv0900_diseqc_send(struct stv0900_internal *i_params , u8 *Data,
				u32 NbData, enum fe_stv0900_demod_num demod)
{
	s32 i = 0;

	switch (demod) {
	case STV0900_DEMOD_1:
	default:
		stv0900_write_bits(i_params, F0900_P1_DIS_PRECHARGE, 1);
		while (i < NbData) {
			while (stv0900_get_bits(i_params, F0900_P1_FIFO_FULL))
				;/* checkpatch complains */
			stv0900_write_reg(i_params, R0900_P1_DISTXDATA, Data[i]);
			i++;
		}

		stv0900_write_bits(i_params, F0900_P1_DIS_PRECHARGE, 0);
		i = 0;
		while ((stv0900_get_bits(i_params, F0900_P1_TX_IDLE) != 1) && (i < 10)) {
			msleep(10);
			i++;
		}

		break;
	case STV0900_DEMOD_2:
		stv0900_write_bits(i_params, F0900_P2_DIS_PRECHARGE, 1);

		while (i < NbData) {
			while (stv0900_get_bits(i_params, F0900_P2_FIFO_FULL))
				;/* checkpatch complains */
			stv0900_write_reg(i_params, R0900_P2_DISTXDATA, Data[i]);
			i++;
		}

		stv0900_write_bits(i_params, F0900_P2_DIS_PRECHARGE, 0);
		i = 0;
		while ((stv0900_get_bits(i_params, F0900_P2_TX_IDLE) != 1) && (i < 10)) {
			msleep(10);
			i++;
		}

		break;
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
	struct stv0900_internal *i_params = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;
	s32 mode_field;
	u32 diseqc_fifo;

	dmd_reg(mode_field, F0900_P1_DISTX_MODE, F0900_P2_DISTX_MODE);
	dmd_reg(diseqc_fifo, R0900_P1_DISTXDATA, R0900_P2_DISTXDATA);

	switch (burst) {
	case SEC_MINI_A:
		stv0900_write_bits(i_params, mode_field, 3);/* Unmodulated */
		stv0900_write_reg(i_params, diseqc_fifo, 0x00);
		break;
	case SEC_MINI_B:
		stv0900_write_bits(i_params, mode_field, 2);/* Modulated */
		stv0900_write_reg(i_params, diseqc_fifo, 0xff);
		break;
	}

	return 0;
}

static int stv0900_recv_slave_reply(struct dvb_frontend *fe,
				struct dvb_diseqc_slave_reply *reply)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *i_params = state->internal;
	s32 i = 0;

	switch (state->demod) {
	case STV0900_DEMOD_1:
	default:
		reply->msg_len = 0;

		while ((stv0900_get_bits(i_params, F0900_P1_RX_END) != 1) && (i < 10)) {
			msleep(10);
			i++;
		}

		if (stv0900_get_bits(i_params, F0900_P1_RX_END)) {
			reply->msg_len = stv0900_get_bits(i_params, F0900_P1_FIFO_BYTENBR);

			for (i = 0; i < reply->msg_len; i++)
				reply->msg[i] = stv0900_read_reg(i_params, R0900_P1_DISRXDATA);
		}
		break;
	case STV0900_DEMOD_2:
		reply->msg_len = 0;

		while ((stv0900_get_bits(i_params, F0900_P2_RX_END) != 1) && (i < 10)) {
			msleep(10);
			i++;
		}

		if (stv0900_get_bits(i_params, F0900_P2_RX_END)) {
			reply->msg_len = stv0900_get_bits(i_params, F0900_P2_FIFO_BYTENBR);

			for (i = 0; i < reply->msg_len; i++)
				reply->msg[i] = stv0900_read_reg(i_params, R0900_P2_DISRXDATA);
		}
		break;
	}

	return 0;
}

static int stv0900_set_tone(struct dvb_frontend *fe, fe_sec_tone_mode_t tone)
{
	struct stv0900_state *state = fe->demodulator_priv;
	struct stv0900_internal *i_params = state->internal;
	enum fe_stv0900_demod_num demod = state->demod;
	s32 mode_field, reset_field;

	dprintk(KERN_INFO "%s: %s\n", __func__, ((tone == 0) ? "Off" : "On"));

	dmd_reg(mode_field, F0900_P1_DISTX_MODE, F0900_P2_DISTX_MODE);
	dmd_reg(reset_field, F0900_P1_DISEQC_RESET, F0900_P2_DISEQC_RESET);

	if (tone) {
		/*Set the DiseqC mode to 22Khz continues tone*/
		stv0900_write_bits(i_params, mode_field, 0);
		stv0900_write_bits(i_params, reset_field, 1);
		/*release DiseqC reset to enable the 22KHz tone*/
		stv0900_write_bits(i_params, reset_field, 0);
	} else {
		stv0900_write_bits(i_params, mode_field, 0);
		/*maintain the DiseqC reset to disable the 22KHz tone*/
		stv0900_write_bits(i_params, reset_field, 1);
	}

	return 0;
}

static void stv0900_release(struct dvb_frontend *fe)
{
	struct stv0900_state *state = fe->demodulator_priv;

	dprintk(KERN_INFO "%s\n", __func__);

	if ((--(state->internal->dmds_used)) <= 0) {

		dprintk(KERN_INFO "%s: Actually removing\n", __func__);

		remove_inode(state->internal);
		kfree(state->internal);
	}

	kfree(state);
}

static struct dvb_frontend_ops stv0900_ops = {

	.info = {
		.name			= "STV0900 frontend",
		.type			= FE_QPSK,
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
	.get_frontend_algo		= stv0900_frontend_algo,
	.i2c_gate_ctrl			= stv0900_i2c_gate_ctrl,
	.diseqc_send_master_cmd		= stv0900_send_master_cmd,
	.diseqc_send_burst		= stv0900_send_burst,
	.diseqc_recv_slave_reply	= stv0900_recv_slave_reply,
	.set_tone			= stv0900_set_tone,
	.set_property			= stb0900_set_property,
	.get_property			= stb0900_get_property,
	.search				= stv0900_search,
	.track				= stv0900_track,
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
		init_params.demod_mode		= STV0900_DUAL;
		init_params.rolloff		= STV0900_35;
		init_params.path1_ts_clock	= config->path1_mode;
		init_params.tun1_maddress	= config->tun1_maddress;
		init_params.tun1_iq_inversion	= STV0900_IQ_NORMAL;
		init_params.tuner1_adc		= config->tun1_adc;
		init_params.path2_ts_clock	= config->path2_mode;
		init_params.ts_config		= config->ts_config_regs;
		init_params.tun2_maddress	= config->tun2_maddress;
		init_params.tuner2_adc		= config->tun2_adc;
		init_params.tun2_iq_inversion	= STV0900_IQ_SWAPPED;

		err_stv0900 = stv0900_init_internal(&state->frontend,
							&init_params);

		if (err_stv0900)
			goto error;

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

/*
    tda18271-common.c - driver for the Philips / NXP TDA18271 silicon tuner

    Copyright (C) 2007, 2008 Michael Krufky <mkrufky@linuxtv.org>

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

#include "tda18271-priv.h"

static int tda18271_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	enum tda18271_i2c_gate gate;
	int ret = 0;

	switch (priv->gate) {
	case TDA18271_GATE_DIGITAL:
	case TDA18271_GATE_ANALOG:
		gate = priv->gate;
		break;
	case TDA18271_GATE_AUTO:
	default:
		switch (priv->mode) {
		case TDA18271_DIGITAL:
			gate = TDA18271_GATE_DIGITAL;
			break;
		case TDA18271_ANALOG:
		default:
			gate = TDA18271_GATE_ANALOG;
			break;
		}
	}

	switch (gate) {
	case TDA18271_GATE_ANALOG:
		if (fe->ops.analog_ops.i2c_gate_ctrl)
			ret = fe->ops.analog_ops.i2c_gate_ctrl(fe, enable);
		break;
	case TDA18271_GATE_DIGITAL:
		if (fe->ops.i2c_gate_ctrl)
			ret = fe->ops.i2c_gate_ctrl(fe, enable);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
};

/*---------------------------------------------------------------------*/

static void tda18271_dump_regs(struct dvb_frontend *fe, int extended)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;

	tda_reg("=== TDA18271 REG DUMP ===\n");
	tda_reg("ID_BYTE            = 0x%02x\n", 0xff & regs[R_ID]);
	tda_reg("THERMO_BYTE        = 0x%02x\n", 0xff & regs[R_TM]);
	tda_reg("POWER_LEVEL_BYTE   = 0x%02x\n", 0xff & regs[R_PL]);
	tda_reg("EASY_PROG_BYTE_1   = 0x%02x\n", 0xff & regs[R_EP1]);
	tda_reg("EASY_PROG_BYTE_2   = 0x%02x\n", 0xff & regs[R_EP2]);
	tda_reg("EASY_PROG_BYTE_3   = 0x%02x\n", 0xff & regs[R_EP3]);
	tda_reg("EASY_PROG_BYTE_4   = 0x%02x\n", 0xff & regs[R_EP4]);
	tda_reg("EASY_PROG_BYTE_5   = 0x%02x\n", 0xff & regs[R_EP5]);
	tda_reg("CAL_POST_DIV_BYTE  = 0x%02x\n", 0xff & regs[R_CPD]);
	tda_reg("CAL_DIV_BYTE_1     = 0x%02x\n", 0xff & regs[R_CD1]);
	tda_reg("CAL_DIV_BYTE_2     = 0x%02x\n", 0xff & regs[R_CD2]);
	tda_reg("CAL_DIV_BYTE_3     = 0x%02x\n", 0xff & regs[R_CD3]);
	tda_reg("MAIN_POST_DIV_BYTE = 0x%02x\n", 0xff & regs[R_MPD]);
	tda_reg("MAIN_DIV_BYTE_1    = 0x%02x\n", 0xff & regs[R_MD1]);
	tda_reg("MAIN_DIV_BYTE_2    = 0x%02x\n", 0xff & regs[R_MD2]);
	tda_reg("MAIN_DIV_BYTE_3    = 0x%02x\n", 0xff & regs[R_MD3]);

	/* only dump extended regs if DBG_ADV is set */
	if (!(tda18271_debug & DBG_ADV))
		return;

	/* W indicates write-only registers.
	 * Register dump for write-only registers shows last value written. */

	tda_reg("EXTENDED_BYTE_1    = 0x%02x\n", 0xff & regs[R_EB1]);
	tda_reg("EXTENDED_BYTE_2    = 0x%02x\n", 0xff & regs[R_EB2]);
	tda_reg("EXTENDED_BYTE_3    = 0x%02x\n", 0xff & regs[R_EB3]);
	tda_reg("EXTENDED_BYTE_4    = 0x%02x\n", 0xff & regs[R_EB4]);
	tda_reg("EXTENDED_BYTE_5    = 0x%02x\n", 0xff & regs[R_EB5]);
	tda_reg("EXTENDED_BYTE_6    = 0x%02x\n", 0xff & regs[R_EB6]);
	tda_reg("EXTENDED_BYTE_7    = 0x%02x\n", 0xff & regs[R_EB7]);
	tda_reg("EXTENDED_BYTE_8    = 0x%02x\n", 0xff & regs[R_EB8]);
	tda_reg("EXTENDED_BYTE_9  W = 0x%02x\n", 0xff & regs[R_EB9]);
	tda_reg("EXTENDED_BYTE_10   = 0x%02x\n", 0xff & regs[R_EB10]);
	tda_reg("EXTENDED_BYTE_11   = 0x%02x\n", 0xff & regs[R_EB11]);
	tda_reg("EXTENDED_BYTE_12   = 0x%02x\n", 0xff & regs[R_EB12]);
	tda_reg("EXTENDED_BYTE_13   = 0x%02x\n", 0xff & regs[R_EB13]);
	tda_reg("EXTENDED_BYTE_14   = 0x%02x\n", 0xff & regs[R_EB14]);
	tda_reg("EXTENDED_BYTE_15   = 0x%02x\n", 0xff & regs[R_EB15]);
	tda_reg("EXTENDED_BYTE_16 W = 0x%02x\n", 0xff & regs[R_EB16]);
	tda_reg("EXTENDED_BYTE_17 W = 0x%02x\n", 0xff & regs[R_EB17]);
	tda_reg("EXTENDED_BYTE_18   = 0x%02x\n", 0xff & regs[R_EB18]);
	tda_reg("EXTENDED_BYTE_19 W = 0x%02x\n", 0xff & regs[R_EB19]);
	tda_reg("EXTENDED_BYTE_20 W = 0x%02x\n", 0xff & regs[R_EB20]);
	tda_reg("EXTENDED_BYTE_21   = 0x%02x\n", 0xff & regs[R_EB21]);
	tda_reg("EXTENDED_BYTE_22   = 0x%02x\n", 0xff & regs[R_EB22]);
	tda_reg("EXTENDED_BYTE_23   = 0x%02x\n", 0xff & regs[R_EB23]);
}

int tda18271_read_regs(struct dvb_frontend *fe)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	unsigned char buf = 0x00;
	int ret;
	struct i2c_msg msg[] = {
		{ .addr = priv->i2c_props.addr, .flags = 0,
		  .buf = &buf, .len = 1 },
		{ .addr = priv->i2c_props.addr, .flags = I2C_M_RD,
		  .buf = regs, .len = 16 }
	};

	tda18271_i2c_gate_ctrl(fe, 1);

	/* read all registers */
	ret = i2c_transfer(priv->i2c_props.adap, msg, 2);

	tda18271_i2c_gate_ctrl(fe, 0);

	if (ret != 2)
		tda_err("ERROR: i2c_transfer returned: %d\n", ret);

	if (tda18271_debug & DBG_REG)
		tda18271_dump_regs(fe, 0);

	return (ret == 2 ? 0 : ret);
}

int tda18271_read_extended(struct dvb_frontend *fe)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	unsigned char regdump[TDA18271_NUM_REGS];
	unsigned char buf = 0x00;
	int ret, i;
	struct i2c_msg msg[] = {
		{ .addr = priv->i2c_props.addr, .flags = 0,
		  .buf = &buf, .len = 1 },
		{ .addr = priv->i2c_props.addr, .flags = I2C_M_RD,
		  .buf = regdump, .len = TDA18271_NUM_REGS }
	};

	tda18271_i2c_gate_ctrl(fe, 1);

	/* read all registers */
	ret = i2c_transfer(priv->i2c_props.adap, msg, 2);

	tda18271_i2c_gate_ctrl(fe, 0);

	if (ret != 2)
		tda_err("ERROR: i2c_transfer returned: %d\n", ret);

	for (i = 0; i < TDA18271_NUM_REGS; i++) {
		/* don't update write-only registers */
		if ((i != R_EB9)  &&
		    (i != R_EB16) &&
		    (i != R_EB17) &&
		    (i != R_EB19) &&
		    (i != R_EB20))
		regs[i] = regdump[i];
	}

	if (tda18271_debug & DBG_REG)
		tda18271_dump_regs(fe, 1);

	return (ret == 2 ? 0 : ret);
}

static int __tda18271_write_regs(struct dvb_frontend *fe, int idx, int len,
			bool lock_i2c)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	unsigned char buf[TDA18271_NUM_REGS + 1];
	struct i2c_msg msg = { .addr = priv->i2c_props.addr, .flags = 0,
			       .buf = buf };
	int i, ret = 1, max;

	BUG_ON((len == 0) || (idx + len > sizeof(buf)));

	switch (priv->small_i2c) {
	case TDA18271_03_BYTE_CHUNK_INIT:
		max = 3;
		break;
	case TDA18271_08_BYTE_CHUNK_INIT:
		max = 8;
		break;
	case TDA18271_16_BYTE_CHUNK_INIT:
		max = 16;
		break;
	case TDA18271_39_BYTE_CHUNK_INIT:
	default:
		max = 39;
	}


	/*
	 * If lock_i2c is true, it will take the I2C bus for tda18271 private
	 * usage during the entire write ops, as otherwise, bad things could
	 * happen.
	 * During device init, several write operations will happen. So,
	 * tda18271_init_regs controls the I2C lock directly,
	 * disabling lock_i2c here.
	 */
	if (lock_i2c) {
		tda18271_i2c_gate_ctrl(fe, 1);
		i2c_lock_adapter(priv->i2c_props.adap);
	}
	while (len) {
		if (max > len)
			max = len;

		buf[0] = idx;
		for (i = 1; i <= max; i++)
			buf[i] = regs[idx - 1 + i];

		msg.len = max + 1;

		/* write registers */
		ret = __i2c_transfer(priv->i2c_props.adap, &msg, 1);
		if (ret != 1)
			break;

		idx += max;
		len -= max;
	}
	if (lock_i2c) {
		i2c_unlock_adapter(priv->i2c_props.adap);
		tda18271_i2c_gate_ctrl(fe, 0);
	}

	if (ret != 1)
		tda_err("ERROR: idx = 0x%x, len = %d, "
			"i2c_transfer returned: %d\n", idx, max, ret);

	return (ret == 1 ? 0 : ret);
}

int tda18271_write_regs(struct dvb_frontend *fe, int idx, int len)
{
	return __tda18271_write_regs(fe, idx, len, true);
}

/*---------------------------------------------------------------------*/

static int __tda18271_charge_pump_source(struct dvb_frontend *fe,
					 enum tda18271_pll pll, int force,
					 bool lock_i2c)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;

	int r_cp = (pll == TDA18271_CAL_PLL) ? R_EB7 : R_EB4;

	regs[r_cp] &= ~0x20;
	regs[r_cp] |= ((force & 1) << 5);

	return __tda18271_write_regs(fe, r_cp, 1, lock_i2c);
}

int tda18271_charge_pump_source(struct dvb_frontend *fe,
				enum tda18271_pll pll, int force)
{
	return __tda18271_charge_pump_source(fe, pll, force, true);
}


int tda18271_init_regs(struct dvb_frontend *fe)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;

	tda_dbg("initializing registers for device @ %d-%04x\n",
		i2c_adapter_id(priv->i2c_props.adap),
		priv->i2c_props.addr);

	/*
	 * Don't let any other I2C transfer to happen at adapter during init,
	 * as those could cause bad things
	 */
	tda18271_i2c_gate_ctrl(fe, 1);
	i2c_lock_adapter(priv->i2c_props.adap);

	/* initialize registers */
	switch (priv->id) {
	case TDA18271HDC1:
		regs[R_ID]   = 0x83;
		break;
	case TDA18271HDC2:
		regs[R_ID]   = 0x84;
		break;
	}

	regs[R_TM]   = 0x08;
	regs[R_PL]   = 0x80;
	regs[R_EP1]  = 0xc6;
	regs[R_EP2]  = 0xdf;
	regs[R_EP3]  = 0x16;
	regs[R_EP4]  = 0x60;
	regs[R_EP5]  = 0x80;
	regs[R_CPD]  = 0x80;
	regs[R_CD1]  = 0x00;
	regs[R_CD2]  = 0x00;
	regs[R_CD3]  = 0x00;
	regs[R_MPD]  = 0x00;
	regs[R_MD1]  = 0x00;
	regs[R_MD2]  = 0x00;
	regs[R_MD3]  = 0x00;

	switch (priv->id) {
	case TDA18271HDC1:
		regs[R_EB1]  = 0xff;
		break;
	case TDA18271HDC2:
		regs[R_EB1]  = 0xfc;
		break;
	}

	regs[R_EB2]  = 0x01;
	regs[R_EB3]  = 0x84;
	regs[R_EB4]  = 0x41;
	regs[R_EB5]  = 0x01;
	regs[R_EB6]  = 0x84;
	regs[R_EB7]  = 0x40;
	regs[R_EB8]  = 0x07;
	regs[R_EB9]  = 0x00;
	regs[R_EB10] = 0x00;
	regs[R_EB11] = 0x96;

	switch (priv->id) {
	case TDA18271HDC1:
		regs[R_EB12] = 0x0f;
		break;
	case TDA18271HDC2:
		regs[R_EB12] = 0x33;
		break;
	}

	regs[R_EB13] = 0xc1;
	regs[R_EB14] = 0x00;
	regs[R_EB15] = 0x8f;
	regs[R_EB16] = 0x00;
	regs[R_EB17] = 0x00;

	switch (priv->id) {
	case TDA18271HDC1:
		regs[R_EB18] = 0x00;
		break;
	case TDA18271HDC2:
		regs[R_EB18] = 0x8c;
		break;
	}

	regs[R_EB19] = 0x00;
	regs[R_EB20] = 0x20;

	switch (priv->id) {
	case TDA18271HDC1:
		regs[R_EB21] = 0x33;
		break;
	case TDA18271HDC2:
		regs[R_EB21] = 0xb3;
		break;
	}

	regs[R_EB22] = 0x48;
	regs[R_EB23] = 0xb0;

	__tda18271_write_regs(fe, 0x00, TDA18271_NUM_REGS, false);

	/* setup agc1 gain */
	regs[R_EB17] = 0x00;
	__tda18271_write_regs(fe, R_EB17, 1, false);
	regs[R_EB17] = 0x03;
	__tda18271_write_regs(fe, R_EB17, 1, false);
	regs[R_EB17] = 0x43;
	__tda18271_write_regs(fe, R_EB17, 1, false);
	regs[R_EB17] = 0x4c;
	__tda18271_write_regs(fe, R_EB17, 1, false);

	/* setup agc2 gain */
	if ((priv->id) == TDA18271HDC1) {
		regs[R_EB20] = 0xa0;
		__tda18271_write_regs(fe, R_EB20, 1, false);
		regs[R_EB20] = 0xa7;
		__tda18271_write_regs(fe, R_EB20, 1, false);
		regs[R_EB20] = 0xe7;
		__tda18271_write_regs(fe, R_EB20, 1, false);
		regs[R_EB20] = 0xec;
		__tda18271_write_regs(fe, R_EB20, 1, false);
	}

	/* image rejection calibration */

	/* low-band */
	regs[R_EP3] = 0x1f;
	regs[R_EP4] = 0x66;
	regs[R_EP5] = 0x81;
	regs[R_CPD] = 0xcc;
	regs[R_CD1] = 0x6c;
	regs[R_CD2] = 0x00;
	regs[R_CD3] = 0x00;
	regs[R_MPD] = 0xcd;
	regs[R_MD1] = 0x77;
	regs[R_MD2] = 0x08;
	regs[R_MD3] = 0x00;

	__tda18271_write_regs(fe, R_EP3, 11, false);

	if ((priv->id) == TDA18271HDC2) {
		/* main pll cp source on */
		__tda18271_charge_pump_source(fe, TDA18271_MAIN_PLL, 1, false);
		msleep(1);

		/* main pll cp source off */
		__tda18271_charge_pump_source(fe, TDA18271_MAIN_PLL, 0, false);
	}

	msleep(5); /* pll locking */

	/* launch detector */
	__tda18271_write_regs(fe, R_EP1, 1, false);
	msleep(5); /* wanted low measurement */

	regs[R_EP5] = 0x85;
	regs[R_CPD] = 0xcb;
	regs[R_CD1] = 0x66;
	regs[R_CD2] = 0x70;

	__tda18271_write_regs(fe, R_EP3, 7, false);
	msleep(5); /* pll locking */

	/* launch optimization algorithm */
	__tda18271_write_regs(fe, R_EP2, 1, false);
	msleep(30); /* image low optimization completion */

	/* mid-band */
	regs[R_EP5] = 0x82;
	regs[R_CPD] = 0xa8;
	regs[R_CD2] = 0x00;
	regs[R_MPD] = 0xa9;
	regs[R_MD1] = 0x73;
	regs[R_MD2] = 0x1a;

	__tda18271_write_regs(fe, R_EP3, 11, false);
	msleep(5); /* pll locking */

	/* launch detector */
	__tda18271_write_regs(fe, R_EP1, 1, false);
	msleep(5); /* wanted mid measurement */

	regs[R_EP5] = 0x86;
	regs[R_CPD] = 0xa8;
	regs[R_CD1] = 0x66;
	regs[R_CD2] = 0xa0;

	__tda18271_write_regs(fe, R_EP3, 7, false);
	msleep(5); /* pll locking */

	/* launch optimization algorithm */
	__tda18271_write_regs(fe, R_EP2, 1, false);
	msleep(30); /* image mid optimization completion */

	/* high-band */
	regs[R_EP5] = 0x83;
	regs[R_CPD] = 0x98;
	regs[R_CD1] = 0x65;
	regs[R_CD2] = 0x00;
	regs[R_MPD] = 0x99;
	regs[R_MD1] = 0x71;
	regs[R_MD2] = 0xcd;

	__tda18271_write_regs(fe, R_EP3, 11, false);
	msleep(5); /* pll locking */

	/* launch detector */
	__tda18271_write_regs(fe, R_EP1, 1, false);
	msleep(5); /* wanted high measurement */

	regs[R_EP5] = 0x87;
	regs[R_CD1] = 0x65;
	regs[R_CD2] = 0x50;

	__tda18271_write_regs(fe, R_EP3, 7, false);
	msleep(5); /* pll locking */

	/* launch optimization algorithm */
	__tda18271_write_regs(fe, R_EP2, 1, false);
	msleep(30); /* image high optimization completion */

	/* return to normal mode */
	regs[R_EP4] = 0x64;
	__tda18271_write_regs(fe, R_EP4, 1, false);

	/* synchronize */
	__tda18271_write_regs(fe, R_EP1, 1, false);

	i2c_unlock_adapter(priv->i2c_props.adap);
	tda18271_i2c_gate_ctrl(fe, 0);

	return 0;
}

/*---------------------------------------------------------------------*/

/*
 *  Standby modes, EP3 [7:5]
 *
 *  | SM  || SM_LT || SM_XT || mode description
 *  |=====\\=======\\=======\\===================================
 *  |  0  ||   0   ||   0   || normal mode
 *  |-----||-------||-------||-----------------------------------
 *  |     ||       ||       || standby mode w/ slave tuner output
 *  |  1  ||   0   ||   0   || & loop thru & xtal oscillator on
 *  |-----||-------||-------||-----------------------------------
 *  |  1  ||   1   ||   0   || standby mode w/ xtal oscillator on
 *  |-----||-------||-------||-----------------------------------
 *  |  1  ||   1   ||   1   || power off
 *
 */

int tda18271_set_standby_mode(struct dvb_frontend *fe,
			      int sm, int sm_lt, int sm_xt)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;

	if (tda18271_debug & DBG_ADV)
		tda_dbg("sm = %d, sm_lt = %d, sm_xt = %d\n", sm, sm_lt, sm_xt);

	regs[R_EP3]  &= ~0xe0; /* clear sm, sm_lt, sm_xt */
	regs[R_EP3]  |= (sm    ? (1 << 7) : 0) |
			(sm_lt ? (1 << 6) : 0) |
			(sm_xt ? (1 << 5) : 0);

	return tda18271_write_regs(fe, R_EP3, 1);
}

/*---------------------------------------------------------------------*/

int tda18271_calc_main_pll(struct dvb_frontend *fe, u32 freq)
{
	/* sets main post divider & divider bytes, but does not write them */
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	u8 d, pd;
	u32 div;

	int ret = tda18271_lookup_pll_map(fe, MAIN_PLL, &freq, &pd, &d);
	if (tda_fail(ret))
		goto fail;

	regs[R_MPD]   = (0x7f & pd);

	div =  ((d * (freq / 1000)) << 7) / 125;

	regs[R_MD1]   = 0x7f & (div >> 16);
	regs[R_MD2]   = 0xff & (div >> 8);
	regs[R_MD3]   = 0xff & div;
fail:
	return ret;
}

int tda18271_calc_cal_pll(struct dvb_frontend *fe, u32 freq)
{
	/* sets cal post divider & divider bytes, but does not write them */
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	u8 d, pd;
	u32 div;

	int ret = tda18271_lookup_pll_map(fe, CAL_PLL, &freq, &pd, &d);
	if (tda_fail(ret))
		goto fail;

	regs[R_CPD]   = pd;

	div =  ((d * (freq / 1000)) << 7) / 125;

	regs[R_CD1]   = 0x7f & (div >> 16);
	regs[R_CD2]   = 0xff & (div >> 8);
	regs[R_CD3]   = 0xff & div;
fail:
	return ret;
}

/*---------------------------------------------------------------------*/

int tda18271_calc_bp_filter(struct dvb_frontend *fe, u32 *freq)
{
	/* sets bp filter bits, but does not write them */
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	u8 val;

	int ret = tda18271_lookup_map(fe, BP_FILTER, freq, &val);
	if (tda_fail(ret))
		goto fail;

	regs[R_EP1]  &= ~0x07; /* clear bp filter bits */
	regs[R_EP1]  |= (0x07 & val);
fail:
	return ret;
}

int tda18271_calc_km(struct dvb_frontend *fe, u32 *freq)
{
	/* sets K & M bits, but does not write them */
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	u8 val;

	int ret = tda18271_lookup_map(fe, RF_CAL_KMCO, freq, &val);
	if (tda_fail(ret))
		goto fail;

	regs[R_EB13] &= ~0x7c; /* clear k & m bits */
	regs[R_EB13] |= (0x7c & val);
fail:
	return ret;
}

int tda18271_calc_rf_band(struct dvb_frontend *fe, u32 *freq)
{
	/* sets rf band bits, but does not write them */
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	u8 val;

	int ret = tda18271_lookup_map(fe, RF_BAND, freq, &val);
	if (tda_fail(ret))
		goto fail;

	regs[R_EP2]  &= ~0xe0; /* clear rf band bits */
	regs[R_EP2]  |= (0xe0 & (val << 5));
fail:
	return ret;
}

int tda18271_calc_gain_taper(struct dvb_frontend *fe, u32 *freq)
{
	/* sets gain taper bits, but does not write them */
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	u8 val;

	int ret = tda18271_lookup_map(fe, GAIN_TAPER, freq, &val);
	if (tda_fail(ret))
		goto fail;

	regs[R_EP2]  &= ~0x1f; /* clear gain taper bits */
	regs[R_EP2]  |= (0x1f & val);
fail:
	return ret;
}

int tda18271_calc_ir_measure(struct dvb_frontend *fe, u32 *freq)
{
	/* sets IR Meas bits, but does not write them */
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	u8 val;

	int ret = tda18271_lookup_map(fe, IR_MEASURE, freq, &val);
	if (tda_fail(ret))
		goto fail;

	regs[R_EP5] &= ~0x07;
	regs[R_EP5] |= (0x07 & val);
fail:
	return ret;
}

int tda18271_calc_rf_cal(struct dvb_frontend *fe, u32 *freq)
{
	/* sets rf cal byte (RFC_Cprog), but does not write it */
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	u8 val;

	int ret = tda18271_lookup_map(fe, RF_CAL, freq, &val);
	/* The TDA18271HD/C1 rf_cal map lookup is expected to go out of range
	 * for frequencies above 61.1 MHz.  In these cases, the internal RF
	 * tracking filters calibration mechanism is used.
	 *
	 * There is no need to warn the user about this.
	 */
	if (ret < 0)
		goto fail;

	regs[R_EB14] = val;
fail:
	return ret;
}

void _tda_printk(struct tda18271_priv *state, const char *level,
		 const char *func, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	if (state)
		printk("%s%s: [%d-%04x|%c] %pV",
		       level, func, i2c_adapter_id(state->i2c_props.adap),
		       state->i2c_props.addr,
		       (state->role == TDA18271_MASTER) ? 'M' : 'S',
		       &vaf);
	else
		printk("%s%s: %pV", level, func, &vaf);

	va_end(args);
}

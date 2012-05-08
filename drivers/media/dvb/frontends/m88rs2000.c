/*
	Driver for M88RS2000 demodulator and tuner

	Copyright (C) 2012 Malcolm Priestley (tvboxspy@gmail.com)
	Beta Driver

	Include various calculation code from DS3000 driver.
	Copyright (C) 2009 Konstantin Dimitrov.

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
#include <linux/module.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/types.h>


#include "dvb_frontend.h"
#include "m88rs2000.h"

struct m88rs2000_state {
	struct i2c_adapter *i2c;
	const struct m88rs2000_config *config;
	struct dvb_frontend frontend;
	u8 no_lock_count;
	u32 tuner_frequency;
	u32 symbol_rate;
	fe_code_rate_t fec_inner;
	u8 tuner_level;
	int errmode;
};

static int m88rs2000_debug;

module_param_named(debug, m88rs2000_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debugging level (1=info (or-able)).");

#define dprintk(level, args...) do { \
	if (level & m88rs2000_debug) \
		printk(KERN_DEBUG "m88rs2000-fe: " args); \
} while (0)

#define deb_info(args...)  dprintk(0x01, args)
#define info(format, arg...) \
	printk(KERN_INFO "m88rs2000-fe: " format "\n" , ## arg)

static int m88rs2000_writereg(struct m88rs2000_state *state, u8 tuner,
	u8 reg, u8 data)
{
	int ret;
	u8 addr = (tuner == 0) ? state->config->tuner_addr :
		state->config->demod_addr;
	u8 buf[] = { reg, data };
	struct i2c_msg msg = {
		.addr = addr,
		.flags = 0,
		.buf = buf,
		.len = 2
	};

	ret = i2c_transfer(state->i2c, &msg, 1);

	if (ret != 1)
		deb_info("%s: writereg error (reg == 0x%02x, val == 0x%02x, "
			"ret == %i)\n", __func__, reg, data, ret);

	return (ret != 1) ? -EREMOTEIO : 0;
}

static int m88rs2000_demod_write(struct m88rs2000_state *state, u8 reg, u8 data)
{
	return m88rs2000_writereg(state, 1, reg, data);
}

static int m88rs2000_tuner_write(struct m88rs2000_state *state, u8 reg, u8 data)
{
	m88rs2000_demod_write(state, 0x81, 0x84);
	udelay(10);
	return m88rs2000_writereg(state, 0, reg, data);

}

static int m88rs2000_write(struct dvb_frontend *fe, const u8 buf[], int len)
{
	struct m88rs2000_state *state = fe->demodulator_priv;

	if (len != 2)
		return -EINVAL;

	return m88rs2000_writereg(state, 1, buf[0], buf[1]);
}

static u8 m88rs2000_readreg(struct m88rs2000_state *state, u8 tuner, u8 reg)
{
	int ret;
	u8 b0[] = { reg };
	u8 b1[] = { 0 };
	u8 addr = (tuner == 0) ? state->config->tuner_addr :
		state->config->demod_addr;
	struct i2c_msg msg[] = {
		{
			.addr = addr,
			.flags = 0,
			.buf = b0,
			.len = 1
		}, {
			.addr = addr,
			.flags = I2C_M_RD,
			.buf = b1,
			.len = 1
		}
	};

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2)
		deb_info("%s: readreg error (reg == 0x%02x, ret == %i)\n",
				__func__, reg, ret);

	return b1[0];
}

static u8 m88rs2000_demod_read(struct m88rs2000_state *state, u8 reg)
{
	return m88rs2000_readreg(state, 1, reg);
}

static u8 m88rs2000_tuner_read(struct m88rs2000_state *state, u8 reg)
{
	m88rs2000_demod_write(state, 0x81, 0x85);
	udelay(10);
	return m88rs2000_readreg(state, 0, reg);
}

static int m88rs2000_set_symbolrate(struct dvb_frontend *fe, u32 srate)
{
	struct m88rs2000_state *state = fe->demodulator_priv;
	int ret;
	u32 temp;
	u8 b[3];

	if ((srate < 1000000) || (srate > 45000000))
		return -EINVAL;

	temp = srate / 1000;
	temp *= 11831;
	temp /= 68;
	temp -= 3;

	b[0] = (u8) (temp >> 16) & 0xff;
	b[1] = (u8) (temp >> 8) & 0xff;
	b[2] = (u8) temp & 0xff;
	ret = m88rs2000_demod_write(state, 0x93, b[2]);
	ret |= m88rs2000_demod_write(state, 0x94, b[1]);
	ret |= m88rs2000_demod_write(state, 0x95, b[0]);

	deb_info("m88rs2000: m88rs2000_set_symbolrate\n");
	return ret;
}

static int m88rs2000_send_diseqc_msg(struct dvb_frontend *fe,
				    struct dvb_diseqc_master_cmd *m)
{
	struct m88rs2000_state *state = fe->demodulator_priv;

	int i;
	u8 reg;
	deb_info("%s\n", __func__);
	m88rs2000_demod_write(state, 0x9a, 0x30);
	reg = m88rs2000_demod_read(state, 0xb2);
	reg &= 0x3f;
	m88rs2000_demod_write(state, 0xb2, reg);
	for (i = 0; i <  m->msg_len; i++)
		m88rs2000_demod_write(state, 0xb3 + i, m->msg[i]);

	reg = m88rs2000_demod_read(state, 0xb1);
	reg &= 0x87;
	reg |= ((m->msg_len - 1) << 3) | 0x07;
	reg &= 0x7f;
	m88rs2000_demod_write(state, 0xb1, reg);

	for (i = 0; i < 15; i++) {
		if ((m88rs2000_demod_read(state, 0xb1) & 0x40) == 0x0)
			break;
		msleep(20);
	}

	reg = m88rs2000_demod_read(state, 0xb1);
	if ((reg & 0x40) > 0x0) {
		reg &= 0x7f;
		reg |= 0x40;
		m88rs2000_demod_write(state, 0xb1, reg);
	}

	reg = m88rs2000_demod_read(state, 0xb2);
	reg &= 0x3f;
	reg |= 0x80;
	m88rs2000_demod_write(state, 0xb2, reg);
	m88rs2000_demod_write(state, 0x9a, 0xb0);


	return 0;
}

static int m88rs2000_send_diseqc_burst(struct dvb_frontend *fe,
						fe_sec_mini_cmd_t burst)
{
	struct m88rs2000_state *state = fe->demodulator_priv;
	u8 reg0, reg1;
	deb_info("%s\n", __func__);
	m88rs2000_demod_write(state, 0x9a, 0x30);
	msleep(50);
	reg0 = m88rs2000_demod_read(state, 0xb1);
	reg1 = m88rs2000_demod_read(state, 0xb2);
	/* TODO complete this section */
	m88rs2000_demod_write(state, 0xb2, reg1);
	m88rs2000_demod_write(state, 0xb1, reg0);
	m88rs2000_demod_write(state, 0x9a, 0xb0);

	return 0;
}

static int m88rs2000_set_tone(struct dvb_frontend *fe, fe_sec_tone_mode_t tone)
{
	struct m88rs2000_state *state = fe->demodulator_priv;
	u8 reg0, reg1;
	m88rs2000_demod_write(state, 0x9a, 0x30);
	reg0 = m88rs2000_demod_read(state, 0xb1);
	reg1 = m88rs2000_demod_read(state, 0xb2);

	reg1 &= 0x3f;

	switch (tone) {
	case SEC_TONE_ON:
		reg0 |= 0x4;
		reg0 &= 0xbc;
		break;
	case SEC_TONE_OFF:
		reg1 |= 0x80;
		break;
	default:
		break;
	}
	m88rs2000_demod_write(state, 0xb2, reg1);
	m88rs2000_demod_write(state, 0xb1, reg0);
	m88rs2000_demod_write(state, 0x9a, 0xb0);
	return 0;
}

struct inittab {
	u8 cmd;
	u8 reg;
	u8 val;
};

struct inittab m88rs2000_setup[] = {
	{DEMOD_WRITE, 0x9a, 0x30},
	{DEMOD_WRITE, 0x00, 0x01},
	{WRITE_DELAY, 0x19, 0x00},
	{DEMOD_WRITE, 0x00, 0x00},
	{DEMOD_WRITE, 0x9a, 0xb0},
	{DEMOD_WRITE, 0x81, 0xc1},
	{TUNER_WRITE, 0x42, 0x73},
	{TUNER_WRITE, 0x05, 0x07},
	{TUNER_WRITE, 0x20, 0x27},
	{TUNER_WRITE, 0x07, 0x02},
	{TUNER_WRITE, 0x11, 0xff},
	{TUNER_WRITE, 0x60, 0xf9},
	{TUNER_WRITE, 0x08, 0x01},
	{TUNER_WRITE, 0x00, 0x41},
	{DEMOD_WRITE, 0x81, 0x81},
	{DEMOD_WRITE, 0x86, 0xc6},
	{DEMOD_WRITE, 0x9a, 0x30},
	{DEMOD_WRITE, 0xf0, 0x22},
	{DEMOD_WRITE, 0xf1, 0xbf},
	{DEMOD_WRITE, 0xb0, 0x45},
	{DEMOD_WRITE, 0xb2, 0x01}, /* set voltage pin always set 1*/
	{DEMOD_WRITE, 0x9a, 0xb0},
	{0xff, 0xaa, 0xff}
};

struct inittab m88rs2000_shutdown[] = {
	{DEMOD_WRITE, 0x9a, 0x30},
	{DEMOD_WRITE, 0xb0, 0x00},
	{DEMOD_WRITE, 0xf1, 0x89},
	{DEMOD_WRITE, 0x00, 0x01},
	{DEMOD_WRITE, 0x9a, 0xb0},
	{TUNER_WRITE, 0x00, 0x40},
	{DEMOD_WRITE, 0x81, 0x81},
	{0xff, 0xaa, 0xff}
};

struct inittab tuner_reset[] = {
	{TUNER_WRITE, 0x42, 0x73},
	{TUNER_WRITE, 0x05, 0x07},
	{TUNER_WRITE, 0x20, 0x27},
	{TUNER_WRITE, 0x07, 0x02},
	{TUNER_WRITE, 0x11, 0xff},
	{TUNER_WRITE, 0x60, 0xf9},
	{TUNER_WRITE, 0x08, 0x01},
	{TUNER_WRITE, 0x00, 0x41},
	{0xff, 0xaa, 0xff}
};

struct inittab fe_reset[] = {
	{DEMOD_WRITE, 0x00, 0x01},
	{DEMOD_WRITE, 0xf1, 0xbf},
	{DEMOD_WRITE, 0x00, 0x01},
	{DEMOD_WRITE, 0x20, 0x81},
	{DEMOD_WRITE, 0x21, 0x80},
	{DEMOD_WRITE, 0x10, 0x33},
	{DEMOD_WRITE, 0x11, 0x44},
	{DEMOD_WRITE, 0x12, 0x07},
	{DEMOD_WRITE, 0x18, 0x20},
	{DEMOD_WRITE, 0x28, 0x04},
	{DEMOD_WRITE, 0x29, 0x8e},
	{DEMOD_WRITE, 0x3b, 0xff},
	{DEMOD_WRITE, 0x32, 0x10},
	{DEMOD_WRITE, 0x33, 0x02},
	{DEMOD_WRITE, 0x34, 0x30},
	{DEMOD_WRITE, 0x35, 0xff},
	{DEMOD_WRITE, 0x38, 0x50},
	{DEMOD_WRITE, 0x39, 0x68},
	{DEMOD_WRITE, 0x3c, 0x7f},
	{DEMOD_WRITE, 0x3d, 0x0f},
	{DEMOD_WRITE, 0x45, 0x20},
	{DEMOD_WRITE, 0x46, 0x24},
	{DEMOD_WRITE, 0x47, 0x7c},
	{DEMOD_WRITE, 0x48, 0x16},
	{DEMOD_WRITE, 0x49, 0x04},
	{DEMOD_WRITE, 0x4a, 0x01},
	{DEMOD_WRITE, 0x4b, 0x78},
	{DEMOD_WRITE, 0X4d, 0xd2},
	{DEMOD_WRITE, 0x4e, 0x6d},
	{DEMOD_WRITE, 0x50, 0x30},
	{DEMOD_WRITE, 0x51, 0x30},
	{DEMOD_WRITE, 0x54, 0x7b},
	{DEMOD_WRITE, 0x56, 0x09},
	{DEMOD_WRITE, 0x58, 0x59},
	{DEMOD_WRITE, 0x59, 0x37},
	{DEMOD_WRITE, 0x63, 0xfa},
	{0xff, 0xaa, 0xff}
};

struct inittab fe_trigger[] = {
	{DEMOD_WRITE, 0x97, 0x04},
	{DEMOD_WRITE, 0x99, 0x77},
	{DEMOD_WRITE, 0x9b, 0x64},
	{DEMOD_WRITE, 0x9e, 0x00},
	{DEMOD_WRITE, 0x9f, 0xf8},
	{DEMOD_WRITE, 0xa0, 0x20},
	{DEMOD_WRITE, 0xa1, 0xe0},
	{DEMOD_WRITE, 0xa3, 0x38},
	{DEMOD_WRITE, 0x98, 0xff},
	{DEMOD_WRITE, 0xc0, 0x0f},
	{DEMOD_WRITE, 0x89, 0x01},
	{DEMOD_WRITE, 0x00, 0x00},
	{WRITE_DELAY, 0x0a, 0x00},
	{DEMOD_WRITE, 0x00, 0x01},
	{DEMOD_WRITE, 0x00, 0x00},
	{DEMOD_WRITE, 0x9a, 0xb0},
	{0xff, 0xaa, 0xff}
};

static int m88rs2000_tab_set(struct m88rs2000_state *state,
		struct inittab *tab)
{
	int ret = 0;
	u8 i;
	if (tab == NULL)
		return -EINVAL;

	for (i = 0; i < 255; i++) {
		switch (tab[i].cmd) {
		case 0x01:
			ret = m88rs2000_demod_write(state, tab[i].reg,
				tab[i].val);
			break;
		case 0x02:
			ret = m88rs2000_tuner_write(state, tab[i].reg,
				tab[i].val);
			break;
		case 0x10:
			if (tab[i].reg > 0)
				mdelay(tab[i].reg);
			break;
		case 0xff:
			if (tab[i].reg == 0xaa && tab[i].val == 0xff)
				return 0;
		case 0x00:
			break;
		default:
			return -EINVAL;
		}
		if (ret < 0)
			return -ENODEV;
	}
	return 0;
}

static int m88rs2000_set_voltage(struct dvb_frontend *fe, fe_sec_voltage_t volt)
{
	struct m88rs2000_state *state = fe->demodulator_priv;
	u8 data;

	data = m88rs2000_demod_read(state, 0xb2);
	data |= 0x03; /* bit0 V/H, bit1 off/on */

	switch (volt) {
	case SEC_VOLTAGE_18:
		data &= ~0x03;
		break;
	case SEC_VOLTAGE_13:
		data &= ~0x03;
		data |= 0x01;
		break;
	case SEC_VOLTAGE_OFF:
		break;
	}

	m88rs2000_demod_write(state, 0xb2, data);

	return 0;
}

static int m88rs2000_startup(struct m88rs2000_state *state)
{
	int ret = 0;
	u8 reg;

	reg = m88rs2000_tuner_read(state, 0x00);
	if ((reg & 0x40) == 0)
		ret = -ENODEV;

	return ret;
}

static int m88rs2000_init(struct dvb_frontend *fe)
{
	struct m88rs2000_state *state = fe->demodulator_priv;
	int ret;

	deb_info("m88rs2000: init chip\n");
	/* Setup frontend from shutdown/cold */
	ret = m88rs2000_tab_set(state, m88rs2000_setup);

	return ret;
}

static int m88rs2000_sleep(struct dvb_frontend *fe)
{
	struct m88rs2000_state *state = fe->demodulator_priv;
	int ret;
	/* Shutdown the frondend */
	ret = m88rs2000_tab_set(state, m88rs2000_shutdown);
	return ret;
}

static int m88rs2000_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct m88rs2000_state *state = fe->demodulator_priv;
	u8 reg = m88rs2000_demod_read(state, 0x8c);

	*status = 0;

	if ((reg & 0x7) == 0x7) {
		*status = FE_HAS_CARRIER | FE_HAS_SIGNAL | FE_HAS_VITERBI
			| FE_HAS_LOCK;
		if (state->config->set_ts_params)
			state->config->set_ts_params(fe, CALL_IS_READ);
	}
	return 0;
}

/* Extact code for these unknown but lmedm04 driver uses interupt callbacks */

static int m88rs2000_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	deb_info("m88rs2000_read_ber %d\n", *ber);
	*ber = 0;
	return 0;
}

static int m88rs2000_read_signal_strength(struct dvb_frontend *fe,
	u16 *strength)
{
	*strength = 0;
	return 0;
}

static int m88rs2000_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	deb_info("m88rs2000_read_snr %d\n", *snr);
	*snr = 0;
	return 0;
}

static int m88rs2000_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	deb_info("m88rs2000_read_ber %d\n", *ucblocks);
	*ucblocks = 0;
	return 0;
}

static int m88rs2000_tuner_gate_ctrl(struct m88rs2000_state *state, u8 offset)
{
	int ret;
	ret = m88rs2000_tuner_write(state, 0x51, 0x1f - offset);
	ret |= m88rs2000_tuner_write(state, 0x51, 0x1f);
	ret |= m88rs2000_tuner_write(state, 0x50, offset);
	ret |= m88rs2000_tuner_write(state, 0x50, 0x00);
	msleep(20);
	return ret;
}

static int m88rs2000_set_tuner_rf(struct dvb_frontend *fe)
{
	struct m88rs2000_state *state = fe->demodulator_priv;
	int reg;
	reg = m88rs2000_tuner_read(state, 0x3d);
	reg &= 0x7f;
	if (reg < 0x16)
		reg = 0xa1;
	else if (reg == 0x16)
		reg = 0x99;
	else
		reg = 0xf9;

	m88rs2000_tuner_write(state, 0x60, reg);
	reg = m88rs2000_tuner_gate_ctrl(state, 0x08);

	if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	return reg;
}

static int m88rs2000_set_tuner(struct dvb_frontend *fe, u16 *offset)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct m88rs2000_state *state = fe->demodulator_priv;
	int ret;
	u32 frequency = c->frequency;
	s32 offset_khz;
	s32 tmp;
	u32 symbol_rate = (c->symbol_rate / 1000);
	u32 f3db, gdiv28;
	u16 value, ndiv, lpf_coeff;
	u8 lpf_mxdiv, mlpf_max, mlpf_min, nlpf;
	u8 lo = 0x01, div4 = 0x0;

	/* Reset Tuner */
	ret = m88rs2000_tab_set(state, tuner_reset);

	/* Calculate frequency divider */
	if (frequency < 1060000) {
		lo |= 0x10;
		div4 = 0x1;
		ndiv = (frequency * 14 * 4) / FE_CRYSTAL_KHZ;
	} else
		ndiv = (frequency * 14 * 2) / FE_CRYSTAL_KHZ;
	ndiv = ndiv + ndiv % 2;
	ndiv = ndiv - 1024;

	ret = m88rs2000_tuner_write(state, 0x10, 0x80 | lo);

	/* Set frequency divider */
	ret |= m88rs2000_tuner_write(state, 0x01, (ndiv >> 8) & 0xf);
	ret |= m88rs2000_tuner_write(state, 0x02, ndiv & 0xff);

	ret |= m88rs2000_tuner_write(state, 0x03, 0x06);
	ret |= m88rs2000_tuner_gate_ctrl(state, 0x10);
	if (ret < 0)
		return -ENODEV;

	/* Tuner Frequency Range */
	ret = m88rs2000_tuner_write(state, 0x10, lo);

	ret |= m88rs2000_tuner_gate_ctrl(state, 0x08);

	/* Tuner RF */
	ret |= m88rs2000_set_tuner_rf(fe);

	gdiv28 = (FE_CRYSTAL_KHZ / 1000 * 1694 + 500) / 1000;
	ret |= m88rs2000_tuner_write(state, 0x04, gdiv28 & 0xff);
	ret |= m88rs2000_tuner_gate_ctrl(state, 0x04);
	if (ret < 0)
		return -ENODEV;

	value = m88rs2000_tuner_read(state, 0x26);

	f3db = (symbol_rate * 135) / 200 + 2000;
	f3db += FREQ_OFFSET_LOW_SYM_RATE;
	if (f3db < 7000)
		f3db = 7000;
	if (f3db > 40000)
		f3db = 40000;

	gdiv28 = gdiv28 * 207 / (value * 2 + 151);
	mlpf_max = gdiv28 * 135 / 100;
	mlpf_min = gdiv28 * 78 / 100;
	if (mlpf_max > 63)
		mlpf_max = 63;

	lpf_coeff = 2766;

	nlpf = (f3db * gdiv28 * 2 / lpf_coeff /
		(FE_CRYSTAL_KHZ / 1000)  + 1) / 2;
	if (nlpf > 23)
		nlpf = 23;
	if (nlpf < 1)
		nlpf = 1;

	lpf_mxdiv = (nlpf * (FE_CRYSTAL_KHZ / 1000)
		* lpf_coeff * 2  / f3db + 1) / 2;

	if (lpf_mxdiv < mlpf_min) {
		nlpf++;
		lpf_mxdiv = (nlpf * (FE_CRYSTAL_KHZ / 1000)
			* lpf_coeff * 2  / f3db + 1) / 2;
	}

	if (lpf_mxdiv > mlpf_max)
		lpf_mxdiv = mlpf_max;

	ret = m88rs2000_tuner_write(state, 0x04, lpf_mxdiv);
	ret |= m88rs2000_tuner_write(state, 0x06, nlpf);

	ret |= m88rs2000_tuner_gate_ctrl(state, 0x04);

	ret |= m88rs2000_tuner_gate_ctrl(state, 0x01);

	msleep(80);
	/* calculate offset assuming 96000kHz*/
	offset_khz = (ndiv - ndiv % 2 + 1024) * FE_CRYSTAL_KHZ
		/ 14 / (div4 + 1) / 2;

	offset_khz -= frequency;

	tmp = offset_khz;
	tmp *= 65536;

	tmp = (2 * tmp + 96000) / (2 * 96000);
	if (tmp < 0)
		tmp += 65536;

	*offset = tmp & 0xffff;

	if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);

	return (ret < 0) ? -EINVAL : 0;
}

static int m88rs2000_set_fec(struct m88rs2000_state *state,
		fe_code_rate_t fec)
{
	u16 fec_set;
	switch (fec) {
	/* This is not confirmed kept for reference */
/*	case FEC_1_2:
		fec_set = 0x88;
		break;
	case FEC_2_3:
		fec_set = 0x68;
		break;
	case FEC_3_4:
		fec_set = 0x48;
		break;
	case FEC_5_6:
		fec_set = 0x28;
		break;
	case FEC_7_8:
		fec_set = 0x18;
		break; */
	case FEC_AUTO:
	default:
		fec_set = 0x08;
	}
	m88rs2000_demod_write(state, 0x76, fec_set);

	return 0;
}


static fe_code_rate_t m88rs2000_get_fec(struct m88rs2000_state *state)
{
	u8 reg;
	m88rs2000_demod_write(state, 0x9a, 0x30);
	reg = m88rs2000_demod_read(state, 0x76);
	m88rs2000_demod_write(state, 0x9a, 0xb0);

	switch (reg) {
	case 0x88:
		return FEC_1_2;
	case 0x68:
		return FEC_2_3;
	case 0x48:
		return FEC_3_4;
	case 0x28:
		return FEC_5_6;
	case 0x18:
		return FEC_7_8;
	case 0x08:
	default:
		break;
	}

	return FEC_AUTO;
}

static int m88rs2000_set_frontend(struct dvb_frontend *fe)
{
	struct m88rs2000_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	fe_status_t status;
	int i, ret;
	u16 offset = 0;
	u8 reg;

	state->no_lock_count = 0;

	if (c->delivery_system != SYS_DVBS) {
			deb_info("%s: unsupported delivery "
				"system selected (%d)\n",
				__func__, c->delivery_system);
			return -EOPNOTSUPP;
	}

	/* Set Tuner */
	ret = m88rs2000_set_tuner(fe, &offset);
	if (ret < 0)
		return -ENODEV;

	ret = m88rs2000_demod_write(state, 0x9a, 0x30);
	/* Unknown usually 0xc6 sometimes 0xc1 */
	reg = m88rs2000_demod_read(state, 0x86);
	ret |= m88rs2000_demod_write(state, 0x86, reg);
	/* Offset lower nibble always 0 */
	ret |= m88rs2000_demod_write(state, 0x9c, (offset >> 8));
	ret |= m88rs2000_demod_write(state, 0x9d, offset & 0xf0);


	/* Reset Demod */
	ret = m88rs2000_tab_set(state, fe_reset);
	if (ret < 0)
		return -ENODEV;

	/* Unknown */
	reg = m88rs2000_demod_read(state, 0x70);
	ret = m88rs2000_demod_write(state, 0x70, reg);

	/* Set FEC */
	ret |= m88rs2000_set_fec(state, c->fec_inner);
	ret |= m88rs2000_demod_write(state, 0x85, 0x1);
	ret |= m88rs2000_demod_write(state, 0x8a, 0xbf);
	ret |= m88rs2000_demod_write(state, 0x8d, 0x1e);
	ret |= m88rs2000_demod_write(state, 0x90, 0xf1);
	ret |= m88rs2000_demod_write(state, 0x91, 0x08);

	if (ret < 0)
		return -ENODEV;

	/* Set Symbol Rate */
	ret = m88rs2000_set_symbolrate(fe, c->symbol_rate);
	if (ret < 0)
		return -ENODEV;

	/* Set up Demod */
	ret = m88rs2000_tab_set(state, fe_trigger);
	if (ret < 0)
		return -ENODEV;

	for (i = 0; i < 25; i++) {
		u8 reg = m88rs2000_demod_read(state, 0x8c);
		if ((reg & 0x7) == 0x7) {
			status = FE_HAS_LOCK;
			break;
		}
		state->no_lock_count++;
		if (state->no_lock_count > 15) {
			reg = m88rs2000_demod_read(state, 0x70);
			reg ^= 0x4;
			m88rs2000_demod_write(state, 0x70, reg);
			state->no_lock_count = 0;
		}
		if (state->no_lock_count == 20)
			m88rs2000_set_tuner_rf(fe);
		msleep(20);
	}

	if (status & FE_HAS_LOCK) {
		state->fec_inner = m88rs2000_get_fec(state);
		/* Uknown suspect SNR level */
		reg = m88rs2000_demod_read(state, 0x65);
	}

	state->tuner_frequency = c->frequency;
	state->symbol_rate = c->symbol_rate;
	return 0;
}

static int m88rs2000_get_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct m88rs2000_state *state = fe->demodulator_priv;
	c->fec_inner = state->fec_inner;
	c->frequency = state->tuner_frequency;
	c->symbol_rate = state->symbol_rate;
	return 0;
}

static int m88rs2000_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct m88rs2000_state *state = fe->demodulator_priv;

	if (enable)
		m88rs2000_demod_write(state, 0x81, 0x84);
	else
		m88rs2000_demod_write(state, 0x81, 0x81);
	udelay(10);
	return 0;
}

static void m88rs2000_release(struct dvb_frontend *fe)
{
	struct m88rs2000_state *state = fe->demodulator_priv;
	kfree(state);
}

static struct dvb_frontend_ops m88rs2000_ops = {
	.delsys = { SYS_DVBS },
	.info = {
		.name			= "M88RS2000 DVB-S",
		.frequency_min		= 950000,
		.frequency_max		= 2150000,
		.frequency_stepsize	= 1000,	 /* kHz for QPSK frontends */
		.frequency_tolerance	= 5000,
		.symbol_rate_min	= 1000000,
		.symbol_rate_max	= 45000000,
		.symbol_rate_tolerance	= 500,	/* ppm */
		.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		      FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 |
		      FE_CAN_QPSK |
		      FE_CAN_FEC_AUTO
	},

	.release = m88rs2000_release,
	.init = m88rs2000_init,
	.sleep = m88rs2000_sleep,
	.write = m88rs2000_write,
	.i2c_gate_ctrl = m88rs2000_i2c_gate_ctrl,
	.read_status = m88rs2000_read_status,
	.read_ber = m88rs2000_read_ber,
	.read_signal_strength = m88rs2000_read_signal_strength,
	.read_snr = m88rs2000_read_snr,
	.read_ucblocks = m88rs2000_read_ucblocks,
	.diseqc_send_master_cmd = m88rs2000_send_diseqc_msg,
	.diseqc_send_burst = m88rs2000_send_diseqc_burst,
	.set_tone = m88rs2000_set_tone,
	.set_voltage = m88rs2000_set_voltage,

	.set_frontend = m88rs2000_set_frontend,
	.get_frontend = m88rs2000_get_frontend,
};

struct dvb_frontend *m88rs2000_attach(const struct m88rs2000_config *config,
				    struct i2c_adapter *i2c)
{
	struct m88rs2000_state *state = NULL;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct m88rs2000_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* setup the state */
	state->config = config;
	state->i2c = i2c;
	state->tuner_frequency = 0;
	state->symbol_rate = 0;
	state->fec_inner = 0;

	if (m88rs2000_startup(state) < 0)
		goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &m88rs2000_ops,
			sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error:
	kfree(state);

	return NULL;
}
EXPORT_SYMBOL(m88rs2000_attach);

MODULE_DESCRIPTION("M88RS2000 DVB-S Demodulator driver");
MODULE_AUTHOR("Malcolm Priestley tvboxspy@gmail.com");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.13");


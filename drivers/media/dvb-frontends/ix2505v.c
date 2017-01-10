/**
 * Driver for Sharp IX2505V (marked B0017) DVB-S silicon tuner
 *
 * Copyright (C) 2010 Malcolm Priestley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/module.h>
#include <linux/dvb/frontend.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "ix2505v.h"

static int ix2505v_debug;
#define dprintk(level, args...) do { \
	if (ix2505v_debug & level) \
		printk(KERN_DEBUG "ix2505v: " args); \
} while (0)

#define deb_info(args...)  dprintk(0x01, args)
#define deb_i2c(args...)  dprintk(0x02, args)

struct ix2505v_state {
	struct i2c_adapter *i2c;
	const struct ix2505v_config *config;
	u32 frequency;
};

/**
 *  Data read format of the Sharp IX2505V B0017
 *
 *  byte1:   1   |   1   |   0   |   0   |   0   |  MA1  |  MA0  |  1
 *  byte2:  POR  |   FL  |  RD2  |  RD1  |  RD0  |   X   |   X   |  X
 *
 *  byte1 = address
 *  byte2;
 *	POR = Power on Reset (VCC H=<2.2v L=>2.2v)
 *	FL  = Phase Lock (H=lock L=unlock)
 *	RD0-2 = Reserved internal operations
 *
 * Only POR can be used to check the tuner is present
 *
 * Caution: after byte2 the I2C reverts to write mode continuing to read
 *          may corrupt tuning data.
 *
 */

static int ix2505v_read_status_reg(struct ix2505v_state *state)
{
	u8 addr = state->config->tuner_address;
	u8 b2[] = {0};
	int ret;

	struct i2c_msg msg[1] = {
		{ .addr = addr, .flags = I2C_M_RD, .buf = b2, .len = 1 }
	};

	ret = i2c_transfer(state->i2c, msg, 1);
	deb_i2c("Read %s ", __func__);

	return (ret == 1) ? (int) b2[0] : -1;
}

static int ix2505v_write(struct ix2505v_state *state, u8 buf[], u8 count)
{
	struct i2c_msg msg[1] = {
		{ .addr = state->config->tuner_address, .flags = 0,
		  .buf = buf, .len = count },
	};

	int ret;

	ret = i2c_transfer(state->i2c, msg, 1);

	if (ret != 1) {
		deb_i2c("%s: i2c error, ret=%d\n", __func__, ret);
		return -EIO;
	}

	return 0;
}

static void ix2505v_release(struct dvb_frontend *fe)
{
	struct ix2505v_state *state = fe->tuner_priv;

	fe->tuner_priv = NULL;
	kfree(state);

}

/**
 *  Data write format of the Sharp IX2505V B0017
 *
 *  byte1:   1   |   1   |   0   |   0   |   0   | 0(MA1)| 0(MA0)|  0
 *  byte2:   0   |  BG1  |  BG2  |   N8  |   N7  |   N6  |  N5   |  N4
 *  byte3:   N3  |   N2  |   N1  |   A5  |   A4  |   A3  |   A2  |  A1
 *  byte4:   1   | 1(C1) | 1(C0) |  PD5  |  PD4  |   TM  | 0(RTS)| 1(REF)
 *  byte5:   BA2 |   BA1 |  BA0  |  PSC  |  PD3  |PD2/TS2|DIV/TS1|PD0/TS0
 *
 *  byte1 = address
 *
 *  Write order
 *  1) byte1 -> byte2 -> byte3 -> byte4 -> byte5
 *  2) byte1 -> byte4 -> byte5 -> byte2 -> byte3
 *  3) byte1 -> byte2 -> byte3 -> byte4
 *  4) byte1 -> byte4 -> byte5 -> byte2
 *  5) byte1 -> byte2 -> byte3
 *  6) byte1 -> byte4 -> byte5
 *  7) byte1 -> byte2
 *  8) byte1 -> byte4
 *
 *  Recommended Setup
 *  1 -> 8 -> 6
 */

static int ix2505v_set_params(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct ix2505v_state *state = fe->tuner_priv;
	u32 frequency = c->frequency;
	u32 b_w  = (c->symbol_rate * 27) / 32000;
	u32 div_factor, N , A, x;
	int ret = 0, len;
	u8 gain, cc, ref, psc, local_osc, lpf;
	u8 data[4] = {0};

	if ((frequency < fe->ops.info.frequency_min)
	||  (frequency > fe->ops.info.frequency_max))
		return -EINVAL;

	if (state->config->tuner_gain)
		gain = (state->config->tuner_gain < 4)
			? state->config->tuner_gain : 0;
	else
		gain = 0x0;

	if (state->config->tuner_chargepump)
		cc = state->config->tuner_chargepump;
	else
		cc = 0x3;

	ref = 8; /* REF =1 */
	psc = 32; /* PSC = 0 */

	div_factor = (frequency * ref) / 40; /* local osc = 4Mhz */
	x = div_factor / psc;
	N = x/100;
	A = ((x - (N * 100)) * psc) / 100;

	data[0] = ((gain & 0x3) << 5) | (N >> 3);
	data[1] = (N << 5) | (A & 0x1f);
	data[2] = 0x81 | ((cc & 0x3) << 5) ; /*PD5,PD4 & TM = 0|C1,C0|REF=1*/

	deb_info("Frq=%d x=%d N=%d A=%d\n", frequency, x, N, A);

	if (frequency <= 1065000)
		local_osc = (6 << 5) | 2;
	else if (frequency <= 1170000)
		local_osc = (7 << 5) | 2;
	else if (frequency <= 1300000)
		local_osc = (1 << 5);
	else if (frequency <= 1445000)
		local_osc = (2 << 5);
	else if (frequency <= 1607000)
		local_osc = (3 << 5);
	else if (frequency <= 1778000)
		local_osc = (4 << 5);
	else if (frequency <= 1942000)
		local_osc = (5 << 5);
	else		/*frequency up to 2150000*/
		local_osc = (6 << 5);

	data[3] = local_osc; /* all other bits set 0 */

	if (b_w <= 10000)
		lpf = 0xc;
	else if (b_w <= 12000)
		lpf = 0x2;
	else if (b_w <= 14000)
		lpf = 0xa;
	else if (b_w <= 16000)
		lpf = 0x6;
	else if (b_w <= 18000)
		lpf = 0xe;
	else if (b_w <= 20000)
		lpf = 0x1;
	else if (b_w <= 22000)
		lpf = 0x9;
	else if (b_w <= 24000)
		lpf = 0x5;
	else if (b_w <= 26000)
		lpf = 0xd;
	else if (b_w <= 28000)
		lpf = 0x3;
		else
		lpf = 0xb;

	deb_info("Osc=%x b_w=%x lpf=%x\n", local_osc, b_w, lpf);
	deb_info("Data 0=[%4phN]\n", data);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	len = sizeof(data);
	ret |= ix2505v_write(state, data, len);

	data[2] |= 0x4; /* set TM = 1 other bits same */

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	len = 1;
	ret |= ix2505v_write(state, &data[2], len); /* write byte 4 only */

	msleep(10);

	data[2] |= ((lpf >> 2) & 0x3) << 3; /* lpf */
	data[3] |= (lpf & 0x3) << 2;

	deb_info("Data 2=[%x%x]\n", data[2], data[3]);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	len = 2;
	ret |= ix2505v_write(state, &data[2], len); /* write byte 4 & 5 */

	if (state->config->min_delay_ms)
		msleep(state->config->min_delay_ms);

	state->frequency = frequency;

	return ret;
}

static int ix2505v_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct ix2505v_state *state = fe->tuner_priv;

	*frequency = state->frequency;

	return 0;
}

static const struct dvb_tuner_ops ix2505v_tuner_ops = {
	.info = {
		.name = "Sharp IX2505V (B0017)",
		.frequency_min = 950000,
		.frequency_max = 2175000
	},
	.release = ix2505v_release,
	.set_params = ix2505v_set_params,
	.get_frequency = ix2505v_get_frequency,
};

struct dvb_frontend *ix2505v_attach(struct dvb_frontend *fe,
				    const struct ix2505v_config *config,
				    struct i2c_adapter *i2c)
{
	struct ix2505v_state *state = NULL;
	int ret;

	if (NULL == config) {
		deb_i2c("%s: no config ", __func__);
		goto error;
	}

	state = kzalloc(sizeof(struct ix2505v_state), GFP_KERNEL);
	if (NULL == state)
		return NULL;

	state->config = config;
	state->i2c = i2c;

	if (state->config->tuner_write_only) {
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);

		ret = ix2505v_read_status_reg(state);

		if (ret & 0x80) {
			deb_i2c("%s: No IX2505V found\n", __func__);
			goto error;
		}

		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	fe->tuner_priv = state;

	memcpy(&fe->ops.tuner_ops, &ix2505v_tuner_ops,
		sizeof(struct dvb_tuner_ops));
	deb_i2c("%s: initialization (%s addr=0x%02x) ok\n",
		__func__, fe->ops.tuner_ops.info.name, config->tuner_address);

	return fe;

error:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(ix2505v_attach);

module_param_named(debug, ix2505v_debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");
MODULE_DESCRIPTION("DVB IX2505V tuner driver");
MODULE_AUTHOR("Malcolm Priestley");
MODULE_LICENSE("GPL");

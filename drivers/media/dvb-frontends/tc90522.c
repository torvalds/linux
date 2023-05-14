// SPDX-License-Identifier: GPL-2.0
/*
 * Toshiba TC90522 Demodulator
 *
 * Copyright (C) 2014 Akihiro Tsukada <tskd08@gmail.com>
 */

/*
 * NOTICE:
 * This driver is incomplete and lacks init/config of the chips,
 * as the necessary info is not disclosed.
 * It assumes that users of this driver (such as a PCI bridge of
 * DTV receiver cards) properly init and configure the chip
 * via I2C *before* calling this driver's init() function.
 *
 * Currently, PT3 driver is the only one that uses this driver,
 * and contains init/config code in its firmware.
 * Thus some part of the code might be dependent on PT3 specific config.
 */

#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/dvb/frontend.h>
#include <media/dvb_math.h>
#include "tc90522.h"

#define TC90522_I2C_THRU_REG 0xfe

#define TC90522_MODULE_IDX(addr) (((u8)(addr) & 0x02U) >> 1)

struct tc90522_state {
	struct tc90522_config cfg;
	struct dvb_frontend fe;
	struct i2c_client *i2c_client;
	struct i2c_adapter tuner_i2c;

	bool lna;
};

struct reg_val {
	u8 reg;
	u8 val;
};

static int
reg_write(struct tc90522_state *state, const struct reg_val *regs, int num)
{
	int i, ret;
	struct i2c_msg msg;

	ret = 0;
	msg.addr = state->i2c_client->addr;
	msg.flags = 0;
	msg.len = 2;
	for (i = 0; i < num; i++) {
		msg.buf = (u8 *)&regs[i];
		ret = i2c_transfer(state->i2c_client->adapter, &msg, 1);
		if (ret == 0)
			ret = -EIO;
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int reg_read(struct tc90522_state *state, u8 reg, u8 *val, u8 len)
{
	struct i2c_msg msgs[2] = {
		{
			.addr = state->i2c_client->addr,
			.flags = 0,
			.buf = &reg,
			.len = 1,
		},
		{
			.addr = state->i2c_client->addr,
			.flags = I2C_M_RD,
			.buf = val,
			.len = len,
		},
	};
	int ret;

	ret = i2c_transfer(state->i2c_client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret == ARRAY_SIZE(msgs))
		ret = 0;
	else if (ret >= 0)
		ret = -EIO;
	return ret;
}

static struct tc90522_state *cfg_to_state(struct tc90522_config *c)
{
	return container_of(c, struct tc90522_state, cfg);
}


static int tc90522s_set_tsid(struct dvb_frontend *fe)
{
	struct reg_val set_tsid[] = {
		{ 0x8f, 00 },
		{ 0x90, 00 }
	};

	set_tsid[0].val = (fe->dtv_property_cache.stream_id & 0xff00) >> 8;
	set_tsid[1].val = fe->dtv_property_cache.stream_id & 0xff;
	return reg_write(fe->demodulator_priv, set_tsid, ARRAY_SIZE(set_tsid));
}

static int tc90522t_set_layers(struct dvb_frontend *fe)
{
	struct reg_val rv;
	u8 laysel;

	laysel = ~fe->dtv_property_cache.isdbt_layer_enabled & 0x07;
	laysel = (laysel & 0x01) << 2 | (laysel & 0x02) | (laysel & 0x04) >> 2;
	rv.reg = 0x71;
	rv.val = laysel;
	return reg_write(fe->demodulator_priv, &rv, 1);
}

/* frontend ops */

static int tc90522s_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct tc90522_state *state;
	int ret;
	u8 reg;

	state = fe->demodulator_priv;
	ret = reg_read(state, 0xc3, &reg, 1);
	if (ret < 0)
		return ret;

	*status = 0;
	if (reg & 0x80) /* input level under min ? */
		return 0;
	*status |= FE_HAS_SIGNAL;

	if (reg & 0x60) /* carrier? */
		return 0;
	*status |= FE_HAS_CARRIER | FE_HAS_VITERBI | FE_HAS_SYNC;

	if (reg & 0x10)
		return 0;
	if (reg_read(state, 0xc5, &reg, 1) < 0 || !(reg & 0x03))
		return 0;
	*status |= FE_HAS_LOCK;
	return 0;
}

static int tc90522t_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct tc90522_state *state;
	int ret;
	u8 reg;

	state = fe->demodulator_priv;
	ret = reg_read(state, 0x96, &reg, 1);
	if (ret < 0)
		return ret;

	*status = 0;
	if (reg & 0xe0) {
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_VITERBI
				| FE_HAS_SYNC | FE_HAS_LOCK;
		return 0;
	}

	ret = reg_read(state, 0x80, &reg, 1);
	if (ret < 0)
		return ret;

	if (reg & 0xf0)
		return 0;
	*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER;

	if (reg & 0x0c)
		return 0;
	*status |= FE_HAS_SYNC | FE_HAS_VITERBI;

	if (reg & 0x02)
		return 0;
	*status |= FE_HAS_LOCK;
	return 0;
}

static const enum fe_code_rate fec_conv_sat[] = {
	FEC_NONE, /* unused */
	FEC_1_2, /* for BPSK */
	FEC_1_2, FEC_2_3, FEC_3_4, FEC_5_6, FEC_7_8, /* for QPSK */
	FEC_2_3, /* for 8PSK. (trellis code) */
};

static int tc90522s_get_frontend(struct dvb_frontend *fe,
				 struct dtv_frontend_properties *c)
{
	struct tc90522_state *state;
	struct dtv_fe_stats *stats;
	int ret, i;
	int layers;
	u8 val[10];
	u32 cndat;

	state = fe->demodulator_priv;
	c->delivery_system = SYS_ISDBS;
	c->symbol_rate = 28860000;

	layers = 0;
	ret = reg_read(state, 0xe6, val, 5);
	if (ret == 0) {
		u8 v;

		c->stream_id = val[0] << 8 | val[1];

		/* high/single layer */
		v = (val[2] & 0x70) >> 4;
		c->modulation = (v == 7) ? PSK_8 : QPSK;
		c->fec_inner = fec_conv_sat[v];
		c->layer[0].fec = c->fec_inner;
		c->layer[0].modulation = c->modulation;
		c->layer[0].segment_count = val[3] & 0x3f; /* slots */

		/* low layer */
		v = (val[2] & 0x07);
		c->layer[1].fec = fec_conv_sat[v];
		if (v == 0)  /* no low layer */
			c->layer[1].segment_count = 0;
		else
			c->layer[1].segment_count = val[4] & 0x3f; /* slots */
		/*
		 * actually, BPSK if v==1, but not defined in
		 * enum fe_modulation
		 */
		c->layer[1].modulation = QPSK;
		layers = (v > 0) ? 2 : 1;
	}

	/* statistics */

	stats = &c->strength;
	stats->len = 0;
	/* let the connected tuner set RSSI property cache */
	if (fe->ops.tuner_ops.get_rf_strength) {
		u16 dummy;

		fe->ops.tuner_ops.get_rf_strength(fe, &dummy);
	}

	stats = &c->cnr;
	stats->len = 1;
	stats->stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	cndat = 0;
	ret = reg_read(state, 0xbc, val, 2);
	if (ret == 0)
		cndat = val[0] << 8 | val[1];
	if (cndat >= 3000) {
		u32 p, p4;
		s64 cn;

		cndat -= 3000;  /* cndat: 4.12 fixed point float */
		/*
		 * cnr[mdB] = -1634.6 * P^5 + 14341 * P^4 - 50259 * P^3
		 *                 + 88977 * P^2 - 89565 * P + 58857
		 *  (P = sqrt(cndat) / 64)
		 */
		/* p := sqrt(cndat) << 8 = P << 14, 2.14 fixed  point float */
		/* cn = cnr << 3 */
		p = int_sqrt(cndat << 16);
		p4 = cndat * cndat;
		cn = div64_s64(-16346LL * p4 * p, 10) >> 35;
		cn += (14341LL * p4) >> 21;
		cn -= (50259LL * cndat * p) >> 23;
		cn += (88977LL * cndat) >> 9;
		cn -= (89565LL * p) >> 11;
		cn += 58857  << 3;
		stats->stat[0].svalue = cn >> 3;
		stats->stat[0].scale = FE_SCALE_DECIBEL;
	}

	/* per-layer post viterbi BER (or PER? config dependent?) */
	stats = &c->post_bit_error;
	memset(stats, 0, sizeof(*stats));
	stats->len = layers;
	ret = reg_read(state, 0xeb, val, 10);
	if (ret < 0)
		for (i = 0; i < layers; i++)
			stats->stat[i].scale = FE_SCALE_NOT_AVAILABLE;
	else {
		for (i = 0; i < layers; i++) {
			stats->stat[i].scale = FE_SCALE_COUNTER;
			stats->stat[i].uvalue = val[i * 5] << 16
				| val[i * 5 + 1] << 8 | val[i * 5 + 2];
		}
	}
	stats = &c->post_bit_count;
	memset(stats, 0, sizeof(*stats));
	stats->len = layers;
	if (ret < 0)
		for (i = 0; i < layers; i++)
			stats->stat[i].scale = FE_SCALE_NOT_AVAILABLE;
	else {
		for (i = 0; i < layers; i++) {
			stats->stat[i].scale = FE_SCALE_COUNTER;
			stats->stat[i].uvalue =
				val[i * 5 + 3] << 8 | val[i * 5 + 4];
			stats->stat[i].uvalue *= 204 * 8;
		}
	}

	return 0;
}


static const enum fe_transmit_mode tm_conv[] = {
	TRANSMISSION_MODE_2K,
	TRANSMISSION_MODE_4K,
	TRANSMISSION_MODE_8K,
	0
};

static const enum fe_code_rate fec_conv_ter[] = {
	FEC_1_2, FEC_2_3, FEC_3_4, FEC_5_6, FEC_7_8, 0, 0, 0
};

static const enum fe_modulation mod_conv[] = {
	DQPSK, QPSK, QAM_16, QAM_64, 0, 0, 0, 0
};

static int tc90522t_get_frontend(struct dvb_frontend *fe,
				 struct dtv_frontend_properties *c)
{
	struct tc90522_state *state;
	struct dtv_fe_stats *stats;
	int ret, i;
	int layers;
	u8 val[15], mode;
	u32 cndat;

	state = fe->demodulator_priv;
	c->delivery_system = SYS_ISDBT;
	c->bandwidth_hz = 6000000;
	mode = 1;
	ret = reg_read(state, 0xb0, val, 1);
	if (ret == 0) {
		mode = (val[0] & 0xc0) >> 6;
		c->transmission_mode = tm_conv[mode];
		c->guard_interval = (val[0] & 0x30) >> 4;
	}

	ret = reg_read(state, 0xb2, val, 6);
	layers = 0;
	if (ret == 0) {
		u8 v;

		c->isdbt_partial_reception = val[0] & 0x01;
		c->isdbt_sb_mode = (val[0] & 0xc0) == 0x40;

		/* layer A */
		v = (val[2] & 0x78) >> 3;
		if (v == 0x0f)
			c->layer[0].segment_count = 0;
		else {
			layers++;
			c->layer[0].segment_count = v;
			c->layer[0].fec = fec_conv_ter[(val[1] & 0x1c) >> 2];
			c->layer[0].modulation = mod_conv[(val[1] & 0xe0) >> 5];
			v = (val[1] & 0x03) << 1 | (val[2] & 0x80) >> 7;
			c->layer[0].interleaving = v;
		}

		/* layer B */
		v = (val[3] & 0x03) << 2 | (val[4] & 0xc0) >> 6;
		if (v == 0x0f)
			c->layer[1].segment_count = 0;
		else {
			layers++;
			c->layer[1].segment_count = v;
			c->layer[1].fec = fec_conv_ter[(val[3] & 0xe0) >> 5];
			c->layer[1].modulation = mod_conv[(val[2] & 0x07)];
			c->layer[1].interleaving = (val[3] & 0x1c) >> 2;
		}

		/* layer C */
		v = (val[5] & 0x1e) >> 1;
		if (v == 0x0f)
			c->layer[2].segment_count = 0;
		else {
			layers++;
			c->layer[2].segment_count = v;
			c->layer[2].fec = fec_conv_ter[(val[4] & 0x07)];
			c->layer[2].modulation = mod_conv[(val[4] & 0x38) >> 3];
			c->layer[2].interleaving = (val[5] & 0xe0) >> 5;
		}
	}

	/* statistics */

	stats = &c->strength;
	stats->len = 0;
	/* let the connected tuner set RSSI property cache */
	if (fe->ops.tuner_ops.get_rf_strength) {
		u16 dummy;

		fe->ops.tuner_ops.get_rf_strength(fe, &dummy);
	}

	stats = &c->cnr;
	stats->len = 1;
	stats->stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	cndat = 0;
	ret = reg_read(state, 0x8b, val, 3);
	if (ret == 0)
		cndat = val[0] << 16 | val[1] << 8 | val[2];
	if (cndat != 0) {
		u32 p, tmp;
		s64 cn;

		/*
		 * cnr[mdB] = 0.024 P^4 - 1.6 P^3 + 39.8 P^2 + 549.1 P + 3096.5
		 * (P = 10log10(5505024/cndat))
		 */
		/* cn = cnr << 3 (61.3 fixed point float */
		/* p = 10log10(5505024/cndat) << 24  (8.24 fixed point float)*/
		p = intlog10(5505024) - intlog10(cndat);
		p *= 10;

		cn = 24772;
		cn += div64_s64(43827LL * p, 10) >> 24;
		tmp = p >> 8;
		cn += div64_s64(3184LL * tmp * tmp, 10) >> 32;
		tmp = p >> 13;
		cn -= div64_s64(128LL * tmp * tmp * tmp, 10) >> 33;
		tmp = p >> 18;
		cn += div64_s64(192LL * tmp * tmp * tmp * tmp, 1000) >> 24;

		stats->stat[0].svalue = cn >> 3;
		stats->stat[0].scale = FE_SCALE_DECIBEL;
	}

	/* per-layer post viterbi BER (or PER? config dependent?) */
	stats = &c->post_bit_error;
	memset(stats, 0, sizeof(*stats));
	stats->len = layers;
	ret = reg_read(state, 0x9d, val, 15);
	if (ret < 0)
		for (i = 0; i < layers; i++)
			stats->stat[i].scale = FE_SCALE_NOT_AVAILABLE;
	else {
		for (i = 0; i < layers; i++) {
			stats->stat[i].scale = FE_SCALE_COUNTER;
			stats->stat[i].uvalue = val[i * 3] << 16
				| val[i * 3 + 1] << 8 | val[i * 3 + 2];
		}
	}
	stats = &c->post_bit_count;
	memset(stats, 0, sizeof(*stats));
	stats->len = layers;
	if (ret < 0)
		for (i = 0; i < layers; i++)
			stats->stat[i].scale = FE_SCALE_NOT_AVAILABLE;
	else {
		for (i = 0; i < layers; i++) {
			stats->stat[i].scale = FE_SCALE_COUNTER;
			stats->stat[i].uvalue =
				val[9 + i * 2] << 8 | val[9 + i * 2 + 1];
			stats->stat[i].uvalue *= 204 * 8;
		}
	}

	return 0;
}

static const struct reg_val reset_sat = { 0x03, 0x01 };
static const struct reg_val reset_ter = { 0x01, 0x40 };

static int tc90522_set_frontend(struct dvb_frontend *fe)
{
	struct tc90522_state *state;
	int ret;

	state = fe->demodulator_priv;

	if (fe->ops.tuner_ops.set_params)
		ret = fe->ops.tuner_ops.set_params(fe);
	else
		ret = -ENODEV;
	if (ret < 0)
		goto failed;

	if (fe->ops.delsys[0] == SYS_ISDBS) {
		ret = tc90522s_set_tsid(fe);
		if (ret < 0)
			goto failed;
		ret = reg_write(state, &reset_sat, 1);
	} else {
		ret = tc90522t_set_layers(fe);
		if (ret < 0)
			goto failed;
		ret = reg_write(state, &reset_ter, 1);
	}
	if (ret < 0)
		goto failed;

	return 0;

failed:
	dev_warn(&state->tuner_i2c.dev, "(%s) failed. [adap%d-fe%d]\n",
			__func__, fe->dvb->num, fe->id);
	return ret;
}

static int tc90522_get_tune_settings(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *settings)
{
	if (fe->ops.delsys[0] == SYS_ISDBS) {
		settings->min_delay_ms = 250;
		settings->step_size = 1000;
		settings->max_drift = settings->step_size * 2;
	} else {
		settings->min_delay_ms = 400;
		settings->step_size = 142857;
		settings->max_drift = settings->step_size;
	}
	return 0;
}

static int tc90522_set_if_agc(struct dvb_frontend *fe, bool on)
{
	struct reg_val agc_sat[] = {
		{ 0x0a, 0x00 },
		{ 0x10, 0x30 },
		{ 0x11, 0x00 },
		{ 0x03, 0x01 },
	};
	struct reg_val agc_ter[] = {
		{ 0x25, 0x00 },
		{ 0x23, 0x4c },
		{ 0x01, 0x40 },
	};
	struct tc90522_state *state;
	struct reg_val *rv;
	int num;

	state = fe->demodulator_priv;
	if (fe->ops.delsys[0] == SYS_ISDBS) {
		agc_sat[0].val = on ? 0xff : 0x00;
		agc_sat[1].val |= 0x80;
		agc_sat[1].val |= on ? 0x01 : 0x00;
		agc_sat[2].val |= on ? 0x40 : 0x00;
		rv = agc_sat;
		num = ARRAY_SIZE(agc_sat);
	} else {
		agc_ter[0].val = on ? 0x40 : 0x00;
		agc_ter[1].val |= on ? 0x00 : 0x01;
		rv = agc_ter;
		num = ARRAY_SIZE(agc_ter);
	}
	return reg_write(state, rv, num);
}

static const struct reg_val sleep_sat = { 0x17, 0x01 };
static const struct reg_val sleep_ter = { 0x03, 0x90 };

static int tc90522_sleep(struct dvb_frontend *fe)
{
	struct tc90522_state *state;
	int ret;

	state = fe->demodulator_priv;
	if (fe->ops.delsys[0] == SYS_ISDBS)
		ret = reg_write(state, &sleep_sat, 1);
	else {
		ret = reg_write(state, &sleep_ter, 1);
		if (ret == 0 && fe->ops.set_lna &&
		    fe->dtv_property_cache.lna == LNA_AUTO) {
			fe->dtv_property_cache.lna = 0;
			ret = fe->ops.set_lna(fe);
			fe->dtv_property_cache.lna = LNA_AUTO;
		}
	}
	if (ret < 0)
		dev_warn(&state->tuner_i2c.dev,
			"(%s) failed. [adap%d-fe%d]\n",
			__func__, fe->dvb->num, fe->id);
	return ret;
}

static const struct reg_val wakeup_sat = { 0x17, 0x00 };
static const struct reg_val wakeup_ter = { 0x03, 0x80 };

static int tc90522_init(struct dvb_frontend *fe)
{
	struct tc90522_state *state;
	int ret;

	/*
	 * Because the init sequence is not public,
	 * the parent device/driver should have init'ed the device before.
	 * just wake up the device here.
	 */

	state = fe->demodulator_priv;
	if (fe->ops.delsys[0] == SYS_ISDBS)
		ret = reg_write(state, &wakeup_sat, 1);
	else {
		ret = reg_write(state, &wakeup_ter, 1);
		if (ret == 0 && fe->ops.set_lna &&
		    fe->dtv_property_cache.lna == LNA_AUTO) {
			fe->dtv_property_cache.lna = 1;
			ret = fe->ops.set_lna(fe);
			fe->dtv_property_cache.lna = LNA_AUTO;
		}
	}
	if (ret < 0) {
		dev_warn(&state->tuner_i2c.dev,
			"(%s) failed. [adap%d-fe%d]\n",
			__func__, fe->dvb->num, fe->id);
		return ret;
	}

	/* prefer 'all-layers' to 'none' as a default */
	if (fe->dtv_property_cache.isdbt_layer_enabled == 0)
		fe->dtv_property_cache.isdbt_layer_enabled = 7;
	return tc90522_set_if_agc(fe, true);
}


/*
 * tuner I2C adapter functions
 */

static int
tc90522_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct tc90522_state *state;
	struct i2c_msg *new_msgs;
	int i, j;
	int ret, rd_num;
	u8 wbuf[256];
	u8 *p, *bufend;

	if (num <= 0)
		return -EINVAL;

	rd_num = 0;
	for (i = 0; i < num; i++)
		if (msgs[i].flags & I2C_M_RD)
			rd_num++;
	new_msgs = kmalloc_array(num + rd_num, sizeof(*new_msgs), GFP_KERNEL);
	if (!new_msgs)
		return -ENOMEM;

	state = i2c_get_adapdata(adap);
	p = wbuf;
	bufend = wbuf + sizeof(wbuf);
	for (i = 0, j = 0; i < num; i++, j++) {
		new_msgs[j].addr = state->i2c_client->addr;
		new_msgs[j].flags = msgs[i].flags;

		if (msgs[i].flags & I2C_M_RD) {
			new_msgs[j].flags &= ~I2C_M_RD;
			if (p + 2 > bufend)
				break;
			p[0] = TC90522_I2C_THRU_REG;
			p[1] = msgs[i].addr << 1 | 0x01;
			new_msgs[j].buf = p;
			new_msgs[j].len = 2;
			p += 2;
			j++;
			new_msgs[j].addr = state->i2c_client->addr;
			new_msgs[j].flags = msgs[i].flags;
			new_msgs[j].buf = msgs[i].buf;
			new_msgs[j].len = msgs[i].len;
			continue;
		}

		if (p + msgs[i].len + 2 > bufend)
			break;
		p[0] = TC90522_I2C_THRU_REG;
		p[1] = msgs[i].addr << 1;
		memcpy(p + 2, msgs[i].buf, msgs[i].len);
		new_msgs[j].buf = p;
		new_msgs[j].len = msgs[i].len + 2;
		p += new_msgs[j].len;
	}

	if (i < num) {
		ret = -ENOMEM;
	} else if (!state->cfg.split_tuner_read_i2c || rd_num == 0) {
		ret = i2c_transfer(state->i2c_client->adapter, new_msgs, j);
	} else {
		/*
		 * Split transactions at each I2C_M_RD message.
		 * Some of the parent device require this,
		 * such as Friio (see. dvb-usb-gl861).
		 */
		int from, to;

		ret = 0;
		from = 0;
		do {
			int r;

			to = from + 1;
			while (to < j && !(new_msgs[to].flags & I2C_M_RD))
				to++;
			r = i2c_transfer(state->i2c_client->adapter,
					 &new_msgs[from], to - from);
			ret = (r <= 0) ? r : ret + r;
			from = to;
		} while (from < j && ret > 0);
	}

	if (ret >= 0 && ret < j)
		ret = -EIO;
	kfree(new_msgs);
	return (ret == j) ? num : ret;
}

static u32 tc90522_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C;
}

static const struct i2c_algorithm tc90522_tuner_i2c_algo = {
	.master_xfer   = &tc90522_master_xfer,
	.functionality = &tc90522_functionality,
};


/*
 * I2C driver functions
 */

static const struct dvb_frontend_ops tc90522_ops_sat = {
	.delsys = { SYS_ISDBS },
	.info = {
		.name = "Toshiba TC90522 ISDB-S module",
		.frequency_min_hz =  950 * MHz,
		.frequency_max_hz = 2150 * MHz,
		.caps = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_AUTO |
			FE_CAN_QAM_AUTO | FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO | FE_CAN_HIERARCHY_AUTO,
	},

	.init = tc90522_init,
	.sleep = tc90522_sleep,
	.set_frontend = tc90522_set_frontend,
	.get_tune_settings = tc90522_get_tune_settings,

	.get_frontend = tc90522s_get_frontend,
	.read_status = tc90522s_read_status,
};

static const struct dvb_frontend_ops tc90522_ops_ter = {
	.delsys = { SYS_ISDBT },
	.info = {
		.name = "Toshiba TC90522 ISDB-T module",
		.frequency_min_hz = 470 * MHz,
		.frequency_max_hz = 770 * MHz,
		.frequency_stepsize_hz = 142857,
		.caps = FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2  | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6  | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_QPSK     | FE_CAN_QAM_16 | FE_CAN_QAM_64 |
			FE_CAN_QAM_AUTO | FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO | FE_CAN_RECOVER |
			FE_CAN_HIERARCHY_AUTO,
	},

	.init = tc90522_init,
	.sleep = tc90522_sleep,
	.set_frontend = tc90522_set_frontend,
	.get_tune_settings = tc90522_get_tune_settings,

	.get_frontend = tc90522t_get_frontend,
	.read_status = tc90522t_read_status,
};


static int tc90522_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct tc90522_state *state;
	struct tc90522_config *cfg;
	const struct dvb_frontend_ops *ops;
	struct i2c_adapter *adap;
	int ret;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;
	state->i2c_client = client;

	cfg = client->dev.platform_data;
	memcpy(&state->cfg, cfg, sizeof(state->cfg));
	cfg->fe = state->cfg.fe = &state->fe;
	ops =  id->driver_data == 0 ? &tc90522_ops_sat : &tc90522_ops_ter;
	memcpy(&state->fe.ops, ops, sizeof(*ops));
	state->fe.demodulator_priv = state;

	adap = &state->tuner_i2c;
	adap->owner = THIS_MODULE;
	adap->algo = &tc90522_tuner_i2c_algo;
	adap->dev.parent = &client->dev;
	strscpy(adap->name, "tc90522_sub", sizeof(adap->name));
	i2c_set_adapdata(adap, state);
	ret = i2c_add_adapter(adap);
	if (ret < 0)
		goto free_state;
	cfg->tuner_i2c = state->cfg.tuner_i2c = adap;

	i2c_set_clientdata(client, &state->cfg);
	dev_info(&client->dev, "Toshiba TC90522 attached.\n");
	return 0;
free_state:
	kfree(state);
	return ret;
}

static void tc90522_remove(struct i2c_client *client)
{
	struct tc90522_state *state;

	state = cfg_to_state(i2c_get_clientdata(client));
	i2c_del_adapter(&state->tuner_i2c);
	kfree(state);
}


static const struct i2c_device_id tc90522_id[] = {
	{ TC90522_I2C_DEV_SAT, 0 },
	{ TC90522_I2C_DEV_TER, 1 },
	{}
};
MODULE_DEVICE_TABLE(i2c, tc90522_id);

static struct i2c_driver tc90522_driver = {
	.driver = {
		.name	= "tc90522",
	},
	.probe		= tc90522_probe,
	.remove		= tc90522_remove,
	.id_table	= tc90522_id,
};

module_i2c_driver(tc90522_driver);

MODULE_DESCRIPTION("Toshiba TC90522 frontend");
MODULE_AUTHOR("Akihiro TSUKADA");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0
/*
 * Sharp QM1D1C0042 8PSK tuner driver
 *
 * Copyright (C) 2014 Akihiro Tsukada <tskd08@gmail.com>
 */

/*
 * NOTICE:
 * As the disclosed information on the chip is very limited,
 * this driver lacks some features, including chip config like IF freq.
 * It assumes that users of this driver (such as a PCI bridge of
 * DTV receiver cards) know the relevant info and
 * configure the chip via I2C if necessary.
 *
 * Currently, PT3 driver is the only one that uses this driver,
 * and contains init/config code in its firmware.
 * Thus some part of the code might be dependent on PT3 specific config.
 */

#include <linux/kernel.h>
#include <linux/math64.h>
#include "qm1d1c0042.h"

#define QM1D1C0042_NUM_REGS 0x20
#define QM1D1C0042_NUM_REG_ROWS 2

static const u8
reg_initval[QM1D1C0042_NUM_REG_ROWS][QM1D1C0042_NUM_REGS] = { {
		0x48, 0x1c, 0xa0, 0x10, 0xbc, 0xc5, 0x20, 0x33,
		0x06, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
		0x00, 0xff, 0xf3, 0x00, 0x2a, 0x64, 0xa6, 0x86,
		0x8c, 0xcf, 0xb8, 0xf1, 0xa8, 0xf2, 0x89, 0x00
	}, {
		0x68, 0x1c, 0xc0, 0x10, 0xbc, 0xc1, 0x11, 0x33,
		0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
		0x00, 0xff, 0xf3, 0x00, 0x3f, 0x25, 0x5c, 0xd6,
		0x55, 0xcf, 0x95, 0xf6, 0x36, 0xf2, 0x09, 0x00
	}
};

static int reg_index;

static const struct qm1d1c0042_config default_cfg = {
	.xtal_freq = 16000,
	.lpf = 1,
	.fast_srch = 0,
	.lpf_wait = 20,
	.fast_srch_wait = 4,
	.normal_srch_wait = 15,
};

struct qm1d1c0042_state {
	struct qm1d1c0042_config cfg;
	struct i2c_client *i2c;
	u8 regs[QM1D1C0042_NUM_REGS];
};

static struct qm1d1c0042_state *cfg_to_state(struct qm1d1c0042_config *c)
{
	return container_of(c, struct qm1d1c0042_state, cfg);
}

static int reg_write(struct qm1d1c0042_state *state, u8 reg, u8 val)
{
	u8 wbuf[2] = { reg, val };
	int ret;

	ret = i2c_master_send(state->i2c, wbuf, sizeof(wbuf));
	if (ret >= 0 && ret < sizeof(wbuf))
		ret = -EIO;
	return (ret == sizeof(wbuf)) ? 0 : ret;
}

static int reg_read(struct qm1d1c0042_state *state, u8 reg, u8 *val)
{
	struct i2c_msg msgs[2] = {
		{
			.addr = state->i2c->addr,
			.flags = 0,
			.buf = &reg,
			.len = 1,
		},
		{
			.addr = state->i2c->addr,
			.flags = I2C_M_RD,
			.buf = val,
			.len = 1,
		},
	};
	int ret;

	ret = i2c_transfer(state->i2c->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret >= 0 && ret < ARRAY_SIZE(msgs))
		ret = -EIO;
	return (ret == ARRAY_SIZE(msgs)) ? 0 : ret;
}


static int qm1d1c0042_set_srch_mode(struct qm1d1c0042_state *state, bool fast)
{
	if (fast)
		state->regs[0x03] |= 0x01; /* set fast search mode */
	else
		state->regs[0x03] &= ~0x01 & 0xff;

	return reg_write(state, 0x03, state->regs[0x03]);
}

static int qm1d1c0042_wakeup(struct qm1d1c0042_state *state)
{
	int ret;

	state->regs[0x01] |= 1 << 3;             /* BB_Reg_enable */
	state->regs[0x01] &= (~(1 << 0)) & 0xff; /* NORMAL (wake-up) */
	state->regs[0x05] &= (~(1 << 3)) & 0xff; /* pfd_rst NORMAL */
	ret = reg_write(state, 0x01, state->regs[0x01]);
	if (ret == 0)
		ret = reg_write(state, 0x05, state->regs[0x05]);

	if (ret < 0)
		dev_warn(&state->i2c->dev, "(%s) failed. [adap%d-fe%d]\n",
			__func__, state->cfg.fe->dvb->num, state->cfg.fe->id);
	return ret;
}

/* tuner_ops */

static int qm1d1c0042_set_config(struct dvb_frontend *fe, void *priv_cfg)
{
	struct qm1d1c0042_state *state;
	struct qm1d1c0042_config *cfg;

	state = fe->tuner_priv;
	cfg = priv_cfg;

	if (cfg->fe)
		state->cfg.fe = cfg->fe;

	if (cfg->xtal_freq != QM1D1C0042_CFG_XTAL_DFLT)
		dev_warn(&state->i2c->dev,
			"(%s) changing xtal_freq not supported. ", __func__);
	state->cfg.xtal_freq = default_cfg.xtal_freq;

	state->cfg.lpf = cfg->lpf;
	state->cfg.fast_srch = cfg->fast_srch;

	if (cfg->lpf_wait != QM1D1C0042_CFG_WAIT_DFLT)
		state->cfg.lpf_wait = cfg->lpf_wait;
	else
		state->cfg.lpf_wait = default_cfg.lpf_wait;

	if (cfg->fast_srch_wait != QM1D1C0042_CFG_WAIT_DFLT)
		state->cfg.fast_srch_wait = cfg->fast_srch_wait;
	else
		state->cfg.fast_srch_wait = default_cfg.fast_srch_wait;

	if (cfg->normal_srch_wait != QM1D1C0042_CFG_WAIT_DFLT)
		state->cfg.normal_srch_wait = cfg->normal_srch_wait;
	else
		state->cfg.normal_srch_wait = default_cfg.normal_srch_wait;
	return 0;
}

/* divisor, vco_band parameters */
/*  {maxfreq,  param1(band?), param2(div?) */
static const u32 conv_table[9][3] = {
	{ 2151000, 1, 7 },
	{ 1950000, 1, 6 },
	{ 1800000, 1, 5 },
	{ 1600000, 1, 4 },
	{ 1450000, 1, 3 },
	{ 1250000, 1, 2 },
	{ 1200000, 0, 7 },
	{  975000, 0, 6 },
	{  950000, 0, 0 }
};

static int qm1d1c0042_set_params(struct dvb_frontend *fe)
{
	struct qm1d1c0042_state *state;
	u32 freq;
	int i, ret;
	u8 val, mask;
	u32 a, sd;
	s32 b;

	state = fe->tuner_priv;
	freq = fe->dtv_property_cache.frequency;

	state->regs[0x08] &= 0xf0;
	state->regs[0x08] |= 0x09;

	state->regs[0x13] &= 0x9f;
	state->regs[0x13] |= 0x20;

	/* div2/vco_band */
	val = state->regs[0x02] & 0x0f;
	for (i = 0; i < 8; i++)
		if (freq < conv_table[i][0] && freq >= conv_table[i + 1][0]) {
			val |= conv_table[i][1] << 7;
			val |= conv_table[i][2] << 4;
			break;
		}
	ret = reg_write(state, 0x02, val);
	if (ret < 0)
		return ret;

	a = DIV_ROUND_CLOSEST(freq, state->cfg.xtal_freq);

	state->regs[0x06] &= 0x40;
	state->regs[0x06] |= (a - 12) / 4;
	ret = reg_write(state, 0x06, state->regs[0x06]);
	if (ret < 0)
		return ret;

	state->regs[0x07] &= 0xf0;
	state->regs[0x07] |= (a - 4 * ((a - 12) / 4 + 1) - 5) & 0x0f;
	ret = reg_write(state, 0x07, state->regs[0x07]);
	if (ret < 0)
		return ret;

	/* LPF */
	val = state->regs[0x08];
	if (state->cfg.lpf) {
		/* LPF_CLK, LPF_FC */
		val &= 0xf0;
		val |= 0x02;
	}
	ret = reg_write(state, 0x08, val);
	if (ret < 0)
		return ret;

	/*
	 * b = (freq / state->cfg.xtal_freq - a) << 20;
	 * sd = b          (b >= 0)
	 *      1<<22 + b  (b < 0)
	 */
	b = (s32)div64_s64(((s64) freq) << 20, state->cfg.xtal_freq)
			   - (((s64) a) << 20);

	if (b >= 0)
		sd = b;
	else
		sd = (1 << 22) + b;

	state->regs[0x09] &= 0xc0;
	state->regs[0x09] |= (sd >> 16) & 0x3f;
	state->regs[0x0a] = (sd >> 8) & 0xff;
	state->regs[0x0b] = sd & 0xff;
	ret = reg_write(state, 0x09, state->regs[0x09]);
	if (ret == 0)
		ret = reg_write(state, 0x0a, state->regs[0x0a]);
	if (ret == 0)
		ret = reg_write(state, 0x0b, state->regs[0x0b]);
	if (ret != 0)
		return ret;

	if (!state->cfg.lpf) {
		/* CSEL_Offset */
		ret = reg_write(state, 0x13, state->regs[0x13]);
		if (ret < 0)
			return ret;
	}

	/* VCO_TM, LPF_TM */
	mask = state->cfg.lpf ? 0x3f : 0x7f;
	val = state->regs[0x0c] & mask;
	ret = reg_write(state, 0x0c, val);
	if (ret < 0)
		return ret;
	usleep_range(2000, 3000);
	val = state->regs[0x0c] | ~mask;
	ret = reg_write(state, 0x0c, val);
	if (ret < 0)
		return ret;

	if (state->cfg.lpf)
		msleep(state->cfg.lpf_wait);
	else if (state->regs[0x03] & 0x01)
		msleep(state->cfg.fast_srch_wait);
	else
		msleep(state->cfg.normal_srch_wait);

	if (state->cfg.lpf) {
		/* LPF_FC */
		ret = reg_write(state, 0x08, 0x09);
		if (ret < 0)
			return ret;

		/* CSEL_Offset */
		ret = reg_write(state, 0x13, state->regs[0x13]);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int qm1d1c0042_sleep(struct dvb_frontend *fe)
{
	struct qm1d1c0042_state *state;
	int ret;

	state = fe->tuner_priv;
	state->regs[0x01] &= (~(1 << 3)) & 0xff; /* BB_Reg_disable */
	state->regs[0x01] |= 1 << 0;             /* STDBY */
	state->regs[0x05] |= 1 << 3;             /* pfd_rst STANDBY */
	ret = reg_write(state, 0x05, state->regs[0x05]);
	if (ret == 0)
		ret = reg_write(state, 0x01, state->regs[0x01]);
	if (ret < 0)
		dev_warn(&state->i2c->dev, "(%s) failed. [adap%d-fe%d]\n",
			__func__, fe->dvb->num, fe->id);
	return ret;
}

static int qm1d1c0042_init(struct dvb_frontend *fe)
{
	struct qm1d1c0042_state *state;
	u8 val;
	int i, ret;

	state = fe->tuner_priv;

	reg_write(state, 0x01, 0x0c);
	reg_write(state, 0x01, 0x0c);

	ret = reg_write(state, 0x01, 0x0c); /* soft reset on */
	if (ret < 0)
		goto failed;
	usleep_range(2000, 3000);

	ret = reg_write(state, 0x01, 0x1c); /* soft reset off */
	if (ret < 0)
		goto failed;

	/* check ID and choose initial registers corresponding ID */
	ret = reg_read(state, 0x00, &val);
	if (ret < 0)
		goto failed;
	for (reg_index = 0; reg_index < QM1D1C0042_NUM_REG_ROWS;
	     reg_index++) {
		if (val == reg_initval[reg_index][0x00])
			break;
	}
	if (reg_index >= QM1D1C0042_NUM_REG_ROWS) {
		ret = -EINVAL;
		goto failed;
	}
	memcpy(state->regs, reg_initval[reg_index], QM1D1C0042_NUM_REGS);
	usleep_range(2000, 3000);

	state->regs[0x0c] |= 0x40;
	ret = reg_write(state, 0x0c, state->regs[0x0c]);
	if (ret < 0)
		goto failed;
	msleep(state->cfg.lpf_wait);

	/* set all writable registers */
	for (i = 1; i <= 0x0c ; i++) {
		ret = reg_write(state, i, state->regs[i]);
		if (ret < 0)
			goto failed;
	}
	for (i = 0x11; i < QM1D1C0042_NUM_REGS; i++) {
		ret = reg_write(state, i, state->regs[i]);
		if (ret < 0)
			goto failed;
	}

	ret = qm1d1c0042_wakeup(state);
	if (ret < 0)
		goto failed;

	ret = qm1d1c0042_set_srch_mode(state, state->cfg.fast_srch);
	if (ret < 0)
		goto failed;

	return ret;

failed:
	dev_warn(&state->i2c->dev, "(%s) failed. [adap%d-fe%d]\n",
		__func__, fe->dvb->num, fe->id);
	return ret;
}

/* I2C driver functions */

static const struct dvb_tuner_ops qm1d1c0042_ops = {
	.info = {
		.name = "Sharp QM1D1C0042",

		.frequency_min_hz =  950 * MHz,
		.frequency_max_hz = 2150 * MHz,
	},

	.init = qm1d1c0042_init,
	.sleep = qm1d1c0042_sleep,
	.set_config = qm1d1c0042_set_config,
	.set_params = qm1d1c0042_set_params,
};


static int qm1d1c0042_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct qm1d1c0042_state *state;
	struct qm1d1c0042_config *cfg;
	struct dvb_frontend *fe;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;
	state->i2c = client;

	cfg = client->dev.platform_data;
	fe = cfg->fe;
	fe->tuner_priv = state;
	qm1d1c0042_set_config(fe, cfg);
	memcpy(&fe->ops.tuner_ops, &qm1d1c0042_ops, sizeof(qm1d1c0042_ops));

	i2c_set_clientdata(client, &state->cfg);
	dev_info(&client->dev, "Sharp QM1D1C0042 attached.\n");
	return 0;
}

static int qm1d1c0042_remove(struct i2c_client *client)
{
	struct qm1d1c0042_state *state;

	state = cfg_to_state(i2c_get_clientdata(client));
	state->cfg.fe->tuner_priv = NULL;
	kfree(state);
	return 0;
}


static const struct i2c_device_id qm1d1c0042_id[] = {
	{"qm1d1c0042", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, qm1d1c0042_id);

static struct i2c_driver qm1d1c0042_driver = {
	.driver = {
		.name	= "qm1d1c0042",
	},
	.probe		= qm1d1c0042_probe,
	.remove		= qm1d1c0042_remove,
	.id_table	= qm1d1c0042_id,
};

module_i2c_driver(qm1d1c0042_driver);

MODULE_DESCRIPTION("Sharp QM1D1C0042 tuner");
MODULE_AUTHOR("Akihiro TSUKADA");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0
/*
 * MaxLinear MxL301RF OFDM tuner driver
 *
 * Copyright (C) 2014 Akihiro Tsukada <tskd08@gmail.com>
 */

/*
 * NOTICE:
 * This driver is incomplete and lacks init/config of the chips,
 * as the necessary info is not disclosed.
 * Other features like get_if_frequency() are missing as well.
 * It assumes that users of this driver (such as a PCI bridge of
 * DTV receiver cards) properly init and configure the chip
 * via I2C *before* calling this driver's init() function.
 *
 * Currently, PT3 driver is the only one that uses this driver,
 * and contains init/config code in its firmware.
 * Thus some part of the code might be dependent on PT3 specific config.
 */

#include <linux/kernel.h>
#include "mxl301rf.h"

struct mxl301rf_state {
	struct mxl301rf_config cfg;
	struct i2c_client *i2c;
};

static struct mxl301rf_state *cfg_to_state(struct mxl301rf_config *c)
{
	return container_of(c, struct mxl301rf_state, cfg);
}

static int raw_write(struct mxl301rf_state *state, const u8 *buf, int len)
{
	int ret;

	ret = i2c_master_send(state->i2c, buf, len);
	if (ret >= 0 && ret < len)
		ret = -EIO;
	return (ret == len) ? 0 : ret;
}

static int reg_write(struct mxl301rf_state *state, u8 reg, u8 val)
{
	u8 buf[2] = { reg, val };

	return raw_write(state, buf, 2);
}

static int reg_read(struct mxl301rf_state *state, u8 reg, u8 *val)
{
	u8 wbuf[2] = { 0xfb, reg };
	int ret;

	ret = raw_write(state, wbuf, sizeof(wbuf));
	if (ret == 0)
		ret = i2c_master_recv(state->i2c, val, 1);
	if (ret >= 0 && ret < 1)
		ret = -EIO;
	return (ret == 1) ? 0 : ret;
}

/* tuner_ops */

/* get RSSI and update propery cache, set to *out in % */
static int mxl301rf_get_rf_strength(struct dvb_frontend *fe, u16 *out)
{
	struct mxl301rf_state *state;
	int ret;
	u8  rf_in1, rf_in2, rf_off1, rf_off2;
	u16 rf_in, rf_off;
	s64 level;
	struct dtv_fe_stats *rssi;

	rssi = &fe->dtv_property_cache.strength;
	rssi->len = 1;
	rssi->stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	*out = 0;

	state = fe->tuner_priv;
	ret = reg_write(state, 0x14, 0x01);
	if (ret < 0)
		return ret;
	usleep_range(1000, 2000);

	ret = reg_read(state, 0x18, &rf_in1);
	if (ret == 0)
		ret = reg_read(state, 0x19, &rf_in2);
	if (ret == 0)
		ret = reg_read(state, 0xd6, &rf_off1);
	if (ret == 0)
		ret = reg_read(state, 0xd7, &rf_off2);
	if (ret != 0)
		return ret;

	rf_in = (rf_in2 & 0x07) << 8 | rf_in1;
	rf_off = (rf_off2 & 0x0f) << 5 | (rf_off1 >> 3);
	level = rf_in - rf_off - (113 << 3); /* x8 dBm */
	level = level * 1000 / 8;
	rssi->stat[0].svalue = level;
	rssi->stat[0].scale = FE_SCALE_DECIBEL;
	/* *out = (level - min) * 100 / (max - min) */
	*out = (rf_in - rf_off + (1 << 9) - 1) * 100 / ((5 << 9) - 2);
	return 0;
}

/* spur shift parameters */
struct shf {
	u32	freq;		/* Channel center frequency */
	u32	ofst_th;	/* Offset frequency threshold */
	u8	shf_val;	/* Spur shift value */
	u8	shf_dir;	/* Spur shift direction */
};

static const struct shf shf_tab[] = {
	{  64500, 500, 0x92, 0x07 },
	{ 191500, 300, 0xe2, 0x07 },
	{ 205500, 500, 0x2c, 0x04 },
	{ 212500, 500, 0x1e, 0x04 },
	{ 226500, 500, 0xd4, 0x07 },
	{  99143, 500, 0x9c, 0x07 },
	{ 173143, 500, 0xd4, 0x07 },
	{ 191143, 300, 0xd4, 0x07 },
	{ 207143, 500, 0xce, 0x07 },
	{ 225143, 500, 0xce, 0x07 },
	{ 243143, 500, 0xd4, 0x07 },
	{ 261143, 500, 0xd4, 0x07 },
	{ 291143, 500, 0xd4, 0x07 },
	{ 339143, 500, 0x2c, 0x04 },
	{ 117143, 500, 0x7a, 0x07 },
	{ 135143, 300, 0x7a, 0x07 },
	{ 153143, 500, 0x01, 0x07 }
};

struct reg_val {
	u8 reg;
	u8 val;
} __attribute__ ((__packed__));

static const struct reg_val set_idac[] = {
	{ 0x0d, 0x00 },
	{ 0x0c, 0x67 },
	{ 0x6f, 0x89 },
	{ 0x70, 0x0c },
	{ 0x6f, 0x8a },
	{ 0x70, 0x0e },
	{ 0x6f, 0x8b },
	{ 0x70, 0x1c },
};

static int mxl301rf_set_params(struct dvb_frontend *fe)
{
	struct reg_val tune0[] = {
		{ 0x13, 0x00 },		/* abort tuning */
		{ 0x3b, 0xc0 },
		{ 0x3b, 0x80 },
		{ 0x10, 0x95 },		/* BW */
		{ 0x1a, 0x05 },
		{ 0x61, 0x00 },		/* spur shift value (placeholder) */
		{ 0x62, 0xa0 }		/* spur shift direction (placeholder) */
	};

	struct reg_val tune1[] = {
		{ 0x11, 0x40 },		/* RF frequency L (placeholder) */
		{ 0x12, 0x0e },		/* RF frequency H (placeholder) */
		{ 0x13, 0x01 }		/* start tune */
	};

	struct mxl301rf_state *state;
	u32 freq;
	u16 f;
	u32 tmp, div;
	int i, ret;

	state = fe->tuner_priv;
	freq = fe->dtv_property_cache.frequency;

	/* spur shift function (for analog) */
	for (i = 0; i < ARRAY_SIZE(shf_tab); i++) {
		if (freq >= (shf_tab[i].freq - shf_tab[i].ofst_th) * 1000 &&
		    freq <= (shf_tab[i].freq + shf_tab[i].ofst_th) * 1000) {
			tune0[5].val = shf_tab[i].shf_val;
			tune0[6].val = 0xa0 | shf_tab[i].shf_dir;
			break;
		}
	}
	ret = raw_write(state, (u8 *) tune0, sizeof(tune0));
	if (ret < 0)
		goto failed;
	usleep_range(3000, 4000);

	/* convert freq to 10.6 fixed point float [MHz] */
	f = freq / 1000000;
	tmp = freq % 1000000;
	div = 1000000;
	for (i = 0; i < 6; i++) {
		f <<= 1;
		div >>= 1;
		if (tmp > div) {
			tmp -= div;
			f |= 1;
		}
	}
	if (tmp > 7812)
		f++;
	tune1[0].val = f & 0xff;
	tune1[1].val = f >> 8;
	ret = raw_write(state, (u8 *) tune1, sizeof(tune1));
	if (ret < 0)
		goto failed;
	msleep(31);

	ret = reg_write(state, 0x1a, 0x0d);
	if (ret < 0)
		goto failed;
	ret = raw_write(state, (u8 *) set_idac, sizeof(set_idac));
	if (ret < 0)
		goto failed;
	return 0;

failed:
	dev_warn(&state->i2c->dev, "(%s) failed. [adap%d-fe%d]\n",
		__func__, fe->dvb->num, fe->id);
	return ret;
}

static const struct reg_val standby_data[] = {
	{ 0x01, 0x00 },
	{ 0x13, 0x00 }
};

static int mxl301rf_sleep(struct dvb_frontend *fe)
{
	struct mxl301rf_state *state;
	int ret;

	state = fe->tuner_priv;
	ret = raw_write(state, (u8 *)standby_data, sizeof(standby_data));
	if (ret < 0)
		dev_warn(&state->i2c->dev, "(%s) failed. [adap%d-fe%d]\n",
			__func__, fe->dvb->num, fe->id);
	return ret;
}


/* init sequence is not public.
 * the parent must have init'ed the device.
 * just wake up here.
 */
static int mxl301rf_init(struct dvb_frontend *fe)
{
	struct mxl301rf_state *state;
	int ret;

	state = fe->tuner_priv;

	ret = reg_write(state, 0x01, 0x01);
	if (ret < 0) {
		dev_warn(&state->i2c->dev, "(%s) failed. [adap%d-fe%d]\n",
			 __func__, fe->dvb->num, fe->id);
		return ret;
	}
	return 0;
}

/* I2C driver functions */

static const struct dvb_tuner_ops mxl301rf_ops = {
	.info = {
		.name = "MaxLinear MxL301RF",

		.frequency_min_hz =  93 * MHz,
		.frequency_max_hz = 803 * MHz + 142857,
	},

	.init = mxl301rf_init,
	.sleep = mxl301rf_sleep,

	.set_params = mxl301rf_set_params,
	.get_rf_strength = mxl301rf_get_rf_strength,
};


static int mxl301rf_probe(struct i2c_client *client)
{
	struct mxl301rf_state *state;
	struct mxl301rf_config *cfg;
	struct dvb_frontend *fe;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->i2c = client;
	cfg = client->dev.platform_data;

	memcpy(&state->cfg, cfg, sizeof(state->cfg));
	fe = cfg->fe;
	fe->tuner_priv = state;
	memcpy(&fe->ops.tuner_ops, &mxl301rf_ops, sizeof(mxl301rf_ops));

	i2c_set_clientdata(client, &state->cfg);
	dev_info(&client->dev, "MaxLinear MxL301RF attached.\n");
	return 0;
}

static void mxl301rf_remove(struct i2c_client *client)
{
	struct mxl301rf_state *state;

	state = cfg_to_state(i2c_get_clientdata(client));
	state->cfg.fe->tuner_priv = NULL;
	kfree(state);
}


static const struct i2c_device_id mxl301rf_id[] = {
	{ "mxl301rf" },
	{}
};
MODULE_DEVICE_TABLE(i2c, mxl301rf_id);

static struct i2c_driver mxl301rf_driver = {
	.driver = {
		.name	= "mxl301rf",
	},
	.probe		= mxl301rf_probe,
	.remove		= mxl301rf_remove,
	.id_table	= mxl301rf_id,
};

module_i2c_driver(mxl301rf_driver);

MODULE_DESCRIPTION("MaxLinear MXL301RF tuner");
MODULE_AUTHOR("Akihiro TSUKADA");
MODULE_LICENSE("GPL");

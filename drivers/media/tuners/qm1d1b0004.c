// SPDX-License-Identifier: GPL-2.0
/*
 * Sharp QM1D1B0004 satellite tuner
 *
 * Copyright (C) 2014 Akihiro Tsukada <tskd08@gmail.com>
 *
 * based on (former) drivers/media/pci/pt1/va1j5jf8007s.c.
 */

/*
 * Note:
 * Since the data-sheet of this tuner chip is not available,
 * this driver lacks some tuner_ops and config options.
 * In addition, the implementation might be dependent on the specific use
 * in the FE module: VA1J5JF8007S and/or in the product: Earthsoft PT1/PT2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <media/dvb_frontend.h>
#include "qm1d1b0004.h"

/*
 * Tuner I/F (copied from the former va1j5jf8007s.c)
 * b[0] I2C addr
 * b[1] "0":1, BG:2, divider_quotient[7:3]:5
 * b[2] divider_quotient[2:0]:3, divider_remainder:5
 * b[3] "111":3, LPF[3:2]:2, TM:1, "0":1, REF:1
 * b[4] BANDX, PSC:1, LPF[1:0]:2, DIV:1, "0":1
 *
 * PLL frequency step :=
 *    REF == 0 -> PLL XTL frequency(4MHz) / 8
 *    REF == 1 -> PLL XTL frequency(4MHz) / 4
 *
 * PreScaler :=
 *    PSC == 0 -> x32
 *    PSC == 1 -> x16
 *
 * divider_quotient := (frequency / PLL frequency step) / PreScaler
 * divider_remainder := (frequency / PLL frequency step) % PreScaler
 *
 * LPF := LPF Frequency / 1000 / 2 - 2
 * LPF Frequency @ baudrate=28.86Mbps = 30000
 *
 * band (1..9)
 *   band 1 (freq <  986000) -> DIV:1, BANDX:5, PSC:1
 *   band 2 (freq < 1072000) -> DIV:1, BANDX:6, PSC:1
 *   band 3 (freq < 1154000) -> DIV:1, BANDX:7, PSC:0
 *   band 4 (freq < 1291000) -> DIV:0, BANDX:1, PSC:0
 *   band 5 (freq < 1447000) -> DIV:0, BANDX:2, PSC:0
 *   band 6 (freq < 1615000) -> DIV:0, BANDX:3, PSC:0
 *   band 7 (freq < 1791000) -> DIV:0, BANDX:4, PSC:0
 *   band 8 (freq < 1972000) -> DIV:0, BANDX:5, PSC:0
 *   band 9 (freq < 2150000) -> DIV:0, BANDX:6, PSC:0
 */

#define QM1D1B0004_PSC_MASK (1 << 4)

#define QM1D1B0004_XTL_FREQ 4000
#define QM1D1B0004_LPF_FALLBACK 30000

#if 0 /* Currently unused */
static const struct qm1d1b0004_config default_cfg = {
	.lpf_freq = QM1D1B0004_CFG_LPF_DFLT,
	.half_step = false,
};
#endif

struct qm1d1b0004_state {
	struct qm1d1b0004_config cfg;
	struct i2c_client *i2c;
};


struct qm1d1b0004_cb_map {
	u32 frequency;
	u8 cb;
};

static const struct qm1d1b0004_cb_map cb_maps[] = {
	{  986000, 0xb2 },
	{ 1072000, 0xd2 },
	{ 1154000, 0xe2 },
	{ 1291000, 0x20 },
	{ 1447000, 0x40 },
	{ 1615000, 0x60 },
	{ 1791000, 0x80 },
	{ 1972000, 0xa0 },
};

static u8 lookup_cb(u32 frequency)
{
	int i;
	const struct qm1d1b0004_cb_map *map;

	for (i = 0; i < ARRAY_SIZE(cb_maps); i++) {
		map = &cb_maps[i];
		if (frequency < map->frequency)
			return map->cb;
	}
	return 0xc0;
}

static int qm1d1b0004_set_params(struct dvb_frontend *fe)
{
	struct qm1d1b0004_state *state;
	u32 frequency, pll, lpf_freq;
	u16 word;
	u8 buf[4], cb, lpf;
	int ret;

	state = fe->tuner_priv;
	frequency = fe->dtv_property_cache.frequency;

	pll = QM1D1B0004_XTL_FREQ / 4;
	if (state->cfg.half_step)
		pll /= 2;
	word = DIV_ROUND_CLOSEST(frequency, pll);
	cb = lookup_cb(frequency);
	if (cb & QM1D1B0004_PSC_MASK)
		word = (word << 1 & ~0x1f) | (word & 0x0f);

	/* step.1: set frequency with BG:2, TM:0(4MHZ), LPF:4MHz */
	buf[0] = 0x40 | word >> 8;
	buf[1] = word;
	/* inconsisnten with the above I/F doc. maybe the doc is wrong */
	buf[2] = 0xe0 | state->cfg.half_step;
	buf[3] = cb;
	ret = i2c_master_send(state->i2c, buf, 4);
	if (ret < 0)
		return ret;

	/* step.2: set TM:1 */
	buf[0] = 0xe4 | state->cfg.half_step;
	ret = i2c_master_send(state->i2c, buf, 1);
	if (ret < 0)
		return ret;
	msleep(20);

	/* step.3: set LPF */
	lpf_freq = state->cfg.lpf_freq;
	if (lpf_freq == QM1D1B0004_CFG_LPF_DFLT)
		lpf_freq = fe->dtv_property_cache.symbol_rate / 1000;
	if (lpf_freq == 0)
		lpf_freq = QM1D1B0004_LPF_FALLBACK;
	lpf = DIV_ROUND_UP(lpf_freq, 2000) - 2;
	buf[0] = 0xe4 | ((lpf & 0x0c) << 1) | state->cfg.half_step;
	buf[1] = cb | ((lpf & 0x03) << 2);
	ret = i2c_master_send(state->i2c, buf, 2);
	if (ret < 0)
		return ret;

	/* step.4: read PLL lock? */
	buf[0] = 0;
	ret = i2c_master_recv(state->i2c, buf, 1);
	if (ret < 0)
		return ret;
	return 0;
}


static int qm1d1b0004_set_config(struct dvb_frontend *fe, void *priv_cfg)
{
	struct qm1d1b0004_state *state;

	state = fe->tuner_priv;
	memcpy(&state->cfg, priv_cfg, sizeof(state->cfg));
	return 0;
}


static int qm1d1b0004_init(struct dvb_frontend *fe)
{
	struct qm1d1b0004_state *state;
	u8 buf[2] = {0xf8, 0x04};

	state = fe->tuner_priv;
	if (state->cfg.half_step)
		buf[0] |= 0x01;

	return i2c_master_send(state->i2c, buf, 2);
}


static const struct dvb_tuner_ops qm1d1b0004_ops = {
	.info = {
		.name = "Sharp qm1d1b0004",

		.frequency_min =  950000,
		.frequency_max = 2150000,
	},

	.init = qm1d1b0004_init,

	.set_params = qm1d1b0004_set_params,
	.set_config = qm1d1b0004_set_config,
};

static int
qm1d1b0004_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct dvb_frontend *fe;
	struct qm1d1b0004_config *cfg;
	struct qm1d1b0004_state *state;
	int ret;

	cfg = client->dev.platform_data;
	fe = cfg->fe;
	i2c_set_clientdata(client, fe);

	fe->tuner_priv = kzalloc(sizeof(struct qm1d1b0004_state), GFP_KERNEL);
	if (!fe->tuner_priv) {
		ret = -ENOMEM;
		goto err_mem;
	}

	memcpy(&fe->ops.tuner_ops, &qm1d1b0004_ops, sizeof(fe->ops.tuner_ops));

	state = fe->tuner_priv;
	state->i2c = client;
	ret = qm1d1b0004_set_config(fe, cfg);
	if (ret != 0)
		goto err_priv;

	dev_info(&client->dev, "Sharp QM1D1B0004 attached.\n");
	return 0;

err_priv:
	kfree(fe->tuner_priv);
err_mem:
	fe->tuner_priv = NULL;
	return ret;
}

static int qm1d1b0004_remove(struct i2c_client *client)
{
	struct dvb_frontend *fe;

	fe = i2c_get_clientdata(client);
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
	return 0;
}


static const struct i2c_device_id qm1d1b0004_id[] = {
	{"qm1d1b0004", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, qm1d1b0004_id);

static struct i2c_driver qm1d1b0004_driver = {
	.driver = {
		.name = "qm1d1b0004",
	},
	.probe    = qm1d1b0004_probe,
	.remove   = qm1d1b0004_remove,
	.id_table = qm1d1b0004_id,
};

module_i2c_driver(qm1d1b0004_driver);

MODULE_DESCRIPTION("Sharp QM1D1B0004");
MODULE_AUTHOR("Akihiro Tsukada");
MODULE_LICENSE("GPL");

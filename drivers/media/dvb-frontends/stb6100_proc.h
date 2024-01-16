/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
	STB6100 Silicon Tuner wrapper
	Copyright (C)2009 Igor M. Liplianin (liplianin@me.by)

*/

#include <linux/dvb/frontend.h>
#include <media/dvb_frontend.h>

static int stb6100_get_freq(struct dvb_frontend *fe, u32 *frequency)
{
	struct dvb_frontend_ops	*frontend_ops = &fe->ops;
	struct dvb_tuner_ops	*tuner_ops = &frontend_ops->tuner_ops;
	int err = 0;

	if (tuner_ops->get_frequency) {
		if (frontend_ops->i2c_gate_ctrl)
			frontend_ops->i2c_gate_ctrl(fe, 1);

		err = tuner_ops->get_frequency(fe, frequency);
		if (err < 0) {
			printk("%s: Invalid parameter\n", __func__);
			return err;
		}

		if (frontend_ops->i2c_gate_ctrl)
			frontend_ops->i2c_gate_ctrl(fe, 0);
	}

	return 0;
}

static int stb6100_set_freq(struct dvb_frontend *fe, u32 frequency)
{
	struct dvb_frontend_ops	*frontend_ops = &fe->ops;
	struct dvb_tuner_ops	*tuner_ops = &frontend_ops->tuner_ops;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u32 bw = c->bandwidth_hz;
	int err = 0;

	c->frequency = frequency;
	c->bandwidth_hz = 0;		/* Don't adjust the bandwidth */

	if (tuner_ops->set_params) {
		if (frontend_ops->i2c_gate_ctrl)
			frontend_ops->i2c_gate_ctrl(fe, 1);

		err = tuner_ops->set_params(fe);
		c->bandwidth_hz = bw;
		if (err < 0) {
			printk("%s: Invalid parameter\n", __func__);
			return err;
		}

		if (frontend_ops->i2c_gate_ctrl)
			frontend_ops->i2c_gate_ctrl(fe, 0);

	}

	return 0;
}

static int stb6100_get_bandw(struct dvb_frontend *fe, u32 *bandwidth)
{
	struct dvb_frontend_ops	*frontend_ops = &fe->ops;
	struct dvb_tuner_ops	*tuner_ops = &frontend_ops->tuner_ops;
	int err = 0;

	if (tuner_ops->get_bandwidth) {
		if (frontend_ops->i2c_gate_ctrl)
			frontend_ops->i2c_gate_ctrl(fe, 1);

		err = tuner_ops->get_bandwidth(fe, bandwidth);
		if (err < 0) {
			printk(KERN_ERR "%s: Invalid parameter\n", __func__);
			return err;
		}

		if (frontend_ops->i2c_gate_ctrl)
			frontend_ops->i2c_gate_ctrl(fe, 0);
	}

	return 0;
}

static int stb6100_set_bandw(struct dvb_frontend *fe, u32 bandwidth)
{
	struct dvb_frontend_ops	*frontend_ops = &fe->ops;
	struct dvb_tuner_ops	*tuner_ops = &frontend_ops->tuner_ops;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u32 freq = c->frequency;
	int err = 0;

	c->bandwidth_hz = bandwidth;
	c->frequency = 0;		/* Don't adjust the frequency */

	if (tuner_ops->set_params) {
		if (frontend_ops->i2c_gate_ctrl)
			frontend_ops->i2c_gate_ctrl(fe, 1);

		err = tuner_ops->set_params(fe);
		c->frequency = freq;
		if (err < 0) {
			printk(KERN_ERR "%s: Invalid parameter\n", __func__);
			return err;
		}

		if (frontend_ops->i2c_gate_ctrl)
			frontend_ops->i2c_gate_ctrl(fe, 0);

	}

	return 0;
}

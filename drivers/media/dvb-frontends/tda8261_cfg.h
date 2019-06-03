/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
	TDA8261 8PSK/QPSK tuner driver
	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

*/

static int tda8261_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct dvb_frontend_ops	*frontend_ops = &fe->ops;
	struct dvb_tuner_ops	*tuner_ops = &frontend_ops->tuner_ops;
	int err = 0;

	if (tuner_ops->get_frequency) {
		err = tuner_ops->get_frequency(fe, frequency);
		if (err < 0) {
			pr_err("%s: Invalid parameter\n", __func__);
			return err;
		}
		pr_debug("%s: Frequency=%d\n", __func__, *frequency);
	}
	return 0;
}

static int tda8261_set_frequency(struct dvb_frontend *fe, u32 frequency)
{
	struct dvb_frontend_ops	*frontend_ops = &fe->ops;
	struct dvb_tuner_ops	*tuner_ops = &frontend_ops->tuner_ops;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int err = 0;

	if (tuner_ops->set_params) {
		err = tuner_ops->set_params(fe);
		if (err < 0) {
			pr_err("%s: Invalid parameter\n", __func__);
			return err;
		}
	}
	pr_debug("%s: Frequency=%d\n", __func__, c->frequency);
	return 0;
}

static int tda8261_get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	/* FIXME! need to calculate Bandwidth */
	*bandwidth = 40000000;

	return 0;
}

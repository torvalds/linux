/*
	TDA8261 8PSK/QPSK tuner driver
	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

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

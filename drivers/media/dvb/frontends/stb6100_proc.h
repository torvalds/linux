/*
	STB6100 Silicon Tuner wrapper
	Copyright (C)2009 Igor M. Liplianin (liplianin@me.by)

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

static int stb6100_get_freq(struct dvb_frontend *fe, u32 *frequency)
{
	struct dvb_frontend_ops	*frontend_ops = NULL;
	struct dvb_tuner_ops	*tuner_ops = NULL;
	struct tuner_state	state;
	int err = 0;

	if (&fe->ops)
		frontend_ops = &fe->ops;
	if (&frontend_ops->tuner_ops)
		tuner_ops = &frontend_ops->tuner_ops;
	if (tuner_ops->get_state) {
		if (frontend_ops->i2c_gate_ctrl)
			frontend_ops->i2c_gate_ctrl(fe, 1);

		err = tuner_ops->get_state(fe, DVBFE_TUNER_FREQUENCY, &state);
		if (err < 0) {
			printk(KERN_ERR "%s: Invalid parameter\n", __func__);
			return err;
		}

		if (frontend_ops->i2c_gate_ctrl)
			frontend_ops->i2c_gate_ctrl(fe, 0);

		*frequency = state.frequency;
	}

	return 0;
}

static int stb6100_set_freq(struct dvb_frontend *fe, u32 frequency)
{
	struct dvb_frontend_ops	*frontend_ops = NULL;
	struct dvb_tuner_ops	*tuner_ops = NULL;
	struct tuner_state	state;
	int err = 0;

	state.frequency = frequency;
	if (&fe->ops)
		frontend_ops = &fe->ops;
	if (&frontend_ops->tuner_ops)
		tuner_ops = &frontend_ops->tuner_ops;
	if (tuner_ops->set_state) {
		if (frontend_ops->i2c_gate_ctrl)
			frontend_ops->i2c_gate_ctrl(fe, 1);

		err = tuner_ops->set_state(fe, DVBFE_TUNER_FREQUENCY, &state);
		if (err < 0) {
			printk(KERN_ERR "%s: Invalid parameter\n", __func__);
			return err;
		}

		if (frontend_ops->i2c_gate_ctrl)
			frontend_ops->i2c_gate_ctrl(fe, 0);

	}

	return 0;
}

static int stb6100_get_bandw(struct dvb_frontend *fe, u32 *bandwidth)
{
	struct dvb_frontend_ops	*frontend_ops = NULL;
	struct dvb_tuner_ops	*tuner_ops = NULL;
	struct tuner_state	state;
	int err = 0;

	if (&fe->ops)
		frontend_ops = &fe->ops;
	if (&frontend_ops->tuner_ops)
		tuner_ops = &frontend_ops->tuner_ops;
	if (tuner_ops->get_state) {
		if (frontend_ops->i2c_gate_ctrl)
			frontend_ops->i2c_gate_ctrl(fe, 1);

		err = tuner_ops->get_state(fe, DVBFE_TUNER_BANDWIDTH, &state);
		if (err < 0) {
			printk(KERN_ERR "%s: Invalid parameter\n", __func__);
			return err;
		}

		if (frontend_ops->i2c_gate_ctrl)
			frontend_ops->i2c_gate_ctrl(fe, 0);

		*bandwidth = state.bandwidth;
	}

	return 0;
}

static int stb6100_set_bandw(struct dvb_frontend *fe, u32 bandwidth)
{
	struct dvb_frontend_ops	*frontend_ops = NULL;
	struct dvb_tuner_ops	*tuner_ops = NULL;
	struct tuner_state	state;
	int err = 0;

	state.bandwidth = bandwidth;
	if (&fe->ops)
		frontend_ops = &fe->ops;
	if (&frontend_ops->tuner_ops)
		tuner_ops = &frontend_ops->tuner_ops;
	if (tuner_ops->set_state) {
		if (frontend_ops->i2c_gate_ctrl)
			frontend_ops->i2c_gate_ctrl(fe, 1);

		err = tuner_ops->set_state(fe, DVBFE_TUNER_BANDWIDTH, &state);
		if (err < 0) {
			printk(KERN_ERR "%s: Invalid parameter\n", __func__);
			return err;
		}

		if (frontend_ops->i2c_gate_ctrl)
			frontend_ops->i2c_gate_ctrl(fe, 0);

	}

	return 0;
}

// SPDX-License-Identifier: GPL-2.0
/*
 * ddbridge-mci.c: Digital Devices microcode interface
 *
 * Copyright (C) 2017 Digital Devices GmbH
 *                    Ralph Metzler <rjkm@metzlerbros.de>
 *                    Marcus Metzler <mocm@metzlerbros.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include "ddbridge.h"
#include "ddbridge-io.h"
#include "ddbridge-mci.h"

static LIST_HEAD(mci_list);

static const u32 MCLK = (1550000000 / 12);
static const u32 MAX_DEMOD_LDPC_BITRATE = (1550000000 / 6);
static const u32 MAX_LDPC_BITRATE = (720000000);

struct mci_base {
	struct list_head     mci_list;
	void                *key;
	struct ddb_link     *link;
	struct completion    completion;

	struct device       *dev;
	struct mutex         tuner_lock; /* concurrent tuner access lock */
	u8                   adr;
	struct mutex         mci_lock; /* concurrent MCI access lock */
	int                  count;

	u8                   tuner_use_count[4];
	u8                   assigned_demod[8];
	u32                  used_ldpc_bitrate[8];
	u8                   demod_in_use[8];
	u32                  iq_mode;
};

struct mci {
	struct mci_base     *base;
	struct dvb_frontend  fe;
	int                  nr;
	int                  demod;
	int                  tuner;
	int                  first_time_lock;
	int                  started;
	struct mci_result    signal_info;

	u32                  bb_mode;
};

static int mci_reset(struct mci *state)
{
	struct ddb_link *link = state->base->link;
	u32 status = 0;
	u32 timeout = 40;

	ddblwritel(link, MCI_CONTROL_RESET, MCI_CONTROL);
	ddblwritel(link, 0, MCI_CONTROL + 4); /* 1= no internal init */
	msleep(300);
	ddblwritel(link, 0, MCI_CONTROL);

	while (1) {
		status = ddblreadl(link, MCI_CONTROL);
		if ((status & MCI_CONTROL_READY) == MCI_CONTROL_READY)
			break;
		if (--timeout == 0)
			break;
		msleep(50);
	}
	if ((status & MCI_CONTROL_READY) == 0)
		return -1;
	if (link->ids.device == 0x0009)
		ddblwritel(link, SX8_TSCONFIG_MODE_NORMAL, SX8_TSCONFIG);
	return 0;
}

static int mci_config(struct mci *state, u32 config)
{
	struct ddb_link *link = state->base->link;

	if (link->ids.device != 0x0009)
		return -EINVAL;
	ddblwritel(link, config, SX8_TSCONFIG);
	return 0;
}

static int _mci_cmd_unlocked(struct mci *state,
			     u32 *cmd, u32 cmd_len,
			     u32 *res, u32 res_len)
{
	struct ddb_link *link = state->base->link;
	u32 i, val;
	unsigned long stat;

	val = ddblreadl(link, MCI_CONTROL);
	if (val & (MCI_CONTROL_RESET | MCI_CONTROL_START_COMMAND))
		return -EIO;
	if (cmd && cmd_len)
		for (i = 0; i < cmd_len; i++)
			ddblwritel(link, cmd[i], MCI_COMMAND + i * 4);
	val |= (MCI_CONTROL_START_COMMAND | MCI_CONTROL_ENABLE_DONE_INTERRUPT);
	ddblwritel(link, val, MCI_CONTROL);

	stat = wait_for_completion_timeout(&state->base->completion, HZ);
	if (stat == 0) {
		dev_warn(state->base->dev, "MCI-%d: MCI timeout\n", state->nr);
		return -EIO;
	}
	if (res && res_len)
		for (i = 0; i < res_len; i++)
			res[i] = ddblreadl(link, MCI_RESULT + i * 4);
	return 0;
}

static int mci_cmd(struct mci *state,
		   struct mci_command *command,
		   struct mci_result *result)
{
	int stat;

	mutex_lock(&state->base->mci_lock);
	stat = _mci_cmd_unlocked(state,
				 (u32 *)command, sizeof(*command) / sizeof(u32),
				 (u32 *)result, sizeof(*result) / sizeof(u32));
	mutex_unlock(&state->base->mci_lock);
	return stat;
}

static void mci_handler(void *priv)
{
	struct mci_base *base = (struct mci_base *)priv;

	complete(&base->completion);
}

static void release(struct dvb_frontend *fe)
{
	struct mci *state = fe->demodulator_priv;

	state->base->count--;
	if (state->base->count == 0) {
		list_del(&state->base->mci_list);
		kfree(state->base);
	}
	kfree(state);
}

static int read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	int stat;
	struct mci *state = fe->demodulator_priv;
	struct mci_command cmd;
	struct mci_result res;

	cmd.command = MCI_CMD_GETSTATUS;
	cmd.demod = state->demod;
	stat = mci_cmd(state, &cmd, &res);
	if (stat)
		return stat;
	*status = 0x00;
	if (res.status == SX8_DEMOD_WAIT_MATYPE)
		*status = 0x0f;
	if (res.status == SX8_DEMOD_LOCKED)
		*status = 0x1f;
	return stat;
}

static int mci_set_tuner(struct dvb_frontend *fe, u32 tuner, u32 on)
{
	struct mci *state = fe->demodulator_priv;
	struct mci_command cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.tuner = state->tuner;
	cmd.command = on ? SX8_CMD_INPUT_ENABLE : SX8_CMD_INPUT_DISABLE;
	return mci_cmd(state, &cmd, NULL);
}

static int stop(struct dvb_frontend *fe)
{
	struct mci *state = fe->demodulator_priv;
	struct mci_command cmd;
	u32 input = state->tuner;

	memset(&cmd, 0, sizeof(cmd));
	if (state->demod != 0xff) {
		cmd.command = MCI_CMD_STOP;
		cmd.demod = state->demod;
		mci_cmd(state, &cmd, NULL);
		if (state->base->iq_mode) {
			cmd.command = MCI_CMD_STOP;
			cmd.demod = state->demod;
			cmd.output = 0;
			mci_cmd(state, &cmd, NULL);
			mci_config(state, SX8_TSCONFIG_MODE_NORMAL);
		}
	}
	mutex_lock(&state->base->tuner_lock);
	state->base->tuner_use_count[input]--;
	if (!state->base->tuner_use_count[input])
		mci_set_tuner(fe, input, 0);
	state->base->demod_in_use[state->demod] = 0;
	state->base->used_ldpc_bitrate[state->nr] = 0;
	state->demod = 0xff;
	state->base->assigned_demod[state->nr] = 0xff;
	state->base->iq_mode = 0;
	mutex_unlock(&state->base->tuner_lock);
	state->started = 0;
	return 0;
}

static int start(struct dvb_frontend *fe, u32 flags, u32 modmask, u32 ts_config)
{
	struct mci *state = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	u32 used_ldpc_bitrate = 0, free_ldpc_bitrate;
	u32 used_demods = 0;
	struct mci_command cmd;
	u32 input = state->tuner;
	u32 bits_per_symbol = 0;
	int i, stat = 0;

	if (p->symbol_rate >= (MCLK / 2))
		flags &= ~1;
	if ((flags & 3) == 0)
		return -EINVAL;

	if (flags & 2) {
		u32 tmp = modmask;

		bits_per_symbol = 1;
		while (tmp & 1) {
			tmp >>= 1;
			bits_per_symbol++;
		}
	}

	mutex_lock(&state->base->tuner_lock);
	if (state->base->iq_mode) {
		stat = -EBUSY;
		goto unlock;
	}
	for (i = 0; i < 8; i++) {
		used_ldpc_bitrate += state->base->used_ldpc_bitrate[i];
		if (state->base->demod_in_use[i])
			used_demods++;
	}
	if (used_ldpc_bitrate >= MAX_LDPC_BITRATE ||
	    ((ts_config & SX8_TSCONFIG_MODE_MASK) >
	     SX8_TSCONFIG_MODE_NORMAL && used_demods > 0)) {
		stat = -EBUSY;
		goto unlock;
	}
	free_ldpc_bitrate = MAX_LDPC_BITRATE - used_ldpc_bitrate;
	if (free_ldpc_bitrate > MAX_DEMOD_LDPC_BITRATE)
		free_ldpc_bitrate = MAX_DEMOD_LDPC_BITRATE;

	while (p->symbol_rate * bits_per_symbol > free_ldpc_bitrate)
		bits_per_symbol--;

	if (bits_per_symbol < 2) {
		stat = -EBUSY;
		goto unlock;
	}
	i = (p->symbol_rate > (MCLK / 2)) ? 3 : 7;
	while (i >= 0 && state->base->demod_in_use[i])
		i--;
	if (i < 0) {
		stat = -EBUSY;
		goto unlock;
	}
	state->base->demod_in_use[i] = 1;
	state->base->used_ldpc_bitrate[state->nr] = p->symbol_rate
						    * bits_per_symbol;
	state->demod = i;
	state->base->assigned_demod[state->nr] = i;

	if (!state->base->tuner_use_count[input])
		mci_set_tuner(fe, input, 1);
	state->base->tuner_use_count[input]++;
	state->base->iq_mode = (ts_config > 1);
unlock:
	mutex_unlock(&state->base->tuner_lock);
	if (stat)
		return stat;
	memset(&cmd, 0, sizeof(cmd));

	if (state->base->iq_mode) {
		cmd.command = SX8_CMD_SELECT_IQOUT;
		cmd.demod = state->demod;
		cmd.output = 0;
		mci_cmd(state, &cmd, NULL);
		mci_config(state, ts_config);
	}
	if (p->stream_id != NO_STREAM_ID_FILTER && p->stream_id != 0x80000000)
		flags |= 0x80;
	dev_dbg(state->base->dev, "MCI-%d: tuner=%d demod=%d\n",
		state->nr, state->tuner, state->demod);
	cmd.command = MCI_CMD_SEARCH_DVBS;
	cmd.dvbs2_search.flags = flags;
	cmd.dvbs2_search.s2_modulation_mask =
		modmask & ((1 << (bits_per_symbol - 1)) - 1);
	cmd.dvbs2_search.retry = 2;
	cmd.dvbs2_search.frequency = p->frequency * 1000;
	cmd.dvbs2_search.symbol_rate = p->symbol_rate;
	cmd.dvbs2_search.scrambling_sequence_index =
		p->scrambling_sequence_index;
	cmd.dvbs2_search.input_stream_id =
		(p->stream_id != NO_STREAM_ID_FILTER) ? p->stream_id : 0;
	cmd.tuner = state->tuner;
	cmd.demod = state->demod;
	cmd.output = state->nr;
	if (p->stream_id == 0x80000000)
		cmd.output |= 0x80;
	stat = mci_cmd(state, &cmd, NULL);
	if (stat)
		stop(fe);
	return stat;
}

static int start_iq(struct dvb_frontend *fe, u32 ts_config)
{
	struct mci *state = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	u32 used_demods = 0;
	struct mci_command cmd;
	u32 input = state->tuner;
	int i, stat = 0;

	mutex_lock(&state->base->tuner_lock);
	if (state->base->iq_mode) {
		stat = -EBUSY;
		goto unlock;
	}
	for (i = 0; i < 8; i++)
		if (state->base->demod_in_use[i])
			used_demods++;
	if (used_demods > 0) {
		stat = -EBUSY;
		goto unlock;
	}
	state->demod = 0;
	state->base->assigned_demod[state->nr] = 0;
	if (!state->base->tuner_use_count[input])
		mci_set_tuner(fe, input, 1);
	state->base->tuner_use_count[input]++;
	state->base->iq_mode = (ts_config > 1);
unlock:
	mutex_unlock(&state->base->tuner_lock);
	if (stat)
		return stat;

	memset(&cmd, 0, sizeof(cmd));
	cmd.command = SX8_CMD_START_IQ;
	cmd.dvbs2_search.frequency = p->frequency * 1000;
	cmd.dvbs2_search.symbol_rate = p->symbol_rate;
	cmd.tuner = state->tuner;
	cmd.demod = state->demod;
	cmd.output = 7;
	mci_config(state, ts_config);
	stat = mci_cmd(state, &cmd, NULL);
	if (stat)
		stop(fe);
	return stat;
}

static int set_parameters(struct dvb_frontend *fe)
{
	int stat = 0;
	struct mci *state = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	u32 ts_config, iq_mode = 0, isi;

	if (state->started)
		stop(fe);

	isi = p->stream_id;
	if (isi != NO_STREAM_ID_FILTER)
		iq_mode = (isi & 0x30000000) >> 28;

	switch (iq_mode) {
	case 1:
		ts_config = (SX8_TSCONFIG_TSHEADER | SX8_TSCONFIG_MODE_IQ);
		break;
	case 2:
		ts_config = (SX8_TSCONFIG_TSHEADER | SX8_TSCONFIG_MODE_IQ);
		break;
	default:
		ts_config = SX8_TSCONFIG_MODE_NORMAL;
		break;
	}

	if (iq_mode != 2) {
		u32 flags = 3;
		u32 mask = 3;

		if (p->modulation == APSK_16 ||
		    p->modulation == APSK_32) {
			flags = 2;
			mask = 15;
		}
		stat = start(fe, flags, mask, ts_config);
	} else {
		stat = start_iq(fe, ts_config);
	}

	if (!stat) {
		state->started = 1;
		state->first_time_lock = 1;
		state->signal_info.status = SX8_DEMOD_WAIT_SIGNAL;
	}

	return stat;
}

static int tune(struct dvb_frontend *fe, bool re_tune,
		unsigned int mode_flags,
		unsigned int *delay, enum fe_status *status)
{
	int r;

	if (re_tune) {
		r = set_parameters(fe);
		if (r)
			return r;
	}
	r = read_status(fe, status);
	if (r)
		return r;

	if (*status & FE_HAS_LOCK)
		return 0;
	*delay = HZ / 10;
	return 0;
}

static enum dvbfe_algo get_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_HW;
}

static int set_input(struct dvb_frontend *fe, int input)
{
	struct mci *state = fe->demodulator_priv;

	state->tuner = input;
	dev_dbg(state->base->dev, "MCI-%d: input=%d\n", state->nr, input);
	return 0;
}

static struct dvb_frontend_ops mci_ops = {
	.delsys = { SYS_DVBS, SYS_DVBS2 },
	.info = {
		.name			= "Digital Devices MaxSX8 MCI DVB-S/S2/S2X",
		.frequency_min		= 950000,
		.frequency_max		= 2150000,
		.frequency_stepsize	= 0,
		.frequency_tolerance	= 0,
		.symbol_rate_min	= 100000,
		.symbol_rate_max	= 100000000,
		.caps			= FE_CAN_INVERSION_AUTO |
					  FE_CAN_FEC_AUTO       |
					  FE_CAN_QPSK           |
					  FE_CAN_2G_MODULATION  |
					  FE_CAN_MULTISTREAM,
	},
	.get_frontend_algo		= get_algo,
	.tune				= tune,
	.release			= release,
	.read_status			= read_status,
};

static struct mci_base *match_base(void *key)
{
	struct mci_base *p;

	list_for_each_entry(p, &mci_list, mci_list)
		if (p->key == key)
			return p;
	return NULL;
}

static int probe(struct mci *state)
{
	mci_reset(state);
	return 0;
}

struct dvb_frontend
*ddb_mci_attach(struct ddb_input *input,
		int mci_type, int nr,
		int (**fn_set_input)(struct dvb_frontend *, int))
{
	struct ddb_port *port = input->port;
	struct ddb *dev = port->dev;
	struct ddb_link *link = &dev->link[port->lnr];
	struct mci_base *base;
	struct mci *state;
	void *key = mci_type ? (void *)port : (void *)link;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	base = match_base(key);
	if (base) {
		base->count++;
		state->base = base;
	} else {
		base = kzalloc(sizeof(*base), GFP_KERNEL);
		if (!base)
			goto fail;
		base->key = key;
		base->count = 1;
		base->link = link;
		base->dev = dev->dev;
		mutex_init(&base->mci_lock);
		mutex_init(&base->tuner_lock);
		ddb_irq_set(dev, link->nr, 0, mci_handler, base);
		init_completion(&base->completion);
		state->base = base;
		if (probe(state) < 0) {
			kfree(base);
			goto fail;
		}
		list_add(&base->mci_list, &mci_list);
	}
	state->fe.ops = mci_ops;
	state->fe.demodulator_priv = state;
	state->nr = nr;
	*fn_set_input = set_input;

	state->tuner = nr;
	state->demod = nr;

	return &state->fe;
fail:
	kfree(state);
	return NULL;
}

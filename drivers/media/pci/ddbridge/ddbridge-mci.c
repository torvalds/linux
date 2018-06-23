// SPDX-License-Identifier: GPL-2.0
/*
 * ddbridge-mci.c: Digital Devices microcode interface
 *
 * Copyright (C) 2017-2018 Digital Devices GmbH
 *                         Ralph Metzler <rjkm@metzlerbros.de>
 *                         Marcus Metzler <mocm@metzlerbros.de>
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

int ddb_mci_config(struct mci *state, u32 config)
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

int ddb_mci_cmd(struct mci *state,
		struct mci_command *command,
		struct mci_result *result)
{
	int stat;

	mutex_lock(&state->base->mci_lock);
	stat = _mci_cmd_unlocked(state,
				 (u32 *)command, sizeof(*command) / sizeof(u32),
				 (u32 *)result,	sizeof(*result) / sizeof(u32));
	mutex_unlock(&state->base->mci_lock);
	return stat;
}

static void mci_handler(void *priv)
{
	struct mci_base *base = (struct mci_base *)priv;

	complete(&base->completion);
}

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
*ddb_mci_attach(struct ddb_input *input, struct mci_cfg *cfg, int nr,
		int (**fn_set_input)(struct dvb_frontend *fe, int input))
{
	struct ddb_port *port = input->port;
	struct ddb *dev = port->dev;
	struct ddb_link *link = &dev->link[port->lnr];
	struct mci_base *base;
	struct mci *state;
	void *key = cfg->type ? (void *)port : (void *)link;

	state = kzalloc(cfg->state_size, GFP_KERNEL);
	if (!state)
		return NULL;

	base = match_base(key);
	if (base) {
		base->count++;
		state->base = base;
	} else {
		base = kzalloc(cfg->base_size, GFP_KERNEL);
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
		if (cfg->base_init)
			cfg->base_init(base);
	}
	memcpy(&state->fe.ops, cfg->fe_ops, sizeof(struct dvb_frontend_ops));
	state->fe.demodulator_priv = state;
	state->nr = nr;
	*fn_set_input = cfg->set_input;
	state->tuner = nr;
	state->demod = nr;
	if (cfg->init)
		cfg->init(state);
	return &state->fe;
fail:
	kfree(state);
	return NULL;
}

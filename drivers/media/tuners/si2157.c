/*
 * Silicon Labs Si2157 silicon tuner driver
 *
 * Copyright (C) 2014 Antti Palosaari <crope@iki.fi>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 */

#include "si2157_priv.h"

/* execute firmware command */
static int si2157_cmd_execute(struct si2157 *s, struct si2157_cmd *cmd)
{
	int ret;
	unsigned long timeout;

	mutex_lock(&s->i2c_mutex);

	if (cmd->wlen) {
		/* write cmd and args for firmware */
		ret = i2c_master_send(s->client, cmd->args, cmd->wlen);
		if (ret < 0) {
			goto err_mutex_unlock;
		} else if (ret != cmd->wlen) {
			ret = -EREMOTEIO;
			goto err_mutex_unlock;
		}
	}

	if (cmd->rlen) {
		/* wait cmd execution terminate */
		#define TIMEOUT 80
		timeout = jiffies + msecs_to_jiffies(TIMEOUT);
		while (!time_after(jiffies, timeout)) {
			ret = i2c_master_recv(s->client, cmd->args, cmd->rlen);
			if (ret < 0) {
				goto err_mutex_unlock;
			} else if (ret != cmd->rlen) {
				ret = -EREMOTEIO;
				goto err_mutex_unlock;
			}

			/* firmware ready? */
			if ((cmd->args[0] >> 7) & 0x01)
				break;
		}

		dev_dbg(&s->client->dev, "%s: cmd execution took %d ms\n",
				__func__,
				jiffies_to_msecs(jiffies) -
				(jiffies_to_msecs(timeout) - TIMEOUT));

		if (!((cmd->args[0] >> 7) & 0x01)) {
			ret = -ETIMEDOUT;
			goto err_mutex_unlock;
		}
	}

	ret = 0;

err_mutex_unlock:
	mutex_unlock(&s->i2c_mutex);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&s->client->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int si2157_init(struct dvb_frontend *fe)
{
	struct si2157 *s = fe->tuner_priv;

	dev_dbg(&s->client->dev, "%s:\n", __func__);

	s->active = true;

	return 0;
}

static int si2157_sleep(struct dvb_frontend *fe)
{
	struct si2157 *s = fe->tuner_priv;
	int ret;
	struct si2157_cmd cmd;

	dev_dbg(&s->client->dev, "%s:\n", __func__);

	s->active = false;

	memcpy(cmd.args, "\x13", 1);
	cmd.wlen = 1;
	cmd.rlen = 0;
	ret = si2157_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&s->client->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static int si2157_set_params(struct dvb_frontend *fe)
{
	struct si2157 *s = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret;
	struct si2157_cmd cmd;

	dev_dbg(&s->client->dev,
			"%s: delivery_system=%d frequency=%u bandwidth_hz=%u\n",
			__func__, c->delivery_system, c->frequency,
			c->bandwidth_hz);

	if (!s->active) {
		ret = -EAGAIN;
		goto err;
	}

	/* configure? */
	cmd.args[0] = 0xc0;
	cmd.args[1] = 0x00;
	cmd.args[2] = 0x0c;
	cmd.args[3] = 0x00;
	cmd.args[4] = 0x00;
	cmd.args[5] = 0x01;
	cmd.args[6] = 0x01;
	cmd.args[7] = 0x01;
	cmd.args[8] = 0x01;
	cmd.args[9] = 0x01;
	cmd.args[10] = 0x01;
	cmd.args[11] = 0x02;
	cmd.args[12] = 0x00;
	cmd.args[13] = 0x00;
	cmd.args[14] = 0x01;
	cmd.wlen = 15;
	cmd.rlen = 1;
	ret = si2157_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	cmd.args[0] = 0x02;
	cmd.wlen = 1;
	cmd.rlen = 13;
	ret = si2157_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	cmd.args[0] = 0x01;
	cmd.args[1] = 0x01;
	cmd.wlen = 2;
	cmd.rlen = 1;
	ret = si2157_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	/* set frequency */
	cmd.args[0] = 0x41;
	cmd.args[1] = 0x00;
	cmd.args[2] = 0x00;
	cmd.args[3] = 0x00;
	cmd.args[4] = (c->frequency >>  0) & 0xff;
	cmd.args[5] = (c->frequency >>  8) & 0xff;
	cmd.args[6] = (c->frequency >> 16) & 0xff;
	cmd.args[7] = (c->frequency >> 24) & 0xff;
	cmd.wlen = 8;
	cmd.rlen = 1;
	ret = si2157_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	return 0;
err:
	dev_dbg(&s->client->dev, "%s: failed=%d\n", __func__, ret);
	return ret;
}

static const struct dvb_tuner_ops si2157_tuner_ops = {
	.info = {
		.name           = "Silicon Labs Si2157",
		.frequency_min  = 110000000,
		.frequency_max  = 862000000,
	},

	.init = si2157_init,
	.sleep = si2157_sleep,
	.set_params = si2157_set_params,
};

static int si2157_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct si2157_config *cfg = client->dev.platform_data;
	struct dvb_frontend *fe = cfg->fe;
	struct si2157 *s;
	struct si2157_cmd cmd;
	int ret;

	s = kzalloc(sizeof(struct si2157), GFP_KERNEL);
	if (!s) {
		ret = -ENOMEM;
		dev_err(&client->dev, "%s: kzalloc() failed\n", KBUILD_MODNAME);
		goto err;
	}

	s->client = client;
	s->fe = cfg->fe;
	mutex_init(&s->i2c_mutex);

	/* check if the tuner is there */
	cmd.wlen = 0;
	cmd.rlen = 1;
	ret = si2157_cmd_execute(s, &cmd);
	if (ret)
		goto err;

	fe->tuner_priv = s;
	memcpy(&fe->ops.tuner_ops, &si2157_tuner_ops,
			sizeof(struct dvb_tuner_ops));

	i2c_set_clientdata(client, s);

	dev_info(&s->client->dev,
			"%s: Silicon Labs Si2157 successfully attached\n",
			KBUILD_MODNAME);
	return 0;
err:
	dev_dbg(&client->dev, "%s: failed=%d\n", __func__, ret);
	kfree(s);

	return ret;
}

static int si2157_remove(struct i2c_client *client)
{
	struct si2157 *s = i2c_get_clientdata(client);
	struct dvb_frontend *fe = s->fe;

	dev_dbg(&client->dev, "%s:\n", __func__);

	memset(&fe->ops.tuner_ops, 0, sizeof(struct dvb_tuner_ops));
	fe->tuner_priv = NULL;
	kfree(s);

	return 0;
}

static const struct i2c_device_id si2157_id[] = {
	{"si2157", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, si2157_id);

static struct i2c_driver si2157_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "si2157",
	},
	.probe		= si2157_probe,
	.remove		= si2157_remove,
	.id_table	= si2157_id,
};

module_i2c_driver(si2157_driver);

MODULE_DESCRIPTION("Silicon Labs Si2157 silicon tuner driver");
MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_LICENSE("GPL");

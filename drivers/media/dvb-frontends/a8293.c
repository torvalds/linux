// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Allegro A8293 SEC driver
 *
 * Copyright (C) 2011 Antti Palosaari <crope@iki.fi>
 */

#include "a8293.h"

#define A8293_FLAG_ODT			0x10

struct a8293_dev {
	struct i2c_client *client;
	u8 reg[2];
	int volt_slew_nanos_per_mv;
};

/*
 * When increasing voltage, do so in minimal steps over time, minimizing
 * risk of vIN undervoltage.
 */

static int a8293_set_voltage_slew(struct a8293_dev *dev,
				  struct i2c_client *client,
				  enum fe_sec_voltage fe_sec_voltage,
				  int min_nanos_per_mv)
{
	int ret;
	u8 reg0, reg1;
	int new_volt_idx;
	const int idx_to_mv[] = {
		0,    12709, 13042, 13375, 14042, 15042, 18042, 18709, 19042
	};
	const u8 idx_to_reg[] = {
		0x00, 0x20,  0x21,  0x22,  0x24,  0x27,  0x28,  0x2A,  0x2B
	};
	int this_volt_idx;
	u8 status;
	int prev_volt_idx;

	dev_dbg(&client->dev, "set_voltage_slew fe_sec_voltage=%d\n",
		fe_sec_voltage);

	/* Read status register to clear any stale faults. */
	ret = i2c_master_recv(client, &status, 1);
	if (ret < 0)
		goto err;

	/* Determine previous voltage */
	switch (dev->reg[0] & 0x2F) {
	case 0x00:
		prev_volt_idx = 0;
		break;
	case 0x20:
		prev_volt_idx = 1;
		break;
	case 0x21:
		prev_volt_idx = 2;
		break;
	case 0x22:
		prev_volt_idx = 3;
		break;
	case 0x24:
		prev_volt_idx = 4;
		break;
	case 0x27:
		prev_volt_idx = 5;
		break;
	case 0x28:
		prev_volt_idx = 6;
		break;
	case 0x2A:
		prev_volt_idx = 7;
		break;
	case 0x2B:
		prev_volt_idx = 8;
		break;
	default:
		prev_volt_idx = 0;
	}

	/* Determine new voltage */
	switch (fe_sec_voltage) {
	case SEC_VOLTAGE_OFF:
		new_volt_idx = 0;
		break;
	case SEC_VOLTAGE_13:
		new_volt_idx = 2;
		break;
	case SEC_VOLTAGE_18:
		new_volt_idx = 6;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}

	/* Slew to new voltage if new voltage is greater than current voltage */
	this_volt_idx = prev_volt_idx;
	if (this_volt_idx < new_volt_idx) {
		while (this_volt_idx < new_volt_idx) {
			int delta_mv = idx_to_mv[this_volt_idx+1] - idx_to_mv[this_volt_idx];
			int min_wait_time = delta_mv * min_nanos_per_mv;

			reg0 = idx_to_reg[this_volt_idx+1];
			reg0 |= A8293_FLAG_ODT;

			ret = i2c_master_send(client, &reg0, 1);
			if (ret < 0)
				goto err;
			dev->reg[0] = reg0;
			this_volt_idx++;
			usleep_range(min_wait_time, min_wait_time * 2);
		}
	} else { /* Else just set the voltage */
		reg0 = idx_to_reg[new_volt_idx];
		reg0 |= A8293_FLAG_ODT;
		ret = i2c_master_send(client, &reg0, 1);
		if (ret < 0)
			goto err;
		dev->reg[0] = reg0;
	}

	/* TMODE=0, TGATE=1 */
	reg1 = 0x82;
	if (reg1 != dev->reg[1]) {
		ret = i2c_master_send(client, &reg1, 1);
		if (ret < 0)
			goto err;
		dev->reg[1] = reg1;
	}

	usleep_range(1500, 5000);

	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}


static int a8293_set_voltage_noslew(struct dvb_frontend *fe,
				    enum fe_sec_voltage fe_sec_voltage)
{
	struct a8293_dev *dev = fe->sec_priv;
	struct i2c_client *client = dev->client;
	int ret;
	u8 reg0, reg1;

	dev_dbg(&client->dev, "set_voltage_noslew fe_sec_voltage=%d\n",
		fe_sec_voltage);

	switch (fe_sec_voltage) {
	case SEC_VOLTAGE_OFF:
		/* ENB=0 */
		reg0 = 0x10;
		break;
	case SEC_VOLTAGE_13:
		/* VSEL0=1, VSEL1=0, VSEL2=0, VSEL3=0, ENB=1*/
		reg0 = 0x31;
		break;
	case SEC_VOLTAGE_18:
		/* VSEL0=0, VSEL1=0, VSEL2=0, VSEL3=1, ENB=1*/
		reg0 = 0x38;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}
	if (reg0 != dev->reg[0]) {
		ret = i2c_master_send(client, &reg0, 1);
		if (ret < 0)
			goto err;
		dev->reg[0] = reg0;
	}

	/* TMODE=0, TGATE=1 */
	reg1 = 0x82;
	if (reg1 != dev->reg[1]) {
		ret = i2c_master_send(client, &reg1, 1);
		if (ret < 0)
			goto err;
		dev->reg[1] = reg1;
	}

	usleep_range(1500, 50000);
	return 0;
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static int a8293_set_voltage(struct dvb_frontend *fe,
			     enum fe_sec_voltage fe_sec_voltage)
{
	struct a8293_dev *dev = fe->sec_priv;
	struct i2c_client *client = dev->client;
	int volt_slew_nanos_per_mv = dev->volt_slew_nanos_per_mv;

	dev_dbg(&client->dev, "set_voltage volt_slew_nanos_per_mv=%d\n",
		volt_slew_nanos_per_mv);

	/* Use slew version if slew rate is set to a sane value */
	if (volt_slew_nanos_per_mv > 0 && volt_slew_nanos_per_mv < 1600)
		a8293_set_voltage_slew(dev, client, fe_sec_voltage,
				       volt_slew_nanos_per_mv);
	else
		a8293_set_voltage_noslew(fe, fe_sec_voltage);

	return 0;
}

static int a8293_probe(struct i2c_client *client)
{
	struct a8293_dev *dev;
	struct a8293_platform_data *pdata = client->dev.platform_data;
	struct dvb_frontend *fe = pdata->dvb_frontend;
	int ret;
	u8 buf[2];

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto err;
	}

	dev->client = client;
	dev->volt_slew_nanos_per_mv = pdata->volt_slew_nanos_per_mv;

	/* check if the SEC is there */
	ret = i2c_master_recv(client, buf, 2);
	if (ret < 0)
		goto err_kfree;

	/* override frontend ops */
	fe->ops.set_voltage = a8293_set_voltage;
	fe->sec_priv = dev;
	i2c_set_clientdata(client, dev);

	dev_info(&client->dev, "Allegro A8293 SEC successfully attached\n");
	return 0;
err_kfree:
	kfree(dev);
err:
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static void a8293_remove(struct i2c_client *client)
{
	struct a8293_dev *dev = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	kfree(dev);
}

static const struct i2c_device_id a8293_id_table[] = {
	{"a8293", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, a8293_id_table);

static struct i2c_driver a8293_driver = {
	.driver = {
		.name	= "a8293",
		.suppress_bind_attrs = true,
	},
	.probe_new	= a8293_probe,
	.remove		= a8293_remove,
	.id_table	= a8293_id_table,
};

module_i2c_driver(a8293_driver);

MODULE_AUTHOR("Antti Palosaari <crope@iki.fi>");
MODULE_DESCRIPTION("Allegro A8293 SEC driver");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-only
/*
 * I2C slave mode testunit
 *
 * Copyright (C) 2020 by Wolfram Sang, Sang Engineering <wsa@sang-engineering.com>
 * Copyright (C) 2020 by Renesas Electronics Corporation
 */

#include <linux/bitops.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/workqueue.h> /* FIXME: is system_long_wq the best choice? */

#define TU_CUR_VERSION 0x01

enum testunit_cmds {
	TU_CMD_READ_BYTES = 1,	/* save 0 for ABORT, RESET or similar */
	TU_CMD_HOST_NOTIFY,
	TU_CMD_SMBUS_BLOCK_PROC_CALL,
	TU_NUM_CMDS
};

enum testunit_regs {
	TU_REG_CMD,
	TU_REG_DATAL,
	TU_REG_DATAH,
	TU_REG_DELAY,
	TU_NUM_REGS
};

enum testunit_flags {
	TU_FLAG_IN_PROCESS,
};

struct testunit_data {
	unsigned long flags;
	u8 regs[TU_NUM_REGS];
	u8 reg_idx;
	struct i2c_client *client;
	struct delayed_work worker;
};

static void i2c_slave_testunit_work(struct work_struct *work)
{
	struct testunit_data *tu = container_of(work, struct testunit_data, worker.work);
	struct i2c_msg msg;
	u8 msgbuf[256];
	int ret = 0;

	msg.addr = I2C_CLIENT_END;
	msg.buf = msgbuf;

	switch (tu->regs[TU_REG_CMD]) {
	case TU_CMD_READ_BYTES:
		msg.addr = tu->regs[TU_REG_DATAL];
		msg.flags = I2C_M_RD;
		msg.len = tu->regs[TU_REG_DATAH];
		break;

	case TU_CMD_HOST_NOTIFY:
		msg.addr = 0x08;
		msg.flags = 0;
		msg.len = 3;
		msgbuf[0] = tu->client->addr;
		msgbuf[1] = tu->regs[TU_REG_DATAL];
		msgbuf[2] = tu->regs[TU_REG_DATAH];
		break;

	default:
		break;
	}

	if (msg.addr != I2C_CLIENT_END) {
		ret = i2c_transfer(tu->client->adapter, &msg, 1);
		/* convert '0 msgs transferred' to errno */
		ret = (ret == 0) ? -EIO : ret;
	}

	if (ret < 0)
		dev_err(&tu->client->dev, "CMD%02X failed (%d)\n", tu->regs[TU_REG_CMD], ret);

	clear_bit(TU_FLAG_IN_PROCESS, &tu->flags);
}

static int i2c_slave_testunit_slave_cb(struct i2c_client *client,
				     enum i2c_slave_event event, u8 *val)
{
	struct testunit_data *tu = i2c_get_clientdata(client);
	bool is_proc_call = tu->reg_idx == 3 && tu->regs[TU_REG_DATAL] == 1 &&
			    tu->regs[TU_REG_CMD] == TU_CMD_SMBUS_BLOCK_PROC_CALL;
	int ret = 0;

	switch (event) {
	case I2C_SLAVE_WRITE_RECEIVED:
		if (test_bit(TU_FLAG_IN_PROCESS, &tu->flags))
			return -EBUSY;

		if (tu->reg_idx < TU_NUM_REGS)
			tu->regs[tu->reg_idx] = *val;
		else
			ret = -EMSGSIZE;

		if (tu->reg_idx <= TU_NUM_REGS)
			tu->reg_idx++;

		/* TU_REG_CMD always written at this point */
		if (tu->regs[TU_REG_CMD] >= TU_NUM_CMDS)
			ret = -EINVAL;

		break;

	case I2C_SLAVE_STOP:
		if (tu->reg_idx == TU_NUM_REGS) {
			set_bit(TU_FLAG_IN_PROCESS, &tu->flags);
			queue_delayed_work(system_long_wq, &tu->worker,
					   msecs_to_jiffies(10 * tu->regs[TU_REG_DELAY]));
		}
		fallthrough;

	case I2C_SLAVE_WRITE_REQUESTED:
		memset(tu->regs, 0, TU_NUM_REGS);
		tu->reg_idx = 0;
		break;

	case I2C_SLAVE_READ_PROCESSED:
		if (is_proc_call && tu->regs[TU_REG_DATAH])
			tu->regs[TU_REG_DATAH]--;
		fallthrough;

	case I2C_SLAVE_READ_REQUESTED:
		*val = is_proc_call ? tu->regs[TU_REG_DATAH] : TU_CUR_VERSION;
		break;
	}

	return ret;
}

static int i2c_slave_testunit_probe(struct i2c_client *client)
{
	struct testunit_data *tu;

	tu = devm_kzalloc(&client->dev, sizeof(struct testunit_data), GFP_KERNEL);
	if (!tu)
		return -ENOMEM;

	tu->client = client;
	i2c_set_clientdata(client, tu);
	INIT_DELAYED_WORK(&tu->worker, i2c_slave_testunit_work);

	return i2c_slave_register(client, i2c_slave_testunit_slave_cb);
};

static void i2c_slave_testunit_remove(struct i2c_client *client)
{
	struct testunit_data *tu = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&tu->worker);
	i2c_slave_unregister(client);
}

static const struct i2c_device_id i2c_slave_testunit_id[] = {
	{ "slave-testunit", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, i2c_slave_testunit_id);

static struct i2c_driver i2c_slave_testunit_driver = {
	.driver = {
		.name = "i2c-slave-testunit",
	},
	.probe = i2c_slave_testunit_probe,
	.remove = i2c_slave_testunit_remove,
	.id_table = i2c_slave_testunit_id,
};
module_i2c_driver(i2c_slave_testunit_driver);

MODULE_AUTHOR("Wolfram Sang <wsa@sang-engineering.com>");
MODULE_DESCRIPTION("I2C slave mode test unit");
MODULE_LICENSE("GPL v2");

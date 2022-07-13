// SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause
/*
 * UCSI driver for STMicroelectronics STM32G0 Type-C PD controller
 *
 * Copyright (C) 2022, STMicroelectronics - All Rights Reserved
 * Author: Fabrice Gasnier <fabrice.gasnier@foss.st.com>.
 */

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "ucsi.h"

struct ucsi_stm32g0 {
	struct i2c_client *client;
	struct completion complete;
	struct device *dev;
	unsigned long flags;
	struct ucsi *ucsi;
	bool suspended;
	bool wakeup_event;
};

static int ucsi_stm32g0_read(struct ucsi *ucsi, unsigned int offset, void *val, size_t len)
{
	struct ucsi_stm32g0 *g0 = ucsi_get_drvdata(ucsi);
	struct i2c_client *client = g0->client;
	u8 reg = offset;
	struct i2c_msg msg[] = {
		{
			.addr	= client->addr,
			.flags  = 0,
			.len	= 1,
			.buf	= &reg,
		},
		{
			.addr	= client->addr,
			.flags  = I2C_M_RD,
			.len	= len,
			.buf	= val,
		},
	};
	int ret;

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret != ARRAY_SIZE(msg)) {
		dev_err(g0->dev, "i2c read %02x, %02x error: %d\n", client->addr, reg, ret);

		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

static int ucsi_stm32g0_async_write(struct ucsi *ucsi, unsigned int offset, const void *val,
				    size_t len)
{
	struct ucsi_stm32g0 *g0 = ucsi_get_drvdata(ucsi);
	struct i2c_client *client = g0->client;
	struct i2c_msg msg[] = {
		{
			.addr	= client->addr,
			.flags  = 0,
		}
	};
	unsigned char *buf;
	int ret;

	buf = kmalloc(len + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf[0] = offset;
	memcpy(&buf[1], val, len);
	msg[0].len = len + 1;
	msg[0].buf = buf;

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	kfree(buf);
	if (ret != ARRAY_SIZE(msg)) {
		dev_err(g0->dev, "i2c write %02x, %02x error: %d\n", client->addr, offset, ret);

		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

static int ucsi_stm32g0_sync_write(struct ucsi *ucsi, unsigned int offset, const void *val,
				   size_t len)
{
	struct ucsi_stm32g0 *g0 = ucsi_get_drvdata(ucsi);
	int ret;

	set_bit(COMMAND_PENDING, &g0->flags);

	ret = ucsi_stm32g0_async_write(ucsi, offset, val, len);
	if (ret)
		goto out_clear_bit;

	if (!wait_for_completion_timeout(&g0->complete, msecs_to_jiffies(5000)))
		ret = -ETIMEDOUT;

out_clear_bit:
	clear_bit(COMMAND_PENDING, &g0->flags);

	return ret;
}

static irqreturn_t ucsi_stm32g0_irq_handler(int irq, void *data)
{
	struct ucsi_stm32g0 *g0 = data;
	u32 cci;
	int ret;

	if (g0->suspended)
		g0->wakeup_event = true;

	ret = ucsi_stm32g0_read(g0->ucsi, UCSI_CCI, &cci, sizeof(cci));
	if (ret)
		return IRQ_NONE;

	if (UCSI_CCI_CONNECTOR(cci))
		ucsi_connector_change(g0->ucsi, UCSI_CCI_CONNECTOR(cci));

	if (test_bit(COMMAND_PENDING, &g0->flags) &&
	    cci & (UCSI_CCI_ACK_COMPLETE | UCSI_CCI_COMMAND_COMPLETE))
		complete(&g0->complete);

	return IRQ_HANDLED;
}

static const struct ucsi_operations ucsi_stm32g0_ops = {
	.read = ucsi_stm32g0_read,
	.sync_write = ucsi_stm32g0_sync_write,
	.async_write = ucsi_stm32g0_async_write,
};

static int ucsi_stm32g0_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct ucsi_stm32g0 *g0;
	int ret;

	g0 = devm_kzalloc(dev, sizeof(*g0), GFP_KERNEL);
	if (!g0)
		return -ENOMEM;

	g0->dev = dev;
	g0->client = client;
	init_completion(&g0->complete);
	i2c_set_clientdata(client, g0);

	g0->ucsi = ucsi_create(dev, &ucsi_stm32g0_ops);
	if (IS_ERR(g0->ucsi))
		return PTR_ERR(g0->ucsi);

	ucsi_set_drvdata(g0->ucsi, g0);

	/* Request alert interrupt */
	ret = request_threaded_irq(client->irq, NULL, ucsi_stm32g0_irq_handler, IRQF_ONESHOT,
				   dev_name(&client->dev), g0);
	if (ret) {
		dev_err_probe(dev, ret, "request IRQ failed\n");
		goto destroy;
	}

	ret = ucsi_register(g0->ucsi);
	if (ret) {
		dev_err_probe(dev, ret, "ucsi_register failed\n");
		goto freeirq;
	}

	return 0;

freeirq:
	free_irq(client->irq, g0);
destroy:
	ucsi_destroy(g0->ucsi);

	return ret;
}

static int ucsi_stm32g0_remove(struct i2c_client *client)
{
	struct ucsi_stm32g0 *g0 = i2c_get_clientdata(client);

	ucsi_unregister(g0->ucsi);
	free_irq(client->irq, g0);
	ucsi_destroy(g0->ucsi);

	return 0;
}

static int ucsi_stm32g0_suspend(struct device *dev)
{
	struct ucsi_stm32g0 *g0 = dev_get_drvdata(dev);
	struct i2c_client *client = g0->client;

	/* Keep the interrupt disabled until the i2c bus has been resumed */
	disable_irq(client->irq);

	g0->suspended = true;
	g0->wakeup_event = false;

	if (device_may_wakeup(dev) || device_wakeup_path(dev))
		enable_irq_wake(client->irq);

	return 0;
}

static int ucsi_stm32g0_resume(struct device *dev)
{
	struct ucsi_stm32g0 *g0 = dev_get_drvdata(dev);
	struct i2c_client *client = g0->client;

	if (device_may_wakeup(dev) || device_wakeup_path(dev))
		disable_irq_wake(client->irq);

	enable_irq(client->irq);

	/* Enforce any pending handler gets called to signal a wakeup_event */
	synchronize_irq(client->irq);

	if (g0->wakeup_event)
		pm_wakeup_event(g0->dev, 0);

	g0->suspended = false;

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(ucsi_stm32g0_pm_ops, ucsi_stm32g0_suspend, ucsi_stm32g0_resume);

static const struct of_device_id __maybe_unused ucsi_stm32g0_typec_of_match[] = {
	{ .compatible = "st,stm32g0-typec" },
	{},
};
MODULE_DEVICE_TABLE(of, ucsi_stm32g0_typec_of_match);

static const struct i2c_device_id ucsi_stm32g0_typec_i2c_devid[] = {
	{"stm32g0-typec", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, ucsi_stm32g0_typec_i2c_devid);

static struct i2c_driver ucsi_stm32g0_i2c_driver = {
	.driver = {
		.name = "ucsi-stm32g0-i2c",
		.of_match_table = of_match_ptr(ucsi_stm32g0_typec_of_match),
		.pm = pm_sleep_ptr(&ucsi_stm32g0_pm_ops),
	},
	.probe = ucsi_stm32g0_probe,
	.remove = ucsi_stm32g0_remove,
	.id_table = ucsi_stm32g0_typec_i2c_devid
};
module_i2c_driver(ucsi_stm32g0_i2c_driver);

MODULE_AUTHOR("Fabrice Gasnier <fabrice.gasnier@foss.st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32G0 Type-C controller");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:ucsi-stm32g0");

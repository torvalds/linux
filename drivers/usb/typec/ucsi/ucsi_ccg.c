// SPDX-License-Identifier: GPL-2.0
/*
 * UCSI driver for Cypress CCGx Type-C controller
 *
 * Copyright (C) 2017-2018 NVIDIA Corporation. All rights reserved.
 * Author: Ajay Gupta <ajayg@nvidia.com>
 *
 * Some code borrowed from drivers/usb/typec/ucsi/ucsi_acpi.c
 */
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

#include <asm/unaligned.h>
#include "ucsi.h"

struct ucsi_ccg {
	struct device *dev;
	struct ucsi *ucsi;
	struct ucsi_ppm ppm;
	struct i2c_client *client;
};

#define CCGX_RAB_INTR_REG			0x06
#define CCGX_RAB_UCSI_CONTROL			0x39
#define CCGX_RAB_UCSI_CONTROL_START		BIT(0)
#define CCGX_RAB_UCSI_CONTROL_STOP		BIT(1)
#define CCGX_RAB_UCSI_DATA_BLOCK(offset)	(0xf000 | ((offset) & 0xff))

static int ccg_read(struct ucsi_ccg *uc, u16 rab, u8 *data, u32 len)
{
	struct i2c_client *client = uc->client;
	const struct i2c_adapter_quirks *quirks = client->adapter->quirks;
	unsigned char buf[2];
	struct i2c_msg msgs[] = {
		{
			.addr	= client->addr,
			.flags  = 0x0,
			.len	= sizeof(buf),
			.buf	= buf,
		},
		{
			.addr	= client->addr,
			.flags  = I2C_M_RD,
			.buf	= data,
		},
	};
	u32 rlen, rem_len = len, max_read_len = len;
	int status;

	/* check any max_read_len limitation on i2c adapter */
	if (quirks && quirks->max_read_len)
		max_read_len = quirks->max_read_len;

	while (rem_len > 0) {
		msgs[1].buf = &data[len - rem_len];
		rlen = min_t(u16, rem_len, max_read_len);
		msgs[1].len = rlen;
		put_unaligned_le16(rab, buf);
		status = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
		if (status < 0) {
			dev_err(uc->dev, "i2c_transfer failed %d\n", status);
			return status;
		}
		rab += rlen;
		rem_len -= rlen;
	}

	return 0;
}

static int ccg_write(struct ucsi_ccg *uc, u16 rab, u8 *data, u32 len)
{
	struct i2c_client *client = uc->client;
	unsigned char *buf;
	struct i2c_msg msgs[] = {
		{
			.addr	= client->addr,
			.flags  = 0x0,
		}
	};
	int status;

	buf = kzalloc(len + sizeof(rab), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	put_unaligned_le16(rab, buf);
	memcpy(buf + sizeof(rab), data, len);

	msgs[0].len = len + sizeof(rab);
	msgs[0].buf = buf;

	status = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (status < 0) {
		dev_err(uc->dev, "i2c_transfer failed %d\n", status);
		kfree(buf);
		return status;
	}

	kfree(buf);
	return 0;
}

static int ucsi_ccg_init(struct ucsi_ccg *uc)
{
	unsigned int count = 10;
	u8 data;
	int status;

	data = CCGX_RAB_UCSI_CONTROL_STOP;
	status = ccg_write(uc, CCGX_RAB_UCSI_CONTROL, &data, sizeof(data));
	if (status < 0)
		return status;

	data = CCGX_RAB_UCSI_CONTROL_START;
	status = ccg_write(uc, CCGX_RAB_UCSI_CONTROL, &data, sizeof(data));
	if (status < 0)
		return status;

	/*
	 * Flush CCGx RESPONSE queue by acking interrupts. Above ucsi control
	 * register write will push response which must be cleared.
	 */
	do {
		status = ccg_read(uc, CCGX_RAB_INTR_REG, &data, sizeof(data));
		if (status < 0)
			return status;

		if (!data)
			return 0;

		status = ccg_write(uc, CCGX_RAB_INTR_REG, &data, sizeof(data));
		if (status < 0)
			return status;

		usleep_range(10000, 11000);
	} while (--count);

	return -ETIMEDOUT;
}

static int ucsi_ccg_send_data(struct ucsi_ccg *uc)
{
	u8 *ppm = (u8 *)uc->ppm.data;
	int status;
	u16 rab;

	rab = CCGX_RAB_UCSI_DATA_BLOCK(offsetof(struct ucsi_data, message_out));
	status = ccg_write(uc, rab, ppm +
			   offsetof(struct ucsi_data, message_out),
			   sizeof(uc->ppm.data->message_out));
	if (status < 0)
		return status;

	rab = CCGX_RAB_UCSI_DATA_BLOCK(offsetof(struct ucsi_data, ctrl));
	return ccg_write(uc, rab, ppm + offsetof(struct ucsi_data, ctrl),
			 sizeof(uc->ppm.data->ctrl));
}

static int ucsi_ccg_recv_data(struct ucsi_ccg *uc)
{
	u8 *ppm = (u8 *)uc->ppm.data;
	int status;
	u16 rab;

	rab = CCGX_RAB_UCSI_DATA_BLOCK(offsetof(struct ucsi_data, cci));
	status = ccg_read(uc, rab, ppm + offsetof(struct ucsi_data, cci),
			  sizeof(uc->ppm.data->cci));
	if (status < 0)
		return status;

	rab = CCGX_RAB_UCSI_DATA_BLOCK(offsetof(struct ucsi_data, message_in));
	return ccg_read(uc, rab, ppm + offsetof(struct ucsi_data, message_in),
			sizeof(uc->ppm.data->message_in));
}

static int ucsi_ccg_ack_interrupt(struct ucsi_ccg *uc)
{
	int status;
	unsigned char data;

	status = ccg_read(uc, CCGX_RAB_INTR_REG, &data, sizeof(data));
	if (status < 0)
		return status;

	return ccg_write(uc, CCGX_RAB_INTR_REG, &data, sizeof(data));
}

static int ucsi_ccg_sync(struct ucsi_ppm *ppm)
{
	struct ucsi_ccg *uc = container_of(ppm, struct ucsi_ccg, ppm);
	int status;

	status = ucsi_ccg_recv_data(uc);
	if (status < 0)
		return status;

	/* ack interrupt to allow next command to run */
	return ucsi_ccg_ack_interrupt(uc);
}

static int ucsi_ccg_cmd(struct ucsi_ppm *ppm, struct ucsi_control *ctrl)
{
	struct ucsi_ccg *uc = container_of(ppm, struct ucsi_ccg, ppm);

	ppm->data->ctrl.raw_cmd = ctrl->raw_cmd;
	return ucsi_ccg_send_data(uc);
}

static irqreturn_t ccg_irq_handler(int irq, void *data)
{
	struct ucsi_ccg *uc = data;

	ucsi_notify(uc->ucsi);

	return IRQ_HANDLED;
}

static int ucsi_ccg_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct ucsi_ccg *uc;
	int status;
	u16 rab;

	uc = devm_kzalloc(dev, sizeof(*uc), GFP_KERNEL);
	if (!uc)
		return -ENOMEM;

	uc->ppm.data = devm_kzalloc(dev, sizeof(struct ucsi_data), GFP_KERNEL);
	if (!uc->ppm.data)
		return -ENOMEM;

	uc->ppm.cmd = ucsi_ccg_cmd;
	uc->ppm.sync = ucsi_ccg_sync;
	uc->dev = dev;
	uc->client = client;

	/* reset ccg device and initialize ucsi */
	status = ucsi_ccg_init(uc);
	if (status < 0) {
		dev_err(uc->dev, "ucsi_ccg_init failed - %d\n", status);
		return status;
	}

	status = devm_request_threaded_irq(dev, client->irq, NULL,
					   ccg_irq_handler,
					   IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
					   dev_name(dev), uc);
	if (status < 0) {
		dev_err(uc->dev, "request_threaded_irq failed - %d\n", status);
		return status;
	}

	uc->ucsi = ucsi_register_ppm(dev, &uc->ppm);
	if (IS_ERR(uc->ucsi)) {
		dev_err(uc->dev, "ucsi_register_ppm failed\n");
		return PTR_ERR(uc->ucsi);
	}

	rab = CCGX_RAB_UCSI_DATA_BLOCK(offsetof(struct ucsi_data, version));
	status = ccg_read(uc, rab, (u8 *)(uc->ppm.data) +
			  offsetof(struct ucsi_data, version),
			  sizeof(uc->ppm.data->version));
	if (status < 0) {
		ucsi_unregister_ppm(uc->ucsi);
		return status;
	}

	i2c_set_clientdata(client, uc);
	return 0;
}

static int ucsi_ccg_remove(struct i2c_client *client)
{
	struct ucsi_ccg *uc = i2c_get_clientdata(client);

	ucsi_unregister_ppm(uc->ucsi);

	return 0;
}

static const struct i2c_device_id ucsi_ccg_device_id[] = {
	{"ccgx-ucsi", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, ucsi_ccg_device_id);

static struct i2c_driver ucsi_ccg_driver = {
	.driver = {
		.name = "ucsi_ccg",
	},
	.probe = ucsi_ccg_probe,
	.remove = ucsi_ccg_remove,
	.id_table = ucsi_ccg_device_id,
};

module_i2c_driver(ucsi_ccg_driver);

MODULE_AUTHOR("Ajay Gupta <ajayg@nvidia.com>");
MODULE_DESCRIPTION("UCSI driver for Cypress CCGx Type-C controller");
MODULE_LICENSE("GPL v2");

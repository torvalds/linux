// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Intel Corporation.
 */

#include <linux/delay.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/pci_ids.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#include "regs/xe_i2c_regs.h"

#include "xe_amc.h"
#include "xe_device.h"
#include "xe_i2c.h"
#include "xe_mmio.h"

/**
 * DOC: Add-In Management Controller (AMC)
 *
 * Handler for the SMBus Alerts from the AMC. All the alerts from AMC will cause
 * the device to be declared wedged.
 */

#define AMC_COMMAND		0x0f
#define AMC_GPU_I2C_ADDR	0x8f
#define AMC_VERSION_V1		0x01
#define AMC_DESTINATION_ID	12
#define AMC_SOURCE_ID		8
#define AMC_FLAGS		0xc8

#define AMC_MSG_TYPE		0x7e
#define AMC_GET_ALERT_REASON	0x01

enum xe_amc_alert {
	AMC_ALERT_UNKNOWN,
	AMC_ALERT_FW_DOWNLOAD,
	AMC_ALERT_THERMAL_TRIP,
	AMC_ALERT_OOB_REQUEST,
	AMC_ALERT_OOB_RESET,
	AMC_ALERT_CATERR,
};

static const char * const amc_alert[] = {
	[AMC_ALERT_FW_DOWNLOAD]		= "Firmware Download",
	[AMC_ALERT_THERMAL_TRIP]	= "Thermal Trip",
	[AMC_ALERT_OOB_REQUEST]		= "OOB Request",
	[AMC_ALERT_OOB_RESET]		= "OOB Reset",
	[AMC_ALERT_CATERR]		= "Catastrophic",
};

struct xe_amc {
	struct xe_i2c *i2c;
	struct work_struct work;
};

struct amc_header {
	u8 command;
	u8 len;
	u8 address;
	u8 version;
	u8 destination;
	u8 source;
	u8 flags;
} __packed;

struct amc_message {
	u8 type;
	u16 vendor;
	u8 command;
} __packed;

struct amc_request {
	struct amc_header header;
	struct amc_message message;
	u32 reserved;
} __packed;

struct amc_response {
	struct amc_header header;
	struct amc_message message;
	u8 error;
	u8 value;
} __packed;

static const struct amc_request amc_get_alert_reason = {
	.header = {
		.command	= AMC_COMMAND,
		.len		= sizeof(struct amc_request) - 2,
		.address	= AMC_GPU_I2C_ADDR,
		.version	= AMC_VERSION_V1,
		.destination	= AMC_DESTINATION_ID,
		.source		= AMC_SOURCE_ID,
		.flags		= AMC_FLAGS,
	},
	.message = {
		.type		= AMC_MSG_TYPE,
		.vendor		= htons(PCI_VENDOR_ID_INTEL),
		.command	= AMC_GET_ALERT_REASON,
	},
};

static void xe_amc_work(struct work_struct *work)
{
	const struct amc_request *request = &amc_get_alert_reason;
	struct xe_amc *amc = from_work(amc, work, work);
	u8 alert_reason = AMC_ALERT_UNKNOWN;
	struct amc_response response;
	struct i2c_client *client;
	int ret;

	client = amc->i2c->client[XE_I2C_CLIENT_AMC];
	if (IS_ERR_OR_NULL(client))
		goto out_reassert_interrupt;

	ret = i2c_master_send(client, (u8 *)request, sizeof(*request));
	if (ret < 0) {
		dev_err(&client->dev, "failed to send request (%d)\n", ret);
		goto out_reassert_interrupt;
	}

	/* AMC needs 20ms to generate the response. */
	fsleep(20 * USEC_PER_MSEC);

	ret = i2c_master_recv(client, (u8 *)&response, sizeof(response));
	if (ret < 0) {
		dev_err(&client->dev, "failed to read response (%d)\n", ret);
		goto out_reassert_interrupt;
	}

	if (!response.header.len) {
		dev_err(&client->dev, "empty response from AMC\n");
		goto out_reassert_interrupt;
	}

	if (memcmp(&response.message, &request->message, sizeof(struct amc_message))) {
		dev_err(&client->dev, "response does not match the request\n");
		goto out_reassert_interrupt;
	}

	if (response.error) {
		dev_err(&client->dev, "AMC error 0x%02x\n", response.error);
		goto out_reassert_interrupt;
	}

	alert_reason = response.value;
	dev_dbg(&client->dev, "Alert reason: %d\n", alert_reason);

out_reassert_interrupt:
	xe_mmio_rmw32(amc->i2c->mmio, I2C_CONFIG_CMD, PCI_COMMAND_INTX_DISABLE, 0);

	switch (alert_reason) {
	case AMC_ALERT_FW_DOWNLOAD:
	case AMC_ALERT_THERMAL_TRIP:
	case AMC_ALERT_OOB_REQUEST:
	case AMC_ALERT_OOB_RESET:
	case AMC_ALERT_CATERR:
		dev_warn(amc->i2c->drm_dev, "AMC Alert: %s\n", amc_alert[alert_reason]);
		xe_device_declare_wedged(i2c_client_to_xe_device(client));
		break;
	default:
		dev_warn(amc->i2c->drm_dev, "unknown AMC alert: %d\n", alert_reason);
		break;
	}
}

void xe_amc_handle_alert(struct xe_i2c *i2c)
{
	queue_work(system_long_wq, &i2c->amc->work);
}

int xe_amc_init(struct xe_i2c *i2c)
{
	struct xe_amc *amc;

	amc = kzalloc_obj(*amc);
	if (!amc)
		return -ENOMEM;

	INIT_WORK(&amc->work, xe_amc_work);
	i2c->amc = amc;
	amc->i2c = i2c;

	return 0;
}

void xe_amc_exit(struct xe_i2c *i2c)
{
	if (i2c->amc) {
		cancel_work_sync(&i2c->amc->work);
		kfree(i2c->amc);
	}
}

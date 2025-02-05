// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2022, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Ltd
 */
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rpmsg.h>
#include <linux/slab.h>
#include <linux/soc/qcom/pdr.h>
#include <linux/debugfs.h>

#define CREATE_TRACE_POINTS
#include "pmic_pdcharger_ulog.h"

#define MSG_OWNER_CHG_ULOG		32778
#define MSG_TYPE_REQ_RESP		1

#define GET_CHG_ULOG_REQ		0x18
#define SET_CHG_ULOG_PROP_REQ		0x19

#define LOG_DEFAULT_TIME_MS		1000

#define MAX_ULOG_SIZE			8192

struct pmic_pdcharger_ulog_hdr {
	__le32 owner;
	__le32 type;
	__le32 opcode;
};

struct pmic_pdcharger_ulog {
	struct rpmsg_device *rpdev;
	struct delayed_work ulog_work;
};

struct get_ulog_req_msg {
	struct pmic_pdcharger_ulog_hdr	hdr;
	u32				log_size;
};

struct get_ulog_resp_msg {
	struct pmic_pdcharger_ulog_hdr	hdr;
	u8				buf[MAX_ULOG_SIZE];
};

static int pmic_pdcharger_ulog_write_async(struct pmic_pdcharger_ulog *pg, void *data, size_t len)
{
	return rpmsg_send(pg->rpdev->ept, data, len);
}

static int pmic_pdcharger_ulog_request(struct pmic_pdcharger_ulog *pg)
{
	struct get_ulog_req_msg req_msg = {
		.hdr = {
			.owner = cpu_to_le32(MSG_OWNER_CHG_ULOG),
			.type = cpu_to_le32(MSG_TYPE_REQ_RESP),
			.opcode = cpu_to_le32(GET_CHG_ULOG_REQ)
		},
		.log_size = MAX_ULOG_SIZE
	};

	return pmic_pdcharger_ulog_write_async(pg, &req_msg, sizeof(req_msg));
}

static void pmic_pdcharger_ulog_work(struct work_struct *work)
{
	struct pmic_pdcharger_ulog *pg = container_of(work, struct pmic_pdcharger_ulog,
						      ulog_work.work);
	int rc;

	rc = pmic_pdcharger_ulog_request(pg);
	if (rc) {
		dev_err(&pg->rpdev->dev, "Error requesting ulog, rc=%d\n", rc);
		return;
	}
}

static void pmic_pdcharger_ulog_handle_message(struct pmic_pdcharger_ulog *pg,
					       struct get_ulog_resp_msg *resp_msg,
					       size_t len)
{
	char *token, *buf = resp_msg->buf;

	if (len != sizeof(*resp_msg)) {
		dev_err(&pg->rpdev->dev, "Expected data length: %zu, received: %zu\n",
			sizeof(*resp_msg), len);
		return;
	}

	buf[MAX_ULOG_SIZE - 1] = '\0';

	do {
		token = strsep((char **)&buf, "\n");
		if (token && strlen(token))
			trace_pmic_pdcharger_ulog_msg(token);
	} while (token);
}

static int pmic_pdcharger_ulog_rpmsg_callback(struct rpmsg_device *rpdev, void *data,
					      int len, void *priv, u32 addr)
{
	struct pmic_pdcharger_ulog *pg = dev_get_drvdata(&rpdev->dev);
	struct pmic_pdcharger_ulog_hdr *hdr = data;
	u32 opcode;

	opcode = le32_to_cpu(hdr->opcode);

	switch (opcode) {
	case GET_CHG_ULOG_REQ:
		schedule_delayed_work(&pg->ulog_work, msecs_to_jiffies(LOG_DEFAULT_TIME_MS));
		pmic_pdcharger_ulog_handle_message(pg, data, len);
		break;
	default:
		dev_err(&pg->rpdev->dev, "Unknown opcode %u\n", opcode);
		break;
	}

	return 0;
}

static int pmic_pdcharger_ulog_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct pmic_pdcharger_ulog *pg;
	struct device *dev = &rpdev->dev;

	pg = devm_kzalloc(dev, sizeof(*pg), GFP_KERNEL);
	if (!pg)
		return -ENOMEM;

	pg->rpdev = rpdev;
	INIT_DELAYED_WORK(&pg->ulog_work, pmic_pdcharger_ulog_work);

	dev_set_drvdata(dev, pg);

	pmic_pdcharger_ulog_request(pg);

	return 0;
}

static void pmic_pdcharger_ulog_rpmsg_remove(struct rpmsg_device *rpdev)
{
	struct pmic_pdcharger_ulog *pg = dev_get_drvdata(&rpdev->dev);

	cancel_delayed_work_sync(&pg->ulog_work);
}

static const struct rpmsg_device_id pmic_pdcharger_ulog_rpmsg_id_match[] = {
	{ "PMIC_LOGS_ADSP_APPS" },
	{}
};
/*
 * No MODULE_DEVICE_TABLE intentionally: that's a debugging module, to be
 * loaded manually only.
 */

static struct rpmsg_driver pmic_pdcharger_ulog_rpmsg_driver = {
	.probe = pmic_pdcharger_ulog_rpmsg_probe,
	.remove = pmic_pdcharger_ulog_rpmsg_remove,
	.callback = pmic_pdcharger_ulog_rpmsg_callback,
	.id_table = pmic_pdcharger_ulog_rpmsg_id_match,
	.drv  = {
		.name  = "qcom_pmic_pdcharger_ulog_rpmsg",
	},
};

module_rpmsg_driver(pmic_pdcharger_ulog_rpmsg_driver);
MODULE_DESCRIPTION("Qualcomm PMIC ChargerPD ULOG driver");
MODULE_LICENSE("GPL");

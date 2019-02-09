/*
 * Copyright (c) 2014-2017, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017, Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/completion.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/rpmsg.h>
#include <linux/remoteproc/qcom_rproc.h>

/**
 * struct do_cleanup_msg - The data structure for an SSR do_cleanup message
 * version:     The G-Link SSR protocol version
 * command:     The G-Link SSR command - do_cleanup
 * seq_num:     Sequence number
 * name_len:    Length of the name of the subsystem being restarted
 * name:        G-Link edge name of the subsystem being restarted
 */
struct do_cleanup_msg {
	__le32 version;
	__le32 command;
	__le32 seq_num;
	__le32 name_len;
	char name[32];
};

/**
 * struct cleanup_done_msg - The data structure for an SSR cleanup_done message
 * version:     The G-Link SSR protocol version
 * response:    The G-Link SSR response to a do_cleanup command, cleanup_done
 * seq_num:     Sequence number
 */
struct cleanup_done_msg {
	__le32 version;
	__le32 response;
	__le32 seq_num;
};

/**
 * G-Link SSR protocol commands
 */
#define GLINK_SSR_DO_CLEANUP	0
#define GLINK_SSR_CLEANUP_DONE	1

struct glink_ssr {
	struct device *dev;
	struct rpmsg_endpoint *ept;

	struct notifier_block nb;

	u32 seq_num;
	struct completion completion;
};

static int qcom_glink_ssr_callback(struct rpmsg_device *rpdev,
				   void *data, int len, void *priv, u32 addr)
{
	struct cleanup_done_msg *msg = data;
	struct glink_ssr *ssr = dev_get_drvdata(&rpdev->dev);

	if (len < sizeof(*msg)) {
		dev_err(ssr->dev, "message too short\n");
		return -EINVAL;
	}

	if (le32_to_cpu(msg->version) != 0)
		return -EINVAL;

	if (le32_to_cpu(msg->response) != GLINK_SSR_CLEANUP_DONE)
		return 0;

	if (le32_to_cpu(msg->seq_num) != ssr->seq_num) {
		dev_err(ssr->dev, "invalid sequence number of response\n");
		return -EINVAL;
	}

	complete(&ssr->completion);

	return 0;
}

static int qcom_glink_ssr_notify(struct notifier_block *nb, unsigned long event,
				 void *data)
{
	struct glink_ssr *ssr = container_of(nb, struct glink_ssr, nb);
	struct do_cleanup_msg msg;
	char *ssr_name = data;
	int ret;

	ssr->seq_num++;
	reinit_completion(&ssr->completion);

	memset(&msg, 0, sizeof(msg));
	msg.command = cpu_to_le32(GLINK_SSR_DO_CLEANUP);
	msg.seq_num = cpu_to_le32(ssr->seq_num);
	msg.name_len = cpu_to_le32(strlen(ssr_name));
	strlcpy(msg.name, ssr_name, sizeof(msg.name));

	ret = rpmsg_send(ssr->ept, &msg, sizeof(msg));
	if (ret < 0)
		dev_err(ssr->dev, "failed to send cleanup message\n");

	ret = wait_for_completion_timeout(&ssr->completion, HZ);
	if (!ret)
		dev_err(ssr->dev, "timeout waiting for cleanup done message\n");

	return NOTIFY_DONE;
}

static int qcom_glink_ssr_probe(struct rpmsg_device *rpdev)
{
	struct glink_ssr *ssr;

	ssr = devm_kzalloc(&rpdev->dev, sizeof(*ssr), GFP_KERNEL);
	if (!ssr)
		return -ENOMEM;

	init_completion(&ssr->completion);

	ssr->dev = &rpdev->dev;
	ssr->ept = rpdev->ept;
	ssr->nb.notifier_call = qcom_glink_ssr_notify;

	dev_set_drvdata(&rpdev->dev, ssr);

	return qcom_register_ssr_notifier(&ssr->nb);
}

static void qcom_glink_ssr_remove(struct rpmsg_device *rpdev)
{
	struct glink_ssr *ssr = dev_get_drvdata(&rpdev->dev);

	qcom_unregister_ssr_notifier(&ssr->nb);
}

static const struct rpmsg_device_id qcom_glink_ssr_match[] = {
	{ "glink_ssr" },
	{}
};

static struct rpmsg_driver qcom_glink_ssr_driver = {
	.probe = qcom_glink_ssr_probe,
	.remove = qcom_glink_ssr_remove,
	.callback = qcom_glink_ssr_callback,
	.id_table = qcom_glink_ssr_match,
	.drv = {
		.name = "qcom_glink_ssr",
	},
};
module_rpmsg_driver(qcom_glink_ssr_driver);

MODULE_ALIAS("rpmsg:glink_ssr");
MODULE_DESCRIPTION("Qualcomm GLINK SSR notifier");
MODULE_LICENSE("GPL v2");

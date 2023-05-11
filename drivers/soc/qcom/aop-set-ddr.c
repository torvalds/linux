// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021 The Linux Foundation. All rights reserved. */
/* Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved. */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox/qmp.h>
#include <linux/platform_device.h>

#define MAX_MSG_SIZE 96 /* Imposed by the remote */

struct set_ddr_freq_data {
	struct qmp_pkt pkt;
	char buf[MAX_MSG_SIZE + 1];
};

static struct set_ddr_freq_data data_pkt;
static struct mbox_chan *chan;
static struct mbox_client *cl;

static ssize_t set_ddr_capped_freq_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	unsigned int freq = 0;

	if (kstrtou32(buf, 0, &freq)) {
		pr_err("%s: failed to read frequency info from string\n", __func__);
		return -EINVAL;
	}

	memset(&data_pkt, 0, sizeof(data_pkt));
	snprintf(data_pkt.buf, MAX_MSG_SIZE,
			"{class:ddr, res:capped, val: %d}", freq);
	pr_debug("%s : data: %s\n", __func__, data_pkt.buf);

	/* Controller expects a 4 byte aligned buffer */
	data_pkt.pkt.size = (strlen(data_pkt.buf) + 0x3) & ~0x3;
	data_pkt.pkt.data = data_pkt.buf;

	if (mbox_send_message(chan, &(data_pkt.pkt)) < 0)
		pr_err("Failed to send qmp request\n");

	return len;
}
static DEVICE_ATTR_WO(set_ddr_capped_freq);

static int set_ddr_freq_probe(struct platform_device *pdev)
{
	int ret = 0;

	cl = devm_kzalloc(&pdev->dev, sizeof(*cl), GFP_KERNEL);
	if (!cl)
		return -ENOMEM;

	cl->dev = &pdev->dev;
	cl->tx_block = true;
	cl->tx_tout = 1000;
	cl->knows_txdone = false;

	chan = mbox_request_channel(cl, 0);
	if (IS_ERR(chan)) {
		dev_err(&pdev->dev, "Failed to mbox channel\n");
		return PTR_ERR(chan);
	}

	ret = device_create_file(&pdev->dev, &dev_attr_set_ddr_capped_freq);
	if (ret)
		dev_err(&pdev->dev, "Couldn't create sysfs attribute\n");

	return 0;
}

static int set_ddr_freq_remove(struct platform_device *pdev)
{
	device_remove_file(&pdev->dev, &dev_attr_set_ddr_capped_freq);
	if (chan)
		mbox_free_channel(chan);
	return 0;
}
static const struct of_device_id aop_qmp_match_tbl[] = {
	{.compatible = "qcom,aop-set-ddr-freq"},
	{},
};

static struct platform_driver aop_qmp_msg_driver = {
	.driver = {
		.name = "aop-set-ddr-freq",
		.suppress_bind_attrs = true,
		.of_match_table = aop_qmp_match_tbl,
	},
	.probe = set_ddr_freq_probe,
	.remove = set_ddr_freq_remove,
};
module_platform_driver(aop_qmp_msg_driver);
MODULE_LICENSE("GPL");

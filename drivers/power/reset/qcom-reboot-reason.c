// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2019, 2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#define pr_fmt(fmt)     "qcom-reboot-reason: %s: " fmt, __func__

#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/pm.h>
#include <linux/of_address.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>

struct qcom_reboot_reason {
	struct device *dev;
	struct notifier_block reboot_nb;
	struct nvmem_cell *nvmem_cell;
	struct poweroff_reason *reasons;
};

struct poweroff_reason {
	const char *cmd;
	unsigned int pon_reason;
	unsigned int size;
};

static struct poweroff_reason pon_reasons[] = {
	{ "recovery",			0x01, 0x1 },
	{ "bootloader",			0x02, 0x1 },
	{ "rtc",			0x03, 0x1 },
	{ "dm-verity device corrupted",	0x04, 0x1 },
	{ "dm-verity enforcing",	0x05, 0x1 },
	{ "keys clear",			0x06, 0x1 },
	{}
};

static  struct poweroff_reason imem_pon_reasons[] = {
	{ "recovery",                   0x77665502, 0x4 },
	{ "bootloader",                 0x77665500, 0x4 },
	{ "rtc",                        0x77665503, 0x4 },
	{ "dm-verity device corrupted", 0x77665508, 0x4 },
	{ "dm-verity enforcing",        0x77665509, 0x4 },
	{ "keys clear",                 0x7766550a, 0x4 },
	{}
};

static const struct of_device_id of_qcom_reboot_reason_match[] = {
	{ .compatible = "qcom,reboot-reason", .data = pon_reasons },
	{ .compatible = "qcom,imem-reboot-reason", .data = imem_pon_reasons },
	{},
};
MODULE_DEVICE_TABLE(of, of_qcom_reboot_reason_match);

static int qcom_reboot_reason_reboot(struct notifier_block *this,
				     unsigned long event, void *ptr)
{
	int rc;
	char *cmd = ptr;
	struct qcom_reboot_reason *reboot = container_of(this,
		struct qcom_reboot_reason, reboot_nb);
	struct poweroff_reason *reason;

	if (!cmd)
		return NOTIFY_OK;
	for (reason = reboot->reasons; reason->cmd; reason++) {
		if (!strcmp(cmd, reason->cmd)) {
			rc = nvmem_cell_write(reboot->nvmem_cell,
					 &reason->pon_reason,
					 reason->size);
			if (rc < 0)
				pr_err("PON reason store failed, rc=%d\n", rc);
			break;
		}
	}

	return NOTIFY_OK;
}

static int qcom_reboot_reason_probe(struct platform_device *pdev)
{
	struct qcom_reboot_reason *reboot;
	const struct of_device_id *match;

	reboot = devm_kzalloc(&pdev->dev, sizeof(*reboot), GFP_KERNEL);
	if (!reboot)
		return -ENOMEM;

	reboot->dev = &pdev->dev;

	/*
	 * can't use of_device_get_match_data() because it returns
	 * const data. nvmem_cell_write() doesn't have signature to accept const data.
	 * Hence used of_match_device().
	 */

	match = of_match_device(of_match_ptr(of_qcom_reboot_reason_match), &pdev->dev);
	if (!match)
		return -ENODEV;
	reboot->reasons = (struct poweroff_reason *)match->data;

	reboot->nvmem_cell = devm_nvmem_cell_get(reboot->dev, "restart_reason");

	if (IS_ERR(reboot->nvmem_cell))
		return PTR_ERR(reboot->nvmem_cell);

	reboot->reboot_nb.notifier_call = qcom_reboot_reason_reboot;
	reboot->reboot_nb.priority = 255;
	register_reboot_notifier(&reboot->reboot_nb);

	platform_set_drvdata(pdev, reboot);

	return 0;
}

static int qcom_reboot_reason_remove(struct platform_device *pdev)
{
	struct qcom_reboot_reason *reboot = platform_get_drvdata(pdev);

	unregister_reboot_notifier(&reboot->reboot_nb);

	return 0;
}


static struct platform_driver qcom_reboot_reason_driver = {
	.probe = qcom_reboot_reason_probe,
	.remove = qcom_reboot_reason_remove,
	.driver = {
		.name = "qcom-reboot-reason",
		.of_match_table = of_match_ptr(of_qcom_reboot_reason_match),
	},
};

module_platform_driver(qcom_reboot_reason_driver);

MODULE_DESCRIPTION("MSM Reboot Reason Driver");
MODULE_LICENSE("GPL v2");

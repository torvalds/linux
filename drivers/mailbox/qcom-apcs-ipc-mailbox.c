/*
 * Copyright (c) 2017, Linaro Ltd
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/mailbox_controller.h>

#define QCOM_APCS_IPC_BITS	32

struct qcom_apcs_ipc {
	struct mbox_controller mbox;
	struct mbox_chan mbox_chans[QCOM_APCS_IPC_BITS];

	void __iomem *reg;
	unsigned long offset;
};

static int qcom_apcs_ipc_send_data(struct mbox_chan *chan, void *data)
{
	struct qcom_apcs_ipc *apcs = container_of(chan->mbox,
						  struct qcom_apcs_ipc, mbox);
	unsigned long idx = (unsigned long)chan->con_priv;

	writel(BIT(idx), apcs->reg);

	return 0;
}

static const struct mbox_chan_ops qcom_apcs_ipc_ops = {
	.send_data = qcom_apcs_ipc_send_data,
};

static int qcom_apcs_ipc_probe(struct platform_device *pdev)
{
	struct qcom_apcs_ipc *apcs;
	struct resource *res;
	unsigned long offset;
	void __iomem *base;
	unsigned long i;
	int ret;

	apcs = devm_kzalloc(&pdev->dev, sizeof(*apcs), GFP_KERNEL);
	if (!apcs)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	offset = (unsigned long)of_device_get_match_data(&pdev->dev);

	apcs->reg = base + offset;

	/* Initialize channel identifiers */
	for (i = 0; i < ARRAY_SIZE(apcs->mbox_chans); i++)
		apcs->mbox_chans[i].con_priv = (void *)i;

	apcs->mbox.dev = &pdev->dev;
	apcs->mbox.ops = &qcom_apcs_ipc_ops;
	apcs->mbox.chans = apcs->mbox_chans;
	apcs->mbox.num_chans = ARRAY_SIZE(apcs->mbox_chans);

	ret = mbox_controller_register(&apcs->mbox);
	if (ret) {
		dev_err(&pdev->dev, "failed to register APCS IPC controller\n");
		return ret;
	}

	platform_set_drvdata(pdev, apcs);

	return 0;
}

static int qcom_apcs_ipc_remove(struct platform_device *pdev)
{
	struct qcom_apcs_ipc *apcs = platform_get_drvdata(pdev);

	mbox_controller_unregister(&apcs->mbox);

	return 0;
}

/* .data is the offset of the ipc register within the global block */
static const struct of_device_id qcom_apcs_ipc_of_match[] = {
	{ .compatible = "qcom,msm8916-apcs-kpss-global", .data = (void *)8 },
	{ .compatible = "qcom,msm8996-apcs-hmss-global", .data = (void *)16 },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_apcs_ipc_of_match);

static struct platform_driver qcom_apcs_ipc_driver = {
	.probe = qcom_apcs_ipc_probe,
	.remove = qcom_apcs_ipc_remove,
	.driver = {
		.name = "qcom_apcs_ipc",
		.of_match_table = qcom_apcs_ipc_of_match,
	},
};

static int __init qcom_apcs_ipc_init(void)
{
	return platform_driver_register(&qcom_apcs_ipc_driver);
}
postcore_initcall(qcom_apcs_ipc_init);

static void __exit qcom_apcs_ipc_exit(void)
{
	platform_driver_unregister(&qcom_apcs_ipc_driver);
}
module_exit(qcom_apcs_ipc_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm APCS IPC driver");

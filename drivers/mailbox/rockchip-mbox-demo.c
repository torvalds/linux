// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip MBOX Demo.
 *
 * Copyright (c) 2023 Rockchip Electronics Co. Ltd.
 * Author: Jiahang Zheng <jiahang.zheng@rock-chips.com>
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <soc/rockchip/rockchip-mailbox.h>
#include <linux/delay.h>

/*
 * The Linux kernel uses the mailbox framework TXDONE_BY_POLL mechanism.
 * The minimum unit of the txpoll period interface is ms.
 * Configure rockchip,txpoll-period-ms = <1> in dts.
 * If data that is longer than MBOX_TX_QUEUE_LEN may be lost,
 * each send should be at least interval txpoll-period-ms
 */
#define MSG_LIMIT (100)
#define LINUX_TEST_COMPENSATION	(1)

struct rk_mbox_dev {
	struct platform_device *pdev;
	struct mbox_client mbox_cl;
	struct mbox_chan *mbox_rx_chan;
	struct mbox_chan *mbox_tx_chan;
	struct rockchip_mbox_msg tx_msg;
	int rx_count;
};

static void rk_mbox_rx_callback(struct mbox_client *client, void *message)
{
	struct rk_mbox_dev *test_dev = container_of(client, struct rk_mbox_dev, mbox_cl);
	struct platform_device *pdev = test_dev->pdev;
	struct device *dev = &pdev->dev;
	struct rockchip_mbox_msg *tx_msg;
	struct rockchip_mbox_msg *rx_msg;

	rx_msg = message;
	dev_info(dev, "mbox master: rx_count:%d cmd=0x%x data=0x%x\n",
			++test_dev->rx_count, rx_msg->cmd, rx_msg->data);

	/* test should not live forever */
	if (test_dev->rx_count >= MSG_LIMIT) {
		dev_info(dev, "Rockchip mbox test exit!\n");
		return;
	}

	mdelay(LINUX_TEST_COMPENSATION);
	tx_msg = &test_dev->tx_msg;
	mbox_send_message(test_dev->mbox_tx_chan, tx_msg);
}

static int mbox_demo_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rk_mbox_dev *test_dev = NULL;
	struct mbox_client *cl;
	struct rockchip_mbox_msg *tx_msg;
	int ret = 0;

	test_dev = devm_kzalloc(dev, sizeof(*test_dev), GFP_KERNEL);
	if (!test_dev)
		return -ENOMEM;

	/* link_id: master core 0 and remote core 3 */
	tx_msg = &test_dev->tx_msg;
	tx_msg->cmd = 0x03U;
	tx_msg->data = 0x524D5347U;

	dev_info(dev, "rockchip mbox demo probe.\n");
	test_dev->pdev = pdev;
	test_dev->rx_count = 0;

	cl = &test_dev->mbox_cl;
	cl->dev = dev;
	cl->rx_callback = rk_mbox_rx_callback;

	platform_set_drvdata(pdev, test_dev);
	test_dev->mbox_rx_chan = mbox_request_channel_byname(cl, "test-rx");
	if (IS_ERR(test_dev->mbox_rx_chan)) {
		ret = PTR_ERR(test_dev->mbox_rx_chan);
		dev_err(dev, "failed to request mbox rx chan, ret %d\n", ret);
		return ret;
	}
	test_dev->mbox_tx_chan = mbox_request_channel_byname(cl, "test-tx");
	if (IS_ERR(test_dev->mbox_tx_chan)) {
		ret = PTR_ERR(test_dev->mbox_tx_chan);
		dev_err(dev, "failed to request mbox tx chan, ret %d\n", ret);
		return ret;
	}

	dev_info(dev, "mbox master: send cmd=0x%x data=0x%x\n", tx_msg->cmd, tx_msg->data);
	mbox_send_message(test_dev->mbox_tx_chan, tx_msg);

	return ret;
}

static int mbox_demo_remove(struct platform_device *pdev)
{
	struct rk_mbox_dev *test_dev = platform_get_drvdata(pdev);

	mbox_free_channel(test_dev->mbox_rx_chan);
	mbox_free_channel(test_dev->mbox_tx_chan);

	return 0;
}

static const struct of_device_id mbox_demo_match[] = {
	{ .compatible = "rockchip,mbox-demo", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mbox_demo_match);

static struct platform_driver mbox_demo_driver = {
	.probe = mbox_demo_probe,
	.remove = mbox_demo_remove,
	.driver = {
		.name = "mbox-demo",
		.of_match_table = mbox_demo_match,
	},
};
module_platform_driver(mbox_demo_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rockchip MBOX Demo");
MODULE_AUTHOR("Jiahang Zheng <jiahang.zheng@rock-chips.com>");

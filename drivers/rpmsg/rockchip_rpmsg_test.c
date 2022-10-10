// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip Remote Processors Messaging Test.
 *
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 * Author: Hongming Zou <hongming.zou@rock-chips.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/rpmsg/rockchip_rpmsg.h>
#include <linux/virtio.h>

#define LINUX_TEST_MSG_1 "Announce master ept id!"
#define LINUX_TEST_MSG_2 "Rockchip rpmsg linux test pingpong!"
#define MSG_LIMIT       100

struct instance_data {
	int rx_count;
};

static int rockchip_rpmsg_test_cb(struct rpmsg_device *rp, void *payload,
				  int payload_len, void *priv, u32 src)
{
	int ret;
	uint32_t remote_ept_id;
	struct instance_data *idata = dev_get_drvdata(&rp->dev);

	remote_ept_id = src;
	dev_info(&rp->dev, "rx msg %s rx_count %d(remote_ept_id: 0x%x)\n",
			(char *)payload, ++idata->rx_count, remote_ept_id);

	/* test should not live forever */
	if (idata->rx_count >= MSG_LIMIT) {
		dev_info(&rp->dev, "Rockchip rpmsg test exit!\n");
		return 0;
	}

	/* send a new message now */
	ret = rpmsg_sendto(rp->ept, LINUX_TEST_MSG_2, strlen(LINUX_TEST_MSG_2), remote_ept_id);
	if (ret)
		dev_err(&rp->dev, "rpmsg_send failed: %d\n", ret);
		return ret;
}

static int rockchip_rpmsg_test_probe(struct rpmsg_device *rp)
{
	int ret;
	uint32_t master_ept_id, remote_ept_id;
	struct instance_data *idata;

	master_ept_id = rp->src;
	remote_ept_id = rp->dst;
	dev_info(&rp->dev, "new channel: 0x%x -> 0x%x!\n", master_ept_id, remote_ept_id);

	idata = devm_kzalloc(&rp->dev, sizeof(*idata), GFP_KERNEL);
	if (!idata)
		return -ENOMEM;

	dev_set_drvdata(&rp->dev, idata);

	/*
	 * send a message to our remote processor, and tell remote
	 * processor about this channel
	 */
	ret = rpmsg_send(rp->ept, LINUX_TEST_MSG_1, strlen(LINUX_TEST_MSG_1));
	if (ret) {
		dev_err(&rp->dev, "rpmsg_send failed: %d\n", ret);
		return ret;
	}

	ret = rpmsg_sendto(rp->ept, LINUX_TEST_MSG_2, strlen(LINUX_TEST_MSG_2), remote_ept_id);
	if (ret) {
		dev_err(&rp->dev, "rpmsg_send failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static void rockchip_rpmsg_test_remove(struct rpmsg_device *rp)
{
	dev_info(&rp->dev, "rockchip rpmsg test is removed\n");
}

static struct rpmsg_device_id rockchip_rpmsg_test_id_table[] = {
	{ .name = "rpmsg-ap3-ch0" },
	{ /* sentinel */ },
};

static struct rpmsg_driver rockchip_rpmsg_test = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rockchip_rpmsg_test_id_table,
	.probe		= rockchip_rpmsg_test_probe,
	.callback	= rockchip_rpmsg_test_cb,
	.remove		= rockchip_rpmsg_test_remove,
};

static int __init init(void)
{
	return register_rpmsg_driver(&rockchip_rpmsg_test);
}

static void __exit fini(void)
{
	unregister_rpmsg_driver(&rockchip_rpmsg_test);
}
module_init(init);
module_exit(fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Rockchip Remote Processors Messaging Test");
MODULE_AUTHOR("Hongming Zou <hongming.zou@rock-chips.com>");


/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 *
 * derived from the omap-rpmsg implementation.
 * Remote processor messaging transport - pingpong driver
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/rpmsg.h>

#define MSG		"hello world!"
#define MSG_LIMIT	100000
static unsigned int rpmsg_pingpong;
static int rx_count;

static void rpmsg_pingpong_cb(struct rpmsg_channel *rpdev, void *data, int len,
						void *priv, u32 src)
{
	int err;

	/* reply */
	rpmsg_pingpong = *(unsigned int *)data;
	pr_info("get %d (src: 0x%x)\n",
			rpmsg_pingpong, src);
	rx_count++;

	/* pingpongs should not live forever */
	if (rx_count >= MSG_LIMIT) {
		dev_info(&rpdev->dev, "goodbye!\n");
		return;
	}
	rpmsg_pingpong++;
	err = rpmsg_sendto(rpdev, (void *)(&rpmsg_pingpong), 4, src);

	if (err)
		dev_err(&rpdev->dev, "rpmsg_send failed: %d\n", err);
}

static int rpmsg_pingpong_probe(struct rpmsg_channel *rpdev)
{
	int err;

	dev_info(&rpdev->dev, "new channel: 0x%x -> 0x%x!\n",
			rpdev->src, rpdev->dst);

	/*
	 * send a message to our remote processor, and tell remote
	 * processor about this channel
	 */
	err = rpmsg_send(rpdev, MSG, strlen(MSG));
	if (err) {
		dev_err(&rpdev->dev, "rpmsg_send failed: %d\n", err);
		return err;
	}

	rpmsg_pingpong = 0;
	rx_count = 0;
	err = rpmsg_sendto(rpdev, (void *)(&rpmsg_pingpong), 4, rpdev->dst);
	if (err) {
		dev_err(&rpdev->dev, "rpmsg_send failed: %d\n", err);
		return err;
	}

	return 0;
}

static void rpmsg_pingpong_remove(struct rpmsg_channel *rpdev)
{
	dev_info(&rpdev->dev, "rpmsg pingpong driver is removed\n");
}

static struct rpmsg_device_id rpmsg_driver_pingpong_id_table[] = {
	{ .name	= "rpmsg-openamp-demo-channel" },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_driver_pingpong_id_table);

static struct rpmsg_driver rpmsg_pingpong_driver = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_driver_pingpong_id_table,
	.probe		= rpmsg_pingpong_probe,
	.callback	= rpmsg_pingpong_cb,
	.remove		= rpmsg_pingpong_remove,
};

static int __init init(void)
{
	return register_rpmsg_driver(&rpmsg_pingpong_driver);
}

static void __exit fini(void)
{
	unregister_rpmsg_driver(&rpmsg_pingpong_driver);
}
module_init(init);
module_exit(fini);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("iMX virtio remote processor messaging pingpong driver");
MODULE_LICENSE("GPL v2");

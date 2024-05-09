// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip PolarFire SoC (MPFS) system controller driver
 *
 * Copyright (c) 2020-2021 Microchip Corporation. All rights reserved.
 *
 * Author: Conor Dooley <conor.dooley@microchip.com>
 *
 */

#include <linux/slab.h>
#include <linux/kref.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/mailbox_client.h>
#include <linux/platform_device.h>
#include <soc/microchip/mpfs.h>

/*
 * This timeout must be long, as some services (example: image authentication)
 * take significant time to complete
 */
#define MPFS_SYS_CTRL_TIMEOUT_MS 30000

static DEFINE_MUTEX(transaction_lock);

struct mpfs_sys_controller {
	struct mbox_client client;
	struct mbox_chan *chan;
	struct completion c;
	struct kref consumers;
};

int mpfs_blocking_transaction(struct mpfs_sys_controller *sys_controller, struct mpfs_mss_msg *msg)
{
	unsigned long timeout = msecs_to_jiffies(MPFS_SYS_CTRL_TIMEOUT_MS);
	int ret;

	ret = mutex_lock_interruptible(&transaction_lock);
	if (ret)
		return ret;

	reinit_completion(&sys_controller->c);

	ret = mbox_send_message(sys_controller->chan, msg);
	if (ret < 0) {
		dev_warn(sys_controller->client.dev, "MPFS sys controller service timeout\n");
		goto out;
	}

	/*
	 * Unfortunately, the system controller will only deliver an interrupt
	 * if a service succeeds. mbox_send_message() will block until the busy
	 * flag is gone. If the busy flag is gone but no interrupt has arrived
	 * to trigger the rx callback then the service can be deemed to have
	 * failed.
	 * The caller can then interrogate msg::response::resp_status to
	 * determine the cause of the failure.
	 * mbox_send_message() returns positive integers in the success path, so
	 * ret needs to be cleared if we do get an interrupt.
	 */
	if (!wait_for_completion_timeout(&sys_controller->c, timeout)) {
		ret = -EBADMSG;
		dev_warn(sys_controller->client.dev, "MPFS sys controller service failed\n");
	} else {
		ret = 0;
	}

out:
	mutex_unlock(&transaction_lock);

	return ret;
}
EXPORT_SYMBOL(mpfs_blocking_transaction);

static void mpfs_sys_controller_rx_callback(struct mbox_client *client, void *msg)
{
	struct mpfs_sys_controller *sys_controller =
		container_of(client, struct mpfs_sys_controller, client);

	complete(&sys_controller->c);
}

static void mpfs_sys_controller_delete(struct kref *kref)
{
	struct mpfs_sys_controller *sys_controller =
		container_of(kref, struct mpfs_sys_controller, consumers);

	mbox_free_channel(sys_controller->chan);
	kfree(sys_controller);
}

static void mpfs_sys_controller_put(void *data)
{
	struct mpfs_sys_controller *sys_controller = data;

	kref_put(&sys_controller->consumers, mpfs_sys_controller_delete);
}

static struct platform_device subdevs[] = {
	{
		.name		= "mpfs-rng",
		.id		= -1,
	},
	{
		.name		= "mpfs-generic-service",
		.id		= -1,
	}
};

static int mpfs_sys_controller_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mpfs_sys_controller *sys_controller;
	int i, ret;

	sys_controller = kzalloc(sizeof(*sys_controller), GFP_KERNEL);
	if (!sys_controller)
		return -ENOMEM;

	sys_controller->client.dev = dev;
	sys_controller->client.rx_callback = mpfs_sys_controller_rx_callback;
	sys_controller->client.tx_block = 1U;
	sys_controller->client.tx_tout = msecs_to_jiffies(MPFS_SYS_CTRL_TIMEOUT_MS);

	sys_controller->chan = mbox_request_channel(&sys_controller->client, 0);
	if (IS_ERR(sys_controller->chan)) {
		ret = dev_err_probe(dev, PTR_ERR(sys_controller->chan),
				    "Failed to get mbox channel\n");
		kfree(sys_controller);
		return ret;
	}

	init_completion(&sys_controller->c);
	kref_init(&sys_controller->consumers);

	platform_set_drvdata(pdev, sys_controller);

	dev_info(&pdev->dev, "Registered MPFS system controller\n");

	for (i = 0; i < ARRAY_SIZE(subdevs); i++) {
		subdevs[i].dev.parent = dev;
		if (platform_device_register(&subdevs[i]))
			dev_warn(dev, "Error registering sub device %s\n", subdevs[i].name);
	}

	return 0;
}

static void mpfs_sys_controller_remove(struct platform_device *pdev)
{
	struct mpfs_sys_controller *sys_controller = platform_get_drvdata(pdev);

	mpfs_sys_controller_put(sys_controller);
}

static const struct of_device_id mpfs_sys_controller_of_match[] = {
	{.compatible = "microchip,mpfs-sys-controller", },
	{},
};
MODULE_DEVICE_TABLE(of, mpfs_sys_controller_of_match);

struct mpfs_sys_controller *mpfs_sys_controller_get(struct device *dev)
{
	const struct of_device_id *match;
	struct mpfs_sys_controller *sys_controller;
	int ret;

	if (!dev->parent)
		goto err_no_device;

	match = of_match_node(mpfs_sys_controller_of_match,  dev->parent->of_node);
	of_node_put(dev->parent->of_node);
	if (!match)
		goto err_no_device;

	sys_controller = dev_get_drvdata(dev->parent);
	if (!sys_controller)
		goto err_bad_device;

	if (!kref_get_unless_zero(&sys_controller->consumers))
		goto err_bad_device;

	ret = devm_add_action_or_reset(dev, mpfs_sys_controller_put, sys_controller);
	if (ret)
		return ERR_PTR(ret);

	return sys_controller;

err_no_device:
	dev_dbg(dev, "Parent device was not an MPFS system controller\n");
	return ERR_PTR(-ENODEV);

err_bad_device:
	dev_dbg(dev, "MPFS system controller found but could not register as a sub device\n");
	return ERR_PTR(-EPROBE_DEFER);
}
EXPORT_SYMBOL(mpfs_sys_controller_get);

static struct platform_driver mpfs_sys_controller_driver = {
	.driver = {
		.name = "mpfs-sys-controller",
		.of_match_table = mpfs_sys_controller_of_match,
	},
	.probe = mpfs_sys_controller_probe,
	.remove_new = mpfs_sys_controller_remove,
};
module_platform_driver(mpfs_sys_controller_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Conor Dooley <conor.dooley@microchip.com>");
MODULE_DESCRIPTION("MPFS system controller driver");

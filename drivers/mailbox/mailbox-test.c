/*
 * Copyright (C) 2015 ST Microelectronics
 *
 * Author: Lee Jones <lee.jones@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define MBOX_MAX_SIG_LEN	8
#define MBOX_MAX_MSG_LEN	128
#define MBOX_BYTES_PER_LINE	16
#define MBOX_HEXDUMP_LINE_LEN	((MBOX_BYTES_PER_LINE * 4) + 2)
#define MBOX_HEXDUMP_MAX_LEN	(MBOX_HEXDUMP_LINE_LEN *		\
				 (MBOX_MAX_MSG_LEN / MBOX_BYTES_PER_LINE))

static struct dentry *root_debugfs_dir;

struct mbox_test_device {
	struct device		*dev;
	void __iomem		*tx_mmio;
	void __iomem		*rx_mmio;
	struct mbox_chan	*tx_channel;
	struct mbox_chan	*rx_channel;
	char			*rx_buffer;
	char			*signal;
	char			*message;
	spinlock_t		lock;
};

static ssize_t mbox_test_signal_write(struct file *filp,
				       const char __user *userbuf,
				       size_t count, loff_t *ppos)
{
	struct mbox_test_device *tdev = filp->private_data;
	int ret;

	if (!tdev->tx_channel) {
		dev_err(tdev->dev, "Channel cannot do Tx\n");
		return -EINVAL;
	}

	if (count > MBOX_MAX_SIG_LEN) {
		dev_err(tdev->dev,
			"Signal length %zd greater than max allowed %d\n",
			count, MBOX_MAX_SIG_LEN);
		return -EINVAL;
	}

	tdev->signal = kzalloc(MBOX_MAX_SIG_LEN, GFP_KERNEL);
	if (!tdev->signal)
		return -ENOMEM;

	ret = copy_from_user(tdev->signal, userbuf, count);
	if (ret) {
		kfree(tdev->signal);
		return -EFAULT;
	}

	return ret < 0 ? ret : count;
}

static const struct file_operations mbox_test_signal_ops = {
	.write	= mbox_test_signal_write,
	.open	= simple_open,
	.llseek	= generic_file_llseek,
};

static ssize_t mbox_test_message_write(struct file *filp,
				       const char __user *userbuf,
				       size_t count, loff_t *ppos)
{
	struct mbox_test_device *tdev = filp->private_data;
	void *data;
	int ret;

	if (!tdev->tx_channel) {
		dev_err(tdev->dev, "Channel cannot do Tx\n");
		return -EINVAL;
	}

	if (count > MBOX_MAX_MSG_LEN) {
		dev_err(tdev->dev,
			"Message length %zd greater than max allowed %d\n",
			count, MBOX_MAX_MSG_LEN);
		return -EINVAL;
	}

	tdev->message = kzalloc(MBOX_MAX_MSG_LEN, GFP_KERNEL);
	if (!tdev->message)
		return -ENOMEM;

	ret = copy_from_user(tdev->message, userbuf, count);
	if (ret) {
		ret = -EFAULT;
		goto out;
	}

	/*
	 * A separate signal is only of use if there is
	 * MMIO to subsequently pass the message through
	 */
	if (tdev->tx_mmio && tdev->signal) {
		print_hex_dump_bytes("Client: Sending: Signal: ", DUMP_PREFIX_ADDRESS,
				     tdev->signal, MBOX_MAX_SIG_LEN);

		data = tdev->signal;
	} else
		data = tdev->message;

	print_hex_dump_bytes("Client: Sending: Message: ", DUMP_PREFIX_ADDRESS,
			     tdev->message, MBOX_MAX_MSG_LEN);

	ret = mbox_send_message(tdev->tx_channel, data);
	if (ret < 0)
		dev_err(tdev->dev, "Failed to send message via mailbox\n");

out:
	kfree(tdev->signal);
	kfree(tdev->message);

	return ret < 0 ? ret : count;
}

static ssize_t mbox_test_message_read(struct file *filp, char __user *userbuf,
				      size_t count, loff_t *ppos)
{
	struct mbox_test_device *tdev = filp->private_data;
	unsigned long flags;
	char *touser, *ptr;
	int l = 0;
	int ret;

	touser = kzalloc(MBOX_HEXDUMP_MAX_LEN + 1, GFP_KERNEL);
	if (!touser)
		return -ENOMEM;

	if (!tdev->rx_channel) {
		ret = snprintf(touser, 20, "<NO RX CAPABILITY>\n");
		ret = simple_read_from_buffer(userbuf, count, ppos,
					      touser, ret);
		goto out;
	}

	if (tdev->rx_buffer[0] == '\0') {
		ret = snprintf(touser, 9, "<EMPTY>\n");
		ret = simple_read_from_buffer(userbuf, count, ppos,
					      touser, ret);
		goto out;
	}

	spin_lock_irqsave(&tdev->lock, flags);

	ptr = tdev->rx_buffer;
	while (l < MBOX_HEXDUMP_MAX_LEN) {
		hex_dump_to_buffer(ptr,
				   MBOX_BYTES_PER_LINE,
				   MBOX_BYTES_PER_LINE, 1, touser + l,
				   MBOX_HEXDUMP_LINE_LEN, true);

		ptr += MBOX_BYTES_PER_LINE;
		l += MBOX_HEXDUMP_LINE_LEN;
		*(touser + (l - 1)) = '\n';
	}
	*(touser + l) = '\0';

	memset(tdev->rx_buffer, 0, MBOX_MAX_MSG_LEN);

	spin_unlock_irqrestore(&tdev->lock, flags);

	ret = simple_read_from_buffer(userbuf, count, ppos, touser, MBOX_HEXDUMP_MAX_LEN);
out:
	kfree(touser);
	return ret;
}

static const struct file_operations mbox_test_message_ops = {
	.write	= mbox_test_message_write,
	.read	= mbox_test_message_read,
	.open	= simple_open,
	.llseek	= generic_file_llseek,
};

static int mbox_test_add_debugfs(struct platform_device *pdev,
				 struct mbox_test_device *tdev)
{
	if (!debugfs_initialized())
		return 0;

	root_debugfs_dir = debugfs_create_dir("mailbox", NULL);
	if (!root_debugfs_dir) {
		dev_err(&pdev->dev, "Failed to create Mailbox debugfs\n");
		return -EINVAL;
	}

	debugfs_create_file("message", 0600, root_debugfs_dir,
			    tdev, &mbox_test_message_ops);

	debugfs_create_file("signal", 0200, root_debugfs_dir,
			    tdev, &mbox_test_signal_ops);

	return 0;
}

static void mbox_test_receive_message(struct mbox_client *client, void *message)
{
	struct mbox_test_device *tdev = dev_get_drvdata(client->dev);
	unsigned long flags;

	spin_lock_irqsave(&tdev->lock, flags);
	if (tdev->rx_mmio) {
		memcpy_fromio(tdev->rx_buffer, tdev->rx_mmio, MBOX_MAX_MSG_LEN);
		print_hex_dump_bytes("Client: Received [MMIO]: ", DUMP_PREFIX_ADDRESS,
				     tdev->rx_buffer, MBOX_MAX_MSG_LEN);
	} else if (message) {
		print_hex_dump_bytes("Client: Received [API]: ", DUMP_PREFIX_ADDRESS,
				     message, MBOX_MAX_MSG_LEN);
		memcpy(tdev->rx_buffer, message, MBOX_MAX_MSG_LEN);
	}
	spin_unlock_irqrestore(&tdev->lock, flags);
}

static void mbox_test_prepare_message(struct mbox_client *client, void *message)
{
	struct mbox_test_device *tdev = dev_get_drvdata(client->dev);

	if (tdev->tx_mmio) {
		if (tdev->signal)
			memcpy_toio(tdev->tx_mmio, tdev->message, MBOX_MAX_MSG_LEN);
		else
			memcpy_toio(tdev->tx_mmio, message, MBOX_MAX_MSG_LEN);
	}
}

static void mbox_test_message_sent(struct mbox_client *client,
				   void *message, int r)
{
	if (r)
		dev_warn(client->dev,
			 "Client: Message could not be sent: %d\n", r);
	else
		dev_info(client->dev,
			 "Client: Message sent\n");
}

static struct mbox_chan *
mbox_test_request_channel(struct platform_device *pdev, const char *name)
{
	struct mbox_client *client;
	struct mbox_chan *channel;

	client = devm_kzalloc(&pdev->dev, sizeof(*client), GFP_KERNEL);
	if (!client)
		return ERR_PTR(-ENOMEM);

	client->dev		= &pdev->dev;
	client->rx_callback	= mbox_test_receive_message;
	client->tx_prepare	= mbox_test_prepare_message;
	client->tx_done		= mbox_test_message_sent;
	client->tx_block	= true;
	client->knows_txdone	= false;
	client->tx_tout		= 500;

	channel = mbox_request_channel_byname(client, name);
	if (IS_ERR(channel)) {
		dev_warn(&pdev->dev, "Failed to request %s channel\n", name);
		return NULL;
	}

	return channel;
}

static int mbox_test_probe(struct platform_device *pdev)
{
	struct mbox_test_device *tdev;
	struct resource *res;
	int ret;

	tdev = devm_kzalloc(&pdev->dev, sizeof(*tdev), GFP_KERNEL);
	if (!tdev)
		return -ENOMEM;

	/* It's okay for MMIO to be NULL */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tdev->tx_mmio = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(tdev->tx_mmio))
		tdev->tx_mmio = NULL;

	/* If specified, second reg entry is Rx MMIO */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	tdev->rx_mmio = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(tdev->rx_mmio))
		tdev->rx_mmio = tdev->tx_mmio;

	tdev->tx_channel = mbox_test_request_channel(pdev, "tx");
	tdev->rx_channel = mbox_test_request_channel(pdev, "rx");

	if (!tdev->tx_channel && !tdev->rx_channel)
		return -EPROBE_DEFER;

	/* If Rx is not specified but has Rx MMIO, then Rx = Tx */
	if (!tdev->rx_channel && (tdev->rx_mmio != tdev->tx_mmio))
		tdev->rx_channel = tdev->tx_channel;

	tdev->dev = &pdev->dev;
	platform_set_drvdata(pdev, tdev);

	spin_lock_init(&tdev->lock);

	if (tdev->rx_channel) {
		tdev->rx_buffer = devm_kzalloc(&pdev->dev,
					       MBOX_MAX_MSG_LEN, GFP_KERNEL);
		if (!tdev->rx_buffer)
			return -ENOMEM;
	}

	ret = mbox_test_add_debugfs(pdev, tdev);
	if (ret)
		return ret;

	dev_info(&pdev->dev, "Successfully registered\n");

	return 0;
}

static int mbox_test_remove(struct platform_device *pdev)
{
	struct mbox_test_device *tdev = platform_get_drvdata(pdev);

	debugfs_remove_recursive(root_debugfs_dir);

	if (tdev->tx_channel)
		mbox_free_channel(tdev->tx_channel);
	if (tdev->rx_channel)
		mbox_free_channel(tdev->rx_channel);

	return 0;
}

static const struct of_device_id mbox_test_match[] = {
	{ .compatible = "mailbox-test" },
	{},
};

static struct platform_driver mbox_test_driver = {
	.driver = {
		.name = "mailbox_test",
		.of_match_table = mbox_test_match,
	},
	.probe  = mbox_test_probe,
	.remove = mbox_test_remove,
};
module_platform_driver(mbox_test_driver);

MODULE_DESCRIPTION("Generic Mailbox Testing Facility");
MODULE_AUTHOR("Lee Jones <lee.jones@linaro.org");
MODULE_LICENSE("GPL v2");

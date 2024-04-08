// SPDX-License-Identifier: GPL-2.0+
/*
 * UART Link Layer for S3FWRN82 NCI based Driver
 *
 * Copyright (C) 2015 Samsung Electronics
 * Robert Baldyga <r.baldyga@samsung.com>
 * Copyright (C) 2020 Samsung Electronics
 * Bongsu Jeon <bongsu.jeon@samsung.com>
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nfc.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/serdev.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include "phy_common.h"

#define S3FWRN82_NCI_HEADER 3
#define S3FWRN82_NCI_IDX 2
#define NCI_SKB_BUFF_LEN 258

struct s3fwrn82_uart_phy {
	struct phy_common common;
	struct serdev_device *ser_dev;
	struct sk_buff *recv_skb;
};

static int s3fwrn82_uart_write(void *phy_id, struct sk_buff *out)
{
	struct s3fwrn82_uart_phy *phy = phy_id;
	int err;

	err = serdev_device_write(phy->ser_dev,
				  out->data, out->len,
				  MAX_SCHEDULE_TIMEOUT);
	if (err < 0)
		return err;

	return 0;
}

static const struct s3fwrn5_phy_ops uart_phy_ops = {
	.set_wake = s3fwrn5_phy_set_wake,
	.set_mode = s3fwrn5_phy_set_mode,
	.get_mode = s3fwrn5_phy_get_mode,
	.write = s3fwrn82_uart_write,
};

static size_t s3fwrn82_uart_read(struct serdev_device *serdev,
				 const u8 *data, size_t count)
{
	struct s3fwrn82_uart_phy *phy = serdev_device_get_drvdata(serdev);
	size_t i;

	for (i = 0; i < count; i++) {
		skb_put_u8(phy->recv_skb, *data++);

		if (phy->recv_skb->len < S3FWRN82_NCI_HEADER)
			continue;

		if ((phy->recv_skb->len - S3FWRN82_NCI_HEADER)
				< phy->recv_skb->data[S3FWRN82_NCI_IDX])
			continue;

		s3fwrn5_recv_frame(phy->common.ndev, phy->recv_skb,
				   phy->common.mode);
		phy->recv_skb = alloc_skb(NCI_SKB_BUFF_LEN, GFP_KERNEL);
		if (!phy->recv_skb)
			return 0;
	}

	return i;
}

static const struct serdev_device_ops s3fwrn82_serdev_ops = {
	.receive_buf = s3fwrn82_uart_read,
	.write_wakeup = serdev_device_write_wakeup,
};

static const struct of_device_id s3fwrn82_uart_of_match[] = {
	{ .compatible = "samsung,s3fwrn82", },
	{},
};
MODULE_DEVICE_TABLE(of, s3fwrn82_uart_of_match);

static int s3fwrn82_uart_parse_dt(struct serdev_device *serdev)
{
	struct s3fwrn82_uart_phy *phy = serdev_device_get_drvdata(serdev);
	struct device_node *np = serdev->dev.of_node;

	if (!np)
		return -ENODEV;

	phy->common.gpio_en = of_get_named_gpio(np, "en-gpios", 0);
	if (!gpio_is_valid(phy->common.gpio_en))
		return -ENODEV;

	phy->common.gpio_fw_wake = of_get_named_gpio(np, "wake-gpios", 0);
	if (!gpio_is_valid(phy->common.gpio_fw_wake))
		return -ENODEV;

	return 0;
}

static int s3fwrn82_uart_probe(struct serdev_device *serdev)
{
	struct s3fwrn82_uart_phy *phy;
	int ret = -ENOMEM;

	phy = devm_kzalloc(&serdev->dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		goto err_exit;

	phy->recv_skb = alloc_skb(NCI_SKB_BUFF_LEN, GFP_KERNEL);
	if (!phy->recv_skb)
		goto err_exit;

	mutex_init(&phy->common.mutex);
	phy->common.mode = S3FWRN5_MODE_COLD;

	phy->ser_dev = serdev;
	serdev_device_set_drvdata(serdev, phy);
	serdev_device_set_client_ops(serdev, &s3fwrn82_serdev_ops);
	ret = serdev_device_open(serdev);
	if (ret) {
		dev_err(&serdev->dev, "Unable to open device\n");
		goto err_skb;
	}

	ret = serdev_device_set_baudrate(serdev, 115200);
	if (ret != 115200) {
		ret = -EINVAL;
		goto err_serdev;
	}

	serdev_device_set_flow_control(serdev, false);

	ret = s3fwrn82_uart_parse_dt(serdev);
	if (ret < 0)
		goto err_serdev;

	ret = devm_gpio_request_one(&phy->ser_dev->dev, phy->common.gpio_en,
				    GPIOF_OUT_INIT_HIGH, "s3fwrn82_en");
	if (ret < 0)
		goto err_serdev;

	ret = devm_gpio_request_one(&phy->ser_dev->dev,
				    phy->common.gpio_fw_wake,
				    GPIOF_OUT_INIT_LOW, "s3fwrn82_fw_wake");
	if (ret < 0)
		goto err_serdev;

	ret = s3fwrn5_probe(&phy->common.ndev, phy, &phy->ser_dev->dev,
			    &uart_phy_ops);
	if (ret < 0)
		goto err_serdev;

	return ret;

err_serdev:
	serdev_device_close(serdev);
err_skb:
	kfree_skb(phy->recv_skb);
err_exit:
	return ret;
}

static void s3fwrn82_uart_remove(struct serdev_device *serdev)
{
	struct s3fwrn82_uart_phy *phy = serdev_device_get_drvdata(serdev);

	s3fwrn5_remove(phy->common.ndev);
	serdev_device_close(serdev);
	kfree_skb(phy->recv_skb);
}

static struct serdev_device_driver s3fwrn82_uart_driver = {
	.probe = s3fwrn82_uart_probe,
	.remove = s3fwrn82_uart_remove,
	.driver = {
		.name = "s3fwrn82_uart",
		.of_match_table = s3fwrn82_uart_of_match,
	},
};

module_serdev_device_driver(s3fwrn82_uart_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("UART driver for Samsung NFC");
MODULE_AUTHOR("Bongsu Jeon <bongsu.jeon@samsung.com>");

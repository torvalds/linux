// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for NXP PN532 NFC Chip - UART transport layer
 *
 * Copyright (C) 2018 Lemonage Software GmbH
 * Author: Lars Pöschel <poeschel@lemonage.de>
 * All rights reserved.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nfc.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/serdev.h>
#include "pn533.h"

#define PN532_UART_SKB_BUFF_LEN	(PN533_CMD_DATAEXCH_DATA_MAXLEN * 2)

enum send_wakeup {
	PN532_SEND_NO_WAKEUP = 0,
	PN532_SEND_WAKEUP,
	PN532_SEND_LAST_WAKEUP,
};


struct pn532_uart_phy {
	struct serdev_device *serdev;
	struct sk_buff *recv_skb;
	struct pn533 *priv;
	/*
	 * send_wakeup variable is used to control if we need to send a wakeup
	 * request to the pn532 chip prior to our actual command. There is a
	 * little propability of a race condition. We decided to not mutex the
	 * variable as the worst that could happen is, that we send a wakeup
	 * to the chip that is already awake. This does not hurt. It is a
	 * no-op to the chip.
	 */
	enum send_wakeup send_wakeup;
	struct timer_list cmd_timeout;
	struct sk_buff *cur_out_buf;
};

static int pn532_uart_send_frame(struct pn533 *dev,
				struct sk_buff *out)
{
	/* wakeup sequence and dummy bytes for waiting time */
	static const u8 wakeup[] = {
		0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	struct pn532_uart_phy *pn532 = dev->phy;
	int err;

	print_hex_dump_debug("PN532_uart TX: ", DUMP_PREFIX_NONE, 16, 1,
			     out->data, out->len, false);

	pn532->cur_out_buf = out;
	if (pn532->send_wakeup) {
		err = serdev_device_write(pn532->serdev,
				wakeup, sizeof(wakeup),
				MAX_SCHEDULE_TIMEOUT);
		if (err < 0)
			return err;
	}

	if (pn532->send_wakeup == PN532_SEND_LAST_WAKEUP)
		pn532->send_wakeup = PN532_SEND_NO_WAKEUP;

	err = serdev_device_write(pn532->serdev, out->data, out->len,
			MAX_SCHEDULE_TIMEOUT);
	if (err < 0)
		return err;

	mod_timer(&pn532->cmd_timeout, HZ / 40 + jiffies);
	return 0;
}

static int pn532_uart_send_ack(struct pn533 *dev, gfp_t flags)
{
	/* spec 7.1.1.3:  Preamble, SoPC (2), ACK Code (2), Postamble */
	static const u8 ack[PN533_STD_FRAME_ACK_SIZE] = {
			0x00, 0x00, 0xff, 0x00, 0xff, 0x00};
	struct pn532_uart_phy *pn532 = dev->phy;
	int err;

	err = serdev_device_write(pn532->serdev, ack, sizeof(ack),
			MAX_SCHEDULE_TIMEOUT);
	if (err < 0)
		return err;

	return 0;
}

static void pn532_uart_abort_cmd(struct pn533 *dev, gfp_t flags)
{
	/* An ack will cancel the last issued command */
	pn532_uart_send_ack(dev, flags);
	/* schedule cmd_complete_work to finish current command execution */
	pn533_recv_frame(dev, NULL, -ENOENT);
}

static int pn532_dev_up(struct pn533 *dev)
{
	struct pn532_uart_phy *pn532 = dev->phy;
	int ret = 0;

	ret = serdev_device_open(pn532->serdev);
	if (ret)
		return ret;

	pn532->send_wakeup = PN532_SEND_LAST_WAKEUP;
	return ret;
}

static int pn532_dev_down(struct pn533 *dev)
{
	struct pn532_uart_phy *pn532 = dev->phy;

	serdev_device_close(pn532->serdev);
	pn532->send_wakeup = PN532_SEND_WAKEUP;

	return 0;
}

static struct pn533_phy_ops uart_phy_ops = {
	.send_frame = pn532_uart_send_frame,
	.send_ack = pn532_uart_send_ack,
	.abort_cmd = pn532_uart_abort_cmd,
	.dev_up = pn532_dev_up,
	.dev_down = pn532_dev_down,
};

static void pn532_cmd_timeout(struct timer_list *t)
{
	struct pn532_uart_phy *dev = from_timer(dev, t, cmd_timeout);

	pn532_uart_send_frame(dev->priv, dev->cur_out_buf);
}

/*
 * scans the buffer if it contains a pn532 frame. It is not checked if the
 * frame is really valid. This is later done with pn533_rx_frame_is_valid.
 * This is useful for malformed or errornous transmitted frames. Adjusts the
 * bufferposition where the frame starts, since pn533_recv_frame expects a
 * well formed frame.
 */
static int pn532_uart_rx_is_frame(struct sk_buff *skb)
{
	struct pn533_std_frame *std;
	struct pn533_ext_frame *ext;
	u16 frame_len;
	int i;

	for (i = 0; i + PN533_STD_FRAME_ACK_SIZE <= skb->len; i++) {
		std = (struct pn533_std_frame *)&skb->data[i];
		/* search start code */
		if (std->start_frame != cpu_to_be16(PN533_STD_FRAME_SOF))
			continue;

		/* frame type */
		switch (std->datalen) {
		case PN533_FRAME_DATALEN_ACK:
			if (std->datalen_checksum == 0xff) {
				skb_pull(skb, i);
				return 1;
			}

			break;
		case PN533_FRAME_DATALEN_ERROR:
			if ((std->datalen_checksum == 0xff) &&
					(skb->len >=
					 PN533_STD_ERROR_FRAME_SIZE)) {
				skb_pull(skb, i);
				return 1;
			}

			break;
		case PN533_FRAME_DATALEN_EXTENDED:
			ext = (struct pn533_ext_frame *)&skb->data[i];
			frame_len = be16_to_cpu(ext->datalen);
			if (skb->len >= frame_len +
					sizeof(struct pn533_ext_frame) +
					2 /* CKS + Postamble */) {
				skb_pull(skb, i);
				return 1;
			}

			break;
		default: /* normal information frame */
			frame_len = std->datalen;
			if (skb->len >= frame_len +
					sizeof(struct pn533_std_frame) +
					2 /* CKS + Postamble */) {
				skb_pull(skb, i);
				return 1;
			}

			break;
		}
	}

	return 0;
}

static int pn532_receive_buf(struct serdev_device *serdev,
		const unsigned char *data, size_t count)
{
	struct pn532_uart_phy *dev = serdev_device_get_drvdata(serdev);
	size_t i;

	del_timer(&dev->cmd_timeout);
	for (i = 0; i < count; i++) {
		skb_put_u8(dev->recv_skb, *data++);
		if (!pn532_uart_rx_is_frame(dev->recv_skb))
			continue;

		pn533_recv_frame(dev->priv, dev->recv_skb, 0);
		dev->recv_skb = alloc_skb(PN532_UART_SKB_BUFF_LEN, GFP_KERNEL);
		if (!dev->recv_skb)
			return 0;
	}

	return i;
}

static struct serdev_device_ops pn532_serdev_ops = {
	.receive_buf = pn532_receive_buf,
	.write_wakeup = serdev_device_write_wakeup,
};

static const struct of_device_id pn532_uart_of_match[] = {
	{ .compatible = "nxp,pn532", },
	{},
};
MODULE_DEVICE_TABLE(of, pn532_uart_of_match);

static int pn532_uart_probe(struct serdev_device *serdev)
{
	struct pn532_uart_phy *pn532;
	struct pn533 *priv;
	int err;

	err = -ENOMEM;
	pn532 = kzalloc(sizeof(*pn532), GFP_KERNEL);
	if (!pn532)
		goto err_exit;

	pn532->recv_skb = alloc_skb(PN532_UART_SKB_BUFF_LEN, GFP_KERNEL);
	if (!pn532->recv_skb)
		goto err_free;

	pn532->serdev = serdev;
	serdev_device_set_drvdata(serdev, pn532);
	serdev_device_set_client_ops(serdev, &pn532_serdev_ops);
	err = serdev_device_open(serdev);
	if (err) {
		dev_err(&serdev->dev, "Unable to open device\n");
		goto err_skb;
	}

	err = serdev_device_set_baudrate(serdev, 115200);
	if (err != 115200) {
		err = -EINVAL;
		goto err_serdev;
	}

	serdev_device_set_flow_control(serdev, false);
	pn532->send_wakeup = PN532_SEND_WAKEUP;
	timer_setup(&pn532->cmd_timeout, pn532_cmd_timeout, 0);
	priv = pn53x_common_init(PN533_DEVICE_PN532_AUTOPOLL,
				     PN533_PROTO_REQ_ACK_RESP,
				     pn532, &uart_phy_ops, NULL,
				     &pn532->serdev->dev);
	if (IS_ERR(priv)) {
		err = PTR_ERR(priv);
		goto err_serdev;
	}

	pn532->priv = priv;
	err = pn533_finalize_setup(pn532->priv);
	if (err)
		goto err_clean;

	serdev_device_close(serdev);
	err = pn53x_register_nfc(priv, PN533_NO_TYPE_B_PROTOCOLS, &serdev->dev);
	if (err) {
		pn53x_common_clean(pn532->priv);
		goto err_skb;
	}

	return err;

err_clean:
	pn53x_common_clean(pn532->priv);
err_serdev:
	serdev_device_close(serdev);
err_skb:
	kfree_skb(pn532->recv_skb);
err_free:
	kfree(pn532);
err_exit:
	return err;
}

static void pn532_uart_remove(struct serdev_device *serdev)
{
	struct pn532_uart_phy *pn532 = serdev_device_get_drvdata(serdev);

	pn53x_unregister_nfc(pn532->priv);
	serdev_device_close(serdev);
	pn53x_common_clean(pn532->priv);
	del_timer_sync(&pn532->cmd_timeout);
	kfree_skb(pn532->recv_skb);
	kfree(pn532);
}

static struct serdev_device_driver pn532_uart_driver = {
	.probe = pn532_uart_probe,
	.remove = pn532_uart_remove,
	.driver = {
		.name = "pn532_uart",
		.of_match_table = of_match_ptr(pn532_uart_of_match),
	},
};

module_serdev_device_driver(pn532_uart_driver);

MODULE_AUTHOR("Lars Pöschel <poeschel@lemonage.de>");
MODULE_DESCRIPTION("PN532 UART driver");
MODULE_LICENSE("GPL");

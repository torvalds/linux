// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"tmecom: [%s][%d]:" fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mailbox_client.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/platform_device.h>
#include <linux/mailbox/qmp.h>
#include <linux/uaccess.h>
#include <linux/mailbox_controller.h>

#include "tmecom.h"

struct tmecom {
	struct device *dev;
	struct mbox_client cl;
	struct mbox_chan *chan;
	struct mutex lock;
	struct qmp_pkt pkt;
	wait_queue_head_t waitq;
	void *txbuf;
	bool rx_done;
};

#if IS_ENABLED(CONFIG_DEBUG_FS)
#include <linux/tme_hwkm_master_defs.h>
#include <linux/tme_hwkm_master.h>

char dpkt[MBOX_MAX_MSG_LEN + 1];
struct dentry *debugfs_file;
#endif /* CONFIG_DEBUG_FS */

static struct tmecom *tmedev;

/**
 * tmecom_msg_hdr - Request/Response message header between HLOS and TME.
 *
 * This header is proceeding any request specific parameters.
 * The transaction id is used to match request with response.
 *
 * Note: glink/QMP layer provides the rx/tx data size, so user payload size
 * is calculated by reducing the header size.
 */
struct tmecom_msg_hdr {
	unsigned int reserved; /* for future use */
	unsigned int txnid;    /* transaction id */
} __packed;
#define TMECOM_TX_HDR_SIZE sizeof(struct tmecom_msg_hdr)
#define CBOR_NUM_BYTES (sizeof(unsigned int))
#define TMECOM_RX_HDR_SIZE (TMECOM_TX_HDR_SIZE + CBOR_NUM_BYTES)

/*
 * CBOR encode emulation
 * Prepend tmecom_msg_hdr space
 * CBOR tag is prepended in request
 */
static inline size_t tmecom_encode(struct tmecom *tdev, const void *reqbuf,
		size_t size)
{
	unsigned int *msg = tdev->txbuf + TMECOM_TX_HDR_SIZE;
	unsigned int *src = (unsigned int *)reqbuf;

	memcpy(msg, src, size);
	return (size + TMECOM_TX_HDR_SIZE);
}

/*
 * CBOR decode emulation
 * Strip tmecom_msg_hdr & CBOR tag
 */
static inline size_t tmecom_decode(struct tmecom *tdev, void *respbuf)
{
	unsigned int *msg = tdev->pkt.data + TMECOM_RX_HDR_SIZE;
	unsigned int *rbuf = (unsigned int *)respbuf;

	memcpy(rbuf, msg, (tdev->pkt.size - TMECOM_RX_HDR_SIZE));
	return (tdev->pkt.size - TMECOM_RX_HDR_SIZE);
}

static bool tmecom_check_rx_done(struct tmecom *tdev)
{
	return  tdev->rx_done;
}

int tmecom_process_request(const void *reqbuf, size_t reqsize, void *respbuf,
		size_t *respsize)
{
	struct tmecom *tdev = tmedev;
	long time_left = 0;
	int ret = 0;

	/*
	 * Check to handle if probe is not successful or not completed yet
	 */
	if (!tdev) {
		pr_err("%s: tmecom dev is NULL\n", __func__);
		return -ENODEV;
	}

	if (!reqbuf || !reqsize || (reqsize > MBOX_MAX_MSG_LEN)) {
		dev_err(tdev->dev, "invalid reqbuf or reqsize\n");
		return -EINVAL;
	}

	if (!respbuf || !respsize || (*respsize > MBOX_MAX_MSG_LEN)) {
		dev_err(tdev->dev, "invalid respbuf or respsize\n");
		return -EINVAL;
	}

	mutex_lock(&tdev->lock);

	tdev->rx_done = false;
	tdev->pkt.size = tmecom_encode(tdev, reqbuf, reqsize);
	/*
	 * Controller expects a 4 byte aligned buffer
	 */
	tdev->pkt.size = (tdev->pkt.size + 0x3) & ~0x3;
	tdev->pkt.data = tdev->txbuf;

	pr_debug("tmecom encoded request size = %u\n", tdev->pkt.size);
	print_hex_dump_bytes("tmecom sending bytes : ",
			DUMP_PREFIX_ADDRESS, tdev->pkt.data, tdev->pkt.size);

	if (mbox_send_message(tdev->chan, &tdev->pkt) < 0) {
		dev_err(tdev->dev, "failed to send qmp message\n");
		ret = -EAGAIN;
		goto err_exit;
	}

	time_left = wait_event_interruptible_timeout(tdev->waitq,
			tmecom_check_rx_done(tdev), tdev->cl.tx_tout);

	if (!time_left) {
		dev_err(tdev->dev, "request timed out\n");
		ret = -ETIMEDOUT;
		goto err_exit;
	}

	dev_info(tdev->dev, "response received\n");

	pr_debug("tmecom received size = %u\n", tdev->pkt.size);
	print_hex_dump_bytes("tmecom received bytes : ",
			DUMP_PREFIX_ADDRESS, tdev->pkt.data, tdev->pkt.size);

	if (tdev->pkt.size <= TMECOM_RX_HDR_SIZE) {
		dev_err(tdev->dev, "invalid pkt.size received\n");
		ret = -EPROTO;
		goto err_exit;
	}

	*respsize = tmecom_decode(tdev, respbuf);

	tdev->rx_done = false;
	ret = 0;

err_exit:
	mutex_unlock(&tdev->lock);
	return ret;
}
EXPORT_SYMBOL(tmecom_process_request);

#if IS_ENABLED(CONFIG_DEBUG_FS)
static ssize_t tmecom_debugfs_write(struct file *file,
		const char __user *userstr, size_t len, loff_t *pos)
{
	int ret = 0;
	size_t rxlen = 0;
	struct tme_ext_err_info *err_info = (struct tme_ext_err_info *)dpkt;


	if (!len || (len > MBOX_MAX_MSG_LEN)) {
		pr_err("invalid message length\n");
		return -EINVAL;
	}

	memset(dpkt, 0, sizeof(*dpkt));
	ret = copy_from_user(dpkt, userstr, len);
	if (ret) {
		pr_err("%s copy from user failed, ret=%d\n", __func__, ret);
		return len;
	}

	tmecom_process_request(dpkt, len, dpkt, &rxlen);

	print_hex_dump_bytes("tmecom decoded bytes : ",
			DUMP_PREFIX_ADDRESS, dpkt, rxlen);

	pr_debug("calling TME_HWKM_CMD_BROADCAST_TP_KEY api\n");
	ret = tme_hwkm_master_broadcast_transportkey(err_info);

	if (ret == 0)
		pr_debug("%s successful\n", __func__);

	return len;
}

static const struct file_operations tmecom_debugfs_ops = {
	.open = simple_open,
	.write = tmecom_debugfs_write,
};
#endif /* CONFIG_DEBUG_FS */

static void tmecom_receive_message(struct mbox_client *client, void *message)
{
	struct tmecom *tdev = dev_get_drvdata(client->dev);
	struct qmp_pkt *pkt = NULL;

	if (!message) {
		dev_err(tdev->dev, "spurious message received\n");
		goto tmecom_receive_end;
	}

	if (tdev->rx_done) {
		dev_err(tdev->dev, "tmecom response pending\n");
		goto tmecom_receive_end;
	}
	pkt = (struct qmp_pkt *)message;
	tdev->pkt.size = pkt->size;
	tdev->pkt.data = pkt->data;
	tdev->rx_done = true;
tmecom_receive_end:
	wake_up_interruptible(&tdev->waitq);
}

static int tmecom_probe(struct platform_device *pdev)
{
	struct tmecom *tdev;
	const char *label;
	char name[32];

	tdev = devm_kzalloc(&pdev->dev, sizeof(*tdev), GFP_KERNEL);
	if (!tdev)
		return -ENOMEM;

	tdev->cl.dev = &pdev->dev;
	tdev->cl.tx_block = true;
	tdev->cl.tx_tout = 500;
	tdev->cl.knows_txdone = false;
	tdev->cl.rx_callback = tmecom_receive_message;

	label = of_get_property(pdev->dev.of_node, "mbox-names", NULL);
	if (!label)
		return -EINVAL;
	snprintf(name, 32, "%s_send_message", label);

	tdev->chan = mbox_request_channel(&tdev->cl, 0);
	if (IS_ERR(tdev->chan)) {
		dev_err(&pdev->dev, "failed to get mbox channel\n");
		return PTR_ERR(tdev->chan);
	}

	mutex_init(&tdev->lock);

	if (tdev->chan) {
		tdev->txbuf =
			devm_kzalloc(&pdev->dev, MBOX_MAX_MSG_LEN, GFP_KERNEL);
		if (!tdev->txbuf) {
			dev_err(&pdev->dev, "message buffer alloc faile\n");
			return -ENOMEM;
		}
	}

	init_waitqueue_head(&tdev->waitq);

#if IS_ENABLED(CONFIG_DEBUG_FS)
	debugfs_file = debugfs_create_file(name, 0220, NULL, tdev,
			&tmecom_debugfs_ops);
	if (!debugfs_file)
		goto err;
#endif /* CONFIG_DEBUG_FS */

	tdev->rx_done = false;
	tdev->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, tdev);

	tmedev = tdev;

	dev_info(&pdev->dev, "tmecom probe success\n");
	return 0;
err:
	mbox_free_channel(tdev->chan);
	return -ENOMEM;
}

static int tmecom_remove(struct platform_device *pdev)
{
	struct tmecom *tdev = platform_get_drvdata(pdev);

#if IS_ENABLED(CONFIG_DEBUG_FS)
	debugfs_remove(debugfs_file);
#endif /* CONFIG_DEBUG_FS */

	if (tdev->chan)
		mbox_free_channel(tdev->chan);

	dev_info(&pdev->dev, "tmecom remove success\n");
	return 0;
}

static const struct of_device_id tmecom_match_tbl[] = {
	{.compatible = "qcom,tmecom-qmp-client"},
	{},
};

static struct platform_driver tmecom_driver = {
	.probe = tmecom_probe,
	.remove = tmecom_remove,
	.driver = {
		.name = "tmecom-qmp-client",
		.suppress_bind_attrs = true,
		.of_match_table = tmecom_match_tbl,
	},
};
module_platform_driver(tmecom_driver);

MODULE_DESCRIPTION("MSM TMECom QTI mailbox protocol client");
MODULE_LICENSE("GPL");

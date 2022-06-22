// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Corporation. All rights reserved.
 * Author: Allen-KH Cheng <allen-kh.cheng@mediatek.com>
 */

#include <linux/firmware/mediatek/mtk-adsp-ipc.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

static const char * const adsp_mbox_ch_names[MTK_ADSP_MBOX_NUM] = { "rx", "tx" };

/*
 * mtk_adsp_ipc_send - send ipc cmd to MTK ADSP
 *
 * @ipc: ADSP IPC handle
 * @idx: index of the mailbox channel
 * @msg: IPC cmd (reply or request)
 *
 * Returns zero for success from mbox_send_message
 * negative value for error
 */
int mtk_adsp_ipc_send(struct mtk_adsp_ipc *ipc, unsigned int idx, uint32_t msg)
{
	struct mtk_adsp_chan *adsp_chan;
	int ret;

	if (idx >= MTK_ADSP_MBOX_NUM)
		return -EINVAL;

	adsp_chan = &ipc->chans[idx];
	ret = mbox_send_message(adsp_chan->ch, &msg);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_adsp_ipc_send);

/*
 * mtk_adsp_ipc_recv - recv callback used by MTK ADSP mailbox
 *
 * @c: mbox client
 * @msg: message received
 *
 * Users of ADSP IPC will need to privde handle_reply and handle_request
 * callbacks.
 */
static void mtk_adsp_ipc_recv(struct mbox_client *c, void *msg)
{
	struct mtk_adsp_chan *chan = container_of(c, struct mtk_adsp_chan, cl);
	struct device *dev = c->dev;

	switch (chan->idx) {
	case MTK_ADSP_MBOX_REPLY:
		chan->ipc->ops->handle_reply(chan->ipc);
		break;
	case MTK_ADSP_MBOX_REQUEST:
		chan->ipc->ops->handle_request(chan->ipc);
		break;
	default:
		dev_err(dev, "wrong mbox chan %d\n", chan->idx);
		break;
	}
}

static int mtk_adsp_ipc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_adsp_ipc *adsp_ipc;
	struct mtk_adsp_chan *adsp_chan;
	struct mbox_client *cl;
	int ret;
	int i, j;

	device_set_of_node_from_dev(&pdev->dev, pdev->dev.parent);

	adsp_ipc = devm_kzalloc(dev, sizeof(*adsp_ipc), GFP_KERNEL);
	if (!adsp_ipc)
		return -ENOMEM;

	for (i = 0; i < MTK_ADSP_MBOX_NUM; i++) {
		adsp_chan = &adsp_ipc->chans[i];
		cl = &adsp_chan->cl;
		cl->dev = dev->parent;
		cl->tx_block = false;
		cl->knows_txdone = false;
		cl->tx_prepare = NULL;
		cl->rx_callback = mtk_adsp_ipc_recv;

		adsp_chan->ipc = adsp_ipc;
		adsp_chan->idx = i;
		adsp_chan->ch = mbox_request_channel_byname(cl, adsp_mbox_ch_names[i]);
		if (IS_ERR(adsp_chan->ch)) {
			ret = PTR_ERR(adsp_chan->ch);
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "Failed to request mbox chan %s ret %d\n",
					adsp_mbox_ch_names[i], ret);

			for (j = 0; j < i; j++) {
				adsp_chan = &adsp_ipc->chans[j];
				mbox_free_channel(adsp_chan->ch);
			}

			return ret;
		}
	}

	adsp_ipc->dev = dev;
	dev_set_drvdata(dev, adsp_ipc);
	dev_dbg(dev, "MTK ADSP IPC initialized\n");

	return 0;
}

static int mtk_adsp_ipc_remove(struct platform_device *pdev)
{
	struct mtk_adsp_ipc *adsp_ipc = dev_get_drvdata(&pdev->dev);
	struct mtk_adsp_chan *adsp_chan;
	int i;

	for (i = 0; i < MTK_ADSP_MBOX_NUM; i++) {
		adsp_chan = &adsp_ipc->chans[i];
		mbox_free_channel(adsp_chan->ch);
	}

	return 0;
}

static struct platform_driver mtk_adsp_ipc_driver = {
	.driver = {
		.name = "mtk-adsp-ipc",
	},
	.probe = mtk_adsp_ipc_probe,
	.remove = mtk_adsp_ipc_remove,
};
builtin_platform_driver(mtk_adsp_ipc_driver);

MODULE_AUTHOR("Allen-KH Cheng <allen-kh.cheng@mediatek.com>");
MODULE_DESCRIPTION("MTK ADSP IPC Driver");
MODULE_LICENSE("GPL");

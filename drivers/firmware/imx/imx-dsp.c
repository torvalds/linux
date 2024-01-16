// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2019 NXP
 *  Author: Daniel Baluta <daniel.baluta@nxp.com>
 *
 * Implementation of the DSP IPC interface (host side)
 */

#include <linux/firmware/imx/dsp.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/*
 * imx_dsp_ring_doorbell - triggers an interrupt on the other side (DSP)
 *
 * @dsp: DSP IPC handle
 * @chan_idx: index of the channel where to trigger the interrupt
 *
 * Returns non-negative value for success, negative value for error
 */
int imx_dsp_ring_doorbell(struct imx_dsp_ipc *ipc, unsigned int idx)
{
	int ret;
	struct imx_dsp_chan *dsp_chan;

	if (idx >= DSP_MU_CHAN_NUM)
		return -EINVAL;

	dsp_chan = &ipc->chans[idx];
	ret = mbox_send_message(dsp_chan->ch, NULL);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(imx_dsp_ring_doorbell);

/*
 * imx_dsp_handle_rx - rx callback used by imx mailbox
 *
 * @c: mbox client
 * @msg: message received
 *
 * Users of DSP IPC will need to privde handle_reply and handle_request
 * callbacks.
 */
static void imx_dsp_handle_rx(struct mbox_client *c, void *msg)
{
	struct imx_dsp_chan *chan = container_of(c, struct imx_dsp_chan, cl);

	if (chan->idx == 0) {
		chan->ipc->ops->handle_reply(chan->ipc);
	} else {
		chan->ipc->ops->handle_request(chan->ipc);
		imx_dsp_ring_doorbell(chan->ipc, 1);
	}
}

struct mbox_chan *imx_dsp_request_channel(struct imx_dsp_ipc *dsp_ipc, int idx)
{
	struct imx_dsp_chan *dsp_chan;

	if (idx >= DSP_MU_CHAN_NUM)
		return ERR_PTR(-EINVAL);

	dsp_chan = &dsp_ipc->chans[idx];
	dsp_chan->ch = mbox_request_channel_byname(&dsp_chan->cl, dsp_chan->name);
	return dsp_chan->ch;
}
EXPORT_SYMBOL(imx_dsp_request_channel);

void imx_dsp_free_channel(struct imx_dsp_ipc *dsp_ipc, int idx)
{
	struct imx_dsp_chan *dsp_chan;

	if (idx >= DSP_MU_CHAN_NUM)
		return;

	dsp_chan = &dsp_ipc->chans[idx];
	mbox_free_channel(dsp_chan->ch);
}
EXPORT_SYMBOL(imx_dsp_free_channel);

static int imx_dsp_setup_channels(struct imx_dsp_ipc *dsp_ipc)
{
	struct device *dev = dsp_ipc->dev;
	struct imx_dsp_chan *dsp_chan;
	struct mbox_client *cl;
	char *chan_name;
	int ret;
	int i, j;

	for (i = 0; i < DSP_MU_CHAN_NUM; i++) {
		if (i < 2)
			chan_name = kasprintf(GFP_KERNEL, "txdb%d", i);
		else
			chan_name = kasprintf(GFP_KERNEL, "rxdb%d", i - 2);

		if (!chan_name)
			return -ENOMEM;

		dsp_chan = &dsp_ipc->chans[i];
		dsp_chan->name = chan_name;
		cl = &dsp_chan->cl;
		cl->dev = dev;
		cl->tx_block = false;
		cl->knows_txdone = true;
		cl->rx_callback = imx_dsp_handle_rx;

		dsp_chan->ipc = dsp_ipc;
		dsp_chan->idx = i % 2;
		dsp_chan->ch = mbox_request_channel_byname(cl, chan_name);
		if (IS_ERR(dsp_chan->ch)) {
			kfree(dsp_chan->name);
			ret = PTR_ERR(dsp_chan->ch);
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "Failed to request mbox chan %s ret %d\n",
					chan_name, ret);
			goto out;
		}

		dev_dbg(dev, "request mbox chan %s\n", chan_name);
	}

	return 0;
out:
	for (j = 0; j < i; j++) {
		dsp_chan = &dsp_ipc->chans[j];
		mbox_free_channel(dsp_chan->ch);
		kfree(dsp_chan->name);
	}

	return ret;
}

static int imx_dsp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct imx_dsp_ipc *dsp_ipc;
	int ret;

	device_set_of_node_from_dev(&pdev->dev, pdev->dev.parent);

	dsp_ipc = devm_kzalloc(dev, sizeof(*dsp_ipc), GFP_KERNEL);
	if (!dsp_ipc)
		return -ENOMEM;

	dsp_ipc->dev = dev;
	dev_set_drvdata(dev, dsp_ipc);

	ret = imx_dsp_setup_channels(dsp_ipc);
	if (ret < 0)
		return ret;

	dev_info(dev, "NXP i.MX DSP IPC initialized\n");

	return 0;
}

static int imx_dsp_remove(struct platform_device *pdev)
{
	struct imx_dsp_chan *dsp_chan;
	struct imx_dsp_ipc *dsp_ipc;
	int i;

	dsp_ipc = dev_get_drvdata(&pdev->dev);

	for (i = 0; i < DSP_MU_CHAN_NUM; i++) {
		dsp_chan = &dsp_ipc->chans[i];
		mbox_free_channel(dsp_chan->ch);
		kfree(dsp_chan->name);
	}

	return 0;
}

static struct platform_driver imx_dsp_driver = {
	.driver = {
		.name = "imx-dsp",
	},
	.probe = imx_dsp_probe,
	.remove = imx_dsp_remove,
};
builtin_platform_driver(imx_dsp_driver);

MODULE_AUTHOR("Daniel Baluta <daniel.baluta@nxp.com>");
MODULE_DESCRIPTION("IMX DSP IPC protocol driver");
MODULE_LICENSE("GPL v2");

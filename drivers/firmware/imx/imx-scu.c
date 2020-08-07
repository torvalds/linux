// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018 NXP
 *  Author: Dong Aisheng <aisheng.dong@nxp.com>
 *
 * Implementation of the SCU IPC functions using MUs (client side).
 *
 */

#include <linux/err.h>
#include <linux/firmware/imx/ipc.h>
#include <linux/firmware/imx/sci.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#define SCU_MU_CHAN_NUM		8
#define MAX_RX_TIMEOUT		(msecs_to_jiffies(30))

struct imx_sc_chan {
	struct imx_sc_ipc *sc_ipc;

	struct mbox_client cl;
	struct mbox_chan *ch;
	int idx;
	struct completion tx_done;
};

struct imx_sc_ipc {
	/* SCU uses 4 Tx and 4 Rx channels */
	struct imx_sc_chan chans[SCU_MU_CHAN_NUM];
	struct device *dev;
	struct mutex lock;
	struct completion done;
	bool fast_ipc;

	/* temporarily store the SCU msg */
	u32 *msg;
	u8 rx_size;
	u8 count;
};

/*
 * This type is used to indicate error response for most functions.
 */
enum imx_sc_error_codes {
	IMX_SC_ERR_NONE = 0,	/* Success */
	IMX_SC_ERR_VERSION = 1,	/* Incompatible API version */
	IMX_SC_ERR_CONFIG = 2,	/* Configuration error */
	IMX_SC_ERR_PARM = 3,	/* Bad parameter */
	IMX_SC_ERR_NOACCESS = 4,	/* Permission error (no access) */
	IMX_SC_ERR_LOCKED = 5,	/* Permission error (locked) */
	IMX_SC_ERR_UNAVAILABLE = 6,	/* Unavailable (out of resources) */
	IMX_SC_ERR_NOTFOUND = 7,	/* Not found */
	IMX_SC_ERR_NOPOWER = 8,	/* No power */
	IMX_SC_ERR_IPC = 9,		/* Generic IPC error */
	IMX_SC_ERR_BUSY = 10,	/* Resource is currently busy/active */
	IMX_SC_ERR_FAIL = 11,	/* General I/O failure */
	IMX_SC_ERR_LAST
};

static int imx_sc_linux_errmap[IMX_SC_ERR_LAST] = {
	0,	 /* IMX_SC_ERR_NONE */
	-EINVAL, /* IMX_SC_ERR_VERSION */
	-EINVAL, /* IMX_SC_ERR_CONFIG */
	-EINVAL, /* IMX_SC_ERR_PARM */
	-EACCES, /* IMX_SC_ERR_NOACCESS */
	-EACCES, /* IMX_SC_ERR_LOCKED */
	-ERANGE, /* IMX_SC_ERR_UNAVAILABLE */
	-EEXIST, /* IMX_SC_ERR_NOTFOUND */
	-EPERM,	 /* IMX_SC_ERR_NOPOWER */
	-EPIPE,	 /* IMX_SC_ERR_IPC */
	-EBUSY,	 /* IMX_SC_ERR_BUSY */
	-EIO,	 /* IMX_SC_ERR_FAIL */
};

static struct imx_sc_ipc *imx_sc_ipc_handle;

static inline int imx_sc_to_linux_errno(int errno)
{
	if (errno >= IMX_SC_ERR_NONE && errno < IMX_SC_ERR_LAST)
		return imx_sc_linux_errmap[errno];
	return -EIO;
}

/*
 * Get the default handle used by SCU
 */
int imx_scu_get_handle(struct imx_sc_ipc **ipc)
{
	if (!imx_sc_ipc_handle)
		return -EPROBE_DEFER;

	*ipc = imx_sc_ipc_handle;
	return 0;
}
EXPORT_SYMBOL(imx_scu_get_handle);

/* Callback called when the word of a message is ack-ed, eg read by SCU */
static void imx_scu_tx_done(struct mbox_client *cl, void *mssg, int r)
{
	struct imx_sc_chan *sc_chan = container_of(cl, struct imx_sc_chan, cl);

	complete(&sc_chan->tx_done);
}

static void imx_scu_rx_callback(struct mbox_client *c, void *msg)
{
	struct imx_sc_chan *sc_chan = container_of(c, struct imx_sc_chan, cl);
	struct imx_sc_ipc *sc_ipc = sc_chan->sc_ipc;
	struct imx_sc_rpc_msg *hdr;
	u32 *data = msg;
	int i;

	if (!sc_ipc->msg) {
		dev_warn(sc_ipc->dev, "unexpected rx idx %d 0x%08x, ignore!\n",
				sc_chan->idx, *data);
		return;
	}

	if (sc_ipc->fast_ipc) {
		hdr = msg;
		sc_ipc->rx_size = hdr->size;
		sc_ipc->msg[0] = *data++;

		for (i = 1; i < sc_ipc->rx_size; i++)
			sc_ipc->msg[i] = *data++;

		complete(&sc_ipc->done);

		return;
	}

	if (sc_chan->idx == 0) {
		hdr = msg;
		sc_ipc->rx_size = hdr->size;
		dev_dbg(sc_ipc->dev, "msg rx size %u\n", sc_ipc->rx_size);
		if (sc_ipc->rx_size > 4)
			dev_warn(sc_ipc->dev, "RPC does not support receiving over 4 words: %u\n",
				 sc_ipc->rx_size);
	}

	sc_ipc->msg[sc_chan->idx] = *data;
	sc_ipc->count++;

	dev_dbg(sc_ipc->dev, "mu %u msg %u 0x%x\n", sc_chan->idx,
		sc_ipc->count, *data);

	if ((sc_ipc->rx_size != 0) && (sc_ipc->count == sc_ipc->rx_size))
		complete(&sc_ipc->done);
}

static int imx_scu_ipc_write(struct imx_sc_ipc *sc_ipc, void *msg)
{
	struct imx_sc_rpc_msg hdr = *(struct imx_sc_rpc_msg *)msg;
	struct imx_sc_chan *sc_chan;
	u32 *data = msg;
	int ret;
	int size;
	int i;

	/* Check size */
	if (hdr.size > IMX_SC_RPC_MAX_MSG)
		return -EINVAL;

	dev_dbg(sc_ipc->dev, "RPC SVC %u FUNC %u SIZE %u\n", hdr.svc,
		hdr.func, hdr.size);

	size = sc_ipc->fast_ipc ? 1 : hdr.size;
	for (i = 0; i < size; i++) {
		sc_chan = &sc_ipc->chans[i % 4];

		/*
		 * SCU requires that all messages words are written
		 * sequentially but linux MU driver implements multiple
		 * independent channels for each register so ordering between
		 * different channels must be ensured by SCU API interface.
		 *
		 * Wait for tx_done before every send to ensure that no
		 * queueing happens at the mailbox channel level.
		 */
		if (!sc_ipc->fast_ipc) {
			wait_for_completion(&sc_chan->tx_done);
			reinit_completion(&sc_chan->tx_done);
		}

		ret = mbox_send_message(sc_chan->ch, &data[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/*
 * RPC command/response
 */
int imx_scu_call_rpc(struct imx_sc_ipc *sc_ipc, void *msg, bool have_resp)
{
	uint8_t saved_svc, saved_func;
	struct imx_sc_rpc_msg *hdr;
	int ret;

	if (WARN_ON(!sc_ipc || !msg))
		return -EINVAL;

	mutex_lock(&sc_ipc->lock);
	reinit_completion(&sc_ipc->done);

	if (have_resp) {
		sc_ipc->msg = msg;
		saved_svc = ((struct imx_sc_rpc_msg *)msg)->svc;
		saved_func = ((struct imx_sc_rpc_msg *)msg)->func;
	}
	sc_ipc->count = 0;
	ret = imx_scu_ipc_write(sc_ipc, msg);
	if (ret < 0) {
		dev_err(sc_ipc->dev, "RPC send msg failed: %d\n", ret);
		goto out;
	}

	if (have_resp) {
		if (!wait_for_completion_timeout(&sc_ipc->done,
						 MAX_RX_TIMEOUT)) {
			dev_err(sc_ipc->dev, "RPC send msg timeout\n");
			mutex_unlock(&sc_ipc->lock);
			return -ETIMEDOUT;
		}

		/* response status is stored in hdr->func field */
		hdr = msg;
		ret = hdr->func;
		/*
		 * Some special SCU firmware APIs do NOT have return value
		 * in hdr->func, but they do have response data, those special
		 * APIs are defined as void function in SCU firmware, so they
		 * should be treated as return success always.
		 */
		if ((saved_svc == IMX_SC_RPC_SVC_MISC) &&
			(saved_func == IMX_SC_MISC_FUNC_UNIQUE_ID ||
			 saved_func == IMX_SC_MISC_FUNC_GET_BUTTON_STATUS))
			ret = 0;
	}

out:
	sc_ipc->msg = NULL;
	mutex_unlock(&sc_ipc->lock);

	dev_dbg(sc_ipc->dev, "RPC SVC done\n");

	return imx_sc_to_linux_errno(ret);
}
EXPORT_SYMBOL(imx_scu_call_rpc);

static int imx_scu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct imx_sc_ipc *sc_ipc;
	struct imx_sc_chan *sc_chan;
	struct mbox_client *cl;
	char *chan_name;
	struct of_phandle_args args;
	int num_channel;
	int ret;
	int i;

	sc_ipc = devm_kzalloc(dev, sizeof(*sc_ipc), GFP_KERNEL);
	if (!sc_ipc)
		return -ENOMEM;

	ret = of_parse_phandle_with_args(pdev->dev.of_node, "mboxes",
					 "#mbox-cells", 0, &args);
	if (ret)
		return ret;

	sc_ipc->fast_ipc = of_device_is_compatible(args.np, "fsl,imx8-mu-scu");

	num_channel = sc_ipc->fast_ipc ? 2 : SCU_MU_CHAN_NUM;
	for (i = 0; i < num_channel; i++) {
		if (i < num_channel / 2)
			chan_name = kasprintf(GFP_KERNEL, "tx%d", i);
		else
			chan_name = kasprintf(GFP_KERNEL, "rx%d",
					      i - num_channel / 2);

		if (!chan_name)
			return -ENOMEM;

		sc_chan = &sc_ipc->chans[i];
		cl = &sc_chan->cl;
		cl->dev = dev;
		cl->tx_block = false;
		cl->knows_txdone = true;
		cl->rx_callback = imx_scu_rx_callback;

		if (!sc_ipc->fast_ipc) {
			/* Initial tx_done completion as "done" */
			cl->tx_done = imx_scu_tx_done;
			init_completion(&sc_chan->tx_done);
			complete(&sc_chan->tx_done);
		}

		sc_chan->sc_ipc = sc_ipc;
		sc_chan->idx = i % (num_channel / 2);
		sc_chan->ch = mbox_request_channel_byname(cl, chan_name);
		if (IS_ERR(sc_chan->ch)) {
			ret = PTR_ERR(sc_chan->ch);
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "Failed to request mbox chan %s ret %d\n",
					chan_name, ret);
			kfree(chan_name);
			return ret;
		}

		dev_dbg(dev, "request mbox chan %s\n", chan_name);
		/* chan_name is not used anymore by framework */
		kfree(chan_name);
	}

	sc_ipc->dev = dev;
	mutex_init(&sc_ipc->lock);
	init_completion(&sc_ipc->done);

	imx_sc_ipc_handle = sc_ipc;

	ret = imx_scu_soc_init(dev);
	if (ret)
		dev_warn(dev, "failed to initialize SoC info: %d\n", ret);

	ret = imx_scu_enable_general_irq_channel(dev);
	if (ret)
		dev_warn(dev,
			"failed to enable general irq channel: %d\n", ret);

	dev_info(dev, "NXP i.MX SCU Initialized\n");

	return devm_of_platform_populate(dev);
}

static const struct of_device_id imx_scu_match[] = {
	{ .compatible = "fsl,imx-scu", },
	{ /* Sentinel */ }
};

static struct platform_driver imx_scu_driver = {
	.driver = {
		.name = "imx-scu",
		.of_match_table = imx_scu_match,
	},
	.probe = imx_scu_probe,
};
builtin_platform_driver(imx_scu_driver);

MODULE_AUTHOR("Dong Aisheng <aisheng.dong@nxp.com>");
MODULE_DESCRIPTION("IMX SCU firmware protocol driver");
MODULE_LICENSE("GPL v2");

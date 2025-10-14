// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Message Mailbox Transport
 * driver.
 *
 * Copyright (C) 2019-2024 ARM Ltd.
 */

#include <linux/err.h>
#include <linux/device.h>
#include <linux/mailbox_client.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "../common.h"

/**
 * struct scmi_mailbox - Structure representing a SCMI mailbox transport
 *
 * @cl: Mailbox Client
 * @chan: Transmit/Receive mailbox uni/bi-directional channel
 * @chan_receiver: Optional Receiver mailbox unidirectional channel
 * @chan_platform_receiver: Optional Platform Receiver mailbox unidirectional channel
 * @cinfo: SCMI channel info
 * @shmem: Transmit/Receive shared memory area
 * @chan_lock: Lock that prevents multiple xfers from being queued
 * @io_ops: Transport specific I/O operations
 */
struct scmi_mailbox {
	struct mbox_client cl;
	struct mbox_chan *chan;
	struct mbox_chan *chan_receiver;
	struct mbox_chan *chan_platform_receiver;
	struct scmi_chan_info *cinfo;
	struct scmi_shared_mem __iomem *shmem;
	struct mutex chan_lock;
	struct scmi_shmem_io_ops *io_ops;
};

#define client_to_scmi_mailbox(c) container_of(c, struct scmi_mailbox, cl)

static struct scmi_transport_core_operations *core;

static void tx_prepare(struct mbox_client *cl, void *m)
{
	struct scmi_mailbox *smbox = client_to_scmi_mailbox(cl);

	core->shmem->tx_prepare(smbox->shmem, m, smbox->cinfo,
				smbox->io_ops->toio);
}

static void rx_callback(struct mbox_client *cl, void *m)
{
	struct scmi_mailbox *smbox = client_to_scmi_mailbox(cl);

	/*
	 * An A2P IRQ is NOT valid when received while the platform still has
	 * the ownership of the channel, because the platform at first releases
	 * the SMT channel and then sends the completion interrupt.
	 *
	 * This addresses a possible race condition in which a spurious IRQ from
	 * a previous timed-out reply which arrived late could be wrongly
	 * associated with the next pending transaction.
	 */
	if (cl->knows_txdone &&
	    !core->shmem->channel_free(smbox->shmem)) {
		dev_warn(smbox->cinfo->dev, "Ignoring spurious A2P IRQ !\n");
		core->bad_message_trace(smbox->cinfo,
			     core->shmem->read_header(smbox->shmem),
							     MSG_MBOX_SPURIOUS);
		return;
	}

	core->rx_callback(smbox->cinfo,
		      core->shmem->read_header(smbox->shmem), NULL);
}

static bool mailbox_chan_available(struct device_node *of_node, int idx)
{
	int num_mb;

	/*
	 * Just check if bidirrectional channels are involved, and check the
	 * index accordingly; proper full validation will be made later
	 * in mailbox_chan_setup().
	 */
	num_mb = of_count_phandle_with_args(of_node, "mboxes", "#mbox-cells");
	if (num_mb == 3 && idx == 1)
		idx = 2;

	return !of_parse_phandle_with_args(of_node, "mboxes",
					   "#mbox-cells", idx, NULL);
}

/**
 * mailbox_chan_validate  - Validate transport configuration and map channels
 *
 * @cdev: Reference to the underlying transport device carrying the
 *	  of_node descriptor to analyze.
 * @a2p_rx_chan: A reference to an optional unidirectional channel to use
 *		 for replies on the a2p channel. Set as zero if not present.
 * @p2a_chan: A reference to the optional p2a channel.
 *	      Set as zero if not present.
 * @p2a_rx_chan: A reference to the optional p2a completion channel.
 *	      Set as zero if not present.
 *
 * At first, validate the transport configuration as described in terms of
 * 'mboxes' and 'shmem', then determin which mailbox channel indexes are
 * appropriate to be use in the current configuration.
 *
 * Return: 0 on Success or error
 */
static int mailbox_chan_validate(struct device *cdev, int *a2p_rx_chan,
				 int *p2a_chan, int *p2a_rx_chan)
{
	int num_mb, num_sh, ret = 0;
	struct device_node *np = cdev->of_node;

	num_mb = of_count_phandle_with_args(np, "mboxes", "#mbox-cells");
	num_sh = of_count_phandle_with_args(np, "shmem", NULL);
	dev_dbg(cdev, "Found %d mboxes and %d shmems !\n", num_mb, num_sh);

	/* Bail out if mboxes and shmem descriptors are inconsistent */
	if (num_mb <= 0 || num_sh <= 0 || num_sh > 2 || num_mb > 4 ||
	    (num_mb == 1 && num_sh != 1) || (num_mb == 3 && num_sh != 2) ||
	    (num_mb == 4 && num_sh != 2)) {
		dev_warn(cdev,
			 "Invalid channel descriptor for '%pOF' - mbs:%d  shm:%d\n",
			 np, num_mb, num_sh);
		return -EINVAL;
	}

	/* Bail out if provided shmem descriptors do not refer distinct areas  */
	if (num_sh > 1) {
		struct device_node *np_tx __free(device_node) =
					of_parse_phandle(np, "shmem", 0);
		struct device_node *np_rx __free(device_node) =
					of_parse_phandle(np, "shmem", 1);

		if (!np_tx || !np_rx || np_tx == np_rx) {
			dev_warn(cdev, "Invalid shmem descriptor for '%pOF'\n", np);
			ret = -EINVAL;
		}
	}

	/* Calculate channels IDs to use depending on mboxes/shmem layout */
	if (!ret) {
		switch (num_mb) {
		case 1:
			*a2p_rx_chan = 0;
			*p2a_chan = 0;
			*p2a_rx_chan = 0;
			break;
		case 2:
			if (num_sh == 2) {
				*a2p_rx_chan = 0;
				*p2a_chan = 1;
			} else {
				*a2p_rx_chan = 1;
				*p2a_chan = 0;
			}
			*p2a_rx_chan = 0;
			break;
		case 3:
			*a2p_rx_chan = 1;
			*p2a_chan = 2;
			*p2a_rx_chan = 0;
			break;
		case 4:
			*a2p_rx_chan = 1;
			*p2a_chan = 2;
			*p2a_rx_chan = 3;
			break;
		}
	}

	return ret;
}

static int mailbox_chan_setup(struct scmi_chan_info *cinfo, struct device *dev,
			      bool tx)
{
	const char *desc = tx ? "Tx" : "Rx";
	struct device *cdev = cinfo->dev;
	struct scmi_mailbox *smbox;
	int ret, a2p_rx_chan, p2a_chan, p2a_rx_chan;
	struct mbox_client *cl;

	ret = mailbox_chan_validate(cdev, &a2p_rx_chan, &p2a_chan, &p2a_rx_chan);
	if (ret)
		return ret;

	if (!tx && !p2a_chan)
		return -ENODEV;

	smbox = devm_kzalloc(dev, sizeof(*smbox), GFP_KERNEL);
	if (!smbox)
		return -ENOMEM;

	smbox->shmem = core->shmem->setup_iomap(cinfo, dev, tx, NULL,
						&smbox->io_ops);
	if (IS_ERR(smbox->shmem))
		return PTR_ERR(smbox->shmem);

	cl = &smbox->cl;
	cl->dev = cdev;
	cl->tx_prepare = tx ? tx_prepare : NULL;
	cl->rx_callback = rx_callback;
	cl->tx_block = false;
	cl->knows_txdone = tx;

	smbox->chan = mbox_request_channel(cl, tx ? 0 : p2a_chan);
	if (IS_ERR(smbox->chan)) {
		ret = PTR_ERR(smbox->chan);
		if (ret != -EPROBE_DEFER)
			dev_err(cdev,
				"failed to request SCMI %s mailbox\n", desc);
		return ret;
	}

	/* Additional unidirectional channel for TX if needed */
	if (tx && a2p_rx_chan) {
		smbox->chan_receiver = mbox_request_channel(cl, a2p_rx_chan);
		if (IS_ERR(smbox->chan_receiver)) {
			ret = PTR_ERR(smbox->chan_receiver);
			if (ret != -EPROBE_DEFER)
				dev_err(cdev, "failed to request SCMI Tx Receiver mailbox\n");
			return ret;
		}
	}

	if (!tx && p2a_rx_chan) {
		smbox->chan_platform_receiver = mbox_request_channel(cl, p2a_rx_chan);
		if (IS_ERR(smbox->chan_platform_receiver)) {
			ret = PTR_ERR(smbox->chan_platform_receiver);
			if (ret != -EPROBE_DEFER)
				dev_err(cdev, "failed to request SCMI P2A Receiver mailbox\n");
			return ret;
		}
	}

	cinfo->transport_info = smbox;
	smbox->cinfo = cinfo;
	mutex_init(&smbox->chan_lock);

	return 0;
}

static int mailbox_chan_free(int id, void *p, void *data)
{
	struct scmi_chan_info *cinfo = p;
	struct scmi_mailbox *smbox = cinfo->transport_info;

	if (smbox && !IS_ERR(smbox->chan)) {
		mbox_free_channel(smbox->chan);
		mbox_free_channel(smbox->chan_receiver);
		mbox_free_channel(smbox->chan_platform_receiver);
		cinfo->transport_info = NULL;
		smbox->chan = NULL;
		smbox->chan_receiver = NULL;
		smbox->chan_platform_receiver = NULL;
		smbox->cinfo = NULL;
	}

	return 0;
}

static int mailbox_send_message(struct scmi_chan_info *cinfo,
				struct scmi_xfer *xfer)
{
	struct scmi_mailbox *smbox = cinfo->transport_info;
	int ret;

	/*
	 * The mailbox layer has its own queue. However the mailbox queue
	 * confuses the per message SCMI timeouts since the clock starts when
	 * the message is submitted into the mailbox queue. So when multiple
	 * messages are queued up the clock starts on all messages instead of
	 * only the one inflight.
	 */
	mutex_lock(&smbox->chan_lock);

	ret = mbox_send_message(smbox->chan, xfer);
	/* mbox_send_message returns non-negative value on success */
	if (ret < 0) {
		mutex_unlock(&smbox->chan_lock);
		return ret;
	}

	return 0;
}

static void mailbox_mark_txdone(struct scmi_chan_info *cinfo, int ret,
				struct scmi_xfer *__unused)
{
	struct scmi_mailbox *smbox = cinfo->transport_info;

	mbox_client_txdone(smbox->chan, ret);

	/* Release channel */
	mutex_unlock(&smbox->chan_lock);
}

static void mailbox_fetch_response(struct scmi_chan_info *cinfo,
				   struct scmi_xfer *xfer)
{
	struct scmi_mailbox *smbox = cinfo->transport_info;

	core->shmem->fetch_response(smbox->shmem, xfer, smbox->io_ops->fromio);
}

static void mailbox_fetch_notification(struct scmi_chan_info *cinfo,
				       size_t max_len, struct scmi_xfer *xfer)
{
	struct scmi_mailbox *smbox = cinfo->transport_info;

	core->shmem->fetch_notification(smbox->shmem, max_len, xfer,
					smbox->io_ops->fromio);
}

static void mailbox_clear_channel(struct scmi_chan_info *cinfo)
{
	struct scmi_mailbox *smbox = cinfo->transport_info;
	struct mbox_chan *intr_chan;
	int ret;

	core->shmem->clear_channel(smbox->shmem);

	if (!core->shmem->channel_intr_enabled(smbox->shmem))
		return;

	if (smbox->chan_platform_receiver)
		intr_chan = smbox->chan_platform_receiver;
	else if (smbox->chan)
		intr_chan = smbox->chan;
	else
		return;

	ret = mbox_send_message(intr_chan, NULL);
	/* mbox_send_message returns non-negative value on success, so reset */
	if (ret > 0)
		ret = 0;

	mbox_client_txdone(intr_chan, ret);
}

static bool
mailbox_poll_done(struct scmi_chan_info *cinfo, struct scmi_xfer *xfer)
{
	struct scmi_mailbox *smbox = cinfo->transport_info;

	return core->shmem->poll_done(smbox->shmem, xfer);
}

static const struct scmi_transport_ops scmi_mailbox_ops = {
	.chan_available = mailbox_chan_available,
	.chan_setup = mailbox_chan_setup,
	.chan_free = mailbox_chan_free,
	.send_message = mailbox_send_message,
	.mark_txdone = mailbox_mark_txdone,
	.fetch_response = mailbox_fetch_response,
	.fetch_notification = mailbox_fetch_notification,
	.clear_channel = mailbox_clear_channel,
	.poll_done = mailbox_poll_done,
};

static struct scmi_desc scmi_mailbox_desc = {
	.ops = &scmi_mailbox_ops,
	.max_rx_timeout_ms = 30, /* We may increase this if required */
	.max_msg = 20, /* Limited by MBOX_TX_QUEUE_LEN */
	.max_msg_size = SCMI_SHMEM_MAX_PAYLOAD_SIZE,
};

static const struct of_device_id scmi_of_match[] = {
	{ .compatible = "arm,scmi" },
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, scmi_of_match);

DEFINE_SCMI_TRANSPORT_DRIVER(scmi_mailbox, scmi_mailbox_driver,
			     scmi_mailbox_desc, scmi_of_match, core);
module_platform_driver(scmi_mailbox_driver);

MODULE_AUTHOR("Sudeep Holla <sudeep.holla@arm.com>");
MODULE_DESCRIPTION("SCMI Mailbox Transport driver");
MODULE_LICENSE("GPL");

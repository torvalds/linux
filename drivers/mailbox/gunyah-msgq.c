// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/gunyah.h>
#include <linux/printk.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/wait.h>

#define mbox_chan_to_msgq(chan) (container_of(chan->mbox, struct gh_msgq, mbox))

static irqreturn_t gh_msgq_rx_irq_handler(int irq, void *data)
{
	struct gh_msgq *msgq = data;
	struct gh_msgq_rx_data rx_data;
	enum gh_error gh_error;
	bool ready = true;

	while (ready) {
		gh_error = gh_hypercall_msgq_recv(msgq->rx_ghrsc->capid,
				&rx_data.data, sizeof(rx_data.data),
				&rx_data.length, &ready);
		if (gh_error != GH_ERROR_OK) {
			if (gh_error != GH_ERROR_MSGQUEUE_EMPTY)
				dev_warn(msgq->mbox.dev, "Failed to receive data: %d\n", gh_error);
			break;
		}
		if (likely(gh_msgq_chan(msgq)->cl))
			mbox_chan_received_data(gh_msgq_chan(msgq), &rx_data);
	}

	return IRQ_HANDLED;
}

/* Fired when message queue transitions from "full" to "space available" to send messages */
static irqreturn_t gh_msgq_tx_irq_handler(int irq, void *data)
{
	struct gh_msgq *msgq = data;

	mbox_chan_txdone(gh_msgq_chan(msgq), 0);

	return IRQ_HANDLED;
}

/* Fired after sending message and hypercall told us there was more space available. */
static void gh_msgq_txdone_tasklet(struct tasklet_struct *tasklet)
{
	struct gh_msgq *msgq = container_of(tasklet, struct gh_msgq, txdone_tasklet);

	mbox_chan_txdone(gh_msgq_chan(msgq), msgq->last_ret);
}

static int gh_msgq_send_data(struct mbox_chan *chan, void *data)
{
	struct gh_msgq *msgq = mbox_chan_to_msgq(chan);
	struct gh_msgq_tx_data *msgq_data = data;
	u64 tx_flags = 0;
	enum gh_error gh_error;
	bool ready;

	if (!msgq->tx_ghrsc)
		return -EOPNOTSUPP;

	if (msgq_data->push)
		tx_flags |= GH_HYPERCALL_MSGQ_TX_FLAGS_PUSH;

	gh_error = gh_hypercall_msgq_send(msgq->tx_ghrsc->capid, msgq_data->length, msgq_data->data,
						tx_flags, &ready);

	/**
	 * unlikely because Linux tracks state of msgq and should not try to
	 * send message when msgq is full.
	 */
	if (unlikely(gh_error == GH_ERROR_MSGQUEUE_FULL))
		return -EAGAIN;

	/**
	 * Propagate all other errors to client. If we return error to mailbox
	 * framework, then no other messages can be sent and nobody will know
	 * to retry this message.
	 */
	msgq->last_ret = gh_error_remap(gh_error);

	/**
	 * This message was successfully sent, but message queue isn't ready to
	 * accept more messages because it's now full. Mailbox framework
	 * requires that we only report that message was transmitted when
	 * we're ready to transmit another message. We'll get that in the form
	 * of tx IRQ once the other side starts to drain the msgq.
	 */
	if (gh_error == GH_ERROR_OK) {
		if (!ready)
			return 0;
	} else {
		dev_err(msgq->mbox.dev, "Failed to send data: %d (%d)\n", gh_error, msgq->last_ret);
	}

	/**
	 * We can send more messages. Mailbox framework requires that tx done
	 * happens asynchronously to sending the message. Gunyah message queues
	 * tell us right away on the hypercall return whether we can send more
	 * messages. To work around this, defer the txdone to a tasklet.
	 */
	tasklet_schedule(&msgq->txdone_tasklet);

	return 0;
}

static struct mbox_chan_ops gh_msgq_ops = {
	.send_data = gh_msgq_send_data,
};

/**
 * gh_msgq_init() - Initialize a Gunyah message queue with an mbox_client
 * @parent: device parent used for the mailbox controller
 * @msgq: Pointer to the gh_msgq to initialize
 * @cl: A mailbox client to bind to the mailbox channel that the message queue creates
 * @tx_ghrsc: optional, the transmission side of the message queue
 * @rx_ghrsc: optional, the receiving side of the message queue
 *
 * At least one of tx_ghrsc and rx_ghrsc must be not NULL. Most message queue use cases come with
 * a pair of message queues to facilitate bidirectional communication. When tx_ghrsc is set,
 * the client can send messages with mbox_send_message(gh_msgq_chan(msgq), msg). When rx_ghrsc
 * is set, the mbox_client must register an .rx_callback() and the message queue driver will
 * deliver all available messages upon receiving the RX ready interrupt. The messages should be
 * consumed or copied by the client right away as the gh_msgq_rx_data will be replaced/destroyed
 * after the callback.
 *
 * Returns - 0 on success, negative otherwise
 */
int gh_msgq_init(struct device *parent, struct gh_msgq *msgq, struct mbox_client *cl,
		 struct gh_resource *tx_ghrsc, struct gh_resource *rx_ghrsc)
{
	int ret;

	/* Must have at least a tx_ghrsc or rx_ghrsc and that they are the right device types */
	if ((!tx_ghrsc && !rx_ghrsc) ||
	    (tx_ghrsc && tx_ghrsc->type != GH_RESOURCE_TYPE_MSGQ_TX) ||
	    (rx_ghrsc && rx_ghrsc->type != GH_RESOURCE_TYPE_MSGQ_RX))
		return -EINVAL;

	msgq->mbox.dev = parent;
	msgq->mbox.ops = &gh_msgq_ops;
	msgq->mbox.num_chans = 1;
	msgq->mbox.txdone_irq = true;
	msgq->mbox.chans = &msgq->mbox_chan;

	ret = mbox_controller_register(&msgq->mbox);
	if (ret)
		return ret;

	ret = mbox_bind_client(gh_msgq_chan(msgq), cl);
	if (ret)
		goto err_mbox;

	if (tx_ghrsc) {
		msgq->tx_ghrsc = tx_ghrsc;

		ret = request_irq(msgq->tx_ghrsc->irq, gh_msgq_tx_irq_handler, 0, "gh_msgq_tx",
				msgq);
		if (ret)
			goto err_tx_ghrsc;

		enable_irq_wake(msgq->tx_ghrsc->irq);

		tasklet_setup(&msgq->txdone_tasklet, gh_msgq_txdone_tasklet);
	}

	if (rx_ghrsc) {
		msgq->rx_ghrsc = rx_ghrsc;

		ret = request_threaded_irq(msgq->rx_ghrsc->irq, NULL, gh_msgq_rx_irq_handler,
						IRQF_ONESHOT, "gh_msgq_rx", msgq);
		if (ret)
			goto err_tx_irq;

		enable_irq_wake(msgq->rx_ghrsc->irq);
	}

	return 0;
err_tx_irq:
	if (msgq->tx_ghrsc)
		free_irq(msgq->tx_ghrsc->irq, msgq);

	msgq->rx_ghrsc = NULL;
err_tx_ghrsc:
	msgq->tx_ghrsc = NULL;
err_mbox:
	mbox_controller_unregister(&msgq->mbox);
	return ret;
}
EXPORT_SYMBOL_GPL(gh_msgq_init);

void gh_msgq_remove(struct gh_msgq *msgq)
{
	mbox_free_channel(gh_msgq_chan(msgq));

	if (msgq->rx_ghrsc)
		free_irq(msgq->rx_ghrsc->irq, msgq);

	if (msgq->tx_ghrsc) {
		tasklet_kill(&msgq->txdone_tasklet);
		free_irq(msgq->tx_ghrsc->irq, msgq);
	}

	mbox_controller_unregister(&msgq->mbox);

	msgq->rx_ghrsc = NULL;
	msgq->tx_ghrsc = NULL;
}
EXPORT_SYMBOL_GPL(gh_msgq_remove);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Gunyah Message Queue Driver");

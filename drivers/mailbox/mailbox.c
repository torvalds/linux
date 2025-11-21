// SPDX-License-Identifier: GPL-2.0-only
/*
 * Mailbox: Common code for Mailbox controllers and users
 *
 * Copyright (C) 2013-2014 Linaro Ltd.
 * Author: Jassi Brar <jassisinghbrar@gmail.com>
 */

#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox_controller.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/spinlock.h>

#include "mailbox.h"

static LIST_HEAD(mbox_cons);
static DEFINE_MUTEX(con_mutex);

static int add_to_rbuf(struct mbox_chan *chan, void *mssg)
{
	int idx;

	guard(spinlock_irqsave)(&chan->lock);

	/* See if there is any space left */
	if (chan->msg_count == MBOX_TX_QUEUE_LEN)
		return -ENOBUFS;

	idx = chan->msg_free;
	chan->msg_data[idx] = mssg;
	chan->msg_count++;

	if (idx == MBOX_TX_QUEUE_LEN - 1)
		chan->msg_free = 0;
	else
		chan->msg_free++;

	return idx;
}

static void msg_submit(struct mbox_chan *chan)
{
	unsigned count, idx;
	void *data;
	int err = -EBUSY;

	scoped_guard(spinlock_irqsave, &chan->lock) {
		if (!chan->msg_count || chan->active_req)
			break;

		count = chan->msg_count;
		idx = chan->msg_free;
		if (idx >= count)
			idx -= count;
		else
			idx += MBOX_TX_QUEUE_LEN - count;

		data = chan->msg_data[idx];

		if (chan->cl->tx_prepare)
			chan->cl->tx_prepare(chan->cl, data);
		/* Try to submit a message to the MBOX controller */
		err = chan->mbox->ops->send_data(chan, data);
		if (!err) {
			chan->active_req = data;
			chan->msg_count--;
		}
	}

	if (!err && (chan->txdone_method & TXDONE_BY_POLL)) {
		/* kick start the timer immediately to avoid delays */
		scoped_guard(spinlock_irqsave, &chan->mbox->poll_hrt_lock)
			hrtimer_start(&chan->mbox->poll_hrt, 0, HRTIMER_MODE_REL);
	}
}

static void tx_tick(struct mbox_chan *chan, int r)
{
	void *mssg;

	scoped_guard(spinlock_irqsave, &chan->lock) {
		mssg = chan->active_req;
		chan->active_req = NULL;
	}

	/* Submit next message */
	msg_submit(chan);

	if (!mssg)
		return;

	/* Notify the client */
	if (chan->cl->tx_done)
		chan->cl->tx_done(chan->cl, mssg, r);

	if (r != -ETIME && chan->cl->tx_block)
		complete(&chan->tx_complete);
}

static enum hrtimer_restart txdone_hrtimer(struct hrtimer *hrtimer)
{
	struct mbox_controller *mbox =
		container_of(hrtimer, struct mbox_controller, poll_hrt);
	bool txdone, resched = false;
	int i;

	for (i = 0; i < mbox->num_chans; i++) {
		struct mbox_chan *chan = &mbox->chans[i];

		if (chan->active_req && chan->cl) {
			txdone = chan->mbox->ops->last_tx_done(chan);
			if (txdone)
				tx_tick(chan, 0);
			else
				resched = true;
		}
	}

	if (resched) {
		scoped_guard(spinlock_irqsave, &mbox->poll_hrt_lock) {
			if (!hrtimer_is_queued(hrtimer))
				hrtimer_forward_now(hrtimer, ms_to_ktime(mbox->txpoll_period));
		}

		return HRTIMER_RESTART;
	}
	return HRTIMER_NORESTART;
}

/**
 * mbox_chan_received_data - A way for controller driver to push data
 *				received from remote to the upper layer.
 * @chan: Pointer to the mailbox channel on which RX happened.
 * @mssg: Client specific message typecasted as void *
 *
 * After startup and before shutdown any data received on the chan
 * is passed on to the API via atomic mbox_chan_received_data().
 * The controller should ACK the RX only after this call returns.
 */
void mbox_chan_received_data(struct mbox_chan *chan, void *mssg)
{
	/* No buffering the received data */
	if (chan->cl->rx_callback)
		chan->cl->rx_callback(chan->cl, mssg);
}
EXPORT_SYMBOL_GPL(mbox_chan_received_data);

/**
 * mbox_chan_txdone - A way for controller driver to notify the
 *			framework that the last TX has completed.
 * @chan: Pointer to the mailbox chan on which TX happened.
 * @r: Status of last TX - OK or ERROR
 *
 * The controller that has IRQ for TX ACK calls this atomic API
 * to tick the TX state machine. It works only if txdone_irq
 * is set by the controller.
 */
void mbox_chan_txdone(struct mbox_chan *chan, int r)
{
	if (unlikely(!(chan->txdone_method & TXDONE_BY_IRQ))) {
		dev_err(chan->mbox->dev,
		       "Controller can't run the TX ticker\n");
		return;
	}

	tx_tick(chan, r);
}
EXPORT_SYMBOL_GPL(mbox_chan_txdone);

/**
 * mbox_client_txdone - The way for a client to run the TX state machine.
 * @chan: Mailbox channel assigned to this client.
 * @r: Success status of last transmission.
 *
 * The client/protocol had received some 'ACK' packet and it notifies
 * the API that the last packet was sent successfully. This only works
 * if the controller can't sense TX-Done.
 */
void mbox_client_txdone(struct mbox_chan *chan, int r)
{
	if (unlikely(!(chan->txdone_method & TXDONE_BY_ACK))) {
		dev_err(chan->mbox->dev, "Client can't run the TX ticker\n");
		return;
	}

	tx_tick(chan, r);
}
EXPORT_SYMBOL_GPL(mbox_client_txdone);

/**
 * mbox_client_peek_data - A way for client driver to pull data
 *			received from remote by the controller.
 * @chan: Mailbox channel assigned to this client.
 *
 * A poke to controller driver for any received data.
 * The data is actually passed onto client via the
 * mbox_chan_received_data()
 * The call can be made from atomic context, so the controller's
 * implementation of peek_data() must not sleep.
 *
 * Return: True, if controller has, and is going to push after this,
 *          some data.
 *         False, if controller doesn't have any data to be read.
 */
bool mbox_client_peek_data(struct mbox_chan *chan)
{
	if (chan->mbox->ops->peek_data)
		return chan->mbox->ops->peek_data(chan);

	return false;
}
EXPORT_SYMBOL_GPL(mbox_client_peek_data);

/**
 * mbox_send_message -	For client to submit a message to be
 *				sent to the remote.
 * @chan: Mailbox channel assigned to this client.
 * @mssg: Client specific message typecasted.
 *
 * For client to submit data to the controller destined for a remote
 * processor. If the client had set 'tx_block', the call will return
 * either when the remote receives the data or when 'tx_tout' millisecs
 * run out.
 *  In non-blocking mode, the requests are buffered by the API and a
 * non-negative token is returned for each queued request. If the request
 * is not queued, a negative token is returned. Upon failure or successful
 * TX, the API calls 'tx_done' from atomic context, from which the client
 * could submit yet another request.
 * The pointer to message should be preserved until it is sent
 * over the chan, i.e, tx_done() is made.
 * This function could be called from atomic context as it simply
 * queues the data and returns a token against the request.
 *
 * Return: Non-negative integer for successful submission (non-blocking mode)
 *	or transmission over chan (blocking mode).
 *	Negative value denotes failure.
 */
int mbox_send_message(struct mbox_chan *chan, void *mssg)
{
	int t;

	if (!chan || !chan->cl)
		return -EINVAL;

	t = add_to_rbuf(chan, mssg);
	if (t < 0) {
		dev_err(chan->mbox->dev, "Try increasing MBOX_TX_QUEUE_LEN\n");
		return t;
	}

	msg_submit(chan);

	if (chan->cl->tx_block) {
		unsigned long wait;
		int ret;

		if (!chan->cl->tx_tout) /* wait forever */
			wait = msecs_to_jiffies(3600000);
		else
			wait = msecs_to_jiffies(chan->cl->tx_tout);

		ret = wait_for_completion_timeout(&chan->tx_complete, wait);
		if (ret == 0) {
			t = -ETIME;
			tx_tick(chan, t);
		}
	}

	return t;
}
EXPORT_SYMBOL_GPL(mbox_send_message);

/**
 * mbox_flush - flush a mailbox channel
 * @chan: mailbox channel to flush
 * @timeout: time, in milliseconds, to allow the flush operation to succeed
 *
 * Mailbox controllers that need to work in atomic context can implement the
 * ->flush() callback to busy loop until a transmission has been completed.
 * The implementation must call mbox_chan_txdone() upon success. Clients can
 * call the mbox_flush() function at any time after mbox_send_message() to
 * flush the transmission. After the function returns success, the mailbox
 * transmission is guaranteed to have completed.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int mbox_flush(struct mbox_chan *chan, unsigned long timeout)
{
	int ret;

	if (!chan->mbox->ops->flush)
		return -ENOTSUPP;

	ret = chan->mbox->ops->flush(chan, timeout);
	if (ret < 0)
		tx_tick(chan, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(mbox_flush);

static int __mbox_bind_client(struct mbox_chan *chan, struct mbox_client *cl)
{
	struct device *dev = cl->dev;
	int ret;

	if (chan->cl || !try_module_get(chan->mbox->dev->driver->owner)) {
		dev_err(dev, "%s: mailbox not free\n", __func__);
		return -EBUSY;
	}

	scoped_guard(spinlock_irqsave, &chan->lock) {
		chan->msg_free = 0;
		chan->msg_count = 0;
		chan->active_req = NULL;
		chan->cl = cl;
		init_completion(&chan->tx_complete);

		if (chan->txdone_method	== TXDONE_BY_POLL && cl->knows_txdone)
			chan->txdone_method = TXDONE_BY_ACK;
	}

	if (chan->mbox->ops->startup) {
		ret = chan->mbox->ops->startup(chan);

		if (ret) {
			dev_err(dev, "Unable to startup the chan (%d)\n", ret);
			mbox_free_channel(chan);
			return ret;
		}
	}

	return 0;
}

/**
 * mbox_bind_client - Request a mailbox channel.
 * @chan: The mailbox channel to bind the client to.
 * @cl: Identity of the client requesting the channel.
 *
 * The Client specifies its requirements and capabilities while asking for
 * a mailbox channel. It can't be called from atomic context.
 * The channel is exclusively allocated and can't be used by another
 * client before the owner calls mbox_free_channel.
 * After assignment, any packet received on this channel will be
 * handed over to the client via the 'rx_callback'.
 * The framework holds reference to the client, so the mbox_client
 * structure shouldn't be modified until the mbox_free_channel returns.
 *
 * Return: 0 if the channel was assigned to the client successfully.
 *         <0 for request failure.
 */
int mbox_bind_client(struct mbox_chan *chan, struct mbox_client *cl)
{
	guard(mutex)(&con_mutex);

	return __mbox_bind_client(chan, cl);
}
EXPORT_SYMBOL_GPL(mbox_bind_client);

/**
 * mbox_request_channel - Request a mailbox channel.
 * @cl: Identity of the client requesting the channel.
 * @index: Index of mailbox specifier in 'mboxes' property.
 *
 * The Client specifies its requirements and capabilities while asking for
 * a mailbox channel. It can't be called from atomic context.
 * The channel is exclusively allocated and can't be used by another
 * client before the owner calls mbox_free_channel.
 * After assignment, any packet received on this channel will be
 * handed over to the client via the 'rx_callback'.
 * The framework holds reference to the client, so the mbox_client
 * structure shouldn't be modified until the mbox_free_channel returns.
 *
 * Return: Pointer to the channel assigned to the client if successful.
 *		ERR_PTR for request failure.
 */
struct mbox_chan *mbox_request_channel(struct mbox_client *cl, int index)
{
	struct fwnode_reference_args fwspec;
	struct fwnode_handle *fwnode;
	struct mbox_controller *mbox;
	struct of_phandle_args spec;
	struct mbox_chan *chan;
	struct device *dev;
	unsigned int i;
	int ret;

	dev = cl->dev;
	if (!dev) {
		pr_debug("No owner device\n");
		return ERR_PTR(-ENODEV);
	}

	fwnode = dev_fwnode(dev);
	if (!fwnode) {
		dev_dbg(dev, "No owner fwnode\n");
		return ERR_PTR(-ENODEV);
	}

	ret = fwnode_property_get_reference_args(fwnode, "mboxes", "#mbox-cells",
						 0, index, &fwspec);
	if (ret) {
		dev_err(dev, "%s: can't parse \"%s\" property\n", __func__, "mboxes");
		return ERR_PTR(ret);
	}

	spec.np = to_of_node(fwspec.fwnode);
	spec.args_count = fwspec.nargs;
	for (i = 0; i < spec.args_count; i++)
		spec.args[i] = fwspec.args[i];

	scoped_guard(mutex, &con_mutex) {
		chan = ERR_PTR(-EPROBE_DEFER);
		list_for_each_entry(mbox, &mbox_cons, node) {
			if (device_match_fwnode(mbox->dev, fwspec.fwnode)) {
				if (mbox->fw_xlate) {
					chan = mbox->fw_xlate(mbox, &fwspec);
					if (!IS_ERR(chan))
						break;
				} else if (mbox->of_xlate) {
					chan = mbox->of_xlate(mbox, &spec);
					if (!IS_ERR(chan))
						break;
				}
			}
		}

		fwnode_handle_put(fwspec.fwnode);

		if (IS_ERR(chan))
			return chan;

		ret = __mbox_bind_client(chan, cl);
		if (ret)
			chan = ERR_PTR(ret);
	}

	return chan;
}
EXPORT_SYMBOL_GPL(mbox_request_channel);

struct mbox_chan *mbox_request_channel_byname(struct mbox_client *cl,
					      const char *name)
{
	int index = device_property_match_string(cl->dev, "mbox-names", name);

	if (index < 0) {
		dev_err(cl->dev, "%s() could not locate channel named \"%s\"\n",
			__func__, name);
		return ERR_PTR(index);
	}
	return mbox_request_channel(cl, index);
}
EXPORT_SYMBOL_GPL(mbox_request_channel_byname);

/**
 * mbox_free_channel - The client relinquishes control of a mailbox
 *			channel by this call.
 * @chan: The mailbox channel to be freed.
 */
void mbox_free_channel(struct mbox_chan *chan)
{
	if (!chan || !chan->cl)
		return;

	if (chan->mbox->ops->shutdown)
		chan->mbox->ops->shutdown(chan);

	/* The queued TX requests are simply aborted, no callbacks are made */
	scoped_guard(spinlock_irqsave, &chan->lock) {
		chan->cl = NULL;
		chan->active_req = NULL;
		if (chan->txdone_method == TXDONE_BY_ACK)
			chan->txdone_method = TXDONE_BY_POLL;
	}

	module_put(chan->mbox->dev->driver->owner);
}
EXPORT_SYMBOL_GPL(mbox_free_channel);

static struct mbox_chan *fw_mbox_index_xlate(struct mbox_controller *mbox,
					     const struct fwnode_reference_args *sp)
{
	int ind = sp->args[0];

	if (ind >= mbox->num_chans)
		return ERR_PTR(-EINVAL);

	return &mbox->chans[ind];
}

/**
 * mbox_controller_register - Register the mailbox controller
 * @mbox:	Pointer to the mailbox controller.
 *
 * The controller driver registers its communication channels
 */
int mbox_controller_register(struct mbox_controller *mbox)
{
	int i, txdone;

	/* Sanity check */
	if (!mbox || !mbox->dev || !mbox->ops || !mbox->num_chans)
		return -EINVAL;

	if (mbox->txdone_irq)
		txdone = TXDONE_BY_IRQ;
	else if (mbox->txdone_poll)
		txdone = TXDONE_BY_POLL;
	else /* It has to be ACK then */
		txdone = TXDONE_BY_ACK;

	if (txdone == TXDONE_BY_POLL) {

		if (!mbox->ops->last_tx_done) {
			dev_err(mbox->dev, "last_tx_done method is absent\n");
			return -EINVAL;
		}

		hrtimer_setup(&mbox->poll_hrt, txdone_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		spin_lock_init(&mbox->poll_hrt_lock);
	}

	for (i = 0; i < mbox->num_chans; i++) {
		struct mbox_chan *chan = &mbox->chans[i];

		chan->cl = NULL;
		chan->mbox = mbox;
		chan->txdone_method = txdone;
		spin_lock_init(&chan->lock);
	}

	if (!mbox->fw_xlate && !mbox->of_xlate)
		mbox->fw_xlate = fw_mbox_index_xlate;

	scoped_guard(mutex, &con_mutex)
		list_add_tail(&mbox->node, &mbox_cons);

	return 0;
}
EXPORT_SYMBOL_GPL(mbox_controller_register);

/**
 * mbox_controller_unregister - Unregister the mailbox controller
 * @mbox:	Pointer to the mailbox controller.
 */
void mbox_controller_unregister(struct mbox_controller *mbox)
{
	int i;

	if (!mbox)
		return;

	scoped_guard(mutex, &con_mutex) {
		list_del(&mbox->node);

		for (i = 0; i < mbox->num_chans; i++)
			mbox_free_channel(&mbox->chans[i]);

		if (mbox->txdone_poll)
			hrtimer_cancel(&mbox->poll_hrt);
	}
}
EXPORT_SYMBOL_GPL(mbox_controller_unregister);

static void __devm_mbox_controller_unregister(struct device *dev, void *res)
{
	struct mbox_controller **mbox = res;

	mbox_controller_unregister(*mbox);
}

/**
 * devm_mbox_controller_register() - managed mbox_controller_register()
 * @dev: device owning the mailbox controller being registered
 * @mbox: mailbox controller being registered
 *
 * This function adds a device-managed resource that will make sure that the
 * mailbox controller, which is registered using mbox_controller_register()
 * as part of this function, will be unregistered along with the rest of
 * device-managed resources upon driver probe failure or driver removal.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int devm_mbox_controller_register(struct device *dev,
				  struct mbox_controller *mbox)
{
	struct mbox_controller **ptr;
	int err;

	ptr = devres_alloc(__devm_mbox_controller_unregister, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	err = mbox_controller_register(mbox);
	if (err < 0) {
		devres_free(ptr);
		return err;
	}

	devres_add(dev, ptr);
	*ptr = mbox;

	return 0;
}
EXPORT_SYMBOL_GPL(devm_mbox_controller_register);

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox_controller.h>

#include "mailbox_internal.h"

static LIST_HEAD(ipc_cons);
static DEFINE_MUTEX(con_mutex);

static request_token_t _add_to_rbuf(struct ipc_chan *chan, void *mssg)
{
	request_token_t idx;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	/* See if there is any space left */
	if (chan->msg_count == MBOX_TX_QUEUE_LEN) {
		spin_unlock_irqrestore(&chan->lock, flags);
		return 0;
	}

	idx = chan->msg_free;
	chan->msg_data[idx] = mssg;
	chan->msg_count++;

	if (idx == MBOX_TX_QUEUE_LEN - 1)
		chan->msg_free = 0;
	else
		chan->msg_free++;

	spin_unlock_irqrestore(&chan->lock, flags);

	/* To aid debugging, we return 'idx+1' instead of 1 */
	return idx + 1;
}

static void _msg_submit(struct ipc_chan *chan)
{
	struct ipc_link *link = chan->link;
	unsigned count, idx;
	unsigned long flags;
	void *data;
	int err;

	spin_lock_irqsave(&chan->lock, flags);

	if (!chan->msg_count || chan->active_req) {
		spin_unlock_irqrestore(&chan->lock, flags);
		return;
	}

	count = chan->msg_count;
	idx = chan->msg_free;
	if (idx >= count)
		idx -= count;
	else
		idx += MBOX_TX_QUEUE_LEN - count;

	data = chan->msg_data[idx];

	/* Try to submit a message to the IPC controller */
	err = chan->link_ops->send_data(link, data);
	if (!err) {
		chan->active_req = data;
		chan->msg_count--;
	}

	spin_unlock_irqrestore(&chan->lock, flags);
}

static void tx_tick(struct ipc_chan *chan, enum xfer_result r)
{
	unsigned long flags;
	void *mssg;

	spin_lock_irqsave(&chan->lock, flags);
	mssg = chan->active_req;
	chan->active_req = NULL;
	spin_unlock_irqrestore(&chan->lock, flags);

	/* Submit next message */
	_msg_submit(chan);

	/* Notify the client */
	if (chan->tx_block)
		complete(&chan->tx_complete);
	else if (mssg && chan->txcb)
		chan->txcb(chan->cl_id, mssg, r);
}

static void poll_txdone(unsigned long data)
{
	struct ipc_con *con = (struct ipc_con *)data;
	bool txdone, resched = false;
	struct ipc_chan *chan;

	list_for_each_entry(chan, &con->channels, node) {
		if (chan->active_req && chan->assigned) {
			resched = true;
			txdone = chan->link_ops->is_ready(chan->link);
			if (txdone)
				tx_tick(chan, XFER_OK);
		}
	}

	if (resched)
		mod_timer(&con->poll,
			jiffies + msecs_to_jiffies(con->period));
}

/*
 * After 'startup' and before 'shutdown', the IPC controller driver
 * notifies the API of data received over the link.
 * The controller driver should make sure the 'RTR' is de-asserted since
 * reception of the packet and until after this call returns.
 * This call could be made from atomic context.
 */
void ipc_link_received_data(struct ipc_link *link, void *mssg)
{
	struct ipc_chan *chan = (struct ipc_chan *)link->api_priv;

	/* No buffering the received data */
	if (chan->rxcb)
		chan->rxcb(chan->cl_id, mssg);
}
EXPORT_SYMBOL(ipc_link_received_data);

/*
 * The IPC controller driver notifies the API that the remote has
 * asserted RTR and it could now send another message on the link.
 */
void ipc_link_txdone(struct ipc_link *link, enum xfer_result r)
{
	struct ipc_chan *chan = (struct ipc_chan *)link->api_priv;

	if (unlikely(!(chan->txdone_method & TXDONE_BY_IRQ))) {
		pr_err("Controller can't run the TX ticker\n");
		return;
	}

	tx_tick(chan, r);
}
EXPORT_SYMBOL(ipc_link_txdone);

/*
 * The client/protocol had received some 'ACK' packet and it notifies
 * the API that the last packet was sent successfully. This only works
 * if the controller doesn't get IRQ for TX done.
 */
void ipc_client_txdone(void *channel, enum xfer_result r)
{
	struct ipc_chan *chan = (struct ipc_chan *)channel;
	bool txdone = true;

	if (unlikely(!(chan->txdone_method & TXDONE_BY_ACK))) {
		pr_err("Client can't run the TX ticker\n");
		return;
	}

	if (chan->txdone_method & TXDONE_BY_POLL)
		txdone = chan->link_ops->is_ready(chan->link);

	if (txdone)
		tx_tick(chan, r);
}
EXPORT_SYMBOL(ipc_client_txdone);

/*
 * Called by a client to "put data on the h/w channel" so that if
 * everything else is fine we don't need to do anything more locally
 * for the remote to receive the data intact.
 * In reality, the remote may receive it intact, corrupted or not at all.
 * This could be called from atomic context as it simply
 * queues the data and returns a token (request_token_t)
 * against the request.
 * The client is later notified of successful transmission of
 * data over the channel via the 'txcb'. The client could in
 * turn queue more messages from txcb.
 */
request_token_t ipc_send_message(void *channel, void *mssg)
{
	struct ipc_chan *chan = (struct ipc_chan *)channel;
	request_token_t t;

	if (!chan || !chan->assigned)
		return 0;

	t = _add_to_rbuf(chan, mssg);
	if (!t)
		pr_err("Try increasing MBOX_TX_QUEUE_LEN\n");

	_msg_submit(chan);

	if (chan->txdone_method	== TXDONE_BY_POLL)
		poll_txdone((unsigned long)chan->con);

	if (chan->tx_block && chan->active_req) {
		int ret;
		init_completion(&chan->tx_complete);
		ret = wait_for_completion_timeout(&chan->tx_complete,
			chan->tx_tout);
		if (ret == 0) {
			t = 0;
			tx_tick(chan, XFER_ERR);
		}
	}

	return t;
}
EXPORT_SYMBOL(ipc_send_message);

/*
 * A client driver asks for exclusive use of a channel/mailbox.
 * If assigned, the channel has to be 'freed' before it could
 * be assigned to some other client.
 * After assignment, any packet received on this channel will be
 * handed over to the client via the 'rxcb' callback.
 * The 'txcb' callback is used to notify client upon sending the
 * packet over the channel, which may or may not have been yet
 * read by the remote processor.
 */
void *ipc_request_channel(struct ipc_client *cl)
{
	struct ipc_chan *chan;
	struct ipc_con *con;
	unsigned long flags;
	char *con_name;
	int len, ret;

	con_name = cl->chan_name;
	len = strcspn(cl->chan_name, ":");

	ret = 0;
	mutex_lock(&con_mutex);
	list_for_each_entry(con, &ipc_cons, node)
		if (!strncmp(con->name, con_name, len)) {
			ret = 1;
			break;
		}
	mutex_unlock(&con_mutex);

	if (!ret) {
		pr_err("Controller(%s) not found!\n", cl->chan_name);
		return NULL;
	}

	ret = 0;
	list_for_each_entry(chan, &con->channels, node) {
		if (!strcmp(con_name + len + 1, chan->name)
				&& !chan->assigned) {
			spin_lock_irqsave(&chan->lock, flags);
			chan->msg_free = 0;
			chan->msg_count = 0;
			chan->active_req = NULL;
			chan->rxcb = cl->rxcb;
			chan->txcb = cl->txcb;
			chan->cl_id = cl->cl_id;
			chan->assigned = true;
			chan->tx_block = cl->tx_block;
			if (!cl->tx_tout)
				chan->tx_tout = ~0;
			else
				chan->tx_tout = msecs_to_jiffies(cl->tx_tout);
			if (chan->txdone_method	== TXDONE_BY_POLL
					&& cl->knows_txdone)
				chan->txdone_method |= TXDONE_BY_ACK;
			spin_unlock_irqrestore(&chan->lock, flags);
			ret = 1;
			break;
		}
	}

	if (!ret) {
		pr_err("Unable to assign mailbox(%s)\n", cl->chan_name);
		return NULL;
	}

	ret = chan->link_ops->startup(chan->link, cl->link_data);
	if (ret) {
		pr_err("Unable to startup the link\n");
		ipc_free_channel((void *)chan);
		return NULL;
	}

	return (void *)chan;
}
EXPORT_SYMBOL(ipc_request_channel);

/* Drop any messages queued and release the channel */
void ipc_free_channel(void *ch)
{
	struct ipc_chan *chan = (struct ipc_chan *)ch;
	unsigned long flags;

	if (!chan || !chan->assigned)
		return;

	chan->link_ops->shutdown(chan->link);

	/* The queued TX requests are simply aborted, no callbacks are made */
	spin_lock_irqsave(&chan->lock, flags);
	chan->assigned = false;
	chan->active_req = NULL;
	if (chan->txdone_method == (TXDONE_BY_POLL | TXDONE_BY_ACK))
		chan->txdone_method = TXDONE_BY_POLL;
	spin_unlock_irqrestore(&chan->lock, flags);

	blocking_notifier_call_chain(&chan->avail, 0, NULL);
}
EXPORT_SYMBOL(ipc_free_channel);

static struct ipc_chan *name_to_chan(const char *name)
{
	struct ipc_chan *chan = NULL;
	struct ipc_con *con;
	int len, found = 0;

	len = strcspn(name, ":");

	mutex_lock(&con_mutex);

	list_for_each_entry(con, &ipc_cons, node) {
		if (!strncmp(con->name, name, len)) {
			list_for_each_entry(chan, &con->channels, node) {
				if (!strcmp(name + len + 1, chan->name)) {
					found = 1;
					goto done;
				}
			}
		}
	}
done:
	mutex_unlock(&con_mutex);

	if (!found)
		return NULL;

	return chan;
}

int ipc_notify_chan_register(const char *name, struct notifier_block *nb)
{
	struct ipc_chan *chan = name_to_chan(name);

	if (chan && nb)
		return blocking_notifier_chain_register(&chan->avail, nb);

	return -EINVAL;
}
EXPORT_SYMBOL(ipc_notify_chan_register);

void ipc_notify_chan_unregister(const char *name, struct notifier_block *nb)
{
	struct ipc_chan *chan = name_to_chan(name);

	if (chan && nb)
		blocking_notifier_chain_unregister(&chan->avail, nb);
}
EXPORT_SYMBOL(ipc_notify_chan_unregister);

/*
 * Call for IPC controller drivers to register a controller, adding
 * its channels/mailboxes to the global pool.
 */
int ipc_links_register(struct ipc_controller *ipc)
{
	int i, num_links, txdone;
	struct ipc_chan *chan;
	struct ipc_con *con;

	/* Sanity check */
	if (!ipc || !ipc->ops)
		return -EINVAL;

	for (i = 0; ipc->links[i]; i++)
		;
	if (!i)
		return -EINVAL;
	num_links = i;

	mutex_lock(&con_mutex);
	/* Check if already populated */
	list_for_each_entry(con, &ipc_cons, node)
		if (!strcmp(ipc->controller_name, con->name)) {
			mutex_unlock(&con_mutex);
			return -EINVAL;
		}
	mutex_unlock(&con_mutex);

	con = kzalloc(sizeof(*con) + sizeof(*chan) * num_links, GFP_KERNEL);
	if (!con)
		return -ENOMEM;

	INIT_LIST_HEAD(&con->channels);
	snprintf(con->name, 16, "%s", ipc->controller_name);

	if (ipc->txdone_irq)
		txdone = TXDONE_BY_IRQ;
	else if (ipc->txdone_poll)
		txdone = TXDONE_BY_POLL;
	else /* It has to be ACK then */
		txdone = TXDONE_BY_ACK;

	if (txdone == TXDONE_BY_POLL) {
		con->period = ipc->txpoll_period;
		con->poll.function = &poll_txdone;
		con->poll.data = (unsigned long)con;
		init_timer(&con->poll);
	}

	chan = (void *)con + sizeof(*con);
	for (i = 0; i < num_links; i++) {
		chan[i].con = con;
		chan[i].assigned = false;
		chan[i].link_ops = ipc->ops;
		chan[i].link = ipc->links[i];
		chan[i].txdone_method = txdone;
		chan[i].link->api_priv = &chan[i];
		spin_lock_init(&chan[i].lock);
		BLOCKING_INIT_NOTIFIER_HEAD(&chan[i].avail);
		list_add_tail(&chan[i].node, &con->channels);
		snprintf(chan[i].name, 16, "%s", ipc->links[i]->link_name);
	}

	mutex_lock(&con_mutex);
	list_add_tail(&con->node, &ipc_cons);
	mutex_unlock(&con_mutex);

	return 0;
}
EXPORT_SYMBOL(ipc_links_register);

void ipc_links_unregister(struct ipc_controller *ipc)
{
	struct ipc_con *t, *con = NULL;
	struct ipc_chan *chan;

	mutex_lock(&con_mutex);

	list_for_each_entry(t, &ipc_cons, node)
		if (!strcmp(ipc->controller_name, t->name)) {
			con = t;
			break;
		}

	if (con)
		list_del(&con->node);

	mutex_unlock(&con_mutex);

	if (!con)
		return;

	list_for_each_entry(chan, &con->channels, node)
		ipc_free_channel((void *)chan);

	del_timer_sync(&con->poll);

	kfree(con);
}
EXPORT_SYMBOL(ipc_links_unregister);

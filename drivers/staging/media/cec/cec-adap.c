/*
 * cec-adap.c - HDMI Consumer Electronics Control framework - CEC adapter
 *
 * Copyright 2016 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/types.h>

#include "cec-priv.h"

static int cec_report_features(struct cec_adapter *adap, unsigned int la_idx);
static int cec_report_phys_addr(struct cec_adapter *adap, unsigned int la_idx);

/*
 * 400 ms is the time it takes for one 16 byte message to be
 * transferred and 5 is the maximum number of retries. Add
 * another 100 ms as a margin. So if the transmit doesn't
 * finish before that time something is really wrong and we
 * have to time out.
 *
 * This is a sign that something it really wrong and a warning
 * will be issued.
 */
#define CEC_XFER_TIMEOUT_MS (5 * 400 + 100)

#define call_op(adap, op, arg...) \
	(adap->ops->op ? adap->ops->op(adap, ## arg) : 0)

#define call_void_op(adap, op, arg...)			\
	do {						\
		if (adap->ops->op)			\
			adap->ops->op(adap, ## arg);	\
	} while (0)

static int cec_log_addr2idx(const struct cec_adapter *adap, u8 log_addr)
{
	int i;

	for (i = 0; i < adap->log_addrs.num_log_addrs; i++)
		if (adap->log_addrs.log_addr[i] == log_addr)
			return i;
	return -1;
}

static unsigned int cec_log_addr2dev(const struct cec_adapter *adap, u8 log_addr)
{
	int i = cec_log_addr2idx(adap, log_addr);

	return adap->log_addrs.primary_device_type[i < 0 ? 0 : i];
}

/*
 * Queue a new event for this filehandle. If ts == 0, then set it
 * to the current time.
 *
 * The two events that are currently defined do not need to keep track
 * of intermediate events, so no actual queue of events is needed,
 * instead just store the latest state and the total number of lost
 * messages.
 *
 * Should new events be added in the future that require intermediate
 * results to be queued as well, then a proper queue data structure is
 * required. But until then, just keep it simple.
 */
void cec_queue_event_fh(struct cec_fh *fh,
			const struct cec_event *new_ev, u64 ts)
{
	struct cec_event *ev = &fh->events[new_ev->event - 1];

	if (ts == 0)
		ts = ktime_get_ns();

	mutex_lock(&fh->lock);
	if (new_ev->event == CEC_EVENT_LOST_MSGS &&
	    fh->pending_events & (1 << new_ev->event)) {
		/*
		 * If there is already a lost_msgs event, then just
		 * update the lost_msgs count. This effectively
		 * merges the old and new events into one.
		 */
		ev->lost_msgs.lost_msgs += new_ev->lost_msgs.lost_msgs;
		goto unlock;
	}

	/*
	 * Intermediate states are not interesting, so just
	 * overwrite any older event.
	 */
	*ev = *new_ev;
	ev->ts = ts;
	fh->pending_events |= 1 << new_ev->event;

unlock:
	mutex_unlock(&fh->lock);
	wake_up_interruptible(&fh->wait);
}

/* Queue a new event for all open filehandles. */
static void cec_queue_event(struct cec_adapter *adap,
			    const struct cec_event *ev)
{
	u64 ts = ktime_get_ns();
	struct cec_fh *fh;

	mutex_lock(&adap->devnode.fhs_lock);
	list_for_each_entry(fh, &adap->devnode.fhs, list)
		cec_queue_event_fh(fh, ev, ts);
	mutex_unlock(&adap->devnode.fhs_lock);
}

/*
 * Queue a new message for this filehandle. If there is no more room
 * in the queue, then send the LOST_MSGS event instead.
 */
static void cec_queue_msg_fh(struct cec_fh *fh, const struct cec_msg *msg)
{
	static const struct cec_event ev_lost_msg = {
		.ts = 0,
		.event = CEC_EVENT_LOST_MSGS,
		.flags = 0,
		{
			.lost_msgs.lost_msgs = 1,
		},
	};
	struct cec_msg_entry *entry;

	mutex_lock(&fh->lock);
	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		goto lost_msgs;

	entry->msg = *msg;
	/* Add new msg at the end of the queue */
	list_add_tail(&entry->list, &fh->msgs);

	/*
	 * if the queue now has more than CEC_MAX_MSG_QUEUE_SZ
	 * messages, drop the oldest one and send a lost message event.
	 */
	if (fh->queued_msgs == CEC_MAX_MSG_QUEUE_SZ) {
		list_del(&entry->list);
		goto lost_msgs;
	}
	fh->queued_msgs++;
	mutex_unlock(&fh->lock);
	wake_up_interruptible(&fh->wait);
	return;

lost_msgs:
	mutex_unlock(&fh->lock);
	cec_queue_event_fh(fh, &ev_lost_msg, 0);
}

/*
 * Queue the message for those filehandles that are in monitor mode.
 * If valid_la is true (this message is for us or was sent by us),
 * then pass it on to any monitoring filehandle. If this message
 * isn't for us or from us, then only give it to filehandles that
 * are in MONITOR_ALL mode.
 *
 * This can only happen if the CEC_CAP_MONITOR_ALL capability is
 * set and the CEC adapter was placed in 'monitor all' mode.
 */
static void cec_queue_msg_monitor(struct cec_adapter *adap,
				  const struct cec_msg *msg,
				  bool valid_la)
{
	struct cec_fh *fh;
	u32 monitor_mode = valid_la ? CEC_MODE_MONITOR :
				      CEC_MODE_MONITOR_ALL;

	mutex_lock(&adap->devnode.fhs_lock);
	list_for_each_entry(fh, &adap->devnode.fhs, list) {
		if (fh->mode_follower >= monitor_mode)
			cec_queue_msg_fh(fh, msg);
	}
	mutex_unlock(&adap->devnode.fhs_lock);
}

/*
 * Queue the message for follower filehandles.
 */
static void cec_queue_msg_followers(struct cec_adapter *adap,
				    const struct cec_msg *msg)
{
	struct cec_fh *fh;

	mutex_lock(&adap->devnode.fhs_lock);
	list_for_each_entry(fh, &adap->devnode.fhs, list) {
		if (fh->mode_follower == CEC_MODE_FOLLOWER)
			cec_queue_msg_fh(fh, msg);
	}
	mutex_unlock(&adap->devnode.fhs_lock);
}

/* Notify userspace of an adapter state change. */
static void cec_post_state_event(struct cec_adapter *adap)
{
	struct cec_event ev = {
		.event = CEC_EVENT_STATE_CHANGE,
	};

	ev.state_change.phys_addr = adap->phys_addr;
	ev.state_change.log_addr_mask = adap->log_addrs.log_addr_mask;
	cec_queue_event(adap, &ev);
}

/*
 * A CEC transmit (and a possible wait for reply) completed.
 * If this was in blocking mode, then complete it, otherwise
 * queue the message for userspace to dequeue later.
 *
 * This function is called with adap->lock held.
 */
static void cec_data_completed(struct cec_data *data)
{
	/*
	 * Delete this transmit from the filehandle's xfer_list since
	 * we're done with it.
	 *
	 * Note that if the filehandle is closed before this transmit
	 * finished, then the release() function will set data->fh to NULL.
	 * Without that we would be referring to a closed filehandle.
	 */
	if (data->fh)
		list_del(&data->xfer_list);

	if (data->blocking) {
		/*
		 * Someone is blocking so mark the message as completed
		 * and call complete.
		 */
		data->completed = true;
		complete(&data->c);
	} else {
		/*
		 * No blocking, so just queue the message if needed and
		 * free the memory.
		 */
		if (data->fh)
			cec_queue_msg_fh(data->fh, &data->msg);
		kfree(data);
	}
}

/*
 * A pending CEC transmit needs to be cancelled, either because the CEC
 * adapter is disabled or the transmit takes an impossibly long time to
 * finish.
 *
 * This function is called with adap->lock held.
 */
static void cec_data_cancel(struct cec_data *data)
{
	/*
	 * It's either the current transmit, or it is a pending
	 * transmit. Take the appropriate action to clear it.
	 */
	if (data->adap->transmitting == data)
		data->adap->transmitting = NULL;
	else
		list_del_init(&data->list);

	/* Mark it as an error */
	data->msg.tx_ts = ktime_get_ns();
	data->msg.tx_status = CEC_TX_STATUS_ERROR |
			      CEC_TX_STATUS_MAX_RETRIES;
	data->attempts = 0;
	data->msg.tx_error_cnt = 1;
	/* Queue transmitted message for monitoring purposes */
	cec_queue_msg_monitor(data->adap, &data->msg, 1);

	cec_data_completed(data);
}

/*
 * Main CEC state machine
 *
 * Wait until the thread should be stopped, or we are not transmitting and
 * a new transmit message is queued up, in which case we start transmitting
 * that message. When the adapter finished transmitting the message it will
 * call cec_transmit_done().
 *
 * If the adapter is disabled, then remove all queued messages instead.
 *
 * If the current transmit times out, then cancel that transmit.
 */
int cec_thread_func(void *_adap)
{
	struct cec_adapter *adap = _adap;

	for (;;) {
		unsigned int signal_free_time;
		struct cec_data *data;
		bool timeout = false;
		u8 attempts;

		if (adap->transmitting) {
			int err;

			/*
			 * We are transmitting a message, so add a timeout
			 * to prevent the state machine to get stuck waiting
			 * for this message to finalize and add a check to
			 * see if the adapter is disabled in which case the
			 * transmit should be canceled.
			 */
			err = wait_event_interruptible_timeout(adap->kthread_waitq,
				kthread_should_stop() ||
				adap->phys_addr == CEC_PHYS_ADDR_INVALID ||
				(!adap->transmitting &&
				 !list_empty(&adap->transmit_queue)),
				msecs_to_jiffies(CEC_XFER_TIMEOUT_MS));
			timeout = err == 0;
		} else {
			/* Otherwise we just wait for something to happen. */
			wait_event_interruptible(adap->kthread_waitq,
				kthread_should_stop() ||
				(!adap->transmitting &&
				 !list_empty(&adap->transmit_queue)));
		}

		mutex_lock(&adap->lock);

		if (adap->phys_addr == CEC_PHYS_ADDR_INVALID ||
		    kthread_should_stop()) {
			/*
			 * If the adapter is disabled, or we're asked to stop,
			 * then cancel any pending transmits.
			 */
			while (!list_empty(&adap->transmit_queue)) {
				data = list_first_entry(&adap->transmit_queue,
							struct cec_data, list);
				cec_data_cancel(data);
			}
			if (adap->transmitting)
				cec_data_cancel(adap->transmitting);

			/*
			 * Cancel the pending timeout work. We have to unlock
			 * the mutex when flushing the work since
			 * cec_wait_timeout() will take it. This is OK since
			 * no new entries can be added to wait_queue as long
			 * as adap->transmitting is NULL, which it is due to
			 * the cec_data_cancel() above.
			 */
			while (!list_empty(&adap->wait_queue)) {
				data = list_first_entry(&adap->wait_queue,
							struct cec_data, list);

				if (!cancel_delayed_work(&data->work)) {
					mutex_unlock(&adap->lock);
					flush_scheduled_work();
					mutex_lock(&adap->lock);
				}
				cec_data_cancel(data);
			}
			goto unlock;
		}

		if (adap->transmitting && timeout) {
			/*
			 * If we timeout, then log that. This really shouldn't
			 * happen and is an indication of a faulty CEC adapter
			 * driver, or the CEC bus is in some weird state.
			 */
			dprintk(0, "message %*ph timed out!\n",
				adap->transmitting->msg.len,
				adap->transmitting->msg.msg);
			/* Just give up on this. */
			cec_data_cancel(adap->transmitting);
			goto unlock;
		}

		/*
		 * If we are still transmitting, or there is nothing new to
		 * transmit, then just continue waiting.
		 */
		if (adap->transmitting || list_empty(&adap->transmit_queue))
			goto unlock;

		/* Get a new message to transmit */
		data = list_first_entry(&adap->transmit_queue,
					struct cec_data, list);
		list_del_init(&data->list);
		/* Make this the current transmitting message */
		adap->transmitting = data;

		/*
		 * Suggested number of attempts as per the CEC 2.0 spec:
		 * 4 attempts is the default, except for 'secondary poll
		 * messages', i.e. poll messages not sent during the adapter
		 * configuration phase when it allocates logical addresses.
		 */
		if (data->msg.len == 1 && adap->is_configured)
			attempts = 2;
		else
			attempts = 4;

		/* Set the suggested signal free time */
		if (data->attempts) {
			/* should be >= 3 data bit periods for a retry */
			signal_free_time = CEC_SIGNAL_FREE_TIME_RETRY;
		} else if (data->new_initiator) {
			/* should be >= 5 data bit periods for new initiator */
			signal_free_time = CEC_SIGNAL_FREE_TIME_NEW_INITIATOR;
		} else {
			/*
			 * should be >= 7 data bit periods for sending another
			 * frame immediately after another.
			 */
			signal_free_time = CEC_SIGNAL_FREE_TIME_NEXT_XFER;
		}
		if (data->attempts == 0)
			data->attempts = attempts;

		/* Tell the adapter to transmit, cancel on error */
		if (adap->ops->adap_transmit(adap, data->attempts,
					     signal_free_time, &data->msg))
			cec_data_cancel(data);

unlock:
		mutex_unlock(&adap->lock);

		if (kthread_should_stop())
			break;
	}
	return 0;
}

/*
 * Called by the CEC adapter if a transmit finished.
 */
void cec_transmit_done(struct cec_adapter *adap, u8 status, u8 arb_lost_cnt,
		       u8 nack_cnt, u8 low_drive_cnt, u8 error_cnt)
{
	struct cec_data *data;
	struct cec_msg *msg;
	u64 ts = ktime_get_ns();

	dprintk(2, "cec_transmit_done %02x\n", status);
	mutex_lock(&adap->lock);
	data = adap->transmitting;
	if (!data) {
		/*
		 * This can happen if a transmit was issued and the cable is
		 * unplugged while the transmit is ongoing. Ignore this
		 * transmit in that case.
		 */
		dprintk(1, "cec_transmit_done without an ongoing transmit!\n");
		goto unlock;
	}

	msg = &data->msg;

	/* Drivers must fill in the status! */
	WARN_ON(status == 0);
	msg->tx_ts = ts;
	msg->tx_status |= status;
	msg->tx_arb_lost_cnt += arb_lost_cnt;
	msg->tx_nack_cnt += nack_cnt;
	msg->tx_low_drive_cnt += low_drive_cnt;
	msg->tx_error_cnt += error_cnt;

	/* Mark that we're done with this transmit */
	adap->transmitting = NULL;

	/*
	 * If there are still retry attempts left and there was an error and
	 * the hardware didn't signal that it retried itself (by setting
	 * CEC_TX_STATUS_MAX_RETRIES), then we will retry ourselves.
	 */
	if (data->attempts > 1 &&
	    !(status & (CEC_TX_STATUS_MAX_RETRIES | CEC_TX_STATUS_OK))) {
		/* Retry this message */
		data->attempts--;
		/* Add the message in front of the transmit queue */
		list_add(&data->list, &adap->transmit_queue);
		goto wake_thread;
	}

	data->attempts = 0;

	/* Always set CEC_TX_STATUS_MAX_RETRIES on error */
	if (!(status & CEC_TX_STATUS_OK))
		msg->tx_status |= CEC_TX_STATUS_MAX_RETRIES;

	/* Queue transmitted message for monitoring purposes */
	cec_queue_msg_monitor(adap, msg, 1);

	if ((status & CEC_TX_STATUS_OK) && adap->is_configured &&
	    msg->timeout) {
		/*
		 * Queue the message into the wait queue if we want to wait
		 * for a reply.
		 */
		list_add_tail(&data->list, &adap->wait_queue);
		schedule_delayed_work(&data->work,
				      msecs_to_jiffies(msg->timeout));
	} else {
		/* Otherwise we're done */
		cec_data_completed(data);
	}

wake_thread:
	/*
	 * Wake up the main thread to see if another message is ready
	 * for transmitting or to retry the current message.
	 */
	wake_up_interruptible(&adap->kthread_waitq);
unlock:
	mutex_unlock(&adap->lock);
}
EXPORT_SYMBOL_GPL(cec_transmit_done);

/*
 * Called when waiting for a reply times out.
 */
static void cec_wait_timeout(struct work_struct *work)
{
	struct cec_data *data = container_of(work, struct cec_data, work.work);
	struct cec_adapter *adap = data->adap;

	mutex_lock(&adap->lock);
	/*
	 * Sanity check in case the timeout and the arrival of the message
	 * happened at the same time.
	 */
	if (list_empty(&data->list))
		goto unlock;

	/* Mark the message as timed out */
	list_del_init(&data->list);
	data->msg.rx_ts = ktime_get_ns();
	data->msg.rx_status = CEC_RX_STATUS_TIMEOUT;
	cec_data_completed(data);
unlock:
	mutex_unlock(&adap->lock);
}

/*
 * Transmit a message. The fh argument may be NULL if the transmit is not
 * associated with a specific filehandle.
 *
 * This function is called with adap->lock held.
 */
int cec_transmit_msg_fh(struct cec_adapter *adap, struct cec_msg *msg,
			struct cec_fh *fh, bool block)
{
	struct cec_data *data;
	u8 last_initiator = 0xff;
	unsigned int timeout;
	int res = 0;

	if (msg->reply && msg->timeout == 0) {
		/* Make sure the timeout isn't 0. */
		msg->timeout = 1000;
	}

	/* Sanity checks */
	if (msg->len == 0 || msg->len > CEC_MAX_MSG_SIZE) {
		dprintk(1, "cec_transmit_msg: invalid length %d\n", msg->len);
		return -EINVAL;
	}
	if (msg->timeout && msg->len == 1) {
		dprintk(1, "cec_transmit_msg: can't reply for poll msg\n");
		return -EINVAL;
	}
	if (msg->len == 1) {
		if (cec_msg_initiator(msg) != 0xf ||
		    cec_msg_destination(msg) == 0xf) {
			dprintk(1, "cec_transmit_msg: invalid poll message\n");
			return -EINVAL;
		}
		if (cec_has_log_addr(adap, cec_msg_destination(msg))) {
			/*
			 * If the destination is a logical address our adapter
			 * has already claimed, then just NACK this.
			 * It depends on the hardware what it will do with a
			 * POLL to itself (some OK this), so it is just as
			 * easy to handle it here so the behavior will be
			 * consistent.
			 */
			msg->tx_ts = ktime_get_ns();
			msg->tx_status = CEC_TX_STATUS_NACK |
					 CEC_TX_STATUS_MAX_RETRIES;
			msg->tx_nack_cnt = 1;
			return 0;
		}
	}
	if (msg->len > 1 && !cec_msg_is_broadcast(msg) &&
	    cec_has_log_addr(adap, cec_msg_destination(msg))) {
		dprintk(1, "cec_transmit_msg: destination is the adapter itself\n");
		return -EINVAL;
	}
	if (cec_msg_initiator(msg) != 0xf &&
	    !cec_has_log_addr(adap, cec_msg_initiator(msg))) {
		dprintk(1, "cec_transmit_msg: initiator has unknown logical address %d\n",
			cec_msg_initiator(msg));
		return -EINVAL;
	}
	if (!adap->is_configured && !adap->is_configuring)
		return -ENONET;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (msg->len > 1 && msg->msg[1] == CEC_MSG_CDC_MESSAGE) {
		msg->msg[2] = adap->phys_addr >> 8;
		msg->msg[3] = adap->phys_addr & 0xff;
	}

	if (msg->timeout)
		dprintk(2, "cec_transmit_msg: %*ph (wait for 0x%02x%s)\n",
			msg->len, msg->msg, msg->reply, !block ? ", nb" : "");
	else
		dprintk(2, "cec_transmit_msg: %*ph%s\n",
			msg->len, msg->msg, !block ? " (nb)" : "");

	msg->rx_ts = 0;
	msg->tx_ts = 0;
	msg->rx_status = 0;
	msg->tx_status = 0;
	msg->tx_arb_lost_cnt = 0;
	msg->tx_nack_cnt = 0;
	msg->tx_low_drive_cnt = 0;
	msg->tx_error_cnt = 0;
	data->msg = *msg;
	data->fh = fh;
	data->adap = adap;
	data->blocking = block;

	/*
	 * Determine if this message follows a message from the same
	 * initiator. Needed to determine the free signal time later on.
	 */
	if (msg->len > 1) {
		if (!(list_empty(&adap->transmit_queue))) {
			const struct cec_data *last;

			last = list_last_entry(&adap->transmit_queue,
					       const struct cec_data, list);
			last_initiator = cec_msg_initiator(&last->msg);
		} else if (adap->transmitting) {
			last_initiator =
				cec_msg_initiator(&adap->transmitting->msg);
		}
	}
	data->new_initiator = last_initiator != cec_msg_initiator(msg);
	init_completion(&data->c);
	INIT_DELAYED_WORK(&data->work, cec_wait_timeout);

	data->msg.sequence = adap->sequence++;
	if (fh)
		list_add_tail(&data->xfer_list, &fh->xfer_list);
	list_add_tail(&data->list, &adap->transmit_queue);
	if (!adap->transmitting)
		wake_up_interruptible(&adap->kthread_waitq);

	/* All done if we don't need to block waiting for completion */
	if (!block)
		return 0;

	/*
	 * If we don't get a completion before this time something is really
	 * wrong and we time out.
	 */
	timeout = CEC_XFER_TIMEOUT_MS;
	/* Add the requested timeout if we have to wait for a reply as well */
	if (msg->timeout)
		timeout += msg->timeout;

	/*
	 * Release the lock and wait, retake the lock afterwards.
	 */
	mutex_unlock(&adap->lock);
	res = wait_for_completion_killable_timeout(&data->c,
						   msecs_to_jiffies(timeout));
	mutex_lock(&adap->lock);

	if (data->completed) {
		/* The transmit completed (possibly with an error) */
		*msg = data->msg;
		kfree(data);
		return 0;
	}
	/*
	 * The wait for completion timed out or was interrupted, so mark this
	 * as non-blocking and disconnect from the filehandle since it is
	 * still 'in flight'. When it finally completes it will just drop the
	 * result silently.
	 */
	data->blocking = false;
	if (data->fh)
		list_del(&data->xfer_list);
	data->fh = NULL;

	if (res == 0) { /* timed out */
		/* Check if the reply or the transmit failed */
		if (msg->timeout && (msg->tx_status & CEC_TX_STATUS_OK))
			msg->rx_status = CEC_RX_STATUS_TIMEOUT;
		else
			msg->tx_status = CEC_TX_STATUS_MAX_RETRIES;
	}
	return res > 0 ? 0 : res;
}

/* Helper function to be used by drivers and this framework. */
int cec_transmit_msg(struct cec_adapter *adap, struct cec_msg *msg,
		     bool block)
{
	int ret;

	mutex_lock(&adap->lock);
	ret = cec_transmit_msg_fh(adap, msg, NULL, block);
	mutex_unlock(&adap->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(cec_transmit_msg);

/*
 * I don't like forward references but without this the low-level
 * cec_received_msg() function would come after a bunch of high-level
 * CEC protocol handling functions. That was very confusing.
 */
static int cec_receive_notify(struct cec_adapter *adap, struct cec_msg *msg,
			      bool is_reply);

/* Called by the CEC adapter if a message is received */
void cec_received_msg(struct cec_adapter *adap, struct cec_msg *msg)
{
	struct cec_data *data;
	u8 msg_init = cec_msg_initiator(msg);
	u8 msg_dest = cec_msg_destination(msg);
	bool is_reply = false;
	bool valid_la = true;

	if (WARN_ON(!msg->len || msg->len > CEC_MAX_MSG_SIZE))
		return;

	msg->rx_ts = ktime_get_ns();
	msg->rx_status = CEC_RX_STATUS_OK;
	msg->sequence = msg->reply = msg->timeout = 0;
	msg->tx_status = 0;
	msg->tx_ts = 0;
	msg->flags = 0;

	mutex_lock(&adap->lock);
	dprintk(2, "cec_received_msg: %*ph\n", msg->len, msg->msg);

	/* Check if this message was for us (directed or broadcast). */
	if (!cec_msg_is_broadcast(msg))
		valid_la = cec_has_log_addr(adap, msg_dest);

	/* It's a valid message and not a poll or CDC message */
	if (valid_la && msg->len > 1 && msg->msg[1] != CEC_MSG_CDC_MESSAGE) {
		u8 cmd = msg->msg[1];
		bool abort = cmd == CEC_MSG_FEATURE_ABORT;

		/* The aborted command is in msg[2] */
		if (abort)
			cmd = msg->msg[2];

		/*
		 * Walk over all transmitted messages that are waiting for a
		 * reply.
		 */
		list_for_each_entry(data, &adap->wait_queue, list) {
			struct cec_msg *dst = &data->msg;

			/* Does the command match? */
			if ((abort && cmd != dst->msg[1]) ||
			    (!abort && cmd != dst->reply))
				continue;

			/* Does the addressing match? */
			if (msg_init != cec_msg_destination(dst) &&
			    !cec_msg_is_broadcast(dst))
				continue;

			/* We got a reply */
			memcpy(dst->msg, msg->msg, msg->len);
			dst->len = msg->len;
			dst->rx_ts = msg->rx_ts;
			dst->rx_status = msg->rx_status;
			if (abort)
				dst->rx_status |= CEC_RX_STATUS_FEATURE_ABORT;
			/* Remove it from the wait_queue */
			list_del_init(&data->list);

			/* Cancel the pending timeout work */
			if (!cancel_delayed_work(&data->work)) {
				mutex_unlock(&adap->lock);
				flush_scheduled_work();
				mutex_lock(&adap->lock);
			}
			/*
			 * Mark this as a reply, provided someone is still
			 * waiting for the answer.
			 */
			if (data->fh)
				is_reply = true;
			cec_data_completed(data);
			break;
		}
	}
	mutex_unlock(&adap->lock);

	/* Pass the message on to any monitoring filehandles */
	cec_queue_msg_monitor(adap, msg, valid_la);

	/* We're done if it is not for us or a poll message */
	if (!valid_la || msg->len <= 1)
		return;

	/*
	 * Process the message on the protocol level. If is_reply is true,
	 * then cec_receive_notify() won't pass on the reply to the listener(s)
	 * since that was already done by cec_data_completed() above.
	 */
	cec_receive_notify(adap, msg, is_reply);
}
EXPORT_SYMBOL_GPL(cec_received_msg);

/* Logical Address Handling */

/*
 * Attempt to claim a specific logical address.
 *
 * This function is called with adap->lock held.
 */
static int cec_config_log_addr(struct cec_adapter *adap,
			       unsigned int idx,
			       unsigned int log_addr)
{
	struct cec_log_addrs *las = &adap->log_addrs;
	struct cec_msg msg = { };
	int err;

	if (cec_has_log_addr(adap, log_addr))
		return 0;

	/* Send poll message */
	msg.len = 1;
	msg.msg[0] = 0xf0 | log_addr;
	err = cec_transmit_msg_fh(adap, &msg, NULL, true);

	/*
	 * While trying to poll the physical address was reset
	 * and the adapter was unconfigured, so bail out.
	 */
	if (!adap->is_configuring)
		return -EINTR;

	if (err)
		return err;

	if (msg.tx_status & CEC_TX_STATUS_OK)
		return 0;

	/*
	 * Message not acknowledged, so this logical
	 * address is free to use.
	 */
	err = adap->ops->adap_log_addr(adap, log_addr);
	if (err)
		return err;

	las->log_addr[idx] = log_addr;
	las->log_addr_mask |= 1 << log_addr;
	adap->phys_addrs[log_addr] = adap->phys_addr;

	dprintk(2, "claimed addr %d (%d)\n", log_addr,
		las->primary_device_type[idx]);
	return 1;
}

/*
 * Unconfigure the adapter: clear all logical addresses and send
 * the state changed event.
 *
 * This function is called with adap->lock held.
 */
static void cec_adap_unconfigure(struct cec_adapter *adap)
{
	WARN_ON(adap->ops->adap_log_addr(adap, CEC_LOG_ADDR_INVALID));
	adap->log_addrs.log_addr_mask = 0;
	adap->is_configuring = false;
	adap->is_configured = false;
	memset(adap->phys_addrs, 0xff, sizeof(adap->phys_addrs));
	wake_up_interruptible(&adap->kthread_waitq);
	cec_post_state_event(adap);
}

/*
 * Attempt to claim the required logical addresses.
 */
static int cec_config_thread_func(void *arg)
{
	/* The various LAs for each type of device */
	static const u8 tv_log_addrs[] = {
		CEC_LOG_ADDR_TV, CEC_LOG_ADDR_SPECIFIC,
		CEC_LOG_ADDR_INVALID
	};
	static const u8 record_log_addrs[] = {
		CEC_LOG_ADDR_RECORD_1, CEC_LOG_ADDR_RECORD_2,
		CEC_LOG_ADDR_RECORD_3,
		CEC_LOG_ADDR_BACKUP_1, CEC_LOG_ADDR_BACKUP_2,
		CEC_LOG_ADDR_INVALID
	};
	static const u8 tuner_log_addrs[] = {
		CEC_LOG_ADDR_TUNER_1, CEC_LOG_ADDR_TUNER_2,
		CEC_LOG_ADDR_TUNER_3, CEC_LOG_ADDR_TUNER_4,
		CEC_LOG_ADDR_BACKUP_1, CEC_LOG_ADDR_BACKUP_2,
		CEC_LOG_ADDR_INVALID
	};
	static const u8 playback_log_addrs[] = {
		CEC_LOG_ADDR_PLAYBACK_1, CEC_LOG_ADDR_PLAYBACK_2,
		CEC_LOG_ADDR_PLAYBACK_3,
		CEC_LOG_ADDR_BACKUP_1, CEC_LOG_ADDR_BACKUP_2,
		CEC_LOG_ADDR_INVALID
	};
	static const u8 audiosystem_log_addrs[] = {
		CEC_LOG_ADDR_AUDIOSYSTEM,
		CEC_LOG_ADDR_INVALID
	};
	static const u8 specific_use_log_addrs[] = {
		CEC_LOG_ADDR_SPECIFIC,
		CEC_LOG_ADDR_BACKUP_1, CEC_LOG_ADDR_BACKUP_2,
		CEC_LOG_ADDR_INVALID
	};
	static const u8 *type2addrs[6] = {
		[CEC_LOG_ADDR_TYPE_TV] = tv_log_addrs,
		[CEC_LOG_ADDR_TYPE_RECORD] = record_log_addrs,
		[CEC_LOG_ADDR_TYPE_TUNER] = tuner_log_addrs,
		[CEC_LOG_ADDR_TYPE_PLAYBACK] = playback_log_addrs,
		[CEC_LOG_ADDR_TYPE_AUDIOSYSTEM] = audiosystem_log_addrs,
		[CEC_LOG_ADDR_TYPE_SPECIFIC] = specific_use_log_addrs,
	};
	static const u16 type2mask[] = {
		[CEC_LOG_ADDR_TYPE_TV] = CEC_LOG_ADDR_MASK_TV,
		[CEC_LOG_ADDR_TYPE_RECORD] = CEC_LOG_ADDR_MASK_RECORD,
		[CEC_LOG_ADDR_TYPE_TUNER] = CEC_LOG_ADDR_MASK_TUNER,
		[CEC_LOG_ADDR_TYPE_PLAYBACK] = CEC_LOG_ADDR_MASK_PLAYBACK,
		[CEC_LOG_ADDR_TYPE_AUDIOSYSTEM] = CEC_LOG_ADDR_MASK_AUDIOSYSTEM,
		[CEC_LOG_ADDR_TYPE_SPECIFIC] = CEC_LOG_ADDR_MASK_SPECIFIC,
	};
	struct cec_adapter *adap = arg;
	struct cec_log_addrs *las = &adap->log_addrs;
	int err;
	int i, j;

	mutex_lock(&adap->lock);
	dprintk(1, "physical address: %x.%x.%x.%x, claim %d logical addresses\n",
		cec_phys_addr_exp(adap->phys_addr), las->num_log_addrs);
	las->log_addr_mask = 0;

	if (las->log_addr_type[0] == CEC_LOG_ADDR_TYPE_UNREGISTERED)
		goto configured;

	for (i = 0; i < las->num_log_addrs; i++) {
		unsigned int type = las->log_addr_type[i];
		const u8 *la_list;
		u8 last_la;

		/*
		 * The TV functionality can only map to physical address 0.
		 * For any other address, try the Specific functionality
		 * instead as per the spec.
		 */
		if (adap->phys_addr && type == CEC_LOG_ADDR_TYPE_TV)
			type = CEC_LOG_ADDR_TYPE_SPECIFIC;

		la_list = type2addrs[type];
		last_la = las->log_addr[i];
		las->log_addr[i] = CEC_LOG_ADDR_INVALID;
		if (last_la == CEC_LOG_ADDR_INVALID ||
		    last_la == CEC_LOG_ADDR_UNREGISTERED ||
		    !(last_la & type2mask[type]))
			last_la = la_list[0];

		err = cec_config_log_addr(adap, i, last_la);
		if (err > 0) /* Reused last LA */
			continue;

		if (err < 0)
			goto unconfigure;

		for (j = 0; la_list[j] != CEC_LOG_ADDR_INVALID; j++) {
			/* Tried this one already, skip it */
			if (la_list[j] == last_la)
				continue;
			/* The backup addresses are CEC 2.0 specific */
			if ((la_list[j] == CEC_LOG_ADDR_BACKUP_1 ||
			     la_list[j] == CEC_LOG_ADDR_BACKUP_2) &&
			    las->cec_version < CEC_OP_CEC_VERSION_2_0)
				continue;

			err = cec_config_log_addr(adap, i, la_list[j]);
			if (err == 0) /* LA is in use */
				continue;
			if (err < 0)
				goto unconfigure;
			/* Done, claimed an LA */
			break;
		}

		if (la_list[j] == CEC_LOG_ADDR_INVALID)
			dprintk(1, "could not claim LA %d\n", i);
	}

configured:
	if (adap->log_addrs.log_addr_mask == 0) {
		/* Fall back to unregistered */
		las->log_addr[0] = CEC_LOG_ADDR_UNREGISTERED;
		las->log_addr_mask = 1 << las->log_addr[0];
	}
	adap->is_configured = true;
	adap->is_configuring = false;
	cec_post_state_event(adap);
	mutex_unlock(&adap->lock);

	for (i = 0; i < las->num_log_addrs; i++) {
		if (las->log_addr[i] == CEC_LOG_ADDR_INVALID)
			continue;

		/*
		 * Report Features must come first according
		 * to CEC 2.0
		 */
		if (las->log_addr[i] != CEC_LOG_ADDR_UNREGISTERED)
			cec_report_features(adap, i);
		cec_report_phys_addr(adap, i);
	}
	mutex_lock(&adap->lock);
	adap->kthread_config = NULL;
	mutex_unlock(&adap->lock);
	complete(&adap->config_completion);
	return 0;

unconfigure:
	for (i = 0; i < las->num_log_addrs; i++)
		las->log_addr[i] = CEC_LOG_ADDR_INVALID;
	cec_adap_unconfigure(adap);
	adap->kthread_config = NULL;
	mutex_unlock(&adap->lock);
	complete(&adap->config_completion);
	return 0;
}

/*
 * Called from either __cec_s_phys_addr or __cec_s_log_addrs to claim the
 * logical addresses.
 *
 * This function is called with adap->lock held.
 */
static void cec_claim_log_addrs(struct cec_adapter *adap, bool block)
{
	if (WARN_ON(adap->is_configuring || adap->is_configured))
		return;

	init_completion(&adap->config_completion);

	/* Ready to kick off the thread */
	adap->is_configuring = true;
	adap->kthread_config = kthread_run(cec_config_thread_func, adap,
					   "ceccfg-%s", adap->name);
	if (IS_ERR(adap->kthread_config)) {
		adap->kthread_config = NULL;
	} else if (block) {
		mutex_unlock(&adap->lock);
		wait_for_completion(&adap->config_completion);
		mutex_lock(&adap->lock);
	}
}

/* Set a new physical address and send an event notifying userspace of this.
 *
 * This function is called with adap->lock held.
 */
void __cec_s_phys_addr(struct cec_adapter *adap, u16 phys_addr, bool block)
{
	if (phys_addr == adap->phys_addr || adap->devnode.unregistered)
		return;

	if (phys_addr == CEC_PHYS_ADDR_INVALID ||
	    adap->phys_addr != CEC_PHYS_ADDR_INVALID) {
		adap->phys_addr = CEC_PHYS_ADDR_INVALID;
		cec_post_state_event(adap);
		cec_adap_unconfigure(adap);
		/* Disabling monitor all mode should always succeed */
		if (adap->monitor_all_cnt)
			WARN_ON(call_op(adap, adap_monitor_all_enable, false));
		WARN_ON(adap->ops->adap_enable(adap, false));
		if (phys_addr == CEC_PHYS_ADDR_INVALID)
			return;
	}

	if (adap->ops->adap_enable(adap, true))
		return;

	if (adap->monitor_all_cnt &&
	    call_op(adap, adap_monitor_all_enable, true)) {
		WARN_ON(adap->ops->adap_enable(adap, false));
		return;
	}
	adap->phys_addr = phys_addr;
	cec_post_state_event(adap);
	if (adap->log_addrs.num_log_addrs)
		cec_claim_log_addrs(adap, block);
}

void cec_s_phys_addr(struct cec_adapter *adap, u16 phys_addr, bool block)
{
	if (IS_ERR_OR_NULL(adap))
		return;

	if (WARN_ON(adap->capabilities & CEC_CAP_PHYS_ADDR))
		return;
	mutex_lock(&adap->lock);
	__cec_s_phys_addr(adap, phys_addr, block);
	mutex_unlock(&adap->lock);
}
EXPORT_SYMBOL_GPL(cec_s_phys_addr);

/*
 * Called from either the ioctl or a driver to set the logical addresses.
 *
 * This function is called with adap->lock held.
 */
int __cec_s_log_addrs(struct cec_adapter *adap,
		      struct cec_log_addrs *log_addrs, bool block)
{
	u16 type_mask = 0;
	int i;

	if (adap->devnode.unregistered)
		return -ENODEV;

	if (!log_addrs || log_addrs->num_log_addrs == 0) {
		adap->log_addrs.num_log_addrs = 0;
		cec_adap_unconfigure(adap);
		return 0;
	}

	/* Ensure the osd name is 0-terminated */
	log_addrs->osd_name[sizeof(log_addrs->osd_name) - 1] = '\0';

	/* Sanity checks */
	if (log_addrs->num_log_addrs > adap->available_log_addrs) {
		dprintk(1, "num_log_addrs > %d\n", adap->available_log_addrs);
		return -EINVAL;
	}

	/*
	 * Vendor ID is a 24 bit number, so check if the value is
	 * within the correct range.
	 */
	if (log_addrs->vendor_id != CEC_VENDOR_ID_NONE &&
	    (log_addrs->vendor_id & 0xff000000) != 0)
		return -EINVAL;

	if (log_addrs->cec_version != CEC_OP_CEC_VERSION_1_4 &&
	    log_addrs->cec_version != CEC_OP_CEC_VERSION_2_0)
		return -EINVAL;

	if (log_addrs->num_log_addrs > 1)
		for (i = 0; i < log_addrs->num_log_addrs; i++)
			if (log_addrs->log_addr_type[i] ==
					CEC_LOG_ADDR_TYPE_UNREGISTERED) {
				dprintk(1, "num_log_addrs > 1 can't be combined with unregistered LA\n");
				return -EINVAL;
			}

	if (log_addrs->cec_version < CEC_OP_CEC_VERSION_2_0) {
		memset(log_addrs->all_device_types, 0,
		       sizeof(log_addrs->all_device_types));
		memset(log_addrs->features, 0, sizeof(log_addrs->features));
	}

	for (i = 0; i < log_addrs->num_log_addrs; i++) {
		u8 *features = log_addrs->features[i];
		bool op_is_dev_features = false;

		log_addrs->log_addr[i] = CEC_LOG_ADDR_INVALID;
		if (type_mask & (1 << log_addrs->log_addr_type[i])) {
			dprintk(1, "duplicate logical address type\n");
			return -EINVAL;
		}
		type_mask |= 1 << log_addrs->log_addr_type[i];
		if ((type_mask & (1 << CEC_LOG_ADDR_TYPE_RECORD)) &&
		    (type_mask & (1 << CEC_LOG_ADDR_TYPE_PLAYBACK))) {
			/* Record already contains the playback functionality */
			dprintk(1, "invalid record + playback combination\n");
			return -EINVAL;
		}
		if (log_addrs->primary_device_type[i] >
					CEC_OP_PRIM_DEVTYPE_PROCESSOR) {
			dprintk(1, "unknown primary device type\n");
			return -EINVAL;
		}
		if (log_addrs->primary_device_type[i] == 2) {
			dprintk(1, "invalid primary device type\n");
			return -EINVAL;
		}
		if (log_addrs->log_addr_type[i] > CEC_LOG_ADDR_TYPE_UNREGISTERED) {
			dprintk(1, "unknown logical address type\n");
			return -EINVAL;
		}
		if (log_addrs->cec_version < CEC_OP_CEC_VERSION_2_0)
			continue;

		for (i = 0; i < ARRAY_SIZE(log_addrs->features[0]); i++) {
			if ((features[i] & 0x80) == 0) {
				if (op_is_dev_features)
					break;
				op_is_dev_features = true;
			}
		}
		if (!op_is_dev_features ||
		    i == ARRAY_SIZE(log_addrs->features[0])) {
			dprintk(1, "malformed features\n");
			return -EINVAL;
		}
	}

	if (log_addrs->cec_version >= CEC_OP_CEC_VERSION_2_0) {
		if (log_addrs->num_log_addrs > 2) {
			dprintk(1, "CEC 2.0 allows no more than 2 logical addresses\n");
			return -EINVAL;
		}
		if (log_addrs->num_log_addrs == 2) {
			if (!(type_mask & ((1 << CEC_LOG_ADDR_TYPE_AUDIOSYSTEM) |
					   (1 << CEC_LOG_ADDR_TYPE_TV)))) {
				dprintk(1, "Two LAs is only allowed for audiosystem and TV\n");
				return -EINVAL;
			}
			if (!(type_mask & ((1 << CEC_LOG_ADDR_TYPE_PLAYBACK) |
					   (1 << CEC_LOG_ADDR_TYPE_RECORD)))) {
				dprintk(1, "An audiosystem/TV can only be combined with record or playback\n");
				return -EINVAL;
			}
		}
	}

	log_addrs->log_addr_mask = adap->log_addrs.log_addr_mask;
	adap->log_addrs = *log_addrs;
	if (adap->phys_addr != CEC_PHYS_ADDR_INVALID)
		cec_claim_log_addrs(adap, block);
	return 0;
}

int cec_s_log_addrs(struct cec_adapter *adap,
		    struct cec_log_addrs *log_addrs, bool block)
{
	int err;

	if (WARN_ON(adap->capabilities & CEC_CAP_LOG_ADDRS))
		return -EINVAL;
	mutex_lock(&adap->lock);
	err = __cec_s_log_addrs(adap, log_addrs, block);
	mutex_unlock(&adap->lock);
	return err;
}
EXPORT_SYMBOL_GPL(cec_s_log_addrs);

/* High-level core CEC message handling */

/* Transmit the Report Features message */
static int cec_report_features(struct cec_adapter *adap, unsigned int la_idx)
{
	struct cec_msg msg = { };
	const struct cec_log_addrs *las = &adap->log_addrs;
	const u8 *features = las->features[la_idx];
	bool op_is_dev_features = false;
	unsigned int idx;

	/* This is 2.0 and up only */
	if (adap->log_addrs.cec_version < CEC_OP_CEC_VERSION_2_0)
		return 0;

	/* Report Features */
	msg.msg[0] = (las->log_addr[la_idx] << 4) | 0x0f;
	msg.len = 4;
	msg.msg[1] = CEC_MSG_REPORT_FEATURES;
	msg.msg[2] = adap->log_addrs.cec_version;
	msg.msg[3] = las->all_device_types[la_idx];

	/* Write RC Profiles first, then Device Features */
	for (idx = 0; idx < ARRAY_SIZE(las->features[0]); idx++) {
		msg.msg[msg.len++] = features[idx];
		if ((features[idx] & CEC_OP_FEAT_EXT) == 0) {
			if (op_is_dev_features)
				break;
			op_is_dev_features = true;
		}
	}
	return cec_transmit_msg(adap, &msg, false);
}

/* Transmit the Report Physical Address message */
static int cec_report_phys_addr(struct cec_adapter *adap, unsigned int la_idx)
{
	const struct cec_log_addrs *las = &adap->log_addrs;
	struct cec_msg msg = { };

	/* Report Physical Address */
	msg.msg[0] = (las->log_addr[la_idx] << 4) | 0x0f;
	cec_msg_report_physical_addr(&msg, adap->phys_addr,
				     las->primary_device_type[la_idx]);
	dprintk(2, "config: la %d pa %x.%x.%x.%x\n",
		las->log_addr[la_idx],
			cec_phys_addr_exp(adap->phys_addr));
	return cec_transmit_msg(adap, &msg, false);
}

/* Transmit the Feature Abort message */
static int cec_feature_abort_reason(struct cec_adapter *adap,
				    struct cec_msg *msg, u8 reason)
{
	struct cec_msg tx_msg = { };

	/*
	 * Don't reply with CEC_MSG_FEATURE_ABORT to a CEC_MSG_FEATURE_ABORT
	 * message!
	 */
	if (msg->msg[1] == CEC_MSG_FEATURE_ABORT)
		return 0;
	cec_msg_set_reply_to(&tx_msg, msg);
	cec_msg_feature_abort(&tx_msg, msg->msg[1], reason);
	return cec_transmit_msg(adap, &tx_msg, false);
}

static int cec_feature_abort(struct cec_adapter *adap, struct cec_msg *msg)
{
	return cec_feature_abort_reason(adap, msg,
					CEC_OP_ABORT_UNRECOGNIZED_OP);
}

static int cec_feature_refused(struct cec_adapter *adap, struct cec_msg *msg)
{
	return cec_feature_abort_reason(adap, msg,
					CEC_OP_ABORT_REFUSED);
}

/*
 * Called when a CEC message is received. This function will do any
 * necessary core processing. The is_reply bool is true if this message
 * is a reply to an earlier transmit.
 *
 * The message is either a broadcast message or a valid directed message.
 */
static int cec_receive_notify(struct cec_adapter *adap, struct cec_msg *msg,
			      bool is_reply)
{
	bool is_broadcast = cec_msg_is_broadcast(msg);
	u8 dest_laddr = cec_msg_destination(msg);
	u8 init_laddr = cec_msg_initiator(msg);
	u8 devtype = cec_log_addr2dev(adap, dest_laddr);
	int la_idx = cec_log_addr2idx(adap, dest_laddr);
	bool is_directed = la_idx >= 0;
	bool from_unregistered = init_laddr == 0xf;
	struct cec_msg tx_cec_msg = { };

	dprintk(1, "cec_receive_notify: %*ph\n", msg->len, msg->msg);

	if (adap->ops->received) {
		/* Allow drivers to process the message first */
		if (adap->ops->received(adap, msg) != -ENOMSG)
			return 0;
	}

	/*
	 * REPORT_PHYSICAL_ADDR, CEC_MSG_USER_CONTROL_PRESSED and
	 * CEC_MSG_USER_CONTROL_RELEASED messages always have to be
	 * handled by the CEC core, even if the passthrough mode is on.
	 * The others are just ignored if passthrough mode is on.
	 */
	switch (msg->msg[1]) {
	case CEC_MSG_GET_CEC_VERSION:
	case CEC_MSG_GIVE_DEVICE_VENDOR_ID:
	case CEC_MSG_ABORT:
	case CEC_MSG_GIVE_DEVICE_POWER_STATUS:
	case CEC_MSG_GIVE_PHYSICAL_ADDR:
	case CEC_MSG_GIVE_OSD_NAME:
	case CEC_MSG_GIVE_FEATURES:
		/*
		 * Skip processing these messages if the passthrough mode
		 * is on.
		 */
		if (adap->passthrough)
			goto skip_processing;
		/* Ignore if addressing is wrong */
		if (is_broadcast || from_unregistered)
			return 0;
		break;

	case CEC_MSG_USER_CONTROL_PRESSED:
	case CEC_MSG_USER_CONTROL_RELEASED:
		/* Wrong addressing mode: don't process */
		if (is_broadcast || from_unregistered)
			goto skip_processing;
		break;

	case CEC_MSG_REPORT_PHYSICAL_ADDR:
		/*
		 * This message is always processed, regardless of the
		 * passthrough setting.
		 *
		 * Exception: don't process if wrong addressing mode.
		 */
		if (!is_broadcast)
			goto skip_processing;
		break;

	default:
		break;
	}

	cec_msg_set_reply_to(&tx_cec_msg, msg);

	switch (msg->msg[1]) {
	/* The following messages are processed but still passed through */
	case CEC_MSG_REPORT_PHYSICAL_ADDR: {
		u16 pa = (msg->msg[2] << 8) | msg->msg[3];

		if (!from_unregistered)
			adap->phys_addrs[init_laddr] = pa;
		dprintk(1, "Reported physical address %x.%x.%x.%x for logical address %d\n",
			cec_phys_addr_exp(pa), init_laddr);
		break;
	}

	case CEC_MSG_USER_CONTROL_PRESSED:
		if (!(adap->capabilities & CEC_CAP_RC))
			break;

#if IS_REACHABLE(CONFIG_RC_CORE)
		switch (msg->msg[2]) {
		/*
		 * Play function, this message can have variable length
		 * depending on the specific play function that is used.
		 */
		case 0x60:
			if (msg->len == 2)
				rc_keydown(adap->rc, RC_TYPE_CEC,
					   msg->msg[2], 0);
			else
				rc_keydown(adap->rc, RC_TYPE_CEC,
					   msg->msg[2] << 8 | msg->msg[3], 0);
			break;
		/*
		 * Other function messages that are not handled.
		 * Currently the RC framework does not allow to supply an
		 * additional parameter to a keypress. These "keys" contain
		 * other information such as channel number, an input number
		 * etc.
		 * For the time being these messages are not processed by the
		 * framework and are simply forwarded to the user space.
		 */
		case 0x56: case 0x57:
		case 0x67: case 0x68: case 0x69: case 0x6a:
			break;
		default:
			rc_keydown(adap->rc, RC_TYPE_CEC, msg->msg[2], 0);
			break;
		}
#endif
		break;

	case CEC_MSG_USER_CONTROL_RELEASED:
		if (!(adap->capabilities & CEC_CAP_RC))
			break;
#if IS_REACHABLE(CONFIG_RC_CORE)
		rc_keyup(adap->rc);
#endif
		break;

	/*
	 * The remaining messages are only processed if the passthrough mode
	 * is off.
	 */
	case CEC_MSG_GET_CEC_VERSION:
		cec_msg_cec_version(&tx_cec_msg, adap->log_addrs.cec_version);
		return cec_transmit_msg(adap, &tx_cec_msg, false);

	case CEC_MSG_GIVE_PHYSICAL_ADDR:
		/* Do nothing for CEC switches using addr 15 */
		if (devtype == CEC_OP_PRIM_DEVTYPE_SWITCH && dest_laddr == 15)
			return 0;
		cec_msg_report_physical_addr(&tx_cec_msg, adap->phys_addr, devtype);
		return cec_transmit_msg(adap, &tx_cec_msg, false);

	case CEC_MSG_GIVE_DEVICE_VENDOR_ID:
		if (adap->log_addrs.vendor_id == CEC_VENDOR_ID_NONE)
			return cec_feature_abort(adap, msg);
		cec_msg_device_vendor_id(&tx_cec_msg, adap->log_addrs.vendor_id);
		return cec_transmit_msg(adap, &tx_cec_msg, false);

	case CEC_MSG_ABORT:
		/* Do nothing for CEC switches */
		if (devtype == CEC_OP_PRIM_DEVTYPE_SWITCH)
			return 0;
		return cec_feature_refused(adap, msg);

	case CEC_MSG_GIVE_OSD_NAME: {
		if (adap->log_addrs.osd_name[0] == 0)
			return cec_feature_abort(adap, msg);
		cec_msg_set_osd_name(&tx_cec_msg, adap->log_addrs.osd_name);
		return cec_transmit_msg(adap, &tx_cec_msg, false);
	}

	case CEC_MSG_GIVE_FEATURES:
		if (adap->log_addrs.cec_version >= CEC_OP_CEC_VERSION_2_0)
			return cec_report_features(adap, la_idx);
		return 0;

	default:
		/*
		 * Unprocessed messages are aborted if userspace isn't doing
		 * any processing either.
		 */
		if (is_directed && !is_reply && !adap->follower_cnt &&
		    !adap->cec_follower && msg->msg[1] != CEC_MSG_FEATURE_ABORT)
			return cec_feature_abort(adap, msg);
		break;
	}

skip_processing:
	/* If this was a reply, then we're done */
	if (is_reply)
		return 0;

	/*
	 * Send to the exclusive follower if there is one, otherwise send
	 * to all followers.
	 */
	if (adap->cec_follower)
		cec_queue_msg_fh(adap->cec_follower, msg);
	else
		cec_queue_msg_followers(adap, msg);
	return 0;
}

/*
 * Helper functions to keep track of the 'monitor all' use count.
 *
 * These functions are called with adap->lock held.
 */
int cec_monitor_all_cnt_inc(struct cec_adapter *adap)
{
	int ret = 0;

	if (adap->monitor_all_cnt == 0)
		ret = call_op(adap, adap_monitor_all_enable, 1);
	if (ret == 0)
		adap->monitor_all_cnt++;
	return ret;
}

void cec_monitor_all_cnt_dec(struct cec_adapter *adap)
{
	adap->monitor_all_cnt--;
	if (adap->monitor_all_cnt == 0)
		WARN_ON(call_op(adap, adap_monitor_all_enable, 0));
}

#ifdef CONFIG_MEDIA_CEC_DEBUG
/*
 * Log the current state of the CEC adapter.
 * Very useful for debugging.
 */
int cec_adap_status(struct seq_file *file, void *priv)
{
	struct cec_adapter *adap = dev_get_drvdata(file->private);
	struct cec_data *data;

	mutex_lock(&adap->lock);
	seq_printf(file, "configured: %d\n", adap->is_configured);
	seq_printf(file, "configuring: %d\n", adap->is_configuring);
	seq_printf(file, "phys_addr: %x.%x.%x.%x\n",
		   cec_phys_addr_exp(adap->phys_addr));
	seq_printf(file, "number of LAs: %d\n", adap->log_addrs.num_log_addrs);
	seq_printf(file, "LA mask: 0x%04x\n", adap->log_addrs.log_addr_mask);
	if (adap->cec_follower)
		seq_printf(file, "has CEC follower%s\n",
			   adap->passthrough ? " (in passthrough mode)" : "");
	if (adap->cec_initiator)
		seq_puts(file, "has CEC initiator\n");
	if (adap->monitor_all_cnt)
		seq_printf(file, "file handles in Monitor All mode: %u\n",
			   adap->monitor_all_cnt);
	data = adap->transmitting;
	if (data)
		seq_printf(file, "transmitting message: %*ph (reply: %02x)\n",
			   data->msg.len, data->msg.msg, data->msg.reply);
	list_for_each_entry(data, &adap->transmit_queue, list) {
		seq_printf(file, "queued tx message: %*ph (reply: %02x)\n",
			   data->msg.len, data->msg.msg, data->msg.reply);
	}
	list_for_each_entry(data, &adap->wait_queue, list) {
		seq_printf(file, "message waiting for reply: %*ph (reply: %02x)\n",
			   data->msg.len, data->msg.msg, data->msg.reply);
	}

	call_void_op(adap, adap_status, file);
	mutex_unlock(&adap->lock);
	return 0;
}
#endif

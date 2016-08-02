/*
 * cec-api.c - HDMI Consumer Electronics Control framework - API
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
#include <linux/uaccess.h>
#include <linux/version.h>

#include "cec-priv.h"

static inline struct cec_devnode *cec_devnode_data(struct file *filp)
{
	struct cec_fh *fh = filp->private_data;

	return &fh->adap->devnode;
}

/* CEC file operations */

static unsigned int cec_poll(struct file *filp,
			     struct poll_table_struct *poll)
{
	struct cec_devnode *devnode = cec_devnode_data(filp);
	struct cec_fh *fh = filp->private_data;
	struct cec_adapter *adap = fh->adap;
	unsigned int res = 0;

	if (!devnode->registered)
		return POLLERR | POLLHUP;
	mutex_lock(&adap->lock);
	if (adap->is_configured &&
	    adap->transmit_queue_sz < CEC_MAX_MSG_TX_QUEUE_SZ)
		res |= POLLOUT | POLLWRNORM;
	if (fh->queued_msgs)
		res |= POLLIN | POLLRDNORM;
	if (fh->pending_events)
		res |= POLLPRI;
	poll_wait(filp, &fh->wait, poll);
	mutex_unlock(&adap->lock);
	return res;
}

static bool cec_is_busy(const struct cec_adapter *adap,
			const struct cec_fh *fh)
{
	bool valid_initiator = adap->cec_initiator && adap->cec_initiator == fh;
	bool valid_follower = adap->cec_follower && adap->cec_follower == fh;

	/*
	 * Exclusive initiators and followers can always access the CEC adapter
	 */
	if (valid_initiator || valid_follower)
		return false;
	/*
	 * All others can only access the CEC adapter if there is no
	 * exclusive initiator and they are in INITIATOR mode.
	 */
	return adap->cec_initiator ||
	       fh->mode_initiator == CEC_MODE_NO_INITIATOR;
}

static long cec_adap_g_caps(struct cec_adapter *adap,
			    struct cec_caps __user *parg)
{
	struct cec_caps caps = {};

	strlcpy(caps.driver, adap->devnode.parent->driver->name,
		sizeof(caps.driver));
	strlcpy(caps.name, adap->name, sizeof(caps.name));
	caps.available_log_addrs = adap->available_log_addrs;
	caps.capabilities = adap->capabilities;
	caps.version = LINUX_VERSION_CODE;
	if (copy_to_user(parg, &caps, sizeof(caps)))
		return -EFAULT;
	return 0;
}

static long cec_adap_g_phys_addr(struct cec_adapter *adap,
				 __u16 __user *parg)
{
	u16 phys_addr;

	mutex_lock(&adap->lock);
	phys_addr = adap->phys_addr;
	mutex_unlock(&adap->lock);
	if (copy_to_user(parg, &phys_addr, sizeof(phys_addr)))
		return -EFAULT;
	return 0;
}

static long cec_adap_s_phys_addr(struct cec_adapter *adap, struct cec_fh *fh,
				 bool block, __u16 __user *parg)
{
	u16 phys_addr;
	long err;

	if (!(adap->capabilities & CEC_CAP_PHYS_ADDR))
		return -ENOTTY;
	if (copy_from_user(&phys_addr, parg, sizeof(phys_addr)))
		return -EFAULT;

	err = cec_phys_addr_validate(phys_addr, NULL, NULL);
	if (err)
		return err;
	mutex_lock(&adap->lock);
	if (cec_is_busy(adap, fh))
		err = -EBUSY;
	else
		__cec_s_phys_addr(adap, phys_addr, block);
	mutex_unlock(&adap->lock);
	return err;
}

static long cec_adap_g_log_addrs(struct cec_adapter *adap,
				 struct cec_log_addrs __user *parg)
{
	struct cec_log_addrs log_addrs;

	mutex_lock(&adap->lock);
	log_addrs = adap->log_addrs;
	if (!adap->is_configured)
		memset(log_addrs.log_addr, CEC_LOG_ADDR_INVALID,
		       sizeof(log_addrs.log_addr));
	mutex_unlock(&adap->lock);

	if (copy_to_user(parg, &log_addrs, sizeof(log_addrs)))
		return -EFAULT;
	return 0;
}

static long cec_adap_s_log_addrs(struct cec_adapter *adap, struct cec_fh *fh,
				 bool block, struct cec_log_addrs __user *parg)
{
	struct cec_log_addrs log_addrs;
	long err = -EBUSY;

	if (!(adap->capabilities & CEC_CAP_LOG_ADDRS))
		return -ENOTTY;
	if (copy_from_user(&log_addrs, parg, sizeof(log_addrs)))
		return -EFAULT;
	log_addrs.flags = 0;
	mutex_lock(&adap->lock);
	if (!adap->is_configuring &&
	    (!log_addrs.num_log_addrs || !adap->is_configured) &&
	    !cec_is_busy(adap, fh)) {
		err = __cec_s_log_addrs(adap, &log_addrs, block);
		if (!err)
			log_addrs = adap->log_addrs;
	}
	mutex_unlock(&adap->lock);
	if (err)
		return err;
	if (copy_to_user(parg, &log_addrs, sizeof(log_addrs)))
		return -EFAULT;
	return 0;
}

static long cec_transmit(struct cec_adapter *adap, struct cec_fh *fh,
			 bool block, struct cec_msg __user *parg)
{
	struct cec_msg msg = {};
	long err = 0;

	if (!(adap->capabilities & CEC_CAP_TRANSMIT))
		return -ENOTTY;
	if (copy_from_user(&msg, parg, sizeof(msg)))
		return -EFAULT;
	mutex_lock(&adap->lock);
	if (!adap->is_configured)
		err = -ENONET;
	else if (cec_is_busy(adap, fh))
		err = -EBUSY;
	else
		err = cec_transmit_msg_fh(adap, &msg, fh, block);
	mutex_unlock(&adap->lock);
	if (err)
		return err;
	if (copy_to_user(parg, &msg, sizeof(msg)))
		return -EFAULT;
	return 0;
}

/* Called by CEC_RECEIVE: wait for a message to arrive */
static int cec_receive_msg(struct cec_fh *fh, struct cec_msg *msg, bool block)
{
	u32 timeout = msg->timeout;
	int res;

	do {
		mutex_lock(&fh->lock);
		/* Are there received messages queued up? */
		if (fh->queued_msgs) {
			/* Yes, return the first one */
			struct cec_msg_entry *entry =
				list_first_entry(&fh->msgs,
						 struct cec_msg_entry, list);

			list_del(&entry->list);
			*msg = entry->msg;
			kfree(entry);
			fh->queued_msgs--;
			mutex_unlock(&fh->lock);
			/* restore original timeout value */
			msg->timeout = timeout;
			return 0;
		}

		/* No, return EAGAIN in non-blocking mode or wait */
		mutex_unlock(&fh->lock);

		/* Return when in non-blocking mode */
		if (!block)
			return -EAGAIN;

		if (msg->timeout) {
			/* The user specified a timeout */
			res = wait_event_interruptible_timeout(fh->wait,
							       fh->queued_msgs,
				msecs_to_jiffies(msg->timeout));
			if (res == 0)
				res = -ETIMEDOUT;
			else if (res > 0)
				res = 0;
		} else {
			/* Wait indefinitely */
			res = wait_event_interruptible(fh->wait,
						       fh->queued_msgs);
		}
		/* Exit on error, otherwise loop to get the new message */
	} while (!res);
	return res;
}

static long cec_receive(struct cec_adapter *adap, struct cec_fh *fh,
			bool block, struct cec_msg __user *parg)
{
	struct cec_msg msg = {};
	long err = 0;

	if (copy_from_user(&msg, parg, sizeof(msg)))
		return -EFAULT;
	mutex_lock(&adap->lock);
	if (!adap->is_configured && fh->mode_follower < CEC_MODE_MONITOR)
		err = -ENONET;
	mutex_unlock(&adap->lock);
	if (err)
		return err;

	err = cec_receive_msg(fh, &msg, block);
	if (err)
		return err;
	if (copy_to_user(parg, &msg, sizeof(msg)))
		return -EFAULT;
	return 0;
}

static long cec_dqevent(struct cec_adapter *adap, struct cec_fh *fh,
			bool block, struct cec_event __user *parg)
{
	struct cec_event *ev = NULL;
	u64 ts = ~0ULL;
	unsigned int i;
	long err = 0;

	mutex_lock(&fh->lock);
	while (!fh->pending_events && block) {
		mutex_unlock(&fh->lock);
		err = wait_event_interruptible(fh->wait, fh->pending_events);
		if (err)
			return err;
		mutex_lock(&fh->lock);
	}

	/* Find the oldest event */
	for (i = 0; i < CEC_NUM_EVENTS; i++) {
		if (fh->pending_events & (1 << (i + 1)) &&
		    fh->events[i].ts <= ts) {
			ev = &fh->events[i];
			ts = ev->ts;
		}
	}
	if (!ev) {
		err = -EAGAIN;
		goto unlock;
	}

	if (copy_to_user(parg, ev, sizeof(*ev))) {
		err = -EFAULT;
		goto unlock;
	}

	fh->pending_events &= ~(1 << ev->event);

unlock:
	mutex_unlock(&fh->lock);
	return err;
}

static long cec_g_mode(struct cec_adapter *adap, struct cec_fh *fh,
		       u32 __user *parg)
{
	u32 mode = fh->mode_initiator | fh->mode_follower;

	if (copy_to_user(parg, &mode, sizeof(mode)))
		return -EFAULT;
	return 0;
}

static long cec_s_mode(struct cec_adapter *adap, struct cec_fh *fh,
		       u32 __user *parg)
{
	u32 mode;
	u8 mode_initiator;
	u8 mode_follower;
	long err = 0;

	if (copy_from_user(&mode, parg, sizeof(mode)))
		return -EFAULT;
	if (mode & ~(CEC_MODE_INITIATOR_MSK | CEC_MODE_FOLLOWER_MSK))
		return -EINVAL;

	mode_initiator = mode & CEC_MODE_INITIATOR_MSK;
	mode_follower = mode & CEC_MODE_FOLLOWER_MSK;

	if (mode_initiator > CEC_MODE_EXCL_INITIATOR ||
	    mode_follower > CEC_MODE_MONITOR_ALL)
		return -EINVAL;

	if (mode_follower == CEC_MODE_MONITOR_ALL &&
	    !(adap->capabilities & CEC_CAP_MONITOR_ALL))
		return -EINVAL;

	/* Follower modes should always be able to send CEC messages */
	if ((mode_initiator == CEC_MODE_NO_INITIATOR ||
	     !(adap->capabilities & CEC_CAP_TRANSMIT)) &&
	    mode_follower >= CEC_MODE_FOLLOWER &&
	    mode_follower <= CEC_MODE_EXCL_FOLLOWER_PASSTHRU)
		return -EINVAL;

	/* Monitor modes require CEC_MODE_NO_INITIATOR */
	if (mode_initiator && mode_follower >= CEC_MODE_MONITOR)
		return -EINVAL;

	/* Monitor modes require CAP_NET_ADMIN */
	if (mode_follower >= CEC_MODE_MONITOR && !capable(CAP_NET_ADMIN))
		return -EPERM;

	mutex_lock(&adap->lock);
	/*
	 * You can't become exclusive follower if someone else already
	 * has that job.
	 */
	if ((mode_follower == CEC_MODE_EXCL_FOLLOWER ||
	     mode_follower == CEC_MODE_EXCL_FOLLOWER_PASSTHRU) &&
	    adap->cec_follower && adap->cec_follower != fh)
		err = -EBUSY;
	/*
	 * You can't become exclusive initiator if someone else already
	 * has that job.
	 */
	if (mode_initiator == CEC_MODE_EXCL_INITIATOR &&
	    adap->cec_initiator && adap->cec_initiator != fh)
		err = -EBUSY;

	if (!err) {
		bool old_mon_all = fh->mode_follower == CEC_MODE_MONITOR_ALL;
		bool new_mon_all = mode_follower == CEC_MODE_MONITOR_ALL;

		if (old_mon_all != new_mon_all) {
			if (new_mon_all)
				err = cec_monitor_all_cnt_inc(adap);
			else
				cec_monitor_all_cnt_dec(adap);
		}
	}

	if (err) {
		mutex_unlock(&adap->lock);
		return err;
	}

	if (fh->mode_follower == CEC_MODE_FOLLOWER)
		adap->follower_cnt--;
	if (mode_follower == CEC_MODE_FOLLOWER)
		adap->follower_cnt++;
	if (mode_follower == CEC_MODE_EXCL_FOLLOWER ||
	    mode_follower == CEC_MODE_EXCL_FOLLOWER_PASSTHRU) {
		adap->passthrough =
			mode_follower == CEC_MODE_EXCL_FOLLOWER_PASSTHRU;
		adap->cec_follower = fh;
	} else if (adap->cec_follower == fh) {
		adap->passthrough = false;
		adap->cec_follower = NULL;
	}
	if (mode_initiator == CEC_MODE_EXCL_INITIATOR)
		adap->cec_initiator = fh;
	else if (adap->cec_initiator == fh)
		adap->cec_initiator = NULL;
	fh->mode_initiator = mode_initiator;
	fh->mode_follower = mode_follower;
	mutex_unlock(&adap->lock);
	return 0;
}

static long cec_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct cec_devnode *devnode = cec_devnode_data(filp);
	struct cec_fh *fh = filp->private_data;
	struct cec_adapter *adap = fh->adap;
	bool block = !(filp->f_flags & O_NONBLOCK);
	void __user *parg = (void __user *)arg;

	if (!devnode->registered)
		return -EIO;

	switch (cmd) {
	case CEC_ADAP_G_CAPS:
		return cec_adap_g_caps(adap, parg);

	case CEC_ADAP_G_PHYS_ADDR:
		return cec_adap_g_phys_addr(adap, parg);

	case CEC_ADAP_S_PHYS_ADDR:
		return cec_adap_s_phys_addr(adap, fh, block, parg);

	case CEC_ADAP_G_LOG_ADDRS:
		return cec_adap_g_log_addrs(adap, parg);

	case CEC_ADAP_S_LOG_ADDRS:
		return cec_adap_s_log_addrs(adap, fh, block, parg);

	case CEC_TRANSMIT:
		return cec_transmit(adap, fh, block, parg);

	case CEC_RECEIVE:
		return cec_receive(adap, fh, block, parg);

	case CEC_DQEVENT:
		return cec_dqevent(adap, fh, block, parg);

	case CEC_G_MODE:
		return cec_g_mode(adap, fh, parg);

	case CEC_S_MODE:
		return cec_s_mode(adap, fh, parg);

	default:
		return -ENOTTY;
	}
}

static int cec_open(struct inode *inode, struct file *filp)
{
	struct cec_devnode *devnode =
		container_of(inode->i_cdev, struct cec_devnode, cdev);
	struct cec_adapter *adap = to_cec_adapter(devnode);
	struct cec_fh *fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	/*
	 * Initial events that are automatically sent when the cec device is
	 * opened.
	 */
	struct cec_event ev_state = {
		.event = CEC_EVENT_STATE_CHANGE,
		.flags = CEC_EVENT_FL_INITIAL_STATE,
	};
	int err;

	if (!fh)
		return -ENOMEM;

	INIT_LIST_HEAD(&fh->msgs);
	INIT_LIST_HEAD(&fh->xfer_list);
	mutex_init(&fh->lock);
	init_waitqueue_head(&fh->wait);

	fh->mode_initiator = CEC_MODE_INITIATOR;
	fh->adap = adap;

	err = cec_get_device(devnode);
	if (err) {
		kfree(fh);
		return err;
	}

	filp->private_data = fh;

	mutex_lock(&devnode->lock);
	/* Queue up initial state events */
	ev_state.state_change.phys_addr = adap->phys_addr;
	ev_state.state_change.log_addr_mask = adap->log_addrs.log_addr_mask;
	cec_queue_event_fh(fh, &ev_state, 0);

	list_add(&fh->list, &devnode->fhs);
	mutex_unlock(&devnode->lock);

	return 0;
}

/* Override for the release function */
static int cec_release(struct inode *inode, struct file *filp)
{
	struct cec_devnode *devnode = cec_devnode_data(filp);
	struct cec_adapter *adap = to_cec_adapter(devnode);
	struct cec_fh *fh = filp->private_data;

	mutex_lock(&adap->lock);
	if (adap->cec_initiator == fh)
		adap->cec_initiator = NULL;
	if (adap->cec_follower == fh) {
		adap->cec_follower = NULL;
		adap->passthrough = false;
	}
	if (fh->mode_follower == CEC_MODE_FOLLOWER)
		adap->follower_cnt--;
	if (fh->mode_follower == CEC_MODE_MONITOR_ALL)
		cec_monitor_all_cnt_dec(adap);
	mutex_unlock(&adap->lock);

	mutex_lock(&devnode->lock);
	list_del(&fh->list);
	mutex_unlock(&devnode->lock);

	/* Unhook pending transmits from this filehandle. */
	mutex_lock(&adap->lock);
	while (!list_empty(&fh->xfer_list)) {
		struct cec_data *data =
			list_first_entry(&fh->xfer_list, struct cec_data, xfer_list);

		data->blocking = false;
		data->fh = NULL;
		list_del(&data->xfer_list);
	}
	mutex_unlock(&adap->lock);
	while (!list_empty(&fh->msgs)) {
		struct cec_msg_entry *entry =
			list_first_entry(&fh->msgs, struct cec_msg_entry, list);

		list_del(&entry->list);
		kfree(entry);
	}
	kfree(fh);

	cec_put_device(devnode);
	filp->private_data = NULL;
	return 0;
}

const struct file_operations cec_devnode_fops = {
	.owner = THIS_MODULE,
	.open = cec_open,
	.unlocked_ioctl = cec_ioctl,
	.release = cec_release,
	.poll = cec_poll,
	.llseek = no_llseek,
};

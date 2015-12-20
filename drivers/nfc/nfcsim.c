/*
 * NFC hardware simulation driver
 * Copyright (c) 2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nfc.h>
#include <net/nfc/nfc.h>

#define DEV_ERR(_dev, fmt, args...) nfc_err(&_dev->nfc_dev->dev, \
						"%s: " fmt, __func__, ## args)

#define DEV_DBG(_dev, fmt, args...) dev_dbg(&_dev->nfc_dev->dev, \
						"%s: " fmt, __func__, ## args)

#define NFCSIM_VERSION "0.1"

#define NFCSIM_POLL_NONE	0
#define NFCSIM_POLL_INITIATOR	1
#define NFCSIM_POLL_TARGET	2
#define NFCSIM_POLL_DUAL	(NFCSIM_POLL_INITIATOR | NFCSIM_POLL_TARGET)

#define RX_DEFAULT_DELAY	5

struct nfcsim {
	struct nfc_dev *nfc_dev;

	struct mutex lock;

	struct delayed_work recv_work;

	struct sk_buff *clone_skb;

	struct delayed_work poll_work;
	u8 polling_mode;
	u8 curr_polling_mode;

	u8 shutting_down;

	u8 up;

	u8 initiator;

	u32 rx_delay;

	data_exchange_cb_t cb;
	void *cb_context;

	struct nfcsim *peer_dev;
};

static struct nfcsim *dev0;
static struct nfcsim *dev1;

static struct workqueue_struct *wq;

static void nfcsim_cleanup_dev(struct nfcsim *dev, u8 shutdown)
{
	DEV_DBG(dev, "shutdown=%d\n", shutdown);

	mutex_lock(&dev->lock);

	dev->polling_mode = NFCSIM_POLL_NONE;
	dev->shutting_down = shutdown;
	dev->cb = NULL;
	dev_kfree_skb(dev->clone_skb);
	dev->clone_skb = NULL;

	mutex_unlock(&dev->lock);

	cancel_delayed_work_sync(&dev->poll_work);
	cancel_delayed_work_sync(&dev->recv_work);
}

static int nfcsim_target_found(struct nfcsim *dev)
{
	struct nfc_target nfc_tgt;

	DEV_DBG(dev, "\n");

	memset(&nfc_tgt, 0, sizeof(struct nfc_target));

	nfc_tgt.supported_protocols = NFC_PROTO_NFC_DEP_MASK;
	nfc_targets_found(dev->nfc_dev, &nfc_tgt, 1);

	return 0;
}

static int nfcsim_dev_up(struct nfc_dev *nfc_dev)
{
	struct nfcsim *dev = nfc_get_drvdata(nfc_dev);

	DEV_DBG(dev, "\n");

	mutex_lock(&dev->lock);

	dev->up = 1;

	mutex_unlock(&dev->lock);

	return 0;
}

static int nfcsim_dev_down(struct nfc_dev *nfc_dev)
{
	struct nfcsim *dev = nfc_get_drvdata(nfc_dev);

	DEV_DBG(dev, "\n");

	mutex_lock(&dev->lock);

	dev->up = 0;

	mutex_unlock(&dev->lock);

	return 0;
}

static int nfcsim_dep_link_up(struct nfc_dev *nfc_dev,
			      struct nfc_target *target,
			      u8 comm_mode, u8 *gb, size_t gb_len)
{
	int rc;
	struct nfcsim *dev = nfc_get_drvdata(nfc_dev);
	struct nfcsim *peer = dev->peer_dev;
	u8 *remote_gb;
	size_t remote_gb_len;

	DEV_DBG(dev, "target_idx: %d, comm_mode: %d\n", target->idx, comm_mode);

	mutex_lock(&peer->lock);

	nfc_tm_activated(peer->nfc_dev, NFC_PROTO_NFC_DEP_MASK,
			 NFC_COMM_ACTIVE, gb, gb_len);

	remote_gb = nfc_get_local_general_bytes(peer->nfc_dev, &remote_gb_len);
	if (!remote_gb) {
		DEV_ERR(peer, "Can't get remote general bytes\n");

		mutex_unlock(&peer->lock);
		return -EINVAL;
	}

	mutex_unlock(&peer->lock);

	mutex_lock(&dev->lock);

	rc = nfc_set_remote_general_bytes(nfc_dev, remote_gb, remote_gb_len);
	if (rc) {
		DEV_ERR(dev, "Can't set remote general bytes\n");
		mutex_unlock(&dev->lock);
		return rc;
	}

	rc = nfc_dep_link_is_up(nfc_dev, target->idx, NFC_COMM_ACTIVE,
				NFC_RF_INITIATOR);

	mutex_unlock(&dev->lock);

	return rc;
}

static int nfcsim_dep_link_down(struct nfc_dev *nfc_dev)
{
	struct nfcsim *dev = nfc_get_drvdata(nfc_dev);

	DEV_DBG(dev, "\n");

	nfcsim_cleanup_dev(dev, 0);

	return 0;
}

static int nfcsim_start_poll(struct nfc_dev *nfc_dev,
			     u32 im_protocols, u32 tm_protocols)
{
	struct nfcsim *dev = nfc_get_drvdata(nfc_dev);
	int rc;

	mutex_lock(&dev->lock);

	if (dev->polling_mode != NFCSIM_POLL_NONE) {
		DEV_ERR(dev, "Already in polling mode\n");
		rc = -EBUSY;
		goto exit;
	}

	if (im_protocols & NFC_PROTO_NFC_DEP_MASK)
		dev->polling_mode |= NFCSIM_POLL_INITIATOR;

	if (tm_protocols & NFC_PROTO_NFC_DEP_MASK)
		dev->polling_mode |= NFCSIM_POLL_TARGET;

	if (dev->polling_mode == NFCSIM_POLL_NONE) {
		DEV_ERR(dev, "Unsupported polling mode\n");
		rc = -EINVAL;
		goto exit;
	}

	dev->initiator = 0;
	dev->curr_polling_mode = NFCSIM_POLL_NONE;

	queue_delayed_work(wq, &dev->poll_work, 0);

	DEV_DBG(dev, "Start polling: im: 0x%X, tm: 0x%X\n", im_protocols,
		tm_protocols);

	rc = 0;
exit:
	mutex_unlock(&dev->lock);

	return rc;
}

static void nfcsim_stop_poll(struct nfc_dev *nfc_dev)
{
	struct nfcsim *dev = nfc_get_drvdata(nfc_dev);

	DEV_DBG(dev, "Stop poll\n");

	mutex_lock(&dev->lock);

	dev->polling_mode = NFCSIM_POLL_NONE;

	mutex_unlock(&dev->lock);

	cancel_delayed_work_sync(&dev->poll_work);
}

static int nfcsim_activate_target(struct nfc_dev *nfc_dev,
				  struct nfc_target *target, u32 protocol)
{
	struct nfcsim *dev = nfc_get_drvdata(nfc_dev);

	DEV_DBG(dev, "\n");

	return -ENOTSUPP;
}

static void nfcsim_deactivate_target(struct nfc_dev *nfc_dev,
				     struct nfc_target *target, u8 mode)
{
	struct nfcsim *dev = nfc_get_drvdata(nfc_dev);

	DEV_DBG(dev, "\n");
}

static void nfcsim_wq_recv(struct work_struct *work)
{
	struct nfcsim *dev = container_of(work, struct nfcsim,
					  recv_work.work);

	mutex_lock(&dev->lock);

	if (dev->shutting_down || !dev->up || !dev->clone_skb) {
		dev_kfree_skb(dev->clone_skb);
		goto exit;
	}

	if (dev->initiator) {
		if (!dev->cb) {
			DEV_ERR(dev, "Null recv callback\n");
			dev_kfree_skb(dev->clone_skb);
			goto exit;
		}

		dev->cb(dev->cb_context, dev->clone_skb, 0);
		dev->cb = NULL;
	} else {
		nfc_tm_data_received(dev->nfc_dev, dev->clone_skb);
	}

exit:
	dev->clone_skb = NULL;

	mutex_unlock(&dev->lock);
}

static int nfcsim_tx(struct nfc_dev *nfc_dev, struct nfc_target *target,
		     struct sk_buff *skb, data_exchange_cb_t cb,
		     void *cb_context)
{
	struct nfcsim *dev = nfc_get_drvdata(nfc_dev);
	struct nfcsim *peer = dev->peer_dev;
	int err;

	mutex_lock(&dev->lock);

	if (dev->shutting_down || !dev->up) {
		mutex_unlock(&dev->lock);
		err = -ENODEV;
		goto exit;
	}

	dev->cb = cb;
	dev->cb_context = cb_context;

	mutex_unlock(&dev->lock);

	mutex_lock(&peer->lock);

	peer->clone_skb = skb_clone(skb, GFP_KERNEL);

	if (!peer->clone_skb) {
		DEV_ERR(dev, "skb_clone failed\n");
		mutex_unlock(&peer->lock);
		err = -ENOMEM;
		goto exit;
	}

	/* This simulates an arbitrary transmission delay between the 2 devices.
	 * If packet transmission occurs immediately between them, we have a
	 * non-stop flow of several tens of thousands SYMM packets per second
	 * and a burning cpu.
	 */
	queue_delayed_work(wq, &peer->recv_work,
			msecs_to_jiffies(dev->rx_delay));

	mutex_unlock(&peer->lock);

	err = 0;
exit:
	dev_kfree_skb(skb);

	return err;
}

static int nfcsim_im_transceive(struct nfc_dev *nfc_dev,
				struct nfc_target *target, struct sk_buff *skb,
				data_exchange_cb_t cb, void *cb_context)
{
	return nfcsim_tx(nfc_dev, target, skb, cb, cb_context);
}

static int nfcsim_tm_send(struct nfc_dev *nfc_dev, struct sk_buff *skb)
{
	return nfcsim_tx(nfc_dev, NULL, skb, NULL, NULL);
}

static struct nfc_ops nfcsim_nfc_ops = {
	.dev_up = nfcsim_dev_up,
	.dev_down = nfcsim_dev_down,
	.dep_link_up = nfcsim_dep_link_up,
	.dep_link_down = nfcsim_dep_link_down,
	.start_poll = nfcsim_start_poll,
	.stop_poll = nfcsim_stop_poll,
	.activate_target = nfcsim_activate_target,
	.deactivate_target = nfcsim_deactivate_target,
	.im_transceive = nfcsim_im_transceive,
	.tm_send = nfcsim_tm_send,
};

static void nfcsim_set_polling_mode(struct nfcsim *dev)
{
	if (dev->polling_mode == NFCSIM_POLL_NONE) {
		dev->curr_polling_mode = NFCSIM_POLL_NONE;
		return;
	}

	if (dev->curr_polling_mode == NFCSIM_POLL_NONE) {
		if (dev->polling_mode & NFCSIM_POLL_INITIATOR)
			dev->curr_polling_mode = NFCSIM_POLL_INITIATOR;
		else
			dev->curr_polling_mode = NFCSIM_POLL_TARGET;

		return;
	}

	if (dev->polling_mode == NFCSIM_POLL_DUAL) {
		if (dev->curr_polling_mode == NFCSIM_POLL_TARGET)
			dev->curr_polling_mode = NFCSIM_POLL_INITIATOR;
		else
			dev->curr_polling_mode = NFCSIM_POLL_TARGET;
	}
}

static void nfcsim_wq_poll(struct work_struct *work)
{
	struct nfcsim *dev = container_of(work, struct nfcsim, poll_work.work);
	struct nfcsim *peer = dev->peer_dev;

	/* These work items run on an ordered workqueue and are therefore
	 * serialized. So we can take both mutexes without being dead locked.
	 */
	mutex_lock(&dev->lock);
	mutex_lock(&peer->lock);

	nfcsim_set_polling_mode(dev);

	if (dev->curr_polling_mode == NFCSIM_POLL_NONE) {
		DEV_DBG(dev, "Not polling\n");
		goto unlock;
	}

	DEV_DBG(dev, "Polling as %s",
		dev->curr_polling_mode == NFCSIM_POLL_INITIATOR ?
		"initiator\n" : "target\n");

	if (dev->curr_polling_mode == NFCSIM_POLL_TARGET)
		goto sched_work;

	if (peer->curr_polling_mode == NFCSIM_POLL_TARGET) {
		peer->polling_mode = NFCSIM_POLL_NONE;
		dev->polling_mode = NFCSIM_POLL_NONE;

		dev->initiator = 1;

		nfcsim_target_found(dev);

		goto unlock;
	}

sched_work:
	/* This defines the delay for an initiator to check if the other device
	 * is polling in target mode.
	 * If the device starts in dual mode polling, it switches between
	 * initiator and target at every round.
	 * Because the wq is ordered and only 1 work item is executed at a time,
	 * we'll always have one device polling as initiator and the other as
	 * target at some point, even if both are started in dual mode.
	 */
	queue_delayed_work(wq, &dev->poll_work, msecs_to_jiffies(200));

unlock:
	mutex_unlock(&peer->lock);
	mutex_unlock(&dev->lock);
}

static struct nfcsim *nfcsim_init_dev(void)
{
	struct nfcsim *dev;
	int rc = -ENOMEM;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL)
		return ERR_PTR(-ENOMEM);

	mutex_init(&dev->lock);

	INIT_DELAYED_WORK(&dev->recv_work, nfcsim_wq_recv);
	INIT_DELAYED_WORK(&dev->poll_work, nfcsim_wq_poll);

	dev->nfc_dev = nfc_allocate_device(&nfcsim_nfc_ops,
					   NFC_PROTO_NFC_DEP_MASK,
					   0, 0);
	if (!dev->nfc_dev)
		goto error;

	nfc_set_drvdata(dev->nfc_dev, dev);

	rc = nfc_register_device(dev->nfc_dev);
	if (rc)
		goto free_nfc_dev;

	dev->rx_delay = RX_DEFAULT_DELAY;
	return dev;

free_nfc_dev:
	nfc_free_device(dev->nfc_dev);

error:
	kfree(dev);

	return ERR_PTR(rc);
}

static void nfcsim_free_device(struct nfcsim *dev)
{
	nfc_unregister_device(dev->nfc_dev);

	nfc_free_device(dev->nfc_dev);

	kfree(dev);
}

static int __init nfcsim_init(void)
{
	int rc;

	/* We need an ordered wq to ensure that poll_work items are executed
	 * one at a time.
	 */
	wq = alloc_ordered_workqueue("nfcsim", 0);
	if (!wq) {
		rc = -ENOMEM;
		goto exit;
	}

	dev0 = nfcsim_init_dev();
	if (IS_ERR(dev0)) {
		rc = PTR_ERR(dev0);
		goto exit;
	}

	dev1 = nfcsim_init_dev();
	if (IS_ERR(dev1)) {
		kfree(dev0);

		rc = PTR_ERR(dev1);
		goto exit;
	}

	dev0->peer_dev = dev1;
	dev1->peer_dev = dev0;

	pr_debug("NFCsim " NFCSIM_VERSION " initialized\n");

	rc = 0;
exit:
	if (rc)
		pr_err("Failed to initialize nfcsim driver (%d)\n",
		       rc);

	return rc;
}

static void __exit nfcsim_exit(void)
{
	nfcsim_cleanup_dev(dev0, 1);
	nfcsim_cleanup_dev(dev1, 1);

	nfcsim_free_device(dev0);
	nfcsim_free_device(dev1);

	destroy_workqueue(wq);
}

module_init(nfcsim_init);
module_exit(nfcsim_exit);

MODULE_DESCRIPTION("NFCSim driver ver " NFCSIM_VERSION);
MODULE_VERSION(NFCSIM_VERSION);
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	cn_queue.c
 *
 * 2004+ Copyright (c) Evgeniy Polyakov <zbr@ioremap.net>
 * All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/suspend.h>
#include <linux/connector.h>
#include <linux/delay.h>

static struct cn_callback_entry *
cn_queue_alloc_callback_entry(struct cn_queue_dev *dev, const char *name,
			      struct cb_id *id,
			      void (*callback)(struct cn_msg *,
					       struct netlink_skb_parms *))
{
	struct cn_callback_entry *cbq;

	cbq = kzalloc(sizeof(*cbq), GFP_KERNEL);
	if (!cbq) {
		pr_err("Failed to create new callback queue.\n");
		return NULL;
	}

	refcount_set(&cbq->refcnt, 1);

	atomic_inc(&dev->refcnt);
	cbq->pdev = dev;

	snprintf(cbq->id.name, sizeof(cbq->id.name), "%s", name);
	memcpy(&cbq->id.id, id, sizeof(struct cb_id));
	cbq->callback = callback;
	return cbq;
}

void cn_queue_release_callback(struct cn_callback_entry *cbq)
{
	if (!refcount_dec_and_test(&cbq->refcnt))
		return;

	atomic_dec(&cbq->pdev->refcnt);
	kfree(cbq);
}

int cn_cb_equal(struct cb_id *i1, struct cb_id *i2)
{
	return ((i1->idx == i2->idx) && (i1->val == i2->val));
}

int cn_queue_add_callback(struct cn_queue_dev *dev, const char *name,
			  struct cb_id *id,
			  void (*callback)(struct cn_msg *,
					   struct netlink_skb_parms *))
{
	struct cn_callback_entry *cbq, *__cbq;
	int found = 0;

	cbq = cn_queue_alloc_callback_entry(dev, name, id, callback);
	if (!cbq)
		return -ENOMEM;

	spin_lock_bh(&dev->queue_lock);
	list_for_each_entry(__cbq, &dev->queue_list, callback_entry) {
		if (cn_cb_equal(&__cbq->id.id, id)) {
			found = 1;
			break;
		}
	}
	if (!found)
		list_add_tail(&cbq->callback_entry, &dev->queue_list);
	spin_unlock_bh(&dev->queue_lock);

	if (found) {
		cn_queue_release_callback(cbq);
		return -EINVAL;
	}

	cbq->seq = 0;
	cbq->group = cbq->id.id.idx;

	return 0;
}

void cn_queue_del_callback(struct cn_queue_dev *dev, struct cb_id *id)
{
	struct cn_callback_entry *cbq, *n;
	int found = 0;

	spin_lock_bh(&dev->queue_lock);
	list_for_each_entry_safe(cbq, n, &dev->queue_list, callback_entry) {
		if (cn_cb_equal(&cbq->id.id, id)) {
			list_del(&cbq->callback_entry);
			found = 1;
			break;
		}
	}
	spin_unlock_bh(&dev->queue_lock);

	if (found)
		cn_queue_release_callback(cbq);
}

struct cn_queue_dev *cn_queue_alloc_dev(const char *name, struct sock *nls)
{
	struct cn_queue_dev *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	snprintf(dev->name, sizeof(dev->name), "%s", name);
	atomic_set(&dev->refcnt, 0);
	INIT_LIST_HEAD(&dev->queue_list);
	spin_lock_init(&dev->queue_lock);

	dev->nls = nls;

	return dev;
}

void cn_queue_free_dev(struct cn_queue_dev *dev)
{
	struct cn_callback_entry *cbq, *n;

	spin_lock_bh(&dev->queue_lock);
	list_for_each_entry_safe(cbq, n, &dev->queue_list, callback_entry)
		list_del(&cbq->callback_entry);
	spin_unlock_bh(&dev->queue_lock);

	while (atomic_read(&dev->refcnt)) {
		pr_info("Waiting for %s to become free: refcnt=%d.\n",
		       dev->name, atomic_read(&dev->refcnt));
		msleep(1000);
	}

	kfree(dev);
	dev = NULL;
}

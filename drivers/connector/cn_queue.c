/*
 * 	cn_queue.c
 *
 * 2004+ Copyright (c) Evgeniy Polyakov <zbr@ioremap.net>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
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


/*
 * This job is sent to the kevent workqueue.
 * While no event is once sent to any callback, the connector workqueue
 * is not created to avoid a useless waiting kernel task.
 * Once the first event is received, we create this dedicated workqueue which
 * is necessary because the flow of data can be high and we don't want
 * to encumber keventd with that.
 */
static void cn_queue_create(struct work_struct *work)
{
	struct cn_queue_dev *dev;

	dev = container_of(work, struct cn_queue_dev, wq_creation);

	dev->cn_queue = create_singlethread_workqueue(dev->name);
	/* If we fail, we will use keventd for all following connector jobs */
	WARN_ON(!dev->cn_queue);
}

/*
 * Queue a data sent to a callback.
 * If the connector workqueue is already created, we queue the job on it.
 * Otherwise, we queue the job to kevent and queue the connector workqueue
 * creation too.
 */
int queue_cn_work(struct cn_callback_entry *cbq, struct work_struct *work)
{
	struct cn_queue_dev *pdev = cbq->pdev;

	if (likely(pdev->cn_queue))
		return queue_work(pdev->cn_queue, work);

	/* Don't create the connector workqueue twice */
	if (atomic_inc_return(&pdev->wq_requested) == 1)
		schedule_work(&pdev->wq_creation);
	else
		atomic_dec(&pdev->wq_requested);

	return schedule_work(work);
}

void cn_queue_wrapper(struct work_struct *work)
{
	struct cn_callback_entry *cbq =
		container_of(work, struct cn_callback_entry, work);
	struct cn_callback_data *d = &cbq->data;
	struct cn_msg *msg = NLMSG_DATA(nlmsg_hdr(d->skb));
	struct netlink_skb_parms *nsp = &NETLINK_CB(d->skb);

	d->callback(msg, nsp);

	kfree_skb(d->skb);
	d->skb = NULL;

	kfree(d->free);
}

static struct cn_callback_entry *
cn_queue_alloc_callback_entry(char *name, struct cb_id *id,
			      void (*callback)(struct cn_msg *, struct netlink_skb_parms *))
{
	struct cn_callback_entry *cbq;

	cbq = kzalloc(sizeof(*cbq), GFP_KERNEL);
	if (!cbq) {
		printk(KERN_ERR "Failed to create new callback queue.\n");
		return NULL;
	}

	snprintf(cbq->id.name, sizeof(cbq->id.name), "%s", name);
	memcpy(&cbq->id.id, id, sizeof(struct cb_id));
	cbq->data.callback = callback;

	INIT_WORK(&cbq->work, &cn_queue_wrapper);
	return cbq;
}

static void cn_queue_free_callback(struct cn_callback_entry *cbq)
{
	/* The first jobs have been sent to kevent, flush them too */
	flush_scheduled_work();
	if (cbq->pdev->cn_queue)
		flush_workqueue(cbq->pdev->cn_queue);

	kfree(cbq);
}

int cn_cb_equal(struct cb_id *i1, struct cb_id *i2)
{
	return ((i1->idx == i2->idx) && (i1->val == i2->val));
}

int cn_queue_add_callback(struct cn_queue_dev *dev, char *name, struct cb_id *id,
			  void (*callback)(struct cn_msg *, struct netlink_skb_parms *))
{
	struct cn_callback_entry *cbq, *__cbq;
	int found = 0;

	cbq = cn_queue_alloc_callback_entry(name, id, callback);
	if (!cbq)
		return -ENOMEM;

	atomic_inc(&dev->refcnt);
	cbq->pdev = dev;

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
		cn_queue_free_callback(cbq);
		atomic_dec(&dev->refcnt);
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

	if (found) {
		cn_queue_free_callback(cbq);
		atomic_dec(&dev->refcnt);
	}
}

struct cn_queue_dev *cn_queue_alloc_dev(char *name, struct sock *nls)
{
	struct cn_queue_dev *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	snprintf(dev->name, sizeof(dev->name), "%s", name);
	atomic_set(&dev->refcnt, 0);
	INIT_LIST_HEAD(&dev->queue_list);
	spin_lock_init(&dev->queue_lock);
	init_waitqueue_head(&dev->wq_created);

	dev->nls = nls;

	INIT_WORK(&dev->wq_creation, cn_queue_create);

	return dev;
}

void cn_queue_free_dev(struct cn_queue_dev *dev)
{
	struct cn_callback_entry *cbq, *n;
	long timeout;
	DEFINE_WAIT(wait);

	/* Flush the first pending jobs queued on kevent */
	flush_scheduled_work();

	/* If the connector workqueue creation is still pending, wait for it */
	prepare_to_wait(&dev->wq_created, &wait, TASK_UNINTERRUPTIBLE);
	if (atomic_read(&dev->wq_requested) && !dev->cn_queue) {
		timeout = schedule_timeout(HZ * 2);
		if (!timeout && !dev->cn_queue)
			WARN_ON(1);
	}
	finish_wait(&dev->wq_created, &wait);

	if (dev->cn_queue) {
		flush_workqueue(dev->cn_queue);
		destroy_workqueue(dev->cn_queue);
	}

	spin_lock_bh(&dev->queue_lock);
	list_for_each_entry_safe(cbq, n, &dev->queue_list, callback_entry)
		list_del(&cbq->callback_entry);
	spin_unlock_bh(&dev->queue_lock);

	while (atomic_read(&dev->refcnt)) {
		printk(KERN_INFO "Waiting for %s to become free: refcnt=%d.\n",
		       dev->name, atomic_read(&dev->refcnt));
		msleep(1000);
	}

	kfree(dev);
	dev = NULL;
}

/*
 * OMAP mailbox driver
 *
 * Copyright (C) 2006-2009 Nokia Corporation. All rights reserved.
 *
 * Contact: Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/delay.h>

#include <plat/mailbox.h>

static struct omap_mbox *mboxes;
static DEFINE_RWLOCK(mboxes_lock);

/* Mailbox FIFO handle functions */
static inline mbox_msg_t mbox_fifo_read(struct omap_mbox *mbox)
{
	return mbox->ops->fifo_read(mbox);
}
static inline void mbox_fifo_write(struct omap_mbox *mbox, mbox_msg_t msg)
{
	mbox->ops->fifo_write(mbox, msg);
}
static inline int mbox_fifo_empty(struct omap_mbox *mbox)
{
	return mbox->ops->fifo_empty(mbox);
}
static inline int mbox_fifo_full(struct omap_mbox *mbox)
{
	return mbox->ops->fifo_full(mbox);
}

/* Mailbox IRQ handle functions */
static inline void ack_mbox_irq(struct omap_mbox *mbox, omap_mbox_irq_t irq)
{
	if (mbox->ops->ack_irq)
		mbox->ops->ack_irq(mbox, irq);
}
static inline int is_mbox_irq(struct omap_mbox *mbox, omap_mbox_irq_t irq)
{
	return mbox->ops->is_irq(mbox, irq);
}

/*
 * message sender
 */
static int __mbox_msg_send(struct omap_mbox *mbox, mbox_msg_t msg)
{
	int ret = 0, i = 1000;

	while (mbox_fifo_full(mbox)) {
		if (mbox->ops->type == OMAP_MBOX_TYPE2)
			return -1;
		if (--i == 0)
			return -1;
		udelay(1);
	}
	mbox_fifo_write(mbox, msg);
	return ret;
}

struct omap_msg_tx_data {
	mbox_msg_t	msg;
};

static void omap_msg_tx_end_io(struct request *rq, int error)
{
	kfree(rq->special);
	__blk_put_request(rq->q, rq);
}

int omap_mbox_msg_send(struct omap_mbox *mbox, mbox_msg_t msg)
{
	struct omap_msg_tx_data *tx_data;
	struct request *rq;
	struct request_queue *q = mbox->txq->queue;

	tx_data = kmalloc(sizeof(*tx_data), GFP_ATOMIC);
	if (unlikely(!tx_data))
		return -ENOMEM;

	rq = blk_get_request(q, WRITE, GFP_ATOMIC);
	if (unlikely(!rq)) {
		kfree(tx_data);
		return -ENOMEM;
	}

	tx_data->msg = msg;
	rq->end_io = omap_msg_tx_end_io;
	blk_insert_request(q, rq, 0, tx_data);

	schedule_work(&mbox->txq->work);
	return 0;
}
EXPORT_SYMBOL(omap_mbox_msg_send);

static void mbox_tx_work(struct work_struct *work)
{
	int ret;
	struct request *rq;
	struct omap_mbox_queue *mq = container_of(work,
				struct omap_mbox_queue, work);
	struct omap_mbox *mbox = mq->queue->queuedata;
	struct request_queue *q = mbox->txq->queue;

	while (1) {
		struct omap_msg_tx_data *tx_data;

		spin_lock(q->queue_lock);
		rq = blk_fetch_request(q);
		spin_unlock(q->queue_lock);

		if (!rq)
			break;

		tx_data = rq->special;

		ret = __mbox_msg_send(mbox, tx_data->msg);
		if (ret) {
			omap_mbox_enable_irq(mbox, IRQ_TX);
			spin_lock(q->queue_lock);
			blk_requeue_request(q, rq);
			spin_unlock(q->queue_lock);
			return;
		}

		spin_lock(q->queue_lock);
		__blk_end_request_all(rq, 0);
		spin_unlock(q->queue_lock);
	}
}

/*
 * Message receiver(workqueue)
 */
static void mbox_rx_work(struct work_struct *work)
{
	struct omap_mbox_queue *mq =
			container_of(work, struct omap_mbox_queue, work);
	struct omap_mbox *mbox = mq->queue->queuedata;
	struct request_queue *q = mbox->rxq->queue;
	struct request *rq;
	mbox_msg_t msg;
	unsigned long flags;

	while (1) {
		spin_lock_irqsave(q->queue_lock, flags);
		rq = blk_fetch_request(q);
		spin_unlock_irqrestore(q->queue_lock, flags);
		if (!rq)
			break;

		msg = (mbox_msg_t)rq->special;
		blk_end_request_all(rq, 0);
		mbox->rxq->callback((void *)msg);
	}
}

/*
 * Mailbox interrupt handler
 */
static void mbox_txq_fn(struct request_queue *q)
{
}

static void mbox_rxq_fn(struct request_queue *q)
{
}

static void __mbox_tx_interrupt(struct omap_mbox *mbox)
{
	omap_mbox_disable_irq(mbox, IRQ_TX);
	ack_mbox_irq(mbox, IRQ_TX);
	schedule_work(&mbox->txq->work);
}

static void __mbox_rx_interrupt(struct omap_mbox *mbox)
{
	struct request *rq;
	mbox_msg_t msg;
	struct request_queue *q = mbox->rxq->queue;

	while (!mbox_fifo_empty(mbox)) {
		rq = blk_get_request(q, WRITE, GFP_ATOMIC);
		if (unlikely(!rq))
			goto nomem;

		msg = mbox_fifo_read(mbox);


		blk_insert_request(q, rq, 0, (void *)msg);
		if (mbox->ops->type == OMAP_MBOX_TYPE1)
			break;
	}

	/* no more messages in the fifo. clear IRQ source. */
	ack_mbox_irq(mbox, IRQ_RX);
nomem:
	schedule_work(&mbox->rxq->work);
}

static irqreturn_t mbox_interrupt(int irq, void *p)
{
	struct omap_mbox *mbox = p;

	if (is_mbox_irq(mbox, IRQ_TX))
		__mbox_tx_interrupt(mbox);

	if (is_mbox_irq(mbox, IRQ_RX))
		__mbox_rx_interrupt(mbox);

	return IRQ_HANDLED;
}

static struct omap_mbox_queue *mbox_queue_alloc(struct omap_mbox *mbox,
					request_fn_proc *proc,
					void (*work) (struct work_struct *))
{
	struct request_queue *q;
	struct omap_mbox_queue *mq;

	mq = kzalloc(sizeof(struct omap_mbox_queue), GFP_KERNEL);
	if (!mq)
		return NULL;

	spin_lock_init(&mq->lock);

	q = blk_init_queue(proc, &mq->lock);
	if (!q)
		goto error;
	q->queuedata = mbox;
	mq->queue = q;

	INIT_WORK(&mq->work, work);

	return mq;
error:
	kfree(mq);
	return NULL;
}

static void mbox_queue_free(struct omap_mbox_queue *q)
{
	blk_cleanup_queue(q->queue);
	kfree(q);
}

static int omap_mbox_startup(struct omap_mbox *mbox)
{
	int ret;
	struct omap_mbox_queue *mq;

	if (likely(mbox->ops->startup)) {
		ret = mbox->ops->startup(mbox);
		if (unlikely(ret))
			return ret;
	}

	ret = request_irq(mbox->irq, mbox_interrupt, IRQF_DISABLED,
				mbox->name, mbox);
	if (unlikely(ret)) {
		printk(KERN_ERR
			"failed to register mailbox interrupt:%d\n", ret);
		goto fail_request_irq;
	}

	mq = mbox_queue_alloc(mbox, mbox_txq_fn, mbox_tx_work);
	if (!mq) {
		ret = -ENOMEM;
		goto fail_alloc_txq;
	}
	mbox->txq = mq;

	mq = mbox_queue_alloc(mbox, mbox_rxq_fn, mbox_rx_work);
	if (!mq) {
		ret = -ENOMEM;
		goto fail_alloc_rxq;
	}
	mbox->rxq = mq;

	return 0;

 fail_alloc_rxq:
	mbox_queue_free(mbox->txq);
 fail_alloc_txq:
	free_irq(mbox->irq, mbox);
 fail_request_irq:
	if (unlikely(mbox->ops->shutdown))
		mbox->ops->shutdown(mbox);

	return ret;
}

static void omap_mbox_fini(struct omap_mbox *mbox)
{
	mbox_queue_free(mbox->txq);
	mbox_queue_free(mbox->rxq);

	free_irq(mbox->irq, mbox);

	if (unlikely(mbox->ops->shutdown))
		mbox->ops->shutdown(mbox);
}

static struct omap_mbox **find_mboxes(const char *name)
{
	struct omap_mbox **p;

	for (p = &mboxes; *p; p = &(*p)->next) {
		if (strcmp((*p)->name, name) == 0)
			break;
	}

	return p;
}

struct omap_mbox *omap_mbox_get(const char *name)
{
	struct omap_mbox *mbox;
	int ret;

	read_lock(&mboxes_lock);
	mbox = *(find_mboxes(name));
	if (mbox == NULL) {
		read_unlock(&mboxes_lock);
		return ERR_PTR(-ENOENT);
	}

	read_unlock(&mboxes_lock);

	ret = omap_mbox_startup(mbox);
	if (ret)
		return ERR_PTR(-ENODEV);

	return mbox;
}
EXPORT_SYMBOL(omap_mbox_get);

void omap_mbox_put(struct omap_mbox *mbox)
{
	omap_mbox_fini(mbox);
}
EXPORT_SYMBOL(omap_mbox_put);

int omap_mbox_register(struct device *parent, struct omap_mbox *mbox)
{
	int ret = 0;
	struct omap_mbox **tmp;

	if (!mbox)
		return -EINVAL;
	if (mbox->next)
		return -EBUSY;

	write_lock(&mboxes_lock);
	tmp = find_mboxes(mbox->name);
	if (*tmp) {
		ret = -EBUSY;
		write_unlock(&mboxes_lock);
		goto err_find;
	}
	*tmp = mbox;
	write_unlock(&mboxes_lock);

	return 0;

err_find:
	return ret;
}
EXPORT_SYMBOL(omap_mbox_register);

int omap_mbox_unregister(struct omap_mbox *mbox)
{
	struct omap_mbox **tmp;

	write_lock(&mboxes_lock);
	tmp = &mboxes;
	while (*tmp) {
		if (mbox == *tmp) {
			*tmp = mbox->next;
			mbox->next = NULL;
			write_unlock(&mboxes_lock);
			return 0;
		}
		tmp = &(*tmp)->next;
	}
	write_unlock(&mboxes_lock);

	return -EINVAL;
}
EXPORT_SYMBOL(omap_mbox_unregister);

static int __init omap_mbox_init(void)
{
	return 0;
}
module_init(omap_mbox_init);

static void __exit omap_mbox_exit(void)
{
}
module_exit(omap_mbox_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("omap mailbox: interrupt driven messaging");
MODULE_AUTHOR("Toshihiro Kobayashi and Hiroshi DOYU");

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

#include <mach/mailbox.h>

static int enable_seq_bit;
module_param(enable_seq_bit, bool, 0);
MODULE_PARM_DESC(enable_seq_bit, "Enable sequence bit checking.");

static struct omap_mbox *mboxes;
static DEFINE_RWLOCK(mboxes_lock);

/*
 * Mailbox sequence bit API
 */

/* seq_rcv should be initialized with any value other than
 * 0 and 1 << 31, to allow either value for the first
 * message.  */
static inline void mbox_seq_init(struct omap_mbox *mbox)
{
	if (!enable_seq_bit)
		return;

	/* any value other than 0 and 1 << 31 */
	mbox->seq_rcv = 0xffffffff;
}

static inline void mbox_seq_toggle(struct omap_mbox *mbox, mbox_msg_t * msg)
{
	if (!enable_seq_bit)
		return;

	/* add seq_snd to msg */
	*msg = (*msg & 0x7fffffff) | mbox->seq_snd;
	/* flip seq_snd */
	mbox->seq_snd ^= 1 << 31;
}

static inline int mbox_seq_test(struct omap_mbox *mbox, mbox_msg_t msg)
{
	mbox_msg_t seq;

	if (!enable_seq_bit)
		return 0;

	seq = msg & (1 << 31);
	if (seq == mbox->seq_rcv)
		return -1;
	mbox->seq_rcv = seq;
	return 0;
}

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
static inline void enable_mbox_irq(struct omap_mbox *mbox, omap_mbox_irq_t irq)
{
	mbox->ops->enable_irq(mbox, irq);
}
static inline void disable_mbox_irq(struct omap_mbox *mbox, omap_mbox_irq_t irq)
{
	mbox->ops->disable_irq(mbox, irq);
}
static inline void ack_mbox_irq(struct omap_mbox *mbox, omap_mbox_irq_t irq)
{
	if (mbox->ops->ack_irq)
		mbox->ops->ack_irq(mbox, irq);
}
static inline int is_mbox_irq(struct omap_mbox *mbox, omap_mbox_irq_t irq)
{
	return mbox->ops->is_irq(mbox, irq);
}

/* Mailbox Sequence Bit function */
void omap_mbox_init_seq(struct omap_mbox *mbox)
{
	mbox_seq_init(mbox);
}
EXPORT_SYMBOL(omap_mbox_init_seq);

/*
 * message sender
 */
static int __mbox_msg_send(struct omap_mbox *mbox, mbox_msg_t msg, void *arg)
{
	int ret = 0, i = 1000;

	while (mbox_fifo_full(mbox)) {
		if (mbox->ops->type == OMAP_MBOX_TYPE2)
			return -1;
		if (--i == 0)
			return -1;
		udelay(1);
	}

	if (arg && mbox->txq->callback) {
		ret = mbox->txq->callback(arg);
		if (ret)
			goto out;
	}

	mbox_seq_toggle(mbox, &msg);
	mbox_fifo_write(mbox, msg);
 out:
	return ret;
}

int omap_mbox_msg_send(struct omap_mbox *mbox, mbox_msg_t msg, void* arg)
{
	struct request *rq;
	struct request_queue *q = mbox->txq->queue;
	int ret = 0;

	rq = blk_get_request(q, WRITE, GFP_ATOMIC);
	if (unlikely(!rq)) {
		ret = -ENOMEM;
		goto fail;
	}

	rq->data = (void *)msg;
	blk_insert_request(q, rq, 0, arg);

	schedule_work(&mbox->txq->work);
 fail:
	return ret;
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
		spin_lock(q->queue_lock);
		rq = elv_next_request(q);
		spin_unlock(q->queue_lock);

		if (!rq)
			break;

		ret = __mbox_msg_send(mbox, (mbox_msg_t) rq->data, rq->special);
		if (ret) {
			enable_mbox_irq(mbox, IRQ_TX);
			return;
		}

		spin_lock(q->queue_lock);
		if (__blk_end_request(rq, 0, 0))
			BUG();
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

	if (mbox->rxq->callback == NULL) {
		sysfs_notify(&mbox->dev->kobj, NULL, "mbox");
		return;
	}

	while (1) {
		spin_lock_irqsave(q->queue_lock, flags);
		rq = elv_next_request(q);
		spin_unlock_irqrestore(q->queue_lock, flags);
		if (!rq)
			break;

		msg = (mbox_msg_t) rq->data;

		if (blk_end_request(rq, 0, 0))
			BUG();

		mbox->rxq->callback((void *)msg);
	}
}

/*
 * Mailbox interrupt handler
 */
static void mbox_txq_fn(struct request_queue * q)
{
}

static void mbox_rxq_fn(struct request_queue * q)
{
}

static void __mbox_tx_interrupt(struct omap_mbox *mbox)
{
	disable_mbox_irq(mbox, IRQ_TX);
	ack_mbox_irq(mbox, IRQ_TX);
	schedule_work(&mbox->txq->work);
}

static void __mbox_rx_interrupt(struct omap_mbox *mbox)
{
	struct request *rq;
	mbox_msg_t msg;
	struct request_queue *q = mbox->rxq->queue;

	disable_mbox_irq(mbox, IRQ_RX);

	while (!mbox_fifo_empty(mbox)) {
		rq = blk_get_request(q, WRITE, GFP_ATOMIC);
		if (unlikely(!rq))
			goto nomem;

		msg = mbox_fifo_read(mbox);
		rq->data = (void *)msg;

		if (unlikely(mbox_seq_test(mbox, msg))) {
			pr_info("mbox: Illegal seq bit!(%08x)\n", msg);
			if (mbox->err_notify)
				mbox->err_notify();
		}

		blk_insert_request(q, rq, 0, NULL);
		if (mbox->ops->type == OMAP_MBOX_TYPE1)
			break;
	}

	/* no more messages in the fifo. clear IRQ source. */
	ack_mbox_irq(mbox, IRQ_RX);
	enable_mbox_irq(mbox, IRQ_RX);
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

/*
 * sysfs files
 */
static ssize_t
omap_mbox_write(struct device *dev, struct device_attribute *attr,
		const char * buf, size_t count)
{
	int ret;
	mbox_msg_t *p = (mbox_msg_t *)buf;
	struct omap_mbox *mbox = dev_get_drvdata(dev);

	for (; count >= sizeof(mbox_msg_t); count -= sizeof(mbox_msg_t)) {
		ret = omap_mbox_msg_send(mbox, be32_to_cpu(*p), NULL);
		if (ret)
			return -EAGAIN;
		p++;
	}

	return (size_t)((char *)p - buf);
}

static ssize_t
omap_mbox_read(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned long flags;
	struct request *rq;
	mbox_msg_t *p = (mbox_msg_t *) buf;
	struct omap_mbox *mbox = dev_get_drvdata(dev);
	struct request_queue *q = mbox->rxq->queue;

	while (1) {
		spin_lock_irqsave(q->queue_lock, flags);
		rq = elv_next_request(q);
		spin_unlock_irqrestore(q->queue_lock, flags);

		if (!rq)
			break;

		*p = (mbox_msg_t) rq->data;

		if (blk_end_request(rq, 0, 0))
			BUG();

		if (unlikely(mbox_seq_test(mbox, *p))) {
			pr_info("mbox: Illegal seq bit!(%08x) ignored\n", *p);
			continue;
		}
		p++;
	}

	pr_debug("%02x %02x %02x %02x\n", buf[0], buf[1], buf[2], buf[3]);

	return (size_t) ((char *)p - buf);
}

static DEVICE_ATTR(mbox, S_IRUGO | S_IWUSR, omap_mbox_read, omap_mbox_write);

static ssize_t mbox_show(struct class *class, char *buf)
{
	return sprintf(buf, "mbox");
}

static CLASS_ATTR(mbox, S_IRUGO, mbox_show, NULL);

static struct class omap_mbox_class = {
	.name = "omap-mailbox",
};

static struct omap_mbox_queue *mbox_queue_alloc(struct omap_mbox *mbox,
					request_fn_proc * proc,
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

static int omap_mbox_init(struct omap_mbox *mbox)
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

	ret = omap_mbox_init(mbox);
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

	mbox->dev = device_create(&omap_mbox_class,
				  parent, 0, mbox, "%s", mbox->name);
	if (IS_ERR(mbox->dev))
		return PTR_ERR(mbox->dev);

	ret = device_create_file(mbox->dev, &dev_attr_mbox);
	if (ret)
		goto err_sysfs;

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
	device_remove_file(mbox->dev, &dev_attr_mbox);
err_sysfs:
	device_unregister(mbox->dev);
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
			device_remove_file(mbox->dev, &dev_attr_mbox);
			device_unregister(mbox->dev);
			return 0;
		}
		tmp = &(*tmp)->next;
	}
	write_unlock(&mboxes_lock);

	return -EINVAL;
}
EXPORT_SYMBOL(omap_mbox_unregister);

static int __init omap_mbox_class_init(void)
{
	int ret = class_register(&omap_mbox_class);
	if (!ret)
		ret = class_create_file(&omap_mbox_class, &class_attr_mbox);

	return ret;
}

static void __exit omap_mbox_class_exit(void)
{
	class_remove_file(&omap_mbox_class, &class_attr_mbox);
	class_unregister(&omap_mbox_class);
}

subsys_initcall(omap_mbox_class_init);
module_exit(omap_mbox_class_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("omap mailbox: interrupt driven messaging");
MODULE_AUTHOR("Toshihiro Kobayashi and Hiroshi DOYU");

/*
 * OMAP mailbox driver
 *
 * Copyright (C) 2006-2009 Nokia Corporation. All rights reserved.
 * Copyright (C) 2013-2014 Texas Instruments Inc.
 *
 * Contact: Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 *          Suman Anna <s-anna@ti.com>
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

#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/kfifo.h>
#include <linux/err.h>
#include <linux/notifier.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/platform_data/mailbox-omap.h>
#include <linux/omap-mailbox.h>

#define MAILBOX_REVISION		0x000
#define MAILBOX_MESSAGE(m)		(0x040 + 4 * (m))
#define MAILBOX_FIFOSTATUS(m)		(0x080 + 4 * (m))
#define MAILBOX_MSGSTATUS(m)		(0x0c0 + 4 * (m))

#define OMAP2_MAILBOX_IRQSTATUS(u)	(0x100 + 8 * (u))
#define OMAP2_MAILBOX_IRQENABLE(u)	(0x104 + 8 * (u))

#define OMAP4_MAILBOX_IRQSTATUS(u)	(0x104 + 0x10 * (u))
#define OMAP4_MAILBOX_IRQENABLE(u)	(0x108 + 0x10 * (u))
#define OMAP4_MAILBOX_IRQENABLE_CLR(u)	(0x10c + 0x10 * (u))

#define MAILBOX_IRQSTATUS(type, u)	(type ? OMAP4_MAILBOX_IRQSTATUS(u) : \
						OMAP2_MAILBOX_IRQSTATUS(u))
#define MAILBOX_IRQENABLE(type, u)	(type ? OMAP4_MAILBOX_IRQENABLE(u) : \
						OMAP2_MAILBOX_IRQENABLE(u))
#define MAILBOX_IRQDISABLE(type, u)	(type ? OMAP4_MAILBOX_IRQENABLE_CLR(u) \
						: OMAP2_MAILBOX_IRQENABLE(u))

#define MAILBOX_IRQ_NEWMSG(m)		(1 << (2 * (m)))
#define MAILBOX_IRQ_NOTFULL(m)		(1 << (2 * (m) + 1))

#define MBOX_REG_SIZE			0x120

#define OMAP4_MBOX_REG_SIZE		0x130

#define MBOX_NR_REGS			(MBOX_REG_SIZE / sizeof(u32))
#define OMAP4_MBOX_NR_REGS		(OMAP4_MBOX_REG_SIZE / sizeof(u32))

struct omap_mbox_fifo {
	unsigned long msg;
	unsigned long fifo_stat;
	unsigned long msg_stat;
};

struct omap_mbox_priv {
	struct omap_mbox_fifo tx_fifo;
	struct omap_mbox_fifo rx_fifo;
	unsigned long irqenable;
	unsigned long irqstatus;
	u32 newmsg_bit;
	u32 notfull_bit;
	u32 ctx[OMAP4_MBOX_NR_REGS];
	unsigned long irqdisable;
	u32 intr_type;
};

struct omap_mbox_queue {
	spinlock_t		lock;
	struct kfifo		fifo;
	struct work_struct	work;
	struct tasklet_struct	tasklet;
	struct omap_mbox	*mbox;
	bool full;
};

struct omap_mbox {
	const char		*name;
	int			irq;
	struct omap_mbox_queue	*txq, *rxq;
	struct device		*dev;
	void			*priv;
	int			use_count;
	struct blocking_notifier_head	notifier;
};

static void __iomem *mbox_base;
static struct omap_mbox **mboxes;

static DEFINE_MUTEX(mbox_configured_lock);

static unsigned int mbox_kfifo_size = CONFIG_OMAP_MBOX_KFIFO_SIZE;
module_param(mbox_kfifo_size, uint, S_IRUGO);
MODULE_PARM_DESC(mbox_kfifo_size, "Size of omap's mailbox kfifo (bytes)");

static inline unsigned int mbox_read_reg(size_t ofs)
{
	return __raw_readl(mbox_base + ofs);
}

static inline void mbox_write_reg(u32 val, size_t ofs)
{
	__raw_writel(val, mbox_base + ofs);
}

/* Mailbox FIFO handle functions */
static mbox_msg_t mbox_fifo_read(struct omap_mbox *mbox)
{
	struct omap_mbox_fifo *fifo =
		&((struct omap_mbox_priv *)mbox->priv)->rx_fifo;
	return (mbox_msg_t) mbox_read_reg(fifo->msg);
}

static void mbox_fifo_write(struct omap_mbox *mbox, mbox_msg_t msg)
{
	struct omap_mbox_fifo *fifo =
		&((struct omap_mbox_priv *)mbox->priv)->tx_fifo;
	mbox_write_reg(msg, fifo->msg);
}

static int mbox_fifo_empty(struct omap_mbox *mbox)
{
	struct omap_mbox_fifo *fifo =
		&((struct omap_mbox_priv *)mbox->priv)->rx_fifo;
	return (mbox_read_reg(fifo->msg_stat) == 0);
}

static int mbox_fifo_full(struct omap_mbox *mbox)
{
	struct omap_mbox_fifo *fifo =
		&((struct omap_mbox_priv *)mbox->priv)->tx_fifo;
	return mbox_read_reg(fifo->fifo_stat);
}

/* Mailbox IRQ handle functions */
static void ack_mbox_irq(struct omap_mbox *mbox, omap_mbox_irq_t irq)
{
	struct omap_mbox_priv *p = mbox->priv;
	u32 bit = (irq == IRQ_TX) ? p->notfull_bit : p->newmsg_bit;

	mbox_write_reg(bit, p->irqstatus);

	/* Flush posted write for irq status to avoid spurious interrupts */
	mbox_read_reg(p->irqstatus);
}

static int is_mbox_irq(struct omap_mbox *mbox, omap_mbox_irq_t irq)
{
	struct omap_mbox_priv *p = mbox->priv;
	u32 bit = (irq == IRQ_TX) ? p->notfull_bit : p->newmsg_bit;
	u32 enable = mbox_read_reg(p->irqenable);
	u32 status = mbox_read_reg(p->irqstatus);

	return (int)(enable & status & bit);
}

/*
 * message sender
 */
int omap_mbox_msg_send(struct omap_mbox *mbox, mbox_msg_t msg)
{
	struct omap_mbox_queue *mq = mbox->txq;
	int ret = 0, len;

	spin_lock_bh(&mq->lock);

	if (kfifo_avail(&mq->fifo) < sizeof(msg)) {
		ret = -ENOMEM;
		goto out;
	}

	if (kfifo_is_empty(&mq->fifo) && !mbox_fifo_full(mbox)) {
		mbox_fifo_write(mbox, msg);
		goto out;
	}

	len = kfifo_in(&mq->fifo, (unsigned char *)&msg, sizeof(msg));
	WARN_ON(len != sizeof(msg));

	tasklet_schedule(&mbox->txq->tasklet);

out:
	spin_unlock_bh(&mq->lock);
	return ret;
}
EXPORT_SYMBOL(omap_mbox_msg_send);

void omap_mbox_save_ctx(struct omap_mbox *mbox)
{
	int i;
	struct omap_mbox_priv *p = mbox->priv;
	int nr_regs;

	if (p->intr_type)
		nr_regs = OMAP4_MBOX_NR_REGS;
	else
		nr_regs = MBOX_NR_REGS;
	for (i = 0; i < nr_regs; i++) {
		p->ctx[i] = mbox_read_reg(i * sizeof(u32));

		dev_dbg(mbox->dev, "%s: [%02x] %08x\n", __func__,
			i, p->ctx[i]);
	}
}
EXPORT_SYMBOL(omap_mbox_save_ctx);

void omap_mbox_restore_ctx(struct omap_mbox *mbox)
{
	int i;
	struct omap_mbox_priv *p = mbox->priv;
	int nr_regs;

	if (p->intr_type)
		nr_regs = OMAP4_MBOX_NR_REGS;
	else
		nr_regs = MBOX_NR_REGS;
	for (i = 0; i < nr_regs; i++) {
		mbox_write_reg(p->ctx[i], i * sizeof(u32));

		dev_dbg(mbox->dev, "%s: [%02x] %08x\n", __func__,
			i, p->ctx[i]);
	}
}
EXPORT_SYMBOL(omap_mbox_restore_ctx);

void omap_mbox_enable_irq(struct omap_mbox *mbox, omap_mbox_irq_t irq)
{
	struct omap_mbox_priv *p = mbox->priv;
	u32 l, bit = (irq == IRQ_TX) ? p->notfull_bit : p->newmsg_bit;

	l = mbox_read_reg(p->irqenable);
	l |= bit;
	mbox_write_reg(l, p->irqenable);
}
EXPORT_SYMBOL(omap_mbox_enable_irq);

void omap_mbox_disable_irq(struct omap_mbox *mbox, omap_mbox_irq_t irq)
{
	struct omap_mbox_priv *p = mbox->priv;
	u32 bit = (irq == IRQ_TX) ? p->notfull_bit : p->newmsg_bit;

	/*
	 * Read and update the interrupt configuration register for pre-OMAP4.
	 * OMAP4 and later SoCs have a dedicated interrupt disabling register.
	 */
	if (!p->intr_type)
		bit = mbox_read_reg(p->irqdisable) & ~bit;

	mbox_write_reg(bit, p->irqdisable);
}
EXPORT_SYMBOL(omap_mbox_disable_irq);

static void mbox_tx_tasklet(unsigned long tx_data)
{
	struct omap_mbox *mbox = (struct omap_mbox *)tx_data;
	struct omap_mbox_queue *mq = mbox->txq;
	mbox_msg_t msg;
	int ret;

	while (kfifo_len(&mq->fifo)) {
		if (mbox_fifo_full(mbox)) {
			omap_mbox_enable_irq(mbox, IRQ_TX);
			break;
		}

		ret = kfifo_out(&mq->fifo, (unsigned char *)&msg,
								sizeof(msg));
		WARN_ON(ret != sizeof(msg));

		mbox_fifo_write(mbox, msg);
	}
}

/*
 * Message receiver(workqueue)
 */
static void mbox_rx_work(struct work_struct *work)
{
	struct omap_mbox_queue *mq =
			container_of(work, struct omap_mbox_queue, work);
	mbox_msg_t msg;
	int len;

	while (kfifo_len(&mq->fifo) >= sizeof(msg)) {
		len = kfifo_out(&mq->fifo, (unsigned char *)&msg, sizeof(msg));
		WARN_ON(len != sizeof(msg));

		blocking_notifier_call_chain(&mq->mbox->notifier, len,
								(void *)msg);
		spin_lock_irq(&mq->lock);
		if (mq->full) {
			mq->full = false;
			omap_mbox_enable_irq(mq->mbox, IRQ_RX);
		}
		spin_unlock_irq(&mq->lock);
	}
}

/*
 * Mailbox interrupt handler
 */
static void __mbox_tx_interrupt(struct omap_mbox *mbox)
{
	omap_mbox_disable_irq(mbox, IRQ_TX);
	ack_mbox_irq(mbox, IRQ_TX);
	tasklet_schedule(&mbox->txq->tasklet);
}

static void __mbox_rx_interrupt(struct omap_mbox *mbox)
{
	struct omap_mbox_queue *mq = mbox->rxq;
	mbox_msg_t msg;
	int len;

	while (!mbox_fifo_empty(mbox)) {
		if (unlikely(kfifo_avail(&mq->fifo) < sizeof(msg))) {
			omap_mbox_disable_irq(mbox, IRQ_RX);
			mq->full = true;
			goto nomem;
		}

		msg = mbox_fifo_read(mbox);

		len = kfifo_in(&mq->fifo, (unsigned char *)&msg, sizeof(msg));
		WARN_ON(len != sizeof(msg));
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
					void (*work) (struct work_struct *),
					void (*tasklet)(unsigned long))
{
	struct omap_mbox_queue *mq;

	mq = kzalloc(sizeof(struct omap_mbox_queue), GFP_KERNEL);
	if (!mq)
		return NULL;

	spin_lock_init(&mq->lock);

	if (kfifo_alloc(&mq->fifo, mbox_kfifo_size, GFP_KERNEL))
		goto error;

	if (work)
		INIT_WORK(&mq->work, work);

	if (tasklet)
		tasklet_init(&mq->tasklet, tasklet, (unsigned long)mbox);
	return mq;
error:
	kfree(mq);
	return NULL;
}

static void mbox_queue_free(struct omap_mbox_queue *q)
{
	kfifo_free(&q->fifo);
	kfree(q);
}

static int omap_mbox_startup(struct omap_mbox *mbox)
{
	int ret = 0;
	struct omap_mbox_queue *mq;

	mutex_lock(&mbox_configured_lock);
	ret = pm_runtime_get_sync(mbox->dev->parent);
	if (unlikely(ret < 0))
		goto fail_startup;

	if (!mbox->use_count++) {
		mq = mbox_queue_alloc(mbox, NULL, mbox_tx_tasklet);
		if (!mq) {
			ret = -ENOMEM;
			goto fail_alloc_txq;
		}
		mbox->txq = mq;

		mq = mbox_queue_alloc(mbox, mbox_rx_work, NULL);
		if (!mq) {
			ret = -ENOMEM;
			goto fail_alloc_rxq;
		}
		mbox->rxq = mq;
		mq->mbox = mbox;
		ret = request_irq(mbox->irq, mbox_interrupt, IRQF_SHARED,
							mbox->name, mbox);
		if (unlikely(ret)) {
			pr_err("failed to register mailbox interrupt:%d\n",
									ret);
			goto fail_request_irq;
		}

		omap_mbox_enable_irq(mbox, IRQ_RX);
	}
	mutex_unlock(&mbox_configured_lock);
	return 0;

fail_request_irq:
	mbox_queue_free(mbox->rxq);
fail_alloc_rxq:
	mbox_queue_free(mbox->txq);
fail_alloc_txq:
	pm_runtime_put_sync(mbox->dev->parent);
	mbox->use_count--;
fail_startup:
	mutex_unlock(&mbox_configured_lock);
	return ret;
}

static void omap_mbox_fini(struct omap_mbox *mbox)
{
	mutex_lock(&mbox_configured_lock);

	if (!--mbox->use_count) {
		omap_mbox_disable_irq(mbox, IRQ_RX);
		free_irq(mbox->irq, mbox);
		tasklet_kill(&mbox->txq->tasklet);
		flush_work(&mbox->rxq->work);
		mbox_queue_free(mbox->txq);
		mbox_queue_free(mbox->rxq);
	}

	pm_runtime_put_sync(mbox->dev->parent);

	mutex_unlock(&mbox_configured_lock);
}

struct omap_mbox *omap_mbox_get(const char *name, struct notifier_block *nb)
{
	struct omap_mbox *_mbox, *mbox = NULL;
	int i, ret;

	if (!mboxes)
		return ERR_PTR(-EINVAL);

	for (i = 0; (_mbox = mboxes[i]); i++) {
		if (!strcmp(_mbox->name, name)) {
			mbox = _mbox;
			break;
		}
	}

	if (!mbox)
		return ERR_PTR(-ENOENT);

	if (nb)
		blocking_notifier_chain_register(&mbox->notifier, nb);

	ret = omap_mbox_startup(mbox);
	if (ret) {
		blocking_notifier_chain_unregister(&mbox->notifier, nb);
		return ERR_PTR(-ENODEV);
	}

	return mbox;
}
EXPORT_SYMBOL(omap_mbox_get);

void omap_mbox_put(struct omap_mbox *mbox, struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&mbox->notifier, nb);
	omap_mbox_fini(mbox);
}
EXPORT_SYMBOL(omap_mbox_put);

static struct class omap_mbox_class = { .name = "mbox", };

static int omap_mbox_register(struct device *parent, struct omap_mbox **list)
{
	int ret;
	int i;

	mboxes = list;
	if (!mboxes)
		return -EINVAL;

	for (i = 0; mboxes[i]; i++) {
		struct omap_mbox *mbox = mboxes[i];
		mbox->dev = device_create(&omap_mbox_class,
				parent, 0, mbox, "%s", mbox->name);
		if (IS_ERR(mbox->dev)) {
			ret = PTR_ERR(mbox->dev);
			goto err_out;
		}

		BLOCKING_INIT_NOTIFIER_HEAD(&mbox->notifier);
	}
	return 0;

err_out:
	while (i--)
		device_unregister(mboxes[i]->dev);
	return ret;
}

static int omap_mbox_unregister(void)
{
	int i;

	if (!mboxes)
		return -EINVAL;

	for (i = 0; mboxes[i]; i++)
		device_unregister(mboxes[i]->dev);
	mboxes = NULL;
	return 0;
}

static int omap_mbox_probe(struct platform_device *pdev)
{
	struct resource *mem;
	int ret;
	struct omap_mbox **list, *mbox, *mboxblk;
	struct omap_mbox_priv *priv, *privblk;
	struct omap_mbox_pdata *pdata = pdev->dev.platform_data;
	struct omap_mbox_dev_info *info;
	u32 intr_type;
	u32 l;
	int i;

	if (!pdata || !pdata->info_cnt || !pdata->info) {
		pr_err("%s: platform not supported\n", __func__);
		return -ENODEV;
	}

	/* allocate one extra for marking end of list */
	list = devm_kzalloc(&pdev->dev, (pdata->info_cnt + 1) * sizeof(*list),
			    GFP_KERNEL);
	if (!list)
		return -ENOMEM;

	mboxblk = devm_kzalloc(&pdev->dev, pdata->info_cnt * sizeof(*mbox),
			       GFP_KERNEL);
	if (!mboxblk)
		return -ENOMEM;

	privblk = devm_kzalloc(&pdev->dev, pdata->info_cnt * sizeof(*priv),
			       GFP_KERNEL);
	if (!privblk)
		return -ENOMEM;

	info = pdata->info;
	intr_type = pdata->intr_type;
	mbox = mboxblk;
	priv = privblk;
	for (i = 0; i < pdata->info_cnt; i++, info++, priv++) {
		priv->tx_fifo.msg = MAILBOX_MESSAGE(info->tx_id);
		priv->tx_fifo.fifo_stat = MAILBOX_FIFOSTATUS(info->tx_id);
		priv->rx_fifo.msg =  MAILBOX_MESSAGE(info->rx_id);
		priv->rx_fifo.msg_stat =  MAILBOX_MSGSTATUS(info->rx_id);
		priv->notfull_bit = MAILBOX_IRQ_NOTFULL(info->tx_id);
		priv->newmsg_bit = MAILBOX_IRQ_NEWMSG(info->rx_id);
		priv->irqenable = MAILBOX_IRQENABLE(intr_type, info->usr_id);
		priv->irqstatus = MAILBOX_IRQSTATUS(intr_type, info->usr_id);
		priv->irqdisable = MAILBOX_IRQDISABLE(intr_type, info->usr_id);
		priv->intr_type = intr_type;

		mbox->priv = priv;
		mbox->name = info->name;
		mbox->irq = platform_get_irq(pdev, info->irq_id);
		if (mbox->irq < 0)
			return mbox->irq;
		list[i] = mbox++;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mbox_base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(mbox_base))
		return PTR_ERR(mbox_base);

	ret = omap_mbox_register(&pdev->dev, list);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, list);
	pm_runtime_enable(&pdev->dev);

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(&pdev->dev);
		goto unregister;
	}

	/*
	 * just print the raw revision register, the format is not
	 * uniform across all SoCs
	 */
	l = mbox_read_reg(MAILBOX_REVISION);
	dev_info(&pdev->dev, "omap mailbox rev 0x%x\n", l);

	ret = pm_runtime_put_sync(&pdev->dev);
	if (ret < 0)
		goto unregister;

	return 0;

unregister:
	pm_runtime_disable(&pdev->dev);
	omap_mbox_unregister();
	return ret;
}

static int omap_mbox_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	omap_mbox_unregister();

	return 0;
}

static struct platform_driver omap_mbox_driver = {
	.probe	= omap_mbox_probe,
	.remove	= omap_mbox_remove,
	.driver	= {
		.name = "omap-mailbox",
		.owner = THIS_MODULE,
	},
};

static int __init omap_mbox_init(void)
{
	int err;

	err = class_register(&omap_mbox_class);
	if (err)
		return err;

	/* kfifo size sanity check: alignment and minimal size */
	mbox_kfifo_size = ALIGN(mbox_kfifo_size, sizeof(mbox_msg_t));
	mbox_kfifo_size = max_t(unsigned int, mbox_kfifo_size,
							sizeof(mbox_msg_t));

	return platform_driver_register(&omap_mbox_driver);
}
subsys_initcall(omap_mbox_init);

static void __exit omap_mbox_exit(void)
{
	platform_driver_unregister(&omap_mbox_driver);
	class_unregister(&omap_mbox_class);
}
module_exit(omap_mbox_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("omap mailbox: interrupt driven messaging");
MODULE_AUTHOR("Toshihiro Kobayashi");
MODULE_AUTHOR("Hiroshi DOYU");

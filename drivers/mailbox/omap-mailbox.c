// SPDX-License-Identifier: GPL-2.0
/*
 * OMAP mailbox driver
 *
 * Copyright (C) 2006-2009 Nokia Corporation. All rights reserved.
 * Copyright (C) 2013-2021 Texas Instruments Incorporated - https://www.ti.com
 *
 * Contact: Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 *          Suman Anna <s-anna@ti.com>
 */

#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/kfifo.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/mailbox_controller.h>
#include <linux/mailbox_client.h>

#include "mailbox.h"

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

/* Interrupt register configuration types */
#define MBOX_INTR_CFG_TYPE1		0
#define MBOX_INTR_CFG_TYPE2		1

typedef enum {
	IRQ_TX = 1,
	IRQ_RX = 2,
} omap_mbox_irq_t;

struct omap_mbox_fifo {
	unsigned long msg;
	unsigned long fifo_stat;
	unsigned long msg_stat;
	unsigned long irqenable;
	unsigned long irqstatus;
	unsigned long irqdisable;
	u32 intr_bit;
};

struct omap_mbox_match_data {
	u32 intr_type;
};

struct omap_mbox_device {
	struct device *dev;
	struct mutex cfg_lock;
	void __iomem *mbox_base;
	u32 *irq_ctx;
	u32 num_users;
	u32 num_fifos;
	u32 intr_type;
};

struct omap_mbox {
	const char		*name;
	int			irq;
	struct omap_mbox_device *parent;
	struct omap_mbox_fifo	tx_fifo;
	struct omap_mbox_fifo	rx_fifo;
	u32			intr_type;
	struct mbox_chan	*chan;
	bool			send_no_irq;
};

static inline
unsigned int mbox_read_reg(struct omap_mbox_device *mdev, size_t ofs)
{
	return __raw_readl(mdev->mbox_base + ofs);
}

static inline
void mbox_write_reg(struct omap_mbox_device *mdev, u32 val, size_t ofs)
{
	__raw_writel(val, mdev->mbox_base + ofs);
}

/* Mailbox FIFO handle functions */
static u32 mbox_fifo_read(struct omap_mbox *mbox)
{
	struct omap_mbox_fifo *fifo = &mbox->rx_fifo;

	return mbox_read_reg(mbox->parent, fifo->msg);
}

static void mbox_fifo_write(struct omap_mbox *mbox, u32 msg)
{
	struct omap_mbox_fifo *fifo = &mbox->tx_fifo;

	mbox_write_reg(mbox->parent, msg, fifo->msg);
}

static int mbox_fifo_empty(struct omap_mbox *mbox)
{
	struct omap_mbox_fifo *fifo = &mbox->rx_fifo;

	return (mbox_read_reg(mbox->parent, fifo->msg_stat) == 0);
}

static int mbox_fifo_full(struct omap_mbox *mbox)
{
	struct omap_mbox_fifo *fifo = &mbox->tx_fifo;

	return mbox_read_reg(mbox->parent, fifo->fifo_stat);
}

/* Mailbox IRQ handle functions */
static void ack_mbox_irq(struct omap_mbox *mbox, omap_mbox_irq_t irq)
{
	struct omap_mbox_fifo *fifo = (irq == IRQ_TX) ?
				&mbox->tx_fifo : &mbox->rx_fifo;
	u32 bit = fifo->intr_bit;
	u32 irqstatus = fifo->irqstatus;

	mbox_write_reg(mbox->parent, bit, irqstatus);

	/* Flush posted write for irq status to avoid spurious interrupts */
	mbox_read_reg(mbox->parent, irqstatus);
}

static int is_mbox_irq(struct omap_mbox *mbox, omap_mbox_irq_t irq)
{
	struct omap_mbox_fifo *fifo = (irq == IRQ_TX) ?
				&mbox->tx_fifo : &mbox->rx_fifo;
	u32 bit = fifo->intr_bit;
	u32 irqenable = fifo->irqenable;
	u32 irqstatus = fifo->irqstatus;

	u32 enable = mbox_read_reg(mbox->parent, irqenable);
	u32 status = mbox_read_reg(mbox->parent, irqstatus);

	return (int)(enable & status & bit);
}

static void omap_mbox_enable_irq(struct omap_mbox *mbox, omap_mbox_irq_t irq)
{
	u32 l;
	struct omap_mbox_fifo *fifo = (irq == IRQ_TX) ?
				&mbox->tx_fifo : &mbox->rx_fifo;
	u32 bit = fifo->intr_bit;
	u32 irqenable = fifo->irqenable;

	l = mbox_read_reg(mbox->parent, irqenable);
	l |= bit;
	mbox_write_reg(mbox->parent, l, irqenable);
}

static void omap_mbox_disable_irq(struct omap_mbox *mbox, omap_mbox_irq_t irq)
{
	struct omap_mbox_fifo *fifo = (irq == IRQ_TX) ?
				&mbox->tx_fifo : &mbox->rx_fifo;
	u32 bit = fifo->intr_bit;
	u32 irqdisable = fifo->irqdisable;

	/*
	 * Read and update the interrupt configuration register for pre-OMAP4.
	 * OMAP4 and later SoCs have a dedicated interrupt disabling register.
	 */
	if (!mbox->intr_type)
		bit = mbox_read_reg(mbox->parent, irqdisable) & ~bit;

	mbox_write_reg(mbox->parent, bit, irqdisable);
}

/*
 * Mailbox interrupt handler
 */
static void __mbox_tx_interrupt(struct omap_mbox *mbox)
{
	omap_mbox_disable_irq(mbox, IRQ_TX);
	ack_mbox_irq(mbox, IRQ_TX);
	mbox_chan_txdone(mbox->chan, 0);
}

static void __mbox_rx_interrupt(struct omap_mbox *mbox)
{
	u32 msg;

	while (!mbox_fifo_empty(mbox)) {
		msg = mbox_fifo_read(mbox);
		mbox_chan_received_data(mbox->chan, (void *)(uintptr_t)msg);
	}

	/* clear IRQ source. */
	ack_mbox_irq(mbox, IRQ_RX);
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

static int omap_mbox_startup(struct omap_mbox *mbox)
{
	int ret = 0;

	ret = request_threaded_irq(mbox->irq, NULL, mbox_interrupt,
				   IRQF_SHARED | IRQF_ONESHOT, mbox->name,
				   mbox);
	if (unlikely(ret)) {
		pr_err("failed to register mailbox interrupt:%d\n", ret);
		return ret;
	}

	if (mbox->send_no_irq)
		mbox->chan->txdone_method = TXDONE_BY_ACK;

	omap_mbox_enable_irq(mbox, IRQ_RX);

	return 0;
}

static void omap_mbox_fini(struct omap_mbox *mbox)
{
	omap_mbox_disable_irq(mbox, IRQ_RX);
	free_irq(mbox->irq, mbox);
}

static int omap_mbox_chan_startup(struct mbox_chan *chan)
{
	struct omap_mbox *mbox = chan->con_priv;
	struct omap_mbox_device *mdev = mbox->parent;
	int ret = 0;

	mutex_lock(&mdev->cfg_lock);
	pm_runtime_get_sync(mdev->dev);
	ret = omap_mbox_startup(mbox);
	if (ret)
		pm_runtime_put_sync(mdev->dev);
	mutex_unlock(&mdev->cfg_lock);
	return ret;
}

static void omap_mbox_chan_shutdown(struct mbox_chan *chan)
{
	struct omap_mbox *mbox = chan->con_priv;
	struct omap_mbox_device *mdev = mbox->parent;

	mutex_lock(&mdev->cfg_lock);
	omap_mbox_fini(mbox);
	pm_runtime_put_sync(mdev->dev);
	mutex_unlock(&mdev->cfg_lock);
}

static int omap_mbox_chan_send_noirq(struct omap_mbox *mbox, u32 msg)
{
	if (mbox_fifo_full(mbox))
		return -EBUSY;

	omap_mbox_enable_irq(mbox, IRQ_RX);
	mbox_fifo_write(mbox, msg);
	omap_mbox_disable_irq(mbox, IRQ_RX);

	/* we must read and ack the interrupt directly from here */
	mbox_fifo_read(mbox);
	ack_mbox_irq(mbox, IRQ_RX);

	return 0;
}

static int omap_mbox_chan_send(struct omap_mbox *mbox, u32 msg)
{
	if (mbox_fifo_full(mbox)) {
		/* always enable the interrupt */
		omap_mbox_enable_irq(mbox, IRQ_TX);
		return -EBUSY;
	}

	mbox_fifo_write(mbox, msg);

	/* always enable the interrupt */
	omap_mbox_enable_irq(mbox, IRQ_TX);
	return 0;
}

static int omap_mbox_chan_send_data(struct mbox_chan *chan, void *data)
{
	struct omap_mbox *mbox = chan->con_priv;
	int ret;
	u32 msg = (u32)(uintptr_t)(data);

	if (!mbox)
		return -EINVAL;

	if (mbox->send_no_irq)
		ret = omap_mbox_chan_send_noirq(mbox, msg);
	else
		ret = omap_mbox_chan_send(mbox, msg);

	return ret;
}

static const struct mbox_chan_ops omap_mbox_chan_ops = {
	.startup        = omap_mbox_chan_startup,
	.send_data      = omap_mbox_chan_send_data,
	.shutdown       = omap_mbox_chan_shutdown,
};

#ifdef CONFIG_PM_SLEEP
static int omap_mbox_suspend(struct device *dev)
{
	struct omap_mbox_device *mdev = dev_get_drvdata(dev);
	u32 usr, fifo, reg;

	if (pm_runtime_status_suspended(dev))
		return 0;

	for (fifo = 0; fifo < mdev->num_fifos; fifo++) {
		if (mbox_read_reg(mdev, MAILBOX_MSGSTATUS(fifo))) {
			dev_err(mdev->dev, "fifo %d has unexpected unread messages\n",
				fifo);
			return -EBUSY;
		}
	}

	for (usr = 0; usr < mdev->num_users; usr++) {
		reg = MAILBOX_IRQENABLE(mdev->intr_type, usr);
		mdev->irq_ctx[usr] = mbox_read_reg(mdev, reg);
	}

	return 0;
}

static int omap_mbox_resume(struct device *dev)
{
	struct omap_mbox_device *mdev = dev_get_drvdata(dev);
	u32 usr, reg;

	if (pm_runtime_status_suspended(dev))
		return 0;

	for (usr = 0; usr < mdev->num_users; usr++) {
		reg = MAILBOX_IRQENABLE(mdev->intr_type, usr);
		mbox_write_reg(mdev, mdev->irq_ctx[usr], reg);
	}

	return 0;
}
#endif

static const struct dev_pm_ops omap_mbox_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(omap_mbox_suspend, omap_mbox_resume)
};

static const struct omap_mbox_match_data omap2_data = { MBOX_INTR_CFG_TYPE1 };
static const struct omap_mbox_match_data omap4_data = { MBOX_INTR_CFG_TYPE2 };

static const struct of_device_id omap_mailbox_of_match[] = {
	{
		.compatible	= "ti,omap2-mailbox",
		.data		= &omap2_data,
	},
	{
		.compatible	= "ti,omap3-mailbox",
		.data		= &omap2_data,
	},
	{
		.compatible	= "ti,omap4-mailbox",
		.data		= &omap4_data,
	},
	{
		.compatible	= "ti,am654-mailbox",
		.data		= &omap4_data,
	},
	{
		.compatible	= "ti,am64-mailbox",
		.data		= &omap4_data,
	},
	{
		/* end */
	},
};
MODULE_DEVICE_TABLE(of, omap_mailbox_of_match);

static struct mbox_chan *omap_mbox_of_xlate(struct mbox_controller *controller,
					    const struct of_phandle_args *sp)
{
	phandle phandle = sp->args[0];
	struct device_node *node;
	struct omap_mbox_device *mdev;
	struct omap_mbox *mbox;
	int i;

	mdev = dev_get_drvdata(controller->dev);
	if (WARN_ON(!mdev))
		return ERR_PTR(-EINVAL);

	node = of_find_node_by_phandle(phandle);
	if (!node) {
		pr_err("%s: could not find node phandle 0x%x\n",
		       __func__, phandle);
		return ERR_PTR(-ENODEV);
	}

	for (i = 0; i < controller->num_chans; i++) {
		mbox = controller->chans[i].con_priv;
		if (!strcmp(mbox->name, node->name)) {
			of_node_put(node);
			return &controller->chans[i];
		}
	}

	of_node_put(node);
	return ERR_PTR(-ENOENT);
}

static int omap_mbox_probe(struct platform_device *pdev)
{
	int ret;
	struct mbox_chan *chnls;
	struct omap_mbox *mbox;
	struct omap_mbox_device *mdev;
	struct omap_mbox_fifo *fifo;
	struct device_node *node = pdev->dev.of_node;
	struct device_node *child;
	const struct omap_mbox_match_data *match_data;
	struct mbox_controller *controller;
	u32 intr_type, info_count;
	u32 num_users, num_fifos;
	u32 tmp[3];
	u32 l;
	int i;

	if (!node) {
		pr_err("%s: only DT-based devices are supported\n", __func__);
		return -ENODEV;
	}

	match_data = of_device_get_match_data(&pdev->dev);
	if (!match_data)
		return -ENODEV;
	intr_type = match_data->intr_type;

	if (of_property_read_u32(node, "ti,mbox-num-users", &num_users))
		return -ENODEV;

	if (of_property_read_u32(node, "ti,mbox-num-fifos", &num_fifos))
		return -ENODEV;

	info_count = of_get_available_child_count(node);
	if (!info_count) {
		dev_err(&pdev->dev, "no available mbox devices found\n");
		return -ENODEV;
	}

	mdev = devm_kzalloc(&pdev->dev, sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return -ENOMEM;

	mdev->mbox_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mdev->mbox_base))
		return PTR_ERR(mdev->mbox_base);

	mdev->irq_ctx = devm_kcalloc(&pdev->dev, num_users, sizeof(u32),
				     GFP_KERNEL);
	if (!mdev->irq_ctx)
		return -ENOMEM;

	chnls = devm_kcalloc(&pdev->dev, info_count + 1, sizeof(*chnls),
			     GFP_KERNEL);
	if (!chnls)
		return -ENOMEM;

	child = NULL;
	for (i = 0; i < info_count; i++) {
		int tx_id, tx_irq, tx_usr;
		int rx_id,         rx_usr;

		mbox = devm_kzalloc(&pdev->dev, sizeof(*mbox), GFP_KERNEL);
		if (!mbox)
			return -ENOMEM;

		child = of_get_next_available_child(node, child);
		ret = of_property_read_u32_array(child, "ti,mbox-tx", tmp,
						 ARRAY_SIZE(tmp));
		if (ret)
			return ret;
		tx_id = tmp[0];
		tx_irq = tmp[1];
		tx_usr = tmp[2];

		ret = of_property_read_u32_array(child, "ti,mbox-rx", tmp,
						 ARRAY_SIZE(tmp));
		if (ret)
			return ret;
		rx_id = tmp[0];
		/* rx_irq = tmp[1]; */
		rx_usr = tmp[2];

		if (tx_id >= num_fifos || rx_id >= num_fifos ||
		    tx_usr >= num_users || rx_usr >= num_users)
			return -EINVAL;

		fifo = &mbox->tx_fifo;
		fifo->msg = MAILBOX_MESSAGE(tx_id);
		fifo->fifo_stat = MAILBOX_FIFOSTATUS(tx_id);
		fifo->intr_bit = MAILBOX_IRQ_NOTFULL(tx_id);
		fifo->irqenable = MAILBOX_IRQENABLE(intr_type, tx_usr);
		fifo->irqstatus = MAILBOX_IRQSTATUS(intr_type, tx_usr);
		fifo->irqdisable = MAILBOX_IRQDISABLE(intr_type, tx_usr);

		fifo = &mbox->rx_fifo;
		fifo->msg = MAILBOX_MESSAGE(rx_id);
		fifo->msg_stat =  MAILBOX_MSGSTATUS(rx_id);
		fifo->intr_bit = MAILBOX_IRQ_NEWMSG(rx_id);
		fifo->irqenable = MAILBOX_IRQENABLE(intr_type, rx_usr);
		fifo->irqstatus = MAILBOX_IRQSTATUS(intr_type, rx_usr);
		fifo->irqdisable = MAILBOX_IRQDISABLE(intr_type, rx_usr);

		mbox->send_no_irq = of_property_read_bool(child, "ti,mbox-send-noirq");
		mbox->intr_type = intr_type;

		mbox->parent = mdev;
		mbox->name = child->name;
		mbox->irq = platform_get_irq(pdev, tx_irq);
		if (mbox->irq < 0)
			return mbox->irq;
		mbox->chan = &chnls[i];
		chnls[i].con_priv = mbox;
	}

	mutex_init(&mdev->cfg_lock);
	mdev->dev = &pdev->dev;
	mdev->num_users = num_users;
	mdev->num_fifos = num_fifos;
	mdev->intr_type = intr_type;

	controller = devm_kzalloc(&pdev->dev, sizeof(*controller), GFP_KERNEL);
	if (!controller)
		return -ENOMEM;
	/*
	 * OMAP/K3 Mailbox IP does not have a Tx-Done IRQ, but rather a Tx-Ready
	 * IRQ and is needed to run the Tx state machine
	 */
	controller->txdone_irq = true;
	controller->dev = mdev->dev;
	controller->ops = &omap_mbox_chan_ops;
	controller->chans = chnls;
	controller->num_chans = info_count;
	controller->of_xlate = omap_mbox_of_xlate;
	ret = devm_mbox_controller_register(mdev->dev, controller);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, mdev);
	devm_pm_runtime_enable(mdev->dev);

	ret = pm_runtime_resume_and_get(mdev->dev);
	if (ret < 0)
		return ret;

	/*
	 * just print the raw revision register, the format is not
	 * uniform across all SoCs
	 */
	l = mbox_read_reg(mdev, MAILBOX_REVISION);
	dev_info(mdev->dev, "omap mailbox rev 0x%x\n", l);

	ret = pm_runtime_put_sync(mdev->dev);
	if (ret < 0 && ret != -ENOSYS)
		return ret;

	return 0;
}

static struct platform_driver omap_mbox_driver = {
	.probe	= omap_mbox_probe,
	.driver	= {
		.name = "omap-mailbox",
		.pm = &omap_mbox_pm_ops,
		.of_match_table = omap_mailbox_of_match,
	},
};
module_platform_driver(omap_mbox_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("omap mailbox: interrupt driven messaging");
MODULE_AUTHOR("Toshihiro Kobayashi");
MODULE_AUTHOR("Hiroshi DOYU");

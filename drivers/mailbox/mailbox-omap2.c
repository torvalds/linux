/*
 * Mailbox reservation modules for OMAP2/3
 *
 * Copyright (C) 2006-2009 Nokia Corporation
 * Written by: Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 *        and  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/platform_data/mailbox-omap.h>

#include "omap-mbox.h"

#define MAILBOX_REVISION		0x000
#define MAILBOX_MESSAGE(m)		(0x040 + 4 * (m))
#define MAILBOX_FIFOSTATUS(m)		(0x080 + 4 * (m))
#define MAILBOX_MSGSTATUS(m)		(0x0c0 + 4 * (m))
#define MAILBOX_IRQSTATUS(u)		(0x100 + 8 * (u))
#define MAILBOX_IRQENABLE(u)		(0x104 + 8 * (u))

#define OMAP4_MAILBOX_IRQSTATUS(u)	(0x104 + 0x10 * (u))
#define OMAP4_MAILBOX_IRQENABLE(u)	(0x108 + 0x10 * (u))
#define OMAP4_MAILBOX_IRQENABLE_CLR(u)	(0x10c + 0x10 * (u))

#define MAILBOX_IRQ_NEWMSG(m)		(1 << (2 * (m)))
#define MAILBOX_IRQ_NOTFULL(m)		(1 << (2 * (m) + 1))

#define MBOX_REG_SIZE			0x120

#define OMAP4_MBOX_REG_SIZE		0x130

#define MBOX_NR_REGS			(MBOX_REG_SIZE / sizeof(u32))
#define OMAP4_MBOX_NR_REGS		(OMAP4_MBOX_REG_SIZE / sizeof(u32))

static void __iomem *mbox_base;

struct omap_mbox2_fifo {
	unsigned long msg;
	unsigned long fifo_stat;
	unsigned long msg_stat;
};

struct omap_mbox2_priv {
	struct omap_mbox2_fifo tx_fifo;
	struct omap_mbox2_fifo rx_fifo;
	unsigned long irqenable;
	unsigned long irqstatus;
	u32 newmsg_bit;
	u32 notfull_bit;
	u32 ctx[OMAP4_MBOX_NR_REGS];
	unsigned long irqdisable;
	u32 intr_type;
};

static inline unsigned int mbox_read_reg(size_t ofs)
{
	return __raw_readl(mbox_base + ofs);
}

static inline void mbox_write_reg(u32 val, size_t ofs)
{
	__raw_writel(val, mbox_base + ofs);
}

/* Mailbox H/W preparations */
static int omap2_mbox_startup(struct omap_mbox *mbox)
{
	u32 l;

	pm_runtime_enable(mbox->dev->parent);
	pm_runtime_get_sync(mbox->dev->parent);

	l = mbox_read_reg(MAILBOX_REVISION);
	pr_debug("omap mailbox rev %d.%d\n", (l & 0xf0) >> 4, (l & 0x0f));

	return 0;
}

static void omap2_mbox_shutdown(struct omap_mbox *mbox)
{
	pm_runtime_put_sync(mbox->dev->parent);
	pm_runtime_disable(mbox->dev->parent);
}

/* Mailbox FIFO handle functions */
static mbox_msg_t omap2_mbox_fifo_read(struct omap_mbox *mbox)
{
	struct omap_mbox2_fifo *fifo =
		&((struct omap_mbox2_priv *)mbox->priv)->rx_fifo;
	return (mbox_msg_t) mbox_read_reg(fifo->msg);
}

static void omap2_mbox_fifo_write(struct omap_mbox *mbox, mbox_msg_t msg)
{
	struct omap_mbox2_fifo *fifo =
		&((struct omap_mbox2_priv *)mbox->priv)->tx_fifo;
	mbox_write_reg(msg, fifo->msg);
}

static int omap2_mbox_fifo_empty(struct omap_mbox *mbox)
{
	struct omap_mbox2_fifo *fifo =
		&((struct omap_mbox2_priv *)mbox->priv)->rx_fifo;
	return (mbox_read_reg(fifo->msg_stat) == 0);
}

static int omap2_mbox_fifo_full(struct omap_mbox *mbox)
{
	struct omap_mbox2_fifo *fifo =
		&((struct omap_mbox2_priv *)mbox->priv)->tx_fifo;
	return mbox_read_reg(fifo->fifo_stat);
}

/* Mailbox IRQ handle functions */
static void omap2_mbox_enable_irq(struct omap_mbox *mbox, omap_mbox_irq_t irq)
{
	struct omap_mbox2_priv *p = mbox->priv;
	u32 l, bit = (irq == IRQ_TX) ? p->notfull_bit : p->newmsg_bit;

	l = mbox_read_reg(p->irqenable);
	l |= bit;
	mbox_write_reg(l, p->irqenable);
}

static void omap2_mbox_disable_irq(struct omap_mbox *mbox, omap_mbox_irq_t irq)
{
	struct omap_mbox2_priv *p = mbox->priv;
	u32 bit = (irq == IRQ_TX) ? p->notfull_bit : p->newmsg_bit;

	/*
	 * Read and update the interrupt configuration register for pre-OMAP4.
	 * OMAP4 and later SoCs have a dedicated interrupt disabling register.
	 */
	if (!p->intr_type)
		bit = mbox_read_reg(p->irqdisable) & ~bit;

	mbox_write_reg(bit, p->irqdisable);
}

static void omap2_mbox_ack_irq(struct omap_mbox *mbox, omap_mbox_irq_t irq)
{
	struct omap_mbox2_priv *p = mbox->priv;
	u32 bit = (irq == IRQ_TX) ? p->notfull_bit : p->newmsg_bit;

	mbox_write_reg(bit, p->irqstatus);

	/* Flush posted write for irq status to avoid spurious interrupts */
	mbox_read_reg(p->irqstatus);
}

static int omap2_mbox_is_irq(struct omap_mbox *mbox, omap_mbox_irq_t irq)
{
	struct omap_mbox2_priv *p = mbox->priv;
	u32 bit = (irq == IRQ_TX) ? p->notfull_bit : p->newmsg_bit;
	u32 enable = mbox_read_reg(p->irqenable);
	u32 status = mbox_read_reg(p->irqstatus);

	return (int)(enable & status & bit);
}

static void omap2_mbox_save_ctx(struct omap_mbox *mbox)
{
	int i;
	struct omap_mbox2_priv *p = mbox->priv;
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

static void omap2_mbox_restore_ctx(struct omap_mbox *mbox)
{
	int i;
	struct omap_mbox2_priv *p = mbox->priv;
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

static struct omap_mbox_ops omap2_mbox_ops = {
	.type		= OMAP_MBOX_TYPE2,
	.startup	= omap2_mbox_startup,
	.shutdown	= omap2_mbox_shutdown,
	.fifo_read	= omap2_mbox_fifo_read,
	.fifo_write	= omap2_mbox_fifo_write,
	.fifo_empty	= omap2_mbox_fifo_empty,
	.fifo_full	= omap2_mbox_fifo_full,
	.enable_irq	= omap2_mbox_enable_irq,
	.disable_irq	= omap2_mbox_disable_irq,
	.ack_irq	= omap2_mbox_ack_irq,
	.is_irq		= omap2_mbox_is_irq,
	.save_ctx	= omap2_mbox_save_ctx,
	.restore_ctx	= omap2_mbox_restore_ctx,
};

static int omap2_mbox_probe(struct platform_device *pdev)
{
	struct resource *mem;
	int ret;
	struct omap_mbox **list, *mbox, *mboxblk;
	struct omap_mbox2_priv *priv, *privblk;
	struct omap_mbox_pdata *pdata = pdev->dev.platform_data;
	struct omap_mbox_dev_info *info;
	int i;

	if (!pdata || !pdata->info_cnt || !pdata->info) {
		pr_err("%s: platform not supported\n", __func__);
		return -ENODEV;
	}

	/* allocate one extra for marking end of list */
	list = kzalloc((pdata->info_cnt + 1) * sizeof(*list), GFP_KERNEL);
	if (!list)
		return -ENOMEM;

	mboxblk = mbox = kzalloc(pdata->info_cnt * sizeof(*mbox), GFP_KERNEL);
	if (!mboxblk) {
		ret = -ENOMEM;
		goto free_list;
	}

	privblk = priv = kzalloc(pdata->info_cnt * sizeof(*priv), GFP_KERNEL);
	if (!privblk) {
		ret = -ENOMEM;
		goto free_mboxblk;
	}

	info = pdata->info;
	for (i = 0; i < pdata->info_cnt; i++, info++, priv++) {
		priv->tx_fifo.msg = MAILBOX_MESSAGE(info->tx_id);
		priv->tx_fifo.fifo_stat = MAILBOX_FIFOSTATUS(info->tx_id);
		priv->rx_fifo.msg =  MAILBOX_MESSAGE(info->rx_id);
		priv->rx_fifo.msg_stat =  MAILBOX_MSGSTATUS(info->rx_id);
		priv->notfull_bit = MAILBOX_IRQ_NOTFULL(info->tx_id);
		priv->newmsg_bit = MAILBOX_IRQ_NEWMSG(info->rx_id);
		if (pdata->intr_type) {
			priv->irqenable = OMAP4_MAILBOX_IRQENABLE(info->usr_id);
			priv->irqstatus = OMAP4_MAILBOX_IRQSTATUS(info->usr_id);
			priv->irqdisable =
				OMAP4_MAILBOX_IRQENABLE_CLR(info->usr_id);
		} else {
			priv->irqenable = MAILBOX_IRQENABLE(info->usr_id);
			priv->irqstatus = MAILBOX_IRQSTATUS(info->usr_id);
			priv->irqdisable = MAILBOX_IRQENABLE(info->usr_id);
		}
		priv->intr_type = pdata->intr_type;

		mbox->priv = priv;
		mbox->name = info->name;
		mbox->ops = &omap2_mbox_ops;
		mbox->irq = platform_get_irq(pdev, info->irq_id);
		if (mbox->irq < 0) {
			ret = mbox->irq;
			goto free_privblk;
		}
		list[i] = mbox++;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		ret = -ENOENT;
		goto free_privblk;
	}

	mbox_base = ioremap(mem->start, resource_size(mem));
	if (!mbox_base) {
		ret = -ENOMEM;
		goto free_privblk;
	}

	ret = omap_mbox_register(&pdev->dev, list);
	if (ret)
		goto unmap_mbox;
	platform_set_drvdata(pdev, list);

	return 0;

unmap_mbox:
	iounmap(mbox_base);
free_privblk:
	kfree(privblk);
free_mboxblk:
	kfree(mboxblk);
free_list:
	kfree(list);
	return ret;
}

static int omap2_mbox_remove(struct platform_device *pdev)
{
	struct omap_mbox2_priv *privblk;
	struct omap_mbox **list = platform_get_drvdata(pdev);
	struct omap_mbox *mboxblk = list[0];

	privblk = mboxblk->priv;
	omap_mbox_unregister();
	iounmap(mbox_base);
	kfree(privblk);
	kfree(mboxblk);
	kfree(list);

	return 0;
}

static struct platform_driver omap2_mbox_driver = {
	.probe	= omap2_mbox_probe,
	.remove	= omap2_mbox_remove,
	.driver	= {
		.name = "omap-mailbox",
	},
};

static int __init omap2_mbox_init(void)
{
	return platform_driver_register(&omap2_mbox_driver);
}

static void __exit omap2_mbox_exit(void)
{
	platform_driver_unregister(&omap2_mbox_driver);
}

module_init(omap2_mbox_init);
module_exit(omap2_mbox_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("omap mailbox: omap2/3/4 architecture specific functions");
MODULE_AUTHOR("Hiroshi DOYU <Hiroshi.DOYU@nokia.com>");
MODULE_AUTHOR("Paul Mundt");
MODULE_ALIAS("platform:omap2-mailbox");

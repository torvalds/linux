/*
 * Mailbox reservation modules for OMAP2
 *
 * Copyright (C) 2006 Nokia Corporation
 * Written by: Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 *        and  Paul Mundt <paul.mundt@nokia.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <asm/arch/mailbox.h>
#include <asm/arch/irqs.h>
#include <asm/io.h>

#define MAILBOX_REVISION		0x00
#define MAILBOX_SYSCONFIG		0x10
#define MAILBOX_SYSSTATUS		0x14
#define MAILBOX_MESSAGE_0		0x40
#define MAILBOX_MESSAGE_1		0x44
#define MAILBOX_MESSAGE_2		0x48
#define MAILBOX_MESSAGE_3		0x4c
#define MAILBOX_MESSAGE_4		0x50
#define MAILBOX_MESSAGE_5		0x54
#define MAILBOX_FIFOSTATUS_0		0x80
#define MAILBOX_FIFOSTATUS_1		0x84
#define MAILBOX_FIFOSTATUS_2		0x88
#define MAILBOX_FIFOSTATUS_3		0x8c
#define MAILBOX_FIFOSTATUS_4		0x90
#define MAILBOX_FIFOSTATUS_5		0x94
#define MAILBOX_MSGSTATUS_0		0xc0
#define MAILBOX_MSGSTATUS_1		0xc4
#define MAILBOX_MSGSTATUS_2		0xc8
#define MAILBOX_MSGSTATUS_3		0xcc
#define MAILBOX_MSGSTATUS_4		0xd0
#define MAILBOX_MSGSTATUS_5		0xd4
#define MAILBOX_IRQSTATUS_0		0x100
#define MAILBOX_IRQENABLE_0		0x104
#define MAILBOX_IRQSTATUS_1		0x108
#define MAILBOX_IRQENABLE_1		0x10c
#define MAILBOX_IRQSTATUS_2		0x110
#define MAILBOX_IRQENABLE_2		0x114
#define MAILBOX_IRQSTATUS_3		0x118
#define MAILBOX_IRQENABLE_3		0x11c

static unsigned long mbox_base;

#define MAILBOX_IRQ_NOTFULL(n)		(1 << (2 * (n) + 1))
#define MAILBOX_IRQ_NEWMSG(n)		(1 << (2 * (n)))

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
};

static struct clk *mbox_ick_handle;

static void omap2_mbox_enable_irq(struct omap_mbox *mbox,
				  omap_mbox_type_t irq);

static inline unsigned int mbox_read_reg(unsigned int reg)
{
	return __raw_readl(mbox_base + reg);
}

static inline void mbox_write_reg(unsigned int val, unsigned int reg)
{
	__raw_writel(val, mbox_base + reg);
}

/* Mailbox H/W preparations */
static int omap2_mbox_startup(struct omap_mbox *mbox)
{
	unsigned int l;

	mbox_ick_handle = clk_get(NULL, "mailboxes_ick");
	if (IS_ERR(mbox_ick_handle)) {
		printk("Could not get mailboxes_ick\n");
		return -ENODEV;
	}
	clk_enable(mbox_ick_handle);

	/* set smart-idle & autoidle */
	l = mbox_read_reg(MAILBOX_SYSCONFIG);
	l |= 0x00000011;
	mbox_write_reg(l, MAILBOX_SYSCONFIG);

	omap2_mbox_enable_irq(mbox, IRQ_RX);

	return 0;
}

static void omap2_mbox_shutdown(struct omap_mbox *mbox)
{
	clk_disable(mbox_ick_handle);
	clk_put(mbox_ick_handle);
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
	return (mbox_read_reg(fifo->fifo_stat));
}

/* Mailbox IRQ handle functions */
static void omap2_mbox_enable_irq(struct omap_mbox *mbox,
		omap_mbox_type_t irq)
{
	struct omap_mbox2_priv *p = (struct omap_mbox2_priv *)mbox->priv;
	u32 l, bit = (irq == IRQ_TX) ? p->notfull_bit : p->newmsg_bit;

	l = mbox_read_reg(p->irqenable);
	l |= bit;
	mbox_write_reg(l, p->irqenable);
}

static void omap2_mbox_disable_irq(struct omap_mbox *mbox,
		omap_mbox_type_t irq)
{
	struct omap_mbox2_priv *p = (struct omap_mbox2_priv *)mbox->priv;
	u32 l, bit = (irq == IRQ_TX) ? p->notfull_bit : p->newmsg_bit;

	l = mbox_read_reg(p->irqenable);
	l &= ~bit;
	mbox_write_reg(l, p->irqenable);
}

static void omap2_mbox_ack_irq(struct omap_mbox *mbox,
		omap_mbox_type_t irq)
{
	struct omap_mbox2_priv *p = (struct omap_mbox2_priv *)mbox->priv;
	u32 bit = (irq == IRQ_TX) ? p->notfull_bit : p->newmsg_bit;

	mbox_write_reg(bit, p->irqstatus);
}

static int omap2_mbox_is_irq(struct omap_mbox *mbox,
		omap_mbox_type_t irq)
{
	struct omap_mbox2_priv *p = (struct omap_mbox2_priv *)mbox->priv;
	u32 bit = (irq == IRQ_TX) ? p->notfull_bit : p->newmsg_bit;
	u32 enable = mbox_read_reg(p->irqenable);
	u32 status = mbox_read_reg(p->irqstatus);

	return (enable & status & bit);
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
};

/*
 * MAILBOX 0: ARM -> DSP,
 * MAILBOX 1: ARM <- DSP.
 * MAILBOX 2: ARM -> IVA,
 * MAILBOX 3: ARM <- IVA.
 */

/* FIXME: the following structs should be filled automatically by the user id */

/* DSP */
static struct omap_mbox2_priv omap2_mbox_dsp_priv = {
	.tx_fifo = {
		.msg		= MAILBOX_MESSAGE_0,
		.fifo_stat	= MAILBOX_FIFOSTATUS_0,
	},
	.rx_fifo = {
		.msg		= MAILBOX_MESSAGE_1,
		.msg_stat	= MAILBOX_MSGSTATUS_1,
	},
	.irqenable	= MAILBOX_IRQENABLE_0,
	.irqstatus	= MAILBOX_IRQSTATUS_0,
	.notfull_bit	= MAILBOX_IRQ_NOTFULL(0),
	.newmsg_bit	= MAILBOX_IRQ_NEWMSG(1),
};

struct omap_mbox mbox_dsp_info = {
	.name	= "dsp",
	.ops	= &omap2_mbox_ops,
	.priv	= &omap2_mbox_dsp_priv,
};
EXPORT_SYMBOL(mbox_dsp_info);

/* IVA */
static struct omap_mbox2_priv omap2_mbox_iva_priv = {
	.tx_fifo = {
		.msg		= MAILBOX_MESSAGE_2,
		.fifo_stat	= MAILBOX_FIFOSTATUS_2,
	},
	.rx_fifo = {
		.msg		= MAILBOX_MESSAGE_3,
		.msg_stat	= MAILBOX_MSGSTATUS_3,
	},
	.irqenable	= MAILBOX_IRQENABLE_3,
	.irqstatus	= MAILBOX_IRQSTATUS_3,
	.notfull_bit	= MAILBOX_IRQ_NOTFULL(2),
	.newmsg_bit	= MAILBOX_IRQ_NEWMSG(3),
};

static struct omap_mbox mbox_iva_info = {
	.name	= "iva",
	.ops	= &omap2_mbox_ops,
	.priv	= &omap2_mbox_iva_priv,
};

static int __init omap2_mbox_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret = 0;

	if (pdev->num_resources != 3) {
		dev_err(&pdev->dev, "invalid number of resources: %d\n",
			pdev->num_resources);
		return -ENODEV;
	}

	/* MBOX base */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!res)) {
		dev_err(&pdev->dev, "invalid mem resource\n");
		return -ENODEV;
	}
	mbox_base = res->start;

	/* DSP IRQ */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (unlikely(!res)) {
		dev_err(&pdev->dev, "invalid irq resource\n");
		return -ENODEV;
	}
	mbox_dsp_info.irq = res->start;

	ret = omap_mbox_register(&mbox_dsp_info);

	/* IVA IRQ */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	if (unlikely(!res)) {
		dev_err(&pdev->dev, "invalid irq resource\n");
		return -ENODEV;
	}
	mbox_iva_info.irq = res->start;

	ret = omap_mbox_register(&mbox_iva_info);

	return ret;
}

static int omap2_mbox_remove(struct platform_device *pdev)
{
	omap_mbox_unregister(&mbox_dsp_info);
	return 0;
}

static struct platform_driver omap2_mbox_driver = {
	.probe = omap2_mbox_probe,
	.remove = omap2_mbox_remove,
	.driver = {
		.name = "mailbox",
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

MODULE_LICENSE("GPL");

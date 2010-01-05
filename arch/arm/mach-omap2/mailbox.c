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

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <plat/mailbox.h>
#include <mach/irqs.h>

#define DRV_NAME "omap2-mailbox"

#define MAILBOX_REVISION		0x000
#define MAILBOX_SYSCONFIG		0x010
#define MAILBOX_SYSSTATUS		0x014
#define MAILBOX_MESSAGE(m)		(0x040 + 4 * (m))
#define MAILBOX_FIFOSTATUS(m)		(0x080 + 4 * (m))
#define MAILBOX_MSGSTATUS(m)		(0x0c0 + 4 * (m))
#define MAILBOX_IRQSTATUS(u)		(0x100 + 8 * (u))
#define MAILBOX_IRQENABLE(u)		(0x104 + 8 * (u))

#define OMAP4_MAILBOX_IRQSTATUS(u)	(0x104 + 10 * (u))
#define OMAP4_MAILBOX_IRQENABLE(u)	(0x108 + 10 * (u))
#define OMAP4_MAILBOX_IRQENABLE_CLR(u)	(0x10c + 10 * (u))

#define MAILBOX_IRQ_NEWMSG(m)		(1 << (2 * (m)))
#define MAILBOX_IRQ_NOTFULL(m)		(1 << (2 * (m) + 1))

/* SYSCONFIG: register bit definition */
#define AUTOIDLE	(1 << 0)
#define SOFTRESET	(1 << 1)
#define SMARTIDLE	(2 << 3)

/* SYSSTATUS: register bit definition */
#define RESETDONE	(1 << 0)

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
};

static struct clk *mbox_ick_handle;

static void omap2_mbox_enable_irq(struct omap_mbox *mbox,
				  omap_mbox_type_t irq);

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
	unsigned long timeout;

	mbox_ick_handle = clk_get(NULL, "mailboxes_ick");
	if (IS_ERR(mbox_ick_handle)) {
		printk(KERN_ERR "Could not get mailboxes_ick: %d\n",
			PTR_ERR(mbox_ick_handle));
		return PTR_ERR(mbox_ick_handle);
	}
	clk_enable(mbox_ick_handle);

	mbox_write_reg(SOFTRESET, MAILBOX_SYSCONFIG);
	timeout = jiffies + msecs_to_jiffies(20);
	do {
		l = mbox_read_reg(MAILBOX_SYSSTATUS);
		if (l & RESETDONE)
			break;
	} while (!time_after(jiffies, timeout));

	if (!(l & RESETDONE)) {
		pr_err("Can't take mmu out of reset\n");
		return -ENODEV;
	}

	l = mbox_read_reg(MAILBOX_REVISION);
	pr_info("omap mailbox rev %d.%d\n", (l & 0xf0) >> 4, (l & 0x0f));

	l = SMARTIDLE | AUTOIDLE;
	mbox_write_reg(l, MAILBOX_SYSCONFIG);

	omap2_mbox_enable_irq(mbox, IRQ_RX);

	return 0;
}

static void omap2_mbox_shutdown(struct omap_mbox *mbox)
{
	clk_disable(mbox_ick_handle);
	clk_put(mbox_ick_handle);
	mbox_ick_handle = NULL;
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
	l = mbox_read_reg(p->irqdisable);
	l &= ~bit;
	mbox_write_reg(l, p->irqdisable);
}

static void omap2_mbox_ack_irq(struct omap_mbox *mbox,
		omap_mbox_type_t irq)
{
	struct omap_mbox2_priv *p = (struct omap_mbox2_priv *)mbox->priv;
	u32 bit = (irq == IRQ_TX) ? p->notfull_bit : p->newmsg_bit;

	mbox_write_reg(bit, p->irqstatus);

	/* Flush posted write for irq status to avoid spurious interrupts */
	mbox_read_reg(p->irqstatus);
}

static int omap2_mbox_is_irq(struct omap_mbox *mbox,
		omap_mbox_type_t irq)
{
	struct omap_mbox2_priv *p = (struct omap_mbox2_priv *)mbox->priv;
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
	if (cpu_is_omap44xx())
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
	if (cpu_is_omap44xx())
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
		.msg		= MAILBOX_MESSAGE(0),
		.fifo_stat	= MAILBOX_FIFOSTATUS(0),
	},
	.rx_fifo = {
		.msg		= MAILBOX_MESSAGE(1),
		.msg_stat	= MAILBOX_MSGSTATUS(1),
	},
	.irqenable	= MAILBOX_IRQENABLE(0),
	.irqstatus	= MAILBOX_IRQSTATUS(0),
	.notfull_bit	= MAILBOX_IRQ_NOTFULL(0),
	.newmsg_bit	= MAILBOX_IRQ_NEWMSG(1),
	.irqdisable	= MAILBOX_IRQENABLE(0),
};



/* OMAP4 specific data structure. Use the cpu_is_omap4xxx()
to use this*/
static struct omap_mbox2_priv omap2_mbox_1_priv = {
	.tx_fifo = {
		.msg		= MAILBOX_MESSAGE(0),
		.fifo_stat	= MAILBOX_FIFOSTATUS(0),
	},
	.rx_fifo = {
		.msg		= MAILBOX_MESSAGE(1),
		.msg_stat	= MAILBOX_MSGSTATUS(1),
	},
	.irqenable	= OMAP4_MAILBOX_IRQENABLE(0),
	.irqstatus	= OMAP4_MAILBOX_IRQSTATUS(0),
	.notfull_bit	= MAILBOX_IRQ_NOTFULL(0),
	.newmsg_bit	= MAILBOX_IRQ_NEWMSG(1),
	.irqdisable	= OMAP4_MAILBOX_IRQENABLE_CLR(0),
};

struct omap_mbox mbox_1_info = {
	.name	= "mailbox-1",
	.ops	= &omap2_mbox_ops,
	.priv	= &omap2_mbox_1_priv,
};
EXPORT_SYMBOL(mbox_1_info);

struct omap_mbox mbox_dsp_info = {
	.name	= "dsp",
	.ops	= &omap2_mbox_ops,
	.priv	= &omap2_mbox_dsp_priv,
};
EXPORT_SYMBOL(mbox_dsp_info);

static struct omap_mbox2_priv omap2_mbox_2_priv = {
	.tx_fifo = {
		.msg		= MAILBOX_MESSAGE(3),
		.fifo_stat	= MAILBOX_FIFOSTATUS(3),
	},
	.rx_fifo = {
		.msg		= MAILBOX_MESSAGE(2),
		.msg_stat	= MAILBOX_MSGSTATUS(2),
	},
	.irqenable	= OMAP4_MAILBOX_IRQENABLE(0),
	.irqstatus	= OMAP4_MAILBOX_IRQSTATUS(0),
	.notfull_bit	= MAILBOX_IRQ_NOTFULL(3),
	.newmsg_bit	= MAILBOX_IRQ_NEWMSG(2),
	.irqdisable     = OMAP4_MAILBOX_IRQENABLE_CLR(0),
};

struct omap_mbox mbox_2_info = {
	.name	= "mailbox-2",
	.ops	= &omap2_mbox_ops,
	.priv	= &omap2_mbox_2_priv,
};
EXPORT_SYMBOL(mbox_2_info);


#if defined(CONFIG_ARCH_OMAP2420) /* IVA */
static struct omap_mbox2_priv omap2_mbox_iva_priv = {
	.tx_fifo = {
		.msg		= MAILBOX_MESSAGE(2),
		.fifo_stat	= MAILBOX_FIFOSTATUS(2),
	},
	.rx_fifo = {
		.msg		= MAILBOX_MESSAGE(3),
		.msg_stat	= MAILBOX_MSGSTATUS(3),
	},
	.irqenable	= MAILBOX_IRQENABLE(3),
	.irqstatus	= MAILBOX_IRQSTATUS(3),
	.notfull_bit	= MAILBOX_IRQ_NOTFULL(2),
	.newmsg_bit	= MAILBOX_IRQ_NEWMSG(3),
	.irqdisable	= MAILBOX_IRQENABLE(3),
};

static struct omap_mbox mbox_iva_info = {
	.name	= "iva",
	.ops	= &omap2_mbox_ops,
	.priv	= &omap2_mbox_iva_priv,
};
#endif

static int __devinit omap2_mbox_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret;

	/* MBOX base */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!res)) {
		dev_err(&pdev->dev, "invalid mem resource\n");
		return -ENODEV;
	}
	mbox_base = ioremap(res->start, resource_size(res));
	if (!mbox_base)
		return -ENOMEM;

	/* DSP or IVA2 IRQ */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);

	if (unlikely(!res)) {
		dev_err(&pdev->dev, "invalid irq resource\n");
		ret = -ENODEV;
		goto err_dsp;
	}
	if (cpu_is_omap44xx()) {
		mbox_1_info.irq = res->start;
		ret = omap_mbox_register(&pdev->dev, &mbox_1_info);
	} else {
		mbox_dsp_info.irq = res->start;
		ret = omap_mbox_register(&pdev->dev, &mbox_dsp_info);
	}
	if (ret)
		goto err_dsp;

	if (cpu_is_omap44xx()) {
		mbox_2_info.irq = res->start;
		ret = omap_mbox_register(&pdev->dev, &mbox_2_info);
		if (ret) {
			omap_mbox_unregister(&mbox_1_info);
			goto err_dsp;
		}
	}
#if defined(CONFIG_ARCH_OMAP2420) /* IVA */
	if (cpu_is_omap2420()) {
		/* IVA IRQ */
		res = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
		if (unlikely(!res)) {
			dev_err(&pdev->dev, "invalid irq resource\n");
			ret = -ENODEV;
			goto err_iva1;
		}
		mbox_iva_info.irq = res->start;
		ret = omap_mbox_register(&pdev->dev, &mbox_iva_info);
		if (ret)
			goto err_iva1;
	}
#endif
	return 0;

err_iva1:
	omap_mbox_unregister(&mbox_dsp_info);

err_dsp:
	iounmap(mbox_base);
	return ret;
}

static int __devexit omap2_mbox_remove(struct platform_device *pdev)
{
#if defined(CONFIG_ARCH_OMAP2420)
	omap_mbox_unregister(&mbox_iva_info);
#endif

	if (cpu_is_omap44xx()) {
		omap_mbox_unregister(&mbox_2_info);
		omap_mbox_unregister(&mbox_1_info);
	} else
		omap_mbox_unregister(&mbox_dsp_info);
	iounmap(mbox_base);
	return 0;
}

static struct platform_driver omap2_mbox_driver = {
	.probe = omap2_mbox_probe,
	.remove = __devexit_p(omap2_mbox_remove),
	.driver = {
		.name = DRV_NAME,
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
MODULE_AUTHOR("Hiroshi DOYU <Hiroshi.DOYU@nokia.com>, Paul Mundt");
MODULE_ALIAS("platform:"DRV_NAME);

// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 *   Copyright (C) 2011 John Crispin <john@phrozen.org>
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/of.h>

#include <lantiq_soc.h>
#include <xway_dma.h>

#define LTQ_DMA_ID		0x08
#define LTQ_DMA_CTRL		0x10
#define LTQ_DMA_CPOLL		0x14
#define LTQ_DMA_CS		0x18
#define LTQ_DMA_CCTRL		0x1C
#define LTQ_DMA_CDBA		0x20
#define LTQ_DMA_CDLEN		0x24
#define LTQ_DMA_CIS		0x28
#define LTQ_DMA_CIE		0x2C
#define LTQ_DMA_PS		0x40
#define LTQ_DMA_PCTRL		0x44
#define LTQ_DMA_IRNEN		0xf4

#define DMA_ID_CHNR		GENMASK(26, 20)	/* channel number */
#define DMA_DESCPT		BIT(3)		/* descriptor complete irq */
#define DMA_TX			BIT(8)		/* TX channel direction */
#define DMA_CHAN_ON		BIT(0)		/* channel on / off bit */
#define DMA_PDEN		BIT(6)		/* enable packet drop */
#define DMA_CHAN_RST		BIT(1)		/* channel on / off bit */
#define DMA_RESET		BIT(0)		/* channel on / off bit */
#define DMA_IRQ_ACK		0x7e		/* IRQ status register */
#define DMA_POLL		BIT(31)		/* turn on channel polling */
#define DMA_CLK_DIV4		BIT(6)		/* polling clock divider */
#define DMA_2W_BURST		BIT(1)		/* 2 word burst length */
#define DMA_ETOP_ENDIANNESS	(0xf << 8) /* endianness swap etop channels */
#define DMA_WEIGHT	(BIT(17) | BIT(16))	/* default channel wheight */

#define ltq_dma_r32(x)			ltq_r32(ltq_dma_membase + (x))
#define ltq_dma_w32(x, y)		ltq_w32(x, ltq_dma_membase + (y))
#define ltq_dma_w32_mask(x, y, z)	ltq_w32_mask(x, y, \
						ltq_dma_membase + (z))

static void __iomem *ltq_dma_membase;
static DEFINE_SPINLOCK(ltq_dma_lock);

void
ltq_dma_enable_irq(struct ltq_dma_channel *ch)
{
	unsigned long flags;

	spin_lock_irqsave(&ltq_dma_lock, flags);
	ltq_dma_w32(ch->nr, LTQ_DMA_CS);
	ltq_dma_w32_mask(0, 1 << ch->nr, LTQ_DMA_IRNEN);
	spin_unlock_irqrestore(&ltq_dma_lock, flags);
}
EXPORT_SYMBOL_GPL(ltq_dma_enable_irq);

void
ltq_dma_disable_irq(struct ltq_dma_channel *ch)
{
	unsigned long flags;

	spin_lock_irqsave(&ltq_dma_lock, flags);
	ltq_dma_w32(ch->nr, LTQ_DMA_CS);
	ltq_dma_w32_mask(1 << ch->nr, 0, LTQ_DMA_IRNEN);
	spin_unlock_irqrestore(&ltq_dma_lock, flags);
}
EXPORT_SYMBOL_GPL(ltq_dma_disable_irq);

void
ltq_dma_ack_irq(struct ltq_dma_channel *ch)
{
	unsigned long flags;

	spin_lock_irqsave(&ltq_dma_lock, flags);
	ltq_dma_w32(ch->nr, LTQ_DMA_CS);
	ltq_dma_w32(DMA_IRQ_ACK, LTQ_DMA_CIS);
	spin_unlock_irqrestore(&ltq_dma_lock, flags);
}
EXPORT_SYMBOL_GPL(ltq_dma_ack_irq);

void
ltq_dma_open(struct ltq_dma_channel *ch)
{
	unsigned long flag;

	spin_lock_irqsave(&ltq_dma_lock, flag);
	ltq_dma_w32(ch->nr, LTQ_DMA_CS);
	ltq_dma_w32_mask(0, DMA_CHAN_ON, LTQ_DMA_CCTRL);
	spin_unlock_irqrestore(&ltq_dma_lock, flag);
}
EXPORT_SYMBOL_GPL(ltq_dma_open);

void
ltq_dma_close(struct ltq_dma_channel *ch)
{
	unsigned long flag;

	spin_lock_irqsave(&ltq_dma_lock, flag);
	ltq_dma_w32(ch->nr, LTQ_DMA_CS);
	ltq_dma_w32_mask(DMA_CHAN_ON, 0, LTQ_DMA_CCTRL);
	ltq_dma_w32_mask(1 << ch->nr, 0, LTQ_DMA_IRNEN);
	spin_unlock_irqrestore(&ltq_dma_lock, flag);
}
EXPORT_SYMBOL_GPL(ltq_dma_close);

static void
ltq_dma_alloc(struct ltq_dma_channel *ch)
{
	unsigned long flags;

	ch->desc = 0;
	ch->desc_base = dma_alloc_coherent(ch->dev,
					   LTQ_DESC_NUM * LTQ_DESC_SIZE,
					   &ch->phys, GFP_ATOMIC);

	spin_lock_irqsave(&ltq_dma_lock, flags);
	ltq_dma_w32(ch->nr, LTQ_DMA_CS);
	ltq_dma_w32(ch->phys, LTQ_DMA_CDBA);
	ltq_dma_w32(LTQ_DESC_NUM, LTQ_DMA_CDLEN);
	ltq_dma_w32_mask(DMA_CHAN_ON, 0, LTQ_DMA_CCTRL);
	wmb();
	ltq_dma_w32_mask(0, DMA_CHAN_RST, LTQ_DMA_CCTRL);
	while (ltq_dma_r32(LTQ_DMA_CCTRL) & DMA_CHAN_RST)
		;
	spin_unlock_irqrestore(&ltq_dma_lock, flags);
}

void
ltq_dma_alloc_tx(struct ltq_dma_channel *ch)
{
	unsigned long flags;

	ltq_dma_alloc(ch);

	spin_lock_irqsave(&ltq_dma_lock, flags);
	ltq_dma_w32(DMA_DESCPT, LTQ_DMA_CIE);
	ltq_dma_w32_mask(0, 1 << ch->nr, LTQ_DMA_IRNEN);
	ltq_dma_w32(DMA_WEIGHT | DMA_TX, LTQ_DMA_CCTRL);
	spin_unlock_irqrestore(&ltq_dma_lock, flags);
}
EXPORT_SYMBOL_GPL(ltq_dma_alloc_tx);

void
ltq_dma_alloc_rx(struct ltq_dma_channel *ch)
{
	unsigned long flags;

	ltq_dma_alloc(ch);

	spin_lock_irqsave(&ltq_dma_lock, flags);
	ltq_dma_w32(DMA_DESCPT, LTQ_DMA_CIE);
	ltq_dma_w32_mask(0, 1 << ch->nr, LTQ_DMA_IRNEN);
	ltq_dma_w32(DMA_WEIGHT, LTQ_DMA_CCTRL);
	spin_unlock_irqrestore(&ltq_dma_lock, flags);
}
EXPORT_SYMBOL_GPL(ltq_dma_alloc_rx);

void
ltq_dma_free(struct ltq_dma_channel *ch)
{
	if (!ch->desc_base)
		return;
	ltq_dma_close(ch);
	dma_free_coherent(ch->dev, LTQ_DESC_NUM * LTQ_DESC_SIZE,
		ch->desc_base, ch->phys);
}
EXPORT_SYMBOL_GPL(ltq_dma_free);

void
ltq_dma_init_port(int p)
{
	ltq_dma_w32(p, LTQ_DMA_PS);
	switch (p) {
	case DMA_PORT_ETOP:
		/*
		 * Tell the DMA engine to swap the endianness of data frames and
		 * drop packets if the channel arbitration fails.
		 */
		ltq_dma_w32_mask(0, DMA_ETOP_ENDIANNESS | DMA_PDEN,
			LTQ_DMA_PCTRL);
		break;

	case DMA_PORT_DEU:
		ltq_dma_w32((DMA_2W_BURST << 4) | (DMA_2W_BURST << 2),
			LTQ_DMA_PCTRL);
		break;

	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(ltq_dma_init_port);

static int
ltq_dma_init(struct platform_device *pdev)
{
	struct clk *clk;
	struct resource *res;
	unsigned int id, nchannels;
	int i;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ltq_dma_membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ltq_dma_membase))
		panic("Failed to remap dma resource");

	/* power up and reset the dma engine */
	clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk))
		panic("Failed to get dma clock");

	clk_enable(clk);
	ltq_dma_w32_mask(0, DMA_RESET, LTQ_DMA_CTRL);

	usleep_range(1, 10);

	/* disable all interrupts */
	ltq_dma_w32(0, LTQ_DMA_IRNEN);

	/* reset/configure each channel */
	id = ltq_dma_r32(LTQ_DMA_ID);
	nchannels = ((id & DMA_ID_CHNR) >> 20);
	for (i = 0; i < nchannels; i++) {
		ltq_dma_w32(i, LTQ_DMA_CS);
		ltq_dma_w32(DMA_CHAN_RST, LTQ_DMA_CCTRL);
		ltq_dma_w32(DMA_POLL | DMA_CLK_DIV4, LTQ_DMA_CPOLL);
		ltq_dma_w32_mask(DMA_CHAN_ON, 0, LTQ_DMA_CCTRL);
	}

	dev_info(&pdev->dev,
		"Init done - hw rev: %X, ports: %d, channels: %d\n",
		id & 0x1f, (id >> 16) & 0xf, nchannels);

	return 0;
}

static const struct of_device_id dma_match[] = {
	{ .compatible = "lantiq,dma-xway" },
	{},
};

static struct platform_driver dma_driver = {
	.probe = ltq_dma_init,
	.driver = {
		.name = "dma-xway",
		.of_match_table = dma_match,
	},
};

int __init
dma_init(void)
{
	return platform_driver_register(&dma_driver);
}

postcore_initcall(dma_init);

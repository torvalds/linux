// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) ASPEED Technology Inc.
 */
#include <linux/delay.h>
#include <linux/bitfield.h>
#include <linux/interrupt.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/dma-direct.h>
#include "dmaengine.h"

#define DEVICE_NAME	"aspeed-udma"

/* register offset */
#define UDMA_TX_EN		0x000
#define UDMA_RX_EN		0x004
#define UDMA_TMOUT		0x00c
#define UDMA_TX_PTR_RST		0x020
#define UDMA_RX_PTR_RST		0x024
#define UDMA_TX_INT_EN		0x030
#define UDMA_TX_INT_STS		0x034
#define UDMA_RX_INT_EN		0x038
#define UDMA_RX_INT_STS		0x03c

#define UDMA_CH_OFFS(x)		((x) * 0x10)
#define UDMA_CH_RPTR(x)		(0x040 + UDMA_CH_OFFS(x))
#define UDMA_CH_WPTR(x)		(0x044 + UDMA_CH_OFFS(x))
#define UDMA_CH_ADDR(x)		(0x048 + UDMA_CH_OFFS(x))
#define UDMA_CH_CTRL(x)		(0x04c + UDMA_CH_OFFS(x))
#define   UDMA_CH_CTRL_BUFSZ	GENMASK(1, 0)

#define UDMA_MAX_BUFSZ	(0x10000)
#define UDMA_MAX_TXSZ	(UDMA_MAX_BUFSZ - 1)

enum ast2600_udma_bufsz {
	UDMA_BUFSZ_1KB,
	UDMA_BUFSZ_4KB,
	UDMA_BUFSZ_16KB,
	UDMA_BUFSZ_64KB,
};

struct ast2600_udma_desc {
	struct dma_async_tx_descriptor tx;
	dma_addr_t addr;
	unsigned int size;
};

struct ast2600_udma_chan {
	struct dma_chan chan;
	struct ast2600_udma_desc ud;
	struct ast2600_udma *udma;
	uint32_t residue;

	/* 4B-aligned local buffer for workaround */
	uint8_t *buf;
	dma_addr_t buf_addr;

	bool is_tx;
};

struct ast2600_udma {
	struct dma_device ddev;
	uint8_t __iomem *regs;
	int irq;
	struct ast2600_udma_chan *ucs;
	uint32_t n_ucs;
	spinlock_t lock;
};

static struct ast2600_udma_chan *to_ast2600_udma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct ast2600_udma_chan, chan);
}

static int ast2600_udma_alloc_chan_resources(struct dma_chan *chan)
{
	dma_cookie_init(chan);

	return 0;
}

static dma_cookie_t ast2600_udma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	return dma_cookie_assign(tx);
}

/* consider only 8250_dma using dmaengine_prep_slave_single() */
static struct dma_async_tx_descriptor *ast2600_udma_prep_slave_sg(struct dma_chan *chan,
								  struct scatterlist *sgl,
								  unsigned int sg_len,
								  enum dma_transfer_direction dir,
								  unsigned long tx_flags,
								  void *context)
{
	void *p;
	struct device *dev = chan->device->dev;
	struct ast2600_udma_chan *uc = to_ast2600_udma_chan(chan);
	struct ast2600_udma_desc *ud = &uc->ud;

	if (!is_slave_direction(dir)) {
		dev_err(dev, "direction is not slave mode\n");
		return NULL;
	}

	if (sg_len != 1) {
		dev_err(dev, "scatter list length is not 1\n");
		return NULL;
	}

	if (uc->is_tx && dir != DMA_MEM_TO_DEV) {
		dev_err(dev, "invalid direction to TX channel\n");
		return NULL;
	}

	ud->addr = sg_dma_address(sgl);
	ud->size = sg_dma_len(sgl);

	if (uc->is_tx) {
		if (ud->size > UDMA_MAX_TXSZ) {
			dev_err(dev, "invalid TX DMA SIZE");
			return NULL;
		}

		/*
		 * UDMA is limited to 4B-aligned DMA addresses.
		 * Thus copy data to local 4B-aligned buffer if
		 * the source does not fit.
		 */
		if (ud->addr & 0x3) {
			p = phys_to_virt(dma_to_phys(chan->device->dev, ud->addr));
			memcpy(uc->buf, p, ud->size);
			ud->addr = uc->buf_addr;
		}
	} else {
		/*
		 * UDMA RX buffer size is limited to 1/4/16/64 KB
		 * We use the lower bits to encode the buffer size
		 */
		switch (ud->size) {
		case 0x400:
			ud->size |= FIELD_PREP(UDMA_CH_CTRL_BUFSZ, UDMA_BUFSZ_1KB);
			break;
		case 0x1000:
			ud->size |= FIELD_PREP(UDMA_CH_CTRL_BUFSZ, UDMA_BUFSZ_4KB);
			break;
		case 0x4000:
			ud->size |= FIELD_PREP(UDMA_CH_CTRL_BUFSZ, UDMA_BUFSZ_16KB);
			break;
		case 0x10000:
			ud->size |= FIELD_PREP(UDMA_CH_CTRL_BUFSZ, UDMA_BUFSZ_64KB);
			break;
		default:
			dev_err(dev, "invalid RX DMA size\n");
			return NULL;
		}
	}

	dma_async_tx_descriptor_init(&ud->tx, &uc->chan);
	ud->tx.tx_submit = ast2600_udma_tx_submit;

	return &ud->tx;
}

static void ast2600_udma_issue_pending(struct dma_chan *chan)
{
	unsigned long flags;
	uint32_t r_pr, r_is, r_ie, r_en, reg;
	uint32_t ch_id = chan->chan_id;
	uint32_t ch_bit = ch_id / 2;
	dma_addr_t rx_addr;
	uint32_t rx_size;
	struct ast2600_udma_chan *uc = to_ast2600_udma_chan(chan);
	struct ast2600_udma_desc *ud = &uc->ud;
	struct ast2600_udma *udma = uc->udma;

	if (uc->is_tx) {
		r_pr = UDMA_TX_PTR_RST;
		r_is = UDMA_TX_INT_STS;
		r_ie = UDMA_TX_INT_EN;
		r_en = UDMA_TX_EN;
	} else {
		r_pr = UDMA_RX_PTR_RST;
		r_is = UDMA_RX_INT_STS;
		r_ie = UDMA_RX_INT_EN;
		r_en = UDMA_RX_EN;
	}

	spin_lock_irqsave(&udma->lock, flags);

	/* reset channel HW read/write pointer */
	writel(BIT(ch_bit), udma->regs + r_pr);
	writel(0, udma->regs + r_pr);

	/* clear interrupt status */
	writel(BIT(ch_bit), udma->regs + r_is);

	/* set transfer address & size */
	if (uc->is_tx) {
		writel(ud->addr, udma->regs + UDMA_CH_ADDR(ch_id));
		writel(ud->size, udma->regs + UDMA_CH_WPTR(ch_id));
		writel(UDMA_BUFSZ_64KB, udma->regs + UDMA_CH_CTRL(ch_id));
	} else {
		/*
		 * UDMA is limited to 4B-aligned addresses.
		 * Thus use local 4B-aligned buffer to get
		 * RX data and copy to the real destination
		 * after then.
		 */
		rx_addr = (ud->addr & 0x3) ? uc->buf_addr : ud->addr;
		rx_size = FIELD_GET(UDMA_CH_CTRL_BUFSZ, ud->size);
		writel(rx_addr, udma->regs + UDMA_CH_ADDR(ch_id));
		writel(rx_size, udma->regs + UDMA_CH_CTRL(ch_id));
	}

	/* enable interrupt */
	reg = readl(udma->regs + r_ie) | BIT(ch_bit);
	writel(reg, udma->regs + r_ie);

	/* start DMA */
	reg = readl(udma->regs + r_en) | BIT(ch_bit);
	writel(reg, udma->regs + r_en);

	spin_unlock_irqrestore(&udma->lock, flags);
}

static enum dma_status ast2600_udma_tx_status(struct dma_chan *chan,
		dma_cookie_t cookie, struct dma_tx_state *txstate)
{
	struct ast2600_udma_chan *uc = to_ast2600_udma_chan(chan);
	enum dma_status sts = dma_cookie_status(chan, cookie, txstate);

	dma_set_residue(txstate, uc->residue);

	return sts;
}

static int ast2600_udma_pause(struct dma_chan *chan)
{
	unsigned long flags;
	uint32_t r_en, r_ie, reg;
	uint32_t ch_id = chan->chan_id;
	uint32_t ch_bit = ch_id / 2;
	struct ast2600_udma_chan *uc = to_ast2600_udma_chan(chan);
	struct ast2600_udma *udma = uc->udma;

	if (uc->is_tx) {
		r_en = UDMA_TX_EN;
		r_ie = UDMA_TX_INT_EN;
	} else {
		r_en = UDMA_RX_EN;
		r_ie = UDMA_RX_INT_EN;
	}

	spin_lock_irqsave(&udma->lock, flags);

	reg = readl(udma->regs + r_en) & ~BIT(ch_bit);
	writel(reg, udma->regs + r_en);

	reg = readl(udma->regs + r_ie) & ~BIT(ch_bit);
	writel(reg, udma->regs + r_ie);

	spin_unlock_irqrestore(&udma->lock, flags);

	return 0;
}

static int ast2600_udma_resume(struct dma_chan *chan)
{
	unsigned long flags;
	uint32_t r_en, r_ie, reg;
	uint32_t ch_id = chan->chan_id;
	uint32_t ch_bit = ch_id / 2;
	struct ast2600_udma_chan *uc = to_ast2600_udma_chan(chan);
	struct ast2600_udma *udma = uc->udma;

	if (uc->is_tx) {
		r_en = UDMA_TX_EN;
		r_ie = UDMA_TX_INT_EN;
	} else {
		r_en = UDMA_RX_EN;
		r_ie = UDMA_RX_INT_EN;
	}

	spin_lock_irqsave(&udma->lock, flags);

	reg = readl(udma->regs + r_en) | BIT(ch_bit);
	writel(reg, udma->regs + r_en);

	reg = readl(udma->regs + r_ie) | BIT(ch_bit);
	writel(reg, udma->regs + r_ie);

	spin_unlock_irqrestore(&udma->lock, flags);

	return 0;
}

static int ast2600_udma_terminate(struct dma_chan *chan)
{
	unsigned long flags;
	uint32_t r_pr, r_is, r_ie, r_en, reg;
	uint32_t ch_id = chan->chan_id;
	uint32_t ch_bit = ch_id / 2;
	struct ast2600_udma_chan *uc = to_ast2600_udma_chan(chan);
	struct ast2600_udma *udma = uc->udma;

	if (uc->is_tx) {
		r_pr = UDMA_TX_PTR_RST;
		r_is = UDMA_TX_INT_STS;
		r_ie = UDMA_TX_INT_EN;
		r_en = UDMA_TX_EN;
	} else {
		r_pr = UDMA_RX_PTR_RST;
		r_is = UDMA_RX_INT_STS;
		r_ie = UDMA_RX_INT_EN;
		r_en = UDMA_RX_EN;
	}

	spin_lock_irqsave(&udma->lock, flags);

	/* disable DMA */
	reg = readl(udma->regs + r_en) & ~BIT(ch_bit);
	writel(reg, udma->regs + r_en);

	/* disable interrupt */
	reg = readl(udma->regs + r_ie) & ~BIT(ch_bit);
	writel(reg, udma->regs + r_ie);

	/* clear interrupt status */
	writel(BIT(ch_bit), udma->regs + r_is);

	/* reset channel HW read/write pointer */
	writel(BIT(ch_bit), udma->regs + r_pr);
	writel(0, udma->regs + r_pr);

	spin_unlock_irqrestore(&udma->lock, flags);

	return 0;
}

static irqreturn_t ast2600_udma_isr(int irq, void *arg)
{
	struct ast2600_udma *udma = arg;
	struct ast2600_udma_chan *uc;
	struct ast2600_udma_desc *ud;
	struct dma_async_tx_descriptor *tx;
	uint32_t sts, rptr, wptr;
	uint32_t ch_id, ch_bit;
	void *p;

	/* handle TX interrupt */
	sts = readl(udma->regs + UDMA_TX_INT_STS);
	for_each_set_bit(ch_bit, (unsigned long *)&sts, (udma->n_ucs / 2)) {
		ch_id = ch_bit << 1;
		rptr = readl(udma->regs + UDMA_CH_RPTR(ch_id));
		wptr = readl(udma->regs + UDMA_CH_WPTR(ch_id));

		uc = &udma->ucs[ch_id];
		uc->residue = wptr - rptr;

		ast2600_udma_terminate(&uc->chan);

		tx = &uc->ud.tx;
		dma_cookie_complete(tx);
		dma_descriptor_unmap(tx);
		dmaengine_desc_get_callback_invoke(tx, NULL);
	}

	/* handle RX interrupt */
	sts = readl(udma->regs + UDMA_RX_INT_STS);
	for_each_set_bit(ch_bit, (unsigned long *)&sts, udma->n_ucs / 2) {
		ch_id = (ch_bit << 1) + 1;
		wptr = readl(udma->regs + UDMA_CH_WPTR(ch_id));

		uc = &udma->ucs[ch_id];
		ud = &uc->ud;
		tx = &ud->tx;

		uc->residue = (ud->size & ~UDMA_CH_CTRL_BUFSZ) - wptr;

		/* handle non-4B-aligned case */
		if (ud->addr & 0x3) {
			p = phys_to_virt(dma_to_phys(uc->chan.device->dev, ud->addr));
			memcpy(p, uc->buf, wptr);
		}

		ast2600_udma_terminate(&uc->chan);

		dma_cookie_complete(tx);
		dma_descriptor_unmap(tx);
		dmaengine_desc_get_callback_invoke(tx, NULL);
	}

	return IRQ_HANDLED;
}

static int ast2600_udma_probe(struct platform_device *pdev)
{
	int i, rc;
	struct resource *res;
	struct ast2600_udma *udma;
	struct device *dev = &pdev->dev;

	udma = devm_kzalloc(dev, sizeof(*udma), GFP_KERNEL);
	if (!udma)
		return -ENOMEM;

	dma_cap_set(DMA_SLAVE, udma->ddev.cap_mask);
	udma->ddev.device_alloc_chan_resources = ast2600_udma_alloc_chan_resources;
	udma->ddev.device_prep_slave_sg = ast2600_udma_prep_slave_sg;
	udma->ddev.device_issue_pending = ast2600_udma_issue_pending;
	udma->ddev.device_tx_status = ast2600_udma_tx_status;
	udma->ddev.device_pause = ast2600_udma_pause;
	udma->ddev.device_resume = ast2600_udma_resume;
	udma->ddev.device_terminate_all = ast2600_udma_terminate;
	udma->ddev.src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE);
	udma->ddev.dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE);
	udma->ddev.directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	udma->ddev.residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;
	udma->ddev.dev = dev;
	INIT_LIST_HEAD(&udma->ddev.channels);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (IS_ERR(res)) {
		dev_err(dev, "cannot get IO resource\n");
		return -ENODEV;
	}

	udma->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(udma->regs)) {
		dev_err(dev, "cannot map IO registers\n");
		return PTR_ERR(udma->regs);
	}

	/* timeout value: 0x200 * (PCLK * 14400) */
	writel(0x200, udma->regs + UDMA_TMOUT);

	/* disable all for safety */
	writel(0x0, udma->regs + UDMA_TX_EN);
	writel(0x0, udma->regs + UDMA_RX_EN);

	udma->irq = platform_get_irq(pdev, 0);
	if (udma->irq < 0)
		return udma->irq;

	rc = devm_request_irq(&pdev->dev, udma->irq, ast2600_udma_isr,
			      IRQF_SHARED, DEVICE_NAME, udma);
	if (rc) {
		dev_err(dev, "cannot request IRQ\n");
		return rc;
	}

	rc = of_property_read_u32(dev->of_node, "dma-channels", &udma->n_ucs);
	if (rc) {
		dev_err(dev, "cannot find number of channels\n");
		return rc;
	}

	udma->ucs = devm_kzalloc(dev,
				 sizeof(struct ast2600_udma_chan) * udma->n_ucs, GFP_KERNEL);
	if (!udma->ucs)
		return -ENOMEM;

	for (i = 0; i < udma->n_ucs; ++i) {
		udma->ucs[i].is_tx = !(i % 2);
		udma->ucs[i].chan.device = &udma->ddev;
		udma->ucs[i].buf = dmam_alloc_coherent(dev, UDMA_MAX_BUFSZ,
						       &udma->ucs[i].buf_addr, GFP_KERNEL);
		if (!udma->ucs[i].buf)
			return -ENOMEM;

		udma->ucs[i].udma = udma;
		list_add_tail(&udma->ucs[i].chan.device_node, &udma->ddev.channels);
	}

	rc = dma_async_device_register(&udma->ddev);
	if (rc)
		return rc;

	rc = of_dma_controller_register(dev->of_node, of_dma_xlate_by_chan_id, &udma->ddev);
	if (rc)
		return rc;

	spin_lock_init(&udma->lock);

	platform_set_drvdata(pdev, udma);

	dev_info(dev, "module loaded\n");

	return 0;
}

static int ast2600_udma_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ast2600_udma *udma = platform_get_drvdata(pdev);

	of_dma_controller_free(dev->of_node);
	dma_async_device_unregister(&udma->ddev);

	dev_info(dev, "module removed\n");

	return 0;
}

static const struct of_device_id ast2600_udma_match[] = {
	{ .compatible = "aspeed,ast2600-udma" },
	{ },
};

static struct platform_driver ast2600_udma_driver = {
	.probe = ast2600_udma_probe,
	.remove = ast2600_udma_remove,
	.driver = {
			.name = DEVICE_NAME,
			.of_match_table = ast2600_udma_match,
	},
};

module_platform_driver(ast2600_udma_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chia-Wei Wang <chiawei_wang@aspeedtech.com");

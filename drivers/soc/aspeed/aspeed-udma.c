// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2020 Aspeed Technology Inc.
 */
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/soc/aspeed/aspeed-udma.h>
#include <linux/spinlock.h>

#define DEVICE_NAME "aspeed-udma"

/* UART DMA registers offset */
#define UDMA_TX_DMA_EN		0x000
#define UDMA_RX_DMA_EN		0x004
#define UDMA_MISC		0x008
#define   UDMA_MISC_RX_BUFSZ	GENMASK(3, 2)
#define   UDMA_MISC_TX_BUFSZ	GENMASK(1, 0)
#define UDMA_TMOUT_TIMER	0x00c
#define UDMA_TX_DMA_RST		0x020
#define UDMA_RX_DMA_RST		0x024
#define UDMA_TX_DMA_INT_EN	0x030
#define UDMA_TX_DMA_INT_STS	0x034
#define UDMA_RX_DMA_INT_EN	0x038
#define UDMA_RX_DMA_INT_STS	0x03c

#define UDMA_CHX_OFF(x)			((x) * 0x20)
#define UDMA_CHX_TX_RD_PTR(x)		(0x040 + UDMA_CHX_OFF(x))
#define UDMA_CHX_TX_WR_PTR(x)		(0x044 + UDMA_CHX_OFF(x))
#define UDMA_CHX_TX_BUF_ADDR(x)		(0x048 + UDMA_CHX_OFF(x))
#define UDMA_CHX_TX_CTRL(x)		(0x04c + UDMA_CHX_OFF(x))
#define   UDMA_TX_CTRL_BUF_ADDRH	GENMASK(10, 8)
#define   UDMA_TX_CTRL_TMOUT_DIS	BIT(4)
#define   UDMA_TX_CTRL_BUFSZ		GENMASK(3, 0)
#define UDMA_CHX_RX_RD_PTR(x)		(0x050 + UDMA_CHX_OFF(x))
#define UDMA_CHX_RX_WR_PTR(x)		(0x054 + UDMA_CHX_OFF(x))
#define UDMA_CHX_RX_BUF_ADDR(x)		(0x058 + UDMA_CHX_OFF(x))
#define UDMA_CHX_RX_CTRL(x)		(0x05c + UDMA_CHX_OFF(x))
#define   UDMA_RX_CTRL_BUF_ADDRH	GENMASK(10, 8)
#define   UDMA_RX_CTRL_TMOUT_DIS	BIT(4)
#define   UDMA_RX_CTRL_BUFSZ		GENMASK(1, 0)

#define UDMA_MAX_CHANNEL	16
#define UDMA_TMOUT		0x200

enum aspeed_udma_bufsz_code {
	UDMA_BUFSZ_CODE_1KB,
	UDMA_BUFSZ_CODE_4KB,
	UDMA_BUFSZ_CODE_16KB,
	UDMA_BUFSZ_CODE_64KB,
};

struct aspeed_udma_chan {
	dma_addr_t dma_addr;

	struct circ_buf *rb;
	u32 rb_sz;

	aspeed_udma_cb_t cb;
	void *cb_arg;

	bool dis_tmout;
};

struct aspeed_udma {
	struct device *dev;
	u8 __iomem *regs;
	int irq;
	struct aspeed_udma_chan tx_chs[UDMA_MAX_CHANNEL];
	struct aspeed_udma_chan rx_chs[UDMA_MAX_CHANNEL];
	spinlock_t lock;
};

struct aspeed_udma udma[1];

static int aspeed_udma_get_bufsz_code(u32 buf_sz)
{
	switch (buf_sz) {
	case SZ_1K:
		return UDMA_BUFSZ_CODE_1KB;
	case SZ_4K:
		return UDMA_BUFSZ_CODE_4KB;
	case SZ_16K:
		return UDMA_BUFSZ_CODE_16KB;
	case SZ_64K:
		return UDMA_BUFSZ_CODE_64KB;
	default:
		break;
	}

	return -1;
}

static u32 aspeed_udma_get_tx_rptr(u32 ch_no)
{
	return readl(udma->regs + UDMA_CHX_TX_RD_PTR(ch_no));
}

static u32 aspeed_udma_get_rx_wptr(u32 ch_no)
{
	return readl(udma->regs + UDMA_CHX_RX_WR_PTR(ch_no));
}

static void aspeed_udma_set_ptr(u32 ch_no, u32 ptr, bool is_tx)
{
	writel(ptr, udma->regs +
	       ((is_tx) ? UDMA_CHX_TX_WR_PTR(ch_no) : UDMA_CHX_RX_RD_PTR(ch_no)));
}

void aspeed_udma_set_tx_wptr(u32 ch_no, u32 wptr)
{
	aspeed_udma_set_ptr(ch_no, wptr, true);
}
EXPORT_SYMBOL(aspeed_udma_set_tx_wptr);

void aspeed_udma_set_rx_rptr(u32 ch_no, u32 rptr)
{
	aspeed_udma_set_ptr(ch_no, rptr, false);
}
EXPORT_SYMBOL(aspeed_udma_set_rx_rptr);

static int aspeed_udma_free_chan(u32 ch_no, bool is_tx)
{
	u32 reg;
	unsigned long flags;

	if (ch_no > UDMA_MAX_CHANNEL)
		return -EINVAL;

	spin_lock_irqsave(&udma->lock, flags);

	reg = readl(udma->regs +
		    ((is_tx) ? UDMA_TX_DMA_INT_EN : UDMA_RX_DMA_INT_EN));
	reg &= ~(0x1 << ch_no);

	writel(reg, udma->regs +
	       ((is_tx) ? UDMA_TX_DMA_INT_EN : UDMA_RX_DMA_INT_EN));

	spin_unlock_irqrestore(&udma->lock, flags);

	return 0;
}

int aspeed_udma_free_tx_chan(u32 ch_no)
{
	return aspeed_udma_free_chan(ch_no, true);
}
EXPORT_SYMBOL(aspeed_udma_free_tx_chan);

int aspeed_udma_free_rx_chan(u32 ch_no)
{
	return aspeed_udma_free_chan(ch_no, false);
}
EXPORT_SYMBOL(aspeed_udma_free_rx_chan);

static int aspeed_udma_request_chan(u32 ch_no, dma_addr_t addr,
		struct circ_buf *rb, u32 rb_sz,
		aspeed_udma_cb_t cb, void *id, bool dis_tmout, bool is_tx)
{
	int retval = 0;
	int rbsz_code;

	u32 reg;
	unsigned long flags;
	struct aspeed_udma_chan *ch;

	if (ch_no > UDMA_MAX_CHANNEL) {
		retval = -EINVAL;
		goto out;
	}

	if (IS_ERR_OR_NULL(rb) || IS_ERR_OR_NULL(rb->buf)) {
		retval = -EINVAL;
		goto out;
	}

	rbsz_code = aspeed_udma_get_bufsz_code(rb_sz);
	if (rbsz_code < 0) {
		retval = -EINVAL;
		goto out;
	}

	spin_lock_irqsave(&udma->lock, flags);

	if (is_tx) {
		reg = readl(udma->regs + UDMA_TX_DMA_INT_EN);
		if (reg & (0x1 << ch_no)) {
			retval = -EBUSY;
			goto unlock_n_out;
		}

		reg |= (0x1 << ch_no);
		writel(reg, udma->regs + UDMA_TX_DMA_INT_EN);

		reg = FIELD_PREP(UDMA_TX_CTRL_BUF_ADDRH, (u64)addr >> 32) |
		      (dis_tmout) ? UDMA_TX_CTRL_TMOUT_DIS : 0 |
		      FIELD_PREP(UDMA_TX_CTRL_BUFSZ, rbsz_code);
		writel(reg, udma->regs + UDMA_CHX_TX_CTRL(ch_no));

		writel(addr, udma->regs + UDMA_CHX_TX_BUF_ADDR(ch_no));
	} else {
		reg = readl(udma->regs + UDMA_RX_DMA_INT_EN);
		if (reg & (0x1 << ch_no)) {
			retval = -EBUSY;
			goto unlock_n_out;
		}

		reg |= (0x1 << ch_no);
		writel(reg, udma->regs + UDMA_RX_DMA_INT_EN);

		reg = FIELD_PREP(UDMA_RX_CTRL_BUF_ADDRH, (u64)addr >> 32) |
		      (dis_tmout) ? UDMA_RX_CTRL_TMOUT_DIS : 0 |
		      FIELD_PREP(UDMA_RX_CTRL_BUFSZ, rbsz_code);
		writel(reg, udma->regs + UDMA_CHX_RX_CTRL(ch_no));

		writel(addr, udma->regs + UDMA_CHX_RX_BUF_ADDR(ch_no));
	}

	ch = (is_tx) ? &udma->tx_chs[ch_no] : &udma->rx_chs[ch_no];
	ch->rb = rb;
	ch->rb_sz = rb_sz;
	ch->cb = cb;
	ch->cb_arg = id;
	ch->dma_addr = addr;
	ch->dis_tmout = dis_tmout;

unlock_n_out:
	spin_unlock_irqrestore(&udma->lock, flags);
out:
	return 0;
}

int aspeed_udma_request_tx_chan(u32 ch_no, dma_addr_t addr,
				struct circ_buf *rb, u32 rb_sz,
				aspeed_udma_cb_t cb, void *id, bool dis_tmout)
{
	return aspeed_udma_request_chan(ch_no, addr, rb, rb_sz, cb, id,
					dis_tmout, true);
}
EXPORT_SYMBOL(aspeed_udma_request_tx_chan);

int aspeed_udma_request_rx_chan(u32 ch_no, dma_addr_t addr,
				struct circ_buf *rb, u32 rb_sz,
				aspeed_udma_cb_t cb, void *id, bool dis_tmout)
{
	return aspeed_udma_request_chan(ch_no, addr, rb, rb_sz, cb, id,
					dis_tmout, false);
}
EXPORT_SYMBOL(aspeed_udma_request_rx_chan);

static void aspeed_udma_chan_ctrl(u32 ch_no, u32 op, bool is_tx)
{
	unsigned long flags;
	u32 reg_en, reg_rst;
	u32 reg_en_off = (is_tx) ? UDMA_TX_DMA_EN : UDMA_RX_DMA_EN;
	u32 reg_rst_off = (is_tx) ? UDMA_TX_DMA_RST : UDMA_TX_DMA_RST;

	if (ch_no > UDMA_MAX_CHANNEL)
		return;

	spin_lock_irqsave(&udma->lock, flags);

	reg_en = readl(udma->regs + reg_en_off);
	reg_rst = readl(udma->regs + reg_rst_off);

	switch (op) {
	case ASPEED_UDMA_OP_ENABLE:
		reg_en |= (0x1 << ch_no);
		writel(reg_en, udma->regs + reg_en_off);
		break;
	case ASPEED_UDMA_OP_DISABLE:
		reg_en &= ~(0x1 << ch_no);
		writel(reg_en, udma->regs + reg_en_off);
		break;
	case ASPEED_UDMA_OP_RESET:
		reg_en &= ~(0x1 << ch_no);
		writel(reg_en, udma->regs + reg_en_off);

		reg_rst |= (0x1 << ch_no);
		writel(reg_rst, udma->regs + reg_rst_off);

		udelay(100);

		reg_rst &= ~(0x1 << ch_no);
		writel(reg_rst, udma->regs + reg_rst_off);
		break;
	default:
		break;
	}

	spin_unlock_irqrestore(&udma->lock, flags);
}

void aspeed_udma_tx_chan_ctrl(u32 ch_no, enum aspeed_udma_ops op)
{
	aspeed_udma_chan_ctrl(ch_no, op, true);
}
EXPORT_SYMBOL(aspeed_udma_tx_chan_ctrl);

void aspeed_udma_rx_chan_ctrl(u32 ch_no, enum aspeed_udma_ops op)
{
	aspeed_udma_chan_ctrl(ch_no, op, false);
}
EXPORT_SYMBOL(aspeed_udma_rx_chan_ctrl);

static irqreturn_t aspeed_udma_isr(int irq, void *arg)
{
	u32 bit;
	unsigned long tx_sts = readl(udma->regs + UDMA_TX_DMA_INT_STS);
	unsigned long rx_sts = readl(udma->regs + UDMA_RX_DMA_INT_STS);

	if (udma != (struct aspeed_udma *)arg)
		return IRQ_NONE;

	if (tx_sts == 0 && rx_sts == 0)
		return IRQ_NONE;

	for_each_set_bit(bit, &tx_sts, UDMA_MAX_CHANNEL) {
		writel((0x1 << bit), udma->regs + UDMA_TX_DMA_INT_STS);
		if (udma->tx_chs[bit].cb)
			udma->tx_chs[bit].cb(aspeed_udma_get_tx_rptr(bit),
					     udma->tx_chs[bit].cb_arg);
	}

	for_each_set_bit(bit, &rx_sts, UDMA_MAX_CHANNEL) {
		writel((0x1 << bit), udma->regs + UDMA_RX_DMA_INT_STS);
		if (udma->rx_chs[bit].cb)
			udma->rx_chs[bit].cb(aspeed_udma_get_rx_wptr(bit),
					     udma->rx_chs[bit].cb_arg);
	}

	return IRQ_HANDLED;
}

static int aspeed_udma_probe(struct platform_device *pdev)
{
	int i, rc;
	uint32_t reg;
	struct resource *res;
	struct device *dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (IS_ERR_OR_NULL(res)) {
		dev_err(dev, "failed to get register base\n");
		return -ENODEV;
	}

	udma->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR_OR_NULL(udma->regs)) {
		dev_err(dev, "failed to map registers\n");
		return PTR_ERR(udma->regs);
	}

	/* disable for safety */
	writel(0x0, udma->regs + UDMA_TX_DMA_EN);
	writel(0x0, udma->regs + UDMA_RX_DMA_EN);

	udma->irq = platform_get_irq(pdev, 0);
	if (udma->irq < 0) {
		dev_err(dev, "failed to get IRQ number\n");
		return -ENODEV;
	}

	rc = devm_request_irq(dev, udma->irq, aspeed_udma_isr,
			      IRQF_SHARED, DEVICE_NAME, udma);
	if (rc) {
		dev_err(dev, "failed to request IRQ handler\n");
		return rc;
	}

	/*
	 * For legacy design.
	 *  - TX ringbuffer size: 4KB
	 *  - RX ringbuffer size: 64KB
	 *  - Timeout timer disabled
	 */
	reg = FIELD_PREP(UDMA_MISC_TX_BUFSZ, UDMA_BUFSZ_CODE_4KB) |
	      FIELD_PREP(UDMA_MISC_RX_BUFSZ, UDMA_BUFSZ_CODE_64KB);
	writel(reg, udma->regs + UDMA_MISC);

	for (i = 0; i < UDMA_MAX_CHANNEL; ++i) {
		writel(0, udma->regs + UDMA_CHX_TX_WR_PTR(i));
		writel(0, udma->regs + UDMA_CHX_RX_RD_PTR(i));
	}

	writel(0xffffffff, udma->regs + UDMA_TX_DMA_RST);
	writel(0x0, udma->regs + UDMA_TX_DMA_RST);

	writel(0xffffffff, udma->regs + UDMA_RX_DMA_RST);
	writel(0x0, udma->regs + UDMA_RX_DMA_RST);

	writel(0x0, udma->regs + UDMA_TX_DMA_INT_EN);
	writel(0xffffffff, udma->regs + UDMA_TX_DMA_INT_STS);
	writel(0x0, udma->regs + UDMA_RX_DMA_INT_EN);
	writel(0xffffffff, udma->regs + UDMA_RX_DMA_INT_STS);

	writel(UDMA_TMOUT, udma->regs + UDMA_TMOUT_TIMER);

	spin_lock_init(&udma->lock);

	dev_set_drvdata(dev, udma);

	return 0;
}

static const struct of_device_id aspeed_udma_match[] = {
	{ .compatible = "aspeed,ast2500-udma" },
	{ .compatible = "aspeed,ast2600-udma" },
	{ .compatible = "aspeed,ast2700-udma" },
	{ },
};

static struct platform_driver aspeed_udma_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.of_match_table = aspeed_udma_match,

	},
	.probe = aspeed_udma_probe,
};

module_platform_driver(aspeed_udma_driver);

MODULE_AUTHOR("Chia-Wei Wang <chiawei_wang@aspeedtech.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Aspeed UDMA Engine Driver");

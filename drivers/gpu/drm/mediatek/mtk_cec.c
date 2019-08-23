// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Jie Qiu <jie.qiu@mediatek.com>
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

#include "mtk_cec.h"

#define TR_CONFIG		0x00
#define CLEAR_CEC_IRQ			BIT(15)

#define CEC_CKGEN		0x04
#define CEC_32K_PDN			BIT(19)
#define PDN				BIT(16)

#define RX_EVENT		0x54
#define HDMI_PORD			BIT(25)
#define HDMI_HTPLG			BIT(24)
#define HDMI_PORD_INT_EN		BIT(9)
#define HDMI_HTPLG_INT_EN		BIT(8)

#define RX_GEN_WD		0x58
#define HDMI_PORD_INT_32K_STATUS	BIT(26)
#define RX_RISC_INT_32K_STATUS		BIT(25)
#define HDMI_HTPLG_INT_32K_STATUS	BIT(24)
#define HDMI_PORD_INT_32K_CLR		BIT(18)
#define RX_INT_32K_CLR			BIT(17)
#define HDMI_HTPLG_INT_32K_CLR		BIT(16)
#define HDMI_PORD_INT_32K_STA_MASK	BIT(10)
#define RX_RISC_INT_32K_STA_MASK	BIT(9)
#define HDMI_HTPLG_INT_32K_STA_MASK	BIT(8)
#define HDMI_PORD_INT_32K_EN		BIT(2)
#define RX_INT_32K_EN			BIT(1)
#define HDMI_HTPLG_INT_32K_EN		BIT(0)

#define NORMAL_INT_CTRL		0x5C
#define HDMI_HTPLG_INT_STA		BIT(0)
#define HDMI_PORD_INT_STA		BIT(1)
#define HDMI_HTPLG_INT_CLR		BIT(16)
#define HDMI_PORD_INT_CLR		BIT(17)
#define HDMI_FULL_INT_CLR		BIT(20)

struct mtk_cec {
	void __iomem *regs;
	struct clk *clk;
	int irq;
	bool hpd;
	void (*hpd_event)(bool hpd, struct device *dev);
	struct device *hdmi_dev;
	spinlock_t lock;
};

static void mtk_cec_clear_bits(struct mtk_cec *cec, unsigned int offset,
			       unsigned int bits)
{
	void __iomem *reg = cec->regs + offset;
	u32 tmp;

	tmp = readl(reg);
	tmp &= ~bits;
	writel(tmp, reg);
}

static void mtk_cec_set_bits(struct mtk_cec *cec, unsigned int offset,
			     unsigned int bits)
{
	void __iomem *reg = cec->regs + offset;
	u32 tmp;

	tmp = readl(reg);
	tmp |= bits;
	writel(tmp, reg);
}

static void mtk_cec_mask(struct mtk_cec *cec, unsigned int offset,
			 unsigned int val, unsigned int mask)
{
	u32 tmp = readl(cec->regs + offset) & ~mask;

	tmp |= val & mask;
	writel(val, cec->regs + offset);
}

void mtk_cec_set_hpd_event(struct device *dev,
			   void (*hpd_event)(bool hpd, struct device *dev),
			   struct device *hdmi_dev)
{
	struct mtk_cec *cec = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&cec->lock, flags);
	cec->hdmi_dev = hdmi_dev;
	cec->hpd_event = hpd_event;
	spin_unlock_irqrestore(&cec->lock, flags);
}

bool mtk_cec_hpd_high(struct device *dev)
{
	struct mtk_cec *cec = dev_get_drvdata(dev);
	unsigned int status;

	status = readl(cec->regs + RX_EVENT);

	return (status & (HDMI_PORD | HDMI_HTPLG)) == (HDMI_PORD | HDMI_HTPLG);
}

static void mtk_cec_htplg_irq_init(struct mtk_cec *cec)
{
	mtk_cec_mask(cec, CEC_CKGEN, 0 | CEC_32K_PDN, PDN | CEC_32K_PDN);
	mtk_cec_set_bits(cec, RX_GEN_WD, HDMI_PORD_INT_32K_CLR |
			 RX_INT_32K_CLR | HDMI_HTPLG_INT_32K_CLR);
	mtk_cec_mask(cec, RX_GEN_WD, 0, HDMI_PORD_INT_32K_CLR | RX_INT_32K_CLR |
		     HDMI_HTPLG_INT_32K_CLR | HDMI_PORD_INT_32K_EN |
		     RX_INT_32K_EN | HDMI_HTPLG_INT_32K_EN);
}

static void mtk_cec_htplg_irq_enable(struct mtk_cec *cec)
{
	mtk_cec_set_bits(cec, RX_EVENT, HDMI_PORD_INT_EN | HDMI_HTPLG_INT_EN);
}

static void mtk_cec_htplg_irq_disable(struct mtk_cec *cec)
{
	mtk_cec_clear_bits(cec, RX_EVENT, HDMI_PORD_INT_EN | HDMI_HTPLG_INT_EN);
}

static void mtk_cec_clear_htplg_irq(struct mtk_cec *cec)
{
	mtk_cec_set_bits(cec, TR_CONFIG, CLEAR_CEC_IRQ);
	mtk_cec_set_bits(cec, NORMAL_INT_CTRL, HDMI_HTPLG_INT_CLR |
			 HDMI_PORD_INT_CLR | HDMI_FULL_INT_CLR);
	mtk_cec_set_bits(cec, RX_GEN_WD, HDMI_PORD_INT_32K_CLR |
			 RX_INT_32K_CLR | HDMI_HTPLG_INT_32K_CLR);
	usleep_range(5, 10);
	mtk_cec_clear_bits(cec, NORMAL_INT_CTRL, HDMI_HTPLG_INT_CLR |
			   HDMI_PORD_INT_CLR | HDMI_FULL_INT_CLR);
	mtk_cec_clear_bits(cec, TR_CONFIG, CLEAR_CEC_IRQ);
	mtk_cec_clear_bits(cec, RX_GEN_WD, HDMI_PORD_INT_32K_CLR |
			   RX_INT_32K_CLR | HDMI_HTPLG_INT_32K_CLR);
}

static void mtk_cec_hpd_event(struct mtk_cec *cec, bool hpd)
{
	void (*hpd_event)(bool hpd, struct device *dev);
	struct device *hdmi_dev;
	unsigned long flags;

	spin_lock_irqsave(&cec->lock, flags);
	hpd_event = cec->hpd_event;
	hdmi_dev = cec->hdmi_dev;
	spin_unlock_irqrestore(&cec->lock, flags);

	if (hpd_event)
		hpd_event(hpd, hdmi_dev);
}

static irqreturn_t mtk_cec_htplg_isr_thread(int irq, void *arg)
{
	struct device *dev = arg;
	struct mtk_cec *cec = dev_get_drvdata(dev);
	bool hpd;

	mtk_cec_clear_htplg_irq(cec);
	hpd = mtk_cec_hpd_high(dev);

	if (cec->hpd != hpd) {
		dev_dbg(dev, "hotplug event! cur hpd = %d, hpd = %d\n",
			cec->hpd, hpd);
		cec->hpd = hpd;
		mtk_cec_hpd_event(cec, hpd);
	}
	return IRQ_HANDLED;
}

static int mtk_cec_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_cec *cec;
	struct resource *res;
	int ret;

	cec = devm_kzalloc(dev, sizeof(*cec), GFP_KERNEL);
	if (!cec)
		return -ENOMEM;

	platform_set_drvdata(pdev, cec);
	spin_lock_init(&cec->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cec->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(cec->regs)) {
		ret = PTR_ERR(cec->regs);
		dev_err(dev, "Failed to ioremap cec: %d\n", ret);
		return ret;
	}

	cec->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(cec->clk)) {
		ret = PTR_ERR(cec->clk);
		dev_err(dev, "Failed to get cec clock: %d\n", ret);
		return ret;
	}

	cec->irq = platform_get_irq(pdev, 0);
	if (cec->irq < 0) {
		dev_err(dev, "Failed to get cec irq: %d\n", cec->irq);
		return cec->irq;
	}

	ret = devm_request_threaded_irq(dev, cec->irq, NULL,
					mtk_cec_htplg_isr_thread,
					IRQF_SHARED | IRQF_TRIGGER_LOW |
					IRQF_ONESHOT, "hdmi hpd", dev);
	if (ret) {
		dev_err(dev, "Failed to register cec irq: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(cec->clk);
	if (ret) {
		dev_err(dev, "Failed to enable cec clock: %d\n", ret);
		return ret;
	}

	mtk_cec_htplg_irq_init(cec);
	mtk_cec_htplg_irq_enable(cec);

	return 0;
}

static int mtk_cec_remove(struct platform_device *pdev)
{
	struct mtk_cec *cec = platform_get_drvdata(pdev);

	mtk_cec_htplg_irq_disable(cec);
	clk_disable_unprepare(cec->clk);
	return 0;
}

static const struct of_device_id mtk_cec_of_ids[] = {
	{ .compatible = "mediatek,mt8173-cec", },
	{}
};

struct platform_driver mtk_cec_driver = {
	.probe = mtk_cec_probe,
	.remove = mtk_cec_remove,
	.driver = {
		.name = "mediatek-cec",
		.of_match_table = mtk_cec_of_ids,
	},
};

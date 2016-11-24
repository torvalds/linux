/*
 * MTK ECC controller driver.
 * Copyright (C) 2016  MediaTek Inc.
 * Authors:	Xiaolei Li		<xiaolei.li@mediatek.com>
 *		Jorge Ramirez-Ortiz	<jorge.ramirez-ortiz@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/mutex.h>

#include "mtk_ecc.h"

#define ECC_IDLE_MASK		BIT(0)
#define ECC_IRQ_EN		BIT(0)
#define ECC_OP_ENABLE		(1)
#define ECC_OP_DISABLE		(0)

#define ECC_ENCCON		(0x00)
#define ECC_ENCCNFG		(0x04)
#define		ECC_CNFG_4BIT		(0)
#define		ECC_CNFG_6BIT		(1)
#define		ECC_CNFG_8BIT		(2)
#define		ECC_CNFG_10BIT		(3)
#define		ECC_CNFG_12BIT		(4)
#define		ECC_CNFG_14BIT		(5)
#define		ECC_CNFG_16BIT		(6)
#define		ECC_CNFG_18BIT		(7)
#define		ECC_CNFG_20BIT		(8)
#define		ECC_CNFG_22BIT		(9)
#define		ECC_CNFG_24BIT		(0xa)
#define		ECC_CNFG_28BIT		(0xb)
#define		ECC_CNFG_32BIT		(0xc)
#define		ECC_CNFG_36BIT		(0xd)
#define		ECC_CNFG_40BIT		(0xe)
#define		ECC_CNFG_44BIT		(0xf)
#define		ECC_CNFG_48BIT		(0x10)
#define		ECC_CNFG_52BIT		(0x11)
#define		ECC_CNFG_56BIT		(0x12)
#define		ECC_CNFG_60BIT		(0x13)
#define		ECC_MODE_SHIFT		(5)
#define		ECC_MS_SHIFT		(16)
#define ECC_ENCDIADDR		(0x08)
#define ECC_ENCIDLE		(0x0C)
#define ECC_ENCPAR(x)		(0x10 + (x) * sizeof(u32))
#define ECC_ENCIRQ_EN		(0x80)
#define ECC_ENCIRQ_STA		(0x84)
#define ECC_DECCON		(0x100)
#define ECC_DECCNFG		(0x104)
#define		DEC_EMPTY_EN		BIT(31)
#define		DEC_CNFG_CORRECT	(0x3 << 12)
#define ECC_DECIDLE		(0x10C)
#define ECC_DECENUM0		(0x114)
#define		ERR_MASK		(0x3f)
#define ECC_DECDONE		(0x124)
#define ECC_DECIRQ_EN		(0x200)
#define ECC_DECIRQ_STA		(0x204)

#define ECC_TIMEOUT		(500000)

#define ECC_IDLE_REG(op)	((op) == ECC_ENCODE ? ECC_ENCIDLE : ECC_DECIDLE)
#define ECC_CTL_REG(op)		((op) == ECC_ENCODE ? ECC_ENCCON : ECC_DECCON)
#define ECC_IRQ_REG(op)		((op) == ECC_ENCODE ? \
					ECC_ENCIRQ_EN : ECC_DECIRQ_EN)

struct mtk_ecc {
	struct device *dev;
	void __iomem *regs;
	struct clk *clk;

	struct completion done;
	struct mutex lock;
	u32 sectors;

	u8 eccdata[112];
};

static inline void mtk_ecc_wait_idle(struct mtk_ecc *ecc,
				     enum mtk_ecc_operation op)
{
	struct device *dev = ecc->dev;
	u32 val;
	int ret;

	ret = readl_poll_timeout_atomic(ecc->regs + ECC_IDLE_REG(op), val,
					val & ECC_IDLE_MASK,
					10, ECC_TIMEOUT);
	if (ret)
		dev_warn(dev, "%s NOT idle\n",
			 op == ECC_ENCODE ? "encoder" : "decoder");
}

static irqreturn_t mtk_ecc_irq(int irq, void *id)
{
	struct mtk_ecc *ecc = id;
	enum mtk_ecc_operation op;
	u32 dec, enc;

	dec = readw(ecc->regs + ECC_DECIRQ_STA) & ECC_IRQ_EN;
	if (dec) {
		op = ECC_DECODE;
		dec = readw(ecc->regs + ECC_DECDONE);
		if (dec & ecc->sectors) {
			ecc->sectors = 0;
			complete(&ecc->done);
		} else {
			return IRQ_HANDLED;
		}
	} else {
		enc = readl(ecc->regs + ECC_ENCIRQ_STA) & ECC_IRQ_EN;
		if (enc) {
			op = ECC_ENCODE;
			complete(&ecc->done);
		} else {
			return IRQ_NONE;
		}
	}

	writel(0, ecc->regs + ECC_IRQ_REG(op));

	return IRQ_HANDLED;
}

static void mtk_ecc_config(struct mtk_ecc *ecc, struct mtk_ecc_config *config)
{
	u32 ecc_bit = ECC_CNFG_4BIT, dec_sz, enc_sz;
	u32 reg;

	switch (config->strength) {
	case 4:
		ecc_bit = ECC_CNFG_4BIT;
		break;
	case 6:
		ecc_bit = ECC_CNFG_6BIT;
		break;
	case 8:
		ecc_bit = ECC_CNFG_8BIT;
		break;
	case 10:
		ecc_bit = ECC_CNFG_10BIT;
		break;
	case 12:
		ecc_bit = ECC_CNFG_12BIT;
		break;
	case 14:
		ecc_bit = ECC_CNFG_14BIT;
		break;
	case 16:
		ecc_bit = ECC_CNFG_16BIT;
		break;
	case 18:
		ecc_bit = ECC_CNFG_18BIT;
		break;
	case 20:
		ecc_bit = ECC_CNFG_20BIT;
		break;
	case 22:
		ecc_bit = ECC_CNFG_22BIT;
		break;
	case 24:
		ecc_bit = ECC_CNFG_24BIT;
		break;
	case 28:
		ecc_bit = ECC_CNFG_28BIT;
		break;
	case 32:
		ecc_bit = ECC_CNFG_32BIT;
		break;
	case 36:
		ecc_bit = ECC_CNFG_36BIT;
		break;
	case 40:
		ecc_bit = ECC_CNFG_40BIT;
		break;
	case 44:
		ecc_bit = ECC_CNFG_44BIT;
		break;
	case 48:
		ecc_bit = ECC_CNFG_48BIT;
		break;
	case 52:
		ecc_bit = ECC_CNFG_52BIT;
		break;
	case 56:
		ecc_bit = ECC_CNFG_56BIT;
		break;
	case 60:
		ecc_bit = ECC_CNFG_60BIT;
		break;
	default:
		dev_err(ecc->dev, "invalid strength %d, default to 4 bits\n",
			config->strength);
	}

	if (config->op == ECC_ENCODE) {
		/* configure ECC encoder (in bits) */
		enc_sz = config->len << 3;

		reg = ecc_bit | (config->mode << ECC_MODE_SHIFT);
		reg |= (enc_sz << ECC_MS_SHIFT);
		writel(reg, ecc->regs + ECC_ENCCNFG);

		if (config->mode != ECC_NFI_MODE)
			writel(lower_32_bits(config->addr),
			       ecc->regs + ECC_ENCDIADDR);

	} else {
		/* configure ECC decoder (in bits) */
		dec_sz = (config->len << 3) +
					config->strength * ECC_PARITY_BITS;

		reg = ecc_bit | (config->mode << ECC_MODE_SHIFT);
		reg |= (dec_sz << ECC_MS_SHIFT) | DEC_CNFG_CORRECT;
		reg |= DEC_EMPTY_EN;
		writel(reg, ecc->regs + ECC_DECCNFG);

		if (config->sectors)
			ecc->sectors = 1 << (config->sectors - 1);
	}
}

void mtk_ecc_get_stats(struct mtk_ecc *ecc, struct mtk_ecc_stats *stats,
		       int sectors)
{
	u32 offset, i, err;
	u32 bitflips = 0;

	stats->corrected = 0;
	stats->failed = 0;

	for (i = 0; i < sectors; i++) {
		offset = (i >> 2) << 2;
		err = readl(ecc->regs + ECC_DECENUM0 + offset);
		err = err >> ((i % 4) * 8);
		err &= ERR_MASK;
		if (err == ERR_MASK) {
			/* uncorrectable errors */
			stats->failed++;
			continue;
		}

		stats->corrected += err;
		bitflips = max_t(u32, bitflips, err);
	}

	stats->bitflips = bitflips;
}
EXPORT_SYMBOL(mtk_ecc_get_stats);

void mtk_ecc_release(struct mtk_ecc *ecc)
{
	clk_disable_unprepare(ecc->clk);
	put_device(ecc->dev);
}
EXPORT_SYMBOL(mtk_ecc_release);

static void mtk_ecc_hw_init(struct mtk_ecc *ecc)
{
	mtk_ecc_wait_idle(ecc, ECC_ENCODE);
	writew(ECC_OP_DISABLE, ecc->regs + ECC_ENCCON);

	mtk_ecc_wait_idle(ecc, ECC_DECODE);
	writel(ECC_OP_DISABLE, ecc->regs + ECC_DECCON);
}

static struct mtk_ecc *mtk_ecc_get(struct device_node *np)
{
	struct platform_device *pdev;
	struct mtk_ecc *ecc;

	pdev = of_find_device_by_node(np);
	if (!pdev || !platform_get_drvdata(pdev))
		return ERR_PTR(-EPROBE_DEFER);

	get_device(&pdev->dev);
	ecc = platform_get_drvdata(pdev);
	clk_prepare_enable(ecc->clk);
	mtk_ecc_hw_init(ecc);

	return ecc;
}

struct mtk_ecc *of_mtk_ecc_get(struct device_node *of_node)
{
	struct mtk_ecc *ecc = NULL;
	struct device_node *np;

	np = of_parse_phandle(of_node, "ecc-engine", 0);
	if (np) {
		ecc = mtk_ecc_get(np);
		of_node_put(np);
	}

	return ecc;
}
EXPORT_SYMBOL(of_mtk_ecc_get);

int mtk_ecc_enable(struct mtk_ecc *ecc, struct mtk_ecc_config *config)
{
	enum mtk_ecc_operation op = config->op;
	int ret;

	ret = mutex_lock_interruptible(&ecc->lock);
	if (ret) {
		dev_err(ecc->dev, "interrupted when attempting to lock\n");
		return ret;
	}

	mtk_ecc_wait_idle(ecc, op);
	mtk_ecc_config(ecc, config);
	writew(ECC_OP_ENABLE, ecc->regs + ECC_CTL_REG(op));

	init_completion(&ecc->done);
	writew(ECC_IRQ_EN, ecc->regs + ECC_IRQ_REG(op));

	return 0;
}
EXPORT_SYMBOL(mtk_ecc_enable);

void mtk_ecc_disable(struct mtk_ecc *ecc)
{
	enum mtk_ecc_operation op = ECC_ENCODE;

	/* find out the running operation */
	if (readw(ecc->regs + ECC_CTL_REG(op)) != ECC_OP_ENABLE)
		op = ECC_DECODE;

	/* disable it */
	mtk_ecc_wait_idle(ecc, op);
	writew(0, ecc->regs + ECC_IRQ_REG(op));
	writew(ECC_OP_DISABLE, ecc->regs + ECC_CTL_REG(op));

	mutex_unlock(&ecc->lock);
}
EXPORT_SYMBOL(mtk_ecc_disable);

int mtk_ecc_wait_done(struct mtk_ecc *ecc, enum mtk_ecc_operation op)
{
	int ret;

	ret = wait_for_completion_timeout(&ecc->done, msecs_to_jiffies(500));
	if (!ret) {
		dev_err(ecc->dev, "%s timeout - interrupt did not arrive)\n",
			(op == ECC_ENCODE) ? "encoder" : "decoder");
		return -ETIMEDOUT;
	}

	return 0;
}
EXPORT_SYMBOL(mtk_ecc_wait_done);

int mtk_ecc_encode(struct mtk_ecc *ecc, struct mtk_ecc_config *config,
		   u8 *data, u32 bytes)
{
	dma_addr_t addr;
	u32 len;
	int ret;

	addr = dma_map_single(ecc->dev, data, bytes, DMA_TO_DEVICE);
	ret = dma_mapping_error(ecc->dev, addr);
	if (ret) {
		dev_err(ecc->dev, "dma mapping error\n");
		return -EINVAL;
	}

	config->op = ECC_ENCODE;
	config->addr = addr;
	ret = mtk_ecc_enable(ecc, config);
	if (ret) {
		dma_unmap_single(ecc->dev, addr, bytes, DMA_TO_DEVICE);
		return ret;
	}

	ret = mtk_ecc_wait_done(ecc, ECC_ENCODE);
	if (ret)
		goto timeout;

	mtk_ecc_wait_idle(ecc, ECC_ENCODE);

	/* Program ECC bytes to OOB: per sector oob = FDM + ECC + SPARE */
	len = (config->strength * ECC_PARITY_BITS + 7) >> 3;

	/* write the parity bytes generated by the ECC back to temp buffer */
	__ioread32_copy(ecc->eccdata, ecc->regs + ECC_ENCPAR(0), round_up(len, 4));

	/* copy into possibly unaligned OOB region with actual length */
	memcpy(data + bytes, ecc->eccdata, len);
timeout:

	dma_unmap_single(ecc->dev, addr, bytes, DMA_TO_DEVICE);
	mtk_ecc_disable(ecc);

	return ret;
}
EXPORT_SYMBOL(mtk_ecc_encode);

void mtk_ecc_adjust_strength(u32 *p)
{
	u32 ecc[] = {4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 28, 32, 36,
			40, 44, 48, 52, 56, 60};
	int i;

	for (i = 0; i < ARRAY_SIZE(ecc); i++) {
		if (*p <= ecc[i]) {
			if (!i)
				*p = ecc[i];
			else if (*p != ecc[i])
				*p = ecc[i - 1];
			return;
		}
	}

	*p = ecc[ARRAY_SIZE(ecc) - 1];
}
EXPORT_SYMBOL(mtk_ecc_adjust_strength);

static int mtk_ecc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_ecc *ecc;
	struct resource *res;
	int irq, ret;

	ecc = devm_kzalloc(dev, sizeof(*ecc), GFP_KERNEL);
	if (!ecc)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ecc->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(ecc->regs)) {
		dev_err(dev, "failed to map regs: %ld\n", PTR_ERR(ecc->regs));
		return PTR_ERR(ecc->regs);
	}

	ecc->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ecc->clk)) {
		dev_err(dev, "failed to get clock: %ld\n", PTR_ERR(ecc->clk));
		return PTR_ERR(ecc->clk);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "failed to get irq\n");
		return -EINVAL;
	}

	ret = dma_set_mask(dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(dev, "failed to set DMA mask\n");
		return ret;
	}

	ret = devm_request_irq(dev, irq, mtk_ecc_irq, 0x0, "mtk-ecc", ecc);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		return -EINVAL;
	}

	ecc->dev = dev;
	mutex_init(&ecc->lock);
	platform_set_drvdata(pdev, ecc);
	dev_info(dev, "probed\n");

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mtk_ecc_suspend(struct device *dev)
{
	struct mtk_ecc *ecc = dev_get_drvdata(dev);

	clk_disable_unprepare(ecc->clk);

	return 0;
}

static int mtk_ecc_resume(struct device *dev)
{
	struct mtk_ecc *ecc = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(ecc->clk);
	if (ret) {
		dev_err(dev, "failed to enable clk\n");
		return ret;
	}

	mtk_ecc_hw_init(ecc);

	return 0;
}

static SIMPLE_DEV_PM_OPS(mtk_ecc_pm_ops, mtk_ecc_suspend, mtk_ecc_resume);
#endif

static const struct of_device_id mtk_ecc_dt_match[] = {
	{ .compatible = "mediatek,mt2701-ecc" },
	{},
};

MODULE_DEVICE_TABLE(of, mtk_ecc_dt_match);

static struct platform_driver mtk_ecc_driver = {
	.probe  = mtk_ecc_probe,
	.driver = {
		.name  = "mtk-ecc",
		.of_match_table = of_match_ptr(mtk_ecc_dt_match),
#ifdef CONFIG_PM_SLEEP
		.pm = &mtk_ecc_pm_ops,
#endif
	},
};

module_platform_driver(mtk_ecc_driver);

MODULE_AUTHOR("Xiaolei Li <xiaolei.li@mediatek.com>");
MODULE_DESCRIPTION("MTK Nand ECC Driver");
MODULE_LICENSE("GPL");

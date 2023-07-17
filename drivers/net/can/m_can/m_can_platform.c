// SPDX-License-Identifier: GPL-2.0
// IOMapped CAN bus driver for Bosch M_CAN controller
// Copyright (C) 2014 Freescale Semiconductor, Inc.
//	Dong Aisheng <b29396@freescale.com>
//
// Copyright (C) 2018-19 Texas Instruments Incorporated - http://www.ti.com/

#include <linux/phy/phy.h>
#include <linux/platform_device.h>

#include "m_can.h"

struct m_can_plat_priv {
	struct m_can_classdev cdev;

	void __iomem *base;
	void __iomem *mram_base;
};

static inline struct m_can_plat_priv *cdev_to_priv(struct m_can_classdev *cdev)
{
	return container_of(cdev, struct m_can_plat_priv, cdev);
}

static u32 iomap_read_reg(struct m_can_classdev *cdev, int reg)
{
	struct m_can_plat_priv *priv = cdev_to_priv(cdev);

	return readl(priv->base + reg);
}

static int iomap_read_fifo(struct m_can_classdev *cdev, int offset, void *val, size_t val_count)
{
	struct m_can_plat_priv *priv = cdev_to_priv(cdev);
	void __iomem *src = priv->mram_base + offset;

	while (val_count--) {
		*(unsigned int *)val = ioread32(src);
		val += 4;
		src += 4;
	}

	return 0;
}

static int iomap_write_reg(struct m_can_classdev *cdev, int reg, int val)
{
	struct m_can_plat_priv *priv = cdev_to_priv(cdev);

	writel(val, priv->base + reg);

	return 0;
}

static int iomap_write_fifo(struct m_can_classdev *cdev, int offset,
			    const void *val, size_t val_count)
{
	struct m_can_plat_priv *priv = cdev_to_priv(cdev);
	void __iomem *dst = priv->mram_base + offset;

	while (val_count--) {
		iowrite32(*(unsigned int *)val, dst);
		val += 4;
		dst += 4;
	}

	return 0;
}

static struct m_can_ops m_can_plat_ops = {
	.read_reg = iomap_read_reg,
	.write_reg = iomap_write_reg,
	.write_fifo = iomap_write_fifo,
	.read_fifo = iomap_read_fifo,
};

static int m_can_plat_probe(struct platform_device *pdev)
{
	struct m_can_classdev *mcan_class;
	struct m_can_plat_priv *priv;
	struct resource *res;
	void __iomem *addr;
	void __iomem *mram_addr;
	struct phy *transceiver;
	int irq, ret = 0;

	mcan_class = m_can_class_allocate_dev(&pdev->dev,
					      sizeof(struct m_can_plat_priv));
	if (!mcan_class)
		return -ENOMEM;

	priv = cdev_to_priv(mcan_class);

	ret = m_can_class_get_clocks(mcan_class);
	if (ret)
		goto probe_fail;

	addr = devm_platform_ioremap_resource_byname(pdev, "m_can");
	irq = platform_get_irq_byname(pdev, "int0");
	if (IS_ERR(addr) || irq < 0) {
		ret = -EINVAL;
		goto probe_fail;
	}

	/* message ram could be shared */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "message_ram");
	if (!res) {
		ret = -ENODEV;
		goto probe_fail;
	}

	mram_addr = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!mram_addr) {
		ret = -ENOMEM;
		goto probe_fail;
	}

	transceiver = devm_phy_optional_get(&pdev->dev, NULL);
	if (IS_ERR(transceiver)) {
		ret = PTR_ERR(transceiver);
		dev_err_probe(&pdev->dev, ret, "failed to get phy\n");
		goto probe_fail;
	}

	if (transceiver)
		mcan_class->can.bitrate_max = transceiver->attrs.max_link_rate;

	priv->base = addr;
	priv->mram_base = mram_addr;

	mcan_class->net->irq = irq;
	mcan_class->pm_clock_support = 1;
	mcan_class->can.clock.freq = clk_get_rate(mcan_class->cclk);
	mcan_class->dev = &pdev->dev;
	mcan_class->transceiver = transceiver;

	mcan_class->ops = &m_can_plat_ops;

	mcan_class->is_peripheral = false;

	platform_set_drvdata(pdev, mcan_class);

	pm_runtime_enable(mcan_class->dev);
	ret = m_can_class_register(mcan_class);
	if (ret)
		goto out_runtime_disable;

	return ret;

out_runtime_disable:
	pm_runtime_disable(mcan_class->dev);
probe_fail:
	m_can_class_free_dev(mcan_class->net);
	return ret;
}

static __maybe_unused int m_can_suspend(struct device *dev)
{
	return m_can_class_suspend(dev);
}

static __maybe_unused int m_can_resume(struct device *dev)
{
	return m_can_class_resume(dev);
}

static void m_can_plat_remove(struct platform_device *pdev)
{
	struct m_can_plat_priv *priv = platform_get_drvdata(pdev);
	struct m_can_classdev *mcan_class = &priv->cdev;

	m_can_class_unregister(mcan_class);

	m_can_class_free_dev(mcan_class->net);
}

static int __maybe_unused m_can_runtime_suspend(struct device *dev)
{
	struct m_can_plat_priv *priv = dev_get_drvdata(dev);
	struct m_can_classdev *mcan_class = &priv->cdev;

	clk_disable_unprepare(mcan_class->cclk);
	clk_disable_unprepare(mcan_class->hclk);

	return 0;
}

static int __maybe_unused m_can_runtime_resume(struct device *dev)
{
	struct m_can_plat_priv *priv = dev_get_drvdata(dev);
	struct m_can_classdev *mcan_class = &priv->cdev;
	int err;

	err = clk_prepare_enable(mcan_class->hclk);
	if (err)
		return err;

	err = clk_prepare_enable(mcan_class->cclk);
	if (err)
		clk_disable_unprepare(mcan_class->hclk);

	return err;
}

static const struct dev_pm_ops m_can_pmops = {
	SET_RUNTIME_PM_OPS(m_can_runtime_suspend,
			   m_can_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(m_can_suspend, m_can_resume)
};

static const struct of_device_id m_can_of_table[] = {
	{ .compatible = "bosch,m_can", .data = NULL },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, m_can_of_table);

static struct platform_driver m_can_plat_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = m_can_of_table,
		.pm     = &m_can_pmops,
	},
	.probe = m_can_plat_probe,
	.remove_new = m_can_plat_remove,
};

module_platform_driver(m_can_plat_driver);

MODULE_AUTHOR("Dong Aisheng <b29396@freescale.com>");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("M_CAN driver for IO Mapped Bosch controllers");

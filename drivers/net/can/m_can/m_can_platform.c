// SPDX-License-Identifier: GPL-2.0
// IOMapped CAN bus driver for Bosch M_CAN controller
// Copyright (C) 2014 Freescale Semiconductor, Inc.
//	Dong Aisheng <b29396@freescale.com>
//
// Copyright (C) 2018-19 Texas Instruments Incorporated - http://www.ti.com/

#include <linux/platform_device.h>

#include "m_can.h"

struct m_can_plat_priv {
	void __iomem *base;
	void __iomem *mram_base;
};

static u32 iomap_read_reg(struct m_can_classdev *cdev, int reg)
{
	struct m_can_plat_priv *priv = cdev->device_data;

	return readl(priv->base + reg);
}

static u32 iomap_read_fifo(struct m_can_classdev *cdev, int offset)
{
	struct m_can_plat_priv *priv = cdev->device_data;

	return readl(priv->mram_base + offset);
}

static int iomap_write_reg(struct m_can_classdev *cdev, int reg, int val)
{
	struct m_can_plat_priv *priv = cdev->device_data;

	writel(val, priv->base + reg);

	return 0;
}

static int iomap_write_fifo(struct m_can_classdev *cdev, int offset, int val)
{
	struct m_can_plat_priv *priv = cdev->device_data;

	writel(val, priv->mram_base + offset);

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
	int irq, ret = 0;

	mcan_class = m_can_class_allocate_dev(&pdev->dev);
	if (!mcan_class)
		return -ENOMEM;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto probe_fail;
	}

	mcan_class->device_data = priv;

	ret = m_can_class_get_clocks(mcan_class);
	if (ret)
		goto probe_fail;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "m_can");
	addr = devm_ioremap_resource(&pdev->dev, res);
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

	priv->base = addr;
	priv->mram_base = mram_addr;

	mcan_class->net->irq = irq;
	mcan_class->pm_clock_support = 1;
	mcan_class->can.clock.freq = clk_get_rate(mcan_class->cclk);
	mcan_class->dev = &pdev->dev;

	mcan_class->ops = &m_can_plat_ops;

	mcan_class->is_peripheral = false;

	platform_set_drvdata(pdev, mcan_class->net);

	m_can_init_ram(mcan_class);

	return m_can_class_register(mcan_class);

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

static int m_can_plat_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct m_can_classdev *mcan_class = netdev_priv(dev);

	m_can_class_unregister(mcan_class);

	m_can_class_free_dev(mcan_class->net);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

static int __maybe_unused m_can_runtime_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct m_can_classdev *mcan_class = netdev_priv(ndev);

	clk_disable_unprepare(mcan_class->cclk);
	clk_disable_unprepare(mcan_class->hclk);

	return 0;
}

static int __maybe_unused m_can_runtime_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct m_can_classdev *mcan_class = netdev_priv(ndev);
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
	.remove = m_can_plat_remove,
};

module_platform_driver(m_can_plat_driver);

MODULE_AUTHOR("Dong Aisheng <b29396@freescale.com>");
MODULE_AUTHOR("Dan Murphy <dmurphy@ti.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("M_CAN driver for IO Mapped Bosch controllers");

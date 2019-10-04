// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015 Broadcom Corporation
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/sched.h>

#define IPROC_GPHY_MDCDIV    0x1a

#define MII_CTRL_OFFSET      0x000

#define MII_CTRL_DIV_SHIFT   0
#define MII_CTRL_PRE_SHIFT   7
#define MII_CTRL_BUSY_SHIFT  8

#define MII_DATA_OFFSET      0x004
#define MII_DATA_MASK        0xffff
#define MII_DATA_TA_SHIFT    16
#define MII_DATA_TA_VAL      2
#define MII_DATA_RA_SHIFT    18
#define MII_DATA_PA_SHIFT    23
#define MII_DATA_OP_SHIFT    28
#define MII_DATA_OP_WRITE    1
#define MII_DATA_OP_READ     2
#define MII_DATA_SB_SHIFT    30

struct iproc_mdio_priv {
	struct mii_bus *mii_bus;
	void __iomem *base;
};

static inline int iproc_mdio_wait_for_idle(void __iomem *base)
{
	u32 val;
	unsigned int timeout = 1000; /* loop for 1s */

	do {
		val = readl(base + MII_CTRL_OFFSET);
		if ((val & BIT(MII_CTRL_BUSY_SHIFT)) == 0)
			return 0;

		usleep_range(1000, 2000);
	} while (timeout--);

	return -ETIMEDOUT;
}

static inline void iproc_mdio_config_clk(void __iomem *base)
{
	u32 val;

	val = (IPROC_GPHY_MDCDIV << MII_CTRL_DIV_SHIFT) |
		  BIT(MII_CTRL_PRE_SHIFT);
	writel(val, base + MII_CTRL_OFFSET);
}

static int iproc_mdio_read(struct mii_bus *bus, int phy_id, int reg)
{
	struct iproc_mdio_priv *priv = bus->priv;
	u32 cmd;
	int rc;

	rc = iproc_mdio_wait_for_idle(priv->base);
	if (rc)
		return rc;

	/* Prepare the read operation */
	cmd = (MII_DATA_TA_VAL << MII_DATA_TA_SHIFT) |
		(reg << MII_DATA_RA_SHIFT) |
		(phy_id << MII_DATA_PA_SHIFT) |
		BIT(MII_DATA_SB_SHIFT) |
		(MII_DATA_OP_READ << MII_DATA_OP_SHIFT);

	writel(cmd, priv->base + MII_DATA_OFFSET);

	rc = iproc_mdio_wait_for_idle(priv->base);
	if (rc)
		return rc;

	cmd = readl(priv->base + MII_DATA_OFFSET) & MII_DATA_MASK;

	return cmd;
}

static int iproc_mdio_write(struct mii_bus *bus, int phy_id,
			    int reg, u16 val)
{
	struct iproc_mdio_priv *priv = bus->priv;
	u32 cmd;
	int rc;

	rc = iproc_mdio_wait_for_idle(priv->base);
	if (rc)
		return rc;

	/* Prepare the write operation */
	cmd = (MII_DATA_TA_VAL << MII_DATA_TA_SHIFT) |
		(reg << MII_DATA_RA_SHIFT) |
		(phy_id << MII_DATA_PA_SHIFT) |
		BIT(MII_DATA_SB_SHIFT) |
		(MII_DATA_OP_WRITE << MII_DATA_OP_SHIFT) |
		((u32)(val) & MII_DATA_MASK);

	writel(cmd, priv->base + MII_DATA_OFFSET);

	rc = iproc_mdio_wait_for_idle(priv->base);
	if (rc)
		return rc;

	return 0;
}

static int iproc_mdio_probe(struct platform_device *pdev)
{
	struct iproc_mdio_priv *priv;
	struct mii_bus *bus;
	int rc;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base)) {
		dev_err(&pdev->dev, "failed to ioremap register\n");
		return PTR_ERR(priv->base);
	}

	priv->mii_bus = mdiobus_alloc();
	if (!priv->mii_bus) {
		dev_err(&pdev->dev, "MDIO bus alloc failed\n");
		return -ENOMEM;
	}

	bus = priv->mii_bus;
	bus->priv = priv;
	bus->name = "iProc MDIO bus";
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-%d", pdev->name, pdev->id);
	bus->parent = &pdev->dev;
	bus->read = iproc_mdio_read;
	bus->write = iproc_mdio_write;

	iproc_mdio_config_clk(priv->base);

	rc = of_mdiobus_register(bus, pdev->dev.of_node);
	if (rc) {
		dev_err(&pdev->dev, "MDIO bus registration failed\n");
		goto err_iproc_mdio;
	}

	platform_set_drvdata(pdev, priv);

	dev_info(&pdev->dev, "Broadcom iProc MDIO bus at 0x%p\n", priv->base);

	return 0;

err_iproc_mdio:
	mdiobus_free(bus);
	return rc;
}

static int iproc_mdio_remove(struct platform_device *pdev)
{
	struct iproc_mdio_priv *priv = platform_get_drvdata(pdev);

	mdiobus_unregister(priv->mii_bus);
	mdiobus_free(priv->mii_bus);

	return 0;
}

static const struct of_device_id iproc_mdio_of_match[] = {
	{ .compatible = "brcm,iproc-mdio", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, iproc_mdio_of_match);

static struct platform_driver iproc_mdio_driver = {
	.driver = {
		.name = "iproc-mdio",
		.of_match_table = iproc_mdio_of_match,
	},
	.probe = iproc_mdio_probe,
	.remove = iproc_mdio_remove,
};

module_platform_driver(iproc_mdio_driver);

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Broadcom iProc MDIO bus controller");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:iproc-mdio");

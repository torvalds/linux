
/* Copyright (c) 2014 Linaro Ltd.
 * Copyright (c) 2014 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/phy.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/of_mdio.h>
#include <linux/sched.h>
#include <linux/wait.h>

#define MDIO_CMD_REG		0x0
#define MDIO_ADDR_REG		0x4
#define MDIO_WDATA_REG		0x8
#define MDIO_RDATA_REG		0xc
#define MDIO_STA_REG		0x10

#define MDIO_START		BIT(14)
#define MDIO_R_VALID		BIT(1)
#define MDIO_READ	        (BIT(12) | BIT(11) | MDIO_START)
#define MDIO_WRITE	        (BIT(12) | BIT(10) | MDIO_START)

struct hip04_mdio_priv {
	struct mutex lock;
	void __iomem *base;
};

#define WAIT_TIMEOUT		10
static int hip04_mdio_wait_ready(struct mii_bus *bus)
{
	struct hip04_mdio_priv *priv = bus->priv;
	int i;

	for (i = 0; readl_relaxed(priv->base + MDIO_CMD_REG) & MDIO_START; i++) {
		if (i == WAIT_TIMEOUT)
			return -ETIMEDOUT;
		msleep(20);
	}

	return 0;
}

static int hip04_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct hip04_mdio_priv *priv = bus->priv;
	u32 val;
	int ret;
	mutex_lock(&priv->lock);

	ret = hip04_mdio_wait_ready(bus);
	if (ret < 0)
		goto out;

	val = (regnum & 0x1f) | ((mii_id & 0x1f) << 5) | MDIO_READ;
	writel_relaxed(val, priv->base + MDIO_CMD_REG);

	ret = hip04_mdio_wait_ready(bus);
	if (ret < 0)
		goto out;
	val = readl_relaxed(priv->base + MDIO_STA_REG);
	if (val & MDIO_R_VALID) {
		dev_err(bus->parent, "SMI bus read not valid\n");
		ret = -ENODEV;
		goto out;
	}
	val = readl_relaxed(priv->base + MDIO_RDATA_REG);
	ret = val & 0xFFFF;
out:
	mutex_unlock(&priv->lock);
	return ret;
}

static int hip04_mdio_write(struct mii_bus *bus, int mii_id,
			    int regnum, u16 value)
{
	struct hip04_mdio_priv *priv = bus->priv;
	u32 val;
	int ret;

	mutex_lock(&priv->lock);

	ret = hip04_mdio_wait_ready(bus);
	if (ret < 0)
		goto out;

	writel_relaxed(value, priv->base + MDIO_WDATA_REG);
	val = (regnum & 0x1f) | ((mii_id & 0x1f) << 5) | MDIO_WRITE;
	writel_relaxed(val, priv->base + MDIO_CMD_REG);
out:
	mutex_unlock(&priv->lock);
	return ret;
}


static int hip04_mdio_reset(struct mii_bus *bus)
{
	int temp, err, i;

	for (i = 0; i < 2; i++) {
		hip04_mdio_write(bus, i, 22, 0);
		temp = hip04_mdio_read(bus, i, MII_BMCR);
		temp |= BMCR_RESET;
		err = hip04_mdio_write(bus, i, MII_BMCR, temp);
		if (err < 0)
			return err;
	}

	mdelay(500);
	return 0;
}

static int hip04_mdio_probe(struct platform_device *pdev)
{
	struct resource *r;
	struct mii_bus *bus;
	struct hip04_mdio_priv *priv;
	int i, ret;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "No SMI register address given\n");
		return -ENODEV;
	}

	bus = mdiobus_alloc_size(sizeof(struct hip04_mdio_priv));
	if (!bus) {
		dev_err(&pdev->dev, "Cannot allocate MDIO bus\n");
		return -ENOMEM;
	}

	bus->name = "hip04_mdio_bus";
	bus->read = hip04_mdio_read;
	bus->write = hip04_mdio_write;
	bus->reset = hip04_mdio_reset;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-mii",
		 dev_name(&pdev->dev));
	bus->parent = &pdev->dev;
	priv = bus->priv;
	priv->base = devm_ioremap(&pdev->dev, r->start, resource_size(r));
	if (!priv->base) {
		dev_err(&pdev->dev, "Unable to remap SMI register\n");
		ret = -ENODEV;
		goto out_mdio;
	}

	mutex_init(&priv->lock);

	ret = of_mdiobus_register(bus, pdev->dev.of_node);
	if (ret < 0) {
		dev_err(&pdev->dev, "Cannot register MDIO bus (%d)\n", ret);
		goto out_mdio;
	}

	platform_set_drvdata(pdev, bus);

	return 0;

out_mdio:
	mdiobus_free(bus);
	return ret;
}

static int hip04_mdio_remove(struct platform_device *pdev)
{
	struct mii_bus *bus = platform_get_drvdata(pdev);

	mdiobus_unregister(bus);
	mdiobus_free(bus);

	return 0;
}

static const struct of_device_id hip04_mdio_match[] = {
	{ .compatible = "hisilicon,hip04-mdio" },
	{ }
};
MODULE_DEVICE_TABLE(of, hip04_mdio_match);

static struct platform_driver hip04_mdio_driver = {
	.probe = hip04_mdio_probe,
	.remove = hip04_mdio_remove,
	.driver = {
		.name = "hip04-mdio",
		.owner = THIS_MODULE,
		.of_match_table = hip04_mdio_match,
	},
};

module_platform_driver(hip04_mdio_driver);

MODULE_DESCRIPTION("HISILICON P04 MDIO interface driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:hip04-mdio");

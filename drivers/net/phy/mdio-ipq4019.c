// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2015, The Linux Foundation. All rights reserved. */
/* Copyright (c) 2020 Sartura Ltd. */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/of_address.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/platform_device.h>

#define MDIO_ADDR_REG				0x44
#define MDIO_DATA_WRITE_REG			0x48
#define MDIO_DATA_READ_REG			0x4c
#define MDIO_CMD_REG				0x50
#define MDIO_CMD_ACCESS_BUSY		BIT(16)
#define MDIO_CMD_ACCESS_START		BIT(8)
#define MDIO_CMD_ACCESS_CODE_READ	0
#define MDIO_CMD_ACCESS_CODE_WRITE	1

#define ipq4019_MDIO_TIMEOUT	10000
#define ipq4019_MDIO_SLEEP		10

struct ipq4019_mdio_data {
	void __iomem	*membase;
};

static int ipq4019_mdio_wait_busy(struct mii_bus *bus)
{
	struct ipq4019_mdio_data *priv = bus->priv;
	unsigned int busy;

	return readl_poll_timeout(priv->membase + MDIO_CMD_REG, busy,
				  (busy & MDIO_CMD_ACCESS_BUSY) == 0,
				  ipq4019_MDIO_SLEEP, ipq4019_MDIO_TIMEOUT);
}

static int ipq4019_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct ipq4019_mdio_data *priv = bus->priv;
	unsigned int cmd;

	/* Reject clause 45 */
	if (regnum & MII_ADDR_C45)
		return -EOPNOTSUPP;

	if (ipq4019_mdio_wait_busy(bus))
		return -ETIMEDOUT;

	/* issue the phy address and reg */
	writel((mii_id << 8) | regnum, priv->membase + MDIO_ADDR_REG);

	cmd = MDIO_CMD_ACCESS_START | MDIO_CMD_ACCESS_CODE_READ;

	/* issue read command */
	writel(cmd, priv->membase + MDIO_CMD_REG);

	/* Wait read complete */
	if (ipq4019_mdio_wait_busy(bus))
		return -ETIMEDOUT;

	/* Read and return data */
	return readl(priv->membase + MDIO_DATA_READ_REG);
}

static int ipq4019_mdio_write(struct mii_bus *bus, int mii_id, int regnum,
							 u16 value)
{
	struct ipq4019_mdio_data *priv = bus->priv;
	unsigned int cmd;

	/* Reject clause 45 */
	if (regnum & MII_ADDR_C45)
		return -EOPNOTSUPP;

	if (ipq4019_mdio_wait_busy(bus))
		return -ETIMEDOUT;

	/* issue the phy address and reg */
	writel((mii_id << 8) | regnum, priv->membase + MDIO_ADDR_REG);

	/* issue write data */
	writel(value, priv->membase + MDIO_DATA_WRITE_REG);

	cmd = MDIO_CMD_ACCESS_START | MDIO_CMD_ACCESS_CODE_WRITE;
	/* issue write command */
	writel(cmd, priv->membase + MDIO_CMD_REG);

	/* Wait write complete */
	if (ipq4019_mdio_wait_busy(bus))
		return -ETIMEDOUT;

	return 0;
}

static int ipq4019_mdio_probe(struct platform_device *pdev)
{
	struct ipq4019_mdio_data *priv;
	struct mii_bus *bus;
	int ret;

	bus = devm_mdiobus_alloc_size(&pdev->dev, sizeof(*priv));
	if (!bus)
		return -ENOMEM;

	priv = bus->priv;

	priv->membase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->membase))
		return PTR_ERR(priv->membase);

	bus->name = "ipq4019_mdio";
	bus->read = ipq4019_mdio_read;
	bus->write = ipq4019_mdio_write;
	bus->parent = &pdev->dev;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s%d", pdev->name, pdev->id);

	ret = of_mdiobus_register(bus, pdev->dev.of_node);
	if (ret) {
		dev_err(&pdev->dev, "Cannot register MDIO bus!\n");
		return ret;
	}

	platform_set_drvdata(pdev, bus);

	return 0;
}

static int ipq4019_mdio_remove(struct platform_device *pdev)
{
	struct mii_bus *bus = platform_get_drvdata(pdev);

	mdiobus_unregister(bus);

	return 0;
}

static const struct of_device_id ipq4019_mdio_dt_ids[] = {
	{ .compatible = "qcom,ipq4019-mdio" },
	{ }
};
MODULE_DEVICE_TABLE(of, ipq4019_mdio_dt_ids);

static struct platform_driver ipq4019_mdio_driver = {
	.probe = ipq4019_mdio_probe,
	.remove = ipq4019_mdio_remove,
	.driver = {
		.name = "ipq4019-mdio",
		.of_match_table = ipq4019_mdio_dt_ids,
	},
};

module_platform_driver(ipq4019_mdio_driver);

MODULE_DESCRIPTION("ipq4019 MDIO interface driver");
MODULE_AUTHOR("Qualcomm Atheros");
MODULE_LICENSE("Dual BSD/GPL");

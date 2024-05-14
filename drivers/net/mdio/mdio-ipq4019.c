// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2015, The Linux Foundation. All rights reserved. */
/* Copyright (c) 2020 Sartura Ltd. */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#define MDIO_MODE_REG				0x40
#define MDIO_ADDR_REG				0x44
#define MDIO_DATA_WRITE_REG			0x48
#define MDIO_DATA_READ_REG			0x4c
#define MDIO_CMD_REG				0x50
#define MDIO_CMD_ACCESS_BUSY		BIT(16)
#define MDIO_CMD_ACCESS_START		BIT(8)
#define MDIO_CMD_ACCESS_CODE_READ	0
#define MDIO_CMD_ACCESS_CODE_WRITE	1
#define MDIO_CMD_ACCESS_CODE_C45_ADDR	0
#define MDIO_CMD_ACCESS_CODE_C45_WRITE	1
#define MDIO_CMD_ACCESS_CODE_C45_READ	2

/* 0 = Clause 22, 1 = Clause 45 */
#define MDIO_MODE_C45				BIT(8)

#define IPQ4019_MDIO_TIMEOUT	10000
#define IPQ4019_MDIO_SLEEP		10

/* MDIO clock source frequency is fixed to 100M */
#define IPQ_MDIO_CLK_RATE	100000000

#define IPQ_PHY_SET_DELAY_US	100000

struct ipq4019_mdio_data {
	void __iomem	*membase;
	void __iomem *eth_ldo_rdy;
	struct clk *mdio_clk;
};

static int ipq4019_mdio_wait_busy(struct mii_bus *bus)
{
	struct ipq4019_mdio_data *priv = bus->priv;
	unsigned int busy;

	return readl_poll_timeout(priv->membase + MDIO_CMD_REG, busy,
				  (busy & MDIO_CMD_ACCESS_BUSY) == 0,
				  IPQ4019_MDIO_SLEEP, IPQ4019_MDIO_TIMEOUT);
}

static int ipq4019_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct ipq4019_mdio_data *priv = bus->priv;
	unsigned int data;
	unsigned int cmd;

	if (ipq4019_mdio_wait_busy(bus))
		return -ETIMEDOUT;

	/* Clause 45 support */
	if (regnum & MII_ADDR_C45) {
		unsigned int mmd = (regnum >> 16) & 0x1F;
		unsigned int reg = regnum & 0xFFFF;

		/* Enter Clause 45 mode */
		data = readl(priv->membase + MDIO_MODE_REG);

		data |= MDIO_MODE_C45;

		writel(data, priv->membase + MDIO_MODE_REG);

		/* issue the phy address and mmd */
		writel((mii_id << 8) | mmd, priv->membase + MDIO_ADDR_REG);

		/* issue reg */
		writel(reg, priv->membase + MDIO_DATA_WRITE_REG);

		cmd = MDIO_CMD_ACCESS_START | MDIO_CMD_ACCESS_CODE_C45_ADDR;
	} else {
		/* Enter Clause 22 mode */
		data = readl(priv->membase + MDIO_MODE_REG);

		data &= ~MDIO_MODE_C45;

		writel(data, priv->membase + MDIO_MODE_REG);

		/* issue the phy address and reg */
		writel((mii_id << 8) | regnum, priv->membase + MDIO_ADDR_REG);

		cmd = MDIO_CMD_ACCESS_START | MDIO_CMD_ACCESS_CODE_READ;
	}

	/* issue read command */
	writel(cmd, priv->membase + MDIO_CMD_REG);

	/* Wait read complete */
	if (ipq4019_mdio_wait_busy(bus))
		return -ETIMEDOUT;

	if (regnum & MII_ADDR_C45) {
		cmd = MDIO_CMD_ACCESS_START | MDIO_CMD_ACCESS_CODE_C45_READ;

		writel(cmd, priv->membase + MDIO_CMD_REG);

		if (ipq4019_mdio_wait_busy(bus))
			return -ETIMEDOUT;
	}

	/* Read and return data */
	return readl(priv->membase + MDIO_DATA_READ_REG);
}

static int ipq4019_mdio_write(struct mii_bus *bus, int mii_id, int regnum,
							 u16 value)
{
	struct ipq4019_mdio_data *priv = bus->priv;
	unsigned int data;
	unsigned int cmd;

	if (ipq4019_mdio_wait_busy(bus))
		return -ETIMEDOUT;

	/* Clause 45 support */
	if (regnum & MII_ADDR_C45) {
		unsigned int mmd = (regnum >> 16) & 0x1F;
		unsigned int reg = regnum & 0xFFFF;

		/* Enter Clause 45 mode */
		data = readl(priv->membase + MDIO_MODE_REG);

		data |= MDIO_MODE_C45;

		writel(data, priv->membase + MDIO_MODE_REG);

		/* issue the phy address and mmd */
		writel((mii_id << 8) | mmd, priv->membase + MDIO_ADDR_REG);

		/* issue reg */
		writel(reg, priv->membase + MDIO_DATA_WRITE_REG);

		cmd = MDIO_CMD_ACCESS_START | MDIO_CMD_ACCESS_CODE_C45_ADDR;

		writel(cmd, priv->membase + MDIO_CMD_REG);

		if (ipq4019_mdio_wait_busy(bus))
			return -ETIMEDOUT;
	} else {
		/* Enter Clause 22 mode */
		data = readl(priv->membase + MDIO_MODE_REG);

		data &= ~MDIO_MODE_C45;

		writel(data, priv->membase + MDIO_MODE_REG);

		/* issue the phy address and reg */
		writel((mii_id << 8) | regnum, priv->membase + MDIO_ADDR_REG);
	}

	/* issue write data */
	writel(value, priv->membase + MDIO_DATA_WRITE_REG);

	/* issue write command */
	if (regnum & MII_ADDR_C45)
		cmd = MDIO_CMD_ACCESS_START | MDIO_CMD_ACCESS_CODE_C45_WRITE;
	else
		cmd = MDIO_CMD_ACCESS_START | MDIO_CMD_ACCESS_CODE_WRITE;

	writel(cmd, priv->membase + MDIO_CMD_REG);

	/* Wait write complete */
	if (ipq4019_mdio_wait_busy(bus))
		return -ETIMEDOUT;

	return 0;
}

static int ipq_mdio_reset(struct mii_bus *bus)
{
	struct ipq4019_mdio_data *priv = bus->priv;
	u32 val;
	int ret;

	/* To indicate CMN_PLL that ethernet_ldo has been ready if platform resource 1
	 * is specified in the device tree.
	 */
	if (priv->eth_ldo_rdy) {
		val = readl(priv->eth_ldo_rdy);
		val |= BIT(0);
		writel(val, priv->eth_ldo_rdy);
		fsleep(IPQ_PHY_SET_DELAY_US);
	}

	/* Configure MDIO clock source frequency if clock is specified in the device tree */
	ret = clk_set_rate(priv->mdio_clk, IPQ_MDIO_CLK_RATE);
	if (ret)
		return ret;

	ret = clk_prepare_enable(priv->mdio_clk);
	if (ret == 0)
		mdelay(10);

	return ret;
}

static int ipq4019_mdio_probe(struct platform_device *pdev)
{
	struct ipq4019_mdio_data *priv;
	struct mii_bus *bus;
	struct resource *res;
	int ret;

	bus = devm_mdiobus_alloc_size(&pdev->dev, sizeof(*priv));
	if (!bus)
		return -ENOMEM;

	priv = bus->priv;

	priv->membase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->membase))
		return PTR_ERR(priv->membase);

	priv->mdio_clk = devm_clk_get_optional(&pdev->dev, "gcc_mdio_ahb_clk");
	if (IS_ERR(priv->mdio_clk))
		return PTR_ERR(priv->mdio_clk);

	/* The platform resource is provided on the chipset IPQ5018 */
	/* This resource is optional */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res)
		priv->eth_ldo_rdy = devm_ioremap_resource(&pdev->dev, res);

	bus->name = "ipq4019_mdio";
	bus->read = ipq4019_mdio_read;
	bus->write = ipq4019_mdio_write;
	bus->reset = ipq_mdio_reset;
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
	{ .compatible = "qcom,ipq5018-mdio" },
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

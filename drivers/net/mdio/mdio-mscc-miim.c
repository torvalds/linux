// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Driver for the MDIO interface of Microsemi network switches.
 *
 * Author: Alexandre Belloni <alexandre.belloni@bootlin.com>
 * Copyright (c) 2017 Microsemi Corporation
 */

#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/platform_device.h>

#define MSCC_MIIM_REG_STATUS		0x0
#define		MSCC_MIIM_STATUS_STAT_PENDING	BIT(2)
#define		MSCC_MIIM_STATUS_STAT_BUSY	BIT(3)
#define MSCC_MIIM_REG_CMD		0x8
#define		MSCC_MIIM_CMD_OPR_WRITE		BIT(1)
#define		MSCC_MIIM_CMD_OPR_READ		BIT(2)
#define		MSCC_MIIM_CMD_WRDATA_SHIFT	4
#define		MSCC_MIIM_CMD_REGAD_SHIFT	20
#define		MSCC_MIIM_CMD_PHYAD_SHIFT	25
#define		MSCC_MIIM_CMD_VLD		BIT(31)
#define MSCC_MIIM_REG_DATA		0xC
#define		MSCC_MIIM_DATA_ERROR		(BIT(16) | BIT(17))

#define MSCC_PHY_REG_PHY_CFG	0x0
#define		PHY_CFG_PHY_ENA		(BIT(0) | BIT(1) | BIT(2) | BIT(3))
#define		PHY_CFG_PHY_COMMON_RESET BIT(4)
#define		PHY_CFG_PHY_RESET	(BIT(5) | BIT(6) | BIT(7) | BIT(8))
#define MSCC_PHY_REG_PHY_STATUS	0x4

struct mscc_miim_dev {
	void __iomem *regs;
	void __iomem *phy_regs;
};

/* When high resolution timers aren't built-in: we can't use usleep_range() as
 * we would sleep way too long. Use udelay() instead.
 */
#define mscc_readl_poll_timeout(addr, val, cond, delay_us, timeout_us)	\
({									\
	if (!IS_ENABLED(CONFIG_HIGH_RES_TIMERS))			\
		readl_poll_timeout_atomic(addr, val, cond, delay_us,	\
					  timeout_us);			\
	readl_poll_timeout(addr, val, cond, delay_us, timeout_us);	\
})

static int mscc_miim_wait_ready(struct mii_bus *bus)
{
	struct mscc_miim_dev *miim = bus->priv;
	u32 val;

	return mscc_readl_poll_timeout(miim->regs + MSCC_MIIM_REG_STATUS, val,
				       !(val & MSCC_MIIM_STATUS_STAT_BUSY), 50,
				       10000);
}

static int mscc_miim_wait_pending(struct mii_bus *bus)
{
	struct mscc_miim_dev *miim = bus->priv;
	u32 val;

	return mscc_readl_poll_timeout(miim->regs + MSCC_MIIM_REG_STATUS, val,
				       !(val & MSCC_MIIM_STATUS_STAT_PENDING),
				       50, 10000);
}

static int mscc_miim_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct mscc_miim_dev *miim = bus->priv;
	u32 val;
	int ret;

	if (regnum & MII_ADDR_C45)
		return -EOPNOTSUPP;

	ret = mscc_miim_wait_pending(bus);
	if (ret)
		goto out;

	writel(MSCC_MIIM_CMD_VLD | (mii_id << MSCC_MIIM_CMD_PHYAD_SHIFT) |
	       (regnum << MSCC_MIIM_CMD_REGAD_SHIFT) | MSCC_MIIM_CMD_OPR_READ,
	       miim->regs + MSCC_MIIM_REG_CMD);

	ret = mscc_miim_wait_ready(bus);
	if (ret)
		goto out;

	val = readl(miim->regs + MSCC_MIIM_REG_DATA);
	if (val & MSCC_MIIM_DATA_ERROR) {
		ret = -EIO;
		goto out;
	}

	ret = val & 0xFFFF;
out:
	return ret;
}

static int mscc_miim_write(struct mii_bus *bus, int mii_id,
			   int regnum, u16 value)
{
	struct mscc_miim_dev *miim = bus->priv;
	int ret;

	if (regnum & MII_ADDR_C45)
		return -EOPNOTSUPP;

	ret = mscc_miim_wait_pending(bus);
	if (ret < 0)
		goto out;

	writel(MSCC_MIIM_CMD_VLD | (mii_id << MSCC_MIIM_CMD_PHYAD_SHIFT) |
	       (regnum << MSCC_MIIM_CMD_REGAD_SHIFT) |
	       (value << MSCC_MIIM_CMD_WRDATA_SHIFT) |
	       MSCC_MIIM_CMD_OPR_WRITE,
	       miim->regs + MSCC_MIIM_REG_CMD);

out:
	return ret;
}

static int mscc_miim_reset(struct mii_bus *bus)
{
	struct mscc_miim_dev *miim = bus->priv;

	if (miim->phy_regs) {
		writel(0, miim->phy_regs + MSCC_PHY_REG_PHY_CFG);
		writel(0x1ff, miim->phy_regs + MSCC_PHY_REG_PHY_CFG);
		mdelay(500);
	}

	return 0;
}

static int mscc_miim_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct mii_bus *bus;
	struct mscc_miim_dev *dev;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	bus = devm_mdiobus_alloc_size(&pdev->dev, sizeof(*dev));
	if (!bus)
		return -ENOMEM;

	bus->name = "mscc_miim";
	bus->read = mscc_miim_read;
	bus->write = mscc_miim_write;
	bus->reset = mscc_miim_reset;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-mii", dev_name(&pdev->dev));
	bus->parent = &pdev->dev;

	dev = bus->priv;
	dev->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(dev->regs)) {
		dev_err(&pdev->dev, "Unable to map MIIM registers\n");
		return PTR_ERR(dev->regs);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res) {
		dev->phy_regs = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(dev->phy_regs)) {
			dev_err(&pdev->dev, "Unable to map internal phy registers\n");
			return PTR_ERR(dev->phy_regs);
		}
	}

	ret = of_mdiobus_register(bus, pdev->dev.of_node);
	if (ret < 0) {
		dev_err(&pdev->dev, "Cannot register MDIO bus (%d)\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, bus);

	return 0;
}

static int mscc_miim_remove(struct platform_device *pdev)
{
	struct mii_bus *bus = platform_get_drvdata(pdev);

	mdiobus_unregister(bus);

	return 0;
}

static const struct of_device_id mscc_miim_match[] = {
	{ .compatible = "mscc,ocelot-miim" },
	{ }
};
MODULE_DEVICE_TABLE(of, mscc_miim_match);

static struct platform_driver mscc_miim_driver = {
	.probe = mscc_miim_probe,
	.remove = mscc_miim_remove,
	.driver = {
		.name = "mscc-miim",
		.of_match_table = mscc_miim_match,
	},
};

module_platform_driver(mscc_miim_driver);

MODULE_DESCRIPTION("Microsemi MIIM driver");
MODULE_AUTHOR("Alexandre Belloni <alexandre.belloni@bootlin.com>");
MODULE_LICENSE("Dual MIT/GPL");

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
#include <linux/mdio/mdio-mscc-miim.h>
#include <linux/module.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

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
	struct regmap *regs;
	int mii_status_offset;
	struct regmap *phy_regs;
	int phy_reset_offset;
};

/* When high resolution timers aren't built-in: we can't use usleep_range() as
 * we would sleep way too long. Use udelay() instead.
 */
#define mscc_readx_poll_timeout(op, addr, val, cond, delay_us, timeout_us)\
({									  \
	if (!IS_ENABLED(CONFIG_HIGH_RES_TIMERS))			  \
		readx_poll_timeout_atomic(op, addr, val, cond, delay_us,  \
					  timeout_us);			  \
	readx_poll_timeout(op, addr, val, cond, delay_us, timeout_us);	  \
})

static int mscc_miim_status(struct mii_bus *bus)
{
	struct mscc_miim_dev *miim = bus->priv;
	int val, ret;

	ret = regmap_read(miim->regs,
			  MSCC_MIIM_REG_STATUS + miim->mii_status_offset, &val);
	if (ret < 0) {
		WARN_ONCE(1, "mscc miim status read error %d\n", ret);
		return ret;
	}

	return val;
}

static int mscc_miim_wait_ready(struct mii_bus *bus)
{
	u32 val;

	return mscc_readx_poll_timeout(mscc_miim_status, bus, val,
				       !(val & MSCC_MIIM_STATUS_STAT_BUSY), 50,
				       10000);
}

static int mscc_miim_wait_pending(struct mii_bus *bus)
{
	u32 val;

	return mscc_readx_poll_timeout(mscc_miim_status, bus, val,
				       !(val & MSCC_MIIM_STATUS_STAT_PENDING),
				       50, 10000);
}

static int mscc_miim_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct mscc_miim_dev *miim = bus->priv;
	u32 val;
	int ret;

	ret = mscc_miim_wait_pending(bus);
	if (ret)
		goto out;

	ret = regmap_write(miim->regs,
			   MSCC_MIIM_REG_CMD + miim->mii_status_offset,
			   MSCC_MIIM_CMD_VLD |
			   (mii_id << MSCC_MIIM_CMD_PHYAD_SHIFT) |
			   (regnum << MSCC_MIIM_CMD_REGAD_SHIFT) |
			   MSCC_MIIM_CMD_OPR_READ);

	if (ret < 0) {
		WARN_ONCE(1, "mscc miim write cmd reg error %d\n", ret);
		goto out;
	}

	ret = mscc_miim_wait_ready(bus);
	if (ret)
		goto out;

	ret = regmap_read(miim->regs,
			  MSCC_MIIM_REG_DATA + miim->mii_status_offset, &val);
	if (ret < 0) {
		WARN_ONCE(1, "mscc miim read data reg error %d\n", ret);
		goto out;
	}

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

	ret = mscc_miim_wait_pending(bus);
	if (ret < 0)
		goto out;

	ret = regmap_write(miim->regs,
			   MSCC_MIIM_REG_CMD + miim->mii_status_offset,
			   MSCC_MIIM_CMD_VLD |
			   (mii_id << MSCC_MIIM_CMD_PHYAD_SHIFT) |
			   (regnum << MSCC_MIIM_CMD_REGAD_SHIFT) |
			   (value << MSCC_MIIM_CMD_WRDATA_SHIFT) |
			   MSCC_MIIM_CMD_OPR_WRITE);

	if (ret < 0)
		WARN_ONCE(1, "mscc miim write error %d\n", ret);
out:
	return ret;
}

static int mscc_miim_reset(struct mii_bus *bus)
{
	struct mscc_miim_dev *miim = bus->priv;
	int offset = miim->phy_reset_offset;
	int ret;

	if (miim->phy_regs) {
		ret = regmap_write(miim->phy_regs,
				   MSCC_PHY_REG_PHY_CFG + offset, 0);
		if (ret < 0) {
			WARN_ONCE(1, "mscc reset set error %d\n", ret);
			return ret;
		}

		ret = regmap_write(miim->phy_regs,
				   MSCC_PHY_REG_PHY_CFG + offset, 0x1ff);
		if (ret < 0) {
			WARN_ONCE(1, "mscc reset clear error %d\n", ret);
			return ret;
		}

		mdelay(500);
	}

	return 0;
}

static const struct regmap_config mscc_miim_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
};

int mscc_miim_setup(struct device *dev, struct mii_bus **pbus, const char *name,
		    struct regmap *mii_regmap, int status_offset)
{
	struct mscc_miim_dev *miim;
	struct mii_bus *bus;

	bus = devm_mdiobus_alloc_size(dev, sizeof(*miim));
	if (!bus)
		return -ENOMEM;

	bus->name = name;
	bus->read = mscc_miim_read;
	bus->write = mscc_miim_write;
	bus->reset = mscc_miim_reset;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-mii", dev_name(dev));
	bus->parent = dev;

	miim = bus->priv;

	*pbus = bus;

	miim->regs = mii_regmap;
	miim->mii_status_offset = status_offset;

	*pbus = bus;

	return 0;
}
EXPORT_SYMBOL(mscc_miim_setup);

static int mscc_miim_probe(struct platform_device *pdev)
{
	struct regmap *mii_regmap, *phy_regmap = NULL;
	void __iomem *regs, *phy_regs;
	struct mscc_miim_dev *miim;
	struct resource *res;
	struct mii_bus *bus;
	int ret;

	regs = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(regs)) {
		dev_err(&pdev->dev, "Unable to map MIIM registers\n");
		return PTR_ERR(regs);
	}

	mii_regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					   &mscc_miim_regmap_config);

	if (IS_ERR(mii_regmap)) {
		dev_err(&pdev->dev, "Unable to create MIIM regmap\n");
		return PTR_ERR(mii_regmap);
	}

	/* This resource is optional */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res) {
		phy_regs = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(phy_regs)) {
			dev_err(&pdev->dev, "Unable to map internal phy registers\n");
			return PTR_ERR(phy_regs);
		}

		phy_regmap = devm_regmap_init_mmio(&pdev->dev, phy_regs,
						   &mscc_miim_regmap_config);
		if (IS_ERR(phy_regmap)) {
			dev_err(&pdev->dev, "Unable to create phy register regmap\n");
			return PTR_ERR(phy_regmap);
		}
	}

	ret = mscc_miim_setup(&pdev->dev, &bus, "mscc_miim", mii_regmap, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to setup the MDIO bus\n");
		return ret;
	}

	miim = bus->priv;
	miim->phy_regs = phy_regmap;
	miim->phy_reset_offset = 0;

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

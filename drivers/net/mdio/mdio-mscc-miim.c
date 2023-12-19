// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Driver for the MDIO interface of Microsemi network switches.
 *
 * Author: Alexandre Belloni <alexandre.belloni@bootlin.com>
 * Copyright (c) 2017 Microsemi Corporation
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mdio/mdio-mscc-miim.h>
#include <linux/mfd/ocelot.h>
#include <linux/module.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/property.h>
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
#define MSCC_MIIM_REG_CFG		0x10
#define		MSCC_MIIM_CFG_PRESCALE_MASK	GENMASK(7, 0)

#define MSCC_PHY_REG_PHY_CFG	0x0
#define		PHY_CFG_PHY_ENA		(BIT(0) | BIT(1) | BIT(2) | BIT(3))
#define		PHY_CFG_PHY_COMMON_RESET BIT(4)
#define		PHY_CFG_PHY_RESET	(BIT(5) | BIT(6) | BIT(7) | BIT(8))
#define MSCC_PHY_REG_PHY_STATUS	0x4

#define LAN966X_CUPHY_COMMON_CFG	0x0
#define		CUPHY_COMMON_CFG_RESET_N	BIT(0)

struct mscc_miim_info {
	unsigned int phy_reset_offset;
	unsigned int phy_reset_bits;
};

struct mscc_miim_dev {
	struct regmap *regs;
	int mii_status_offset;
	bool ignore_read_errors;
	struct regmap *phy_regs;
	const struct mscc_miim_info *info;
	struct clk *clk;
	u32 bus_freq;
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

	if (!miim->ignore_read_errors && !!(val & MSCC_MIIM_DATA_ERROR)) {
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
	unsigned int offset, bits;
	int ret;

	if (!miim->phy_regs)
		return 0;

	offset = miim->info->phy_reset_offset;
	bits = miim->info->phy_reset_bits;

	ret = regmap_update_bits(miim->phy_regs, offset, bits, 0);
	if (ret < 0) {
		WARN_ONCE(1, "mscc reset set error %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(miim->phy_regs, offset, bits, bits);
	if (ret < 0) {
		WARN_ONCE(1, "mscc reset clear error %d\n", ret);
		return ret;
	}

	mdelay(500);

	return 0;
}

static const struct regmap_config mscc_miim_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
};

static const struct regmap_config mscc_miim_phy_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.name		= "phy",
};

int mscc_miim_setup(struct device *dev, struct mii_bus **pbus, const char *name,
		    struct regmap *mii_regmap, int status_offset,
		    bool ignore_read_errors)
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
	miim->ignore_read_errors = ignore_read_errors;

	*pbus = bus;

	return 0;
}
EXPORT_SYMBOL(mscc_miim_setup);

static int mscc_miim_clk_set(struct mii_bus *bus)
{
	struct mscc_miim_dev *miim = bus->priv;
	unsigned long rate;
	u32 div;

	/* Keep the current settings */
	if (!miim->bus_freq)
		return 0;

	rate = clk_get_rate(miim->clk);

	div = DIV_ROUND_UP(rate, 2 * miim->bus_freq) - 1;
	if (div == 0 || div & ~MSCC_MIIM_CFG_PRESCALE_MASK) {
		dev_err(&bus->dev, "Incorrect MDIO clock frequency\n");
		return -EINVAL;
	}

	return regmap_update_bits(miim->regs, MSCC_MIIM_REG_CFG,
				  MSCC_MIIM_CFG_PRESCALE_MASK, div);
}

static int mscc_miim_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct regmap *mii_regmap, *phy_regmap;
	struct device *dev = &pdev->dev;
	struct mscc_miim_dev *miim;
	struct mii_bus *bus;
	int ret;

	mii_regmap = ocelot_regmap_from_resource(pdev, 0,
						 &mscc_miim_regmap_config);
	if (IS_ERR(mii_regmap))
		return dev_err_probe(dev, PTR_ERR(mii_regmap),
				     "Unable to create MIIM regmap\n");

	/* This resource is optional */
	phy_regmap = ocelot_regmap_from_resource_optional(pdev, 1,
						 &mscc_miim_phy_regmap_config);
	if (IS_ERR(phy_regmap))
		return dev_err_probe(dev, PTR_ERR(phy_regmap),
				     "Unable to create phy register regmap\n");

	ret = mscc_miim_setup(dev, &bus, "mscc_miim", mii_regmap, 0, false);
	if (ret < 0) {
		dev_err(dev, "Unable to setup the MDIO bus\n");
		return ret;
	}

	miim = bus->priv;
	miim->phy_regs = phy_regmap;

	miim->info = device_get_match_data(dev);
	if (!miim->info)
		return -EINVAL;

	miim->clk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(miim->clk))
		return PTR_ERR(miim->clk);

	of_property_read_u32(np, "clock-frequency", &miim->bus_freq);

	if (miim->bus_freq && !miim->clk) {
		dev_err(dev, "cannot use clock-frequency without a clock\n");
		return -EINVAL;
	}

	ret = clk_prepare_enable(miim->clk);
	if (ret)
		return ret;

	ret = mscc_miim_clk_set(bus);
	if (ret)
		goto out_disable_clk;

	ret = of_mdiobus_register(bus, np);
	if (ret < 0) {
		dev_err(dev, "Cannot register MDIO bus (%d)\n", ret);
		goto out_disable_clk;
	}

	platform_set_drvdata(pdev, bus);

	return 0;

out_disable_clk:
	clk_disable_unprepare(miim->clk);
	return ret;
}

static void mscc_miim_remove(struct platform_device *pdev)
{
	struct mii_bus *bus = platform_get_drvdata(pdev);
	struct mscc_miim_dev *miim = bus->priv;

	clk_disable_unprepare(miim->clk);
	mdiobus_unregister(bus);
}

static const struct mscc_miim_info mscc_ocelot_miim_info = {
	.phy_reset_offset = MSCC_PHY_REG_PHY_CFG,
	.phy_reset_bits = PHY_CFG_PHY_ENA | PHY_CFG_PHY_COMMON_RESET |
			  PHY_CFG_PHY_RESET,
};

static const struct mscc_miim_info microchip_lan966x_miim_info = {
	.phy_reset_offset = LAN966X_CUPHY_COMMON_CFG,
	.phy_reset_bits = CUPHY_COMMON_CFG_RESET_N,
};

static const struct of_device_id mscc_miim_match[] = {
	{
		.compatible = "mscc,ocelot-miim",
		.data = &mscc_ocelot_miim_info
	}, {
		.compatible = "microchip,lan966x-miim",
		.data = &microchip_lan966x_miim_info
	},
	{ }
};
MODULE_DEVICE_TABLE(of, mscc_miim_match);

static struct platform_driver mscc_miim_driver = {
	.probe = mscc_miim_probe,
	.remove_new = mscc_miim_remove,
	.driver = {
		.name = "mscc-miim",
		.of_match_table = mscc_miim_match,
	},
};

module_platform_driver(mscc_miim_driver);

MODULE_DESCRIPTION("Microsemi MIIM driver");
MODULE_AUTHOR("Alexandre Belloni <alexandre.belloni@bootlin.com>");
MODULE_LICENSE("Dual MIT/GPL");

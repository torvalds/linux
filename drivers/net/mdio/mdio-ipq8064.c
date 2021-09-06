// SPDX-License-Identifier: GPL-2.0
/* Qualcomm IPQ8064 MDIO interface driver
 *
 * Copyright (C) 2019 Christian Lamparter <chunkeey@gmail.com>
 * Copyright (C) 2020 Ansuel Smith <ansuelsmth@gmail.com>
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* MII address register definitions */
#define MII_ADDR_REG_ADDR                       0x10
#define MII_BUSY                                BIT(0)
#define MII_WRITE                               BIT(1)
#define MII_CLKRANGE_60_100M                    (0 << 2)
#define MII_CLKRANGE_100_150M                   (1 << 2)
#define MII_CLKRANGE_20_35M                     (2 << 2)
#define MII_CLKRANGE_35_60M                     (3 << 2)
#define MII_CLKRANGE_150_250M                   (4 << 2)
#define MII_CLKRANGE_250_300M                   (5 << 2)
#define MII_CLKRANGE_MASK			GENMASK(4, 2)
#define MII_REG_SHIFT				6
#define MII_REG_MASK				GENMASK(10, 6)
#define MII_ADDR_SHIFT				11
#define MII_ADDR_MASK				GENMASK(15, 11)

#define MII_DATA_REG_ADDR                       0x14

#define MII_MDIO_DELAY_USEC                     (1000)
#define MII_MDIO_RETRY_MSEC                     (10)

struct ipq8064_mdio {
	struct regmap *base; /* NSS_GMAC0_BASE */
};

static int
ipq8064_mdio_wait_busy(struct ipq8064_mdio *priv)
{
	u32 busy;

	return regmap_read_poll_timeout(priv->base, MII_ADDR_REG_ADDR, busy,
					!(busy & MII_BUSY), MII_MDIO_DELAY_USEC,
					MII_MDIO_RETRY_MSEC * USEC_PER_MSEC);
}

static int
ipq8064_mdio_read(struct mii_bus *bus, int phy_addr, int reg_offset)
{
	u32 miiaddr = MII_BUSY | MII_CLKRANGE_250_300M;
	struct ipq8064_mdio *priv = bus->priv;
	u32 ret_val;
	int err;

	/* Reject clause 45 */
	if (reg_offset & MII_ADDR_C45)
		return -EOPNOTSUPP;

	miiaddr |= ((phy_addr << MII_ADDR_SHIFT) & MII_ADDR_MASK) |
		   ((reg_offset << MII_REG_SHIFT) & MII_REG_MASK);

	regmap_write(priv->base, MII_ADDR_REG_ADDR, miiaddr);
	usleep_range(8, 10);

	err = ipq8064_mdio_wait_busy(priv);
	if (err)
		return err;

	regmap_read(priv->base, MII_DATA_REG_ADDR, &ret_val);
	return (int)ret_val;
}

static int
ipq8064_mdio_write(struct mii_bus *bus, int phy_addr, int reg_offset, u16 data)
{
	u32 miiaddr = MII_WRITE | MII_BUSY | MII_CLKRANGE_250_300M;
	struct ipq8064_mdio *priv = bus->priv;

	/* Reject clause 45 */
	if (reg_offset & MII_ADDR_C45)
		return -EOPNOTSUPP;

	regmap_write(priv->base, MII_DATA_REG_ADDR, data);

	miiaddr |= ((phy_addr << MII_ADDR_SHIFT) & MII_ADDR_MASK) |
		   ((reg_offset << MII_REG_SHIFT) & MII_REG_MASK);

	regmap_write(priv->base, MII_ADDR_REG_ADDR, miiaddr);
	usleep_range(8, 10);

	return ipq8064_mdio_wait_busy(priv);
}

static int
ipq8064_mdio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct ipq8064_mdio *priv;
	struct mii_bus *bus;
	int ret;

	bus = devm_mdiobus_alloc_size(&pdev->dev, sizeof(*priv));
	if (!bus)
		return -ENOMEM;

	bus->name = "ipq8064_mdio_bus";
	bus->read = ipq8064_mdio_read;
	bus->write = ipq8064_mdio_write;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-mii", dev_name(&pdev->dev));
	bus->parent = &pdev->dev;

	priv = bus->priv;
	priv->base = device_node_to_regmap(np);
	if (IS_ERR(priv->base)) {
		if (priv->base == ERR_PTR(-EPROBE_DEFER))
			return -EPROBE_DEFER;

		dev_err(&pdev->dev, "error getting device regmap, error=%pe\n",
			priv->base);
		return PTR_ERR(priv->base);
	}

	ret = of_mdiobus_register(bus, np);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, bus);
	return 0;
}

static int
ipq8064_mdio_remove(struct platform_device *pdev)
{
	struct mii_bus *bus = platform_get_drvdata(pdev);

	mdiobus_unregister(bus);

	return 0;
}

static const struct of_device_id ipq8064_mdio_dt_ids[] = {
	{ .compatible = "qcom,ipq8064-mdio" },
	{ }
};
MODULE_DEVICE_TABLE(of, ipq8064_mdio_dt_ids);

static struct platform_driver ipq8064_mdio_driver = {
	.probe = ipq8064_mdio_probe,
	.remove = ipq8064_mdio_remove,
	.driver = {
		.name = "ipq8064-mdio",
		.of_match_table = ipq8064_mdio_dt_ids,
	},
};

module_platform_driver(ipq8064_mdio_driver);

MODULE_DESCRIPTION("Qualcomm IPQ8064 MDIO interface driver");
MODULE_AUTHOR("Christian Lamparter <chunkeey@gmail.com>");
MODULE_AUTHOR("Ansuel Smith <ansuelsmth@gmail.com>");
MODULE_LICENSE("GPL");

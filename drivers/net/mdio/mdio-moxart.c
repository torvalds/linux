// SPDX-License-Identifier: GPL-2.0
/* MOXA ART Ethernet (RTL8201CP) MDIO interface driver
 *
 * Copyright (C) 2013 Jonas Jensen <jonas.jensen@gmail.com>
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_address.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/platform_device.h>

#define REG_PHY_CTRL            0
#define REG_PHY_WRITE_DATA      4

/* REG_PHY_CTRL */
#define MIIWR                   BIT(27) /* init write sequence (auto cleared)*/
#define MIIRD                   BIT(26)
#define REGAD_MASK              0x3e00000
#define PHYAD_MASK              0x1f0000
#define MIIRDATA_MASK           0xffff

/* REG_PHY_WRITE_DATA */
#define MIIWDATA_MASK           0xffff

struct moxart_mdio_data {
	void __iomem		*base;
};

static int moxart_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct moxart_mdio_data *data = bus->priv;
	u32 ctrl = 0;
	unsigned int count = 5;

	dev_dbg(&bus->dev, "%s\n", __func__);

	ctrl |= MIIRD | ((mii_id << 16) & PHYAD_MASK) |
		((regnum << 21) & REGAD_MASK);

	writel(ctrl, data->base + REG_PHY_CTRL);

	do {
		ctrl = readl(data->base + REG_PHY_CTRL);

		if (!(ctrl & MIIRD))
			return ctrl & MIIRDATA_MASK;

		mdelay(10);
		count--;
	} while (count > 0);

	dev_dbg(&bus->dev, "%s timed out\n", __func__);

	return -ETIMEDOUT;
}

static int moxart_mdio_write(struct mii_bus *bus, int mii_id,
			     int regnum, u16 value)
{
	struct moxart_mdio_data *data = bus->priv;
	u32 ctrl = 0;
	unsigned int count = 5;

	dev_dbg(&bus->dev, "%s\n", __func__);

	ctrl |= MIIWR | ((mii_id << 16) & PHYAD_MASK) |
		((regnum << 21) & REGAD_MASK);

	value &= MIIWDATA_MASK;

	writel(value, data->base + REG_PHY_WRITE_DATA);
	writel(ctrl, data->base + REG_PHY_CTRL);

	do {
		ctrl = readl(data->base + REG_PHY_CTRL);

		if (!(ctrl & MIIWR))
			return 0;

		mdelay(10);
		count--;
	} while (count > 0);

	dev_dbg(&bus->dev, "%s timed out\n", __func__);

	return -ETIMEDOUT;
}

static int moxart_mdio_reset(struct mii_bus *bus)
{
	int data, i;

	for (i = 0; i < PHY_MAX_ADDR; i++) {
		data = moxart_mdio_read(bus, i, MII_BMCR);
		if (data < 0)
			continue;

		data |= BMCR_RESET;
		if (moxart_mdio_write(bus, i, MII_BMCR, data) < 0)
			continue;
	}

	return 0;
}

static int moxart_mdio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct mii_bus *bus;
	struct moxart_mdio_data *data;
	int ret, i;

	bus = mdiobus_alloc_size(sizeof(*data));
	if (!bus)
		return -ENOMEM;

	bus->name = "MOXA ART Ethernet MII";
	bus->read = &moxart_mdio_read;
	bus->write = &moxart_mdio_write;
	bus->reset = &moxart_mdio_reset;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-%d-mii", pdev->name, pdev->id);
	bus->parent = &pdev->dev;

	/* Setting PHY_MAC_INTERRUPT here even if it has no effect,
	 * of_mdiobus_register() sets these PHY_POLL.
	 * Ideally, the interrupt from MAC controller could be used to
	 * detect link state changes, not polling, i.e. if there was
	 * a way phy_driver could set PHY_HAS_INTERRUPT but have that
	 * interrupt handled in ethernet drivercode.
	 */
	for (i = 0; i < PHY_MAX_ADDR; i++)
		bus->irq[i] = PHY_MAC_INTERRUPT;

	data = bus->priv;
	data->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(data->base)) {
		ret = PTR_ERR(data->base);
		goto err_out_free_mdiobus;
	}

	ret = of_mdiobus_register(bus, np);
	if (ret < 0)
		goto err_out_free_mdiobus;

	platform_set_drvdata(pdev, bus);

	return 0;

err_out_free_mdiobus:
	mdiobus_free(bus);
	return ret;
}

static void moxart_mdio_remove(struct platform_device *pdev)
{
	struct mii_bus *bus = platform_get_drvdata(pdev);

	mdiobus_unregister(bus);
	mdiobus_free(bus);
}

static const struct of_device_id moxart_mdio_dt_ids[] = {
	{ .compatible = "moxa,moxart-mdio" },
	{ }
};
MODULE_DEVICE_TABLE(of, moxart_mdio_dt_ids);

static struct platform_driver moxart_mdio_driver = {
	.probe = moxart_mdio_probe,
	.remove = moxart_mdio_remove,
	.driver = {
		.name = "moxart-mdio",
		.of_match_table = moxart_mdio_dt_ids,
	},
};

module_platform_driver(moxart_mdio_driver);

MODULE_DESCRIPTION("MOXA ART MDIO interface driver");
MODULE_AUTHOR("Jonas Jensen <jonas.jensen@gmail.com>");
MODULE_LICENSE("GPL v2");

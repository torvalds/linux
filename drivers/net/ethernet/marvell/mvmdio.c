/*
 * Driver for the MDIO interface of Marvell network interfaces.
 *
 * Since the MDIO interface of Marvell network interfaces is shared
 * between all network interfaces, having a single driver allows to
 * handle concurrent accesses properly (you may have four Ethernet
 * ports, but they in fact share the same SMI interface to access the
 * MDIO bus). Moreover, this MDIO interface code is similar between
 * the mv643xx_eth driver and the mvneta driver. For now, it is only
 * used by the mvneta driver, but it could later be used by the
 * mv643xx_eth driver as well.
 *
 * Copyright (C) 2012 Marvell
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/phy.h>
#include <linux/of_address.h>
#include <linux/of_mdio.h>
#include <linux/platform_device.h>
#include <linux/delay.h>

#define MVMDIO_SMI_DATA_SHIFT              0
#define MVMDIO_SMI_PHY_ADDR_SHIFT          16
#define MVMDIO_SMI_PHY_REG_SHIFT           21
#define MVMDIO_SMI_READ_OPERATION          BIT(26)
#define MVMDIO_SMI_WRITE_OPERATION         0
#define MVMDIO_SMI_READ_VALID              BIT(27)
#define MVMDIO_SMI_BUSY                    BIT(28)

struct orion_mdio_dev {
	struct mutex lock;
	void __iomem *smireg;
};

/* Wait for the SMI unit to be ready for another operation
 */
static int orion_mdio_wait_ready(struct mii_bus *bus)
{
	struct orion_mdio_dev *dev = bus->priv;
	int count;
	u32 val;

	count = 0;
	while (1) {
		val = readl(dev->smireg);
		if (!(val & MVMDIO_SMI_BUSY))
			break;

		if (count > 100) {
			dev_err(bus->parent, "Timeout: SMI busy for too long\n");
			return -ETIMEDOUT;
		}

		udelay(10);
		count++;
	}

	return 0;
}

static int orion_mdio_read(struct mii_bus *bus, int mii_id,
			   int regnum)
{
	struct orion_mdio_dev *dev = bus->priv;
	int count;
	u32 val;
	int ret;

	mutex_lock(&dev->lock);

	ret = orion_mdio_wait_ready(bus);
	if (ret < 0) {
		mutex_unlock(&dev->lock);
		return ret;
	}

	writel(((mii_id << MVMDIO_SMI_PHY_ADDR_SHIFT) |
		(regnum << MVMDIO_SMI_PHY_REG_SHIFT)  |
		MVMDIO_SMI_READ_OPERATION),
	       dev->smireg);

	/* Wait for the value to become available */
	count = 0;
	while (1) {
		val = readl(dev->smireg);
		if (val & MVMDIO_SMI_READ_VALID)
			break;

		if (count > 100) {
			dev_err(bus->parent, "Timeout when reading PHY\n");
			mutex_unlock(&dev->lock);
			return -ETIMEDOUT;
		}

		udelay(10);
		count++;
	}

	mutex_unlock(&dev->lock);

	return val & 0xFFFF;
}

static int orion_mdio_write(struct mii_bus *bus, int mii_id,
			    int regnum, u16 value)
{
	struct orion_mdio_dev *dev = bus->priv;
	int ret;

	mutex_lock(&dev->lock);

	ret = orion_mdio_wait_ready(bus);
	if (ret < 0) {
		mutex_unlock(&dev->lock);
		return ret;
	}

	writel(((mii_id << MVMDIO_SMI_PHY_ADDR_SHIFT) |
		(regnum << MVMDIO_SMI_PHY_REG_SHIFT)  |
		MVMDIO_SMI_WRITE_OPERATION            |
		(value << MVMDIO_SMI_DATA_SHIFT)),
	       dev->smireg);

	mutex_unlock(&dev->lock);

	return 0;
}

static int orion_mdio_reset(struct mii_bus *bus)
{
	return 0;
}

static int __devinit orion_mdio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct mii_bus *bus;
	struct orion_mdio_dev *dev;
	int i, ret;

	bus = mdiobus_alloc_size(sizeof(struct orion_mdio_dev));
	if (!bus) {
		dev_err(&pdev->dev, "Cannot allocate MDIO bus\n");
		return -ENOMEM;
	}

	bus->name = "orion_mdio_bus";
	bus->read = orion_mdio_read;
	bus->write = orion_mdio_write;
	bus->reset = orion_mdio_reset;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-mii",
		 dev_name(&pdev->dev));
	bus->parent = &pdev->dev;

	bus->irq = kmalloc(sizeof(int) * PHY_MAX_ADDR, GFP_KERNEL);
	if (!bus->irq) {
		dev_err(&pdev->dev, "Cannot allocate PHY IRQ array\n");
		mdiobus_free(bus);
		return -ENOMEM;
	}

	for (i = 0; i < PHY_MAX_ADDR; i++)
		bus->irq[i] = PHY_POLL;

	dev = bus->priv;
	dev->smireg = of_iomap(pdev->dev.of_node, 0);
	if (!dev->smireg) {
		dev_err(&pdev->dev, "No SMI register address given in DT\n");
		kfree(bus->irq);
		mdiobus_free(bus);
		return -ENODEV;
	}

	mutex_init(&dev->lock);

	ret = of_mdiobus_register(bus, np);
	if (ret < 0) {
		dev_err(&pdev->dev, "Cannot register MDIO bus (%d)\n", ret);
		iounmap(dev->smireg);
		kfree(bus->irq);
		mdiobus_free(bus);
		return ret;
	}

	platform_set_drvdata(pdev, bus);

	return 0;
}

static int __devexit orion_mdio_remove(struct platform_device *pdev)
{
	struct mii_bus *bus = platform_get_drvdata(pdev);
	mdiobus_unregister(bus);
	kfree(bus->irq);
	mdiobus_free(bus);
	return 0;
}

static const struct of_device_id orion_mdio_match[] = {
	{ .compatible = "marvell,orion-mdio" },
	{ }
};
MODULE_DEVICE_TABLE(of, orion_mdio_match);

static struct platform_driver orion_mdio_driver = {
	.probe = orion_mdio_probe,
	.remove = __devexit_p(orion_mdio_remove),
	.driver = {
		.name = "orion-mdio",
		.of_match_table = orion_mdio_match,
	},
};

module_platform_driver(orion_mdio_driver);

MODULE_DESCRIPTION("Marvell MDIO interface driver");
MODULE_AUTHOR("Thomas Petazzoni <thomas.petazzoni@free-electrons.com>");
MODULE_LICENSE("GPL");

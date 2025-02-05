// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2009-2015 Cavium, Inc.
 */

#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/platform_device.h>

#include "mdio-cavium.h"

static int octeon_mdiobus_probe(struct platform_device *pdev)
{
	struct cavium_mdiobus *bus;
	struct mii_bus *mii_bus;
	union cvmx_smix_en smi_en;
	int err;

	mii_bus = devm_mdiobus_alloc_size(&pdev->dev, sizeof(*bus));
	if (!mii_bus)
		return -ENOMEM;

	bus = mii_bus->priv;
	bus->mii_bus = mii_bus;

	bus->register_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(bus->register_base)) {
		dev_err(&pdev->dev, "dev_ioremap failed\n");
		return PTR_ERR(bus->register_base);
	}

	smi_en.u64 = 0;
	smi_en.s.en = 1;
	oct_mdio_writeq(smi_en.u64, bus->register_base + SMI_EN);

	bus->mii_bus->name = KBUILD_MODNAME;
	snprintf(bus->mii_bus->id, MII_BUS_ID_SIZE, "%px", bus->register_base);
	bus->mii_bus->parent = &pdev->dev;

	bus->mii_bus->read = cavium_mdiobus_read_c22;
	bus->mii_bus->write = cavium_mdiobus_write_c22;
	bus->mii_bus->read_c45 = cavium_mdiobus_read_c45;
	bus->mii_bus->write_c45 = cavium_mdiobus_write_c45;

	platform_set_drvdata(pdev, bus);

	err = of_mdiobus_register(bus->mii_bus, pdev->dev.of_node);
	if (err)
		goto fail_register;

	dev_info(&pdev->dev, "Probed\n");

	return 0;
fail_register:
	smi_en.u64 = 0;
	oct_mdio_writeq(smi_en.u64, bus->register_base + SMI_EN);
	return err;
}

static void octeon_mdiobus_remove(struct platform_device *pdev)
{
	struct cavium_mdiobus *bus;
	union cvmx_smix_en smi_en;

	bus = platform_get_drvdata(pdev);

	mdiobus_unregister(bus->mii_bus);
	smi_en.u64 = 0;
	oct_mdio_writeq(smi_en.u64, bus->register_base + SMI_EN);
}

static const struct of_device_id octeon_mdiobus_match[] = {
	{
		.compatible = "cavium,octeon-3860-mdio",
	},
	{},
};
MODULE_DEVICE_TABLE(of, octeon_mdiobus_match);

static struct platform_driver octeon_mdiobus_driver = {
	.driver = {
		.name		= KBUILD_MODNAME,
		.of_match_table = octeon_mdiobus_match,
	},
	.probe		= octeon_mdiobus_probe,
	.remove		= octeon_mdiobus_remove,
};

module_platform_driver(octeon_mdiobus_driver);

MODULE_DESCRIPTION("Cavium OCTEON MDIO bus driver");
MODULE_AUTHOR("David Daney");
MODULE_LICENSE("GPL v2");

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2004-2013 Synopsys, Inc. (www.synopsys.com)
 *
 * MDIO implementation for ARC EMAC
 */

#include <linux/delay.h>
#include <linux/of_mdio.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>

#include "emac.h"

/* Number of seconds we wait for "MDIO complete" flag to appear */
#define ARC_MDIO_COMPLETE_POLL_COUNT	1

/**
 * arc_mdio_complete_wait - Waits until MDIO transaction is completed.
 * @priv:	Pointer to ARC EMAC private data structure.
 *
 * returns:	0 on success, -ETIMEDOUT on a timeout.
 */
static int arc_mdio_complete_wait(struct arc_emac_priv *priv)
{
	unsigned int i;

	for (i = 0; i < ARC_MDIO_COMPLETE_POLL_COUNT * 40; i++) {
		unsigned int status = arc_reg_get(priv, R_STATUS);

		status &= MDIO_MASK;

		if (status) {
			/* Reset "MDIO complete" flag */
			arc_reg_set(priv, R_STATUS, status);
			return 0;
		}

		msleep(25);
	}

	return -ETIMEDOUT;
}

/**
 * arc_mdio_read - MDIO interface read function.
 * @bus:	Pointer to MII bus structure.
 * @phy_addr:	Address of the PHY device.
 * @reg_num:	PHY register to read.
 *
 * returns:	The register contents on success, -ETIMEDOUT on a timeout.
 *
 * Reads the contents of the requested register from the requested PHY
 * address.
 */
static int arc_mdio_read(struct mii_bus *bus, int phy_addr, int reg_num)
{
	struct arc_emac_priv *priv = bus->priv;
	unsigned int value;
	int error;

	arc_reg_set(priv, R_MDIO,
		    0x60020000 | (phy_addr << 23) | (reg_num << 18));

	error = arc_mdio_complete_wait(priv);
	if (error < 0)
		return error;

	value = arc_reg_get(priv, R_MDIO) & 0xffff;

	dev_dbg(priv->dev, "arc_mdio_read(phy_addr=%i, reg_num=%x) = %x\n",
		phy_addr, reg_num, value);

	return value;
}

/**
 * arc_mdio_write - MDIO interface write function.
 * @bus:	Pointer to MII bus structure.
 * @phy_addr:	Address of the PHY device.
 * @reg_num:	PHY register to write to.
 * @value:	Value to be written into the register.
 *
 * returns:	0 on success, -ETIMEDOUT on a timeout.
 *
 * Writes the value to the requested register.
 */
static int arc_mdio_write(struct mii_bus *bus, int phy_addr,
			  int reg_num, u16 value)
{
	struct arc_emac_priv *priv = bus->priv;

	dev_dbg(priv->dev,
		"arc_mdio_write(phy_addr=%i, reg_num=%x, value=%x)\n",
		phy_addr, reg_num, value);

	arc_reg_set(priv, R_MDIO,
		    0x50020000 | (phy_addr << 23) | (reg_num << 18) | value);

	return arc_mdio_complete_wait(priv);
}

/**
 * arc_mdio_reset
 * @bus: points to the mii_bus structure
 * Description: reset the MII bus
 */
static int arc_mdio_reset(struct mii_bus *bus)
{
	struct arc_emac_priv *priv = bus->priv;
	struct arc_emac_mdio_bus_data *data = &priv->bus_data;

	if (data->reset_gpio) {
		gpiod_set_value_cansleep(data->reset_gpio, 1);
		msleep(data->msec);
		gpiod_set_value_cansleep(data->reset_gpio, 0);
	}

	return 0;
}

/**
 * arc_mdio_probe - MDIO probe function.
 * @priv:	Pointer to ARC EMAC private data structure.
 *
 * returns:	0 on success, -ENOMEM when mdiobus_alloc
 * (to allocate memory for MII bus structure) fails.
 *
 * Sets up and registers the MDIO interface.
 */
int arc_mdio_probe(struct arc_emac_priv *priv)
{
	struct arc_emac_mdio_bus_data *data = &priv->bus_data;
	struct device_node *np = priv->dev->of_node;
	struct mii_bus *bus;
	int error;

	bus = mdiobus_alloc();
	if (!bus)
		return -ENOMEM;

	priv->bus = bus;
	bus->priv = priv;
	bus->parent = priv->dev;
	bus->name = "Synopsys MII Bus";
	bus->read = &arc_mdio_read;
	bus->write = &arc_mdio_write;
	bus->reset = &arc_mdio_reset;

	/* optional reset-related properties */
	data->reset_gpio = devm_gpiod_get_optional(priv->dev, "phy-reset",
						   GPIOD_OUT_LOW);
	if (IS_ERR(data->reset_gpio)) {
		error = PTR_ERR(data->reset_gpio);
		dev_err(priv->dev, "Failed to request gpio: %d\n", error);
		return error;
	}

	of_property_read_u32(np, "phy-reset-duration", &data->msec);
	/* A sane reset duration should not be longer than 1s */
	if (data->msec > 1000)
		data->msec = 1;

	snprintf(bus->id, MII_BUS_ID_SIZE, "%s", bus->name);

	error = of_mdiobus_register(bus, priv->dev->of_node);
	if (error) {
		dev_err(priv->dev, "cannot register MDIO bus %s\n", bus->name);
		mdiobus_free(bus);
		return error;
	}

	return 0;
}

/**
 * arc_mdio_remove - MDIO remove function.
 * @priv:	Pointer to ARC EMAC private data structure.
 *
 * Unregisters the MDIO and frees any associate memory for MII bus.
 */
int arc_mdio_remove(struct arc_emac_priv *priv)
{
	mdiobus_unregister(priv->bus);
	mdiobus_free(priv->bus);
	priv->bus = NULL;

	return 0;
}

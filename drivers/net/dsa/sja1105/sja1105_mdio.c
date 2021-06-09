// SPDX-License-Identifier: GPL-2.0
/* Copyright 2021, NXP Semiconductors
 */
#include <linux/of_mdio.h>
#include "sja1105.h"

enum sja1105_mdio_opcode {
	SJA1105_C45_ADDR = 0,
	SJA1105_C22 = 1,
	SJA1105_C45_DATA = 2,
	SJA1105_C45_DATA_AUTOINC = 3,
};

static u64 sja1105_base_t1_encode_addr(struct sja1105_private *priv,
				       int phy, enum sja1105_mdio_opcode op,
				       int xad)
{
	const struct sja1105_regs *regs = priv->info->regs;

	return regs->mdio_100base_t1 | (phy << 7) | (op << 5) | (xad << 0);
}

static int sja1105_base_t1_mdio_read(struct mii_bus *bus, int phy, int reg)
{
	struct sja1105_mdio_private *mdio_priv = bus->priv;
	struct sja1105_private *priv = mdio_priv->priv;
	u64 addr;
	u32 tmp;
	int rc;

	if (reg & MII_ADDR_C45) {
		u16 mmd = (reg >> MII_DEVADDR_C45_SHIFT) & 0x1f;

		addr = sja1105_base_t1_encode_addr(priv, phy, SJA1105_C45_ADDR,
						   mmd);

		tmp = reg & MII_REGADDR_C45_MASK;

		rc = sja1105_xfer_u32(priv, SPI_WRITE, addr, &tmp, NULL);
		if (rc < 0)
			return rc;

		addr = sja1105_base_t1_encode_addr(priv, phy, SJA1105_C45_DATA,
						   mmd);

		rc = sja1105_xfer_u32(priv, SPI_READ, addr, &tmp, NULL);
		if (rc < 0)
			return rc;

		return tmp & 0xffff;
	}

	/* Clause 22 read */
	addr = sja1105_base_t1_encode_addr(priv, phy, SJA1105_C22, reg & 0x1f);

	rc = sja1105_xfer_u32(priv, SPI_READ, addr, &tmp, NULL);
	if (rc < 0)
		return rc;

	return tmp & 0xffff;
}

static int sja1105_base_t1_mdio_write(struct mii_bus *bus, int phy, int reg,
				      u16 val)
{
	struct sja1105_mdio_private *mdio_priv = bus->priv;
	struct sja1105_private *priv = mdio_priv->priv;
	u64 addr;
	u32 tmp;
	int rc;

	if (reg & MII_ADDR_C45) {
		u16 mmd = (reg >> MII_DEVADDR_C45_SHIFT) & 0x1f;

		addr = sja1105_base_t1_encode_addr(priv, phy, SJA1105_C45_ADDR,
						   mmd);

		tmp = reg & MII_REGADDR_C45_MASK;

		rc = sja1105_xfer_u32(priv, SPI_WRITE, addr, &tmp, NULL);
		if (rc < 0)
			return rc;

		addr = sja1105_base_t1_encode_addr(priv, phy, SJA1105_C45_DATA,
						   mmd);

		tmp = val & 0xffff;

		rc = sja1105_xfer_u32(priv, SPI_WRITE, addr, &tmp, NULL);
		if (rc < 0)
			return rc;

		return 0;
	}

	/* Clause 22 write */
	addr = sja1105_base_t1_encode_addr(priv, phy, SJA1105_C22, reg & 0x1f);

	tmp = val & 0xffff;

	return sja1105_xfer_u32(priv, SPI_WRITE, addr, &tmp, NULL);
}

static int sja1105_base_tx_mdio_read(struct mii_bus *bus, int phy, int reg)
{
	struct sja1105_mdio_private *mdio_priv = bus->priv;
	struct sja1105_private *priv = mdio_priv->priv;
	const struct sja1105_regs *regs = priv->info->regs;
	u32 tmp;
	int rc;

	rc = sja1105_xfer_u32(priv, SPI_READ, regs->mdio_100base_tx + reg,
			      &tmp, NULL);
	if (rc < 0)
		return rc;

	return tmp & 0xffff;
}

static int sja1105_base_tx_mdio_write(struct mii_bus *bus, int phy, int reg,
				      u16 val)
{
	struct sja1105_mdio_private *mdio_priv = bus->priv;
	struct sja1105_private *priv = mdio_priv->priv;
	const struct sja1105_regs *regs = priv->info->regs;
	u32 tmp = val;

	return sja1105_xfer_u32(priv, SPI_WRITE, regs->mdio_100base_tx + reg,
				&tmp, NULL);
}

static int sja1105_mdiobus_base_tx_register(struct sja1105_private *priv,
					    struct device_node *mdio_node)
{
	struct sja1105_mdio_private *mdio_priv;
	struct device_node *np;
	struct mii_bus *bus;
	int rc = 0;

	np = of_find_compatible_node(mdio_node, NULL,
				     "nxp,sja1110-base-tx-mdio");
	if (!np)
		return 0;

	if (!of_device_is_available(np))
		goto out_put_np;

	bus = mdiobus_alloc_size(sizeof(*mdio_priv));
	if (!bus) {
		rc = -ENOMEM;
		goto out_put_np;
	}

	bus->name = "SJA1110 100base-TX MDIO bus";
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-base-tx",
		 dev_name(priv->ds->dev));
	bus->read = sja1105_base_tx_mdio_read;
	bus->write = sja1105_base_tx_mdio_write;
	bus->parent = priv->ds->dev;
	mdio_priv = bus->priv;
	mdio_priv->priv = priv;

	rc = of_mdiobus_register(bus, np);
	if (rc) {
		mdiobus_free(bus);
		goto out_put_np;
	}

	priv->mdio_base_tx = bus;

out_put_np:
	of_node_put(np);

	return rc;
}

static void sja1105_mdiobus_base_tx_unregister(struct sja1105_private *priv)
{
	if (!priv->mdio_base_tx)
		return;

	mdiobus_unregister(priv->mdio_base_tx);
	mdiobus_free(priv->mdio_base_tx);
	priv->mdio_base_tx = NULL;
}

static int sja1105_mdiobus_base_t1_register(struct sja1105_private *priv,
					    struct device_node *mdio_node)
{
	struct sja1105_mdio_private *mdio_priv;
	struct device_node *np;
	struct mii_bus *bus;
	int rc = 0;

	np = of_find_compatible_node(mdio_node, NULL,
				     "nxp,sja1110-base-t1-mdio");
	if (!np)
		return 0;

	if (!of_device_is_available(np))
		goto out_put_np;

	bus = mdiobus_alloc_size(sizeof(*mdio_priv));
	if (!bus) {
		rc = -ENOMEM;
		goto out_put_np;
	}

	bus->name = "SJA1110 100base-T1 MDIO bus";
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-base-t1",
		 dev_name(priv->ds->dev));
	bus->read = sja1105_base_t1_mdio_read;
	bus->write = sja1105_base_t1_mdio_write;
	bus->parent = priv->ds->dev;
	mdio_priv = bus->priv;
	mdio_priv->priv = priv;

	rc = of_mdiobus_register(bus, np);
	if (rc) {
		mdiobus_free(bus);
		goto out_put_np;
	}

	priv->mdio_base_t1 = bus;

out_put_np:
	of_node_put(np);

	return rc;
}

static void sja1105_mdiobus_base_t1_unregister(struct sja1105_private *priv)
{
	if (!priv->mdio_base_t1)
		return;

	mdiobus_unregister(priv->mdio_base_t1);
	mdiobus_free(priv->mdio_base_t1);
	priv->mdio_base_t1 = NULL;
}

int sja1105_mdiobus_register(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;
	const struct sja1105_regs *regs = priv->info->regs;
	struct device_node *switch_node = ds->dev->of_node;
	struct device_node *mdio_node;
	int rc;

	mdio_node = of_get_child_by_name(switch_node, "mdios");
	if (!mdio_node)
		return 0;

	if (!of_device_is_available(mdio_node))
		goto out_put_mdio_node;

	if (regs->mdio_100base_tx != SJA1105_RSV_ADDR) {
		rc = sja1105_mdiobus_base_tx_register(priv, mdio_node);
		if (rc)
			goto err_put_mdio_node;
	}

	if (regs->mdio_100base_t1 != SJA1105_RSV_ADDR) {
		rc = sja1105_mdiobus_base_t1_register(priv, mdio_node);
		if (rc)
			goto err_free_base_tx_mdiobus;
	}

out_put_mdio_node:
	of_node_put(mdio_node);

	return 0;

err_free_base_tx_mdiobus:
	sja1105_mdiobus_base_tx_unregister(priv);
err_put_mdio_node:
	of_node_put(mdio_node);

	return rc;
}

void sja1105_mdiobus_unregister(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;

	sja1105_mdiobus_base_t1_unregister(priv);
	sja1105_mdiobus_base_tx_unregister(priv);
}

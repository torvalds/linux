// SPDX-License-Identifier: GPL-2.0
/* Copyright 2021 NXP
 */
#include <linux/pcs/pcs-xpcs.h>
#include <linux/of_mdio.h>
#include "sja1105.h"

#define SJA1110_PCS_BANK_REG		SJA1110_SPI_ADDR(0x3fc)

int sja1105_pcs_mdio_read_c45(struct mii_bus *bus, int phy, int mmd, int reg)
{
	struct sja1105_mdio_private *mdio_priv = bus->priv;
	struct sja1105_private *priv = mdio_priv->priv;
	u64 addr;
	u32 tmp;
	int rc;

	addr = (mmd << 16) | reg;

	if (mmd != MDIO_MMD_VEND1 && mmd != MDIO_MMD_VEND2)
		return 0xffff;

	if (mmd == MDIO_MMD_VEND2 && (reg & GENMASK(15, 0)) == MII_PHYSID1)
		return NXP_SJA1105_XPCS_ID >> 16;
	if (mmd == MDIO_MMD_VEND2 && (reg & GENMASK(15, 0)) == MII_PHYSID2)
		return NXP_SJA1105_XPCS_ID & GENMASK(15, 0);

	rc = sja1105_xfer_u32(priv, SPI_READ, addr, &tmp, NULL);
	if (rc < 0)
		return rc;

	return tmp & 0xffff;
}

int sja1105_pcs_mdio_write_c45(struct mii_bus *bus, int phy, int mmd,
			       int reg, u16 val)
{
	struct sja1105_mdio_private *mdio_priv = bus->priv;
	struct sja1105_private *priv = mdio_priv->priv;
	u64 addr;
	u32 tmp;

	addr = (mmd << 16) | reg;
	tmp = val;

	if (mmd != MDIO_MMD_VEND1 && mmd != MDIO_MMD_VEND2)
		return -EINVAL;

	return sja1105_xfer_u32(priv, SPI_WRITE, addr, &tmp, NULL);
}

int sja1110_pcs_mdio_read_c45(struct mii_bus *bus, int phy, int mmd, int reg)
{
	struct sja1105_mdio_private *mdio_priv = bus->priv;
	struct sja1105_private *priv = mdio_priv->priv;
	const struct sja1105_regs *regs = priv->info->regs;
	int offset, bank;
	u64 addr;
	u32 tmp;
	int rc;

	if (regs->pcs_base[phy] == SJA1105_RSV_ADDR)
		return -ENODEV;

	addr = (mmd << 16) | reg;

	if (mmd == MDIO_MMD_VEND2 && (reg & GENMASK(15, 0)) == MII_PHYSID1)
		return NXP_SJA1110_XPCS_ID >> 16;
	if (mmd == MDIO_MMD_VEND2 && (reg & GENMASK(15, 0)) == MII_PHYSID2)
		return NXP_SJA1110_XPCS_ID & GENMASK(15, 0);

	bank = addr >> 8;
	offset = addr & GENMASK(7, 0);

	/* This addressing scheme reserves register 0xff for the bank address
	 * register, so that can never be addressed.
	 */
	if (WARN_ON(offset == 0xff))
		return -ENODEV;

	tmp = bank;

	rc = sja1105_xfer_u32(priv, SPI_WRITE,
			      regs->pcs_base[phy] + SJA1110_PCS_BANK_REG,
			      &tmp, NULL);
	if (rc < 0)
		return rc;

	rc = sja1105_xfer_u32(priv, SPI_READ, regs->pcs_base[phy] + offset,
			      &tmp, NULL);
	if (rc < 0)
		return rc;

	return tmp & 0xffff;
}

int sja1110_pcs_mdio_write_c45(struct mii_bus *bus, int phy, int mmd, int reg,
			       u16 val)
{
	struct sja1105_mdio_private *mdio_priv = bus->priv;
	struct sja1105_private *priv = mdio_priv->priv;
	const struct sja1105_regs *regs = priv->info->regs;
	int offset, bank;
	u64 addr;
	u32 tmp;
	int rc;

	if (regs->pcs_base[phy] == SJA1105_RSV_ADDR)
		return -ENODEV;

	addr = (mmd << 16) | reg;

	bank = addr >> 8;
	offset = addr & GENMASK(7, 0);

	/* This addressing scheme reserves register 0xff for the bank address
	 * register, so that can never be addressed.
	 */
	if (WARN_ON(offset == 0xff))
		return -ENODEV;

	tmp = bank;

	rc = sja1105_xfer_u32(priv, SPI_WRITE,
			      regs->pcs_base[phy] + SJA1110_PCS_BANK_REG,
			      &tmp, NULL);
	if (rc < 0)
		return rc;

	tmp = val;

	return sja1105_xfer_u32(priv, SPI_WRITE, regs->pcs_base[phy] + offset,
				&tmp, NULL);
}

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

static int sja1105_base_t1_mdio_read_c22(struct mii_bus *bus, int phy, int reg)
{
	struct sja1105_mdio_private *mdio_priv = bus->priv;
	struct sja1105_private *priv = mdio_priv->priv;
	u64 addr;
	u32 tmp;
	int rc;

	addr = sja1105_base_t1_encode_addr(priv, phy, SJA1105_C22, reg & 0x1f);

	rc = sja1105_xfer_u32(priv, SPI_READ, addr, &tmp, NULL);
	if (rc < 0)
		return rc;

	return tmp & 0xffff;
}

static int sja1105_base_t1_mdio_read_c45(struct mii_bus *bus, int phy,
					 int mmd, int reg)
{
	struct sja1105_mdio_private *mdio_priv = bus->priv;
	struct sja1105_private *priv = mdio_priv->priv;
	u64 addr;
	u32 tmp;
	int rc;

	addr = sja1105_base_t1_encode_addr(priv, phy, SJA1105_C45_ADDR, mmd);

	rc = sja1105_xfer_u32(priv, SPI_WRITE, addr, &reg, NULL);
	if (rc < 0)
		return rc;

	addr = sja1105_base_t1_encode_addr(priv, phy, SJA1105_C45_DATA, mmd);

	rc = sja1105_xfer_u32(priv, SPI_READ, addr, &tmp, NULL);
	if (rc < 0)
		return rc;

	return tmp & 0xffff;
}

static int sja1105_base_t1_mdio_write_c22(struct mii_bus *bus, int phy, int reg,
					  u16 val)
{
	struct sja1105_mdio_private *mdio_priv = bus->priv;
	struct sja1105_private *priv = mdio_priv->priv;
	u64 addr;
	u32 tmp;

	addr = sja1105_base_t1_encode_addr(priv, phy, SJA1105_C22, reg & 0x1f);

	tmp = val & 0xffff;

	return sja1105_xfer_u32(priv, SPI_WRITE, addr, &tmp, NULL);
}

static int sja1105_base_t1_mdio_write_c45(struct mii_bus *bus, int phy,
					  int mmd, int reg, u16 val)
{
	struct sja1105_mdio_private *mdio_priv = bus->priv;
	struct sja1105_private *priv = mdio_priv->priv;
	u64 addr;
	u32 tmp;
	int rc;

	addr = sja1105_base_t1_encode_addr(priv, phy, SJA1105_C45_ADDR, mmd);

	rc = sja1105_xfer_u32(priv, SPI_WRITE, addr, &reg, NULL);
	if (rc < 0)
		return rc;

	addr = sja1105_base_t1_encode_addr(priv, phy, SJA1105_C45_DATA, mmd);

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

	np = of_get_compatible_child(mdio_node, "nxp,sja1110-base-tx-mdio");
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

	np = of_get_compatible_child(mdio_node, "nxp,sja1110-base-t1-mdio");
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
	bus->read = sja1105_base_t1_mdio_read_c22;
	bus->write = sja1105_base_t1_mdio_write_c22;
	bus->read_c45 = sja1105_base_t1_mdio_read_c45;
	bus->write_c45 = sja1105_base_t1_mdio_write_c45;
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

static int sja1105_mdiobus_pcs_register(struct sja1105_private *priv)
{
	struct sja1105_mdio_private *mdio_priv;
	struct dsa_switch *ds = priv->ds;
	struct mii_bus *bus;
	int rc = 0;
	int port;

	if (!priv->info->pcs_mdio_read_c45 || !priv->info->pcs_mdio_write_c45)
		return 0;

	bus = mdiobus_alloc_size(sizeof(*mdio_priv));
	if (!bus)
		return -ENOMEM;

	bus->name = "SJA1105 PCS MDIO bus";
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-pcs",
		 dev_name(ds->dev));
	bus->read_c45 = priv->info->pcs_mdio_read_c45;
	bus->write_c45 = priv->info->pcs_mdio_write_c45;
	bus->parent = ds->dev;
	/* There is no PHY on this MDIO bus => mask out all PHY addresses
	 * from auto probing.
	 */
	bus->phy_mask = ~0;
	mdio_priv = bus->priv;
	mdio_priv->priv = priv;

	rc = mdiobus_register(bus);
	if (rc) {
		mdiobus_free(bus);
		return rc;
	}

	for (port = 0; port < ds->num_ports; port++) {
		struct phylink_pcs *pcs;

		if (dsa_is_unused_port(ds, port))
			continue;

		if (priv->phy_mode[port] != PHY_INTERFACE_MODE_SGMII &&
		    priv->phy_mode[port] != PHY_INTERFACE_MODE_2500BASEX)
			continue;

		pcs = xpcs_create_pcs_mdiodev(bus, port);
		if (IS_ERR(pcs)) {
			rc = PTR_ERR(pcs);
			goto out_pcs_free;
		}

		priv->pcs[port] = pcs;
	}

	priv->mdio_pcs = bus;

	return 0;

out_pcs_free:
	for (port = 0; port < ds->num_ports; port++) {
		if (priv->pcs[port]) {
			xpcs_destroy_pcs(priv->pcs[port]);
			priv->pcs[port] = NULL;
		}
	}

	mdiobus_unregister(bus);
	mdiobus_free(bus);

	return rc;
}

static void sja1105_mdiobus_pcs_unregister(struct sja1105_private *priv)
{
	struct dsa_switch *ds = priv->ds;
	int port;

	if (!priv->mdio_pcs)
		return;

	for (port = 0; port < ds->num_ports; port++) {
		if (priv->pcs[port]) {
			xpcs_destroy_pcs(priv->pcs[port]);
			priv->pcs[port] = NULL;
		}
	}

	mdiobus_unregister(priv->mdio_pcs);
	mdiobus_free(priv->mdio_pcs);
	priv->mdio_pcs = NULL;
}

int sja1105_mdiobus_register(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;
	const struct sja1105_regs *regs = priv->info->regs;
	struct device_node *switch_node = ds->dev->of_node;
	struct device_node *mdio_node;
	int rc;

	rc = sja1105_mdiobus_pcs_register(priv);
	if (rc)
		return rc;

	mdio_node = of_get_available_child_by_name(switch_node, "mdios");
	if (!mdio_node)
		return 0;

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

	of_node_put(mdio_node);

	return 0;

err_free_base_tx_mdiobus:
	sja1105_mdiobus_base_tx_unregister(priv);
err_put_mdio_node:
	of_node_put(mdio_node);
	sja1105_mdiobus_pcs_unregister(priv);

	return rc;
}

void sja1105_mdiobus_unregister(struct dsa_switch *ds)
{
	struct sja1105_private *priv = ds->priv;

	sja1105_mdiobus_base_t1_unregister(priv);
	sja1105_mdiobus_base_tx_unregister(priv);
	sja1105_mdiobus_pcs_unregister(priv);
}

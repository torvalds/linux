// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/mdio.h>
#include <linux/pcs/pcs-xpcs.h>

#include "fbnic.h"
#include "fbnic_netdev.h"

#define DW_VENDOR		BIT(15)
#define FBNIC_PCS_VENDOR	BIT(9)
#define FBNIC_PCS_ZERO_MASK	(DW_VENDOR - FBNIC_PCS_VENDOR)

static int
fbnic_mdio_read_pmd(struct fbnic_dev *fbd, int addr, int regnum)
{
	u8 aui = FBNIC_AUI_UNKNOWN;
	struct fbnic_net *fbn;
	int ret = 0;

	/* We don't need a second PMD, just one can handle both lanes */
	if (addr)
		return 0;

	if (fbd->netdev) {
		fbn = netdev_priv(fbd->netdev);
		if (fbn->aui < FBNIC_AUI_UNKNOWN)
			aui = fbn->aui;
	}

	switch (regnum) {
	case MDIO_DEVID1:
		ret = MP_FBNIC_XPCS_PMA_100G_ID >> 16;
		break;
	case MDIO_DEVID2:
		ret = MP_FBNIC_XPCS_PMA_100G_ID & 0xffff;
		break;
	case MDIO_DEVS1:
		ret = MDIO_DEVS_PMAPMD | MDIO_DEVS_PCS;
		break;
	case MDIO_STAT2:
		ret = MDIO_STAT2_DEVPRST_VAL;
		break;
	case MDIO_PMA_RXDET:
		/* If training isn't complete default to 0 */
		if (fbd->pmd_state != FBNIC_PMD_SEND_DATA)
			break;
		/* Report either 1 or 2 lanes detected depending on config */
		ret = (MDIO_PMD_RXDET_GLOBAL | MDIO_PMD_RXDET_0) |
		      ((aui & FBNIC_AUI_MODE_R2) *
		       (MDIO_PMD_RXDET_1 / FBNIC_AUI_MODE_R2));
		break;
	default:
		break;
	}

	dev_dbg(fbd->dev,
		"SWMII PMD Rd: Addr: %d RegNum: %d Value: 0x%04x\n",
		addr, regnum, ret);

	return ret;
}

static int
fbnic_mdio_read_pcs(struct fbnic_dev *fbd, int addr, int regnum)
{
	int ret, offset = 0;

	/* We will need access to both PCS instances to get config info */
	if (addr >= 2)
		return 0;

	/* Report 0 for reserved registers */
	if (regnum & FBNIC_PCS_ZERO_MASK)
		return 0;

	/* Intercept and return correct ID for PCS */
	if (regnum == MDIO_DEVID1)
		return DW_XPCS_ID >> 16;
	if (regnum == MDIO_DEVID2)
		return DW_XPCS_ID & 0xffff;
	if (regnum == MDIO_DEVS1)
		return MDIO_DEVS_PMAPMD | MDIO_DEVS_PCS;

	/* Swap vendor page bit for FBNIC PCS vendor page bit */
	if (regnum & DW_VENDOR)
		offset ^= DW_VENDOR | FBNIC_PCS_VENDOR;

	ret = fbnic_rd32(fbd, FBNIC_PCS_PAGE(addr) + (regnum ^ offset));

	dev_dbg(fbd->dev,
		"SWMII PCS Rd: Addr: %d RegNum: %d Value: 0x%04x\n",
		addr, regnum, ret);

	return ret;
}

static int
fbnic_mdio_read_c45(struct mii_bus *bus, int addr, int devnum, int regnum)
{
	struct fbnic_dev *fbd = bus->priv;

	if (devnum == MDIO_MMD_PMAPMD)
		return fbnic_mdio_read_pmd(fbd, addr, regnum);

	if (devnum == MDIO_MMD_PCS)
		return fbnic_mdio_read_pcs(fbd, addr, regnum);

	return 0;
}

static void
fbnic_mdio_write_pmd(struct fbnic_dev *fbd, int addr, int regnum, u16 val)
{
	dev_dbg(fbd->dev,
		"SWMII PMD Wr: Addr: %d RegNum: %d Value: 0x%04x\n",
		addr, regnum, val);
}

static void
fbnic_mdio_write_pcs(struct fbnic_dev *fbd, int addr, int regnum, u16 val)
{
	dev_dbg(fbd->dev,
		"SWMII PCS Wr: Addr: %d RegNum: %d Value: 0x%04x\n",
		addr, regnum, val);

	/* Allow access to both halves of PCS for 50R2 config */
	if (addr > 2)
		return;

	/* Skip write for reserved registers */
	if (regnum & FBNIC_PCS_ZERO_MASK)
		return;

	/* Swap vendor page bit for FBNIC PCS vendor page bit */
	if (regnum & DW_VENDOR)
		regnum ^= DW_VENDOR | FBNIC_PCS_VENDOR;

	fbnic_wr32(fbd, FBNIC_PCS_PAGE(addr) + regnum, val);
}

static int
fbnic_mdio_write_c45(struct mii_bus *bus, int addr, int devnum,
		     int regnum, u16 val)
{
	struct fbnic_dev *fbd = bus->priv;

	if (devnum == MDIO_MMD_PMAPMD)
		fbnic_mdio_write_pmd(fbd, addr, regnum, val);

	if (devnum == MDIO_MMD_PCS)
		fbnic_mdio_write_pcs(fbd, addr, regnum, val);

	return 0;
}

/**
 * fbnic_mdiobus_create - Create an MDIO bus to allow interfacing w/ PHYs
 * @fbd: Pointer to FBNIC device structure to populate bus on
 *
 * Initialize an MDIO bus and place a pointer to it on the fbd struct. This bus
 * will be used to interface with the PMA/PMD and PCS.
 *
 * Return: 0 on success, negative on failure
 **/
int fbnic_mdiobus_create(struct fbnic_dev *fbd)
{
	struct mii_bus *bus;
	int err;

	bus = devm_mdiobus_alloc(fbd->dev);
	if (!bus)
		return -ENOMEM;

	bus->name = "fbnic_mii_bus";
	bus->read_c45 = &fbnic_mdio_read_c45;
	bus->write_c45 = &fbnic_mdio_write_c45;

	/* Disable PHY auto probing. We will add PCS manually */
	bus->phy_mask = ~0;

	bus->parent = fbd->dev;
	bus->priv = fbd;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-mii", dev_name(fbd->dev));

	err = devm_mdiobus_register(fbd->dev, bus);
	if (err) {
		dev_err(fbd->dev, "Failed to create MDIO bus: %d\n", err);
		return err;
	}

	fbd->mdio_bus = bus;

	return 0;
}

/*
 * Core PHY library, taken from phy.c
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/export.h>
#include <linux/phy.h>

static inline void mmd_phy_indirect(struct mii_bus *bus, int prtad, int devad,
				    int addr)
{
	/* Write the desired MMD Devad */
	bus->write(bus, addr, MII_MMD_CTRL, devad);

	/* Write the desired MMD register address */
	bus->write(bus, addr, MII_MMD_DATA, prtad);

	/* Select the Function : DATA with no post increment */
	bus->write(bus, addr, MII_MMD_CTRL, (devad | MII_MMD_CTRL_NOINCR));
}

/**
 * phy_read_mmd_indirect - reads data from the MMD registers
 * @phydev: The PHY device bus
 * @prtad: MMD Address
 * @devad: MMD DEVAD
 *
 * Description: it reads data from the MMD registers (clause 22 to access to
 * clause 45) of the specified phy address.
 * To read these register we have:
 * 1) Write reg 13 // DEVAD
 * 2) Write reg 14 // MMD Address
 * 3) Write reg 13 // MMD Data Command for MMD DEVAD
 * 3) Read  reg 14 // Read MMD data
 */
int phy_read_mmd_indirect(struct phy_device *phydev, int prtad, int devad)
{
	struct phy_driver *phydrv = phydev->drv;
	int addr = phydev->mdio.addr;
	int value = -1;

	if (!phydrv->read_mmd_indirect) {
		struct mii_bus *bus = phydev->mdio.bus;

		mutex_lock(&bus->mdio_lock);
		mmd_phy_indirect(bus, prtad, devad, addr);

		/* Read the content of the MMD's selected register */
		value = bus->read(bus, addr, MII_MMD_DATA);
		mutex_unlock(&bus->mdio_lock);
	} else {
		value = phydrv->read_mmd_indirect(phydev, prtad, devad, addr);
	}
	return value;
}
EXPORT_SYMBOL(phy_read_mmd_indirect);

/**
 * phy_read_mmd - Convenience function for reading a register
 * from an MMD on a given PHY.
 * @phydev: The phy_device struct
 * @devad: The MMD to read from
 * @regnum: The register on the MMD to read
 *
 * Same rules as for phy_read();
 */
int phy_read_mmd(struct phy_device *phydev, int devad, u32 regnum)
{
	if (!phydev->is_c45)
		return -EOPNOTSUPP;

	return mdiobus_read(phydev->mdio.bus, phydev->mdio.addr,
			    MII_ADDR_C45 | (devad << 16) | (regnum & 0xffff));
}
EXPORT_SYMBOL(phy_read_mmd);

/**
 * phy_write_mmd_indirect - writes data to the MMD registers
 * @phydev: The PHY device
 * @prtad: MMD Address
 * @devad: MMD DEVAD
 * @data: data to write in the MMD register
 *
 * Description: Write data from the MMD registers of the specified
 * phy address.
 * To write these register we have:
 * 1) Write reg 13 // DEVAD
 * 2) Write reg 14 // MMD Address
 * 3) Write reg 13 // MMD Data Command for MMD DEVAD
 * 3) Write reg 14 // Write MMD data
 */
void phy_write_mmd_indirect(struct phy_device *phydev, int prtad,
				   int devad, u32 data)
{
	struct phy_driver *phydrv = phydev->drv;
	int addr = phydev->mdio.addr;

	if (!phydrv->write_mmd_indirect) {
		struct mii_bus *bus = phydev->mdio.bus;

		mutex_lock(&bus->mdio_lock);
		mmd_phy_indirect(bus, prtad, devad, addr);

		/* Write the data into MMD's selected register */
		bus->write(bus, addr, MII_MMD_DATA, data);
		mutex_unlock(&bus->mdio_lock);
	} else {
		phydrv->write_mmd_indirect(phydev, prtad, devad, addr, data);
	}
}
EXPORT_SYMBOL(phy_write_mmd_indirect);

/**
 * phy_write_mmd - Convenience function for writing a register
 * on an MMD on a given PHY.
 * @phydev: The phy_device struct
 * @devad: The MMD to read from
 * @regnum: The register on the MMD to read
 * @val: value to write to @regnum
 *
 * Same rules as for phy_write();
 */
int phy_write_mmd(struct phy_device *phydev, int devad, u32 regnum, u16 val)
{
	if (!phydev->is_c45)
		return -EOPNOTSUPP;

	regnum = MII_ADDR_C45 | ((devad & 0x1f) << 16) | (regnum & 0xffff);

	return mdiobus_write(phydev->mdio.bus, phydev->mdio.addr, regnum, val);
}
EXPORT_SYMBOL(phy_write_mmd);

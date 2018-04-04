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

const char *phy_speed_to_str(int speed)
{
	switch (speed) {
	case SPEED_10:
		return "10Mbps";
	case SPEED_100:
		return "100Mbps";
	case SPEED_1000:
		return "1Gbps";
	case SPEED_2500:
		return "2.5Gbps";
	case SPEED_5000:
		return "5Gbps";
	case SPEED_10000:
		return "10Gbps";
	case SPEED_14000:
		return "14Gbps";
	case SPEED_20000:
		return "20Gbps";
	case SPEED_25000:
		return "25Gbps";
	case SPEED_40000:
		return "40Gbps";
	case SPEED_50000:
		return "50Gbps";
	case SPEED_56000:
		return "56Gbps";
	case SPEED_100000:
		return "100Gbps";
	case SPEED_UNKNOWN:
		return "Unknown";
	default:
		return "Unsupported (update phy-core.c)";
	}
}
EXPORT_SYMBOL_GPL(phy_speed_to_str);

const char *phy_duplex_to_str(unsigned int duplex)
{
	if (duplex == DUPLEX_HALF)
		return "Half";
	if (duplex == DUPLEX_FULL)
		return "Full";
	if (duplex == DUPLEX_UNKNOWN)
		return "Unknown";
	return "Unsupported (update phy-core.c)";
}
EXPORT_SYMBOL_GPL(phy_duplex_to_str);

/* A mapping of all SUPPORTED settings to speed/duplex.  This table
 * must be grouped by speed and sorted in descending match priority
 * - iow, descending speed. */
static const struct phy_setting settings[] = {
	{
		.speed = SPEED_10000,
		.duplex = DUPLEX_FULL,
		.bit = ETHTOOL_LINK_MODE_10000baseKR_Full_BIT,
	},
	{
		.speed = SPEED_10000,
		.duplex = DUPLEX_FULL,
		.bit = ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT,
	},
	{
		.speed = SPEED_10000,
		.duplex = DUPLEX_FULL,
		.bit = ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
	},
	{
		.speed = SPEED_2500,
		.duplex = DUPLEX_FULL,
		.bit = ETHTOOL_LINK_MODE_2500baseX_Full_BIT,
	},
	{
		.speed = SPEED_1000,
		.duplex = DUPLEX_FULL,
		.bit = ETHTOOL_LINK_MODE_1000baseKX_Full_BIT,
	},
	{
		.speed = SPEED_1000,
		.duplex = DUPLEX_FULL,
		.bit = ETHTOOL_LINK_MODE_1000baseX_Full_BIT,
	},
	{
		.speed = SPEED_1000,
		.duplex = DUPLEX_FULL,
		.bit = ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
	},
	{
		.speed = SPEED_1000,
		.duplex = DUPLEX_HALF,
		.bit = ETHTOOL_LINK_MODE_1000baseT_Half_BIT,
	},
	{
		.speed = SPEED_100,
		.duplex = DUPLEX_FULL,
		.bit = ETHTOOL_LINK_MODE_100baseT_Full_BIT,
	},
	{
		.speed = SPEED_100,
		.duplex = DUPLEX_HALF,
		.bit = ETHTOOL_LINK_MODE_100baseT_Half_BIT,
	},
	{
		.speed = SPEED_10,
		.duplex = DUPLEX_FULL,
		.bit = ETHTOOL_LINK_MODE_10baseT_Full_BIT,
	},
	{
		.speed = SPEED_10,
		.duplex = DUPLEX_HALF,
		.bit = ETHTOOL_LINK_MODE_10baseT_Half_BIT,
	},
};

/**
 * phy_lookup_setting - lookup a PHY setting
 * @speed: speed to match
 * @duplex: duplex to match
 * @mask: allowed link modes
 * @maxbit: bit size of link modes
 * @exact: an exact match is required
 *
 * Search the settings array for a setting that matches the speed and
 * duplex, and which is supported.
 *
 * If @exact is unset, either an exact match or %NULL for no match will
 * be returned.
 *
 * If @exact is set, an exact match, the fastest supported setting at
 * or below the specified speed, the slowest supported setting, or if
 * they all fail, %NULL will be returned.
 */
const struct phy_setting *
phy_lookup_setting(int speed, int duplex, const unsigned long *mask,
		   size_t maxbit, bool exact)
{
	const struct phy_setting *p, *match = NULL, *last = NULL;
	int i;

	for (i = 0, p = settings; i < ARRAY_SIZE(settings); i++, p++) {
		if (p->bit < maxbit && test_bit(p->bit, mask)) {
			last = p;
			if (p->speed == speed && p->duplex == duplex) {
				/* Exact match for speed and duplex */
				match = p;
				break;
			} else if (!exact) {
				if (!match && p->speed <= speed)
					/* Candidate */
					match = p;

				if (p->speed < speed)
					break;
			}
		}
	}

	if (!match && !exact)
		match = last;

	return match;
}
EXPORT_SYMBOL_GPL(phy_lookup_setting);

size_t phy_speeds(unsigned int *speeds, size_t size,
		  unsigned long *mask, size_t maxbit)
{
	size_t count;
	int i;

	for (i = 0, count = 0; i < ARRAY_SIZE(settings) && count < size; i++)
		if (settings[i].bit < maxbit &&
		    test_bit(settings[i].bit, mask) &&
		    (count == 0 || speeds[count - 1] != settings[i].speed))
			speeds[count++] = settings[i].speed;

	return count;
}

/**
 * phy_resolve_aneg_linkmode - resolve the advertisments into phy settings
 * @phydev: The phy_device struct
 *
 * Resolve our and the link partner advertisments into their corresponding
 * speed and duplex. If full duplex was negotiated, extract the pause mode
 * from the link partner mask.
 */
void phy_resolve_aneg_linkmode(struct phy_device *phydev)
{
	u32 common = phydev->lp_advertising & phydev->advertising;

	if (common & ADVERTISED_10000baseT_Full) {
		phydev->speed = SPEED_10000;
		phydev->duplex = DUPLEX_FULL;
	} else if (common & ADVERTISED_1000baseT_Full) {
		phydev->speed = SPEED_1000;
		phydev->duplex = DUPLEX_FULL;
	} else if (common & ADVERTISED_1000baseT_Half) {
		phydev->speed = SPEED_1000;
		phydev->duplex = DUPLEX_HALF;
	} else if (common & ADVERTISED_100baseT_Full) {
		phydev->speed = SPEED_100;
		phydev->duplex = DUPLEX_FULL;
	} else if (common & ADVERTISED_100baseT_Half) {
		phydev->speed = SPEED_100;
		phydev->duplex = DUPLEX_HALF;
	} else if (common & ADVERTISED_10baseT_Full) {
		phydev->speed = SPEED_10;
		phydev->duplex = DUPLEX_FULL;
	} else if (common & ADVERTISED_10baseT_Half) {
		phydev->speed = SPEED_10;
		phydev->duplex = DUPLEX_HALF;
	}

	if (phydev->duplex == DUPLEX_FULL) {
		phydev->pause = !!(phydev->lp_advertising & ADVERTISED_Pause);
		phydev->asym_pause = !!(phydev->lp_advertising &
					ADVERTISED_Asym_Pause);
	}
}
EXPORT_SYMBOL_GPL(phy_resolve_aneg_linkmode);

static void mmd_phy_indirect(struct mii_bus *bus, int phy_addr, int devad,
			     u16 regnum)
{
	/* Write the desired MMD Devad */
	__mdiobus_write(bus, phy_addr, MII_MMD_CTRL, devad);

	/* Write the desired MMD register address */
	__mdiobus_write(bus, phy_addr, MII_MMD_DATA, regnum);

	/* Select the Function : DATA with no post increment */
	__mdiobus_write(bus, phy_addr, MII_MMD_CTRL,
			devad | MII_MMD_CTRL_NOINCR);
}

/**
 * phy_read_mmd - Convenience function for reading a register
 * from an MMD on a given PHY.
 * @phydev: The phy_device struct
 * @devad: The MMD to read from (0..31)
 * @regnum: The register on the MMD to read (0..65535)
 *
 * Same rules as for phy_read();
 */
int phy_read_mmd(struct phy_device *phydev, int devad, u32 regnum)
{
	int val;

	if (regnum > (u16)~0 || devad > 32)
		return -EINVAL;

	if (phydev->drv->read_mmd) {
		val = phydev->drv->read_mmd(phydev, devad, regnum);
	} else if (phydev->is_c45) {
		u32 addr = MII_ADDR_C45 | (devad << 16) | (regnum & 0xffff);

		val = mdiobus_read(phydev->mdio.bus, phydev->mdio.addr, addr);
	} else {
		struct mii_bus *bus = phydev->mdio.bus;
		int phy_addr = phydev->mdio.addr;

		mutex_lock(&bus->mdio_lock);
		mmd_phy_indirect(bus, phy_addr, devad, regnum);

		/* Read the content of the MMD's selected register */
		val = __mdiobus_read(bus, phy_addr, MII_MMD_DATA);
		mutex_unlock(&bus->mdio_lock);
	}
	return val;
}
EXPORT_SYMBOL(phy_read_mmd);

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
	int ret;

	if (regnum > (u16)~0 || devad > 32)
		return -EINVAL;

	if (phydev->drv->write_mmd) {
		ret = phydev->drv->write_mmd(phydev, devad, regnum, val);
	} else if (phydev->is_c45) {
		u32 addr = MII_ADDR_C45 | (devad << 16) | (regnum & 0xffff);

		ret = mdiobus_write(phydev->mdio.bus, phydev->mdio.addr,
				    addr, val);
	} else {
		struct mii_bus *bus = phydev->mdio.bus;
		int phy_addr = phydev->mdio.addr;

		mutex_lock(&bus->mdio_lock);
		mmd_phy_indirect(bus, phy_addr, devad, regnum);

		/* Write the data into MMD's selected register */
		__mdiobus_write(bus, phy_addr, MII_MMD_DATA, val);
		mutex_unlock(&bus->mdio_lock);

		ret = 0;
	}
	return ret;
}
EXPORT_SYMBOL(phy_write_mmd);

/**
 * __phy_modify() - Convenience function for modifying a PHY register
 * @phydev: a pointer to a &struct phy_device
 * @regnum: register number
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 *
 * Unlocked helper function which allows a PHY register to be modified as
 * new register value = (old register value & ~mask) | set
 */
int __phy_modify(struct phy_device *phydev, u32 regnum, u16 mask, u16 set)
{
	int ret;

	ret = __phy_read(phydev, regnum);
	if (ret < 0)
		return ret;

	ret = __phy_write(phydev, regnum, (ret & ~mask) | set);

	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL_GPL(__phy_modify);

/**
 * phy_modify - Convenience function for modifying a given PHY register
 * @phydev: the phy_device struct
 * @regnum: register number to write
 * @mask: bit mask of bits to clear
 * @set: new value of bits set in mask to write to @regnum
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 */
int phy_modify(struct phy_device *phydev, u32 regnum, u16 mask, u16 set)
{
	int ret;

	mutex_lock(&phydev->mdio.bus->mdio_lock);
	ret = __phy_modify(phydev, regnum, mask, set);
	mutex_unlock(&phydev->mdio.bus->mdio_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(phy_modify);

static int __phy_read_page(struct phy_device *phydev)
{
	return phydev->drv->read_page(phydev);
}

static int __phy_write_page(struct phy_device *phydev, int page)
{
	return phydev->drv->write_page(phydev, page);
}

/**
 * phy_save_page() - take the bus lock and save the current page
 * @phydev: a pointer to a &struct phy_device
 *
 * Take the MDIO bus lock, and return the current page number. On error,
 * returns a negative errno. phy_restore_page() must always be called
 * after this, irrespective of success or failure of this call.
 */
int phy_save_page(struct phy_device *phydev)
{
	mutex_lock(&phydev->mdio.bus->mdio_lock);
	return __phy_read_page(phydev);
}
EXPORT_SYMBOL_GPL(phy_save_page);

/**
 * phy_select_page() - take the bus lock, save the current page, and set a page
 * @phydev: a pointer to a &struct phy_device
 * @page: desired page
 *
 * Take the MDIO bus lock to protect against concurrent access, save the
 * current PHY page, and set the current page.  On error, returns a
 * negative errno, otherwise returns the previous page number.
 * phy_restore_page() must always be called after this, irrespective
 * of success or failure of this call.
 */
int phy_select_page(struct phy_device *phydev, int page)
{
	int ret, oldpage;

	oldpage = ret = phy_save_page(phydev);
	if (ret < 0)
		return ret;

	if (oldpage != page) {
		ret = __phy_write_page(phydev, page);
		if (ret < 0)
			return ret;
	}

	return oldpage;
}
EXPORT_SYMBOL_GPL(phy_select_page);

/**
 * phy_restore_page() - restore the page register and release the bus lock
 * @phydev: a pointer to a &struct phy_device
 * @oldpage: the old page, return value from phy_save_page() or phy_select_page()
 * @ret: operation's return code
 *
 * Release the MDIO bus lock, restoring @oldpage if it is a valid page.
 * This function propagates the earliest error code from the group of
 * operations.
 *
 * Returns:
 *   @oldpage if it was a negative value, otherwise
 *   @ret if it was a negative errno value, otherwise
 *   phy_write_page()'s negative value if it were in error, otherwise
 *   @ret.
 */
int phy_restore_page(struct phy_device *phydev, int oldpage, int ret)
{
	int r;

	if (oldpage >= 0) {
		r = __phy_write_page(phydev, oldpage);

		/* Propagate the operation return code if the page write
		 * was successful.
		 */
		if (ret >= 0 && r < 0)
			ret = r;
	} else {
		/* Propagate the phy page selection error code */
		ret = oldpage;
	}

	mutex_unlock(&phydev->mdio.bus->mdio_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(phy_restore_page);

/**
 * phy_read_paged() - Convenience function for reading a paged register
 * @phydev: a pointer to a &struct phy_device
 * @page: the page for the phy
 * @regnum: register number
 *
 * Same rules as for phy_read().
 */
int phy_read_paged(struct phy_device *phydev, int page, u32 regnum)
{
	int ret = 0, oldpage;

	oldpage = phy_select_page(phydev, page);
	if (oldpage >= 0)
		ret = __phy_read(phydev, regnum);

	return phy_restore_page(phydev, oldpage, ret);
}
EXPORT_SYMBOL(phy_read_paged);

/**
 * phy_write_paged() - Convenience function for writing a paged register
 * @phydev: a pointer to a &struct phy_device
 * @page: the page for the phy
 * @regnum: register number
 * @val: value to write
 *
 * Same rules as for phy_write().
 */
int phy_write_paged(struct phy_device *phydev, int page, u32 regnum, u16 val)
{
	int ret = 0, oldpage;

	oldpage = phy_select_page(phydev, page);
	if (oldpage >= 0)
		ret = __phy_write(phydev, regnum, val);

	return phy_restore_page(phydev, oldpage, ret);
}
EXPORT_SYMBOL(phy_write_paged);

/**
 * phy_modify_paged() - Convenience function for modifying a paged register
 * @phydev: a pointer to a &struct phy_device
 * @page: the page for the phy
 * @regnum: register number
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 *
 * Same rules as for phy_read() and phy_write().
 */
int phy_modify_paged(struct phy_device *phydev, int page, u32 regnum,
		     u16 mask, u16 set)
{
	int ret = 0, oldpage;

	oldpage = phy_select_page(phydev, page);
	if (oldpage >= 0)
		ret = __phy_modify(phydev, regnum, mask, set);

	return phy_restore_page(phydev, oldpage, ret);
}
EXPORT_SYMBOL(phy_modify_paged);

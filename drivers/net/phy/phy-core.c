// SPDX-License-Identifier: GPL-2.0+
/*
 * Core PHY library, taken from phy.c
 */
#include <linux/export.h>
#include <linux/phy.h>
#include <linux/of.h>

#include "phylib.h"
#include "phylib-internal.h"
#include "phy-caps.h"

/**
 * phy_speed_to_str - Return a string representing the PHY link speed
 *
 * @speed: Speed of the link
 */
const char *phy_speed_to_str(int speed)
{
	BUILD_BUG_ON_MSG(__ETHTOOL_LINK_MODE_MASK_NBITS != 121,
		"Enum ethtool_link_mode_bit_indices and phylib are out of sync. "
		"If a speed or mode has been added please update phy_speed_to_str "
		"and the PHY settings array.\n");

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
	case SPEED_200000:
		return "200Gbps";
	case SPEED_400000:
		return "400Gbps";
	case SPEED_800000:
		return "800Gbps";
	case SPEED_UNKNOWN:
		return "Unknown";
	default:
		return "Unsupported (update phy-core.c)";
	}
}
EXPORT_SYMBOL_GPL(phy_speed_to_str);

/**
 * phy_duplex_to_str - Return string describing the duplex
 *
 * @duplex: Duplex setting to describe
 */
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

/**
 * phy_rate_matching_to_str - Return a string describing the rate matching
 *
 * @rate_matching: Type of rate matching to describe
 */
const char *phy_rate_matching_to_str(int rate_matching)
{
	switch (rate_matching) {
	case RATE_MATCH_NONE:
		return "none";
	case RATE_MATCH_PAUSE:
		return "pause";
	case RATE_MATCH_CRS:
		return "crs";
	case RATE_MATCH_OPEN_LOOP:
		return "open-loop";
	}
	return "Unsupported (update phy-core.c)";
}
EXPORT_SYMBOL_GPL(phy_rate_matching_to_str);

/**
 * phy_interface_num_ports - Return the number of links that can be carried by
 *			     a given MAC-PHY physical link. Returns 0 if this is
 *			     unknown, the number of links else.
 *
 * @interface: The interface mode we want to get the number of ports
 */
int phy_interface_num_ports(phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_NA:
		return 0;
	case PHY_INTERFACE_MODE_INTERNAL:
	case PHY_INTERFACE_MODE_MII:
	case PHY_INTERFACE_MODE_MIILITE:
	case PHY_INTERFACE_MODE_GMII:
	case PHY_INTERFACE_MODE_TBI:
	case PHY_INTERFACE_MODE_REVMII:
	case PHY_INTERFACE_MODE_RMII:
	case PHY_INTERFACE_MODE_REVRMII:
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RTBI:
	case PHY_INTERFACE_MODE_XGMII:
	case PHY_INTERFACE_MODE_XLGMII:
	case PHY_INTERFACE_MODE_MOCA:
	case PHY_INTERFACE_MODE_TRGMII:
	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_SMII:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
	case PHY_INTERFACE_MODE_5GBASER:
	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_25GBASER:
	case PHY_INTERFACE_MODE_10GKR:
	case PHY_INTERFACE_MODE_100BASEX:
	case PHY_INTERFACE_MODE_RXAUI:
	case PHY_INTERFACE_MODE_XAUI:
	case PHY_INTERFACE_MODE_1000BASEKX:
	case PHY_INTERFACE_MODE_50GBASER:
	case PHY_INTERFACE_MODE_LAUI:
	case PHY_INTERFACE_MODE_100GBASEP:
		return 1;
	case PHY_INTERFACE_MODE_QSGMII:
	case PHY_INTERFACE_MODE_QUSGMII:
	case PHY_INTERFACE_MODE_10G_QXGMII:
		return 4;
	case PHY_INTERFACE_MODE_PSGMII:
		return 5;
	case PHY_INTERFACE_MODE_MAX:
		WARN_ONCE(1, "PHY_INTERFACE_MODE_MAX isn't a valid interface mode");
		return 0;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(phy_interface_num_ports);

static void __set_phy_supported(struct phy_device *phydev, u32 max_speed)
{
	phy_caps_linkmode_max_speed(max_speed, phydev->supported);
}

/**
 * phy_set_max_speed - Set the maximum speed the PHY should support
 *
 * @phydev: The phy_device struct
 * @max_speed: Maximum speed
 *
 * The PHY might be more capable than the MAC. For example a Fast Ethernet
 * is connected to a 1G PHY. This function allows the MAC to indicate its
 * maximum speed, and so limit what the PHY will advertise.
 */
void phy_set_max_speed(struct phy_device *phydev, u32 max_speed)
{
	__set_phy_supported(phydev, max_speed);

	phy_advertise_supported(phydev);
}
EXPORT_SYMBOL(phy_set_max_speed);

void of_set_phy_supported(struct phy_device *phydev)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	u32 max_speed;

	if (!IS_ENABLED(CONFIG_OF_MDIO))
		return;

	if (!node)
		return;

	if (!of_property_read_u32(node, "max-speed", &max_speed))
		__set_phy_supported(phydev, max_speed);
}

void of_set_phy_eee_broken(struct phy_device *phydev)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	unsigned long *modes = phydev->eee_disabled_modes;

	if (!IS_ENABLED(CONFIG_OF_MDIO) || !node)
		return;

	linkmode_zero(modes);

	if (of_property_read_bool(node, "eee-broken-100tx"))
		linkmode_set_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT, modes);
	if (of_property_read_bool(node, "eee-broken-1000t"))
		linkmode_set_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT, modes);
	if (of_property_read_bool(node, "eee-broken-10gt"))
		linkmode_set_bit(ETHTOOL_LINK_MODE_10000baseT_Full_BIT, modes);
	if (of_property_read_bool(node, "eee-broken-1000kx"))
		linkmode_set_bit(ETHTOOL_LINK_MODE_1000baseKX_Full_BIT, modes);
	if (of_property_read_bool(node, "eee-broken-10gkx4"))
		linkmode_set_bit(ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT, modes);
	if (of_property_read_bool(node, "eee-broken-10gkr"))
		linkmode_set_bit(ETHTOOL_LINK_MODE_10000baseKR_Full_BIT, modes);
}

/**
 * of_set_phy_timing_role - Set the master/slave mode of the PHY
 *
 * @phydev: The phy_device struct
 *
 * Set master/slave configuration of the PHY based on the device tree.
 */
void of_set_phy_timing_role(struct phy_device *phydev)
{
	struct device_node *node = phydev->mdio.dev.of_node;
	const char *master;

	if (!IS_ENABLED(CONFIG_OF_MDIO))
		return;

	if (!node)
		return;

	if (of_property_read_string(node, "timing-role", &master))
		return;

	if (strcmp(master, "forced-master") == 0)
		phydev->master_slave_set = MASTER_SLAVE_CFG_MASTER_FORCE;
	else if (strcmp(master, "forced-slave") == 0)
		phydev->master_slave_set = MASTER_SLAVE_CFG_SLAVE_FORCE;
	else if (strcmp(master, "preferred-master") == 0)
		phydev->master_slave_set = MASTER_SLAVE_CFG_MASTER_PREFERRED;
	else if (strcmp(master, "preferred-slave") == 0)
		phydev->master_slave_set = MASTER_SLAVE_CFG_SLAVE_PREFERRED;
	else
		phydev_warn(phydev, "Unknown master-slave mode %s\n", master);
}

/**
 * phy_resolve_aneg_pause - Determine pause autoneg results
 *
 * @phydev: The phy_device struct
 *
 * Once autoneg has completed the local pause settings can be
 * resolved.  Determine if pause and asymmetric pause should be used
 * by the MAC.
 */

void phy_resolve_aneg_pause(struct phy_device *phydev)
{
	if (phydev->duplex == DUPLEX_FULL) {
		phydev->pause = linkmode_test_bit(ETHTOOL_LINK_MODE_Pause_BIT,
						  phydev->lp_advertising);
		phydev->asym_pause = linkmode_test_bit(
			ETHTOOL_LINK_MODE_Asym_Pause_BIT,
			phydev->lp_advertising);
	}
}
EXPORT_SYMBOL_GPL(phy_resolve_aneg_pause);

/**
 * phy_resolve_aneg_linkmode - resolve the advertisements into PHY settings
 * @phydev: The phy_device struct
 *
 * Resolve our and the link partner advertisements into their corresponding
 * speed and duplex. If full duplex was negotiated, extract the pause mode
 * from the link partner mask.
 */
void phy_resolve_aneg_linkmode(struct phy_device *phydev)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(common);
	const struct link_capabilities *c;

	linkmode_and(common, phydev->lp_advertising, phydev->advertising);

	c = phy_caps_lookup_by_linkmode(common);
	if (c) {
		phydev->speed = c->speed;
		phydev->duplex = c->duplex;
	}

	phy_resolve_aneg_pause(phydev);
}
EXPORT_SYMBOL_GPL(phy_resolve_aneg_linkmode);

/**
 * phy_check_downshift - check whether downshift occurred
 * @phydev: The phy_device struct
 *
 * Check whether a downshift to a lower speed occurred. If this should be the
 * case warn the user.
 * Prerequisite for detecting downshift is that PHY driver implements the
 * read_status callback and sets phydev->speed to the actual link speed.
 */
void phy_check_downshift(struct phy_device *phydev)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(common);
	const struct link_capabilities *c;
	int speed = SPEED_UNKNOWN;

	phydev->downshifted_rate = 0;

	if (phydev->autoneg == AUTONEG_DISABLE ||
	    phydev->speed == SPEED_UNKNOWN)
		return;

	linkmode_and(common, phydev->lp_advertising, phydev->advertising);

	c = phy_caps_lookup_by_linkmode(common);
	if (c)
		speed = c->speed;

	if (speed == SPEED_UNKNOWN || phydev->speed >= speed)
		return;

	phydev_warn(phydev, "Downshift occurred from negotiated speed %s to actual speed %s, check cabling!\n",
		    phy_speed_to_str(speed), phy_speed_to_str(phydev->speed));

	phydev->downshifted_rate = 1;
}

static int phy_resolve_min_speed(struct phy_device *phydev, bool fdx_only)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(common);
	const struct link_capabilities *c;

	linkmode_and(common, phydev->lp_advertising, phydev->advertising);

	c = phy_caps_lookup_by_linkmode_rev(common, fdx_only);
	if (c)
		return c->speed;

	return SPEED_UNKNOWN;
}

int phy_speed_down_core(struct phy_device *phydev)
{
	int min_common_speed = phy_resolve_min_speed(phydev, true);

	if (min_common_speed == SPEED_UNKNOWN)
		return -EINVAL;

	phy_caps_linkmode_max_speed(min_common_speed, phydev->advertising);

	return 0;
}

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

int mmd_phy_read(struct mii_bus *bus, int phy_addr, bool is_c45,
		 int devad, u32 regnum)
{
	if (is_c45)
		return __mdiobus_c45_read(bus, phy_addr, devad, regnum);

	mmd_phy_indirect(bus, phy_addr, devad, regnum);
	/* Read the content of the MMD's selected register */
	return __mdiobus_read(bus, phy_addr, MII_MMD_DATA);
}
EXPORT_SYMBOL_GPL(mmd_phy_read);

int mmd_phy_write(struct mii_bus *bus, int phy_addr, bool is_c45,
		  int devad, u32 regnum, u16 val)
{
	if (is_c45)
		return __mdiobus_c45_write(bus, phy_addr, devad, regnum, val);

	mmd_phy_indirect(bus, phy_addr, devad, regnum);
	/* Write the data into MMD's selected register */
	return __mdiobus_write(bus, phy_addr, MII_MMD_DATA, val);
}
EXPORT_SYMBOL_GPL(mmd_phy_write);

/**
 * __phy_read_mmd - Convenience function for reading a register
 * from an MMD on a given PHY.
 * @phydev: The phy_device struct
 * @devad: The MMD to read from (0..31)
 * @regnum: The register on the MMD to read (0..65535)
 *
 * Same rules as for __phy_read();
 */
int __phy_read_mmd(struct phy_device *phydev, int devad, u32 regnum)
{
	if (regnum > (u16)~0 || devad > 32)
		return -EINVAL;

	if (phydev->drv && phydev->drv->read_mmd)
		return phydev->drv->read_mmd(phydev, devad, regnum);

	return mmd_phy_read(phydev->mdio.bus, phydev->mdio.addr,
			    phydev->is_c45, devad, regnum);
}
EXPORT_SYMBOL(__phy_read_mmd);

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
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = __phy_read_mmd(phydev, devad, regnum);
	phy_unlock_mdio_bus(phydev);

	return ret;
}
EXPORT_SYMBOL(phy_read_mmd);

/**
 * __phy_write_mmd - Convenience function for writing a register
 * on an MMD on a given PHY.
 * @phydev: The phy_device struct
 * @devad: The MMD to read from
 * @regnum: The register on the MMD to read
 * @val: value to write to @regnum
 *
 * Same rules as for __phy_write();
 */
int __phy_write_mmd(struct phy_device *phydev, int devad, u32 regnum, u16 val)
{
	if (regnum > (u16)~0 || devad > 32)
		return -EINVAL;

	if (phydev->drv && phydev->drv->write_mmd)
		return phydev->drv->write_mmd(phydev, devad, regnum, val);

	return mmd_phy_write(phydev->mdio.bus, phydev->mdio.addr,
			     phydev->is_c45, devad, regnum, val);
}
EXPORT_SYMBOL(__phy_write_mmd);

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

	phy_lock_mdio_bus(phydev);
	ret = __phy_write_mmd(phydev, devad, regnum, val);
	phy_unlock_mdio_bus(phydev);

	return ret;
}
EXPORT_SYMBOL(phy_write_mmd);

/**
 * phy_modify_changed - Function for modifying a PHY register
 * @phydev: the phy_device struct
 * @regnum: register number to modify
 * @mask: bit mask of bits to clear
 * @set: new value of bits set in mask to write to @regnum
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 *
 * Returns negative errno, 0 if there was no change, and 1 in case of change
 */
int phy_modify_changed(struct phy_device *phydev, u32 regnum, u16 mask, u16 set)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = __phy_modify_changed(phydev, regnum, mask, set);
	phy_unlock_mdio_bus(phydev);

	return ret;
}
EXPORT_SYMBOL_GPL(phy_modify_changed);

/**
 * __phy_modify - Convenience function for modifying a PHY register
 * @phydev: the phy_device struct
 * @regnum: register number to modify
 * @mask: bit mask of bits to clear
 * @set: new value of bits set in mask to write to @regnum
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 */
int __phy_modify(struct phy_device *phydev, u32 regnum, u16 mask, u16 set)
{
	int ret;

	ret = __phy_modify_changed(phydev, regnum, mask, set);

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

	phy_lock_mdio_bus(phydev);
	ret = __phy_modify(phydev, regnum, mask, set);
	phy_unlock_mdio_bus(phydev);

	return ret;
}
EXPORT_SYMBOL_GPL(phy_modify);

/**
 * __phy_modify_mmd_changed - Function for modifying a register on MMD
 * @phydev: the phy_device struct
 * @devad: the MMD containing register to modify
 * @regnum: register number to modify
 * @mask: bit mask of bits to clear
 * @set: new value of bits set in mask to write to @regnum
 *
 * Unlocked helper function which allows a MMD register to be modified as
 * new register value = (old register value & ~mask) | set
 *
 * Returns negative errno, 0 if there was no change, and 1 in case of change
 */
int __phy_modify_mmd_changed(struct phy_device *phydev, int devad, u32 regnum,
			     u16 mask, u16 set)
{
	int new, ret;

	ret = __phy_read_mmd(phydev, devad, regnum);
	if (ret < 0)
		return ret;

	new = (ret & ~mask) | set;
	if (new == ret)
		return 0;

	ret = __phy_write_mmd(phydev, devad, regnum, new);

	return ret < 0 ? ret : 1;
}
EXPORT_SYMBOL_GPL(__phy_modify_mmd_changed);

/**
 * phy_modify_mmd_changed - Function for modifying a register on MMD
 * @phydev: the phy_device struct
 * @devad: the MMD containing register to modify
 * @regnum: register number to modify
 * @mask: bit mask of bits to clear
 * @set: new value of bits set in mask to write to @regnum
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 *
 * Returns negative errno, 0 if there was no change, and 1 in case of change
 */
int phy_modify_mmd_changed(struct phy_device *phydev, int devad, u32 regnum,
			   u16 mask, u16 set)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = __phy_modify_mmd_changed(phydev, devad, regnum, mask, set);
	phy_unlock_mdio_bus(phydev);

	return ret;
}
EXPORT_SYMBOL_GPL(phy_modify_mmd_changed);

/**
 * __phy_modify_mmd - Convenience function for modifying a register on MMD
 * @phydev: the phy_device struct
 * @devad: the MMD containing register to modify
 * @regnum: register number to modify
 * @mask: bit mask of bits to clear
 * @set: new value of bits set in mask to write to @regnum
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 */
int __phy_modify_mmd(struct phy_device *phydev, int devad, u32 regnum,
		     u16 mask, u16 set)
{
	int ret;

	ret = __phy_modify_mmd_changed(phydev, devad, regnum, mask, set);

	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL_GPL(__phy_modify_mmd);

/**
 * phy_modify_mmd - Convenience function for modifying a register on MMD
 * @phydev: the phy_device struct
 * @devad: the MMD containing register to modify
 * @regnum: register number to modify
 * @mask: bit mask of bits to clear
 * @set: new value of bits set in mask to write to @regnum
 *
 * NOTE: MUST NOT be called from interrupt context,
 * because the bus read/write functions may wait for an interrupt
 * to conclude the operation.
 */
int phy_modify_mmd(struct phy_device *phydev, int devad, u32 regnum,
		   u16 mask, u16 set)
{
	int ret;

	phy_lock_mdio_bus(phydev);
	ret = __phy_modify_mmd(phydev, devad, regnum, mask, set);
	phy_unlock_mdio_bus(phydev);

	return ret;
}
EXPORT_SYMBOL_GPL(phy_modify_mmd);

static int __phy_read_page(struct phy_device *phydev)
{
	if (WARN_ONCE(!phydev->drv->read_page, "read_page callback not available, PHY driver not loaded?\n"))
		return -EOPNOTSUPP;

	return phydev->drv->read_page(phydev);
}

static int __phy_write_page(struct phy_device *phydev, int page)
{
	if (WARN_ONCE(!phydev->drv->write_page, "write_page callback not available, PHY driver not loaded?\n"))
		return -EOPNOTSUPP;

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
	phy_lock_mdio_bus(phydev);
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

	phy_unlock_mdio_bus(phydev);

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
 * phy_modify_paged_changed() - Function for modifying a paged register
 * @phydev: a pointer to a &struct phy_device
 * @page: the page for the phy
 * @regnum: register number
 * @mask: bit mask of bits to clear
 * @set: bit mask of bits to set
 *
 * Returns negative errno, 0 if there was no change, and 1 in case of change
 */
int phy_modify_paged_changed(struct phy_device *phydev, int page, u32 regnum,
			     u16 mask, u16 set)
{
	int ret = 0, oldpage;

	oldpage = phy_select_page(phydev, page);
	if (oldpage >= 0)
		ret = __phy_modify_changed(phydev, regnum, mask, set);

	return phy_restore_page(phydev, oldpage, ret);
}
EXPORT_SYMBOL(phy_modify_paged_changed);

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
	int ret = phy_modify_paged_changed(phydev, page, regnum, mask, set);

	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL(phy_modify_paged);

// SPDX-License-Identifier: GPL-2.0
/*
 * phylink models the MAC to optional PHY connection, supporting
 * technologies such as SFP cages where the PHY is hot-pluggable.
 *
 * Copyright (C) 2015 Russell King
 */
#include <linux/acpi.h>
#include <linux/ethtool.h>
#include <linux/export.h>
#include <linux/gpio/consumer.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/phylink.h>
#include <linux/rtnetlink.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#include "sfp.h"
#include "swphy.h"

#define SUPPORTED_INTERFACES \
	(SUPPORTED_TP | SUPPORTED_MII | SUPPORTED_FIBRE | \
	 SUPPORTED_BNC | SUPPORTED_AUI | SUPPORTED_Backplane)
#define ADVERTISED_INTERFACES \
	(ADVERTISED_TP | ADVERTISED_MII | ADVERTISED_FIBRE | \
	 ADVERTISED_BNC | ADVERTISED_AUI | ADVERTISED_Backplane)

enum {
	PHYLINK_DISABLE_STOPPED,
	PHYLINK_DISABLE_LINK,
	PHYLINK_DISABLE_MAC_WOL,

	PCS_STATE_DOWN = 0,
	PCS_STATE_STARTING,
	PCS_STATE_STARTED,
};

/**
 * struct phylink - internal data type for phylink
 */
struct phylink {
	/* private: */
	struct net_device *netdev;
	const struct phylink_mac_ops *mac_ops;
	struct phylink_config *config;
	struct phylink_pcs *pcs;
	struct device *dev;
	unsigned int old_link_state:1;

	unsigned long phylink_disable_state; /* bitmask of disables */
	struct phy_device *phydev;
	phy_interface_t link_interface;	/* PHY_INTERFACE_xxx */
	u8 cfg_link_an_mode;		/* MLO_AN_xxx */
	u8 req_link_an_mode;		/* Requested MLO_AN_xxx mode */
	u8 act_link_an_mode;		/* Active MLO_AN_xxx mode */
	u8 link_port;			/* The current non-phy ethtool port */
	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported);

	/* The link configuration settings */
	struct phylink_link_state link_config;

	/* The current settings */
	phy_interface_t cur_interface;

	struct gpio_desc *link_gpio;
	unsigned int link_irq;
	struct timer_list link_poll;
	void (*get_fixed_state)(struct net_device *dev,
				struct phylink_link_state *s);

	struct mutex state_mutex;
	struct phylink_link_state phy_state;
	unsigned int phy_ib_mode;
	struct work_struct resolve;
	unsigned int pcs_neg_mode;
	unsigned int pcs_state;

	bool link_failed;

	struct sfp_bus *sfp_bus;
	bool sfp_may_have_phy;
	DECLARE_PHY_INTERFACE_MASK(sfp_interfaces);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(sfp_support);
	u8 sfp_port;
};

#define phylink_printk(level, pl, fmt, ...) \
	do { \
		if ((pl)->config->type == PHYLINK_NETDEV) \
			netdev_printk(level, (pl)->netdev, fmt, ##__VA_ARGS__); \
		else if ((pl)->config->type == PHYLINK_DEV) \
			dev_printk(level, (pl)->dev, fmt, ##__VA_ARGS__); \
	} while (0)

#define phylink_err(pl, fmt, ...) \
	phylink_printk(KERN_ERR, pl, fmt, ##__VA_ARGS__)
#define phylink_warn(pl, fmt, ...) \
	phylink_printk(KERN_WARNING, pl, fmt, ##__VA_ARGS__)
#define phylink_info(pl, fmt, ...) \
	phylink_printk(KERN_INFO, pl, fmt, ##__VA_ARGS__)
#if defined(CONFIG_DYNAMIC_DEBUG)
#define phylink_dbg(pl, fmt, ...) \
do {									\
	if ((pl)->config->type == PHYLINK_NETDEV)			\
		netdev_dbg((pl)->netdev, fmt, ##__VA_ARGS__);		\
	else if ((pl)->config->type == PHYLINK_DEV)			\
		dev_dbg((pl)->dev, fmt, ##__VA_ARGS__);			\
} while (0)
#elif defined(DEBUG)
#define phylink_dbg(pl, fmt, ...)					\
	phylink_printk(KERN_DEBUG, pl, fmt, ##__VA_ARGS__)
#else
#define phylink_dbg(pl, fmt, ...)					\
({									\
	if (0)								\
		phylink_printk(KERN_DEBUG, pl, fmt, ##__VA_ARGS__);	\
})
#endif

static const phy_interface_t phylink_sfp_interface_preference[] = {
	PHY_INTERFACE_MODE_25GBASER,
	PHY_INTERFACE_MODE_USXGMII,
	PHY_INTERFACE_MODE_10GBASER,
	PHY_INTERFACE_MODE_5GBASER,
	PHY_INTERFACE_MODE_2500BASEX,
	PHY_INTERFACE_MODE_SGMII,
	PHY_INTERFACE_MODE_1000BASEX,
	PHY_INTERFACE_MODE_100BASEX,
};

static DECLARE_PHY_INTERFACE_MASK(phylink_sfp_interfaces);

/**
 * phylink_set_port_modes() - set the port type modes in the ethtool mask
 * @mask: ethtool link mode mask
 *
 * Sets all the port type modes in the ethtool mask.  MAC drivers should
 * use this in their 'validate' callback.
 */
void phylink_set_port_modes(unsigned long *mask)
{
	phylink_set(mask, TP);
	phylink_set(mask, AUI);
	phylink_set(mask, MII);
	phylink_set(mask, FIBRE);
	phylink_set(mask, BNC);
	phylink_set(mask, Backplane);
}
EXPORT_SYMBOL_GPL(phylink_set_port_modes);

static int phylink_is_empty_linkmode(const unsigned long *linkmode)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(tmp) = { 0, };

	phylink_set_port_modes(tmp);
	phylink_set(tmp, Autoneg);
	phylink_set(tmp, Pause);
	phylink_set(tmp, Asym_Pause);

	return linkmode_subset(linkmode, tmp);
}

static const char *phylink_an_mode_str(unsigned int mode)
{
	static const char *modestr[] = {
		[MLO_AN_PHY] = "phy",
		[MLO_AN_FIXED] = "fixed",
		[MLO_AN_INBAND] = "inband",
	};

	return mode < ARRAY_SIZE(modestr) ? modestr[mode] : "unknown";
}

static const char *phylink_pcs_mode_str(unsigned int mode)
{
	if (!mode)
		return "none";

	if (mode & PHYLINK_PCS_NEG_OUTBAND)
		return "outband";

	if (mode & PHYLINK_PCS_NEG_INBAND) {
		if (mode & PHYLINK_PCS_NEG_ENABLED)
			return "inband,an-enabled";
		else
			return "inband,an-disabled";
	}

	return "unknown";
}

static unsigned int phylink_interface_signal_rate(phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_1000BASEX: /* 1.25Mbd */
		return 1250;
	case PHY_INTERFACE_MODE_2500BASEX: /* 3.125Mbd */
		return 3125;
	case PHY_INTERFACE_MODE_5GBASER: /* 5.15625Mbd */
		return 5156;
	case PHY_INTERFACE_MODE_10GBASER: /* 10.3125Mbd */
		return 10313;
	default:
		return 0;
	}
}

/**
 * phylink_interface_max_speed() - get the maximum speed of a phy interface
 * @interface: phy interface mode defined by &typedef phy_interface_t
 *
 * Determine the maximum speed of a phy interface. This is intended to help
 * determine the correct speed to pass to the MAC when the phy is performing
 * rate matching.
 *
 * Return: The maximum speed of @interface
 */
static int phylink_interface_max_speed(phy_interface_t interface)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_100BASEX:
	case PHY_INTERFACE_MODE_REVRMII:
	case PHY_INTERFACE_MODE_RMII:
	case PHY_INTERFACE_MODE_SMII:
	case PHY_INTERFACE_MODE_REVMII:
	case PHY_INTERFACE_MODE_MII:
		return SPEED_100;

	case PHY_INTERFACE_MODE_TBI:
	case PHY_INTERFACE_MODE_MOCA:
	case PHY_INTERFACE_MODE_RTBI:
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_1000BASEKX:
	case PHY_INTERFACE_MODE_TRGMII:
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_PSGMII:
	case PHY_INTERFACE_MODE_QSGMII:
	case PHY_INTERFACE_MODE_QUSGMII:
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_GMII:
		return SPEED_1000;

	case PHY_INTERFACE_MODE_2500BASEX:
	case PHY_INTERFACE_MODE_10G_QXGMII:
		return SPEED_2500;

	case PHY_INTERFACE_MODE_5GBASER:
		return SPEED_5000;

	case PHY_INTERFACE_MODE_XGMII:
	case PHY_INTERFACE_MODE_RXAUI:
	case PHY_INTERFACE_MODE_XAUI:
	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_10GKR:
	case PHY_INTERFACE_MODE_USXGMII:
		return SPEED_10000;

	case PHY_INTERFACE_MODE_25GBASER:
		return SPEED_25000;

	case PHY_INTERFACE_MODE_XLGMII:
		return SPEED_40000;

	case PHY_INTERFACE_MODE_INTERNAL:
	case PHY_INTERFACE_MODE_NA:
	case PHY_INTERFACE_MODE_MAX:
		/* No idea! Garbage in, unknown out */
		return SPEED_UNKNOWN;
	}

	/* If we get here, someone forgot to add an interface mode above */
	WARN_ON_ONCE(1);
	return SPEED_UNKNOWN;
}

/**
 * phylink_caps_to_linkmodes() - Convert capabilities to ethtool link modes
 * @linkmodes: ethtool linkmode mask (must be already initialised)
 * @caps: bitmask of MAC capabilities
 *
 * Set all possible pause, speed and duplex linkmodes in @linkmodes that are
 * supported by the @caps. @linkmodes must have been initialised previously.
 */
static void phylink_caps_to_linkmodes(unsigned long *linkmodes,
				      unsigned long caps)
{
	if (caps & MAC_SYM_PAUSE)
		__set_bit(ETHTOOL_LINK_MODE_Pause_BIT, linkmodes);

	if (caps & MAC_ASYM_PAUSE)
		__set_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT, linkmodes);

	if (caps & MAC_10HD) {
		__set_bit(ETHTOOL_LINK_MODE_10baseT_Half_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_10baseT1S_Half_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_10baseT1S_P2MP_Half_BIT, linkmodes);
	}

	if (caps & MAC_10FD) {
		__set_bit(ETHTOOL_LINK_MODE_10baseT_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_10baseT1L_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_10baseT1S_Full_BIT, linkmodes);
	}

	if (caps & MAC_100HD) {
		__set_bit(ETHTOOL_LINK_MODE_100baseT_Half_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_100baseFX_Half_BIT, linkmodes);
	}

	if (caps & MAC_100FD) {
		__set_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_100baseT1_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_100baseFX_Full_BIT, linkmodes);
	}

	if (caps & MAC_1000HD)
		__set_bit(ETHTOOL_LINK_MODE_1000baseT_Half_BIT, linkmodes);

	if (caps & MAC_1000FD) {
		__set_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_1000baseKX_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_1000baseX_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_1000baseT1_Full_BIT, linkmodes);
	}

	if (caps & MAC_2500FD) {
		__set_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_2500baseX_Full_BIT, linkmodes);
	}

	if (caps & MAC_5000FD)
		__set_bit(ETHTOOL_LINK_MODE_5000baseT_Full_BIT, linkmodes);

	if (caps & MAC_10000FD) {
		__set_bit(ETHTOOL_LINK_MODE_10000baseT_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_10000baseKR_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_10000baseR_FEC_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_10000baseCR_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_10000baseSR_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_10000baseLR_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_10000baseLRM_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_10000baseER_Full_BIT, linkmodes);
	}

	if (caps & MAC_25000FD) {
		__set_bit(ETHTOOL_LINK_MODE_25000baseCR_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_25000baseKR_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_25000baseSR_Full_BIT, linkmodes);
	}

	if (caps & MAC_40000FD) {
		__set_bit(ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_40000baseSR4_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_40000baseLR4_Full_BIT, linkmodes);
	}

	if (caps & MAC_50000FD) {
		__set_bit(ETHTOOL_LINK_MODE_50000baseCR2_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_50000baseKR2_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_50000baseSR2_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_50000baseKR_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_50000baseSR_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_50000baseCR_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_50000baseLR_ER_FR_Full_BIT,
			  linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_50000baseDR_Full_BIT, linkmodes);
	}

	if (caps & MAC_56000FD) {
		__set_bit(ETHTOOL_LINK_MODE_56000baseKR4_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_56000baseCR4_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_56000baseSR4_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_56000baseLR4_Full_BIT, linkmodes);
	}

	if (caps & MAC_100000FD) {
		__set_bit(ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_100000baseSR4_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_100000baseLR4_ER4_Full_BIT,
			  linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_100000baseKR2_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_100000baseSR2_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_100000baseCR2_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_100000baseLR2_ER2_FR2_Full_BIT,
			  linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_100000baseDR2_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_100000baseKR_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_100000baseSR_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_100000baseLR_ER_FR_Full_BIT,
			  linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_100000baseCR_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_100000baseDR_Full_BIT, linkmodes);
	}

	if (caps & MAC_200000FD) {
		__set_bit(ETHTOOL_LINK_MODE_200000baseKR4_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_200000baseSR4_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_200000baseLR4_ER4_FR4_Full_BIT,
			  linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_200000baseDR4_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_200000baseCR4_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_200000baseKR2_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_200000baseSR2_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_200000baseLR2_ER2_FR2_Full_BIT,
			  linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_200000baseDR2_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_200000baseCR2_Full_BIT, linkmodes);
	}

	if (caps & MAC_400000FD) {
		__set_bit(ETHTOOL_LINK_MODE_400000baseKR8_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_400000baseSR8_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_400000baseLR8_ER8_FR8_Full_BIT,
			  linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_400000baseDR8_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_400000baseCR8_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_400000baseKR4_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_400000baseSR4_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_400000baseLR4_ER4_FR4_Full_BIT,
			  linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_400000baseDR4_Full_BIT, linkmodes);
		__set_bit(ETHTOOL_LINK_MODE_400000baseCR4_Full_BIT, linkmodes);
	}
}

static struct {
	unsigned long mask;
	int speed;
	unsigned int duplex;
} phylink_caps_params[] = {
	{ MAC_400000FD, SPEED_400000, DUPLEX_FULL },
	{ MAC_200000FD, SPEED_200000, DUPLEX_FULL },
	{ MAC_100000FD, SPEED_100000, DUPLEX_FULL },
	{ MAC_56000FD,  SPEED_56000,  DUPLEX_FULL },
	{ MAC_50000FD,  SPEED_50000,  DUPLEX_FULL },
	{ MAC_40000FD,  SPEED_40000,  DUPLEX_FULL },
	{ MAC_25000FD,  SPEED_25000,  DUPLEX_FULL },
	{ MAC_20000FD,  SPEED_20000,  DUPLEX_FULL },
	{ MAC_10000FD,  SPEED_10000,  DUPLEX_FULL },
	{ MAC_5000FD,   SPEED_5000,   DUPLEX_FULL },
	{ MAC_2500FD,   SPEED_2500,   DUPLEX_FULL },
	{ MAC_1000FD,   SPEED_1000,   DUPLEX_FULL },
	{ MAC_1000HD,   SPEED_1000,   DUPLEX_HALF },
	{ MAC_100FD,    SPEED_100,    DUPLEX_FULL },
	{ MAC_100HD,    SPEED_100,    DUPLEX_HALF },
	{ MAC_10FD,     SPEED_10,     DUPLEX_FULL },
	{ MAC_10HD,     SPEED_10,     DUPLEX_HALF },
};

/**
 * phylink_limit_mac_speed - limit the phylink_config to a maximum speed
 * @config: pointer to a &struct phylink_config
 * @max_speed: maximum speed
 *
 * Mask off MAC capabilities for speeds higher than the @max_speed parameter.
 * Any further motifications of config.mac_capabilities will override this.
 */
void phylink_limit_mac_speed(struct phylink_config *config, u32 max_speed)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(phylink_caps_params) &&
		    phylink_caps_params[i].speed > max_speed; i++)
		config->mac_capabilities &= ~phylink_caps_params[i].mask;
}
EXPORT_SYMBOL_GPL(phylink_limit_mac_speed);

/**
 * phylink_cap_from_speed_duplex - Get mac capability from speed/duplex
 * @speed: the speed to search for
 * @duplex: the duplex to search for
 *
 * Find the mac capability for a given speed and duplex.
 *
 * Return: A mask with the mac capability patching @speed and @duplex, or 0 if
 *         there were no matches.
 */
static unsigned long phylink_cap_from_speed_duplex(int speed,
						   unsigned int duplex)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(phylink_caps_params); i++) {
		if (speed == phylink_caps_params[i].speed &&
		    duplex == phylink_caps_params[i].duplex)
			return phylink_caps_params[i].mask;
	}

	return 0;
}

/**
 * phylink_get_capabilities() - get capabilities for a given MAC
 * @interface: phy interface mode defined by &typedef phy_interface_t
 * @mac_capabilities: bitmask of MAC capabilities
 * @rate_matching: type of rate matching being performed
 *
 * Get the MAC capabilities that are supported by the @interface mode and
 * @mac_capabilities.
 */
static unsigned long phylink_get_capabilities(phy_interface_t interface,
					      unsigned long mac_capabilities,
					      int rate_matching)
{
	int max_speed = phylink_interface_max_speed(interface);
	unsigned long caps = MAC_SYM_PAUSE | MAC_ASYM_PAUSE;
	unsigned long matched_caps = 0;

	switch (interface) {
	case PHY_INTERFACE_MODE_USXGMII:
		caps |= MAC_10000FD | MAC_5000FD;
		fallthrough;

	case PHY_INTERFACE_MODE_10G_QXGMII:
		caps |= MAC_2500FD;
		fallthrough;

	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_PSGMII:
	case PHY_INTERFACE_MODE_QSGMII:
	case PHY_INTERFACE_MODE_QUSGMII:
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_GMII:
		caps |= MAC_1000HD | MAC_1000FD;
		fallthrough;

	case PHY_INTERFACE_MODE_REVRMII:
	case PHY_INTERFACE_MODE_RMII:
	case PHY_INTERFACE_MODE_SMII:
	case PHY_INTERFACE_MODE_REVMII:
	case PHY_INTERFACE_MODE_MII:
		caps |= MAC_10HD | MAC_10FD;
		fallthrough;

	case PHY_INTERFACE_MODE_100BASEX:
		caps |= MAC_100HD | MAC_100FD;
		break;

	case PHY_INTERFACE_MODE_TBI:
	case PHY_INTERFACE_MODE_MOCA:
	case PHY_INTERFACE_MODE_RTBI:
	case PHY_INTERFACE_MODE_1000BASEX:
		caps |= MAC_1000HD;
		fallthrough;
	case PHY_INTERFACE_MODE_1000BASEKX:
	case PHY_INTERFACE_MODE_TRGMII:
		caps |= MAC_1000FD;
		break;

	case PHY_INTERFACE_MODE_2500BASEX:
		caps |= MAC_2500FD;
		break;

	case PHY_INTERFACE_MODE_5GBASER:
		caps |= MAC_5000FD;
		break;

	case PHY_INTERFACE_MODE_XGMII:
	case PHY_INTERFACE_MODE_RXAUI:
	case PHY_INTERFACE_MODE_XAUI:
	case PHY_INTERFACE_MODE_10GBASER:
	case PHY_INTERFACE_MODE_10GKR:
		caps |= MAC_10000FD;
		break;

	case PHY_INTERFACE_MODE_25GBASER:
		caps |= MAC_25000FD;
		break;

	case PHY_INTERFACE_MODE_XLGMII:
		caps |= MAC_40000FD;
		break;

	case PHY_INTERFACE_MODE_INTERNAL:
		caps |= ~0;
		break;

	case PHY_INTERFACE_MODE_NA:
	case PHY_INTERFACE_MODE_MAX:
		break;
	}

	switch (rate_matching) {
	case RATE_MATCH_OPEN_LOOP:
		/* TODO */
		fallthrough;
	case RATE_MATCH_NONE:
		matched_caps = 0;
		break;
	case RATE_MATCH_PAUSE: {
		/* The MAC must support asymmetric pause towards the local
		 * device for this. We could allow just symmetric pause, but
		 * then we might have to renegotiate if the link partner
		 * doesn't support pause. This is because there's no way to
		 * accept pause frames without transmitting them if we only
		 * support symmetric pause.
		 */
		if (!(mac_capabilities & MAC_SYM_PAUSE) ||
		    !(mac_capabilities & MAC_ASYM_PAUSE))
			break;

		/* We can't adapt if the MAC doesn't support the interface's
		 * max speed at full duplex.
		 */
		if (mac_capabilities &
		    phylink_cap_from_speed_duplex(max_speed, DUPLEX_FULL))
			matched_caps = GENMASK(__fls(caps), __fls(MAC_10HD));
		break;
	}
	case RATE_MATCH_CRS:
		/* The MAC must support half duplex at the interface's max
		 * speed.
		 */
		if (mac_capabilities &
		    phylink_cap_from_speed_duplex(max_speed, DUPLEX_HALF)) {
			matched_caps = GENMASK(__fls(caps), __fls(MAC_10HD));
			matched_caps &= mac_capabilities;
		}
		break;
	}

	return (caps & mac_capabilities) | matched_caps;
}

/**
 * phylink_validate_mask_caps() - Restrict link modes based on caps
 * @supported: ethtool bitmask for supported link modes.
 * @state: pointer to a &struct phylink_link_state.
 * @mac_capabilities: bitmask of MAC capabilities
 *
 * Calculate the supported link modes based on @mac_capabilities, and restrict
 * @supported and @state based on that. Use this function if your capabiliies
 * aren't constant, such as if they vary depending on the interface.
 */
static void phylink_validate_mask_caps(unsigned long *supported,
				       struct phylink_link_state *state,
				       unsigned long mac_capabilities)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };
	unsigned long caps;

	phylink_set_port_modes(mask);
	phylink_set(mask, Autoneg);
	caps = phylink_get_capabilities(state->interface, mac_capabilities,
					state->rate_matching);
	phylink_caps_to_linkmodes(mask, caps);

	linkmode_and(supported, supported, mask);
	linkmode_and(state->advertising, state->advertising, mask);
}

static int phylink_validate_mac_and_pcs(struct phylink *pl,
					unsigned long *supported,
					struct phylink_link_state *state)
{
	struct phylink_pcs *pcs = NULL;
	unsigned long capabilities;
	int ret;

	/* Get the PCS for this interface mode */
	if (pl->mac_ops->mac_select_pcs) {
		pcs = pl->mac_ops->mac_select_pcs(pl->config, state->interface);
		if (IS_ERR(pcs))
			return PTR_ERR(pcs);
	}

	if (pcs) {
		/* The PCS, if present, must be setup before phylink_create()
		 * has been called. If the ops is not initialised, print an
		 * error and backtrace rather than oopsing the kernel.
		 */
		if (!pcs->ops) {
			phylink_err(pl, "interface %s: uninitialised PCS\n",
				    phy_modes(state->interface));
			dump_stack();
			return -EINVAL;
		}

		/* Validate the link parameters with the PCS */
		if (pcs->ops->pcs_validate) {
			ret = pcs->ops->pcs_validate(pcs, supported, state);
			if (ret < 0 || phylink_is_empty_linkmode(supported))
				return -EINVAL;

			/* Ensure the advertising mask is a subset of the
			 * supported mask.
			 */
			linkmode_and(state->advertising, state->advertising,
				     supported);
		}
	}

	/* Then validate the link parameters with the MAC */
	if (pl->mac_ops->mac_get_caps)
		capabilities = pl->mac_ops->mac_get_caps(pl->config,
							 state->interface);
	else
		capabilities = pl->config->mac_capabilities;

	phylink_validate_mask_caps(supported, state, capabilities);

	return phylink_is_empty_linkmode(supported) ? -EINVAL : 0;
}

static void phylink_validate_one(struct phylink *pl, struct phy_device *phy,
				 const unsigned long *supported,
				 const struct phylink_link_state *state,
				 phy_interface_t interface,
				 unsigned long *accum_supported,
				 unsigned long *accum_advertising)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(tmp_supported);
	struct phylink_link_state tmp_state;

	linkmode_copy(tmp_supported, supported);

	tmp_state = *state;
	tmp_state.interface = interface;

	if (phy)
		tmp_state.rate_matching = phy_get_rate_matching(phy, interface);

	if (!phylink_validate_mac_and_pcs(pl, tmp_supported, &tmp_state)) {
		phylink_dbg(pl, " interface %u (%s) rate match %s supports %*pbl\n",
			    interface, phy_modes(interface),
			    phy_rate_matching_to_str(tmp_state.rate_matching),
			    __ETHTOOL_LINK_MODE_MASK_NBITS, tmp_supported);

		linkmode_or(accum_supported, accum_supported, tmp_supported);
		linkmode_or(accum_advertising, accum_advertising,
			    tmp_state.advertising);
	}
}

static int phylink_validate_mask(struct phylink *pl, struct phy_device *phy,
				 unsigned long *supported,
				 struct phylink_link_state *state,
				 const unsigned long *interfaces)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(all_adv) = { 0, };
	__ETHTOOL_DECLARE_LINK_MODE_MASK(all_s) = { 0, };
	int interface;

	for_each_set_bit(interface, interfaces, PHY_INTERFACE_MODE_MAX)
		phylink_validate_one(pl, phy, supported, state, interface,
				     all_s, all_adv);

	linkmode_copy(supported, all_s);
	linkmode_copy(state->advertising, all_adv);

	return phylink_is_empty_linkmode(supported) ? -EINVAL : 0;
}

static int phylink_validate(struct phylink *pl, unsigned long *supported,
			    struct phylink_link_state *state)
{
	const unsigned long *interfaces = pl->config->supported_interfaces;

	if (state->interface == PHY_INTERFACE_MODE_NA)
		return phylink_validate_mask(pl, NULL, supported, state,
					     interfaces);

	if (!test_bit(state->interface, interfaces))
		return -EINVAL;

	return phylink_validate_mac_and_pcs(pl, supported, state);
}

static int phylink_parse_fixedlink(struct phylink *pl,
				   const struct fwnode_handle *fwnode)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask) = { 0, };
	struct fwnode_handle *fixed_node;
	const struct phy_setting *s;
	struct gpio_desc *desc;
	u32 speed;
	int ret;

	fixed_node = fwnode_get_named_child_node(fwnode, "fixed-link");
	if (fixed_node) {
		ret = fwnode_property_read_u32(fixed_node, "speed", &speed);

		pl->link_config.speed = speed;
		pl->link_config.duplex = DUPLEX_HALF;

		if (fwnode_property_read_bool(fixed_node, "full-duplex"))
			pl->link_config.duplex = DUPLEX_FULL;

		/* We treat the "pause" and "asym-pause" terminology as
		 * defining the link partner's ability.
		 */
		if (fwnode_property_read_bool(fixed_node, "pause"))
			__set_bit(ETHTOOL_LINK_MODE_Pause_BIT,
				  pl->link_config.lp_advertising);
		if (fwnode_property_read_bool(fixed_node, "asym-pause"))
			__set_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT,
				  pl->link_config.lp_advertising);

		if (ret == 0) {
			desc = fwnode_gpiod_get_index(fixed_node, "link", 0,
						      GPIOD_IN, "?");

			if (!IS_ERR(desc))
				pl->link_gpio = desc;
			else if (desc == ERR_PTR(-EPROBE_DEFER))
				ret = -EPROBE_DEFER;
		}
		fwnode_handle_put(fixed_node);

		if (ret)
			return ret;
	} else {
		u32 prop[5];

		ret = fwnode_property_read_u32_array(fwnode, "fixed-link",
						     NULL, 0);
		if (ret != ARRAY_SIZE(prop)) {
			phylink_err(pl, "broken fixed-link?\n");
			return -EINVAL;
		}

		ret = fwnode_property_read_u32_array(fwnode, "fixed-link",
						     prop, ARRAY_SIZE(prop));
		if (!ret) {
			pl->link_config.duplex = prop[1] ?
						DUPLEX_FULL : DUPLEX_HALF;
			pl->link_config.speed = prop[2];
			if (prop[3])
				__set_bit(ETHTOOL_LINK_MODE_Pause_BIT,
					  pl->link_config.lp_advertising);
			if (prop[4])
				__set_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT,
					  pl->link_config.lp_advertising);
		}
	}

	if (pl->link_config.speed > SPEED_1000 &&
	    pl->link_config.duplex != DUPLEX_FULL)
		phylink_warn(pl, "fixed link specifies half duplex for %dMbps link?\n",
			     pl->link_config.speed);

	linkmode_fill(pl->supported);
	linkmode_copy(pl->link_config.advertising, pl->supported);
	phylink_validate(pl, pl->supported, &pl->link_config);

	s = phy_lookup_setting(pl->link_config.speed, pl->link_config.duplex,
			       pl->supported, true);

	linkmode_set_bit(ETHTOOL_LINK_MODE_Pause_BIT, mask);
	linkmode_set_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT, mask);
	linkmode_set_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, mask);
	linkmode_and(pl->supported, pl->supported, mask);

	phylink_set(pl->supported, MII);

	if (s) {
		__set_bit(s->bit, pl->supported);
		__set_bit(s->bit, pl->link_config.lp_advertising);
	} else {
		phylink_warn(pl, "fixed link %s duplex %dMbps not recognised\n",
			     pl->link_config.duplex == DUPLEX_FULL ? "full" : "half",
			     pl->link_config.speed);
	}

	linkmode_and(pl->link_config.advertising, pl->link_config.advertising,
		     pl->supported);

	pl->link_config.link = 1;
	pl->link_config.an_complete = 1;

	return 0;
}

static int phylink_parse_mode(struct phylink *pl,
			      const struct fwnode_handle *fwnode)
{
	struct fwnode_handle *dn;
	const char *managed;
	unsigned long caps;

	if (pl->config->default_an_inband)
		pl->cfg_link_an_mode = MLO_AN_INBAND;

	dn = fwnode_get_named_child_node(fwnode, "fixed-link");
	if (dn || fwnode_property_present(fwnode, "fixed-link"))
		pl->cfg_link_an_mode = MLO_AN_FIXED;
	fwnode_handle_put(dn);

	if ((fwnode_property_read_string(fwnode, "managed", &managed) == 0 &&
	     strcmp(managed, "in-band-status") == 0)) {
		if (pl->cfg_link_an_mode == MLO_AN_FIXED) {
			phylink_err(pl,
				    "can't use both fixed-link and in-band-status\n");
			return -EINVAL;
		}

		pl->cfg_link_an_mode = MLO_AN_INBAND;
	}

	if (pl->cfg_link_an_mode == MLO_AN_INBAND) {
		linkmode_zero(pl->supported);
		phylink_set(pl->supported, MII);
		phylink_set(pl->supported, Autoneg);
		phylink_set(pl->supported, Asym_Pause);
		phylink_set(pl->supported, Pause);

		switch (pl->link_config.interface) {
		case PHY_INTERFACE_MODE_SGMII:
		case PHY_INTERFACE_MODE_PSGMII:
		case PHY_INTERFACE_MODE_QSGMII:
		case PHY_INTERFACE_MODE_QUSGMII:
		case PHY_INTERFACE_MODE_RGMII:
		case PHY_INTERFACE_MODE_RGMII_ID:
		case PHY_INTERFACE_MODE_RGMII_RXID:
		case PHY_INTERFACE_MODE_RGMII_TXID:
		case PHY_INTERFACE_MODE_RTBI:
		case PHY_INTERFACE_MODE_1000BASEX:
		case PHY_INTERFACE_MODE_2500BASEX:
		case PHY_INTERFACE_MODE_5GBASER:
		case PHY_INTERFACE_MODE_25GBASER:
		case PHY_INTERFACE_MODE_USXGMII:
		case PHY_INTERFACE_MODE_10G_QXGMII:
		case PHY_INTERFACE_MODE_10GKR:
		case PHY_INTERFACE_MODE_10GBASER:
		case PHY_INTERFACE_MODE_XLGMII:
			caps = ~(MAC_SYM_PAUSE | MAC_ASYM_PAUSE);
			caps = phylink_get_capabilities(pl->link_config.interface, caps,
							RATE_MATCH_NONE);
			phylink_caps_to_linkmodes(pl->supported, caps);
			break;

		default:
			phylink_err(pl,
				    "incorrect link mode %s for in-band status\n",
				    phy_modes(pl->link_config.interface));
			return -EINVAL;
		}

		linkmode_copy(pl->link_config.advertising, pl->supported);

		if (phylink_validate(pl, pl->supported, &pl->link_config)) {
			phylink_err(pl,
				    "failed to validate link configuration for in-band status\n");
			return -EINVAL;
		}
	}

	return 0;
}

static void phylink_apply_manual_flow(struct phylink *pl,
				      struct phylink_link_state *state)
{
	/* If autoneg is disabled, pause AN is also disabled */
	if (!linkmode_test_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
			       state->advertising))
		state->pause &= ~MLO_PAUSE_AN;

	/* Manual configuration of pause modes */
	if (!(pl->link_config.pause & MLO_PAUSE_AN))
		state->pause = pl->link_config.pause;
}

static void phylink_resolve_an_pause(struct phylink_link_state *state)
{
	bool tx_pause, rx_pause;

	if (state->duplex == DUPLEX_FULL) {
		linkmode_resolve_pause(state->advertising,
				       state->lp_advertising,
				       &tx_pause, &rx_pause);
		if (tx_pause)
			state->pause |= MLO_PAUSE_TX;
		if (rx_pause)
			state->pause |= MLO_PAUSE_RX;
	}
}

static unsigned int phylink_pcs_inband_caps(struct phylink_pcs *pcs,
				    phy_interface_t interface)
{
	if (pcs && pcs->ops->pcs_inband_caps)
		return pcs->ops->pcs_inband_caps(pcs, interface);

	return 0;
}

static void phylink_pcs_pre_config(struct phylink_pcs *pcs,
				   phy_interface_t interface)
{
	if (pcs && pcs->ops->pcs_pre_config)
		pcs->ops->pcs_pre_config(pcs, interface);
}

static int phylink_pcs_post_config(struct phylink_pcs *pcs,
				   phy_interface_t interface)
{
	int err = 0;

	if (pcs && pcs->ops->pcs_post_config)
		err = pcs->ops->pcs_post_config(pcs, interface);

	return err;
}

static void phylink_pcs_disable(struct phylink_pcs *pcs)
{
	if (pcs && pcs->ops->pcs_disable)
		pcs->ops->pcs_disable(pcs);
}

static int phylink_pcs_enable(struct phylink_pcs *pcs)
{
	int err = 0;

	if (pcs && pcs->ops->pcs_enable)
		err = pcs->ops->pcs_enable(pcs);

	return err;
}

static int phylink_pcs_config(struct phylink_pcs *pcs, unsigned int neg_mode,
			      const struct phylink_link_state *state,
			      bool permit_pause_to_mac)
{
	if (!pcs)
		return 0;

	return pcs->ops->pcs_config(pcs, neg_mode, state->interface,
				    state->advertising, permit_pause_to_mac);
}

static void phylink_pcs_link_up(struct phylink_pcs *pcs, unsigned int neg_mode,
				phy_interface_t interface, int speed,
				int duplex)
{
	if (pcs && pcs->ops->pcs_link_up)
		pcs->ops->pcs_link_up(pcs, neg_mode, interface, speed, duplex);
}

/* Query inband for a specific interface mode, asking the MAC for the
 * PCS which will be used to handle the interface mode.
 */
static unsigned int phylink_inband_caps(struct phylink *pl,
					 phy_interface_t interface)
{
	struct phylink_pcs *pcs;

	if (!pl->mac_ops->mac_select_pcs)
		return 0;

	pcs = pl->mac_ops->mac_select_pcs(pl->config, interface);
	if (!pcs)
		return 0;

	return phylink_pcs_inband_caps(pcs, interface);
}

static void phylink_pcs_poll_stop(struct phylink *pl)
{
	if (pl->cfg_link_an_mode == MLO_AN_INBAND)
		del_timer(&pl->link_poll);
}

static void phylink_pcs_poll_start(struct phylink *pl)
{
	if (pl->pcs && pl->pcs->poll && pl->cfg_link_an_mode == MLO_AN_INBAND)
		mod_timer(&pl->link_poll, jiffies + HZ);
}

int phylink_pcs_pre_init(struct phylink *pl, struct phylink_pcs *pcs)
{
	int ret = 0;

	/* Signal to PCS driver that MAC requires RX clock for init */
	if (pl->config->mac_requires_rxc)
		pcs->rxc_always_on = true;

	if (pcs->ops->pcs_pre_init)
		ret = pcs->ops->pcs_pre_init(pcs);

	return ret;
}
EXPORT_SYMBOL_GPL(phylink_pcs_pre_init);

static void phylink_mac_config(struct phylink *pl,
			       const struct phylink_link_state *state)
{
	struct phylink_link_state st = *state;

	/* Stop drivers incorrectly using these */
	linkmode_zero(st.lp_advertising);
	st.speed = SPEED_UNKNOWN;
	st.duplex = DUPLEX_UNKNOWN;
	st.an_complete = false;
	st.link = false;

	phylink_dbg(pl,
		    "%s: mode=%s/%s/%s adv=%*pb pause=%02x\n",
		    __func__, phylink_an_mode_str(pl->act_link_an_mode),
		    phy_modes(st.interface),
		    phy_rate_matching_to_str(st.rate_matching),
		    __ETHTOOL_LINK_MODE_MASK_NBITS, st.advertising,
		    st.pause);

	pl->mac_ops->mac_config(pl->config, pl->act_link_an_mode, &st);
}

static void phylink_pcs_an_restart(struct phylink *pl)
{
	if (pl->pcs && linkmode_test_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
					 pl->link_config.advertising) &&
	    phy_interface_mode_is_8023z(pl->link_config.interface) &&
	    phylink_autoneg_inband(pl->act_link_an_mode))
		pl->pcs->ops->pcs_an_restart(pl->pcs);
}

/**
 * phylink_pcs_neg_mode() - helper to determine PCS inband mode
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @pcs: a pointer to &struct phylink_pcs
 * @interface: interface mode to be used
 * @advertising: adertisement ethtool link mode mask
 *
 * Determines the negotiation mode to be used by the PCS, and returns
 * one of:
 *
 * - %PHYLINK_PCS_NEG_NONE: interface mode does not support inband
 * - %PHYLINK_PCS_NEG_OUTBAND: an out of band mode (e.g. reading the PHY)
 *   will be used.
 * - %PHYLINK_PCS_NEG_INBAND_DISABLED: inband mode selected but autoneg
 *   disabled
 * - %PHYLINK_PCS_NEG_INBAND_ENABLED: inband mode selected and autoneg enabled
 *
 * Note: this is for cases where the PCS itself is involved in negotiation
 * (e.g. Clause 37, SGMII and similar) not Clause 73.
 */
static void phylink_pcs_neg_mode(struct phylink *pl, struct phylink_pcs *pcs,
				 phy_interface_t interface,
				 const unsigned long *advertising)
{
	unsigned int pcs_ib_caps = 0;
	unsigned int phy_ib_caps = 0;
	unsigned int neg_mode, mode;
	enum {
		INBAND_CISCO_SGMII,
		INBAND_BASEX,
	} type;

	mode = pl->req_link_an_mode;

	pl->phy_ib_mode = 0;

	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
	case PHY_INTERFACE_MODE_QUSGMII:
	case PHY_INTERFACE_MODE_USXGMII:
	case PHY_INTERFACE_MODE_10G_QXGMII:
		/* These protocols are designed for use with a PHY which
		 * communicates its negotiation result back to the MAC via
		 * inband communication. Note: there exist PHYs that run
		 * with SGMII but do not send the inband data.
		 */
		type = INBAND_CISCO_SGMII;
		break;

	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		/* 1000base-X is designed for use media-side for Fibre
		 * connections, and thus the Autoneg bit needs to be
		 * taken into account. We also do this for 2500base-X
		 * as well, but drivers may not support this, so may
		 * need to override this.
		 */
		type = INBAND_BASEX;
		break;

	default:
		pl->pcs_neg_mode = PHYLINK_PCS_NEG_NONE;
		pl->act_link_an_mode = mode;
		return;
	}

	if (pcs)
		pcs_ib_caps = phylink_pcs_inband_caps(pcs, interface);

	if (pl->phydev)
		phy_ib_caps = phy_inband_caps(pl->phydev, interface);

	phylink_dbg(pl, "interface %s inband modes: pcs=%02x phy=%02x\n",
		    phy_modes(interface), pcs_ib_caps, phy_ib_caps);

	if (!phylink_autoneg_inband(mode)) {
		bool pcs_ib_only = false;
		bool phy_ib_only = false;

		if (pcs_ib_caps && pcs_ib_caps != LINK_INBAND_DISABLE) {
			/* PCS supports reporting in-band capabilities, and
			 * supports more than disable mode.
			 */
			if (pcs_ib_caps & LINK_INBAND_DISABLE)
				neg_mode = PHYLINK_PCS_NEG_OUTBAND;
			else if (pcs_ib_caps & LINK_INBAND_ENABLE)
				pcs_ib_only = true;
		}

		if (phy_ib_caps && phy_ib_caps != LINK_INBAND_DISABLE) {
			/* PHY supports in-band capabilities, and supports
			 * more than disable mode.
			 */
			if (phy_ib_caps & LINK_INBAND_DISABLE)
				pl->phy_ib_mode = LINK_INBAND_DISABLE;
			else if (phy_ib_caps & LINK_INBAND_BYPASS)
				pl->phy_ib_mode = LINK_INBAND_BYPASS;
			else if (phy_ib_caps & LINK_INBAND_ENABLE)
				phy_ib_only = true;
		}

		/* If either the PCS or PHY requires inband to be enabled,
		 * this is an invalid configuration. Provide a diagnostic
		 * message for this case, but don't try to force the issue.
		 */
		if (pcs_ib_only || phy_ib_only)
			phylink_warn(pl,
				     "firmware wants %s mode, but %s%s%s requires inband\n",
				     phylink_an_mode_str(mode),
				     pcs_ib_only ? "PCS" : "",
				     pcs_ib_only && phy_ib_only ? " and " : "",
				     phy_ib_only ? "PHY" : "");

		neg_mode = PHYLINK_PCS_NEG_OUTBAND;
	} else if (type == INBAND_CISCO_SGMII || pl->phydev) {
		/* For SGMII modes which are designed to be used with PHYs, or
		 * Base-X with a PHY, we try to use in-band mode where-ever
		 * possible. However, there are some PHYs e.g. BCM84881 which
		 * do not support in-band.
		 */
		const unsigned int inband_ok = LINK_INBAND_ENABLE |
					       LINK_INBAND_BYPASS;
		const unsigned int outband_ok = LINK_INBAND_DISABLE |
						LINK_INBAND_BYPASS;
		/* PCS	PHY
		 * D E	D E
		 * 0 0  0 0	no information			inband enabled
		 * 1 0  0 0	pcs doesn't support		outband
		 * 0 1  0 0	pcs required			inband enabled
		 * 1 1  0 0	pcs optional			inband enabled
		 * 0 0  1 0	phy doesn't support		outband
		 * 1 0  1 0	pcs+phy doesn't support		outband
		 * 0 1  1 0	pcs required, phy doesn't support, invalid
		 * 1 1  1 0	pcs optional, phy doesn't support, outband
		 * 0 0  0 1	phy required			inband enabled
		 * 1 0  0 1	pcs doesn't support, phy required, invalid
		 * 0 1  0 1	pcs+phy required		inband enabled
		 * 1 1  0 1	pcs optional, phy required	inband enabled
		 * 0 0  1 1	phy optional			inband enabled
		 * 1 0  1 1	pcs doesn't support, phy optional, outband
		 * 0 1  1 1	pcs required, phy optional	inband enabled
		 * 1 1  1 1	pcs+phy optional		inband enabled
		 */
		if ((!pcs_ib_caps || pcs_ib_caps & inband_ok) &&
		    (!phy_ib_caps || phy_ib_caps & inband_ok)) {
			/* In-band supported or unknown at both ends. Enable
			 * in-band mode with or without bypass at the PHY.
			 */
			if (phy_ib_caps & LINK_INBAND_ENABLE)
				pl->phy_ib_mode = LINK_INBAND_ENABLE;
			else if (phy_ib_caps & LINK_INBAND_BYPASS)
				pl->phy_ib_mode = LINK_INBAND_BYPASS;

			neg_mode = PHYLINK_PCS_NEG_INBAND_ENABLED;
		} else if ((!pcs_ib_caps || pcs_ib_caps & outband_ok) &&
			   (!phy_ib_caps || phy_ib_caps & outband_ok)) {
			/* Either in-band not supported at at least one end.
			 * In-band bypass at the other end is possible.
			 */
			if (phy_ib_caps & LINK_INBAND_DISABLE)
				pl->phy_ib_mode = LINK_INBAND_DISABLE;
			else if (phy_ib_caps & LINK_INBAND_BYPASS)
				pl->phy_ib_mode = LINK_INBAND_BYPASS;

			neg_mode = PHYLINK_PCS_NEG_OUTBAND;
			if (pl->phydev)
				mode = MLO_AN_PHY;
		} else {
			/* invalid */
			phylink_warn(pl, "%s: incompatible in-band capabilities, trying in-band",
				     phy_modes(interface));
			neg_mode = PHYLINK_PCS_NEG_INBAND_ENABLED;
		}
	} else {
		/* For Base-X without a PHY */
		if (pcs_ib_caps == LINK_INBAND_DISABLE)
			/* If the PCS doesn't support inband, then inband must
			 * be disabled.
			 */
			neg_mode = PHYLINK_PCS_NEG_INBAND_DISABLED;
		else if (pcs_ib_caps == LINK_INBAND_ENABLE)
			/* If the PCS requires inband, then inband must always
			 * be enabled.
			 */
			neg_mode = PHYLINK_PCS_NEG_INBAND_ENABLED;
		else if (linkmode_test_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
					   advertising))
			neg_mode = PHYLINK_PCS_NEG_INBAND_ENABLED;
		else
			neg_mode = PHYLINK_PCS_NEG_INBAND_DISABLED;
	}

	pl->pcs_neg_mode = neg_mode;
	pl->act_link_an_mode = mode;
}

static void phylink_major_config(struct phylink *pl, bool restart,
				  const struct phylink_link_state *state)
{
	struct phylink_pcs *pcs = NULL;
	bool pcs_changed = false;
	unsigned int rate_kbd;
	unsigned int neg_mode;
	int err;

	phylink_dbg(pl, "major config, requested %s/%s\n",
		    phylink_an_mode_str(pl->req_link_an_mode),
		    phy_modes(state->interface));

	if (pl->mac_ops->mac_select_pcs) {
		pcs = pl->mac_ops->mac_select_pcs(pl->config, state->interface);
		if (IS_ERR(pcs)) {
			phylink_err(pl,
				    "mac_select_pcs unexpectedly failed: %pe\n",
				    pcs);
			return;
		}

		pcs_changed = pl->pcs != pcs;
	}

	phylink_pcs_neg_mode(pl, pcs, state->interface, state->advertising);

	phylink_dbg(pl, "major config, active %s/%s/%s\n",
		    phylink_an_mode_str(pl->act_link_an_mode),
		    phylink_pcs_mode_str(pl->pcs_neg_mode),
		    phy_modes(state->interface));

	phylink_pcs_poll_stop(pl);

	if (pl->mac_ops->mac_prepare) {
		err = pl->mac_ops->mac_prepare(pl->config, pl->act_link_an_mode,
					       state->interface);
		if (err < 0) {
			phylink_err(pl, "mac_prepare failed: %pe\n",
				    ERR_PTR(err));
			return;
		}
	}

	/* If we have a new PCS, switch to the new PCS after preparing the MAC
	 * for the change.
	 */
	if (pcs_changed) {
		phylink_pcs_disable(pl->pcs);

		if (pl->pcs)
			pl->pcs->phylink = NULL;

		pcs->phylink = pl;

		pl->pcs = pcs;
	}

	if (pl->pcs)
		phylink_pcs_pre_config(pl->pcs, state->interface);

	phylink_mac_config(pl, state);

	if (pl->pcs)
		phylink_pcs_post_config(pl->pcs, state->interface);

	if (pl->pcs_state == PCS_STATE_STARTING || pcs_changed)
		phylink_pcs_enable(pl->pcs);

	neg_mode = pl->act_link_an_mode;
	if (pl->pcs && pl->pcs->neg_mode)
		neg_mode = pl->pcs_neg_mode;

	err = phylink_pcs_config(pl->pcs, neg_mode, state,
				 !!(pl->link_config.pause & MLO_PAUSE_AN));
	if (err < 0)
		phylink_err(pl, "pcs_config failed: %pe\n",
			    ERR_PTR(err));
	else if (err > 0)
		restart = true;

	if (restart)
		phylink_pcs_an_restart(pl);

	if (pl->mac_ops->mac_finish) {
		err = pl->mac_ops->mac_finish(pl->config, pl->act_link_an_mode,
					      state->interface);
		if (err < 0)
			phylink_err(pl, "mac_finish failed: %pe\n",
				    ERR_PTR(err));
	}

	if (pl->phydev && pl->phy_ib_mode) {
		err = phy_config_inband(pl->phydev, pl->phy_ib_mode);
		if (err < 0)
			phylink_err(pl, "phy_config_inband: %pe\n",
				    ERR_PTR(err));
	}

	if (pl->sfp_bus) {
		rate_kbd = phylink_interface_signal_rate(state->interface);
		if (rate_kbd)
			sfp_upstream_set_signal_rate(pl->sfp_bus, rate_kbd);
	}

	phylink_pcs_poll_start(pl);
}

/*
 * Reconfigure for a change of inband advertisement.
 * If we have a separate PCS, we only need to call its pcs_config() method,
 * and then restart AN if it indicates something changed. Otherwise, we do
 * the full MAC reconfiguration.
 */
static int phylink_change_inband_advert(struct phylink *pl)
{
	unsigned int neg_mode;
	int ret;

	if (test_bit(PHYLINK_DISABLE_STOPPED, &pl->phylink_disable_state))
		return 0;

	phylink_dbg(pl, "%s: mode=%s/%s adv=%*pb pause=%02x\n", __func__,
		    phylink_an_mode_str(pl->req_link_an_mode),
		    phy_modes(pl->link_config.interface),
		    __ETHTOOL_LINK_MODE_MASK_NBITS, pl->link_config.advertising,
		    pl->link_config.pause);

	/* Recompute the PCS neg mode */
	phylink_pcs_neg_mode(pl, pl->pcs, pl->link_config.interface,
			     pl->link_config.advertising);

	neg_mode = pl->act_link_an_mode;
	if (pl->pcs->neg_mode)
		neg_mode = pl->pcs_neg_mode;

	/* Modern PCS-based method; update the advert at the PCS, and
	 * restart negotiation if the pcs_config() helper indicates that
	 * the programmed advertisement has changed.
	 */
	ret = phylink_pcs_config(pl->pcs, neg_mode, &pl->link_config,
				 !!(pl->link_config.pause & MLO_PAUSE_AN));
	if (ret < 0)
		return ret;

	if (ret > 0)
		phylink_pcs_an_restart(pl);

	return 0;
}

static void phylink_mac_pcs_get_state(struct phylink *pl,
				      struct phylink_link_state *state)
{
	linkmode_copy(state->advertising, pl->link_config.advertising);
	linkmode_zero(state->lp_advertising);
	state->interface = pl->link_config.interface;
	state->rate_matching = pl->link_config.rate_matching;
	if (linkmode_test_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
			      state->advertising)) {
		state->speed = SPEED_UNKNOWN;
		state->duplex = DUPLEX_UNKNOWN;
		state->pause = MLO_PAUSE_NONE;
	} else {
		state->speed =  pl->link_config.speed;
		state->duplex = pl->link_config.duplex;
		state->pause = pl->link_config.pause;
	}
	state->an_complete = 0;
	state->link = 1;

	if (pl->pcs)
		pl->pcs->ops->pcs_get_state(pl->pcs, state);
	else
		state->link = 0;
}

/* The fixed state is... fixed except for the link state,
 * which may be determined by a GPIO or a callback.
 */
static void phylink_get_fixed_state(struct phylink *pl,
				    struct phylink_link_state *state)
{
	*state = pl->link_config;
	if (pl->config->get_fixed_state)
		pl->config->get_fixed_state(pl->config, state);
	else if (pl->link_gpio)
		state->link = !!gpiod_get_value_cansleep(pl->link_gpio);

	state->pause = MLO_PAUSE_NONE;
	phylink_resolve_an_pause(state);
}

static void phylink_mac_initial_config(struct phylink *pl, bool force_restart)
{
	struct phylink_link_state link_state;

	switch (pl->req_link_an_mode) {
	case MLO_AN_PHY:
		link_state = pl->phy_state;
		break;

	case MLO_AN_FIXED:
		phylink_get_fixed_state(pl, &link_state);
		break;

	case MLO_AN_INBAND:
		link_state = pl->link_config;
		if (link_state.interface == PHY_INTERFACE_MODE_SGMII)
			link_state.pause = MLO_PAUSE_NONE;
		break;

	default: /* can't happen */
		return;
	}

	link_state.link = false;

	phylink_apply_manual_flow(pl, &link_state);
	phylink_major_config(pl, force_restart, &link_state);
}

static const char *phylink_pause_to_str(int pause)
{
	switch (pause & MLO_PAUSE_TXRX_MASK) {
	case MLO_PAUSE_TX | MLO_PAUSE_RX:
		return "rx/tx";
	case MLO_PAUSE_TX:
		return "tx";
	case MLO_PAUSE_RX:
		return "rx";
	default:
		return "off";
	}
}

static void phylink_link_up(struct phylink *pl,
			    struct phylink_link_state link_state)
{
	struct net_device *ndev = pl->netdev;
	unsigned int neg_mode;
	int speed, duplex;
	bool rx_pause;

	speed = link_state.speed;
	duplex = link_state.duplex;
	rx_pause = !!(link_state.pause & MLO_PAUSE_RX);

	switch (link_state.rate_matching) {
	case RATE_MATCH_PAUSE:
		/* The PHY is doing rate matchion from the media rate (in
		 * the link_state) to the interface speed, and will send
		 * pause frames to the MAC to limit its transmission speed.
		 */
		speed = phylink_interface_max_speed(link_state.interface);
		duplex = DUPLEX_FULL;
		rx_pause = true;
		break;

	case RATE_MATCH_CRS:
		/* The PHY is doing rate matchion from the media rate (in
		 * the link_state) to the interface speed, and will cause
		 * collisions to the MAC to limit its transmission speed.
		 */
		speed = phylink_interface_max_speed(link_state.interface);
		duplex = DUPLEX_HALF;
		break;
	}

	pl->cur_interface = link_state.interface;

	neg_mode = pl->act_link_an_mode;
	if (pl->pcs && pl->pcs->neg_mode)
		neg_mode = pl->pcs_neg_mode;

	phylink_pcs_link_up(pl->pcs, neg_mode, pl->cur_interface, speed,
			    duplex);

	pl->mac_ops->mac_link_up(pl->config, pl->phydev, pl->act_link_an_mode,
				 pl->cur_interface, speed, duplex,
				 !!(link_state.pause & MLO_PAUSE_TX), rx_pause);

	if (ndev)
		netif_carrier_on(ndev);

	phylink_info(pl,
		     "Link is Up - %s/%s - flow control %s\n",
		     phy_speed_to_str(link_state.speed),
		     phy_duplex_to_str(link_state.duplex),
		     phylink_pause_to_str(link_state.pause));
}

static void phylink_link_down(struct phylink *pl)
{
	struct net_device *ndev = pl->netdev;

	if (ndev)
		netif_carrier_off(ndev);
	pl->mac_ops->mac_link_down(pl->config, pl->act_link_an_mode,
				   pl->cur_interface);
	phylink_info(pl, "Link is Down\n");
}

static void phylink_resolve(struct work_struct *w)
{
	struct phylink *pl = container_of(w, struct phylink, resolve);
	struct phylink_link_state link_state;
	struct net_device *ndev = pl->netdev;
	bool mac_config = false;
	bool retrigger = false;
	bool cur_link_state;

	mutex_lock(&pl->state_mutex);
	if (pl->netdev)
		cur_link_state = netif_carrier_ok(ndev);
	else
		cur_link_state = pl->old_link_state;

	if (pl->phylink_disable_state) {
		pl->link_failed = false;
		link_state.link = false;
	} else if (pl->link_failed) {
		link_state.link = false;
		retrigger = true;
	} else if (pl->act_link_an_mode == MLO_AN_FIXED) {
		phylink_get_fixed_state(pl, &link_state);
		mac_config = link_state.link;
	} else if (pl->act_link_an_mode == MLO_AN_PHY) {
		link_state = pl->phy_state;
		mac_config = link_state.link;
	} else {
		phylink_mac_pcs_get_state(pl, &link_state);

		/* The PCS may have a latching link-fail indicator. If the link
		 * was up, bring the link down and re-trigger the resolve.
		 * Otherwise, re-read the PCS state to get the current status
		 * of the link.
		 */
		if (!link_state.link) {
			if (cur_link_state)
				retrigger = true;
			else
				phylink_mac_pcs_get_state(pl, &link_state);
		}

		/* If we have a phy, the "up" state is the union of both the
		 * PHY and the MAC
		 */
		if (pl->phydev)
			link_state.link &= pl->phy_state.link;

		/* Only update if the PHY link is up */
		if (pl->phydev && pl->phy_state.link) {
			/* If the interface has changed, force a link down
			 * event if the link isn't already down, and re-resolve.
			 */
			if (link_state.interface != pl->phy_state.interface) {
				retrigger = true;
				link_state.link = false;
			}

			link_state.interface = pl->phy_state.interface;

			/* If we are doing rate matching, then the link
			 * speed/duplex comes from the PHY
			 */
			if (pl->phy_state.rate_matching) {
				link_state.rate_matching =
					pl->phy_state.rate_matching;
				link_state.speed = pl->phy_state.speed;
				link_state.duplex = pl->phy_state.duplex;
			}

			/* If we have a PHY, we need to update with the PHY
			 * flow control bits.
			 */
			link_state.pause = pl->phy_state.pause;
			mac_config = true;
		}
	}

	if (pl->act_link_an_mode != MLO_AN_FIXED)
		phylink_apply_manual_flow(pl, &link_state);

	if (mac_config) {
		if (link_state.interface != pl->link_config.interface) {
			/* The interface has changed, force the link down and
			 * then reconfigure.
			 */
			if (cur_link_state) {
				phylink_link_down(pl);
				cur_link_state = false;
			}
			phylink_major_config(pl, false, &link_state);
			pl->link_config.interface = link_state.interface;
		}
	}

	if (link_state.link != cur_link_state) {
		pl->old_link_state = link_state.link;
		if (!link_state.link)
			phylink_link_down(pl);
		else
			phylink_link_up(pl, link_state);
	}
	if (!link_state.link && retrigger) {
		pl->link_failed = false;
		queue_work(system_power_efficient_wq, &pl->resolve);
	}
	mutex_unlock(&pl->state_mutex);
}

static void phylink_run_resolve(struct phylink *pl)
{
	if (!pl->phylink_disable_state)
		queue_work(system_power_efficient_wq, &pl->resolve);
}

static void phylink_run_resolve_and_disable(struct phylink *pl, int bit)
{
	unsigned long state = pl->phylink_disable_state;

	set_bit(bit, &pl->phylink_disable_state);
	if (state == 0) {
		queue_work(system_power_efficient_wq, &pl->resolve);
		flush_work(&pl->resolve);
	}
}

static void phylink_enable_and_run_resolve(struct phylink *pl, int bit)
{
	clear_bit(bit, &pl->phylink_disable_state);
	phylink_run_resolve(pl);
}

static void phylink_fixed_poll(struct timer_list *t)
{
	struct phylink *pl = container_of(t, struct phylink, link_poll);

	mod_timer(t, jiffies + HZ);

	phylink_run_resolve(pl);
}

static const struct sfp_upstream_ops sfp_phylink_ops;

static int phylink_register_sfp(struct phylink *pl,
				const struct fwnode_handle *fwnode)
{
	struct sfp_bus *bus;
	int ret;

	if (!fwnode)
		return 0;

	bus = sfp_bus_find_fwnode(fwnode);
	if (IS_ERR(bus)) {
		phylink_err(pl, "unable to attach SFP bus: %pe\n", bus);
		return PTR_ERR(bus);
	}

	pl->sfp_bus = bus;

	ret = sfp_bus_add_upstream(bus, pl, &sfp_phylink_ops);
	sfp_bus_put(bus);

	return ret;
}

/**
 * phylink_set_fixed_link() - set the fixed link
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @state: a pointer to a struct phylink_link_state.
 *
 * This function is used when the link parameters are known and do not change,
 * making it suitable for certain types of network connections.
 *
 * Returns: zero on success or negative error code.
 */
int phylink_set_fixed_link(struct phylink *pl,
			   const struct phylink_link_state *state)
{
	const struct phy_setting *s;
	unsigned long *adv;

	if (pl->cfg_link_an_mode != MLO_AN_PHY || !state ||
	    !test_bit(PHYLINK_DISABLE_STOPPED, &pl->phylink_disable_state))
		return -EINVAL;

	s = phy_lookup_setting(state->speed, state->duplex,
			       pl->supported, true);
	if (!s)
		return -EINVAL;

	adv = pl->link_config.advertising;
	linkmode_zero(adv);
	linkmode_set_bit(s->bit, adv);
	linkmode_set_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, adv);

	pl->link_config.speed = state->speed;
	pl->link_config.duplex = state->duplex;
	pl->link_config.link = 1;
	pl->link_config.an_complete = 1;

	pl->cfg_link_an_mode = MLO_AN_FIXED;
	pl->req_link_an_mode = pl->cfg_link_an_mode;

	return 0;
}
EXPORT_SYMBOL_GPL(phylink_set_fixed_link);

/**
 * phylink_create() - create a phylink instance
 * @config: a pointer to the target &struct phylink_config
 * @fwnode: a pointer to a &struct fwnode_handle describing the network
 *	interface
 * @iface: the desired link mode defined by &typedef phy_interface_t
 * @mac_ops: a pointer to a &struct phylink_mac_ops for the MAC.
 *
 * Create a new phylink instance, and parse the link parameters found in @np.
 * This will parse in-band modes, fixed-link or SFP configuration.
 *
 * Note: the rtnl lock must not be held when calling this function.
 *
 * Returns a pointer to a &struct phylink, or an error-pointer value. Users
 * must use IS_ERR() to check for errors from this function.
 */
struct phylink *phylink_create(struct phylink_config *config,
			       const struct fwnode_handle *fwnode,
			       phy_interface_t iface,
			       const struct phylink_mac_ops *mac_ops)
{
	struct phylink *pl;
	int ret;

	/* Validate the supplied configuration */
	if (phy_interface_empty(config->supported_interfaces)) {
		dev_err(config->dev,
			"phylink: error: empty supported_interfaces\n");
		return ERR_PTR(-EINVAL);
	}

	pl = kzalloc(sizeof(*pl), GFP_KERNEL);
	if (!pl)
		return ERR_PTR(-ENOMEM);

	mutex_init(&pl->state_mutex);
	INIT_WORK(&pl->resolve, phylink_resolve);

	pl->config = config;
	if (config->type == PHYLINK_NETDEV) {
		pl->netdev = to_net_dev(config->dev);
		netif_carrier_off(pl->netdev);
	} else if (config->type == PHYLINK_DEV) {
		pl->dev = config->dev;
	} else {
		kfree(pl);
		return ERR_PTR(-EINVAL);
	}

	pl->phy_state.interface = iface;
	pl->link_interface = iface;
	if (iface == PHY_INTERFACE_MODE_MOCA)
		pl->link_port = PORT_BNC;
	else
		pl->link_port = PORT_MII;
	pl->link_config.interface = iface;
	pl->link_config.pause = MLO_PAUSE_AN;
	pl->link_config.speed = SPEED_UNKNOWN;
	pl->link_config.duplex = DUPLEX_UNKNOWN;
	pl->pcs_state = PCS_STATE_DOWN;
	pl->mac_ops = mac_ops;
	__set_bit(PHYLINK_DISABLE_STOPPED, &pl->phylink_disable_state);
	timer_setup(&pl->link_poll, phylink_fixed_poll, 0);

	linkmode_fill(pl->supported);
	linkmode_copy(pl->link_config.advertising, pl->supported);
	phylink_validate(pl, pl->supported, &pl->link_config);

	ret = phylink_parse_mode(pl, fwnode);
	if (ret < 0) {
		kfree(pl);
		return ERR_PTR(ret);
	}

	if (pl->cfg_link_an_mode == MLO_AN_FIXED) {
		ret = phylink_parse_fixedlink(pl, fwnode);
		if (ret < 0) {
			kfree(pl);
			return ERR_PTR(ret);
		}
	}

	pl->req_link_an_mode = pl->cfg_link_an_mode;

	ret = phylink_register_sfp(pl, fwnode);
	if (ret < 0) {
		kfree(pl);
		return ERR_PTR(ret);
	}

	return pl;
}
EXPORT_SYMBOL_GPL(phylink_create);

/**
 * phylink_destroy() - cleanup and destroy the phylink instance
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 *
 * Destroy a phylink instance. Any PHY that has been attached must have been
 * cleaned up via phylink_disconnect_phy() prior to calling this function.
 *
 * Note: the rtnl lock must not be held when calling this function.
 */
void phylink_destroy(struct phylink *pl)
{
	sfp_bus_del_upstream(pl->sfp_bus);
	if (pl->link_gpio)
		gpiod_put(pl->link_gpio);

	cancel_work_sync(&pl->resolve);
	kfree(pl);
}
EXPORT_SYMBOL_GPL(phylink_destroy);

/**
 * phylink_expects_phy() - Determine if phylink expects a phy to be attached
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 *
 * When using fixed-link mode, or in-band mode with 1000base-X or 2500base-X,
 * no PHY is needed.
 *
 * Returns true if phylink will be expecting a PHY.
 */
bool phylink_expects_phy(struct phylink *pl)
{
	if (pl->cfg_link_an_mode == MLO_AN_FIXED ||
	    (pl->cfg_link_an_mode == MLO_AN_INBAND &&
	     phy_interface_mode_is_8023z(pl->link_config.interface)))
		return false;
	return true;
}
EXPORT_SYMBOL_GPL(phylink_expects_phy);

static void phylink_phy_change(struct phy_device *phydev, bool up)
{
	struct phylink *pl = phydev->phylink;
	bool tx_pause, rx_pause;

	phy_get_pause(phydev, &tx_pause, &rx_pause);

	mutex_lock(&pl->state_mutex);
	pl->phy_state.speed = phydev->speed;
	pl->phy_state.duplex = phydev->duplex;
	pl->phy_state.rate_matching = phydev->rate_matching;
	pl->phy_state.pause = MLO_PAUSE_NONE;
	if (tx_pause)
		pl->phy_state.pause |= MLO_PAUSE_TX;
	if (rx_pause)
		pl->phy_state.pause |= MLO_PAUSE_RX;
	pl->phy_state.interface = phydev->interface;
	pl->phy_state.link = up;
	if (!up)
		pl->link_failed = true;
	mutex_unlock(&pl->state_mutex);

	phylink_run_resolve(pl);

	phylink_dbg(pl, "phy link %s %s/%s/%s/%s/%s\n", up ? "up" : "down",
		    phy_modes(phydev->interface),
		    phy_speed_to_str(phydev->speed),
		    phy_duplex_to_str(phydev->duplex),
		    phy_rate_matching_to_str(phydev->rate_matching),
		    phylink_pause_to_str(pl->phy_state.pause));
}

static int phylink_validate_phy(struct phylink *pl, struct phy_device *phy,
				unsigned long *supported,
				struct phylink_link_state *state)
{
	DECLARE_PHY_INTERFACE_MASK(interfaces);

	/* If the PHY provides a bitmap of the interfaces it will be using
	 * depending on the negotiated media speeds, use this to validate
	 * which ethtool link modes can be used.
	 */
	if (!phy_interface_empty(phy->possible_interfaces)) {
		/* We only care about the union of the PHY's interfaces and
		 * those which the host supports.
		 */
		phy_interface_and(interfaces, phy->possible_interfaces,
				  pl->config->supported_interfaces);

		if (phy_interface_empty(interfaces)) {
			phylink_err(pl, "PHY has no common interfaces\n");
			return -EINVAL;
		}

		if (phy_on_sfp(phy)) {
			/* If the PHY is on a SFP, limit the interfaces to
			 * those that can be used with a SFP module.
			 */
			phy_interface_and(interfaces, interfaces,
					  phylink_sfp_interfaces);

			if (phy_interface_empty(interfaces)) {
				phylink_err(pl, "SFP PHY's possible interfaces becomes empty\n");
				return -EINVAL;
			}
		}

		phylink_dbg(pl, "PHY %s uses interfaces %*pbl, validating %*pbl\n",
			    phydev_name(phy),
			    (int)PHY_INTERFACE_MODE_MAX,
			    phy->possible_interfaces,
			    (int)PHY_INTERFACE_MODE_MAX, interfaces);

		return phylink_validate_mask(pl, phy, supported, state,
					     interfaces);
	}

	phylink_dbg(pl, "PHY %s doesn't supply possible interfaces\n",
		    phydev_name(phy));

	/* Check whether we would use rate matching for the proposed interface
	 * mode.
	 */
	state->rate_matching = phy_get_rate_matching(phy, state->interface);

	/* Clause 45 PHYs may switch their Serdes lane between, e.g. 10GBASE-R,
	 * 5GBASE-R, 2500BASE-X and SGMII if they are not using rate matching.
	 * For some interface modes (e.g. RXAUI, XAUI and USXGMII) switching
	 * their Serdes is either unnecessary or not reasonable.
	 *
	 * For these which switch interface modes, we really need to know which
	 * interface modes the PHY supports to properly work out which ethtool
	 * linkmodes can be supported. For now, as a work-around, we validate
	 * against all interface modes, which may lead to more ethtool link
	 * modes being advertised than are actually supported.
	 */
	if (phy->is_c45 && state->rate_matching == RATE_MATCH_NONE &&
	    state->interface != PHY_INTERFACE_MODE_RXAUI &&
	    state->interface != PHY_INTERFACE_MODE_XAUI &&
	    state->interface != PHY_INTERFACE_MODE_USXGMII)
		state->interface = PHY_INTERFACE_MODE_NA;

	return phylink_validate(pl, supported, state);
}

static int phylink_bringup_phy(struct phylink *pl, struct phy_device *phy,
			       phy_interface_t interface)
{
	struct phylink_link_state config;
	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported);
	char *irq_str;
	int ret;

	/*
	 * This is the new way of dealing with flow control for PHYs,
	 * as described by Timur Tabi in commit 529ed1275263 ("net: phy:
	 * phy drivers should not set SUPPORTED_[Asym_]Pause") except
	 * using our validate call to the MAC, we rely upon the MAC
	 * clearing the bits from both supported and advertising fields.
	 */
	phy_support_asym_pause(phy);

	memset(&config, 0, sizeof(config));
	linkmode_copy(supported, phy->supported);
	linkmode_copy(config.advertising, phy->advertising);
	config.interface = interface;

	ret = phylink_validate_phy(pl, phy, supported, &config);
	if (ret) {
		phylink_warn(pl, "validation of %s with support %*pb and advertisement %*pb failed: %pe\n",
			     phy_modes(config.interface),
			     __ETHTOOL_LINK_MODE_MASK_NBITS, phy->supported,
			     __ETHTOOL_LINK_MODE_MASK_NBITS, config.advertising,
			     ERR_PTR(ret));
		return ret;
	}

	phy->phylink = pl;
	phy->phy_link_change = phylink_phy_change;

	irq_str = phy_attached_info_irq(phy);
	phylink_info(pl,
		     "PHY [%s] driver [%s] (irq=%s)\n",
		     dev_name(&phy->mdio.dev), phy->drv->name, irq_str);
	kfree(irq_str);

	mutex_lock(&phy->lock);
	mutex_lock(&pl->state_mutex);
	pl->phydev = phy;
	pl->phy_state.interface = interface;
	pl->phy_state.pause = MLO_PAUSE_NONE;
	pl->phy_state.speed = SPEED_UNKNOWN;
	pl->phy_state.duplex = DUPLEX_UNKNOWN;
	pl->phy_state.rate_matching = RATE_MATCH_NONE;
	linkmode_copy(pl->supported, supported);
	linkmode_copy(pl->link_config.advertising, config.advertising);

	/* Restrict the phy advertisement according to the MAC support. */
	linkmode_copy(phy->advertising, config.advertising);
	mutex_unlock(&pl->state_mutex);
	mutex_unlock(&phy->lock);

	phylink_dbg(pl,
		    "phy: %s setting supported %*pb advertising %*pb\n",
		    phy_modes(interface),
		    __ETHTOOL_LINK_MODE_MASK_NBITS, pl->supported,
		    __ETHTOOL_LINK_MODE_MASK_NBITS, phy->advertising);

	if (phy_interrupt_is_valid(phy))
		phy_request_interrupt(phy);

	if (pl->config->mac_managed_pm)
		phy->mac_managed_pm = true;

	return 0;
}

static int phylink_attach_phy(struct phylink *pl, struct phy_device *phy,
			      phy_interface_t interface)
{
	u32 flags = 0;

	if (WARN_ON(pl->cfg_link_an_mode == MLO_AN_FIXED ||
		    (pl->cfg_link_an_mode == MLO_AN_INBAND &&
		     phy_interface_mode_is_8023z(interface) && !pl->sfp_bus)))
		return -EINVAL;

	if (pl->phydev)
		return -EBUSY;

	if (pl->config->mac_requires_rxc)
		flags |= PHY_F_RXC_ALWAYS_ON;

	return phy_attach_direct(pl->netdev, phy, flags, interface);
}

/**
 * phylink_connect_phy() - connect a PHY to the phylink instance
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @phy: a pointer to a &struct phy_device.
 *
 * Connect @phy to the phylink instance specified by @pl by calling
 * phy_attach_direct(). Configure the @phy according to the MAC driver's
 * capabilities, start the PHYLIB state machine and enable any interrupts
 * that the PHY supports.
 *
 * This updates the phylink's ethtool supported and advertising link mode
 * masks.
 *
 * Returns 0 on success or a negative errno.
 */
int phylink_connect_phy(struct phylink *pl, struct phy_device *phy)
{
	int ret;

	/* Use PHY device/driver interface */
	if (pl->link_interface == PHY_INTERFACE_MODE_NA) {
		pl->link_interface = phy->interface;
		pl->link_config.interface = pl->link_interface;
	}

	ret = phylink_attach_phy(pl, phy, pl->link_interface);
	if (ret < 0)
		return ret;

	ret = phylink_bringup_phy(pl, phy, pl->link_config.interface);
	if (ret)
		phy_detach(phy);

	return ret;
}
EXPORT_SYMBOL_GPL(phylink_connect_phy);

/**
 * phylink_of_phy_connect() - connect the PHY specified in the DT mode.
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @dn: a pointer to a &struct device_node.
 * @flags: PHY-specific flags to communicate to the PHY device driver
 *
 * Connect the phy specified in the device node @dn to the phylink instance
 * specified by @pl. Actions specified in phylink_connect_phy() will be
 * performed.
 *
 * Returns 0 on success or a negative errno.
 */
int phylink_of_phy_connect(struct phylink *pl, struct device_node *dn,
			   u32 flags)
{
	return phylink_fwnode_phy_connect(pl, of_fwnode_handle(dn), flags);
}
EXPORT_SYMBOL_GPL(phylink_of_phy_connect);

/**
 * phylink_fwnode_phy_connect() - connect the PHY specified in the fwnode.
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @fwnode: a pointer to a &struct fwnode_handle.
 * @flags: PHY-specific flags to communicate to the PHY device driver
 *
 * Connect the phy specified @fwnode to the phylink instance specified
 * by @pl.
 *
 * Returns 0 on success or a negative errno.
 */
int phylink_fwnode_phy_connect(struct phylink *pl,
			       const struct fwnode_handle *fwnode,
			       u32 flags)
{
	struct fwnode_handle *phy_fwnode;
	struct phy_device *phy_dev;
	int ret;

	/* Fixed links and 802.3z are handled without needing a PHY */
	if (pl->cfg_link_an_mode == MLO_AN_FIXED ||
	    (pl->cfg_link_an_mode == MLO_AN_INBAND &&
	     phy_interface_mode_is_8023z(pl->link_interface)))
		return 0;

	phy_fwnode = fwnode_get_phy_node(fwnode);
	if (IS_ERR(phy_fwnode)) {
		if (pl->cfg_link_an_mode == MLO_AN_PHY)
			return -ENODEV;
		return 0;
	}

	phy_dev = fwnode_phy_find_device(phy_fwnode);
	/* We're done with the phy_node handle */
	fwnode_handle_put(phy_fwnode);
	if (!phy_dev)
		return -ENODEV;

	/* Use PHY device/driver interface */
	if (pl->link_interface == PHY_INTERFACE_MODE_NA) {
		pl->link_interface = phy_dev->interface;
		pl->link_config.interface = pl->link_interface;
	}

	if (pl->config->mac_requires_rxc)
		flags |= PHY_F_RXC_ALWAYS_ON;

	ret = phy_attach_direct(pl->netdev, phy_dev, flags,
				pl->link_interface);
	phy_device_free(phy_dev);
	if (ret)
		return ret;

	ret = phylink_bringup_phy(pl, phy_dev, pl->link_config.interface);
	if (ret)
		phy_detach(phy_dev);

	return ret;
}
EXPORT_SYMBOL_GPL(phylink_fwnode_phy_connect);

/**
 * phylink_disconnect_phy() - disconnect any PHY attached to the phylink
 *   instance.
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 *
 * Disconnect any current PHY from the phylink instance described by @pl.
 */
void phylink_disconnect_phy(struct phylink *pl)
{
	struct phy_device *phy;

	ASSERT_RTNL();

	phy = pl->phydev;
	if (phy) {
		mutex_lock(&phy->lock);
		mutex_lock(&pl->state_mutex);
		pl->phydev = NULL;
		mutex_unlock(&pl->state_mutex);
		mutex_unlock(&phy->lock);
		flush_work(&pl->resolve);

		phy_disconnect(phy);
	}
}
EXPORT_SYMBOL_GPL(phylink_disconnect_phy);

static void phylink_link_changed(struct phylink *pl, bool up, const char *what)
{
	if (!up)
		pl->link_failed = true;
	phylink_run_resolve(pl);
	phylink_dbg(pl, "%s link %s\n", what, up ? "up" : "down");
}

/**
 * phylink_mac_change() - notify phylink of a change in MAC state
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @up: indicates whether the link is currently up.
 *
 * The MAC driver should call this driver when the state of its link
 * changes (eg, link failure, new negotiation results, etc.)
 */
void phylink_mac_change(struct phylink *pl, bool up)
{
	phylink_link_changed(pl, up, "mac");
}
EXPORT_SYMBOL_GPL(phylink_mac_change);

/**
 * phylink_pcs_change() - notify phylink of a change to PCS link state
 * @pcs: pointer to &struct phylink_pcs
 * @up: indicates whether the link is currently up.
 *
 * The PCS driver should call this when the state of its link changes
 * (e.g. link failure, new negotiation results, etc.) Note: it should
 * not determine "up" by reading the BMSR. If in doubt about the link
 * state at interrupt time, then pass true if pcs_get_state() returns
 * the latched link-down state, otherwise pass false.
 */
void phylink_pcs_change(struct phylink_pcs *pcs, bool up)
{
	struct phylink *pl = pcs->phylink;

	if (pl)
		phylink_link_changed(pl, up, "pcs");
}
EXPORT_SYMBOL_GPL(phylink_pcs_change);

static irqreturn_t phylink_link_handler(int irq, void *data)
{
	struct phylink *pl = data;

	phylink_run_resolve(pl);

	return IRQ_HANDLED;
}

/**
 * phylink_start() - start a phylink instance
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 *
 * Start the phylink instance specified by @pl, configuring the MAC for the
 * desired link mode(s) and negotiation style. This should be called from the
 * network device driver's &struct net_device_ops ndo_open() method.
 */
void phylink_start(struct phylink *pl)
{
	bool poll = false;

	ASSERT_RTNL();

	phylink_info(pl, "configuring for %s/%s link mode\n",
		     phylink_an_mode_str(pl->req_link_an_mode),
		     phy_modes(pl->link_config.interface));

	/* Always set the carrier off */
	if (pl->netdev)
		netif_carrier_off(pl->netdev);

	pl->pcs_state = PCS_STATE_STARTING;

	/* Apply the link configuration to the MAC when starting. This allows
	 * a fixed-link to start with the correct parameters, and also
	 * ensures that we set the appropriate advertisement for Serdes links.
	 *
	 * Restart autonegotiation if using 802.3z to ensure that the link
	 * parameters are properly negotiated.  This is necessary for DSA
	 * switches using 802.3z negotiation to ensure they see our modes.
	 */
	phylink_mac_initial_config(pl, true);

	pl->pcs_state = PCS_STATE_STARTED;

	phylink_enable_and_run_resolve(pl, PHYLINK_DISABLE_STOPPED);

	if (pl->cfg_link_an_mode == MLO_AN_FIXED && pl->link_gpio) {
		int irq = gpiod_to_irq(pl->link_gpio);

		if (irq > 0) {
			if (!request_irq(irq, phylink_link_handler,
					 IRQF_TRIGGER_RISING |
					 IRQF_TRIGGER_FALLING,
					 "netdev link", pl))
				pl->link_irq = irq;
			else
				irq = 0;
		}
		if (irq <= 0)
			poll = true;
	}

	if (pl->cfg_link_an_mode == MLO_AN_FIXED)
		poll |= pl->config->poll_fixed_state;

	if (poll)
		mod_timer(&pl->link_poll, jiffies + HZ);
	if (pl->phydev)
		phy_start(pl->phydev);
	if (pl->sfp_bus)
		sfp_upstream_start(pl->sfp_bus);
}
EXPORT_SYMBOL_GPL(phylink_start);

/**
 * phylink_stop() - stop a phylink instance
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 *
 * Stop the phylink instance specified by @pl. This should be called from the
 * network device driver's &struct net_device_ops ndo_stop() method.  The
 * network device's carrier state should not be changed prior to calling this
 * function.
 *
 * This will synchronously bring down the link if the link is not already
 * down (in other words, it will trigger a mac_link_down() method call.)
 */
void phylink_stop(struct phylink *pl)
{
	ASSERT_RTNL();

	if (pl->sfp_bus)
		sfp_upstream_stop(pl->sfp_bus);
	if (pl->phydev)
		phy_stop(pl->phydev);
	del_timer_sync(&pl->link_poll);
	if (pl->link_irq) {
		free_irq(pl->link_irq, pl);
		pl->link_irq = 0;
	}

	phylink_run_resolve_and_disable(pl, PHYLINK_DISABLE_STOPPED);

	pl->pcs_state = PCS_STATE_DOWN;

	phylink_pcs_disable(pl->pcs);
}
EXPORT_SYMBOL_GPL(phylink_stop);

/**
 * phylink_suspend() - handle a network device suspend event
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @mac_wol: true if the MAC needs to receive packets for Wake-on-Lan
 *
 * Handle a network device suspend event. There are several cases:
 *
 * - If Wake-on-Lan is not active, we can bring down the link between
 *   the MAC and PHY by calling phylink_stop().
 * - If Wake-on-Lan is active, and being handled only by the PHY, we
 *   can also bring down the link between the MAC and PHY.
 * - If Wake-on-Lan is active, but being handled by the MAC, the MAC
 *   still needs to receive packets, so we can not bring the link down.
 */
void phylink_suspend(struct phylink *pl, bool mac_wol)
{
	ASSERT_RTNL();

	if (mac_wol && (!pl->netdev || pl->netdev->ethtool->wol_enabled)) {
		/* Wake-on-Lan enabled, MAC handling */
		mutex_lock(&pl->state_mutex);

		/* Stop the resolver bringing the link up */
		__set_bit(PHYLINK_DISABLE_MAC_WOL, &pl->phylink_disable_state);

		/* Disable the carrier, to prevent transmit timeouts,
		 * but one would hope all packets have been sent. This
		 * also means phylink_resolve() will do nothing.
		 */
		if (pl->netdev)
			netif_carrier_off(pl->netdev);
		else
			pl->old_link_state = false;

		/* We do not call mac_link_down() here as we want the
		 * link to remain up to receive the WoL packets.
		 */
		mutex_unlock(&pl->state_mutex);
	} else {
		phylink_stop(pl);
	}
}
EXPORT_SYMBOL_GPL(phylink_suspend);

/**
 * phylink_resume() - handle a network device resume event
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 *
 * Undo the effects of phylink_suspend(), returning the link to an
 * operational state.
 */
void phylink_resume(struct phylink *pl)
{
	ASSERT_RTNL();

	if (test_bit(PHYLINK_DISABLE_MAC_WOL, &pl->phylink_disable_state)) {
		/* Wake-on-Lan enabled, MAC handling */

		/* Call mac_link_down() so we keep the overall state balanced.
		 * Do this under the state_mutex lock for consistency. This
		 * will cause a "Link Down" message to be printed during
		 * resume, which is harmless - the true link state will be
		 * printed when we run a resolve.
		 */
		mutex_lock(&pl->state_mutex);
		phylink_link_down(pl);
		mutex_unlock(&pl->state_mutex);

		/* Re-apply the link parameters so that all the settings get
		 * restored to the MAC.
		 */
		phylink_mac_initial_config(pl, true);

		/* Re-enable and re-resolve the link parameters */
		phylink_enable_and_run_resolve(pl, PHYLINK_DISABLE_MAC_WOL);
	} else {
		phylink_start(pl);
	}
}
EXPORT_SYMBOL_GPL(phylink_resume);

/**
 * phylink_ethtool_get_wol() - get the wake on lan parameters for the PHY
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @wol: a pointer to &struct ethtool_wolinfo to hold the read parameters
 *
 * Read the wake on lan parameters from the PHY attached to the phylink
 * instance specified by @pl. If no PHY is currently attached, report no
 * support for wake on lan.
 */
void phylink_ethtool_get_wol(struct phylink *pl, struct ethtool_wolinfo *wol)
{
	ASSERT_RTNL();

	wol->supported = 0;
	wol->wolopts = 0;

	if (pl->phydev)
		phy_ethtool_get_wol(pl->phydev, wol);
}
EXPORT_SYMBOL_GPL(phylink_ethtool_get_wol);

/**
 * phylink_ethtool_set_wol() - set wake on lan parameters
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @wol: a pointer to &struct ethtool_wolinfo for the desired parameters
 *
 * Set the wake on lan parameters for the PHY attached to the phylink
 * instance specified by @pl. If no PHY is attached, returns %EOPNOTSUPP
 * error.
 *
 * Returns zero on success or negative errno code.
 */
int phylink_ethtool_set_wol(struct phylink *pl, struct ethtool_wolinfo *wol)
{
	int ret = -EOPNOTSUPP;

	ASSERT_RTNL();

	if (pl->phydev)
		ret = phy_ethtool_set_wol(pl->phydev, wol);

	return ret;
}
EXPORT_SYMBOL_GPL(phylink_ethtool_set_wol);

static phy_interface_t phylink_sfp_select_interface(struct phylink *pl,
						const unsigned long *link_modes)
{
	phy_interface_t interface;

	interface = sfp_select_interface(pl->sfp_bus, link_modes);
	if (interface == PHY_INTERFACE_MODE_NA) {
		phylink_err(pl,
			    "selection of interface failed, advertisement %*pb\n",
			    __ETHTOOL_LINK_MODE_MASK_NBITS,
			    link_modes);
		return interface;
	}

	if (!test_bit(interface, pl->config->supported_interfaces)) {
		phylink_err(pl,
			    "selection of interface failed, SFP selected %s (%u) but MAC supports %*pbl\n",
			    phy_modes(interface), interface,
			    (int)PHY_INTERFACE_MODE_MAX,
			    pl->config->supported_interfaces);
		return PHY_INTERFACE_MODE_NA;
	}

	return interface;
}

static void phylink_merge_link_mode(unsigned long *dst, const unsigned long *b)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(mask);

	linkmode_zero(mask);
	phylink_set_port_modes(mask);

	linkmode_and(dst, dst, mask);
	linkmode_or(dst, dst, b);
}

static void phylink_get_ksettings(const struct phylink_link_state *state,
				  struct ethtool_link_ksettings *kset)
{
	phylink_merge_link_mode(kset->link_modes.advertising, state->advertising);
	linkmode_copy(kset->link_modes.lp_advertising, state->lp_advertising);
	if (kset->base.rate_matching == RATE_MATCH_NONE) {
		kset->base.speed = state->speed;
		kset->base.duplex = state->duplex;
	}
	kset->base.autoneg = linkmode_test_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
					       state->advertising) ?
				AUTONEG_ENABLE : AUTONEG_DISABLE;
}

/**
 * phylink_ethtool_ksettings_get() - get the current link settings
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @kset: a pointer to a &struct ethtool_link_ksettings to hold link settings
 *
 * Read the current link settings for the phylink instance specified by @pl.
 * This will be the link settings read from the MAC, PHY or fixed link
 * settings depending on the current negotiation mode.
 */
int phylink_ethtool_ksettings_get(struct phylink *pl,
				  struct ethtool_link_ksettings *kset)
{
	struct phylink_link_state link_state;

	ASSERT_RTNL();

	if (pl->phydev)
		phy_ethtool_ksettings_get(pl->phydev, kset);
	else
		kset->base.port = pl->link_port;

	linkmode_copy(kset->link_modes.supported, pl->supported);

	switch (pl->act_link_an_mode) {
	case MLO_AN_FIXED:
		/* We are using fixed settings. Report these as the
		 * current link settings - and note that these also
		 * represent the supported speeds/duplex/pause modes.
		 */
		phylink_get_fixed_state(pl, &link_state);
		phylink_get_ksettings(&link_state, kset);
		break;

	case MLO_AN_INBAND:
		/* If there is a phy attached, then use the reported
		 * settings from the phy with no modification.
		 */
		if (pl->phydev)
			break;

		phylink_mac_pcs_get_state(pl, &link_state);

		/* The MAC is reporting the link results from its own PCS
		 * layer via in-band status. Report these as the current
		 * link settings.
		 */
		phylink_get_ksettings(&link_state, kset);
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(phylink_ethtool_ksettings_get);

static bool phylink_validate_pcs_inband_autoneg(struct phylink *pl,
					        phy_interface_t interface,
						unsigned long *adv)
{
	unsigned int inband = phylink_inband_caps(pl, interface);
	unsigned int mask;

	/* If the PCS doesn't implement inband support, be permissive. */
	if (!inband)
		return true;

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, adv))
		mask = LINK_INBAND_ENABLE;
	else
		mask = LINK_INBAND_DISABLE;

	/* Check whether the PCS implements the required mode */
	return !!(inband & mask);
}

/**
 * phylink_ethtool_ksettings_set() - set the link settings
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @kset: a pointer to a &struct ethtool_link_ksettings for the desired modes
 */
int phylink_ethtool_ksettings_set(struct phylink *pl,
				  const struct ethtool_link_ksettings *kset)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(support);
	struct phylink_link_state config;
	const struct phy_setting *s;

	ASSERT_RTNL();

	if (pl->phydev) {
		struct ethtool_link_ksettings phy_kset = *kset;

		linkmode_and(phy_kset.link_modes.advertising,
			     phy_kset.link_modes.advertising,
			     pl->supported);

		/* We can rely on phylib for this update; we also do not need
		 * to update the pl->link_config settings:
		 * - the configuration returned via ksettings_get() will come
		 *   from phylib whenever a PHY is present.
		 * - link_config.interface will be updated by the PHY calling
		 *   back via phylink_phy_change() and a subsequent resolve.
		 * - initial link configuration for PHY mode comes from the
		 *   last phy state updated via phylink_phy_change().
		 * - other configuration changes (e.g. pause modes) are
		 *   performed directly via phylib.
		 * - if in in-band mode with a PHY, the link configuration
		 *   is passed on the link from the PHY, and all of
		 *   link_config.{speed,duplex,an_enabled,pause} are not used.
		 * - the only possible use would be link_config.advertising
		 *   pause modes when in 1000base-X mode with a PHY, but in
		 *   the presence of a PHY, this should not be changed as that
		 *   should be determined from the media side advertisement.
		 */
		return phy_ethtool_ksettings_set(pl->phydev, &phy_kset);
	}

	config = pl->link_config;
	/* Mask out unsupported advertisements */
	linkmode_and(config.advertising, kset->link_modes.advertising,
		     pl->supported);

	/* FIXME: should we reject autoneg if phy/mac does not support it? */
	switch (kset->base.autoneg) {
	case AUTONEG_DISABLE:
		/* Autonegotiation disabled, select a suitable speed and
		 * duplex.
		 */
		s = phy_lookup_setting(kset->base.speed, kset->base.duplex,
				       pl->supported, false);
		if (!s)
			return -EINVAL;

		/* If we have a fixed link, refuse to change link parameters.
		 * If the link parameters match, accept them but do nothing.
		 */
		if (pl->req_link_an_mode == MLO_AN_FIXED) {
			if (s->speed != pl->link_config.speed ||
			    s->duplex != pl->link_config.duplex)
				return -EINVAL;
			return 0;
		}

		config.speed = s->speed;
		config.duplex = s->duplex;
		break;

	case AUTONEG_ENABLE:
		/* If we have a fixed link, allow autonegotiation (since that
		 * is our default case) but do not allow the advertisement to
		 * be changed. If the advertisement matches, simply return.
		 */
		if (pl->req_link_an_mode == MLO_AN_FIXED) {
			if (!linkmode_equal(config.advertising,
					    pl->link_config.advertising))
				return -EINVAL;
			return 0;
		}

		config.speed = SPEED_UNKNOWN;
		config.duplex = DUPLEX_UNKNOWN;
		break;

	default:
		return -EINVAL;
	}

	/* We have ruled out the case with a PHY attached, and the
	 * fixed-link cases.  All that is left are in-band links.
	 */
	linkmode_mod_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, config.advertising,
			 kset->base.autoneg == AUTONEG_ENABLE);

	/* If this link is with an SFP, ensure that changes to advertised modes
	 * also cause the associated interface to be selected such that the
	 * link can be configured correctly.
	 */
	if (pl->sfp_bus) {
		config.interface = phylink_sfp_select_interface(pl,
							config.advertising);
		if (config.interface == PHY_INTERFACE_MODE_NA)
			return -EINVAL;

		/* Revalidate with the selected interface */
		linkmode_copy(support, pl->supported);
		if (phylink_validate(pl, support, &config)) {
			phylink_err(pl, "validation of %s/%s with support %*pb failed\n",
				    phylink_an_mode_str(pl->req_link_an_mode),
				    phy_modes(config.interface),
				    __ETHTOOL_LINK_MODE_MASK_NBITS, support);
			return -EINVAL;
		}
	} else {
		/* Validate without changing the current supported mask. */
		linkmode_copy(support, pl->supported);
		if (phylink_validate(pl, support, &config))
			return -EINVAL;
	}

	/* If autonegotiation is enabled, we must have an advertisement */
	if (linkmode_test_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
			      config.advertising) &&
	    phylink_is_empty_linkmode(config.advertising))
		return -EINVAL;

	/* Validate the autonegotiation state. We don't have a PHY in this
	 * situation, so the PCS is the media-facing entity.
	 */
	if (!phylink_validate_pcs_inband_autoneg(pl, config.interface,
						 config.advertising))
		return -EINVAL;

	mutex_lock(&pl->state_mutex);
	pl->link_config.speed = config.speed;
	pl->link_config.duplex = config.duplex;

	if (pl->link_config.interface != config.interface) {
		/* The interface changed, e.g. 1000base-X <-> 2500base-X */
		/* We need to force the link down, then change the interface */
		if (pl->old_link_state) {
			phylink_link_down(pl);
			pl->old_link_state = false;
		}
		if (!test_bit(PHYLINK_DISABLE_STOPPED,
			      &pl->phylink_disable_state))
			phylink_major_config(pl, false, &config);
		pl->link_config.interface = config.interface;
		linkmode_copy(pl->link_config.advertising, config.advertising);
	} else if (!linkmode_equal(pl->link_config.advertising,
				   config.advertising)) {
		linkmode_copy(pl->link_config.advertising, config.advertising);
		phylink_change_inband_advert(pl);
	}
	mutex_unlock(&pl->state_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(phylink_ethtool_ksettings_set);

/**
 * phylink_ethtool_nway_reset() - restart negotiation
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 *
 * Restart negotiation for the phylink instance specified by @pl. This will
 * cause any attached phy to restart negotiation with the link partner, and
 * if the MAC is in a BaseX mode, the MAC will also be requested to restart
 * negotiation.
 *
 * Returns zero on success, or negative error code.
 */
int phylink_ethtool_nway_reset(struct phylink *pl)
{
	int ret = 0;

	ASSERT_RTNL();

	if (pl->phydev)
		ret = phy_restart_aneg(pl->phydev);
	phylink_pcs_an_restart(pl);

	return ret;
}
EXPORT_SYMBOL_GPL(phylink_ethtool_nway_reset);

/**
 * phylink_ethtool_get_pauseparam() - get the current pause parameters
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @pause: a pointer to a &struct ethtool_pauseparam
 */
void phylink_ethtool_get_pauseparam(struct phylink *pl,
				    struct ethtool_pauseparam *pause)
{
	ASSERT_RTNL();

	pause->autoneg = !!(pl->link_config.pause & MLO_PAUSE_AN);
	pause->rx_pause = !!(pl->link_config.pause & MLO_PAUSE_RX);
	pause->tx_pause = !!(pl->link_config.pause & MLO_PAUSE_TX);
}
EXPORT_SYMBOL_GPL(phylink_ethtool_get_pauseparam);

/**
 * phylink_ethtool_set_pauseparam() - set the current pause parameters
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @pause: a pointer to a &struct ethtool_pauseparam
 */
int phylink_ethtool_set_pauseparam(struct phylink *pl,
				   struct ethtool_pauseparam *pause)
{
	struct phylink_link_state *config = &pl->link_config;
	bool manual_changed;
	int pause_state;

	ASSERT_RTNL();

	if (pl->req_link_an_mode == MLO_AN_FIXED)
		return -EOPNOTSUPP;

	if (!phylink_test(pl->supported, Pause) &&
	    !phylink_test(pl->supported, Asym_Pause))
		return -EOPNOTSUPP;

	if (!phylink_test(pl->supported, Asym_Pause) &&
	    pause->rx_pause != pause->tx_pause)
		return -EINVAL;

	pause_state = 0;
	if (pause->autoneg)
		pause_state |= MLO_PAUSE_AN;
	if (pause->rx_pause)
		pause_state |= MLO_PAUSE_RX;
	if (pause->tx_pause)
		pause_state |= MLO_PAUSE_TX;

	mutex_lock(&pl->state_mutex);
	/*
	 * See the comments for linkmode_set_pause(), wrt the deficiencies
	 * with the current implementation.  A solution to this issue would
	 * be:
	 * ethtool  Local device
	 *  rx  tx  Pause AsymDir
	 *  0   0   0     0
	 *  1   0   1     1
	 *  0   1   0     1
	 *  1   1   1     1
	 * and then use the ethtool rx/tx enablement status to mask the
	 * rx/tx pause resolution.
	 */
	linkmode_set_pause(config->advertising, pause->tx_pause,
			   pause->rx_pause);

	manual_changed = (config->pause ^ pause_state) & MLO_PAUSE_AN ||
			 (!(pause_state & MLO_PAUSE_AN) &&
			   (config->pause ^ pause_state) & MLO_PAUSE_TXRX_MASK);

	config->pause = pause_state;

	/* Update our in-band advertisement, triggering a renegotiation if
	 * the advertisement changed.
	 */
	if (!pl->phydev)
		phylink_change_inband_advert(pl);

	mutex_unlock(&pl->state_mutex);

	/* If we have a PHY, a change of the pause frame advertisement will
	 * cause phylib to renegotiate (if AN is enabled) which will in turn
	 * call our phylink_phy_change() and trigger a resolve.  Note that
	 * we can't hold our state mutex while calling phy_set_asym_pause().
	 */
	if (pl->phydev)
		phy_set_asym_pause(pl->phydev, pause->rx_pause,
				   pause->tx_pause);

	/* If the manual pause settings changed, make sure we trigger a
	 * resolve to update their state; we can not guarantee that the
	 * link will cycle.
	 */
	if (manual_changed) {
		pl->link_failed = true;
		phylink_run_resolve(pl);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(phylink_ethtool_set_pauseparam);

/**
 * phylink_get_eee_err() - read the energy efficient ethernet error
 *   counter
 * @pl: a pointer to a &struct phylink returned from phylink_create().
 *
 * Read the Energy Efficient Ethernet error counter from the PHY associated
 * with the phylink instance specified by @pl.
 *
 * Returns positive error counter value, or negative error code.
 */
int phylink_get_eee_err(struct phylink *pl)
{
	int ret = 0;

	ASSERT_RTNL();

	if (pl->phydev)
		ret = phy_get_eee_err(pl->phydev);

	return ret;
}
EXPORT_SYMBOL_GPL(phylink_get_eee_err);

/**
 * phylink_init_eee() - init and check the EEE features
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @clk_stop_enable: allow PHY to stop receive clock
 *
 * Must be called either with RTNL held or within mac_link_up()
 */
int phylink_init_eee(struct phylink *pl, bool clk_stop_enable)
{
	int ret = -EOPNOTSUPP;

	if (pl->phydev)
		ret = phy_init_eee(pl->phydev, clk_stop_enable);

	return ret;
}
EXPORT_SYMBOL_GPL(phylink_init_eee);

/**
 * phylink_ethtool_get_eee() - read the energy efficient ethernet parameters
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @eee: a pointer to a &struct ethtool_keee for the read parameters
 */
int phylink_ethtool_get_eee(struct phylink *pl, struct ethtool_keee *eee)
{
	int ret = -EOPNOTSUPP;

	ASSERT_RTNL();

	if (pl->phydev)
		ret = phy_ethtool_get_eee(pl->phydev, eee);

	return ret;
}
EXPORT_SYMBOL_GPL(phylink_ethtool_get_eee);

/**
 * phylink_ethtool_set_eee() - set the energy efficient ethernet parameters
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @eee: a pointer to a &struct ethtool_keee for the desired parameters
 */
int phylink_ethtool_set_eee(struct phylink *pl, struct ethtool_keee *eee)
{
	int ret = -EOPNOTSUPP;

	ASSERT_RTNL();

	if (pl->phydev)
		ret = phy_ethtool_set_eee(pl->phydev, eee);

	return ret;
}
EXPORT_SYMBOL_GPL(phylink_ethtool_set_eee);

/* This emulates MII registers for a fixed-mode phy operating as per the
 * passed in state. "aneg" defines if we report negotiation is possible.
 *
 * FIXME: should deal with negotiation state too.
 */
static int phylink_mii_emul_read(unsigned int reg,
				 struct phylink_link_state *state)
{
	struct fixed_phy_status fs;
	unsigned long *lpa = state->lp_advertising;
	int val;

	fs.link = state->link;
	fs.speed = state->speed;
	fs.duplex = state->duplex;
	fs.pause = test_bit(ETHTOOL_LINK_MODE_Pause_BIT, lpa);
	fs.asym_pause = test_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT, lpa);

	val = swphy_read_reg(reg, &fs);
	if (reg == MII_BMSR) {
		if (!state->an_complete)
			val &= ~BMSR_ANEGCOMPLETE;
	}
	return val;
}

static int phylink_phy_read(struct phylink *pl, unsigned int phy_id,
			    unsigned int reg)
{
	struct phy_device *phydev = pl->phydev;
	int prtad, devad;

	if (mdio_phy_id_is_c45(phy_id)) {
		prtad = mdio_phy_id_prtad(phy_id);
		devad = mdio_phy_id_devad(phy_id);
		return mdiobus_c45_read(pl->phydev->mdio.bus, prtad, devad,
					reg);
	}

	if (phydev->is_c45) {
		switch (reg) {
		case MII_BMCR:
		case MII_BMSR:
		case MII_PHYSID1:
		case MII_PHYSID2:
			devad = __ffs(phydev->c45_ids.mmds_present);
			break;
		case MII_ADVERTISE:
		case MII_LPA:
			if (!(phydev->c45_ids.mmds_present & MDIO_DEVS_AN))
				return -EINVAL;
			devad = MDIO_MMD_AN;
			if (reg == MII_ADVERTISE)
				reg = MDIO_AN_ADVERTISE;
			else
				reg = MDIO_AN_LPA;
			break;
		default:
			return -EINVAL;
		}
		prtad = phy_id;
		return mdiobus_c45_read(pl->phydev->mdio.bus, prtad, devad,
					reg);
	}

	return mdiobus_read(pl->phydev->mdio.bus, phy_id, reg);
}

static int phylink_phy_write(struct phylink *pl, unsigned int phy_id,
			     unsigned int reg, unsigned int val)
{
	struct phy_device *phydev = pl->phydev;
	int prtad, devad;

	if (mdio_phy_id_is_c45(phy_id)) {
		prtad = mdio_phy_id_prtad(phy_id);
		devad = mdio_phy_id_devad(phy_id);
		return mdiobus_c45_write(pl->phydev->mdio.bus, prtad, devad,
					 reg, val);
	}

	if (phydev->is_c45) {
		switch (reg) {
		case MII_BMCR:
		case MII_BMSR:
		case MII_PHYSID1:
		case MII_PHYSID2:
			devad = __ffs(phydev->c45_ids.mmds_present);
			break;
		case MII_ADVERTISE:
		case MII_LPA:
			if (!(phydev->c45_ids.mmds_present & MDIO_DEVS_AN))
				return -EINVAL;
			devad = MDIO_MMD_AN;
			if (reg == MII_ADVERTISE)
				reg = MDIO_AN_ADVERTISE;
			else
				reg = MDIO_AN_LPA;
			break;
		default:
			return -EINVAL;
		}
		return mdiobus_c45_write(pl->phydev->mdio.bus, phy_id, devad,
					 reg, val);
	}

	return mdiobus_write(phydev->mdio.bus, phy_id, reg, val);
}

static int phylink_mii_read(struct phylink *pl, unsigned int phy_id,
			    unsigned int reg)
{
	struct phylink_link_state state;
	int val = 0xffff;

	switch (pl->act_link_an_mode) {
	case MLO_AN_FIXED:
		if (phy_id == 0) {
			phylink_get_fixed_state(pl, &state);
			val = phylink_mii_emul_read(reg, &state);
		}
		break;

	case MLO_AN_PHY:
		return -EOPNOTSUPP;

	case MLO_AN_INBAND:
		if (phy_id == 0) {
			phylink_mac_pcs_get_state(pl, &state);
			val = phylink_mii_emul_read(reg, &state);
		}
		break;
	}

	return val & 0xffff;
}

static int phylink_mii_write(struct phylink *pl, unsigned int phy_id,
			     unsigned int reg, unsigned int val)
{
	switch (pl->act_link_an_mode) {
	case MLO_AN_FIXED:
		break;

	case MLO_AN_PHY:
		return -EOPNOTSUPP;

	case MLO_AN_INBAND:
		break;
	}

	return 0;
}

/**
 * phylink_mii_ioctl() - generic mii ioctl interface
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @ifr: a pointer to a &struct ifreq for socket ioctls
 * @cmd: ioctl cmd to execute
 *
 * Perform the specified MII ioctl on the PHY attached to the phylink instance
 * specified by @pl. If no PHY is attached, emulate the presence of the PHY.
 *
 * Returns: zero on success or negative error code.
 *
 * %SIOCGMIIPHY:
 *  read register from the current PHY.
 * %SIOCGMIIREG:
 *  read register from the specified PHY.
 * %SIOCSMIIREG:
 *  set a register on the specified PHY.
 */
int phylink_mii_ioctl(struct phylink *pl, struct ifreq *ifr, int cmd)
{
	struct mii_ioctl_data *mii = if_mii(ifr);
	int  ret;

	ASSERT_RTNL();

	if (pl->phydev) {
		/* PHYs only exist for MLO_AN_PHY and SGMII */
		switch (cmd) {
		case SIOCGMIIPHY:
			mii->phy_id = pl->phydev->mdio.addr;
			fallthrough;

		case SIOCGMIIREG:
			ret = phylink_phy_read(pl, mii->phy_id, mii->reg_num);
			if (ret >= 0) {
				mii->val_out = ret;
				ret = 0;
			}
			break;

		case SIOCSMIIREG:
			ret = phylink_phy_write(pl, mii->phy_id, mii->reg_num,
						mii->val_in);
			break;

		default:
			ret = phy_mii_ioctl(pl->phydev, ifr, cmd);
			break;
		}
	} else {
		switch (cmd) {
		case SIOCGMIIPHY:
			mii->phy_id = 0;
			fallthrough;

		case SIOCGMIIREG:
			ret = phylink_mii_read(pl, mii->phy_id, mii->reg_num);
			if (ret >= 0) {
				mii->val_out = ret;
				ret = 0;
			}
			break;

		case SIOCSMIIREG:
			ret = phylink_mii_write(pl, mii->phy_id, mii->reg_num,
						mii->val_in);
			break;

		default:
			ret = -EOPNOTSUPP;
			break;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(phylink_mii_ioctl);

/**
 * phylink_speed_down() - set the non-SFP PHY to lowest speed supported by both
 *   link partners
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @sync: perform action synchronously
 *
 * If we have a PHY that is not part of a SFP module, then set the speed
 * as described in the phy_speed_down() function. Please see this function
 * for a description of the @sync parameter.
 *
 * Returns zero if there is no PHY, otherwise as per phy_speed_down().
 */
int phylink_speed_down(struct phylink *pl, bool sync)
{
	int ret = 0;

	ASSERT_RTNL();

	if (!pl->sfp_bus && pl->phydev)
		ret = phy_speed_down(pl->phydev, sync);

	return ret;
}
EXPORT_SYMBOL_GPL(phylink_speed_down);

/**
 * phylink_speed_up() - restore the advertised speeds prior to the call to
 *   phylink_speed_down()
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 *
 * If we have a PHY that is not part of a SFP module, then restore the
 * PHY speeds as per phy_speed_up().
 *
 * Returns zero if there is no PHY, otherwise as per phy_speed_up().
 */
int phylink_speed_up(struct phylink *pl)
{
	int ret = 0;

	ASSERT_RTNL();

	if (!pl->sfp_bus && pl->phydev)
		ret = phy_speed_up(pl->phydev);

	return ret;
}
EXPORT_SYMBOL_GPL(phylink_speed_up);

static void phylink_sfp_attach(void *upstream, struct sfp_bus *bus)
{
	struct phylink *pl = upstream;

	pl->netdev->sfp_bus = bus;
}

static void phylink_sfp_detach(void *upstream, struct sfp_bus *bus)
{
	struct phylink *pl = upstream;

	pl->netdev->sfp_bus = NULL;
}

static phy_interface_t phylink_choose_sfp_interface(struct phylink *pl,
						    const unsigned long *intf)
{
	phy_interface_t interface;
	size_t i;

	interface = PHY_INTERFACE_MODE_NA;
	for (i = 0; i < ARRAY_SIZE(phylink_sfp_interface_preference); i++)
		if (test_bit(phylink_sfp_interface_preference[i], intf)) {
			interface = phylink_sfp_interface_preference[i];
			break;
		}

	return interface;
}

static void phylink_sfp_set_config(struct phylink *pl,
				   unsigned long *supported,
				   struct phylink_link_state *state)
{
	u8 mode = MLO_AN_INBAND;
	bool changed = false;

	phylink_dbg(pl, "requesting link mode %s/%s with support %*pb\n",
		    phylink_an_mode_str(mode), phy_modes(state->interface),
		    __ETHTOOL_LINK_MODE_MASK_NBITS, supported);

	if (!linkmode_equal(pl->supported, supported)) {
		linkmode_copy(pl->supported, supported);
		changed = true;
	}

	if (!linkmode_equal(pl->link_config.advertising, state->advertising)) {
		linkmode_copy(pl->link_config.advertising, state->advertising);
		changed = true;
	}

	if (pl->req_link_an_mode != mode ||
	    pl->link_config.interface != state->interface) {
		pl->req_link_an_mode = mode;
		pl->link_config.interface = state->interface;

		changed = true;

		phylink_info(pl, "switched to %s/%s link mode\n",
			     phylink_an_mode_str(mode),
			     phy_modes(state->interface));
	}

	if (changed && !test_bit(PHYLINK_DISABLE_STOPPED,
				 &pl->phylink_disable_state))
		phylink_mac_initial_config(pl, false);
}

static int phylink_sfp_config_phy(struct phylink *pl, struct phy_device *phy)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(support);
	struct phylink_link_state config;
	int ret;

	linkmode_copy(support, phy->supported);

	memset(&config, 0, sizeof(config));
	linkmode_copy(config.advertising, phy->advertising);
	config.interface = PHY_INTERFACE_MODE_NA;
	config.speed = SPEED_UNKNOWN;
	config.duplex = DUPLEX_UNKNOWN;
	config.pause = MLO_PAUSE_AN;

	/* Ignore errors if we're expecting a PHY to attach later */
	ret = phylink_validate(pl, support, &config);
	if (ret) {
		phylink_err(pl, "validation with support %*pb failed: %pe\n",
			    __ETHTOOL_LINK_MODE_MASK_NBITS, support,
			    ERR_PTR(ret));
		return ret;
	}

	config.interface = phylink_sfp_select_interface(pl, config.advertising);
	if (config.interface == PHY_INTERFACE_MODE_NA)
		return -EINVAL;

	/* Attach the PHY so that the PHY is present when we do the major
	 * configuration step.
	 */
	ret = phylink_attach_phy(pl, phy, config.interface);
	if (ret < 0)
		return ret;

	/* This will validate the configuration for us. */
	ret = phylink_bringup_phy(pl, phy, config.interface);
	if (ret < 0) {
		phy_detach(phy);
		return ret;
	}

	pl->link_port = pl->sfp_port;

	phylink_sfp_set_config(pl, support, &config);

	return 0;
}

static int phylink_sfp_config_optical(struct phylink *pl)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(support);
	DECLARE_PHY_INTERFACE_MASK(interfaces);
	struct phylink_link_state config;
	phy_interface_t interface;
	int ret;

	phylink_dbg(pl, "optical SFP: interfaces=[mac=%*pbl, sfp=%*pbl]\n",
		    (int)PHY_INTERFACE_MODE_MAX,
		    pl->config->supported_interfaces,
		    (int)PHY_INTERFACE_MODE_MAX,
		    pl->sfp_interfaces);

	/* Find the union of the supported interfaces by the PCS/MAC and
	 * the SFP module.
	 */
	phy_interface_and(interfaces, pl->config->supported_interfaces,
			  pl->sfp_interfaces);
	if (phy_interface_empty(interfaces)) {
		phylink_err(pl, "unsupported SFP module: no common interface modes\n");
		return -EINVAL;
	}

	memset(&config, 0, sizeof(config));
	linkmode_copy(support, pl->sfp_support);
	linkmode_copy(config.advertising, pl->sfp_support);
	config.speed = SPEED_UNKNOWN;
	config.duplex = DUPLEX_UNKNOWN;
	config.pause = MLO_PAUSE_AN;

	/* For all the interfaces that are supported, reduce the sfp_support
	 * mask to only those link modes that can be supported.
	 */
	ret = phylink_validate_mask(pl, NULL, pl->sfp_support, &config,
				    interfaces);
	if (ret) {
		phylink_err(pl, "unsupported SFP module: validation with support %*pb failed\n",
			    __ETHTOOL_LINK_MODE_MASK_NBITS, support);
		return ret;
	}

	interface = phylink_choose_sfp_interface(pl, interfaces);
	if (interface == PHY_INTERFACE_MODE_NA) {
		phylink_err(pl, "failed to select SFP interface\n");
		return -EINVAL;
	}

	phylink_dbg(pl, "optical SFP: chosen %s interface\n",
		    phy_modes(interface));

	if (!phylink_validate_pcs_inband_autoneg(pl, interface,
						 config.advertising)) {
		phylink_err(pl, "autoneg setting not compatible with PCS");
		return -EINVAL;
	}

	config.interface = interface;

	/* Ignore errors if we're expecting a PHY to attach later */
	ret = phylink_validate(pl, support, &config);
	if (ret) {
		phylink_err(pl, "validation with support %*pb failed: %pe\n",
			    __ETHTOOL_LINK_MODE_MASK_NBITS, support,
			    ERR_PTR(ret));
		return ret;
	}

	pl->link_port = pl->sfp_port;

	phylink_sfp_set_config(pl, pl->sfp_support, &config);

	return 0;
}

static int phylink_sfp_module_insert(void *upstream,
				     const struct sfp_eeprom_id *id)
{
	struct phylink *pl = upstream;

	ASSERT_RTNL();

	linkmode_zero(pl->sfp_support);
	phy_interface_zero(pl->sfp_interfaces);
	sfp_parse_support(pl->sfp_bus, id, pl->sfp_support, pl->sfp_interfaces);
	pl->sfp_port = sfp_parse_port(pl->sfp_bus, id, pl->sfp_support);

	/* If this module may have a PHY connecting later, defer until later */
	pl->sfp_may_have_phy = sfp_may_have_phy(pl->sfp_bus, id);
	if (pl->sfp_may_have_phy)
		return 0;

	return phylink_sfp_config_optical(pl);
}

static int phylink_sfp_module_start(void *upstream)
{
	struct phylink *pl = upstream;

	/* If this SFP module has a PHY, start the PHY now. */
	if (pl->phydev) {
		phy_start(pl->phydev);
		return 0;
	}

	/* If the module may have a PHY but we didn't detect one we
	 * need to configure the MAC here.
	 */
	if (!pl->sfp_may_have_phy)
		return 0;

	return phylink_sfp_config_optical(pl);
}

static void phylink_sfp_module_stop(void *upstream)
{
	struct phylink *pl = upstream;

	/* If this SFP module has a PHY, stop it. */
	if (pl->phydev)
		phy_stop(pl->phydev);
}

static void phylink_sfp_link_down(void *upstream)
{
	struct phylink *pl = upstream;

	ASSERT_RTNL();

	phylink_run_resolve_and_disable(pl, PHYLINK_DISABLE_LINK);
}

static void phylink_sfp_link_up(void *upstream)
{
	struct phylink *pl = upstream;

	ASSERT_RTNL();

	phylink_enable_and_run_resolve(pl, PHYLINK_DISABLE_LINK);
}

static int phylink_sfp_connect_phy(void *upstream, struct phy_device *phy)
{
	struct phylink *pl = upstream;

	/*
	 * This is the new way of dealing with flow control for PHYs,
	 * as described by Timur Tabi in commit 529ed1275263 ("net: phy:
	 * phy drivers should not set SUPPORTED_[Asym_]Pause") except
	 * using our validate call to the MAC, we rely upon the MAC
	 * clearing the bits from both supported and advertising fields.
	 */
	phy_support_asym_pause(phy);

	/* Set the PHY's host supported interfaces */
	phy_interface_and(phy->host_interfaces, phylink_sfp_interfaces,
			  pl->config->supported_interfaces);

	/* Do the initial configuration */
	return phylink_sfp_config_phy(pl, phy);
}

static void phylink_sfp_disconnect_phy(void *upstream,
				       struct phy_device *phydev)
{
	phylink_disconnect_phy(upstream);
}

static const struct sfp_upstream_ops sfp_phylink_ops = {
	.attach = phylink_sfp_attach,
	.detach = phylink_sfp_detach,
	.module_insert = phylink_sfp_module_insert,
	.module_start = phylink_sfp_module_start,
	.module_stop = phylink_sfp_module_stop,
	.link_up = phylink_sfp_link_up,
	.link_down = phylink_sfp_link_down,
	.connect_phy = phylink_sfp_connect_phy,
	.disconnect_phy = phylink_sfp_disconnect_phy,
};

/* Helpers for MAC drivers */

static struct {
	int bit;
	int speed;
} phylink_c73_priority_resolution[] = {
	{ ETHTOOL_LINK_MODE_100000baseCR4_Full_BIT, SPEED_100000 },
	{ ETHTOOL_LINK_MODE_100000baseKR4_Full_BIT, SPEED_100000 },
	/* 100GBASE-KP4 and 100GBASE-CR10 not supported */
	{ ETHTOOL_LINK_MODE_40000baseCR4_Full_BIT, SPEED_40000 },
	{ ETHTOOL_LINK_MODE_40000baseKR4_Full_BIT, SPEED_40000 },
	{ ETHTOOL_LINK_MODE_10000baseKR_Full_BIT, SPEED_10000 },
	{ ETHTOOL_LINK_MODE_10000baseKX4_Full_BIT, SPEED_10000 },
	/* 5GBASE-KR not supported */
	{ ETHTOOL_LINK_MODE_2500baseX_Full_BIT, SPEED_2500 },
	{ ETHTOOL_LINK_MODE_1000baseKX_Full_BIT, SPEED_1000 },
};

void phylink_resolve_c73(struct phylink_link_state *state)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(phylink_c73_priority_resolution); i++) {
		int bit = phylink_c73_priority_resolution[i].bit;
		if (linkmode_test_bit(bit, state->advertising) &&
		    linkmode_test_bit(bit, state->lp_advertising))
			break;
	}

	if (i < ARRAY_SIZE(phylink_c73_priority_resolution)) {
		state->speed = phylink_c73_priority_resolution[i].speed;
		state->duplex = DUPLEX_FULL;
	} else {
		/* negotiation failure */
		state->link = false;
	}

	phylink_resolve_an_pause(state);
}
EXPORT_SYMBOL_GPL(phylink_resolve_c73);

static void phylink_decode_c37_word(struct phylink_link_state *state,
				    uint16_t config_reg, int speed)
{
	int fd_bit;

	if (speed == SPEED_2500)
		fd_bit = ETHTOOL_LINK_MODE_2500baseX_Full_BIT;
	else
		fd_bit = ETHTOOL_LINK_MODE_1000baseX_Full_BIT;

	mii_lpa_mod_linkmode_x(state->lp_advertising, config_reg, fd_bit);

	if (linkmode_test_bit(fd_bit, state->advertising) &&
	    linkmode_test_bit(fd_bit, state->lp_advertising)) {
		state->speed = speed;
		state->duplex = DUPLEX_FULL;
	} else {
		/* negotiation failure */
		state->link = false;
	}

	phylink_resolve_an_pause(state);
}

static void phylink_decode_sgmii_word(struct phylink_link_state *state,
				      uint16_t config_reg)
{
	if (!(config_reg & LPA_SGMII_LINK)) {
		state->link = false;
		return;
	}

	switch (config_reg & LPA_SGMII_SPD_MASK) {
	case LPA_SGMII_10:
		state->speed = SPEED_10;
		break;
	case LPA_SGMII_100:
		state->speed = SPEED_100;
		break;
	case LPA_SGMII_1000:
		state->speed = SPEED_1000;
		break;
	default:
		state->link = false;
		return;
	}
	if (config_reg & LPA_SGMII_FULL_DUPLEX)
		state->duplex = DUPLEX_FULL;
	else
		state->duplex = DUPLEX_HALF;
}

/**
 * phylink_decode_usxgmii_word() - decode the USXGMII word from a MAC PCS
 * @state: a pointer to a struct phylink_link_state.
 * @lpa: a 16 bit value which stores the USXGMII auto-negotiation word
 *
 * Helper for MAC PCS supporting the USXGMII protocol and the auto-negotiation
 * code word.  Decode the USXGMII code word and populate the corresponding fields
 * (speed, duplex) into the phylink_link_state structure.
 */
void phylink_decode_usxgmii_word(struct phylink_link_state *state,
				 uint16_t lpa)
{
	switch (lpa & MDIO_USXGMII_SPD_MASK) {
	case MDIO_USXGMII_10:
		state->speed = SPEED_10;
		break;
	case MDIO_USXGMII_100:
		state->speed = SPEED_100;
		break;
	case MDIO_USXGMII_1000:
		state->speed = SPEED_1000;
		break;
	case MDIO_USXGMII_2500:
		state->speed = SPEED_2500;
		break;
	case MDIO_USXGMII_5000:
		state->speed = SPEED_5000;
		break;
	case MDIO_USXGMII_10G:
		state->speed = SPEED_10000;
		break;
	default:
		state->link = false;
		return;
	}

	if (lpa & MDIO_USXGMII_FULL_DUPLEX)
		state->duplex = DUPLEX_FULL;
	else
		state->duplex = DUPLEX_HALF;
}
EXPORT_SYMBOL_GPL(phylink_decode_usxgmii_word);

/**
 * phylink_decode_usgmii_word() - decode the USGMII word from a MAC PCS
 * @state: a pointer to a struct phylink_link_state.
 * @lpa: a 16 bit value which stores the USGMII auto-negotiation word
 *
 * Helper for MAC PCS supporting the USGMII protocol and the auto-negotiation
 * code word.  Decode the USGMII code word and populate the corresponding fields
 * (speed, duplex) into the phylink_link_state structure. The structure for this
 * word is the same as the USXGMII word, except it only supports speeds up to
 * 1Gbps.
 */
static void phylink_decode_usgmii_word(struct phylink_link_state *state,
				       uint16_t lpa)
{
	switch (lpa & MDIO_USXGMII_SPD_MASK) {
	case MDIO_USXGMII_10:
		state->speed = SPEED_10;
		break;
	case MDIO_USXGMII_100:
		state->speed = SPEED_100;
		break;
	case MDIO_USXGMII_1000:
		state->speed = SPEED_1000;
		break;
	default:
		state->link = false;
		return;
	}

	if (lpa & MDIO_USXGMII_FULL_DUPLEX)
		state->duplex = DUPLEX_FULL;
	else
		state->duplex = DUPLEX_HALF;
}

/**
 * phylink_mii_c22_pcs_decode_state() - Decode MAC PCS state from MII registers
 * @state: a pointer to a &struct phylink_link_state.
 * @bmsr: The value of the %MII_BMSR register
 * @lpa: The value of the %MII_LPA register
 *
 * Helper for MAC PCS supporting the 802.3 clause 22 register set for
 * clause 37 negotiation and/or SGMII control.
 *
 * Parse the Clause 37 or Cisco SGMII link partner negotiation word into
 * the phylink @state structure. This is suitable to be used for implementing
 * the pcs_get_state() member of the struct phylink_pcs_ops structure if
 * accessing @bmsr and @lpa cannot be done with MDIO directly.
 */
void phylink_mii_c22_pcs_decode_state(struct phylink_link_state *state,
				      u16 bmsr, u16 lpa)
{
	state->link = !!(bmsr & BMSR_LSTATUS);
	state->an_complete = !!(bmsr & BMSR_ANEGCOMPLETE);
	/* If there is no link or autonegotiation is disabled, the LP advertisement
	 * data is not meaningful, so don't go any further.
	 */
	if (!state->link || !linkmode_test_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
					       state->advertising))
		return;

	switch (state->interface) {
	case PHY_INTERFACE_MODE_1000BASEX:
		phylink_decode_c37_word(state, lpa, SPEED_1000);
		break;

	case PHY_INTERFACE_MODE_2500BASEX:
		phylink_decode_c37_word(state, lpa, SPEED_2500);
		break;

	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
		phylink_decode_sgmii_word(state, lpa);
		break;
	case PHY_INTERFACE_MODE_QUSGMII:
		phylink_decode_usgmii_word(state, lpa);
		break;

	default:
		state->link = false;
		break;
	}
}
EXPORT_SYMBOL_GPL(phylink_mii_c22_pcs_decode_state);

/**
 * phylink_mii_c22_pcs_get_state() - read the MAC PCS state
 * @pcs: a pointer to a &struct mdio_device.
 * @state: a pointer to a &struct phylink_link_state.
 *
 * Helper for MAC PCS supporting the 802.3 clause 22 register set for
 * clause 37 negotiation and/or SGMII control.
 *
 * Read the MAC PCS state from the MII device configured in @config and
 * parse the Clause 37 or Cisco SGMII link partner negotiation word into
 * the phylink @state structure. This is suitable to be directly plugged
 * into the pcs_get_state() member of the struct phylink_pcs_ops
 * structure.
 */
void phylink_mii_c22_pcs_get_state(struct mdio_device *pcs,
				   struct phylink_link_state *state)
{
	int bmsr, lpa;

	bmsr = mdiodev_read(pcs, MII_BMSR);
	lpa = mdiodev_read(pcs, MII_LPA);
	if (bmsr < 0 || lpa < 0) {
		state->link = false;
		return;
	}

	phylink_mii_c22_pcs_decode_state(state, bmsr, lpa);
}
EXPORT_SYMBOL_GPL(phylink_mii_c22_pcs_get_state);

/**
 * phylink_mii_c22_pcs_encode_advertisement() - configure the clause 37 PCS
 *	advertisement
 * @interface: the PHY interface mode being configured
 * @advertising: the ethtool advertisement mask
 *
 * Helper for MAC PCS supporting the 802.3 clause 22 register set for
 * clause 37 negotiation and/or SGMII control.
 *
 * Encode the clause 37 PCS advertisement as specified by @interface and
 * @advertising.
 *
 * Return: The new value for @adv, or ``-EINVAL`` if it should not be changed.
 */
int phylink_mii_c22_pcs_encode_advertisement(phy_interface_t interface,
					     const unsigned long *advertising)
{
	u16 adv;

	switch (interface) {
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		adv = ADVERTISE_1000XFULL;
		if (linkmode_test_bit(ETHTOOL_LINK_MODE_Pause_BIT,
				      advertising))
			adv |= ADVERTISE_1000XPAUSE;
		if (linkmode_test_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT,
				      advertising))
			adv |= ADVERTISE_1000XPSE_ASYM;
		return adv;
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
		return 0x0001;
	default:
		/* Nothing to do for other modes */
		return -EINVAL;
	}
}
EXPORT_SYMBOL_GPL(phylink_mii_c22_pcs_encode_advertisement);

/**
 * phylink_mii_c22_pcs_config() - configure clause 22 PCS
 * @pcs: a pointer to a &struct mdio_device.
 * @interface: the PHY interface mode being configured
 * @advertising: the ethtool advertisement mask
 * @neg_mode: PCS negotiation mode
 *
 * Configure a Clause 22 PCS PHY with the appropriate negotiation
 * parameters for the @mode, @interface and @advertising parameters.
 * Returns negative error number on failure, zero if the advertisement
 * has not changed, or positive if there is a change.
 */
int phylink_mii_c22_pcs_config(struct mdio_device *pcs,
			       phy_interface_t interface,
			       const unsigned long *advertising,
			       unsigned int neg_mode)
{
	bool changed = 0;
	u16 bmcr;
	int ret, adv;

	adv = phylink_mii_c22_pcs_encode_advertisement(interface, advertising);
	if (adv >= 0) {
		ret = mdiobus_modify_changed(pcs->bus, pcs->addr,
					     MII_ADVERTISE, 0xffff, adv);
		if (ret < 0)
			return ret;
		changed = ret;
	}

	if (neg_mode == PHYLINK_PCS_NEG_INBAND_ENABLED)
		bmcr = BMCR_ANENABLE;
	else
		bmcr = 0;

	/* Configure the inband state. Ensure ISOLATE bit is disabled */
	ret = mdiodev_modify(pcs, MII_BMCR, BMCR_ANENABLE | BMCR_ISOLATE, bmcr);
	if (ret < 0)
		return ret;

	return changed;
}
EXPORT_SYMBOL_GPL(phylink_mii_c22_pcs_config);

/**
 * phylink_mii_c22_pcs_an_restart() - restart 802.3z autonegotiation
 * @pcs: a pointer to a &struct mdio_device.
 *
 * Helper for MAC PCS supporting the 802.3 clause 22 register set for
 * clause 37 negotiation.
 *
 * Restart the clause 37 negotiation with the link partner. This is
 * suitable to be directly plugged into the pcs_get_state() member
 * of the struct phylink_pcs_ops structure.
 */
void phylink_mii_c22_pcs_an_restart(struct mdio_device *pcs)
{
	int val = mdiodev_read(pcs, MII_BMCR);

	if (val >= 0) {
		val |= BMCR_ANRESTART;

		mdiodev_write(pcs, MII_BMCR, val);
	}
}
EXPORT_SYMBOL_GPL(phylink_mii_c22_pcs_an_restart);

void phylink_mii_c45_pcs_get_state(struct mdio_device *pcs,
				   struct phylink_link_state *state)
{
	struct mii_bus *bus = pcs->bus;
	int addr = pcs->addr;
	int stat;

	stat = mdiobus_c45_read(bus, addr, MDIO_MMD_PCS, MDIO_STAT1);
	if (stat < 0) {
		state->link = false;
		return;
	}

	state->link = !!(stat & MDIO_STAT1_LSTATUS);
	if (!state->link)
		return;

	switch (state->interface) {
	case PHY_INTERFACE_MODE_10GBASER:
		state->speed = SPEED_10000;
		state->duplex = DUPLEX_FULL;
		break;

	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(phylink_mii_c45_pcs_get_state);

static int __init phylink_init(void)
{
	for (int i = 0; i < ARRAY_SIZE(phylink_sfp_interface_preference); ++i)
		__set_bit(phylink_sfp_interface_preference[i],
			  phylink_sfp_interfaces);

	return 0;
}

module_init(phylink_init);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("phylink models the MAC to optional PHY connection");

/*
 * phylink models the MAC to optional PHY connection, supporting
 * technologies such as SFP cages where the PHY is hot-pluggable.
 *
 * Copyright (C) 2015 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
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
};

/**
 * struct phylink - internal data type for phylink
 */
struct phylink {
	/* private: */
	struct net_device *netdev;
	const struct phylink_mac_ops *ops;

	unsigned long phylink_disable_state; /* bitmask of disables */
	struct phy_device *phydev;
	phy_interface_t link_interface;	/* PHY_INTERFACE_xxx */
	u8 link_an_mode;		/* MLO_AN_xxx */
	u8 link_port;			/* The current non-phy ethtool port */
	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported);

	/* The link configuration settings */
	struct phylink_link_state link_config;
	struct gpio_desc *link_gpio;
	struct timer_list link_poll;
	void (*get_fixed_state)(struct net_device *dev,
				struct phylink_link_state *s);

	struct mutex state_mutex;
	struct phylink_link_state phy_state;
	struct work_struct resolve;

	bool mac_link_dropped;

	struct sfp_bus *sfp_bus;
};

static inline void linkmode_zero(unsigned long *dst)
{
	bitmap_zero(dst, __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static inline void linkmode_copy(unsigned long *dst, const unsigned long *src)
{
	bitmap_copy(dst, src, __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static inline void linkmode_and(unsigned long *dst, const unsigned long *a,
				const unsigned long *b)
{
	bitmap_and(dst, a, b, __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static inline void linkmode_or(unsigned long *dst, const unsigned long *a,
				const unsigned long *b)
{
	bitmap_or(dst, a, b, __ETHTOOL_LINK_MODE_MASK_NBITS);
}

static inline bool linkmode_empty(const unsigned long *src)
{
	return bitmap_empty(src, __ETHTOOL_LINK_MODE_MASK_NBITS);
}

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

	bitmap_andnot(tmp, linkmode, tmp, __ETHTOOL_LINK_MODE_MASK_NBITS);

	return linkmode_empty(tmp);
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

static int phylink_validate(struct phylink *pl, unsigned long *supported,
			    struct phylink_link_state *state)
{
	pl->ops->validate(pl->netdev, supported, state);

	return phylink_is_empty_linkmode(supported) ? -EINVAL : 0;
}

static int phylink_parse_fixedlink(struct phylink *pl,
				   struct fwnode_handle *fwnode)
{
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
		 * defining the link partner's ability. */
		if (fwnode_property_read_bool(fixed_node, "pause"))
			pl->link_config.pause |= MLO_PAUSE_SYM;
		if (fwnode_property_read_bool(fixed_node, "asym-pause"))
			pl->link_config.pause |= MLO_PAUSE_ASYM;

		if (ret == 0) {
			desc = fwnode_get_named_gpiod(fixed_node, "link-gpios",
						      0, GPIOD_IN, "?");

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
			netdev_err(pl->netdev, "broken fixed-link?\n");
			return -EINVAL;
		}

		ret = fwnode_property_read_u32_array(fwnode, "fixed-link",
						     prop, ARRAY_SIZE(prop));
		if (!ret) {
			pl->link_config.duplex = prop[1] ?
						DUPLEX_FULL : DUPLEX_HALF;
			pl->link_config.speed = prop[2];
			if (prop[3])
				pl->link_config.pause |= MLO_PAUSE_SYM;
			if (prop[4])
				pl->link_config.pause |= MLO_PAUSE_ASYM;
		}
	}

	if (pl->link_config.speed > SPEED_1000 &&
	    pl->link_config.duplex != DUPLEX_FULL)
		netdev_warn(pl->netdev, "fixed link specifies half duplex for %dMbps link?\n",
			    pl->link_config.speed);

	bitmap_fill(pl->supported, __ETHTOOL_LINK_MODE_MASK_NBITS);
	linkmode_copy(pl->link_config.advertising, pl->supported);
	phylink_validate(pl, pl->supported, &pl->link_config);

	s = phy_lookup_setting(pl->link_config.speed, pl->link_config.duplex,
			       pl->supported,
			       __ETHTOOL_LINK_MODE_MASK_NBITS, true);
	linkmode_zero(pl->supported);
	phylink_set(pl->supported, MII);
	if (s) {
		__set_bit(s->bit, pl->supported);
	} else {
		netdev_warn(pl->netdev, "fixed link %s duplex %dMbps not recognised\n",
			    pl->link_config.duplex == DUPLEX_FULL ? "full" : "half",
			    pl->link_config.speed);
	}

	linkmode_and(pl->link_config.advertising, pl->link_config.advertising,
		     pl->supported);

	pl->link_config.link = 1;
	pl->link_config.an_complete = 1;

	return 0;
}

static int phylink_parse_mode(struct phylink *pl, struct fwnode_handle *fwnode)
{
	struct fwnode_handle *dn;
	const char *managed;

	dn = fwnode_get_named_child_node(fwnode, "fixed-link");
	if (dn || fwnode_property_present(fwnode, "fixed-link"))
		pl->link_an_mode = MLO_AN_FIXED;
	fwnode_handle_put(dn);

	if (fwnode_property_read_string(fwnode, "managed", &managed) == 0 &&
	    strcmp(managed, "in-band-status") == 0) {
		if (pl->link_an_mode == MLO_AN_FIXED) {
			netdev_err(pl->netdev,
				   "can't use both fixed-link and in-band-status\n");
			return -EINVAL;
		}

		linkmode_zero(pl->supported);
		phylink_set(pl->supported, MII);
		phylink_set(pl->supported, Autoneg);
		phylink_set(pl->supported, Asym_Pause);
		phylink_set(pl->supported, Pause);
		pl->link_config.an_enabled = true;
		pl->link_an_mode = MLO_AN_INBAND;

		switch (pl->link_config.interface) {
		case PHY_INTERFACE_MODE_SGMII:
			phylink_set(pl->supported, 10baseT_Half);
			phylink_set(pl->supported, 10baseT_Full);
			phylink_set(pl->supported, 100baseT_Half);
			phylink_set(pl->supported, 100baseT_Full);
			phylink_set(pl->supported, 1000baseT_Half);
			phylink_set(pl->supported, 1000baseT_Full);
			break;

		case PHY_INTERFACE_MODE_1000BASEX:
			phylink_set(pl->supported, 1000baseX_Full);
			break;

		case PHY_INTERFACE_MODE_2500BASEX:
			phylink_set(pl->supported, 2500baseX_Full);
			break;

		case PHY_INTERFACE_MODE_10GKR:
			phylink_set(pl->supported, 10baseT_Half);
			phylink_set(pl->supported, 10baseT_Full);
			phylink_set(pl->supported, 100baseT_Half);
			phylink_set(pl->supported, 100baseT_Full);
			phylink_set(pl->supported, 1000baseT_Half);
			phylink_set(pl->supported, 1000baseT_Full);
			phylink_set(pl->supported, 1000baseX_Full);
			phylink_set(pl->supported, 10000baseKR_Full);
			phylink_set(pl->supported, 10000baseCR_Full);
			phylink_set(pl->supported, 10000baseSR_Full);
			phylink_set(pl->supported, 10000baseLR_Full);
			phylink_set(pl->supported, 10000baseLRM_Full);
			phylink_set(pl->supported, 10000baseER_Full);
			break;

		default:
			netdev_err(pl->netdev,
				   "incorrect link mode %s for in-band status\n",
				   phy_modes(pl->link_config.interface));
			return -EINVAL;
		}

		linkmode_copy(pl->link_config.advertising, pl->supported);

		if (phylink_validate(pl, pl->supported, &pl->link_config)) {
			netdev_err(pl->netdev,
				   "failed to validate link configuration for in-band status\n");
			return -EINVAL;
		}
	}

	return 0;
}

static void phylink_mac_config(struct phylink *pl,
			       const struct phylink_link_state *state)
{
	netdev_dbg(pl->netdev,
		   "%s: mode=%s/%s/%s/%s adv=%*pb pause=%02x link=%u an=%u\n",
		   __func__, phylink_an_mode_str(pl->link_an_mode),
		   phy_modes(state->interface),
		   phy_speed_to_str(state->speed),
		   phy_duplex_to_str(state->duplex),
		   __ETHTOOL_LINK_MODE_MASK_NBITS, state->advertising,
		   state->pause, state->link, state->an_enabled);

	pl->ops->mac_config(pl->netdev, pl->link_an_mode, state);
}

static void phylink_mac_an_restart(struct phylink *pl)
{
	if (pl->link_config.an_enabled &&
	    phy_interface_mode_is_8023z(pl->link_config.interface))
		pl->ops->mac_an_restart(pl->netdev);
}

static int phylink_get_mac_state(struct phylink *pl, struct phylink_link_state *state)
{
	struct net_device *ndev = pl->netdev;

	linkmode_copy(state->advertising, pl->link_config.advertising);
	linkmode_zero(state->lp_advertising);
	state->interface = pl->link_config.interface;
	state->an_enabled = pl->link_config.an_enabled;
	state->link = 1;

	return pl->ops->mac_link_state(ndev, state);
}

/* The fixed state is... fixed except for the link state,
 * which may be determined by a GPIO or a callback.
 */
static void phylink_get_fixed_state(struct phylink *pl, struct phylink_link_state *state)
{
	*state = pl->link_config;
	if (pl->get_fixed_state)
		pl->get_fixed_state(pl->netdev, state);
	else if (pl->link_gpio)
		state->link = !!gpiod_get_value_cansleep(pl->link_gpio);
}

/* Flow control is resolved according to our and the link partners
 * advertisements using the following drawn from the 802.3 specs:
 *  Local device  Link partner
 *  Pause AsymDir Pause AsymDir Result
 *    1     X       1     X     TX+RX
 *    0     1       1     1     RX
 *    1     1       0     1     TX
 */
static void phylink_resolve_flow(struct phylink *pl,
				 struct phylink_link_state *state)
{
	int new_pause = 0;

	if (pl->link_config.pause & MLO_PAUSE_AN) {
		int pause = 0;

		if (phylink_test(pl->link_config.advertising, Pause))
			pause |= MLO_PAUSE_SYM;
		if (phylink_test(pl->link_config.advertising, Asym_Pause))
			pause |= MLO_PAUSE_ASYM;

		pause &= state->pause;

		if (pause & MLO_PAUSE_SYM)
			new_pause = MLO_PAUSE_TX | MLO_PAUSE_RX;
		else if (pause & MLO_PAUSE_ASYM)
			new_pause = state->pause & MLO_PAUSE_SYM ?
				 MLO_PAUSE_RX : MLO_PAUSE_TX;
	} else {
		new_pause = pl->link_config.pause & MLO_PAUSE_TXRX_MASK;
	}

	state->pause &= ~MLO_PAUSE_TXRX_MASK;
	state->pause |= new_pause;
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

static void phylink_resolve(struct work_struct *w)
{
	struct phylink *pl = container_of(w, struct phylink, resolve);
	struct phylink_link_state link_state;
	struct net_device *ndev = pl->netdev;

	mutex_lock(&pl->state_mutex);
	if (pl->phylink_disable_state) {
		pl->mac_link_dropped = false;
		link_state.link = false;
	} else if (pl->mac_link_dropped) {
		link_state.link = false;
	} else {
		switch (pl->link_an_mode) {
		case MLO_AN_PHY:
			link_state = pl->phy_state;
			phylink_resolve_flow(pl, &link_state);
			phylink_mac_config(pl, &link_state);
			break;

		case MLO_AN_FIXED:
			phylink_get_fixed_state(pl, &link_state);
			phylink_mac_config(pl, &link_state);
			break;

		case MLO_AN_INBAND:
			phylink_get_mac_state(pl, &link_state);
			if (pl->phydev) {
				bool changed = false;

				link_state.link = link_state.link &&
						  pl->phy_state.link;

				if (pl->phy_state.interface !=
				    link_state.interface) {
					link_state.interface = pl->phy_state.interface;
					changed = true;
				}

				/* Propagate the flow control from the PHY
				 * to the MAC. Also propagate the interface
				 * if changed.
				 */
				if (pl->phy_state.link || changed) {
					link_state.pause |= pl->phy_state.pause;
					phylink_resolve_flow(pl, &link_state);

					phylink_mac_config(pl, &link_state);
				}
			}
			break;
		}
	}

	if (link_state.link != netif_carrier_ok(ndev)) {
		if (!link_state.link) {
			netif_carrier_off(ndev);
			pl->ops->mac_link_down(ndev, pl->link_an_mode,
					       pl->phy_state.interface);
			netdev_info(ndev, "Link is Down\n");
		} else {
			pl->ops->mac_link_up(ndev, pl->link_an_mode,
					     pl->phy_state.interface,
					     pl->phydev);

			netif_carrier_on(ndev);

			netdev_info(ndev,
				    "Link is Up - %s/%s - flow control %s\n",
				    phy_speed_to_str(link_state.speed),
				    phy_duplex_to_str(link_state.duplex),
				    phylink_pause_to_str(link_state.pause));
		}
	}
	if (!link_state.link && pl->mac_link_dropped) {
		pl->mac_link_dropped = false;
		queue_work(system_power_efficient_wq, &pl->resolve);
	}
	mutex_unlock(&pl->state_mutex);
}

static void phylink_run_resolve(struct phylink *pl)
{
	if (!pl->phylink_disable_state)
		queue_work(system_power_efficient_wq, &pl->resolve);
}

static void phylink_fixed_poll(struct timer_list *t)
{
	struct phylink *pl = container_of(t, struct phylink, link_poll);

	mod_timer(t, jiffies + HZ);

	phylink_run_resolve(pl);
}

static const struct sfp_upstream_ops sfp_phylink_ops;

static int phylink_register_sfp(struct phylink *pl,
				struct fwnode_handle *fwnode)
{
	struct fwnode_reference_args ref;
	int ret;

	if (!fwnode)
		return 0;

	ret = fwnode_property_get_reference_args(fwnode, "sfp", NULL,
						 0, 0, &ref);
	if (ret < 0) {
		if (ret == -ENOENT)
			return 0;

		netdev_err(pl->netdev, "unable to parse \"sfp\" node: %d\n",
			   ret);
		return ret;
	}

	pl->sfp_bus = sfp_register_upstream(ref.fwnode, pl->netdev, pl,
					    &sfp_phylink_ops);
	if (!pl->sfp_bus)
		return -ENOMEM;

	return 0;
}

/**
 * phylink_create() - create a phylink instance
 * @ndev: a pointer to the &struct net_device
 * @fwnode: a pointer to a &struct fwnode_handle describing the network
 *	interface
 * @iface: the desired link mode defined by &typedef phy_interface_t
 * @ops: a pointer to a &struct phylink_mac_ops for the MAC.
 *
 * Create a new phylink instance, and parse the link parameters found in @np.
 * This will parse in-band modes, fixed-link or SFP configuration.
 *
 * Returns a pointer to a &struct phylink, or an error-pointer value. Users
 * must use IS_ERR() to check for errors from this function.
 */
struct phylink *phylink_create(struct net_device *ndev,
			       struct fwnode_handle *fwnode,
			       phy_interface_t iface,
			       const struct phylink_mac_ops *ops)
{
	struct phylink *pl;
	int ret;

	pl = kzalloc(sizeof(*pl), GFP_KERNEL);
	if (!pl)
		return ERR_PTR(-ENOMEM);

	mutex_init(&pl->state_mutex);
	INIT_WORK(&pl->resolve, phylink_resolve);
	pl->netdev = ndev;
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
	pl->link_config.an_enabled = true;
	pl->ops = ops;
	__set_bit(PHYLINK_DISABLE_STOPPED, &pl->phylink_disable_state);
	timer_setup(&pl->link_poll, phylink_fixed_poll, 0);

	bitmap_fill(pl->supported, __ETHTOOL_LINK_MODE_MASK_NBITS);
	linkmode_copy(pl->link_config.advertising, pl->supported);
	phylink_validate(pl, pl->supported, &pl->link_config);

	ret = phylink_parse_mode(pl, fwnode);
	if (ret < 0) {
		kfree(pl);
		return ERR_PTR(ret);
	}

	if (pl->link_an_mode == MLO_AN_FIXED) {
		ret = phylink_parse_fixedlink(pl, fwnode);
		if (ret < 0) {
			kfree(pl);
			return ERR_PTR(ret);
		}
	}

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
 */
void phylink_destroy(struct phylink *pl)
{
	if (pl->sfp_bus)
		sfp_unregister_upstream(pl->sfp_bus);
	if (!IS_ERR_OR_NULL(pl->link_gpio))
		gpiod_put(pl->link_gpio);

	cancel_work_sync(&pl->resolve);
	kfree(pl);
}
EXPORT_SYMBOL_GPL(phylink_destroy);

static void phylink_phy_change(struct phy_device *phydev, bool up,
			       bool do_carrier)
{
	struct phylink *pl = phydev->phylink;

	mutex_lock(&pl->state_mutex);
	pl->phy_state.speed = phydev->speed;
	pl->phy_state.duplex = phydev->duplex;
	pl->phy_state.pause = MLO_PAUSE_NONE;
	if (phydev->pause)
		pl->phy_state.pause |= MLO_PAUSE_SYM;
	if (phydev->asym_pause)
		pl->phy_state.pause |= MLO_PAUSE_ASYM;
	pl->phy_state.interface = phydev->interface;
	pl->phy_state.link = up;
	mutex_unlock(&pl->state_mutex);

	phylink_run_resolve(pl);

	netdev_dbg(pl->netdev, "phy link %s %s/%s/%s\n", up ? "up" : "down",
		   phy_modes(phydev->interface),
		   phy_speed_to_str(phydev->speed),
		   phy_duplex_to_str(phydev->duplex));
}

static int phylink_bringup_phy(struct phylink *pl, struct phy_device *phy)
{
	struct phylink_link_state config;
	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported);
	u32 advertising;
	int ret;

	memset(&config, 0, sizeof(config));
	ethtool_convert_legacy_u32_to_link_mode(supported, phy->supported);
	ethtool_convert_legacy_u32_to_link_mode(config.advertising,
						phy->advertising);
	config.interface = pl->link_config.interface;

	/*
	 * This is the new way of dealing with flow control for PHYs,
	 * as described by Timur Tabi in commit 529ed1275263 ("net: phy:
	 * phy drivers should not set SUPPORTED_[Asym_]Pause") except
	 * using our validate call to the MAC, we rely upon the MAC
	 * clearing the bits from both supported and advertising fields.
	 */
	if (phylink_test(supported, Pause))
		phylink_set(config.advertising, Pause);
	if (phylink_test(supported, Asym_Pause))
		phylink_set(config.advertising, Asym_Pause);

	ret = phylink_validate(pl, supported, &config);
	if (ret)
		return ret;

	phy->phylink = pl;
	phy->phy_link_change = phylink_phy_change;

	netdev_info(pl->netdev,
		    "PHY [%s] driver [%s]\n", dev_name(&phy->mdio.dev),
		    phy->drv->name);

	mutex_lock(&phy->lock);
	mutex_lock(&pl->state_mutex);
	pl->phydev = phy;
	linkmode_copy(pl->supported, supported);
	linkmode_copy(pl->link_config.advertising, config.advertising);

	/* Restrict the phy advertisement according to the MAC support. */
	ethtool_convert_link_mode_to_legacy_u32(&advertising, config.advertising);
	phy->advertising = advertising;
	mutex_unlock(&pl->state_mutex);
	mutex_unlock(&phy->lock);

	netdev_dbg(pl->netdev,
		   "phy: setting supported %*pb advertising 0x%08x\n",
		   __ETHTOOL_LINK_MODE_MASK_NBITS, pl->supported,
		   phy->advertising);

	phy_start_machine(phy);
	if (phy->irq > 0)
		phy_start_interrupts(phy);

	return 0;
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

	if (WARN_ON(pl->link_an_mode == MLO_AN_FIXED ||
		    (pl->link_an_mode == MLO_AN_INBAND &&
		     phy_interface_mode_is_8023z(pl->link_interface))))
		return -EINVAL;

	if (pl->phydev)
		return -EBUSY;

	/* Use PHY device/driver interface */
	if (pl->link_interface == PHY_INTERFACE_MODE_NA) {
		pl->link_interface = phy->interface;
		pl->link_config.interface = pl->link_interface;
	}

	ret = phy_attach_direct(pl->netdev, phy, 0, pl->link_interface);
	if (ret)
		return ret;

	ret = phylink_bringup_phy(pl, phy);
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
	struct device_node *phy_node;
	struct phy_device *phy_dev;
	int ret;

	/* Fixed links and 802.3z are handled without needing a PHY */
	if (pl->link_an_mode == MLO_AN_FIXED ||
	    (pl->link_an_mode == MLO_AN_INBAND &&
	     phy_interface_mode_is_8023z(pl->link_interface)))
		return 0;

	phy_node = of_parse_phandle(dn, "phy-handle", 0);
	if (!phy_node)
		phy_node = of_parse_phandle(dn, "phy", 0);
	if (!phy_node)
		phy_node = of_parse_phandle(dn, "phy-device", 0);

	if (!phy_node) {
		if (pl->link_an_mode == MLO_AN_PHY)
			return -ENODEV;
		return 0;
	}

	phy_dev = of_phy_attach(pl->netdev, phy_node, flags,
				pl->link_interface);
	/* We're done with the phy_node handle */
	of_node_put(phy_node);

	if (!phy_dev)
		return -ENODEV;

	ret = phylink_bringup_phy(pl, phy_dev);
	if (ret)
		phy_detach(phy_dev);

	return ret;
}
EXPORT_SYMBOL_GPL(phylink_of_phy_connect);

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

/**
 * phylink_fixed_state_cb() - allow setting a fixed link callback
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @cb: callback to execute to determine the fixed link state.
 *
 * The MAC driver should call this driver when the state of its link
 * can be determined through e.g: an out of band MMIO register.
 */
int phylink_fixed_state_cb(struct phylink *pl,
			   void (*cb)(struct net_device *dev,
				      struct phylink_link_state *state))
{
	/* It does not make sense to let the link be overriden unless we use
	 * MLO_AN_FIXED
	 */
	if (pl->link_an_mode != MLO_AN_FIXED)
		return -EINVAL;

	mutex_lock(&pl->state_mutex);
	pl->get_fixed_state = cb;
	mutex_unlock(&pl->state_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(phylink_fixed_state_cb);

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
	if (!up)
		pl->mac_link_dropped = true;
	phylink_run_resolve(pl);
	netdev_dbg(pl->netdev, "mac link %s\n", up ? "up" : "down");
}
EXPORT_SYMBOL_GPL(phylink_mac_change);

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
	ASSERT_RTNL();

	netdev_info(pl->netdev, "configuring for %s/%s link mode\n",
		    phylink_an_mode_str(pl->link_an_mode),
		    phy_modes(pl->link_config.interface));

	/* Apply the link configuration to the MAC when starting. This allows
	 * a fixed-link to start with the correct parameters, and also
	 * ensures that we set the appropriate advertisement for Serdes links.
	 */
	phylink_resolve_flow(pl, &pl->link_config);
	phylink_mac_config(pl, &pl->link_config);

	/* Restart autonegotiation if using 802.3z to ensure that the link
	 * parameters are properly negotiated.  This is necessary for DSA
	 * switches using 802.3z negotiation to ensure they see our modes.
	 */
	phylink_mac_an_restart(pl);

	clear_bit(PHYLINK_DISABLE_STOPPED, &pl->phylink_disable_state);
	phylink_run_resolve(pl);

	if (pl->link_an_mode == MLO_AN_FIXED && !IS_ERR(pl->link_gpio))
		mod_timer(&pl->link_poll, jiffies + HZ);
	if (pl->sfp_bus)
		sfp_upstream_start(pl->sfp_bus);
	if (pl->phydev)
		phy_start(pl->phydev);
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
 */
void phylink_stop(struct phylink *pl)
{
	ASSERT_RTNL();

	if (pl->phydev)
		phy_stop(pl->phydev);
	if (pl->sfp_bus)
		sfp_upstream_stop(pl->sfp_bus);
	if (pl->link_an_mode == MLO_AN_FIXED && !IS_ERR(pl->link_gpio))
		del_timer_sync(&pl->link_poll);

	set_bit(PHYLINK_DISABLE_STOPPED, &pl->phylink_disable_state);
	queue_work(system_power_efficient_wq, &pl->resolve);
	flush_work(&pl->resolve);
}
EXPORT_SYMBOL_GPL(phylink_stop);

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
	kset->base.speed = state->speed;
	kset->base.duplex = state->duplex;
	kset->base.autoneg = state->an_enabled ? AUTONEG_ENABLE :
				AUTONEG_DISABLE;
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

	if (pl->phydev) {
		phy_ethtool_ksettings_get(pl->phydev, kset);
	} else {
		kset->base.port = pl->link_port;
	}

	linkmode_copy(kset->link_modes.supported, pl->supported);

	switch (pl->link_an_mode) {
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

		phylink_get_mac_state(pl, &link_state);

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

/**
 * phylink_ethtool_ksettings_set() - set the link settings
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @kset: a pointer to a &struct ethtool_link_ksettings for the desired modes
 */
int phylink_ethtool_ksettings_set(struct phylink *pl,
				  const struct ethtool_link_ksettings *kset)
{
	struct ethtool_link_ksettings our_kset;
	struct phylink_link_state config;
	int ret;

	ASSERT_RTNL();

	if (kset->base.autoneg != AUTONEG_DISABLE &&
	    kset->base.autoneg != AUTONEG_ENABLE)
		return -EINVAL;

	config = pl->link_config;

	/* Mask out unsupported advertisements */
	linkmode_and(config.advertising, kset->link_modes.advertising,
		     pl->supported);

	/* FIXME: should we reject autoneg if phy/mac does not support it? */
	if (kset->base.autoneg == AUTONEG_DISABLE) {
		const struct phy_setting *s;

		/* Autonegotiation disabled, select a suitable speed and
		 * duplex.
		 */
		s = phy_lookup_setting(kset->base.speed, kset->base.duplex,
				       pl->supported,
				       __ETHTOOL_LINK_MODE_MASK_NBITS, false);
		if (!s)
			return -EINVAL;

		/* If we have a fixed link (as specified by firmware), refuse
		 * to change link parameters.
		 */
		if (pl->link_an_mode == MLO_AN_FIXED &&
		    (s->speed != pl->link_config.speed ||
		     s->duplex != pl->link_config.duplex))
			return -EINVAL;

		config.speed = s->speed;
		config.duplex = s->duplex;
		config.an_enabled = false;

		__clear_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, config.advertising);
	} else {
		/* If we have a fixed link, refuse to enable autonegotiation */
		if (pl->link_an_mode == MLO_AN_FIXED)
			return -EINVAL;

		config.speed = SPEED_UNKNOWN;
		config.duplex = DUPLEX_UNKNOWN;
		config.an_enabled = true;

		__set_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, config.advertising);
	}

	if (phylink_validate(pl, pl->supported, &config))
		return -EINVAL;

	/* If autonegotiation is enabled, we must have an advertisement */
	if (config.an_enabled && phylink_is_empty_linkmode(config.advertising))
		return -EINVAL;

	our_kset = *kset;
	linkmode_copy(our_kset.link_modes.advertising, config.advertising);
	our_kset.base.speed = config.speed;
	our_kset.base.duplex = config.duplex;

	/* If we have a PHY, configure the phy */
	if (pl->phydev) {
		ret = phy_ethtool_ksettings_set(pl->phydev, &our_kset);
		if (ret)
			return ret;
	}

	mutex_lock(&pl->state_mutex);
	/* Configure the MAC to match the new settings */
	linkmode_copy(pl->link_config.advertising, our_kset.link_modes.advertising);
	pl->link_config.interface = config.interface;
	pl->link_config.speed = our_kset.base.speed;
	pl->link_config.duplex = our_kset.base.duplex;
	pl->link_config.an_enabled = our_kset.base.autoneg != AUTONEG_DISABLE;

	if (!test_bit(PHYLINK_DISABLE_STOPPED, &pl->phylink_disable_state)) {
		phylink_mac_config(pl, &pl->link_config);
		phylink_mac_an_restart(pl);
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
	phylink_mac_an_restart(pl);

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

	ASSERT_RTNL();

	if (!phylink_test(pl->supported, Pause) &&
	    !phylink_test(pl->supported, Asym_Pause))
		return -EOPNOTSUPP;

	if (!phylink_test(pl->supported, Asym_Pause) &&
	    !pause->autoneg && pause->rx_pause != pause->tx_pause)
		return -EINVAL;

	config->pause &= ~(MLO_PAUSE_AN | MLO_PAUSE_TXRX_MASK);

	if (pause->autoneg)
		config->pause |= MLO_PAUSE_AN;
	if (pause->rx_pause)
		config->pause |= MLO_PAUSE_RX;
	if (pause->tx_pause)
		config->pause |= MLO_PAUSE_TX;

	if (!test_bit(PHYLINK_DISABLE_STOPPED, &pl->phylink_disable_state)) {
		switch (pl->link_an_mode) {
		case MLO_AN_PHY:
			/* Silently mark the carrier down, and then trigger a resolve */
			netif_carrier_off(pl->netdev);
			phylink_run_resolve(pl);
			break;

		case MLO_AN_FIXED:
			/* Should we allow fixed links to change against the config? */
			phylink_resolve_flow(pl, config);
			phylink_mac_config(pl, config);
			break;

		case MLO_AN_INBAND:
			phylink_mac_config(pl, config);
			phylink_mac_an_restart(pl);
			break;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(phylink_ethtool_set_pauseparam);

/**
 * phylink_ethtool_get_eee_err() - read the energy efficient ethernet error
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
 * phylink_ethtool_get_eee() - read the energy efficient ethernet parameters
 * @pl: a pointer to a &struct phylink returned from phylink_create()
 * @eee: a pointer to a &struct ethtool_eee for the read parameters
 */
int phylink_ethtool_get_eee(struct phylink *pl, struct ethtool_eee *eee)
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
 * @eee: a pointer to a &struct ethtool_eee for the desired parameters
 */
int phylink_ethtool_set_eee(struct phylink *pl, struct ethtool_eee *eee)
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
static int phylink_mii_emul_read(struct net_device *ndev, unsigned int reg,
				 struct phylink_link_state *state, bool aneg)
{
	struct fixed_phy_status fs;
	int val;

	fs.link = state->link;
	fs.speed = state->speed;
	fs.duplex = state->duplex;
	fs.pause = state->pause & MLO_PAUSE_SYM;
	fs.asym_pause = state->pause & MLO_PAUSE_ASYM;

	val = swphy_read_reg(reg, &fs);
	if (reg == MII_BMSR) {
		if (!state->an_complete)
			val &= ~BMSR_ANEGCOMPLETE;
		if (!aneg)
			val &= ~BMSR_ANEGCAPABLE;
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
		devad = MII_ADDR_C45 | devad << 16 | reg;
	} else if (phydev->is_c45) {
		switch (reg) {
		case MII_BMCR:
		case MII_BMSR:
		case MII_PHYSID1:
		case MII_PHYSID2:
			devad = __ffs(phydev->c45_ids.devices_in_package);
			break;
		case MII_ADVERTISE:
		case MII_LPA:
			if (!(phydev->c45_ids.devices_in_package & MDIO_DEVS_AN))
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
		devad = MII_ADDR_C45 | devad << 16 | reg;
	} else {
		prtad = phy_id;
		devad = reg;
	}
	return mdiobus_read(pl->phydev->mdio.bus, prtad, devad);
}

static int phylink_phy_write(struct phylink *pl, unsigned int phy_id,
			     unsigned int reg, unsigned int val)
{
	struct phy_device *phydev = pl->phydev;
	int prtad, devad;

	if (mdio_phy_id_is_c45(phy_id)) {
		prtad = mdio_phy_id_prtad(phy_id);
		devad = mdio_phy_id_devad(phy_id);
		devad = MII_ADDR_C45 | devad << 16 | reg;
	} else if (phydev->is_c45) {
		switch (reg) {
		case MII_BMCR:
		case MII_BMSR:
		case MII_PHYSID1:
		case MII_PHYSID2:
			devad = __ffs(phydev->c45_ids.devices_in_package);
			break;
		case MII_ADVERTISE:
		case MII_LPA:
			if (!(phydev->c45_ids.devices_in_package & MDIO_DEVS_AN))
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
		devad = MII_ADDR_C45 | devad << 16 | reg;
	} else {
		prtad = phy_id;
		devad = reg;
	}

	return mdiobus_write(phydev->mdio.bus, prtad, devad, val);
}

static int phylink_mii_read(struct phylink *pl, unsigned int phy_id,
			    unsigned int reg)
{
	struct phylink_link_state state;
	int val = 0xffff;

	switch (pl->link_an_mode) {
	case MLO_AN_FIXED:
		if (phy_id == 0) {
			phylink_get_fixed_state(pl, &state);
			val = phylink_mii_emul_read(pl->netdev, reg, &state,
						    true);
		}
		break;

	case MLO_AN_PHY:
		return -EOPNOTSUPP;

	case MLO_AN_INBAND:
		if (phy_id == 0) {
			val = phylink_get_mac_state(pl, &state);
			if (val < 0)
				return val;

			val = phylink_mii_emul_read(pl->netdev, reg, &state,
						    true);
		}
		break;
	}

	return val & 0xffff;
}

static int phylink_mii_write(struct phylink *pl, unsigned int phy_id,
			     unsigned int reg, unsigned int val)
{
	switch (pl->link_an_mode) {
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
			/* fall through */

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
			/* fall through */

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

static int phylink_sfp_module_insert(void *upstream,
				     const struct sfp_eeprom_id *id)
{
	struct phylink *pl = upstream;
	__ETHTOOL_DECLARE_LINK_MODE_MASK(support) = { 0, };
	struct phylink_link_state config;
	phy_interface_t iface;
	int ret = 0;
	bool changed;
	u8 port;

	ASSERT_RTNL();

	sfp_parse_support(pl->sfp_bus, id, support);
	port = sfp_parse_port(pl->sfp_bus, id, support);

	memset(&config, 0, sizeof(config));
	linkmode_copy(config.advertising, support);
	config.interface = PHY_INTERFACE_MODE_NA;
	config.speed = SPEED_UNKNOWN;
	config.duplex = DUPLEX_UNKNOWN;
	config.pause = MLO_PAUSE_AN;
	config.an_enabled = pl->link_config.an_enabled;

	/* Ignore errors if we're expecting a PHY to attach later */
	ret = phylink_validate(pl, support, &config);
	if (ret) {
		netdev_err(pl->netdev, "validation with support %*pb failed: %d\n",
			   __ETHTOOL_LINK_MODE_MASK_NBITS, support, ret);
		return ret;
	}

	iface = sfp_select_interface(pl->sfp_bus, id, config.advertising);
	if (iface == PHY_INTERFACE_MODE_NA) {
		netdev_err(pl->netdev,
			   "selection of interface failed, advertisement %*pb\n",
			   __ETHTOOL_LINK_MODE_MASK_NBITS, config.advertising);
		return -EINVAL;
	}

	config.interface = iface;
	ret = phylink_validate(pl, support, &config);
	if (ret) {
		netdev_err(pl->netdev, "validation of %s/%s with support %*pb failed: %d\n",
			   phylink_an_mode_str(MLO_AN_INBAND),
			   phy_modes(config.interface),
			   __ETHTOOL_LINK_MODE_MASK_NBITS, support, ret);
		return ret;
	}

	netdev_dbg(pl->netdev, "requesting link mode %s/%s with support %*pb\n",
		   phylink_an_mode_str(MLO_AN_INBAND),
		   phy_modes(config.interface),
		   __ETHTOOL_LINK_MODE_MASK_NBITS, support);

	if (phy_interface_mode_is_8023z(iface) && pl->phydev)
		return -EINVAL;

	changed = !bitmap_equal(pl->supported, support,
				__ETHTOOL_LINK_MODE_MASK_NBITS);
	if (changed) {
		linkmode_copy(pl->supported, support);
		linkmode_copy(pl->link_config.advertising, config.advertising);
	}

	if (pl->link_an_mode != MLO_AN_INBAND ||
	    pl->link_config.interface != config.interface) {
		pl->link_config.interface = config.interface;
		pl->link_an_mode = MLO_AN_INBAND;

		changed = true;

		netdev_info(pl->netdev, "switched to %s/%s link mode\n",
			    phylink_an_mode_str(MLO_AN_INBAND),
			    phy_modes(config.interface));
	}

	pl->link_port = port;

	if (changed && !test_bit(PHYLINK_DISABLE_STOPPED,
				 &pl->phylink_disable_state))
		phylink_mac_config(pl, &pl->link_config);

	return ret;
}

static void phylink_sfp_link_down(void *upstream)
{
	struct phylink *pl = upstream;

	ASSERT_RTNL();

	set_bit(PHYLINK_DISABLE_LINK, &pl->phylink_disable_state);
	queue_work(system_power_efficient_wq, &pl->resolve);
	flush_work(&pl->resolve);
}

static void phylink_sfp_link_up(void *upstream)
{
	struct phylink *pl = upstream;

	ASSERT_RTNL();

	clear_bit(PHYLINK_DISABLE_LINK, &pl->phylink_disable_state);
	phylink_run_resolve(pl);
}

static int phylink_sfp_connect_phy(void *upstream, struct phy_device *phy)
{
	return phylink_connect_phy(upstream, phy);
}

static void phylink_sfp_disconnect_phy(void *upstream)
{
	phylink_disconnect_phy(upstream);
}

static const struct sfp_upstream_ops sfp_phylink_ops = {
	.module_insert = phylink_sfp_module_insert,
	.link_up = phylink_sfp_link_up,
	.link_down = phylink_sfp_link_down,
	.connect_phy = phylink_sfp_connect_phy,
	.disconnect_phy = phylink_sfp_disconnect_phy,
};

MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0+
/* Framework for configuring and reading PHY devices
 * Based on code in sungem_phy.c and gianfar_phy.c
 *
 * Author: Andy Fleming
 *
 * Copyright (c) 2004 Freescale Semiconductor, Inc.
 * Copyright (c) 2006, 2007  Maciej W. Rozycki
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/ethtool_netlink.h>
#include <linux/phy.h>
#include <linux/phy_led_triggers.h>
#include <linux/sfp.h>
#include <linux/workqueue.h>
#include <linux/mdio.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/suspend.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <net/sock.h>

#include "phylib-internal.h"
#include "phy-caps.h"

#define PHY_STATE_TIME	HZ

#define PHY_STATE_STR(_state)			\
	case PHY_##_state:			\
		return __stringify(_state);	\

static const char *phy_state_to_str(enum phy_state st)
{
	switch (st) {
	PHY_STATE_STR(DOWN)
	PHY_STATE_STR(READY)
	PHY_STATE_STR(UP)
	PHY_STATE_STR(RUNNING)
	PHY_STATE_STR(NOLINK)
	PHY_STATE_STR(CABLETEST)
	PHY_STATE_STR(HALTED)
	PHY_STATE_STR(ERROR)
	}

	return NULL;
}

static void phy_process_state_change(struct phy_device *phydev,
				     enum phy_state old_state)
{
	if (old_state != phydev->state) {
		phydev_dbg(phydev, "PHY state change %s -> %s\n",
			   phy_state_to_str(old_state),
			   phy_state_to_str(phydev->state));
		if (phydev->drv && phydev->drv->link_change_notify)
			phydev->drv->link_change_notify(phydev);
	}
}

static void phy_link_up(struct phy_device *phydev)
{
	phydev->phy_link_change(phydev, true);
	phy_led_trigger_change_speed(phydev);
}

static void phy_link_down(struct phy_device *phydev)
{
	phydev->phy_link_change(phydev, false);
	phy_led_trigger_change_speed(phydev);
	WRITE_ONCE(phydev->link_down_events, phydev->link_down_events + 1);
}

static const char *phy_pause_str(struct phy_device *phydev)
{
	bool local_pause, local_asym_pause;

	if (phydev->autoneg == AUTONEG_DISABLE)
		goto no_pause;

	local_pause = linkmode_test_bit(ETHTOOL_LINK_MODE_Pause_BIT,
					phydev->advertising);
	local_asym_pause = linkmode_test_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT,
					     phydev->advertising);

	if (local_pause && phydev->pause)
		return "rx/tx";

	if (local_asym_pause && phydev->asym_pause) {
		if (local_pause)
			return "rx";
		if (phydev->pause)
			return "tx";
	}

no_pause:
	return "off";
}

/**
 * phy_print_status - Convenience function to print out the current phy status
 * @phydev: the phy_device struct
 */
void phy_print_status(struct phy_device *phydev)
{
	if (phydev->link) {
		netdev_info(phydev->attached_dev,
			"Link is Up - %s/%s %s- flow control %s\n",
			phy_speed_to_str(phydev->speed),
			phy_duplex_to_str(phydev->duplex),
			phydev->downshifted_rate ? "(downshifted) " : "",
			phy_pause_str(phydev));
	} else	{
		netdev_info(phydev->attached_dev, "Link is Down\n");
	}
}
EXPORT_SYMBOL(phy_print_status);

/**
 * phy_get_rate_matching - determine if rate matching is supported
 * @phydev: The phy device to return rate matching for
 * @iface: The interface mode to use
 *
 * This determines the type of rate matching (if any) that @phy supports
 * using @iface. @iface may be %PHY_INTERFACE_MODE_NA to determine if any
 * interface supports rate matching.
 *
 * Return: The type of rate matching @phy supports for @iface, or
 *         %RATE_MATCH_NONE.
 */
int phy_get_rate_matching(struct phy_device *phydev,
			  phy_interface_t iface)
{
	int ret = RATE_MATCH_NONE;

	if (phydev->drv->get_rate_matching) {
		mutex_lock(&phydev->lock);
		ret = phydev->drv->get_rate_matching(phydev, iface);
		mutex_unlock(&phydev->lock);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(phy_get_rate_matching);

/**
 * phy_config_interrupt - configure the PHY device for the requested interrupts
 * @phydev: the phy_device struct
 * @interrupts: interrupt flags to configure for this @phydev
 *
 * Returns 0 on success or < 0 on error.
 */
static int phy_config_interrupt(struct phy_device *phydev, bool interrupts)
{
	phydev->interrupts = interrupts ? 1 : 0;
	if (phydev->drv->config_intr)
		return phydev->drv->config_intr(phydev);

	return 0;
}

/**
 * phy_restart_aneg - restart auto-negotiation
 * @phydev: target phy_device struct
 *
 * Restart the autonegotiation on @phydev.  Returns >= 0 on success or
 * negative errno on error.
 */
int phy_restart_aneg(struct phy_device *phydev)
{
	int ret;

	if (phydev->is_c45 && !(phydev->c45_ids.devices_in_package & BIT(0)))
		ret = genphy_c45_restart_aneg(phydev);
	else
		ret = genphy_restart_aneg(phydev);

	return ret;
}
EXPORT_SYMBOL_GPL(phy_restart_aneg);

/**
 * phy_aneg_done - return auto-negotiation status
 * @phydev: target phy_device struct
 *
 * Description: Return the auto-negotiation status from this @phydev
 * Returns > 0 on success or < 0 on error. 0 means that auto-negotiation
 * is still pending.
 */
int phy_aneg_done(struct phy_device *phydev)
{
	if (phydev->drv && phydev->drv->aneg_done)
		return phydev->drv->aneg_done(phydev);
	else if (phydev->is_c45)
		return genphy_c45_aneg_done(phydev);
	else
		return genphy_aneg_done(phydev);
}
EXPORT_SYMBOL(phy_aneg_done);

/**
 * phy_supported_speeds - return all speeds currently supported by a phy device
 * @phy: The phy device to return supported speeds of.
 * @speeds: buffer to store supported speeds in.
 * @size:   size of speeds buffer.
 *
 * Description: Returns the number of supported speeds, and fills the speeds
 * buffer with the supported speeds. If speeds buffer is too small to contain
 * all currently supported speeds, will return as many speeds as can fit.
 */
unsigned int phy_supported_speeds(struct phy_device *phy,
				  unsigned int *speeds,
				  unsigned int size)
{
	return phy_caps_speeds(speeds, size, phy->supported);
}

/**
 * phy_check_valid - check if there is a valid PHY setting which matches
 *		     speed, duplex, and feature mask
 * @speed: speed to match
 * @duplex: duplex to match
 * @features: A mask of the valid settings
 *
 * Description: Returns true if there is a valid setting, false otherwise.
 */
bool phy_check_valid(int speed, int duplex, unsigned long *features)
{
	return phy_caps_valid(speed, duplex, features);
}
EXPORT_SYMBOL(phy_check_valid);

/**
 * phy_sanitize_settings - make sure the PHY is set to supported speed and duplex
 * @phydev: the target phy_device struct
 *
 * Description: Make sure the PHY is set to supported speeds and
 *   duplexes.  Drop down by one in this order:  1000/FULL,
 *   1000/HALF, 100/FULL, 100/HALF, 10/FULL, 10/HALF.
 */
static void phy_sanitize_settings(struct phy_device *phydev)
{
	const struct link_capabilities *c;

	c = phy_caps_lookup(phydev->speed, phydev->duplex, phydev->supported,
			    false);

	if (c) {
		phydev->speed = c->speed;
		phydev->duplex = c->duplex;
	} else {
		/* We failed to find anything (no supported speeds?) */
		phydev->speed = SPEED_UNKNOWN;
		phydev->duplex = DUPLEX_UNKNOWN;
	}
}

void phy_ethtool_ksettings_get(struct phy_device *phydev,
			       struct ethtool_link_ksettings *cmd)
{
	mutex_lock(&phydev->lock);
	linkmode_copy(cmd->link_modes.supported, phydev->supported);
	linkmode_copy(cmd->link_modes.advertising, phydev->advertising);
	linkmode_copy(cmd->link_modes.lp_advertising, phydev->lp_advertising);

	cmd->base.speed = phydev->speed;
	cmd->base.duplex = phydev->duplex;
	cmd->base.master_slave_cfg = phydev->master_slave_get;
	cmd->base.master_slave_state = phydev->master_slave_state;
	cmd->base.rate_matching = phydev->rate_matching;
	if (phydev->interface == PHY_INTERFACE_MODE_MOCA)
		cmd->base.port = PORT_BNC;
	else
		cmd->base.port = phydev->port;
	cmd->base.transceiver = phydev->is_internal ?
				XCVR_INTERNAL : XCVR_EXTERNAL;
	cmd->base.phy_address = phydev->mdio.addr;
	cmd->base.autoneg = phydev->autoneg;
	cmd->base.eth_tp_mdix_ctrl = phydev->mdix_ctrl;
	cmd->base.eth_tp_mdix = phydev->mdix;
	mutex_unlock(&phydev->lock);
}
EXPORT_SYMBOL(phy_ethtool_ksettings_get);

/**
 * phy_mii_ioctl - generic PHY MII ioctl interface
 * @phydev: the phy_device struct
 * @ifr: &struct ifreq for socket ioctl's
 * @cmd: ioctl cmd to execute
 *
 * Note that this function is currently incompatible with the
 * PHYCONTROL layer.  It changes registers without regard to
 * current state.  Use at own risk.
 */
int phy_mii_ioctl(struct phy_device *phydev, struct ifreq *ifr, int cmd)
{
	struct mii_ioctl_data *mii_data = if_mii(ifr);
	struct kernel_hwtstamp_config kernel_cfg;
	struct netlink_ext_ack extack = {};
	u16 val = mii_data->val_in;
	bool change_autoneg = false;
	struct hwtstamp_config cfg;
	int prtad, devad;
	int ret;

	switch (cmd) {
	case SIOCGMIIPHY:
		mii_data->phy_id = phydev->mdio.addr;
		fallthrough;

	case SIOCGMIIREG:
		if (mdio_phy_id_is_c45(mii_data->phy_id)) {
			prtad = mdio_phy_id_prtad(mii_data->phy_id);
			devad = mdio_phy_id_devad(mii_data->phy_id);
			ret = mdiobus_c45_read(phydev->mdio.bus, prtad, devad,
					       mii_data->reg_num);

		} else {
			ret = mdiobus_read(phydev->mdio.bus, mii_data->phy_id,
					   mii_data->reg_num);
		}

		if (ret < 0)
			return ret;

		mii_data->val_out = ret;

		return 0;

	case SIOCSMIIREG:
		if (mdio_phy_id_is_c45(mii_data->phy_id)) {
			prtad = mdio_phy_id_prtad(mii_data->phy_id);
			devad = mdio_phy_id_devad(mii_data->phy_id);
		} else {
			prtad = mii_data->phy_id;
			devad = mii_data->reg_num;
		}
		if (prtad == phydev->mdio.addr) {
			switch (devad) {
			case MII_BMCR:
				if ((val & (BMCR_RESET | BMCR_ANENABLE)) == 0) {
					if (phydev->autoneg == AUTONEG_ENABLE)
						change_autoneg = true;
					phydev->autoneg = AUTONEG_DISABLE;
					if (val & BMCR_FULLDPLX)
						phydev->duplex = DUPLEX_FULL;
					else
						phydev->duplex = DUPLEX_HALF;
					if (val & BMCR_SPEED1000)
						phydev->speed = SPEED_1000;
					else if (val & BMCR_SPEED100)
						phydev->speed = SPEED_100;
					else phydev->speed = SPEED_10;
				} else {
					if (phydev->autoneg == AUTONEG_DISABLE)
						change_autoneg = true;
					phydev->autoneg = AUTONEG_ENABLE;
				}
				break;
			case MII_ADVERTISE:
				mii_adv_mod_linkmode_adv_t(phydev->advertising,
							   val);
				change_autoneg = true;
				break;
			case MII_CTRL1000:
				mii_ctrl1000_mod_linkmode_adv_t(phydev->advertising,
							        val);
				change_autoneg = true;
				break;
			default:
				/* do nothing */
				break;
			}
		}

		if (mdio_phy_id_is_c45(mii_data->phy_id))
			mdiobus_c45_write(phydev->mdio.bus, prtad, devad,
					  mii_data->reg_num, val);
		else
			mdiobus_write(phydev->mdio.bus, prtad, devad, val);

		if (prtad == phydev->mdio.addr &&
		    devad == MII_BMCR &&
		    val & BMCR_RESET)
			return phy_init_hw(phydev);

		if (change_autoneg)
			return phy_start_aneg(phydev);

		return 0;

	case SIOCSHWTSTAMP:
		if (phydev->mii_ts && phydev->mii_ts->hwtstamp) {
			if (copy_from_user(&cfg, ifr->ifr_data, sizeof(cfg)))
				return -EFAULT;

			hwtstamp_config_to_kernel(&kernel_cfg, &cfg);
			ret = phydev->mii_ts->hwtstamp(phydev->mii_ts, &kernel_cfg, &extack);
			if (ret)
				return ret;

			hwtstamp_config_from_kernel(&cfg, &kernel_cfg);
			if (copy_to_user(ifr->ifr_data, &cfg, sizeof(cfg)))
				return -EFAULT;

			return 0;
		}
		fallthrough;

	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL(phy_mii_ioctl);

/**
 * phy_do_ioctl - generic ndo_eth_ioctl implementation
 * @dev: the net_device struct
 * @ifr: &struct ifreq for socket ioctl's
 * @cmd: ioctl cmd to execute
 */
int phy_do_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	if (!dev->phydev)
		return -ENODEV;

	return phy_mii_ioctl(dev->phydev, ifr, cmd);
}
EXPORT_SYMBOL(phy_do_ioctl);

/**
 * phy_do_ioctl_running - generic ndo_eth_ioctl implementation but test first
 *
 * @dev: the net_device struct
 * @ifr: &struct ifreq for socket ioctl's
 * @cmd: ioctl cmd to execute
 *
 * Same as phy_do_ioctl, but ensures that net_device is running before
 * handling the ioctl.
 */
int phy_do_ioctl_running(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	if (!netif_running(dev))
		return -ENODEV;

	return phy_do_ioctl(dev, ifr, cmd);
}
EXPORT_SYMBOL(phy_do_ioctl_running);

/**
 * __phy_hwtstamp_get - Get hardware timestamping configuration from PHY
 *
 * @phydev: the PHY device structure
 * @config: structure holding the timestamping configuration
 *
 * Query the PHY device for its current hardware timestamping configuration.
 */
int __phy_hwtstamp_get(struct phy_device *phydev,
		       struct kernel_hwtstamp_config *config)
{
	if (!phydev)
		return -ENODEV;

	return -EOPNOTSUPP;
}

/**
 * __phy_hwtstamp_set - Modify PHY hardware timestamping configuration
 *
 * @phydev: the PHY device structure
 * @config: structure holding the timestamping configuration
 * @extack: netlink extended ack structure, for error reporting
 */
int __phy_hwtstamp_set(struct phy_device *phydev,
		       struct kernel_hwtstamp_config *config,
		       struct netlink_ext_ack *extack)
{
	if (!phydev)
		return -ENODEV;

	if (phydev->mii_ts && phydev->mii_ts->hwtstamp)
		return phydev->mii_ts->hwtstamp(phydev->mii_ts, config, extack);

	return -EOPNOTSUPP;
}

/**
 * phy_queue_state_machine - Trigger the state machine to run soon
 *
 * @phydev: the phy_device struct
 * @jiffies: Run the state machine after these jiffies
 */
static void phy_queue_state_machine(struct phy_device *phydev,
				    unsigned long jiffies)
{
	mod_delayed_work(system_power_efficient_wq, &phydev->state_queue,
			 jiffies);
}

/**
 * phy_trigger_machine - Trigger the state machine to run now
 *
 * @phydev: the phy_device struct
 */
void phy_trigger_machine(struct phy_device *phydev)
{
	phy_queue_state_machine(phydev, 0);
}
EXPORT_SYMBOL(phy_trigger_machine);

static void phy_abort_cable_test(struct phy_device *phydev)
{
	int err;

	ethnl_cable_test_finished(phydev);

	err = phy_init_hw(phydev);
	if (err)
		phydev_err(phydev, "Error while aborting cable test");
}

/**
 * phy_ethtool_get_strings - Get the statistic counter names
 *
 * @phydev: the phy_device struct
 * @data: Where to put the strings
 */
int phy_ethtool_get_strings(struct phy_device *phydev, u8 *data)
{
	if (!phydev->drv)
		return -EIO;

	mutex_lock(&phydev->lock);
	phydev->drv->get_strings(phydev, data);
	mutex_unlock(&phydev->lock);

	return 0;
}
EXPORT_SYMBOL(phy_ethtool_get_strings);

/**
 * phy_ethtool_get_sset_count - Get the number of statistic counters
 *
 * @phydev: the phy_device struct
 */
int phy_ethtool_get_sset_count(struct phy_device *phydev)
{
	int ret;

	if (!phydev->drv)
		return -EIO;

	if (phydev->drv->get_sset_count &&
	    phydev->drv->get_strings &&
	    phydev->drv->get_stats) {
		mutex_lock(&phydev->lock);
		ret = phydev->drv->get_sset_count(phydev);
		mutex_unlock(&phydev->lock);

		return ret;
	}

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(phy_ethtool_get_sset_count);

/**
 * phy_ethtool_get_stats - Get the statistic counters
 *
 * @phydev: the phy_device struct
 * @stats: What counters to get
 * @data: Where to store the counters
 */
int phy_ethtool_get_stats(struct phy_device *phydev,
			  struct ethtool_stats *stats, u64 *data)
{
	if (!phydev->drv)
		return -EIO;

	mutex_lock(&phydev->lock);
	phydev->drv->get_stats(phydev, stats, data);
	mutex_unlock(&phydev->lock);

	return 0;
}
EXPORT_SYMBOL(phy_ethtool_get_stats);

/**
 * __phy_ethtool_get_phy_stats - Retrieve standardized PHY statistics
 * @phydev: Pointer to the PHY device
 * @phy_stats: Pointer to ethtool_eth_phy_stats structure
 * @phydev_stats: Pointer to ethtool_phy_stats structure
 *
 * Fetches PHY statistics using a kernel-defined interface for consistent
 * diagnostics. Unlike phy_ethtool_get_stats(), which allows custom stats,
 * this function enforces a standardized format for better interoperability.
 */
void __phy_ethtool_get_phy_stats(struct phy_device *phydev,
				 struct ethtool_eth_phy_stats *phy_stats,
				 struct ethtool_phy_stats *phydev_stats)
{
	if (!phydev->drv || !phydev->drv->get_phy_stats)
		return;

	mutex_lock(&phydev->lock);
	phydev->drv->get_phy_stats(phydev, phy_stats, phydev_stats);
	mutex_unlock(&phydev->lock);
}

/**
 * __phy_ethtool_get_link_ext_stats - Retrieve extended link statistics for a PHY
 * @phydev: Pointer to the PHY device
 * @link_stats: Pointer to the structure to store extended link statistics
 *
 * Populates the ethtool_link_ext_stats structure with link down event counts
 * and additional driver-specific link statistics, if available.
 */
void __phy_ethtool_get_link_ext_stats(struct phy_device *phydev,
				      struct ethtool_link_ext_stats *link_stats)
{
	link_stats->link_down_events = READ_ONCE(phydev->link_down_events);

	if (!phydev->drv || !phydev->drv->get_link_stats)
		return;

	mutex_lock(&phydev->lock);
	phydev->drv->get_link_stats(phydev, link_stats);
	mutex_unlock(&phydev->lock);
}

/**
 * phy_ethtool_get_plca_cfg - Get PLCA RS configuration
 * @phydev: the phy_device struct
 * @plca_cfg: where to store the retrieved configuration
 *
 * Retrieve the PLCA configuration from the PHY. Return 0 on success or a
 * negative value if an error occurred.
 */
int phy_ethtool_get_plca_cfg(struct phy_device *phydev,
			     struct phy_plca_cfg *plca_cfg)
{
	int ret;

	if (!phydev->drv) {
		ret = -EIO;
		goto out;
	}

	if (!phydev->drv->get_plca_cfg) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	mutex_lock(&phydev->lock);
	ret = phydev->drv->get_plca_cfg(phydev, plca_cfg);

	mutex_unlock(&phydev->lock);
out:
	return ret;
}

/**
 * plca_check_valid - Check PLCA configuration before enabling
 * @phydev: the phy_device struct
 * @plca_cfg: current PLCA configuration
 * @extack: extack for reporting useful error messages
 *
 * Checks whether the PLCA and PHY configuration are consistent and it is safe
 * to enable PLCA. Returns 0 on success or a negative value if the PLCA or PHY
 * configuration is not consistent.
 */
static int plca_check_valid(struct phy_device *phydev,
			    const struct phy_plca_cfg *plca_cfg,
			    struct netlink_ext_ack *extack)
{
	int ret = 0;

	if (!linkmode_test_bit(ETHTOOL_LINK_MODE_10baseT1S_P2MP_Half_BIT,
			       phydev->advertising)) {
		ret = -EOPNOTSUPP;
		NL_SET_ERR_MSG(extack,
			       "Point to Multi-Point mode is not enabled");
	} else if (plca_cfg->node_id >= 255) {
		NL_SET_ERR_MSG(extack, "PLCA node ID is not set");
		ret = -EINVAL;
	}

	return ret;
}

/**
 * phy_ethtool_set_plca_cfg - Set PLCA RS configuration
 * @phydev: the phy_device struct
 * @plca_cfg: new PLCA configuration to apply
 * @extack: extack for reporting useful error messages
 *
 * Sets the PLCA configuration in the PHY. Return 0 on success or a
 * negative value if an error occurred.
 */
int phy_ethtool_set_plca_cfg(struct phy_device *phydev,
			     const struct phy_plca_cfg *plca_cfg,
			     struct netlink_ext_ack *extack)
{
	struct phy_plca_cfg *curr_plca_cfg;
	int ret;

	if (!phydev->drv) {
		ret = -EIO;
		goto out;
	}

	if (!phydev->drv->set_plca_cfg ||
	    !phydev->drv->get_plca_cfg) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	curr_plca_cfg = kmalloc(sizeof(*curr_plca_cfg), GFP_KERNEL);
	if (!curr_plca_cfg) {
		ret = -ENOMEM;
		goto out;
	}

	mutex_lock(&phydev->lock);

	ret = phydev->drv->get_plca_cfg(phydev, curr_plca_cfg);
	if (ret)
		goto out_drv;

	if (curr_plca_cfg->enabled < 0 && plca_cfg->enabled >= 0) {
		NL_SET_ERR_MSG(extack,
			       "PHY does not support changing the PLCA 'enable' attribute");
		ret = -EINVAL;
		goto out_drv;
	}

	if (curr_plca_cfg->node_id < 0 && plca_cfg->node_id >= 0) {
		NL_SET_ERR_MSG(extack,
			       "PHY does not support changing the PLCA 'local node ID' attribute");
		ret = -EINVAL;
		goto out_drv;
	}

	if (curr_plca_cfg->node_cnt < 0 && plca_cfg->node_cnt >= 0) {
		NL_SET_ERR_MSG(extack,
			       "PHY does not support changing the PLCA 'node count' attribute");
		ret = -EINVAL;
		goto out_drv;
	}

	if (curr_plca_cfg->to_tmr < 0 && plca_cfg->to_tmr >= 0) {
		NL_SET_ERR_MSG(extack,
			       "PHY does not support changing the PLCA 'TO timer' attribute");
		ret = -EINVAL;
		goto out_drv;
	}

	if (curr_plca_cfg->burst_cnt < 0 && plca_cfg->burst_cnt >= 0) {
		NL_SET_ERR_MSG(extack,
			       "PHY does not support changing the PLCA 'burst count' attribute");
		ret = -EINVAL;
		goto out_drv;
	}

	if (curr_plca_cfg->burst_tmr < 0 && plca_cfg->burst_tmr >= 0) {
		NL_SET_ERR_MSG(extack,
			       "PHY does not support changing the PLCA 'burst timer' attribute");
		ret = -EINVAL;
		goto out_drv;
	}

	// if enabling PLCA, perform a few sanity checks
	if (plca_cfg->enabled > 0) {
		// allow setting node_id concurrently with enabled
		if (plca_cfg->node_id >= 0)
			curr_plca_cfg->node_id = plca_cfg->node_id;

		ret = plca_check_valid(phydev, curr_plca_cfg, extack);
		if (ret)
			goto out_drv;
	}

	ret = phydev->drv->set_plca_cfg(phydev, plca_cfg);

out_drv:
	kfree(curr_plca_cfg);
	mutex_unlock(&phydev->lock);
out:
	return ret;
}

/**
 * phy_ethtool_get_plca_status - Get PLCA RS status information
 * @phydev: the phy_device struct
 * @plca_st: where to store the retrieved status information
 *
 * Retrieve the PLCA status information from the PHY. Return 0 on success or a
 * negative value if an error occurred.
 */
int phy_ethtool_get_plca_status(struct phy_device *phydev,
				struct phy_plca_status *plca_st)
{
	int ret;

	if (!phydev->drv) {
		ret = -EIO;
		goto out;
	}

	if (!phydev->drv->get_plca_status) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	mutex_lock(&phydev->lock);
	ret = phydev->drv->get_plca_status(phydev, plca_st);

	mutex_unlock(&phydev->lock);
out:
	return ret;
}

/**
 * phy_start_cable_test - Start a cable test
 *
 * @phydev: the phy_device struct
 * @extack: extack for reporting useful error messages
 */
int phy_start_cable_test(struct phy_device *phydev,
			 struct netlink_ext_ack *extack)
{
	struct net_device *dev = phydev->attached_dev;
	int err = -ENOMEM;

	if (!(phydev->drv &&
	      phydev->drv->cable_test_start &&
	      phydev->drv->cable_test_get_status)) {
		NL_SET_ERR_MSG(extack,
			       "PHY driver does not support cable testing");
		return -EOPNOTSUPP;
	}

	mutex_lock(&phydev->lock);
	if (phydev->state == PHY_CABLETEST) {
		NL_SET_ERR_MSG(extack,
			       "PHY already performing a test");
		err = -EBUSY;
		goto out;
	}

	if (phydev->state < PHY_UP ||
	    phydev->state > PHY_CABLETEST) {
		NL_SET_ERR_MSG(extack,
			       "PHY not configured. Try setting interface up");
		err = -EBUSY;
		goto out;
	}

	err = ethnl_cable_test_alloc(phydev, ETHTOOL_MSG_CABLE_TEST_NTF);
	if (err)
		goto out;

	/* Mark the carrier down until the test is complete */
	phy_link_down(phydev);

	netif_testing_on(dev);
	err = phydev->drv->cable_test_start(phydev);
	if (err) {
		netif_testing_off(dev);
		phy_link_up(phydev);
		goto out_free;
	}

	phydev->state = PHY_CABLETEST;

	if (phy_polling_mode(phydev))
		phy_trigger_machine(phydev);

	mutex_unlock(&phydev->lock);

	return 0;

out_free:
	ethnl_cable_test_free(phydev);
out:
	mutex_unlock(&phydev->lock);

	return err;
}
EXPORT_SYMBOL(phy_start_cable_test);

/**
 * phy_start_cable_test_tdr - Start a raw TDR cable test
 *
 * @phydev: the phy_device struct
 * @extack: extack for reporting useful error messages
 * @config: Configuration of the test to run
 */
int phy_start_cable_test_tdr(struct phy_device *phydev,
			     struct netlink_ext_ack *extack,
			     const struct phy_tdr_config *config)
{
	struct net_device *dev = phydev->attached_dev;
	int err = -ENOMEM;

	if (!(phydev->drv &&
	      phydev->drv->cable_test_tdr_start &&
	      phydev->drv->cable_test_get_status)) {
		NL_SET_ERR_MSG(extack,
			       "PHY driver does not support cable test TDR");
		return -EOPNOTSUPP;
	}

	mutex_lock(&phydev->lock);
	if (phydev->state == PHY_CABLETEST) {
		NL_SET_ERR_MSG(extack,
			       "PHY already performing a test");
		err = -EBUSY;
		goto out;
	}

	if (phydev->state < PHY_UP ||
	    phydev->state > PHY_CABLETEST) {
		NL_SET_ERR_MSG(extack,
			       "PHY not configured. Try setting interface up");
		err = -EBUSY;
		goto out;
	}

	err = ethnl_cable_test_alloc(phydev, ETHTOOL_MSG_CABLE_TEST_TDR_NTF);
	if (err)
		goto out;

	/* Mark the carrier down until the test is complete */
	phy_link_down(phydev);

	netif_testing_on(dev);
	err = phydev->drv->cable_test_tdr_start(phydev, config);
	if (err) {
		netif_testing_off(dev);
		phy_link_up(phydev);
		goto out_free;
	}

	phydev->state = PHY_CABLETEST;

	if (phy_polling_mode(phydev))
		phy_trigger_machine(phydev);

	mutex_unlock(&phydev->lock);

	return 0;

out_free:
	ethnl_cable_test_free(phydev);
out:
	mutex_unlock(&phydev->lock);

	return err;
}
EXPORT_SYMBOL(phy_start_cable_test_tdr);

int phy_config_aneg(struct phy_device *phydev)
{
	if (phydev->drv->config_aneg)
		return phydev->drv->config_aneg(phydev);

	/* Clause 45 PHYs that don't implement Clause 22 registers are not
	 * allowed to call genphy_config_aneg()
	 */
	if (phydev->is_c45 && !(phydev->c45_ids.devices_in_package & BIT(0)))
		return genphy_c45_config_aneg(phydev);

	return genphy_config_aneg(phydev);
}
EXPORT_SYMBOL(phy_config_aneg);

/**
 * phy_check_link_status - check link status and set state accordingly
 * @phydev: the phy_device struct
 *
 * Description: Check for link and whether autoneg was triggered / is running
 * and set state accordingly
 */
static int phy_check_link_status(struct phy_device *phydev)
{
	int err;

	lockdep_assert_held(&phydev->lock);

	/* Keep previous state if loopback is enabled because some PHYs
	 * report that Link is Down when loopback is enabled.
	 */
	if (phydev->loopback_enabled)
		return 0;

	err = phy_read_status(phydev);
	if (err)
		return err;

	if (phydev->link && phydev->state != PHY_RUNNING) {
		phy_check_downshift(phydev);
		phydev->state = PHY_RUNNING;
		err = genphy_c45_eee_is_active(phydev, NULL);
		phydev->eee_active = err > 0;
		phydev->enable_tx_lpi = phydev->eee_cfg.tx_lpi_enabled &&
					phydev->eee_active;

		phy_link_up(phydev);
	} else if (!phydev->link && phydev->state != PHY_NOLINK) {
		phydev->state = PHY_NOLINK;
		phydev->eee_active = false;
		phydev->enable_tx_lpi = false;
		phy_link_down(phydev);
	}

	return 0;
}

/**
 * phy_inband_caps - query which in-band signalling modes are supported
 * @phydev: a pointer to a &struct phy_device
 * @interface: the interface mode for the PHY
 *
 * Returns zero if it is unknown what in-band signalling is supported by the
 * PHY (e.g. because the PHY driver doesn't implement the method.) Otherwise,
 * returns a bit mask of the LINK_INBAND_* values from
 * &enum link_inband_signalling to describe which inband modes are supported
 * by the PHY for this interface mode.
 */
unsigned int phy_inband_caps(struct phy_device *phydev,
			     phy_interface_t interface)
{
	if (phydev->drv && phydev->drv->inband_caps)
		return phydev->drv->inband_caps(phydev, interface);

	return 0;
}
EXPORT_SYMBOL_GPL(phy_inband_caps);

/**
 * phy_config_inband - configure the desired PHY in-band mode
 * @phydev: the phy_device struct
 * @modes: in-band modes to configure
 *
 * Description: disables, enables or enables-with-bypass in-band signalling
 *   between the PHY and host system.
 *
 * Returns: zero on success, or negative errno value.
 */
int phy_config_inband(struct phy_device *phydev, unsigned int modes)
{
	lockdep_assert_held(&phydev->lock);

	if (!!(modes & LINK_INBAND_DISABLE) +
	    !!(modes & LINK_INBAND_ENABLE) +
	    !!(modes & LINK_INBAND_BYPASS) != 1)
		return -EINVAL;

	if (!phydev->drv)
		return -EIO;
	else if (!phydev->drv->config_inband)
		return -EOPNOTSUPP;

	return phydev->drv->config_inband(phydev, modes);
}
EXPORT_SYMBOL(phy_config_inband);

/**
 * _phy_start_aneg - start auto-negotiation for this PHY device
 * @phydev: the phy_device struct
 *
 * Description: Sanitizes the settings (if we're not autonegotiating
 *   them), and then calls the driver's config_aneg function.
 *   If the PHYCONTROL Layer is operating, we change the state to
 *   reflect the beginning of Auto-negotiation or forcing.
 */
int _phy_start_aneg(struct phy_device *phydev)
{
	int err;

	lockdep_assert_held(&phydev->lock);

	if (!phydev->drv)
		return -EIO;

	if (AUTONEG_DISABLE == phydev->autoneg)
		phy_sanitize_settings(phydev);

	err = phy_config_aneg(phydev);
	if (err < 0)
		return err;

	if (phy_is_started(phydev))
		err = phy_check_link_status(phydev);

	return err;
}
EXPORT_SYMBOL(_phy_start_aneg);

/**
 * phy_start_aneg - start auto-negotiation for this PHY device
 * @phydev: the phy_device struct
 *
 * Description: Sanitizes the settings (if we're not autonegotiating
 *   them), and then calls the driver's config_aneg function.
 *   If the PHYCONTROL Layer is operating, we change the state to
 *   reflect the beginning of Auto-negotiation or forcing.
 */
int phy_start_aneg(struct phy_device *phydev)
{
	int err;

	mutex_lock(&phydev->lock);
	err = _phy_start_aneg(phydev);
	mutex_unlock(&phydev->lock);

	return err;
}
EXPORT_SYMBOL(phy_start_aneg);

static int phy_poll_aneg_done(struct phy_device *phydev)
{
	unsigned int retries = 100;
	int ret;

	do {
		msleep(100);
		ret = phy_aneg_done(phydev);
	} while (!ret && --retries);

	if (!ret)
		return -ETIMEDOUT;

	return ret < 0 ? ret : 0;
}

int phy_ethtool_ksettings_set(struct phy_device *phydev,
			      const struct ethtool_link_ksettings *cmd)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(advertising);
	u8 autoneg = cmd->base.autoneg;
	u8 duplex = cmd->base.duplex;
	u32 speed = cmd->base.speed;

	if (cmd->base.phy_address != phydev->mdio.addr)
		return -EINVAL;

	linkmode_copy(advertising, cmd->link_modes.advertising);

	/* We make sure that we don't pass unsupported values in to the PHY */
	linkmode_and(advertising, advertising, phydev->supported);

	/* Verify the settings we care about. */
	if (autoneg != AUTONEG_ENABLE && autoneg != AUTONEG_DISABLE)
		return -EINVAL;

	if (autoneg == AUTONEG_ENABLE &&
	    (linkmode_empty(advertising) ||
	     !linkmode_test_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
				phydev->supported)))
		return -EINVAL;

	if (autoneg == AUTONEG_DISABLE &&
	    ((speed != SPEED_1000 &&
	      speed != SPEED_100 &&
	      speed != SPEED_10) ||
	     (duplex != DUPLEX_HALF &&
	      duplex != DUPLEX_FULL)))
		return -EINVAL;

	mutex_lock(&phydev->lock);
	phydev->autoneg = autoneg;

	if (autoneg == AUTONEG_DISABLE) {
		phydev->speed = speed;
		phydev->duplex = duplex;
	}

	linkmode_copy(phydev->advertising, advertising);

	linkmode_mod_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
			 phydev->advertising, autoneg == AUTONEG_ENABLE);

	phydev->master_slave_set = cmd->base.master_slave_cfg;
	phydev->mdix_ctrl = cmd->base.eth_tp_mdix_ctrl;

	/* Restart the PHY */
	if (phy_is_started(phydev)) {
		phydev->state = PHY_UP;
		phy_trigger_machine(phydev);
	} else {
		_phy_start_aneg(phydev);
	}

	mutex_unlock(&phydev->lock);
	return 0;
}
EXPORT_SYMBOL(phy_ethtool_ksettings_set);

/**
 * phy_speed_down - set speed to lowest speed supported by both link partners
 * @phydev: the phy_device struct
 * @sync: perform action synchronously
 *
 * Description: Typically used to save energy when waiting for a WoL packet
 *
 * WARNING: Setting sync to false may cause the system being unable to suspend
 * in case the PHY generates an interrupt when finishing the autonegotiation.
 * This interrupt may wake up the system immediately after suspend.
 * Therefore use sync = false only if you're sure it's safe with the respective
 * network chip.
 */
int phy_speed_down(struct phy_device *phydev, bool sync)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(adv_tmp);
	int ret = 0;

	mutex_lock(&phydev->lock);

	if (phydev->autoneg != AUTONEG_ENABLE)
		goto out;

	linkmode_copy(adv_tmp, phydev->advertising);

	ret = phy_speed_down_core(phydev);
	if (ret)
		goto out;

	linkmode_copy(phydev->adv_old, adv_tmp);

	if (linkmode_equal(phydev->advertising, adv_tmp)) {
		ret = 0;
		goto out;
	}

	ret = phy_config_aneg(phydev);
	if (ret)
		goto out;

	ret = sync ? phy_poll_aneg_done(phydev) : 0;
out:
	mutex_unlock(&phydev->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(phy_speed_down);

/**
 * phy_speed_up - (re)set advertised speeds to all supported speeds
 * @phydev: the phy_device struct
 *
 * Description: Used to revert the effect of phy_speed_down
 */
int phy_speed_up(struct phy_device *phydev)
{
	__ETHTOOL_DECLARE_LINK_MODE_MASK(adv_tmp);
	int ret = 0;

	mutex_lock(&phydev->lock);

	if (phydev->autoneg != AUTONEG_ENABLE)
		goto out;

	if (linkmode_empty(phydev->adv_old))
		goto out;

	linkmode_copy(adv_tmp, phydev->advertising);
	linkmode_copy(phydev->advertising, phydev->adv_old);
	linkmode_zero(phydev->adv_old);

	if (linkmode_equal(phydev->advertising, adv_tmp))
		goto out;

	ret = phy_config_aneg(phydev);
out:
	mutex_unlock(&phydev->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(phy_speed_up);

/**
 * phy_start_machine - start PHY state machine tracking
 * @phydev: the phy_device struct
 *
 * Description: The PHY infrastructure can run a state machine
 *   which tracks whether the PHY is starting up, negotiating,
 *   etc.  This function starts the delayed workqueue which tracks
 *   the state of the PHY. If you want to maintain your own state machine,
 *   do not call this function.
 */
void phy_start_machine(struct phy_device *phydev)
{
	phy_trigger_machine(phydev);
}
EXPORT_SYMBOL_GPL(phy_start_machine);

/**
 * phy_stop_machine - stop the PHY state machine tracking
 * @phydev: target phy_device struct
 *
 * Description: Stops the state machine delayed workqueue, sets the
 *   state to UP (unless it wasn't up yet). This function must be
 *   called BEFORE phy_detach.
 */
void phy_stop_machine(struct phy_device *phydev)
{
	cancel_delayed_work_sync(&phydev->state_queue);

	mutex_lock(&phydev->lock);
	if (phy_is_started(phydev))
		phydev->state = PHY_UP;
	mutex_unlock(&phydev->lock);
}

static void phy_process_error(struct phy_device *phydev)
{
	/* phydev->lock must be held for the state change to be safe */
	if (!mutex_is_locked(&phydev->lock))
		phydev_err(phydev, "PHY-device data unsafe context\n");

	phydev->state = PHY_ERROR;

	phy_trigger_machine(phydev);
}

static void phy_error_precise(struct phy_device *phydev,
			      const void *func, int err)
{
	WARN(1, "%pS: returned: %d\n", func, err);
	phy_process_error(phydev);
}

/**
 * phy_error - enter ERROR state for this PHY device
 * @phydev: target phy_device struct
 *
 * Moves the PHY to the ERROR state in response to a read
 * or write error, and tells the controller the link is down.
 * Must be called with phydev->lock held.
 */
void phy_error(struct phy_device *phydev)
{
	WARN_ON(1);
	phy_process_error(phydev);
}
EXPORT_SYMBOL(phy_error);

/**
 * phy_disable_interrupts - Disable the PHY interrupts from the PHY side
 * @phydev: target phy_device struct
 */
int phy_disable_interrupts(struct phy_device *phydev)
{
	/* Disable PHY interrupts */
	return phy_config_interrupt(phydev, PHY_INTERRUPT_DISABLED);
}

/**
 * phy_interrupt - PHY interrupt handler
 * @irq: interrupt line
 * @phy_dat: phy_device pointer
 *
 * Description: Handle PHY interrupt
 */
static irqreturn_t phy_interrupt(int irq, void *phy_dat)
{
	struct phy_device *phydev = phy_dat;
	irqreturn_t ret;

	/* Wakeup interrupts may occur during a system sleep transition.
	 * Postpone handling until the PHY has resumed.
	 */
	if (IS_ENABLED(CONFIG_PM_SLEEP) && phydev->irq_suspended) {
		struct net_device *netdev = phydev->attached_dev;

		if (netdev) {
			struct device *parent = netdev->dev.parent;

			if (netdev->ethtool->wol_enabled)
				pm_system_wakeup();
			else if (device_may_wakeup(&netdev->dev))
				pm_wakeup_dev_event(&netdev->dev, 0, true);
			else if (parent && device_may_wakeup(parent))
				pm_wakeup_dev_event(parent, 0, true);
		}

		phydev->irq_rerun = 1;
		disable_irq_nosync(irq);
		return IRQ_HANDLED;
	}

	mutex_lock(&phydev->lock);
	ret = phydev->drv->handle_interrupt(phydev);
	mutex_unlock(&phydev->lock);

	return ret;
}

/**
 * phy_enable_interrupts - Enable the interrupts from the PHY side
 * @phydev: target phy_device struct
 */
static int phy_enable_interrupts(struct phy_device *phydev)
{
	return phy_config_interrupt(phydev, PHY_INTERRUPT_ENABLED);
}

/**
 * phy_update_stats - Update PHY device statistics if supported.
 * @phydev: Pointer to the PHY device structure.
 *
 * If the PHY driver provides an update_stats callback, this function
 * invokes it to update the PHY statistics. If not, it returns 0.
 *
 * Return: 0 on success, or a negative error code if the callback fails.
 */
static int phy_update_stats(struct phy_device *phydev)
{
	if (!phydev->drv->update_stats)
		return 0;

	return phydev->drv->update_stats(phydev);
}

/**
 * phy_request_interrupt - request and enable interrupt for a PHY device
 * @phydev: target phy_device struct
 *
 * Description: Request and enable the interrupt for the given PHY.
 *   If this fails, then we set irq to PHY_POLL.
 *   This should only be called with a valid IRQ number.
 */
void phy_request_interrupt(struct phy_device *phydev)
{
	int err;

	err = request_threaded_irq(phydev->irq, NULL, phy_interrupt,
				   IRQF_ONESHOT | IRQF_SHARED,
				   phydev_name(phydev), phydev);
	if (err) {
		phydev_warn(phydev, "Error %d requesting IRQ %d, falling back to polling\n",
			    err, phydev->irq);
		phydev->irq = PHY_POLL;
	} else {
		if (phy_enable_interrupts(phydev)) {
			phydev_warn(phydev, "Can't enable interrupt, falling back to polling\n");
			phy_free_interrupt(phydev);
			phydev->irq = PHY_POLL;
		}
	}
}
EXPORT_SYMBOL(phy_request_interrupt);

/**
 * phy_free_interrupt - disable and free interrupt for a PHY device
 * @phydev: target phy_device struct
 *
 * Description: Disable and free the interrupt for the given PHY.
 *   This should only be called with a valid IRQ number.
 */
void phy_free_interrupt(struct phy_device *phydev)
{
	phy_disable_interrupts(phydev);
	free_irq(phydev->irq, phydev);
}
EXPORT_SYMBOL(phy_free_interrupt);

/**
 * phy_get_next_update_time - Determine the next PHY update time
 * @phydev: Pointer to the phy_device structure
 *
 * This function queries the PHY driver to get the time for the next polling
 * event. If the driver does not implement the callback, a default value is
 * used.
 *
 * Return: The time for the next polling event in jiffies
 */
static unsigned int phy_get_next_update_time(struct phy_device *phydev)
{
	if (phydev->drv && phydev->drv->get_next_update_time)
		return phydev->drv->get_next_update_time(phydev);

	return PHY_STATE_TIME;
}

enum phy_state_work {
	PHY_STATE_WORK_NONE,
	PHY_STATE_WORK_ANEG,
	PHY_STATE_WORK_SUSPEND,
};

static enum phy_state_work _phy_state_machine(struct phy_device *phydev)
{
	enum phy_state_work state_work = PHY_STATE_WORK_NONE;
	struct net_device *dev = phydev->attached_dev;
	enum phy_state old_state = phydev->state;
	const void *func = NULL;
	bool finished = false;
	int err = 0;

	switch (phydev->state) {
	case PHY_DOWN:
	case PHY_READY:
		break;
	case PHY_UP:
		state_work = PHY_STATE_WORK_ANEG;
		break;
	case PHY_NOLINK:
	case PHY_RUNNING:
		err = phy_check_link_status(phydev);
		func = &phy_check_link_status;

		if (!err)
			err = phy_update_stats(phydev);
		break;
	case PHY_CABLETEST:
		err = phydev->drv->cable_test_get_status(phydev, &finished);
		if (err) {
			phy_abort_cable_test(phydev);
			netif_testing_off(dev);
			state_work = PHY_STATE_WORK_ANEG;
			phydev->state = PHY_UP;
			break;
		}

		if (finished) {
			ethnl_cable_test_finished(phydev);
			netif_testing_off(dev);
			state_work = PHY_STATE_WORK_ANEG;
			phydev->state = PHY_UP;
		}
		break;
	case PHY_HALTED:
		if (phydev->link) {
			if (phydev->autoneg == AUTONEG_ENABLE) {
				phydev->speed = SPEED_UNKNOWN;
				phydev->duplex = DUPLEX_UNKNOWN;
			}
			if (phydev->master_slave_state !=
						MASTER_SLAVE_STATE_UNSUPPORTED)
				phydev->master_slave_state =
						MASTER_SLAVE_STATE_UNKNOWN;
			phydev->mdix = ETH_TP_MDI_INVALID;
			linkmode_zero(phydev->lp_advertising);
		}
		fallthrough;
	case PHY_ERROR:
		if (phydev->link) {
			phydev->link = 0;
			phydev->eee_active = false;
			phydev->enable_tx_lpi = false;
			phy_link_down(phydev);
		}
		state_work = PHY_STATE_WORK_SUSPEND;
		break;
	}

	if (state_work == PHY_STATE_WORK_ANEG) {
		err = _phy_start_aneg(phydev);
		func = &_phy_start_aneg;
	}

	if (err == -ENODEV)
		return state_work;

	if (err < 0)
		phy_error_precise(phydev, func, err);

	phy_process_state_change(phydev, old_state);

	/* Only re-schedule a PHY state machine change if we are polling the
	 * PHY, if PHY_MAC_INTERRUPT is set, then we will be moving
	 * between states from phy_mac_interrupt().
	 *
	 * In state PHY_HALTED the PHY gets suspended, so rescheduling the
	 * state machine would be pointless and possibly error prone when
	 * called from phy_disconnect() synchronously.
	 */
	if (phy_polling_mode(phydev) && phy_is_started(phydev))
		phy_queue_state_machine(phydev,
					phy_get_next_update_time(phydev));

	return state_work;
}

/* unlocked part of the PHY state machine */
static void _phy_state_machine_post_work(struct phy_device *phydev,
					 enum phy_state_work state_work)
{
	if (state_work == PHY_STATE_WORK_SUSPEND)
		phy_suspend(phydev);
}

/**
 * phy_state_machine - Handle the state machine
 * @work: work_struct that describes the work to be done
 */
void phy_state_machine(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct phy_device *phydev =
			container_of(dwork, struct phy_device, state_queue);
	enum phy_state_work state_work;

	mutex_lock(&phydev->lock);
	state_work = _phy_state_machine(phydev);
	mutex_unlock(&phydev->lock);

	_phy_state_machine_post_work(phydev, state_work);
}

/**
 * phy_stop - Bring down the PHY link, and stop checking the status
 * @phydev: target phy_device struct
 */
void phy_stop(struct phy_device *phydev)
{
	struct net_device *dev = phydev->attached_dev;
	enum phy_state_work state_work;
	enum phy_state old_state;

	if (!phy_is_started(phydev) && phydev->state != PHY_DOWN &&
	    phydev->state != PHY_ERROR) {
		WARN(1, "called from state %s\n",
		     phy_state_to_str(phydev->state));
		return;
	}

	mutex_lock(&phydev->lock);
	old_state = phydev->state;

	if (phydev->state == PHY_CABLETEST) {
		phy_abort_cable_test(phydev);
		netif_testing_off(dev);
	}

	if (phydev->sfp_bus)
		sfp_upstream_stop(phydev->sfp_bus);

	phydev->state = PHY_HALTED;
	phy_process_state_change(phydev, old_state);

	state_work = _phy_state_machine(phydev);
	mutex_unlock(&phydev->lock);

	_phy_state_machine_post_work(phydev, state_work);
	phy_stop_machine(phydev);

	/* Cannot call flush_scheduled_work() here as desired because
	 * of rtnl_lock(), but PHY_HALTED shall guarantee irq handler
	 * will not reenable interrupts.
	 */
}
EXPORT_SYMBOL(phy_stop);

/**
 * phy_start - start or restart a PHY device
 * @phydev: target phy_device struct
 *
 * Description: Indicates the attached device's readiness to
 *   handle PHY-related work.  Used during startup to start the
 *   PHY, and after a call to phy_stop() to resume operation.
 *   Also used to indicate the MDIO bus has cleared an error
 *   condition.
 */
void phy_start(struct phy_device *phydev)
{
	mutex_lock(&phydev->lock);

	if (phydev->state != PHY_READY && phydev->state != PHY_HALTED) {
		WARN(1, "called from state %s\n",
		     phy_state_to_str(phydev->state));
		goto out;
	}

	if (phydev->sfp_bus)
		sfp_upstream_start(phydev->sfp_bus);

	/* if phy was suspended, bring the physical link up again */
	__phy_resume(phydev);

	phydev->state = PHY_UP;

	phy_start_machine(phydev);
out:
	mutex_unlock(&phydev->lock);
}
EXPORT_SYMBOL(phy_start);

/**
 * phy_mac_interrupt - MAC says the link has changed
 * @phydev: phy_device struct with changed link
 *
 * The MAC layer is able to indicate there has been a change in the PHY link
 * status. Trigger the state machine and work a work queue.
 */
void phy_mac_interrupt(struct phy_device *phydev)
{
	/* Trigger a state machine change */
	phy_trigger_machine(phydev);
}
EXPORT_SYMBOL(phy_mac_interrupt);

/**
 * phy_loopback - Configure loopback mode of PHY
 * @phydev: target phy_device struct
 * @enable: enable or disable loopback mode
 * @speed: enable loopback mode with speed
 *
 * Configure loopback mode of PHY and signal link down and link up if speed is
 * changing.
 *
 * Return: 0 on success, negative error code on failure.
 */
int phy_loopback(struct phy_device *phydev, bool enable, int speed)
{
	bool link_up = false;
	int ret = 0;

	if (!phydev->drv)
		return -EIO;

	mutex_lock(&phydev->lock);

	if (enable && phydev->loopback_enabled) {
		ret = -EBUSY;
		goto out;
	}

	if (!enable && !phydev->loopback_enabled) {
		ret = -EINVAL;
		goto out;
	}

	if (enable) {
		/*
		 * Link up is signaled with a defined speed. If speed changes,
		 * then first link down and after that link up needs to be
		 * signaled.
		 */
		if (phydev->link && phydev->state == PHY_RUNNING) {
			/* link is up and signaled */
			if (speed && phydev->speed != speed) {
				/* signal link down and up for new speed */
				phydev->link = false;
				phydev->state = PHY_NOLINK;
				phy_link_down(phydev);

				link_up = true;
			}
		} else {
			/* link is not signaled */
			if (speed) {
				/* signal link up for new speed */
				link_up = true;
			}
		}
	}

	if (phydev->drv->set_loopback)
		ret = phydev->drv->set_loopback(phydev, enable, speed);
	else
		ret = genphy_loopback(phydev, enable, speed);

	if (ret) {
		if (enable) {
			/* try to restore link if enabling loopback fails */
			if (phydev->drv->set_loopback)
				phydev->drv->set_loopback(phydev, false, 0);
			else
				genphy_loopback(phydev, false, 0);
		}

		goto out;
	}

	if (link_up) {
		phydev->link = true;
		phydev->state = PHY_RUNNING;
		phy_link_up(phydev);
	}

	phydev->loopback_enabled = enable;

out:
	mutex_unlock(&phydev->lock);
	return ret;
}
EXPORT_SYMBOL(phy_loopback);

/**
 * phy_eee_tx_clock_stop_capable() - indicate whether the MAC can stop tx clock
 * @phydev: target phy_device struct
 *
 * Indicate whether the MAC can disable the transmit xMII clock while in LPI
 * state. Returns 1 if the MAC may stop the transmit clock, 0 if the MAC must
 * not stop the transmit clock, or negative error.
 */
int phy_eee_tx_clock_stop_capable(struct phy_device *phydev)
{
	int stat1;

	stat1 = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_STAT1);
	if (stat1 < 0)
		return stat1;

	return !!(stat1 & MDIO_PCS_STAT1_CLKSTOP_CAP);
}
EXPORT_SYMBOL_GPL(phy_eee_tx_clock_stop_capable);

/**
 * phy_eee_rx_clock_stop() - configure PHY receive clock in LPI
 * @phydev: target phy_device struct
 * @clk_stop_enable: flag to indicate whether the clock can be stopped
 *
 * Configure whether the PHY can disable its receive clock during LPI mode,
 * See IEEE 802.3 sections 22.2.2.2, 35.2.2.10, and 45.2.3.1.4.
 *
 * Returns: 0 or negative error.
 */
int phy_eee_rx_clock_stop(struct phy_device *phydev, bool clk_stop_enable)
{
	/* Configure the PHY to stop receiving xMII
	 * clock while it is signaling LPI.
	 */
	return phy_modify_mmd(phydev, MDIO_MMD_PCS, MDIO_CTRL1,
			      MDIO_PCS_CTRL1_CLKSTOP_EN,
			      clk_stop_enable ? MDIO_PCS_CTRL1_CLKSTOP_EN : 0);
}
EXPORT_SYMBOL_GPL(phy_eee_rx_clock_stop);

/**
 * phy_init_eee - init and check the EEE feature
 * @phydev: target phy_device struct
 * @clk_stop_enable: PHY may stop the clock during LPI
 *
 * Description: it checks if the Energy-Efficient Ethernet (EEE)
 * is supported by looking at the MMD registers 3.20 and 7.60/61
 * and it programs the MMD register 3.0 setting the "Clock stop enable"
 * bit if required.
 */
int phy_init_eee(struct phy_device *phydev, bool clk_stop_enable)
{
	int ret;

	if (!phydev->drv)
		return -EIO;

	ret = genphy_c45_eee_is_active(phydev, NULL);
	if (ret < 0)
		return ret;
	if (!ret)
		return -EPROTONOSUPPORT;

	if (clk_stop_enable)
		ret = phy_eee_rx_clock_stop(phydev, true);

	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL(phy_init_eee);

/**
 * phy_get_eee_err - report the EEE wake error count
 * @phydev: target phy_device struct
 *
 * Description: it is to report the number of time where the PHY
 * failed to complete its normal wake sequence.
 */
int phy_get_eee_err(struct phy_device *phydev)
{
	int ret;

	if (!phydev->drv)
		return -EIO;

	mutex_lock(&phydev->lock);
	ret = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_PCS_EEE_WK_ERR);
	mutex_unlock(&phydev->lock);

	return ret;
}
EXPORT_SYMBOL(phy_get_eee_err);

/**
 * phy_ethtool_get_eee - get EEE supported and status
 * @phydev: target phy_device struct
 * @data: ethtool_keee data
 *
 * Description: get the current EEE settings, filling in all members of
 * @data.
 */
int phy_ethtool_get_eee(struct phy_device *phydev, struct ethtool_keee *data)
{
	int ret;

	if (!phydev->drv)
		return -EIO;

	mutex_lock(&phydev->lock);
	ret = genphy_c45_ethtool_get_eee(phydev, data);
	eeecfg_to_eee(data, &phydev->eee_cfg);
	mutex_unlock(&phydev->lock);

	return ret;
}
EXPORT_SYMBOL(phy_ethtool_get_eee);

/**
 * phy_ethtool_set_eee_noneg - Adjusts MAC LPI configuration without PHY
 *			       renegotiation
 * @phydev: pointer to the target PHY device structure
 * @old_cfg: pointer to the eee_config structure containing the old EEE settings
 *
 * This function updates the Energy Efficient Ethernet (EEE) configuration
 * for cases where only the MAC's Low Power Idle (LPI) configuration changes,
 * without triggering PHY renegotiation. It ensures that the MAC is properly
 * informed of the new LPI settings by cycling the link down and up, which
 * is necessary for the MAC to adopt the new configuration. This adjustment
 * is done only if there is a change in the tx_lpi_enabled or tx_lpi_timer
 * configuration.
 */
static void phy_ethtool_set_eee_noneg(struct phy_device *phydev,
				      const struct eee_config *old_cfg)
{
	bool enable_tx_lpi;

	if (!phydev->link)
		return;

	enable_tx_lpi = phydev->eee_cfg.tx_lpi_enabled && phydev->eee_active;

	if (phydev->enable_tx_lpi != enable_tx_lpi ||
	    phydev->eee_cfg.tx_lpi_timer != old_cfg->tx_lpi_timer) {
		phydev->enable_tx_lpi = false;
		phydev->link = false;
		phy_link_down(phydev);
		phydev->enable_tx_lpi = enable_tx_lpi;
		phydev->link = true;
		phy_link_up(phydev);
	}
}

/**
 * phy_ethtool_set_eee - set EEE supported and status
 * @phydev: target phy_device struct
 * @data: ethtool_keee data
 *
 * Description: it is to program the Advertisement EEE register.
 */
int phy_ethtool_set_eee(struct phy_device *phydev, struct ethtool_keee *data)
{
	struct eee_config old_cfg;
	int ret;

	if (!phydev->drv)
		return -EIO;

	mutex_lock(&phydev->lock);

	old_cfg = phydev->eee_cfg;
	eee_to_eeecfg(&phydev->eee_cfg, data);

	ret = genphy_c45_ethtool_set_eee(phydev, data);
	if (ret == 0)
		phy_ethtool_set_eee_noneg(phydev, &old_cfg);
	else if (ret < 0)
		phydev->eee_cfg = old_cfg;

	mutex_unlock(&phydev->lock);

	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL(phy_ethtool_set_eee);

/**
 * phy_ethtool_set_wol - Configure Wake On LAN
 *
 * @phydev: target phy_device struct
 * @wol: Configuration requested
 */
int phy_ethtool_set_wol(struct phy_device *phydev, struct ethtool_wolinfo *wol)
{
	int ret;

	if (phydev->drv && phydev->drv->set_wol) {
		mutex_lock(&phydev->lock);
		ret = phydev->drv->set_wol(phydev, wol);
		mutex_unlock(&phydev->lock);

		return ret;
	}

	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(phy_ethtool_set_wol);

/**
 * phy_ethtool_get_wol - Get the current Wake On LAN configuration
 *
 * @phydev: target phy_device struct
 * @wol: Store the current configuration here
 */
void phy_ethtool_get_wol(struct phy_device *phydev, struct ethtool_wolinfo *wol)
{
	if (phydev->drv && phydev->drv->get_wol) {
		mutex_lock(&phydev->lock);
		phydev->drv->get_wol(phydev, wol);
		mutex_unlock(&phydev->lock);
	}
}
EXPORT_SYMBOL(phy_ethtool_get_wol);

int phy_ethtool_get_link_ksettings(struct net_device *ndev,
				   struct ethtool_link_ksettings *cmd)
{
	struct phy_device *phydev = ndev->phydev;

	if (!phydev)
		return -ENODEV;

	phy_ethtool_ksettings_get(phydev, cmd);

	return 0;
}
EXPORT_SYMBOL(phy_ethtool_get_link_ksettings);

int phy_ethtool_set_link_ksettings(struct net_device *ndev,
				   const struct ethtool_link_ksettings *cmd)
{
	struct phy_device *phydev = ndev->phydev;

	if (!phydev)
		return -ENODEV;

	return phy_ethtool_ksettings_set(phydev, cmd);
}
EXPORT_SYMBOL(phy_ethtool_set_link_ksettings);

/**
 * phy_ethtool_nway_reset - Restart auto negotiation
 * @ndev: Network device to restart autoneg for
 */
int phy_ethtool_nway_reset(struct net_device *ndev)
{
	struct phy_device *phydev = ndev->phydev;
	int ret;

	if (!phydev)
		return -ENODEV;

	if (!phydev->drv)
		return -EIO;

	mutex_lock(&phydev->lock);
	ret = phy_restart_aneg(phydev);
	mutex_unlock(&phydev->lock);

	return ret;
}
EXPORT_SYMBOL(phy_ethtool_nway_reset);

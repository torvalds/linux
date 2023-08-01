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
 * phy_find_valid - find a PHY setting that matches the requested parameters
 * @speed: desired speed
 * @duplex: desired duplex
 * @supported: mask of supported link modes
 *
 * Locate a supported phy setting that is, in priority order:
 * - an exact match for the specified speed and duplex mode
 * - a match for the specified speed, or slower speed
 * - the slowest supported speed
 * Returns the matched phy_setting entry, or %NULL if no supported phy
 * settings were found.
 */
static const struct phy_setting *
phy_find_valid(int speed, int duplex, unsigned long *supported)
{
	return phy_lookup_setting(speed, duplex, supported, false);
}

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
	return phy_speeds(speeds, size, phy->supported);
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
	return !!phy_lookup_setting(speed, duplex, features, true);
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
	const struct phy_setting *setting;

	setting = phy_find_valid(phydev->speed, phydev->duplex,
				 phydev->supported);
	if (setting) {
		phydev->speed = setting->speed;
		phydev->duplex = setting->duplex;
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
	cmd->base.transceiver = phy_is_internal(phydev) ?
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
	u16 val = mii_data->val_in;
	bool change_autoneg = false;
	int prtad, devad;

	switch (cmd) {
	case SIOCGMIIPHY:
		mii_data->phy_id = phydev->mdio.addr;
		fallthrough;

	case SIOCGMIIREG:
		if (mdio_phy_id_is_c45(mii_data->phy_id)) {
			prtad = mdio_phy_id_prtad(mii_data->phy_id);
			devad = mdio_phy_id_devad(mii_data->phy_id);
			mii_data->val_out = mdiobus_c45_read(
				phydev->mdio.bus, prtad, devad,
				mii_data->reg_num);
		} else {
			mii_data->val_out = mdiobus_read(
				phydev->mdio.bus, mii_data->phy_id,
				mii_data->reg_num);
		}
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
		if (phydev->mii_ts && phydev->mii_ts->hwtstamp)
			return phydev->mii_ts->hwtstamp(phydev->mii_ts, ifr);
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

	return phy_mii_ioctl(phydev, config->ifr, SIOCGHWTSTAMP);
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

	return phy_mii_ioctl(phydev, config->ifr, SIOCSHWTSTAMP);
}

/**
 * phy_queue_state_machine - Trigger the state machine to run soon
 *
 * @phydev: the phy_device struct
 * @jiffies: Run the state machine after these jiffies
 */
void phy_queue_state_machine(struct phy_device *phydev, unsigned long jiffies)
{
	mod_delayed_work(system_power_efficient_wq, &phydev->state_queue,
			 jiffies);
}
EXPORT_SYMBOL(phy_queue_state_machine);

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
		phy_link_up(phydev);
	} else if (!phydev->link && phydev->state != PHY_NOLINK) {
		phydev->state = PHY_NOLINK;
		phy_link_down(phydev);
	}

	return 0;
}

/**
 * _phy_start_aneg - start auto-negotiation for this PHY device
 * @phydev: the phy_device struct
 *
 * Description: Sanitizes the settings (if we're not autonegotiating
 *   them), and then calls the driver's config_aneg function.
 *   If the PHYCONTROL Layer is operating, we change the state to
 *   reflect the beginning of Auto-negotiation or forcing.
 */
static int _phy_start_aneg(struct phy_device *phydev)
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

	if (autoneg == AUTONEG_ENABLE && linkmode_empty(advertising))
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
	mutex_lock(&phydev->lock);
	phydev->state = PHY_ERROR;
	mutex_unlock(&phydev->lock);

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
 * Must not be called from interrupt context, or while the
 * phydev->lock is held.
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
	struct phy_driver *drv = phydev->drv;
	irqreturn_t ret;

	/* Wakeup interrupts may occur during a system sleep transition.
	 * Postpone handling until the PHY has resumed.
	 */
	if (IS_ENABLED(CONFIG_PM_SLEEP) && phydev->irq_suspended) {
		struct net_device *netdev = phydev->attached_dev;

		if (netdev) {
			struct device *parent = netdev->dev.parent;

			if (netdev->wol_enabled)
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
	ret = drv->handle_interrupt(phydev);
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
 * phy_stop - Bring down the PHY link, and stop checking the status
 * @phydev: target phy_device struct
 */
void phy_stop(struct phy_device *phydev)
{
	struct net_device *dev = phydev->attached_dev;
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

	mutex_unlock(&phydev->lock);

	phy_state_machine(&phydev->state_queue.work);
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
 * phy_state_machine - Handle the state machine
 * @work: work_struct that describes the work to be done
 */
void phy_state_machine(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct phy_device *phydev =
			container_of(dwork, struct phy_device, state_queue);
	struct net_device *dev = phydev->attached_dev;
	bool needs_aneg = false, do_suspend = false;
	enum phy_state old_state;
	const void *func = NULL;
	bool finished = false;
	int err = 0;

	mutex_lock(&phydev->lock);

	old_state = phydev->state;

	switch (phydev->state) {
	case PHY_DOWN:
	case PHY_READY:
		break;
	case PHY_UP:
		needs_aneg = true;

		break;
	case PHY_NOLINK:
	case PHY_RUNNING:
		err = phy_check_link_status(phydev);
		func = &phy_check_link_status;
		break;
	case PHY_CABLETEST:
		err = phydev->drv->cable_test_get_status(phydev, &finished);
		if (err) {
			phy_abort_cable_test(phydev);
			netif_testing_off(dev);
			needs_aneg = true;
			phydev->state = PHY_UP;
			break;
		}

		if (finished) {
			ethnl_cable_test_finished(phydev);
			netif_testing_off(dev);
			needs_aneg = true;
			phydev->state = PHY_UP;
		}
		break;
	case PHY_HALTED:
	case PHY_ERROR:
		if (phydev->link) {
			phydev->link = 0;
			phy_link_down(phydev);
		}
		do_suspend = true;
		break;
	}

	mutex_unlock(&phydev->lock);

	if (needs_aneg) {
		err = phy_start_aneg(phydev);
		func = &phy_start_aneg;
	} else if (do_suspend) {
		phy_suspend(phydev);
	}

	if (err == -ENODEV)
		return;

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
	mutex_lock(&phydev->lock);
	if (phy_polling_mode(phydev) && phy_is_started(phydev))
		phy_queue_state_machine(phydev, PHY_STATE_TIME);
	mutex_unlock(&phydev->lock);
}

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

	ret = genphy_c45_eee_is_active(phydev, NULL, NULL, NULL);
	if (ret < 0)
		return ret;
	if (!ret)
		return -EPROTONOSUPPORT;

	if (clk_stop_enable)
		/* Configure the PHY to stop receiving xMII
		 * clock while it is signaling LPI.
		 */
		ret = phy_set_bits_mmd(phydev, MDIO_MMD_PCS, MDIO_CTRL1,
				       MDIO_PCS_CTRL1_CLKSTOP_EN);

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
 * @data: ethtool_eee data
 *
 * Description: it reportes the Supported/Advertisement/LP Advertisement
 * capabilities.
 */
int phy_ethtool_get_eee(struct phy_device *phydev, struct ethtool_eee *data)
{
	int ret;

	if (!phydev->drv)
		return -EIO;

	mutex_lock(&phydev->lock);
	ret = genphy_c45_ethtool_get_eee(phydev, data);
	mutex_unlock(&phydev->lock);

	return ret;
}
EXPORT_SYMBOL(phy_ethtool_get_eee);

/**
 * phy_ethtool_set_eee - set EEE supported and status
 * @phydev: target phy_device struct
 * @data: ethtool_eee data
 *
 * Description: it is to program the Advertisement EEE register.
 */
int phy_ethtool_set_eee(struct phy_device *phydev, struct ethtool_eee *data)
{
	int ret;

	if (!phydev->drv)
		return -EIO;

	mutex_lock(&phydev->lock);
	ret = genphy_c45_ethtool_set_eee(phydev, data);
	mutex_unlock(&phydev->lock);

	return ret;
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

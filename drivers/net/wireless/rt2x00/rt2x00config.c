/*
	Copyright (C) 2004 - 2007 rt2x00 SourceForge Project
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: rt2x00lib
	Abstract: rt2x00 generic configuration routines.
 */

/*
 * Set enviroment defines for rt2x00.h
 */
#define DRV_NAME "rt2x00lib"

#include <linux/kernel.h>
#include <linux/module.h>

#include "rt2x00.h"
#include "rt2x00lib.h"

void rt2x00lib_config_mac_addr(struct rt2x00_dev *rt2x00dev, u8 *mac)
{
	if (mac)
		rt2x00dev->ops->lib->config_mac_addr(rt2x00dev, mac);
}

void rt2x00lib_config_bssid(struct rt2x00_dev *rt2x00dev, u8 *bssid)
{
	if (bssid)
		rt2x00dev->ops->lib->config_bssid(rt2x00dev, bssid);
}

void rt2x00lib_config_packet_filter(struct rt2x00_dev *rt2x00dev, int filter)
{
	/*
	 * Only configure the device when something has changed,
	 * or if we are in RESUME state in which case all configuration
	 * will be forced upon the device.
	 */
	if (!test_bit(INTERFACE_RESUME, &rt2x00dev->flags) &&
	    !test_bit(PACKET_FILTER_PENDING, &rt2x00dev->flags))
		return;

	/*
	 * Write configuration to device and clear the update flag.
	 */
	rt2x00dev->ops->lib->config_packet_filter(rt2x00dev, filter);
	__clear_bit(PACKET_FILTER_PENDING, &rt2x00dev->flags);
}

void rt2x00lib_config_type(struct rt2x00_dev *rt2x00dev, int type)
{
	struct interface *intf = &rt2x00dev->interface;

	/*
	 * Fallback when a invalid interface is attempted to
	 * be configured. If a monitor interface is present,
	 * we are going configure that, otherwise exit.
	 */
	if (type == INVALID_INTERFACE) {
		if (is_monitor_present(intf))
			type = IEEE80211_IF_TYPE_MNTR;
		else
			return;
	}

	/*
	 * Only configure the device when something has changed,
	 * or if we are in RESUME state in which case all configuration
	 * will be forced upon the device.
	 */
	if (!test_bit(INTERFACE_RESUME, &rt2x00dev->flags) &&
	    (!(is_interface_present(intf) ^
	       test_bit(INTERFACE_ENABLED, &rt2x00dev->flags)) &&
	     !(is_monitor_present(intf) ^
	       test_bit(INTERFACE_ENABLED_MONITOR, &rt2x00dev->flags))))
		return;

	/*
	 * Configure device.
	 */
	rt2x00dev->ops->lib->config_type(rt2x00dev, type);

	/*
	 * Update the configuration flags.
	 */
	if (type != IEEE80211_IF_TYPE_MNTR) {
		if (is_interface_present(intf))
			__set_bit(INTERFACE_ENABLED, &rt2x00dev->flags);
		else
			__clear_bit(INTERFACE_ENABLED, &rt2x00dev->flags);
	} else {
		if (is_monitor_present(intf))
			__set_bit(INTERFACE_ENABLED_MONITOR, &rt2x00dev->flags);
		else
			__clear_bit(INTERFACE_ENABLED_MONITOR,
				    &rt2x00dev->flags);
	}
}

void rt2x00lib_config(struct rt2x00_dev *rt2x00dev, struct ieee80211_conf *conf)
{
	int flags = 0;

	/*
	 * If we are in RESUME state we should
	 * force all configuration options.
	 */
	if (test_bit(INTERFACE_RESUME, &rt2x00dev->flags)) {
		flags = CONFIG_UPDATE_ALL;
		goto config;
	}

	/*
	 * Check which configuration options have been
	 * updated and should be send to the device.
	 */
	if (rt2x00dev->rx_status.phymode != conf->phymode)
		flags |= CONFIG_UPDATE_PHYMODE;
	if (rt2x00dev->rx_status.channel != conf->channel)
		flags |= CONFIG_UPDATE_CHANNEL;
	if (rt2x00dev->tx_power != conf->power_level)
		flags |= CONFIG_UPDATE_TXPOWER;
	if (rt2x00dev->rx_status.antenna == conf->antenna_sel_rx)
		flags |= CONFIG_UPDATE_ANTENNA;

	/*
	 * The following configuration options are never
	 * stored anywhere and will always be updated.
	 */
	flags |= CONFIG_UPDATE_SLOT_TIME;
	flags |= CONFIG_UPDATE_BEACON_INT;

config:
	rt2x00dev->ops->lib->config(rt2x00dev, flags, conf);

	/*
	 * Some configuration changes affect the link quality
	 * which means we need to reset the link tuner.
	 */
	if (flags & (CONFIG_UPDATE_CHANNEL | CONFIG_UPDATE_ANTENNA))
		rt2x00lib_reset_link_tuner(rt2x00dev);

	rt2x00dev->rx_status.phymode = conf->phymode;
	rt2x00dev->rx_status.freq = conf->freq;
	rt2x00dev->rx_status.channel = conf->channel;
	rt2x00dev->tx_power = conf->power_level;
	rt2x00dev->rx_status.antenna = conf->antenna_sel_rx;
}

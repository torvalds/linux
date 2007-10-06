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


/*
 * The MAC and BSSID addressess are simple array of bytes,
 * these arrays are little endian, so when sending the addressess
 * to the drivers, copy the it into a endian-signed variable.
 *
 * Note that all devices (except rt2500usb) have 32 bits
 * register word sizes. This means that whatever variable we
 * pass _must_ be a multiple of 32 bits. Otherwise the device
 * might not accept what we are sending to it.
 * This will also make it easier for the driver to write
 * the data to the device.
 *
 * Also note that when NULL is passed as address the
 * we will send 00:00:00:00:00 to the device to clear the address.
 * This will prevent the device being confused when it wants
 * to ACK frames or consideres itself associated.
 */
void rt2x00lib_config_mac_addr(struct rt2x00_dev *rt2x00dev, u8 *mac)
{
	__le32 reg[2];

	memset(&reg, 0, sizeof(reg));
	if (mac)
		memcpy(&reg, mac, ETH_ALEN);

	rt2x00dev->ops->lib->config_mac_addr(rt2x00dev, &reg[0]);
}

void rt2x00lib_config_bssid(struct rt2x00_dev *rt2x00dev, u8 *bssid)
{
	__le32 reg[2];

	memset(&reg, 0, sizeof(reg));
	if (bssid)
		memcpy(&reg, bssid, ETH_ALEN);

	rt2x00dev->ops->lib->config_bssid(rt2x00dev, &reg[0]);
}

void rt2x00lib_config_type(struct rt2x00_dev *rt2x00dev, const int type)
{
	int tsf_sync;

	switch (type) {
	case IEEE80211_IF_TYPE_IBSS:
	case IEEE80211_IF_TYPE_AP:
		tsf_sync = TSF_SYNC_BEACON;
		break;
	case IEEE80211_IF_TYPE_STA:
		tsf_sync = TSF_SYNC_INFRA;
		break;
	default:
		tsf_sync = TSF_SYNC_NONE;
		break;
	}

	rt2x00dev->ops->lib->config_type(rt2x00dev, type, tsf_sync);
}

void rt2x00lib_config(struct rt2x00_dev *rt2x00dev,
		      struct ieee80211_conf *conf, const int force_config)
{
	int flags = 0;

	/*
	 * In some situations we want to force all configurations
	 * to be reloaded (When resuming for instance).
	 */
	if (force_config) {
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

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
	struct rt2x00lib_conf libconf;
	struct ieee80211_hw_mode *mode;
	struct ieee80211_rate *rate;
	int flags = 0;
	int short_slot_time;

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

	/*
	 * We have determined what options should be updated,
	 * now precalculate device configuration values depending
	 * on what configuration options need to be updated.
	 */
config:
	memset(&libconf, 0, sizeof(libconf));

	if (flags & CONFIG_UPDATE_PHYMODE) {
		switch (conf->phymode) {
		case MODE_IEEE80211A:
			libconf.phymode = HWMODE_A;
			break;
		case MODE_IEEE80211B:
			libconf.phymode = HWMODE_B;
			break;
		case MODE_IEEE80211G:
			libconf.phymode = HWMODE_G;
			break;
		default:
			ERROR(rt2x00dev,
			      "Attempt to configure unsupported mode (%d)"
			      "Defaulting to 802.11b", conf->phymode);
			libconf.phymode = HWMODE_B;
		}

		mode = &rt2x00dev->hwmodes[libconf.phymode];
		rate = &mode->rates[mode->num_rates - 1];

		libconf.basic_rates =
		    DEVICE_GET_RATE_FIELD(rate->val, RATEMASK) & DEV_BASIC_RATEMASK;
	}

	if (flags & CONFIG_UPDATE_CHANNEL) {
		memcpy(&libconf.rf,
		       &rt2x00dev->spec.channels[conf->channel_val],
		       sizeof(libconf.rf));
	}

	if (flags & CONFIG_UPDATE_SLOT_TIME) {
		short_slot_time = conf->flags & IEEE80211_CONF_SHORT_SLOT_TIME;

		libconf.slot_time =
		    short_slot_time ? SHORT_SLOT_TIME : SLOT_TIME;
		libconf.sifs = SIFS;
		libconf.pifs = short_slot_time ? SHORT_PIFS : PIFS;
		libconf.difs = short_slot_time ? SHORT_DIFS : DIFS;
		libconf.eifs = EIFS;
	}

	libconf.conf = conf;

	/*
	 * Start configuration.
	 */
	rt2x00dev->ops->lib->config(rt2x00dev, flags, &libconf);

	/*
	 * Some configuration changes affect the link quality
	 * which means we need to reset the link tuner.
	 */
	if (flags & (CONFIG_UPDATE_CHANNEL | CONFIG_UPDATE_ANTENNA))
		rt2x00lib_reset_link_tuner(rt2x00dev);

	rt2x00dev->curr_hwmode = libconf.phymode;
	rt2x00dev->rx_status.phymode = conf->phymode;
	rt2x00dev->rx_status.freq = conf->freq;
	rt2x00dev->rx_status.channel = conf->channel;
	rt2x00dev->tx_power = conf->power_level;
	rt2x00dev->rx_status.antenna = conf->antenna_sel_rx;
}

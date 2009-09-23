/*
	Copyright (C) 2004 - 2009 rt2x00 SourceForge Project
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

#include <linux/kernel.h>
#include <linux/module.h>

#include "rt2x00.h"
#include "rt2x00lib.h"

void rt2x00lib_config_intf(struct rt2x00_dev *rt2x00dev,
			   struct rt2x00_intf *intf,
			   enum nl80211_iftype type,
			   const u8 *mac, const u8 *bssid)
{
	struct rt2x00intf_conf conf;
	unsigned int flags = 0;

	conf.type = type;

	switch (type) {
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_WDS:
		conf.sync = TSF_SYNC_BEACON;
		break;
	case NL80211_IFTYPE_STATION:
		conf.sync = TSF_SYNC_INFRA;
		break;
	default:
		conf.sync = TSF_SYNC_NONE;
		break;
	}

	/*
	 * Note that when NULL is passed as address we will send
	 * 00:00:00:00:00 to the device to clear the address.
	 * This will prevent the device being confused when it wants
	 * to ACK frames or consideres itself associated.
	 */
	memset(&conf.mac, 0, sizeof(conf.mac));
	if (mac)
		memcpy(&conf.mac, mac, ETH_ALEN);

	memset(&conf.bssid, 0, sizeof(conf.bssid));
	if (bssid)
		memcpy(&conf.bssid, bssid, ETH_ALEN);

	flags |= CONFIG_UPDATE_TYPE;
	if (mac || (!rt2x00dev->intf_ap_count && !rt2x00dev->intf_sta_count))
		flags |= CONFIG_UPDATE_MAC;
	if (bssid || (!rt2x00dev->intf_ap_count && !rt2x00dev->intf_sta_count))
		flags |= CONFIG_UPDATE_BSSID;

	rt2x00dev->ops->lib->config_intf(rt2x00dev, intf, &conf, flags);
}

void rt2x00lib_config_erp(struct rt2x00_dev *rt2x00dev,
			  struct rt2x00_intf *intf,
			  struct ieee80211_bss_conf *bss_conf)
{
	struct rt2x00lib_erp erp;

	memset(&erp, 0, sizeof(erp));

	erp.short_preamble = bss_conf->use_short_preamble;
	erp.cts_protection = bss_conf->use_cts_prot;

	erp.slot_time = bss_conf->use_short_slot ? SHORT_SLOT_TIME : SLOT_TIME;
	erp.sifs = SIFS;
	erp.pifs = bss_conf->use_short_slot ? SHORT_PIFS : PIFS;
	erp.difs = bss_conf->use_short_slot ? SHORT_DIFS : DIFS;
	erp.eifs = bss_conf->use_short_slot ? SHORT_EIFS : EIFS;

	erp.basic_rates = bss_conf->basic_rates;
	erp.beacon_int = bss_conf->beacon_int;

	/* Update global beacon interval time, this is needed for PS support */
	rt2x00dev->beacon_int = bss_conf->beacon_int;

	rt2x00dev->ops->lib->config_erp(rt2x00dev, &erp);
}

static inline
enum antenna rt2x00lib_config_antenna_check(enum antenna current_ant,
					    enum antenna default_ant)
{
	if (current_ant != ANTENNA_SW_DIVERSITY)
		return current_ant;
	return (default_ant != ANTENNA_SW_DIVERSITY) ? default_ant : ANTENNA_B;
}

void rt2x00lib_config_antenna(struct rt2x00_dev *rt2x00dev,
			      struct antenna_setup config)
{
	struct link_ant *ant = &rt2x00dev->link.ant;
	struct antenna_setup *def = &rt2x00dev->default_ant;
	struct antenna_setup *active = &rt2x00dev->link.ant.active;

	/*
	 * Failsafe: Make sure we are not sending the
	 * ANTENNA_SW_DIVERSITY state to the driver.
	 * If that happens, fallback to hardware defaults,
	 * or our own default.
	 * If diversity handling is active for a particular antenna,
	 * we shouldn't overwrite that antenna.
	 * The calls to rt2x00lib_config_antenna_check()
	 * might have caused that we restore back to the already
	 * active setting. If that has happened we can quit.
	 */
	if (!(ant->flags & ANTENNA_RX_DIVERSITY))
		config.rx = rt2x00lib_config_antenna_check(config.rx, def->rx);
	else
		config.rx = active->rx;

	if (!(ant->flags & ANTENNA_TX_DIVERSITY))
		config.tx = rt2x00lib_config_antenna_check(config.tx, def->tx);
	else
		config.tx = active->tx;

	if (config.rx == active->rx && config.tx == active->tx)
		return;

	/*
	 * Antenna setup changes require the RX to be disabled,
	 * else the changes will be ignored by the device.
	 */
	if (test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		rt2x00lib_toggle_rx(rt2x00dev, STATE_RADIO_RX_OFF_LINK);

	/*
	 * Write new antenna setup to device and reset the link tuner.
	 * The latter is required since we need to recalibrate the
	 * noise-sensitivity ratio for the new setup.
	 */
	rt2x00dev->ops->lib->config_ant(rt2x00dev, &config);

	rt2x00link_reset_tuner(rt2x00dev, true);

	memcpy(active, &config, sizeof(config));

	if (test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		rt2x00lib_toggle_rx(rt2x00dev, STATE_RADIO_RX_ON_LINK);
}

void rt2x00lib_config(struct rt2x00_dev *rt2x00dev,
		      struct ieee80211_conf *conf,
		      unsigned int ieee80211_flags)
{
	struct rt2x00lib_conf libconf;

	memset(&libconf, 0, sizeof(libconf));

	libconf.conf = conf;

	if (ieee80211_flags & IEEE80211_CONF_CHANGE_CHANNEL) {
		if (conf_is_ht40(conf))
			__set_bit(CONFIG_CHANNEL_HT40, &rt2x00dev->flags);
		else
			__clear_bit(CONFIG_CHANNEL_HT40, &rt2x00dev->flags);

		memcpy(&libconf.rf,
		       &rt2x00dev->spec.channels[conf->channel->hw_value],
		       sizeof(libconf.rf));

		memcpy(&libconf.channel,
		       &rt2x00dev->spec.channels_info[conf->channel->hw_value],
		       sizeof(libconf.channel));
	}

	/*
	 * Start configuration.
	 */
	rt2x00dev->ops->lib->config(rt2x00dev, &libconf, ieee80211_flags);

	/*
	 * Some configuration changes affect the link quality
	 * which means we need to reset the link tuner.
	 */
	if (ieee80211_flags & IEEE80211_CONF_CHANGE_CHANNEL)
		rt2x00link_reset_tuner(rt2x00dev, false);

	rt2x00dev->curr_band = conf->channel->band;
	rt2x00dev->tx_power = conf->power_level;
	rt2x00dev->short_retry = conf->short_frame_max_tx_count;
	rt2x00dev->long_retry = conf->long_frame_max_tx_count;

	rt2x00dev->rx_status.band = conf->channel->band;
	rt2x00dev->rx_status.freq = conf->channel->center_freq;
}

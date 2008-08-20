/*
	Copyright (C) 2004 - 2008 rt2x00 SourceForge Project
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
			   enum ieee80211_if_types type,
			   u8 *mac, u8 *bssid)
{
	struct rt2x00intf_conf conf;
	unsigned int flags = 0;

	conf.type = type;

	switch (type) {
	case IEEE80211_IF_TYPE_IBSS:
	case IEEE80211_IF_TYPE_AP:
		conf.sync = TSF_SYNC_BEACON;
		break;
	case IEEE80211_IF_TYPE_STA:
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

	erp.ack_timeout = PLCP + get_duration(ACK_SIZE, 10);
	erp.ack_consume_time = SIFS + PLCP + get_duration(ACK_SIZE, 10);

	if (rt2x00dev->hw->conf.flags & IEEE80211_CONF_SHORT_SLOT_TIME)
		erp.ack_timeout += SHORT_DIFS;
	else
		erp.ack_timeout += DIFS;

	if (bss_conf->use_short_preamble) {
		erp.ack_timeout += SHORT_PREAMBLE;
		erp.ack_consume_time += SHORT_PREAMBLE;
	} else {
		erp.ack_timeout += PREAMBLE;
		erp.ack_consume_time += PREAMBLE;
	}

	rt2x00dev->ops->lib->config_erp(rt2x00dev, &erp);
}

void rt2x00lib_config_antenna(struct rt2x00_dev *rt2x00dev,
			      enum antenna rx, enum antenna tx)
{
	struct rt2x00lib_conf libconf;

	libconf.ant.rx = rx;
	libconf.ant.tx = tx;

	if (rx == rt2x00dev->link.ant.active.rx &&
	    tx == rt2x00dev->link.ant.active.tx)
		return;

	/*
	 * Antenna setup changes require the RX to be disabled,
	 * else the changes will be ignored by the device.
	 */
	if (test_bit(DEVICE_ENABLED_RADIO, &rt2x00dev->flags))
		rt2x00lib_toggle_rx(rt2x00dev, STATE_RADIO_RX_OFF_LINK);

	/*
	 * Write new antenna setup to device and reset the link tuner.
	 * The latter is required since we need to recalibrate the
	 * noise-sensitivity ratio for the new setup.
	 */
	rt2x00dev->ops->lib->config(rt2x00dev, &libconf, CONFIG_UPDATE_ANTENNA);
	rt2x00lib_reset_link_tuner(rt2x00dev);
	rt2x00_reset_link_ant_rssi(&rt2x00dev->link);

	rt2x00dev->link.ant.active.rx = libconf.ant.rx;
	rt2x00dev->link.ant.active.tx = libconf.ant.tx;

	if (test_bit(DEVICE_ENABLED_RADIO, &rt2x00dev->flags))
		rt2x00lib_toggle_rx(rt2x00dev, STATE_RADIO_RX_ON_LINK);
}

static u32 rt2x00lib_get_basic_rates(struct ieee80211_supported_band *band)
{
	const struct rt2x00_rate *rate;
	unsigned int i;
	u32 mask = 0;

	for (i = 0; i < band->n_bitrates; i++) {
		rate = rt2x00_get_rate(band->bitrates[i].hw_value);
		if (rate->flags & DEV_RATE_BASIC)
			mask |= rate->ratemask;
	}

	return mask;
}

void rt2x00lib_config(struct rt2x00_dev *rt2x00dev,
		      struct ieee80211_conf *conf, const int force_config)
{
	struct rt2x00lib_conf libconf;
	struct ieee80211_supported_band *band;
	struct antenna_setup *default_ant = &rt2x00dev->default_ant;
	struct antenna_setup *active_ant = &rt2x00dev->link.ant.active;
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
	if (rt2x00dev->rx_status.band != conf->channel->band)
		flags |= CONFIG_UPDATE_PHYMODE;
	if (rt2x00dev->rx_status.freq != conf->channel->center_freq)
		flags |= CONFIG_UPDATE_CHANNEL;
	if (rt2x00dev->tx_power != conf->power_level)
		flags |= CONFIG_UPDATE_TXPOWER;

	/*
	 * Determining changes in the antenna setups request several checks:
	 * antenna_sel_{r,t}x = 0
	 *    -> Does active_{r,t}x match default_{r,t}x
	 *    -> Is default_{r,t}x SW_DIVERSITY
	 * antenna_sel_{r,t}x = 1/2
	 *    -> Does active_{r,t}x match antenna_sel_{r,t}x
	 * The reason for not updating the antenna while SW diversity
	 * should be used is simple: Software diversity means that
	 * we should switch between the antenna's based on the
	 * quality. This means that the current antenna is good enough
	 * to work with untill the link tuner decides that an antenna
	 * switch should be performed.
	 */
	if (!conf->antenna_sel_rx &&
	    default_ant->rx != ANTENNA_SW_DIVERSITY &&
	    default_ant->rx != active_ant->rx)
		flags |= CONFIG_UPDATE_ANTENNA;
	else if (conf->antenna_sel_rx &&
		 conf->antenna_sel_rx != active_ant->rx)
		flags |= CONFIG_UPDATE_ANTENNA;
	else if (active_ant->rx == ANTENNA_SW_DIVERSITY)
		flags |= CONFIG_UPDATE_ANTENNA;

	if (!conf->antenna_sel_tx &&
	    default_ant->tx != ANTENNA_SW_DIVERSITY &&
	    default_ant->tx != active_ant->tx)
		flags |= CONFIG_UPDATE_ANTENNA;
	else if (conf->antenna_sel_tx &&
		 conf->antenna_sel_tx != active_ant->tx)
		flags |= CONFIG_UPDATE_ANTENNA;
	else if (active_ant->tx == ANTENNA_SW_DIVERSITY)
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
		band = &rt2x00dev->bands[conf->channel->band];

		libconf.band = conf->channel->band;
		libconf.basic_rates = rt2x00lib_get_basic_rates(band);
	}

	if (flags & CONFIG_UPDATE_CHANNEL) {
		memcpy(&libconf.rf,
		       &rt2x00dev->spec.channels[conf->channel->hw_value],
		       sizeof(libconf.rf));
	}

	if (flags & CONFIG_UPDATE_ANTENNA) {
		if (conf->antenna_sel_rx)
			libconf.ant.rx = conf->antenna_sel_rx;
		else if (default_ant->rx != ANTENNA_SW_DIVERSITY)
			libconf.ant.rx = default_ant->rx;
		else if (active_ant->rx == ANTENNA_SW_DIVERSITY)
			libconf.ant.rx = ANTENNA_B;
		else
			libconf.ant.rx = active_ant->rx;

		if (conf->antenna_sel_tx)
			libconf.ant.tx = conf->antenna_sel_tx;
		else if (default_ant->tx != ANTENNA_SW_DIVERSITY)
			libconf.ant.tx = default_ant->tx;
		else if (active_ant->tx == ANTENNA_SW_DIVERSITY)
			libconf.ant.tx = ANTENNA_B;
		else
			libconf.ant.tx = active_ant->tx;
	}

	if (flags & CONFIG_UPDATE_SLOT_TIME) {
		short_slot_time = conf->flags & IEEE80211_CONF_SHORT_SLOT_TIME;

		libconf.slot_time =
		    short_slot_time ? SHORT_SLOT_TIME : SLOT_TIME;
		libconf.sifs = SIFS;
		libconf.pifs = short_slot_time ? SHORT_PIFS : PIFS;
		libconf.difs = short_slot_time ? SHORT_DIFS : DIFS;
		libconf.eifs = short_slot_time ? SHORT_EIFS : EIFS;
	}

	libconf.conf = conf;

	/*
	 * Start configuration.
	 */
	rt2x00dev->ops->lib->config(rt2x00dev, &libconf, flags);

	/*
	 * Some configuration changes affect the link quality
	 * which means we need to reset the link tuner.
	 */
	if (flags & (CONFIG_UPDATE_CHANNEL | CONFIG_UPDATE_ANTENNA))
		rt2x00lib_reset_link_tuner(rt2x00dev);

	if (flags & CONFIG_UPDATE_PHYMODE) {
		rt2x00dev->curr_band = conf->channel->band;
		rt2x00dev->rx_status.band = conf->channel->band;
	}

	rt2x00dev->rx_status.freq = conf->channel->center_freq;
	rt2x00dev->tx_power = conf->power_level;

	if (flags & CONFIG_UPDATE_ANTENNA) {
		rt2x00dev->link.ant.active.rx = libconf.ant.rx;
		rt2x00dev->link.ant.active.tx = libconf.ant.tx;
	}
}

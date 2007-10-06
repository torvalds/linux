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
	Abstract: Data structures and definitions for the rt2x00lib module.
 */

#ifndef RT2X00LIB_H
#define RT2X00LIB_H

/*
 * Interval defines
 * Both the link tuner as the rfkill will be called once per second.
 */
#define LINK_TUNE_INTERVAL	( round_jiffies(HZ) )
#define RFKILL_POLL_INTERVAL	( 1000 )

/*
 * Radio control handlers.
 */
int rt2x00lib_enable_radio(struct rt2x00_dev *rt2x00dev);
void rt2x00lib_disable_radio(struct rt2x00_dev *rt2x00dev);
void rt2x00lib_toggle_rx(struct rt2x00_dev *rt2x00dev, enum dev_state state);
void rt2x00lib_reset_link_tuner(struct rt2x00_dev *rt2x00dev);

/*
 * Initialization handlers.
 */
int rt2x00lib_initialize(struct rt2x00_dev *rt2x00dev);
void rt2x00lib_uninitialize(struct rt2x00_dev *rt2x00dev);

/*
 * Configuration handlers.
 */
void rt2x00lib_config_mac_addr(struct rt2x00_dev *rt2x00dev, u8 *mac);
void rt2x00lib_config_bssid(struct rt2x00_dev *rt2x00dev, u8 *bssid);
void rt2x00lib_config_type(struct rt2x00_dev *rt2x00dev, const int type);
void rt2x00lib_config(struct rt2x00_dev *rt2x00dev,
		      struct ieee80211_conf *conf, const int force_config);

/*
 * Firmware handlers.
 */
#ifdef CONFIG_RT2X00_LIB_FIRMWARE
int rt2x00lib_load_firmware(struct rt2x00_dev *rt2x00dev);
void rt2x00lib_free_firmware(struct rt2x00_dev *rt2x00dev);
#else
static inline int rt2x00lib_load_firmware(struct rt2x00_dev *rt2x00dev)
{
	return 0;
}
static inline void rt2x00lib_free_firmware(struct rt2x00_dev *rt2x00dev)
{
}
#endif /* CONFIG_RT2X00_LIB_FIRMWARE */

/*
 * Debugfs handlers.
 */
#ifdef CONFIG_RT2X00_LIB_DEBUGFS
void rt2x00debug_register(struct rt2x00_dev *rt2x00dev);
void rt2x00debug_deregister(struct rt2x00_dev *rt2x00dev);
#else
static inline void rt2x00debug_register(struct rt2x00_dev *rt2x00dev)
{
}

static inline void rt2x00debug_deregister(struct rt2x00_dev *rt2x00dev)
{
}
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */

/*
 * RFkill handlers.
 */
#ifdef CONFIG_RT2X00_LIB_RFKILL
int rt2x00rfkill_register(struct rt2x00_dev *rt2x00dev);
void rt2x00rfkill_unregister(struct rt2x00_dev *rt2x00dev);
int rt2x00rfkill_allocate(struct rt2x00_dev *rt2x00dev);
void rt2x00rfkill_free(struct rt2x00_dev *rt2x00dev);
#else
static inline int rt2x00rfkill_register(struct rt2x00_dev *rt2x00dev)
{
	return 0;
}

static inline void rt2x00rfkill_unregister(struct rt2x00_dev *rt2x00dev)
{
}

static inline int rt2x00rfkill_allocate(struct rt2x00_dev *rt2x00dev)
{
	return 0;
}

static inline void rt2x00rfkill_free(struct rt2x00_dev *rt2x00dev)
{
}
#endif /* CONFIG_RT2X00_LIB_RFKILL */

#endif /* RT2X00LIB_H */

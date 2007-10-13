/******************************************************************************
 *
 * Copyright(c) 2005 - 2007 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/
#ifndef __iwl_channel_h__
#define __iwl_channel_h__

#define IWL_NUM_SCAN_RATES         (2)

struct iwl_channel_tgd_info {
	u8 type;
	s8 max_power;
};

struct iwl_channel_tgh_info {
	s64 last_radar_time;
};

/* current Tx power values to use, one for each rate for each channel.
 * requested power is limited by:
 * -- regulatory EEPROM limits for this channel
 * -- hardware capabilities (clip-powers)
 * -- spectrum management
 * -- user preference (e.g. iwconfig)
 * when requested power is set, base power index must also be set. */
struct iwl_channel_power_info {
	struct iwl_tx_power tpc;	/* actual radio and DSP gain settings */
	s8 power_table_index;	/* actual (compenst'd) index into gain table */
	s8 base_power_index;	/* gain index for power at factory temp. */
	s8 requested_power;	/* power (dBm) requested for this chnl/rate */
};

/* current scan Tx power values to use, one for each scan rate for each
 * channel. */
struct iwl_scan_power_info {
	struct iwl_tx_power tpc;	/* actual radio and DSP gain settings */
	s8 power_table_index;	/* actual (compenst'd) index into gain table */
	s8 requested_power;	/* scan pwr (dBm) requested for chnl/rate */
};

/* Channel unlock period is 15 seconds. If no beacon or probe response
 * has been received within 15 seconds on a locked channel then the channel
 * remains locked. */
#define TX_UNLOCK_PERIOD 15

/* CSA lock period is 15 seconds.  If a CSA has been received on a channel in
 * the last 15 seconds, the channel is locked */
#define CSA_LOCK_PERIOD 15
/*
 * One for each channel, holds all channel setup data
 * Some of the fields (e.g. eeprom and flags/max_power_avg) are redundant
 *     with one another!
 */
#define IWL4965_MAX_RATE (33)

struct iwl_channel_info {
	struct iwl_channel_tgd_info tgd;
	struct iwl_channel_tgh_info tgh;
	struct iwl_eeprom_channel eeprom;	/* EEPROM regulatory limit */
	struct iwl_eeprom_channel fat_eeprom;	/* EEPROM regulatory limit for
						 * FAT channel */

	u8 channel;	  /* channel number */
	u8 flags;	  /* flags copied from EEPROM */
	s8 max_power_avg; /* (dBm) regul. eeprom, normal Tx, any rate */
	s8 curr_txpow;	  /* (dBm) regulatory/spectrum/user (not h/w) */
	s8 min_power;	  /* always 0 */
	s8 scan_power;	  /* (dBm) regul. eeprom, direct scans, any rate */

	u8 group_index;	  /* 0-4, maps channel to group1/2/3/4/5 */
	u8 band_index;	  /* 0-4, maps channel to band1/2/3/4/5 */
	u8 phymode;	  /* MODE_IEEE80211{A,B,G} */

	/* Radio/DSP gain settings for each "normal" data Tx rate.
	 * These include, in addition to RF and DSP gain, a few fields for
	 *   remembering/modifying gain settings (indexes). */
	struct iwl_channel_power_info power_info[IWL4965_MAX_RATE];

#if IWL == 4965
	/* FAT channel info */
	s8 fat_max_power_avg;	/* (dBm) regul. eeprom, normal Tx, any rate */
	s8 fat_curr_txpow;	/* (dBm) regulatory/spectrum/user (not h/w) */
	s8 fat_min_power;	/* always 0 */
	s8 fat_scan_power;	/* (dBm) eeprom, direct scans, any rate */
	u8 fat_flags;		/* flags copied from EEPROM */
	u8 fat_extension_channel;
#endif

	/* Radio/DSP gain settings for each scan rate, for directed scans. */
	struct iwl_scan_power_info scan_pwr_info[IWL_NUM_SCAN_RATES];
};

struct iwl_clip_group {
	/* maximum power level to prevent clipping for each rate, derived by
	 *   us from this band's saturation power in EEPROM */
	const s8 clip_powers[IWL_MAX_RATES];
};

static inline int is_channel_valid(const struct iwl_channel_info *ch_info)
{
	if (ch_info == NULL)
		return 0;
	return (ch_info->flags & EEPROM_CHANNEL_VALID) ? 1 : 0;
}

static inline int is_channel_narrow(const struct iwl_channel_info *ch_info)
{
	return (ch_info->flags & EEPROM_CHANNEL_NARROW) ? 1 : 0;
}

static inline int is_channel_radar(const struct iwl_channel_info *ch_info)
{
	return (ch_info->flags & EEPROM_CHANNEL_RADAR) ? 1 : 0;
}

static inline u8 is_channel_a_band(const struct iwl_channel_info *ch_info)
{
	return ch_info->phymode == MODE_IEEE80211A;
}

static inline u8 is_channel_bg_band(const struct iwl_channel_info *ch_info)
{
	return ((ch_info->phymode == MODE_IEEE80211B) ||
		(ch_info->phymode == MODE_IEEE80211G));
}

static inline int is_channel_passive(const struct iwl_channel_info *ch)
{
	return (!(ch->flags & EEPROM_CHANNEL_ACTIVE)) ? 1 : 0;
}

static inline int is_channel_ibss(const struct iwl_channel_info *ch)
{
	return ((ch->flags & EEPROM_CHANNEL_IBSS)) ? 1 : 0;
}

extern const struct iwl_channel_info *iwl_get_channel_info(
	const struct iwl_priv *priv, int phymode, u16 channel);

#endif

/*
	Copyright (C) 2004 - 2009 Ivo van Doorn <IvDoorn@gmail.com>
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
	Abstract: rt2x00 HT specific routines.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "rt2x00.h"
#include "rt2x00lib.h"

void rt2x00ht_create_tx_descriptor(struct queue_entry *entry,
				   struct txentry_desc *txdesc,
				   const struct rt2x00_rate *hwrate)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(entry->skb);
	struct ieee80211_tx_rate *txrate = &tx_info->control.rates[0];
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)entry->skb->data;

	if (tx_info->control.sta)
		txdesc->mpdu_density =
		    tx_info->control.sta->ht_cap.ampdu_density;
	else
		txdesc->mpdu_density = 0;

	txdesc->ba_size = 7;	/* FIXME: What value is needed? */

	txdesc->stbc =
	    (tx_info->flags & IEEE80211_TX_CTL_STBC) >> IEEE80211_TX_CTL_STBC_SHIFT;

	/*
	 * If IEEE80211_TX_RC_MCS is set txrate->idx just contains the
	 * mcs rate to be used
	 */
	if (txrate->flags & IEEE80211_TX_RC_MCS) {
		txdesc->mcs = txrate->idx;

		/*
		 * MIMO PS should be set to 1 for STA's using dynamic SM PS
		 * when using more then one tx stream (>MCS7).
		 */
		if (tx_info->control.sta && txdesc->mcs > 7 &&
		    ((tx_info->control.sta->ht_cap.cap &
		      IEEE80211_HT_CAP_SM_PS) >>
		     IEEE80211_HT_CAP_SM_PS_SHIFT) ==
		    WLAN_HT_CAP_SM_PS_DYNAMIC)
			__set_bit(ENTRY_TXD_HT_MIMO_PS, &txdesc->flags);
	} else {
		txdesc->mcs = rt2x00_get_rate_mcs(hwrate->mcs);
		if (txrate->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
			txdesc->mcs |= 0x08;
	}


	/*
	 * Convert flags
	 */
	if (tx_info->flags & IEEE80211_TX_CTL_AMPDU)
		__set_bit(ENTRY_TXD_HT_AMPDU, &txdesc->flags);

	/*
	 * Determine HT Mix/Greenfield rate mode
	 */
	if (txrate->flags & IEEE80211_TX_RC_MCS)
		txdesc->rate_mode = RATE_MODE_HT_MIX;
	if (txrate->flags & IEEE80211_TX_RC_GREEN_FIELD)
		txdesc->rate_mode = RATE_MODE_HT_GREENFIELD;

	/*
	 * Set 40Mhz mode if necessary (for legacy rates this will
	 * duplicate the frame to both channels).
	 */
	if (txrate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH ||
	    txrate->flags & IEEE80211_TX_RC_DUP_DATA)
		__set_bit(ENTRY_TXD_HT_BW_40, &txdesc->flags);
	if (txrate->flags & IEEE80211_TX_RC_SHORT_GI)
		__set_bit(ENTRY_TXD_HT_SHORT_GI, &txdesc->flags);

	/*
	 * Determine IFS values
	 * - Use TXOP_BACKOFF for management frames
	 * - Use TXOP_SIFS for fragment bursts
	 * - Use TXOP_HTTXOP for everything else
	 *
	 * Note: rt2800 devices won't use CTS protection (if used)
	 * for frames not transmitted with TXOP_HTTXOP
	 */
	if (ieee80211_is_mgmt(hdr->frame_control))
		txdesc->txop = TXOP_BACKOFF;
	else if (!(tx_info->flags & IEEE80211_TX_CTL_FIRST_FRAGMENT))
		txdesc->txop = TXOP_SIFS;
	else
		txdesc->txop = TXOP_HTTXOP;
}

u16 rt2x00ht_center_channel(struct rt2x00_dev *rt2x00dev,
			    struct ieee80211_conf *conf)
{
	struct hw_mode_spec *spec = &rt2x00dev->spec;
	int center_channel;
	u16 i;

	/*
	 * Initialize center channel to current channel.
	 */
	center_channel = spec->channels[conf->channel->hw_value].channel;

	/*
	 * Adjust center channel to HT40+ and HT40- operation.
	 */
	if (conf_is_ht40_plus(conf))
		center_channel += 2;
	else if (conf_is_ht40_minus(conf))
		center_channel -= (center_channel == 14) ? 1 : 2;

	for (i = 0; i < spec->num_channels; i++)
		if (spec->channels[i].channel == center_channel)
			return i;

	WARN_ON(1);
	return conf->channel->hw_value;
}

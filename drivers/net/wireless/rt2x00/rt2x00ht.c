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
	txdesc->stbc = 0;	/* FIXME: What value is needed? */

	txdesc->mcs = rt2x00_get_rate_mcs(hwrate->mcs);
	if (txrate->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
		txdesc->mcs |= 0x08;

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
	if (txrate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
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

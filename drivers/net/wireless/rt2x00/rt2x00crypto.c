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
	Abstract: rt2x00 crypto specific routines.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "rt2x00.h"
#include "rt2x00lib.h"

enum cipher rt2x00crypto_key_to_cipher(struct ieee80211_key_conf *key)
{
	switch (key->alg) {
	case ALG_WEP:
		if (key->keylen == LEN_WEP40)
			return CIPHER_WEP64;
		else
			return CIPHER_WEP128;
	case ALG_TKIP:
		return CIPHER_TKIP;
	case ALG_CCMP:
		return CIPHER_AES;
	default:
		return CIPHER_NONE;
	}
}

unsigned int rt2x00crypto_tx_overhead(struct ieee80211_tx_info *tx_info)
{
	struct ieee80211_key_conf *key = tx_info->control.hw_key;
	unsigned int overhead = 0;

	/*
	 * Extend frame length to include IV/EIV/ICV/MMIC,
	 * note that these lengths should only be added when
	 * mac80211 does not generate it.
	 */
	overhead += key->icv_len;

	if (!(key->flags & IEEE80211_KEY_FLAG_GENERATE_IV))
		overhead += key->iv_len;

	if (!(key->flags & IEEE80211_KEY_FLAG_GENERATE_MMIC)) {
		if (key->alg == ALG_TKIP)
			overhead += 8;
	}

	return overhead;
}

void rt2x00crypto_tx_remove_iv(struct sk_buff *skb, unsigned int iv_len)
{
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(skb);
	unsigned int header_length = ieee80211_get_hdrlen_from_skb(skb);

	if (unlikely(!iv_len))
		return;

	/* Copy IV/EIV data */
	if (iv_len >= 4)
		memcpy(&skbdesc->iv, skb->data + header_length, 4);
	if (iv_len >= 8)
		memcpy(&skbdesc->eiv, skb->data + header_length + 4, 4);

	/* Move ieee80211 header */
	memmove(skb->data + iv_len, skb->data, header_length);

	/* Pull buffer to correct size */
	skb_pull(skb, iv_len);

	/* IV/EIV data has officially be stripped */
	skbdesc->flags |= FRAME_DESC_IV_STRIPPED;
}

void rt2x00crypto_tx_insert_iv(struct sk_buff *skb)
{
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(skb);
	unsigned int header_length = ieee80211_get_hdrlen_from_skb(skb);
	const unsigned int iv_len =
	    ((!!(skbdesc->iv)) * 4) + ((!!(skbdesc->eiv)) * 4);

	if (!(skbdesc->flags & FRAME_DESC_IV_STRIPPED))
		return;

	skb_push(skb, iv_len);

	/* Move ieee80211 header */
	memmove(skb->data, skb->data + iv_len, header_length);

	/* Copy IV/EIV data */
	if (iv_len >= 4)
		memcpy(skb->data + header_length, &skbdesc->iv, 4);
	if (iv_len >= 8)
		memcpy(skb->data + header_length + 4, &skbdesc->eiv, 4);

	/* IV/EIV data has returned into the frame */
	skbdesc->flags &= ~FRAME_DESC_IV_STRIPPED;
}

void rt2x00crypto_rx_insert_iv(struct sk_buff *skb, unsigned int align,
			       unsigned int header_length,
			       struct rxdone_entry_desc *rxdesc)
{
	unsigned int payload_len = rxdesc->size - header_length;
	unsigned int iv_len;
	unsigned int icv_len;
	unsigned int transfer = 0;

	/*
	 * WEP64/WEP128: Provides IV & ICV
	 * TKIP: Provides IV/EIV & ICV
	 * AES: Provies IV/EIV & ICV
	 */
	switch (rxdesc->cipher) {
	case CIPHER_WEP64:
	case CIPHER_WEP128:
		iv_len = 4;
		icv_len = 4;
		break;
	case CIPHER_TKIP:
		iv_len = 8;
		icv_len = 4;
		break;
	case CIPHER_AES:
		iv_len = 8;
		icv_len = 8;
		break;
	default:
		/* Unsupport type */
		return;
	}

	/*
	 * Make room for new data, note that we increase both
	 * headsize and tailsize when required. The tailsize is
	 * only needed when ICV data needs to be inserted and
	 * the padding is smaller then the ICV data.
	 * When alignment requirements is greater then the
	 * ICV data we must trim the skb to the correct size
	 * because we need to remove the extra bytes.
	 */
	skb_push(skb, iv_len + align);
	if (align < icv_len)
		skb_put(skb, icv_len - align);
	else if (align > icv_len)
		skb_trim(skb, rxdesc->size + iv_len + icv_len);

	/* Move ieee80211 header */
	memmove(skb->data + transfer,
		skb->data + transfer + iv_len + align,
		header_length);
	transfer += header_length;

	/* Copy IV data */
	if (iv_len >= 4) {
		memcpy(skb->data + transfer, &rxdesc->iv, 4);
		transfer += 4;
	}

	/* Copy EIV data */
	if (iv_len >= 8) {
		memcpy(skb->data + transfer, &rxdesc->eiv, 4);
		transfer += 4;
	}

	/* Move payload */
	if (align) {
		memmove(skb->data + transfer,
			skb->data + transfer + align,
			payload_len);
	}

	/*
	 * NOTE: Always count the payload as transfered,
	 * even when alignment was set to zero. This is required
	 * for determining the correct offset for the ICV data.
	 */
	transfer += payload_len;

	/* Copy ICV data */
	if (icv_len >= 4) {
		memcpy(skb->data + transfer, &rxdesc->icv, 4);
		/*
		 * AES appends 8 bytes, we can't fill the upper
		 * 4 bytes, but mac80211 doesn't care about what
		 * we provide here anyway and strips it immediately.
		 */
		transfer += icv_len;
	}

	/* IV/EIV/ICV has been inserted into frame */
	rxdesc->size = transfer;
	rxdesc->flags &= ~RX_FLAG_IV_STRIPPED;
}

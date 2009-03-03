/*
 * Copyright (c) 2008-2009 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "ath9k.h"

struct ath9k_vif_iter_data {
	int count;
	u8 *addr;
};

static void ath9k_vif_iter(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct ath9k_vif_iter_data *iter_data = data;
	u8 *nbuf;

	nbuf = krealloc(iter_data->addr, (iter_data->count + 1) * ETH_ALEN,
			GFP_ATOMIC);
	if (nbuf == NULL)
		return;

	memcpy(nbuf + iter_data->count * ETH_ALEN, mac, ETH_ALEN);
	iter_data->addr = nbuf;
	iter_data->count++;
}

void ath9k_set_bssid_mask(struct ieee80211_hw *hw)
{
	struct ath_softc *sc = hw->priv;
	struct ath9k_vif_iter_data iter_data;
	int i, j;
	u8 mask[ETH_ALEN];

	/*
	 * Add primary MAC address even if it is not in active use since it
	 * will be configured to the hardware as the starting point and the
	 * BSSID mask will need to be changed if another address is active.
	 */
	iter_data.addr = kmalloc(ETH_ALEN, GFP_ATOMIC);
	if (iter_data.addr) {
		memcpy(iter_data.addr, sc->sc_ah->macaddr, ETH_ALEN);
		iter_data.count = 1;
	} else
		iter_data.count = 0;

	/* Get list of all active MAC addresses */
	ieee80211_iterate_active_interfaces_atomic(hw, ath9k_vif_iter,
						   &iter_data);

	/* Generate an address mask to cover all active addresses */
	memset(mask, 0, ETH_ALEN);
	for (i = 0; i < iter_data.count; i++) {
		u8 *a1 = iter_data.addr + i * ETH_ALEN;
		for (j = i + 1; j < iter_data.count; j++) {
			u8 *a2 = iter_data.addr + j * ETH_ALEN;
			mask[0] |= a1[0] ^ a2[0];
			mask[1] |= a1[1] ^ a2[1];
			mask[2] |= a1[2] ^ a2[2];
			mask[3] |= a1[3] ^ a2[3];
			mask[4] |= a1[4] ^ a2[4];
			mask[5] |= a1[5] ^ a2[5];
		}
	}

	kfree(iter_data.addr);

	/* Invert the mask and configure hardware */
	sc->bssidmask[0] = ~mask[0];
	sc->bssidmask[1] = ~mask[1];
	sc->bssidmask[2] = ~mask[2];
	sc->bssidmask[3] = ~mask[3];
	sc->bssidmask[4] = ~mask[4];
	sc->bssidmask[5] = ~mask[5];

	ath9k_hw_setbssidmask(sc);
}

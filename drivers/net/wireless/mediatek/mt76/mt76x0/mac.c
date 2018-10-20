/*
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/etherdevice.h>

#include "mt76x0.h"

void mt76x0_mac_set_protection(struct mt76x02_dev *dev, bool legacy_prot,
			       int ht_mode)
{
	int mode = ht_mode & IEEE80211_HT_OP_MODE_PROTECTION;
	bool non_gf = !!(ht_mode & IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT);
	u32 prot[6];
	bool ht_rts[4] = {};
	int i;

	prot[0] = MT_PROT_NAV_SHORT |
		  MT_PROT_TXOP_ALLOW_ALL |
		  MT_PROT_RTS_THR_EN;
	prot[1] = prot[0];
	if (legacy_prot)
		prot[1] |= MT_PROT_CTRL_CTS2SELF;

	prot[2] = prot[4] = MT_PROT_NAV_SHORT | MT_PROT_TXOP_ALLOW_BW20;
	prot[3] = prot[5] = MT_PROT_NAV_SHORT | MT_PROT_TXOP_ALLOW_ALL;

	if (legacy_prot) {
		prot[2] |= MT_PROT_RATE_CCK_11;
		prot[3] |= MT_PROT_RATE_CCK_11;
		prot[4] |= MT_PROT_RATE_CCK_11;
		prot[5] |= MT_PROT_RATE_CCK_11;
	} else {
		prot[2] |= MT_PROT_RATE_OFDM_24;
		prot[3] |= MT_PROT_RATE_DUP_OFDM_24;
		prot[4] |= MT_PROT_RATE_OFDM_24;
		prot[5] |= MT_PROT_RATE_DUP_OFDM_24;
	}

	switch (mode) {
	case IEEE80211_HT_OP_MODE_PROTECTION_NONE:
		break;

	case IEEE80211_HT_OP_MODE_PROTECTION_NONMEMBER:
		ht_rts[0] = ht_rts[1] = ht_rts[2] = ht_rts[3] = true;
		break;

	case IEEE80211_HT_OP_MODE_PROTECTION_20MHZ:
		ht_rts[1] = ht_rts[3] = true;
		break;

	case IEEE80211_HT_OP_MODE_PROTECTION_NONHT_MIXED:
		ht_rts[0] = ht_rts[1] = ht_rts[2] = ht_rts[3] = true;
		break;
	}

	if (non_gf)
		ht_rts[2] = ht_rts[3] = true;

	for (i = 0; i < 4; i++)
		if (ht_rts[i])
			prot[i + 2] |= MT_PROT_CTRL_RTS_CTS;

	for (i = 0; i < 6; i++)
		mt76_wr(dev, MT_CCK_PROT_CFG + i * 4, prot[i]);
}

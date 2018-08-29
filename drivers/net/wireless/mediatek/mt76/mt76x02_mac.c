/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
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

#include "mt76.h"
#include "mt76x02_regs.h"
#include "mt76x02_mac.h"

enum mt76x02_cipher_type
mt76x02_mac_get_key_info(struct ieee80211_key_conf *key, u8 *key_data)
{
	memset(key_data, 0, 32);
	if (!key)
		return MT_CIPHER_NONE;

	if (key->keylen > 32)
		return MT_CIPHER_NONE;

	memcpy(key_data, key->key, key->keylen);

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		return MT_CIPHER_WEP40;
	case WLAN_CIPHER_SUITE_WEP104:
		return MT_CIPHER_WEP104;
	case WLAN_CIPHER_SUITE_TKIP:
		return MT_CIPHER_TKIP;
	case WLAN_CIPHER_SUITE_CCMP:
		return MT_CIPHER_AES_CCMP;
	default:
		return MT_CIPHER_NONE;
	}
}
EXPORT_SYMBOL_GPL(mt76x02_mac_get_key_info);

int mt76x02_mac_shared_key_setup(struct mt76_dev *dev, u8 vif_idx, u8 key_idx,
				struct ieee80211_key_conf *key)
{
	enum mt76x02_cipher_type cipher;
	u8 key_data[32];
	u32 val;

	cipher = mt76x02_mac_get_key_info(key, key_data);
	if (cipher == MT_CIPHER_NONE && key)
		return -EOPNOTSUPP;

	val = __mt76_rr(dev, MT_SKEY_MODE(vif_idx));
	val &= ~(MT_SKEY_MODE_MASK << MT_SKEY_MODE_SHIFT(vif_idx, key_idx));
	val |= cipher << MT_SKEY_MODE_SHIFT(vif_idx, key_idx);
	__mt76_wr(dev, MT_SKEY_MODE(vif_idx), val);

	__mt76_wr_copy(dev, MT_SKEY(vif_idx, key_idx), key_data,
		       sizeof(key_data));

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_mac_shared_key_setup);

int mt76x02_mac_wcid_set_key(struct mt76_dev *dev, u8 idx,
			    struct ieee80211_key_conf *key)
{
	enum mt76x02_cipher_type cipher;
	u8 key_data[32];
	u8 iv_data[8];

	cipher = mt76x02_mac_get_key_info(key, key_data);
	if (cipher == MT_CIPHER_NONE && key)
		return -EOPNOTSUPP;

	__mt76_wr_copy(dev, MT_WCID_KEY(idx), key_data, sizeof(key_data));
	__mt76_rmw_field(dev, MT_WCID_ATTR(idx), MT_WCID_ATTR_PKEY_MODE, cipher);

	memset(iv_data, 0, sizeof(iv_data));
	if (key) {
		__mt76_rmw_field(dev, MT_WCID_ATTR(idx), MT_WCID_ATTR_PAIRWISE,
				 !!(key->flags & IEEE80211_KEY_FLAG_PAIRWISE));
		iv_data[3] = key->keyidx << 6;
		if (cipher >= MT_CIPHER_TKIP)
			iv_data[3] |= 0x20;
	}

	__mt76_wr_copy(dev, MT_WCID_IV(idx), iv_data, sizeof(iv_data));

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_mac_wcid_set_key);

void mt76x02_mac_wcid_setup(struct mt76_dev *dev, u8 idx, u8 vif_idx, u8 *mac)
{
	struct mt76_wcid_addr addr = {};
	u32 attr;

	attr = FIELD_PREP(MT_WCID_ATTR_BSS_IDX, vif_idx & 7) |
	       FIELD_PREP(MT_WCID_ATTR_BSS_IDX_EXT, !!(vif_idx & 8));

	__mt76_wr(dev, MT_WCID_ATTR(idx), attr);

	__mt76_wr(dev, MT_WCID_TX_RATE(idx), 0);
	__mt76_wr(dev, MT_WCID_TX_RATE(idx) + 4, 0);

	if (idx >= 128)
		return;

	if (mac)
		memcpy(addr.macaddr, mac, ETH_ALEN);

	__mt76_wr_copy(dev, MT_WCID_ADDR(idx), &addr, sizeof(addr));
}
EXPORT_SYMBOL_GPL(mt76x02_mac_wcid_setup);

void mt76x02_mac_wcid_set_drop(struct mt76_dev *dev, u8 idx, bool drop)
{
	u32 val = __mt76_rr(dev, MT_WCID_DROP(idx));
	u32 bit = MT_WCID_DROP_MASK(idx);

	/* prevent unnecessary writes */
	if ((val & bit) != (bit * drop))
		__mt76_wr(dev, MT_WCID_DROP(idx), (val & ~bit) | (bit * drop));
}
EXPORT_SYMBOL_GPL(mt76x02_mac_wcid_set_drop);

void mt76x02_txq_init(struct mt76_dev *dev, struct ieee80211_txq *txq)
{
	struct mt76_txq *mtxq;

	if (!txq)
		return;

	mtxq = (struct mt76_txq *) txq->drv_priv;
	if (txq->sta) {
		struct mt76x02_sta *sta;

		sta = (struct mt76x02_sta *) txq->sta->drv_priv;
		mtxq->wcid = &sta->wcid;
	} else {
		struct mt76x02_vif *mvif;

		mvif = (struct mt76x02_vif *) txq->vif->drv_priv;
		mtxq->wcid = &mvif->group_wcid;
	}

	mt76_txq_init(dev, txq);
}
EXPORT_SYMBOL_GPL(mt76x02_txq_init);

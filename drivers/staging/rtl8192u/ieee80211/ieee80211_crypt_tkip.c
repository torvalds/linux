/*
 * Host AP crypt: host-based TKIP encryption implementation for Host AP driver
 *
 * Copyright (c) 2003-2004, Jouni Malinen <jkmaline@cc.hut.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. See README and COPYING for
 * more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/string.h>

#include "ieee80211.h"

#include <linux/crypto.h>
	#include <linux/scatterlist.h>
#include <linux/crc32.h>

MODULE_AUTHOR("Jouni Malinen");
MODULE_DESCRIPTION("Host AP crypt: TKIP");
MODULE_LICENSE("GPL");

struct ieee80211_tkip_data {
#define TKIP_KEY_LEN 32
	u8 key[TKIP_KEY_LEN];
	int key_set;

	u32 tx_iv32;
	u16 tx_iv16;
	u16 tx_ttak[5];
	int tx_phase1_done;

	u32 rx_iv32;
	u16 rx_iv16;
	u16 rx_ttak[5];
	int rx_phase1_done;
	u32 rx_iv32_new;
	u16 rx_iv16_new;

	u32 dot11RSNAStatsTKIPReplays;
	u32 dot11RSNAStatsTKIPICVErrors;
	u32 dot11RSNAStatsTKIPLocalMICFailures;

	int key_idx;

	struct crypto_blkcipher *rx_tfm_arc4;
	struct crypto_hash *rx_tfm_michael;
	struct crypto_blkcipher *tx_tfm_arc4;
	struct crypto_hash *tx_tfm_michael;

	/* scratch buffers for virt_to_page() (crypto API) */
	u8 rx_hdr[16], tx_hdr[16];
};

static void *ieee80211_tkip_init(int key_idx)
{
	struct ieee80211_tkip_data *priv;

	priv = kzalloc(sizeof(*priv), GFP_ATOMIC);
	if (priv == NULL)
		goto fail;
	priv->key_idx = key_idx;

	priv->tx_tfm_arc4 = crypto_alloc_blkcipher("ecb(arc4)", 0,
			CRYPTO_ALG_ASYNC);
	if (IS_ERR(priv->tx_tfm_arc4)) {
		printk(KERN_DEBUG "ieee80211_crypt_tkip: could not allocate "
				"crypto API arc4\n");
		priv->tx_tfm_arc4 = NULL;
		goto fail;
	}

	priv->tx_tfm_michael = crypto_alloc_hash("michael_mic", 0,
			CRYPTO_ALG_ASYNC);
	if (IS_ERR(priv->tx_tfm_michael)) {
		printk(KERN_DEBUG "ieee80211_crypt_tkip: could not allocate "
				"crypto API michael_mic\n");
		priv->tx_tfm_michael = NULL;
		goto fail;
	}

	priv->rx_tfm_arc4 = crypto_alloc_blkcipher("ecb(arc4)", 0,
			CRYPTO_ALG_ASYNC);
	if (IS_ERR(priv->rx_tfm_arc4)) {
		printk(KERN_DEBUG "ieee80211_crypt_tkip: could not allocate "
				"crypto API arc4\n");
		priv->rx_tfm_arc4 = NULL;
		goto fail;
	}

	priv->rx_tfm_michael = crypto_alloc_hash("michael_mic", 0,
			CRYPTO_ALG_ASYNC);
	if (IS_ERR(priv->rx_tfm_michael)) {
		printk(KERN_DEBUG "ieee80211_crypt_tkip: could not allocate "
				"crypto API michael_mic\n");
		priv->rx_tfm_michael = NULL;
		goto fail;
	}

	return priv;

fail:
	if (priv) {
		if (priv->tx_tfm_michael)
			crypto_free_hash(priv->tx_tfm_michael);
		if (priv->tx_tfm_arc4)
			crypto_free_blkcipher(priv->tx_tfm_arc4);
		if (priv->rx_tfm_michael)
			crypto_free_hash(priv->rx_tfm_michael);
		if (priv->rx_tfm_arc4)
			crypto_free_blkcipher(priv->rx_tfm_arc4);
		kfree(priv);
	}

	return NULL;
}


static void ieee80211_tkip_deinit(void *priv)
{
	struct ieee80211_tkip_data *_priv = priv;

	if (_priv) {
		if (_priv->tx_tfm_michael)
			crypto_free_hash(_priv->tx_tfm_michael);
		if (_priv->tx_tfm_arc4)
			crypto_free_blkcipher(_priv->tx_tfm_arc4);
		if (_priv->rx_tfm_michael)
			crypto_free_hash(_priv->rx_tfm_michael);
		if (_priv->rx_tfm_arc4)
			crypto_free_blkcipher(_priv->rx_tfm_arc4);
	}
	kfree(priv);
}


static inline u16 RotR1(u16 val)
{
	return (val >> 1) | (val << 15);
}


static inline u8 Lo8(u16 val)
{
	return val & 0xff;
}


static inline u8 Hi8(u16 val)
{
	return val >> 8;
}


static inline u16 Lo16(u32 val)
{
	return val & 0xffff;
}


static inline u16 Hi16(u32 val)
{
	return val >> 16;
}


static inline u16 Mk16(u8 hi, u8 lo)
{
	return lo | (((u16) hi) << 8);
}


static inline u16 Mk16_le(u16 *v)
{
	return le16_to_cpu(*v);
}


static const u16 Sbox[256] = {
	0xC6A5, 0xF884, 0xEE99, 0xF68D, 0xFF0D, 0xD6BD, 0xDEB1, 0x9154,
	0x6050, 0x0203, 0xCEA9, 0x567D, 0xE719, 0xB562, 0x4DE6, 0xEC9A,
	0x8F45, 0x1F9D, 0x8940, 0xFA87, 0xEF15, 0xB2EB, 0x8EC9, 0xFB0B,
	0x41EC, 0xB367, 0x5FFD, 0x45EA, 0x23BF, 0x53F7, 0xE496, 0x9B5B,
	0x75C2, 0xE11C, 0x3DAE, 0x4C6A, 0x6C5A, 0x7E41, 0xF502, 0x834F,
	0x685C, 0x51F4, 0xD134, 0xF908, 0xE293, 0xAB73, 0x6253, 0x2A3F,
	0x080C, 0x9552, 0x4665, 0x9D5E, 0x3028, 0x37A1, 0x0A0F, 0x2FB5,
	0x0E09, 0x2436, 0x1B9B, 0xDF3D, 0xCD26, 0x4E69, 0x7FCD, 0xEA9F,
	0x121B, 0x1D9E, 0x5874, 0x342E, 0x362D, 0xDCB2, 0xB4EE, 0x5BFB,
	0xA4F6, 0x764D, 0xB761, 0x7DCE, 0x527B, 0xDD3E, 0x5E71, 0x1397,
	0xA6F5, 0xB968, 0x0000, 0xC12C, 0x4060, 0xE31F, 0x79C8, 0xB6ED,
	0xD4BE, 0x8D46, 0x67D9, 0x724B, 0x94DE, 0x98D4, 0xB0E8, 0x854A,
	0xBB6B, 0xC52A, 0x4FE5, 0xED16, 0x86C5, 0x9AD7, 0x6655, 0x1194,
	0x8ACF, 0xE910, 0x0406, 0xFE81, 0xA0F0, 0x7844, 0x25BA, 0x4BE3,
	0xA2F3, 0x5DFE, 0x80C0, 0x058A, 0x3FAD, 0x21BC, 0x7048, 0xF104,
	0x63DF, 0x77C1, 0xAF75, 0x4263, 0x2030, 0xE51A, 0xFD0E, 0xBF6D,
	0x814C, 0x1814, 0x2635, 0xC32F, 0xBEE1, 0x35A2, 0x88CC, 0x2E39,
	0x9357, 0x55F2, 0xFC82, 0x7A47, 0xC8AC, 0xBAE7, 0x322B, 0xE695,
	0xC0A0, 0x1998, 0x9ED1, 0xA37F, 0x4466, 0x547E, 0x3BAB, 0x0B83,
	0x8CCA, 0xC729, 0x6BD3, 0x283C, 0xA779, 0xBCE2, 0x161D, 0xAD76,
	0xDB3B, 0x6456, 0x744E, 0x141E, 0x92DB, 0x0C0A, 0x486C, 0xB8E4,
	0x9F5D, 0xBD6E, 0x43EF, 0xC4A6, 0x39A8, 0x31A4, 0xD337, 0xF28B,
	0xD532, 0x8B43, 0x6E59, 0xDAB7, 0x018C, 0xB164, 0x9CD2, 0x49E0,
	0xD8B4, 0xACFA, 0xF307, 0xCF25, 0xCAAF, 0xF48E, 0x47E9, 0x1018,
	0x6FD5, 0xF088, 0x4A6F, 0x5C72, 0x3824, 0x57F1, 0x73C7, 0x9751,
	0xCB23, 0xA17C, 0xE89C, 0x3E21, 0x96DD, 0x61DC, 0x0D86, 0x0F85,
	0xE090, 0x7C42, 0x71C4, 0xCCAA, 0x90D8, 0x0605, 0xF701, 0x1C12,
	0xC2A3, 0x6A5F, 0xAEF9, 0x69D0, 0x1791, 0x9958, 0x3A27, 0x27B9,
	0xD938, 0xEB13, 0x2BB3, 0x2233, 0xD2BB, 0xA970, 0x0789, 0x33A7,
	0x2DB6, 0x3C22, 0x1592, 0xC920, 0x8749, 0xAAFF, 0x5078, 0xA57A,
	0x038F, 0x59F8, 0x0980, 0x1A17, 0x65DA, 0xD731, 0x84C6, 0xD0B8,
	0x82C3, 0x29B0, 0x5A77, 0x1E11, 0x7BCB, 0xA8FC, 0x6DD6, 0x2C3A,
};


static inline u16 _S_(u16 v)
{
	u16 t = Sbox[Hi8(v)];
	return Sbox[Lo8(v)] ^ ((t << 8) | (t >> 8));
}


#define PHASE1_LOOP_COUNT 8


static void tkip_mixing_phase1(u16 *TTAK, const u8 *TK, const u8 *TA, u32 IV32)
{
	int i, j;

	/* Initialize the 80-bit TTAK from TSC (IV32) and TA[0..5] */
	TTAK[0] = Lo16(IV32);
	TTAK[1] = Hi16(IV32);
	TTAK[2] = Mk16(TA[1], TA[0]);
	TTAK[3] = Mk16(TA[3], TA[2]);
	TTAK[4] = Mk16(TA[5], TA[4]);

	for (i = 0; i < PHASE1_LOOP_COUNT; i++) {
		j = 2 * (i & 1);
		TTAK[0] += _S_(TTAK[4] ^ Mk16(TK[1 + j], TK[0 + j]));
		TTAK[1] += _S_(TTAK[0] ^ Mk16(TK[5 + j], TK[4 + j]));
		TTAK[2] += _S_(TTAK[1] ^ Mk16(TK[9 + j], TK[8 + j]));
		TTAK[3] += _S_(TTAK[2] ^ Mk16(TK[13 + j], TK[12 + j]));
		TTAK[4] += _S_(TTAK[3] ^ Mk16(TK[1 + j], TK[0 + j])) + i;
	}
}


static void tkip_mixing_phase2(u8 *WEPSeed, const u8 *TK, const u16 *TTAK,
			       u16 IV16)
{
	/*
	 * Make temporary area overlap WEP seed so that the final copy can be
	 * avoided on little endian hosts.
	 */
	u16 *PPK = (u16 *) &WEPSeed[4];

	/* Step 1 - make copy of TTAK and bring in TSC */
	PPK[0] = TTAK[0];
	PPK[1] = TTAK[1];
	PPK[2] = TTAK[2];
	PPK[3] = TTAK[3];
	PPK[4] = TTAK[4];
	PPK[5] = TTAK[4] + IV16;

	/* Step 2 - 96-bit bijective mixing using S-box */
	PPK[0] += _S_(PPK[5] ^ Mk16_le((u16 *) &TK[0]));
	PPK[1] += _S_(PPK[0] ^ Mk16_le((u16 *) &TK[2]));
	PPK[2] += _S_(PPK[1] ^ Mk16_le((u16 *) &TK[4]));
	PPK[3] += _S_(PPK[2] ^ Mk16_le((u16 *) &TK[6]));
	PPK[4] += _S_(PPK[3] ^ Mk16_le((u16 *) &TK[8]));
	PPK[5] += _S_(PPK[4] ^ Mk16_le((u16 *) &TK[10]));

	PPK[0] += RotR1(PPK[5] ^ Mk16_le((u16 *) &TK[12]));
	PPK[1] += RotR1(PPK[0] ^ Mk16_le((u16 *) &TK[14]));
	PPK[2] += RotR1(PPK[1]);
	PPK[3] += RotR1(PPK[2]);
	PPK[4] += RotR1(PPK[3]);
	PPK[5] += RotR1(PPK[4]);

	/*
	 * Step 3 - bring in last of TK bits, assign 24-bit WEP IV value
	 * WEPSeed[0..2] is transmitted as WEP IV
	 */
	WEPSeed[0] = Hi8(IV16);
	WEPSeed[1] = (Hi8(IV16) | 0x20) & 0x7F;
	WEPSeed[2] = Lo8(IV16);
	WEPSeed[3] = Lo8((PPK[5] ^ Mk16_le((u16 *) &TK[0])) >> 1);

#ifdef __BIG_ENDIAN
	{
		int i;

		for (i = 0; i < 6; i++)
			PPK[i] = (PPK[i] << 8) | (PPK[i] >> 8);
	}
#endif
}


static int ieee80211_tkip_encrypt(struct sk_buff *skb, int hdr_len, void *priv)
{
	struct ieee80211_tkip_data *tkey = priv;
	int len;
	u8 *pos;
	struct rtl_80211_hdr_4addr *hdr;
	cb_desc *tcb_desc = (cb_desc *)(skb->cb + MAX_DEV_ADDR_SIZE);
	struct blkcipher_desc desc = {.tfm = tkey->tx_tfm_arc4};
	int ret = 0;
	u8 rc4key[16],  *icv;
	u32 crc;
	struct scatterlist sg;

	if (skb_headroom(skb) < 8 || skb_tailroom(skb) < 4 ||
	    skb->len < hdr_len)
		return -1;

	hdr = (struct rtl_80211_hdr_4addr *) skb->data;

	if (!tcb_desc->bHwSec) {
		if (!tkey->tx_phase1_done) {
			tkip_mixing_phase1(tkey->tx_ttak, tkey->key, hdr->addr2,
					   tkey->tx_iv32);
			tkey->tx_phase1_done = 1;
		}
		tkip_mixing_phase2(rc4key, tkey->key, tkey->tx_ttak, tkey->tx_iv16);
	} else
		tkey->tx_phase1_done = 1;


	len = skb->len - hdr_len;
	pos = skb_push(skb, 8);
	memmove(pos, pos + 8, hdr_len);
	pos += hdr_len;

	if (tcb_desc->bHwSec) {
		*pos++ = Hi8(tkey->tx_iv16);
		*pos++ = (Hi8(tkey->tx_iv16) | 0x20) & 0x7F;
		*pos++ = Lo8(tkey->tx_iv16);
	} else {
		*pos++ = rc4key[0];
		*pos++ = rc4key[1];
		*pos++ = rc4key[2];
	}

	*pos++ = (tkey->key_idx << 6) | (1 << 5) /* Ext IV included */;
	*pos++ = tkey->tx_iv32 & 0xff;
	*pos++ = (tkey->tx_iv32 >> 8) & 0xff;
	*pos++ = (tkey->tx_iv32 >> 16) & 0xff;
	*pos++ = (tkey->tx_iv32 >> 24) & 0xff;

	if (!tcb_desc->bHwSec) {
		icv = skb_put(skb, 4);
		crc = ~crc32_le(~0, pos, len);
		icv[0] = crc;
		icv[1] = crc >> 8;
		icv[2] = crc >> 16;
		icv[3] = crc >> 24;
		crypto_blkcipher_setkey(tkey->tx_tfm_arc4, rc4key, 16);
		sg_init_one(&sg, pos, len+4);
		ret = crypto_blkcipher_encrypt(&desc, &sg, &sg, len + 4);
	}

	tkey->tx_iv16++;
	if (tkey->tx_iv16 == 0) {
		tkey->tx_phase1_done = 0;
		tkey->tx_iv32++;
	}

	if (!tcb_desc->bHwSec)
		return ret;
	else
		return 0;


}

static int ieee80211_tkip_decrypt(struct sk_buff *skb, int hdr_len, void *priv)
{
	struct ieee80211_tkip_data *tkey = priv;
	u8 keyidx, *pos;
	u32 iv32;
	u16 iv16;
	struct rtl_80211_hdr_4addr *hdr;
	cb_desc *tcb_desc = (cb_desc *)(skb->cb + MAX_DEV_ADDR_SIZE);
	struct blkcipher_desc desc = {.tfm = tkey->rx_tfm_arc4};
	u8 rc4key[16];
	u8 icv[4];
	u32 crc;
	struct scatterlist sg;
	int plen;

	if (skb->len < hdr_len + 8 + 4)
		return -1;

	hdr = (struct rtl_80211_hdr_4addr *) skb->data;
	pos = skb->data + hdr_len;
	keyidx = pos[3];
	if (!(keyidx & (1 << 5))) {
		if (net_ratelimit()) {
			printk(KERN_DEBUG "TKIP: received packet without ExtIV"
			       " flag from %pM\n", hdr->addr2);
		}
		return -2;
	}
	keyidx >>= 6;
	if (tkey->key_idx != keyidx) {
		printk(KERN_DEBUG "TKIP: RX tkey->key_idx=%d frame "
		       "keyidx=%d priv=%p\n", tkey->key_idx, keyidx, priv);
		return -6;
	}
	if (!tkey->key_set) {
		if (net_ratelimit()) {
			printk(KERN_DEBUG "TKIP: received packet from %pM"
			       " with keyid=%d that does not have a configured"
			       " key\n", hdr->addr2, keyidx);
		}
		return -3;
	}
	iv16 = (pos[0] << 8) | pos[2];
	iv32 = pos[4] | (pos[5] << 8) | (pos[6] << 16) | (pos[7] << 24);
	pos += 8;

	if (!tcb_desc->bHwSec) {
		if (iv32 < tkey->rx_iv32 ||
		(iv32 == tkey->rx_iv32 && iv16 <= tkey->rx_iv16)) {
			if (net_ratelimit()) {
				printk(KERN_DEBUG "TKIP: replay detected: STA=%pM"
				" previous TSC %08x%04x received TSC "
				"%08x%04x\n", hdr->addr2,
				tkey->rx_iv32, tkey->rx_iv16, iv32, iv16);
			}
			tkey->dot11RSNAStatsTKIPReplays++;
			return -4;
		}

		if (iv32 != tkey->rx_iv32 || !tkey->rx_phase1_done) {
			tkip_mixing_phase1(tkey->rx_ttak, tkey->key, hdr->addr2, iv32);
			tkey->rx_phase1_done = 1;
		}
		tkip_mixing_phase2(rc4key, tkey->key, tkey->rx_ttak, iv16);

		plen = skb->len - hdr_len - 12;

		crypto_blkcipher_setkey(tkey->rx_tfm_arc4, rc4key, 16);
		sg_init_one(&sg, pos, plen+4);

		if (crypto_blkcipher_decrypt(&desc, &sg, &sg, plen + 4)) {
			if (net_ratelimit()) {
				printk(KERN_DEBUG ": TKIP: failed to decrypt "
						"received packet from %pM\n",
						hdr->addr2);
			}
			return -7;
		}

		crc = ~crc32_le(~0, pos, plen);
		icv[0] = crc;
		icv[1] = crc >> 8;
		icv[2] = crc >> 16;
		icv[3] = crc >> 24;

		if (memcmp(icv, pos + plen, 4) != 0) {
			if (iv32 != tkey->rx_iv32) {
				/*
				 * Previously cached Phase1 result was already
				 * lost, so it needs to be recalculated for the
				 * next packet.
				 */
				tkey->rx_phase1_done = 0;
			}
			if (net_ratelimit()) {
				printk(KERN_DEBUG "TKIP: ICV error detected: STA="
				"%pM\n", hdr->addr2);
			}
			tkey->dot11RSNAStatsTKIPICVErrors++;
			return -5;
		}

	}

	/*
	 * Update real counters only after Michael MIC verification has
	 * completed.
	 */
	tkey->rx_iv32_new = iv32;
	tkey->rx_iv16_new = iv16;

	/* Remove IV and ICV */
	memmove(skb->data + 8, skb->data, hdr_len);
	skb_pull(skb, 8);
	skb_trim(skb, skb->len - 4);

	return keyidx;
}

static int michael_mic(struct crypto_hash *tfm_michael, u8 *key, u8 *hdr,
		       u8 *data, size_t data_len, u8 *mic)
{
	struct hash_desc desc;
	struct scatterlist sg[2];

	if (tfm_michael == NULL) {
		printk(KERN_WARNING "michael_mic: tfm_michael == NULL\n");
		return -1;
	}

	sg_init_table(sg, 2);
	sg_set_buf(&sg[0], hdr, 16);
	sg_set_buf(&sg[1], data, data_len);

	if (crypto_hash_setkey(tfm_michael, key, 8))
		return -1;

	desc.tfm = tfm_michael;
	desc.flags = 0;
	return crypto_hash_digest(&desc, sg, data_len + 16, mic);
}

static void michael_mic_hdr(struct sk_buff *skb, u8 *hdr)
{
	struct rtl_80211_hdr_4addr *hdr11;

	hdr11 = (struct rtl_80211_hdr_4addr *) skb->data;
	switch (le16_to_cpu(hdr11->frame_ctl) &
		(IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS)) {
	case IEEE80211_FCTL_TODS:
		memcpy(hdr, hdr11->addr3, ETH_ALEN); /* DA */
		memcpy(hdr + ETH_ALEN, hdr11->addr2, ETH_ALEN); /* SA */
		break;
	case IEEE80211_FCTL_FROMDS:
		memcpy(hdr, hdr11->addr1, ETH_ALEN); /* DA */
		memcpy(hdr + ETH_ALEN, hdr11->addr3, ETH_ALEN); /* SA */
		break;
	case IEEE80211_FCTL_FROMDS | IEEE80211_FCTL_TODS:
		memcpy(hdr, hdr11->addr3, ETH_ALEN); /* DA */
		memcpy(hdr + ETH_ALEN, hdr11->addr4, ETH_ALEN); /* SA */
		break;
	case 0:
		memcpy(hdr, hdr11->addr1, ETH_ALEN); /* DA */
		memcpy(hdr + ETH_ALEN, hdr11->addr2, ETH_ALEN); /* SA */
		break;
	}

	hdr[12] = 0; /* priority */

	hdr[13] = hdr[14] = hdr[15] = 0; /* reserved */
}


static int ieee80211_michael_mic_add(struct sk_buff *skb, int hdr_len, void *priv)
{
	struct ieee80211_tkip_data *tkey = priv;
	u8 *pos;
	struct rtl_80211_hdr_4addr *hdr;

	hdr = (struct rtl_80211_hdr_4addr *) skb->data;

	if (skb_tailroom(skb) < 8 || skb->len < hdr_len) {
		printk(KERN_DEBUG "Invalid packet for Michael MIC add "
		       "(tailroom=%d hdr_len=%d skb->len=%d)\n",
		       skb_tailroom(skb), hdr_len, skb->len);
		return -1;
	}

	michael_mic_hdr(skb, tkey->tx_hdr);

	// { david, 2006.9.1
	// fix the wpa process with wmm enabled.
	if (IEEE80211_QOS_HAS_SEQ(le16_to_cpu(hdr->frame_ctl)))
		tkey->tx_hdr[12] = *(skb->data + hdr_len - 2) & 0x07;
	// }
	pos = skb_put(skb, 8);

	if (michael_mic(tkey->tx_tfm_michael, &tkey->key[16], tkey->tx_hdr,
				skb->data + hdr_len, skb->len - 8 - hdr_len, pos))
		return -1;

	return 0;
}

static void ieee80211_michael_mic_failure(struct net_device *dev,
				       struct rtl_80211_hdr_4addr *hdr,
				       int keyidx)
{
	union iwreq_data wrqu;
	struct iw_michaelmicfailure ev;

	/* TODO: needed parameters: count, keyid, key type, TSC */
	memset(&ev, 0, sizeof(ev));
	ev.flags = keyidx & IW_MICFAILURE_KEY_ID;
	if (hdr->addr1[0] & 0x01)
		ev.flags |= IW_MICFAILURE_GROUP;
	else
		ev.flags |= IW_MICFAILURE_PAIRWISE;
	ev.src_addr.sa_family = ARPHRD_ETHER;
	memcpy(ev.src_addr.sa_data, hdr->addr2, ETH_ALEN);
	memset(&wrqu, 0, sizeof(wrqu));
	wrqu.data.length = sizeof(ev);
	wireless_send_event(dev, IWEVMICHAELMICFAILURE, &wrqu, (char *) &ev);
}

static int ieee80211_michael_mic_verify(struct sk_buff *skb, int keyidx,
					int hdr_len, void *priv)
{
	struct ieee80211_tkip_data *tkey = priv;
	u8 mic[8];
	struct rtl_80211_hdr_4addr *hdr;

	hdr = (struct rtl_80211_hdr_4addr *) skb->data;

	if (!tkey->key_set)
		return -1;

	michael_mic_hdr(skb, tkey->rx_hdr);
	// { david, 2006.9.1
	// fix the wpa process with wmm enabled.
	if (IEEE80211_QOS_HAS_SEQ(le16_to_cpu(hdr->frame_ctl)))
		tkey->rx_hdr[12] = *(skb->data + hdr_len - 2) & 0x07;
	// }

	if (michael_mic(tkey->rx_tfm_michael, &tkey->key[24], tkey->rx_hdr,
			skb->data + hdr_len, skb->len - 8 - hdr_len, mic))
		return -1;
	if (memcmp(mic, skb->data + skb->len - 8, 8) != 0) {
		struct rtl_80211_hdr_4addr *hdr;
		hdr = (struct rtl_80211_hdr_4addr *) skb->data;

		printk(KERN_DEBUG "%s: Michael MIC verification failed for "
		       "MSDU from %pM keyidx=%d\n",
		       skb->dev ? skb->dev->name : "N/A", hdr->addr2,
		       keyidx);
		if (skb->dev)
			ieee80211_michael_mic_failure(skb->dev, hdr, keyidx);
		tkey->dot11RSNAStatsTKIPLocalMICFailures++;
		return -1;
	}

	/*
	 * Update TSC counters for RX now that the packet verification has
	 * completed.
	 */
	tkey->rx_iv32 = tkey->rx_iv32_new;
	tkey->rx_iv16 = tkey->rx_iv16_new;

	skb_trim(skb, skb->len - 8);

	return 0;
}


static int ieee80211_tkip_set_key(void *key, int len, u8 *seq, void *priv)
{
	struct ieee80211_tkip_data *tkey = priv;
	int keyidx;
	struct crypto_hash *tfm = tkey->tx_tfm_michael;
	struct crypto_blkcipher *tfm2 = tkey->tx_tfm_arc4;
	struct crypto_hash *tfm3 = tkey->rx_tfm_michael;
	struct crypto_blkcipher *tfm4 = tkey->rx_tfm_arc4;

	keyidx = tkey->key_idx;
	memset(tkey, 0, sizeof(*tkey));
	tkey->key_idx = keyidx;
	tkey->tx_tfm_michael = tfm;
	tkey->tx_tfm_arc4 = tfm2;
	tkey->rx_tfm_michael = tfm3;
	tkey->rx_tfm_arc4 = tfm4;

	if (len == TKIP_KEY_LEN) {
		memcpy(tkey->key, key, TKIP_KEY_LEN);
		tkey->key_set = 1;
		tkey->tx_iv16 = 1; /* TSC is initialized to 1 */
		if (seq) {
			tkey->rx_iv32 = (seq[5] << 24) | (seq[4] << 16) |
				(seq[3] << 8) | seq[2];
			tkey->rx_iv16 = (seq[1] << 8) | seq[0];
		}
	} else if (len == 0)
		tkey->key_set = 0;
	else
		return -1;

	return 0;
}


static int ieee80211_tkip_get_key(void *key, int len, u8 *seq, void *priv)
{
	struct ieee80211_tkip_data *tkey = priv;

	if (len < TKIP_KEY_LEN)
		return -1;

	if (!tkey->key_set)
		return 0;
	memcpy(key, tkey->key, TKIP_KEY_LEN);

	if (seq) {
		/* Return the sequence number of the last transmitted frame. */
		u16 iv16 = tkey->tx_iv16;
		u32 iv32 = tkey->tx_iv32;

		if (iv16 == 0)
			iv32--;
		iv16--;
		seq[0] = tkey->tx_iv16;
		seq[1] = tkey->tx_iv16 >> 8;
		seq[2] = tkey->tx_iv32;
		seq[3] = tkey->tx_iv32 >> 8;
		seq[4] = tkey->tx_iv32 >> 16;
		seq[5] = tkey->tx_iv32 >> 24;
	}

	return TKIP_KEY_LEN;
}


static char *ieee80211_tkip_print_stats(char *p, void *priv)
{
	struct ieee80211_tkip_data *tkip = priv;

	p += sprintf(p, "key[%d] alg=TKIP key_set=%d "
		     "tx_pn=%02x%02x%02x%02x%02x%02x "
		     "rx_pn=%02x%02x%02x%02x%02x%02x "
		     "replays=%d icv_errors=%d local_mic_failures=%d\n",
		     tkip->key_idx, tkip->key_set,
		     (tkip->tx_iv32 >> 24) & 0xff,
		     (tkip->tx_iv32 >> 16) & 0xff,
		     (tkip->tx_iv32 >> 8) & 0xff,
		     tkip->tx_iv32 & 0xff,
		     (tkip->tx_iv16 >> 8) & 0xff,
		     tkip->tx_iv16 & 0xff,
		     (tkip->rx_iv32 >> 24) & 0xff,
		     (tkip->rx_iv32 >> 16) & 0xff,
		     (tkip->rx_iv32 >> 8) & 0xff,
		     tkip->rx_iv32 & 0xff,
		     (tkip->rx_iv16 >> 8) & 0xff,
		     tkip->rx_iv16 & 0xff,
		     tkip->dot11RSNAStatsTKIPReplays,
		     tkip->dot11RSNAStatsTKIPICVErrors,
		     tkip->dot11RSNAStatsTKIPLocalMICFailures);
	return p;
}


static struct ieee80211_crypto_ops ieee80211_crypt_tkip = {
	.name			= "TKIP",
	.init			= ieee80211_tkip_init,
	.deinit			= ieee80211_tkip_deinit,
	.encrypt_mpdu		= ieee80211_tkip_encrypt,
	.decrypt_mpdu		= ieee80211_tkip_decrypt,
	.encrypt_msdu		= ieee80211_michael_mic_add,
	.decrypt_msdu		= ieee80211_michael_mic_verify,
	.set_key		= ieee80211_tkip_set_key,
	.get_key		= ieee80211_tkip_get_key,
	.print_stats		= ieee80211_tkip_print_stats,
	.extra_prefix_len	= 4 + 4, /* IV + ExtIV */
	.extra_postfix_len	= 8 + 4, /* MIC + ICV */
	.owner			= THIS_MODULE,
};

int __init ieee80211_crypto_tkip_init(void)
{
	return ieee80211_register_crypto_ops(&ieee80211_crypt_tkip);
}

void __exit ieee80211_crypto_tkip_exit(void)
{
	ieee80211_unregister_crypto_ops(&ieee80211_crypt_tkip);
}

void ieee80211_tkip_null(void)
{
//    printk("============>%s()\n", __func__);
	return;
}

// SPDX-License-Identifier: GPL-2.0-or-later
/*

  Broadcom B43 wireless driver

  Transmission (TX/RX) related functions.

  Copyright (C) 2005 Martin Langer <martin-langer@gmx.de>
  Copyright (C) 2005 Stefano Brivio <stefano.brivio@polimi.it>
  Copyright (C) 2005, 2006 Michael Buesch <m@bues.ch>
  Copyright (C) 2005 Danny van Dyk <kugelfang@gentoo.org>
  Copyright (C) 2005 Andreas Jaggi <andreas.jaggi@waterwave.ch>


*/

#include "xmit.h"
#include "phy_common.h"
#include "dma.h"
#include "pio.h"

static const struct b43_tx_legacy_rate_phy_ctl_entry b43_tx_legacy_rate_phy_ctl[] = {
	{ B43_CCK_RATE_1MB,	0x0,			0x0 },
	{ B43_CCK_RATE_2MB,	0x0,			0x1 },
	{ B43_CCK_RATE_5MB,	0x0,			0x2 },
	{ B43_CCK_RATE_11MB,	0x0,			0x3 },
	{ B43_OFDM_RATE_6MB,	B43_TXH_PHY1_CRATE_1_2,	B43_TXH_PHY1_MODUL_BPSK },
	{ B43_OFDM_RATE_9MB,	B43_TXH_PHY1_CRATE_3_4,	B43_TXH_PHY1_MODUL_BPSK },
	{ B43_OFDM_RATE_12MB,	B43_TXH_PHY1_CRATE_1_2,	B43_TXH_PHY1_MODUL_QPSK },
	{ B43_OFDM_RATE_18MB,	B43_TXH_PHY1_CRATE_3_4,	B43_TXH_PHY1_MODUL_QPSK },
	{ B43_OFDM_RATE_24MB,	B43_TXH_PHY1_CRATE_1_2,	B43_TXH_PHY1_MODUL_QAM16 },
	{ B43_OFDM_RATE_36MB,	B43_TXH_PHY1_CRATE_3_4,	B43_TXH_PHY1_MODUL_QAM16 },
	{ B43_OFDM_RATE_48MB,	B43_TXH_PHY1_CRATE_2_3,	B43_TXH_PHY1_MODUL_QAM64 },
	{ B43_OFDM_RATE_54MB,	B43_TXH_PHY1_CRATE_3_4,	B43_TXH_PHY1_MODUL_QAM64 },
};

static const struct b43_tx_legacy_rate_phy_ctl_entry *
b43_tx_legacy_rate_phy_ctl_ent(u8 bitrate)
{
	const struct b43_tx_legacy_rate_phy_ctl_entry *e;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(b43_tx_legacy_rate_phy_ctl); i++) {
		e = &(b43_tx_legacy_rate_phy_ctl[i]);
		if (e->bitrate == bitrate)
			return e;
	}

	B43_WARN_ON(1);
	return NULL;
}

/* Extract the bitrate index out of a CCK PLCP header. */
static int b43_plcp_get_bitrate_idx_cck(struct b43_plcp_hdr6 *plcp)
{
	switch (plcp->raw[0]) {
	case 0x0A:
		return 0;
	case 0x14:
		return 1;
	case 0x37:
		return 2;
	case 0x6E:
		return 3;
	}
	return -1;
}

/* Extract the bitrate index out of an OFDM PLCP header. */
static int b43_plcp_get_bitrate_idx_ofdm(struct b43_plcp_hdr6 *plcp, bool ghz5)
{
	/* For 2 GHz band first OFDM rate is at index 4, see main.c */
	int base = ghz5 ? 0 : 4;

	switch (plcp->raw[0] & 0xF) {
	case 0xB:
		return base + 0;
	case 0xF:
		return base + 1;
	case 0xA:
		return base + 2;
	case 0xE:
		return base + 3;
	case 0x9:
		return base + 4;
	case 0xD:
		return base + 5;
	case 0x8:
		return base + 6;
	case 0xC:
		return base + 7;
	}
	return -1;
}

u8 b43_plcp_get_ratecode_cck(const u8 bitrate)
{
	switch (bitrate) {
	case B43_CCK_RATE_1MB:
		return 0x0A;
	case B43_CCK_RATE_2MB:
		return 0x14;
	case B43_CCK_RATE_5MB:
		return 0x37;
	case B43_CCK_RATE_11MB:
		return 0x6E;
	}
	B43_WARN_ON(1);
	return 0;
}

u8 b43_plcp_get_ratecode_ofdm(const u8 bitrate)
{
	switch (bitrate) {
	case B43_OFDM_RATE_6MB:
		return 0xB;
	case B43_OFDM_RATE_9MB:
		return 0xF;
	case B43_OFDM_RATE_12MB:
		return 0xA;
	case B43_OFDM_RATE_18MB:
		return 0xE;
	case B43_OFDM_RATE_24MB:
		return 0x9;
	case B43_OFDM_RATE_36MB:
		return 0xD;
	case B43_OFDM_RATE_48MB:
		return 0x8;
	case B43_OFDM_RATE_54MB:
		return 0xC;
	}
	B43_WARN_ON(1);
	return 0;
}

void b43_generate_plcp_hdr(struct b43_plcp_hdr4 *plcp,
			   const u16 octets, const u8 bitrate)
{
	__u8 *raw = plcp->raw;

	if (b43_is_ofdm_rate(bitrate)) {
		u32 d;

		d = b43_plcp_get_ratecode_ofdm(bitrate);
		B43_WARN_ON(octets & 0xF000);
		d |= (octets << 5);
		plcp->data = cpu_to_le32(d);
	} else {
		u32 plen;

		plen = octets * 16 / bitrate;
		if ((octets * 16 % bitrate) > 0) {
			plen++;
			if ((bitrate == B43_CCK_RATE_11MB)
			    && ((octets * 8 % 11) < 4)) {
				raw[1] = 0x84;
			} else
				raw[1] = 0x04;
		} else
			raw[1] = 0x04;
		plcp->data |= cpu_to_le32(plen << 16);
		raw[0] = b43_plcp_get_ratecode_cck(bitrate);
	}
}

/* TODO: verify if needed for SSLPN or LCN  */
static u16 b43_generate_tx_phy_ctl1(struct b43_wldev *dev, u8 bitrate)
{
	const struct b43_phy *phy = &dev->phy;
	const struct b43_tx_legacy_rate_phy_ctl_entry *e;
	u16 control = 0;
	u16 bw;

	if (phy->type == B43_PHYTYPE_LP)
		bw = B43_TXH_PHY1_BW_20;
	else /* FIXME */
		bw = B43_TXH_PHY1_BW_20;

	if (0) { /* FIXME: MIMO */
	} else if (b43_is_cck_rate(bitrate) && phy->type != B43_PHYTYPE_LP) {
		control = bw;
	} else {
		control = bw;
		e = b43_tx_legacy_rate_phy_ctl_ent(bitrate);
		if (e) {
			control |= e->coding_rate;
			control |= e->modulation;
		}
		control |= B43_TXH_PHY1_MODE_SISO;
	}

	return control;
}

static u8 b43_calc_fallback_rate(u8 bitrate, int gmode)
{
	switch (bitrate) {
	case B43_CCK_RATE_1MB:
		return B43_CCK_RATE_1MB;
	case B43_CCK_RATE_2MB:
		return B43_CCK_RATE_1MB;
	case B43_CCK_RATE_5MB:
		return B43_CCK_RATE_2MB;
	case B43_CCK_RATE_11MB:
		return B43_CCK_RATE_5MB;
	/*
	 * Don't just fallback to CCK; it may be in 5GHz operation
	 * and falling back to CCK won't work out very well.
	 */
	case B43_OFDM_RATE_6MB:
		if (gmode)
			return B43_CCK_RATE_5MB;
		else
			return B43_OFDM_RATE_6MB;
	case B43_OFDM_RATE_9MB:
		return B43_OFDM_RATE_6MB;
	case B43_OFDM_RATE_12MB:
		return B43_OFDM_RATE_9MB;
	case B43_OFDM_RATE_18MB:
		return B43_OFDM_RATE_12MB;
	case B43_OFDM_RATE_24MB:
		return B43_OFDM_RATE_18MB;
	case B43_OFDM_RATE_36MB:
		return B43_OFDM_RATE_24MB;
	case B43_OFDM_RATE_48MB:
		return B43_OFDM_RATE_36MB;
	case B43_OFDM_RATE_54MB:
		return B43_OFDM_RATE_48MB;
	}
	B43_WARN_ON(1);
	return 0;
}

/* Generate a TX data header. */
int b43_generate_txhdr(struct b43_wldev *dev,
		       u8 *_txhdr,
		       struct sk_buff *skb_frag,
		       struct ieee80211_tx_info *info,
		       u16 cookie)
{
	const unsigned char *fragment_data = skb_frag->data;
	unsigned int fragment_len = skb_frag->len;
	struct b43_txhdr *txhdr = (struct b43_txhdr *)_txhdr;
	const struct b43_phy *phy = &dev->phy;
	const struct ieee80211_hdr *wlhdr =
	    (const struct ieee80211_hdr *)fragment_data;
	int use_encryption = !!info->control.hw_key;
	__le16 fctl = wlhdr->frame_control;
	struct ieee80211_rate *fbrate;
	u8 rate, rate_fb;
	int rate_ofdm, rate_fb_ofdm;
	unsigned int plcp_fragment_len;
	u32 mac_ctl = 0;
	u16 phy_ctl = 0;
	bool fill_phy_ctl1 = (phy->type == B43_PHYTYPE_LP ||
			      phy->type == B43_PHYTYPE_N ||
			      phy->type == B43_PHYTYPE_HT);
	u8 extra_ft = 0;
	struct ieee80211_rate *txrate;
	struct ieee80211_tx_rate *rates;

	memset(txhdr, 0, sizeof(*txhdr));

	txrate = ieee80211_get_tx_rate(dev->wl->hw, info);
	rate = txrate ? txrate->hw_value : B43_CCK_RATE_1MB;
	rate_ofdm = b43_is_ofdm_rate(rate);
	fbrate = ieee80211_get_alt_retry_rate(dev->wl->hw, info, 0) ? : txrate;
	rate_fb = fbrate->hw_value;
	rate_fb_ofdm = b43_is_ofdm_rate(rate_fb);

	if (rate_ofdm)
		txhdr->phy_rate = b43_plcp_get_ratecode_ofdm(rate);
	else
		txhdr->phy_rate = b43_plcp_get_ratecode_cck(rate);
	txhdr->mac_frame_ctl = wlhdr->frame_control;
	memcpy(txhdr->tx_receiver, wlhdr->addr1, ETH_ALEN);

	/* Calculate duration for fallback rate */
	if ((rate_fb == rate) ||
	    (wlhdr->duration_id & cpu_to_le16(0x8000)) ||
	    (wlhdr->duration_id == cpu_to_le16(0))) {
		/* If the fallback rate equals the normal rate or the
		 * dur_id field contains an AID, CFP magic or 0,
		 * use the original dur_id field. */
		txhdr->dur_fb = wlhdr->duration_id;
	} else {
		txhdr->dur_fb = ieee80211_generic_frame_duration(
			dev->wl->hw, info->control.vif, info->band,
			fragment_len, fbrate);
	}

	plcp_fragment_len = fragment_len + FCS_LEN;
	if (use_encryption) {
		u8 key_idx = info->control.hw_key->hw_key_idx;
		struct b43_key *key;
		int wlhdr_len;
		size_t iv_len;

		B43_WARN_ON(key_idx >= ARRAY_SIZE(dev->key));
		key = &(dev->key[key_idx]);

		if (unlikely(!key->keyconf)) {
			/* This key is invalid. This might only happen
			 * in a short timeframe after machine resume before
			 * we were able to reconfigure keys.
			 * Drop this packet completely. Do not transmit it
			 * unencrypted to avoid leaking information. */
			return -ENOKEY;
		}

		/* Hardware appends ICV. */
		plcp_fragment_len += info->control.hw_key->icv_len;

		key_idx = b43_kidx_to_fw(dev, key_idx);
		mac_ctl |= (key_idx << B43_TXH_MAC_KEYIDX_SHIFT) &
			   B43_TXH_MAC_KEYIDX;
		mac_ctl |= (key->algorithm << B43_TXH_MAC_KEYALG_SHIFT) &
			   B43_TXH_MAC_KEYALG;
		wlhdr_len = ieee80211_hdrlen(fctl);
		if (key->algorithm == B43_SEC_ALGO_TKIP) {
			u16 phase1key[5];
			int i;
			/* we give the phase1key and iv16 here, the key is stored in
			 * shm. With that the hardware can do phase 2 and encryption.
			 */
			ieee80211_get_tkip_p1k(info->control.hw_key, skb_frag, phase1key);
			/* phase1key is in host endian. Copy to little-endian txhdr->iv. */
			for (i = 0; i < 5; i++) {
				txhdr->iv[i * 2 + 0] = phase1key[i];
				txhdr->iv[i * 2 + 1] = phase1key[i] >> 8;
			}
			/* iv16 */
			memcpy(txhdr->iv + 10, ((u8 *) wlhdr) + wlhdr_len, 3);
		} else {
			iv_len = min_t(size_t, info->control.hw_key->iv_len,
				     ARRAY_SIZE(txhdr->iv));
			memcpy(txhdr->iv, ((u8 *) wlhdr) + wlhdr_len, iv_len);
		}
	}
	switch (dev->fw.hdr_format) {
	case B43_FW_HDR_598:
		b43_generate_plcp_hdr((struct b43_plcp_hdr4 *)(&txhdr->format_598.plcp),
				      plcp_fragment_len, rate);
		break;
	case B43_FW_HDR_351:
		b43_generate_plcp_hdr((struct b43_plcp_hdr4 *)(&txhdr->format_351.plcp),
				      plcp_fragment_len, rate);
		break;
	case B43_FW_HDR_410:
		b43_generate_plcp_hdr((struct b43_plcp_hdr4 *)(&txhdr->format_410.plcp),
				      plcp_fragment_len, rate);
		break;
	}
	b43_generate_plcp_hdr((struct b43_plcp_hdr4 *)(&txhdr->plcp_fb),
			      plcp_fragment_len, rate_fb);

	/* Extra Frame Types */
	if (rate_fb_ofdm)
		extra_ft |= B43_TXH_EFT_FB_OFDM;
	else
		extra_ft |= B43_TXH_EFT_FB_CCK;

	/* Set channel radio code. Note that the micrcode ORs 0x100 to
	 * this value before comparing it to the value in SHM, if this
	 * is a 5Ghz packet.
	 */
	txhdr->chan_radio_code = phy->channel;

	/* PHY TX Control word */
	if (rate_ofdm)
		phy_ctl |= B43_TXH_PHY_ENC_OFDM;
	else
		phy_ctl |= B43_TXH_PHY_ENC_CCK;
	if (info->control.rates[0].flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
		phy_ctl |= B43_TXH_PHY_SHORTPRMBL;

	switch (b43_ieee80211_antenna_sanitize(dev, 0)) {
	case 0: /* Default */
		phy_ctl |= B43_TXH_PHY_ANT01AUTO;
		break;
	case 1: /* Antenna 0 */
		phy_ctl |= B43_TXH_PHY_ANT0;
		break;
	case 2: /* Antenna 1 */
		phy_ctl |= B43_TXH_PHY_ANT1;
		break;
	case 3: /* Antenna 2 */
		phy_ctl |= B43_TXH_PHY_ANT2;
		break;
	case 4: /* Antenna 3 */
		phy_ctl |= B43_TXH_PHY_ANT3;
		break;
	default:
		B43_WARN_ON(1);
	}

	rates = info->control.rates;
	/* MAC control */
	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK))
		mac_ctl |= B43_TXH_MAC_ACK;
	/* use hardware sequence counter as the non-TID counter */
	if (info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ)
		mac_ctl |= B43_TXH_MAC_HWSEQ;
	if (info->flags & IEEE80211_TX_CTL_FIRST_FRAGMENT)
		mac_ctl |= B43_TXH_MAC_STMSDU;
	if (!phy->gmode)
		mac_ctl |= B43_TXH_MAC_5GHZ;

	/* Overwrite rates[0].count to make the retry calculation
	 * in the tx status easier. need the actual retry limit to
	 * detect whether the fallback rate was used.
	 */
	if ((rates[0].flags & IEEE80211_TX_RC_USE_RTS_CTS) ||
	    (rates[0].count <= dev->wl->hw->conf.long_frame_max_tx_count)) {
		rates[0].count = dev->wl->hw->conf.long_frame_max_tx_count;
		mac_ctl |= B43_TXH_MAC_LONGFRAME;
	} else {
		rates[0].count = dev->wl->hw->conf.short_frame_max_tx_count;
	}

	/* Generate the RTS or CTS-to-self frame */
	if ((rates[0].flags & IEEE80211_TX_RC_USE_RTS_CTS) ||
	    (rates[0].flags & IEEE80211_TX_RC_USE_CTS_PROTECT)) {
		unsigned int len;
		struct ieee80211_hdr *uninitialized_var(hdr);
		int rts_rate, rts_rate_fb;
		int rts_rate_ofdm, rts_rate_fb_ofdm;
		struct b43_plcp_hdr6 *uninitialized_var(plcp);
		struct ieee80211_rate *rts_cts_rate;

		rts_cts_rate = ieee80211_get_rts_cts_rate(dev->wl->hw, info);

		rts_rate = rts_cts_rate ? rts_cts_rate->hw_value : B43_CCK_RATE_1MB;
		rts_rate_ofdm = b43_is_ofdm_rate(rts_rate);
		rts_rate_fb = b43_calc_fallback_rate(rts_rate, phy->gmode);
		rts_rate_fb_ofdm = b43_is_ofdm_rate(rts_rate_fb);

		if (rates[0].flags & IEEE80211_TX_RC_USE_CTS_PROTECT) {
			struct ieee80211_cts *uninitialized_var(cts);

			switch (dev->fw.hdr_format) {
			case B43_FW_HDR_598:
				cts = (struct ieee80211_cts *)
					(txhdr->format_598.rts_frame);
				break;
			case B43_FW_HDR_351:
				cts = (struct ieee80211_cts *)
					(txhdr->format_351.rts_frame);
				break;
			case B43_FW_HDR_410:
				cts = (struct ieee80211_cts *)
					(txhdr->format_410.rts_frame);
				break;
			}
			ieee80211_ctstoself_get(dev->wl->hw, info->control.vif,
						fragment_data, fragment_len,
						info, cts);
			mac_ctl |= B43_TXH_MAC_SENDCTS;
			len = sizeof(struct ieee80211_cts);
		} else {
			struct ieee80211_rts *uninitialized_var(rts);

			switch (dev->fw.hdr_format) {
			case B43_FW_HDR_598:
				rts = (struct ieee80211_rts *)
					(txhdr->format_598.rts_frame);
				break;
			case B43_FW_HDR_351:
				rts = (struct ieee80211_rts *)
					(txhdr->format_351.rts_frame);
				break;
			case B43_FW_HDR_410:
				rts = (struct ieee80211_rts *)
					(txhdr->format_410.rts_frame);
				break;
			}
			ieee80211_rts_get(dev->wl->hw, info->control.vif,
					  fragment_data, fragment_len,
					  info, rts);
			mac_ctl |= B43_TXH_MAC_SENDRTS;
			len = sizeof(struct ieee80211_rts);
		}
		len += FCS_LEN;

		/* Generate the PLCP headers for the RTS/CTS frame */
		switch (dev->fw.hdr_format) {
		case B43_FW_HDR_598:
			plcp = &txhdr->format_598.rts_plcp;
			break;
		case B43_FW_HDR_351:
			plcp = &txhdr->format_351.rts_plcp;
			break;
		case B43_FW_HDR_410:
			plcp = &txhdr->format_410.rts_plcp;
			break;
		}
		b43_generate_plcp_hdr((struct b43_plcp_hdr4 *)plcp,
				      len, rts_rate);
		plcp = &txhdr->rts_plcp_fb;
		b43_generate_plcp_hdr((struct b43_plcp_hdr4 *)plcp,
				      len, rts_rate_fb);

		switch (dev->fw.hdr_format) {
		case B43_FW_HDR_598:
			hdr = (struct ieee80211_hdr *)
				(&txhdr->format_598.rts_frame);
			break;
		case B43_FW_HDR_351:
			hdr = (struct ieee80211_hdr *)
				(&txhdr->format_351.rts_frame);
			break;
		case B43_FW_HDR_410:
			hdr = (struct ieee80211_hdr *)
				(&txhdr->format_410.rts_frame);
			break;
		}
		txhdr->rts_dur_fb = hdr->duration_id;

		if (rts_rate_ofdm) {
			extra_ft |= B43_TXH_EFT_RTS_OFDM;
			txhdr->phy_rate_rts =
			    b43_plcp_get_ratecode_ofdm(rts_rate);
		} else {
			extra_ft |= B43_TXH_EFT_RTS_CCK;
			txhdr->phy_rate_rts =
			    b43_plcp_get_ratecode_cck(rts_rate);
		}
		if (rts_rate_fb_ofdm)
			extra_ft |= B43_TXH_EFT_RTSFB_OFDM;
		else
			extra_ft |= B43_TXH_EFT_RTSFB_CCK;

		if (rates[0].flags & IEEE80211_TX_RC_USE_RTS_CTS &&
		    fill_phy_ctl1) {
			txhdr->phy_ctl1_rts = cpu_to_le16(
				b43_generate_tx_phy_ctl1(dev, rts_rate));
			txhdr->phy_ctl1_rts_fb = cpu_to_le16(
				b43_generate_tx_phy_ctl1(dev, rts_rate_fb));
		}
	}

	/* Magic cookie */
	switch (dev->fw.hdr_format) {
	case B43_FW_HDR_598:
		txhdr->format_598.cookie = cpu_to_le16(cookie);
		break;
	case B43_FW_HDR_351:
		txhdr->format_351.cookie = cpu_to_le16(cookie);
		break;
	case B43_FW_HDR_410:
		txhdr->format_410.cookie = cpu_to_le16(cookie);
		break;
	}

	if (fill_phy_ctl1) {
		txhdr->phy_ctl1 =
			cpu_to_le16(b43_generate_tx_phy_ctl1(dev, rate));
		txhdr->phy_ctl1_fb =
			cpu_to_le16(b43_generate_tx_phy_ctl1(dev, rate_fb));
	}

	/* Apply the bitfields */
	txhdr->mac_ctl = cpu_to_le32(mac_ctl);
	txhdr->phy_ctl = cpu_to_le16(phy_ctl);
	txhdr->extra_ft = extra_ft;

	return 0;
}

static s8 b43_rssi_postprocess(struct b43_wldev *dev,
			       u8 in_rssi, int ofdm,
			       int adjust_2053, int adjust_2050)
{
	struct b43_phy *phy = &dev->phy;
	struct b43_phy_g *gphy = phy->g;
	s32 tmp;

	switch (phy->radio_ver) {
	case 0x2050:
		if (ofdm) {
			tmp = in_rssi;
			if (tmp > 127)
				tmp -= 256;
			tmp *= 73;
			tmp /= 64;
			if (adjust_2050)
				tmp += 25;
			else
				tmp -= 3;
		} else {
			if (dev->dev->bus_sprom->
			    boardflags_lo & B43_BFL_RSSI) {
				if (in_rssi > 63)
					in_rssi = 63;
				B43_WARN_ON(phy->type != B43_PHYTYPE_G);
				tmp = gphy->nrssi_lt[in_rssi];
				tmp = 31 - tmp;
				tmp *= -131;
				tmp /= 128;
				tmp -= 57;
			} else {
				tmp = in_rssi;
				tmp = 31 - tmp;
				tmp *= -149;
				tmp /= 128;
				tmp -= 68;
			}
			if (phy->type == B43_PHYTYPE_G && adjust_2050)
				tmp += 25;
		}
		break;
	case 0x2060:
		if (in_rssi > 127)
			tmp = in_rssi - 256;
		else
			tmp = in_rssi;
		break;
	default:
		tmp = in_rssi;
		tmp -= 11;
		tmp *= 103;
		tmp /= 64;
		if (adjust_2053)
			tmp -= 109;
		else
			tmp -= 83;
	}

	return (s8) tmp;
}

void b43_rx(struct b43_wldev *dev, struct sk_buff *skb, const void *_rxhdr)
{
	struct ieee80211_rx_status status;
	struct b43_plcp_hdr6 *plcp;
	struct ieee80211_hdr *wlhdr;
	const struct b43_rxhdr_fw4 *rxhdr = _rxhdr;
	__le16 fctl;
	u16 phystat0, phystat3;
	u16 uninitialized_var(chanstat), uninitialized_var(mactime);
	u32 uninitialized_var(macstat);
	u16 chanid;
	int padding, rate_idx;

	memset(&status, 0, sizeof(status));

	/* Get metadata about the frame from the header. */
	phystat0 = le16_to_cpu(rxhdr->phy_status0);
	phystat3 = le16_to_cpu(rxhdr->phy_status3);
	switch (dev->fw.hdr_format) {
	case B43_FW_HDR_598:
		macstat = le32_to_cpu(rxhdr->format_598.mac_status);
		mactime = le16_to_cpu(rxhdr->format_598.mac_time);
		chanstat = le16_to_cpu(rxhdr->format_598.channel);
		break;
	case B43_FW_HDR_410:
	case B43_FW_HDR_351:
		macstat = le32_to_cpu(rxhdr->format_351.mac_status);
		mactime = le16_to_cpu(rxhdr->format_351.mac_time);
		chanstat = le16_to_cpu(rxhdr->format_351.channel);
		break;
	}

	if (unlikely(macstat & B43_RX_MAC_FCSERR)) {
		dev->wl->ieee_stats.dot11FCSErrorCount++;
		status.flag |= RX_FLAG_FAILED_FCS_CRC;
	}
	if (unlikely(phystat0 & (B43_RX_PHYST0_PLCPHCF | B43_RX_PHYST0_PLCPFV)))
		status.flag |= RX_FLAG_FAILED_PLCP_CRC;
	if (phystat0 & B43_RX_PHYST0_SHORTPRMBL)
		status.enc_flags |= RX_ENC_FLAG_SHORTPRE;
	if (macstat & B43_RX_MAC_DECERR) {
		/* Decryption with the given key failed.
		 * Drop the packet. We also won't be able to decrypt it with
		 * the key in software. */
		goto drop;
	}

	/* Skip PLCP and padding */
	padding = (macstat & B43_RX_MAC_PADDING) ? 2 : 0;
	if (unlikely(skb->len < (sizeof(struct b43_plcp_hdr6) + padding))) {
		b43dbg(dev->wl, "RX: Packet size underrun (1)\n");
		goto drop;
	}
	plcp = (struct b43_plcp_hdr6 *)(skb->data + padding);
	skb_pull(skb, sizeof(struct b43_plcp_hdr6) + padding);
	/* The skb contains the Wireless Header + payload data now */
	if (unlikely(skb->len < (2 + 2 + 6 /*minimum hdr */  + FCS_LEN))) {
		b43dbg(dev->wl, "RX: Packet size underrun (2)\n");
		goto drop;
	}
	wlhdr = (struct ieee80211_hdr *)(skb->data);
	fctl = wlhdr->frame_control;

	if (macstat & B43_RX_MAC_DEC) {
		unsigned int keyidx;
		int wlhdr_len;

		keyidx = ((macstat & B43_RX_MAC_KEYIDX)
			  >> B43_RX_MAC_KEYIDX_SHIFT);
		/* We must adjust the key index here. We want the "physical"
		 * key index, but the ucode passed it slightly different.
		 */
		keyidx = b43_kidx_to_raw(dev, keyidx);
		B43_WARN_ON(keyidx >= ARRAY_SIZE(dev->key));

		if (dev->key[keyidx].algorithm != B43_SEC_ALGO_NONE) {
			wlhdr_len = ieee80211_hdrlen(fctl);
			if (unlikely(skb->len < (wlhdr_len + 3))) {
				b43dbg(dev->wl,
				       "RX: Packet size underrun (3)\n");
				goto drop;
			}
			status.flag |= RX_FLAG_DECRYPTED;
		}
	}

	/* Link quality statistics */
	switch (chanstat & B43_RX_CHAN_PHYTYPE) {
	case B43_PHYTYPE_HT:
		/* TODO: is max the right choice? */
		status.signal = max_t(__s8,
			max(rxhdr->phy_ht_power0, rxhdr->phy_ht_power1),
			rxhdr->phy_ht_power2);
		break;
	case B43_PHYTYPE_N:
		/* Broadcom has code for min and avg, but always uses max */
		if (rxhdr->power0 == 16 || rxhdr->power0 == 32)
			status.signal = max(rxhdr->power1, rxhdr->power2);
		else
			status.signal = max(rxhdr->power0, rxhdr->power1);
		break;
	case B43_PHYTYPE_B:
	case B43_PHYTYPE_G:
	case B43_PHYTYPE_LP:
		status.signal = b43_rssi_postprocess(dev, rxhdr->jssi,
						  (phystat0 & B43_RX_PHYST0_OFDM),
						  (phystat0 & B43_RX_PHYST0_GAINCTL),
						  (phystat3 & B43_RX_PHYST3_TRSTATE));
		break;
	}

	if (phystat0 & B43_RX_PHYST0_OFDM)
		rate_idx = b43_plcp_get_bitrate_idx_ofdm(plcp,
					!!(chanstat & B43_RX_CHAN_5GHZ));
	else
		rate_idx = b43_plcp_get_bitrate_idx_cck(plcp);
	if (unlikely(rate_idx == -1)) {
		/* PLCP seems to be corrupted.
		 * Drop the frame, if we are not interested in corrupted frames. */
		if (!(dev->wl->filter_flags & FIF_PLCPFAIL))
			goto drop;
	}
	status.rate_idx = rate_idx;
	status.antenna = !!(phystat0 & B43_RX_PHYST0_ANT);

	/*
	 * All frames on monitor interfaces and beacons always need a full
	 * 64-bit timestamp. Monitor interfaces need it for diagnostic
	 * purposes and beacons for IBSS merging.
	 * This code assumes we get to process the packet within 16 bits
	 * of timestamp, i.e. about 65 milliseconds after the PHY received
	 * the first symbol.
	 */
	if (ieee80211_is_beacon(fctl) || dev->wl->radiotap_enabled) {
		u16 low_mactime_now;

		b43_tsf_read(dev, &status.mactime);
		low_mactime_now = status.mactime;
		status.mactime = status.mactime & ~0xFFFFULL;
		status.mactime += mactime;
		if (low_mactime_now <= mactime)
			status.mactime -= 0x10000;
		status.flag |= RX_FLAG_MACTIME_START;
	}

	chanid = (chanstat & B43_RX_CHAN_ID) >> B43_RX_CHAN_ID_SHIFT;
	switch (chanstat & B43_RX_CHAN_PHYTYPE) {
	case B43_PHYTYPE_G:
		status.band = NL80211_BAND_2GHZ;
		/* Somewhere between 478.104 and 508.1084 firmware for G-PHY
		 * has been modified to be compatible with N-PHY and others.
		 */
		if (dev->fw.rev >= 508)
			status.freq = ieee80211_channel_to_frequency(chanid, status.band);
		else
			status.freq = chanid + 2400;
		break;
	case B43_PHYTYPE_N:
	case B43_PHYTYPE_LP:
	case B43_PHYTYPE_HT:
		/* chanid is the SHM channel cookie. Which is the plain
		 * channel number in b43. */
		if (chanstat & B43_RX_CHAN_5GHZ)
			status.band = NL80211_BAND_5GHZ;
		else
			status.band = NL80211_BAND_2GHZ;
		status.freq =
			ieee80211_channel_to_frequency(chanid, status.band);
		break;
	default:
		B43_WARN_ON(1);
		goto drop;
	}

	memcpy(IEEE80211_SKB_RXCB(skb), &status, sizeof(status));
	ieee80211_rx_ni(dev->wl->hw, skb);

#if B43_DEBUG
	dev->rx_count++;
#endif
	return;
drop:
	dev_kfree_skb_any(skb);
}

void b43_handle_txstatus(struct b43_wldev *dev,
			 const struct b43_txstatus *status)
{
	b43_debugfs_log_txstat(dev, status);

	if (status->intermediate)
		return;
	if (status->for_ampdu)
		return;
	if (!status->acked)
		dev->wl->ieee_stats.dot11ACKFailureCount++;
	if (status->rts_count) {
		if (status->rts_count == 0xF)	//FIXME
			dev->wl->ieee_stats.dot11RTSFailureCount++;
		else
			dev->wl->ieee_stats.dot11RTSSuccessCount++;
	}

	if (b43_using_pio_transfers(dev))
		b43_pio_handle_txstatus(dev, status);
	else
		b43_dma_handle_txstatus(dev, status);

	b43_phy_txpower_check(dev, 0);
}

/* Fill out the mac80211 TXstatus report based on the b43-specific
 * txstatus report data. This returns a boolean whether the frame was
 * successfully transmitted. */
bool b43_fill_txstatus_report(struct b43_wldev *dev,
			      struct ieee80211_tx_info *report,
			      const struct b43_txstatus *status)
{
	bool frame_success = true;
	int retry_limit;

	/* preserve the confiured retry limit before clearing the status
	 * The xmit function has overwritten the rc's value with the actual
	 * retry limit done by the hardware */
	retry_limit = report->status.rates[0].count;
	ieee80211_tx_info_clear_status(report);

	if (status->acked) {
		/* The frame was ACKed. */
		report->flags |= IEEE80211_TX_STAT_ACK;
	} else {
		/* The frame was not ACKed... */
		if (!(report->flags & IEEE80211_TX_CTL_NO_ACK)) {
			/* ...but we expected an ACK. */
			frame_success = false;
		}
	}
	if (status->frame_count == 0) {
		/* The frame was not transmitted at all. */
		report->status.rates[0].count = 0;
	} else if (status->rts_count > dev->wl->hw->conf.short_frame_max_tx_count) {
		/*
		 * If the short retries (RTS, not data frame) have exceeded
		 * the limit, the hw will not have tried the selected rate,
		 * but will have used the fallback rate instead.
		 * Don't let the rate control count attempts for the selected
		 * rate in this case, otherwise the statistics will be off.
		 */
		report->status.rates[0].count = 0;
		report->status.rates[1].count = status->frame_count;
	} else {
		if (status->frame_count > retry_limit) {
			report->status.rates[0].count = retry_limit;
			report->status.rates[1].count = status->frame_count -
					retry_limit;

		} else {
			report->status.rates[0].count = status->frame_count;
			report->status.rates[1].idx = -1;
		}
	}

	return frame_success;
}

/* Stop any TX operation on the device (suspend the hardware queues) */
void b43_tx_suspend(struct b43_wldev *dev)
{
	if (b43_using_pio_transfers(dev))
		b43_pio_tx_suspend(dev);
	else
		b43_dma_tx_suspend(dev);
}

/* Resume any TX operation on the device (resume the hardware queues) */
void b43_tx_resume(struct b43_wldev *dev)
{
	if (b43_using_pio_transfers(dev))
		b43_pio_tx_resume(dev);
	else
		b43_dma_tx_resume(dev);
}

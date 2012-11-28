/*

  Broadcom B43legacy wireless driver

  Transmission (TX/RX) related functions.

  Copyright (C) 2005 Martin Langer <martin-langer@gmx.de>
  Copyright (C) 2005 Stefano Brivio <stefano.brivio@polimi.it>
  Copyright (C) 2005, 2006 Michael Buesch <m@bues.ch>
  Copyright (C) 2005 Danny van Dyk <kugelfang@gentoo.org>
  Copyright (C) 2005 Andreas Jaggi <andreas.jaggi@waterwave.ch>
  Copyright (C) 2007 Larry Finger <Larry.Finger@lwfinger.net>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
  Boston, MA 02110-1301, USA.

*/

#include <net/dst.h>

#include "xmit.h"
#include "phy.h"
#include "dma.h"
#include "pio.h"


/* Extract the bitrate out of a CCK PLCP header. */
static u8 b43legacy_plcp_get_bitrate_idx_cck(struct b43legacy_plcp_hdr6 *plcp)
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
	B43legacy_BUG_ON(1);
	return -1;
}

/* Extract the bitrate out of an OFDM PLCP header. */
static u8 b43legacy_plcp_get_bitrate_idx_ofdm(struct b43legacy_plcp_hdr6 *plcp,
					      bool aphy)
{
	int base = aphy ? 0 : 4;

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
	B43legacy_BUG_ON(1);
	return -1;
}

u8 b43legacy_plcp_get_ratecode_cck(const u8 bitrate)
{
	switch (bitrate) {
	case B43legacy_CCK_RATE_1MB:
		return 0x0A;
	case B43legacy_CCK_RATE_2MB:
		return 0x14;
	case B43legacy_CCK_RATE_5MB:
		return 0x37;
	case B43legacy_CCK_RATE_11MB:
		return 0x6E;
	}
	B43legacy_BUG_ON(1);
	return 0;
}

u8 b43legacy_plcp_get_ratecode_ofdm(const u8 bitrate)
{
	switch (bitrate) {
	case B43legacy_OFDM_RATE_6MB:
		return 0xB;
	case B43legacy_OFDM_RATE_9MB:
		return 0xF;
	case B43legacy_OFDM_RATE_12MB:
		return 0xA;
	case B43legacy_OFDM_RATE_18MB:
		return 0xE;
	case B43legacy_OFDM_RATE_24MB:
		return 0x9;
	case B43legacy_OFDM_RATE_36MB:
		return 0xD;
	case B43legacy_OFDM_RATE_48MB:
		return 0x8;
	case B43legacy_OFDM_RATE_54MB:
		return 0xC;
	}
	B43legacy_BUG_ON(1);
	return 0;
}

void b43legacy_generate_plcp_hdr(struct b43legacy_plcp_hdr4 *plcp,
				 const u16 octets, const u8 bitrate)
{
	__le32 *data = &(plcp->data);
	__u8 *raw = plcp->raw;

	if (b43legacy_is_ofdm_rate(bitrate)) {
		u16 d;

		d = b43legacy_plcp_get_ratecode_ofdm(bitrate);
		B43legacy_WARN_ON(octets & 0xF000);
		d |= (octets << 5);
		*data = cpu_to_le32(d);
	} else {
		u32 plen;

		plen = octets * 16 / bitrate;
		if ((octets * 16 % bitrate) > 0) {
			plen++;
			if ((bitrate == B43legacy_CCK_RATE_11MB)
			    && ((octets * 8 % 11) < 4))
				raw[1] = 0x84;
			else
				raw[1] = 0x04;
		} else
			raw[1] = 0x04;
		*data |= cpu_to_le32(plen << 16);
		raw[0] = b43legacy_plcp_get_ratecode_cck(bitrate);
	}
}

static u8 b43legacy_calc_fallback_rate(u8 bitrate)
{
	switch (bitrate) {
	case B43legacy_CCK_RATE_1MB:
		return B43legacy_CCK_RATE_1MB;
	case B43legacy_CCK_RATE_2MB:
		return B43legacy_CCK_RATE_1MB;
	case B43legacy_CCK_RATE_5MB:
		return B43legacy_CCK_RATE_2MB;
	case B43legacy_CCK_RATE_11MB:
		return B43legacy_CCK_RATE_5MB;
	case B43legacy_OFDM_RATE_6MB:
		return B43legacy_CCK_RATE_5MB;
	case B43legacy_OFDM_RATE_9MB:
		return B43legacy_OFDM_RATE_6MB;
	case B43legacy_OFDM_RATE_12MB:
		return B43legacy_OFDM_RATE_9MB;
	case B43legacy_OFDM_RATE_18MB:
		return B43legacy_OFDM_RATE_12MB;
	case B43legacy_OFDM_RATE_24MB:
		return B43legacy_OFDM_RATE_18MB;
	case B43legacy_OFDM_RATE_36MB:
		return B43legacy_OFDM_RATE_24MB;
	case B43legacy_OFDM_RATE_48MB:
		return B43legacy_OFDM_RATE_36MB;
	case B43legacy_OFDM_RATE_54MB:
		return B43legacy_OFDM_RATE_48MB;
	}
	B43legacy_BUG_ON(1);
	return 0;
}

static int generate_txhdr_fw3(struct b43legacy_wldev *dev,
			       struct b43legacy_txhdr_fw3 *txhdr,
			       const unsigned char *fragment_data,
			       unsigned int fragment_len,
			       struct ieee80211_tx_info *info,
			       u16 cookie)
{
	const struct ieee80211_hdr *wlhdr;
	int use_encryption = !!info->control.hw_key;
	u8 rate;
	struct ieee80211_rate *rate_fb;
	int rate_ofdm;
	int rate_fb_ofdm;
	unsigned int plcp_fragment_len;
	u32 mac_ctl = 0;
	u16 phy_ctl = 0;
	struct ieee80211_rate *tx_rate;
	struct ieee80211_tx_rate *rates;

	wlhdr = (const struct ieee80211_hdr *)fragment_data;

	memset(txhdr, 0, sizeof(*txhdr));

	tx_rate = ieee80211_get_tx_rate(dev->wl->hw, info);

	rate = tx_rate->hw_value;
	rate_ofdm = b43legacy_is_ofdm_rate(rate);
	rate_fb = ieee80211_get_alt_retry_rate(dev->wl->hw, info, 0) ? : tx_rate;
	rate_fb_ofdm = b43legacy_is_ofdm_rate(rate_fb->hw_value);

	txhdr->mac_frame_ctl = wlhdr->frame_control;
	memcpy(txhdr->tx_receiver, wlhdr->addr1, 6);

	/* Calculate duration for fallback rate */
	if ((rate_fb->hw_value == rate) ||
	    (wlhdr->duration_id & cpu_to_le16(0x8000)) ||
	    (wlhdr->duration_id == cpu_to_le16(0))) {
		/* If the fallback rate equals the normal rate or the
		 * dur_id field contains an AID, CFP magic or 0,
		 * use the original dur_id field. */
		txhdr->dur_fb = wlhdr->duration_id;
	} else {
		txhdr->dur_fb = ieee80211_generic_frame_duration(dev->wl->hw,
							 info->control.vif,
							 info->band,
							 fragment_len,
							 rate_fb);
	}

	plcp_fragment_len = fragment_len + FCS_LEN;
	if (use_encryption) {
		u8 key_idx = info->control.hw_key->hw_key_idx;
		struct b43legacy_key *key;
		int wlhdr_len;
		size_t iv_len;

		B43legacy_WARN_ON(key_idx >= dev->max_nr_keys);
		key = &(dev->key[key_idx]);

		if (key->enabled) {
			/* Hardware appends ICV. */
			plcp_fragment_len += info->control.hw_key->icv_len;

			key_idx = b43legacy_kidx_to_fw(dev, key_idx);
			mac_ctl |= (key_idx << B43legacy_TX4_MAC_KEYIDX_SHIFT) &
				   B43legacy_TX4_MAC_KEYIDX;
			mac_ctl |= (key->algorithm <<
				   B43legacy_TX4_MAC_KEYALG_SHIFT) &
				   B43legacy_TX4_MAC_KEYALG;
			wlhdr_len = ieee80211_hdrlen(wlhdr->frame_control);
			iv_len = min((size_t)info->control.hw_key->iv_len,
				     ARRAY_SIZE(txhdr->iv));
			memcpy(txhdr->iv, ((u8 *)wlhdr) + wlhdr_len, iv_len);
		} else {
			/* This key is invalid. This might only happen
			 * in a short timeframe after machine resume before
			 * we were able to reconfigure keys.
			 * Drop this packet completely. Do not transmit it
			 * unencrypted to avoid leaking information. */
			return -ENOKEY;
		}
	}
	b43legacy_generate_plcp_hdr((struct b43legacy_plcp_hdr4 *)
				    (&txhdr->plcp), plcp_fragment_len,
				    rate);
	b43legacy_generate_plcp_hdr(&txhdr->plcp_fb, plcp_fragment_len,
				    rate_fb->hw_value);

	/* PHY TX Control word */
	if (rate_ofdm)
		phy_ctl |= B43legacy_TX4_PHY_ENC_OFDM;
	if (info->control.rates[0].flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
		phy_ctl |= B43legacy_TX4_PHY_SHORTPRMBL;
	phy_ctl |= B43legacy_TX4_PHY_ANTLAST;

	/* MAC control */
	rates = info->control.rates;
	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK))
		mac_ctl |= B43legacy_TX4_MAC_ACK;
	if (info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ)
		mac_ctl |= B43legacy_TX4_MAC_HWSEQ;
	if (info->flags & IEEE80211_TX_CTL_FIRST_FRAGMENT)
		mac_ctl |= B43legacy_TX4_MAC_STMSDU;
	if (rate_fb_ofdm)
		mac_ctl |= B43legacy_TX4_MAC_FALLBACKOFDM;

	/* Overwrite rates[0].count to make the retry calculation
	 * in the tx status easier. need the actual retry limit to
	 * detect whether the fallback rate was used.
	 */
	if ((rates[0].flags & IEEE80211_TX_RC_USE_RTS_CTS) ||
	    (rates[0].count <= dev->wl->hw->conf.long_frame_max_tx_count)) {
		rates[0].count = dev->wl->hw->conf.long_frame_max_tx_count;
		mac_ctl |= B43legacy_TX4_MAC_LONGFRAME;
	} else {
		rates[0].count = dev->wl->hw->conf.short_frame_max_tx_count;
	}

	/* Generate the RTS or CTS-to-self frame */
	if ((rates[0].flags & IEEE80211_TX_RC_USE_RTS_CTS) ||
	    (rates[0].flags & IEEE80211_TX_RC_USE_CTS_PROTECT)) {
		unsigned int len;
		struct ieee80211_hdr *hdr;
		int rts_rate;
		int rts_rate_fb;
		int rts_rate_fb_ofdm;

		rts_rate = ieee80211_get_rts_cts_rate(dev->wl->hw, info)->hw_value;
		rts_rate_fb = b43legacy_calc_fallback_rate(rts_rate);
		rts_rate_fb_ofdm = b43legacy_is_ofdm_rate(rts_rate_fb);
		if (rts_rate_fb_ofdm)
			mac_ctl |= B43legacy_TX4_MAC_CTSFALLBACKOFDM;

		if (rates[0].flags & IEEE80211_TX_RC_USE_CTS_PROTECT) {
			ieee80211_ctstoself_get(dev->wl->hw,
						info->control.vif,
						fragment_data,
						fragment_len, info,
						(struct ieee80211_cts *)
						(txhdr->rts_frame));
			mac_ctl |= B43legacy_TX4_MAC_SENDCTS;
			len = sizeof(struct ieee80211_cts);
		} else {
			ieee80211_rts_get(dev->wl->hw,
					  info->control.vif,
					  fragment_data, fragment_len, info,
					  (struct ieee80211_rts *)
					  (txhdr->rts_frame));
			mac_ctl |= B43legacy_TX4_MAC_SENDRTS;
			len = sizeof(struct ieee80211_rts);
		}
		len += FCS_LEN;
		b43legacy_generate_plcp_hdr((struct b43legacy_plcp_hdr4 *)
					    (&txhdr->rts_plcp),
					    len, rts_rate);
		b43legacy_generate_plcp_hdr(&txhdr->rts_plcp_fb,
					    len, rts_rate_fb);
		hdr = (struct ieee80211_hdr *)(&txhdr->rts_frame);
		txhdr->rts_dur_fb = hdr->duration_id;
	}

	/* Magic cookie */
	txhdr->cookie = cpu_to_le16(cookie);

	/* Apply the bitfields */
	txhdr->mac_ctl = cpu_to_le32(mac_ctl);
	txhdr->phy_ctl = cpu_to_le16(phy_ctl);

	return 0;
}

int b43legacy_generate_txhdr(struct b43legacy_wldev *dev,
			      u8 *txhdr,
			      const unsigned char *fragment_data,
			      unsigned int fragment_len,
			      struct ieee80211_tx_info *info,
			      u16 cookie)
{
	return generate_txhdr_fw3(dev, (struct b43legacy_txhdr_fw3 *)txhdr,
			   fragment_data, fragment_len,
			   info, cookie);
}

static s8 b43legacy_rssi_postprocess(struct b43legacy_wldev *dev,
				     u8 in_rssi, int ofdm,
				     int adjust_2053, int adjust_2050)
{
	struct b43legacy_phy *phy = &dev->phy;
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
			if (dev->dev->bus->sprom.boardflags_lo
			    & B43legacy_BFL_RSSI) {
				if (in_rssi > 63)
					in_rssi = 63;
				tmp = phy->nrssi_lt[in_rssi];
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
			if (phy->type == B43legacy_PHYTYPE_G &&
			    adjust_2050)
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

	return (s8)tmp;
}

void b43legacy_rx(struct b43legacy_wldev *dev,
		  struct sk_buff *skb,
		  const void *_rxhdr)
{
	struct ieee80211_rx_status status;
	struct b43legacy_plcp_hdr6 *plcp;
	struct ieee80211_hdr *wlhdr;
	const struct b43legacy_rxhdr_fw3 *rxhdr = _rxhdr;
	__le16 fctl;
	u16 phystat0;
	u16 phystat3;
	u16 chanstat;
	u16 mactime;
	u32 macstat;
	u16 chanid;
	u8 jssi;
	int padding;

	memset(&status, 0, sizeof(status));

	/* Get metadata about the frame from the header. */
	phystat0 = le16_to_cpu(rxhdr->phy_status0);
	phystat3 = le16_to_cpu(rxhdr->phy_status3);
	jssi = rxhdr->jssi;
	macstat = le16_to_cpu(rxhdr->mac_status);
	mactime = le16_to_cpu(rxhdr->mac_time);
	chanstat = le16_to_cpu(rxhdr->channel);

	if (macstat & B43legacy_RX_MAC_FCSERR)
		dev->wl->ieee_stats.dot11FCSErrorCount++;

	/* Skip PLCP and padding */
	padding = (macstat & B43legacy_RX_MAC_PADDING) ? 2 : 0;
	if (unlikely(skb->len < (sizeof(struct b43legacy_plcp_hdr6) +
	    padding))) {
		b43legacydbg(dev->wl, "RX: Packet size underrun (1)\n");
		goto drop;
	}
	plcp = (struct b43legacy_plcp_hdr6 *)(skb->data + padding);
	skb_pull(skb, sizeof(struct b43legacy_plcp_hdr6) + padding);
	/* The skb contains the Wireless Header + payload data now */
	if (unlikely(skb->len < (2+2+6/*minimum hdr*/ + FCS_LEN))) {
		b43legacydbg(dev->wl, "RX: Packet size underrun (2)\n");
		goto drop;
	}
	wlhdr = (struct ieee80211_hdr *)(skb->data);
	fctl = wlhdr->frame_control;

	if ((macstat & B43legacy_RX_MAC_DEC) &&
	    !(macstat & B43legacy_RX_MAC_DECERR)) {
		unsigned int keyidx;
		int wlhdr_len;
		int iv_len;
		int icv_len;

		keyidx = ((macstat & B43legacy_RX_MAC_KEYIDX)
			  >> B43legacy_RX_MAC_KEYIDX_SHIFT);
		/* We must adjust the key index here. We want the "physical"
		 * key index, but the ucode passed it slightly different.
		 */
		keyidx = b43legacy_kidx_to_raw(dev, keyidx);
		B43legacy_WARN_ON(keyidx >= dev->max_nr_keys);

		if (dev->key[keyidx].algorithm != B43legacy_SEC_ALGO_NONE) {
			/* Remove PROTECTED flag to mark it as decrypted. */
			B43legacy_WARN_ON(!ieee80211_has_protected(fctl));
			fctl &= ~cpu_to_le16(IEEE80211_FCTL_PROTECTED);
			wlhdr->frame_control = fctl;

			wlhdr_len = ieee80211_hdrlen(fctl);
			if (unlikely(skb->len < (wlhdr_len + 3))) {
				b43legacydbg(dev->wl, "RX: Packet size"
					     " underrun3\n");
				goto drop;
			}
			if (skb->data[wlhdr_len + 3] & (1 << 5)) {
				/* The Ext-IV Bit is set in the "KeyID"
				 * octet of the IV.
				 */
				iv_len = 8;
				icv_len = 8;
			} else {
				iv_len = 4;
				icv_len = 4;
			}
			if (unlikely(skb->len < (wlhdr_len + iv_len +
			    icv_len))) {
				b43legacydbg(dev->wl, "RX: Packet size"
					     " underrun4\n");
				goto drop;
			}
			/* Remove the IV */
			memmove(skb->data + iv_len, skb->data, wlhdr_len);
			skb_pull(skb, iv_len);
			/* Remove the ICV */
			skb_trim(skb, skb->len - icv_len);

			status.flag |= RX_FLAG_DECRYPTED;
		}
	}

	status.signal = b43legacy_rssi_postprocess(dev, jssi,
				      (phystat0 & B43legacy_RX_PHYST0_OFDM),
				      (phystat0 & B43legacy_RX_PHYST0_GAINCTL),
				      (phystat3 & B43legacy_RX_PHYST3_TRSTATE));
	/* change to support A PHY */
	if (phystat0 & B43legacy_RX_PHYST0_OFDM)
		status.rate_idx = b43legacy_plcp_get_bitrate_idx_ofdm(plcp, false);
	else
		status.rate_idx = b43legacy_plcp_get_bitrate_idx_cck(plcp);
	status.antenna = !!(phystat0 & B43legacy_RX_PHYST0_ANT);

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

		b43legacy_tsf_read(dev, &status.mactime);
		low_mactime_now = status.mactime;
		status.mactime = status.mactime & ~0xFFFFULL;
		status.mactime += mactime;
		if (low_mactime_now <= mactime)
			status.mactime -= 0x10000;
		status.flag |= RX_FLAG_MACTIME_START;
	}

	chanid = (chanstat & B43legacy_RX_CHAN_ID) >>
		  B43legacy_RX_CHAN_ID_SHIFT;
	switch (chanstat & B43legacy_RX_CHAN_PHYTYPE) {
	case B43legacy_PHYTYPE_B:
	case B43legacy_PHYTYPE_G:
		status.band = IEEE80211_BAND_2GHZ;
		status.freq = chanid + 2400;
		break;
	default:
		b43legacywarn(dev->wl, "Unexpected value for chanstat (0x%X)\n",
		       chanstat);
	}

	memcpy(IEEE80211_SKB_RXCB(skb), &status, sizeof(status));
	ieee80211_rx_irqsafe(dev->wl->hw, skb);

	return;
drop:
	b43legacydbg(dev->wl, "RX: Packet dropped\n");
	dev_kfree_skb_any(skb);
}

void b43legacy_handle_txstatus(struct b43legacy_wldev *dev,
			     const struct b43legacy_txstatus *status)
{
	b43legacy_debugfs_log_txstat(dev, status);

	if (status->intermediate)
		return;
	if (status->for_ampdu)
		return;
	if (!status->acked)
		dev->wl->ieee_stats.dot11ACKFailureCount++;
	if (status->rts_count) {
		if (status->rts_count == 0xF) /* FIXME */
			dev->wl->ieee_stats.dot11RTSFailureCount++;
		else
			dev->wl->ieee_stats.dot11RTSSuccessCount++;
	}

	if (b43legacy_using_pio(dev))
		b43legacy_pio_handle_txstatus(dev, status);
	else
		b43legacy_dma_handle_txstatus(dev, status);
}

/* Handle TX status report as received through DMA/PIO queues */
void b43legacy_handle_hwtxstatus(struct b43legacy_wldev *dev,
				 const struct b43legacy_hwtxstatus *hw)
{
	struct b43legacy_txstatus status;
	u8 tmp;

	status.cookie = le16_to_cpu(hw->cookie);
	status.seq = le16_to_cpu(hw->seq);
	status.phy_stat = hw->phy_stat;
	tmp = hw->count;
	status.frame_count = (tmp >> 4);
	status.rts_count = (tmp & 0x0F);
	tmp = hw->flags << 1;
	status.supp_reason = ((tmp & 0x1C) >> 2);
	status.pm_indicated = !!(tmp & 0x80);
	status.intermediate = !!(tmp & 0x40);
	status.for_ampdu = !!(tmp & 0x20);
	status.acked = !!(tmp & 0x02);

	b43legacy_handle_txstatus(dev, &status);
}

/* Stop any TX operation on the device (suspend the hardware queues) */
void b43legacy_tx_suspend(struct b43legacy_wldev *dev)
{
	if (b43legacy_using_pio(dev))
		b43legacy_pio_freeze_txqueues(dev);
	else
		b43legacy_dma_tx_suspend(dev);
}

/* Resume any TX operation on the device (resume the hardware queues) */
void b43legacy_tx_resume(struct b43legacy_wldev *dev)
{
	if (b43legacy_using_pio(dev))
		b43legacy_pio_thaw_txqueues(dev);
	else
		b43legacy_dma_tx_resume(dev);
}

/* Initialize the QoS parameters */
void b43legacy_qos_init(struct b43legacy_wldev *dev)
{
	/* FIXME: This function must probably be called from the mac80211
	 * config callback. */
return;

	b43legacy_hf_write(dev, b43legacy_hf_read(dev) | B43legacy_HF_EDCF);
	/* FIXME kill magic */
	b43legacy_write16(dev, 0x688,
			  b43legacy_read16(dev, 0x688) | 0x4);


	/*TODO: We might need some stack support here to get the values. */
}

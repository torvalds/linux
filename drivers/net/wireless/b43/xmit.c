/*

  Broadcom B43 wireless driver

  Transmission (TX/RX) related functions.

  Copyright (C) 2005 Martin Langer <martin-langer@gmx.de>
  Copyright (C) 2005 Stefano Brivio <st3@riseup.net>
  Copyright (C) 2005, 2006 Michael Buesch <mb@bu3sch.de>
  Copyright (C) 2005 Danny van Dyk <kugelfang@gentoo.org>
  Copyright (C) 2005 Andreas Jaggi <andreas.jaggi@waterwave.ch>

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

#include "xmit.h"
#include "phy.h"
#include "dma.h"
#include "pio.h"

/* Extract the bitrate out of a CCK PLCP header. */
static u8 b43_plcp_get_bitrate_cck(struct b43_plcp_hdr6 *plcp)
{
	switch (plcp->raw[0]) {
	case 0x0A:
		return B43_CCK_RATE_1MB;
	case 0x14:
		return B43_CCK_RATE_2MB;
	case 0x37:
		return B43_CCK_RATE_5MB;
	case 0x6E:
		return B43_CCK_RATE_11MB;
	}
	B43_WARN_ON(1);
	return 0;
}

/* Extract the bitrate out of an OFDM PLCP header. */
static u8 b43_plcp_get_bitrate_ofdm(struct b43_plcp_hdr6 *plcp)
{
	switch (plcp->raw[0] & 0xF) {
	case 0xB:
		return B43_OFDM_RATE_6MB;
	case 0xF:
		return B43_OFDM_RATE_9MB;
	case 0xA:
		return B43_OFDM_RATE_12MB;
	case 0xE:
		return B43_OFDM_RATE_18MB;
	case 0x9:
		return B43_OFDM_RATE_24MB;
	case 0xD:
		return B43_OFDM_RATE_36MB;
	case 0x8:
		return B43_OFDM_RATE_48MB;
	case 0xC:
		return B43_OFDM_RATE_54MB;
	}
	B43_WARN_ON(1);
	return 0;
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
	__le32 *data = &(plcp->data);
	__u8 *raw = plcp->raw;

	if (b43_is_ofdm_rate(bitrate)) {
		u32 d;

		d = b43_plcp_get_ratecode_ofdm(bitrate);
		B43_WARN_ON(octets & 0xF000);
		d |= (octets << 5);
		*data = cpu_to_le32(d);
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
		*data |= cpu_to_le32(plen << 16);
		raw[0] = b43_plcp_get_ratecode_cck(bitrate);
	}
}

static u8 b43_calc_fallback_rate(u8 bitrate)
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
	case B43_OFDM_RATE_6MB:
		return B43_CCK_RATE_5MB;
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

static void generate_txhdr_fw4(struct b43_wldev *dev,
			       struct b43_txhdr_fw4 *txhdr,
			       const unsigned char *fragment_data,
			       unsigned int fragment_len,
			       const struct ieee80211_tx_control *txctl,
			       u16 cookie)
{
	const struct b43_phy *phy = &dev->phy;
	const struct ieee80211_hdr *wlhdr =
	    (const struct ieee80211_hdr *)fragment_data;
	int use_encryption = (!(txctl->flags & IEEE80211_TXCTL_DO_NOT_ENCRYPT));
	u16 fctl = le16_to_cpu(wlhdr->frame_control);
	u8 rate, rate_fb;
	int rate_ofdm, rate_fb_ofdm;
	unsigned int plcp_fragment_len;
	u32 mac_ctl = 0;
	u16 phy_ctl = 0;
	u8 extra_ft = 0;

	memset(txhdr, 0, sizeof(*txhdr));

	rate = txctl->tx_rate;
	rate_ofdm = b43_is_ofdm_rate(rate);
	rate_fb = (txctl->alt_retry_rate == -1) ? rate : txctl->alt_retry_rate;
	rate_fb_ofdm = b43_is_ofdm_rate(rate_fb);

	if (rate_ofdm)
		txhdr->phy_rate = b43_plcp_get_ratecode_ofdm(rate);
	else
		txhdr->phy_rate = b43_plcp_get_ratecode_cck(rate);
	txhdr->mac_frame_ctl = wlhdr->frame_control;
	memcpy(txhdr->tx_receiver, wlhdr->addr1, 6);

	/* Calculate duration for fallback rate */
	if ((rate_fb == rate) ||
	    (wlhdr->duration_id & cpu_to_le16(0x8000)) ||
	    (wlhdr->duration_id == cpu_to_le16(0))) {
		/* If the fallback rate equals the normal rate or the
		 * dur_id field contains an AID, CFP magic or 0,
		 * use the original dur_id field. */
		txhdr->dur_fb = wlhdr->duration_id;
	} else {
		int fbrate_base100kbps = B43_RATE_TO_BASE100KBPS(rate_fb);
		txhdr->dur_fb = ieee80211_generic_frame_duration(dev->wl->hw,
								 dev->wl->if_id,
								 fragment_len,
								 fbrate_base100kbps);
	}

	plcp_fragment_len = fragment_len + FCS_LEN;
	if (use_encryption) {
		u8 key_idx = (u16) (txctl->key_idx);
		struct b43_key *key;
		int wlhdr_len;
		size_t iv_len;

		B43_WARN_ON(key_idx >= dev->max_nr_keys);
		key = &(dev->key[key_idx]);
		B43_WARN_ON(!key->keyconf);

		/* Hardware appends ICV. */
		plcp_fragment_len += txctl->icv_len;

		key_idx = b43_kidx_to_fw(dev, key_idx);
		mac_ctl |= (key_idx << B43_TX4_MAC_KEYIDX_SHIFT) &
			   B43_TX4_MAC_KEYIDX;
		mac_ctl |= (key->algorithm << B43_TX4_MAC_KEYALG_SHIFT) &
			   B43_TX4_MAC_KEYALG;
		wlhdr_len = ieee80211_get_hdrlen(fctl);
		iv_len = min((size_t) txctl->iv_len,
			     ARRAY_SIZE(txhdr->iv));
		memcpy(txhdr->iv, ((u8 *) wlhdr) + wlhdr_len, iv_len);
	}
	b43_generate_plcp_hdr((struct b43_plcp_hdr4 *)(&txhdr->plcp),
			      plcp_fragment_len, rate);
	b43_generate_plcp_hdr((struct b43_plcp_hdr4 *)(&txhdr->plcp_fb),
			      plcp_fragment_len, rate_fb);

	/* Extra Frame Types */
	if (rate_fb_ofdm)
		extra_ft |= B43_TX4_EFT_FBOFDM;

	/* Set channel radio code. Note that the micrcode ORs 0x100 to
	 * this value before comparing it to the value in SHM, if this
	 * is a 5Ghz packet.
	 */
	txhdr->chan_radio_code = phy->channel;

	/* PHY TX Control word */
	if (rate_ofdm)
		phy_ctl |= B43_TX4_PHY_OFDM;
	if (dev->short_preamble)
		phy_ctl |= B43_TX4_PHY_SHORTPRMBL;
	switch (txctl->antenna_sel_tx) {
	case 0:
		phy_ctl |= B43_TX4_PHY_ANTLAST;
		break;
	case 1:
		phy_ctl |= B43_TX4_PHY_ANT0;
		break;
	case 2:
		phy_ctl |= B43_TX4_PHY_ANT1;
		break;
	default:
		B43_WARN_ON(1);
	}

	/* MAC control */
	if (!(txctl->flags & IEEE80211_TXCTL_NO_ACK))
		mac_ctl |= B43_TX4_MAC_ACK;
	if (!(((fctl & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_CTL) &&
	      ((fctl & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_PSPOLL)))
		mac_ctl |= B43_TX4_MAC_HWSEQ;
	if (txctl->flags & IEEE80211_TXCTL_FIRST_FRAGMENT)
		mac_ctl |= B43_TX4_MAC_STMSDU;
	if (phy->type == B43_PHYTYPE_A)
		mac_ctl |= B43_TX4_MAC_5GHZ;

	/* Generate the RTS or CTS-to-self frame */
	if ((txctl->flags & IEEE80211_TXCTL_USE_RTS_CTS) ||
	    (txctl->flags & IEEE80211_TXCTL_USE_CTS_PROTECT)) {
		unsigned int len;
		struct ieee80211_hdr *hdr;
		int rts_rate, rts_rate_fb;
		int rts_rate_ofdm, rts_rate_fb_ofdm;

		rts_rate = txctl->rts_cts_rate;
		rts_rate_ofdm = b43_is_ofdm_rate(rts_rate);
		rts_rate_fb = b43_calc_fallback_rate(rts_rate);
		rts_rate_fb_ofdm = b43_is_ofdm_rate(rts_rate_fb);

		if (txctl->flags & IEEE80211_TXCTL_USE_CTS_PROTECT) {
			ieee80211_ctstoself_get(dev->wl->hw, dev->wl->if_id,
						fragment_data, fragment_len,
						txctl,
						(struct ieee80211_cts *)(txhdr->
									 rts_frame));
			mac_ctl |= B43_TX4_MAC_SENDCTS;
			len = sizeof(struct ieee80211_cts);
		} else {
			ieee80211_rts_get(dev->wl->hw, dev->wl->if_id,
					  fragment_data, fragment_len, txctl,
					  (struct ieee80211_rts *)(txhdr->
								   rts_frame));
			mac_ctl |= B43_TX4_MAC_SENDRTS;
			len = sizeof(struct ieee80211_rts);
		}
		len += FCS_LEN;
		b43_generate_plcp_hdr((struct b43_plcp_hdr4 *)(&txhdr->
							       rts_plcp), len,
				      rts_rate);
		b43_generate_plcp_hdr((struct b43_plcp_hdr4 *)(&txhdr->
							       rts_plcp_fb),
				      len, rts_rate_fb);
		hdr = (struct ieee80211_hdr *)(&txhdr->rts_frame);
		txhdr->rts_dur_fb = hdr->duration_id;
		if (rts_rate_ofdm) {
			extra_ft |= B43_TX4_EFT_RTSOFDM;
			txhdr->phy_rate_rts =
			    b43_plcp_get_ratecode_ofdm(rts_rate);
		} else
			txhdr->phy_rate_rts =
			    b43_plcp_get_ratecode_cck(rts_rate);
		if (rts_rate_fb_ofdm)
			extra_ft |= B43_TX4_EFT_RTSFBOFDM;
		mac_ctl |= B43_TX4_MAC_LONGFRAME;
	}

	/* Magic cookie */
	txhdr->cookie = cpu_to_le16(cookie);

	/* Apply the bitfields */
	txhdr->mac_ctl = cpu_to_le32(mac_ctl);
	txhdr->phy_ctl = cpu_to_le16(phy_ctl);
	txhdr->extra_ft = extra_ft;
}

void b43_generate_txhdr(struct b43_wldev *dev,
			u8 * txhdr,
			const unsigned char *fragment_data,
			unsigned int fragment_len,
			const struct ieee80211_tx_control *txctl, u16 cookie)
{
	generate_txhdr_fw4(dev, (struct b43_txhdr_fw4 *)txhdr,
			   fragment_data, fragment_len, txctl, cookie);
}

static s8 b43_rssi_postprocess(struct b43_wldev *dev,
			       u8 in_rssi, int ofdm,
			       int adjust_2053, int adjust_2050)
{
	struct b43_phy *phy = &dev->phy;
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
			if (dev->dev->bus->sprom.r1.
			    boardflags_lo & B43_BFL_RSSI) {
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

//TODO
#if 0
static s8 b43_rssinoise_postprocess(struct b43_wldev *dev, u8 in_rssi)
{
	struct b43_phy *phy = &dev->phy;
	s8 ret;

	if (phy->type == B43_PHYTYPE_A) {
		//TODO: Incomplete specs.
		ret = 0;
	} else
		ret = b43_rssi_postprocess(dev, in_rssi, 0, 1, 1);

	return ret;
}
#endif

void b43_rx(struct b43_wldev *dev, struct sk_buff *skb, const void *_rxhdr)
{
	struct ieee80211_rx_status status;
	struct b43_plcp_hdr6 *plcp;
	struct ieee80211_hdr *wlhdr;
	const struct b43_rxhdr_fw4 *rxhdr = _rxhdr;
	u16 fctl;
	u16 phystat0, phystat3, chanstat, mactime;
	u32 macstat;
	u16 chanid;
	u8 jssi;
	int padding;

	memset(&status, 0, sizeof(status));

	/* Get metadata about the frame from the header. */
	phystat0 = le16_to_cpu(rxhdr->phy_status0);
	phystat3 = le16_to_cpu(rxhdr->phy_status3);
	jssi = rxhdr->jssi;
	macstat = le32_to_cpu(rxhdr->mac_status);
	mactime = le16_to_cpu(rxhdr->mac_time);
	chanstat = le16_to_cpu(rxhdr->channel);

	if (macstat & B43_RX_MAC_FCSERR)
		dev->wl->ieee_stats.dot11FCSErrorCount++;
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
	fctl = le16_to_cpu(wlhdr->frame_control);
	skb_trim(skb, skb->len - FCS_LEN);

	if (macstat & B43_RX_MAC_DEC) {
		unsigned int keyidx;
		int wlhdr_len;

		keyidx = ((macstat & B43_RX_MAC_KEYIDX)
			  >> B43_RX_MAC_KEYIDX_SHIFT);
		/* We must adjust the key index here. We want the "physical"
		 * key index, but the ucode passed it slightly different.
		 */
		keyidx = b43_kidx_to_raw(dev, keyidx);
		B43_WARN_ON(keyidx >= dev->max_nr_keys);

		if (dev->key[keyidx].algorithm != B43_SEC_ALGO_NONE) {
			wlhdr_len = ieee80211_get_hdrlen(fctl);
			if (unlikely(skb->len < (wlhdr_len + 3))) {
				b43dbg(dev->wl,
				       "RX: Packet size underrun (3)\n");
				goto drop;
			}
			status.flag |= RX_FLAG_DECRYPTED;
		}
	}

	status.ssi = b43_rssi_postprocess(dev, jssi,
					  (phystat0 & B43_RX_PHYST0_OFDM),
					  (phystat0 & B43_RX_PHYST0_GAINCTL),
					  (phystat3 & B43_RX_PHYST3_TRSTATE));
	status.noise = dev->stats.link_noise;
	/* the next line looks wrong, but is what mac80211 wants */
	status.signal = (jssi * 100) / B43_RX_MAX_SSI;
	if (phystat0 & B43_RX_PHYST0_OFDM)
		status.rate = b43_plcp_get_bitrate_ofdm(plcp);
	else
		status.rate = b43_plcp_get_bitrate_cck(plcp);
	status.antenna = !!(phystat0 & B43_RX_PHYST0_ANT);
	status.mactime = mactime;

	chanid = (chanstat & B43_RX_CHAN_ID) >> B43_RX_CHAN_ID_SHIFT;
	switch (chanstat & B43_RX_CHAN_PHYTYPE) {
	case B43_PHYTYPE_A:
		status.phymode = MODE_IEEE80211A;
		B43_WARN_ON(1);
		/* FIXME: We don't really know which value the "chanid" contains.
		 *        So the following assignment might be wrong. */
		status.channel = chanid;
		status.freq = b43_channel_to_freq_5ghz(status.channel);
		break;
	case B43_PHYTYPE_G:
		status.phymode = MODE_IEEE80211G;
		/* chanid is the radio channel cookie value as used
		 * to tune the radio. */
		status.freq = chanid + 2400;
		status.channel = b43_freq_to_channel_2ghz(status.freq);
		break;
	case B43_PHYTYPE_N:
		status.phymode = 0xDEAD /*FIXME MODE_IEEE80211N*/;
		/* chanid is the SHM channel cookie. Which is the plain
		 * channel number in b43. */
		status.channel = chanid;
		if (chanstat & B43_RX_CHAN_5GHZ)
			status.freq = b43_freq_to_channel_5ghz(status.freq);
		else
			status.freq = b43_freq_to_channel_2ghz(status.freq);
		break;
	default:
		B43_WARN_ON(1);
		goto drop;
	}

	dev->stats.last_rx = jiffies;
	ieee80211_rx_irqsafe(dev->wl->hw, skb, &status);

	return;
drop:
	b43dbg(dev->wl, "RX: Packet dropped\n");
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

	if (b43_using_pio(dev))
		b43_pio_handle_txstatus(dev, status);
	else
		b43_dma_handle_txstatus(dev, status);
}

/* Handle TX status report as received through DMA/PIO queues */
void b43_handle_hwtxstatus(struct b43_wldev *dev,
			   const struct b43_hwtxstatus *hw)
{
	struct b43_txstatus status;
	u8 tmp;

	status.cookie = le16_to_cpu(hw->cookie);
	status.seq = le16_to_cpu(hw->seq);
	status.phy_stat = hw->phy_stat;
	tmp = hw->count;
	status.frame_count = (tmp >> 4);
	status.rts_count = (tmp & 0x0F);
	tmp = hw->flags;
	status.supp_reason = ((tmp & 0x1C) >> 2);
	status.pm_indicated = !!(tmp & 0x80);
	status.intermediate = !!(tmp & 0x40);
	status.for_ampdu = !!(tmp & 0x20);
	status.acked = !!(tmp & 0x02);

	b43_handle_txstatus(dev, &status);
}

/* Stop any TX operation on the device (suspend the hardware queues) */
void b43_tx_suspend(struct b43_wldev *dev)
{
	if (b43_using_pio(dev))
		b43_pio_freeze_txqueues(dev);
	else
		b43_dma_tx_suspend(dev);
}

/* Resume any TX operation on the device (resume the hardware queues) */
void b43_tx_resume(struct b43_wldev *dev)
{
	if (b43_using_pio(dev))
		b43_pio_thaw_txqueues(dev);
	else
		b43_dma_tx_resume(dev);
}

#if 0
static void upload_qos_parms(struct b43_wldev *dev,
			     const u16 * parms, u16 offset)
{
	int i;

	for (i = 0; i < B43_NR_QOSPARMS; i++) {
		b43_shm_write16(dev, B43_SHM_SHARED,
				offset + (i * 2), parms[i]);
	}
}
#endif

/* Initialize the QoS parameters */
void b43_qos_init(struct b43_wldev *dev)
{
	/* FIXME: This function must probably be called from the mac80211
	 * config callback. */
	return;

	b43_hf_write(dev, b43_hf_read(dev) | B43_HF_EDCF);
	//FIXME kill magic
	b43_write16(dev, 0x688, b43_read16(dev, 0x688) | 0x4);

	/*TODO: We might need some stack support here to get the values. */
}

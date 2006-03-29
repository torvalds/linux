/*

  Broadcom BCM43xx wireless driver

  Transmission (TX/RX) related functions.

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>,
                     Stefano Brivio <st3@riseup.net>
                     Michael Buesch <mbuesch@freenet.de>
                     Danny van Dyk <kugelfang@gentoo.org>
                     Andreas Jaggi <andreas.jaggi@waterwave.ch>

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

#include "bcm43xx_xmit.h"

#include <linux/etherdevice.h>


/* Extract the bitrate out of a CCK PLCP header. */
static u8 bcm43xx_plcp_get_bitrate_cck(struct bcm43xx_plcp_hdr4 *plcp)
{
	switch (plcp->raw[0]) {
	case 0x0A:
		return IEEE80211_CCK_RATE_1MB;
	case 0x14:
		return IEEE80211_CCK_RATE_2MB;
	case 0x37:
		return IEEE80211_CCK_RATE_5MB;
	case 0x6E:
		return IEEE80211_CCK_RATE_11MB;
	}
	assert(0);
	return 0;
}

/* Extract the bitrate out of an OFDM PLCP header. */
static u8 bcm43xx_plcp_get_bitrate_ofdm(struct bcm43xx_plcp_hdr4 *plcp)
{
	switch (plcp->raw[0] & 0xF) {
	case 0xB:
		return IEEE80211_OFDM_RATE_6MB;
	case 0xF:
		return IEEE80211_OFDM_RATE_9MB;
	case 0xA:
		return IEEE80211_OFDM_RATE_12MB;
	case 0xE:
		return IEEE80211_OFDM_RATE_18MB;
	case 0x9:
		return IEEE80211_OFDM_RATE_24MB;
	case 0xD:
		return IEEE80211_OFDM_RATE_36MB;
	case 0x8:
		return IEEE80211_OFDM_RATE_48MB;
	case 0xC:
		return IEEE80211_OFDM_RATE_54MB;
	}
	assert(0);
	return 0;
}

u8 bcm43xx_plcp_get_ratecode_cck(const u8 bitrate)
{
	switch (bitrate) {
	case IEEE80211_CCK_RATE_1MB:
		return 0x0A;
	case IEEE80211_CCK_RATE_2MB:
		return 0x14;
	case IEEE80211_CCK_RATE_5MB:
		return 0x37;
	case IEEE80211_CCK_RATE_11MB:
		return 0x6E;
	}
	assert(0);
	return 0;
}

u8 bcm43xx_plcp_get_ratecode_ofdm(const u8 bitrate)
{
	switch (bitrate) {
	case IEEE80211_OFDM_RATE_6MB:
		return 0xB;
	case IEEE80211_OFDM_RATE_9MB:
		return 0xF;
	case IEEE80211_OFDM_RATE_12MB:
		return 0xA;
	case IEEE80211_OFDM_RATE_18MB:
		return 0xE;
	case IEEE80211_OFDM_RATE_24MB:
		return 0x9;
	case IEEE80211_OFDM_RATE_36MB:
		return 0xD;
	case IEEE80211_OFDM_RATE_48MB:
		return 0x8;
	case IEEE80211_OFDM_RATE_54MB:
		return 0xC;
	}
	assert(0);
	return 0;
}

static void bcm43xx_generate_plcp_hdr(struct bcm43xx_plcp_hdr4 *plcp,
				      const u16 octets, const u8 bitrate,
				      const int ofdm_modulation)
{
	__le32 *data = &(plcp->data);
	__u8 *raw = plcp->raw;

	if (ofdm_modulation) {
		*data = bcm43xx_plcp_get_ratecode_ofdm(bitrate);
		assert(!(octets & 0xF000));
		*data |= (octets << 5);
		*data = cpu_to_le32(*data);
	} else {
		u32 plen;

		plen = octets * 16 / bitrate;
		if ((octets * 16 % bitrate) > 0) {
			plen++;
			if ((bitrate == IEEE80211_CCK_RATE_11MB)
			    && ((octets * 8 % 11) < 4)) {
				raw[1] = 0x84;
			} else
				raw[1] = 0x04;
		} else
			raw[1] = 0x04;
		*data |= cpu_to_le32(plen << 16);
		raw[0] = bcm43xx_plcp_get_ratecode_cck(bitrate);
	}
}

static u8 bcm43xx_calc_fallback_rate(u8 bitrate)
{
	switch (bitrate) {
	case IEEE80211_CCK_RATE_1MB:
		return IEEE80211_CCK_RATE_1MB;
	case IEEE80211_CCK_RATE_2MB:
		return IEEE80211_CCK_RATE_1MB;
	case IEEE80211_CCK_RATE_5MB:
		return IEEE80211_CCK_RATE_2MB;
	case IEEE80211_CCK_RATE_11MB:
		return IEEE80211_CCK_RATE_5MB;
	case IEEE80211_OFDM_RATE_6MB:
		return IEEE80211_CCK_RATE_5MB;
	case IEEE80211_OFDM_RATE_9MB:
		return IEEE80211_OFDM_RATE_6MB;
	case IEEE80211_OFDM_RATE_12MB:
		return IEEE80211_OFDM_RATE_9MB;
	case IEEE80211_OFDM_RATE_18MB:
		return IEEE80211_OFDM_RATE_12MB;
	case IEEE80211_OFDM_RATE_24MB:
		return IEEE80211_OFDM_RATE_18MB;
	case IEEE80211_OFDM_RATE_36MB:
		return IEEE80211_OFDM_RATE_24MB;
	case IEEE80211_OFDM_RATE_48MB:
		return IEEE80211_OFDM_RATE_36MB;
	case IEEE80211_OFDM_RATE_54MB:
		return IEEE80211_OFDM_RATE_48MB;
	}
	assert(0);
	return 0;
}

static
__le16 bcm43xx_calc_duration_id(const struct ieee80211_hdr *wireless_header,
				u8 bitrate)
{
	const u16 frame_ctl = le16_to_cpu(wireless_header->frame_ctl);
	__le16 duration_id = wireless_header->duration_id;

	switch (WLAN_FC_GET_TYPE(frame_ctl)) {
	case IEEE80211_FTYPE_DATA:
	case IEEE80211_FTYPE_MGMT:
		//TODO: Steal the code from ieee80211, once it is completed there.
		break;
	case IEEE80211_FTYPE_CTL:
		/* Use the original duration/id. */
		break;
	default:
		assert(0);
	}

	return duration_id;
}

static inline
u16 ceiling_div(u16 dividend, u16 divisor)
{
	return ((dividend + divisor - 1) / divisor);
}

static void bcm43xx_generate_rts(const struct bcm43xx_phyinfo *phy,
				 struct bcm43xx_txhdr *txhdr,
				 u16 *flags,
				 u8 bitrate,
				 const struct ieee80211_hdr_4addr *wlhdr)
{
	u16 fctl;
	u16 dur;
	u8 fallback_bitrate;
	int ofdm_modulation;
	int fallback_ofdm_modulation;
//	u8 *sa, *da;
	u16 flen;

//FIXME	sa = ieee80211_get_SA((struct ieee80211_hdr *)wlhdr);
//FIXME	da = ieee80211_get_DA((struct ieee80211_hdr *)wlhdr);
	fallback_bitrate = bcm43xx_calc_fallback_rate(bitrate);
	ofdm_modulation = !(ieee80211_is_cck_rate(bitrate));
	fallback_ofdm_modulation = !(ieee80211_is_cck_rate(fallback_bitrate));

	flen = sizeof(u16) + sizeof(u16) + ETH_ALEN + ETH_ALEN + IEEE80211_FCS_LEN,
	bcm43xx_generate_plcp_hdr((struct bcm43xx_plcp_hdr4 *)(&txhdr->rts_cts_plcp),
				  flen, bitrate,
				  !ieee80211_is_cck_rate(bitrate));
	bcm43xx_generate_plcp_hdr((struct bcm43xx_plcp_hdr4 *)(&txhdr->rts_cts_fallback_plcp),
				  flen, fallback_bitrate,
				  !ieee80211_is_cck_rate(fallback_bitrate));
	fctl = IEEE80211_FTYPE_CTL;
	fctl |= IEEE80211_STYPE_RTS;
	dur = le16_to_cpu(wlhdr->duration_id);
/*FIXME: should we test for dur==0 here and let it unmodified in this case?
 *       The following assert checks for this case...
 */
assert(dur);
/*FIXME: The duration calculation is not really correct.
 *       I am not 100% sure which bitrate to use. We use the RTS rate here,
 *       but this is likely to be wrong.
 */
	if (phy->type == BCM43xx_PHYTYPE_A) {
		/* Three times SIFS */
		dur += 16 * 3;
		/* Add ACK duration. */
		dur += ceiling_div((16 + 8 * (14 /*bytes*/) + 6) * 10,
				   bitrate * 4);
		/* Add CTS duration. */
		dur += ceiling_div((16 + 8 * (14 /*bytes*/) + 6) * 10,
				   bitrate * 4);
	} else {
		/* Three times SIFS */
		dur += 10 * 3;
		/* Add ACK duration. */
		dur += ceiling_div(8 * (14 /*bytes*/) * 10,
				   bitrate);
		/* Add CTS duration. */
		dur += ceiling_div(8 * (14 /*bytes*/) * 10,
				   bitrate);
	}

	txhdr->rts_cts_frame_control = cpu_to_le16(fctl);
	txhdr->rts_cts_dur = cpu_to_le16(dur);
//printk(BCM43xx_MACFMT "  " BCM43xx_MACFMT "  " BCM43xx_MACFMT "\n", BCM43xx_MACARG(wlhdr->addr1), BCM43xx_MACARG(wlhdr->addr2), BCM43xx_MACARG(wlhdr->addr3));
//printk(BCM43xx_MACFMT "  " BCM43xx_MACFMT "\n", BCM43xx_MACARG(sa), BCM43xx_MACARG(da));
	memcpy(txhdr->rts_cts_mac1, wlhdr->addr1, ETH_ALEN);//FIXME!
//	memcpy(txhdr->rts_cts_mac2, sa, ETH_ALEN);

	*flags |= BCM43xx_TXHDRFLAG_RTSCTS;
	*flags |= BCM43xx_TXHDRFLAG_RTS;
	if (ofdm_modulation)
		*flags |= BCM43xx_TXHDRFLAG_RTSCTS_OFDM;
	if (fallback_ofdm_modulation)
		*flags |= BCM43xx_TXHDRFLAG_RTSCTSFALLBACK_OFDM;
}
				 
void bcm43xx_generate_txhdr(struct bcm43xx_private *bcm,
			    struct bcm43xx_txhdr *txhdr,
			    const unsigned char *fragment_data,
			    const unsigned int fragment_len,
			    const int is_first_fragment,
			    const u16 cookie)
{
	const struct bcm43xx_phyinfo *phy = bcm43xx_current_phy(bcm);
	const struct ieee80211_hdr_4addr *wireless_header = (const struct ieee80211_hdr_4addr *)fragment_data;
	const struct ieee80211_security *secinfo = &bcm->ieee->sec;
	u8 bitrate;
	u8 fallback_bitrate;
	int ofdm_modulation;
	int fallback_ofdm_modulation;
	u16 plcp_fragment_len = fragment_len;
	u16 flags = 0;
	u16 control = 0;
	u16 wsec_rate = 0;
	u16 encrypt_frame;

	/* Now construct the TX header. */
	memset(txhdr, 0, sizeof(*txhdr));

	bitrate = bcm->softmac->txrates.default_rate;
	ofdm_modulation = !(ieee80211_is_cck_rate(bitrate));
	fallback_bitrate = bcm43xx_calc_fallback_rate(bitrate);
	fallback_ofdm_modulation = !(ieee80211_is_cck_rate(fallback_bitrate));

	/* Set Frame Control from 80211 header. */
	txhdr->frame_control = wireless_header->frame_ctl;
	/* Copy address1 from 80211 header. */
	memcpy(txhdr->mac1, wireless_header->addr1, 6);
	/* Set the fallback duration ID. */
	txhdr->fallback_dur_id = bcm43xx_calc_duration_id((const struct ieee80211_hdr *)wireless_header,
							  fallback_bitrate);
	/* Set the cookie (used as driver internal ID for the frame) */
	txhdr->cookie = cpu_to_le16(cookie);

	/* Hardware appends FCS. */
	plcp_fragment_len += IEEE80211_FCS_LEN;

	/* Hardware encryption. */
	encrypt_frame = le16_to_cpup(&wireless_header->frame_ctl) & IEEE80211_FCTL_PROTECTED;
	if (encrypt_frame && !bcm->ieee->host_encrypt) {
		const struct ieee80211_hdr_3addr *hdr = (struct ieee80211_hdr_3addr *)wireless_header;
		memcpy(txhdr->wep_iv, hdr->payload, 4);
		/* Hardware appends ICV. */
		plcp_fragment_len += 4;

		wsec_rate |= (bcm->key[secinfo->active_key].algorithm << BCM43xx_TXHDR_WSEC_ALGO_SHIFT)
			     & BCM43xx_TXHDR_WSEC_ALGO_MASK;
		wsec_rate |= (secinfo->active_key << BCM43xx_TXHDR_WSEC_KEYINDEX_SHIFT)
			     & BCM43xx_TXHDR_WSEC_KEYINDEX_MASK;
	}

	/* Generate the PLCP header and the fallback PLCP header. */
	bcm43xx_generate_plcp_hdr((struct bcm43xx_plcp_hdr4 *)(&txhdr->plcp),
				  plcp_fragment_len,
				  bitrate, ofdm_modulation);
	bcm43xx_generate_plcp_hdr(&txhdr->fallback_plcp, plcp_fragment_len,
				  fallback_bitrate, fallback_ofdm_modulation);

	/* Set the CONTROL field */
	if (ofdm_modulation)
		control |= BCM43xx_TXHDRCTL_OFDM;
	if (bcm->short_preamble) //FIXME: could be the other way around, please test
		control |= BCM43xx_TXHDRCTL_SHORT_PREAMBLE;
	control |= (phy->antenna_diversity << BCM43xx_TXHDRCTL_ANTENNADIV_SHIFT)
		   & BCM43xx_TXHDRCTL_ANTENNADIV_MASK;

	/* Set the FLAGS field */
	if (!is_multicast_ether_addr(wireless_header->addr1) &&
	    !is_broadcast_ether_addr(wireless_header->addr1))
		flags |= BCM43xx_TXHDRFLAG_EXPECTACK;
	if (1 /* FIXME: PS poll?? */)
		flags |= 0x10; // FIXME: unknown meaning.
	if (fallback_ofdm_modulation)
		flags |= BCM43xx_TXHDRFLAG_FALLBACKOFDM;
	if (is_first_fragment)
		flags |= BCM43xx_TXHDRFLAG_FIRSTFRAGMENT;

	/* Set WSEC/RATE field */
	wsec_rate |= (txhdr->plcp.raw[0] << BCM43xx_TXHDR_RATE_SHIFT)
		     & BCM43xx_TXHDR_RATE_MASK;

	/* Generate the RTS/CTS packet, if required. */
	/* FIXME: We should first try with CTS-to-self,
	 *        if we are on 80211g. If we get too many
	 *        failures (hidden nodes), we should switch back to RTS/CTS.
	 */
	if (0/*FIXME txctl->use_rts_cts*/) {
		bcm43xx_generate_rts(phy, txhdr, &flags,
				     0/*FIXME txctl->rts_cts_rate*/,
				     wireless_header);
	}

	txhdr->flags = cpu_to_le16(flags);
	txhdr->control = cpu_to_le16(control);
	txhdr->wsec_rate = cpu_to_le16(wsec_rate);
}

static s8 bcm43xx_rssi_postprocess(struct bcm43xx_private *bcm,
				   u8 in_rssi, int ofdm,
				   int adjust_2053, int adjust_2050)
{
	struct bcm43xx_radioinfo *radio = bcm43xx_current_radio(bcm);
	struct bcm43xx_phyinfo *phy = bcm43xx_current_phy(bcm);
	s32 tmp;

	switch (radio->version) {
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
			if (bcm->sprom.boardflags & BCM43xx_BFL_RSSI) {
				if (in_rssi > 63)
					in_rssi = 63;
				tmp = radio->nrssi_lt[in_rssi];
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
			if (phy->type == BCM43xx_PHYTYPE_G &&
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

//TODO
#if 0
static s8 bcm43xx_rssinoise_postprocess(struct bcm43xx_private *bcm,
					u8 in_rssi)
{
	struct bcm43xx_phyinfo *phy = bcm43xx_current_phy(bcm);
	s8 ret;

	if (phy->type == BCM43xx_PHYTYPE_A) {
		//TODO: Incomplete specs.
		ret = 0;
	} else
		ret = bcm43xx_rssi_postprocess(bcm, in_rssi, 0, 1, 1);

	return ret;
}
#endif

int bcm43xx_rx(struct bcm43xx_private *bcm,
	       struct sk_buff *skb,
	       struct bcm43xx_rxhdr *rxhdr)
{
	struct bcm43xx_radioinfo *radio = bcm43xx_current_radio(bcm);
	struct bcm43xx_phyinfo *phy = bcm43xx_current_phy(bcm);
	struct bcm43xx_plcp_hdr4 *plcp;
	struct ieee80211_rx_stats stats;
	struct ieee80211_hdr_4addr *wlhdr;
	u16 frame_ctl;
	int is_packet_for_us = 0;
	int err = -EINVAL;
	const u16 rxflags1 = le16_to_cpu(rxhdr->flags1);
	const u16 rxflags2 = le16_to_cpu(rxhdr->flags2);
	const u16 rxflags3 = le16_to_cpu(rxhdr->flags3);
	const int is_ofdm = !!(rxflags1 & BCM43xx_RXHDR_FLAGS1_OFDM);

	if (rxflags2 & BCM43xx_RXHDR_FLAGS2_TYPE2FRAME) {
		plcp = (struct bcm43xx_plcp_hdr4 *)(skb->data + 2);
		/* Skip two unknown bytes and the PLCP header. */
		skb_pull(skb, 2 + sizeof(struct bcm43xx_plcp_hdr6));
	} else {
		plcp = (struct bcm43xx_plcp_hdr4 *)(skb->data);
		/* Skip the PLCP header. */
		skb_pull(skb, sizeof(struct bcm43xx_plcp_hdr6));
	}
	/* The SKB contains the PAYLOAD (wireless header + data)
	 * at this point. The FCS at the end is stripped.
	 */

	memset(&stats, 0, sizeof(stats));
	stats.mac_time = le16_to_cpu(rxhdr->mactime);
	stats.rssi = bcm43xx_rssi_postprocess(bcm, rxhdr->rssi, is_ofdm,
					      !!(rxflags1 & BCM43xx_RXHDR_FLAGS1_2053RSSIADJ),
					      !!(rxflags3 & BCM43xx_RXHDR_FLAGS3_2050RSSIADJ));
	stats.signal = rxhdr->signal_quality;	//FIXME
//TODO	stats.noise = 
	if (is_ofdm)
		stats.rate = bcm43xx_plcp_get_bitrate_ofdm(plcp);
	else
		stats.rate = bcm43xx_plcp_get_bitrate_cck(plcp);
//printk("RX ofdm %d, rate == %u\n", is_ofdm, stats.rate);
	stats.received_channel = radio->channel;
//TODO	stats.control = 
	stats.mask = IEEE80211_STATMASK_SIGNAL |
//TODO		     IEEE80211_STATMASK_NOISE |
		     IEEE80211_STATMASK_RATE |
		     IEEE80211_STATMASK_RSSI;
	if (phy->type == BCM43xx_PHYTYPE_A)
		stats.freq = IEEE80211_52GHZ_BAND;
	else
		stats.freq = IEEE80211_24GHZ_BAND;
	stats.len = skb->len;

	bcm->stats.last_rx = jiffies;
	if (bcm->ieee->iw_mode == IW_MODE_MONITOR) {
		err = ieee80211_rx(bcm->ieee, skb, &stats);
		return (err == 0) ? -EINVAL : 0;
	}

	wlhdr = (struct ieee80211_hdr_4addr *)(skb->data);

	switch (bcm->ieee->iw_mode) {
	case IW_MODE_ADHOC:
		if (memcmp(wlhdr->addr1, bcm->net_dev->dev_addr, ETH_ALEN) == 0 ||
		    memcmp(wlhdr->addr3, bcm->ieee->bssid, ETH_ALEN) == 0 ||
		    is_broadcast_ether_addr(wlhdr->addr1) ||
		    is_multicast_ether_addr(wlhdr->addr1) ||
		    bcm->net_dev->flags & IFF_PROMISC)
			is_packet_for_us = 1;
		break;
	case IW_MODE_INFRA:
	default:
		/* When receiving multicast or broadcast packets, filter out
		   the packets we send ourself; we shouldn't see those */
		if (memcmp(wlhdr->addr3, bcm->ieee->bssid, ETH_ALEN) == 0 ||
		    memcmp(wlhdr->addr1, bcm->net_dev->dev_addr, ETH_ALEN) == 0 ||
		    (memcmp(wlhdr->addr3, bcm->net_dev->dev_addr, ETH_ALEN) &&
		     (is_broadcast_ether_addr(wlhdr->addr1) ||
		      is_multicast_ether_addr(wlhdr->addr1) ||
		      bcm->net_dev->flags & IFF_PROMISC)))
			is_packet_for_us = 1;
		break;
	}

	frame_ctl = le16_to_cpu(wlhdr->frame_ctl);
	if ((frame_ctl & IEEE80211_FCTL_PROTECTED) && !bcm->ieee->host_decrypt) {
		frame_ctl &= ~IEEE80211_FCTL_PROTECTED;
		wlhdr->frame_ctl = cpu_to_le16(frame_ctl);		
		/* trim IV and ICV */
		/* FIXME: this must be done only for WEP encrypted packets */
		if (skb->len < 32) {
			dprintkl(KERN_ERR PFX "RX packet dropped (PROTECTED flag "
					      "set and length < 32)\n");
			return -EINVAL;
		} else {		
			memmove(skb->data + 4, skb->data, 24);
			skb_pull(skb, 4);
			skb_trim(skb, skb->len - 4);
			stats.len -= 8;
		}
		wlhdr = (struct ieee80211_hdr_4addr *)(skb->data);
	}
	
	switch (WLAN_FC_GET_TYPE(frame_ctl)) {
	case IEEE80211_FTYPE_MGMT:
		ieee80211_rx_mgt(bcm->ieee, wlhdr, &stats);
		break;
	case IEEE80211_FTYPE_DATA:
		if (is_packet_for_us) {
			err = ieee80211_rx(bcm->ieee, skb, &stats);
			err = (err == 0) ? -EINVAL : 0;
		}
		break;
	case IEEE80211_FTYPE_CTL:
		break;
	default:
		assert(0);
		return -EINVAL;
	}

	return err;
}

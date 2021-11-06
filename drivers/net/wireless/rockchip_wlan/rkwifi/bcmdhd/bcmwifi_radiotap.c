/*
 * RadioTap utility routines for WL
 * This file housing the functions use by
 * wl driver.
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#include <bcmutils.h>
#include <bcmendian.h>
#include <bcmwifi_channels.h>
#include <hndd11.h>
#include <bcmwifi_radiotap.h>

const struct rtap_field rtap_parse_info[] = {
	{8, 8}, /* 0:  IEEE80211_RADIOTAP_TSFT */
	{1, 1}, /* 1:  IEEE80211_RADIOTAP_FLAGS */
	{1, 1}, /* 2:  IEEE80211_RADIOTAP_RATE */
	{4, 2}, /* 3:  IEEE80211_RADIOTAP_CHANNEL */
	{2, 2}, /* 4:  IEEE80211_RADIOTAP_FHSS */
	{1, 1}, /* 5:  IEEE80211_RADIOTAP_DBM_ANTSIGNAL */
	{1, 1}, /* 6:  IEEE80211_RADIOTAP_DBM_ANTNOISE */
	{2, 2}, /* 7:  IEEE80211_RADIOTAP_LOCK_QUALITY */
	{2, 2}, /* 8:  IEEE80211_RADIOTAP_TX_ATTENUATION */
	{2, 2}, /* 9:  IEEE80211_RADIOTAP_DB_TX_ATTENUATION */
	{1, 1}, /* 10: IEEE80211_RADIOTAP_DBM_TX_POWER */
	{1, 1}, /* 11: IEEE80211_RADIOTAP_ANTENNA */
	{1, 1}, /* 12: IEEE80211_RADIOTAP_DB_ANTSIGNAL */
	{1, 1}, /* 13: IEEE80211_RADIOTAP_DB_ANTNOISE */
	{0, 0}, /* 14: netbsd */
	{2, 2}, /* 15: IEEE80211_RADIOTAP_TXFLAGS */
	{0, 0}, /* 16: missing */
	{1, 1}, /* 17: IEEE80211_RADIOTAP_RETRIES */
	{8, 4}, /* 18: IEEE80211_RADIOTAP_XCHANNEL */
	{3, 1}, /* 19: IEEE80211_RADIOTAP_MCS */
	{8, 4}, /* 20: IEEE80211_RADIOTAP_AMPDU_STATUS */
	{12, 2}, /* 21: IEEE80211_RADIOTAP_VHT */
	{0, 0}, /* 22: */
	{0, 0}, /* 23: */
	{0, 0}, /* 24: */
	{0, 0}, /* 25: */
	{0, 0}, /* 26: */
	{0, 0}, /* 27: */
	{0, 0}, /* 28: */
	{0, 0}, /* 29: IEEE80211_RADIOTAP_RADIOTAP_NAMESPACE */
	{6, 2}, /* 30: IEEE80211_RADIOTAP_VENDOR_NAMESPACE */
	{0, 0}  /* 31: IEEE80211_RADIOTAP_EXT */
};

static int bitmap = 0;

void
radiotap_add_vendor_ns(ieee80211_radiotap_header_t *hdr);

void
radiotap_encode_multi_rssi(monitor_pkt_rxsts_t* rxsts, ieee80211_radiotap_header_t *hdr);
void
radiotap_encode_bw_signaling(uint16 mask, struct wl_rxsts* rxsts, ieee80211_radiotap_header_t *hdr);
#ifdef MONITOR_DNGL_CONV
void radiotap_encode_alignpad(ieee80211_radiotap_header_t *hdr, uint16 pad_req);
#endif

static const uint8 brcm_oui[] =  {0x00, 0x10, 0x18};

static void
wl_rtapParseReset(radiotap_parse_t *rtap)
{
	rtap->idx = 0;		/* reset parse index */
	rtap->offset = 0;	/* reset current field pointer */
}

static void*
wl_rtapParseFindField(radiotap_parse_t *rtap, uint search_idx)
{
	uint idx;	/* first bit index to parse */
	uint32 btmap;	/* presence bitmap */
	uint offset, field_offset;
	uint align, len;
	void *ptr = NULL;

	if (search_idx > IEEE80211_RADIOTAP_EXT)
		return ptr;

	if (search_idx < rtap->idx)
		wl_rtapParseReset(rtap);

	btmap = rtap->hdr->it_present;
	idx = rtap->idx;
	offset = rtap->offset;

	/* loop through each field index until we get to the target idx */
	while (idx <= search_idx) {
		/* if field 'idx' is present, update the offset and check for a match */
		if ((1 << idx) & btmap) {
			/* if we hit a field for which we have no parse info
			 * we need to just bail out
			 */
			if (rtap_parse_info[idx].align == 0)
				break;

			/* step past any alignment padding */
			align = rtap_parse_info[idx].align;
			len = rtap_parse_info[idx].len;

			/* ROUNDUP */
			field_offset = ((offset + (align - 1)) / align) * align;

			/* if this field is not in the boulds of the header
			 * just bail out
			 */
			if (field_offset + len > rtap->fields_len)
				break;

			/* did we find the field? */
			if (idx == search_idx)
				ptr = (uint8*)rtap->fields + field_offset;

			/* step past this field */
			offset = field_offset + len;
		}

		idx++;
	}

	rtap->idx = idx;
	rtap->offset = offset;

	return ptr;
}

ratespec_t
wl_calcRspecFromRTap(uint8 *rtap_header)
{
	ratespec_t rspec = 0;
	radiotap_parse_t rtap;
	uint8 rate = 0;
	uint8 flags = 0;
	int flags_present = FALSE;
	uint8 mcs = 0;
	uint8 mcs_flags = 0;
	uint8 mcs_known = 0;
	int mcs_present = FALSE;
	void *p;

	wl_rtapParseInit(&rtap, rtap_header);

	p = wl_rtapParseFindField(&rtap, IEEE80211_RADIOTAP_FLAGS);
	if (p != NULL) {
		flags_present = TRUE;
		flags = ((uint8*)p)[0];
	}

	p = wl_rtapParseFindField(&rtap, IEEE80211_RADIOTAP_RATE);
	if (p != NULL)
		rate = ((uint8*)p)[0];

	p = wl_rtapParseFindField(&rtap, IEEE80211_RADIOTAP_MCS);
	if (p != NULL) {
		mcs_present = TRUE;
		mcs_known = ((uint8*)p)[0];
		mcs_flags = ((uint8*)p)[1];
		mcs = ((uint8*)p)[2];
	}

	if (rate != 0) {
		/* validate the DSSS rates 1,2,5.5,11 */
		if (rate == 2 || rate == 4 || rate == 11 || rate == 22) {
			rspec = LEGACY_RSPEC(rate) | WL_RSPEC_OVERRIDE_RATE;
			if (flags_present && (flags & IEEE80211_RADIOTAP_F_SHORTPRE)) {
				rspec |= WL_RSPEC_OVERRIDE_MODE | WL_RSPEC_SHORT_PREAMBLE;
			}
		}
	} else if (mcs_present) {
		/* validate the MCS value */
		if (mcs <= 23 || mcs == 32) {
			uint32 override = 0;
			if (mcs_known &
			    (IEEE80211_RADIOTAP_MCS_HAVE_GI |
			     IEEE80211_RADIOTAP_MCS_HAVE_FMT |
			     IEEE80211_RADIOTAP_MCS_HAVE_FEC)) {
				override = WL_RSPEC_OVERRIDE_MODE;
			}

			rspec = HT_RSPEC(mcs) | WL_RSPEC_OVERRIDE_RATE;

			if ((mcs_known & IEEE80211_RADIOTAP_MCS_HAVE_GI) &&
			    (mcs_flags & IEEE80211_RADIOTAP_MCS_SGI))
				rspec |= WL_RSPEC_SGI;
			if ((mcs_known & IEEE80211_RADIOTAP_MCS_HAVE_FMT) &&
			    (mcs_flags & IEEE80211_RADIOTAP_MCS_FMT_GF))
				rspec |= WL_RSPEC_SHORT_PREAMBLE;
			if ((mcs_known & IEEE80211_RADIOTAP_MCS_HAVE_FEC) &&
			    (mcs_flags & IEEE80211_RADIOTAP_MCS_FEC_LDPC))
				rspec |= WL_RSPEC_LDPC;

			rspec |= override;
		}
	}

	return rspec;
}

bool
wl_rtapFlags(uint8 *rtap_header, uint8* flags)
{
	radiotap_parse_t rtap;
	void *p;

	wl_rtapParseInit(&rtap, rtap_header);

	p = wl_rtapParseFindField(&rtap, IEEE80211_RADIOTAP_FLAGS);
	if (p != NULL) {
		*flags = ((uint8*)p)[0];
	}

	return (p != NULL);
}

void
wl_rtapParseInit(radiotap_parse_t *rtap, uint8 *rtap_header)
{
	uint rlen;
	uint32 *present_word;
	struct ieee80211_radiotap_header *hdr = (struct ieee80211_radiotap_header*)rtap_header;

	bzero(rtap, sizeof(radiotap_parse_t));

	rlen = hdr->it_len; /* total space in rtap_header */

	/* If a precence word has the IEEE80211_RADIOTAP_EXT bit set it indicates
	 * that there is another precence word.
	 * Step over the presence words until we find the end of the list
	 */
	present_word = &hdr->it_present;
	/* remaining length in header past it_present */
	rlen -= sizeof(struct ieee80211_radiotap_header);

	while ((*present_word & (1<<IEEE80211_RADIOTAP_EXT)) && rlen >= 4) {
		present_word++;
		rlen -= 4;	/* account for 4 bytes of present_word */
	}

	rtap->hdr = hdr;
	rtap->fields = (uint8*)(present_word + 1);
	rtap->fields_len = rlen;
	wl_rtapParseReset(rtap);
}

uint
wl_radiotap_rx(struct dot11_header *mac_header,	wl_rxsts_t *rxsts, bsd_header_rx_t *bsd_header)
{
	int channel_frequency;
	uint32 channel_flags;
	uint8 flags;
	uint8 *cp;
	uint pad_len;
	uint32 field_map;
	uint16 fc;
	uint bsd_header_len;
	uint16 ampdu_flags = 0;

	fc = LTOH16(mac_header->fc);
	pad_len = 3;
	field_map = WL_RADIOTAP_PRESENT_BASIC;

	if (CHSPEC_IS2G(rxsts->chanspec)) {
		channel_flags = IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_DYN;
		channel_frequency = wf_channel2mhz(wf_chspec_ctlchan(rxsts->chanspec),
			WF_CHAN_FACTOR_2_4_G);
	} else if (CHSPEC_IS5G(rxsts->chanspec)) {
		channel_flags = IEEE80211_CHAN_5GHZ | IEEE80211_CHAN_OFDM;
		channel_frequency = wf_channel2mhz(wf_chspec_ctlchan(rxsts->chanspec),
			WF_CHAN_FACTOR_5_G);
	} else {
		channel_flags = IEEE80211_CHAN_OFDM;
		channel_frequency = wf_channel2mhz(wf_chspec_ctlchan(rxsts->chanspec),
			WF_CHAN_FACTOR_6_G);
	}

	if ((rxsts->nfrmtype & WL_RXS_NFRM_AMPDU_FIRST) ||
		(rxsts->nfrmtype & WL_RXS_NFRM_AMPDU_SUB)) {

		ampdu_flags = IEEE80211_RADIOTAP_AMPDU_LAST_KNOWN;
	}

	flags = IEEE80211_RADIOTAP_F_FCS;

	if (rxsts->preamble == WL_RXS_PREAMBLE_SHORT)
		flags |= IEEE80211_RADIOTAP_F_SHORTPRE;

	if ((fc &  FC_WEP) == FC_WEP)
		flags |= IEEE80211_RADIOTAP_F_WEP;

	if ((fc & FC_MOREFRAG) == FC_MOREFRAG)
		flags |= IEEE80211_RADIOTAP_F_FRAG;

	if (rxsts->pkterror & WL_RXS_CRC_ERROR)
		flags |= IEEE80211_RADIOTAP_F_BADFCS;

	if (rxsts->encoding == WL_RXS_ENCODING_HT)
		field_map = WL_RADIOTAP_PRESENT_HT;
	else if (rxsts->encoding == WL_RXS_ENCODING_VHT)
		field_map = WL_RADIOTAP_PRESENT_VHT;

	bsd_header_len = sizeof(struct wl_radiotap_sna); /* start with sna size */
	/* Test for signal/noise values and update length and field bitmap */
	if (rxsts->signal == 0) {
		field_map &= ~(1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL);
		pad_len = (pad_len - 1);
		bsd_header_len--;
	}

	if (rxsts->noise == 0) {
		field_map &= ~(1 << IEEE80211_RADIOTAP_DBM_ANTNOISE);
		pad_len = (pad_len - 1);
		bsd_header_len--;
	}

	if (rxsts->encoding == WL_RXS_ENCODING_HT ||
	    rxsts->encoding == WL_RXS_ENCODING_VHT) {
		struct wl_radiotap_hdr *rtht = &bsd_header->hdr;
		struct wl_radiotap_ht_tail *tail;

		/*
		 * Header length is complicated due to dynamic
		 * presence of signal and noise fields
		 * and padding for xchannel following
		 * signal/noise/ant.
		 * Start with length of wl_radiotap_ht plus
		 * signal/noise/ant
		 */
		bsd_header_len += sizeof(struct wl_radiotap_hdr) + pad_len;
		bsd_header_len += sizeof(struct wl_radiotap_xchan);
		if (rxsts->nfrmtype == WL_RXS_NFRM_AMPDU_FIRST ||
			rxsts->nfrmtype == WL_RXS_NFRM_AMPDU_SUB) {
			bsd_header_len += sizeof(struct wl_radiotap_ampdu);
		}
		/* add the length of the tail end of the structure */
		if (rxsts->encoding == WL_RXS_ENCODING_HT)
			bsd_header_len += sizeof(struct wl_htmcs);
		else if (rxsts->encoding == WL_RXS_ENCODING_VHT)
			bsd_header_len += sizeof(struct wl_vhtmcs);
		bzero((uint8 *)rtht, sizeof(*rtht));

		rtht->ieee_radiotap.it_version = 0;
		rtht->ieee_radiotap.it_pad = 0;
		rtht->ieee_radiotap.it_len = (uint16)HTOL16(bsd_header_len);
		rtht->ieee_radiotap.it_present = HTOL32(field_map);

		rtht->tsft = HTOL64((uint64)rxsts->mactime);
		rtht->flags = flags;
		rtht->channel_freq = (uint16)HTOL16(channel_frequency);
		rtht->channel_flags = (uint16)HTOL16(channel_flags);

		cp = bsd_header->pad;
		/* add in signal/noise/ant */
		if (rxsts->signal != 0) {
			*cp++ = (int8)rxsts->signal;
			pad_len--;
		}
		if (rxsts->noise != 0) {
			*cp++ = (int8)rxsts->noise;
			pad_len--;
		}
		*cp++ = (int8)rxsts->antenna;
		pad_len--;

		tail = (struct wl_radiotap_ht_tail *)(bsd_header->ht);
		/* Fill in XCHANNEL */
		if (CHSPEC_IS40(rxsts->chanspec)) {
			if (CHSPEC_SB_UPPER(rxsts->chanspec))
				channel_flags |= IEEE80211_CHAN_HT40D;
			else
				channel_flags |= IEEE80211_CHAN_HT40U;
		} else
			channel_flags |= IEEE80211_CHAN_HT20;

		tail->xc.xchannel_flags = HTOL32(channel_flags);
		tail->xc.xchannel_freq = (uint16)HTOL16(channel_frequency);
		tail->xc.xchannel_channel = wf_chspec_ctlchan(rxsts->chanspec);
		tail->xc.xchannel_maxpower = (17*2);
		/* fill in A-mpdu Status */
		tail->ampdu.ref_num = mac_header->seq;
		tail->ampdu.flags = ampdu_flags;
		tail->ampdu.delimiter_crc = 0;
		tail->ampdu.reserved = 0;

		if (rxsts->encoding == WL_RXS_ENCODING_HT) {
			tail->u.ht.mcs_index = rxsts->mcs;
			tail->u.ht.mcs_known = (IEEE80211_RADIOTAP_MCS_HAVE_BW |
			                        IEEE80211_RADIOTAP_MCS_HAVE_MCS |
			                        IEEE80211_RADIOTAP_MCS_HAVE_GI |
			                        IEEE80211_RADIOTAP_MCS_HAVE_FEC |
			                        IEEE80211_RADIOTAP_MCS_HAVE_FMT);
			tail->u.ht.mcs_flags = 0;

			switch (rxsts->htflags & WL_RXS_HTF_BW_MASK) {
			case WL_RXS_HTF_20L:
				tail->u.ht.mcs_flags |= IEEE80211_RADIOTAP_MCS_BW_20L;
				break;
			case WL_RXS_HTF_20U:
				tail->u.ht.mcs_flags |= IEEE80211_RADIOTAP_MCS_BW_20U;
				break;
			case WL_RXS_HTF_40:
				tail->u.ht.mcs_flags |= IEEE80211_RADIOTAP_MCS_BW_40;
				break;
			default:
				tail->u.ht.mcs_flags |= IEEE80211_RADIOTAP_MCS_BW_20;
				break;
			}

			if (rxsts->htflags & WL_RXS_HTF_SGI)
				tail->u.ht.mcs_flags |= IEEE80211_RADIOTAP_MCS_SGI;
			if (rxsts->preamble & WL_RXS_PREAMBLE_HT_GF)
				tail->u.ht.mcs_flags |= IEEE80211_RADIOTAP_MCS_FMT_GF;
			if (rxsts->htflags & WL_RXS_HTF_LDPC)
				tail->u.ht.mcs_flags |= IEEE80211_RADIOTAP_MCS_FEC_LDPC;
		} else if (rxsts->encoding == WL_RXS_ENCODING_VHT) {
			tail->u.vht.vht_known = (IEEE80211_RADIOTAP_VHT_HAVE_STBC |
			                         IEEE80211_RADIOTAP_VHT_HAVE_TXOP_PS |
			                         IEEE80211_RADIOTAP_VHT_HAVE_GI |
			                         IEEE80211_RADIOTAP_VHT_HAVE_SGI_NSYM_DA |
			                         IEEE80211_RADIOTAP_VHT_HAVE_LDPC_EXTRA |
			                         IEEE80211_RADIOTAP_VHT_HAVE_BF |
			                         IEEE80211_RADIOTAP_VHT_HAVE_BW |
			                         IEEE80211_RADIOTAP_VHT_HAVE_GID |
			                         IEEE80211_RADIOTAP_VHT_HAVE_PAID);

			tail->u.vht.vht_flags = (uint8)HTOL16(rxsts->vhtflags);

			switch (rxsts->bw) {
			case WL_RXS_VHT_BW_20:
				tail->u.vht.vht_bw = IEEE80211_RADIOTAP_VHT_BW_20;
				break;
			case WL_RXS_VHT_BW_40:
				tail->u.vht.vht_bw = IEEE80211_RADIOTAP_VHT_BW_40;
				break;
			case WL_RXS_VHT_BW_20L:
				tail->u.vht.vht_bw = IEEE80211_RADIOTAP_VHT_BW_20L;
				break;
			case WL_RXS_VHT_BW_20U:
				tail->u.vht.vht_bw = IEEE80211_RADIOTAP_VHT_BW_20U;
				break;
			case WL_RXS_VHT_BW_80:
				tail->u.vht.vht_bw = IEEE80211_RADIOTAP_VHT_BW_80;
				break;
			case WL_RXS_VHT_BW_40L:
				tail->u.vht.vht_bw = IEEE80211_RADIOTAP_VHT_BW_40L;
				break;
			case WL_RXS_VHT_BW_40U:
				tail->u.vht.vht_bw = IEEE80211_RADIOTAP_VHT_BW_40U;
				break;
			case WL_RXS_VHT_BW_20LL:
				tail->u.vht.vht_bw = IEEE80211_RADIOTAP_VHT_BW_20LL;
				break;
			case WL_RXS_VHT_BW_20LU:
				tail->u.vht.vht_bw = IEEE80211_RADIOTAP_VHT_BW_20LU;
				break;
			case WL_RXS_VHT_BW_20UL:
				tail->u.vht.vht_bw = IEEE80211_RADIOTAP_VHT_BW_20UL;
				break;
			case WL_RXS_VHT_BW_20UU:
				tail->u.vht.vht_bw = IEEE80211_RADIOTAP_VHT_BW_20UU;
				break;
			default:
				tail->u.vht.vht_bw = IEEE80211_RADIOTAP_VHT_BW_20;
				break;
			}

			tail->u.vht.vht_mcs_nss[0] = (rxsts->mcs << 4) |
				(rxsts->nss & IEEE80211_RADIOTAP_VHT_NSS);
			tail->u.vht.vht_mcs_nss[1] = 0;
			tail->u.vht.vht_mcs_nss[2] = 0;
			tail->u.vht.vht_mcs_nss[3] = 0;

			tail->u.vht.vht_coding = rxsts->coding;
			tail->u.vht.vht_group_id = rxsts->gid;
			tail->u.vht.vht_partial_aid = HTOL16(rxsts->aid);
		}
	} else {
		struct wl_radiotap_hdr *rtl = &bsd_header->hdr;

		/*
		 * Header length is complicated due to dynamic presence of signal and noise fields
		 * Start with length of wl_radiotap_legacy plus signal/noise/ant
		 */
		bsd_header_len = sizeof(struct wl_radiotap_hdr) + pad_len;
		bzero((uint8 *)rtl, sizeof(*rtl));

		rtl->ieee_radiotap.it_version = 0;
		rtl->ieee_radiotap.it_pad = 0;
		rtl->ieee_radiotap.it_len = (uint16)HTOL16(bsd_header_len);
		rtl->ieee_radiotap.it_present = HTOL32(field_map);

		rtl->tsft = HTOL64((uint64)rxsts->mactime);
		rtl->flags = flags;
		rtl->u.rate = (uint8)rxsts->datarate;
		rtl->channel_freq = (uint16)HTOL16(channel_frequency);
		rtl->channel_flags = (uint16)HTOL16(channel_flags);

		/* add in signal/noise/ant */
		cp = bsd_header->pad;
		if (rxsts->signal != 0)
			*cp++ = (int8)rxsts->signal;
		if (rxsts->noise != 0)
			*cp++ = (int8)rxsts->noise;
		*cp++ = (int8)rxsts->antenna;
	}
	return bsd_header_len;
}

static int
wl_radiotap_rx_channel_frequency(wl_rxsts_t *rxsts)
{
	if (CHSPEC_IS2G(rxsts->chanspec)) {
		return wf_channel2mhz(wf_chspec_ctlchan(rxsts->chanspec),
			WF_CHAN_FACTOR_2_4_G);
	} else if (CHSPEC_IS5G(rxsts->chanspec)) {
		return wf_channel2mhz(wf_chspec_ctlchan(rxsts->chanspec),
			WF_CHAN_FACTOR_5_G);
	} else {
		return wf_channel2mhz(wf_chspec_ctlchan(rxsts->chanspec),
			WF_CHAN_FACTOR_6_G);
	}
}

static uint16
wl_radiotap_rx_channel_flags(wl_rxsts_t *rxsts)
{
	if (CHSPEC_IS2G(rxsts->chanspec)) {
		return (IEEE80211_CHAN_2GHZ | IEEE80211_CHAN_DYN);
	} else if (CHSPEC_IS5G(rxsts->chanspec)) {
		return (IEEE80211_CHAN_5GHZ | IEEE80211_CHAN_OFDM);
	} else {
		return (IEEE80211_CHAN_OFDM);
	}
}

static uint8
wl_radiotap_rx_flags(struct dot11_header *mac_header, wl_rxsts_t *rxsts)
{
	uint8 flags;
	uint16 fc;

	fc = ltoh16(mac_header->fc);

	flags = IEEE80211_RADIOTAP_F_FCS;

	if (rxsts->preamble == WL_RXS_PREAMBLE_SHORT)
		flags |= IEEE80211_RADIOTAP_F_SHORTPRE;

	if (fc & FC_WEP)
		flags |= IEEE80211_RADIOTAP_F_WEP;

	if (fc & FC_MOREFRAG)
		flags |= IEEE80211_RADIOTAP_F_FRAG;

	return flags;
}

uint
wl_radiotap_rx_legacy(struct dot11_header *mac_header,
	wl_rxsts_t *rxsts, ieee80211_radiotap_header_t *rtap_hdr)
{
	int channel_frequency;
	uint16 channel_flags;
	uint8 flags;
	uint16 rtap_len = LTOH16(rtap_hdr->it_len);
	wl_radiotap_legacy_t *rtl = (wl_radiotap_legacy_t *)((uint8*)rtap_hdr + rtap_len);

	rtap_len += sizeof(wl_radiotap_legacy_t);
	rtap_hdr->it_len = HTOL16(rtap_len);
	rtap_hdr->it_present |= HTOL32(WL_RADIOTAP_PRESENT_LEGACY);

	channel_frequency = (uint16)wl_radiotap_rx_channel_frequency(rxsts);
	channel_flags = wl_radiotap_rx_channel_flags(rxsts);
	flags = wl_radiotap_rx_flags(mac_header, rxsts);

	rtl->basic.tsft_l = HTOL32(rxsts->mactime);
	rtl->basic.tsft_h = 0;
	rtl->basic.flags = flags;
	rtl->basic.rate = (uint8)rxsts->datarate;
	rtl->basic.channel_freq = (uint16)HTOL16(channel_frequency);
	rtl->basic.channel_flags = HTOL16(channel_flags);
	rtl->basic.signal = (int8)rxsts->signal;
	rtl->basic.noise = (int8)rxsts->noise;
	rtl->basic.antenna = (int8)rxsts->antenna;

	return 0;
}

uint
wl_radiotap_rx_ht(struct dot11_header *mac_header,
                  wl_rxsts_t *rxsts, ieee80211_radiotap_header_t *rtap_hdr)
{
	int channel_frequency;
	uint16 channel_flags;
	uint32 xchannel_flags;
	uint8 flags;

	uint16 rtap_len = LTOH16(rtap_hdr->it_len);
	wl_radiotap_ht_t *rtht = (wl_radiotap_ht_t *)((uint8*)rtap_hdr + rtap_len);

	rtap_len += sizeof(wl_radiotap_ht_t);
	rtap_hdr->it_len = HTOL16(rtap_len);
	rtap_hdr->it_present |= HTOL32(WL_RADIOTAP_PRESENT_HT);

	channel_frequency = (uint16)wl_radiotap_rx_channel_frequency(rxsts);
	channel_flags = wl_radiotap_rx_channel_flags(rxsts);
	flags = wl_radiotap_rx_flags(mac_header, rxsts);

	rtht->basic.tsft_l = HTOL32(rxsts->mactime);
	rtht->basic.tsft_h = 0;
	rtht->basic.flags = flags;
	rtht->basic.channel_freq = (uint16)HTOL16(channel_frequency);
	rtht->basic.channel_flags = HTOL16(channel_flags);
	rtht->basic.signal = (int8)rxsts->signal;
	rtht->basic.noise = (int8)rxsts->noise;
	rtht->basic.antenna = (uint8)rxsts->antenna;

	/* xchannel */
	xchannel_flags = (uint32)channel_flags;
	if (CHSPEC_IS40(rxsts->chanspec)) {
		if (CHSPEC_SB_UPPER(rxsts->chanspec))
			xchannel_flags |= IEEE80211_CHAN_HT40D;
		else {
			xchannel_flags |= IEEE80211_CHAN_HT40U;
		}
	} else {
		xchannel_flags |= IEEE80211_CHAN_HT20;
	}

	rtht->xchannel_flags = HTOL32(xchannel_flags);
	rtht->xchannel_freq = (uint16)HTOL16(channel_frequency);
	rtht->xchannel_channel = wf_chspec_ctlchan(rxsts->chanspec);
	rtht->xchannel_maxpower = (17*2);

	/* add standard MCS */
	rtht->mcs_known = (IEEE80211_RADIOTAP_MCS_HAVE_BW |
		IEEE80211_RADIOTAP_MCS_HAVE_MCS |
		IEEE80211_RADIOTAP_MCS_HAVE_GI |
		IEEE80211_RADIOTAP_MCS_HAVE_FEC |
		IEEE80211_RADIOTAP_MCS_HAVE_FMT);

	rtht->mcs_flags = 0;
	switch (rxsts->htflags & WL_RXS_HTF_BW_MASK) {
		case WL_RXS_HTF_20L:
			rtht->mcs_flags |= IEEE80211_RADIOTAP_MCS_BW_20L;
			break;
		case WL_RXS_HTF_20U:
			rtht->mcs_flags |= IEEE80211_RADIOTAP_MCS_BW_20U;
			break;
		case WL_RXS_HTF_40:
			rtht->mcs_flags |= IEEE80211_RADIOTAP_MCS_BW_40;
			break;
		default:
			rtht->mcs_flags |= IEEE80211_RADIOTAP_MCS_BW_20;
	}

	if (rxsts->htflags & WL_RXS_HTF_SGI) {
		rtht->mcs_flags |= IEEE80211_RADIOTAP_MCS_SGI;
	}
	if (rxsts->preamble & WL_RXS_PREAMBLE_HT_GF) {
		rtht->mcs_flags |= IEEE80211_RADIOTAP_MCS_FMT_GF;
	}
	if (rxsts->htflags & WL_RXS_HTF_LDPC) {
		rtht->mcs_flags |= IEEE80211_RADIOTAP_MCS_FEC_LDPC;
	}
	rtht->mcs_index = rxsts->mcs;
	rtht->ampdu_flags = 0;
	rtht->ampdu_delim_crc = 0;

	rtht->ampdu_ref_num = rxsts->ampdu_counter;

	if (!(rxsts->nfrmtype & WL_RXS_NFRM_AMPDU_FIRST) &&
		!(rxsts->nfrmtype & WL_RXS_NFRM_AMPDU_SUB)) {
		rtht->ampdu_flags |= IEEE80211_RADIOTAP_AMPDU_IS_LAST;
	} else {
		rtht->ampdu_flags |= IEEE80211_RADIOTAP_AMPDU_LAST_KNOWN;
	}
	return 0;
}

uint
wl_radiotap_rx_vht(struct dot11_header *mac_header,
                   wl_rxsts_t *rxsts, ieee80211_radiotap_header_t *rtap_hdr)
{
	int channel_frequency;
	uint16 channel_flags;
	uint8 flags;

	uint16 rtap_len = LTOH16(rtap_hdr->it_len);
	wl_radiotap_vht_t *rtvht = (wl_radiotap_vht_t *)((uint8*)rtap_hdr + rtap_len);

	rtap_len += sizeof(wl_radiotap_vht_t);
	rtap_hdr->it_len = HTOL16(rtap_len);
	rtap_hdr->it_present |= HTOL32(WL_RADIOTAP_PRESENT_VHT);

	channel_frequency = (uint16)wl_radiotap_rx_channel_frequency(rxsts);
	channel_flags = wl_radiotap_rx_channel_flags(rxsts);
	flags = wl_radiotap_rx_flags(mac_header, rxsts);

	rtvht->basic.tsft_l = HTOL32(rxsts->mactime);
	rtvht->basic.tsft_h = 0;
	rtvht->basic.flags = flags;
	rtvht->basic.channel_freq = (uint16)HTOL16(channel_frequency);
	rtvht->basic.channel_flags = HTOL16(channel_flags);
	rtvht->basic.signal = (int8)rxsts->signal;
	rtvht->basic.noise = (int8)rxsts->noise;
	rtvht->basic.antenna = (uint8)rxsts->antenna;

	rtvht->vht_known = (IEEE80211_RADIOTAP_VHT_HAVE_STBC |
		IEEE80211_RADIOTAP_VHT_HAVE_TXOP_PS |
		IEEE80211_RADIOTAP_VHT_HAVE_GI |
		IEEE80211_RADIOTAP_VHT_HAVE_SGI_NSYM_DA |
		IEEE80211_RADIOTAP_VHT_HAVE_LDPC_EXTRA |
		IEEE80211_RADIOTAP_VHT_HAVE_BF |
		IEEE80211_RADIOTAP_VHT_HAVE_BW |
		IEEE80211_RADIOTAP_VHT_HAVE_GID |
		IEEE80211_RADIOTAP_VHT_HAVE_PAID);

	STATIC_ASSERT(WL_RXS_VHTF_STBC ==
		IEEE80211_RADIOTAP_VHT_STBC);
	STATIC_ASSERT(WL_RXS_VHTF_TXOP_PS ==
		IEEE80211_RADIOTAP_VHT_TXOP_PS);
	STATIC_ASSERT(WL_RXS_VHTF_SGI ==
		IEEE80211_RADIOTAP_VHT_SGI);
	STATIC_ASSERT(WL_RXS_VHTF_SGI_NSYM_DA ==
		IEEE80211_RADIOTAP_VHT_SGI_NSYM_DA);
	STATIC_ASSERT(WL_RXS_VHTF_LDPC_EXTRA ==
		IEEE80211_RADIOTAP_VHT_LDPC_EXTRA);
	STATIC_ASSERT(WL_RXS_VHTF_BF ==
		IEEE80211_RADIOTAP_VHT_BF);

	rtvht->vht_flags = (uint8)HTOL16(rxsts->vhtflags);

	STATIC_ASSERT(WL_RXS_VHT_BW_20 ==
		IEEE80211_RADIOTAP_VHT_BW_20);
	STATIC_ASSERT(WL_RXS_VHT_BW_40 ==
		IEEE80211_RADIOTAP_VHT_BW_40);
	STATIC_ASSERT(WL_RXS_VHT_BW_20L ==
		IEEE80211_RADIOTAP_VHT_BW_20L);
	STATIC_ASSERT(WL_RXS_VHT_BW_20U ==
		IEEE80211_RADIOTAP_VHT_BW_20U);
	STATIC_ASSERT(WL_RXS_VHT_BW_80 ==
		IEEE80211_RADIOTAP_VHT_BW_80);
	STATIC_ASSERT(WL_RXS_VHT_BW_40L ==
		IEEE80211_RADIOTAP_VHT_BW_40L);
	STATIC_ASSERT(WL_RXS_VHT_BW_40U ==
		IEEE80211_RADIOTAP_VHT_BW_40U);
	STATIC_ASSERT(WL_RXS_VHT_BW_20LL ==
		IEEE80211_RADIOTAP_VHT_BW_20LL);
	STATIC_ASSERT(WL_RXS_VHT_BW_20LU ==
		IEEE80211_RADIOTAP_VHT_BW_20LU);
	STATIC_ASSERT(WL_RXS_VHT_BW_20UL ==
		IEEE80211_RADIOTAP_VHT_BW_20UL);
	STATIC_ASSERT(WL_RXS_VHT_BW_20UU ==
		IEEE80211_RADIOTAP_VHT_BW_20UU);

	rtvht->vht_bw = rxsts->bw;

	rtvht->vht_mcs_nss[0] = (rxsts->mcs << 4) |
		(rxsts->nss & IEEE80211_RADIOTAP_VHT_NSS);
	rtvht->vht_mcs_nss[1] = 0;
	rtvht->vht_mcs_nss[2] = 0;
	rtvht->vht_mcs_nss[3] = 0;

	STATIC_ASSERT(WL_RXS_VHTF_CODING_LDCP ==
		IEEE80211_RADIOTAP_VHT_CODING_LDPC);

	rtvht->vht_coding = rxsts->coding;
	rtvht->vht_group_id = rxsts->gid;
	rtvht->vht_partial_aid = HTOL16(rxsts->aid);

	rtvht->ampdu_flags = 0;
	rtvht->ampdu_delim_crc = 0;
	rtvht->ampdu_ref_num = HTOL32(rxsts->ampdu_counter);
	if (!(rxsts->nfrmtype & WL_RXS_NFRM_AMPDU_FIRST) &&
		!(rxsts->nfrmtype & WL_RXS_NFRM_AMPDU_SUB)) {
		rtvht->ampdu_flags |= HTOL16(IEEE80211_RADIOTAP_AMPDU_IS_LAST);
	} else {
		rtvht->ampdu_flags |= HTOL16(IEEE80211_RADIOTAP_AMPDU_LAST_KNOWN);
	}

	return 0;
}

/* Rx status to radiotap conversion of HE type */
uint
wl_radiotap_rx_he(struct dot11_header *mac_header, wl_rxsts_t *rxsts,
		ieee80211_radiotap_header_t *rtap_hdr)
{
	int channel_frequency;
	uint16 channel_flags;
	uint8 flags;
	uint16 rtap_len = LTOH16(rtap_hdr->it_len);
	wl_radiotap_he_t *rthe = (wl_radiotap_he_t *)((uint8*)rtap_hdr + rtap_len);

	rtap_len += sizeof(wl_radiotap_he_t);
	rtap_hdr->it_len = HTOL16(rtap_len);
	rtap_hdr->it_present |= HTOL32(WL_RADIOTAP_PRESENT_HE);

	channel_frequency = (uint16)wl_radiotap_rx_channel_frequency(rxsts);
	channel_flags = wl_radiotap_rx_channel_flags(rxsts);
	flags = wl_radiotap_rx_flags(mac_header, rxsts);

	rthe->basic.tsft_l = HTOL32(rxsts->mactime);
	rthe->basic.tsft_h = 0;
	rthe->basic.flags = flags;
	rthe->basic.channel_freq = (uint16)HTOL16(channel_frequency);
	rthe->basic.channel_flags = HTOL16(channel_flags);
	rthe->basic.signal = (int8)rxsts->signal;
	rthe->basic.noise = (int8)rxsts->noise;
	rthe->basic.antenna = (uint8)rxsts->antenna;

	rthe->ampdu_flags = 0;
	rthe->ampdu_delim_crc = 0;
	rthe->ampdu_ref_num = HTOL32(rxsts->ampdu_counter);
	if (!(rxsts->nfrmtype & WL_RXS_NFRM_AMPDU_FIRST) &&
		!(rxsts->nfrmtype & WL_RXS_NFRM_AMPDU_SUB)) {
		rthe->ampdu_flags |= HTOL16(IEEE80211_RADIOTAP_AMPDU_IS_LAST);
	} else {
		rthe->ampdu_flags |= HTOL16(IEEE80211_RADIOTAP_AMPDU_LAST_KNOWN);
	}

	rthe->data1 = HTOL16(rxsts->data1);
	rthe->data2 = HTOL16(rxsts->data2);
	rthe->data3 = HTOL16(rxsts->data3);
	rthe->data4 = HTOL16(rxsts->data4);
	rthe->data5 = HTOL16(rxsts->data5);
	rthe->data6 = HTOL16(rxsts->data6);

	return 0;
}

/* Rx status to radiotap conversion of EHT type */
uint
wl_radiotap_rx_eht(struct dot11_header *mac_header, wl_rxsts_t *rxsts,
	ieee80211_radiotap_header_t *rtap_hdr)
{
	ASSERT(!"wl_radiotap_rx_eht: not implemented!");
	return 0;
}

uint16
wl_rxsts_to_rtap(monitor_pkt_rxsts_t *pkt_rxsts, void *payload,
                 uint16 len, void* pout, uint16 pad_req)
{
	uint16 rtap_len = 0;
	struct dot11_header* mac_header;
	uint8* p = payload;
	ieee80211_radiotap_header_t* rtap_hdr = (ieee80211_radiotap_header_t*)pout;
	wl_rxsts_t* rxsts;

	ASSERT(p && pkt_rxsts);
	rxsts = pkt_rxsts->rxsts;
	rtap_hdr->it_version = 0;
	rtap_hdr->it_pad = 0;
	rtap_hdr->it_len = HTOL16(sizeof(*rtap_hdr));
	rtap_hdr->it_present = 0;
	bitmap = 0;

#ifdef MONITOR_DNGL_CONV
	if (pad_req) {
		radiotap_add_vendor_ns(rtap_hdr);
	}
#endif

#ifdef BCM_MON_QDBM_RSSI
	/* if per-core RSSI is present, add vendor element */
	if (pkt_rxsts->corenum != 0) {
		radiotap_add_vendor_ns(rtap_hdr);
	}
#endif
	mac_header = (struct dot11_header *)(p);

	if (rxsts->encoding == WL_RXS_ENCODING_EHT) {
		wl_radiotap_rx_eht(mac_header, rxsts, rtap_hdr);
	} else if (rxsts->encoding == WL_RXS_ENCODING_HE) {
		wl_radiotap_rx_he(mac_header, rxsts, rtap_hdr);
	} else if (rxsts->encoding == WL_RXS_ENCODING_VHT) {
		wl_radiotap_rx_vht(mac_header, rxsts, rtap_hdr);
	} else if (rxsts->encoding == WL_RXS_ENCODING_HT) {
		wl_radiotap_rx_ht(mac_header, rxsts, rtap_hdr);
	} else {
		uint16 mask = ltoh16(mac_header->fc) & FC_KIND_MASK;
		if (mask == FC_RTS || mask == FC_CTS) {
			radiotap_add_vendor_ns(rtap_hdr);
		}
		wl_radiotap_rx_legacy(mac_header, rxsts, rtap_hdr);
		if (mask == FC_RTS || mask == FC_CTS) {
			radiotap_encode_bw_signaling(mask, rxsts, rtap_hdr);
		}
	}
#ifdef BCM_MON_QDBM_RSSI
	/* if per-core RSSI is present, add vendor element */
	if (pkt_rxsts->corenum != 0) {
		radiotap_encode_multi_rssi(pkt_rxsts, rtap_hdr);
	}
#endif
#ifdef MONITOR_DNGL_CONV
	if (pad_req) {
		radiotap_encode_alignpad(rtap_hdr, pad_req);
	}
#endif
	rtap_len = LTOH16(rtap_hdr->it_len);
	len += rtap_len;

#ifndef MONITOR_DNGL_CONV
	if (len > MAX_MON_PKT_SIZE) {
		return 0;
	}
	/* copy payload */
	if (!(rxsts->nfrmtype & WL_RXS_NFRM_AMSDU_FIRST) &&
			!(rxsts->nfrmtype & WL_RXS_NFRM_AMSDU_SUB)) {
		memcpy((uint8*)pout + rtap_len, (uint8*)p, len - rtap_len);
	}
#endif
#ifdef MONITOR_DNGL_CONV
	return rtap_len;
#else
	return len;
#endif
}

#ifdef BCM_MON_QDBM_RSSI
void
radiotap_encode_multi_rssi(monitor_pkt_rxsts_t* rxsts, ieee80211_radiotap_header_t *hdr)
{
	uint16 cur_len = LTOH16(hdr->it_len);
	uint16 len = ROUNDUP(1 + rxsts->corenum * sizeof(monitor_pkt_rssi_t), 4);
	int i = 0;
	uint8 *vend_p = (uint8 *)hdr + cur_len;
	radiotap_vendor_ns_t *vendor_ns = (radiotap_vendor_ns_t*)vend_p;
	memcpy(vendor_ns->vend_oui, brcm_oui, sizeof(vendor_ns->vend_oui));
	vendor_ns->sns = 1;
	vendor_ns->skip_len = HTOL16(len);
	vend_p += sizeof(*vendor_ns);
	vend_p[0] = rxsts->corenum;
	for (i = 0; i < rxsts->corenum; i++) {
	  vend_p[2*i + 1] = rxsts->rxpwr[i].dBm;
	  vend_p[2*i + 2] = rxsts->rxpwr[i].decidBm;
	}
	hdr->it_len = HTOL16(cur_len + sizeof(radiotap_vendor_ns_t) + len);
}
#endif /* BCM_CORE_RSSI */

#ifdef MONITOR_DNGL_CONV
#define AILIGN_4BYTES	(4u)
void
radiotap_encode_alignpad(ieee80211_radiotap_header_t *hdr, uint16 pad_req)
{
	uint16 cur_len = LTOH16(hdr->it_len);
	uint8 *vend_p = (uint8 *)hdr + cur_len;
	radiotap_vendor_ns_t *vendor_ns = (radiotap_vendor_ns_t*)vend_p;
	uint16 len;
	uint16 align_pad = 0;

	memcpy(vendor_ns->vend_oui, brcm_oui, sizeof(vendor_ns->vend_oui));
	vendor_ns->sns = WL_RADIOTAP_BRCM_PAD_SNS;
	len = cur_len + sizeof(radiotap_vendor_ns_t);
	if (len % AILIGN_4BYTES	!= 0) {
		align_pad = (AILIGN_4BYTES - (len % AILIGN_4BYTES));
	}
	hdr->it_len = HTOL16(len + pad_req + align_pad);
	vendor_ns->skip_len = HTOL16(pad_req + align_pad);
}
#endif /* MONITOR_DNGL_CONV */

void
radiotap_encode_bw_signaling(uint16 mask,
                             struct wl_rxsts* rxsts, ieee80211_radiotap_header_t *hdr)
{
	uint16 cur_len = LTOH16(hdr->it_len);
	uint8 *vend_p = (uint8 *)hdr + cur_len;
	radiotap_vendor_ns_t *vendor_ns = (radiotap_vendor_ns_t  *)vend_p;
	wl_radiotap_nonht_vht_t* nonht_vht;

	memcpy(vendor_ns->vend_oui, brcm_oui, sizeof(vendor_ns->vend_oui));
	vendor_ns->sns = 0;
	vendor_ns->skip_len = sizeof(wl_radiotap_nonht_vht_t);
	nonht_vht = (wl_radiotap_nonht_vht_t *)(vend_p + sizeof(*vendor_ns));

	/* VHT b/w signalling */
	bzero((uint8 *)nonht_vht, sizeof(wl_radiotap_nonht_vht_t));
	nonht_vht->len = WL_RADIOTAP_NONHT_VHT_LEN;
	nonht_vht->flags |= WL_RADIOTAP_F_NONHT_VHT_BW;
	nonht_vht->bw = (uint8)rxsts->bw_nonht;

	if (mask == FC_RTS) {
		if (rxsts->vhtflags & WL_RXS_VHTF_DYN_BW_NONHT) {
			nonht_vht->flags |= WL_RADIOTAP_F_NONHT_VHT_DYN_BW;
		}
	}
	hdr->it_len = HTOL16(cur_len + sizeof(radiotap_vendor_ns_t) +
	                     sizeof(wl_radiotap_nonht_vht_t));
}

void
radiotap_add_vendor_ns(ieee80211_radiotap_header_t *hdr)
{

	uint32 * it_present = &hdr->it_present;
	uint16 len = LTOH16(hdr->it_len);

	/* if the last bitmap has a vendor ns, add a new one */
	if (it_present[bitmap] & (1 << IEEE80211_RADIOTAP_VENDOR_NAMESPACE)) {
		it_present[bitmap] |= 1 << IEEE80211_RADIOTAP_EXT;
		bitmap++;
		/* align to 8 bytes */
		if (bitmap%2) {
			hdr->it_len = HTOL16(len + 8);
		}
		it_present[bitmap] = 1 << IEEE80211_RADIOTAP_VENDOR_NAMESPACE;
	} else {
		it_present[bitmap] |= 1 << IEEE80211_RADIOTAP_VENDOR_NAMESPACE;
	}
}

/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_BRCMU_WIFI_H_
#define	_BRCMU_WIFI_H_

#include <linux/if_ether.h>		/* for ETH_ALEN */
#include <linux/ieee80211.h>		/* for WLAN_PMKID_LEN */

/* A chanspec holds the channel number, band, bandwidth and control sideband */
typedef u16 chanspec_t;

/* channel defines */
#define CH_UPPER_SB			0x01
#define CH_LOWER_SB			0x02
#define CH_EWA_VALID			0x04
#define CH_20MHZ_APART			4
#define CH_10MHZ_APART			2
#define CH_5MHZ_APART			1	/* 2G band channels are 5 Mhz apart */
#define CH_MAX_2G_CHANNEL		14	/* Max channel in 2G band */
#define BRCM_MAX_2G_CHANNEL	CH_MAX_2G_CHANNEL	/* legacy define */
#define	MAXCHANNEL		224	/* max # supported channels. The max channel no is 216,
					 * this is that + 1 rounded up to a multiple of NBBY (8).
					 * DO NOT MAKE it > 255: channels are u8's all over
					 */

#define WL_CHANSPEC_CHAN_MASK		0x00ff
#define WL_CHANSPEC_CHAN_SHIFT		0

#define WL_CHANSPEC_CTL_SB_MASK		0x0300
#define WL_CHANSPEC_CTL_SB_SHIFT	     8
#define WL_CHANSPEC_CTL_SB_LOWER	0x0100
#define WL_CHANSPEC_CTL_SB_UPPER	0x0200
#define WL_CHANSPEC_CTL_SB_NONE		0x0300

#define WL_CHANSPEC_BW_MASK		0x0C00
#define WL_CHANSPEC_BW_SHIFT		    10
#define WL_CHANSPEC_BW_10		0x0400
#define WL_CHANSPEC_BW_20		0x0800
#define WL_CHANSPEC_BW_40		0x0C00

#define WL_CHANSPEC_BAND_MASK		0xf000
#define WL_CHANSPEC_BAND_SHIFT		12
#define WL_CHANSPEC_BAND_5G		0x1000
#define WL_CHANSPEC_BAND_2G		0x2000
#define INVCHANSPEC			255

/* used to calculate the chan_freq = chan_factor * 500Mhz + 5 * chan_number */
#define WF_CHAN_FACTOR_2_4_G		4814	/* 2.4 GHz band, 2407 MHz */
#define WF_CHAN_FACTOR_5_G		10000	/* 5   GHz band, 5000 MHz */
#define WF_CHAN_FACTOR_4_G		8000	/* 4.9 GHz band for Japan */

/* channel defines */
#define LOWER_20_SB(channel)	(((channel) > CH_10MHZ_APART) ? ((channel) - CH_10MHZ_APART) : 0)
#define UPPER_20_SB(channel)	(((channel) < (MAXCHANNEL - CH_10MHZ_APART)) ? \
				((channel) + CH_10MHZ_APART) : 0)
#define CHSPEC_BANDUNIT(chspec)	(CHSPEC_IS5G(chspec) ? BAND_5G_INDEX : \
						       BAND_2G_INDEX)
#define CH20MHZ_CHSPEC(channel)	(chanspec_t)((chanspec_t)(channel) | WL_CHANSPEC_BW_20 | \
				WL_CHANSPEC_CTL_SB_NONE | (((channel) <= CH_MAX_2G_CHANNEL) ? \
				WL_CHANSPEC_BAND_2G : WL_CHANSPEC_BAND_5G))
#define NEXT_20MHZ_CHAN(channel)	(((channel) < (MAXCHANNEL - CH_20MHZ_APART)) ? \
					((channel) + CH_20MHZ_APART) : 0)
#define CH40MHZ_CHSPEC(channel, ctlsb)	(chanspec_t) \
					((channel) | (ctlsb) | WL_CHANSPEC_BW_40 | \
					((channel) <= CH_MAX_2G_CHANNEL ? WL_CHANSPEC_BAND_2G : \
					WL_CHANSPEC_BAND_5G))
#define CHSPEC_CHANNEL(chspec)	((u8)((chspec) & WL_CHANSPEC_CHAN_MASK))
#define CHSPEC_BAND(chspec)	((chspec) & WL_CHANSPEC_BAND_MASK)

#ifdef WL11N_20MHZONLY

#define CHSPEC_CTL_SB(chspec)	WL_CHANSPEC_CTL_SB_NONE
#define CHSPEC_BW(chspec)	WL_CHANSPEC_BW_20
#define CHSPEC_IS10(chspec)	0
#define CHSPEC_IS20(chspec)	1
#ifndef CHSPEC_IS40
#define CHSPEC_IS40(chspec)	0
#endif

#else				/* !WL11N_20MHZONLY */

#define CHSPEC_CTL_SB(chspec)	((chspec) & WL_CHANSPEC_CTL_SB_MASK)
#define CHSPEC_BW(chspec)	((chspec) & WL_CHANSPEC_BW_MASK)
#define CHSPEC_IS10(chspec)	(((chspec) & WL_CHANSPEC_BW_MASK) == WL_CHANSPEC_BW_10)
#define CHSPEC_IS20(chspec)	(((chspec) & WL_CHANSPEC_BW_MASK) == WL_CHANSPEC_BW_20)
#ifndef CHSPEC_IS40
#define CHSPEC_IS40(chspec)	(((chspec) & WL_CHANSPEC_BW_MASK) == WL_CHANSPEC_BW_40)
#endif

#endif				/* !WL11N_20MHZONLY */

#define CHSPEC_IS5G(chspec)	(((chspec) & WL_CHANSPEC_BAND_MASK) == WL_CHANSPEC_BAND_5G)
#define CHSPEC_IS2G(chspec)	(((chspec) & WL_CHANSPEC_BAND_MASK) == WL_CHANSPEC_BAND_2G)
#define CHSPEC_SB_NONE(chspec)	(((chspec) & WL_CHANSPEC_CTL_SB_MASK) == WL_CHANSPEC_CTL_SB_NONE)
#define CHSPEC_SB_UPPER(chspec)	(((chspec) & WL_CHANSPEC_CTL_SB_MASK) == WL_CHANSPEC_CTL_SB_UPPER)
#define CHSPEC_SB_LOWER(chspec)	(((chspec) & WL_CHANSPEC_CTL_SB_MASK) == WL_CHANSPEC_CTL_SB_LOWER)
#define CHSPEC_CTL_CHAN(chspec)  ((CHSPEC_SB_LOWER(chspec)) ? \
				  (LOWER_20_SB(((chspec) & WL_CHANSPEC_CHAN_MASK))) : \
				  (UPPER_20_SB(((chspec) & WL_CHANSPEC_CHAN_MASK))))
#define CHSPEC2BAND(chspec) (CHSPEC_IS5G(chspec) ? BRCM_BAND_5G : BRCM_BAND_2G)

#define CHANSPEC_STR_LEN    8

/* defined rate in 500kbps */
#define BRCM_MAXRATE	108	/* in 500kbps units */
#define BRCM_RATE_1M	2	/* in 500kbps units */
#define BRCM_RATE_2M	4	/* in 500kbps units */
#define BRCM_RATE_5M5	11	/* in 500kbps units */
#define BRCM_RATE_11M	22	/* in 500kbps units */
#define BRCM_RATE_6M	12	/* in 500kbps units */
#define BRCM_RATE_9M	18	/* in 500kbps units */
#define BRCM_RATE_12M	24	/* in 500kbps units */
#define BRCM_RATE_18M	36	/* in 500kbps units */
#define BRCM_RATE_24M	48	/* in 500kbps units */
#define BRCM_RATE_36M	72	/* in 500kbps units */
#define BRCM_RATE_48M	96	/* in 500kbps units */
#define BRCM_RATE_54M	108	/* in 500kbps units */

#define BRCM_2G_25MHZ_OFFSET		5	/* 2.4GHz band channel offset */

#define MCSSET_LEN	16

#define AC_BITMAP_TST(ab, ac)	(((ab) & (1 << (ac))) != 0)

/*
 * Verify the chanspec is using a legal set of parameters, i.e. that the
 * chanspec specified a band, bw, ctl_sb and channel and that the
 * combination could be legal given any set of circumstances.
 * RETURNS: true is the chanspec is malformed, false if it looks good.
 */
extern bool brcmu_chspec_malformed(chanspec_t chanspec);

/*
 * This function returns the channel number that control traffic is being sent on, for legacy
 * channels this is just the channel number, for 40MHZ channels it is the upper or lowre 20MHZ
 * sideband depending on the chanspec selected
 */
extern u8 brcmu_chspec_ctlchan(chanspec_t chspec);

/*
 * Return the channel number for a given frequency and base frequency.
 * The returned channel number is relative to the given base frequency.
 * If the given base frequency is zero, a base frequency of 5 GHz is assumed for
 * frequencies from 5 - 6 GHz, and 2.407 GHz is assumed for 2.4 - 2.5 GHz.
 *
 * Frequency is specified in MHz.
 * The base frequency is specified as (start_factor * 500 kHz).
 * Constants WF_CHAN_FACTOR_2_4_G, WF_CHAN_FACTOR_5_G are defined for
 * 2.4 GHz and 5 GHz bands.
 *
 * The returned channel will be in the range [1, 14] in the 2.4 GHz band
 * and [0, 200] otherwise.
 * -1 is returned if the start_factor is WF_CHAN_FACTOR_2_4_G and the
 * frequency is not a 2.4 GHz channel, or if the frequency is not and even
 * multiple of 5 MHz from the base frequency to the base plus 1 GHz.
 *
 * Reference 802.11 REVma, section 17.3.8.3, and 802.11B section 18.4.6.2
 */
extern int brcmu_mhz2channel(uint freq, uint start_factor);

/* Enumerate crypto algorithms */
#define	CRYPTO_ALGO_OFF			0
#define	CRYPTO_ALGO_WEP1		1
#define	CRYPTO_ALGO_TKIP		2
#define	CRYPTO_ALGO_WEP128		3
#define CRYPTO_ALGO_AES_CCM		4
#define CRYPTO_ALGO_AES_RESERVED1	5
#define CRYPTO_ALGO_AES_RESERVED2	6
#define CRYPTO_ALGO_NALG		7

/* wireless security bitvec */
#define WEP_ENABLED		0x0001
#define TKIP_ENABLED		0x0002
#define AES_ENABLED		0x0004
#define WSEC_SWFLAG		0x0008
#define SES_OW_ENABLED		0x0040	/* to go into transition mode without setting wep */

/* WPA authentication mode bitvec */
#define WPA_AUTH_DISABLED	0x0000	/* Legacy (i.e., non-WPA) */
#define WPA_AUTH_NONE		0x0001	/* none (IBSS) */
#define WPA_AUTH_UNSPECIFIED	0x0002	/* over 802.1x */
#define WPA_AUTH_PSK		0x0004	/* Pre-shared key */
#define WPA_AUTH_RESERVED1	0x0008
#define WPA_AUTH_RESERVED2	0x0010
					/* #define WPA_AUTH_8021X 0x0020 *//* 802.1x, reserved */
#define WPA2_AUTH_RESERVED1	0x0020
#define WPA2_AUTH_UNSPECIFIED	0x0040	/* over 802.1x */
#define WPA2_AUTH_PSK		0x0080	/* Pre-shared key */
#define WPA2_AUTH_RESERVED3	0x0200
#define WPA2_AUTH_RESERVED4	0x0400
#define WPA2_AUTH_RESERVED5	0x0800

/* pmkid */
#define	MAXPMKID		16

#define DOT11_DEFAULT_RTS_LEN		2347
#define DOT11_DEFAULT_FRAG_LEN		2346

#define DOT11_ICV_AES_LEN		8
#define DOT11_QOS_LEN			2
#define DOT11_IV_MAX_LEN		8
#define DOT11_A4_HDR_LEN		30

#define HT_CAP_RX_STBC_NO		0x0
#define HT_CAP_RX_STBC_ONE_STREAM	0x1

typedef struct _pmkid {
	u8 BSSID[ETH_ALEN];
	u8 PMKID[WLAN_PMKID_LEN];
} pmkid_t;

typedef struct _pmkid_list {
	u32 npmkid;
	pmkid_t pmkid[1];
} pmkid_list_t;

typedef struct _pmkid_cand {
	u8 BSSID[ETH_ALEN];
	u8 preauth;
} pmkid_cand_t;

typedef struct _pmkid_cand_list {
	u32 npmkid_cand;
	pmkid_cand_t pmkid_cand[1];
} pmkid_cand_list_t;

typedef u8 ac_bitmap_t;

#endif				/* _BRCMU_WIFI_H_ */

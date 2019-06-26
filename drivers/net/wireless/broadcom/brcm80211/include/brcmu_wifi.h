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

/*
 * A chanspec (u16) holds the channel number, band, bandwidth and control
 * sideband
 */

/* channel defines */
#define CH_UPPER_SB			0x01
#define CH_LOWER_SB			0x02
#define CH_EWA_VALID			0x04
#define CH_70MHZ_APART			14
#define CH_50MHZ_APART			10
#define CH_30MHZ_APART			6
#define CH_20MHZ_APART			4
#define CH_10MHZ_APART			2
#define CH_5MHZ_APART			1 /* 2G band channels are 5 Mhz apart */
#define CH_MIN_2G_CHANNEL		1
#define CH_MAX_2G_CHANNEL		14	/* Max channel in 2G band */
#define CH_MIN_5G_CHANNEL		34

/* bandstate array indices */
#define BAND_2G_INDEX		0	/* wlc->bandstate[x] index */
#define BAND_5G_INDEX		1	/* wlc->bandstate[x] index */

/*
 * max # supported channels. The max channel no is 216, this is that + 1
 * rounded up to a multiple of NBBY (8). DO NOT MAKE it > 255: channels are
 * u8's all over
*/
#define	MAXCHANNEL		224

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
#define WL_CHANSPEC_BW_80		0x2000

#define WL_CHANSPEC_BAND_MASK		0xf000
#define WL_CHANSPEC_BAND_SHIFT		12
#define WL_CHANSPEC_BAND_5G		0x1000
#define WL_CHANSPEC_BAND_2G		0x2000
#define INVCHANSPEC			255

#define WL_CHAN_VALID_HW		(1 << 0) /* valid with current HW */
#define WL_CHAN_VALID_SW		(1 << 1) /* valid with country sett. */
#define WL_CHAN_BAND_5G			(1 << 2) /* 5GHz-band channel */
#define WL_CHAN_RADAR			(1 << 3) /* radar sensitive  channel */
#define WL_CHAN_INACTIVE		(1 << 4) /* inactive due to radar */
#define WL_CHAN_PASSIVE			(1 << 5) /* channel in passive mode */
#define WL_CHAN_RESTRICTED		(1 << 6) /* restricted use channel */

/* values for band specific 40MHz capabilities  */
#define WLC_N_BW_20ALL			0
#define WLC_N_BW_40ALL			1
#define WLC_N_BW_20IN2G_40IN5G		2

#define WLC_BW_20MHZ_BIT		BIT(0)
#define WLC_BW_40MHZ_BIT		BIT(1)
#define WLC_BW_80MHZ_BIT		BIT(2)
#define WLC_BW_160MHZ_BIT		BIT(3)

/* Bandwidth capabilities */
#define WLC_BW_CAP_20MHZ		(WLC_BW_20MHZ_BIT)
#define WLC_BW_CAP_40MHZ		(WLC_BW_40MHZ_BIT|WLC_BW_20MHZ_BIT)
#define WLC_BW_CAP_80MHZ		(WLC_BW_80MHZ_BIT|WLC_BW_40MHZ_BIT| \
					 WLC_BW_20MHZ_BIT)
#define WLC_BW_CAP_160MHZ		(WLC_BW_160MHZ_BIT|WLC_BW_80MHZ_BIT| \
					 WLC_BW_40MHZ_BIT|WLC_BW_20MHZ_BIT)
#define WLC_BW_CAP_UNRESTRICTED		0xFF

/* band types */
#define	WLC_BAND_AUTO			0	/* auto-select */
#define	WLC_BAND_5G			1	/* 5 Ghz */
#define	WLC_BAND_2G			2	/* 2.4 Ghz */
#define	WLC_BAND_ALL			3	/* all bands */

#define CHSPEC_CHANNEL(chspec)	((u8)((chspec) & WL_CHANSPEC_CHAN_MASK))
#define CHSPEC_BAND(chspec)	((chspec) & WL_CHANSPEC_BAND_MASK)

#define CHSPEC_CTL_SB(chspec)	((chspec) & WL_CHANSPEC_CTL_SB_MASK)
#define CHSPEC_BW(chspec)	((chspec) & WL_CHANSPEC_BW_MASK)

#define CHSPEC_IS10(chspec) \
	(((chspec) & WL_CHANSPEC_BW_MASK) == WL_CHANSPEC_BW_10)

#define CHSPEC_IS20(chspec) \
	(((chspec) & WL_CHANSPEC_BW_MASK) == WL_CHANSPEC_BW_20)

#define CHSPEC_IS40(chspec) \
	(((chspec) & WL_CHANSPEC_BW_MASK) == WL_CHANSPEC_BW_40)

#define CHSPEC_IS80(chspec) \
	(((chspec) & WL_CHANSPEC_BW_MASK) == WL_CHANSPEC_BW_80)

#define CHSPEC_IS5G(chspec) \
	(((chspec) & WL_CHANSPEC_BAND_MASK) == WL_CHANSPEC_BAND_5G)

#define CHSPEC_IS2G(chspec) \
	(((chspec) & WL_CHANSPEC_BAND_MASK) == WL_CHANSPEC_BAND_2G)

#define CHSPEC_SB_NONE(chspec) \
	(((chspec) & WL_CHANSPEC_CTL_SB_MASK) == WL_CHANSPEC_CTL_SB_NONE)

#define CHSPEC_SB_UPPER(chspec) \
	(((chspec) & WL_CHANSPEC_CTL_SB_MASK) == WL_CHANSPEC_CTL_SB_UPPER)

#define CHSPEC_SB_LOWER(chspec) \
	(((chspec) & WL_CHANSPEC_CTL_SB_MASK) == WL_CHANSPEC_CTL_SB_LOWER)

#define CHSPEC_CTL_CHAN(chspec) \
	((CHSPEC_SB_LOWER(chspec)) ? \
	(lower_20_sb(((chspec) & WL_CHANSPEC_CHAN_MASK))) : \
	(upper_20_sb(((chspec) & WL_CHANSPEC_CHAN_MASK))))

#define CHSPEC2BAND(chspec) (CHSPEC_IS5G(chspec) ? BRCM_BAND_5G : BRCM_BAND_2G)

#define CHANSPEC_STR_LEN    8

static inline int lower_20_sb(int channel)
{
	return channel > CH_10MHZ_APART ? (channel - CH_10MHZ_APART) : 0;
}

static inline int upper_20_sb(int channel)
{
	return (channel < (MAXCHANNEL - CH_10MHZ_APART)) ?
	       channel + CH_10MHZ_APART : 0;
}

static inline int chspec_bandunit(u16 chspec)
{
	return CHSPEC_IS5G(chspec) ? BAND_5G_INDEX : BAND_2G_INDEX;
}

static inline u16 ch20mhz_chspec(int channel)
{
	u16 rc = channel <= CH_MAX_2G_CHANNEL ?
		 WL_CHANSPEC_BAND_2G : WL_CHANSPEC_BAND_5G;

	return	(u16)((u16)channel | WL_CHANSPEC_BW_20 |
		      WL_CHANSPEC_CTL_SB_NONE | rc);
}

static inline int next_20mhz_chan(int channel)
{
	return channel < (MAXCHANNEL - CH_20MHZ_APART) ?
	       channel + CH_20MHZ_APART : 0;
}

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

static inline bool ac_bitmap_tst(u8 bitmap, int prec)
{
	return (bitmap & (1 << (prec))) != 0;
}

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
/* to go into transition mode without setting wep */
#define SES_OW_ENABLED		0x0040
/* MFP */
#define MFP_CAPABLE		0x0200
#define MFP_REQUIRED		0x0400

/* WPA authentication mode bitvec */
#define WPA_AUTH_DISABLED	0x0000	/* Legacy (i.e., non-WPA) */
#define WPA_AUTH_NONE		0x0001	/* none (IBSS) */
#define WPA_AUTH_UNSPECIFIED	0x0002	/* over 802.1x */
#define WPA_AUTH_PSK		0x0004	/* Pre-shared key */
#define WPA_AUTH_RESERVED1	0x0008
#define WPA_AUTH_RESERVED2	0x0010

#define WPA2_AUTH_RESERVED1	0x0020
#define WPA2_AUTH_UNSPECIFIED	0x0040	/* over 802.1x */
#define WPA2_AUTH_PSK		0x0080	/* Pre-shared key */
#define WPA2_AUTH_RESERVED3	0x0200
#define WPA2_AUTH_RESERVED4	0x0400
#define WPA2_AUTH_RESERVED5	0x0800
#define WPA2_AUTH_1X_SHA256	0x1000  /* 1X with SHA256 key derivation */
#define WPA2_AUTH_FT		0x4000	/* Fast BSS Transition */
#define WPA2_AUTH_PSK_SHA256	0x8000	/* PSK with SHA256 key derivation */

#define DOT11_DEFAULT_RTS_LEN		2347
#define DOT11_DEFAULT_FRAG_LEN		2346

#define DOT11_ICV_AES_LEN		8
#define DOT11_QOS_LEN			2
#define DOT11_IV_MAX_LEN		8
#define DOT11_A4_HDR_LEN		30

#define HT_CAP_RX_STBC_NO		0x0
#define HT_CAP_RX_STBC_ONE_STREAM	0x1

#endif				/* _BRCMU_WIFI_H_ */

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

#ifndef _BRCM_PUB_H_
#define _BRCM_PUB_H_

#include <brcmu_wifi.h>
#include "types.h"
#include "defs.h"

enum brcms_srom_id {
	BRCMS_SROM_NULL,
	BRCMS_SROM_CONT,
	BRCMS_SROM_AA2G,
	BRCMS_SROM_AA5G,
	BRCMS_SROM_AG0,
	BRCMS_SROM_AG1,
	BRCMS_SROM_AG2,
	BRCMS_SROM_AG3,
	BRCMS_SROM_ANTSWCTL2G,
	BRCMS_SROM_ANTSWCTL5G,
	BRCMS_SROM_ANTSWITCH,
	BRCMS_SROM_BOARDFLAGS2,
	BRCMS_SROM_BOARDFLAGS,
	BRCMS_SROM_BOARDNUM,
	BRCMS_SROM_BOARDREV,
	BRCMS_SROM_BOARDTYPE,
	BRCMS_SROM_BW40PO,
	BRCMS_SROM_BWDUPPO,
	BRCMS_SROM_BXA2G,
	BRCMS_SROM_BXA5G,
	BRCMS_SROM_CC,
	BRCMS_SROM_CCK2GPO,
	BRCMS_SROM_CCKBW202GPO,
	BRCMS_SROM_CCKBW20UL2GPO,
	BRCMS_SROM_CCODE,
	BRCMS_SROM_CDDPO,
	BRCMS_SROM_DEVID,
	BRCMS_SROM_ET1MACADDR,
	BRCMS_SROM_EXTPAGAIN2G,
	BRCMS_SROM_EXTPAGAIN5G,
	BRCMS_SROM_FREQOFFSET_CORR,
	BRCMS_SROM_HW_IQCAL_EN,
	BRCMS_SROM_IL0MACADDR,
	BRCMS_SROM_IQCAL_SWP_DIS,
	BRCMS_SROM_LEDBH0,
	BRCMS_SROM_LEDBH1,
	BRCMS_SROM_LEDBH2,
	BRCMS_SROM_LEDBH3,
	BRCMS_SROM_LEDDC,
	BRCMS_SROM_LEGOFDM40DUPPO,
	BRCMS_SROM_LEGOFDMBW202GPO,
	BRCMS_SROM_LEGOFDMBW205GHPO,
	BRCMS_SROM_LEGOFDMBW205GLPO,
	BRCMS_SROM_LEGOFDMBW205GMPO,
	BRCMS_SROM_LEGOFDMBW20UL2GPO,
	BRCMS_SROM_LEGOFDMBW20UL5GHPO,
	BRCMS_SROM_LEGOFDMBW20UL5GLPO,
	BRCMS_SROM_LEGOFDMBW20UL5GMPO,
	BRCMS_SROM_MACADDR,
	BRCMS_SROM_MCS2GPO0,
	BRCMS_SROM_MCS2GPO1,
	BRCMS_SROM_MCS2GPO2,
	BRCMS_SROM_MCS2GPO3,
	BRCMS_SROM_MCS2GPO4,
	BRCMS_SROM_MCS2GPO5,
	BRCMS_SROM_MCS2GPO6,
	BRCMS_SROM_MCS2GPO7,
	BRCMS_SROM_MCS32PO,
	BRCMS_SROM_MCS5GHPO0,
	BRCMS_SROM_MCS5GHPO1,
	BRCMS_SROM_MCS5GHPO2,
	BRCMS_SROM_MCS5GHPO3,
	BRCMS_SROM_MCS5GHPO4,
	BRCMS_SROM_MCS5GHPO5,
	BRCMS_SROM_MCS5GHPO6,
	BRCMS_SROM_MCS5GHPO7,
	BRCMS_SROM_MCS5GLPO0,
	BRCMS_SROM_MCS5GLPO1,
	BRCMS_SROM_MCS5GLPO2,
	BRCMS_SROM_MCS5GLPO3,
	BRCMS_SROM_MCS5GLPO4,
	BRCMS_SROM_MCS5GLPO5,
	BRCMS_SROM_MCS5GLPO6,
	BRCMS_SROM_MCS5GLPO7,
	BRCMS_SROM_MCS5GPO0,
	BRCMS_SROM_MCS5GPO1,
	BRCMS_SROM_MCS5GPO2,
	BRCMS_SROM_MCS5GPO3,
	BRCMS_SROM_MCS5GPO4,
	BRCMS_SROM_MCS5GPO5,
	BRCMS_SROM_MCS5GPO6,
	BRCMS_SROM_MCS5GPO7,
	BRCMS_SROM_MCSBW202GPO,
	BRCMS_SROM_MCSBW205GHPO,
	BRCMS_SROM_MCSBW205GLPO,
	BRCMS_SROM_MCSBW205GMPO,
	BRCMS_SROM_MCSBW20UL2GPO,
	BRCMS_SROM_MCSBW20UL5GHPO,
	BRCMS_SROM_MCSBW20UL5GLPO,
	BRCMS_SROM_MCSBW20UL5GMPO,
	BRCMS_SROM_MCSBW402GPO,
	BRCMS_SROM_MCSBW405GHPO,
	BRCMS_SROM_MCSBW405GLPO,
	BRCMS_SROM_MCSBW405GMPO,
	BRCMS_SROM_MEASPOWER,
	BRCMS_SROM_OFDM2GPO,
	BRCMS_SROM_OFDM5GHPO,
	BRCMS_SROM_OFDM5GLPO,
	BRCMS_SROM_OFDM5GPO,
	BRCMS_SROM_OPO,
	BRCMS_SROM_PA0B0,
	BRCMS_SROM_PA0B1,
	BRCMS_SROM_PA0B2,
	BRCMS_SROM_PA0ITSSIT,
	BRCMS_SROM_PA0MAXPWR,
	BRCMS_SROM_PA1B0,
	BRCMS_SROM_PA1B1,
	BRCMS_SROM_PA1B2,
	BRCMS_SROM_PA1HIB0,
	BRCMS_SROM_PA1HIB1,
	BRCMS_SROM_PA1HIB2,
	BRCMS_SROM_PA1HIMAXPWR,
	BRCMS_SROM_PA1ITSSIT,
	BRCMS_SROM_PA1LOB0,
	BRCMS_SROM_PA1LOB1,
	BRCMS_SROM_PA1LOB2,
	BRCMS_SROM_PA1LOMAXPWR,
	BRCMS_SROM_PA1MAXPWR,
	BRCMS_SROM_PDETRANGE2G,
	BRCMS_SROM_PDETRANGE5G,
	BRCMS_SROM_PHYCAL_TEMPDELTA,
	BRCMS_SROM_RAWTEMPSENSE,
	BRCMS_SROM_REGREV,
	BRCMS_SROM_REV,
	BRCMS_SROM_RSSISAV2G,
	BRCMS_SROM_RSSISAV5G,
	BRCMS_SROM_RSSISMC2G,
	BRCMS_SROM_RSSISMC5G,
	BRCMS_SROM_RSSISMF2G,
	BRCMS_SROM_RSSISMF5G,
	BRCMS_SROM_RXCHAIN,
	BRCMS_SROM_RXPO2G,
	BRCMS_SROM_RXPO5G,
	BRCMS_SROM_STBCPO,
	BRCMS_SROM_TEMPCORRX,
	BRCMS_SROM_TEMPOFFSET,
	BRCMS_SROM_TEMPSENSE_OPTION,
	BRCMS_SROM_TEMPSENSE_SLOPE,
	BRCMS_SROM_TEMPTHRESH,
	BRCMS_SROM_TRI2G,
	BRCMS_SROM_TRI5GH,
	BRCMS_SROM_TRI5GL,
	BRCMS_SROM_TRI5G,
	BRCMS_SROM_TRISO2G,
	BRCMS_SROM_TRISO5G,
	BRCMS_SROM_TSSIPOS2G,
	BRCMS_SROM_TSSIPOS5G,
	BRCMS_SROM_TXCHAIN,
	BRCMS_SROM_TXPID2GA0,
	BRCMS_SROM_TXPID2GA1,
	BRCMS_SROM_TXPID2GA2,
	BRCMS_SROM_TXPID2GA3,
	BRCMS_SROM_TXPID5GA0,
	BRCMS_SROM_TXPID5GA1,
	BRCMS_SROM_TXPID5GA2,
	BRCMS_SROM_TXPID5GA3,
	BRCMS_SROM_TXPID5GHA0,
	BRCMS_SROM_TXPID5GHA1,
	BRCMS_SROM_TXPID5GHA2,
	BRCMS_SROM_TXPID5GHA3,
	BRCMS_SROM_TXPID5GLA0,
	BRCMS_SROM_TXPID5GLA1,
	BRCMS_SROM_TXPID5GLA2,
	BRCMS_SROM_TXPID5GLA3,
	/*
	 * per-path identifiers (see srom.c)
	 */
	BRCMS_SROM_ITT2GA0,
	BRCMS_SROM_ITT2GA1,
	BRCMS_SROM_ITT2GA2,
	BRCMS_SROM_ITT2GA3,
	BRCMS_SROM_ITT5GA0,
	BRCMS_SROM_ITT5GA1,
	BRCMS_SROM_ITT5GA2,
	BRCMS_SROM_ITT5GA3,
	BRCMS_SROM_MAXP2GA0,
	BRCMS_SROM_MAXP2GA1,
	BRCMS_SROM_MAXP2GA2,
	BRCMS_SROM_MAXP2GA3,
	BRCMS_SROM_MAXP5GA0,
	BRCMS_SROM_MAXP5GA1,
	BRCMS_SROM_MAXP5GA2,
	BRCMS_SROM_MAXP5GA3,
	BRCMS_SROM_MAXP5GHA0,
	BRCMS_SROM_MAXP5GHA1,
	BRCMS_SROM_MAXP5GHA2,
	BRCMS_SROM_MAXP5GHA3,
	BRCMS_SROM_MAXP5GLA0,
	BRCMS_SROM_MAXP5GLA1,
	BRCMS_SROM_MAXP5GLA2,
	BRCMS_SROM_MAXP5GLA3,
	BRCMS_SROM_PA2GW0A0,
	BRCMS_SROM_PA2GW0A1,
	BRCMS_SROM_PA2GW0A2,
	BRCMS_SROM_PA2GW0A3,
	BRCMS_SROM_PA2GW1A0,
	BRCMS_SROM_PA2GW1A1,
	BRCMS_SROM_PA2GW1A2,
	BRCMS_SROM_PA2GW1A3,
	BRCMS_SROM_PA2GW2A0,
	BRCMS_SROM_PA2GW2A1,
	BRCMS_SROM_PA2GW2A2,
	BRCMS_SROM_PA2GW2A3,
	BRCMS_SROM_PA2GW3A0,
	BRCMS_SROM_PA2GW3A1,
	BRCMS_SROM_PA2GW3A2,
	BRCMS_SROM_PA2GW3A3,
	BRCMS_SROM_PA5GHW0A0,
	BRCMS_SROM_PA5GHW0A1,
	BRCMS_SROM_PA5GHW0A2,
	BRCMS_SROM_PA5GHW0A3,
	BRCMS_SROM_PA5GHW1A0,
	BRCMS_SROM_PA5GHW1A1,
	BRCMS_SROM_PA5GHW1A2,
	BRCMS_SROM_PA5GHW1A3,
	BRCMS_SROM_PA5GHW2A0,
	BRCMS_SROM_PA5GHW2A1,
	BRCMS_SROM_PA5GHW2A2,
	BRCMS_SROM_PA5GHW2A3,
	BRCMS_SROM_PA5GHW3A0,
	BRCMS_SROM_PA5GHW3A1,
	BRCMS_SROM_PA5GHW3A2,
	BRCMS_SROM_PA5GHW3A3,
	BRCMS_SROM_PA5GLW0A0,
	BRCMS_SROM_PA5GLW0A1,
	BRCMS_SROM_PA5GLW0A2,
	BRCMS_SROM_PA5GLW0A3,
	BRCMS_SROM_PA5GLW1A0,
	BRCMS_SROM_PA5GLW1A1,
	BRCMS_SROM_PA5GLW1A2,
	BRCMS_SROM_PA5GLW1A3,
	BRCMS_SROM_PA5GLW2A0,
	BRCMS_SROM_PA5GLW2A1,
	BRCMS_SROM_PA5GLW2A2,
	BRCMS_SROM_PA5GLW2A3,
	BRCMS_SROM_PA5GLW3A0,
	BRCMS_SROM_PA5GLW3A1,
	BRCMS_SROM_PA5GLW3A2,
	BRCMS_SROM_PA5GLW3A3,
	BRCMS_SROM_PA5GW0A0,
	BRCMS_SROM_PA5GW0A1,
	BRCMS_SROM_PA5GW0A2,
	BRCMS_SROM_PA5GW0A3,
	BRCMS_SROM_PA5GW1A0,
	BRCMS_SROM_PA5GW1A1,
	BRCMS_SROM_PA5GW1A2,
	BRCMS_SROM_PA5GW1A3,
	BRCMS_SROM_PA5GW2A0,
	BRCMS_SROM_PA5GW2A1,
	BRCMS_SROM_PA5GW2A2,
	BRCMS_SROM_PA5GW2A3,
	BRCMS_SROM_PA5GW3A0,
	BRCMS_SROM_PA5GW3A1,
	BRCMS_SROM_PA5GW3A2,
	BRCMS_SROM_PA5GW3A3,
};

#define	BRCMS_NUMRATES	16	/* max # of rates in a rateset */
#define	D11_PHY_HDR_LEN	6	/* Phy header length - 6 bytes */

/* phy types */
#define	PHY_TYPE_A	0	/* Phy type A */
#define	PHY_TYPE_G	2	/* Phy type G */
#define	PHY_TYPE_N	4	/* Phy type N */
#define	PHY_TYPE_LP	5	/* Phy type Low Power A/B/G */
#define	PHY_TYPE_SSN	6	/* Phy type Single Stream N */
#define	PHY_TYPE_LCN	8	/* Phy type Single Stream N */
#define	PHY_TYPE_LCNXN	9	/* Phy type 2-stream N */
#define	PHY_TYPE_HT	7	/* Phy type 3-Stream N */

/* bw */
#define BRCMS_10_MHZ	10	/* 10Mhz nphy channel bandwidth */
#define BRCMS_20_MHZ	20	/* 20Mhz nphy channel bandwidth */
#define BRCMS_40_MHZ	40	/* 40Mhz nphy channel bandwidth */

#define	BRCMS_RSSI_MINVAL	-200	/* Low value, e.g. for forcing roam */
#define	BRCMS_RSSI_NO_SIGNAL	-91	/* NDIS RSSI link quality cutoffs */
#define	BRCMS_RSSI_VERY_LOW	-80	/* Very low quality cutoffs */
#define	BRCMS_RSSI_LOW		-70	/* Low quality cutoffs */
#define	BRCMS_RSSI_GOOD		-68	/* Good quality cutoffs */
#define	BRCMS_RSSI_VERY_GOOD	-58	/* Very good quality cutoffs */
#define	BRCMS_RSSI_EXCELLENT	-57	/* Excellent quality cutoffs */

/* a large TX Power as an init value to factor out of min() calculations,
 * keep low enough to fit in an s8, units are .25 dBm
 */
#define BRCMS_TXPWR_MAX		(127)	/* ~32 dBm = 1,500 mW */

/* rate related definitions */
#define	BRCMS_RATE_FLAG	0x80	/* Flag to indicate it is a basic rate */
#define	BRCMS_RATE_MASK	0x7f	/* Rate value mask w/o basic rate flag */

/* legacy rx Antenna diversity for SISO rates */
#define	ANT_RX_DIV_FORCE_0	0	/* Use antenna 0 */
#define	ANT_RX_DIV_FORCE_1	1	/* Use antenna 1 */
#define	ANT_RX_DIV_START_1	2	/* Choose starting with 1 */
#define	ANT_RX_DIV_START_0	3	/* Choose starting with 0 */
#define	ANT_RX_DIV_ENABLE	3	/* APHY bbConfig Enable RX Diversity */
/* default antdiv setting */
#define ANT_RX_DIV_DEF		ANT_RX_DIV_START_0

/* legacy rx Antenna diversity for SISO rates */
/* Tx on antenna 0, "legacy term Main" */
#define ANT_TX_FORCE_0		0
/* Tx on antenna 1, "legacy term Aux" */
#define ANT_TX_FORCE_1		1
/* Tx on phy's last good Rx antenna */
#define ANT_TX_LAST_RX		3
/* driver's default tx antenna setting */
#define ANT_TX_DEF		3

/* Tx Chain values */
/* def bitmap of txchain */
#define TXCHAIN_DEF		0x1
/* default bitmap of tx chains for nphy */
#define TXCHAIN_DEF_NPHY	0x3
/* default bitmap of tx chains for nphy */
#define TXCHAIN_DEF_HTPHY	0x7
/* def bitmap of rxchain */
#define RXCHAIN_DEF		0x1
/* default bitmap of rx chains for nphy */
#define RXCHAIN_DEF_NPHY	0x3
/* default bitmap of rx chains for nphy */
#define RXCHAIN_DEF_HTPHY	0x7
/* no antenna switch */
#define ANTSWITCH_NONE		0
/* antenna switch on 4321CB2, 2of3 */
#define ANTSWITCH_TYPE_1	1
/* antenna switch on 4321MPCI, 2of3 */
#define ANTSWITCH_TYPE_2	2
/* antenna switch on 4322, 2of3 */
#define ANTSWITCH_TYPE_3	3

#define RXBUFSZ		PKTBUFSZ

#define MAX_STREAMS_SUPPORTED	4	/* max number of streams supported */

struct brcm_rateset {
	/* # rates in this set */
	u32 count;
	/* rates in 500kbps units w/hi bit set if basic */
	u8 rates[WL_NUMRATES];
};

struct brcms_c_rateset {
	uint count;		/* number of rates in rates[] */
	 /* rates in 500kbps units w/hi bit set if basic */
	u8 rates[BRCMS_NUMRATES];
	u8 htphy_membership;	/* HT PHY Membership */
	u8 mcs[MCSSET_LEN];	/* supported mcs index bit map */
};

/* All the HT-specific default advertised capabilities (including AMPDU)
 * should be grouped here at one place
 */
#define AMPDU_DEF_MPDU_DENSITY	6	/* default mpdu density (110 ==> 4us) */

/* wlc internal bss_info */
struct brcms_bss_info {
	u8 BSSID[ETH_ALEN];	/* network BSSID */
	u16 flags;		/* flags for internal attributes */
	u8 SSID_len;		/* the length of SSID */
	u8 SSID[32];		/* SSID string */
	s16 RSSI;		/* receive signal strength (in dBm) */
	s16 SNR;		/* receive signal SNR in dB */
	u16 beacon_period;	/* units are Kusec */
	u16 chanspec;	/* Channel num, bw, ctrl_sb and band */
	struct brcms_c_rateset rateset;	/* supported rates */
};

#define MAC80211_PROMISC_BCNS	(1 << 0)
#define MAC80211_SCAN		(1 << 1)

/*
 * Public portion of common driver state structure.
 * The wlc handle points at this.
 */
struct brcms_pub {
	struct brcms_c_info *wlc;
	struct ieee80211_hw *ieee_hw;
	struct scb_ampdu *global_ampdu;
	uint mac80211_state;
	uint unit;		/* device instance number */
	uint corerev;		/* core revision */
	struct si_pub *sih;	/* SI handle (cookie for siutils calls) */
	bool up;		/* interface up and running */
	bool hw_off;		/* HW is off */
	bool hw_up;		/* one time hw up/down */
	bool _piomode;		/* true if pio mode */
	uint _nbands;		/* # bands supported */
	uint now;		/* # elapsed seconds */

	bool promisc;		/* promiscuous destination address */
	bool delayed_down;	/* down delayed */
	bool associated;	/* true:part of [I]BSS, false: not */
	/* (union of stas_associated, aps_associated) */
	bool _ampdu;		/* ampdu enabled or not */
	u8 _n_enab;		/* bitmap of 11N + HT support */

	u8 cur_etheraddr[ETH_ALEN];	/* our local ethernet address */

	int bcmerror;		/* last bcm error */

	u32 radio_disabled;	/* bit vector for radio disabled reasons */

	u16 boardrev;	/* version # of particular board */
	u8 sromrev;		/* version # of the srom */
	char srom_ccode[BRCM_CNTRY_BUF_SZ];	/* Country Code in SROM */
	u32 boardflags;	/* Board specific flags from srom */
	u32 boardflags2;	/* More board flags if sromrev >= 4 */
	bool phy_11ncapable;	/* the PHY/HW is capable of 802.11N */

	struct wl_cnt *_cnt;	/* low-level counters in driver */
};

enum wlc_par_id {
	IOV_MPC = 1,
	IOV_RTSTHRESH,
	IOV_QTXPOWER,
	IOV_BCN_LI_BCN		/* Beacon listen interval in # of beacons */
};

/***********************************************
 * Feature-related macros to optimize out code *
 * *********************************************
 */

#define ENAB_1x1	0x01
#define ENAB_2x2	0x02
#define ENAB_3x3	0x04
#define ENAB_4x4	0x08
#define SUPPORT_11N	(ENAB_1x1|ENAB_2x2)
#define SUPPORT_HT	(ENAB_1x1|ENAB_2x2|ENAB_3x3)

/* WL11N Support */
#define AMPDU_AGG_HOST	1

/* pri is priority encoded in the packet. This maps the Packet priority to
 * enqueue precedence as defined in wlc_prec_map
 */
extern const u8 wlc_prio2prec_map[];
#define BRCMS_PRIO_TO_PREC(pri)	wlc_prio2prec_map[(pri) & 7]

#define	BRCMS_PREC_COUNT	16	/* Max precedence level implemented */

/* Mask to describe all precedence levels */
#define BRCMS_PREC_BMP_ALL		MAXBITVAL(BRCMS_PREC_COUNT)

/*
 * This maps priority to one precedence higher - Used by PS-Poll response
 * packets to simulate enqueue-at-head operation, but still maintain the
 * order on the queue
 */
#define BRCMS_PRIO_TO_HI_PREC(pri)	min(BRCMS_PRIO_TO_PREC(pri) + 1,\
					    BRCMS_PREC_COUNT - 1)

/* Define a bitmap of precedences comprised by each AC */
#define BRCMS_PREC_BMP_AC_BE	(NBITVAL(BRCMS_PRIO_TO_PREC(PRIO_8021D_BE)) | \
			NBITVAL(BRCMS_PRIO_TO_HI_PREC(PRIO_8021D_BE)) |	\
			NBITVAL(BRCMS_PRIO_TO_PREC(PRIO_8021D_EE)) |	\
			NBITVAL(BRCMS_PRIO_TO_HI_PREC(PRIO_8021D_EE)))
#define BRCMS_PREC_BMP_AC_BK	(NBITVAL(BRCMS_PRIO_TO_PREC(PRIO_8021D_BK)) | \
			NBITVAL(BRCMS_PRIO_TO_HI_PREC(PRIO_8021D_BK)) |	\
			NBITVAL(BRCMS_PRIO_TO_PREC(PRIO_8021D_NONE)) |	\
			NBITVAL(BRCMS_PRIO_TO_HI_PREC(PRIO_8021D_NONE)))
#define BRCMS_PREC_BMP_AC_VI	(NBITVAL(BRCMS_PRIO_TO_PREC(PRIO_8021D_CL)) | \
			NBITVAL(BRCMS_PRIO_TO_HI_PREC(PRIO_8021D_CL)) |	\
			NBITVAL(BRCMS_PRIO_TO_PREC(PRIO_8021D_VI)) |	\
			NBITVAL(BRCMS_PRIO_TO_HI_PREC(PRIO_8021D_VI)))
#define BRCMS_PREC_BMP_AC_VO	(NBITVAL(BRCMS_PRIO_TO_PREC(PRIO_8021D_VO)) | \
			NBITVAL(BRCMS_PRIO_TO_HI_PREC(PRIO_8021D_VO)) |	\
			NBITVAL(BRCMS_PRIO_TO_PREC(PRIO_8021D_NC)) |	\
			NBITVAL(BRCMS_PRIO_TO_HI_PREC(PRIO_8021D_NC)))

/* network protection config */
#define	BRCMS_PROT_G_SPEC		1	/* SPEC g protection */
#define	BRCMS_PROT_G_OVR		2	/* SPEC g prot override */
#define	BRCMS_PROT_G_USER		3	/* gmode specified by user */
#define	BRCMS_PROT_OVERLAP	4	/* overlap */
#define	BRCMS_PROT_N_USER		10	/* nmode specified by user */
#define	BRCMS_PROT_N_CFG		11	/* n protection */
#define	BRCMS_PROT_N_CFG_OVR	12	/* n protection override */
#define	BRCMS_PROT_N_NONGF	13	/* non-GF protection */
#define	BRCMS_PROT_N_NONGF_OVR	14	/* non-GF protection override */
#define	BRCMS_PROT_N_PAM_OVR	15	/* n preamble override */
#define	BRCMS_PROT_N_OBSS		16	/* non-HT OBSS present */

/*
 * 54g modes (basic bits may still be overridden)
 *
 * GMODE_LEGACY_B
 *	Rateset: 1b, 2b, 5.5, 11
 *	Preamble: Long
 *	Shortslot: Off
 * GMODE_AUTO
 *	Rateset: 1b, 2b, 5.5b, 11b, 18, 24, 36, 54
 *	Extended Rateset: 6, 9, 12, 48
 *	Preamble: Long
 *	Shortslot: Auto
 * GMODE_ONLY
 *	Rateset: 1b, 2b, 5.5b, 11b, 18, 24b, 36, 54
 *	Extended Rateset: 6b, 9, 12b, 48
 *	Preamble: Short required
 *	Shortslot: Auto
 * GMODE_B_DEFERRED
 *	Rateset: 1b, 2b, 5.5b, 11b, 18, 24, 36, 54
 *	Extended Rateset: 6, 9, 12, 48
 *	Preamble: Long
 *	Shortslot: On
 * GMODE_PERFORMANCE
 *	Rateset: 1b, 2b, 5.5b, 6b, 9, 11b, 12b, 18, 24b, 36, 48, 54
 *	Preamble: Short required
 *	Shortslot: On and required
 * GMODE_LRS
 *	Rateset: 1b, 2b, 5.5b, 11b
 *	Extended Rateset: 6, 9, 12, 18, 24, 36, 48, 54
 *	Preamble: Long
 *	Shortslot: Auto
 */
#define GMODE_LEGACY_B		0
#define GMODE_AUTO		1
#define GMODE_ONLY		2
#define GMODE_B_DEFERRED	3
#define GMODE_PERFORMANCE	4
#define GMODE_LRS		5
#define GMODE_MAX		6

/* MCS values greater than this enable multiple streams */
#define HIGHEST_SINGLE_STREAM_MCS	7

#define	MAXBANDS		2	/* Maximum #of bands */

/* max number of antenna configurations */
#define ANT_SELCFG_MAX		4

struct brcms_antselcfg {
	u8 ant_config[ANT_SELCFG_MAX];	/* antenna configuration */
	u8 num_antcfg;	/* number of available antenna configurations */
};

/* common functions for every port */
extern struct brcms_c_info *
brcms_c_attach(struct brcms_info *wl, u16 vendor, u16 device, uint unit,
	       bool piomode, void __iomem *regsva, struct pci_dev *btparam,
	       uint *perr);
extern uint brcms_c_detach(struct brcms_c_info *wlc);
extern int brcms_c_up(struct brcms_c_info *wlc);
extern uint brcms_c_down(struct brcms_c_info *wlc);

extern bool brcms_c_chipmatch(u16 vendor, u16 device);
extern void brcms_c_init(struct brcms_c_info *wlc);
extern void brcms_c_reset(struct brcms_c_info *wlc);

extern void brcms_c_intrson(struct brcms_c_info *wlc);
extern u32 brcms_c_intrsoff(struct brcms_c_info *wlc);
extern void brcms_c_intrsrestore(struct brcms_c_info *wlc, u32 macintmask);
extern bool brcms_c_intrsupd(struct brcms_c_info *wlc);
extern bool brcms_c_isr(struct brcms_c_info *wlc, bool *wantdpc);
extern bool brcms_c_dpc(struct brcms_c_info *wlc, bool bounded);
extern void brcms_c_sendpkt_mac80211(struct brcms_c_info *wlc,
				     struct sk_buff *sdu,
				     struct ieee80211_hw *hw);
extern bool brcms_c_aggregatable(struct brcms_c_info *wlc, u8 tid);
extern void brcms_c_protection_upd(struct brcms_c_info *wlc, uint idx,
				   int val);
extern int brcms_c_get_header_len(void);
extern void brcms_c_set_addrmatch(struct brcms_c_info *wlc,
				  int match_reg_offset,
				  const u8 *addr);
extern void brcms_c_wme_setparams(struct brcms_c_info *wlc, u16 aci,
			      const struct ieee80211_tx_queue_params *arg,
			      bool suspend);
extern struct brcms_pub *brcms_c_pub(struct brcms_c_info *wlc);
extern void brcms_c_ampdu_flush(struct brcms_c_info *wlc,
			    struct ieee80211_sta *sta, u16 tid);
extern void brcms_c_ampdu_tx_operational(struct brcms_c_info *wlc, u8 tid,
					 u8 ba_wsize, uint max_rx_ampdu_bytes);
extern char *getvar(struct si_pub *sih, enum brcms_srom_id id);
extern int getintvar(struct si_pub *sih, enum brcms_srom_id id);
extern int brcms_c_module_register(struct brcms_pub *pub,
				   const char *name, struct brcms_info *hdl,
				   int (*down_fn)(void *handle));
extern int brcms_c_module_unregister(struct brcms_pub *pub, const char *name,
				     struct brcms_info *hdl);
extern void brcms_c_suspend_mac_and_wait(struct brcms_c_info *wlc);
extern void brcms_c_enable_mac(struct brcms_c_info *wlc);
extern void brcms_c_associate_upd(struct brcms_c_info *wlc, bool state);
extern void brcms_c_scan_start(struct brcms_c_info *wlc);
extern void brcms_c_scan_stop(struct brcms_c_info *wlc);
extern int brcms_c_get_curband(struct brcms_c_info *wlc);
extern void brcms_c_wait_for_tx_completion(struct brcms_c_info *wlc,
					   bool drop);
extern int brcms_c_set_channel(struct brcms_c_info *wlc, u16 channel);
extern int brcms_c_set_rate_limit(struct brcms_c_info *wlc, u16 srl, u16 lrl);
extern void brcms_c_get_current_rateset(struct brcms_c_info *wlc,
				 struct brcm_rateset *currs);
extern int brcms_c_set_rateset(struct brcms_c_info *wlc,
					struct brcm_rateset *rs);
extern int brcms_c_set_beacon_period(struct brcms_c_info *wlc, u16 period);
extern u16 brcms_c_get_phy_type(struct brcms_c_info *wlc, int phyidx);
extern void brcms_c_set_shortslot_override(struct brcms_c_info *wlc,
				    s8 sslot_override);
extern void brcms_c_set_beacon_listen_interval(struct brcms_c_info *wlc,
					u8 interval);
extern int brcms_c_set_tx_power(struct brcms_c_info *wlc, int txpwr);
extern int brcms_c_get_tx_power(struct brcms_c_info *wlc);
extern void brcms_c_set_radio_mpc(struct brcms_c_info *wlc, bool mpc);
extern bool brcms_c_check_radio_disabled(struct brcms_c_info *wlc);

#endif				/* _BRCM_PUB_H_ */

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

#define	BRCMS_NUMRATES	16	/* max # of rates in a rateset */
#define	MAXMULTILIST	32	/* max # multicast addresses */
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

#define CHSPEC_WLC_BW(chanspec)	(CHSPEC_IS40(chanspec) ? BRCMS_40_MHZ : \
				 CHSPEC_IS20(chanspec) ? BRCMS_20_MHZ : \
							 BRCMS_10_MHZ)

#define	BRCMS_RSSI_MINVAL	-200	/* Low value, e.g. for forcing roam */
#define	BRCMS_RSSI_NO_SIGNAL	-91	/* NDIS RSSI link quality cutoffs */
#define	BRCMS_RSSI_VERY_LOW	-80	/* Very low quality cutoffs */
#define	BRCMS_RSSI_LOW		-70	/* Low quality cutoffs */
#define	BRCMS_RSSI_GOOD		-68	/* Good quality cutoffs */
#define	BRCMS_RSSI_VERY_GOOD	-58	/* Very good quality cutoffs */
#define	BRCMS_RSSI_EXCELLENT	-57	/* Excellent quality cutoffs */

/* macro to perform PHY -> D11 PHY TYPE, currently 1:1 */
#define BRCMS_PHYTYPE(_x) (_x)

#define MA_WINDOW_SZ		8	/* moving average window size */

#define BRCMS_SNR_INVALID		0	/* invalid SNR value */

/* a large TX Power as an init value to factor out of min() calculations,
 * keep low enough to fit in an s8, units are .25 dBm
 */
#define BRCMS_TXPWR_MAX		(127)	/* ~32 dBm = 1,500 mW */

/* rate related definitions */
#define	BRCMS_RATE_FLAG	0x80	/* Flag to indicate it is a basic rate */
#define	BRCMS_RATE_MASK	0x7f	/* Rate value mask w/o basic rate flag */

/* legacy rx Antenna diversity for SISO rates */
#define	ANT_RX_DIV_FORCE_0		0	/* Use antenna 0 */
#define	ANT_RX_DIV_FORCE_1		1	/* Use antenna 1 */
#define	ANT_RX_DIV_START_1		2	/* Choose starting with 1 */
#define	ANT_RX_DIV_START_0		3	/* Choose starting with 0 */
#define	ANT_RX_DIV_ENABLE		3	/* APHY bbConfig Enable RX Diversity */
#define ANT_RX_DIV_DEF		ANT_RX_DIV_START_0	/* default antdiv setting */

/* legacy rx Antenna diversity for SISO rates */
#define ANT_TX_FORCE_0		0	/* Tx on antenna 0, "legacy term Main" */
#define ANT_TX_FORCE_1		1	/* Tx on antenna 1, "legacy term Aux" */
#define ANT_TX_LAST_RX		3	/* Tx on phy's last good Rx antenna */
#define ANT_TX_DEF			3	/* driver's default tx antenna setting */

#define TXCORE_POLICY_ALL	0x1	/* use all available core for transmit */

/* Tx Chain values */
#define TXCHAIN_DEF		0x1	/* def bitmap of txchain */
#define TXCHAIN_DEF_NPHY	0x3	/* default bitmap of tx chains for nphy */
#define TXCHAIN_DEF_HTPHY	0x7	/* default bitmap of tx chains for nphy */
#define RXCHAIN_DEF		0x1	/* def bitmap of rxchain */
#define RXCHAIN_DEF_NPHY	0x3	/* default bitmap of rx chains for nphy */
#define RXCHAIN_DEF_HTPHY	0x7	/* default bitmap of rx chains for nphy */
#define ANTSWITCH_NONE		0	/* no antenna switch */
#define ANTSWITCH_TYPE_1	1	/* antenna switch on 4321CB2, 2of3 */
#define ANTSWITCH_TYPE_2	2	/* antenna switch on 4321MPCI, 2of3 */
#define ANTSWITCH_TYPE_3	3	/* antenna switch on 4322, 2of3 */

#define RXBUFSZ		PKTBUFSZ
#ifndef AIDMAPSZ
#define AIDMAPSZ	(roundup(MAXSCB, NBBY)/NBBY)	/* aid bitmap size in bytes */
#endif				/* AIDMAPSZ */

#define MAX_STREAMS_SUPPORTED	4	/* max number of streams supported */

#define	WL_SPURAVOID_OFF	0
#define	WL_SPURAVOID_ON1	1
#define	WL_SPURAVOID_ON2	2

struct brcms_tunables {
	int ntxd;		/* size of tx descriptor table */
	int nrxd;		/* size of rx descriptor table */
	int rxbufsz;		/* size of rx buffers to post */
	int nrxbufpost;		/* # of rx buffers to post */
	int maxscb;		/* # of SCBs supported */
	int ampdunummpdu;	/* max number of mpdu in an ampdu */
	int maxpktcb;		/* max # of packet callbacks */
	int maxucodebss;	/* max # of BSS handled in ucode bcn/prb */
	int maxucodebss4;	/* max # of BSS handled in sw bcn/prb */
	int maxbss;		/* max # of bss info elements in scan list */
	int datahiwat;		/* data msg txq hiwat mark */
	int ampdudatahiwat;	/* AMPDU msg txq hiwat mark */
	int rxbnd;		/* max # of rx bufs to process before deferring to dpc */
	int txsbnd;		/* max # tx status to process in wlc_txstatus() */
	int memreserved;	/* memory reserved for BMAC's USB dma rx */
};

struct brcms_rateset {
	uint count;		/* number of rates in rates[] */
	 /* rates in 500kbps units w/hi bit set if basic */
	u8 rates[BRCMS_NUMRATES];
	u8 htphy_membership;	/* HT PHY Membership */
	u8 mcs[MCSSET_LEN];	/* supported mcs index bit map */
};

struct rsn_parms {
	u8 flags;		/* misc booleans (e.g., supported) */
	u8 multicast;	/* multicast cipher */
	u8 ucount;		/* count of unicast ciphers */
	u8 unicast[4];	/* unicast ciphers */
	u8 acount;		/* count of auth modes */
	u8 auth[4];		/* Authentication modes */
	u8 PAD[4];		/* padding for future growth */
};

/*
 * 32 SSID chars, max of 4 chars for each SSID char "\xFF", plus NULL.
 */
#define SSID_FMT_BUF_LEN	((4 * IEEE80211_MAX_SSID_LEN) + 1)

#define RSN_FLAGS_SUPPORTED		0x1	/* Flag for rsn_params */
#define RSN_FLAGS_PREAUTH		0x2	/* Flag for WPA2 rsn_params */

/* All the HT-specific default advertised capabilities (including AMPDU)
 * should be grouped here at one place
 */
#define AMPDU_DEF_MPDU_DENSITY	6	/* default mpdu density (110 ==> 4us) */

/* defaults for the HT (MIMO) bss */
#define HT_CAP	(IEEE80211_HT_CAP_SM_PS |\
	IEEE80211_HT_CAP_SUP_WIDTH_20_40 | IEEE80211_HT_CAP_GRN_FLD |\
	IEEE80211_HT_CAP_MAX_AMSDU | IEEE80211_HT_CAP_DSSSCCK40)

/* wlc internal bss_info */
struct brcms_bss_info {
	u8 BSSID[ETH_ALEN];	/* network BSSID */
	u16 flags;		/* flags for internal attributes */
	u8 SSID_len;		/* the length of SSID */
	u8 SSID[32];		/* SSID string */
	s16 RSSI;		/* receive signal strength (in dBm) */
	s16 SNR;		/* receive signal SNR in dB */
	u16 beacon_period;	/* units are Kusec */
	u16 atim_window;	/* units are Kusec */
	chanspec_t chanspec;	/* Channel num, bw, ctrl_sb and band */
	s8 infra;		/* 0=IBSS, 1=infrastructure, 2=unknown */
	wlc_rateset_t rateset;	/* supported rates */
	u8 dtim_period;	/* DTIM period */
	s8 phy_noise;		/* noise right after tx (in dBm) */
	u16 capability;	/* Capability information */
	u8 wme_qosinfo;	/* QoS Info from WME IE; valid if BSS_WME flag set */
	struct rsn_parms wpa;
	struct rsn_parms wpa2;
	u16 qbss_load_aac;	/* qbss load available admission capacity */
	/* qbss_load_chan_free <- (0xff - channel_utilization of qbss_load_ie_t) */
	u8 qbss_load_chan_free;	/* indicates how free the channel is */
	u8 mcipher;		/* multicast cipher */
	u8 wpacfg;		/* wpa config index */
};

/* IOVar flags for common error checks */
#define IOVF_MFG	(1<<3)	/* flag for mfgtest iovars */
#define IOVF_WHL	(1<<4)	/* value must be whole (0-max) */
#define IOVF_NTRL	(1<<5)	/* value must be natural (1-max) */

#define IOVF_SET_UP	(1<<6)	/* set requires driver be up */
#define IOVF_SET_DOWN	(1<<7)	/* set requires driver be down */
#define IOVF_SET_CLK	(1<<8)	/* set requires core clock */
#define IOVF_SET_BAND	(1<<9)	/* set requires fixed band */

#define IOVF_GET_UP	(1<<10)	/* get requires driver be up */
#define IOVF_GET_DOWN	(1<<11)	/* get requires driver be down */
#define IOVF_GET_CLK	(1<<12)	/* get requires core clock */
#define IOVF_GET_BAND	(1<<13)	/* get requires fixed band */
#define IOVF_OPEN_ALLOW	(1<<14)	/* set allowed iovar for opensrc */

/* watchdog down and dump callback function proto's */
typedef int (*watchdog_fn_t) (void *handle);
typedef int (*down_fn_t) (void *handle);
typedef int (*dump_fn_t) (void *handle, struct brcmu_strbuf *b);

/* IOVar handler
 *
 * handle - a pointer value registered with the function
 * vi - iovar_info that was looked up
 * actionid - action ID, calculated by IOV_GVAL() and IOV_SVAL() based on varid.
 * name - the actual iovar name
 * params/plen - parameters and length for a get, input only.
 * arg/len - buffer and length for value to be set or retrieved, input or output.
 * vsize - value size, valid for integer type only.
 * wlcif - interface context (brcms_c_if pointer)
 *
 * All pointers may point into the same buffer.
 */
typedef int (*iovar_fn_t) (void *handle, const struct brcmu_iovar *vi,
			   u32 actionid, const char *name, void *params,
			   uint plen, void *arg, int alen, int vsize,
			   struct brcms_c_if *wlcif);

#define MAC80211_PROMISC_BCNS	(1 << 0)
#define MAC80211_SCAN		(1 << 1)

/*
 * Public portion of "common" os-independent state structure.
 * The wlc handle points at this.
 */
struct brcms_pub {
	void *wlc;

	struct ieee80211_hw *ieee_hw;
	struct scb *global_scb;
	struct scb_ampdu *global_ampdu;
	uint mac80211_state;
	uint unit;		/* device instance number */
	uint corerev;		/* core revision */
	struct si_pub *sih;	/* SI handle (cookie for siutils calls) */
	char *vars;		/* "environment" name=value */
	bool up;		/* interface up and running */
	bool hw_off;		/* HW is off */
	/* tunables: ntxd, nrxd, maxscb, etc. */
	struct brcms_tunables *tunables;
	bool hw_up;		/* one time hw up/down(from boot or hibernation) */
	bool _piomode;		/* true if pio mode *//* BMAC_NOTE: NEED In both */
	uint _nbands;		/* # bands supported */
	uint now;		/* # elapsed seconds */

	bool promisc;		/* promiscuous destination address */
	bool delayed_down;	/* down delayed */
	bool _ap;		/* AP mode enabled */
	bool _apsta;		/* simultaneous AP/STA mode enabled */
	bool _assoc_recreate;	/* association recreation on up transitions */
	int _wme;		/* WME QoS mode */
	u8 _mbss;		/* MBSS mode on */
	bool allmulti;		/* enable all multicasts */
	bool associated;	/* true:part of [I]BSS, false: not */
	/* (union of stas_associated, aps_associated) */
	bool phytest_on;	/* whether a PHY test is running */
	bool bf_preempt_4306;	/* True to enable 'darwin' mode */
	bool _ampdu;		/* ampdu enabled or not */
	bool _cac;		/* 802.11e CAC enabled */
	u8 _n_enab;		/* bitmap of 11N + HT support */
	bool _n_reqd;		/* N support required for clients */

	s8 _coex;		/* 20/40 MHz BSS Management AUTO, ENAB, DISABLE */
	bool _priofc;		/* Priority-based flowcontrol */

	u8 cur_etheraddr[ETH_ALEN];	/* our local ethernet address */

	u8 *multicast;	/* ptr to list of multicast addresses */
	uint nmulticast;	/* # enabled multicast addresses */

	u32 wlfeatureflag;	/* Flags to control sw features from registry */
	int psq_pkts_total;	/* total num of ps pkts */

	u16 txmaxpkts;	/* max number of large pkts allowed to be pending */

	/* s/w decryption counters */
	u32 swdecrypt;	/* s/w decrypt attempts */

	int bcmerror;		/* last bcm error */

	mbool radio_disabled;	/* bit vector for radio disabled reasons */
	bool radio_active;	/* radio on/off state */
	u16 roam_time_thresh;	/* Max. # secs. of not hearing beacons
					 * before roaming.
					 */
	bool align_wd_tbtt;	/* Align watchdog with tbtt indication
				 * handling. This flag is cleared by default
				 * and is set by per port code explicitly and
				 * you need to make sure the OSL_SYSUPTIME()
				 * is implemented properly in osl of that port
				 * when it enables this Power Save feature.
				 */

	u16 boardrev;	/* version # of particular board */
	u8 sromrev;		/* version # of the srom */
	char srom_ccode[BRCM_CNTRY_BUF_SZ];	/* Country Code in SROM */
	u32 boardflags;	/* Board specific flags from srom */
	u32 boardflags2;	/* More board flags if sromrev >= 4 */
	bool tempsense_disable;	/* disable periodic tempsense check */
	bool phy_11ncapable;	/* the PHY/HW is capable of 802.11N */
	bool _ampdumac;		/* mac assist ampdu enabled or not */

	struct wl_cnt *_cnt;	/* low-level counters in driver */
};

/* wl_monitor rx status per packet */
struct wl_rxsts {
	uint pkterror;		/* error flags per pkt */
	uint phytype;		/* 802.11 A/B/G ... */
	uint channel;		/* channel */
	uint datarate;		/* rate in 500kbps */
	uint antenna;		/* antenna pkts received on */
	uint pktlength;		/* pkt length minus bcm phy hdr */
	u32 mactime;		/* time stamp from mac, count per 1us */
	uint sq;		/* signal quality */
	s32 signal;		/* in dbm */
	s32 noise;		/* in dbm */
	uint preamble;		/* Unknown, short, long */
	uint encoding;		/* Unknown, CCK, PBCC, OFDM */
	uint nfrmtype;		/* special 802.11n frames(AMPDU, AMSDU) */
	struct brcms_if *wlif;	/* wl interface */
};

/* status per error RX pkt */
#define WL_RXS_CRC_ERROR		0x00000001	/* CRC Error in packet */
#define WL_RXS_RUNT_ERROR		0x00000002	/* Runt packet */
#define WL_RXS_ALIGN_ERROR		0x00000004	/* Misaligned packet */
#define WL_RXS_OVERSIZE_ERROR		0x00000008	/* packet bigger than RX_LENGTH (usually 1518) */
#define WL_RXS_WEP_ICV_ERROR		0x00000010	/* Integrity Check Value error */
#define WL_RXS_WEP_ENCRYPTED		0x00000020	/* Encrypted with WEP */
#define WL_RXS_PLCP_SHORT		0x00000040	/* Short PLCP error */
#define WL_RXS_DECRYPT_ERR		0x00000080	/* Decryption error */
#define WL_RXS_OTHER_ERR		0x80000000	/* Other errors */

/* phy type */
#define WL_RXS_PHY_A			0x00000000	/* A phy type */
#define WL_RXS_PHY_B			0x00000001	/* B phy type */
#define WL_RXS_PHY_G			0x00000002	/* G phy type */
#define WL_RXS_PHY_N			0x00000004	/* N phy type */

/* encoding */
#define WL_RXS_ENCODING_CCK		0x00000000	/* CCK encoding */
#define WL_RXS_ENCODING_OFDM		0x00000001	/* OFDM encoding */

/* preamble */
#define WL_RXS_UNUSED_STUB		0x0	/* stub to match with wlc_ethereal.h */
#define WL_RXS_PREAMBLE_SHORT		0x00000001	/* Short preamble */
#define WL_RXS_PREAMBLE_LONG		0x00000002	/* Long preamble */
#define WL_RXS_PREAMBLE_MIMO_MM		0x00000003	/* MIMO mixed mode preamble */
#define WL_RXS_PREAMBLE_MIMO_GF		0x00000004	/* MIMO green field preamble */

#define WL_RXS_NFRM_AMPDU_FIRST		0x00000001	/* first MPDU in A-MPDU */
#define WL_RXS_NFRM_AMPDU_SUB		0x00000002	/* subsequent MPDU(s) in A-MPDU */
#define WL_RXS_NFRM_AMSDU_FIRST		0x00000004	/* first MSDU in A-MSDU */
#define WL_RXS_NFRM_AMSDU_SUB		0x00000008	/* subsequent MSDU(s) in A-MSDU */

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

/* AP Support (versus STA) */
#define	AP_ENAB(pub)	(0)

/* Macro to check if APSTA mode enabled */
#define APSTA_ENAB(pub)	(0)

/* Some useful combinations */
#define STA_ONLY(pub)	(!AP_ENAB(pub))
#define AP_ONLY(pub)	(AP_ENAB(pub) && !APSTA_ENAB(pub))

#define ENAB_1x1	0x01
#define ENAB_2x2	0x02
#define ENAB_3x3	0x04
#define ENAB_4x4	0x08
#define SUPPORT_11N	(ENAB_1x1|ENAB_2x2)
#define SUPPORT_HT	(ENAB_1x1|ENAB_2x2|ENAB_3x3)
/* WL11N Support */
#if ((defined(NCONF) && (NCONF != 0)) || (defined(LCNCONF) && (LCNCONF != 0)) || \
	(defined(HTCONF) && (HTCONF != 0)) || (defined(SSLPNCONF) && (SSLPNCONF != 0)))
#define N_ENAB(pub) ((pub)->_n_enab & SUPPORT_11N)
#define N_REQD(pub) ((pub)->_n_reqd)
#else
#define N_ENAB(pub)	0
#define N_REQD(pub)	0
#endif

#if (defined(HTCONF) && (HTCONF != 0))
#define HT_ENAB(pub) (((pub)->_n_enab & SUPPORT_HT) == SUPPORT_HT)
#else
#define HT_ENAB(pub) 0
#endif

#define AMPDU_AGG_HOST	1
#define AMPDU_ENAB(pub) ((pub)->_ampdu)

#define EDCF_ENAB(pub) (WME_ENAB(pub))
#define QOS_ENAB(pub) (WME_ENAB(pub) || N_ENAB(pub))

#define MONITOR_ENAB(wlc)	((wlc)->monitor)

#define PROMISC_ENAB(wlc)	((wlc)->promisc)

#define	BRCMS_PREC_COUNT	16	/* Max precedence level implemented */

/* pri is priority encoded in the packet. This maps the Packet priority to
 * enqueue precedence as defined in wlc_prec_map
 */
extern const u8 wlc_prio2prec_map[];
#define BRCMS_PRIO_TO_PREC(pri)	wlc_prio2prec_map[(pri) & 7]

/* This maps priority to one precedence higher - Used by PS-Poll response packets to
 * simulate enqueue-at-head operation, but still maintain the order on the queue
 */
#define BRCMS_PRIO_TO_HI_PREC(pri)	min(BRCMS_PRIO_TO_PREC(pri) + 1,\
					    BRCMS_PREC_COUNT - 1)

extern const u8 wme_fifo2ac[];
#define WME_PRIO2AC(prio)	wme_fifo2ac[prio2fifo[(prio)]]

/* Mask to describe all precedence levels */
#define BRCMS_PREC_BMP_ALL		MAXBITVAL(BRCMS_PREC_COUNT)

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

/* WME Support */
#define WME_ENAB(pub) ((pub)->_wme != OFF)
#define WME_AUTO(wlc) ((wlc)->pub->_wme == AUTO)

/* invalid core flags, use the saved coreflags */
#define BRCMS_USE_COREFLAGS	0xffffffff


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
 * GMODE_LEGACY_B			Rateset: 1b, 2b, 5.5, 11
 *					Preamble: Long
 *					Shortslot: Off
 * GMODE_AUTO				Rateset: 1b, 2b, 5.5b, 11b, 18, 24, 36, 54
 *					Extended Rateset: 6, 9, 12, 48
 *					Preamble: Long
 *					Shortslot: Auto
 * GMODE_ONLY				Rateset: 1b, 2b, 5.5b, 11b, 18, 24b, 36, 54
 *					Extended Rateset: 6b, 9, 12b, 48
 *					Preamble: Short required
 *					Shortslot: Auto
 * GMODE_B_DEFERRED			Rateset: 1b, 2b, 5.5b, 11b, 18, 24, 36, 54
 *					Extended Rateset: 6, 9, 12, 48
 *					Preamble: Long
 *					Shortslot: On
 * GMODE_PERFORMANCE			Rateset: 1b, 2b, 5.5b, 6b, 9, 11b, 12b, 18, 24b, 36, 48, 54
 *					Preamble: Short required
 *					Shortslot: On and required
 * GMODE_LRS				Rateset: 1b, 2b, 5.5b, 11b
 *					Extended Rateset: 6, 9, 12, 18, 24, 36, 48, 54
 *					Preamble: Long
 *					Shortslot: Auto
 */
#define GMODE_LEGACY_B		0
#define GMODE_AUTO		1
#define GMODE_ONLY		2
#define GMODE_B_DEFERRED	3
#define GMODE_PERFORMANCE	4
#define GMODE_LRS		5
#define GMODE_MAX		6

/* values for PLCPHdr_override */
#define BRCMS_PLCP_AUTO	-1
#define BRCMS_PLCP_SHORT	0
#define BRCMS_PLCP_LONG	1

/* values for g_protection_override and n_protection_override */
#define BRCMS_PROTECTION_AUTO		-1
#define BRCMS_PROTECTION_OFF		0
#define BRCMS_PROTECTION_ON		1
#define BRCMS_PROTECTION_MMHDR_ONLY	2
#define BRCMS_PROTECTION_CTS_ONLY		3

/* values for g_protection_control and n_protection_control */
#define BRCMS_PROTECTION_CTL_OFF		0
#define BRCMS_PROTECTION_CTL_LOCAL	1
#define BRCMS_PROTECTION_CTL_OVERLAP	2

/* values for n_protection */
#define BRCMS_N_PROTECTION_OFF		0
#define BRCMS_N_PROTECTION_OPTIONAL	1
#define BRCMS_N_PROTECTION_20IN40		2
#define BRCMS_N_PROTECTION_MIXEDMODE	3

/* values for band specific 40MHz capabilities */
#define BRCMS_N_BW_20ALL			0
#define BRCMS_N_BW_40ALL			1
#define BRCMS_N_BW_20IN2G_40IN5G		2

/* bitflags for SGI support (sgi_rx iovar) */
#define BRCMS_N_SGI_20			0x01
#define BRCMS_N_SGI_40			0x02

/* defines used by the nrate iovar */
#define NRATE_MCS_INUSE	0x00000080	/* MSC in use,indicates b0-6 holds an mcs */
#define NRATE_RATE_MASK 0x0000007f	/* rate/mcs value */
#define NRATE_STF_MASK	0x0000ff00	/* stf mode mask: siso, cdd, stbc, sdm */
#define NRATE_STF_SHIFT	8	/* stf mode shift */
#define NRATE_OVERRIDE	0x80000000	/* bit indicates override both rate & mode */
#define NRATE_OVERRIDE_MCS_ONLY 0x40000000	/* bit indicate to override mcs only */
#define NRATE_SGI_MASK  0x00800000	/* sgi mode */
#define NRATE_SGI_SHIFT 23	/* sgi mode */
#define NRATE_LDPC_CODING 0x00400000	/* bit indicates adv coding in use */
#define NRATE_LDPC_SHIFT 22	/* ldpc shift */

#define NRATE_STF_SISO	0	/* stf mode SISO */
#define NRATE_STF_CDD	1	/* stf mode CDD */
#define NRATE_STF_STBC	2	/* stf mode STBC */
#define NRATE_STF_SDM	3	/* stf mode SDM */

#define ANT_SELCFG_MAX		4	/* max number of antenna configurations */

#define HIGHEST_SINGLE_STREAM_MCS	7	/* MCS values greater than this enable multiple streams */

struct brcms_antselcfg {
	u8 ant_config[ANT_SELCFG_MAX];	/* antenna configuration */
	u8 num_antcfg;	/* number of available antenna configurations */
};

/* common functions for every port */
extern void *brcms_c_attach(struct brcms_info *wl, u16 vendor, u16 device,
			uint unit, bool piomode, void *regsva, uint bustype,
			void *btparam, uint *perr);
extern uint brcms_c_detach(struct brcms_c_info *wlc);
extern int brcms_c_up(struct brcms_c_info *wlc);
extern uint brcms_c_down(struct brcms_c_info *wlc);

extern int brcms_c_set(struct brcms_c_info *wlc, int cmd, int arg);
extern int brcms_c_get(struct brcms_c_info *wlc, int cmd, int *arg);
extern bool brcms_c_chipmatch(u16 vendor, u16 device);
extern void brcms_c_init(struct brcms_c_info *wlc);
extern void brcms_c_reset(struct brcms_c_info *wlc);

extern void brcms_c_intrson(struct brcms_c_info *wlc);
extern u32 brcms_c_intrsoff(struct brcms_c_info *wlc);
extern void brcms_c_intrsrestore(struct brcms_c_info *wlc, u32 macintmask);
extern bool brcms_c_intrsupd(struct brcms_c_info *wlc);
extern bool brcms_c_isr(struct brcms_c_info *wlc, bool *wantdpc);
extern bool brcms_c_dpc(struct brcms_c_info *wlc, bool bounded);
extern bool brcms_c_sendpkt_mac80211(struct brcms_c_info *wlc,
				     struct sk_buff *sdu,
				     struct ieee80211_hw *hw);
extern int brcms_c_ioctl(struct brcms_c_info *wlc, int cmd, void *arg, int len,
			 struct brcms_c_if *wlcif);
extern bool brcms_c_aggregatable(struct brcms_c_info *wlc, u8 tid);

/* helper functions */
extern void brcms_c_statsupd(struct brcms_c_info *wlc);
extern void brcms_c_protection_upd(struct brcms_c_info *wlc, uint idx,
				   int val);
extern int brcms_c_get_header_len(void);
extern void brcms_c_mac_bcn_promisc_change(struct brcms_c_info *wlc,
					   bool promisc);
extern void brcms_c_set_addrmatch(struct brcms_c_info *wlc,
				  int match_reg_offset,
				  const u8 *addr);
extern void brcms_c_wme_setparams(struct brcms_c_info *wlc, u16 aci,
			      const struct ieee80211_tx_queue_params *arg,
			      bool suspend);
extern struct brcms_pub *brcms_c_pub(void *wlc);

/* common functions for every port */
extern void brcms_c_mhf(struct brcms_c_info *wlc, u8 idx, u16 mask, u16 val,
		    int bands);
extern void brcms_c_rate_lookup_init(struct brcms_c_info *wlc,
				     wlc_rateset_t *rateset);
extern void brcms_default_rateset(struct brcms_c_info *wlc, wlc_rateset_t *rs);

extern void brcms_c_ampdu_flush(struct brcms_c_info *wlc,
			    struct ieee80211_sta *sta, u16 tid);
extern void brcms_c_ampdu_tx_operational(struct brcms_c_info *wlc, u8 tid,
					 u8 ba_wsize, uint max_rx_ampdu_bytes);
extern int brcms_c_set_par(struct brcms_c_info *wlc, enum wlc_par_id par_id,
			   int val);
extern int brcms_c_get_par(struct brcms_c_info *wlc, enum wlc_par_id par_id,
			   int *ret_int_ptr);
extern char *getvar(char *vars, const char *name);
extern int getintvar(char *vars, const char *name);

/* wlc_phy.c helper functions */
extern void brcms_c_set_ps_ctrl(struct brcms_c_info *wlc);
extern void brcms_c_mctrl(struct brcms_c_info *wlc, u32 mask, u32 val);

extern int brcms_c_module_register(struct brcms_pub *pub,
			       const char *name, void *hdl,
			       watchdog_fn_t watchdog_fn, down_fn_t down_fn);
extern int brcms_c_module_unregister(struct brcms_pub *pub, const char *name,
				 void *hdl);
extern void brcms_c_suspend_mac_and_wait(struct brcms_c_info *wlc);
extern void brcms_c_enable_mac(struct brcms_c_info *wlc);
extern void brcms_c_associate_upd(struct brcms_c_info *wlc, bool state);
extern void brcms_c_scan_start(struct brcms_c_info *wlc);
extern void brcms_c_scan_stop(struct brcms_c_info *wlc);
extern int brcms_c_get_curband(struct brcms_c_info *wlc);
extern void brcms_c_wait_for_tx_completion(struct brcms_c_info *wlc,
					   bool drop);

/* helper functions */
extern bool brcms_c_check_radio_disabled(struct brcms_c_info *wlc);
extern bool brcms_c_radio_monitor_stop(struct brcms_c_info *wlc);

#define	MAXBANDS		2	/* Maximum #of bands */
/* bandstate array indices */
#define BAND_2G_INDEX		0	/* wlc->bandstate[x] index */
#define BAND_5G_INDEX		1	/* wlc->bandstate[x] index */

#define BAND_2G_NAME		"2.4G"
#define BAND_5G_NAME		"5G"

/* BMAC RPC: 7 u32 params: pkttotlen, fifo, commit, fid, txpktpend, pktflag, rpc_id */
#define BRCMS_RPCTX_PARAMS		32

#endif				/* _BRCM_PUB_H_ */

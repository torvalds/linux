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

#ifndef _wlc_pub_h_
#define _wlc_pub_h_

#include <wlc_types.h>
#include <wlc_scb.h>

#define	WLC_NUMRATES	16	/* max # of rates in a rateset */
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
#define WLC_10_MHZ	10	/* 10Mhz nphy channel bandwidth */
#define WLC_20_MHZ	20	/* 20Mhz nphy channel bandwidth */
#define WLC_40_MHZ	40	/* 40Mhz nphy channel bandwidth */

#define CHSPEC_WLC_BW(chanspec)	(CHSPEC_IS40(chanspec) ? WLC_40_MHZ : \
				 CHSPEC_IS20(chanspec) ? WLC_20_MHZ : \
							 WLC_10_MHZ)

#define	WLC_RSSI_MINVAL		-200	/* Low value, e.g. for forcing roam */
#define	WLC_RSSI_NO_SIGNAL	-91	/* NDIS RSSI link quality cutoffs */
#define	WLC_RSSI_VERY_LOW	-80	/* Very low quality cutoffs */
#define	WLC_RSSI_LOW		-70	/* Low quality cutoffs */
#define	WLC_RSSI_GOOD		-68	/* Good quality cutoffs */
#define	WLC_RSSI_VERY_GOOD	-58	/* Very good quality cutoffs */
#define	WLC_RSSI_EXCELLENT	-57	/* Excellent quality cutoffs */

#define WLC_PHYTYPE(_x) (_x)	/* macro to perform WLC PHY -> D11 PHY TYPE, currently 1:1 */

#define MA_WINDOW_SZ		8	/* moving average window size */

#define WLC_SNR_INVALID		0	/* invalid SNR value */

/* a large TX Power as an init value to factor out of MIN() calculations,
 * keep low enough to fit in an int8, units are .25 dBm
 */
#define WLC_TXPWR_MAX		(127)	/* ~32 dBm = 1,500 mW */

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
#define AIDMAPSZ	(ROUNDUP(MAXSCB, NBBY)/NBBY)	/* aid bitmap size in bytes */
#endif				/* AIDMAPSZ */

typedef struct wlc_tunables {
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
} wlc_tunables_t;

typedef struct wlc_rateset {
	uint count;		/* number of rates in rates[] */
	uint8 rates[WLC_NUMRATES];	/* rates in 500kbps units w/hi bit set if basic */
	uint8 htphy_membership;	/* HT PHY Membership */
	uint8 mcs[MCSSET_LEN];	/* supported mcs index bit map */
} wlc_rateset_t;

struct rsn_parms {
	uint8 flags;		/* misc booleans (e.g., supported) */
	uint8 multicast;	/* multicast cipher */
	uint8 ucount;		/* count of unicast ciphers */
	uint8 unicast[4];	/* unicast ciphers */
	uint8 acount;		/* count of auth modes */
	uint8 auth[4];		/* Authentication modes */
	uint8 PAD[4];		/* padding for future growth */
};

/*
 * buffer length needed for wlc_format_ssid
 * 32 SSID chars, max of 4 chars for each SSID char "\xFF", plus NULL.
 */
#define SSID_FMT_BUF_LEN	((4 * DOT11_MAX_SSID_LEN) + 1)

#define RSN_FLAGS_SUPPORTED		0x1	/* Flag for rsn_params */
#define RSN_FLAGS_PREAUTH		0x2	/* Flag for WPA2 rsn_params */

/* All the HT-specific default advertised capabilities (including AMPDU)
 * should be grouped here at one place
 */
#define AMPDU_DEF_MPDU_DENSITY	6	/* default mpdu density (110 ==> 4us) */

/* defaults for the HT (MIMO) bss */
#define HT_CAP	((HT_CAP_MIMO_PS_OFF << HT_CAP_MIMO_PS_SHIFT) | HT_CAP_40MHZ | \
	HT_CAP_GF | HT_CAP_MAX_AMSDU | HT_CAP_DSSS_CCK)

/* WLC packet type is a void * */
typedef void *wlc_pkt_t;

/* Event data type */
typedef struct wlc_event {
	wl_event_msg_t event;	/* encapsulated event */
	struct ether_addr *addr;	/* used to keep a trace of the potential present of
					 * an address in wlc_event_msg_t
					 */
	int bsscfgidx;		/* BSS config when needed */
	struct wl_if *wlif;	/* pointer to wlif */
	void *data;		/* used to hang additional data on an event */
	struct wlc_event *next;	/* enables ordered list of pending events */
} wlc_event_t;

/* wlc internal bss_info, wl external one is in wlioctl.h */
typedef struct wlc_bss_info {
	struct ether_addr BSSID;	/* network BSSID */
	uint16 flags;		/* flags for internal attributes */
	uint8 SSID_len;		/* the length of SSID */
	uint8 SSID[32];		/* SSID string */
	int16 RSSI;		/* receive signal strength (in dBm) */
	int16 SNR;		/* receive signal SNR in dB */
	uint16 beacon_period;	/* units are Kusec */
	uint16 atim_window;	/* units are Kusec */
	chanspec_t chanspec;	/* Channel num, bw, ctrl_sb and band */
	int8 infra;		/* 0=IBSS, 1=infrastructure, 2=unknown */
	wlc_rateset_t rateset;	/* supported rates */
	uint8 dtim_period;	/* DTIM period */
	int8 phy_noise;		/* noise right after tx (in dBm) */
	uint16 capability;	/* Capability information */
	struct dot11_bcn_prb *bcn_prb;	/* beacon/probe response frame (ioctl na) */
	uint16 bcn_prb_len;	/* beacon/probe response frame length (ioctl na) */
	uint8 wme_qosinfo;	/* QoS Info from WME IE; valid if WLC_BSS_WME flag set */
	struct rsn_parms wpa;
	struct rsn_parms wpa2;
	uint16 qbss_load_aac;	/* qbss load available admission capacity */
	/* qbss_load_chan_free <- (0xff - channel_utilization of qbss_load_ie_t) */
	uint8 qbss_load_chan_free;	/* indicates how free the channel is */
	uint8 mcipher;		/* multicast cipher */
	uint8 wpacfg;		/* wpa config index */
} wlc_bss_info_t;

/* forward declarations */
struct wlc_if;

/* wlc_ioctl error codes */
#define WLC_ENOIOCTL	1	/* No such Ioctl */
#define WLC_EINVAL	2	/* Invalid value */
#define WLC_ETOOSMALL	3	/* Value too small */
#define WLC_ETOOBIG	4	/* Value too big */
#define WLC_ERANGE	5	/* Out of range */
#define WLC_EDOWN	6	/* Down */
#define WLC_EUP		7	/* Up */
#define WLC_ENOMEM	8	/* No Memory */
#define WLC_EBUSY	9	/* Busy */

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
typedef int (*dump_fn_t) (void *handle, struct bcmstrbuf *b);

/* IOVar handler
 *
 * handle - a pointer value registered with the function
 * vi - iovar_info that was looked up
 * actionid - action ID, calculated by IOV_GVAL() and IOV_SVAL() based on varid.
 * name - the actual iovar name
 * params/plen - parameters and length for a get, input only.
 * arg/len - buffer and length for value to be set or retrieved, input or output.
 * vsize - value size, valid for integer type only.
 * wlcif - interface context (wlc_if pointer)
 *
 * All pointers may point into the same buffer.
 */
typedef int (*iovar_fn_t) (void *handle, const bcm_iovar_t *vi,
			   uint32 actionid, const char *name, void *params,
			   uint plen, void *arg, int alen, int vsize,
			   struct wlc_if *wlcif);

#define MAC80211_PROMISC_BCNS	(1 << 0)
#define MAC80211_SCAN		(1 << 1)

/*
 * Public portion of "common" os-independent state structure.
 * The wlc handle points at this.
 */
typedef struct wlc_pub {
	void *wlc;

	struct ieee80211_hw *ieee_hw;
	struct scb *global_scb;
	scb_ampdu_t *global_ampdu;
	uint mac80211_state;
	uint unit;		/* device instance number */
	uint corerev;		/* core revision */
	osl_t *osh;		/* pointer to os handle */
	si_t *sih;		/* SB handle (cookie for siutils calls) */
	char *vars;		/* "environment" name=value */
	bool up;		/* interface up and running */
	bool hw_off;		/* HW is off */
	wlc_tunables_t *tunables;	/* tunables: ntxd, nrxd, maxscb, etc. */
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
	uint8 _mbss;		/* MBSS mode on */
	bool allmulti;		/* enable all multicasts */
	bool associated;	/* true:part of [I]BSS, false: not */
	/* (union of stas_associated, aps_associated) */
	bool phytest_on;	/* whether a PHY test is running */
	bool bf_preempt_4306;	/* True to enable 'darwin' mode */
	bool _ampdu;		/* ampdu enabled or not */
	bool _cac;		/* 802.11e CAC enabled */
	uint8 _n_enab;		/* bitmap of 11N + HT support */
	bool _n_reqd;		/* N support required for clients */

	int8 _coex;		/* 20/40 MHz BSS Management AUTO, ENAB, DISABLE */
	bool _priofc;		/* Priority-based flowcontrol */

	struct ether_addr cur_etheraddr;	/* our local ethernet address */

	struct ether_addr *multicast;	/* ptr to list of multicast addresses */
	uint nmulticast;	/* # enabled multicast addresses */

	uint32 wlfeatureflag;	/* Flags to control sw features from registry */
	int psq_pkts_total;	/* total num of ps pkts */

	uint16 txmaxpkts;	/* max number of large pkts allowed to be pending */

	/* s/w decryption counters */
	uint32 swdecrypt;	/* s/w decrypt attempts */

	int bcmerror;		/* last bcm error */

	mbool radio_disabled;	/* bit vector for radio disabled reasons */
	bool radio_active;	/* radio on/off state */
	uint16 roam_time_thresh;	/* Max. # secs. of not hearing beacons
					 * before roaming.
					 */
	bool align_wd_tbtt;	/* Align watchdog with tbtt indication
				 * handling. This flag is cleared by default
				 * and is set by per port code explicitly and
				 * you need to make sure the OSL_SYSUPTIME()
				 * is implemented properly in osl of that port
				 * when it enables this Power Save feature.
				 */
#ifdef BCMSDIO
	uint sdiod_drive_strength;	/* SDIO drive strength */
#endif				/* BCMSDIO */

	uint16 boardrev;	/* version # of particular board */
	uint8 sromrev;		/* version # of the srom */
	char srom_ccode[WLC_CNTRY_BUF_SZ];	/* Country Code in SROM */
	uint32 boardflags;	/* Board specific flags from srom */
	uint32 boardflags2;	/* More board flags if sromrev >= 4 */
	bool tempsense_disable;	/* disable periodic tempsense check */

	bool _lmac;		/* lmac module included and enabled */
	bool _lmacproto;	/* lmac protocol module included and enabled */
	bool phy_11ncapable;	/* the PHY/HW is capable of 802.11N */
	bool _ampdumac;		/* mac assist ampdu enabled or not */
} wlc_pub_t;

/* wl_monitor rx status per packet */
typedef struct wl_rxsts {
	uint pkterror;		/* error flags per pkt */
	uint phytype;		/* 802.11 A/B/G ... */
	uint channel;		/* channel */
	uint datarate;		/* rate in 500kbps */
	uint antenna;		/* antenna pkts received on */
	uint pktlength;		/* pkt length minus bcm phy hdr */
	uint32 mactime;		/* time stamp from mac, count per 1us */
	uint sq;		/* signal quality */
	int32 signal;		/* in dbm */
	int32 noise;		/* in dbm */
	uint preamble;		/* Unknown, short, long */
	uint encoding;		/* Unknown, CCK, PBCC, OFDM */
	uint nfrmtype;		/* special 802.11n frames(AMPDU, AMSDU) */
	struct wl_if *wlif;	/* wl interface */
} wl_rxsts_t;

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

/* forward declare and use the struct notation so we don't have to
 * have it defined if not necessary.
 */
struct wlc_info;
struct wlc_hw_info;
struct wlc_bsscfg;
struct wlc_if;

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

#define MONITOR_ENAB(wlc)	(bcmspace && (wlc)->monitor)

#define PROMISC_ENAB(wlc)	(bcmspace && (wlc)->promisc)

extern void wlc_pkttag_info_move(wlc_pub_t *pub, void *pkt_from, void *pkt_to);

#define WLPKTTAGSCB(p) (WLPKTTAG(p)->_scb)

#define	WLC_PREC_COUNT		16	/* Max precedence level implemented */

/* pri is PKTPRIO encoded in the packet. This maps the Packet priority to
 * enqueue precedence as defined in wlc_prec_map
 */
extern const uint8 wlc_prio2prec_map[];
#define WLC_PRIO_TO_PREC(pri)	wlc_prio2prec_map[(pri) & 7]

/* This maps priority to one precedence higher - Used by PS-Poll response packets to
 * simulate enqueue-at-head operation, but still maintain the order on the queue
 */
#define WLC_PRIO_TO_HI_PREC(pri)	MIN(WLC_PRIO_TO_PREC(pri) + 1, WLC_PREC_COUNT - 1)

extern const uint8 wme_fifo2ac[];
#define WME_PRIO2AC(prio)	wme_fifo2ac[prio2fifo[(prio)]]

/* Mask to describe all precedence levels */
#define WLC_PREC_BMP_ALL		MAXBITVAL(WLC_PREC_COUNT)

/* Define a bitmap of precedences comprised by each AC */
#define WLC_PREC_BMP_AC_BE	(NBITVAL(WLC_PRIO_TO_PREC(PRIO_8021D_BE)) |	\
				NBITVAL(WLC_PRIO_TO_HI_PREC(PRIO_8021D_BE)) |	\
				NBITVAL(WLC_PRIO_TO_PREC(PRIO_8021D_EE)) |	\
				NBITVAL(WLC_PRIO_TO_HI_PREC(PRIO_8021D_EE)))
#define WLC_PREC_BMP_AC_BK	(NBITVAL(WLC_PRIO_TO_PREC(PRIO_8021D_BK)) |	\
				NBITVAL(WLC_PRIO_TO_HI_PREC(PRIO_8021D_BK)) |	\
				NBITVAL(WLC_PRIO_TO_PREC(PRIO_8021D_NONE)) |	\
				NBITVAL(WLC_PRIO_TO_HI_PREC(PRIO_8021D_NONE)))
#define WLC_PREC_BMP_AC_VI	(NBITVAL(WLC_PRIO_TO_PREC(PRIO_8021D_CL)) |	\
				NBITVAL(WLC_PRIO_TO_HI_PREC(PRIO_8021D_CL)) |	\
				NBITVAL(WLC_PRIO_TO_PREC(PRIO_8021D_VI)) |	\
				NBITVAL(WLC_PRIO_TO_HI_PREC(PRIO_8021D_VI)))
#define WLC_PREC_BMP_AC_VO	(NBITVAL(WLC_PRIO_TO_PREC(PRIO_8021D_VO)) |	\
				NBITVAL(WLC_PRIO_TO_HI_PREC(PRIO_8021D_VO)) |	\
				NBITVAL(WLC_PRIO_TO_PREC(PRIO_8021D_NC)) |	\
				NBITVAL(WLC_PRIO_TO_HI_PREC(PRIO_8021D_NC)))

/* WME Support */
#define WME_ENAB(pub) ((pub)->_wme != OFF)
#define WME_AUTO(wlc) ((wlc)->pub->_wme == AUTO)

#define WLC_USE_COREFLAGS	0xffffffff	/* invalid core flags, use the saved coreflags */

#define WLC_UPDATE_STATS(wlc)	0	/* No stats support */
#define WLCNTINCR(a)		/* No stats support */
#define WLCNTDECR(a)		/* No stats support */
#define WLCNTADD(a, delta)	/* No stats support */
#define WLCNTSET(a, value)	/* No stats support */
#define WLCNTVAL(a)		0	/* No stats support */

/* common functions for every port */
extern void *wlc_attach(void *wl, uint16 vendor, uint16 device, uint unit,
			bool piomode, osl_t *osh, void *regsva, uint bustype,
			void *btparam, uint *perr);
extern uint wlc_detach(struct wlc_info *wlc);
extern int wlc_up(struct wlc_info *wlc);
extern uint wlc_down(struct wlc_info *wlc);

extern int wlc_set(struct wlc_info *wlc, int cmd, int arg);
extern int wlc_get(struct wlc_info *wlc, int cmd, int *arg);
extern int wlc_iovar_getint(struct wlc_info *wlc, const char *name, int *arg);
extern int wlc_iovar_setint(struct wlc_info *wlc, const char *name, int arg);
extern bool wlc_chipmatch(uint16 vendor, uint16 device);
extern void wlc_init(struct wlc_info *wlc);
extern void wlc_reset(struct wlc_info *wlc);

extern void wlc_intrson(struct wlc_info *wlc);
extern uint32 wlc_intrsoff(struct wlc_info *wlc);
extern void wlc_intrsrestore(struct wlc_info *wlc, uint32 macintmask);
extern bool wlc_intrsupd(struct wlc_info *wlc);
extern bool wlc_isr(struct wlc_info *wlc, bool *wantdpc);
extern bool wlc_dpc(struct wlc_info *wlc, bool bounded);
extern bool wlc_send80211_raw(struct wlc_info *wlc, wlc_if_t *wlcif, void *p,
			      uint ac);
extern int wlc_iovar_op(struct wlc_info *wlc, const char *name, void *params,
			int p_len, void *arg, int len, bool set,
			struct wlc_if *wlcif);
extern int wlc_ioctl(struct wlc_info *wlc, int cmd, void *arg, int len,
		     struct wlc_if *wlcif);
/* helper functions */
extern void wlc_statsupd(struct wlc_info *wlc);
extern int wlc_get_header_len(void);

extern wlc_pub_t *wlc_pub(void *wlc);

/* common functions for every port */
extern int wlc_bmac_up_prep(struct wlc_hw_info *wlc_hw);
extern int wlc_bmac_up_finish(struct wlc_hw_info *wlc_hw);
extern int wlc_bmac_down_prep(struct wlc_hw_info *wlc_hw);
extern int wlc_bmac_down_finish(struct wlc_hw_info *wlc_hw);

extern uint32 wlc_reg_read(struct wlc_info *wlc, void *r, uint size);
extern void wlc_reg_write(struct wlc_info *wlc, void *r, uint32 v, uint size);
extern void wlc_corereset(struct wlc_info *wlc, uint32 flags);
extern void wlc_mhf(struct wlc_info *wlc, uint8 idx, uint16 mask, uint16 val,
		    int bands);
extern uint16 wlc_mhf_get(struct wlc_info *wlc, uint8 idx, int bands);
extern uint32 wlc_delta_txfunfl(struct wlc_info *wlc, int fifo);
extern void wlc_rate_lookup_init(struct wlc_info *wlc, wlc_rateset_t *rateset);
extern void wlc_default_rateset(struct wlc_info *wlc, wlc_rateset_t *rs);

/* wlc_phy.c helper functions */
extern void wlc_set_ps_ctrl(struct wlc_info *wlc);
extern void wlc_mctrl(struct wlc_info *wlc, uint32 mask, uint32 val);
extern void wlc_scb_ratesel_init_all(struct wlc_info *wlc);

/* ioctl */
extern int wlc_iovar_getint8(struct wlc_info *wlc, const char *name,
			     int8 *arg);
extern int wlc_iovar_check(wlc_pub_t *pub, const bcm_iovar_t *vi, void *arg,
			   int len, bool set);

extern int wlc_module_register(wlc_pub_t *pub, const bcm_iovar_t *iovars,
			       const char *name, void *hdl, iovar_fn_t iovar_fn,
			       watchdog_fn_t watchdog_fn, down_fn_t down_fn);
extern int wlc_module_unregister(wlc_pub_t *pub, const char *name, void *hdl);
extern void wlc_event_if(struct wlc_info *wlc, struct wlc_bsscfg *cfg,
			 wlc_event_t *e, const struct ether_addr *addr);
extern void wlc_suspend_mac_and_wait(struct wlc_info *wlc);
extern void wlc_enable_mac(struct wlc_info *wlc);
extern uint16 wlc_rate_shm_offset(struct wlc_info *wlc, uint8 rate);
extern uint32 wlc_get_rspec_history(struct wlc_bsscfg *cfg);
extern uint32 wlc_get_current_highest_rate(struct wlc_bsscfg *cfg);

static INLINE int wlc_iovar_getuint(struct wlc_info *wlc, const char *name,
				    uint *arg)
{
	return wlc_iovar_getint(wlc, name, (int *)arg);
}

static INLINE int wlc_iovar_getuint8(struct wlc_info *wlc, const char *name,
				     uint8 *arg)
{
	return wlc_iovar_getint8(wlc, name, (int8 *) arg);
}

static INLINE int wlc_iovar_setuint(struct wlc_info *wlc, const char *name,
				    uint arg)
{
	return wlc_iovar_setint(wlc, name, (int)arg);
}

#if defined(BCMDBG)
extern int wlc_iocregchk(struct wlc_info *wlc, uint band);
#endif
#if defined(BCMDBG)
extern int wlc_iocpichk(struct wlc_info *wlc, uint phytype);
#endif

/* helper functions */
extern void wlc_getrand(struct wlc_info *wlc, uint8 *buf, int len);

struct scb;
extern void wlc_ps_on(struct wlc_info *wlc, struct scb *scb);
extern void wlc_ps_off(struct wlc_info *wlc, struct scb *scb, bool discard);
extern bool wlc_radio_monitor_stop(struct wlc_info *wlc);

#if defined(BCMDBG)
extern int wlc_format_ssid(char *buf, const uchar ssid[], uint ssid_len);
#endif

extern void wlc_pmkid_build_cand_list(struct wlc_bsscfg *cfg, bool check_SSID);
extern void wlc_pmkid_event(struct wlc_bsscfg *cfg);

#define	MAXBANDS		2	/* Maximum #of bands */
/* bandstate array indices */
#define BAND_2G_INDEX		0	/* wlc->bandstate[x] index */
#define BAND_5G_INDEX		1	/* wlc->bandstate[x] index */

#define BAND_2G_NAME		"2.4G"
#define BAND_5G_NAME		"5G"

#if defined(BCMSDIO) || defined(WLC_HIGH_ONLY)
void wlc_device_removed(void *arg);
#endif

/* BMAC RPC: 7 uint32 params: pkttotlen, fifo, commit, fid, txpktpend, pktflag, rpc_id */
#define WLC_RPCTX_PARAMS		32

#endif				/* _wlc_pub_h_ */

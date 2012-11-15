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

#include <linux/bcma/bcma.h>
#include <brcmu_wifi.h>
#include "types.h"
#include "defs.h"

#define	BRCMS_NUMRATES	16	/* max # of rates in a rateset */

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
brcms_c_attach(struct brcms_info *wl, struct bcma_device *core, uint unit,
	       bool piomode, uint *perr);
extern uint brcms_c_detach(struct brcms_c_info *wlc);
extern int brcms_c_up(struct brcms_c_info *wlc);
extern uint brcms_c_down(struct brcms_c_info *wlc);

extern bool brcms_c_chipmatch(struct bcma_device *core);
extern void brcms_c_init(struct brcms_c_info *wlc, bool mute_tx);
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
extern bool brcms_c_check_radio_disabled(struct brcms_c_info *wlc);
extern void brcms_c_mute(struct brcms_c_info *wlc, bool on);

#endif				/* _BRCM_PUB_H_ */

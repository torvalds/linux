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

#ifndef _WLC_BSSCFG_H_
#define _WLC_BSSCFG_H_

#include <wlc_types.h>

/* Check if a particular BSS config is AP or STA */
#define BSSCFG_AP(cfg)		(0)
#define BSSCFG_STA(cfg)		(1)

#define BSSCFG_IBSS(cfg)	(!(cfg)->BSS)

/* forward declarations */
typedef struct wlc_bsscfg wlc_bsscfg_t;

#include <wlc_rate.h>

#define NTXRATE			64	/* # tx MPDUs rate is reported for */
#define MAXMACLIST		64	/* max # source MAC matches */
#define BCN_TEMPLATE_COUNT 	2

/* Iterator for "associated" STA bss configs:  (wlc_info_t *wlc, int idx, wlc_bsscfg_t *cfg) */
#define FOREACH_AS_STA(wlc, idx, cfg) \
	for (idx = 0; (int) idx < WLC_MAXBSSCFG; idx++) \
		if ((cfg = (wlc)->bsscfg[idx]) && BSSCFG_STA(cfg) && cfg->associated)

/* As above for all non-NULL BSS configs */
#define FOREACH_BSS(wlc, idx, cfg) \
	for (idx = 0; (int) idx < WLC_MAXBSSCFG; idx++) \
		if ((cfg = (wlc)->bsscfg[idx]))

/* BSS configuration state */
struct wlc_bsscfg {
	struct wlc_info *wlc;	/* wlc to which this bsscfg belongs to. */
	bool up;		/* is this configuration up operational */
	bool enable;		/* is this configuration enabled */
	bool associated;	/* is BSS in ASSOCIATED state */
	bool BSS;		/* infraustructure or adhac */
	bool dtim_programmed;
#ifdef LATER
	bool _ap;		/* is this configuration an AP */
	struct wlc_if *wlcif;	/* virtual interface, NULL for primary bsscfg */
	void *sup;		/* pointer to supplicant state */
	s8 sup_type;		/* type of supplicant */
	bool sup_enable_wpa;	/* supplicant WPA on/off */
	void *authenticator;	/* pointer to authenticator state */
	bool sup_auth_pending;	/* flag for auth timeout */
#endif
	u8 SSID_len;		/* the length of SSID */
	u8 SSID[DOT11_MAX_SSID_LEN];	/* SSID string */
	struct scb *bcmc_scb[MAXBANDS];	/* one bcmc_scb per band */
	s8 _idx;		/* the index of this bsscfg,
				 * assigned at wlc_bsscfg_alloc()
				 */
	/* MAC filter */
	uint nmac;		/* # of entries on maclist array */
	int macmode;		/* allow/deny stations on maclist array */
	struct ether_addr *maclist;	/* list of source MAC addrs to match */

	/* security */
	u32 wsec;		/* wireless security bitvec */
	s16 auth;		/* 802.11 authentication: Open, Shared Key, WPA */
	s16 openshared;	/* try Open auth first, then Shared Key */
	bool wsec_restrict;	/* drop unencrypted packets if wsec is enabled */
	bool eap_restrict;	/* restrict data until 802.1X auth succeeds */
	u16 WPA_auth;	/* WPA: authenticated key management */
	bool wpa2_preauth;	/* default is true, wpa_cap sets value */
	bool wsec_portopen;	/* indicates keys are plumbed */
	wsec_iv_t wpa_none_txiv;	/* global txiv for WPA_NONE, tkip and aes */
	int wsec_index;		/* 0-3: default tx key, -1: not set */
	wsec_key_t *bss_def_keys[WLC_DEFAULT_KEYS];	/* default key storage */

	/* TKIP countermeasures */
	bool tkip_countermeasures;	/* flags TKIP no-assoc period */
	u32 tk_cm_dt;	/* detect timer */
	u32 tk_cm_bt;	/* blocking timer */
	u32 tk_cm_bt_tmstmp;	/* Timestamp when TKIP BT is activated */
	bool tk_cm_activate;	/* activate countermeasures after EAPOL-Key sent */

	struct ether_addr BSSID;	/* BSSID (associated) */
	struct ether_addr cur_etheraddr;	/* h/w address */
	u16 bcmc_fid;	/* the last BCMC FID queued to TX_BCMC_FIFO */
	u16 bcmc_fid_shm;	/* the last BCMC FID written to shared mem */

	u32 flags;		/* WLC_BSSCFG flags; see below */

	u8 *bcn;		/* AP beacon */
	uint bcn_len;		/* AP beacon length */
	bool ar_disassoc;	/* disassociated in associated recreation */

	int auth_atmptd;	/* auth type (open/shared) attempted */

	pmkid_cand_t pmkid_cand[MAXPMKID];	/* PMKID candidate list */
	uint npmkid_cand;	/* num PMKID candidates */
	pmkid_t pmkid[MAXPMKID];	/* PMKID cache */
	uint npmkid;		/* num cached PMKIDs */

	wlc_bss_info_t *target_bss;	/* BSS parms during tran. to ASSOCIATED state */
	wlc_bss_info_t *current_bss;	/* BSS parms in ASSOCIATED state */

	/* PM states */
	bool PMawakebcn;	/* bcn recvd during current waking state */
	bool PMpending;		/* waiting for tx status with PM indicated set */
	bool priorPMstate;	/* Detecting PM state transitions */
	bool PSpoll;		/* whether there is an outstanding PS-Poll frame */

	/* BSSID entry in RCMTA, use the wsec key management infrastructure to
	 * manage the RCMTA entries.
	 */
	wsec_key_t *rcmta;

	/* 'unique' ID of this bsscfg, assigned at bsscfg allocation */
	u16 ID;

	uint txrspecidx;	/* index into tx rate circular buffer */
	ratespec_t txrspec[NTXRATE][2];	/* circular buffer of prev MPDUs tx rates */
};

#define WLC_BSSCFG_11N_DISABLE	0x1000	/* Do not advertise .11n IEs for this BSS */
#define WLC_BSSCFG_HW_BCN	0x20	/* The BSS is generating beacons in HW */

#define HWBCN_ENAB(cfg)		(((cfg)->flags & WLC_BSSCFG_HW_BCN) != 0)
#define HWPRB_ENAB(cfg)		(((cfg)->flags & WLC_BSSCFG_HW_PRB) != 0)

extern void wlc_bsscfg_ID_assign(struct wlc_info *wlc, wlc_bsscfg_t * bsscfg);

/* Extend N_ENAB to per-BSS */
#define BSS_N_ENAB(wlc, cfg) \
	(N_ENAB((wlc)->pub) && !((cfg)->flags & WLC_BSSCFG_11N_DISABLE))

#define MBSS_BCN_ENAB(cfg)       0
#define MBSS_PRB_ENAB(cfg)       0
#define SOFTBCN_ENAB(pub)    (0)
#define SOFTPRB_ENAB(pub)    (0)
#define wlc_bsscfg_tx_check(a) do { } while (0);

#endif				/* _WLC_BSSCFG_H_ */

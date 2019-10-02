// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2014 Broadcom Corporation
 */
#ifndef _BRCMF_FEATURE_H
#define _BRCMF_FEATURE_H

/*
 * Features:
 *
 * MBSS: multiple BSSID support (eg. guest network in AP mode).
 * MCHAN: multi-channel for concurrent P2P.
 * PNO: preferred network offload.
 * WOWL: Wake-On-WLAN.
 * P2P: peer-to-peer
 * RSDB: Real Simultaneous Dual Band
 * TDLS: Tunneled Direct Link Setup
 * SCAN_RANDOM_MAC: Random MAC during (net detect) scheduled scan.
 * WOWL_ND: WOWL net detect (PNO)
 * WOWL_GTK: (WOWL) GTK rekeying offload
 * WOWL_ARP_ND: ARP and Neighbor Discovery offload support during WOWL.
 * MFP: 802.11w Management Frame Protection.
 * GSCAN: enhanced scan offload feature.
 * FWSUP: Firmware supplicant.
 * MONITOR: firmware can pass monitor packets to host.
 * MONITOR_FMT_RADIOTAP: firmware provides monitor packets with radiotap header
 * MONITOR_FMT_HW_RX_HDR: firmware provides monitor packets with hw/ucode header
 * DOT11H: firmware supports 802.11h
 * SAE: simultaneous authentication of equals
 */
#define BRCMF_FEAT_LIST \
	BRCMF_FEAT_DEF(MBSS) \
	BRCMF_FEAT_DEF(MCHAN) \
	BRCMF_FEAT_DEF(PNO) \
	BRCMF_FEAT_DEF(WOWL) \
	BRCMF_FEAT_DEF(P2P) \
	BRCMF_FEAT_DEF(RSDB) \
	BRCMF_FEAT_DEF(TDLS) \
	BRCMF_FEAT_DEF(SCAN_RANDOM_MAC) \
	BRCMF_FEAT_DEF(WOWL_ND) \
	BRCMF_FEAT_DEF(WOWL_GTK) \
	BRCMF_FEAT_DEF(WOWL_ARP_ND) \
	BRCMF_FEAT_DEF(MFP) \
	BRCMF_FEAT_DEF(GSCAN) \
	BRCMF_FEAT_DEF(FWSUP) \
	BRCMF_FEAT_DEF(MONITOR) \
	BRCMF_FEAT_DEF(MONITOR_FMT_RADIOTAP) \
	BRCMF_FEAT_DEF(MONITOR_FMT_HW_RX_HDR) \
	BRCMF_FEAT_DEF(DOT11H) \
	BRCMF_FEAT_DEF(SAE)

/*
 * Quirks:
 *
 * AUTO_AUTH: workaround needed for automatic authentication type.
 * NEED_MPC: driver needs to disable MPC during scanning operation.
 */
#define BRCMF_QUIRK_LIST \
	BRCMF_QUIRK_DEF(AUTO_AUTH) \
	BRCMF_QUIRK_DEF(NEED_MPC)

#define BRCMF_FEAT_DEF(_f) \
	BRCMF_FEAT_ ## _f,
/*
 * expand feature list to enumeration.
 */
enum brcmf_feat_id {
	BRCMF_FEAT_LIST
	BRCMF_FEAT_LAST
};
#undef BRCMF_FEAT_DEF

#define BRCMF_QUIRK_DEF(_q) \
	BRCMF_FEAT_QUIRK_ ## _q,
/*
 * expand quirk list to enumeration.
 */
enum brcmf_feat_quirk {
	BRCMF_QUIRK_LIST
	BRCMF_FEAT_QUIRK_LAST
};
#undef BRCMF_QUIRK_DEF

/**
 * brcmf_feat_attach() - determine features and quirks.
 *
 * @drvr: driver instance.
 */
void brcmf_feat_attach(struct brcmf_pub *drvr);

/**
 * brcmf_feat_debugfs_create() - create debugfs entries.
 *
 * @drvr: driver instance.
 */
void brcmf_feat_debugfs_create(struct brcmf_pub *drvr);

/**
 * brcmf_feat_is_enabled() - query feature.
 *
 * @ifp: interface instance.
 * @id: feature id to check.
 *
 * Return: true is feature is enabled; otherwise false.
 */
bool brcmf_feat_is_enabled(struct brcmf_if *ifp, enum brcmf_feat_id id);

/**
 * brcmf_feat_is_quirk_enabled() - query chip quirk.
 *
 * @ifp: interface instance.
 * @quirk: quirk id to check.
 *
 * Return: true is quirk is enabled; otherwise false.
 */
bool brcmf_feat_is_quirk_enabled(struct brcmf_if *ifp,
				 enum brcmf_feat_quirk quirk);

#endif /* _BRCMF_FEATURE_H */

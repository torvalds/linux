// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2016 Broadcom
 */
#ifndef _BRCMF_PNO_H
#define _BRCMF_PNO_H

#define BRCMF_PNO_SCAN_COMPLETE			1
#define BRCMF_PNO_MAX_PFN_COUNT			16
#define BRCMF_PNO_SCHED_SCAN_MIN_PERIOD	10
#define BRCMF_PNO_SCHED_SCAN_MAX_PERIOD	508

/* forward declaration */
struct brcmf_pno_info;

/**
 * brcmf_pno_start_sched_scan - initiate scheduled scan on device.
 *
 * @ifp: interface object used.
 * @req: configuration parameters for scheduled scan.
 */
int brcmf_pno_start_sched_scan(struct brcmf_if *ifp,
			       struct cfg80211_sched_scan_request *req);

/**
 * brcmf_pno_stop_sched_scan - terminate scheduled scan on device.
 *
 * @ifp: interface object used.
 * @reqid: unique identifier of scan to be stopped.
 */
int brcmf_pno_stop_sched_scan(struct brcmf_if *ifp, u64 reqid);

/**
 * brcmf_pno_wiphy_params - fill scheduled scan parameters in wiphy instance.
 *
 * @wiphy: wiphy instance to be used.
 * @gscan: indicates whether the device has support for g-scan feature.
 */
void brcmf_pno_wiphy_params(struct wiphy *wiphy, bool gscan);

/**
 * brcmf_pno_attach - allocate and attach module information.
 *
 * @cfg: cfg80211 context used.
 */
int brcmf_pno_attach(struct brcmf_cfg80211_info *cfg);

/**
 * brcmf_pno_detach - detach and free module information.
 *
 * @cfg: cfg80211 context used.
 */
void brcmf_pno_detach(struct brcmf_cfg80211_info *cfg);

/**
 * brcmf_pno_find_reqid_by_bucket - find request id for given bucket index.
 *
 * @pi: pno instance used.
 * @bucket: index of firmware bucket.
 */
u64 brcmf_pno_find_reqid_by_bucket(struct brcmf_pno_info *pi, u32 bucket);

/**
 * brcmf_pno_get_bucket_map - determine bucket map for given netinfo.
 *
 * @pi: pno instance used.
 * @netinfo: netinfo to compare with bucket configuration.
 */
u32 brcmf_pno_get_bucket_map(struct brcmf_pno_info *pi,
			     struct brcmf_pno_net_info_le *netinfo);

#endif /* _BRCMF_PNO_H */

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
struct brcmf_pyes_info;

/**
 * brcmf_pyes_start_sched_scan - initiate scheduled scan on device.
 *
 * @ifp: interface object used.
 * @req: configuration parameters for scheduled scan.
 */
int brcmf_pyes_start_sched_scan(struct brcmf_if *ifp,
			       struct cfg80211_sched_scan_request *req);

/**
 * brcmf_pyes_stop_sched_scan - terminate scheduled scan on device.
 *
 * @ifp: interface object used.
 * @reqid: unique identifier of scan to be stopped.
 */
int brcmf_pyes_stop_sched_scan(struct brcmf_if *ifp, u64 reqid);

/**
 * brcmf_pyes_wiphy_params - fill scheduled scan parameters in wiphy instance.
 *
 * @wiphy: wiphy instance to be used.
 * @gscan: indicates whether the device has support for g-scan feature.
 */
void brcmf_pyes_wiphy_params(struct wiphy *wiphy, bool gscan);

/**
 * brcmf_pyes_attach - allocate and attach module information.
 *
 * @cfg: cfg80211 context used.
 */
int brcmf_pyes_attach(struct brcmf_cfg80211_info *cfg);

/**
 * brcmf_pyes_detach - detach and free module information.
 *
 * @cfg: cfg80211 context used.
 */
void brcmf_pyes_detach(struct brcmf_cfg80211_info *cfg);

/**
 * brcmf_pyes_find_reqid_by_bucket - find request id for given bucket index.
 *
 * @pi: pyes instance used.
 * @bucket: index of firmware bucket.
 */
u64 brcmf_pyes_find_reqid_by_bucket(struct brcmf_pyes_info *pi, u32 bucket);

/**
 * brcmf_pyes_get_bucket_map - determine bucket map for given netinfo.
 *
 * @pi: pyes instance used.
 * @netinfo: netinfo to compare with bucket configuration.
 */
u32 brcmf_pyes_get_bucket_map(struct brcmf_pyes_info *pi,
			     struct brcmf_pyes_net_info_le *netinfo);

#endif /* _BRCMF_PNO_H */

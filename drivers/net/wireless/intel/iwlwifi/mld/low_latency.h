/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024 Intel Corporation
 */
#ifndef __iwl_mld_low_latency_h__
#define __iwl_mld_low_latency_h__

/**
 * struct iwl_mld_low_latency_packets_counters - Packets counters
 * @lock: synchronize the counting in data path against the worker
 * @vo_vi: per-mac, counts the number of TX and RX voice and video packets
 */
struct iwl_mld_low_latency_packets_counters {
	spinlock_t lock;
	u32 vo_vi[NUM_MAC_INDEX_DRIVER];
} ____cacheline_aligned_in_smp;

/**
 * enum iwl_mld_low_latency_cause - low-latency set causes
 *
 * @LOW_LATENCY_TRAFFIC: indicates low-latency traffic was detected
 * @LOW_LATENCY_DEBUGFS: low-latency mode set from debugfs
 * @LOW_LATENCY_VIF_TYPE: low-latency mode set because of vif type (AP)
 */
enum iwl_mld_low_latency_cause {
	LOW_LATENCY_TRAFFIC	= BIT(0),
	LOW_LATENCY_DEBUGFS	= BIT(1),
	LOW_LATENCY_VIF_TYPE	= BIT(2),
};

/**
 * struct iwl_mld_low_latency - Manage low-latency detection and activation.
 * @work: this work is used to detect low-latency by monitoring the number of
 *	voice and video packets transmitted in a period of time. If the
 *	threshold is reached, low-latency is activated. When active,
 *	it is deactivated if the threshold is not reached within a
 *	10-second period.
 * @timestamp: timestamp of the last update.
 * @window_start: per-mac, timestamp of the start of the current window. when
 *	the window is over, the counters are reset.
 * @pkts_counters: per-queue array voice/video packet counters
 * @result: per-mac latest low-latency result
 * @stopped: if true, ignore the requests to update the counters
 */
struct iwl_mld_low_latency {
	struct wiphy_delayed_work work;
	unsigned long timestamp;
	unsigned long window_start[NUM_MAC_INDEX_DRIVER];
	struct iwl_mld_low_latency_packets_counters *pkts_counters;
	bool result[NUM_MAC_INDEX_DRIVER];
	bool stopped;
};

int iwl_mld_low_latency_init(struct iwl_mld *mld);
void iwl_mld_low_latency_free(struct iwl_mld *mld);
void iwl_mld_low_latency_restart_cleanup(struct iwl_mld *mld);
void iwl_mld_vif_update_low_latency(struct iwl_mld *mld,
				    struct ieee80211_vif *vif,
				    bool low_latency,
				    enum iwl_mld_low_latency_cause cause);
void iwl_mld_low_latency_update_counters(struct iwl_mld *mld,
					 struct ieee80211_hdr *hdr,
					 struct ieee80211_sta *sta,
					 u8 queue);
void iwl_mld_low_latency_stop(struct iwl_mld *mld);
void iwl_mld_low_latency_restart(struct iwl_mld *mld);

#endif /* __iwl_mld_low_latency_h__ */

/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021, Intel Corporation. */

#ifndef _ICE_PTP_H_
#define _ICE_PTP_H_

#include <linux/ptp_clock_kernel.h>
#include <linux/kthread.h>

#include "ice_ptp_hw.h"

/**
 * struct ice_ptp - data used for integrating with CONFIG_PTP_1588_CLOCK
 * @work: delayed work function for periodic tasks
 * @cached_phc_time: a cached copy of the PHC time for timestamp extension
 * @kworker: kwork thread for handling periodic work
 * @info: structure defining PTP hardware capabilities
 * @clock: pointer to registered PTP clock device
 * @tstamp_config: hardware timestamping configuration
 */
struct ice_ptp {
	struct kthread_delayed_work work;
	u64 cached_phc_time;
	struct kthread_worker *kworker;
	struct ptp_clock_info info;
	struct ptp_clock *clock;
	struct hwtstamp_config tstamp_config;
};

#define __ptp_info_to_ptp(i) \
	container_of((i), struct ice_ptp, info)
#define ptp_info_to_pf(i) \
	container_of(__ptp_info_to_ptp((i)), struct ice_pf, ptp)

#define PTP_SHARED_CLK_IDX_VALID	BIT(31)
#define ICE_PTP_TS_VALID		BIT(0)

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)
struct ice_pf;
int ice_ptp_set_ts_config(struct ice_pf *pf, struct ifreq *ifr);
int ice_ptp_get_ts_config(struct ice_pf *pf, struct ifreq *ifr);
int ice_get_ptp_clock_index(struct ice_pf *pf);
void
ice_ptp_rx_hwtstamp(struct ice_ring *rx_ring,
		    union ice_32b_rx_flex_desc *rx_desc, struct sk_buff *skb);
void ice_ptp_init(struct ice_pf *pf);
void ice_ptp_release(struct ice_pf *pf);
#else /* IS_ENABLED(CONFIG_PTP_1588_CLOCK) */
static inline int ice_ptp_set_ts_config(struct ice_pf *pf, struct ifreq *ifr)
{
	return -EOPNOTSUPP;
}

static inline int ice_ptp_get_ts_config(struct ice_pf *pf, struct ifreq *ifr)
{
	return -EOPNOTSUPP;
}

static inline int ice_get_ptp_clock_index(struct ice_pf *pf)
{
	return -1;
}

static inline void
ice_ptp_rx_hwtstamp(struct ice_ring *rx_ring,
		    union ice_32b_rx_flex_desc *rx_desc, struct sk_buff *skb) { }
static inline void ice_ptp_init(struct ice_pf *pf) { }
static inline void ice_ptp_release(struct ice_pf *pf) { }
#endif /* IS_ENABLED(CONFIG_PTP_1588_CLOCK) */
#endif /* _ICE_PTP_H_ */

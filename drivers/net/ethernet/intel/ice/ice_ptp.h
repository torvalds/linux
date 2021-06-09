/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021, Intel Corporation. */

#ifndef _ICE_PTP_H_
#define _ICE_PTP_H_

#include <linux/ptp_clock_kernel.h>

#include "ice_ptp_hw.h"

/**
 * struct ice_ptp - data used for integrating with CONFIG_PTP_1588_CLOCK
 * @info: structure defining PTP hardware capabilities
 * @clock: pointer to registered PTP clock device
 */
struct ice_ptp {
	struct ptp_clock_info info;
	struct ptp_clock *clock;
};

#define __ptp_info_to_ptp(i) \
	container_of((i), struct ice_ptp, info)
#define ptp_info_to_pf(i) \
	container_of(__ptp_info_to_ptp((i)), struct ice_pf, ptp)

#define PTP_SHARED_CLK_IDX_VALID	BIT(31)

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)
struct ice_pf;
int ice_get_ptp_clock_index(struct ice_pf *pf);
void ice_ptp_init(struct ice_pf *pf);
void ice_ptp_release(struct ice_pf *pf);
#else /* IS_ENABLED(CONFIG_PTP_1588_CLOCK) */
static inline int ice_get_ptp_clock_index(struct ice_pf *pf)
{
	return -1;
}
static inline void ice_ptp_init(struct ice_pf *pf) { }
static inline void ice_ptp_release(struct ice_pf *pf) { }
#endif /* IS_ENABLED(CONFIG_PTP_1588_CLOCK) */
#endif /* _ICE_PTP_H_ */

/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2024 Intel Corporation. */

#ifndef _IAVF_PTP_H_
#define _IAVF_PTP_H_

#include "iavf_types.h"

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)
void iavf_ptp_init(struct iavf_adapter *adapter);
void iavf_ptp_release(struct iavf_adapter *adapter);
void iavf_ptp_process_caps(struct iavf_adapter *adapter);
bool iavf_ptp_cap_supported(const struct iavf_adapter *adapter, u32 cap);
#else /* IS_ENABLED(CONFIG_PTP_1588_CLOCK) */
static inline void iavf_ptp_init(struct iavf_adapter *adapter) { }
static inline void iavf_ptp_release(struct iavf_adapter *adapter) { }
static inline void iavf_ptp_process_caps(struct iavf_adapter *adapter) { }
static inline bool iavf_ptp_cap_supported(struct iavf_adapter *adapter, u32 cap)
{
	return false;
}
#endif /* IS_ENABLED(CONFIG_PTP_1588_CLOCK) */
#endif /* _IAVF_PTP_H_ */

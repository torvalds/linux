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
void iavf_virtchnl_send_ptp_cmd(struct iavf_adapter *adapter);
int iavf_ptp_set_ts_config(struct iavf_adapter *adapter,
			   struct kernel_hwtstamp_config *config,
			   struct netlink_ext_ack *extack);
#else /* IS_ENABLED(CONFIG_PTP_1588_CLOCK) */
static inline void iavf_ptp_init(struct iavf_adapter *adapter) { }
static inline void iavf_ptp_release(struct iavf_adapter *adapter) { }
static inline void iavf_ptp_process_caps(struct iavf_adapter *adapter) { }
static inline bool iavf_ptp_cap_supported(const struct iavf_adapter *adapter,
					  u32 cap)
{
	return false;
}

static inline void iavf_virtchnl_send_ptp_cmd(struct iavf_adapter *adapter) { }
static inline int iavf_ptp_set_ts_config(struct iavf_adapter *adapter,
					 struct kernel_hwtstamp_config *config,
					 struct netlink_ext_ack *extack)
{
	return -1;
}
#endif /* IS_ENABLED(CONFIG_PTP_1588_CLOCK) */
#endif /* _IAVF_PTP_H_ */

/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qede NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#ifndef _QEDE_PTP_H_
#define _QEDE_PTP_H_

#include <linux/ptp_clock_kernel.h>
#include <linux/net_tstamp.h>
#include <linux/timecounter.h>
#include "qede.h"

void qede_ptp_rx_ts(struct qede_dev *edev, struct sk_buff *skb);
void qede_ptp_tx_ts(struct qede_dev *edev, struct sk_buff *skb);
int qede_ptp_hw_ts(struct qede_dev *edev, struct ifreq *req);
void qede_ptp_disable(struct qede_dev *edev);
int qede_ptp_enable(struct qede_dev *edev);
int qede_ptp_get_ts_info(struct qede_dev *edev, struct ethtool_ts_info *ts);

static inline void qede_ptp_record_rx_ts(struct qede_dev *edev,
					 union eth_rx_cqe *cqe,
					 struct sk_buff *skb)
{
	/* Check if this packet was timestamped */
	if (unlikely(le16_to_cpu(cqe->fast_path_regular.pars_flags.flags) &
		     (1 << PARSING_AND_ERR_FLAGS_TIMESTAMPRECORDED_SHIFT))) {
		if (likely(le16_to_cpu(cqe->fast_path_regular.pars_flags.flags)
		    & (1 << PARSING_AND_ERR_FLAGS_TIMESYNCPKT_SHIFT))) {
			qede_ptp_rx_ts(edev, skb);
		} else {
			DP_INFO(edev,
				"Timestamp recorded for non PTP packets\n");
		}
	}
}
#endif /* _QEDE_PTP_H_ */

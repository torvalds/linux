/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 - 2025 Beijing WangXun Technology Co., Ltd. */

#ifndef _WX_PTP_H_
#define _WX_PTP_H_

void wx_ptp_check_pps_event(struct wx *wx);
void wx_ptp_reset_cyclecounter(struct wx *wx);
void wx_ptp_reset(struct wx *wx);
void wx_ptp_init(struct wx *wx);
void wx_ptp_suspend(struct wx *wx);
void wx_ptp_stop(struct wx *wx);
void wx_ptp_rx_hwtstamp(struct wx *wx, struct sk_buff *skb);
int wx_hwtstamp_get(struct net_device *dev,
		    struct kernel_hwtstamp_config *cfg);
int wx_hwtstamp_set(struct net_device *dev,
		    struct kernel_hwtstamp_config *cfg,
		    struct netlink_ext_ack *extack);

#endif /* _WX_PTP_H_ */

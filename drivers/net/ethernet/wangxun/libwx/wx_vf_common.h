/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2025 Beijing WangXun Technology Co., Ltd. */

#ifndef _WX_VF_COMMON_H_
#define _WX_VF_COMMON_H_

int wx_request_msix_irqs_vf(struct wx *wx);
void wx_negotiate_api_vf(struct wx *wx);
void wx_reset_vf(struct wx *wx);
void wx_set_rx_mode_vf(struct net_device *netdev);
void wx_configure_vf(struct wx *wx);
int wx_set_mac_vf(struct net_device *netdev, void *p);

#endif /* _WX_VF_COMMON_H_ */

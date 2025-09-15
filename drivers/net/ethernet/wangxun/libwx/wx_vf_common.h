/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2025 Beijing WangXun Technology Co., Ltd. */

#ifndef _WX_VF_COMMON_H_
#define _WX_VF_COMMON_H_

int wxvf_suspend(struct device *dev_d);
void wxvf_shutdown(struct pci_dev *pdev);
int wxvf_resume(struct device *dev_d);
void wxvf_remove(struct pci_dev *pdev);
int wx_request_msix_irqs_vf(struct wx *wx);
void wx_negotiate_api_vf(struct wx *wx);
void wx_reset_vf(struct wx *wx);
void wx_set_rx_mode_vf(struct net_device *netdev);
void wx_configure_vf(struct wx *wx);
int wx_set_mac_vf(struct net_device *netdev, void *p);
void wxvf_watchdog_update_link(struct wx *wx);
int wxvf_open(struct net_device *netdev);
int wxvf_close(struct net_device *netdev);
void wxvf_init_service(struct wx *wx);

#endif /* _WX_VF_COMMON_H_ */

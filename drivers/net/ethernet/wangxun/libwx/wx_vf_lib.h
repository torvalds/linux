/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2015 - 2025 Beijing WangXun Technology Co., Ltd. */

#ifndef _WX_VF_LIB_H_
#define _WX_VF_LIB_H_

void wx_write_eitr_vf(struct wx_q_vector *q_vector);
void wx_configure_msix_vf(struct wx *wx);
int wx_write_uc_addr_list_vf(struct net_device *netdev);
void wx_setup_psrtype_vf(struct wx *wx);
void wx_setup_vfmrqc_vf(struct wx *wx);
void wx_configure_tx_vf(struct wx *wx);
void wx_configure_rx_ring_vf(struct wx *wx, struct wx_ring *ring);

#endif /* _WX_VF_LIB_H_ */

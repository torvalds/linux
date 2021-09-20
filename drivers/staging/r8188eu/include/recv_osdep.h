/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __RECV_OSDEP_H_
#define __RECV_OSDEP_H_

#include "osdep_service.h"
#include "drv_types.h"

int _rtw_init_recv_priv(struct recv_priv *precvpriv, struct adapter *padapter);
void _rtw_free_recv_priv(struct recv_priv *precvpriv);

s32  rtw_recv_entry(struct recv_frame *precv_frame);
int rtw_recv_indicatepkt(struct adapter *adapter, struct recv_frame *recv_frame);
void rtw_recv_returnpacket(struct  net_device *cnxt, struct sk_buff *retpkt);

void rtw_hostapd_mlme_rx(struct adapter *padapter, struct recv_frame *recv_fr);
void rtw_handle_tkip_mic_err(struct adapter *padapter, u8 bgroup);

int rtw_init_recv_priv(struct recv_priv *precvpriv, struct adapter *padapter);
void rtw_free_recv_priv(struct recv_priv *precvpriv);

int rtw_os_recv_resource_init(struct recv_priv *recvpr, struct adapter *adapt);
int rtw_os_recv_resource_alloc(struct adapter *adapt, struct recv_frame *recvfr);
void rtw_os_recv_resource_free(struct recv_priv *precvpriv);

int rtw_os_recvbuf_resource_alloc(struct adapter *adapt, struct recv_buf *buf);
int rtw_os_recvbuf_resource_free(struct adapter *adapt, struct recv_buf *buf);

void rtw_os_read_port(struct adapter *padapter, struct recv_buf *precvbuf);

void rtw_init_recv_timer(struct recv_reorder_ctrl *preorder_ctrl);
int _netdev_open(struct net_device *pnetdev);
int netdev_open(struct net_device *pnetdev);
int netdev_close(struct net_device *pnetdev);

#endif /*  */

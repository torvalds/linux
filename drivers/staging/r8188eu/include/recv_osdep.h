/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __RECV_OSDEP_H_
#define __RECV_OSDEP_H_

#include <osdep_service.h>
#include <drv_types.h>

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
int nat25_handle_frame(struct adapter *priv, struct sk_buff *skb);
int _netdev_open(struct net_device *pnetdev);
int netdev_open(struct net_device *pnetdev);
int netdev_close(struct net_device *pnetdev);

#endif /*  */

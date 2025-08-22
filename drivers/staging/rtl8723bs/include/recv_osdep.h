/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __RECV_OSDEP_H_
#define __RECV_OSDEP_H_


extern signed int _rtw_init_recv_priv(struct recv_priv *precvpriv, struct adapter *padapter);
extern void _rtw_free_recv_priv(struct recv_priv *precvpriv);


extern s32  rtw_recv_entry(union recv_frame *precv_frame);
extern void rtw_recv_returnpacket(struct net_device *cnxt, struct sk_buff *preturnedpkt);

int	rtw_init_recv_priv(struct recv_priv *precvpriv, struct adapter *padapter);
void rtw_free_recv_priv(struct recv_priv *precvpriv);

void rtw_os_recvbuf_resource_free(struct adapter *padapter, struct recv_buf *precvbuf);

struct sk_buff *rtw_os_alloc_msdu_pkt(union recv_frame *prframe, u16 nSubframe_Length, u8 *pdata);
void rtw_os_recv_indicate_pkt(struct adapter *padapter, struct sk_buff *pkt, struct rx_pkt_attrib *pattrib);

#endif /*  */

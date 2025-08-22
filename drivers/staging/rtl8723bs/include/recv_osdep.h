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

#endif /*  */

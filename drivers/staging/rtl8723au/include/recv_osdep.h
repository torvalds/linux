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
 ******************************************************************************/
#ifndef __RECV_OSDEP_H_
#define __RECV_OSDEP_H_

#include <osdep_service.h>
#include <drv_types.h>

int _rtw_init_recv_priv23a(struct recv_priv *precvpriv, struct rtw_adapter *padapter);
void _rtw_free_recv_priv23a (struct recv_priv *precvpriv);

int rtw_recv_entry23a(struct recv_frame *precv_frame);
int rtw_recv_indicatepkt23a(struct rtw_adapter *adapter, struct recv_frame *precv_frame);

void rtw_handle_tkip_mic_err23a(struct rtw_adapter *padapter, u8 bgroup);

int	rtw_init_recv_priv(struct recv_priv *precvpriv, struct rtw_adapter *padapter);
void rtw_free_recv_priv (struct recv_priv *precvpriv);

int rtw_os_recv_resource_init(struct recv_priv *precvpriv, struct rtw_adapter *padapter);

void rtw_init_recv_timer23a(struct recv_reorder_ctrl *preorder_ctrl);

#endif

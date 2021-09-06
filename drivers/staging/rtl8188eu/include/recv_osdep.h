/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __RECV_OSDEP_H_
#define __RECV_OSDEP_H_

#include <osdep_service.h>
#include <drv_types.h>

int _rtw_init_recv_priv(struct recv_priv *precvpriv, struct adapter *padapter);
void _rtw_free_recv_priv(struct recv_priv *precvpriv);

s32  rtw_recv_entry(struct recv_frame *precv_frame);
int rtw_recv_indicatepkt(struct adapter *adapter,
			 struct recv_frame *recv_frame);

void rtw_handle_tkip_mic_err(struct adapter *padapter, u8 bgroup);

int rtw_os_recvbuf_resource_alloc(struct recv_buf *precvbuf);

void rtw_init_recv_timer(struct recv_reorder_ctrl *preorder_ctrl);

#endif /*  */

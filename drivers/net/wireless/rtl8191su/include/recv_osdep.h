/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>


extern sint _init_recv_priv(struct recv_priv *precvpriv, _adapter *padapter);
extern void _free_recv_priv (struct recv_priv *precvpriv);
extern  sint update_rxstatus(_adapter *adapter, union recv_frame *precv_frame, struct recv_stat *rxstatus );


extern s32  recv_entry(union recv_frame *precv_frame);	
extern void recv_indicatepkt(_adapter *adapter, union recv_frame *precv_frame);
extern void recv_returnpacket(IN _nic_hdl cnxt, IN _pkt *preturnedpkt);


extern void handle_tkip_mic_err(_adapter *padapter,u8 bgroup);
		

int	init_recv_priv(struct recv_priv *precvpriv, _adapter *padapter);
void free_recv_priv (struct recv_priv *precvpriv);


int os_recv_resource_init(struct recv_priv *precvpriv, _adapter *padapter);
int os_recv_resource_alloc(_adapter *padapter, union recv_frame *precvframe);
void os_recv_resource_free(struct recv_priv *precvpriv);


int os_recvbuf_resource_alloc(_adapter *padapter, struct recv_buf *precvbuf);
int os_recvbuf_resource_free(_adapter *padapter, struct recv_buf *precvbuf);

void os_read_port(_adapter *padapter, struct recv_buf *precvbuf);

void init_recv_timer(struct recv_reorder_ctrl *preorder_ctrl);

#endif //


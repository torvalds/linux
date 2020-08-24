/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef _RTL8821CS_RECV_H_
#define _RTL8821CS_RECV_H_

#include <drv_types.h>		/* PADAPTER and etc. */

s32 rtl8821cs_init_recv_priv(PADAPTER);
void rtl8821cs_free_recv_priv(PADAPTER);
/*_pkt* rtl8821cs_alloc_recvbuf_skb(struct recv_buf*, u32 size);*/
/*void rtl8821cs_free_recvbuf_skb(struct recv_buf*);*/
void rtl8821cs_rxhandler(PADAPTER, struct recv_buf *);
s32 rtl8821cs_recv_hdl(_adapter *adapter);

#endif /* _RTL8821CS_RECV_H_ */

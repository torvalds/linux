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
 *
 ******************************************************************************/
#ifndef __XMIT_OSDEP_H_
#define __XMIT_OSDEP_H_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>

#define NR_XMITFRAME	256

struct xmit_priv;
struct pkt_attrib;
struct sta_xmit_priv;
struct xmit_frame;
struct xmit_buf;

int rtw_xmit_entry(struct sk_buff *pkt, struct net_device *pnetdev);

void rtw_os_xmit_schedule(struct rtw_adapter *padapter);

int rtw_os_xmit_resource_alloc(struct rtw_adapter *padapter, struct xmit_buf *pxmitbuf,u32 alloc_sz);
void rtw_os_xmit_resource_free(struct rtw_adapter *padapter, struct xmit_buf *pxmitbuf,u32 free_sz);

void rtw_set_tx_chksum_offload(struct sk_buff *pkt, struct pkt_attrib *pattrib);

void rtw_os_pkt_complete(struct rtw_adapter *padapter, struct sk_buff *pkt);
void rtw_os_xmit_complete(struct rtw_adapter *padapter, struct xmit_frame *pxframe);

#endif /* __XMIT_OSDEP_H_ */

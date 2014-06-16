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
#ifndef __USB_OSINTF_H
#define __USB_OSINTF_H

#include <osdep_service.h>
#include <drv_types.h>

extern char *rtw_initmac;
extern int rtw_mc2u_disable;

#define USBD_HALTED(Status) ((u32)(Status) >> 30 == 3)

int pm_netdev_open(struct net_device *pnetdev, u8 bnormal);
void dhcp_flag_bcast(struct adapter *priv, struct sk_buff *skb);
void *scdb_findEntry(struct adapter *priv, unsigned char *macAddr,
		     unsigned char *ipAddr);

int rtw_resume_process(struct adapter *padapter);

#endif

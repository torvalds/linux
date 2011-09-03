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
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef __USB_OSINTF_H
#define __USB_OSINTF_H

#include "osdep_service.h"
#include "drv_types.h"
#include "usb_vendor_req.h"

#define USBD_HALTED(Status) ((u32)(Status) >> 30 == 3)

extern char *r8712_initmac;

unsigned int r8712_usb_inirp_init(struct _adapter *padapter);
unsigned int r8712_usb_inirp_deinit(struct _adapter *padapter);
uint rtl871x_hal_init(struct _adapter *padapter);
uint rtl8712_hal_deinit(struct _adapter *padapter);

void rtl871x_intf_stop(struct _adapter *padapter);
void r871x_dev_unload(struct _adapter *padapter);
void r8712_stop_drv_threads(struct _adapter *padapter);
void r8712_stop_drv_timers(struct _adapter *padapter);
u8 r8712_init_drv_sw(struct _adapter *padapter);
u8 r8712_free_drv_sw(struct _adapter *padapter);
struct net_device *r8712_init_netdev(void);

#endif

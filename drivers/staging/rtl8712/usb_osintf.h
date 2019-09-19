/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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

extern char *r8712_initmac;

unsigned int r8712_usb_inirp_init(struct _adapter *padapter);
unsigned int r8712_usb_inirp_deinit(struct _adapter *padapter);
uint rtl871x_hal_init(struct _adapter *padapter);
uint rtl8712_hal_deinit(struct _adapter *padapter);

void rtl871x_intf_stop(struct _adapter *padapter);
void r871x_dev_unload(struct _adapter *padapter);
void r8712_stop_drv_threads(struct _adapter *padapter);
void r8712_stop_drv_timers(struct _adapter *padapter);
int r8712_init_drv_sw(struct _adapter *padapter);
void r8712_free_drv_sw(struct _adapter *padapter);
struct net_device *r8712_init_netdev(void);

#endif

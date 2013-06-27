/*
 * drivers/usb/sunxi_usb/hcd/include/sw_hcd_virt_hub.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * javen <javen@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef  __SW_HCD_VIRT_HUB_H__
#define  __SW_HCD_VIRT_HUB_H__

void sw_hcd_root_disconnect(struct sw_hcd *sw_hcd);
int sw_hcd_hub_status_data(struct usb_hcd *hcd, char *buf);
int sw_hcd_hub_control(struct usb_hcd *hcd,
                     u16 typeReq,
                     u16 wValue,
                     u16 wIndex,
                     char *buf,
                     u16 wLength);

void sw_hcd_port_suspend_ex(struct sw_hcd *sw_hcd);
void sw_hcd_port_resume_ex(struct sw_hcd *sw_hcd);
void sw_hcd_port_reset_ex(struct sw_hcd *sw_hcd);

#endif   //__SW_HCD_VIRT_HUB_H__


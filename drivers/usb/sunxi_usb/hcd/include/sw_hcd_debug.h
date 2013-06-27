/*
 * drivers/usb/sunxi_usb/hcd/include/sw_hcd_debug.h
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

#ifndef  __SW_HCD_DEBUG_H__
#define  __SW_HCD_DEBUG_H__

#include  "sw_hcd_core.h"

void print_sw_hcd_config(struct sw_hcd_config *config, char *str);
void print_sw_hcd_list(struct list_head *list_head, char *str);
void print_urb_list(struct usb_host_endpoint *hep, char *str);

#endif   //__SW_HCD_DEBUG_H__


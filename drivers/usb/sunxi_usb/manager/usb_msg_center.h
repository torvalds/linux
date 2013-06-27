/*
 * drivers/usb/sunxi_usb/manager/usb_msg_center.h
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

#ifndef  __USB_MSG_CENTER_H__
#define  __USB_MSG_CENTER_H__

/* usb role mode */
typedef enum usb_role{
	USB_ROLE_NULL = 0,
	USB_ROLE_HOST,
	USB_ROLE_DEVICE,
}usb_role_t;

typedef struct usb_msg{
	u8  app_drv_null;       //不加载如何驱动
	u8  app_insmod_host;
	u8  app_rmmod_host;
	u8  app_insmod_device;
	u8  app_rmmod_device;

	u8  hw_insmod_host;
	u8  hw_rmmod_host;
	u8  hw_insmod_device;
	u8  hw_rmmod_device;
}usb_msg_t;

typedef struct usb_msg_center_info{
	struct usb_cfg *cfg;

	struct usb_msg msg;
	enum usb_role role;

	u32 skip;                 	//是否跳跃，不进入消息处理
	                          	//主要是过滤无效消息
}usb_msg_center_info_t;

//----------------------------------------------------------
//
//----------------------------------------------------------
void hw_insmod_usb_host(void);
void hw_rmmod_usb_host(void);
void hw_insmod_usb_device(void);
void hw_rmmod_usb_device(void);

enum usb_role get_usb_role(void);
void usb_msg_center(struct usb_cfg *cfg);

s32 usb_msg_center_init(struct usb_cfg *cfg);
s32 usb_msg_center_exit(struct usb_cfg *cfg);

#endif   //__USB_MSG_CENTER_H__



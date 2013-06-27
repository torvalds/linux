/*
 * drivers/usb/sunxi_usb/include/sw_usb_board.h
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

#ifndef  __SW_USB_BOARD_H__
#define  __SW_USB_BOARD_H__

//----------------------------------------------------------
//
//----------------------------------------------------------
#define  SET_USB_PARA				"usb_para"
#define  SET_USB0					"usbc0"
#define  SET_USB1					"usbc1"
#define  SET_USB2					"usbc2"

#define  KEY_USB_GLOBAL_ENABLE		"usb_global_enable"
#define  KEY_USBC_NUM				"usbc_num"

#define  KEY_USB_ENABLE				"usb_used"
#define  KEY_USB_PORT_TYPE			"usb_port_type"
#define  KEY_USB_DETECT_TYPE		"usb_detect_type"
#define  KEY_USB_ID_GPIO			"usb_id_gpio"
#define  KEY_USB_DETVBUS_GPIO		"usb_det_vbus_gpio"
#define  KEY_USB_DRVVBUS_GPIO		"usb_drv_vbus_gpio"

#define  KEY_USB_HOST_INIT_STATE    "usb_host_init_state"

//---------------------------------------------------
//
//  USB  配置信息
//
//---------------------------------------------------
enum usb_gpio_group_type{
    GPIO_GROUP_TYPE_PIO = 0,
    GPIO_GROUP_TYPE_POWER,
};

/* 0: device only; 1: host only; 2: otg */
enum usb_port_type{
    USB_PORT_TYPE_DEVICE = 0,
    USB_PORT_TYPE_HOST,
    USB_PORT_TYPE_OTG,
};

/* 0: dp/dm检测， 1: vbus/id检测 */
enum usb_detect_type{
    USB_DETECT_TYPE_DP_DM = 0,
    USB_DETECT_TYPE_VBUS_ID,
};

/* pio信息 */
typedef struct usb_gpio{
	__u32 valid;          	/* pio是否可用。 0:无效, !0:有效	*/

	__u32 group_type;		/* pio类型 							*/
	user_gpio_set_t gpio_set;
}usb_gpio_t;

typedef struct usb_port_info{
	__u32 enable;          				/* port是否可用			*/

	__u32 port_no;						/* usb端口号			*/
	enum usb_port_type port_type;    	/* usb端口类型			*/
	enum usb_detect_type detect_type; 	/* usb检测方式			*/

	usb_gpio_t id;						/* usb id pin信息 		*/
	usb_gpio_t det_vbus;				/* usb vbus pin信息 	*/
	usb_gpio_t drv_vbus;				/* usb drv_vbus pin信息	*/
	__u32 host_init_state;				/* usb 控制器的初始化状态。0 : 不工作. 1 : 工作 */
}usb_port_info_t;

typedef struct usb_cfg{
	u32 usb_global_enable;
	u32 usbc_num;

	struct usb_port_info port[USBC_MAX_CTL_NUM];
}usb_cfg_t;

#endif   //__SW_USB_BOARD_H__


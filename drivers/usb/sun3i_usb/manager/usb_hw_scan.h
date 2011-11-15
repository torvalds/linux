/*
*************************************************************************************
*                         			      Linux
*					           USB Host Controller Driver
*
*				        (c) Copyright 2006-2010, All winners Co,Ld.
*							       All Rights Reserved
*
* File Name 	: usb_hw_scan.h
*
* Author 		: javen
*
* Description 	: USB ¼ì²â
*
* History 		:
*      <author>    		<time>       	<version >    		<desc>
*       javen     	  2011-4-14            1.0          create this file
*
*************************************************************************************
*/
#ifndef  __USB_HW_SCAN_H__
#define  __USB_HW_SCAN_H__

#define  USB_SCAN_INSMOD_DEVICE_DRIVER_DELAY	4
#define  USB_SCAN_INSMOD_HOST_DRIVER_DELAY		1

/* ubs id */
typedef enum usb_id_state{
	USB_HOST_MODE = 0,
	USB_DEVICE_MODE = 1,
}usb_id_state_t;

/* usb detect vbus */
typedef enum usb_det_vbus_state{
    USB_DET_VBUS_INVALID = 0,
	USB_DET_VBUS_VALID  = 1
}usb_det_vbus_state_t;

/* usb info */
typedef struct usb_scan_info{
	struct usb_cfg 			*cfg;

	u32                     id_hdle;                /* id handle                */
	user_gpio_set_t         id_gpio_set;            /* id gpio set              */

	u32                     det_vbus_hdle;        	/* detect vbus handle       */
	user_gpio_set_t         det_vbus_gpio_set;      /* detect vbus gpio set     */

    usb_id_state_t          id_old_state;           /* last id state            */
    usb_det_vbus_state_t    det_vbus_old_state;     /* last detect vbus state   */

    u32                     device_insmod_delay;    /* debounce time            */
    u32                     host_insmod_delay;    	/* debounce time            */
}usb_scan_info_t;

void usb_hw_scan(struct usb_cfg *cfg);

__s32 usb_hw_scan_init(struct usb_cfg *cfg);
__s32 usb_hw_scan_exit(struct usb_cfg *cfg);

#endif   //__USB_HW_SCAN_H__


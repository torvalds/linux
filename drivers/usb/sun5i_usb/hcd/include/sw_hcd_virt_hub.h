/*
*************************************************************************************
*                         			      Linux
*					           USB Host Controller Driver
*
*				        (c) Copyright 2006-2010, All winners Co,Ld.
*							       All Rights Reserved
*
* File Name 	: sw_hcd_virt_hub.h
*
* Author 		: javen
*
* Description 	: ÐéÄâ hub
*
* History 		:
*      <author>    		<time>       	<version >    		<desc>
*       javen     	  2010-12-20           1.0          create this file
*
*************************************************************************************
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


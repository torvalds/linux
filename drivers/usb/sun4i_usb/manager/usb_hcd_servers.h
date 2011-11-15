/*
*************************************************************************************
*                         			      Linux
*					           USB Host Controller Driver
*
*				        (c) Copyright 2006-2010, All winners Co,Ld.
*							       All Rights Reserved
*
* File Name 	: usb_hcd_servers.h
*
* Author 		: javen
*
* Description 	: USB 主机控制器驱动服务函数集
*
* History 		:
*      <author>    		<time>       	<version >    		<desc>
*       javen     	  2011-4-14            1.0          create this file
*
*************************************************************************************
*/
#ifndef  __USB_HCD_SERVERS_H__
#define  __USB_HCD_SERVERS_H__


int sw_usb_disable_hcd(__u32 usbc_no);
int sw_usb_enable_hcd(__u32 usbc_no);



#endif  //__USB_HCD_SERVERS_H__



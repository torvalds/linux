/*
*************************************************************************************
*                         			      Linux
*					           USB Host Controller Driver
*
*				        (c) Copyright 2006-2010, All winners Co,Ld.
*							       All Rights Reserved
*
* File Name 	: sw_hcd_debug.h
*
* Author 		: javen
*
* Description 	:
*
* History 		:
*      <author>    		<time>       	<version >    		<desc>
*       javen     	  2010-12-20           1.0          create this file
*
*************************************************************************************
*/
#ifndef  __SW_HCD_DEBUG_H__
#define  __SW_HCD_DEBUG_H__

#include  "sw_hcd_core.h"

void print_sw_hcd_config(struct sw_hcd_config *config, char *str);
void print_sw_hcd_list(struct list_head *list_head, char *str);
void print_urb_list(struct usb_host_endpoint *hep, char *str);

#endif   //__SW_HCD_DEBUG_H__


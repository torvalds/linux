/*
*************************************************************************************
*                         			      Linux
*					           USB Host Controller Driver
*
*				        (c) Copyright 2006-2010, All winners Co,Ld.
*							       All Rights Reserved
*
* File Name 	: sw_udc_board.h
*
* Author 		: javen
*
* Description 	: °å¼¶¿ØÖÆ
*
* Notes         :
*
* History 		:
*      <author>    		<time>       	<version >    		<desc>
*       javen     	  2010-12-20           1.0          create this file
*
*************************************************************************************
*/
#ifndef  __SW_UDC_BOARD_H__
#define  __SW_UDC_BOARD_H__

u32 open_usb_clock(sw_udc_io_t *sw_udc_io);
u32 close_usb_clock(sw_udc_io_t *sw_udc_io);

__s32 sw_udc_io_init(__u32 usbc_no, struct platform_device *pdev, sw_udc_io_t *sw_udc_io);
__s32 sw_udc_io_exit(__u32 usbc_no, struct platform_device *pdev, sw_udc_io_t *sw_udc_io);

#endif   //__SW_UDC_BOARD_H__



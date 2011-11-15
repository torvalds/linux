/*
*************************************************************************************
*                         			      Linux
*					           USB Host Controller Driver
*
*				        (c) Copyright 2006-2010, All winners Co,Ld.
*							       All Rights Reserved
*
* File Name 	: usbc_platform.h
*
* Author 		: javen
*
* Description 	: USB控制器设备信息
*
* History 		:
*      <author>    		<time>       	<version >    		<desc>
*       javen     	  2011-4-14            1.0          create this file
*
*************************************************************************************
*/
#ifndef  __USBC_PLATFORM_H__
#define  __USBC_PLATFORM_H__

__s32 usbc0_platform_device_init(void);
__s32 usbc1_platform_device_init(void);
__s32 usbc2_platform_device_init(void);

__s32 usbc0_platform_device_exit(void);
__s32 usbc1_platform_device_exit(void);
__s32 usbc2_platform_device_exit(void);

#endif   //__USBC_PLATFORM_H__


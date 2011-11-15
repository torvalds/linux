/*
********************************************************************************************************************
*                                              usb controller
*
*                              (c) Copyright 2007-2009, daniel.China
*										All	Rights Reserved
*
* File Name 	: usbc_i.h
*
* Author 		: daniel
*
* Version 		: 1.0
*
* Date 			: 2009.09.15
*
* Description 	: 适用于sunii平台，USB公共操作部分
*
* History 		:
*
********************************************************************************************************************
*/
#ifndef  __USBC_I_H__
#define  __USBC_I_H__

#include "../include/sw_usb_config.h"

#define  USBC_MAX_OPEN_NUM    8

/* 记录USB的公共信息 */
typedef struct __fifo_info{
    __u32 port0_fifo_addr;
	__u32 port0_fifo_size;

    __u32 port1_fifo_addr;
	__u32 port1_fifo_size;

	__u32 port2_fifo_addr;
	__u32 port2_fifo_size;
}__fifo_info_t;

/* 记录当前USB port所有的硬件信息 */
typedef struct __usbc_otg{
    __u32 port_num;
	__u32 base_addr;        /* usb base address 		*/

	__u32 used;             /* 是否正在被使用   		*/
    __u32 no;               /* 在管理数组中的位置 		*/
}__usbc_otg_t;

#endif   //__USBC_I_H__


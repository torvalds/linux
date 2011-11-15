/*
*************************************************************************************
*                         			      Linux
*					           USB Host Controller Driver
*
*				        (c) Copyright 2006-2010, All winners Co,Ld.
*							       All Rights Reserved
*
* File Name 	: sw_hcd_dma.h
*
* Author 		: javen
*
* Description 	: dma操作
*
* Notes         :
*
* History 		:
*      <author>    		<time>       	<version >    		<desc>
*       javen     	  2010-12-20           1.0          create this file
*
*************************************************************************************
*/
#ifndef  __SW_HCD_DMA_H__
#define  __SW_HCD_DMA_H__

//---------------------------------------------------------------
//  宏 定义
//---------------------------------------------------------------
//modify by dengkexi for android
//#define  is_hcd_support_dma()	1
#define  is_hcd_support_dma()	0

/* 使用DMA的条件: 1、大于整包  2、DMA空闲 3、非ep0 */
#define  is_sw_hcd_dma_capable(len, maxpacket, epnum)	(is_hcd_support_dma() \
	                                             		 && (len > maxpacket) \
	                                             		 && epnum)

//---------------------------------------------------------------
//  数据结构 定义
//---------------------------------------------------------------
typedef struct sw_hcd_dma{
	char name[32];
	struct sw_dma_client dma_client;

	int dma_hdle;	/* dma 句柄 */
}sw_hcd_dma_t;

//---------------------------------------------------------------
//  函数 定义
//---------------------------------------------------------------
void sw_hcd_switch_bus_to_dma(struct sw_hcd_qh *qh, u32 is_in);
void sw_hcd_switch_bus_to_pio(struct sw_hcd_qh *qh, __u32 is_in);

void sw_hcd_dma_set_config(struct sw_hcd_qh *qh, __u32 buff_addr, __u32 len);
__u32 sw_hcd_dma_is_busy(struct sw_hcd_qh *qh);

void sw_hcd_dma_start(struct sw_hcd_qh *qh, __u32 fifo, __u32 buffer, __u32 len);
void sw_hcd_dma_stop(struct sw_hcd_qh *qh);
__u32 sw_hcd_dma_transmit_length(struct sw_hcd_qh *qh, __u32 is_in, __u32 buffer_addr);

__s32 sw_hcd_dma_probe(struct sw_hcd *sw_hcd);
__s32 sw_hcd_dma_remove(struct sw_hcd *sw_hcd);

#endif   //__SW_HCD_DMA_H__



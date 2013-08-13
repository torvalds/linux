/*
 * drivers/usb/sunxi_usb/hcd/include/sw_hcd_dma.h
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

#ifndef  __SW_HCD_DMA_H__
#define  __SW_HCD_DMA_H__

//---------------------------------------------------------------
//  宏 定义
//---------------------------------------------------------------
#ifdef SW_HCD_DMA
#define  is_hcd_support_dma(usbc_no)    (usbc_no == 0)
#else
#define  is_hcd_support_dma(usbc_no)    0
#endif

/* 使用DMA的条件: 1、大于整包  2、DMA空闲 3、非ep0 */
#define  is_sw_hcd_dma_capable(usbc_no, len, maxpacket, epnum)	(is_hcd_support_dma(usbc_no) \
        	                                             		 && (len > maxpacket) \
        	                                             		 && epnum)

#ifdef SW_HCD_DMA
//---------------------------------------------------------------
//  数据结构 定义
//---------------------------------------------------------------
typedef struct sw_hcd_dma{
	char name[32];
	struct sw_dma_client dma_client;

	int dma_hdle;	/* dma 句柄 */
}sw_hcd_dma_t;
#endif

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



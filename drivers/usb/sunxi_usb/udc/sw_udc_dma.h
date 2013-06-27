/*
 * drivers/usb/sunxi_usb/udc/sw_udc_dma.h
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

#ifndef  __SW_UDC_DMA_H__
#define  __SW_UDC_DMA_H__

//---------------------------------------------------------------
//  宏 定义
//---------------------------------------------------------------
#ifdef  SW_UDC_DMA
#define  is_udc_support_dma()       1
#else
#define  is_udc_support_dma()       0
#endif


#define  is_tx_ep(ep)		((ep->bEndpointAddress) & USB_DIR_IN)

//---------------------------------------------------------------
//  函数 定义
//---------------------------------------------------------------
void sw_udc_switch_bus_to_dma(struct sw_udc_ep *ep, u32 is_tx);
void sw_udc_switch_bus_to_pio(struct sw_udc_ep *ep, __u32 is_tx);

void sw_udc_enable_dma_channel_irq(struct sw_udc_ep *ep);
void sw_udc_disable_dma_channel_irq(struct sw_udc_ep *ep);

void sw_udc_dma_set_config(struct sw_udc_ep *ep, struct sw_udc_request *req, __u32 buff_addr, __u32 len);
void sw_udc_dma_start(struct sw_udc_ep *ep, __u32 fifo, __u32 buffer, __u32 len);
void sw_udc_dma_stop(struct sw_udc_ep *ep);
__u32 sw_udc_dma_transmit_length(struct sw_udc_ep *ep, __u32 is_in, __u32 buffer_addr);
__u32 sw_udc_dma_is_busy(struct sw_udc_ep *ep);

__s32 sw_udc_dma_probe(struct sw_udc *dev);
__s32 sw_udc_dma_remove(struct sw_udc *dev);

#endif   //__SW_UDC_DMA_H__


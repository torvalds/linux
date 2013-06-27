/*
 * drivers/usb/sunxi_usb/udc/sw_udc_dma.c
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


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <asm/cacheflush.h>

#include  "sw_udc_config.h"
#include  "sw_udc_board.h"
#include  "sw_udc_dma.h"

static sw_udc_dma_parg_t sw_udc_dma_para;

extern void sw_udc_dma_completion(struct sw_udc *dev, struct sw_udc_ep *ep, struct sw_udc_request *req);

static void sw_udc_CleanFlushDCacheRegion(void *adr, __u32 bytes)
{
	__cpuc_flush_dcache_area(adr, bytes + (1 << 5) * 2 - 2);
}

/*
*******************************************************************************
*                     sw_udc_switch_bus_to_dma
*
* Description:
*    切换 USB 总线给 DMA
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void sw_udc_switch_bus_to_dma(struct sw_udc_ep *ep, u32 is_tx)
{
	if(!is_tx){ /* ep in, rx */
		USBC_SelectBus(ep->dev->sw_udc_io->usb_bsp_hdle,
			           USBC_IO_TYPE_DMA,
			           USBC_EP_TYPE_RX,
			           ep->num);
	}else{  /* ep out, tx */
		USBC_SelectBus(ep->dev->sw_udc_io->usb_bsp_hdle,
					   USBC_IO_TYPE_DMA,
					   USBC_EP_TYPE_TX,
					   ep->num);
	}

    return;
}

/*
*******************************************************************************
*                     sw_udc_switch_bus_to_pio
*
* Description:
*    切换 USB 总线给 PIO
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void sw_udc_switch_bus_to_pio(struct sw_udc_ep *ep, __u32 is_tx)
{
	USBC_SelectBus(ep->dev->sw_udc_io->usb_bsp_hdle, USBC_IO_TYPE_PIO, 0, 0);

    return;
}

/*
*******************************************************************************
*                     sw_udc_enable_dma_channel_irq
*
* Description:
*    使能 DMA channel 中断
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void sw_udc_enable_dma_channel_irq(struct sw_udc_ep *ep)
{
	DMSG_DBG_DMA("sw_udc_enable_dma_channel_irq\n");

    return;
}

/*
*******************************************************************************
*                     sw_udc_disable_dma_channel_irq
*
* Description:
*    禁止 DMA channel 中断
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void sw_udc_disable_dma_channel_irq(struct sw_udc_ep *ep)
{
	DMSG_DBG_DMA("sw_udc_disable_dma_channel_irq\n");

    return;
}

/*
*******************************************************************************
*                     sw_udc_dma_set_config
*
* Description:
*    配置 DMA
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void sw_udc_dma_set_config(struct sw_udc_ep *ep, struct sw_udc_request *req, __u32 buff_addr, __u32 len)
{
	struct dma_hw_conf dma_config;

	__u32 is_tx				= 0;
	__u32 packet_size		= 0;
	__u32 usbc_no          	= 0;
	__u32 usb_cmt_blk_cnt  	= 0;
	__u32 dram_cmt_blk_cnt 	= 0;
	__u32 fifo_addr 	   	= 0;

	memset(&dma_config, 0, sizeof(struct dma_hw_conf));

	is_tx = is_tx_ep(ep);
	packet_size = ep->ep.maxpacket;

	usb_cmt_blk_cnt  = (((packet_size >> 2) - 1) << 8) | 0x0f;
	dram_cmt_blk_cnt = (((packet_size >> 2) - 1) << 8);
	fifo_addr        = USBC_REG_EPFIFOx(USBC0_BASE, ep->num);

	DMSG_DBG_DMA("config: ep(%d), fifo_addr(0x%x), buff_addr(0x%x), len (0x%x)\n",
		      	ep->num, fifo_addr, buff_addr, len);

	if(!is_tx){ /* ep out, rx*/
		usbc_no = D_DRQSRC_USB0;
		dma_config.drqsrc_type	= usbc_no;
		dma_config.drqdst_type	= D_DRQDST_SDRAM;
		if((u32)buff_addr & 0x03){
			dma_config.xfer_type = DMAXFER_D_SBYTE_S_BWORD;
		}else{
			dma_config.xfer_type = DMAXFER_D_BWORD_S_BWORD;
		}
		dma_config.address_type	= DMAADDRT_D_LN_S_IO;
		dma_config.dir			= SW_DMA_RDEV;
		dma_config.hf_irq		= SW_DMA_IRQ_FULL;
		dma_config.reload		= 0;
		dma_config.from			= fifo_addr;
		dma_config.to			= 0;
		dma_config.cmbk			= usb_cmt_blk_cnt | (dram_cmt_blk_cnt << 16);
	}else{ /* ep out, tx*/
		usbc_no = D_DRQDST_USB0;
		dma_config.drqsrc_type	= D_DRQSRC_SDRAM;
		dma_config.drqdst_type	= usbc_no;
		if((u32)buff_addr & 0x03){
			dma_config.xfer_type = DMAXFER_D_BWORD_S_SBYTE;
		}else{
			dma_config.xfer_type = DMAXFER_D_BWORD_S_BWORD;
		}
		dma_config.address_type	= DMAADDRT_D_IO_S_LN;
		dma_config.dir			= SW_DMA_WDEV;
		dma_config.hf_irq		= SW_DMA_IRQ_FULL;
		dma_config.reload		= 0;
		dma_config.from			= 0;
		dma_config.to			= fifo_addr;
		dma_config.cmbk			= (usb_cmt_blk_cnt << 16) | dram_cmt_blk_cnt;
	}

	sw_udc_dma_para.ep    = ep;
	sw_udc_dma_para.req	= req;

	sw_dma_config(ep->dev->sw_udc_dma.dma_hdle, &dma_config);

    return;
}

/*
*******************************************************************************
*                     sw_udc_dma_start
*
* Description:
*    开始 DMA 传输
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void sw_udc_dma_start(struct sw_udc_ep *ep, __u32 fifo, __u32 buffer, __u32 len)
{
	DMSG_DBG_DMA("start: ep(%d), fifo = 0x%x, buffer = (0x%x, 0x%x), len = 0x%x\n",
		      sw_udc_dma_para.ep->num,
		      fifo, buffer, (u32)phys_to_virt(buffer), len);

	sw_udc_CleanFlushDCacheRegion((void *)buffer, (size_t)len);

	sw_udc_switch_bus_to_dma(ep, is_tx_ep(ep));

	sw_dma_enqueue(ep->dev->sw_udc_dma.dma_hdle, NULL, (dma_addr_t)buffer, len);
	sw_dma_ctrl(ep->dev->sw_udc_dma.dma_hdle, SW_DMAOP_START);

    return;
}

/*
*******************************************************************************
*                     sw_udc_dma_stop
*
* Description:
*    终止 DMA 传输
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void sw_udc_dma_stop(struct sw_udc_ep *ep)
{
	DMSG_DBG_DMA("sw_udc_dma_stop\n");

	sw_dma_ctrl(ep->dev->sw_udc_dma.dma_hdle, SW_DMAOP_STOP);

	sw_udc_switch_bus_to_pio(ep, is_tx_ep(ep));

	sw_udc_dma_para.ep    			= NULL;
	sw_udc_dma_para.req				= NULL;

    return;
}

/*
*******************************************************************************
*                     sw_udc_dma_transmit_length
*
* Description:
*    查询 DMA 已经传输的长度
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
#if 0
__u32 sw_udc_dma_left_length(struct sw_udc_ep *ep, __u32 is_in, __u32 buffer_addr)
{
    dma_addr_t src = 0;
    dma_addr_t dst = 0;
	__u32 dma_buffer = 0;
	__u32 left_len = 0;

	DMSG_DBG_DMA("sw_udc_dma_transmit_length\n");

	sw_dma_getcurposition(ep->dev->sw_udc_dma.dma_hdle, &src, &dst);
	if(is_in){	/* tx */
		dma_buffer = (__u32)src;
	}else{	/* rx */
		dma_buffer = (__u32)dst;
	}

	left_len = buffer_addr - (u32)phys_to_virt(dma_buffer);

    DMSG_DBG_DMA("dma transfer lenght, buffer_addr(0x%x), dma_buffer(0x%x), left_len(%d), want(%d)\n",
		      buffer_addr, dma_buffer, left_len, ep->dma_transfer_len);

    return left_len;
}

__u32 sw_udc_dma_transmit_length(struct sw_udc_ep *ep, __u32 is_in, __u32 buffer_addr)
{
    if(ep->dma_transfer_len){
		return (ep->dma_transfer_len - sw_udc_dma_left_length(ep, is_in, buffer_addr));
	}

	return ep->dma_transfer_len;
}

#else
__u32 sw_udc_dma_transmit_length(struct sw_udc_ep *ep, __u32 is_in, __u32 buffer_addr)
{
    return ep->dma_transfer_len;
}
#endif

/*
*******************************************************************************
*                     sw_udc_dma_probe
*
* Description:
*    DMA 初始化
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
__u32 sw_udc_dma_is_busy(struct sw_udc_ep *ep)
{
	return 	ep->dma_working;
}

/*
*******************************************************************************
*                     sw_udc_dma_probe
*
* Description:
*    DMA 初始化
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
static void sw_udc_dma_callback(struct sw_dma_chan * ch, void *buf, int size, enum sw_dma_buffresult result)
{
	struct sw_udc_ep *ep = sw_udc_dma_para.ep;
	struct sw_udc_request *req = sw_udc_dma_para.req;

	DMSG_DBG_DMA("callback: epnum(%d), ep(0x%p), length(%d)\n",
		  		sw_udc_dma_para.ep->num, sw_udc_dma_para.ep, sw_udc_dma_para.ep->dma_transfer_len);

	if(sw_udc_dma_para.ep){
		sw_udc_switch_bus_to_pio(sw_udc_dma_para.ep, is_tx_ep(sw_udc_dma_para.ep));

		sw_udc_dma_para.ep = NULL;
		sw_udc_dma_para.req = NULL;

		sw_udc_dma_completion(sw_udc_dma_para.dev, ep, req);
	}else{
		DMSG_PANIC("ERR: sw_udc_dma_callback: dma is remove, but dma irq is happened, (0x%x, 0x%p)\n",
			       sw_udc_dma_para.dev->sw_udc_dma.dma_hdle, sw_udc_dma_para.ep);
	}

	return;
}

/*
*******************************************************************************
*                     sw_udc_dma_probe
*
* Description:
*    DMA 初始化
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
__s32 sw_udc_dma_probe(struct sw_udc *dev)
{
    __u32 channel = 0;

	DMSG_DBG_DMA("sw_udc_dma_probe\n");

	memset(&sw_udc_dma_para, 0, sizeof(sw_udc_dma_parg_t));
	sw_udc_dma_para.dev = dev;

    /* request dma */
	strcpy(dev->sw_udc_dma.name, dev->driver_name);
	strcat(dev->sw_udc_dma.name, "_dma");
	dev->sw_udc_dma.dma_client.name = dev->sw_udc_dma.name;

	channel = DMACH_DUSB0;

	dev->sw_udc_dma.dma_hdle = sw_dma_request(channel, &(dev->sw_udc_dma.dma_client), NULL);
	if(dev->sw_udc_dma.dma_hdle < 0){
		DMSG_PANIC("ERR: sw_dma_request failed\n");
		return -1;
	}

	DMSG_INFO_UDC("dma probe name(%s), dma_hdle = 0x%x\n",
		      dev->sw_udc_dma.name, dev->sw_udc_dma.dma_hdle);

	/* set callback */
	sw_dma_set_buffdone_fn(dev->sw_udc_dma.dma_hdle, sw_udc_dma_callback);

    return 0;
}

/*
*******************************************************************************
*                     sw_udc_dma_remove
*
* Description:
*    DMA 移除
*
* Parameters:
*    void
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
__s32 sw_udc_dma_remove(struct sw_udc *dev)
{
	__u32 channel = 0;

	DMSG_INFO_UDC("dma remove\n");

	channel = DMACH_DUSB0;


	if(dev->sw_udc_dma.dma_hdle >= 0){
		sw_dma_free(channel, &(dev->sw_udc_dma.dma_client));
		dev->sw_udc_dma.dma_hdle = -1;
	}else{
		DMSG_PANIC("ERR: sw_udc_dma_remove, dma_hdle is null\n");
	}

	memset(&sw_udc_dma_para, 0, sizeof(sw_udc_dma_parg_t));

	return 0;
}




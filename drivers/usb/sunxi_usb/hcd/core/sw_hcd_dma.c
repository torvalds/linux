/*
 * drivers/usb/sunxi_usb/hcd/core/sw_hcd_dma.c
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


#include  "../include/sw_hcd_core.h"
#include  "../include/sw_hcd_dma.h"
#include <asm/cacheflush.h>

extern void sw_hcd_dma_completion(struct sw_hcd *sw_hcd, u8 epnum, u8 transmit);

static void hcd_CleanFlushDCacheRegion(void *adr, __u32 bytes)
{
	__cpuc_flush_dcache_area(adr, bytes + (1 << 5) * 2 - 2);
}

/*
*******************************************************************************
*                     sw_hcd_switch_bus_to_dma
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
void sw_hcd_switch_bus_to_dma(struct sw_hcd_qh *qh, u32 is_in)
{
	DMSG_DBG_DMA("sw_hcd_switch_bus_to_dma\n");

	if(is_in){ /* ep in, rx */
		USBC_SelectBus(qh->hw_ep->sw_hcd->sw_hcd_io->usb_bsp_hdle,
			           USBC_IO_TYPE_DMA,
			           USBC_EP_TYPE_RX,
			           qh->hw_ep->epnum);
	}else{  /* ep out, tx */
		USBC_SelectBus(qh->hw_ep->sw_hcd->sw_hcd_io->usb_bsp_hdle,
					   USBC_IO_TYPE_DMA,
					   USBC_EP_TYPE_TX,
					   qh->hw_ep->epnum);
	}

    return;
}
EXPORT_SYMBOL(sw_hcd_switch_bus_to_dma);

/*
*******************************************************************************
*                     sw_hcd_switch_bus_to_pio
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
void sw_hcd_switch_bus_to_pio(struct sw_hcd_qh *qh, __u32 is_in)
{
	DMSG_DBG_DMA("sw_hcd_switch_bus_to_pio\n");

	USBC_SelectBus(qh->hw_ep->sw_hcd->sw_hcd_io->usb_bsp_hdle, USBC_IO_TYPE_PIO, 0, 0);

    return;
}
EXPORT_SYMBOL(sw_hcd_switch_bus_to_pio);

/*
*******************************************************************************
*                     sw_hcd_enable_dma_channel_irq
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
void sw_hcd_enable_dma_channel_irq(struct sw_hcd_qh *qh)
{
	DMSG_DBG_DMA("sw_hcd_enable_dma_channel_irq\n");

    return;
}
EXPORT_SYMBOL(sw_hcd_enable_dma_channel_irq);

/*
*******************************************************************************
*                     sw_hcd_disable_dma_channel_irq
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
void sw_hcd_disable_dma_channel_irq(struct sw_hcd_qh *qh)
{
	DMSG_DBG_DMA("sw_hcd_disable_dma_channel_irq\n");

    return;
}
EXPORT_SYMBOL(sw_hcd_disable_dma_channel_irq);

/*
*******************************************************************************
*                     sw_hcd_dma_set_config
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
void sw_hcd_dma_set_config(struct sw_hcd_qh *qh, __u32 buff_addr, __u32 len)
{
	struct dma_hw_conf dma_config;

	__u32 is_in				= 0;
	__u32 packet_size		= 0;
	__u32 usbc_no          	= 0;
	__u32 usbc_base 		= 0;
	__u32 usb_cmt_blk_cnt  	= 0;
	__u32 dram_cmt_blk_cnt 	= 0;
	__u32 fifo_addr 	   	= 0;

	memset(&dma_config, 0, sizeof(struct dma_hw_conf));

	is_in = is_direction_in(qh);
	packet_size = qh->maxpacket;

	usb_cmt_blk_cnt  = (((packet_size >> 2) - 1) << 8) | 0x0f;
	dram_cmt_blk_cnt = (((packet_size >> 2) - 1) << 8);

	usbc_base = USBC0_BASE;
	fifo_addr = USBC_REG_EPFIFOx(usbc_base, qh->hw_ep->epnum);

	DMSG_DBG_DMA("\nsw_hcd_dma_set_config, fifo_addr(0x%x, 0x%p), buff_addr = 0x%x\n",
		      fifo_addr, qh->hw_ep->fifo, buff_addr);

	if(is_in){ /* ep in, rx*/
		usbc_no = DRQ_TYPE_USB0;

		dma_config.drqsrc_type	= usbc_no;
		dma_config.drqdst_type	= D_DRQDST_SDRAM;
		if((u32)buff_addr & 0x03){
			dma_config.xfer_type = DMAXFER_D_SBYTE_S_BWORD;
		}else{
			dma_config.xfer_type = DMAXFER_D_BWORD_S_BWORD;
		}
		dma_config.address_type	= DMAADDRT_D_LN_S_IO;
		dma_config.dir			= 1;
		dma_config.hf_irq		= SW_DMA_IRQ_FULL;
		dma_config.reload		= 0;
		dma_config.from			= fifo_addr;
		dma_config.to			= 0;
		dma_config.cmbk			= usb_cmt_blk_cnt | (dram_cmt_blk_cnt << 16);
	}else{ /* ep out, tx*/
		usbc_no = DRQ_TYPE_USB0;

		dma_config.drqsrc_type	= D_DRQSRC_SDRAM;
		dma_config.drqdst_type	= usbc_no;
		if((u32)buff_addr & 0x03){
			dma_config.xfer_type = DMAXFER_D_BWORD_S_SBYTE;
		}else{
			dma_config.xfer_type = DMAXFER_D_BWORD_S_BWORD;
		}
		dma_config.address_type	= DMAADDRT_D_IO_S_LN;
		dma_config.dir			= 2;
		dma_config.hf_irq		= SW_DMA_IRQ_FULL;
		dma_config.reload		= 0;
		dma_config.from			= 0;
		dma_config.to			= fifo_addr;
		dma_config.cmbk			= (usb_cmt_blk_cnt << 16) | dram_cmt_blk_cnt;
	}

	sw_dma_config(qh->hw_ep->sw_hcd->sw_hcd_dma.dma_hdle, &dma_config);

    return;
}
EXPORT_SYMBOL(sw_hcd_dma_set_config);

/*
*******************************************************************************
*                     sw_hcd_dma_start
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
void sw_hcd_dma_start(struct sw_hcd_qh *qh, __u32 fifo, __u32 buffer, __u32 len)
{
	DMSG_DBG_DMA("sw_hcd_dma_start, ep(%d, %d), fifo = 0x%x, buffer = (0x%x, 0x%x), len = 0x%x\n",
		      	qh->epnum, qh->hw_ep->epnum,
		      	fifo, buffer, (u32)phys_to_virt(buffer), len);

	qh->dma_working = 1;

	hcd_CleanFlushDCacheRegion((void *)buffer, len);
	sw_hcd_switch_bus_to_dma(qh, is_direction_in(qh));

	sw_dma_enqueue(qh->hw_ep->sw_hcd->sw_hcd_dma.dma_hdle, (void *)qh, (dma_addr_t)phys_to_virt(buffer), len);
	sw_dma_ctrl(qh->hw_ep->sw_hcd->sw_hcd_dma.dma_hdle, SW_DMAOP_START);

    return;
}
EXPORT_SYMBOL(sw_hcd_dma_start);

/*
*******************************************************************************
*                     sw_hcd_dma_stop
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
void sw_hcd_dma_stop(struct sw_hcd_qh *qh)
{
	DMSG_DBG_DMA("sw_hcd_dma_stop\n");

	sw_dma_ctrl(qh->hw_ep->sw_hcd->sw_hcd_dma.dma_hdle, SW_DMAOP_STOP);

	sw_hcd_switch_bus_to_pio(qh, is_direction_in(qh));
	qh->dma_working = 0;
	qh->dma_transfer_len = 0;

    return;
}
EXPORT_SYMBOL(sw_hcd_dma_stop);

/*
*******************************************************************************
*                     sw_hcd_dma_transmit_length
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
#if 1
static __u32 sw_hcd_dma_left_length(struct sw_hcd_qh *qh, __u32 is_in, __u32 buffer_addr)
{
    dma_addr_t src = 0;
    dma_addr_t dst = 0;
	__u32 dma_buffer = 0;
	__u32 left_len = 0;

	sw_dma_getcurposition(qh->hw_ep->sw_hcd->sw_hcd_dma.dma_hdle, &src, &dst);
	if(is_in){
		dma_buffer = (__u32)dst;
	}else{
		dma_buffer = (__u32)src;
	}

	left_len = buffer_addr - dma_buffer;

	DMSG_DBG_DMA("dma transfer lenght, buffer_addr(0x%x), dma_buffer(0x%x), left_len(%d), want(%d)\n",
		      buffer_addr, dma_buffer, left_len, qh->dma_transfer_len);

    return left_len;
}

__u32 sw_hcd_dma_transmit_length(struct sw_hcd_qh *qh, __u32 is_in, __u32 buffer_addr)
{
	DMSG_DBG_DMA("sw_hcd_dma_transmit_length: qh(0x%p, %d, %d), is_in(%d), buffer_addr(0x%x)\n",
		         qh, qh->dma_transfer_len, qh->dma_working,
		         is_in, buffer_addr);

	if(qh->dma_transfer_len){
		return (qh->dma_transfer_len - sw_hcd_dma_left_length(qh, is_in, buffer_addr));
	}else{
		return qh->dma_transfer_len;
	}
}

#else
__u32 sw_hcd_dma_transmit_length(struct sw_hcd_qh *qh, __u32 is_in, __u32 buffer_addr)
{
	DMSG_DBG_DMA("sw_hcd_dma_transmit_length: dma_transfer_len = %d\n", qh->dma_transfer_len);

	return qh->dma_transfer_len;
}
#endif

EXPORT_SYMBOL(sw_hcd_dma_transmit_length);

/*
*******************************************************************************
*                     sw_hcd_dma_probe
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
static void sw_hcd_dma_callback(struct sw_dma_chan * ch, void *buf, int size, enum sw_dma_buffresult result)
{
	struct sw_hcd_qh *qh = (struct sw_hcd_qh *)buf;

	DMSG_DBG_DMA("sw_hcd_dma_callback, epnum(%d, %d), qh(0x%p), length(%d)\n\n",
		  		qh->epnum, qh->hw_ep->epnum, qh, size);

	if(qh){
		qh->dma_working = 0;
		qh->dma_transfer_len = size;

		sw_hcd_switch_bus_to_pio(qh, is_direction_in(qh));
		sw_hcd_dma_completion(qh->hw_ep->sw_hcd, qh->hw_ep->epnum, !is_direction_in(qh));
	}else{
		DMSG_PANIC("ERR: sw_hcd_dma_callback, dma is remove, but dma irq is happened, (0x%x, 0x%p)\n",
			       qh->hw_ep->sw_hcd->sw_hcd_dma.dma_hdle, qh);
	}

	return;
}

/*
*******************************************************************************
*                     sw_hcd_dma_probe
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
__u32 sw_hcd_dma_is_busy(struct sw_hcd_qh *qh)
{
	return qh->dma_working;
}
EXPORT_SYMBOL(sw_hcd_dma_is_busy);

/*
*******************************************************************************
*                     sw_hcd_dma_probe
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
__s32 sw_hcd_dma_probe(struct sw_hcd *sw_hcd)
{
	__u32 dma_channel = 0;

	DMSG_DBG_DMA("sw_hcd_dma_probe\n");

    /* request dma */
	strcpy(sw_hcd->sw_hcd_dma.name, sw_hcd->driver_name);
	strcat(sw_hcd->sw_hcd_dma.name, "_DMA");
	sw_hcd->sw_hcd_dma.dma_client.name = sw_hcd->sw_hcd_dma.name;

	dma_channel = DMACH_DUSB0;

	sw_hcd->sw_hcd_dma.dma_hdle = sw_dma_request(dma_channel, &(sw_hcd->sw_hcd_dma.dma_client), NULL);
	if(sw_hcd->sw_hcd_dma.dma_hdle < 0){
		DMSG_PANIC("ERR: sw_dma_request failed\n");
		return -1;
	}

	DMSG_INFO("sw_hcd_dma_probe: dma_hdle = 0x%x\n", sw_hcd->sw_hcd_dma.dma_hdle);

	/* set callback */
	sw_dma_set_buffdone_fn(sw_hcd->sw_hcd_dma.dma_hdle, sw_hcd_dma_callback);

    return 0;
}
EXPORT_SYMBOL(sw_hcd_dma_probe);

/*
*******************************************************************************
*                     sw_hcd_dma_remove
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
__s32 sw_hcd_dma_remove(struct sw_hcd *sw_hcd)
{
	__u32 dma_channel = 0;

	DMSG_INFO("sw_hcd_dma_remove\n");

	if(sw_hcd->sw_hcd_dma.dma_hdle >= 0){
		dma_channel = DMACH_DUSB0;
		sw_dma_free(dma_channel, &(sw_hcd->sw_hcd_dma.dma_client));
		sw_hcd->sw_hcd_dma.dma_hdle = -1;
	}else{
		DMSG_PANIC("ERR: sw_hcd_dma_remove, sw_hcd->sw_hcd_dma.dma_hdle is null\n");
	}

	return 0;
}
EXPORT_SYMBOL(sw_hcd_dma_remove);





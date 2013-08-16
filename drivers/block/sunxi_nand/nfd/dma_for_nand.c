/*
 * drivers/block/sunxi_nand/nfd/dma_for_nand.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
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

//#include "nand_oal.h"
#include "nand_private.h"
#include <plat/dma_compat.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <asm/cacheflush.h>

#define DMA_HALF_INT_MASK       (1<<0)
#define DMA_END_INT_MASK        (1<<1)

#define NAND_DMA_TIMEOUT 20000 /*20 sec*/

static int nanddma_completed_flag = 1;
static DECLARE_WAIT_QUEUE_HEAD(DMA_wait);

struct sunxi_dma_params nand_dma = {
	.client.name="NAND_DMA",
#if defined CONFIG_ARCH_SUN4I || defined CONFIG_ARCH_SUN5I
	.channel = DMACH_DNAND,
#endif
	.dma_addr = 0x01c03030,
};

void nanddma_buffdone(struct sunxi_dma_params *dma, void *dev_id)
{
	nanddma_completed_flag = 1;
	wake_up( &DMA_wait );
}

__hdle NAND_RequestDMA  (__u32 dmatype)
{
	int r;

	r = sunxi_dma_request(&nand_dma, 1);
	if (r < 0)
		return r;

	r = sunxi_dma_set_callback(&nand_dma, nanddma_buffdone, NULL);
	if (r < 0)
		return r;

	return 1;
}

__s32  NAND_ReleaseDMA  (__hdle hDma)
{
	return 0;
}

void NAND_Config_Start_DMA(__u8 rw, dma_addr_t buff_addr, __u32 len)
{
#if defined CONFIG_ARCH_SUN4I || defined CONFIG_ARCH_SUN5I
	struct dma_hw_conf nand_hwconf = {
		.xfer_type = DMAXFER_D_BWORD_S_BWORD,
		.hf_irq = SW_DMA_IRQ_FULL,
	};

	nand_hwconf.dir = rw+1;

	if(rw == 0){
		nand_hwconf.from = 0x01C03030,
		nand_hwconf.address_type = DMAADDRT_D_LN_S_IO,
		nand_hwconf.drqsrc_type = DRQ_TYPE_NAND;
	} else {
		nand_hwconf.to = 0x01C03030,
		nand_hwconf.address_type = DMAADDRT_D_IO_S_LN,
		nand_hwconf.drqdst_type = DRQ_TYPE_NAND;
	}

	sw_dma_setflags(DMACH_DNAND, SW_DMAF_AUTOSTART);
#else
	static int dma_started = 0;

	dma_config_t nand_hwconf = {
		.xfer_type.src_data_width	= DATA_WIDTH_32BIT,
		.xfer_type.src_bst_len		= DATA_BRST_4,
		.xfer_type.dst_data_width	= DATA_WIDTH_32BIT,
		.xfer_type.dst_bst_len		= DATA_BRST_4,
		.bconti_mode			= false,
		.irq_spt			= CHAN_IRQ_FD,
	};

	if(rw == 0) {
		nand_hwconf.address_type.src_addr_mode	= DDMA_ADDR_IO; 
		nand_hwconf.address_type.dst_addr_mode	= DDMA_ADDR_LINEAR;
		nand_hwconf.src_drq_type		= D_DST_NAND;
		nand_hwconf.dst_drq_type		= D_DST_SDRAM;
	} else {
		nand_hwconf.address_type.src_addr_mode	= DDMA_ADDR_LINEAR;
		nand_hwconf.address_type.dst_addr_mode	= DDMA_ADDR_IO;
		nand_hwconf.src_drq_type		= D_DST_SDRAM;
		nand_hwconf.dst_drq_type		= D_DST_NAND;
	}
#endif
	sunxi_dma_config(&nand_dma, &nand_hwconf, 0x7f077f07);

	nanddma_completed_flag = 0;
	sunxi_dma_enqueue(&nand_dma, buff_addr, len, rw == 0);
#if !defined CONFIG_ARCH_SUN4I && !defined CONFIG_ARCH_SUN5I
	/* No auto-start, start manually */
	if (!dma_started) {
		sunxi_dma_start(&nand_dma);
		dma_started = 1;
	}
#endif
}

__s32 NAND_WaitDmaFinish(void)
{
	int ret = wait_event_timeout(DMA_wait, nanddma_completed_flag,\
			msecs_to_jiffies(NAND_DMA_TIMEOUT));
	if (!ret)
		pr_err("sunxi:nand: Dma operation finish timeout\n");
	return ret;
}

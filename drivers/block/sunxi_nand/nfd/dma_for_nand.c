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
#include <plat/dma.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <asm/cacheflush.h>

#define DMA_HALF_INT_MASK       (1<<0)
#define DMA_END_INT_MASK        (1<<1)

#define NAND_DMA_TIMEOUT 20000 /*20 sec*/

static int nanddma_completed_flag = 1;
static DECLARE_WAIT_QUEUE_HEAD(DMA_wait);

struct sw_dma_client nand_dma_client = {
	.name="NAND_DMA",
};


void nanddma_buffdone(struct sw_dma_chan * ch, void *buf, int size,enum sw_dma_buffresult result){
	nanddma_completed_flag = 1;
	wake_up( &DMA_wait );
}

__hdle NAND_RequestDMA  (__u32 dmatype)
{
	__hdle ch;

	ch = sw_dma_request(DMACH_DNAND, &nand_dma_client, NULL);
	if(ch < 0)
		return ch;

	sw_dma_set_buffdone_fn(ch, nanddma_buffdone);

	return ch;
}

__s32  NAND_ReleaseDMA  (__hdle hDma)
{
	return 0;
}

void NAND_Config_Start_DMA(__u8 rw, __u32 buff_addr, __u32 len)
{	struct dma_hw_conf nand_hwconf = {
		.xfer_type = DMAXFER_D_BWORD_S_BWORD,
		.hf_irq = SW_DMA_IRQ_FULL,
		.cmbk = 0x7f077f07,
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
	sw_dma_config(DMACH_DNAND, &nand_hwconf);

	nanddma_completed_flag = 0;
	sw_dma_enqueue(DMACH_DNAND, NULL, buff_addr, len);
}

__s32 NAND_WaitDmaFinish(void)
{
	int ret = wait_event_timeout(DMA_wait, nanddma_completed_flag,\
			msecs_to_jiffies(NAND_DMA_TIMEOUT));
	if (!ret)
		pr_err("sunxi:nand: Dma operation finish timeout\n");
	return ret;
}

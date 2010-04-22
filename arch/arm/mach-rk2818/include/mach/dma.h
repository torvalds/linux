/*
 * arch/arm/mach-rk2818/include/mach/dma.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __ASM_RK2818_DMA_H
#define __ASM_RK2818_DMA_H

#include <asm/dma.h>


#define SAR 			0x000	/* Source Address Register */
#define DAR 			0x008	/* Destination Address Register */
#define LLP 			0x010	/* Linked List Pointer Register */
#define CTL_L 			0x018	/* Control Register LOW */
#define CTL_H			0x01C	/* Control Register HIGH */
#define CFG_L 			0x040	/* Configuration Register */
#define CFG_H 			0x044	/* Configuration Register */
#define SGR 			0x048	/* Source Gather Register */
#define DSR 			0x050	/* Destination Scatter Register */

#define RawTfr 			0x2c0 	/* Raw Status for IntTfr Interrupt */
#define RawBlock 		0x2c8 	/* Raw Status for IntBlock Interrupt */
#define RawSrcTran 		0x2d0 	/* Raw Status for IntSrcTran Interrupt */
#define RawDstTran 		0x2d8 	/* Raw Status for IntDstTran Interrupt */
#define RawErr 			0x2e0 	/* Raw Status for IntErr Interrupt */

#define StatusTfr		0x2e8 	/* Status for IntTfr Interrupt */
#define StatusBlock		0x2f0 	/* Status for IntBlock Interrupt */
#define StatusSrcTran		0x2f8 	/* Status for IntSrcTran Interrupt */
#define StatusDstTran		0x300 	/* Status for IntDstTran Interrupt */
#define StatusErr		0x308 	/* Status for IntErr Interrupt */

#define MaskTfr			0x310	/*Mask for IntTfr Interrupt */
#define MaskBlock 		0x318	/*Mask for IntBlock Interrupt */
#define MaskSrcTran 		0x320	/*Mask for IntSrcTran Interrupt */
#define MaskDstTran 		0x328	/*Mask for IntDstTran Interrupt */
#define MaskErr 		0x330	/*Mask for IntErr Interrupt */

#define ClearTfr		0x338 	/* Clear for IntTfr Interrupt */
#define ClearBlock 		0x340 	/* Clear for IntBlock Interrupt */
#define ClearSrcTran 		0x348 	/* Clear for IntSrcTran Interrupt */
#define ClearDstTran 		0x350 	/* Clear for IntDstTran Interrupt */
#define ClearErr 		0x358 	/* Clear for IntErr Interrupt */
#define StatusInt 		0x360 	/* Status for each interrupt type */

#define DmaCfgReg 		0x398 	/* DMA Configuration Register */
#define ChEnReg 		0x3a0 	/* DMA Channel Enable Register */

/* Detail CFG_L  Register Description */
#define CH_PRIOR_MASK     	(0x7 << 5)
#define CH_PRIOR_OFFSET		5
#define CH_SUSP           	(0x1 << 8)
#define FIFO_EMPTY        	(0x1 << 9)
#define HS_SEL_DST         	(0x1 << 10)
#define HS_SEL_SRC         	(0x1 << 11)
#define LOCK_CH_L_MASK 		(0x3 << 12)
#define LOCK_CH_L_OFFSET	12
#define LOCK_B_L_MASK     	(0x3 << 14)
#define LOCK_B_L_OFFSET		14
#define LOCK_CH			(0x1 << 16)
#define LOCK_B         		(0x1 << 17)
#define DST_HS_POL      	(0x1 << 18)
#define SRC_HS_POL      	(0x1 << 19)
#define MAX_ABRST_MASK		(0x3FF << 20)
#define MAX_ABRST_OFFSET	20
#define RELOAD_SRC        	(0x1 << 30)
#define RELOAD_DST        	(0x1 << 31)

/* Detail CFG_H  Register Description */
#define FCMODE			(0x1 << 0)
#define FIFO_MODE		(0x1 << 1)
#define PROTCTL_MASK		(0x7 << 2)
#define PROTCTL_OFFSET		2
#define DS_UPD_EN		(0x1 << 5)
#define SS_UPD_EN		(0x1 << 6)
#define SRC_PER_MASK		(0xF << 7)
#define SRC_PER_OFFSET		7
#define DST_PER_MASK		(0xF << 11)
#define DST_PER_OFFSET		11

/* Detail CTL_L  Register Description */
#define INT_EN            	(0x1 << 0)
#define DST_TR_WIDTH_MASK    	(0x7 << 1)
#define DST_TR_WIDTH_OFFSET	1
#define SRC_TR_WIDTH_MASK    	(0x7 << 4)
#define SRC_TR_WIDTH_OFFSET	4
#define DINC_MASK		(0x3 << 7)
#define DINC_OFFSET		7
#define SINC_MASK		(0x3 << 9)
#define SINC_OFFSET		9
#define DST_MSIZE_MASK		(0x7 << 11)
#define DST_MSIZE_OFFSET	11
#define SRC_MSIZE_MASK		(0x7 << 14)
#define SRC_MSIZE_OFFSET	14
#define SRC_GATHER_EN		(0x1 << 17)
#define DST_SCATTER_EN		(0x1 << 18)
#define TT_FC_MASK		(0x7 << 20)
#define TT_FC_OFFSET		20
#define DMS_MASK		(0x3 << 23)
#define DMS_OFFSET		23
#define SMS_MASK		(0x3 << 25)
#define SMS_OFFSET		25
#define LLP_DST_EN		(0x1 << 27)
#define LLP_SRC_EN		(0x1 << 28)

#define AHBMASTER_1		0x0
#define AHBMASTER_2		0x1

#define INCREMENT		0x0
#define DECREMENT		0x1
#define NOCHANGE		0x2

#define MSIZE_1			0x0
#define MSIZE_4			0x1
#define MSIZE_8			0x2
#define MSIZE_16		0x3
#define MSIZE_32		0x4

#define TR_WIDTH_8		0x0
#define TR_WIDTH_16		0x1
#define TR_WIDTH_32		0x2

#define M2M			0x0
#define M2P			0x1
#define P2M			0x2
#define P2P			0x3

/* Detail ChEnReg  Register Description */
#define CH_EN_MASK		(0xF << 0)
#define CH_EN_OFFSET		0
#define CH_EN_WE_MASK		(0xF << 8)
#define CH_EN_WE_OFFSET		8


/* Detail DmaCfgReg  Register Description */
#define DMA_EN			(0x1 << 0)


#define ROCKCHIP_DMA_CHANNELS  3
typedef enum {
	DMA_PRIO_HIGH = 0,
	DMA_PRIO_MEDIUM = 1,
	DMA_PRIO_LOW = 2
} rockchip_dma_prio;

struct rockchip_dma_channel {
	const char *name;
	void (*irq_handler) (int, void *);
	void (*err_handler) (int, void *, int errcode);
	void *data;
	unsigned int  dma_mode;
	struct scatterlist *sg;
	unsigned int sgbc;
	unsigned int sgcount;
	unsigned int resbytes;
	dma_addr_t LLI;
	void *dma_vaddr;
	unsigned int curLLI;
	unsigned int lli_count;
	int dma_num;
	unsigned int channel_base;
};

struct LLI_INFO {
    unsigned int SARx;
    unsigned int DARx;
    unsigned int LLPx;
    unsigned int CTL_Lx;
    unsigned int CTL_Hx;
//    unsigned int SSATx;
//    unsigned int DSATx;
};

extern struct rockchip_dma_channel rockchip_dma_channels[ROCKCHIP_DMA_CHANNELS];

int rockchip_dma_setup_single(int dma_ch, dma_addr_t dma_address,
		     unsigned int dma_length, unsigned int dev_addr,
		     unsigned int  dmamode);

int rockchip_dma_setup_sg(int dma_ch,
		 struct scatterlist *sg, unsigned int sgcount, unsigned int dma_length,
		 unsigned int dev_addr, unsigned int  dmamode);

int rockchip_dma_setup_handlers(int dma_ch,
		void (*irq_handler) (int, void *),
		void (*err_handler) (int, void *, int), void *data);

void rockchip_dma_enable(int dma_ch);

void rockchip_dma_disable(int dma_ch);

int rockchip_dma_request(int dma_ch, const char *name);

void rockchip_dma_free(int dma_ch);

int rockchip_dma_request_by_prio(const char *name, rockchip_dma_prio prio);

#endif

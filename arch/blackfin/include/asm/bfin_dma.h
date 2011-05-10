/*
 * bfin_dma.h - Blackfin DMA defines/structures/etc...
 *
 * Copyright 2004-2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __ASM_BFIN_DMA_H__
#define __ASM_BFIN_DMA_H__

#include <linux/types.h>

/* DMA_CONFIG Masks */
#define DMAEN			0x0001	/* DMA Channel Enable */
#define WNR				0x0002	/* Channel Direction (W/R*) */
#define WDSIZE_8		0x0000	/* Transfer Word Size = 8 */
#define WDSIZE_16		0x0004	/* Transfer Word Size = 16 */
#define WDSIZE_32		0x0008	/* Transfer Word Size = 32 */
#define DMA2D			0x0010	/* DMA Mode (2D/1D*) */
#define RESTART			0x0020	/* DMA Buffer Clear */
#define DI_SEL			0x0040	/* Data Interrupt Timing Select */
#define DI_EN			0x0080	/* Data Interrupt Enable */
#define NDSIZE_0		0x0000	/* Next Descriptor Size = 0 (Stop/Autobuffer) */
#define NDSIZE_1		0x0100	/* Next Descriptor Size = 1 */
#define NDSIZE_2		0x0200	/* Next Descriptor Size = 2 */
#define NDSIZE_3		0x0300	/* Next Descriptor Size = 3 */
#define NDSIZE_4		0x0400	/* Next Descriptor Size = 4 */
#define NDSIZE_5		0x0500	/* Next Descriptor Size = 5 */
#define NDSIZE_6		0x0600	/* Next Descriptor Size = 6 */
#define NDSIZE_7		0x0700	/* Next Descriptor Size = 7 */
#define NDSIZE_8		0x0800	/* Next Descriptor Size = 8 */
#define NDSIZE_9		0x0900	/* Next Descriptor Size = 9 */
#define NDSIZE			0x0f00	/* Next Descriptor Size */
#define DMAFLOW			0x7000	/* Flow Control */
#define DMAFLOW_STOP	0x0000	/* Stop Mode */
#define DMAFLOW_AUTO	0x1000	/* Autobuffer Mode */
#define DMAFLOW_ARRAY	0x4000	/* Descriptor Array Mode */
#define DMAFLOW_SMALL	0x6000	/* Small Model Descriptor List Mode */
#define DMAFLOW_LARGE	0x7000	/* Large Model Descriptor List Mode */

/* DMA_IRQ_STATUS Masks */
#define DMA_DONE		0x0001	/* DMA Completion Interrupt Status */
#define DMA_ERR			0x0002	/* DMA Error Interrupt Status */
#define DFETCH			0x0004	/* DMA Descriptor Fetch Indicator */
#define DMA_RUN			0x0008	/* DMA Channel Running Indicator */

/*
 * All Blackfin system MMRs are padded to 32bits even if the register
 * itself is only 16bits.  So use a helper macro to streamline this.
 */
#define __BFP(m) u16 m; u16 __pad_##m

/*
 * bfin dma registers layout
 */
struct bfin_dma_regs {
	u32 next_desc_ptr;
	u32 start_addr;
	__BFP(config);
	u32 __pad0;
	__BFP(x_count);
	__BFP(x_modify);
	__BFP(y_count);
	__BFP(y_modify);
	u32 curr_desc_ptr;
	u32 curr_addr;
	__BFP(irq_status);
	__BFP(peripheral_map);
	__BFP(curr_x_count);
	u32 __pad1;
	__BFP(curr_y_count);
	u32 __pad2;
};

/*
 * bfin handshake mdma registers layout
 */
struct bfin_hmdma_regs {
	__BFP(control);
	__BFP(ecinit);
	__BFP(bcinit);
	__BFP(ecurgent);
	__BFP(ecoverflow);
	__BFP(ecount);
	__BFP(bcount);
};

#undef __BFP

#endif

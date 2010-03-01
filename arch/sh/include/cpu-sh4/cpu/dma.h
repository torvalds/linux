#ifndef __ASM_CPU_SH4_DMA_H
#define __ASM_CPU_SH4_DMA_H

/* SH7751/7760/7780 DMA IRQ sources */

#ifdef CONFIG_CPU_SH4A

#define DMAOR_INIT	(DMAOR_DME)

#include <cpu/dma-sh4a.h>
#else /* CONFIG_CPU_SH4A */
/*
 * SH7750/SH7751/SH7760
 */
#define DMTE0_IRQ	34
#define DMTE4_IRQ	44
#define DMTE6_IRQ	46
#define DMAE0_IRQ	38

#define DMAOR_INIT	(0x8000|DMAOR_DME)
#define SH_DMAC_BASE0	0xffa00000
#define SH_DMAC_BASE1	0xffa00070
/* Definitions for the SuperH DMAC */
#define TM_BURST	0x00000080
#define TS_8		0x00000010
#define TS_16		0x00000020
#define TS_32		0x00000030
#define TS_64		0x00000000

#define CHCR_TS_LOW_MASK	0x70
#define CHCR_TS_LOW_SHIFT	4
#define CHCR_TS_HIGH_MASK	0
#define CHCR_TS_HIGH_SHIFT	0

#define DMAOR_COD	0x00000008

/*
 * The SuperH DMAC supports a number of transmit sizes, we list them here,
 * with their respective values as they appear in the CHCR registers.
 *
 * Defaults to a 64-bit transfer size.
 */
enum {
	XMIT_SZ_8BIT	= 1,
	XMIT_SZ_16BIT	= 2,
	XMIT_SZ_32BIT	= 3,
	XMIT_SZ_64BIT	= 0,
	XMIT_SZ_256BIT	= 4,
};

/*
 * The DMA count is defined as the number of bytes to transfer.
 */
#define TS_SHIFT {			\
	[XMIT_SZ_8BIT]		= 0,	\
	[XMIT_SZ_16BIT]		= 1,	\
	[XMIT_SZ_32BIT]		= 2,	\
	[XMIT_SZ_64BIT]		= 3,	\
	[XMIT_SZ_256BIT]	= 5,	\
}

#define TS_INDEX2VAL(i)	(((i) & 7) << CHCR_TS_LOW_SHIFT)

#endif

#endif /* __ASM_CPU_SH4_DMA_H */

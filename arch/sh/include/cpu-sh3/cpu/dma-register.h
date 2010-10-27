/*
 * SH3 CPU-specific DMA definitions, used by both DMA drivers
 *
 * Copyright (C) 2010 Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef CPU_DMA_REGISTER_H
#define CPU_DMA_REGISTER_H

#define CHCR_TS_LOW_MASK	0x18
#define CHCR_TS_LOW_SHIFT	3
#define CHCR_TS_HIGH_MASK	0
#define CHCR_TS_HIGH_SHIFT	0

#define DMAOR_INIT	DMAOR_DME

/*
 * The SuperH DMAC supports a number of transmit sizes, we list them here,
 * with their respective values as they appear in the CHCR registers.
 */
enum {
	XMIT_SZ_8BIT,
	XMIT_SZ_16BIT,
	XMIT_SZ_32BIT,
	XMIT_SZ_128BIT,
};

/* log2(size / 8) - used to calculate number of transfers */
#define TS_SHIFT {			\
	[XMIT_SZ_8BIT]		= 0,	\
	[XMIT_SZ_16BIT]		= 1,	\
	[XMIT_SZ_32BIT]		= 2,	\
	[XMIT_SZ_128BIT]	= 4,	\
}

#define TS_INDEX2VAL(i)	(((i) & 3) << CHCR_TS_LOW_SHIFT)

#endif

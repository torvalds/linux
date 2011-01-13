/*
 * SH4 CPU-specific DMA definitions, used by both DMA drivers
 *
 * Copyright (C) 2010 Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef CPU_DMA_REGISTER_H
#define CPU_DMA_REGISTER_H

/* SH7751/7760/7780 DMA IRQ sources */

#ifdef CONFIG_CPU_SH4A

#define DMAOR_INIT	DMAOR_DME

#if defined(CONFIG_CPU_SUBTYPE_SH7343) || \
	defined(CONFIG_CPU_SUBTYPE_SH7730)
#define CHCR_TS_LOW_MASK	0x00000018
#define CHCR_TS_LOW_SHIFT	3
#define CHCR_TS_HIGH_MASK	0
#define CHCR_TS_HIGH_SHIFT	0
#elif defined(CONFIG_CPU_SUBTYPE_SH7722) || \
	defined(CONFIG_CPU_SUBTYPE_SH7724) || \
	defined(CONFIG_CPU_SUBTYPE_SH7786)
#define CHCR_TS_LOW_MASK	0x00000018
#define CHCR_TS_LOW_SHIFT	3
#define CHCR_TS_HIGH_MASK	0x00300000
#define CHCR_TS_HIGH_SHIFT	(20 - 2)	/* 2 bits for shifted low TS */
#elif defined(CONFIG_CPU_SUBTYPE_SH7763) || \
	defined(CONFIG_CPU_SUBTYPE_SH7764)
#define CHCR_TS_LOW_MASK	0x00000018
#define CHCR_TS_LOW_SHIFT	3
#define CHCR_TS_HIGH_MASK	0
#define CHCR_TS_HIGH_SHIFT	0
#elif defined(CONFIG_CPU_SUBTYPE_SH7723)
#define CHCR_TS_LOW_MASK	0x00000018
#define CHCR_TS_LOW_SHIFT	3
#define CHCR_TS_HIGH_MASK	0
#define CHCR_TS_HIGH_SHIFT	0
#elif defined(CONFIG_CPU_SUBTYPE_SH7780)
#define CHCR_TS_LOW_MASK	0x00000018
#define CHCR_TS_LOW_SHIFT	3
#define CHCR_TS_HIGH_MASK	0
#define CHCR_TS_HIGH_SHIFT	0
#else /* SH7785 */
#define CHCR_TS_LOW_MASK	0x00000018
#define CHCR_TS_LOW_SHIFT	3
#define CHCR_TS_HIGH_MASK	0
#define CHCR_TS_HIGH_SHIFT	0
#endif

/* Transmit sizes and respective CHCR register values */
enum {
	XMIT_SZ_8BIT		= 0,
	XMIT_SZ_16BIT		= 1,
	XMIT_SZ_32BIT		= 2,
	XMIT_SZ_64BIT		= 7,
	XMIT_SZ_128BIT		= 3,
	XMIT_SZ_256BIT		= 4,
	XMIT_SZ_128BIT_BLK	= 0xb,
	XMIT_SZ_256BIT_BLK	= 0xc,
};

/* log2(size / 8) - used to calculate number of transfers */
#define TS_SHIFT {			\
	[XMIT_SZ_8BIT]		= 0,	\
	[XMIT_SZ_16BIT]		= 1,	\
	[XMIT_SZ_32BIT]		= 2,	\
	[XMIT_SZ_64BIT]		= 3,	\
	[XMIT_SZ_128BIT]	= 4,	\
	[XMIT_SZ_256BIT]	= 5,	\
	[XMIT_SZ_128BIT_BLK]	= 4,	\
	[XMIT_SZ_256BIT_BLK]	= 5,	\
}

#define TS_INDEX2VAL(i)	((((i) & 3) << CHCR_TS_LOW_SHIFT) | \
			 (((i) & 0xc) << CHCR_TS_HIGH_SHIFT))

#else /* CONFIG_CPU_SH4A */

#define DMAOR_INIT	(0x8000 | DMAOR_DME)

#define CHCR_TS_LOW_MASK	0x70
#define CHCR_TS_LOW_SHIFT	4
#define CHCR_TS_HIGH_MASK	0
#define CHCR_TS_HIGH_SHIFT	0

/* Transmit sizes and respective CHCR register values */
enum {
	XMIT_SZ_8BIT	= 1,
	XMIT_SZ_16BIT	= 2,
	XMIT_SZ_32BIT	= 3,
	XMIT_SZ_64BIT	= 0,
	XMIT_SZ_256BIT	= 4,
};

/* log2(size / 8) - used to calculate number of transfers */
#define TS_SHIFT {			\
	[XMIT_SZ_8BIT]		= 0,	\
	[XMIT_SZ_16BIT]		= 1,	\
	[XMIT_SZ_32BIT]		= 2,	\
	[XMIT_SZ_64BIT]		= 3,	\
	[XMIT_SZ_256BIT]	= 5,	\
}

#define TS_INDEX2VAL(i)	(((i) & 7) << CHCR_TS_LOW_SHIFT)

#endif /* CONFIG_CPU_SH4A */

#endif

/*
 * include/asm-sh/dreamcast/dma.h
 *
 * Copyright (C) 2003 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_SH_DREAMCAST_DMA_H
#define __ASM_SH_DREAMCAST_DMA_H

/* Number of DMA channels */
#define ONCHIP_NR_DMA_CHANNELS	4
#define G2_NR_DMA_CHANNELS	4
#define PVR2_NR_DMA_CHANNELS	1

/* Channels for cascading */
#define PVR2_CASCADE_CHAN	2
#define G2_CASCADE_CHAN		3

/* PVR2 DMA Registers */
#define PVR2_DMA_BASE		0xa05f6800
#define PVR2_DMA_ADDR		(PVR2_DMA_BASE + 0)
#define PVR2_DMA_COUNT		(PVR2_DMA_BASE + 4)
#define PVR2_DMA_MODE		(PVR2_DMA_BASE + 8)
#define PVR2_DMA_LMMODE0	(PVR2_DMA_BASE + 132)
#define PVR2_DMA_LMMODE1	(PVR2_DMA_BASE + 136)

/* G2 DMA Register */
#define G2_DMA_BASE		0xa05f7800

#endif /* __ASM_SH_DREAMCAST_DMA_H */


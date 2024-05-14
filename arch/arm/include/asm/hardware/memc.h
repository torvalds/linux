/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/hardware/memc.h
 *
 *  Copyright (C) Russell King.
 */
#define VDMA_ALIGNMENT	PAGE_SIZE
#define VDMA_XFERSIZE	16
#define VDMA_INIT	0
#define VDMA_START	1
#define VDMA_END	2

#ifndef __ASSEMBLY__
extern void memc_write(unsigned int reg, unsigned long val);

#define video_set_dma(start,end,offset)				\
do {								\
	memc_write (VDMA_START, (start >> 2));			\
	memc_write (VDMA_END, (end - VDMA_XFERSIZE) >> 2);	\
	memc_write (VDMA_INIT, (offset >> 2));			\
} while (0)

#endif

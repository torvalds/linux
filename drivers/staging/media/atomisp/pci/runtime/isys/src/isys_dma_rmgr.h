/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2015, Intel Corporation.
 */

#ifndef __ISYS_DMA_RMGR_H_INCLUDED__
#define __ISYS_DMA_RMGR_H_INCLUDED__

typedef struct isys_dma_rsrc_s	isys_dma_rsrc_t;
struct isys_dma_rsrc_s {
	u32 active_table;
	u16 num_active;
};

#endif /* __ISYS_DMA_RMGR_H_INCLUDED__ */

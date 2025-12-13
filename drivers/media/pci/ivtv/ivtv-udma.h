/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2004  Chris Kennedy <c@groovy.org>
    Copyright (C) 2006-2007  Hans Verkuil <hverkuil@kernel.org>

 */

#ifndef IVTV_UDMA_H
#define IVTV_UDMA_H

/* User DMA functions */
void ivtv_udma_get_page_info(struct ivtv_dma_page_info *dma_page, unsigned long first, unsigned long size);
int ivtv_udma_fill_sg_list(struct ivtv_user_dma *dma, struct ivtv_dma_page_info *dma_page, int map_offset);
void ivtv_udma_fill_sg_array(struct ivtv_user_dma *dma, u32 buffer_offset, u32 buffer_offset_2, u32 split);
int ivtv_udma_setup(struct ivtv *itv, unsigned long ivtv_dest_addr,
		       void __user *userbuf, int size_in_bytes);
void ivtv_udma_unmap(struct ivtv *itv);
void ivtv_udma_free(struct ivtv *itv);
void ivtv_udma_alloc(struct ivtv *itv);
void ivtv_udma_prepare(struct ivtv *itv);
void ivtv_udma_start(struct ivtv *itv);

static inline void ivtv_udma_sync_for_device(struct ivtv *itv)
{
	dma_sync_single_for_device(&itv->pdev->dev, itv->udma.SG_handle,
				   sizeof(itv->udma.SGarray), DMA_TO_DEVICE);
}

static inline void ivtv_udma_sync_for_cpu(struct ivtv *itv)
{
	dma_sync_single_for_cpu(&itv->pdev->dev, itv->udma.SG_handle,
				sizeof(itv->udma.SGarray), DMA_TO_DEVICE);
}

#endif

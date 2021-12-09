/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * DMA-BUF sysfs statistics.
 *
 * Copyright (C) 2021 Google LLC.
 */

#ifndef _DMA_BUF_SYSFS_STATS_H
#define _DMA_BUF_SYSFS_STATS_H

#ifdef CONFIG_DMABUF_SYSFS_STATS

int dma_buf_init_sysfs_statistics(void);
void dma_buf_uninit_sysfs_statistics(void);

int dma_buf_stats_setup(struct dma_buf *dmabuf);

void dma_buf_stats_teardown(struct dma_buf *dmabuf);
#else

static inline int dma_buf_init_sysfs_statistics(void)
{
	return 0;
}

static inline void dma_buf_uninit_sysfs_statistics(void) {}

static inline int dma_buf_stats_setup(struct dma_buf *dmabuf)
{
	return 0;
}

static inline void dma_buf_stats_teardown(struct dma_buf *dmabuf) {}
#endif
#endif // _DMA_BUF_SYSFS_STATS_H

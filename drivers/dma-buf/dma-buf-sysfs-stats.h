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
int dma_buf_attach_stats_setup(struct dma_buf_attachment *attach,
			       unsigned int uid);
static inline void dma_buf_update_attachment_map_count(struct dma_buf_attachment *attach,
						       int delta)
{
	struct dma_buf_attach_sysfs_entry *entry = attach->sysfs_entry;

	entry->map_counter += delta;
}
void dma_buf_stats_teardown(struct dma_buf *dmabuf);
void dma_buf_attach_stats_teardown(struct dma_buf_attachment *attach);
static inline unsigned int dma_buf_update_attach_uid(struct dma_buf *dmabuf)
{
	struct dma_buf_sysfs_entry *entry = dmabuf->sysfs_entry;

	return entry->attachment_uid++;
}
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
static inline int dma_buf_attach_stats_setup(struct dma_buf_attachment *attach,
					     unsigned int uid)
{
	return 0;
}

static inline void dma_buf_stats_teardown(struct dma_buf *dmabuf) {}
static inline void dma_buf_attach_stats_teardown(struct dma_buf_attachment *attach) {}
static inline void dma_buf_update_attachment_map_count(struct dma_buf_attachment *attach,
						       int delta) {}
static inline unsigned int dma_buf_update_attach_uid(struct dma_buf *dmabuf)
{
	return 0;
}
#endif
#endif // _DMA_BUF_SYSFS_STATS_H

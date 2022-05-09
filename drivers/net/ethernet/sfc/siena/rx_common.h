/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2018 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_RX_COMMON_H
#define EFX_RX_COMMON_H

/* Preferred number of descriptors to fill at once */
#define EFX_RX_PREFERRED_BATCH 8U

/* Each packet can consume up to ceil(max_frame_len / buffer_size) buffers */
#define EFX_RX_MAX_FRAGS DIV_ROUND_UP(EFX_MAX_FRAME_LEN(EFX_MAX_MTU), \
				      EFX_RX_USR_BUF_SIZE)

/* Number of RX buffers to recycle pages for.  When creating the RX page recycle
 * ring, this number is divided by the number of buffers per page to calculate
 * the number of pages to store in the RX page recycle ring.
 */
#define EFX_RECYCLE_RING_SIZE_10G	256

static inline u8 *efx_rx_buf_va(struct efx_rx_buffer *buf)
{
	return page_address(buf->page) + buf->page_offset;
}

static inline u32 efx_rx_buf_hash(struct efx_nic *efx, const u8 *eh)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	return __le32_to_cpup((const __le32 *)(eh + efx->rx_packet_hash_offset));
#else
	const u8 *data = eh + efx->rx_packet_hash_offset;

	return (u32)data[0]	  |
	       (u32)data[1] << 8  |
	       (u32)data[2] << 16 |
	       (u32)data[3] << 24;
#endif
}

void efx_rx_slow_fill(struct timer_list *t);

void efx_recycle_rx_pages(struct efx_channel *channel,
			  struct efx_rx_buffer *rx_buf,
			  unsigned int n_frags);
void efx_discard_rx_packet(struct efx_channel *channel,
			   struct efx_rx_buffer *rx_buf,
			   unsigned int n_frags);

int efx_probe_rx_queue(struct efx_rx_queue *rx_queue);
void efx_init_rx_queue(struct efx_rx_queue *rx_queue);
void efx_fini_rx_queue(struct efx_rx_queue *rx_queue);
void efx_remove_rx_queue(struct efx_rx_queue *rx_queue);
void efx_destroy_rx_queue(struct efx_rx_queue *rx_queue);

void efx_init_rx_buffer(struct efx_rx_queue *rx_queue,
			struct page *page,
			unsigned int page_offset,
			u16 flags);
void efx_unmap_rx_buffer(struct efx_nic *efx, struct efx_rx_buffer *rx_buf);

static inline void efx_sync_rx_buffer(struct efx_nic *efx,
				      struct efx_rx_buffer *rx_buf,
				      unsigned int len)
{
	dma_sync_single_for_cpu(&efx->pci_dev->dev, rx_buf->dma_addr, len,
				DMA_FROM_DEVICE);
}

void efx_free_rx_buffers(struct efx_rx_queue *rx_queue,
			 struct efx_rx_buffer *rx_buf,
			 unsigned int num_bufs);

void efx_schedule_slow_fill(struct efx_rx_queue *rx_queue);
void efx_rx_config_page_split(struct efx_nic *efx);
void efx_fast_push_rx_descriptors(struct efx_rx_queue *rx_queue, bool atomic);

void
efx_siena_rx_packet_gro(struct efx_channel *channel,
			struct efx_rx_buffer *rx_buf,
			unsigned int n_frags, u8 *eh, __wsum csum);

struct efx_rss_context *efx_alloc_rss_context_entry(struct efx_nic *efx);
struct efx_rss_context *efx_find_rss_context_entry(struct efx_nic *efx, u32 id);
void efx_free_rss_context_entry(struct efx_rss_context *ctx);
void efx_set_default_rx_indir_table(struct efx_nic *efx,
				    struct efx_rss_context *ctx);

bool efx_filter_is_mc_recipient(const struct efx_filter_spec *spec);
bool efx_filter_spec_equal(const struct efx_filter_spec *left,
			   const struct efx_filter_spec *right);
u32 efx_filter_spec_hash(const struct efx_filter_spec *spec);

#ifdef CONFIG_RFS_ACCEL
bool efx_rps_check_rule(struct efx_arfs_rule *rule, unsigned int filter_idx,
			bool *force);
struct efx_arfs_rule *efx_rps_hash_find(struct efx_nic *efx,
					const struct efx_filter_spec *spec);
struct efx_arfs_rule *efx_rps_hash_add(struct efx_nic *efx,
				       const struct efx_filter_spec *spec,
				       bool *new);
void efx_rps_hash_del(struct efx_nic *efx, const struct efx_filter_spec *spec);

int efx_filter_rfs(struct net_device *net_dev, const struct sk_buff *skb,
		   u16 rxq_index, u32 flow_id);
bool __efx_filter_rfs_expire(struct efx_channel *channel, unsigned int quota);
#endif

int efx_probe_filters(struct efx_nic *efx);
void efx_remove_filters(struct efx_nic *efx);

#endif

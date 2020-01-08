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

void efx_rx_slow_fill(struct timer_list *t);

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
void efx_free_rx_buffers(struct efx_rx_queue *rx_queue,
			 struct efx_rx_buffer *rx_buf,
			 unsigned int num_bufs);

void efx_schedule_slow_fill(struct efx_rx_queue *rx_queue);
void efx_rx_config_page_split(struct efx_nic *efx);
void efx_fast_push_rx_descriptors(struct efx_rx_queue *rx_queue, bool atomic);

#endif

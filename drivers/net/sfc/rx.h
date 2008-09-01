/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2006 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_RX_H
#define EFX_RX_H

#include "net_driver.h"

int efx_probe_rx_queue(struct efx_rx_queue *rx_queue);
void efx_remove_rx_queue(struct efx_rx_queue *rx_queue);
void efx_init_rx_queue(struct efx_rx_queue *rx_queue);
void efx_fini_rx_queue(struct efx_rx_queue *rx_queue);

int efx_lro_init(struct net_lro_mgr *lro_mgr, struct efx_nic *efx);
void efx_lro_fini(struct net_lro_mgr *lro_mgr);
void efx_flush_lro(struct efx_channel *channel);
void efx_rx_strategy(struct efx_channel *channel);
void efx_fast_push_rx_descriptors(struct efx_rx_queue *rx_queue);
void efx_rx_work(struct work_struct *data);
void __efx_rx_packet(struct efx_channel *channel,
		     struct efx_rx_buffer *rx_buf, bool checksummed);

#endif /* EFX_RX_H */

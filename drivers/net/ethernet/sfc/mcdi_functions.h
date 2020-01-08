/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2018 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */
#ifndef EFX_MCDI_FUNCTIONS_H
#define EFX_MCDI_FUNCTIONS_H

int efx_mcdi_alloc_vis(struct efx_nic *efx, unsigned int min_vis,
		       unsigned int max_vis, unsigned int *vi_base,
		       unsigned int *allocated_vis);
int efx_mcdi_free_vis(struct efx_nic *efx);

int efx_mcdi_ev_probe(struct efx_channel *channel);
int efx_mcdi_ev_init(struct efx_channel *channel, bool v1_cut_thru, bool v2);
void efx_mcdi_ev_remove(struct efx_channel *channel);
void efx_mcdi_ev_fini(struct efx_channel *channel);
int efx_mcdi_tx_init(struct efx_tx_queue *tx_queue, bool tso_v2);
void efx_mcdi_tx_remove(struct efx_tx_queue *tx_queue);
void efx_mcdi_tx_fini(struct efx_tx_queue *tx_queue);
int efx_mcdi_rx_probe(struct efx_rx_queue *rx_queue);
int efx_mcdi_rx_init(struct efx_rx_queue *rx_queue, bool want_outer_classes);
void efx_mcdi_rx_remove(struct efx_rx_queue *rx_queue);
void efx_mcdi_rx_fini(struct efx_rx_queue *rx_queue);

#endif

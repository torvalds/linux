/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2018 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_CHANNELS_H
#define EFX_CHANNELS_H

extern unsigned int efx_siena_interrupt_mode;
extern unsigned int efx_siena_rss_cpus;

int efx_siena_probe_interrupts(struct efx_nic *efx);
void efx_siena_remove_interrupts(struct efx_nic *efx);
int efx_siena_enable_interrupts(struct efx_nic *efx);
void efx_siena_disable_interrupts(struct efx_nic *efx);

void efx_siena_set_interrupt_affinity(struct efx_nic *efx);
void efx_siena_clear_interrupt_affinity(struct efx_nic *efx);

void efx_siena_start_eventq(struct efx_channel *channel);
void efx_siena_stop_eventq(struct efx_channel *channel);

int efx_siena_realloc_channels(struct efx_nic *efx, u32 rxq_entries,
			       u32 txq_entries);
void efx_siena_set_channel_names(struct efx_nic *efx);
int efx_siena_init_channels(struct efx_nic *efx);
int efx_siena_probe_channels(struct efx_nic *efx);
int efx_siena_set_channels(struct efx_nic *efx);
void efx_siena_remove_channel(struct efx_channel *channel);
void efx_siena_remove_channels(struct efx_nic *efx);
void efx_siena_fini_channels(struct efx_nic *efx);
void efx_siena_start_channels(struct efx_nic *efx);
void efx_siena_stop_channels(struct efx_nic *efx);

void efx_siena_init_napi(struct efx_nic *efx);
void efx_siena_fini_napi(struct efx_nic *efx);

void efx_siena_channel_dummy_op_void(struct efx_channel *channel);

#endif

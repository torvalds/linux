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

extern unsigned int efx_interrupt_mode;
extern unsigned int rss_cpus;

int efx_probe_interrupts(struct efx_nic *efx);
void efx_remove_interrupts(struct efx_nic *efx);
int efx_soft_enable_interrupts(struct efx_nic *efx);
void efx_soft_disable_interrupts(struct efx_nic *efx);
int efx_enable_interrupts(struct efx_nic *efx);
void efx_disable_interrupts(struct efx_nic *efx);

void efx_set_interrupt_affinity(struct efx_nic *efx);
void efx_clear_interrupt_affinity(struct efx_nic *efx);

int efx_probe_eventq(struct efx_channel *channel);
int efx_init_eventq(struct efx_channel *channel);
void efx_start_eventq(struct efx_channel *channel);
void efx_stop_eventq(struct efx_channel *channel);
void efx_fini_eventq(struct efx_channel *channel);
void efx_remove_eventq(struct efx_channel *channel);

int efx_realloc_channels(struct efx_nic *efx, u32 rxq_entries, u32 txq_entries);
void efx_get_channel_name(struct efx_channel *channel, char *buf, size_t len);
void efx_set_channel_names(struct efx_nic *efx);
int efx_init_channels(struct efx_nic *efx);
int efx_probe_channels(struct efx_nic *efx);
int efx_set_channels(struct efx_nic *efx);
bool efx_default_channel_want_txqs(struct efx_channel *channel);
void efx_remove_channel(struct efx_channel *channel);
void efx_remove_channels(struct efx_nic *efx);
void efx_fini_channels(struct efx_nic *efx);
struct efx_channel *efx_copy_channel(const struct efx_channel *old_channel);
void efx_start_channels(struct efx_nic *efx);
void efx_stop_channels(struct efx_nic *efx);

void efx_init_napi_channel(struct efx_channel *channel);
void efx_init_napi(struct efx_nic *efx);
void efx_fini_napi_channel(struct efx_channel *channel);
void efx_fini_napi(struct efx_nic *efx);

int efx_channel_dummy_op_int(struct efx_channel *channel);
void efx_channel_dummy_op_void(struct efx_channel *channel);

#endif

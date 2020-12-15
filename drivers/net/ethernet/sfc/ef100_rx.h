/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2019 Solarflare Communications Inc.
 * Copyright 2019-2020 Xilinx Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_EF100_RX_H
#define EFX_EF100_RX_H

#include "net_driver.h"

bool ef100_rx_buf_hash_valid(const u8 *prefix);
void efx_ef100_ev_rx(struct efx_channel *channel, const efx_qword_t *p_event);
void ef100_rx_write(struct efx_rx_queue *rx_queue);
void __ef100_rx_packet(struct efx_channel *channel);

#endif

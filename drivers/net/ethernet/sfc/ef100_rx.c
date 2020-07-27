// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2019 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "net_driver.h"
#include "ef100_rx.h"
#include "rx_common.h"
#include "efx.h"

/* RX stubs */

void ef100_rx_write(struct efx_rx_queue *rx_queue)
{
}

void __ef100_rx_packet(struct efx_channel *channel)
{
	/* Stub.  No RX path yet.  Discard the buffer. */
	struct efx_rx_buffer *rx_buf = efx_rx_buffer(&channel->rx_queue,
						     channel->rx_pkt_index);
	struct efx_rx_queue *rx_queue = efx_channel_get_rx_queue(channel);

	efx_free_rx_buffers(rx_queue, rx_buf, 1);
	channel->rx_pkt_n_frags = 0;
}

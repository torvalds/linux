// SPDX-License-Identifier: GPL-2.0
//
// mcp251xfd - Microchip MCP251xFD Family CAN controller driver
//
// Copyright (c) 2021, 2022 Pengutronix,
//               Marc Kleine-Budde <kernel@pengutronix.de>
//

#include "mcp251xfd-ram.h"

static inline u8 can_ram_clamp(const struct can_ram_config *config,
			       const struct can_ram_obj_config *obj,
			       u8 val)
{
	u8 max;

	max = min_t(u8, obj->max, obj->fifo_num * config->fifo_depth);
	return clamp(val, obj->min, max);
}

static u8
can_ram_rounddown_pow_of_two(const struct can_ram_config *config,
			     const struct can_ram_obj_config *obj, u8 val)
{
	u8 fifo_num = obj->fifo_num;
	u8 ret = 0, i;

	val = can_ram_clamp(config, obj, val);

	for (i = 0; i < fifo_num && val; i++) {
		u8 n;

		n = min_t(u8, rounddown_pow_of_two(val),
			  config->fifo_depth);

		/* skip small FIFOs */
		if (n < obj->fifo_depth_min)
			return ret;

		ret += n;
		val -= n;
	}

	return ret;
}

void can_ram_get_layout(struct can_ram_layout *layout,
			const struct can_ram_config *config,
			const struct ethtool_ringparam *ring,
			const bool fd_mode)
{
	u8 num_rx, num_tx;
	u16 ram_free;

	/* default CAN */

	num_tx = config->tx.def[fd_mode];
	num_tx = can_ram_rounddown_pow_of_two(config, &config->tx, num_tx);

	ram_free = config->size;
	ram_free -= config->tx.size[fd_mode] * num_tx;

	num_rx = ram_free / config->rx.size[fd_mode];

	layout->default_rx = can_ram_rounddown_pow_of_two(config, &config->rx, num_rx);
	layout->default_tx = num_tx;

	/* MAX CAN */

	ram_free = config->size;
	ram_free -= config->tx.size[fd_mode] * config->tx.min;
	num_rx = ram_free / config->rx.size[fd_mode];

	ram_free = config->size;
	ram_free -= config->rx.size[fd_mode] * config->rx.min;
	num_tx = ram_free / config->tx.size[fd_mode];

	layout->max_rx = can_ram_rounddown_pow_of_two(config, &config->rx, num_rx);
	layout->max_tx = can_ram_rounddown_pow_of_two(config, &config->tx, num_tx);

	/* cur CAN */

	if (ring) {
		num_rx = can_ram_rounddown_pow_of_two(config, &config->rx, ring->rx_pending);

		ram_free = config->size - config->rx.size[fd_mode] * num_rx;
		num_tx = ram_free / config->tx.size[fd_mode];
		num_tx = min_t(u8, ring->tx_pending, num_tx);
		num_tx = can_ram_rounddown_pow_of_two(config, &config->tx, num_tx);

		layout->cur_rx = num_rx;
		layout->cur_tx = num_tx;
	} else {
		layout->cur_rx = layout->default_rx;
		layout->cur_tx = layout->default_tx;
	}
}

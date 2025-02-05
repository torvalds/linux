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
			     const struct can_ram_obj_config *obj,
			     const u8 coalesce, u8 val)
{
	u8 fifo_num = obj->fifo_num;
	u8 ret = 0, i;

	val = can_ram_clamp(config, obj, val);

	if (coalesce) {
		/* Use 1st FIFO for coalescing, if requested.
		 *
		 * Either use complete FIFO (and FIFO Full IRQ) for
		 * coalescing or only half of FIFO (FIFO Half Full
		 * IRQ) and use remaining half for normal objects.
		 */
		ret = min_t(u8, coalesce * 2, config->fifo_depth);
		val -= ret;
		fifo_num--;
	}

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
			const struct ethtool_coalesce *ec,
			const bool fd_mode)
{
	u8 num_rx, num_tx;
	u16 ram_free;

	/* default CAN */

	num_tx = config->tx.def[fd_mode];
	num_tx = can_ram_rounddown_pow_of_two(config, &config->tx, 0, num_tx);

	ram_free = config->size;
	ram_free -= config->tx.size[fd_mode] * num_tx;

	num_rx = ram_free / config->rx.size[fd_mode];

	layout->default_rx = can_ram_rounddown_pow_of_two(config, &config->rx, 0, num_rx);
	layout->default_tx = num_tx;

	/* MAX CAN */

	ram_free = config->size;
	ram_free -= config->tx.size[fd_mode] * config->tx.min;
	num_rx = ram_free / config->rx.size[fd_mode];

	ram_free = config->size;
	ram_free -= config->rx.size[fd_mode] * config->rx.min;
	num_tx = ram_free / config->tx.size[fd_mode];

	layout->max_rx = can_ram_rounddown_pow_of_two(config, &config->rx, 0, num_rx);
	layout->max_tx = can_ram_rounddown_pow_of_two(config, &config->tx, 0, num_tx);

	/* cur CAN */

	if (ring) {
		u8 num_rx_coalesce = 0, num_tx_coalesce = 0;

		/* If the ring parameters have been configured in
		 * CAN-CC mode, but and we are in CAN-FD mode now,
		 * they might be to big. Use the default CAN-FD values
		 * in this case.
		 */
		num_rx = ring->rx_pending;
		if (num_rx > layout->max_rx)
			num_rx = layout->default_rx;

		num_rx = can_ram_rounddown_pow_of_two(config, &config->rx, 0, num_rx);

		/* The ethtool doc says:
		 * To disable coalescing, set usecs = 0 and max_frames = 1.
		 */
		if (ec && !(ec->rx_coalesce_usecs_irq == 0 &&
			    ec->rx_max_coalesced_frames_irq == 1)) {
			u8 max;

			/* use only max half of available objects for coalescing */
			max = min_t(u8, num_rx / 2, config->fifo_depth);
			num_rx_coalesce = clamp(ec->rx_max_coalesced_frames_irq,
						(u32)config->rx.fifo_depth_coalesce_min,
						(u32)max);
			num_rx_coalesce = rounddown_pow_of_two(num_rx_coalesce);

			num_rx = can_ram_rounddown_pow_of_two(config, &config->rx,
							      num_rx_coalesce, num_rx);
		}

		ram_free = config->size - config->rx.size[fd_mode] * num_rx;
		num_tx = ram_free / config->tx.size[fd_mode];
		num_tx = min_t(u8, ring->tx_pending, num_tx);
		num_tx = can_ram_rounddown_pow_of_two(config, &config->tx, 0, num_tx);

		/* The ethtool doc says:
		 * To disable coalescing, set usecs = 0 and max_frames = 1.
		 */
		if (ec && !(ec->tx_coalesce_usecs_irq == 0 &&
			    ec->tx_max_coalesced_frames_irq == 1)) {
			u8 max;

			/* use only max half of available objects for coalescing */
			max = min_t(u8, num_tx / 2, config->fifo_depth);
			num_tx_coalesce = clamp(ec->tx_max_coalesced_frames_irq,
						(u32)config->tx.fifo_depth_coalesce_min,
						(u32)max);
			num_tx_coalesce = rounddown_pow_of_two(num_tx_coalesce);

			num_tx = can_ram_rounddown_pow_of_two(config, &config->tx,
							      num_tx_coalesce, num_tx);
		}

		layout->cur_rx = num_rx;
		layout->cur_tx = num_tx;
		layout->rx_coalesce = num_rx_coalesce;
		layout->tx_coalesce = num_tx_coalesce;
	} else {
		layout->cur_rx = layout->default_rx;
		layout->cur_tx = layout->default_tx;
		layout->rx_coalesce = 0;
		layout->tx_coalesce = 0;
	}
}

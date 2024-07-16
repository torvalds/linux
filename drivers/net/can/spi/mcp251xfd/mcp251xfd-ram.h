/* SPDX-License-Identifier: GPL-2.0
 *
 * mcp251xfd - Microchip MCP251xFD Family CAN controller driver
 *
 * Copyright (c) 2021, 2022 Pengutronix,
 *               Marc Kleine-Budde <kernel@pengutronix.de>
 */

#ifndef _MCP251XFD_RAM_H
#define _MCP251XFD_RAM_H

#include <linux/ethtool.h>

#define CAN_RAM_NUM_MAX (-1)

enum can_ram_mode {
	CAN_RAM_MODE_CAN,
	CAN_RAM_MODE_CANFD,
	__CAN_RAM_MODE_MAX
};

struct can_ram_obj_config {
	u8 size[__CAN_RAM_MODE_MAX];

	u8 def[__CAN_RAM_MODE_MAX];
	u8 min;
	u8 max;

	u8 fifo_num;
	u8 fifo_depth_min;
	u8 fifo_depth_coalesce_min;
};

struct can_ram_config {
	const struct can_ram_obj_config rx;
	const struct can_ram_obj_config tx;

	u16 size;
	u8 fifo_depth;
};

struct can_ram_layout {
	u8 default_rx;
	u8 default_tx;

	u8 max_rx;
	u8 max_tx;

	u8 cur_rx;
	u8 cur_tx;

	u8 rx_coalesce;
	u8 tx_coalesce;
};

void can_ram_get_layout(struct can_ram_layout *layout,
			const struct can_ram_config *config,
			const struct ethtool_ringparam *ring,
			const struct ethtool_coalesce *ec,
			const bool fd_mode);

#endif

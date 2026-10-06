// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2026 Microchip Technology Inc. and its subsidiaries.
 */

#include "sparx5_main.h"

enum sparx5_tas_link_speed {
	TAS_SPEED_NO_GB,
	TAS_SPEED_10,
	TAS_SPEED_100,
	TAS_SPEED_1000,
	TAS_SPEED_2500,
	TAS_SPEED_5000,
	TAS_SPEED_10000,
	TAS_SPEED_25000,
};

void sparx5_tas_speed(struct sparx5_port *port, int speed)
{
	struct sparx5 *sparx5 = port->sparx5;
	u8 spd;

	switch (speed) {
	case SPEED_10:
		spd = TAS_SPEED_10;
		break;
	case SPEED_100:
		spd = TAS_SPEED_100;
		break;
	case SPEED_1000:
		spd = TAS_SPEED_1000;
		break;
	case SPEED_2500:
		spd = TAS_SPEED_2500;
		break;
	case SPEED_5000:
		spd = TAS_SPEED_5000;
		break;
	case SPEED_10000:
		spd = TAS_SPEED_10000;
		break;
	case SPEED_25000:
		spd = TAS_SPEED_25000;
		break;
	default:
		netdev_err(port->ndev, "TAS: Unsupported speed: %d\n", speed);
		return;
	}

	spx5_rmw(HSCH_TAS_PROFILE_CONFIG_LINK_SPEED_SET(spd),
		 HSCH_TAS_PROFILE_CONFIG_LINK_SPEED,
		 sparx5,
		 HSCH_TAS_PROFILE_CONFIG(port->portno));
}

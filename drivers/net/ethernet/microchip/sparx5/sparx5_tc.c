// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#include "sparx5_tc.h"
#include "sparx5_main.h"

int sparx5_port_setup_tc(struct net_device *ndev, enum tc_setup_type type,
			 void *type_data)
{
	switch (type) {
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

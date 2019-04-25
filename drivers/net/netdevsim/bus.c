// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2017 Netronome Systems, Inc.
 * Copyright (C) 2019 Mellanox Technologies. All rights reserved
 */

#include <linux/device.h>

#include "netdevsim.h"

struct bus_type nsim_bus = {
	.name		= DRV_NAME,
	.dev_name	= DRV_NAME,
	.num_vf		= nsim_num_vf,
};

int nsim_bus_init(void)
{
	return bus_register(&nsim_bus);
}

void nsim_bus_exit(void)
{
	bus_unregister(&nsim_bus);
}

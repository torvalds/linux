/*
 * drivers/net/ethernet/rocker/rocker.h - Rocker switch device driver
 * Copyright (c) 2014-2016 Jiri Pirko <jiri@mellanox.com>
 * Copyright (c) 2014 Scott Feldman <sfeldma@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _ROCKER_H
#define _ROCKER_H

#include <linux/types.h>

#include "rocker_hw.h"

struct rocker_desc_info {
	char *data; /* mapped */
	size_t data_size;
	size_t tlv_size;
	struct rocker_desc *desc;
	dma_addr_t mapaddr;
};

#endif

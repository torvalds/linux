/*
 * Header file for the Atmel AHB DMA Controller driver
 *
 * Copyright (C) 2008 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef AT_HDMAC_H
#define AT_HDMAC_H

#include <linux/dmaengine.h>

/**
 * struct at_dma_platform_data - Controller configuration parameters
 * @nr_channels: Number of channels supported by hardware (max 8)
 * @cap_mask: dma_capability flags supported by the platform
 */
struct at_dma_platform_data {
	unsigned int	nr_channels;
	dma_cap_mask_t  cap_mask;
};

#endif /* AT_HDMAC_H */

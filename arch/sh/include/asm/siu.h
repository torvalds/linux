/*
 * platform header for the SIU ASoC driver
 *
 * Copyright (C) 2009-2010 Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ASM_SIU_H
#define ASM_SIU_H

#include <asm/dma-sh.h>

struct device;

struct siu_platform {
	struct device *dma_dev;
	enum sh_dmae_slave_chan_id dma_slave_tx_a;
	enum sh_dmae_slave_chan_id dma_slave_rx_a;
	enum sh_dmae_slave_chan_id dma_slave_tx_b;
	enum sh_dmae_slave_chan_id dma_slave_rx_b;
};

#endif /* ASM_SIU_H */

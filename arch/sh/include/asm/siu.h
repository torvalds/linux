/* SPDX-License-Identifier: GPL-2.0
 *
 * platform header for the SIU ASoC driver
 *
 * Copyright (C) 2009-2010 Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 */

#ifndef ASM_SIU_H
#define ASM_SIU_H

struct device;

struct siu_platform {
	unsigned int dma_slave_tx_a;
	unsigned int dma_slave_rx_a;
	unsigned int dma_slave_tx_b;
	unsigned int dma_slave_rx_b;
};

#endif /* ASM_SIU_H */

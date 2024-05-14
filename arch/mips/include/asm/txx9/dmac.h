/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TXx9 SoC DMA Controller
 */

#ifndef __ASM_TXX9_DMAC_H
#define __ASM_TXX9_DMAC_H

#include <linux/dmaengine.h>

#define TXX9_DMA_MAX_NR_CHANNELS	4

/**
 * struct txx9dmac_platform_data - Controller configuration parameters
 * @memcpy_chan: Channel used for DMA_MEMCPY
 * @have_64bit_regs: DMAC have 64 bit registers
 */
struct txx9dmac_platform_data {
	int	memcpy_chan;
	bool	have_64bit_regs;
};

/**
 * struct txx9dmac_chan_platform_data - Channel configuration parameters
 * @dmac_dev: A platform device for DMAC
 */
struct txx9dmac_chan_platform_data {
	struct platform_device *dmac_dev;
};

/**
 * struct txx9dmac_slave - Controller-specific information about a slave
 * @tx_reg: physical address of data register used for
 *	memory-to-peripheral transfers
 * @rx_reg: physical address of data register used for
 *	peripheral-to-memory transfers
 * @reg_width: peripheral register width
 */
struct txx9dmac_slave {
	u64		tx_reg;
	u64		rx_reg;
	unsigned int	reg_width;
};

void txx9_dmac_init(int id, unsigned long baseaddr, int irq,
		    const struct txx9dmac_platform_data *pdata);

#endif /* __ASM_TXX9_DMAC_H */

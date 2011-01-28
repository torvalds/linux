/*
 * Copyright (C) 2010 RockChip Electronics Co. Ltd.
 *	ZhenFu Fang <fzf@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef	__RK29_DMA_PL330_H_
#define	__RK29_DMA_PL330_H_

#define RK29_DMAF_AUTOSTART		(1 << 0)
#define RK29_DMAF_CIRCULAR		(1 << 1)

/*
 * PL330 can assign any channel to communicate with
 * any of the peripherals attched to the DMAC.
 * For the sake of consistency across client drivers,
 * We keep the channel names unchanged and only add
 * missing peripherals are added.
 * Order is not important since rk29 PL330 API driver
 * use these just as IDs.
 */
enum dma_ch {
	DMACH_UART0_TX,
	DMACH_UART0_RX,
	DMACH_I2S_8CH_TX,
	DMACH_I2S_8CH_RX,        
	DMACH_I2S_2CH_TX,
	DMACH_I2S_2CH_RX,
	DMACH_SPDIF,
	DMACH_HSADC,
	DMACH_SDMMC,
	DMACH_SDIO,
	DMACH_EMMC,
	DMACH_UART1_TX,
	DMACH_UART1_RX,
	DMACH_UART2_TX,
	DMACH_UART2_RX,
	DMACH_UART3_TX,
	DMACH_UART3_RX,
	DMACH_SPI0_TX,
	DMACH_SPI0_RX,
	DMACH_SPI1_TX,
	DMACH_SPI1_RX,
	DMACH_PID_FILTER,
    DMACH_DMAC0_MEMTOMEM,
	/* END Marker, also used to denote a reserved channel */
	DMACH_MAX,
};

static inline bool rk29_dma_has_circular(void)
{
	return true;
}

/*
 * Every PL330 DMAC has max 32 peripheral interfaces,
 * of which some may be not be really used in your
 * DMAC's configuration.
 * Populate this array of 32 peri i/fs with relevant
 * channel IDs for used peri i/f and DMACH_MAX for
 * those unused.
 *
 * The platforms just need to provide this info
 * to the rk29 DMA API driver for PL330.
 */
struct rk29_pl330_platdata {
	enum dma_ch peri[32];
};

#include <mach/dma.h>

#endif	/* __RK29_DMA_PL330_H_ */

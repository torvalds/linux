/*
 * arch/arm/mach-ep93xx/dma.c
 *
 * Platform support code for the EP93xx dmaengine driver.
 *
 * Copyright (C) 2011 Mika Westerberg
 *
 * This work is based on the original dma-m2p implementation with
 * following copyrights:
 *
 *   Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 *   Copyright (C) 2006 Applied Data Systems
 *   Copyright (C) 2009 Ryan Mallon <rmallon@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <linux/platform_data/dma-ep93xx.h>
#include <mach/hardware.h>

#include "soc.h"

#define DMA_CHANNEL(_name, _base, _irq) \
	{ .name = (_name), .base = (_base), .irq = (_irq) }

/*
 * DMA M2P channels.
 *
 * On the EP93xx chip the following peripherals my be allocated to the 10
 * Memory to Internal Peripheral (M2P) channels (5 transmit + 5 receive).
 *
 *	I2S	contains 3 Tx and 3 Rx DMA Channels
 *	AAC	contains 3 Tx and 3 Rx DMA Channels
 *	UART1	contains 1 Tx and 1 Rx DMA Channels
 *	UART2	contains 1 Tx and 1 Rx DMA Channels
 *	UART3	contains 1 Tx and 1 Rx DMA Channels
 *	IrDA	contains 1 Tx and 1 Rx DMA Channels
 *
 * Registers are mapped statically in ep93xx_map_io().
 */
static struct ep93xx_dma_chan_data ep93xx_dma_m2p_channels[] = {
	DMA_CHANNEL("m2p0", EP93XX_DMA_BASE + 0x0000, IRQ_EP93XX_DMAM2P0),
	DMA_CHANNEL("m2p1", EP93XX_DMA_BASE + 0x0040, IRQ_EP93XX_DMAM2P1),
	DMA_CHANNEL("m2p2", EP93XX_DMA_BASE + 0x0080, IRQ_EP93XX_DMAM2P2),
	DMA_CHANNEL("m2p3", EP93XX_DMA_BASE + 0x00c0, IRQ_EP93XX_DMAM2P3),
	DMA_CHANNEL("m2p4", EP93XX_DMA_BASE + 0x0240, IRQ_EP93XX_DMAM2P4),
	DMA_CHANNEL("m2p5", EP93XX_DMA_BASE + 0x0200, IRQ_EP93XX_DMAM2P5),
	DMA_CHANNEL("m2p6", EP93XX_DMA_BASE + 0x02c0, IRQ_EP93XX_DMAM2P6),
	DMA_CHANNEL("m2p7", EP93XX_DMA_BASE + 0x0280, IRQ_EP93XX_DMAM2P7),
	DMA_CHANNEL("m2p8", EP93XX_DMA_BASE + 0x0340, IRQ_EP93XX_DMAM2P8),
	DMA_CHANNEL("m2p9", EP93XX_DMA_BASE + 0x0300, IRQ_EP93XX_DMAM2P9),
};

static struct ep93xx_dma_platform_data ep93xx_dma_m2p_data = {
	.channels		= ep93xx_dma_m2p_channels,
	.num_channels		= ARRAY_SIZE(ep93xx_dma_m2p_channels),
};

static struct platform_device ep93xx_dma_m2p_device = {
	.name			= "ep93xx-dma-m2p",
	.id			= -1,
	.dev			= {
		.platform_data	= &ep93xx_dma_m2p_data,
	},
};

/*
 * DMA M2M channels.
 *
 * There are 2 M2M channels which support memcpy/memset and in addition simple
 * hardware requests from/to SSP and IDE. We do not implement an external
 * hardware requests.
 *
 * Registers are mapped statically in ep93xx_map_io().
 */
static struct ep93xx_dma_chan_data ep93xx_dma_m2m_channels[] = {
	DMA_CHANNEL("m2m0", EP93XX_DMA_BASE + 0x0100, IRQ_EP93XX_DMAM2M0),
	DMA_CHANNEL("m2m1", EP93XX_DMA_BASE + 0x0140, IRQ_EP93XX_DMAM2M1),
};

static struct ep93xx_dma_platform_data ep93xx_dma_m2m_data = {
	.channels		= ep93xx_dma_m2m_channels,
	.num_channels		= ARRAY_SIZE(ep93xx_dma_m2m_channels),
};

static struct platform_device ep93xx_dma_m2m_device = {
	.name			= "ep93xx-dma-m2m",
	.id			= -1,
	.dev			= {
		.platform_data	= &ep93xx_dma_m2m_data,
	},
};

static int __init ep93xx_dma_init(void)
{
	platform_device_register(&ep93xx_dma_m2p_device);
	platform_device_register(&ep93xx_dma_m2m_device);
	return 0;
}
arch_initcall(ep93xx_dma_init);

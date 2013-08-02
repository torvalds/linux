/*
 * Renesas SuperH DMA Engine support for r8a73a4 (APE6) SoCs
 *
 * Copyright (C) 2013 Renesas Electronics, Inc.
 *
 * This is free software; you can redistribute it and/or modify it under the
 * terms of version 2 the GNU General Public License as published by the Free
 * Software Foundation.
 */
#include <linux/sh_dma.h>

#include "shdma-arm.h"

const unsigned int dma_ts_shift[] = SH_DMAE_TS_SHIFT;

static const struct sh_dmae_slave_config dma_slaves[] = {
	{
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0xd1,		/* MMC0 Tx */
	}, {
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0xd2,		/* MMC0 Rx */
	}, {
		.chcr		= CHCR_TX(XMIT_SZ_32BIT),
		.mid_rid	= 0xe1,		/* MMC1 Tx */
	}, {
		.chcr		= CHCR_RX(XMIT_SZ_32BIT),
		.mid_rid	= 0xe2,		/* MMC1 Rx */
	},
};

#define DMAE_CHANNEL(a, b)				\
	{						\
		.offset         = (a) - 0x20,		\
		.dmars          = (a) - 0x20 + 0x40,	\
		.chclr_bit	= (b),			\
		.chclr_offset	= 0x80 - 0x20,		\
	}

static const struct sh_dmae_channel dma_channels[] = {
	DMAE_CHANNEL(0x8000, 0),
	DMAE_CHANNEL(0x8080, 1),
	DMAE_CHANNEL(0x8100, 2),
	DMAE_CHANNEL(0x8180, 3),
	DMAE_CHANNEL(0x8200, 4),
	DMAE_CHANNEL(0x8280, 5),
	DMAE_CHANNEL(0x8300, 6),
	DMAE_CHANNEL(0x8380, 7),
	DMAE_CHANNEL(0x8400, 8),
	DMAE_CHANNEL(0x8480, 9),
	DMAE_CHANNEL(0x8500, 10),
	DMAE_CHANNEL(0x8580, 11),
	DMAE_CHANNEL(0x8600, 12),
	DMAE_CHANNEL(0x8680, 13),
	DMAE_CHANNEL(0x8700, 14),
	DMAE_CHANNEL(0x8780, 15),
	DMAE_CHANNEL(0x8800, 16),
	DMAE_CHANNEL(0x8880, 17),
	DMAE_CHANNEL(0x8900, 18),
	DMAE_CHANNEL(0x8980, 19),
};

const struct sh_dmae_pdata r8a73a4_dma_pdata = {
	.slave		= dma_slaves,
	.slave_num	= ARRAY_SIZE(dma_slaves),
	.channel	= dma_channels,
	.channel_num	= ARRAY_SIZE(dma_channels),
	.ts_low_shift	= TS_LOW_SHIFT,
	.ts_low_mask	= TS_LOW_BIT << TS_LOW_SHIFT,
	.ts_high_shift	= TS_HI_SHIFT,
	.ts_high_mask	= TS_HI_BIT << TS_HI_SHIFT,
	.ts_shift	= dma_ts_shift,
	.ts_shift_num	= ARRAY_SIZE(dma_ts_shift),
	.dmaor_init     = DMAOR_DME,
	.chclr_present	= 1,
	.chclr_bitwise	= 1,
};

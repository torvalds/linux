/*
 * Header for the new SH dmaengine driver
 *
 * Copyright (C) 2010 Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef ASM_DMAENGINE_H
#define ASM_DMAENGINE_H

#include <linux/dmaengine.h>
#include <linux/list.h>

#include <asm/dma-register.h>

#define SH_DMAC_MAX_CHANNELS	6

enum sh_dmae_slave_chan_id {
	SHDMA_SLAVE_SCIF0_TX,
	SHDMA_SLAVE_SCIF0_RX,
	SHDMA_SLAVE_SCIF1_TX,
	SHDMA_SLAVE_SCIF1_RX,
	SHDMA_SLAVE_SCIF2_TX,
	SHDMA_SLAVE_SCIF2_RX,
	SHDMA_SLAVE_SCIF3_TX,
	SHDMA_SLAVE_SCIF3_RX,
	SHDMA_SLAVE_SCIF4_TX,
	SHDMA_SLAVE_SCIF4_RX,
	SHDMA_SLAVE_SCIF5_TX,
	SHDMA_SLAVE_SCIF5_RX,
	SHDMA_SLAVE_SIUA_TX,
	SHDMA_SLAVE_SIUA_RX,
	SHDMA_SLAVE_SIUB_TX,
	SHDMA_SLAVE_SIUB_RX,
	SHDMA_SLAVE_NUMBER,	/* Must stay last */
};

struct sh_dmae_slave_config {
	enum sh_dmae_slave_chan_id	slave_id;
	dma_addr_t			addr;
	u32				chcr;
	char				mid_rid;
};

struct sh_dmae_channel {
	unsigned int	offset;
	unsigned int	dmars;
	unsigned int	dmars_bit;
};

struct sh_dmae_pdata {
	struct sh_dmae_slave_config *slave;
	int slave_num;
	struct sh_dmae_channel *channel;
	int channel_num;
	unsigned int ts_low_shift;
	unsigned int ts_low_mask;
	unsigned int ts_high_shift;
	unsigned int ts_high_mask;
	unsigned int *ts_shift;
	int ts_shift_num;
	u16 dmaor_init;
};

struct device;

/* Used by slave DMA clients to request DMA to/from a specific peripheral */
struct sh_dmae_slave {
	enum sh_dmae_slave_chan_id	slave_id; /* Set by the platform */
	struct device			*dma_dev; /* Set by the platform */
	struct sh_dmae_slave_config	*config;  /* Set by the driver */
};

struct sh_dmae_regs {
	u32 sar; /* SAR / source address */
	u32 dar; /* DAR / destination address */
	u32 tcr; /* TCR / transfer count */
};

struct sh_desc {
	struct sh_dmae_regs hw;
	struct list_head node;
	struct dma_async_tx_descriptor async_tx;
	enum dma_data_direction direction;
	dma_cookie_t cookie;
	size_t partial;
	int chunks;
	int mark;
};

#endif

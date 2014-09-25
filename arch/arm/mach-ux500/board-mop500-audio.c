/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License (GPL), version 2
 */

#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/platform_data/dma-ste-dma40.h>

#include <linux/platform_data/asoc-ux500-msp.h>

#include "ste-dma40-db8500.h"
#include "board-mop500.h"

static struct stedma40_chan_cfg msp0_dma_rx = {
	.high_priority = true,
	.dir = DMA_DEV_TO_MEM,
	.dev_type = DB8500_DMA_DEV31_MSP0_SLIM0_CH0,
};

static struct stedma40_chan_cfg msp0_dma_tx = {
	.high_priority = true,
	.dir = DMA_MEM_TO_DEV,
	.dev_type = DB8500_DMA_DEV31_MSP0_SLIM0_CH0,
};

struct msp_i2s_platform_data msp0_platform_data = {
	.id = 0,
	.msp_i2s_dma_rx = &msp0_dma_rx,
	.msp_i2s_dma_tx = &msp0_dma_tx,
};

static struct stedma40_chan_cfg msp1_dma_rx = {
	.high_priority = true,
	.dir = DMA_DEV_TO_MEM,
	.dev_type = DB8500_DMA_DEV30_MSP3,
};

static struct stedma40_chan_cfg msp1_dma_tx = {
	.high_priority = true,
	.dir = DMA_MEM_TO_DEV,
	.dev_type = DB8500_DMA_DEV30_MSP1,
};

struct msp_i2s_platform_data msp1_platform_data = {
	.id = 1,
	.msp_i2s_dma_rx = NULL,
	.msp_i2s_dma_tx = &msp1_dma_tx,
};

static struct stedma40_chan_cfg msp2_dma_rx = {
	.high_priority = true,
	.dir = DMA_DEV_TO_MEM,
	.dev_type = DB8500_DMA_DEV14_MSP2,
};

static struct stedma40_chan_cfg msp2_dma_tx = {
	.high_priority = true,
	.dir = DMA_MEM_TO_DEV,
	.dev_type = DB8500_DMA_DEV14_MSP2,
	.use_fixed_channel = true,
	.phy_channel = 1,
};

struct msp_i2s_platform_data msp2_platform_data = {
	.id = 2,
	.msp_i2s_dma_rx = &msp2_dma_rx,
	.msp_i2s_dma_tx = &msp2_dma_tx,
};

struct msp_i2s_platform_data msp3_platform_data = {
	.id		= 3,
	.msp_i2s_dma_rx	= &msp1_dma_rx,
	.msp_i2s_dma_tx	= NULL,
};

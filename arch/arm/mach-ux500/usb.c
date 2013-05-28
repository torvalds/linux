/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Mian Yousaf Kaukab <mian.yousaf.kaukab@stericsson.com>
 * License terms: GNU General Public License (GPL) version 2
 */
#include <linux/platform_device.h>
#include <linux/usb/musb.h>
#include <linux/dma-mapping.h>
#include <linux/platform_data/usb-musb-ux500.h>
#include <linux/platform_data/dma-ste-dma40.h>

#include "db8500-regs.h"

#define MUSB_DMA40_RX_CH { \
		.mode = STEDMA40_MODE_LOGICAL, \
		.dir = STEDMA40_PERIPH_TO_MEM, \
	}

#define MUSB_DMA40_TX_CH { \
		.mode = STEDMA40_MODE_LOGICAL, \
		.dir = STEDMA40_MEM_TO_PERIPH, \
	}

static struct stedma40_chan_cfg musb_dma_rx_ch[UX500_MUSB_DMA_NUM_RX_CHANNELS]
	= {
	MUSB_DMA40_RX_CH,
	MUSB_DMA40_RX_CH,
	MUSB_DMA40_RX_CH,
	MUSB_DMA40_RX_CH,
	MUSB_DMA40_RX_CH,
	MUSB_DMA40_RX_CH,
	MUSB_DMA40_RX_CH,
	MUSB_DMA40_RX_CH
};

static struct stedma40_chan_cfg musb_dma_tx_ch[UX500_MUSB_DMA_NUM_TX_CHANNELS]
	= {
	MUSB_DMA40_TX_CH,
	MUSB_DMA40_TX_CH,
	MUSB_DMA40_TX_CH,
	MUSB_DMA40_TX_CH,
	MUSB_DMA40_TX_CH,
	MUSB_DMA40_TX_CH,
	MUSB_DMA40_TX_CH,
	MUSB_DMA40_TX_CH,
};

static void *ux500_dma_rx_param_array[UX500_MUSB_DMA_NUM_RX_CHANNELS] = {
	&musb_dma_rx_ch[0],
	&musb_dma_rx_ch[1],
	&musb_dma_rx_ch[2],
	&musb_dma_rx_ch[3],
	&musb_dma_rx_ch[4],
	&musb_dma_rx_ch[5],
	&musb_dma_rx_ch[6],
	&musb_dma_rx_ch[7]
};

static void *ux500_dma_tx_param_array[UX500_MUSB_DMA_NUM_TX_CHANNELS] = {
	&musb_dma_tx_ch[0],
	&musb_dma_tx_ch[1],
	&musb_dma_tx_ch[2],
	&musb_dma_tx_ch[3],
	&musb_dma_tx_ch[4],
	&musb_dma_tx_ch[5],
	&musb_dma_tx_ch[6],
	&musb_dma_tx_ch[7]
};

static struct ux500_musb_board_data musb_board_data = {
	.dma_rx_param_array = ux500_dma_rx_param_array,
	.dma_tx_param_array = ux500_dma_tx_param_array,
	.num_rx_channels = UX500_MUSB_DMA_NUM_RX_CHANNELS,
	.num_tx_channels = UX500_MUSB_DMA_NUM_TX_CHANNELS,
	.dma_filter = stedma40_filter,
};

static u64 ux500_musb_dmamask = DMA_BIT_MASK(32);

static struct musb_hdrc_config musb_hdrc_config = {
	.multipoint	= true,
	.dyn_fifo	= true,
	.num_eps	= 16,
	.ram_bits	= 16,
};

static struct musb_hdrc_platform_data musb_platform_data = {
	.mode = MUSB_OTG,
	.config = &musb_hdrc_config,
	.board_data = &musb_board_data,
};

static struct resource usb_resources[] = {
	[0] = {
		.name	= "usb-mem",
		.flags	=  IORESOURCE_MEM,
	},

	[1] = {
		.name   = "mc", /* hard-coded in musb */
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device ux500_musb_device = {
	.name = "musb-ux500",
	.id = 0,
	.dev = {
		.platform_data = &musb_platform_data,
		.dma_mask = &ux500_musb_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.num_resources = ARRAY_SIZE(usb_resources),
	.resource = usb_resources,
};

static inline void ux500_usb_dma_update_rx_ch_config(int *dev_type)
{
	u32 idx;

	for (idx = 0; idx < UX500_MUSB_DMA_NUM_RX_CHANNELS; idx++)
		musb_dma_rx_ch[idx].dev_type = dev_type[idx];
}

static inline void ux500_usb_dma_update_tx_ch_config(int *dev_type)
{
	u32 idx;

	for (idx = 0; idx < UX500_MUSB_DMA_NUM_TX_CHANNELS; idx++)
		musb_dma_tx_ch[idx].dev_type = dev_type[idx];
}

void ux500_add_usb(struct device *parent, resource_size_t base, int irq,
		   int *dma_rx_cfg, int *dma_tx_cfg)
{
	ux500_musb_device.resource[0].start = base;
	ux500_musb_device.resource[0].end = base + SZ_64K - 1;
	ux500_musb_device.resource[1].start = irq;
	ux500_musb_device.resource[1].end = irq;

	ux500_usb_dma_update_rx_ch_config(dma_rx_cfg);
	ux500_usb_dma_update_tx_ch_config(dma_tx_cfg);

	ux500_musb_device.dev.parent = parent;

	platform_device_register(&ux500_musb_device);
}

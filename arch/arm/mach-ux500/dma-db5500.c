/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Per Forlin <per.forlin@stericsson.com> for ST-Ericsson
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com> for ST-Ericsson
 * Author: Rabin Vincent <rabinv.vincent@stericsson.com> for ST-Ericsson
 *
 * License terms: GNU General Public License (GPL), version 2
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <plat/ste_dma40.h>
#include <mach/setup.h>
#include <mach/hardware.h>

#include "ste-dma40-db5500.h"

static struct resource dma40_resources[] = {
	[0] = {
		.start = U5500_DMA_BASE,
		.end   = U5500_DMA_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
		.name  = "base",
	},
	[1] = {
		.start = U5500_DMA_LCPA_BASE,
		.end   = U5500_DMA_LCPA_BASE + 2 * SZ_1K - 1,
		.flags = IORESOURCE_MEM,
		.name  = "lcpa",
	},
	[2] = {
		.start = IRQ_DB5500_DMA,
		.end   = IRQ_DB5500_DMA,
		.flags = IORESOURCE_IRQ
	}
};

/* Default configuration for physical memcpy */
static struct stedma40_chan_cfg dma40_memcpy_conf_phy = {
	.mode = STEDMA40_MODE_PHYSICAL,
	.dir = STEDMA40_MEM_TO_MEM,

	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.src_info.psize = STEDMA40_PSIZE_PHY_1,
	.src_info.flow_ctrl = STEDMA40_NO_FLOW_CTRL,

	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.psize = STEDMA40_PSIZE_PHY_1,
	.dst_info.flow_ctrl = STEDMA40_NO_FLOW_CTRL,
};

/* Default configuration for logical memcpy */
static struct stedma40_chan_cfg dma40_memcpy_conf_log = {
	.dir = STEDMA40_MEM_TO_MEM,

	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.src_info.psize = STEDMA40_PSIZE_LOG_1,
	.src_info.flow_ctrl = STEDMA40_NO_FLOW_CTRL,

	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.psize = STEDMA40_PSIZE_LOG_1,
	.dst_info.flow_ctrl = STEDMA40_NO_FLOW_CTRL,
};

/*
 * Mapping between soruce event lines and physical device address This was
 * created assuming that the event line is tied to a device and therefore the
 * address is constant, however this is not true for at least USB, and the
 * values are just placeholders for USB.  This table is preserved and used for
 * now.
 */
static const dma_addr_t dma40_rx_map[DB5500_DMA_NR_DEV] = {
	[DB5500_DMA_DEV24_SDMMC0_RX] = -1,
};

/* Mapping between destination event lines and physical device address */
static const dma_addr_t dma40_tx_map[DB5500_DMA_NR_DEV] = {
	[DB5500_DMA_DEV24_SDMMC0_TX] = -1,
};

static int dma40_memcpy_event[] = {
	DB5500_DMA_MEMCPY_TX_1,
	DB5500_DMA_MEMCPY_TX_2,
	DB5500_DMA_MEMCPY_TX_3,
	DB5500_DMA_MEMCPY_TX_4,
	DB5500_DMA_MEMCPY_TX_5,
};

static struct stedma40_platform_data dma40_plat_data = {
	.dev_len		= ARRAY_SIZE(dma40_rx_map),
	.dev_rx			= dma40_rx_map,
	.dev_tx			= dma40_tx_map,
	.memcpy			= dma40_memcpy_event,
	.memcpy_len		= ARRAY_SIZE(dma40_memcpy_event),
	.memcpy_conf_phy	= &dma40_memcpy_conf_phy,
	.memcpy_conf_log	= &dma40_memcpy_conf_log,
	.disabled_channels	= {-1},
};

static struct platform_device dma40_device = {
	.dev = {
		.platform_data = &dma40_plat_data,
	},
	.name		= "dma40",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(dma40_resources),
	.resource	= dma40_resources
};

void __init db5500_dma_init(void)
{
	int ret;

	ret = platform_device_register(&dma40_device);
	if (ret)
		dev_err(&dma40_device.dev, "unable to register device: %d\n", ret);

}

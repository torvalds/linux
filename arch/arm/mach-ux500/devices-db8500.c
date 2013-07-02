/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl022.h>
#include <linux/platform_data/dma-ste-dma40.h>
#include <linux/mfd/dbx500-prcmu.h>

#include "setup.h"
#include "irqs.h"

#include "db8500-regs.h"
#include "devices-db8500.h"
#include "ste-dma40-db8500.h"

static struct resource dma40_resources[] = {
	[0] = {
		.start = U8500_DMA_BASE,
		.end   = U8500_DMA_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
		.name  = "base",
	},
	[1] = {
		.start = U8500_DMA_LCPA_BASE,
		.end   = U8500_DMA_LCPA_BASE + 2 * SZ_1K - 1,
		.flags = IORESOURCE_MEM,
		.name  = "lcpa",
	},
	[2] = {
		.start = IRQ_DB8500_DMA,
		.end   = IRQ_DB8500_DMA,
		.flags = IORESOURCE_IRQ,
	}
};

/* Default configuration for physcial memcpy */
struct stedma40_chan_cfg dma40_memcpy_conf_phy = {
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
struct stedma40_chan_cfg dma40_memcpy_conf_log = {
	.dir = STEDMA40_MEM_TO_MEM,

	.src_info.data_width = STEDMA40_BYTE_WIDTH,
	.src_info.psize = STEDMA40_PSIZE_LOG_1,
	.src_info.flow_ctrl = STEDMA40_NO_FLOW_CTRL,

	.dst_info.data_width = STEDMA40_BYTE_WIDTH,
	.dst_info.psize = STEDMA40_PSIZE_LOG_1,
	.dst_info.flow_ctrl = STEDMA40_NO_FLOW_CTRL,
};

/*
 * Mapping between destination event lines and physical device address.
 * The event line is tied to a device and therefore the address is constant.
 * When the address comes from a primecell it will be configured in runtime
 * and we set the address to -1 as a placeholder.
 */
static const dma_addr_t dma40_tx_map[DB8500_DMA_NR_DEV] = {
	/* MUSB - these will be runtime-reconfigured */
	[DB8500_DMA_DEV39_USB_OTG_OEP_8] = -1,
	[DB8500_DMA_DEV16_USB_OTG_OEP_7_15] = -1,
	[DB8500_DMA_DEV17_USB_OTG_OEP_6_14] = -1,
	[DB8500_DMA_DEV18_USB_OTG_OEP_5_13] = -1,
	[DB8500_DMA_DEV19_USB_OTG_OEP_4_12] = -1,
	[DB8500_DMA_DEV36_USB_OTG_OEP_3_11] = -1,
	[DB8500_DMA_DEV37_USB_OTG_OEP_2_10] = -1,
	[DB8500_DMA_DEV38_USB_OTG_OEP_1_9] = -1,
	/* PrimeCells - run-time configured */
	[DB8500_DMA_DEV0_SPI0_TX] = -1,
	[DB8500_DMA_DEV1_SD_MMC0_TX] = -1,
	[DB8500_DMA_DEV2_SD_MMC1_TX] = -1,
	[DB8500_DMA_DEV3_SD_MMC2_TX] = -1,
	[DB8500_DMA_DEV8_SSP0_TX] = -1,
	[DB8500_DMA_DEV9_SSP1_TX] = -1,
	[DB8500_DMA_DEV11_UART2_TX] = -1,
	[DB8500_DMA_DEV12_UART1_TX] = -1,
	[DB8500_DMA_DEV13_UART0_TX] = -1,
	[DB8500_DMA_DEV28_SD_MM2_TX] = -1,
	[DB8500_DMA_DEV29_SD_MM0_TX] = -1,
	[DB8500_DMA_DEV32_SD_MM1_TX] = -1,
	[DB8500_DMA_DEV33_SPI2_TX] = -1,
	[DB8500_DMA_DEV35_SPI1_TX] = -1,
	[DB8500_DMA_DEV40_SPI3_TX] = -1,
	[DB8500_DMA_DEV41_SD_MM3_TX] = -1,
	[DB8500_DMA_DEV42_SD_MM4_TX] = -1,
	[DB8500_DMA_DEV43_SD_MM5_TX] = -1,
	[DB8500_DMA_DEV14_MSP2_TX] = U8500_MSP2_BASE + MSP_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV30_MSP1_TX] = U8500_MSP1_BASE + MSP_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV31_MSP0_TX_SLIM0_CH0_TX] = U8500_MSP0_BASE + MSP_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV48_CAC1_TX] = U8500_CRYP1_BASE + CRYP1_TX_REG_OFFSET,
	[DB8500_DMA_DEV50_HAC1_TX] = U8500_HASH1_BASE + HASH1_TX_REG_OFFSET,
};

/* Mapping between source event lines and physical device address */
static const dma_addr_t dma40_rx_map[DB8500_DMA_NR_DEV] = {
	/* MUSB - these will be runtime-reconfigured */
	[DB8500_DMA_DEV39_USB_OTG_IEP_8] = -1,
	[DB8500_DMA_DEV16_USB_OTG_IEP_7_15] = -1,
	[DB8500_DMA_DEV17_USB_OTG_IEP_6_14] = -1,
	[DB8500_DMA_DEV18_USB_OTG_IEP_5_13] = -1,
	[DB8500_DMA_DEV19_USB_OTG_IEP_4_12] = -1,
	[DB8500_DMA_DEV36_USB_OTG_IEP_3_11] = -1,
	[DB8500_DMA_DEV37_USB_OTG_IEP_2_10] = -1,
	[DB8500_DMA_DEV38_USB_OTG_IEP_1_9] = -1,
	/* PrimeCells */
	[DB8500_DMA_DEV0_SPI0_RX] = -1,
	[DB8500_DMA_DEV1_SD_MMC0_RX] = -1,
	[DB8500_DMA_DEV2_SD_MMC1_RX] = -1,
	[DB8500_DMA_DEV3_SD_MMC2_RX] = -1,
	[DB8500_DMA_DEV8_SSP0_RX] = -1,
	[DB8500_DMA_DEV9_SSP1_RX] = -1,
	[DB8500_DMA_DEV11_UART2_RX] = -1,
	[DB8500_DMA_DEV12_UART1_RX] = -1,
	[DB8500_DMA_DEV13_UART0_RX] = -1,
	[DB8500_DMA_DEV28_SD_MM2_RX] = -1,
	[DB8500_DMA_DEV29_SD_MM0_RX] = -1,
	[DB8500_DMA_DEV32_SD_MM1_RX] = -1,
	[DB8500_DMA_DEV33_SPI2_RX] = -1,
	[DB8500_DMA_DEV35_SPI1_RX] = -1,
	[DB8500_DMA_DEV40_SPI3_RX] = -1,
	[DB8500_DMA_DEV41_SD_MM3_RX] = -1,
	[DB8500_DMA_DEV42_SD_MM4_RX] = -1,
	[DB8500_DMA_DEV43_SD_MM5_RX] = -1,
	[DB8500_DMA_DEV14_MSP2_RX] = U8500_MSP2_BASE + MSP_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV30_MSP3_RX] = U8500_MSP3_BASE + MSP_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV31_MSP0_RX_SLIM0_CH0_RX] = U8500_MSP0_BASE + MSP_TX_RX_REG_OFFSET,
	[DB8500_DMA_DEV48_CAC1_RX] = U8500_CRYP1_BASE + CRYP1_RX_REG_OFFSET,
};

/* Reserved event lines for memcpy only */
static int dma40_memcpy_event[] = {
	DB8500_DMA_MEMCPY_TX_0,
	DB8500_DMA_MEMCPY_TX_1,
	DB8500_DMA_MEMCPY_TX_2,
	DB8500_DMA_MEMCPY_TX_3,
	DB8500_DMA_MEMCPY_TX_4,
	DB8500_DMA_MEMCPY_TX_5,
};

static struct stedma40_platform_data dma40_plat_data = {
	.dev_len = DB8500_DMA_NR_DEV,
	.dev_rx = dma40_rx_map,
	.dev_tx = dma40_tx_map,
	.memcpy = dma40_memcpy_event,
	.memcpy_len = ARRAY_SIZE(dma40_memcpy_event),
	.memcpy_conf_phy = &dma40_memcpy_conf_phy,
	.memcpy_conf_log = &dma40_memcpy_conf_log,
	.disabled_channels = {-1},
};

struct platform_device u8500_dma40_device = {
	.dev = {
		.platform_data = &dma40_plat_data,
	},
	.name = "dma40",
	.id = 0,
	.num_resources = ARRAY_SIZE(dma40_resources),
	.resource = dma40_resources
};

struct resource keypad_resources[] = {
	[0] = {
		.start = U8500_SKE_BASE,
		.end = U8500_SKE_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_DB8500_KB,
		.end = IRQ_DB8500_KB,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device u8500_ske_keypad_device = {
	.name = "nmk-ske-keypad",
	.id = -1,
	.num_resources = ARRAY_SIZE(keypad_resources),
	.resource = keypad_resources,
};

struct prcmu_pdata db8500_prcmu_pdata = {
	.ab_platdata	= &ab8500_platdata,
	.ab_irq		= IRQ_DB8500_AB8500,
	.irq_base	= IRQ_PRCMU_BASE,
	.version_offset	= DB8500_PRCMU_FW_VERSION_OFFSET,
	.legacy_offset	= DB8500_PRCMU_LEGACY_OFFSET,
};

static struct resource db8500_prcmu_res[] = {
	{
		.name  = "prcmu",
		.start = U8500_PRCMU_BASE,
		.end   = U8500_PRCMU_BASE + SZ_8K - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.name  = "prcmu-tcdm",
		.start = U8500_PRCMU_TCDM_BASE,
		.end   = U8500_PRCMU_TCDM_BASE + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.name  = "irq",
		.start = IRQ_DB8500_PRCMU1,
		.end   = IRQ_DB8500_PRCMU1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name  = "prcmu-tcpm",
		.start = U8500_PRCMU_TCPM_BASE,
		.end   = U8500_PRCMU_TCPM_BASE + SZ_32K - 1,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device db8500_prcmu_device = {
	.name			= "db8500-prcmu",
	.resource		= db8500_prcmu_res,
	.num_resources		= ARRAY_SIZE(db8500_prcmu_res),
	.dev = {
		.platform_data = &db8500_prcmu_pdata,
	},
};

/*
 * linux/arch/arm/mach-exynos/dev-dwmci.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Platform device for Synopsys DesignWare Mobile Storage IP
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/mmc/dw_mmc.h>
#include <linux/mmc/host.h>

#include <plat/devs.h>
#include <plat/cpu.h>

#include <mach/map.h>

#define DWMCI_CLKSEL	0x09c

static int exynos_dwmci_get_bus_wd(u32 slot_id)
{
	return 4;
}

static int exynos_dwmci_init(u32 slot_id, irq_handler_t handler, void *data)
{
	struct dw_mci *host = (struct dw_mci *)data;

	/* Set Phase Shift Register */
	if (soc_is_exynos4210()) {
		host->pdata->sdr_timing = 0x00010001;
		host->pdata->ddr_timing = 0x00020002;
	} else if (soc_is_exynos4212() || soc_is_exynos4412()) {
		host->pdata->sdr_timing = 0x00010001;
		host->pdata->ddr_timing = 0x00010002;
	} else if (soc_is_exynos5250()) {
		if (samsung_rev() >= EXYNOS5250_REV_1_0) {
			switch (host->pdev->id) {
			case 0:
				host->pdata->sdr_timing = 0x03020001;
				host->pdata->ddr_timing = 0x03030002;
				break;
			case 1:
				host->pdata->sdr_timing = 0x03020001;
				host->pdata->ddr_timing = 0x03030002;
				break;
			case 2:
				host->pdata->sdr_timing = 0x03020001;
				host->pdata->ddr_timing = 0x03030002;
				break;
			case 3:
				host->pdata->sdr_timing = 0x03020001;
				host->pdata->ddr_timing = 0x03030002;
				break;
			default:
				host->pdata->sdr_timing = 0x03020001;
				host->pdata->ddr_timing = 0x03030002;
				break;
			}
		} else {
			host->pdata->sdr_timing = 0x00010000;
			host->pdata->ddr_timing = 0x00010000;
		}
	}
#ifdef CONFIG_SLP
	host->pdata->sdr_timing = 0x00020001;
	host->pdata->ddr_timing = 0x00020002;
#endif

	return 0;
}

static void exynos_dwmci_set_io_timing(void *data, unsigned char timing)
{
	struct dw_mci *host = (struct dw_mci *)data;

	if (timing == MMC_TIMING_UHS_DDR50)
		__raw_writel(host->pdata->ddr_timing,
			host->regs + DWMCI_CLKSEL);
	else
		__raw_writel(host->pdata->sdr_timing,
			host->regs + DWMCI_CLKSEL);
}

static struct resource exynos_dwmci_resource[] = {
	[0] = {
		.start	= EXYNOS_PA_DWMCI,
		.end	= EXYNOS_PA_DWMCI + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_DWMCI,
		.end	= IRQ_DWMCI,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct dw_mci_board exynos_dwmci_def_platdata = {
	.num_slots		= 1,
	.quirks			= DW_MCI_QUIRK_BROKEN_CARD_DETECTION,
	.bus_hz			= 80 * 1000 * 1000,
	.detect_delay_ms	= 200,
	.init			= exynos_dwmci_init,
	.get_bus_wd		= exynos_dwmci_get_bus_wd,
	.set_io_timing		= exynos_dwmci_set_io_timing,
};

static u64 exynos_dwmci_dmamask = DMA_BIT_MASK(32);

struct platform_device exynos_device_dwmci = {
	.name		= "dw_mmc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(exynos_dwmci_resource),
	.resource	= exynos_dwmci_resource,
	.dev		= {
		.dma_mask		= &exynos_dwmci_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &exynos_dwmci_def_platdata,
	},
};

#define EXYNOS5_DWMCI_RESOURCE(_channel) \
static struct resource exynos5_dwmci##_channel##_resource[] = { \
	[0] = {							\
		.start	= S3C_PA_HSMMC##_channel,		\
		.end	= S3C_PA_HSMMC##_channel + SZ_4K - 1,	\
		.flags	= IORESOURCE_MEM,			\
	},							\
	[1] = {							\
		.start	= IRQ_HSMMC##_channel,			\
		.end	= IRQ_HSMMC##_channel,			\
		.flags	= IORESOURCE_IRQ,			\
	}							\
};

EXYNOS5_DWMCI_RESOURCE(0)
EXYNOS5_DWMCI_RESOURCE(1)
EXYNOS5_DWMCI_RESOURCE(2)
EXYNOS5_DWMCI_RESOURCE(3)

#define EXYNOS_DWMCI_DEF_PLATDATA(_channel)			\
struct dw_mci_board exynos_dwmci##_channel##_def_platdata = {	\
	.num_slots		= 1,				\
	.quirks			=				\
		DW_MCI_QUIRK_BROKEN_CARD_DETECTION,		\
	.bus_hz			= 200 * 1000 * 1000,		\
	.detect_delay_ms	= 200,				\
	.init			= exynos_dwmci_init,		\
	.get_bus_wd		= exynos_dwmci_get_bus_wd,	\
	.set_io_timing		= exynos_dwmci_set_io_timing,	\
	.cd_type 		= DW_MCI_CD_INTERNAL 		\
};

EXYNOS_DWMCI_DEF_PLATDATA(0)
EXYNOS_DWMCI_DEF_PLATDATA(1)
EXYNOS_DWMCI_DEF_PLATDATA(2)
EXYNOS_DWMCI_DEF_PLATDATA(3)

#define EXYNOS_DWMCI_PLATFORM_DEVICE(_channel)			\
struct platform_device exynos_device_dwmci##_channel =		\
{								\
	.name		= "dw_mmc",				\
	.id		= _channel,				\
	.num_resources	=					\
	ARRAY_SIZE(exynos5_dwmci##_channel##_resource), 	\
	.resource	= exynos5_dwmci##_channel##_resource,	\
	.dev		= {					\
		.dma_mask		= &exynos_dwmci_dmamask,\
		.coherent_dma_mask	= DMA_BIT_MASK(32),	\
		.platform_data		=			\
		&exynos_dwmci##_channel##_def_platdata,	\
	},							\
};

EXYNOS_DWMCI_PLATFORM_DEVICE(0)
EXYNOS_DWMCI_PLATFORM_DEVICE(1)
EXYNOS_DWMCI_PLATFORM_DEVICE(2)
EXYNOS_DWMCI_PLATFORM_DEVICE(3)

void __init exynos_dwmci_set_platdata(struct dw_mci_board *pd, u32 slot_id)
{
	struct dw_mci_board *npd = NULL;

	if ((soc_is_exynos4210()) ||
		soc_is_exynos4212() || soc_is_exynos4412()) {
		npd = s3c_set_platdata(pd, sizeof(struct dw_mci_board),
			&exynos_device_dwmci);
	} else if (soc_is_exynos5250()) {
		if (slot_id == 0) {
			if (samsung_rev() < EXYNOS5250_REV_1_0) {
				exynos_device_dwmci0.resource[0].start =
					EXYNOS_PA_DWMCI;
				exynos_device_dwmci0.resource[0].end =
					EXYNOS_PA_DWMCI + SZ_4K - 1;
				exynos_device_dwmci0.resource[1].start =
					IRQ_DWMCI;
				exynos_device_dwmci0.resource[1].end =
					IRQ_DWMCI;
			}
			npd = s3c_set_platdata(pd, sizeof(struct dw_mci_board),
				&exynos_device_dwmci0);
		} else if (slot_id == 1) {
			npd = s3c_set_platdata(pd, sizeof(struct dw_mci_board),
				&exynos_device_dwmci1);
		} else if (slot_id == 2) {
			npd = s3c_set_platdata(pd, sizeof(struct dw_mci_board),
				&exynos_device_dwmci2);
		} else if (slot_id == 3) {
			npd = s3c_set_platdata(pd, sizeof(struct dw_mci_board),
				&exynos_device_dwmci3);
		} else {
			pr_err("This channel %d Cannot support.\n", slot_id);
		}
	} else {
		printk("dwmci platform data support only exynos4/5!\n");
#ifdef CONFIG_SLP
		npd = s3c_set_platdata(pd, sizeof(struct dw_mci_board),
			&exynos_device_dwmci);
#endif
	}

	if (npd) {
		if (!npd->init)
			npd->init = exynos_dwmci_init;
		if (!npd->get_bus_wd)
			npd->get_bus_wd = exynos_dwmci_get_bus_wd;
		if (!npd->set_io_timing)
			npd->set_io_timing = exynos_dwmci_set_io_timing;
	}
}

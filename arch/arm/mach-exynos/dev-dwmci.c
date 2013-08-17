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
#include <linux/ioport.h>
#include <linux/mmc/dw_mmc.h>
#include <linux/mmc/host.h>
#include <linux/clk.h>

#include <plat/devs.h>
#include <plat/cpu.h>

#include <mach/map.h>

#define DWMCI_CLKSEL	0x09c
#define DWMCI_DDR200_RDDQS_EN		0x110
#define DWMCI_DDR200_ASYNC_FIFO_CTRL	0x114
#define DWMCI_DDR200_DLINE_CTRL		0x118
/* DDR200 RDDQS Enable*/
#define DWMCI_TXDT_CRC_TIMER_FASTLIMIT(x)	(((x) & 0xFF) << 16)
#define DWMCI_TXDT_CRC_TIMER_INITVAL(x)		(((x) & 0xFF) << 8)
#define DWMCI_BUSY_CHK_CLK_STOP_EN		BIT(2)
#define DWMCI_RXDATA_START_BIT_SEL		BIT(1)
#define DWMCI_RDDQS_EN				BIT(0)
#define DWMCI_DDR200_RDDQS_EN_DEF	DWMCI_TXDT_CRC_TIMER_FASTLIMIT(0x12) | \
					DWMCI_TXDT_CRC_TIMER_INITVAL(0x14)
#define DWMCI_DDR200_DLINE_CTRL_DEF	DWMCI_FIFO_CLK_DELAY_CTRL(0x2) | \
					DWMCI_RD_DQS_DELAY_CTRL(0x40)

/* DDR200 Async FIFO Control */
#define DWMCI_ASYNC_FIFO_RESET		BIT(0)

/* DDR200 DLINE Control */
#define DWMCI_FIFO_CLK_DELAY_CTRL(x)	(((x) & 0x3) << 16)
#define DWMCI_RD_DQS_DELAY_CTRL(x)	((x) & 0x3FF)

static struct dw_mci_clk exynos_dwmci_clk_rates[] = {
	{25 * 1000 * 1000, 100 * 1000 * 1000},
	{50 * 1000 * 1000, 200 * 1000 * 1000},
	{50 * 1000 * 1000, 200 * 1000 * 1000},
	{100 * 1000 * 1000, 400 * 1000 * 1000},
	{200 * 1000 * 1000, 800 * 1000 * 1000},
	{100 * 1000 * 1000, 400 * 1000 * 1000},
	{200 * 1000 * 1000, 800 * 1000 * 1000},
	{400 * 1000 * 1000, 800 * 1000 * 1000},
};

static int exynos_dwmci_get_ocr(u32 slot_id)
{
	u32 ocr_avail = MMC_VDD_165_195 | MMC_VDD_32_33 | MMC_VDD_33_34;

	return ocr_avail;
}

static int exynos_dwmci_get_bus_wd(u32 slot_id)
{
	return 4;
}

static int exynos_dwmci_init(u32 slot_id, irq_handler_t handler, void *data)
{
	return 0;
}

static void exynos_dwmci_set_io_timing(void *data, unsigned char timing)
{
	struct dw_mci *host = (struct dw_mci *)data;
	struct dw_mci_board *pdata = host->pdata;
	struct dw_mci_clk *clk_tbl = pdata->clk_tbl;
	u32 clksel, rddqs, dline;
	u32 sclkin, cclkin;

	if (timing > MMC_TIMING_MMC_HS200_DDR) {
		pr_err("%s: timing(%d): not suppored\n", __func__, timing);
		return;
	}

	sclkin = clk_tbl[timing].sclkin;
	cclkin = clk_tbl[timing].cclkin;
	rddqs = DWMCI_DDR200_RDDQS_EN_DEF;
	dline = DWMCI_DDR200_DLINE_CTRL_DEF;
	clksel = __raw_readl(host->regs + DWMCI_CLKSEL);

	if (host->bus_hz != cclkin) {
		host->bus_hz = cclkin;
		host->current_speed = 0;
		clk_set_rate(host->cclk, sclkin);
	}

	if (timing == MMC_TIMING_MMC_HS200_DDR) {
		clksel = (pdata->ddr200_timing & 0xfffffff8) | (pdata->clk_smpl >> 1);
		rddqs |= DWMCI_RDDQS_EN;
		dline = DWMCI_FIFO_CLK_DELAY_CTRL(0x2) | DWMCI_RD_DQS_DELAY_CTRL(90);
		host->quirks &= ~DW_MCI_QUIRK_NO_DETECT_EBIT;
	} else if (timing == MMC_TIMING_MMC_HS200 ||
			timing == MMC_TIMING_UHS_SDR104) {
		clksel = (clksel & 0xfff8ffff) | (pdata->clk_drv << 16);
	} else if (timing == MMC_TIMING_UHS_SDR50) {
		clksel = (clksel & 0xfff8ffff) | (pdata->clk_drv << 16);
	} else if (timing == MMC_TIMING_UHS_DDR50) {
		clksel = pdata->ddr_timing;
	} else {
		clksel = pdata->sdr_timing;
	}

	__raw_writel(clksel, host->regs + DWMCI_CLKSEL);
	__raw_writel(rddqs, host->regs + DWMCI_DDR200_RDDQS_EN);
	__raw_writel(dline, host->regs + DWMCI_DDR200_DLINE_CTRL);
}

static struct dw_mci_board exynos4_dwmci_pdata = {
	.num_slots		= 1,
	.quirks			= DW_MCI_QUIRK_BROKEN_CARD_DETECTION,
	.bus_hz			= 100 * 1000 * 1000,
	.detect_delay_ms	= 200,
	.init			= exynos_dwmci_init,
	.get_bus_wd		= exynos_dwmci_get_bus_wd,
	.set_io_timing		= exynos_dwmci_set_io_timing,
	.clk_tbl		= exynos_dwmci_clk_rates,
};

static u64 exynos_dwmci_dmamask = DMA_BIT_MASK(32);

static struct resource exynos4_dwmci_resources[] = {
	[0] = DEFINE_RES_MEM(EXYNOS4_PA_DWMCI, SZ_4K),
	[1] = DEFINE_RES_IRQ(EXYNOS4_IRQ_DWMCI),
};

struct platform_device exynos4_device_dwmci = {
	.name		= "dw_mmc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(exynos4_dwmci_resources),
	.resource	= exynos4_dwmci_resources,
	.dev		= {
		.dma_mask		= &exynos_dwmci_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &exynos4_dwmci_pdata,
	},
};


#define EXYNOS5_DWMCI_RESOURCE(_channel)			\
static struct resource exynos5_dwmci##_channel##_resource[] = {	\
	[0] = DEFINE_RES_MEM(S3C_PA_HSMMC##_channel, SZ_4K),	\
	[1] = DEFINE_RES_IRQ(IRQ_HSMMC##_channel),		\
}

EXYNOS5_DWMCI_RESOURCE(0);
EXYNOS5_DWMCI_RESOURCE(1);
EXYNOS5_DWMCI_RESOURCE(2);
EXYNOS5_DWMCI_RESOURCE(3);

#define EXYNOS5_DWMCI_DEF_PLATDATA(_channel)			\
struct dw_mci_board exynos5_dwmci##_channel##_def_platdata = {	\
	.num_slots		= 1,				\
	.quirks			=				\
		DW_MCI_QUIRK_BROKEN_CARD_DETECTION,		\
	.bus_hz			= 200 * 1000 * 1000,		\
	.detect_delay_ms	= 200,				\
	.init			= exynos_dwmci_init,		\
	.get_bus_wd		= exynos_dwmci_get_bus_wd,	\
	.set_io_timing		= exynos_dwmci_set_io_timing,	\
	.get_ocr		= exynos_dwmci_get_ocr,		\
	.cd_type		= DW_MCI_CD_PERMANENT,		\
	.clk_tbl		= exynos_dwmci_clk_rates,	\
}

EXYNOS5_DWMCI_DEF_PLATDATA(0);
EXYNOS5_DWMCI_DEF_PLATDATA(1);
EXYNOS5_DWMCI_DEF_PLATDATA(2);
EXYNOS5_DWMCI_DEF_PLATDATA(3);

#define EXYNOS5_DWMCI_PLATFORM_DEVICE(_channel)			\
struct platform_device exynos5_device_dwmci##_channel =		\
{								\
	.name		= "dw_mmc",				\
	.id		= _channel,				\
	.num_resources	=					\
	ARRAY_SIZE(exynos5_dwmci##_channel##_resource),		\
	.resource	= exynos5_dwmci##_channel##_resource,	\
	.dev		= {					\
		.dma_mask		= &exynos_dwmci_dmamask,\
		.coherent_dma_mask	= DMA_BIT_MASK(32),	\
		.platform_data		=			\
			&exynos5_dwmci##_channel##_def_platdata,\
	},							\
}

EXYNOS5_DWMCI_PLATFORM_DEVICE(0);
EXYNOS5_DWMCI_PLATFORM_DEVICE(1);
EXYNOS5_DWMCI_PLATFORM_DEVICE(2);
EXYNOS5_DWMCI_PLATFORM_DEVICE(3);

static struct platform_device *exynos5_dwmci_devs[] = {
	&exynos5_device_dwmci0,
	&exynos5_device_dwmci1,
	&exynos5_device_dwmci2,
	&exynos5_device_dwmci3,
};

void __init exynos_dwmci_set_platdata(struct dw_mci_board *pd, u32 slot_id)
{
	struct dw_mci_board *npd = NULL;

	if ((soc_is_exynos4210()) || soc_is_exynos4212() ||
		soc_is_exynos4412()) {
		npd = s3c_set_platdata(pd, sizeof(struct dw_mci_board),
				&exynos4_device_dwmci);
	} else if (soc_is_exynos5250() || soc_is_exynos5410()) {
		if (slot_id < ARRAY_SIZE(exynos5_dwmci_devs))
			npd = s3c_set_platdata(pd, sizeof(struct dw_mci_board),
					       exynos5_dwmci_devs[slot_id]);
		else
			pr_err("%s: slot %d is not supported\n", __func__,
			       slot_id);
	}

	if (!npd)
		return;

	if (!npd->init)
		npd->init = exynos_dwmci_init;
	if (!npd->get_bus_wd)
		npd->get_bus_wd = exynos_dwmci_get_bus_wd;
	if (!npd->set_io_timing)
		npd->set_io_timing = exynos_dwmci_set_io_timing;
	if (!npd->get_ocr)
		npd->get_ocr = exynos_dwmci_get_ocr;
	if (!npd->clk_tbl)
		npd->clk_tbl = exynos_dwmci_clk_rates;
}

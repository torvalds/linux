/*
 * Samsung's S3C64XX generic DMA support using amba-pl08x driver.
 *
 * Copyright (c) 2013 Tomasz Figa <tomasz.figa@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl080.h>
#include <linux/amba/pl08x.h>
#include <linux/of.h>

#include <mach/irqs.h>
#include <mach/map.h>

#include "regs-sys.h"

static int pl08x_get_xfer_signal(const struct pl08x_channel_data *cd)
{
	return cd->min_signal;
}

static void pl08x_put_xfer_signal(const struct pl08x_channel_data *cd, int ch)
{
}

/*
 * DMA0
 */

static struct pl08x_channel_data s3c64xx_dma0_info[] = {
	{
		.bus_id = "uart0_tx",
		.min_signal = 0,
		.max_signal = 0,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "uart0_rx",
		.min_signal = 1,
		.max_signal = 1,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "uart1_tx",
		.min_signal = 2,
		.max_signal = 2,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "uart1_rx",
		.min_signal = 3,
		.max_signal = 3,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "uart2_tx",
		.min_signal = 4,
		.max_signal = 4,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "uart2_rx",
		.min_signal = 5,
		.max_signal = 5,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "uart3_tx",
		.min_signal = 6,
		.max_signal = 6,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "uart3_rx",
		.min_signal = 7,
		.max_signal = 7,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "pcm0_tx",
		.min_signal = 8,
		.max_signal = 8,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "pcm0_rx",
		.min_signal = 9,
		.max_signal = 9,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "i2s0_tx",
		.min_signal = 10,
		.max_signal = 10,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "i2s0_rx",
		.min_signal = 11,
		.max_signal = 11,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "spi0_tx",
		.min_signal = 12,
		.max_signal = 12,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "spi0_rx",
		.min_signal = 13,
		.max_signal = 13,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "i2s2_tx",
		.min_signal = 14,
		.max_signal = 14,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "i2s2_rx",
		.min_signal = 15,
		.max_signal = 15,
		.periph_buses = PL08X_AHB2,
	}
};

struct pl08x_platform_data s3c64xx_dma0_plat_data = {
	.memcpy_channel = {
		.bus_id = "memcpy",
		.cctl_memcpy =
			(PL080_BSIZE_4 << PL080_CONTROL_SB_SIZE_SHIFT |
			PL080_BSIZE_4 << PL080_CONTROL_DB_SIZE_SHIFT |
			PL080_WIDTH_32BIT << PL080_CONTROL_SWIDTH_SHIFT |
			PL080_WIDTH_32BIT << PL080_CONTROL_DWIDTH_SHIFT |
			PL080_CONTROL_PROT_BUFF | PL080_CONTROL_PROT_CACHE |
			PL080_CONTROL_PROT_SYS),
	},
	.lli_buses = PL08X_AHB1,
	.mem_buses = PL08X_AHB1,
	.get_xfer_signal = pl08x_get_xfer_signal,
	.put_xfer_signal = pl08x_put_xfer_signal,
	.slave_channels = s3c64xx_dma0_info,
	.num_slave_channels = ARRAY_SIZE(s3c64xx_dma0_info),
};

static AMBA_AHB_DEVICE(s3c64xx_dma0, "dma-pl080s.0", 0,
			0x75000000, {IRQ_DMA0}, &s3c64xx_dma0_plat_data);

/*
 * DMA1
 */

static struct pl08x_channel_data s3c64xx_dma1_info[] = {
	{
		.bus_id = "pcm1_tx",
		.min_signal = 0,
		.max_signal = 0,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "pcm1_rx",
		.min_signal = 1,
		.max_signal = 1,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "i2s1_tx",
		.min_signal = 2,
		.max_signal = 2,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "i2s1_rx",
		.min_signal = 3,
		.max_signal = 3,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "spi1_tx",
		.min_signal = 4,
		.max_signal = 4,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "spi1_rx",
		.min_signal = 5,
		.max_signal = 5,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ac97_out",
		.min_signal = 6,
		.max_signal = 6,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ac97_in",
		.min_signal = 7,
		.max_signal = 7,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "ac97_mic",
		.min_signal = 8,
		.max_signal = 8,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "pwm",
		.min_signal = 9,
		.max_signal = 9,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "irda",
		.min_signal = 10,
		.max_signal = 10,
		.periph_buses = PL08X_AHB2,
	}, {
		.bus_id = "external",
		.min_signal = 11,
		.max_signal = 11,
		.periph_buses = PL08X_AHB2,
	},
};

struct pl08x_platform_data s3c64xx_dma1_plat_data = {
	.memcpy_channel = {
		.bus_id = "memcpy",
		.cctl_memcpy =
			(PL080_BSIZE_4 << PL080_CONTROL_SB_SIZE_SHIFT |
			PL080_BSIZE_4 << PL080_CONTROL_DB_SIZE_SHIFT |
			PL080_WIDTH_32BIT << PL080_CONTROL_SWIDTH_SHIFT |
			PL080_WIDTH_32BIT << PL080_CONTROL_DWIDTH_SHIFT |
			PL080_CONTROL_PROT_BUFF | PL080_CONTROL_PROT_CACHE |
			PL080_CONTROL_PROT_SYS),
	},
	.lli_buses = PL08X_AHB1,
	.mem_buses = PL08X_AHB1,
	.get_xfer_signal = pl08x_get_xfer_signal,
	.put_xfer_signal = pl08x_put_xfer_signal,
	.slave_channels = s3c64xx_dma1_info,
	.num_slave_channels = ARRAY_SIZE(s3c64xx_dma1_info),
};

static AMBA_AHB_DEVICE(s3c64xx_dma1, "dma-pl080s.1", 0,
			0x75100000, {IRQ_DMA1}, &s3c64xx_dma1_plat_data);

static int __init s3c64xx_pl080_init(void)
{
	/* Set all DMA configuration to be DMA, not SDMA */
	writel(0xffffff, S3C64XX_SDMA_SEL);

	if (of_have_populated_dt())
		return 0;

	amba_device_register(&s3c64xx_dma0_device, &iomem_resource);
	amba_device_register(&s3c64xx_dma1_device, &iomem_resource);

	return 0;
}
arch_initcall(s3c64xx_pl080_init);

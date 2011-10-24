/* linux/arch/arm/mach-exynos4/dma.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Copyright (C) 2010 Samsung Electronics Co. Ltd.
 *	Jaswinder Singh <jassi.brar@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/dma-mapping.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl330.h>

#include <asm/irq.h>
#include <plat/devs.h>
#include <plat/irqs.h>

#include <mach/map.h>
#include <mach/irqs.h>
#include <mach/dma.h>

static u64 dma_dmamask = DMA_BIT_MASK(32);

u8 pdma0_peri[] = {
	DMACH_PCM0_RX,
	DMACH_PCM0_TX,
	DMACH_PCM2_RX,
	DMACH_PCM2_TX,
	DMACH_MSM_REQ0,
	DMACH_MSM_REQ2,
	DMACH_SPI0_RX,
	DMACH_SPI0_TX,
	DMACH_SPI2_RX,
	DMACH_SPI2_TX,
	DMACH_I2S0S_TX,
	DMACH_I2S0_RX,
	DMACH_I2S0_TX,
	DMACH_I2S2_RX,
	DMACH_I2S2_TX,
	DMACH_UART0_RX,
	DMACH_UART0_TX,
	DMACH_UART2_RX,
	DMACH_UART2_TX,
	DMACH_UART4_RX,
	DMACH_UART4_TX,
	DMACH_SLIMBUS0_RX,
	DMACH_SLIMBUS0_TX,
	DMACH_SLIMBUS2_RX,
	DMACH_SLIMBUS2_TX,
	DMACH_SLIMBUS4_RX,
	DMACH_SLIMBUS4_TX,
	DMACH_AC97_MICIN,
	DMACH_AC97_PCMIN,
	DMACH_AC97_PCMOUT,
};

struct dma_pl330_platdata exynos4_pdma0_pdata = {
	.nr_valid_peri = ARRAY_SIZE(pdma0_peri),
	.peri_id = pdma0_peri,
};

struct amba_device exynos4_device_pdma0 = {
	.dev = {
		.init_name = "dma-pl330.0",
		.dma_mask = &dma_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data = &exynos4_pdma0_pdata,
	},
	.res = {
		.start = EXYNOS4_PA_PDMA0,
		.end = EXYNOS4_PA_PDMA0 + SZ_4K,
		.flags = IORESOURCE_MEM,
	},
	.irq = {IRQ_PDMA0, NO_IRQ},
	.periphid = 0x00041330,
};

u8 pdma1_peri[] = {
	DMACH_PCM0_RX,
	DMACH_PCM0_TX,
	DMACH_PCM1_RX,
	DMACH_PCM1_TX,
	DMACH_MSM_REQ1,
	DMACH_MSM_REQ3,
	DMACH_SPI1_RX,
	DMACH_SPI1_TX,
	DMACH_I2S0S_TX,
	DMACH_I2S0_RX,
	DMACH_I2S0_TX,
	DMACH_I2S1_RX,
	DMACH_I2S1_TX,
	DMACH_UART0_RX,
	DMACH_UART0_TX,
	DMACH_UART1_RX,
	DMACH_UART1_TX,
	DMACH_UART3_RX,
	DMACH_UART3_TX,
	DMACH_SLIMBUS1_RX,
	DMACH_SLIMBUS1_TX,
	DMACH_SLIMBUS3_RX,
	DMACH_SLIMBUS3_TX,
	DMACH_SLIMBUS5_RX,
	DMACH_SLIMBUS5_TX,
};

struct dma_pl330_platdata exynos4_pdma1_pdata = {
	.nr_valid_peri = ARRAY_SIZE(pdma1_peri),
	.peri_id = pdma1_peri,
};

struct amba_device exynos4_device_pdma1 = {
	.dev = {
		.init_name = "dma-pl330.1",
		.dma_mask = &dma_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data = &exynos4_pdma1_pdata,
	},
	.res = {
		.start = EXYNOS4_PA_PDMA1,
		.end = EXYNOS4_PA_PDMA1 + SZ_4K,
		.flags = IORESOURCE_MEM,
	},
	.irq = {IRQ_PDMA1, NO_IRQ},
	.periphid = 0x00041330,
};

static int __init exynos4_dma_init(void)
{
	dma_cap_set(DMA_SLAVE, exynos4_pdma0_pdata.cap_mask);
	dma_cap_set(DMA_CYCLIC, exynos4_pdma0_pdata.cap_mask);
	amba_device_register(&exynos4_device_pdma0, &iomem_resource);

	dma_cap_set(DMA_SLAVE, exynos4_pdma1_pdata.cap_mask);
	dma_cap_set(DMA_CYCLIC, exynos4_pdma1_pdata.cap_mask);
	amba_device_register(&exynos4_device_pdma1, &iomem_resource);

	return 0;
}
arch_initcall(exynos4_dma_init);

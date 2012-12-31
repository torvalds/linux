/* linux/arch/arm/mach-exynos/dma.c
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

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include <plat/devs.h>
#include <plat/irqs.h>
#ifdef CONFIG_EXYNOS_DEV_PD
#include <plat/pd.h>
#endif

#include <mach/map.h>
#include <mach/irqs.h>

#include <plat/cpu.h>
#include <plat/s3c-pl330-pdata.h>

static u64 dma_dmamask = DMA_BIT_MASK(32);

static struct resource exynos_mdma_resource[] = {
	[0] = {
		.start  = S5P_PA_MDMA1,
		.end    = S5P_PA_MDMA1 + SZ_4K,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_MDMA1,
		.end	= IRQ_MDMA1,
		.flags	= IORESOURCE_IRQ,
	},
};

struct s3c_pl330_platdata exynos_mdma_pdata = {
	.peri = {
		/*
		 * The DMAC can have max 8 channel so there
		 * can be MAX 8 M<->M requests served at any time.
		 *
		 * Always keep the first 8 M->M channels on the
		 * DMAC dedicated for M->M transfers.
		 */
		[0] = DMACH_MTOM_0,
		[1] = DMACH_MTOM_1,
		[2] = DMACH_MTOM_2,
		[3] = DMACH_MTOM_3,
		[4] = DMACH_MTOM_4,
		[5] = DMACH_MTOM_5,
		[6] = DMACH_MTOM_6,
		[7] = DMACH_MTOM_7,
		[8] = DMACH_MAX,
		[9] = DMACH_MAX,
		[10] = DMACH_MAX,
		[11] = DMACH_MAX,
		[12] = DMACH_MAX,
		[13] = DMACH_MAX,
		[14] = DMACH_MAX,
		[15] = DMACH_MAX,
		[16] = DMACH_MAX,
		[17] = DMACH_MAX,
		[18] = DMACH_MAX,
		[19] = DMACH_MAX,
		[20] = DMACH_MAX,
		[21] = DMACH_MAX,
		[22] = DMACH_MAX,
		[23] = DMACH_MAX,
		[24] = DMACH_MAX,
		[25] = DMACH_MAX,
		[26] = DMACH_MAX,
		[27] = DMACH_MAX,
		[28] = DMACH_MAX,
		[29] = DMACH_MAX,
		[30] = DMACH_MAX,
		[31] = DMACH_MAX,
	},
};

struct platform_device exynos_device_mdma = {
	.name		= "s3c-pl330",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(exynos_mdma_resource),
	.resource	= exynos_mdma_resource,
	.dev		= {
		.dma_mask = &dma_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data = &exynos_mdma_pdata,
#if defined(CONFIG_ARCH_EXYNOS4) && defined(CONFIG_EXYNOS_DEV_PD)
		.parent = &exynos4_device_pd[PD_LCD0].dev,
#endif
	},
};

static struct resource exynos_pdma0_resource[] = {
	[0] = {
		.start	= S5P_PA_PDMA0,
		.end	= S5P_PA_PDMA0 + SZ_4K,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_PDMA0,
		.end	= IRQ_PDMA0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct s3c_pl330_platdata exynos4210_pdma0_pdata = {
	.peri = {
		[0] = DMACH_PCM0_RX,
		[1] = DMACH_PCM0_TX,
		[2] = DMACH_PCM2_RX,
		[3] = DMACH_PCM2_TX,
		[4] = DMACH_MSM_REQ0,
		[5] = DMACH_MSM_REQ2,
		[6] = DMACH_SPI0_RX,
		[7] = DMACH_SPI0_TX,
		[8] = DMACH_SPI2_RX,
		[9] = DMACH_SPI2_TX,
		[10] = DMACH_I2S0S_TX,
		[11] = DMACH_I2S0_RX,
		[12] = DMACH_I2S0_TX,
		[13] = DMACH_I2S2_RX,
		[14] = DMACH_I2S2_TX,
		[15] = DMACH_UART0_RX,
		[16] = DMACH_UART0_TX,
		[17] = DMACH_UART2_RX,
		[18] = DMACH_UART2_TX,
		[19] = DMACH_UART4_RX,
		[20] = DMACH_UART4_TX,
		[21] = DMACH_SLIMBUS0_RX,
		[22] = DMACH_SLIMBUS0_TX,
		[23] = DMACH_SLIMBUS2_RX,
		[24] = DMACH_SLIMBUS2_TX,
		[25] = DMACH_SLIMBUS4_RX,
		[26] = DMACH_SLIMBUS4_TX,
		[27] = DMACH_AC97_MICIN,
		[28] = DMACH_AC97_PCMIN,
		[29] = DMACH_AC97_PCMOUT,
		[30] = DMACH_MAX,
		[31] = DMACH_MAX,
	},
};

static struct s3c_pl330_platdata exynos4212_pdma0_pdata = {
	.peri = {
		[0] = DMACH_PCM0_RX,
		[1] = DMACH_PCM0_TX,
		[2] = DMACH_PCM2_RX,
		[3] = DMACH_PCM2_TX,
		[4] = DMACH_MIPI_HSI0,
		[5] = DMACH_MIPI_HSI1,
		[6] = DMACH_SPI0_RX,
		[7] = DMACH_SPI0_TX,
		[8] = DMACH_SPI2_RX,
		[9] = DMACH_SPI2_TX,
		[10] = DMACH_I2S0S_TX,
		[11] = DMACH_I2S0_RX,
		[12] = DMACH_I2S0_TX,
		[13] = DMACH_I2S2_RX,
		[14] = DMACH_I2S2_TX,
		[15] = DMACH_UART0_RX,
		[16] = DMACH_UART0_TX,
		[17] = DMACH_UART2_RX,
		[18] = DMACH_UART2_TX,
		[19] = DMACH_UART4_RX,
		[20] = DMACH_UART4_TX,
		[21] = DMACH_SLIMBUS0_RX,
		[22] = DMACH_SLIMBUS0_TX,
		[23] = DMACH_SLIMBUS2_RX,
		[24] = DMACH_SLIMBUS2_TX,
		[25] = DMACH_SLIMBUS4_RX,
		[26] = DMACH_SLIMBUS4_TX,
		[27] = DMACH_AC97_MICIN,
		[28] = DMACH_AC97_PCMIN,
		[29] = DMACH_AC97_PCMOUT,
		[30] = DMACH_MIPI_HSI4,
		[31] = DMACH_MIPI_HSI5,
	},
};

static struct s3c_pl330_platdata exynos5250_pdma0_pdata = {
	.peri = {
		[0] = DMACH_PCM0_RX,
		[1] = DMACH_PCM0_TX,
		[2] = DMACH_PCM2_RX,
		[3] = DMACH_PCM2_TX,
		[4] = DMACH_SPI0_RX,
		[5] = DMACH_SPI0_TX,
		[6] = DMACH_SPI2_RX,
		[7] = DMACH_SPI2_TX,
		[8] = DMACH_I2S0S_TX,
		[9] = DMACH_I2S0_RX,
		[10] = DMACH_I2S0_TX,
		[11] = DMACH_I2S2_RX,
		[12] = DMACH_I2S2_TX,
		[13] = DMACH_UART0_RX,
		[14] = DMACH_UART0_TX,
		[15] = DMACH_UART2_RX,
		[16] = DMACH_UART2_TX,
		[17] = DMACH_UART4_RX,
		[18] = DMACH_UART4_TX,
		[19] = DMACH_SLIMBUS0_RX,
		[20] = DMACH_SLIMBUS0_TX,
		[21] = DMACH_SLIMBUS2_RX,
		[22] = DMACH_SLIMBUS2_TX,
		[23] = DMACH_SLIMBUS4_RX,
		[24] = DMACH_SLIMBUS4_TX,
		[25] = DMACH_AC97_MICIN,
		[26] = DMACH_AC97_PCMIN,
		[27] = DMACH_AC97_PCMOUT,
		[28] = DMACH_MIPI_HSI0,
		[29] = DMACH_MIPI_HSI2,
		[30] = DMACH_MIPI_HSI4,
		[31] = DMACH_MIPI_HSI6,
	},
};

struct platform_device exynos_device_pdma0 = {
	.name		= "s3c-pl330",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(exynos_pdma0_resource),
	.resource	= exynos_pdma0_resource,
	.dev		= {
		.dma_mask = &dma_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct resource exynos_pdma1_resource[] = {
	[0] = {
		.start	= S5P_PA_PDMA1,
		.end	= S5P_PA_PDMA1 + SZ_4K,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_PDMA1,
		.end	= IRQ_PDMA1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct s3c_pl330_platdata exynos4210_pdma1_pdata = {
	.peri = {
		[0] = DMACH_PCM0_RX,
		[1] = DMACH_PCM0_TX,
		[2] = DMACH_PCM1_RX,
		[3] = DMACH_PCM1_TX,
		[4] = DMACH_MSM_REQ1,
		[5] = DMACH_MSM_REQ3,
		[6] = DMACH_SPI1_RX,
		[7] = DMACH_SPI1_TX,
		[8] = DMACH_I2S0S_TX,
		[9] = DMACH_I2S0_RX,
		[10] = DMACH_I2S0_TX,
		[11] = DMACH_I2S1_RX,
		[12] = DMACH_I2S1_TX,
		[13] = DMACH_UART0_RX,
		[14] = DMACH_UART0_TX,
		[15] = DMACH_UART1_RX,
		[16] = DMACH_UART1_TX,
		[17] = DMACH_UART3_RX,
		[18] = DMACH_UART3_TX,
		[19] = DMACH_SLIMBUS1_RX,
		[20] = DMACH_SLIMBUS1_TX,
		[21] = DMACH_SLIMBUS3_RX,
		[22] = DMACH_SLIMBUS3_TX,
		[23] = DMACH_SLIMBUS5_RX,
		[24] = DMACH_SLIMBUS5_TX,
		[25] = DMACH_SLIMBUS0AUX_RX,
		[26] = DMACH_SLIMBUS0AUX_TX,
		[27] = DMACH_SPDIF,
		[28] = DMACH_MAX,
		[29] = DMACH_MAX,
		[30] = DMACH_MAX,
		[31] = DMACH_MAX,
	},
};

static struct s3c_pl330_platdata exynos4212_pdma1_pdata = {
	.peri = {
		[0] = DMACH_PCM0_RX,
		[1] = DMACH_PCM0_TX,
		[2] = DMACH_PCM1_RX,
		[3] = DMACH_PCM1_TX,
		[4] = DMACH_MIPI_HSI2,
		[5] = DMACH_MIPI_HSI3,
		[6] = DMACH_SPI1_RX,
		[7] = DMACH_SPI1_TX,
		[8] = DMACH_I2S0S_TX,
		[9] = DMACH_I2S0_RX,
		[10] = DMACH_I2S0_TX,
		[11] = DMACH_I2S1_RX,
		[12] = DMACH_I2S1_TX,
		[13] = DMACH_UART0_RX,
		[14] = DMACH_UART0_TX,
		[15] = DMACH_UART1_RX,
		[16] = DMACH_UART1_TX,
		[17] = DMACH_UART3_RX,
		[18] = DMACH_UART3_TX,
		[19] = DMACH_SLIMBUS1_RX,
		[20] = DMACH_SLIMBUS1_TX,
		[21] = DMACH_SLIMBUS3_RX,
		[22] = DMACH_SLIMBUS3_TX,
		[23] = DMACH_SLIMBUS5_RX,
		[24] = DMACH_SLIMBUS5_TX,
		[25] = DMACH_SLIMBUS0AUX_RX,
		[26] = DMACH_SLIMBUS0AUX_TX,
		[27] = DMACH_SPDIF,
		[28] = DMACH_MIPI_HSI6,
		[29] = DMACH_MIPI_HSI7,
		[30] = DMACH_MAX,
		[31] = DMACH_MAX,
	},
};

static struct s3c_pl330_platdata exynos5250_pdma1_pdata = {
	.peri = {
		[0] = DMACH_PCM0_RX,
		[1] = DMACH_PCM0_TX,
		[2] = DMACH_PCM1_RX,
		[3] = DMACH_PCM1_TX,
		[4] = DMACH_SPI1_RX,
		[5] = DMACH_SPI1_TX,
		[6] = DMACH_PWM,
		[7] = DMACH_SPDIF,
		[8] = DMACH_I2S0S_TX,
		[9] = DMACH_I2S0_RX,
		[10] = DMACH_I2S0_TX,
		[11] = DMACH_I2S1_RX,
		[12] = DMACH_I2S1_TX,
		[13] = DMACH_UART0_RX,
		[14] = DMACH_UART0_TX,
		[15] = DMACH_UART1_RX,
		[16] = DMACH_UART1_TX,
		[17] = DMACH_UART3_RX,
		[18] = DMACH_UART3_TX,
		[19] = DMACH_SLIMBUS1_RX,
		[20] = DMACH_SLIMBUS1_TX,
		[21] = DMACH_SLIMBUS3_RX,
		[22] = DMACH_SLIMBUS3_TX,
		[23] = DMACH_SLIMBUS5_RX,
		[24] = DMACH_SLIMBUS5_TX,
		[25] = DMACH_SLIMBUS0AUX_RX,
		[26] = DMACH_SLIMBUS0AUX_TX,
		[27] = DMACH_DISP1,
		[28] = DMACH_MIPI_HSI1,
		[29] = DMACH_MIPI_HSI3,
		[30] = DMACH_MIPI_HSI5,
		[31] = DMACH_MIPI_HSI7,
	},
};

struct platform_device exynos_device_pdma1 = {
	.name		= "s3c-pl330",
	.id		= 2,
	.num_resources	= ARRAY_SIZE(exynos_pdma1_resource),
	.resource	= exynos_pdma1_resource,
	.dev		= {
		.dma_mask = &dma_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct platform_device *exynos_dmacs[] __initdata = {
	&exynos_device_mdma,
	&exynos_device_pdma0,
	&exynos_device_pdma1,
};

static int __init exynos_dma_init(void)
{
	if (soc_is_exynos4210()) {
		exynos_device_pdma0.dev.platform_data = &exynos4210_pdma0_pdata;
		exynos_device_pdma1.dev.platform_data = &exynos4210_pdma1_pdata;
	} else if (soc_is_exynos4212() || soc_is_exynos4412()) {
		exynos_device_pdma0.dev.platform_data = &exynos4212_pdma0_pdata;
		exynos_device_pdma1.dev.platform_data = &exynos4212_pdma1_pdata;
	} else if (soc_is_exynos5250()) {
		exynos_device_pdma0.dev.platform_data = &exynos5250_pdma0_pdata;
		exynos_device_pdma1.dev.platform_data = &exynos5250_pdma1_pdata;
	}

	return platform_add_devices(exynos_dmacs, ARRAY_SIZE(exynos_dmacs));
}
arch_initcall(exynos_dma_init);

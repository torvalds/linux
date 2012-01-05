/* linux/arch/arm/mach-s5p64x0/dma.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
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

#include <mach/map.h>
#include <mach/irqs.h>
#include <mach/regs-clock.h>
#include <mach/dma.h>

#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/irqs.h>

static u64 dma_dmamask = DMA_BIT_MASK(32);

struct dma_pl330_peri s5p6440_pdma_peri[22] = {
	{
		.peri_id = (u8)DMACH_UART0_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_UART0_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_UART1_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_UART1_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_UART2_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_UART2_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_UART3_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_UART3_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = DMACH_MAX,
	}, {
		.peri_id = DMACH_MAX,
	}, {
		.peri_id = (u8)DMACH_PCM0_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_PCM0_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_I2S0_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_I2S0_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_SPI0_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_SPI0_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_MAX,
	}, {
		.peri_id = (u8)DMACH_MAX,
	}, {
		.peri_id = (u8)DMACH_MAX,
	}, {
		.peri_id = (u8)DMACH_MAX,
	}, {
		.peri_id = (u8)DMACH_SPI1_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_SPI1_RX,
		.rqtype = DEVTOMEM,
	},
};

struct dma_pl330_platdata s5p6440_pdma_pdata = {
	.nr_valid_peri = ARRAY_SIZE(s5p6440_pdma_peri),
	.peri = s5p6440_pdma_peri,
};

struct dma_pl330_peri s5p6450_pdma_peri[32] = {
	{
		.peri_id = (u8)DMACH_UART0_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_UART0_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_UART1_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_UART1_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_UART2_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_UART2_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_UART3_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_UART3_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_UART4_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_UART4_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_PCM0_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_PCM0_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_I2S0_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_I2S0_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_SPI0_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_SPI0_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_PCM1_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_PCM1_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_PCM2_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_PCM2_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_SPI1_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_SPI1_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_USI_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_USI_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_MAX,
	}, {
		.peri_id = (u8)DMACH_I2S1_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_I2S1_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_I2S2_TX,
		.rqtype = MEMTODEV,
	}, {
		.peri_id = (u8)DMACH_I2S2_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_PWM,
	}, {
		.peri_id = (u8)DMACH_UART5_RX,
		.rqtype = DEVTOMEM,
	}, {
		.peri_id = (u8)DMACH_UART5_TX,
		.rqtype = MEMTODEV,
	},
};

struct dma_pl330_platdata s5p6450_pdma_pdata = {
	.nr_valid_peri = ARRAY_SIZE(s5p6450_pdma_peri),
	.peri = s5p6450_pdma_peri,
};

struct amba_device s5p64x0_device_pdma = {
	.dev = {
		.init_name = "dma-pl330",
		.dma_mask = &dma_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.res = {
		.start = S5P64X0_PA_PDMA,
		.end = S5P64X0_PA_PDMA + SZ_4K,
		.flags = IORESOURCE_MEM,
	},
	.irq = {IRQ_DMA0, NO_IRQ},
	.periphid = 0x00041330,
};

static int __init s5p64x0_dma_init(void)
{
	if (soc_is_s5p6450())
		s5p64x0_device_pdma.dev.platform_data = &s5p6450_pdma_pdata;
	else
		s5p64x0_device_pdma.dev.platform_data = &s5p6440_pdma_pdata;

	amba_device_register(&s5p64x0_device_pdma, &iomem_resource);

	return 0;
}
arch_initcall(s5p64x0_dma_init);

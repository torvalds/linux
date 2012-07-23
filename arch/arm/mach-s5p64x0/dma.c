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

static u8 s5p6440_pdma_peri[] = {
	DMACH_UART0_RX,
	DMACH_UART0_TX,
	DMACH_UART1_RX,
	DMACH_UART1_TX,
	DMACH_UART2_RX,
	DMACH_UART2_TX,
	DMACH_UART3_RX,
	DMACH_UART3_TX,
	DMACH_MAX,
	DMACH_MAX,
	DMACH_PCM0_TX,
	DMACH_PCM0_RX,
	DMACH_I2S0_TX,
	DMACH_I2S0_RX,
	DMACH_SPI0_TX,
	DMACH_SPI0_RX,
	DMACH_MAX,
	DMACH_MAX,
	DMACH_MAX,
	DMACH_MAX,
	DMACH_SPI1_TX,
	DMACH_SPI1_RX,
};

static struct dma_pl330_platdata s5p6440_pdma_pdata = {
	.nr_valid_peri = ARRAY_SIZE(s5p6440_pdma_peri),
	.peri_id = s5p6440_pdma_peri,
};

static u8 s5p6450_pdma_peri[] = {
	DMACH_UART0_RX,
	DMACH_UART0_TX,
	DMACH_UART1_RX,
	DMACH_UART1_TX,
	DMACH_UART2_RX,
	DMACH_UART2_TX,
	DMACH_UART3_RX,
	DMACH_UART3_TX,
	DMACH_UART4_RX,
	DMACH_UART4_TX,
	DMACH_PCM0_TX,
	DMACH_PCM0_RX,
	DMACH_I2S0_TX,
	DMACH_I2S0_RX,
	DMACH_SPI0_TX,
	DMACH_SPI0_RX,
	DMACH_PCM1_TX,
	DMACH_PCM1_RX,
	DMACH_PCM2_TX,
	DMACH_PCM2_RX,
	DMACH_SPI1_TX,
	DMACH_SPI1_RX,
	DMACH_USI_TX,
	DMACH_USI_RX,
	DMACH_MAX,
	DMACH_I2S1_TX,
	DMACH_I2S1_RX,
	DMACH_I2S2_TX,
	DMACH_I2S2_RX,
	DMACH_PWM,
	DMACH_UART5_RX,
	DMACH_UART5_TX,
};

static struct dma_pl330_platdata s5p6450_pdma_pdata = {
	.nr_valid_peri = ARRAY_SIZE(s5p6450_pdma_peri),
	.peri_id = s5p6450_pdma_peri,
};

static AMBA_AHB_DEVICE(s5p64x0_pdma, "dma-pl330", 0x00041330,
	S5P64X0_PA_PDMA, {IRQ_DMA0}, NULL);

static int __init s5p64x0_dma_init(void)
{
	if (soc_is_s5p6450()) {
		dma_cap_set(DMA_SLAVE, s5p6450_pdma_pdata.cap_mask);
		dma_cap_set(DMA_CYCLIC, s5p6450_pdma_pdata.cap_mask);
		s5p64x0_pdma_device.dev.platform_data = &s5p6450_pdma_pdata;
	} else {
		dma_cap_set(DMA_SLAVE, s5p6440_pdma_pdata.cap_mask);
		dma_cap_set(DMA_CYCLIC, s5p6440_pdma_pdata.cap_mask);
		s5p64x0_pdma_device.dev.platform_data = &s5p6440_pdma_pdata;
	}

	amba_device_register(&s5p64x0_pdma_device, &iomem_resource);

	return 0;
}
arch_initcall(s5p64x0_dma_init);

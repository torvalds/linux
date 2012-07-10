/* arch/arm/mach-rk2928/devices.c
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#ifdef CONFIG_USB_ANDROID
#include <linux/usb/android_composite.h>
#endif
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <asm/pmu.h>
#include <mach/irqs.h>
#include <mach/board.h>
#include <mach/dma-pl330.h>
#include <mach/gpio.h>
//#include <mach/iomux.h>
#include <plat/rk_fiq_debugger.h>


static u64 dma_dmamask = DMA_BIT_MASK(32);

static struct resource resource_dmac[] = {
	[0] = {
		.start  = RK2928_DMAC_PHYS,
		.end    = RK2928_DMAC_PHYS + RK2928_DMAC_SIZE -1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_DMAC_0,
		.end	= IRQ_DMAC_1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct rk29_pl330_platdata dmac_pdata = {
	.peri = {
		[0] = DMACH_I2S0_8CH_TX,
		[1] = DMACH_I2S0_8CH_RX,
		[2] = DMACH_UART0_TX,
		[3] = DMACH_UART0_RX,
		[4] = DMACH_UART1_TX,
		[5] = DMACH_UART1_RX,
		[6] = DMACH_UART2_TX,
		[7] = DMACH_UART2_RX,
		[8] = DMACH_SPI0_TX,
		[9] = DMACH_SPI0_RX,
		[10] = DMACH_SDMMC,
		[11] = DMACH_SDIO,
		[12] = DMACH_EMMC,
		[13] = DMACH_DMAC1_MEMTOMEM,
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

static struct platform_device device_dmac = {
	.name		= "rk29-pl330",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(resource_dmac),
	.resource	= resource_dmac,
	.dev		= {
		.dma_mask = &dma_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data = &dmac_pdata,
	},
};

static struct platform_device *rk2928_dmacs[] __initdata = {
	&device_dmac,
};

static void __init rk2928_init_dma(void)
{
	platform_add_devices(rk2928_dmacs, ARRAY_SIZE(rk2928_dmacs));
}
static int __init rk2928_init_devices(void)
{
	rk2928_init_dma();
#if defined(CONFIG_FIQ_DEBUGGER) && defined(DEBUG_UART_PHYS)
	rk_serial_debug_init(DEBUG_UART_BASE, IRQ_DEBUG_UART, IRQ_UART_SIGNAL, -1);
#endif
	return 0;
}
arch_initcall(rk2928_init_devices);

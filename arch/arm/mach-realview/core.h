/*
 *  linux/arch/arm/mach-realview/core.h
 *
 *  Copyright (C) 2004 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ASM_ARCH_REALVIEW_H
#define __ASM_ARCH_REALVIEW_H

#include <asm/hardware/amba.h>
#include <asm/leds.h>
#include <asm/io.h>

extern struct sys_timer realview_timer;

#define AMBA_DEVICE(name,busid,base,plat)			\
static struct amba_device name##_device = {			\
	.dev		= {					\
		.coherent_dma_mask = ~0,			\
		.bus_id	= busid,				\
		.platform_data = plat,				\
	},							\
	.res		= {					\
		.start	= REALVIEW_##base##_BASE,		\
		.end	= (REALVIEW_##base##_BASE) + SZ_4K - 1,\
		.flags	= IORESOURCE_MEM,			\
	},							\
	.dma_mask	= ~0,					\
	.irq		= base##_IRQ,				\
	/* .dma		= base##_DMA,*/				\
}

/*
 * These devices are connected via the core APB bridge
 */
#define GPIO2_IRQ	{ IRQ_GPIOINT2, NO_IRQ }
#define GPIO2_DMA	{ 0, 0 }
#define GPIO3_IRQ	{ IRQ_GPIOINT3, NO_IRQ }
#define GPIO3_DMA	{ 0, 0 }

#define AACI_IRQ	{ IRQ_AACI, NO_IRQ }
#define AACI_DMA	{ 0x80, 0x81 }
#define MMCI0_IRQ	{ IRQ_MMCI0A,IRQ_MMCI0B }
#define MMCI0_DMA	{ 0x84, 0 }
#define KMI0_IRQ	{ IRQ_KMI0, NO_IRQ }
#define KMI0_DMA	{ 0, 0 }
#define KMI1_IRQ	{ IRQ_KMI1, NO_IRQ }
#define KMI1_DMA	{ 0, 0 }

/*
 * These devices are connected directly to the multi-layer AHB switch
 */
#define SMC_IRQ		{ NO_IRQ, NO_IRQ }
#define SMC_DMA		{ 0, 0 }
#define MPMC_IRQ	{ NO_IRQ, NO_IRQ }
#define MPMC_DMA	{ 0, 0 }
#define CLCD_IRQ	{ IRQ_CLCDINT, NO_IRQ }
#define CLCD_DMA	{ 0, 0 }
#define DMAC_IRQ	{ IRQ_DMAINT, NO_IRQ }
#define DMAC_DMA	{ 0, 0 }

/*
 * These devices are connected via the core APB bridge
 */
#define SCTL_IRQ	{ NO_IRQ, NO_IRQ }
#define SCTL_DMA	{ 0, 0 }
#define WATCHDOG_IRQ	{ IRQ_WDOGINT, NO_IRQ }
#define WATCHDOG_DMA	{ 0, 0 }
#define GPIO0_IRQ	{ IRQ_GPIOINT0, NO_IRQ }
#define GPIO0_DMA	{ 0, 0 }
#define GPIO1_IRQ	{ IRQ_GPIOINT1, NO_IRQ }
#define GPIO1_DMA	{ 0, 0 }
#define RTC_IRQ		{ IRQ_RTCINT, NO_IRQ }
#define RTC_DMA		{ 0, 0 }

/*
 * These devices are connected via the DMA APB bridge
 */
#define SCI_IRQ		{ IRQ_SCIINT, NO_IRQ }
#define SCI_DMA		{ 7, 6 }
#define UART0_IRQ	{ IRQ_UARTINT0, NO_IRQ }
#define UART0_DMA	{ 15, 14 }
#define UART1_IRQ	{ IRQ_UARTINT1, NO_IRQ }
#define UART1_DMA	{ 13, 12 }
#define UART2_IRQ	{ IRQ_UARTINT2, NO_IRQ }
#define UART2_DMA	{ 11, 10 }
#define UART3_IRQ	{ IRQ_UART3, NO_IRQ }
#define UART3_DMA	{ 0x86, 0x87 }
#define SSP_IRQ		{ IRQ_SSPINT, NO_IRQ }
#define SSP_DMA		{ 9, 8 }


extern struct platform_device realview_flash_device;
extern struct platform_device realview_smc91x_device;
extern struct mmc_platform_data realview_mmc0_plat_data;
extern struct mmc_platform_data realview_mmc1_plat_data;
extern struct clk realview_clcd_clk;
extern struct clcd_board clcd_plat_data;

extern void realview_leds_event(led_event_t ledevt);

#endif

/*
 *  linux/arch/arm/mach-mmp/pxa168.c
 *
 *  Code specific to PXA168
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/clk.h>

#include <asm/mach/time.h>
#include <mach/addr-map.h>
#include <mach/cputype.h>
#include <mach/regs-apbc.h>
#include <mach/irqs.h>
#include <mach/dma.h>
#include <mach/devices.h>

#include "common.h"
#include "clock.h"

void __init pxa168_init_irq(void)
{
	icu_init_irq();
}

/* APB peripheral clocks */
static APBC_CLK(uart1, PXA168_UART1, 1, 14745600);
static APBC_CLK(uart2, PXA168_UART2, 1, 14745600);

/* device and clock bindings */
static struct clk_lookup pxa168_clkregs[] = {
	INIT_CLKREG(&clk_uart1, "pxa2xx-uart.0", NULL),
	INIT_CLKREG(&clk_uart2, "pxa2xx-uart.1", NULL),
};

static int __init pxa168_init(void)
{
	if (cpu_is_pxa168()) {
		pxa_init_dma(IRQ_PXA168_DMA_INT0, 32);
		clks_register(ARRAY_AND_SIZE(pxa168_clkregs));
	}

	return 0;
}
postcore_initcall(pxa168_init);

/* system timer - clock enabled, 3.25MHz */
#define TIMER_CLK_RST	(APBC_APBCLK | APBC_FNCLK | APBC_FNCLKSEL(3))

static void __init pxa168_timer_init(void)
{
	/* this is early, we have to initialize the CCU registers by
	 * ourselves instead of using clk_* API. Clock rate is defined
	 * by APBC_TIMERS_CLK_RST (3.25MHz) and enabled free-running
	 */
	__raw_writel(APBC_APBCLK | APBC_RST, APBC_PXA168_TIMERS);

	/* 3.25MHz, bus/functional clock enabled, release reset */
	__raw_writel(TIMER_CLK_RST, APBC_PXA168_TIMERS);

	timer_init(IRQ_PXA168_TIMER1);
}

struct sys_timer pxa168_timer = {
	.init	= pxa168_timer_init,
};

/* on-chip devices */
PXA168_DEVICE(uart1, "pxa2xx-uart", 0, UART1, 0xd4017000, 0x30, 21, 22);
PXA168_DEVICE(uart2, "pxa2xx-uart", 1, UART2, 0xd4018000, 0x30, 23, 24);

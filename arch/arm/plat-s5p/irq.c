/* arch/arm/plat-s5p/irq.c
 *
 * Copyright (c) 2009 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5P - Interrupt handling
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/hardware/vic.h>

#include <linux/serial_core.h>
#include <mach/map.h>
#include <plat/regs-timer.h>
#include <plat/regs-serial.h>
#include <plat/cpu.h>
#include <plat/irq-vic-timer.h>
#include <plat/irq-uart.h>

/*
 * Note, we make use of the fact that the parent IRQs, IRQ_UART[0..3]
 * are consecutive when looking up the interrupt in the demux routines.
 */
static struct s3c_uart_irq uart_irqs[] = {
	[0] = {
		.regs		= S5P_VA_UART0,
		.base_irq	= IRQ_S5P_UART_BASE0,
		.parent_irq	= IRQ_UART0,
	},
	[1] = {
		.regs		= S5P_VA_UART1,
		.base_irq	= IRQ_S5P_UART_BASE1,
		.parent_irq	= IRQ_UART1,
	},
	[2] = {
		.regs		= S5P_VA_UART2,
		.base_irq	= IRQ_S5P_UART_BASE2,
		.parent_irq	= IRQ_UART2,
	},
#if CONFIG_SERIAL_SAMSUNG_UARTS > 3
	[3] = {
		.regs		= S5P_VA_UART3,
		.base_irq	= IRQ_S5P_UART_BASE3,
		.parent_irq	= IRQ_UART3,
	},
#endif
};

void __init s5p_init_irq(u32 *vic, u32 num_vic)
{
	int irq;

	/* initialize the VICs */
	for (irq = 0; irq < num_vic; irq++)
		vic_init(VA_VIC(irq), VIC_BASE(irq), vic[irq], 0);

	s3c_init_vic_timer_irq(IRQ_TIMER0_VIC, IRQ_TIMER0);
	s3c_init_vic_timer_irq(IRQ_TIMER1_VIC, IRQ_TIMER1);
	s3c_init_vic_timer_irq(IRQ_TIMER2_VIC, IRQ_TIMER2);
	s3c_init_vic_timer_irq(IRQ_TIMER3_VIC, IRQ_TIMER3);
	s3c_init_vic_timer_irq(IRQ_TIMER4_VIC, IRQ_TIMER4);

	s3c_init_uart_irqs(uart_irqs, ARRAY_SIZE(uart_irqs));
}

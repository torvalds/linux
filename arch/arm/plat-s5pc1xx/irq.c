/* arch/arm/plat-s5pc1xx/irq.c
 *
 * Copyright 2009 Samsung Electronics Co.
 *      Byungho Min <bhmin@samsung.com>
 *
 * S5PC1XX - Interrupt handling
 *
 * Based on plat-s3c64xx/irq.c
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

#include <mach/map.h>
#include <plat/irq-vic-timer.h>
#include <plat/irq-uart.h>
#include <plat/cpu.h>

/* Note, we make use of the fact that the parent IRQs, IRQ_UART[0..3]
 * are consecutive when looking up the interrupt in the demux routines.
 */
static struct s3c_uart_irq uart_irqs[] = {
	[0] = {
		.regs		= (void *)S3C_VA_UART0,
		.base_irq	= IRQ_S3CUART_BASE0,
		.parent_irq	= IRQ_UART0,
	},
	[1] = {
		.regs		= (void *)S3C_VA_UART1,
		.base_irq	= IRQ_S3CUART_BASE1,
		.parent_irq	= IRQ_UART1,
	},
	[2] = {
		.regs		= (void *)S3C_VA_UART2,
		.base_irq	= IRQ_S3CUART_BASE2,
		.parent_irq	= IRQ_UART2,
	},
	[3] = {
		.regs		= (void *)S3C_VA_UART3,
		.base_irq	= IRQ_S3CUART_BASE3,
		.parent_irq	= IRQ_UART3,
	},
};

void __init s5pc1xx_init_irq(u32 *vic_valid, int num)
{
	int i;

	printk(KERN_DEBUG "%s: initialising interrupts\n", __func__);

	/* initialise the pair of VICs */
	for (i = 0; i < num; i++)
		vic_init((void *)S5PC1XX_VA_VIC(i), S3C_IRQ(i * S3C_IRQ_OFFSET),
				vic_valid[i], 0);

	/* add the timer sub-irqs */

	s3c_init_vic_timer_irq(IRQ_TIMER0_VIC, IRQ_TIMER0);
	s3c_init_vic_timer_irq(IRQ_TIMER1_VIC, IRQ_TIMER1);
	s3c_init_vic_timer_irq(IRQ_TIMER2_VIC, IRQ_TIMER2);
	s3c_init_vic_timer_irq(IRQ_TIMER3_VIC, IRQ_TIMER3);
	s3c_init_vic_timer_irq(IRQ_TIMER4_VIC, IRQ_TIMER4);

	s3c_init_uart_irqs(uart_irqs, ARRAY_SIZE(uart_irqs));
}



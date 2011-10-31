/* arch/arm/plat-s3c64xx/irq.c
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C64XX - Interrupt handling
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/serial_core.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/hardware/vic.h>

#include <mach/map.h>
#include <plat/irq-vic-timer.h>
#include <plat/irq-uart.h>
#include <plat/cpu.h>

/* setup the sources the vic should advertise resume for, even though it
 * is not doing the wake (set_irq_wake needs to be valid) */
#define IRQ_VIC0_RESUME (1 << (IRQ_RTC_TIC - IRQ_VIC0_BASE))
#define IRQ_VIC1_RESUME (1 << (IRQ_RTC_ALARM - IRQ_VIC1_BASE) |	\
			 1 << (IRQ_PENDN - IRQ_VIC1_BASE) |	\
			 1 << (IRQ_HSMMC0 - IRQ_VIC1_BASE) |	\
			 1 << (IRQ_HSMMC1 - IRQ_VIC1_BASE) |	\
			 1 << (IRQ_HSMMC2 - IRQ_VIC1_BASE))

void __init s3c64xx_init_irq(u32 vic0_valid, u32 vic1_valid)
{
	printk(KERN_DEBUG "%s: initialising interrupts\n", __func__);

	/* initialise the pair of VICs */
	vic_init(VA_VIC0, IRQ_VIC0_BASE, vic0_valid, IRQ_VIC0_RESUME);
	vic_init(VA_VIC1, IRQ_VIC1_BASE, vic1_valid, IRQ_VIC1_RESUME);

	/* add the timer sub-irqs */
	s3c_init_vic_timer_irq(5, IRQ_TIMER0);
}

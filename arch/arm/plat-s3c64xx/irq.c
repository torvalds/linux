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
#include <linux/io.h>

#include <asm/hardware/vic.h>
#include <asm/irq.h>

#include <mach/map.h>
#include <plat/cpu.h>

void __init s3c64xx_init_irq(u32 vic0_valid, u32 vic1_valid)
{
	printk(KERN_INFO "%s: initialising interrupts\n", __func__);

	/* initialise the pair of VICs */
	vic_init(S3C_VA_VIC0, S3C_VIC0_BASE, vic0_valid);
	vic_init(S3C_VA_VIC1, S3C_VIC1_BASE, vic1_valid);
}



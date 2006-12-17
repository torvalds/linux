/* linux/arch/arm/mach-s3c2410/s3c2440.c
 *
 * Copyright (c) 2004-2006 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * Samsung S3C2440 Mobile CPU support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/sysdev.h>
#include <linux/clk.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <asm/arch/regs-clock.h>
#include <asm/arch/regs-serial.h>
#include <asm/arch/regs-gpio.h>
#include <asm/arch/regs-gpioj.h>
#include <asm/arch/regs-dsc.h>

#include "s3c2440.h"
#include "clock.h"
#include "devs.h"
#include "cpu.h"
#include "pm.h"

static struct sys_device s3c2440_sysdev = {
	.cls		= &s3c2440_sysclass,
};

int __init s3c2440_init(void)
{
	printk("S3C2440: Initialising architecture\n");

	/* change irq for watchdog */

	s3c_device_wdt.resource[1].start = IRQ_S3C2440_WDT;
	s3c_device_wdt.resource[1].end   = IRQ_S3C2440_WDT;

	/* register our system device for everything else */

	return sysdev_register(&s3c2440_sysdev);
}

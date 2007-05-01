/* linux/arch/arm/mach-s3c2442/s3c2442.c
 *
 * Copyright (c) 2006 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * Samsung S3C2442 Mobile CPU support
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
#include <linux/serial_core.h>
#include <linux/sysdev.h>

#include <asm/plat-s3c24xx/s3c2442.h>
#include <asm/plat-s3c24xx/cpu.h>

static struct sys_device s3c2442_sysdev = {
	.cls		= &s3c2442_sysclass,
};

int __init s3c2442_init(void)
{
	printk("S3C2442: Initialising architecture\n");

	return sysdev_register(&s3c2442_sysdev);
}

/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * Based on arch/i386/kernel/irq.c
 *   Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sysdev.h>

/* May be overridden by platform code */
int __weak nmi_enable(void)
{
	return -ENOSYS;
}

void __weak nmi_disable(void)
{

}

/*
 * OpenRISC prom.c
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 * Architecture specific procedures for creating, accessing and
 * interpreting the device tree.
 *
 */

#include <stdarg.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/threads.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/stringify.h>
#include <linux/delay.h>
#include <linux/initrd.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/kexec.h>
#include <linux/debugfs.h>
#include <linux/irq.h>
#include <linux/memblock.h>
#include <linux/of_fdt.h>

#include <asm/prom.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <linux/io.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/sections.h>
#include <asm/setup.h>

void __init early_init_devtree(void *params)
{
	early_init_dt_scan(params);
	memblock_allow_resize();
}

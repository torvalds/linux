// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 SoC prom code
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>

#include <linux/serial_reg.h>

#include <asm/bootinfo.h>
#include <asm/fw/fw.h>
#include <asm/mach-jz4740/base.h>

void __init prom_init(void)
{
	fw_init_cmdline();
}

void __init prom_free_prom_memory(void)
{
}

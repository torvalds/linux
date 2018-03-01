/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 SoC prom code
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General	 Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
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

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2011 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * Modified from arch/mips/pnx833x/common/prom.c.
 */

#include <linux/io.h>
#include <linux/init.h>
#include <linux/serial_reg.h>
#include <asm/bootinfo.h>
#include <asm/fw/fw.h>

#include <loongson1.h>

unsigned long memsize;

void __init prom_init(void)
{
	void __iomem *uart_base;

	fw_init_cmdline();

	memsize = fw_getenvl("memsize");
	if(!memsize)
		memsize = DEFAULT_MEMSIZE;

	if (strstr(arcs_cmdline, "console=ttyS3"))
		uart_base = ioremap(LS1X_UART3_BASE, 0x0f);
	else if (strstr(arcs_cmdline, "console=ttyS2"))
		uart_base = ioremap(LS1X_UART2_BASE, 0x0f);
	else if (strstr(arcs_cmdline, "console=ttyS1"))
		uart_base = ioremap(LS1X_UART1_BASE, 0x0f);
	else
		uart_base = ioremap(LS1X_UART0_BASE, 0x0f);
	setup_8250_early_printk_port((unsigned long)uart_base, 0, 0);
}

void __init prom_free_prom_memory(void)
{
}

void __init plat_mem_setup(void)
{
	add_memory_region(0x0, (memsize << 20), BOOT_MEM_RAM);
}

/*
 * linux/arch/mips/tx4938/toshiba_rbtx4938/prom.c
 *
 * rbtx4938 specific prom routines
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for TX4938 in 2.6 - Manish Lachwani (mlachwani@mvista.com)
 */

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/bootmem.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/tx4938/tx4938.h>

void __init prom_init_cmdline(void)
{
	int argc = (int) fw_arg0;
	char **argv = (char **) fw_arg1;
	int i;

	/* ignore all built-in args if any f/w args given */
	if (argc > 1) {
		*arcs_cmdline = '\0';
	}

	for (i = 1; i < argc; i++) {
		if (i != 1) {
			strcat(arcs_cmdline, " ");
		}
		strcat(arcs_cmdline, argv[i]);
	}
}

void __init prom_init(void)
{
	extern int tx4938_get_mem_size(void);
	int msize;
#ifndef CONFIG_TX4938_NAND_BOOT
	prom_init_cmdline();
#endif

	msize = tx4938_get_mem_size();
	add_memory_region(0, msize << 20, BOOT_MEM_RAM);

	return;
}

void __init prom_free_prom_memory(void)
{
}

void __init prom_fixup_mem_map(unsigned long start, unsigned long end)
{
	return;
}

const char *get_system_type(void)
{
	return "Toshiba RBTX4938";
}

char * __init prom_getcmdline(void)
{
	return &(arcs_cmdline[0]);
}

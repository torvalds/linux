/*
 *  STB810 specific prom routines
 *
 *  Author: MontaVista Software, Inc.
 *          source@mvista.com
 *
 *  Copyright 2005 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 */

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/bootmem.h>
#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <linux/string.h>
#include <linux/kernel.h>

int prom_argc;
char **prom_argv, **prom_envp;
extern void  __init prom_init_cmdline(void);
extern char *prom_getenv(char *envname);

const char *get_system_type(void)
{
	return "Philips PNX8550/STB810";
}

void __init prom_init(void)
{
	unsigned long memsize;

	prom_argc = (int) fw_arg0;
	prom_argv = (char **) fw_arg1;
	prom_envp = (char **) fw_arg2;

	prom_init_cmdline();

	memsize = 0x08000000; /* Trimedia uses memory above */
	add_memory_region(0, memsize, BOOT_MEM_RAM);
}

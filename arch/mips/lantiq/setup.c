/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 * Copyright (C) 2010 John Crispin <blogic@openwrt.org>
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <asm/bootinfo.h>

#include <lantiq_soc.h>

#include "prom.h"

void __init plat_mem_setup(void)
{
	/* assume 16M as default incase uboot fails to pass proper ramsize */
	unsigned long memsize = 16;
	char **envp = (char **) KSEG1ADDR(fw_arg2);

	ioport_resource.start = IOPORT_RESOURCE_START;
	ioport_resource.end = IOPORT_RESOURCE_END;
	iomem_resource.start = IOMEM_RESOURCE_START;
	iomem_resource.end = IOMEM_RESOURCE_END;

	set_io_port_base((unsigned long) KSEG1);

	while (*envp) {
		char *e = (char *)KSEG1ADDR(*envp);
		if (!strncmp(e, "memsize=", 8)) {
			e += 8;
			if (strict_strtoul(e, 0, &memsize))
				pr_warn("bad memsize specified\n");
		}
		envp++;
	}
	memsize *= 1024 * 1024;
	add_memory_region(0x00000000, memsize, BOOT_MEM_RAM);
}

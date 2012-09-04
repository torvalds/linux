/*
 * Copyright (c) 2011 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <asm/bootinfo.h>

#include <prom.h>

void __init plat_mem_setup(void)
{
	add_memory_region(0x0, (memsize << 20), BOOT_MEM_RAM);
}

const char *get_system_type(void)
{
	unsigned int processor_id = (&current_cpu_data)->processor_id;

	switch (processor_id & PRID_REV_MASK) {
	case PRID_REV_LOONGSON1B:
		return "LOONGSON LS1B";
	default:
		return "LOONGSON (unknown)";
	}
}

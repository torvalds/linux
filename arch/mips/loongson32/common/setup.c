// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2011 Zhang, Keguang <keguang.zhang@gmail.com>
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
#if defined(CONFIG_LOONGSON1_LS1B)
		return "LOONGSON LS1B";
#elif defined(CONFIG_LOONGSON1_LS1C)
		return "LOONGSON LS1C";
#endif
	default:
		return "LOONGSON (unknown)";
	}
}

// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2011 Zhang, Keguang <keguang.zhang@gmail.com>
 */

#include <linux/io.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <asm/cpu-info.h>
#include <asm/bootinfo.h>

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

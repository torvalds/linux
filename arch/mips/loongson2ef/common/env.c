// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Based on Ocelot Linux port, which is
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * Copyright 2003 ICT CAS
 * Author: Michael Guo <guoyi@ict.ac.cn>
 *
 * Copyright (C) 2007 Lemote Inc. & Institute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 *
 * Copyright (C) 2009 Lemote Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 */
#include <linux/export.h>
#include <asm/bootinfo.h>
#include <asm/fw/fw.h>
#include <loongson.h>

u32 cpu_clock_freq;
EXPORT_SYMBOL(cpu_clock_freq);

void __init prom_init_env(void)
{
	/* pmon passes arguments in 32bit pointers */
	unsigned int processor_id;

	cpu_clock_freq = fw_getenvl("cpuclock");
	memsize = fw_getenvl("memsize");
	highmemsize = fw_getenvl("highmemsize");

	if (memsize == 0)
		memsize = 256;

	pr_info("memsize=%u, highmemsize=%u\n", memsize, highmemsize);

	if (cpu_clock_freq == 0) {
		processor_id = (&current_cpu_data)->processor_id;
		switch (processor_id & PRID_REV_MASK) {
		case PRID_REV_LOONGSON2E:
			cpu_clock_freq = 533080000;
			break;
		case PRID_REV_LOONGSON2F:
			cpu_clock_freq = 797000000;
			break;
		default:
			cpu_clock_freq = 100000000;
			break;
		}
	}
	pr_info("CpuClock = %u\n", cpu_clock_freq);
}

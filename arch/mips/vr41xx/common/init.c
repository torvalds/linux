// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  init.c, Common initialization routines for NEC VR4100 series.
 *
 *  Copyright (C) 2003-2009  Yoichi Yuasa <yuasa@linux-mips.org>
 */
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/string.h>

#include <asm/bootinfo.h>
#include <asm/time.h>
#include <asm/vr41xx/irq.h>
#include <asm/vr41xx/vr41xx.h>

#define IO_MEM_RESOURCE_START	0UL
#define IO_MEM_RESOURCE_END	0x1fffffffUL

static void __init iomem_resource_init(void)
{
	iomem_resource.start = IO_MEM_RESOURCE_START;
	iomem_resource.end = IO_MEM_RESOURCE_END;
}

void __init plat_time_init(void)
{
	unsigned long tclock;

	vr41xx_calculate_clock_frequency();

	tclock = vr41xx_get_tclock_frequency();
	if (current_cpu_data.processor_id == PRID_VR4131_REV2_0 ||
	    current_cpu_data.processor_id == PRID_VR4131_REV2_1)
		mips_hpt_frequency = tclock / 2;
	else
		mips_hpt_frequency = tclock / 4;
}

void __init plat_mem_setup(void)
{
	iomem_resource_init();

	vr41xx_siu_setup();
}

void __init prom_init(void)
{
	int argc, i;
	char **argv;

	argc = fw_arg0;
	argv = (char **)fw_arg1;

	for (i = 1; i < argc; i++) {
		strlcat(arcs_cmdline, argv[i], COMMAND_LINE_SIZE);
		if (i < (argc - 1))
			strlcat(arcs_cmdline, " ", COMMAND_LINE_SIZE);
	}
}

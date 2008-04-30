/*
 *  init.c, Common initialization routines for NEC VR4100 series.
 *
 *  Copyright (C) 2003-2008  Yoichi Yuasa <yoichi_yuasa@tripeaks.co.jp>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
		strcat(arcs_cmdline, argv[i]);
		if (i < (argc - 1))
			strcat(arcs_cmdline, " ");
	}
}

void __init prom_free_prom_memory(void)
{
}

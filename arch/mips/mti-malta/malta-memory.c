/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * PROM library functions for acquiring/using memory descriptors given to
 * us from the YAMON.
 *
 * Copyright (C) 1999,2000,2012  MIPS Technologies, Inc.
 * All rights reserved.
 * Authors: Carsten Langgaard <carstenl@mips.com>
 *          Steven J. Hill <sjhill@mips.com>
 */
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/string.h>

#include <asm/bootinfo.h>
#include <asm/cdmm.h>
#include <asm/maar.h>
#include <asm/sections.h>
#include <asm/fw/fw.h>

/* determined physical memory size, not overridden by command line args	 */
unsigned long physical_memsize = 0L;

static void free_init_pages_eva_malta(void *begin, void *end)
{
	free_init_pages("unused kernel", __pa_symbol((unsigned long *)begin),
			__pa_symbol((unsigned long *)end));
}

void __init fw_meminit(void)
{
	bool eva = IS_ENABLED(CONFIG_EVA);

	free_init_pages_eva = eva ? free_init_pages_eva_malta : NULL;
}

phys_addr_t mips_cdmm_phys_base(void)
{
	/* This address is "typically unused" */
	return 0x1fc10000;
}

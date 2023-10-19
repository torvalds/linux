// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2010 Ben. Herrenschmidt, IBM Corporation.
 *
 * Based on earlier code:
 *   Copyright (C) Paul Mackerras 1997.
 *
 *   Matt Porter <mporter@kernel.crashing.org>
 *   Copyright 2002-2005 MontaVista Software Inc.
 *
 *   Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 *   Copyright (c) 2003, 2004 Zultys Technologies
 *
 *    Copyright 2007 David Gibson, IBM Corporation.
 */
#include <stdarg.h>
#include <stddef.h>
#include "types.h"
#include "elf.h"
#include "string.h"
#include "stdio.h"
#include "page.h"
#include "ops.h"
#include "reg.h"
#include "io.h"
#include "dcr.h"
#include "4xx.h"
#include "44x.h"
#include "libfdt.h"

BSS_STACK(4096);

static u32 ibm4xx_memstart;

static void iss_4xx_fixups(void)
{
	void *memory;
	u32 reg[3];

	memory = finddevice("/memory");
	if (!memory)
		fatal("Can't find memory node\n");
	/* This assumes #address-cells = 2, #size-cells =1 and that */
	getprop(memory, "reg", reg, sizeof(reg));
	if (reg[2])
		/* If the device tree specifies the memory range, use it */
		ibm4xx_memstart = reg[1];
	else
		/* othersize, read it from the SDRAM controller */
		ibm4xx_sdram_fixup_memsize();
}

static void *iss_4xx_vmlinux_alloc(unsigned long size)
{
	return (void *)ibm4xx_memstart;
}

#define SPRN_PIR	0x11E	/* Processor Identification Register */
void platform_init(void)
{
	unsigned long end_of_ram = 0x08000000;
	unsigned long avail_ram = end_of_ram - (unsigned long)_end;
	u32 pir_reg;

	simple_alloc_init(_end, avail_ram, 128, 64);
	platform_ops.fixups = iss_4xx_fixups;
	platform_ops.vmlinux_alloc = iss_4xx_vmlinux_alloc;
	platform_ops.exit = ibm44x_dbcr_reset;
	pir_reg = mfspr(SPRN_PIR);
	fdt_set_boot_cpuid_phys(_dtb_start, pir_reg);
	fdt_init(_dtb_start);
	serial_console_init();
}

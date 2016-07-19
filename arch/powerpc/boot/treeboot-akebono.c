/*
 * Copyright © 2013 Tony Breeds IBM Corporation
 * Copyright © 2013 Alistair Popple IBM Corporation
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
 *    Copyright 2010 Ben. Herrenschmidt, IBM Corporation.
 *    Copyright © 2011 David Kleikamp IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <stdarg.h>
#include <stddef.h>
#include "types.h"
#include "elf.h"
#include "string.h"
#include "stdlib.h"
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

#define SPRN_PIR	0x11E	/* Processor Identification Register */
#define USERDATA_LEN	256	/* Length of userdata passed in by PIBS */
#define MAX_RANKS	0x4
#define DDR3_MR0CF	0x80010011U
#define CCTL0_MCO2	0x8000080FU
#define CCTL0_MCO3	0x80000810U
#define CCTL0_MCO4	0x80000811U
#define CCTL0_MCO5	0x80000812U
#define CCTL0_MCO6	0x80000813U

static unsigned long long ibm_akebono_memsize;
static long long unsigned mac_addr;

static unsigned long long ibm_akebono_detect_memsize(void)
{
	u32 reg;
	unsigned i;
	unsigned long long memsize = 0;

	for (i = 0; i < MAX_RANKS; i++) {
		reg = mfdcrx(DDR3_MR0CF + i);

		if (!(reg & 1))
			continue;

		reg &= 0x0000f000;
		reg >>= 12;
		memsize += (0x800000ULL << reg);
	}

	return memsize;
}

static void ibm_akebono_fixups(void)
{
	void *emac;
	u32 reg;

	dt_fixup_memory(0x0ULL,  ibm_akebono_memsize);

	/* Fixup the SD timeout frequency */
	mtdcrx(CCTL0_MCO4, 0x1);

	/* Disable SD high-speed mode (which seems to be broken) */
	reg = mfdcrx(CCTL0_MCO2) & ~0x2;
	mtdcrx(CCTL0_MCO2, reg);

	/* Set the MAC address */
	emac = finddevice("/plb/opb/ethernet");
	if (emac > 0) {
		if (mac_addr)
			setprop(emac, "local-mac-address",
				((u8 *) &mac_addr) + 2 , 6);
	}
}

void platform_init(char *userdata)
{
	unsigned long end_of_ram, avail_ram;
	u32 pir_reg;
	int node, size;
	const u32 *timebase;
	int len, i, userdata_len;
	char *end;

	userdata[USERDATA_LEN - 1] = '\0';
	userdata_len = strlen(userdata);
	for (i = 0; i < userdata_len - 15; i++) {
		if (strncmp(&userdata[i], "local-mac-addr=", 15) == 0) {
			if (i > 0 && userdata[i - 1] != ' ') {
				/* We've only found a substring ending
				 * with local-mac-addr so this isn't
				 * our mac address. */
				continue;
			}

			mac_addr = strtoull(&userdata[i + 15], &end, 16);

			/* Remove the "local-mac-addr=<...>" from the kernel
			 * command line, including the tailing space if
			 * present. */
			if (*end == ' ')
				end++;

			len = ((int) end) - ((int) &userdata[i]);
			memmove(&userdata[i], end,
				userdata_len - (len + i) + 1);
			break;
		}
	}

	loader_info.cmdline = userdata;
	loader_info.cmdline_len = 256;

	ibm_akebono_memsize = ibm_akebono_detect_memsize();
	if (ibm_akebono_memsize >> 32)
		end_of_ram = ~0UL;
	else
		end_of_ram = ibm_akebono_memsize;
	avail_ram = end_of_ram - (unsigned long)_end;

	simple_alloc_init(_end, avail_ram, 128, 64);
	platform_ops.fixups = ibm_akebono_fixups;
	platform_ops.exit = ibm44x_dbcr_reset;
	pir_reg = mfspr(SPRN_PIR);

	/* Make sure FDT blob is sane */
	if (fdt_check_header(_dtb_start) != 0)
		fatal("Invalid device tree blob\n");

	node = fdt_node_offset_by_prop_value(_dtb_start, -1, "device_type",
					     "cpu", sizeof("cpu"));
	if (!node)
		fatal("Cannot find cpu node\n");
	timebase = fdt_getprop(_dtb_start, node, "timebase-frequency", &size);
	if (timebase && (size == 4))
		timebase_period_ns = 1000000000 / *timebase;

	fdt_set_boot_cpuid_phys(_dtb_start, pir_reg);
	fdt_init(_dtb_start);

	serial_console_init();
}

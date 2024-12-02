// SPDX-License-Identifier: GPL-2.0-only
/*
 * Old U-boot compatibility for Katmai
 *
 * Author: Hugh Blemings <hugh@au.ibm.com>
 *
 * Copyright 2007 Hugh Blemings, IBM Corporation.
 *   Based on cuboot-ebony.c which is:
 * Copyright 2007 David Gibson, IBM Corporation.
 *   Based on cuboot-83xx.c, which is:
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 */

#include "ops.h"
#include "stdio.h"
#include "reg.h"
#include "dcr.h"
#include "4xx.h"
#include "44x.h"
#include "cuboot.h"

#define TARGET_4xx
#define TARGET_44x
#include "ppcboot.h"

static bd_t bd;

BSS_STACK(4096);

static void katmai_fixups(void)
{
	unsigned long sysclk = 33333000;

	/* 440SP Clock logic is all but identical to 440GX
	 * so we just use that code for now at least
	 */
	ibm440spe_fixup_clocks(sysclk, 6 * 1843200, 0);

	ibm440spe_fixup_memsize();

	dt_fixup_mac_address(0, bd.bi_enetaddr);

	ibm4xx_fixup_ebc_ranges("/plb/opb/ebc");
}

void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
		   unsigned long r6, unsigned long r7)
{
	CUBOOT_INIT();

	platform_ops.fixups = katmai_fixups;
	fdt_init(_dtb_start);
	serial_console_init();
}

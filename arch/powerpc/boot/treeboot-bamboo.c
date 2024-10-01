// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright IBM Corporation, 2007
 * Josh Boyer <jwboyer@linux.vnet.ibm.com>
 *
 * Based on ebony wrapper:
 * Copyright 2007 David Gibson, IBM Corporation.
 */
#include "ops.h"
#include "stdio.h"
#include "44x.h"
#include "stdlib.h"

BSS_STACK(4096);

#define PIBS_MAC0 0xfffc0400
#define PIBS_MAC1 0xfffc0500
char pibs_mac0[6];
char pibs_mac1[6];

static void read_pibs_mac(void)
{
	unsigned long long mac64;

	mac64 = strtoull((char *)PIBS_MAC0, 0, 16);
	memcpy(&pibs_mac0, (char *)&mac64+2, 6);

	mac64 = strtoull((char *)PIBS_MAC1, 0, 16);
	memcpy(&pibs_mac1, (char *)&mac64+2, 6);
}

void platform_init(void)
{
	unsigned long end_of_ram = 0x8000000;
	unsigned long avail_ram = end_of_ram - (unsigned long)_end;

	simple_alloc_init(_end, avail_ram, 32, 64);
	read_pibs_mac();
	bamboo_init((u8 *)&pibs_mac0, (u8 *)&pibs_mac1);
}

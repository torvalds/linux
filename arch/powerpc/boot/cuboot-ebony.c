// SPDX-License-Identifier: GPL-2.0-only
/*
 * Old U-boot compatibility for Ebony
 *
 * Author: David Gibson <david@gibson.dropbear.id.au>
 *
 * Copyright 2007 David Gibson, IBM Corporatio.
 *   Based on cuboot-83xx.c, which is:
 * Copyright (c) 2007 Freescale Semiconductor, Inc.
 */

#include "ops.h"
#include "stdio.h"
#include "44x.h"
#include "cuboot.h"

#define TARGET_4xx
#define TARGET_44x
#include "ppcboot.h"

static bd_t bd;

void platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
                   unsigned long r6, unsigned long r7)
{
	CUBOOT_INIT();
	ebony_init(&bd.bi_enetaddr, &bd.bi_enet1addr);
}

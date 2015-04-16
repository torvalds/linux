/*
 * SMP boot code for secondary CPUs
 * Based on arch/arm/mach-tegra/headsmp.S
 *
 * Copyright (C) 2010 NVIDIA, Inc.
 * Copyright (C) 2013-2014 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <asm/assembler.h>
#include <linux/linkage.h>
#include <linux/init.h>

        .section ".text.head", "ax"

ENTRY(brcmstb_secondary_startup)
        /*
         * Ensure CPU is in a sane state by disabling all IRQs and switching
         * into SVC mode.
         */
        setmode	PSR_I_BIT | PSR_F_BIT | SVC_MODE, r0

        bl      v7_invalidate_l1
        b       secondary_startup
ENDPROC(brcmstb_secondary_startup)

/*
 * arch/arm/mach-tegra/delay.S
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *  Colin Cross <ccross@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <mach/iomap.h>
#include <mach/io.h>
#include "power-macros.S"

    .text

ENTRY(__udelay)
ENTRY(__const_udelay)
    mov32 r3, (IO_PPSB_VIRT + TEGRA_TMRUS_BASE - IO_PPSB_PHYS)
    ldr r1, [r3]

/* r0 - usecs to wait
 * r1 - initial value of the counter
 */
loop:
    ldr r2, [r3]
    sub r2, r2, r1
    cmp r2, r0
    bls loop
    mov pc, lr
ENDPROC(__const_udelay)
ENDPROC(__udelay)


@ Delay routine
ENTRY(__delay)
    subs  r0, r0, #1
    bhi __delay
    mov pc, lr
ENDPROC(__delay)

/*
 * arch/arm/plat-sunxi/include/plat/entry-macro.S
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Benn Huang <benn@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <mach/io.h>
#include <mach/hardware.h>
#include <mach/platform.h>
#include <asm/hardware/vic.h>

	.macro getioaddr, rp, rv
	ldr	\rp, =SW_IO_PHYS
	ldr	\rv, =SW_IO_VIRT
	.endm

        .macro  disable_fiq
        .endm

        .macro  get_irqnr_preamble, base, tmp
        ldr \base, =SW_VA_INT_IO_BASE
        .endm

        .macro  arch_ret_to_user, tmp1, tmp2
        .endm

        .macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
        ldr \irqstat, [\base, #0x10]    @ get pending status
        mov \irqnr, #0
        teq \irqstat, #0
        beq 1003f

1001:   tst \irqstat, #15
        bne 1002f
        add \irqnr, \irqnr, #4
        movs    \irqstat, \irqstat, lsr #4
        bne 1001b
1002:   tst \irqstat, #1
        bne 5000f
        add \irqnr, \irqnr, #1
        movs    \irqstat, \irqstat, lsr #1
        bne 1002b
1003:   /* EQ will be set if no irqs pending */

        ldr \irqstat, [\base, #0x14]    @ get pending status
        teq \irqstat, #0
        beq 3000f

        mov \irqnr, #32
2001:       tst \irqstat, #15
        bne 2002f
        add \irqnr, \irqnr, #4
        movs    \irqstat, \irqstat, lsr #4
        bne 2001b
2002:       tst \irqstat, #1
        bne 5000f
        add \irqnr, \irqnr, #1
        movs    \irqstat, \irqstat, lsr #1
        bne 2002b

3000:   /* EQ will be set if no irqs pending */

        ldr \irqstat, [\base, #0x18]    @ get pending status
        teq \irqstat, #0
        beq 5000f

        mov \irqnr, #64
4001:       tst \irqstat, #15
        bne 4002f
        add \irqnr, \irqnr, #4
        movs    \irqstat, \irqstat, lsr #4
        bne 4001b
4002:       tst \irqstat, #1
        bne 5000f
        add \irqnr, \irqnr, #1
        movs    \irqstat, \irqstat, lsr #1
        bne 4002b

5000:
        .endm


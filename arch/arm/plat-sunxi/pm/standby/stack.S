/*
 * arch/arm/plat-sunxi/pm/standby/stack.S
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Kevin Zhang <kevin@allwinnertech.com>
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

#include <mach/platform.h>

/* stack point address in sram */
#define SP_IN_SRAM	0xf0007ffc

    .text
    .globl save_sp
save_sp:
    mov r0, r13
    ldr  r13, =SP_IN_SRAM
    mov pc,lr

    .text
    .globl restore_sp
restore_sp:
    mov r13, r0
    mov pc,lr


    .text
    .globl standby_flush_tlb
standby_flush_tlb:
    push    {r0-r3}
    /* invalid un-locked data and instruction tlb */
    mcr p15, 0, r0, c8, c7, 0
    pop     {r0-r3}
    mov pc,lr

    .text
    .globl standby_preload_tlb
standby_preload_tlb:
    push    {r0-r3}
    /* preload data tlb for standby io operation */
    ldr r0, =SW_VA_CCM_IO_BASE
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =SW_VA_TWI0_IO_BASE
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =SW_VA_TWI1_IO_BASE
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =SW_VA_TWI2_IO_BASE
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =SW_VA_INT_IO_BASE
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =SW_VA_TIMERC_IO_BASE
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =SW_VA_LRADC_IO_BASE
    mcr p15, 0, r0, c10, c1, 0
    /* preload data tlb for standby code&data area */
    ldr r0, =0xf0000000
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =0xf0000400
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =0xf0000800
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =0xf0000c00
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =0xf0001000
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =0xf0001400
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =0xf0001800
    mcr p15, 0, r0, c10, c1, 0
    ldr r0, =0xf0001c00
    mcr p15, 0, r0, c10, c1, 0

    /* preload instruction tlb for standby code&data area */
    ldr r0, =0xf0000000
    mcr p15, 0, r0, c10, c1, 1
    ldr r0, =0xf0000400
    mcr p15, 0, r0, c10, c1, 1
    ldr r0, =0xf0000800
    mcr p15, 0, r0, c10, c1, 1
    ldr r0, =0xf0000c00
    mcr p15, 0, r0, c10, c1, 1
    ldr r0, =0xf0001000
    mcr p15, 0, r0, c10, c1, 1
    ldr r0, =0xf0001400
    mcr p15, 0, r0, c10, c1, 1
    ldr r0, =0xf0001800
    mcr p15, 0, r0, c10, c1, 1
    ldr r0, =0xf0001c00
    mcr p15, 0, r0, c10, c1, 1

    pop     {r0-r3}
    mov pc,lr


/* arch/arm/mach-rk2818/include/mach/entry-macro.S
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
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

#include "irqs.h"

    .macro  disable_fiq
    .endm

    .macro  get_irqnr_preamble, base, tmp
	    ldr		\base,=0xff0aa000	
    .endm

    .macro  arch_ret_to_user, tmp1, tmp2
    .endm

    .macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
1001:    
        ldr     \irqstat, [\base, #IRQ_REG_FINALSTATUS_L]
        cmp     \irqstat, #0
        beq     1002f
        
        clz     \irqnr, \irqstat
        rsb     \irqnr, \irqnr, #31
        b       1003f

1002:
        ldr     \irqstat, [\base, #IRQ_REG_FINALSTATUS_H]
        lsr     \tmp, \irqstat, #8
        and     \tmp, \tmp, #0xff
        lsl     \tmp, \tmp, #8
        and     \irqstat, \irqstat, #0xff
        orr     \irqstat, \irqstat, \tmp
        
        cmp     \irqstat, #0
        beq     1003f
        
        clz     \irqnr, \irqstat
        rsb     \irqnr, \irqnr, #31
        add     \irqnr, \irqnr, #32
1003:
    .endm

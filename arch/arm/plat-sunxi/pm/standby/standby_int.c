/*
 * arch/arm/plat-sunxi/pm/standby/standby_int.c
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

#include "standby_i.h"

static __u32    IrqEnReg[3], IrqMaskReg[3], IrqSelReg[3];
static struct standby_int_reg_t  *IntcReg;




/*
*********************************************************************************************************
*                                       STANDBY INTERRUPT INITIALISE
*
* Description: standby interrupt initialise.
*
* Arguments  : none.
*
* Returns    : 0/-1;
*********************************************************************************************************
*/
__s32 standby_int_init(void)
{
    IntcReg = (struct standby_int_reg_t *)SW_VA_INT_IO_BASE;

    /* save interrupt controller registers */
    IrqEnReg[0] = IntcReg->IrqEn[0];
    IrqEnReg[1] = IntcReg->IrqEn[1];
    IrqEnReg[2] = IntcReg->IrqEn[2];
    IrqMaskReg[0] = IntcReg->IrqMask[0];
    IrqMaskReg[1] = IntcReg->IrqMask[1];
    IrqMaskReg[2] = IntcReg->IrqMask[2];
    IrqSelReg[0] = IntcReg->TypeSel[0];
    IrqSelReg[1] = IntcReg->TypeSel[1];
    IrqSelReg[2] = IntcReg->TypeSel[2];

    /* initialise interrupt enable and mask for standby */
    IntcReg->IrqEn[0] = 0;
    IntcReg->IrqEn[1] = 0;
    IntcReg->IrqEn[2] = 0;
    IntcReg->IrqMask[0] = 0xffffffff;
    IntcReg->IrqMask[1] = 0xffffffff;
    IntcReg->IrqMask[2] = 0xffffffff;
    IntcReg->TypeSel[0] = 0;
    IntcReg->TypeSel[1] = 0;
    IntcReg->TypeSel[2] = 0;

    /* clear external irq pending */
    IntcReg->IrqPend[0] = 1;

    return 0;
}


/*
*********************************************************************************************************
*                                       STANDBY INTERRUPT INITIALISE
*
* Description: standby interrupt initialise.
*
* Arguments  : none.
*
* Returns    : 0/-1;
*********************************************************************************************************
*/
__s32 standby_int_exit(void)
{
    /* restore interrupt registers */
    IntcReg->IrqEn[0] = IrqEnReg[0];
    IntcReg->IrqEn[1] = IrqEnReg[1];
    IntcReg->IrqEn[2] = IrqEnReg[2];
    IntcReg->IrqMask[0] = IrqMaskReg[0];
    IntcReg->IrqMask[1] = IrqMaskReg[1];
    IntcReg->IrqMask[2] = IrqMaskReg[2];
    IntcReg->TypeSel[0] = IrqSelReg[0];
    IntcReg->TypeSel[1] = IrqSelReg[1];
    IntcReg->TypeSel[2] = IrqSelReg[2];

    return 0;
}


/*
*********************************************************************************************************
*                                       QUERY INTERRUPT
*
* Description: query interrupt.
*
* Arguments  : src  interrupt source number.
*
* Returns    : 0/-1;
*********************************************************************************************************
*/
__s32 standby_enable_int(enum interrupt_source_e src)
{
    __u32   tmpGrp = (__u32)src >> 5;
    __u32   tmpSrc = (__u32)src & 0x1f;

    //enable interrupt source
    IntcReg->IrqEn[tmpGrp] |=  (1<<tmpSrc);
    IntcReg->IrqMask[tmpGrp] &= ~(1<<tmpSrc);

    return 0;
}


/*
*********************************************************************************************************
*                                       QUERY INTERRUPT
*
* Description: query interrupt.
*
* Arguments  : src  interrupt source number.
*
* Returns    : 0/-1;
*********************************************************************************************************
*/
__s32 standby_query_int(enum interrupt_source_e src)
{
    __s32   result = 0;
    __u32   tmpGrp = (__u32)src >> 5;
    __u32   tmpSrc = (__u32)src & 0x1f;

    result = IntcReg->IrqPend[tmpGrp] & (1<<tmpSrc);

    return result? 0:-1;
}


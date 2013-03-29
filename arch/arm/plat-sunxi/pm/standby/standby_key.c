/*
 * arch/arm/plat-sunxi/pm/standby/standby_key.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Kevin Zhang <kevin@allwinnertech.com>
 *
 * Querry key to wake up system from standby
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

static __standby_key_reg_t  *KeyReg;
static __u32 KeyCtrl, KeyIntc, KeyInts, KeyData0, KeyData1;

/*
*********************************************************************************************************
*                                     INIT KEY FOR STANDBY
*
* Description: init key for standby.
*
* Arguments  : none
*
* Returns    : EPDK_OK;
*********************************************************************************************************
*/
__s32 standby_key_init(void)
{
    /* set key register base */
    KeyReg = (__standby_key_reg_t *)SW_VA_LRADC_IO_BASE;

    /* backup LRADC registers */
    KeyCtrl = KeyReg->Lradc_Ctrl;
    KeyIntc = KeyReg->Lradc_Intc;
    KeyReg->Lradc_Ctrl = 0;
    standby_mdelay(10);
    KeyReg->Lradc_Ctrl = (0x1<<6)|(0x1<<0);
    KeyReg->Lradc_Intc = (0x1<<1);
    KeyReg->Lradc_Ints = (0x1<<1);

    return 0;
}


/*
*********************************************************************************************************
*                                     EXIT KEY FOR STANDBY
*
* Description: exit key for standby.
*
* Arguments  : none
*
* Returns    : EPDK_OK;
*********************************************************************************************************
*/
__s32 standby_key_exit(void)
{
    KeyReg->Lradc_Ctrl =  KeyCtrl;
    KeyReg->Lradc_Intc =  KeyIntc;
    return 0;
}
/*
*********************************************************************************************************
*                                     QUERY KEY FOR WAKEUP STANDBY
*
* Description: query key for wakeup standby.
*
* Arguments  : none
*
* Returns    : result;
*               EPDK_TRUE,      get a key;
*               EPDK_FALSE,     no key;
*********************************************************************************************************
*/
__s32 standby_query_key(void)
{
    if(KeyReg->Lradc_Ints & 0x2)
    {
        KeyReg->Lradc_Ints = 0x2;
        return 0;
    }
    return -1;
}


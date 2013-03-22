/*
 * arch/arm/mach-sun4i/clock/ccmu/ccm.c
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
#include <mach/clock.h>
#include "ccm_i.h"



__ccmu_reg_list_t   *aw_ccu_reg;


/*
*********************************************************************************************************
*                           aw_ccu_init
*
*Description: initialise clock mangement unit;
*
*Arguments  : none
*
*Return     : result,
*               AW_CCMU_OK,     initialise ccu successed;
*               AW_CCMU_FAIL,   initialise ccu failed;
*
*Notes      :
*
*********************************************************************************************************
*/
__s32 aw_ccu_init(void)
{
    /* initialise the CCU io base */
    aw_ccu_reg = (__ccmu_reg_list_t *)SW_VA_CCM_IO_BASE;

    /* config the CCU to default status */
    if (SUNXI_VER_A10C == sw_get_ic_ver()) {
        /* switch PLL4 to PLL6 */
        #if(USE_PLL6M_REPLACE_PLL4)
        aw_ccu_reg->Pll4Ctl.PllSwitch = 1;
        #else
        aw_ccu_reg->Pll4Ctl.PllSwitch = 0;
        #endif
    }

    return AW_CCU_ERR_NONE;
}


/*
*********************************************************************************************************
*                           aw_ccu_exit
*
*Description: exit clock managment unit;
*
*Arguments  : none
*
*Return     : result,
*               AW_CCMU_OK,     exit ccu successed;
*               AW_CCMU_FAIL,   exit ccu failed;
*
*Notes      :
*
*********************************************************************************************************
*/
__s32 aw_ccu_exit(void)
{
    return AW_CCU_ERR_NONE;
}


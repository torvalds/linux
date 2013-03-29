/*
 * arch/arm/plat-sunxi/pm/standby/standby_ir.c
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

#include  "standby_i.h"



/*
*********************************************************************************************************
*                           INIT IR FOR STANDBY
*
*Description: init ir for standby;
*
*Arguments  : none
*
*Return     : result;
*               EPDK_OK,    init ir successed;
*               EPDK_FAIL,  init ir failed;
*********************************************************************************************************
*/
__s32  standby_ir_init(void)
{
    return 0;
}


/*
*********************************************************************************************************
*                           EXIT IR FOR STANDBY
*
*Description: exit ir for standby;
*
*Arguments  : none;
*
*Return     : result.
*               EPDK_OK,    exit ir successed;
*               EPDK_FAIL,  exit ir failed;
*********************************************************************************************************
*/
__s32 standby_ir_exit(void)
{
    return 0;
}


/*
*********************************************************************************************************
*                           DETECT IR FOR STANDBY
*
*Description: detect ir for standby;
*
*Arguments  : none
*
*Return     : result;
*               EPDK_OK,    receive some signal;
*               EPDK_FAIL,  no signal;
*********************************************************************************************************
*/
__s32 standby_ir_detect(void)
{
    return 0;
}

/*
*********************************************************************************************************
*                           VERIFY IR SIGNAL FOR STANDBY
*
*Description: verify ir signal for standby;
*
*Arguments  : none
*
*Return     : result;
*               EPDK_OK,    valid ir signal;
*               EPDK_FAIL,  invalid ir signal;
*********************************************************************************************************
*/
__s32 standby_ir_verify(void)
{
    return -1;
}


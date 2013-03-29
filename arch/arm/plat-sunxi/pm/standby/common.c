/*
 * arch/arm/plat-sunxi/pm/standby/common.c
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


/*
*********************************************************************************************************
*                           standby_memcpy
*
*Description: memory copy function for standby.
*
*Arguments  :
*
*Return     :
*
*Notes      :
*
*********************************************************************************************************
*/
void standby_memcpy(void *dest, void *src, int n)
{
    char    *tmp_src = (char *)src;
    char    *tmp_dst = (char *)dest;

    if(!dest || !src){
        /* parameter is invalid */
        return;
    }

    for( ; n > 0; n--){
        *tmp_dst ++ = *tmp_src ++;
    }

    return;
}


/*
*********************************************************************************************************
*                           mdelay
*
*Description: mdelay function
*
*Arguments  :
*
*Return     :
*
*Notes      :
*
*********************************************************************************************************
*/
void standby_mdelay(int ms)
{
    standby_delay(ms * cpu_ms_loopcnt);
}


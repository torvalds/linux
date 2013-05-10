/*
 * arch/arm/plat-sunxi/pm/standby/standby_usb.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Kevin Zhang <kevin@allwinnertech.com>
 *
 * chech usb to wake up system from standby
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
*                                     standby_usb_init
*
* Description: init usb for standby.
*
* Arguments  : none;
*
* Returns    : none;
*********************************************************************************************************
*/
__s32 standby_usb_init(void)
{
    return 0;
}


/*
*********************************************************************************************************
*                                     standby_usb_exit
*
* Description: exit usb for standby.
*
* Arguments  : none;
*
* Returns    : none;
*********************************************************************************************************
*/
__s32 standby_usb_exit(void)
{
    return 0;
}


/*
*********************************************************************************************************
*                           standby_is_usb_status_change
*
*Description: check if usb status is change.
*
*Arguments  : port  usb port number;
*
*Return     : result, 0 status not change, !0 status changed;
*
*Notes      :
*
*********************************************************************************************************
*/
__s32 standby_is_usb_status_change(__u32 port)
{
    return 0;
}


/*
*********************************************************************************************************
*                                     standby_query_usb_event
*
* Description: query usb event for wakeup system from standby.
*
* Arguments  : none;
*
* Returns    : result;
*               EPDK_TRUE,  some usb event happenned;
*               EPDK_FALSE, none usb event;
*********************************************************************************************************
*/
__s32 standby_query_usb_event(void)
{
    return -1;
}

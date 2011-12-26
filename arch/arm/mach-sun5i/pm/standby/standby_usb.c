/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : standby_usb.c
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-31 15:18
* Descript:
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#include "standby_i.h"



//==============================================================================
// USB CHECK FOR WAKEUP SYSTEM FROM STANDBY
//==============================================================================


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



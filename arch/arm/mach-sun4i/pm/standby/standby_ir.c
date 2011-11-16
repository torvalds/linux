/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : standby_ir.c
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-31 14:36
* Descript:
* Update  : date                auther      ver     notes
*********************************************************************************************************
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


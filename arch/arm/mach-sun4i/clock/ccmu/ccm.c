/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : ccm.c
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-13 18:42
* Descript:
* Update  : date                auther      ver     notes
*********************************************************************************************************
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
    if(MAGIC_VER_C == sw_get_ic_ver()) {
        /* switch PLL4 to PLL6 */
        #if(USE_PLL6M_REPLACE_PLL4)
        aw_ccu_reg->VeClk.PllSwitch = 1;
        #else
        aw_ccu_reg->VeClk.PllSwitch = 0;
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

